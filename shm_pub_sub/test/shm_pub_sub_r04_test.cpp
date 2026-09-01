//! @file shm_pub_sub_r04_test.cpp
//! @brief 4 回目のレビュー(R04)の指摘に対する回帰テスト

#include <gtest/gtest.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace
{
struct Msg
{
  uint32_t v;
};
}  // namespace

class SHMR04Test : public ::testing::Test
{
protected:
  void SetUp() override { cleanupAll(); }
  void TearDown() override { cleanupAll(); }
  void cleanupAll()
  {
    for (const char *t : { "r04_unlink_pub", "r04_unlink_sub", "r04_cap", "r04_deadreader", "r04_contend",
                           "r04_heldslot", "r04_abi" })
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
// R04-F01: root が unlink された後の publish が SIGSEGV してはならない
//
// openRoot() は root セグメントが unlink されていると張り直すが、そのとき
// 古い SharedMemoryPosix のデストラクタが munmap する。世代 1 の ring_ と
// 全世代の sequence_source は root のマッピングを指しているので、
// 先に捨てておかないと直後の ring_->isLayoutChanged() が解放済み領域を読む。
//
// 引き金は稼働中の `shm_tool remove <topic>` と Publisher プロセスの再起動で、
// **R03 の移行手順（更新後は一度 remove せよ）そのもの**である。
// -----------------------------------------------------------------------------
TEST_F(SHMR04Test, PublishAfterTheRootSegmentIsUnlinkedMustNotCrash)
{
  Publisher<Msg> pub("r04_unlink_pub", 3);
  ASSERT_NO_THROW(pub.publish(Msg{ 1 }));

  // 別プロセスの `shm_tool remove <topic>` に相当する
  disconnectMemory("r04_unlink_pub");

  // 失敗して例外を投げるのは許容する。落ちてはならない。
  ASSERT_NO_FATAL_FAILURE({
    try
    {
      pub.publish(Msg{ 2 });
    }
    catch (const std::exception &)
    {
    }
  });

  // 張り直しに成功しているなら、購読できること
  Subscriber<Msg> sub("r04_unlink_pub");
  bool            ok = false;
  const Msg      &m  = sub.subscribe(&ok);
  if (ok)
  {
    EXPECT_EQ(m.v, 2u);
  }
}

// -----------------------------------------------------------------------------
// R04-F01: root が unlink され、別のプロセスが作り直した後の subscribe
// -----------------------------------------------------------------------------
TEST_F(SHMR04Test, SubscribeAfterTheTopicIsRecreatedMustNotCrash)
{
  {
    Publisher<Msg> pub("r04_unlink_sub", 3);
    pub.publish(Msg{ 11 });
  }

  Subscriber<Msg> sub("r04_unlink_sub");
  bool            ok = false;
  ASSERT_TRUE((sub.subscribe(&ok), ok));

  // unlink → 作り直し（Publisher プロセスの再起動に相当）
  disconnectMemory("r04_unlink_sub");
  Publisher<Msg> recreated("r04_unlink_sub", 3);
  recreated.publish(Msg{ 22 });

  ASSERT_NO_FATAL_FAILURE({
    bool ok2 = false;
    sub.subscribe(&ok2);
  });

  // 作り直された方を読めるようになっていること
  bool       ok3 = false;
  const Msg &m   = sub.subscribe(&ok3);
  EXPECT_TRUE(ok3) << "作り直されたトピックへ追随できていない";
  if (ok3)
  {
    EXPECT_EQ(m.v, 22u);
  }
}

// -----------------------------------------------------------------------------
// R04-F02: ensureCapacity() は要求を満たせないなら成功を返してはならない
//
// 容量は MAX_ELEMENT_SIZE で clamp されるが、clamp した後に要求充足を
// 再確認していなかった。満たせないまま true を返すので、呼び出し側は
// 要求どおりの長さを memcpy し、スロットの外へ書いていた。
// -----------------------------------------------------------------------------
TEST_F(SHMR04Test, PublishingBeyondTheMaximumElementSizeFailsInsteadOfOverflowing)
{
  Publisher<std::vector<uint8_t>> pub("r04_cap", 2);
  ASSERT_NO_THROW(pub.publish(std::vector<uint8_t>(16, 1)));

  // 1 スロットの上限を超える長さ。確保できないので publish は失敗するべき。
  const size_t too_big = static_cast<size_t>(RingBuffer::MAX_ELEMENT_SIZE) + (1u << 20);
  std::vector<uint8_t> huge;
  try
  {
    huge.assign(too_big, 2);
  }
  catch (const std::bad_alloc &)
  {
    GTEST_SKIP() << "この環境では検証用のバッファを確保できない";
  }

  EXPECT_THROW(pub.publish(huge), std::runtime_error)
      << "1 スロットの上限を超える長さを受け付けた（スロット外へ書き込む）";

  // 直前の値が壊れていないこと。
  // 巨大バッファの確保はサニタイザ下だと秒単位かかるので、
  // 既定の有効期限で「古すぎる」と判定されないよう期限を切っておく。
  Subscriber<std::vector<uint8_t>> sub("r04_cap");
  sub.setDataExpiryTime_us(0);
  bool                             ok = false;
  const std::vector<uint8_t>      &v  = sub.subscribe(&ok);
  ASSERT_TRUE(ok);
  EXPECT_EQ(v.size(), 16u);
  EXPECT_EQ(v[0], 1u);
}

// -----------------------------------------------------------------------------
// R04-F06: reader がスロットのロックを保持したまま死んでも、publish 済みの
//          サンプルは残らなければならない
//
// R03-F04 で reader もスロットの robust mutex を取るようになったため、
// EOWNERDEAD は「writer が書き込み中に死んだ」を意味しなくなった。
// それなのに readSample() が無条件に sequence を 0 にしていたので、
// subscriber ノードが落ちた瞬間に最新センサ値が消えていた。
// -----------------------------------------------------------------------------
TEST_F(SHMR04Test, SampleSurvivesAReaderThatDiesHoldingTheSlotLock)
{
  const std::string topic = "r04_deadreader";

  Publisher<Msg> pub(topic, 1);
  pub.publish(Msg{ 4242 });

  {
    Subscriber<Msg> warm(topic);
    bool            ok = false;
    ASSERT_TRUE((warm.subscribe(&ok), ok)) << "前提: 死ぬ前は読めること";
  }

  // 子プロセスでスロットのロックを取ったまま SIGKILL される
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0)
  {
    SharedMemoryPosix shm(topic, O_RDWR, static_cast<PERM>(0));
    if (shm.connect())
    {
      unsigned char   *ptr    = shm.getPtr();
      const ShmHeader *header = reinterpret_cast<const ShmHeader *>(ptr);
      SlotRecord      *slot   = reinterpret_cast<SlotRecord *>(ptr + header->slot_offset);
      pthread_mutex_lock(&slot->owner);
    }
    ::raise(SIGKILL);
    _exit(0);
  }
  int status = 0;
  ASSERT_EQ(::waitpid(pid, &status, 0), pid);

  // 死んだ reader のロックは EOWNERDEAD として回収されるが、
  // publish 済みのデータは残っていなければならない。
  Subscriber<Msg> sub(topic);
  sub.setDataExpiryTime_us(0);
  bool       ok = false;
  const Msg &m  = sub.subscribe(&ok);
  EXPECT_TRUE(ok) << "reader が死んだだけで publish 済みサンプルが破棄された";
  if (ok)
  {
    EXPECT_EQ(m.v, 4242u);
  }
}

// -----------------------------------------------------------------------------
// R04-F07: 世代 2 以降でも Contended と Empty を取り違えてはならない
//
// 発行番号の採番元を root へ一元化した副作用で、世代 2 以降のセグメントの
// header->sequence は永久に 0 になる。findBufferNum() がそれを見て
// Empty/Contended を判別していたため、データで満杯のトピックに対して
// 「データが 1 件も無い」と報告していた。subscribeAt() は Empty で
// 即座に諦めるので実害がある。
//
// vector トピックは最初の publish で必ず世代 2 へ進むので、
// スカラだけを見ていた既存テストではこの経路を通らなかった。
// -----------------------------------------------------------------------------
TEST_F(SHMR04Test, ContentionIsDistinguishedFromMissingDataOnLaterGenerations)
{
  const std::string topic = "r04_contend";

  Publisher<std::vector<uint32_t>>  pub(topic, 1);
  Subscriber<std::vector<uint32_t>> sub(topic);
  pub.publish(std::vector<uint32_t>(64, 5));

  {
    bool ok = false;
    ASSERT_TRUE((sub.subscribe(&ok), ok));
  }

  // 前提: vector トピックは容量確保のために世代が 2 以上へ進んでいる
  SharedMemoryPosix root(topic, O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(root.connect());
  const uint64_t tag = reinterpret_cast<const ShmHeader *>(root.getPtr())->latest_generation.load();
  ASSERT_GE(unpackGeneration(tag), 2u) << "前提: この検証には世代 2 以上が必要";

  // 唯一のスロットを writer として確保する。allocateBuffer() は
  // 「書き込み中」を表すために発行番号を 0 に落とすので、
  // findBufferNum() から見ると有効なスロットが 1 つも無い状態になる。
  // このとき Empty（データが無い）と Contended（今は読めないだけ）を
  // 区別するのが本題で、判定にはトピック全体の採番カウンタを使わなければならない。
  SharedMemoryPosix seg(ShmTopic::generationName(topic, tag), O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(seg.connect());
  RingBuffer live(seg.getPtr());
  // ShmTopic と同じく、採番元を root のカウンタへ束ねる。
  // これをしないと自セグメントのカウンタ（世代 2 以降は常に 0）を使ってしまう。
  RingBuffer root_ring(root.getPtr());
  live.setSequenceSource(root_ring.sequenceCounter());

  ASSERT_EQ(live.getBufferNum(), 1u) << "前提: 1 面リングであること";
  ASSERT_NE(live.getSequenceCounter(), 0u) << "前提: 既に publish されていること";
  ASSERT_TRUE(live.allocateBuffer(0));

  SearchStatus status = SearchStatus::Success;
  SampleInfo   info{};
  sub.subscribeAt(TimeQuery{ getCurrentTimeUSec(), SearchPolicy::Nearest }, &status, &info);

  live.commitBuffer(0, sizeof(uint32_t) * 64);  // スロットを解放する

  EXPECT_EQ(status, SearchStatus::Contended)
      << "データで満杯のトピックに対して Empty(=" << static_cast<int>(SearchStatus::Empty)
      << ") を返した。実際の status=" << static_cast<int>(status);
}

// -----------------------------------------------------------------------------
// R04-F08: 1 つのスロットが押さえられていても、空いているスロットがあれば
//          publish は成功しなければならない
//
// R03-F04 で reader もスロットの robust mutex を取るようになったのに、
// writer 側は getOldestBufferNum() が返す**同じ 1 つ**を 10 回試すだけだった。
// そのため、時間検索型の Subscriber（最古スロットを読む）が張り付くと、
// 他のスロットが空いていても publish が
// 「Could not allocate a buffer (all buffers are in use)」で失敗した。
// buffer_num を増やしても直らないうえ、メッセージも実態と食い違っていた。
// -----------------------------------------------------------------------------
TEST_F(SHMR04Test, PublishSucceedsWhileOneSlotIsHeldByAReader)
{
  const std::string topic = "r04_heldslot";
  constexpr int     BUF   = 3;

  Publisher<Msg> pub(topic, BUF);
  pub.publish(Msg{ 1 });

  SharedMemoryPosix shm(topic, O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(shm.connect());
  unsigned char   *ptr    = shm.getPtr();
  const ShmHeader *header = reinterpret_cast<const ShmHeader *>(ptr);
  ASSERT_EQ(header->buf_num, static_cast<uint64_t>(BUF));

  // 最古のスロット（次に writer が狙う先）を外から保持する
  SlotRecord *slots  = reinterpret_cast<SlotRecord *>(ptr + header->slot_offset);
  int         oldest = 0;
  uint64_t    oldest_seq = UINT64_MAX;
  for (int i = 0; i < BUF; ++i)
  {
    const uint64_t seq = slots[i].sequence.load();
    if (seq < oldest_seq)
    {
      oldest_seq = seq;
      oldest     = i;
    }
  }
  ASSERT_EQ(pthread_mutex_lock(&slots[oldest].owner), 0);

  // 残り 2 面が空いているのだから、publish は通らなければならない。
  // また、100Hz の制御ループを止めないよう短時間で決着すること。
  const auto started = std::chrono::steady_clock::now();
  for (int i = 0; i < 20; ++i)
  {
    ASSERT_NO_THROW(pub.publish(Msg{ static_cast<uint32_t>(100 + i) }))
        << "1 面が押さえられているだけで publish が失敗した（残り " << (BUF - 1) << " 面は空いている）";
  }
  const auto elapsed = std::chrono::steady_clock::now() - started;

  ASSERT_EQ(pthread_mutex_unlock(&slots[oldest].owner), 0);

  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100)
      << "空きスロットがあるのに待たされている";

  Subscriber<Msg> sub(topic);
  sub.setDataExpiryTime_us(0);
  bool       ok = false;
  const Msg &m  = sub.subscribe(&ok);
  EXPECT_TRUE(ok);
  if (ok)
  {
    EXPECT_EQ(m.v, 119u);
  }
}

// -----------------------------------------------------------------------------
// R04-F13: 待っても直らない不整合では、待たずに失敗しなければならない
//
// openRoot() の 1 秒ループは「O_EXCL の競争に負けたので勝者の初期化完了を待つ」
// ためのものだが、ABI・contract・magic の不一致という**待っても絶対に解決しない**
// 失敗でも同じだけ待っていた。publish のたびに呼ばれるので、40Hz のセンサノードが
// 1Hz のエラーログ生成器になり、原因も追いにくい。
//
// あわせて、復旧手順（shm_tool remove）とトピック名がメッセージに出ること。
// ABI 4 への移行で運用者が実機で最初に見るのがこのメッセージである。
// -----------------------------------------------------------------------------
TEST_F(SHMR04Test, APermanentMismatchFailsFastAndSaysHowToRecover)
{
  const std::string topic = "r04_abi";

  // 古い ABI のセグメントが残っている状態を作る。
  // ABI 4 へ上げた後、実機に前のビルドのセグメントが残っているのがこれにあたる。
  {
    Publisher<std::vector<uint32_t>> seed(topic, 3);
    seed.publish(std::vector<uint32_t>(8, 1));
  }
  {
    SharedMemoryPosix shm(topic, O_RDWR, static_cast<PERM>(0));
    ASSERT_TRUE(shm.connect());
    reinterpret_cast<ShmHeader *>(shm.getPtr())->abi_major = RingBuffer::ABI_MAJOR - 1;
  }

  // 古いセグメントに繋ごうとする新しいプロセスにあたる。
  // publish のたびに ensureCapacity を通る特殊化（lidar / point_cloud）では、
  // 40Hz なら 40 回/秒この経路を通るので、1 回でも秒単位待つと実害が出る。
  // ここでは同じ経路を繰り返し叩いて、毎回すぐ返ることを確かめる。
  std::string message;
  for (int i = 0; i < 5; ++i)
  {
    const auto started = std::chrono::steady_clock::now();
    try
    {
      Publisher<std::vector<uint32_t>> pub(topic, 3);
      pub.publish(std::vector<uint32_t>(8, 2));
      FAIL() << "ABI が違うのに publish が成功した";
    }
    catch (const std::exception &e)
    {
      message = e.what();
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    EXPECT_LT(elapsed, 100) << "待っても直らない不整合で " << elapsed << " ms 待たされた（" << i << " 回目）";
  }

  EXPECT_NE(message.find(topic), std::string::npos) << "メッセージにトピック名が無い: " << message;
  EXPECT_NE(message.find("shm_tool remove"), std::string::npos) << "メッセージに復旧手順が無い: " << message;
  EXPECT_NE(message.find("ABI"), std::string::npos) << "メッセージに原因が無い: " << message;

  // Subscriber 側も待たないこと
  Subscriber<std::vector<uint32_t>> sub(topic);
  const auto                        started = std::chrono::steady_clock::now();
  bool                              ok      = false;
  sub.subscribe(&ok);
  EXPECT_FALSE(ok);
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count(),
            100);
}
