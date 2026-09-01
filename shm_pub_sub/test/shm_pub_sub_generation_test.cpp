//! @file shm_pub_sub_generation_test.cpp
//! @brief レイアウト世代セグメントの回帰テスト (R01-F01)
//!
//! F01 の TOCTOU は「稼働中のセグメントを ftruncate して作り直す」ことに由来した。
//! 形式 v3 では既存セグメントに一切触れず、新しい世代を別セグメントとして作る。
//! ここでは次を検証する:
//!   - レイアウトを変えても既存セグメントが書き換わらないこと
//!   - 古い世代を掴んだままのプロセスが範囲外アクセスをしないこと
//!   - 容量が「増やすだけ」で運用され、長さが揺れても世代が回らないこと
//!   - 要求が食い違う複数 Publisher が有限回で収束すること

#include <gtest/gtest.h>
#include <dirent.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace
{
struct Msg
{
  uint32_t value;
  uint32_t pad[7];
};

Msg makeMsg(uint32_t v)
{
  Msg m{};
  m.value = v;
  for (auto &p : m.pad)
  {
    p = v;
  }
  return m;
}

//! "shm_<prefix>..." に前方一致する /dev/shm のエントリ数
size_t countSegmentsWithPrefix(const std::string &prefix)
{
  const std::string full  = "shm_" + prefix;
  size_t            count = 0;
  DIR              *dir   = opendir("/dev/shm");
  if (dir == nullptr)
  {
    return 0;
  }
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr)
  {
    if (std::string(entry->d_name).compare(0, full.size(), full) == 0)
    {
      ++count;
    }
  }
  closedir(dir);
  return count;
}

bool segmentExists(const std::string &shm_name)
{
  struct stat st;
  return ::stat(("/dev/shm/shm_" + shm_name).c_str(), &st) == 0;
}

//! root の世代タグ（世代 + ノンス）
uint64_t latestGenerationTag(const std::string &topic)
{
  SharedMemoryPosix shm(topic, O_RDWR, DEFAULT_PERM);
  if (!shm.connect())
  {
    return 0;
  }
  const ShmHeader *h = reinterpret_cast<const ShmHeader *>(shm.getPtr());
  return h->latest_generation.load();
}

uint64_t latestGeneration(const std::string &topic)
{
  return unpackGeneration(latestGenerationTag(topic));
}

//! 世代セグメントの名前はノンスを含むので、名前を組み立てるのではなく
//! root のタグから引く（R03-F03）
std::string generationSegmentName(const std::string &topic)
{
  return ShmTopic::generationName(topic, latestGenerationTag(topic));
}
}  // namespace

class SHMGenerationTest : public ::testing::Test
{
protected:
  void SetUp() override { cleanupAll(); }
  void TearDown() override { cleanupAll(); }
  void cleanupAll()
  {
    for (const char *t : { "gen_grow", "gen_keep", "gen_stale", "gen_converge", "gen_remove", "gen_hyst" })
    {
      try
      {
        disconnectTopic(t);
      }
      catch (...)
      {
      }
    }
  }
};

// ---------------------------------------------------------------------------
// 世代の生成と、既存セグメントの不変性
// ---------------------------------------------------------------------------

TEST_F(SHMGenerationTest, ScalarTopicNeverCreatesASecondGeneration)
{
  // 固定長の型は容量が変わらないので、世代 1 のまま動き続けるはず。
  // 余計なセグメントが増えないことを確認する。
  Publisher<Msg>  pub("gen_keep", 3);
  Subscriber<Msg> sub("gen_keep");
  for (uint32_t i = 1; i <= 50; ++i)
  {
    pub.publish(makeMsg(i));
    bool ok = false;
    const Msg &m = sub.subscribe(&ok);
    ASSERT_TRUE(ok) << "i=" << i;
    EXPECT_EQ(m.value, i);
  }
  EXPECT_EQ(latestGeneration("gen_keep"), 1u);
  EXPECT_EQ(countSegmentsWithPrefix("gen_keep#"), 0u) << "固定長トピックで世代が増えた";
}

TEST_F(SHMGenerationTest, GrowingVectorCreatesANewGenerationWithoutTouchingTheOldOne)
{
  Publisher<std::vector<uint32_t>>  pub("gen_grow", 3);
  Subscriber<std::vector<uint32_t>> sub("gen_grow");

  pub.publish(std::vector<uint32_t>(4, 11));
  bool ok = false;
  {
    const std::vector<uint32_t> &v = sub.subscribe(&ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(v.size(), 4u);
    EXPECT_EQ(v[0], 11u);
  }

  const uint64_t gen_before = latestGeneration("gen_grow");

  // 世代 1 のセグメントの中身を控えておく
  SharedMemoryPosix root("gen_grow", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(root.connect());
  std::vector<unsigned char> before(root.getSize());
  std::memcpy(before.data(), root.getPtr(), before.size());
  const ShmHeader *root_header = reinterpret_cast<const ShmHeader *>(root.getPtr());
  const uint64_t   root_capacity_before = root_header->element_capacity;

  // 容量を大きく超える長さを publish する → 新しい世代ができるはず
  pub.publish(std::vector<uint32_t>(4096, 22));
  {
    const std::vector<uint32_t> &v = sub.subscribe(&ok);
    ASSERT_TRUE(ok);
    ASSERT_EQ(v.size(), 4096u);
    EXPECT_EQ(v[0], 22u);
    EXPECT_EQ(v[4095], 22u);
  }

  const uint64_t gen_after = latestGeneration("gen_grow");
  EXPECT_GT(gen_after, gen_before) << "レイアウトが変わったのに世代が進んでいない";
  EXPECT_TRUE(segmentExists(generationSegmentName("gen_grow")));

  // 世代 1 のセグメントは、latest_generation 以外は書き換わっていないこと。
  // これが F01 の要点: 稼働中のセグメントを破壊的に作り直さない。
  EXPECT_EQ(root_header->element_capacity, root_capacity_before)
      << "既存セグメントのレイアウトが書き換えられた（破壊的な再レイアウト）";

  // 進んでよいのは次の 2 つだけ:
  //   latest_generation … 世代タグ（世代 + ノンス）の公開
  //   sequence          … 発行番号はトピック全体で root のカウンタから採る（R03-F01）
  const size_t latest_gen_offset = offsetof(ShmHeader, latest_generation);
  const size_t sequence_offset   = offsetof(ShmHeader, sequence);
  for (size_t i = 0; i < sizeof(ShmHeader); ++i)
  {
    if (i >= latest_gen_offset && i < latest_gen_offset + sizeof(uint64_t))
    {
      continue;
    }
    if (i >= sequence_offset && i < sequence_offset + sizeof(uint64_t))
    {
      continue;
    }
    ASSERT_EQ(root.getPtr()[i], before[i]) << "世代 1 のヘッダのオフセット " << i << " が書き換えられた";
  }
}

TEST_F(SHMGenerationTest, CapacityOnlyGrowsAndShrinkingDoesNotChurnGenerations)
{
  // 長さが揺れるだけで世代が回ると、そのたびにセグメントが増えて
  // 全参加者が張り直すことになる。容量は増やすだけで運用する。
  Publisher<std::vector<uint32_t>> pub("gen_hyst", 3);

  pub.publish(std::vector<uint32_t>(1000, 1));
  const uint64_t gen_after_big = latestGeneration("gen_hyst");

  // 短くしても世代は進まない
  for (int i = 0; i < 20; ++i)
  {
    pub.publish(std::vector<uint32_t>(10, 2));
    pub.publish(std::vector<uint32_t>(500, 3));
  }
  EXPECT_EQ(latestGeneration("gen_hyst"), gen_after_big) << "長さが揺れただけで世代が回った";

  // 少しだけ増やしても、確保時の余裕に収まっていれば世代は進まない
  pub.publish(std::vector<uint32_t>(1010, 4));
  EXPECT_EQ(latestGeneration("gen_hyst"), gen_after_big) << "余裕の範囲内で世代が回った";

  Subscriber<std::vector<uint32_t>> sub("gen_hyst");
  bool                              ok = false;
  const std::vector<uint32_t>      &v  = sub.subscribe(&ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(v.size(), 1010u) << "容量ではなく実際の長さが返るべき";
  EXPECT_EQ(v[0], 4u);
}

// ---------------------------------------------------------------------------
// 取り残された参加者 (F01 の核心)
// ---------------------------------------------------------------------------

TEST_F(SHMGenerationTest, StaleParticipantsFollowTheNewGenerationSafely)
{
  Publisher<std::vector<uint32_t>>  pub_a("gen_stale", 3);
  Subscriber<std::vector<uint32_t>> sub("gen_stale");

  pub_a.publish(std::vector<uint32_t>(8, 1));
  bool ok = false;
  ASSERT_EQ(sub.subscribe(&ok).size(), 8u);
  ASSERT_TRUE(ok);

  // 別の Publisher が大きなレイアウトへ世代を進める
  {
    Publisher<std::vector<uint32_t>> pub_b("gen_stale", 3);
    pub_b.publish(std::vector<uint32_t>(20000, 2));
  }

  // 取り残された Publisher が publish しても落ちず、
  // 現世代へ追随して正しく書けること
  ASSERT_NO_THROW(pub_a.publish(std::vector<uint32_t>(8, 3)));

  const std::vector<uint32_t> &v = sub.subscribe(&ok);
  ASSERT_TRUE(ok);
  ASSERT_EQ(v.size(), 8u);
  EXPECT_EQ(v[0], 3u) << "取り残された Publisher の書き込みが読めない";
}

TEST_F(SHMGenerationTest, PublishersWithDifferentRequirementsConverge)
{
  // 要求が食い違う Publisher が互いに相手のレイアウトを作り直し合うと、
  // 世代が往復していつまでも収束しない。容量・スロット数・アライメントを
  // すべて「増やすだけ」にして、必ず最大値へ収束させる。
  Publisher<Msg> small("gen_converge", 3);
  small.publish(makeMsg(1));

  Publisher<Msg> large("gen_converge", 8);
  large.publish(makeMsg(2));

  // 小さい方を何度呼んでも、大きい世代を受け入れて世代は増えないこと
  const uint64_t gen = latestGeneration("gen_converge");
  for (int i = 0; i < 20; ++i)
  {
    ASSERT_NO_THROW(small.publish(makeMsg(100 + i)));
    ASSERT_NO_THROW(large.publish(makeMsg(200 + i)));
  }
  EXPECT_EQ(latestGeneration("gen_converge"), gen) << "要求の食い違いで世代が往復している";

  Subscriber<Msg> sub("gen_converge");
  bool            ok = false;
  const Msg      &m  = sub.subscribe(&ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(m.value, m.pad[0]) << "torn read";
}

// ---------------------------------------------------------------------------
// 後片付け
// ---------------------------------------------------------------------------

TEST_F(SHMGenerationTest, DisconnectTopicRemovesEveryGeneration)
{
  {
    Publisher<std::vector<uint32_t>> pub("gen_remove", 3);
    pub.publish(std::vector<uint32_t>(8, 1));
    pub.publish(std::vector<uint32_t>(40000, 2));
  }

  const uint64_t gen = latestGeneration("gen_remove");
  ASSERT_GT(gen, 1u) << "この検証には世代が 2 以上必要";
  ASSERT_TRUE(segmentExists("gen_remove"));
  const std::string live_segment = generationSegmentName("gen_remove");
  ASSERT_TRUE(segmentExists(live_segment));

  disconnectTopic("gen_remove");

  EXPECT_FALSE(segmentExists("gen_remove"));
  EXPECT_FALSE(segmentExists(live_segment)) << live_segment << " が残っている";
  // 名前にノンスが入るので、接頭辞で /dev/shm を走査して取り残しを見る
  EXPECT_EQ(countSegmentsWithPrefix("gen_remove#"), 0u) << "世代セグメントが残っている";
}
