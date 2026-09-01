//! @file shm_pub_sub_contract_test.cpp
//! @brief 再レビュー R02 の指摘に対する回帰テスト
//!
//! R02 で実際に再現した異常系をそのままテストにしてある。
//!   F01: 型不一致で SIGSEGV / 誤データ
//!   F02: 初期化途中のセグメントに接続できてしまう
//!   F03: payload と SampleInfo が別サンプルになる
//!   F04: 未初期化の次世代セグメントが残ると容量拡張が回復しない（R03-F03 で方式変更）
//!   F05: 世代をまたいで発行番号が重複する
//!   F06: 旧世代セグメントが溜まり続ける

#include <gtest/gtest.h>
#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace
{
struct Small
{
  uint32_t v;
};
struct SameSizeAsSmall
{
  uint32_t w;
};
struct Huge
{
  char blob[1024 * 1024];
};
struct Tagged
{
  uint64_t seq;
  uint64_t fill[7];
};

size_t countSegments(const std::string &prefix)
{
  size_t n = 0;
  for (const auto &entry : { std::string("/dev/shm") })
  {
    (void)entry;
  }
  // /dev/shm を直接数える
  std::string cmd = "ls /dev/shm 2>/dev/null | grep -c '^shm_" + prefix + "'";
  FILE *fp = popen(cmd.c_str(), "r");
  if (fp == nullptr)
  {
    return 0;
  }
  char buf[32] = { 0 };
  if (fgets(buf, sizeof(buf), fp) != nullptr)
  {
    n = static_cast<size_t>(atoi(buf));
  }
  pclose(fp);
  return n;
}

//! 現在有効な世代番号
//! @details latest_generation は「世代 16bit + ノンス 48bit」のパック値なので、
//!          生の値を比較すると世代 1（= 1<<48）でも大きな数になり、
//!          EXPECT_GT(gen, 3) のような前提が**必ず通ってしまう**（R04-F25）。
//!          必ず unpackGeneration() を通すこと。
uint64_t latestGeneration(const std::string &topic)
{
  SharedMemoryPosix shm(topic, O_RDWR, DEFAULT_PERM);
  if (!shm.connect())
  {
    return 0;
  }
  return unpackGeneration(reinterpret_cast<const ShmHeader *>(shm.getPtr())->latest_generation.load());
}
}  // namespace

class SHMContractTest : public ::testing::Test
{
protected:
  void SetUp() override { cleanupAll(); }
  void TearDown() override { cleanupAll(); }
  void cleanupAll()
  {
    for (const char *t : { "c_mismatch", "c_samesize", "c_kind", "c_state", "c_orphan", "c_pair", "c_seq", "c_life" })
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
// R02-F01: topic contract
// ---------------------------------------------------------------------------

TEST_F(SHMContractTest, SubscribingWithABiggerTypeIsRejectedInsteadOfCrashing)
{
  // 小さい型で publish されたトピックへ大きな型で接続すると、
  // 以前は容量を超えて memcpy し SIGSEGV していた（exit 139 を実測）。
  Publisher<uint8_t> pub("c_mismatch", 1);
  pub.publish(7);

  Subscriber<Huge> sub("c_mismatch");
  bool             ok = true;
  sub.subscribe(&ok);
  EXPECT_FALSE(ok) << "型が食い違うのに成功を返した";
}

TEST_F(SHMContractTest, SubscribingWithADifferentTypeOfTheSameSizeIsRejected)
{
  // サイズが同じ別の型は落ちないが、誤った値を success として返していた。
  Publisher<Small> pub("c_samesize", 3);
  pub.publish(Small{ 42 });

  Subscriber<SameSizeAsSmall> sub("c_samesize");
  bool                        ok = true;
  sub.subscribe(&ok);
  EXPECT_FALSE(ok) << "同じサイズの別の型を受け入れてしまった";
}

TEST_F(SHMContractTest, ScalarAndVectorAreNotInterchangeable)
{
  Publisher<uint32_t> pub("c_kind", 3);
  pub.publish(1);

  Subscriber<std::vector<uint32_t>> sub("c_kind");
  bool                              ok = true;
  sub.subscribe(&ok);
  EXPECT_FALSE(ok) << "scalar のトピックを vector として読めてしまった";
}

TEST_F(SHMContractTest, MatchingTypeStillWorks)
{
  Publisher<Small>  pub("c_mismatch", 3);
  Subscriber<Small> sub("c_mismatch");
  pub.publish(Small{ 5 });
  bool ok = false;
  EXPECT_EQ(sub.subscribe(&ok).v, 5u);
  EXPECT_TRUE(ok);
}

// ---------------------------------------------------------------------------
// R02-F02: 初期化状態
// ---------------------------------------------------------------------------

TEST_F(SHMContractTest, SegmentBeingInitializedIsNotUsable)
{
  // state を INITIALIZING にしても validateLayout が成功し、購読側が
  // データを success で返していた。未初期化の mutex を触ることになる。
  Publisher<int> pub("c_state", 3);
  pub.publish(42);

  SharedMemoryPosix shm("c_state", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  auto          *h     = reinterpret_cast<ShmHeader *>(shm.getPtr());
  const uint32_t saved = h->state.load();
  h->state.store(2);  // INITIALIZING

  std::string reason;
  EXPECT_FALSE(RingBuffer::validateLayout(shm.getPtr(), shm.getSize(), &reason))
      << "初期化途中のセグメントを有効と判定した";

  Subscriber<int> sub("c_state");
  bool            ok = true;
  sub.subscribe(&ok);
  EXPECT_FALSE(ok) << "初期化途中のセグメントから読めてしまった";

  h->state.store(saved);
}

// ---------------------------------------------------------------------------
// R02-F03: payload と SampleInfo の一体性
// ---------------------------------------------------------------------------

TEST_F(SHMContractTest, PayloadAndSampleInfoAlwaysDescribeTheSameSample)
{
  // 1 スロットで競合させると、payload は sequence N、info は N+1 という
  // 組合せが返り得た（63 万回中 1,869 回）。時刻合わせでは別時刻のセンサ値を
  // 「整列済み」として返すことになる。
  Publisher<Tagged> pub("c_pair", 1);

  std::atomic<bool>     stop{ false };
  std::atomic<uint64_t> success{ 0 }, mismatch{ 0 };
  std::thread           writer([&] {
    uint64_t v = 1;
    while (!stop.load())
    {
      Tagged t{};
      t.seq = v++;
      for (auto &f : t.fill)
      {
        f = t.seq;
      }
      try
      {
        pub.publish(t);
      }
      catch (const std::runtime_error &)
      {
      }
    }
  });

  Subscriber<Tagged> sub("c_pair");
  sub.setDataExpiryTime_us(0);
  for (int i = 0; i < 200000; ++i)
  {
    bool       ok = false;
    SampleInfo info{};
    const Tagged &t = sub.subscribe(&ok, &info);
    if (!ok)
    {
      continue;
    }
    ++success;
    bool consistent = (info.sequence != 0);
    for (auto f : t.fill)
    {
      if (f != t.seq)
      {
        consistent = false;
        break;
      }
    }
    if (!consistent)
    {
      ++mismatch;
    }
  }
  stop.store(true);
  writer.join();

  EXPECT_GT(success.load(), 0u);
  EXPECT_EQ(mismatch.load(), 0u) << "payload と SampleInfo が別サンプルになった";
}

// ---------------------------------------------------------------------------
// R02-F04: 孤児セグメントからの回復
// ---------------------------------------------------------------------------

// R02-F04 で入れた「時間切れで孤児を回収する」テストは R03-F03 により廃止した。
// 時間で他プロセスのセグメントの生死を判定する方式そのものを取り除き、
// 世代セグメント名にノンスを含める方式へ変えたため、孤児が名前を占有すること自体が
// 起きなくなっている。置き換えの検証は shm_pub_sub_r03_test.cpp にある
// OrphanedGenerationDoesNotBlockGrowthAndIsNotWaitedFor /
// SegmentOfAFutureGenerationIsNeverUnlinked を参照。


// ---------------------------------------------------------------------------
// R02-F05 / F06: 世代をまたぐ発行番号の一意性と、旧世代の回収
// ---------------------------------------------------------------------------

// NOTE: これは「単一 Publisher が順に grow/publish しても番号が重複しない」ことしか
//       見ておらず、切替直後に旧世代へ commit が滑り込む競合は起こしていない
//       （R03 の軽微所見）。その決定的な検証は shm_pub_sub_r03_test.cpp の
//       OldGenerationCommitCannotReuseANewGenerationSequence にある。
TEST_F(SHMContractTest, SequenceStaysUniqueAcrossGenerations)
{
  Publisher<std::vector<uint32_t>>  pub("c_seq", 6);
  Subscriber<std::vector<uint32_t>> sub("c_seq");

  std::set<uint64_t> seen;
  size_t             duplicates = 0;
  uint64_t           first_gen  = 0;
  for (int n = 1; n <= 200; ++n)
  {
    pub.publish(std::vector<uint32_t>(static_cast<size_t>(n) * 8, static_cast<uint32_t>(n)));
    bool       ok = false;
    SampleInfo info{};
    sub.subscribe(&ok, &info);
    if (ok && !seen.insert(info.sequence).second)
    {
      ++duplicates;
    }
    if (first_gen == 0)
    {
      first_gen = latestGeneration("c_seq");
    }
  }
  EXPECT_GT(latestGeneration("c_seq"), first_gen) << "前提: この間に世代が進んでいること";
  EXPECT_EQ(duplicates, 0u) << "世代をまたいで発行番号が重複した";
}

TEST_F(SHMContractTest, SupersededGenerationsAreUnlinked)
{
  // 旧世代を消さないと、段階的な容量拡張で /dev/shm を食い潰す。
  Publisher<std::vector<uint32_t>> pub("c_life", 3);
  for (int n = 1; n <= 200; ++n)
  {
    pub.publish(std::vector<uint32_t>(static_cast<size_t>(n) * 16, 1));
  }
  const uint64_t generations = latestGeneration("c_life");
  EXPECT_GT(generations, 3u) << "前提: 世代が何度も進んでいること";

  // 残ってよいのは root（世代 1）と現世代だけ
  EXPECT_LE(countSegments("c_life"), 2u)
      << "旧世代のセグメントが " << countSegments("c_life") << " 個残っている（世代は " << generations << " まで進んだ）";
}
