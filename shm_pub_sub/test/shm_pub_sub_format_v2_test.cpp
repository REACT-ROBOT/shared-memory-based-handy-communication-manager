//! @file shm_pub_sub_format_v2_test.cpp
//! @brief 共有メモリ形式 v2 の回帰テスト (R01-F04 / F05 / F06 / F07-a)
//!
//! F04: 停止しているだけの生きた writer からスロットを奪わないこと
//!      SIGKILL された writer のスロットは EOWNERDEAD 経由で回収されること
//! F05: 発行番号が「最新」の正本であり、同一 microsecond でも誤選択しないこと
//! F06: magic / ABI 版 / ヘッダの自己矛盾を検出すること
//! F07-a: alignas を持つ型のペイロードが正しい境界に置かれること

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include "shm_pub_sub.hpp"

using namespace irlab::shm;

namespace
{
struct Payload
{
  uint64_t seq;
  uint64_t fill[7];
};

struct alignas(32) WidePayload
{
  double v[4];
};

void cleanup(const char *topic)
{
  try
  {
    disconnectMemory(topic);
  }
  catch (...)
  {
  }
}
}  // namespace

class SHMFormatV2Test : public ::testing::Test
{
protected:
  void SetUp() override { cleanupAll(); }
  void TearDown() override { cleanupAll(); }
  void cleanupAll()
  {
    for (const char *t : { "v2_header", "v2_seq", "v2_align", "v2_stop", "v2_kill", "v2_multi", "v2_gen" })
    {
      cleanup(t);
    }
  }
};

// ---------------------------------------------------------------------------
// 形式ヘッダ (F06)
// ---------------------------------------------------------------------------

TEST_F(SHMFormatV2Test, HeaderIsSelfDescribing)
{
  Publisher<Payload> pub("v2_header", 4);
  pub.publish(Payload{ 1, {} });

  SharedMemoryPosix shm("v2_header", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  const ShmHeader *h = reinterpret_cast<const ShmHeader *>(shm.getPtr());

  EXPECT_EQ(h->magic, RingBuffer::SHM_MAGIC);
  EXPECT_EQ(h->abi_major, RingBuffer::ABI_MAJOR);
  EXPECT_EQ(h->header_size, sizeof(ShmHeader));
  EXPECT_EQ(h->slot_size, sizeof(SlotRecord));
  EXPECT_EQ(h->buf_num, 4u);
  EXPECT_EQ(h->element_capacity, sizeof(Payload));
  EXPECT_EQ(h->payload_alignment, alignof(Payload));
  EXPECT_GT(h->generation.load(), 0u);
  EXPECT_LE(h->total_size, shm.getSize());
}

TEST_F(SHMFormatV2Test, ForeignMagicIsRejected)
{
  Publisher<Payload> pub("v2_header", 3);
  pub.publish(Payload{ 1, {} });

  SharedMemoryPosix shm("v2_header", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  ShmHeader *h = reinterpret_cast<ShmHeader *>(shm.getPtr());

  std::string reason;
  ASSERT_TRUE(RingBuffer::validateLayout(shm.getPtr(), shm.getSize(), &reason)) << reason;

  // v1 の共有メモリや別形式の領域を模す
  const uint32_t saved = h->magic;
  h->magic             = 1;  // v1 の initialization_flag = INITIALIZED
  EXPECT_FALSE(RingBuffer::validateLayout(shm.getPtr(), shm.getSize(), &reason));
  EXPECT_NE(reason.find("magic"), std::string::npos) << reason;

  h->magic = saved;
  // ABI 版の不一致
  h->abi_major = RingBuffer::ABI_MAJOR + 1;
  EXPECT_FALSE(RingBuffer::validateLayout(shm.getPtr(), shm.getSize(), &reason));
  h->abi_major = RingBuffer::ABI_MAJOR;

  // ヘッダが自分自身と矛盾している場合
  h->data_offset += 8;
  EXPECT_FALSE(RingBuffer::validateLayout(shm.getPtr(), shm.getSize(), &reason));
}

// ---------------------------------------------------------------------------
// 発行番号 (F05)
// ---------------------------------------------------------------------------

TEST_F(SHMFormatV2Test, SequenceIsUniqueAndMonotonicWithinOneMicrosecond)
{
  // 同一 microsecond に収まる速度で連続 publish しても、
  // 発行番号は重複せず単調増加し、最新値は必ず最後に publish した値になる。
  // v1 は時刻を順序の正本にしていたため、同値のときスロット番号で
  // 「最新」が決まり、最後の値とは限らなかった（R01-F05）。
  Publisher<Payload>  pub("v2_seq", 3);
  Subscriber<Payload> sub("v2_seq");

  constexpr int N = 200;
  for (int i = 1; i <= N; ++i)
  {
    pub.publish(Payload{ static_cast<uint64_t>(i), {} });
    bool ok = false;
    const Payload &got = sub.subscribe(&ok);
    ASSERT_TRUE(ok) << "i=" << i;
    EXPECT_EQ(got.seq, static_cast<uint64_t>(i)) << "最後に publish した値が最新として返らなかった (i=" << i << ")";
  }
}

TEST_F(SHMFormatV2Test, SequenceNeverRepeatsAcrossSlots)
{
  Publisher<Payload> pub("v2_seq", 3);
  for (int i = 0; i < 50; ++i)
  {
    pub.publish(Payload{ static_cast<uint64_t>(i), {} });
  }

  SharedMemoryPosix shm("v2_seq", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  auto rb = attachRingBuffer(shm);
  ASSERT_NE(rb, nullptr);

  std::vector<uint64_t> seqs;
  for (int i = 0; i < 3; ++i)
  {
    seqs.push_back(rb->getSequence(i));
  }
  std::sort(seqs.begin(), seqs.end());
  EXPECT_EQ(std::adjacent_find(seqs.begin(), seqs.end()), seqs.end()) << "発行番号が重複している";
}

// ---------------------------------------------------------------------------
// ペイロードのアライメント (F07-a)
// ---------------------------------------------------------------------------

TEST_F(SHMFormatV2Test, OverAlignedPayloadIsPlacedOnItsBoundary)
{
  // v1 は data_offset を 8 バイト境界にしか揃えておらず、alignas(32) の型を
  // その位置へ直接 cast して代入していた。
  Publisher<WidePayload> pub("v2_align", 3);
  WidePayload            w{};
  w.v[0] = 1.5;
  pub.publish(w);

  SharedMemoryPosix shm("v2_align", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  const ShmHeader *h = reinterpret_cast<const ShmHeader *>(shm.getPtr());

  EXPECT_EQ(h->payload_alignment, alignof(WidePayload));
  const unsigned char *base = shm.getPtr() + h->data_offset;
  EXPECT_EQ(reinterpret_cast<uintptr_t>(base) % alignof(WidePayload), 0u) << "ペイロード先頭が型の境界に載っていない";
  for (size_t i = 0; i < h->buf_num; ++i)
  {
    const unsigned char *slot_ptr = base + i * h->element_capacity;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(slot_ptr) % alignof(WidePayload), 0u)
        << "スロット " << i << " が型の境界に載っていない";
  }

  Subscriber<WidePayload> sub("v2_align");
  bool                    ok = false;
  const WidePayload      &got = sub.subscribe(&ok);
  EXPECT_TRUE(ok);
  EXPECT_DOUBLE_EQ(got.v[0], 1.5);
}

// ---------------------------------------------------------------------------
// スロット所有権 (F04)
// ---------------------------------------------------------------------------

TEST_F(SHMFormatV2Test, StoppedWriterSlotIsNotStolen)
{
  // 子プロセスにスロットを確保させたまま SIGSTOP で 1 秒以上止め、
  // その間に親がそのスロットを奪えないことを確認する。
  //
  // v1 は「書き込み中マーカーが 1 秒より古ければクラッシュ済み」とみなして
  // 無条件に奪っていた。再開した旧 writer が新 writer の payload を上書きし、
  // 壊れた値が有効データとして公開され得た（R01-F04）。
  const char *topic = "v2_stop";
  { Publisher<Payload> warm(topic, 3); }

  SharedMemoryPosix parent_shm(topic, O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(parent_shm.connect());
  auto parent_rb = attachRingBuffer(parent_shm);
  ASSERT_NE(parent_rb, nullptr);

  // 子が確保したスロット番号を親へ伝えるための小さな共有メモリ
  SharedMemoryPosix handshake("v2_stop_hs", O_RDWR | O_CREAT, DEFAULT_PERM);
  ASSERT_TRUE(handshake.connect(sizeof(std::atomic<int>)));
  auto *taken = reinterpret_cast<std::atomic<int> *>(handshake.getPtr());
  taken->store(-1);

  pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0)
  {
    SharedMemoryPosix child_shm(topic, O_RDWR, DEFAULT_PERM);
    child_shm.connect();
    auto child_rb = attachRingBuffer(child_shm);
    if (child_rb == nullptr)
    {
      _exit(2);
    }
    int slot = child_rb->getOldestBufferNum();
    if (!child_rb->allocateBuffer(slot))
    {
      _exit(3);
    }
    taken->store(slot);
    // 確保したまま待つ。親が SIGSTOP をかける。
    std::this_thread::sleep_for(std::chrono::seconds(30));
    _exit(0);
  }

  // 子が確保するのを待つ
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (taken->load() < 0 && std::chrono::steady_clock::now() < deadline)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const int slot = taken->load();
  ASSERT_GE(slot, 0) << "子プロセスがスロットを確保できなかった";

  kill(pid, SIGSTOP);
  // v1 の STALE_WRITE_TIMEOUT_US は 1 秒だった。それを十分に超えて待つ。
  std::this_thread::sleep_for(std::chrono::milliseconds(1500));

  EXPECT_FALSE(parent_rb->allocateBuffer(slot))
      << "停止しているだけの生きた writer からスロットを奪ってしまった (R01-F04)";

  kill(pid, SIGCONT);
  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);
  handshake.disconnectAndUnlink();
}

TEST_F(SHMFormatV2Test, KilledWriterSlotIsReclaimed)
{
  // 確保したまま SIGKILL された writer のスロットは、
  // robust mutex の EOWNERDEAD 経由で回収できること。
  const char *topic = "v2_kill";
  { Publisher<Payload> warm(topic, 3); }

  SharedMemoryPosix parent_shm(topic, O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(parent_shm.connect());
  auto parent_rb = attachRingBuffer(parent_shm);
  ASSERT_NE(parent_rb, nullptr);

  SharedMemoryPosix handshake("v2_kill_hs", O_RDWR | O_CREAT, DEFAULT_PERM);
  ASSERT_TRUE(handshake.connect(sizeof(std::atomic<int>)));
  auto *taken = reinterpret_cast<std::atomic<int> *>(handshake.getPtr());
  taken->store(-1);

  pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0)
  {
    SharedMemoryPosix child_shm(topic, O_RDWR, DEFAULT_PERM);
    child_shm.connect();
    auto child_rb = attachRingBuffer(child_shm);
    if (child_rb == nullptr)
    {
      _exit(2);
    }
    int slot = child_rb->getOldestBufferNum();
    if (!child_rb->allocateBuffer(slot))
    {
      _exit(3);
    }
    taken->store(slot);
    std::this_thread::sleep_for(std::chrono::seconds(30));
    _exit(0);
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (taken->load() < 0 && std::chrono::steady_clock::now() < deadline)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  const int slot = taken->load();
  ASSERT_GE(slot, 0);

  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);

  EXPECT_TRUE(parent_rb->allocateBuffer(slot)) << "死んだ writer のスロットが回収できない";
  EXPECT_EQ(parent_rb->getSequence(slot), 0u) << "回収したスロットの中身が有効なままになっている";
  parent_rb->releaseBuffer(slot);

  handshake.disconnectAndUnlink();
}

// ---------------------------------------------------------------------------
// 複数 Publisher (§3A)
// ---------------------------------------------------------------------------

TEST_F(SHMFormatV2Test, MultiplePublishersProduceAConsistentTotalOrder)
{
  // buf_num は同時に動く Publisher の数より大きくすること、という制約のもとで、
  // (a) 発行番号が重複しない (b) 読み手が返す値が必ずどれか1本の完全な1メッセージ
  // であることを確認する。
  constexpr int NUM_PUBLISHERS = 3;
  constexpr int NUM_MESSAGES   = 300;

  Publisher<Payload> warm("v2_multi", NUM_PUBLISHERS + 2);
  (void)warm;

  std::atomic<bool> start{ false };
  std::atomic<int>  publish_failures{ 0 };
  std::vector<std::thread> writers;
  for (int w = 0; w < NUM_PUBLISHERS; ++w)
  {
    writers.emplace_back([&, w] {
      Publisher<Payload> pub("v2_multi", NUM_PUBLISHERS + 2);
      while (!start.load())
      {
        std::this_thread::yield();
      }
      for (int i = 0; i < NUM_MESSAGES; ++i)
      {
        Payload p{};
        // 同一 Publisher からのメッセージは全フィールドが同じ値になる。
        // 混ざった値が読めたらそれは torn read である。
        p.seq = static_cast<uint64_t>(w) * 1000000 + i;
        for (auto &f : p.fill)
        {
          f = p.seq;
        }
        try
        {
          pub.publish(p);
        }
        catch (const std::exception &)
        {
          ++publish_failures;
        }
      }
    });
  }

  std::atomic<int> torn{ 0 };
  std::atomic<int> reads{ 0 };
  std::thread      reader([&] {
    Subscriber<Payload> sub("v2_multi");
    sub.setDataExpiryTime_us(0);
    while (!start.load())
    {
      std::this_thread::yield();
    }
    for (int i = 0; i < 20000; ++i)
    {
      bool ok = false;
      const Payload &p = sub.subscribe(&ok);
      if (!ok)
      {
        continue;
      }
      ++reads;
      for (auto f : p.fill)
      {
        if (f != p.seq)
        {
          ++torn;
          break;
        }
      }
    }
  });

  start.store(true);
  for (auto &t : writers)
  {
    t.join();
  }
  reader.join();

  EXPECT_EQ(torn.load(), 0) << "複数 Publisher の書き込みが混ざった値を success で返した";
  EXPECT_GT(reads.load(), 0) << "一度も読めていない";
  EXPECT_EQ(publish_failures.load(), 0) << "buf_num が Publisher 数より大きいのに publish が失敗した";
}

TEST_F(SHMFormatV2Test, ExhaustedSlotsFailLoudlyInsteadOfCorrupting)
{
  // 全スロットを生きた writer が占有している状態では、publish は
  // 沈黙して壊すのではなく例外で失敗すること。
  const char *topic     = "v2_gen";
  constexpr int BUF_NUM = 2;
  { Publisher<Payload> warm(topic, BUF_NUM); }

  SharedMemoryPosix shm(topic, O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  auto holder = attachRingBuffer(shm);
  ASSERT_NE(holder, nullptr);

  // 全スロットを確保したまま保持する
  int held = 0;
  for (int i = 0; i < BUF_NUM; ++i)
  {
    if (holder->allocateBuffer(i))
    {
      ++held;
    }
  }
  ASSERT_EQ(held, BUF_NUM);

  Publisher<Payload> pub(topic, BUF_NUM);
  EXPECT_THROW(pub.publish(Payload{ 1, {} }), std::runtime_error);

  for (int i = 0; i < BUF_NUM; ++i)
  {
    holder->releaseBuffer(i);
  }
}
