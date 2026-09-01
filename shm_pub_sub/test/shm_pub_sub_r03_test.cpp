//! @file shm_pub_sub_r03_test.cpp
//! @brief 3 回目のレビュー(R03)の指摘に対する回帰テスト
//!
//!   F01: 世代切替を跨いだ発行番号の一意性と、旧世代へ滑り込んだ commit の扱い
//!   F02: vector の最新値読み出しが共通の snapshot 経路を通ること
//!   F03: 時間だけを根拠に他プロセスのセグメントを消さないこと
//!   F04: payload の読み書きが相互排他されること
//!   F05: 可搬な schema 版で書式変更を検出できること

#include <gtest/gtest.h>
#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace
{
//! 版を明示した型（F05）
struct Versioned
{
  uint32_t a;
  uint32_t b;
};

uint64_t generationTagOf(const std::string &topic)
{
  SharedMemoryPosix shm(topic, O_RDWR, DEFAULT_PERM);
  if (!shm.connect())
  {
    return 0;
  }
  return reinterpret_cast<const ShmHeader *>(shm.getPtr())->latest_generation.load();
}

bool segmentExists(const std::string &shm_name)
{
  struct stat st;
  return ::stat(("/dev/shm/shm_" + shm_name).c_str(), &st) == 0;
}

//! 「作成しただけで初期化していないセグメント」を作る（＝作成途中で死んだ残骸）
bool createUninitializedSegment(const std::string &shm_name)
{
  const int fd = ::shm_open(("/" + shm_name).c_str(), O_RDWR | O_CREAT | O_EXCL, 0660);
  if (fd < 0)
  {
    return false;
  }
  const bool ok = (::ftruncate(fd, 4096) == 0);
  ::close(fd);
  return ok;
}
}  // namespace

namespace irlab
{
namespace shm
{
template <>
struct shm_schema<Versioned>
{
  static constexpr uint32_t version = 7;
};
}  // namespace shm
}  // namespace irlab

class SHMR03Test : public ::testing::Test
{
protected:
  void SetUp() override { cleanupAll(); }
  void TearDown() override { cleanupAll(); }
  void cleanupAll()
  {
    for (const char *t : { "r03_orphan", "r03_future", "r03_seq", "r03_pair", "r03_grow", "r03_ver" })
    {
      try
      {
        disconnectTopic(t);
      }
      catch (const std::exception &)
      {
      }
    }
  }
};

// -----------------------------------------------------------------------------
// R03-F03: 初期化前に作成者が死んだセグメントが残っても、容量拡張は止まらない
//
// 以前は世代セグメント名が固定の "#N" だったため、残骸が名前を占有すると
// O_EXCL が必ず失敗した。それを避けるための「一定時間待って未初期化なら消す」
// 方式は、単に遅い／SIGSTOP で止められているだけの生きた作成者を消し得た。
// 名前にノンスを入れることで、名前の取り合い自体が起きなくなる。
// -----------------------------------------------------------------------------
TEST_F(SHMR03Test, OrphanedGenerationDoesNotBlockGrowthAndIsNotWaitedFor)
{
  Publisher<std::vector<uint32_t>> pub("r03_orphan", 3);
  pub.publish(std::vector<uint32_t>(4, 1));

  const uint64_t tag = generationTagOf("r03_orphan");
  ASSERT_GT(unpackGeneration(tag), 0u);

  // 次の世代番号に、別ノンスの「作りかけ」を置く
  const std::string orphan =
      "shm_r03_orphan#" + std::to_string(unpackGeneration(tag) + 1) + "-0123456789ab";
  ASSERT_TRUE(createUninitializedSegment(orphan));

  // 容量を大きく超える長さ → 新世代が必要。孤児を待たずに即座に進むこと。
  const auto started = std::chrono::steady_clock::now();
  ASSERT_NO_THROW(pub.publish(std::vector<uint32_t>(40000, 2)));
  const auto elapsed = std::chrono::steady_clock::now() - started;

  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 200)
      << "孤児セグメントの初期化完了を待ってしまっている";

  Subscriber<std::vector<uint32_t>> sub("r03_orphan");
  bool                              ok = false;
  const std::vector<uint32_t>      &v  = sub.subscribe(&ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(v.size(), 40000u);
  EXPECT_EQ(v[0], 2u);

  // 同じ世代番号でノンスが違う残骸は「切り替え競争に負けた側」と確定できるので
  // 片付けてよい。時刻ではなく世代タグで判定している。
  if (unpackGeneration(generationTagOf("r03_orphan")) == unpackGeneration(tag) + 1)
  {
    EXPECT_FALSE(segmentExists(orphan.substr(4))) << "負けた側の残骸が片付いていない";
  }
}

// -----------------------------------------------------------------------------
// R03-F03: 現世代より新しい番号のセグメントは、生きた作成者のものかもしれない
//          ので絶対に消してはならない
// -----------------------------------------------------------------------------
TEST_F(SHMR03Test, SegmentOfAFutureGenerationIsNeverUnlinked)
{
  Publisher<std::vector<uint32_t>> pub("r03_future", 3);
  pub.publish(std::vector<uint32_t>(4, 1));

  const uint64_t before = unpackGeneration(generationTagOf("r03_future"));

  // 「今まさに作成中」を模した、ずっと先の世代のセグメント
  const std::string in_progress = "shm_r03_future#" + std::to_string(before + 5) + "-fedcba987654";
  ASSERT_TRUE(createUninitializedSegment(in_progress));

  // 何度か世代を進めても、先の世代のセグメントには触れないこと
  pub.publish(std::vector<uint32_t>(40000, 2));
  pub.publish(std::vector<uint32_t>(200000, 3));

  EXPECT_GT(unpackGeneration(generationTagOf("r03_future")), before);
  EXPECT_TRUE(segmentExists(in_progress.substr(4)))
      << "作成中かもしれないセグメントを消した（生きた作成者を殺し得る）";

  ::shm_unlink(("/" + in_progress).c_str());
}

// -----------------------------------------------------------------------------
// R03-F01: 発行番号はトピック全体で一意。世代ごとのカウンタでは、切り替え直後に
//          旧世代へ滑り込んだ commit と新世代の最初の commit が同じ番号を採り得た
// -----------------------------------------------------------------------------
TEST_F(SHMR03Test, OldGenerationCommitCannotReuseANewGenerationSequence)
{
  Publisher<std::vector<uint32_t>>  pub("r03_seq", 4);
  Subscriber<std::vector<uint32_t>> sub("r03_seq");
  pub.publish(std::vector<uint32_t>(4, 1));

  const uint64_t first_tag = generationTagOf("r03_seq");
  ASSERT_GT(unpackGeneration(first_tag), 0u);

  // 現世代のリングを直接掴んでおく。切替後もマッピングは生きている。
  SharedMemoryPosix old_seg(ShmTopic::generationName("r03_seq", first_tag), O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(old_seg.connect());
  RingBuffer old_ring(old_seg.getPtr());

  // ShmTopic は世代を問わず root のカウンタを採番元に束ねる。ここでも同じにする。
  // これをしない（＝世代ごとにカウンタを持つ）と、この後の commit は 1 番から
  // 採り直してしまい、新世代が既に配った番号と衝突する。それが R03-F01。
  SharedMemoryPosix root_seg("r03_seq", O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(root_seg.connect());
  RingBuffer root_ring(root_seg.getPtr());
  old_ring.setSequenceSource(root_ring.sequenceCounter());

  // 世代を進める
  pub.publish(std::vector<uint32_t>(40000, 2));
  ASSERT_GT(unpackGeneration(generationTagOf("r03_seq")), unpackGeneration(first_tag));

  bool       ok = false;
  SampleInfo after_cutover{};
  sub.subscribe(&ok, &after_cutover);
  ASSERT_TRUE(ok);

  // 切替に気付かない Publisher が旧世代へ commit したことにする
  int slot = old_ring.getOldestBufferNum();
  ASSERT_TRUE(old_ring.allocateBuffer(slot));
  old_ring.commitBuffer(slot, sizeof(uint32_t) * 4);
  const uint64_t straggler = old_ring.getSequence(slot);

  // 新世代側で次の publish
  pub.publish(std::vector<uint32_t>(40000, 3));
  SampleInfo newest{};
  sub.subscribe(&ok, &newest);
  ASSERT_TRUE(ok);

  EXPECT_NE(straggler, after_cutover.sequence) << "世代をまたいで発行番号が重複した";
  EXPECT_NE(straggler, newest.sequence) << "世代をまたいで発行番号が重複した";
  EXPECT_GT(straggler, after_cutover.sequence) << "旧世代の commit が過去の番号を採った";
  EXPECT_GT(newest.sequence, straggler) << "新世代の commit が旧世代より前の番号を採った";
}

// -----------------------------------------------------------------------------
// R03-F01: 世代が動いている最中でも publish は失敗せず、最新値が読めること
// -----------------------------------------------------------------------------
TEST_F(SHMR03Test, PublishingWhileTheGenerationMovesKeepsTheLatestValueVisible)
{
  std::atomic<bool>     stop{ false };
  std::atomic<uint64_t> throws{ 0 };

  // 大きさを振って世代を進め続ける側
  std::thread grower([&] {
    Publisher<std::vector<uint32_t>> pub("r03_grow", 4);
    size_t                           n = 16;
    while (!stop.load())
    {
      try
      {
        pub.publish(std::vector<uint32_t>(n, 0xA5A5A5A5u));
      }
      catch (const std::exception &)
      {
        ++throws;
      }
      n = (n < 200000) ? n * 2 : 16;
    }
  });

  // 一定長で publish し続ける側
  std::thread steady([&] {
    Publisher<std::vector<uint32_t>> pub("r03_grow", 4);
    while (!stop.load())
    {
      try
      {
        pub.publish(std::vector<uint32_t>(8, 0x5A5A5A5Au));
      }
      catch (const std::exception &)
      {
        ++throws;
      }
    }
  });

  Subscriber<std::vector<uint32_t>> sub("r03_grow");
  uint64_t                          reads = 0, inconsistent = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline)
  {
    bool                         ok = false;
    const std::vector<uint32_t> &v  = sub.subscribe(&ok);
    if (!ok)
    {
      continue;
    }
    ++reads;
    for (uint32_t x : v)
    {
      if (x != 0xA5A5A5A5u && x != 0x5A5A5A5Au)
      {
        ++inconsistent;
        break;
      }
    }
  }
  stop.store(true);
  grower.join();
  steady.join();

  EXPECT_GT(reads, 0u) << "世代が動いている間に一度も読めていない";
  EXPECT_EQ(inconsistent, 0u) << "世代切替を跨いで壊れた値が読めた";
  EXPECT_EQ(throws.load(), 0u) << "世代切替の競合で publish が失敗した";
}

// -----------------------------------------------------------------------------
// R03-F02/F04: vector でも payload と SampleInfo が必ず同じサンプルを指すこと
//
// スカラ版は R02-F03 で直したが、vector 版は subscribe(bool*) が旧実装のままで、
// info を取るために subscribe() の後で getSampleInfo() を呼び直していた。
// -----------------------------------------------------------------------------
TEST_F(SHMR03Test, VectorPayloadAndSampleInfoAlwaysDescribeTheSameSample)
{
  Publisher<std::vector<uint32_t>> pub("r03_pair", 1);

  std::atomic<bool>     stop{ false };
  std::atomic<uint64_t> mismatch{ 0 };
  std::thread           writer([&] {
    uint32_t v = 1;
    while (!stop.load())
    {
      try
      {
        pub.publish(std::vector<uint32_t>(4096, v++));
      }
      catch (const std::exception &)
      {
      }
    }
  });

  Subscriber<std::vector<uint32_t>> sub("r03_pair");
  sub.setDataExpiryTime_us(0);
  uint64_t success = 0;
  for (int i = 0; i < 100000; ++i)
  {
    bool                         ok = false;
    SampleInfo                   info{};
    const std::vector<uint32_t> &v = sub.subscribe(&ok, &info);
    if (!ok)
    {
      continue;
    }
    ++success;
    // 素性が空、長さが食い違う、中身が混ざっている、のいずれも不整合
    if (info.sequence == 0 || info.payload_size != v.size() * sizeof(uint32_t))
    {
      ++mismatch;
      continue;
    }
    for (uint32_t x : v)
    {
      if (x != v[0])
      {
        ++mismatch;
        break;
      }
    }
  }
  stop.store(true);
  writer.join();

  EXPECT_GT(success, 0u);
  EXPECT_EQ(mismatch.load(), 0u) << "vector の payload と SampleInfo が別サンプルになった";
}

// -----------------------------------------------------------------------------
// R03-F05: 自動 schema ID は型名の hash でしかないので、書式の版は利用者が示す
// -----------------------------------------------------------------------------
TEST_F(SHMR03Test, ExplicitSchemaVersionIsRecordedAndEnforced)
{
  Publisher<Versioned> pub("r03_ver", 3);
  pub.publish(Versioned{ 1, 2 });

  // 版がヘッダに記録されていること
  SharedMemoryPosix shm("r03_ver", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  EXPECT_EQ(reinterpret_cast<const ShmHeader *>(shm.getPtr())->schema_version, 7u);

  // 同じ版なら読める
  Subscriber<Versioned> sub("r03_ver");
  bool                  ok = false;
  const Versioned      &v  = sub.subscribe(&ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(v.a, 1u);

  // 版だけを書き換えると、型もサイズも同じでも拒まれること
  const_cast<ShmHeader *>(reinterpret_cast<const ShmHeader *>(shm.getPtr()))->schema_version = 8;
  Subscriber<Versioned> stale("r03_ver");
  ok = false;
  stale.subscribe(&ok);
  EXPECT_FALSE(ok) << "書式の版が違うのに接続できてしまった";
}
