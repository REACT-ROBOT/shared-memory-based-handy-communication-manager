// =============================================================================
// データ破損・レースコンディション回帰テスト
//
// このファイルのテストは「既知の未修正バグ」を再現するもので、
// 現行実装では FAIL することを確認済み（テストファースト）。
// 実装修正（seqlock 方式の読み出し検証など）の完了をもって PASS になる。
//
// 対象バグ:
//   1. torn read: subscribe() はバッファ選択後のコピー中にタイムスタンプを
//      再確認しないため、publisher がリングを一周して同じバッファを
//      上書きすると、新旧データが混ざった値が「成功」として返る。
//   2. publish() のフォールスルー: allocateBuffer() が 10 回失敗しても
//      成否を確認せずに書き込み、確保できていないバッファを破壊する。
//   3. クラッシュしたプロセスが確保したまま残したバッファ
//      (タイムスタンプ = UINT64_MAX) が永久に再利用されず、
//      リングバッファが実質的に縮小していく。
// =============================================================================

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"

namespace {

// torn read を検出するための大きなメッセージ。
// 全ワードに同じシーケンス値を書き込み、読み出し側で全ワードの一致を検証する。
// 一致しない = コピー中に上書きされた torn read。
struct BigMsg {
  static constexpr size_t N = 16384;  // 64KB
  uint32_t words[N];

  void fill(uint32_t v) {
    for (size_t i = 0; i < N; ++i) {
      words[i] = v;
    }
  }
  // 全ワードが一致していれば -1、混在していれば最初の不一致インデックス
  long firstInconsistentWord() const {
    for (size_t i = 1; i < N; ++i) {
      if (words[i] != words[0]) {
        return static_cast<long>(i);
      }
    }
    return -1;
  }
};

// publisher スレッドを duration の間全力で回し、subscriber スレッドで
// 読み出した全メッセージの内部一貫性を検証する共通ルーチン。
struct TornReadResult {
  uint64_t reads = 0;
  uint64_t torn = 0;
};

// publisher スレッドが投げた例外は、そのままだと std::terminate になり
// FAIL ではなくアボートとして現れる（R04-F28）。数えて本体で EXPECT に落とす。
struct PublishFailures {
  std::atomic<uint64_t> count{0};
  std::string           first;
  std::mutex            mtx;

  void record(const std::exception& e) {
    if (count.fetch_add(1) == 0) {
      std::lock_guard<std::mutex> lock(mtx);
      first = e.what();
    }
  }
};

TornReadResult runTornReadStress(const std::string& topic, int buffer_num,
                                 std::chrono::milliseconds duration,
                                 PublishFailures* failures) {
  std::atomic<bool> stop(false);
  TornReadResult result;

  std::thread pub_thread([&]() {
    try {
      irlab::shm::Publisher<BigMsg> pub(topic, buffer_num);
      BigMsg msg;
      for (uint32_t seq = 1; !stop.load(std::memory_order_relaxed); ++seq) {
        msg.fill(seq);
        pub.publish(msg);
      }
    } catch (const std::exception& e) {
      failures->record(e);
    }
  });

  std::thread sub_thread([&]() {
    try {
    irlab::shm::Subscriber<BigMsg> sub(topic);
    while (!stop.load(std::memory_order_relaxed)) {
      bool success = false;
      const BigMsg& m = sub.subscribe(&success);
      if (!success) {
        continue;
      }
      ++result.reads;
      long bad = m.firstInconsistentWord();
      if (bad >= 0) {
        ++result.torn;
        if (result.torn <= 5) {
          std::cout << "TORN READ: words[0]=" << m.words[0] << " words[" << bad
                    << "]=" << m.words[bad] << std::endl;
        }
      }
    }
    } catch (const std::exception& e) {
      failures->record(e);
    }
  });

  std::this_thread::sleep_for(duration);
  stop.store(true);
  pub_thread.join();
  sub_thread.join();
  return result;
}

}  // namespace

class SHMPubSubRaceTest : public ::testing::Test {
protected:
  void TearDown() override {
    irlab::shm::disconnectMemory("race_torn_default");
    irlab::shm::disconnectMemory("race_torn_single");
    irlab::shm::disconnectMemory("race_unallocated_write");
    irlab::shm::disconnectMemory("race_slot_reclaim");
    irlab::shm::disconnectMemory("race_contention_held");
    irlab::shm::disconnectMemory("race_contention_fast");
    irlab::shm::disconnectMemory("race_contention_sane");
    irlab::shm::disconnectMemory("race_failed_read_clobber");
  }
};

// -----------------------------------------------------------------------------
// バグ1: torn read（デフォルト構成）
//
// デフォルトのバッファ3面で、publisher が全力で publish し続ける間、
// subscriber が読んだメッセージの全ワード一致を検証する。
// subscribe() が成功(is_success=true)で返した以上、内容の一貫性は
// ライブラリが保証すべきであり、torn read は 1 件も許容しない。
// -----------------------------------------------------------------------------
TEST_F(SHMPubSubRaceTest, SubscribeMustNeverReturnTornData) {
  PublishFailures failures;
  TornReadResult r =
      runTornReadStress("/race_torn_default", 3, std::chrono::milliseconds(4000), &failures);
  EXPECT_EQ(failures.count.load(), 0u) << "ワーカースレッドが例外を投げた: " << failures.first;

  std::cout << "default buffers: reads=" << r.reads << " torn=" << r.torn
            << std::endl;
  ASSERT_GT(r.reads, 0u) << "subscriber が一度も読めていない（テスト環境異常）";
  EXPECT_EQ(r.torn, 0u) << "subscribe() が新旧データの混ざった値を成功として返した";
}

// -----------------------------------------------------------------------------
// バグ1: torn read（単一バッファ構成）
//
// buffer_num=1 は API 上許容されており（既存テストでも使用）、
// 上書きサイクルが最短になるため torn read が最も高頻度に再現する。
// -----------------------------------------------------------------------------
TEST_F(SHMPubSubRaceTest, SubscribeMustNeverReturnTornDataSingleBuffer) {
  PublishFailures failures;
  TornReadResult r =
      runTornReadStress("/race_torn_single", 1, std::chrono::milliseconds(2000), &failures);
  EXPECT_EQ(failures.count.load(), 0u) << "ワーカースレッドが例外を投げた: " << failures.first;

  std::cout << "single buffer: reads=" << r.reads << " torn=" << r.torn
            << std::endl;
  ASSERT_GT(r.reads, 0u) << "subscriber が一度も読めていない（テスト環境異常）";
  EXPECT_EQ(r.torn, 0u) << "subscribe() が新旧データの混ざった値を成功として返した";
}

// -----------------------------------------------------------------------------
// バグ2: publish() は確保に失敗したバッファへ書き込んではならない
//
// 全バッファのタイムスタンプを UINT64_MAX（= 他の writer が確保中）にした
// 状態で publish() を呼ぶ。allocateBuffer() は全て失敗するので、
// publish() はデータ領域に一切書き込まず（例外送出または黙って断念）、
// 確保中マークも解除してはならない。
// 現行実装は 10 回リトライ後に成否を確認せず書き込み、さらに実時刻の
// タイムスタンプで確保中マークを上書きして「書き込み途中のバッファ」を
// 購読可能にしてしまう。
// -----------------------------------------------------------------------------
TEST_F(SHMPubSubRaceTest, PublishMustNotWriteToUnallocatedBuffer) {
  const std::string topic = "/race_unallocated_write";
  const int buffer_num = 3;

  irlab::shm::Publisher<BigMsg> pub(topic, buffer_num);

  // 同じ共有メモリに直接アクセスし、全バッファを「確保中」にする
  irlab::shm::SharedMemoryPosix shm(topic, O_RDWR,
                                    static_cast<irlab::shm::PERM>(0));
  ASSERT_TRUE(shm.connect());
  irlab::shm::RingBuffer rb(shm.getPtr());
  ASSERT_EQ(rb.getElementSize(), sizeof(BigMsg));

  // データ領域を既知のパターンで埋め、「生きている writer が全バッファを
  // 確保している」状態を作る。allocateBuffer() のマーカーには確保時刻が
  // 埋め込まれるため、新鮮なマーカー = 生きている writer と判定される
  // （クラッシュ writer の古いマーカーの回収は別テストで検証する）。
  unsigned char* data = rb.getDataList();
  const size_t data_bytes = sizeof(BigMsg) * buffer_num;
  std::memset(data, 0xDD, data_bytes);
  for (int i = 0; i < buffer_num; ++i) {
    ASSERT_TRUE(rb.allocateBuffer(i));
  }
  std::vector<unsigned char> snapshot(data, data + data_bytes);

  // publish は失敗してよい（例外も可）が、書き込んではならない
  BigMsg msg;
  msg.fill(0xCAFE);
  try {
    pub.publish(msg);
  } catch (const std::exception&) {
    // 確保失敗を例外で通知するのは許容される修正方針
  }

  EXPECT_EQ(std::memcmp(snapshot.data(), data, data_bytes), 0)
      << "publish() が確保できていないバッファに書き込んだ";

  // 確保中バッファが購読可能になってしまっていないことも確認
  irlab::shm::Subscriber<BigMsg> sub(topic);
  bool success = false;
  const BigMsg& m = sub.subscribe(&success);
  if (success) {
    EXPECT_NE(m.words[0], 0xCAFEu)
        << "確保に失敗したはずの publish の内容が購読できてしまった";
  }
}

// -----------------------------------------------------------------------------
// バグ3: クラッシュした writer が残した確保中バッファは再利用されなければならない
//
// writer が publish 途中（タイムスタンプ = UINT64_MAX のまま）で SIGKILL
// されたケースを模擬する。3面中2面をこの状態にし、生きている publisher が
// 発行を続けたとき、リークしたバッファがいずれ回収・再利用されること。
// 現行実装では UINT64_MAX のバッファは getOldestBufferNum() に決して選ばれず
// 永久にリークし、リングが実質1面に縮小して torn read を悪化させる。
// -----------------------------------------------------------------------------
TEST_F(SHMPubSubRaceTest, CrashedWriterSlotsMustBeReclaimed) {
  // 形式 v2 では「書き込み中」を時刻付きマーカーではなくスロット単位の
  // robust mutex で表す。したがってクラッシュした writer の再現は
  // 「確保したまま本当にプロセスを殺す」ことでしか作れない。
  // （v1 はタイムスタンプに UINT64_MAX を書き込めばマーカーを偽装できたが、
  //   その仕組みは「停止していただけの生きた writer からスロットを奪う」
  //   という R01-F04 の原因そのものだったため廃止した）
  const std::string topic = "/race_slot_reclaim";
  const int buffer_num = 3;

  { irlab::shm::Publisher<BigMsg> warm(topic, buffer_num); }

  // 子が確保したスロット番号を親へ伝えるための小さな共有メモリ
  irlab::shm::SharedMemoryPosix handshake("/race_slot_reclaim_hs", O_RDWR | O_CREAT,
                                          irlab::shm::DEFAULT_PERM);
  ASSERT_TRUE(handshake.connect(sizeof(std::atomic<int>)));
  auto* taken = reinterpret_cast<std::atomic<int>*>(handshake.getPtr());
  taken->store(-1);

  pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    irlab::shm::SharedMemoryPosix child_shm(topic, O_RDWR, irlab::shm::DEFAULT_PERM);
    child_shm.connect();
    auto child_rb = irlab::shm::attachRingBuffer(child_shm);
    if (child_rb == nullptr) {
      _exit(2);
    }
    // 2面を確保したまま死ぬ
    int first = child_rb->getOldestBufferNum();
    if (!child_rb->allocateBuffer(first)) {
      _exit(3);
    }
    int second = -1;
    for (int i = 0; i < buffer_num; ++i) {
      if (i != first && child_rb->allocateBuffer(i)) {
        second = i;
        break;
      }
    }
    if (second < 0) {
      _exit(4);
    }
    taken->store(first);
    std::this_thread::sleep_for(std::chrono::seconds(30));
    _exit(0);
  }

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (taken->load() < 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  ASSERT_GE(taken->load(), 0) << "子プロセスがスロットを確保できなかった";

  kill(pid, SIGKILL);
  waitpid(pid, nullptr, 0);

  // 死んだ writer が握っていた 2 面は EOWNERDEAD 経由で回収され、
  // 生きている publisher は普通に publish を続けられるはず。
  irlab::shm::Publisher<BigMsg> pub(topic, buffer_num);
  irlab::shm::Subscriber<BigMsg> sub(topic);
  BigMsg msg;
  for (int i = 1; i <= 20; ++i) {
    msg.fill(static_cast<uint32_t>(i));
    ASSERT_NO_THROW(pub.publish(msg))
        << "死んだ writer のスロットが回収されず publish が詰まった (i=" << i << ")";
    bool ok = false;
    const BigMsg& got = sub.subscribe(&ok);
    EXPECT_TRUE(ok) << "i=" << i;
    if (ok) {
      EXPECT_EQ(got.firstInconsistentWord(), -1) << "i=" << i;
    }
  }

  handshake.disconnectAndUnlink();
}

// -----------------------------------------------------------------------------
// 競合カウンタ: スロットが長く塞がれたことを数えられる
//
// R03-F04 で reader もスロットの robust mutex を取るようになったため、
// 「writer が reader を追い越した」だけでは競合は観測されなくなった
// （reader は負けて捨てるのではなく、短時間待って必ず読める）。
// カウンタが上がるのは **スロットが SLOT_LOCK_TIMEOUT_US を超えて塞がったとき**
// だけである。
//
// R03 対応時にこのテストの期待値を retry>0 から failure==0 へ緩めたが、
// それだけだと過負荷条件と正常条件の assertion が同一になり、カウンタ機構が
// 壊れても気づけなくなっていた（R04-F27）。ここでは外部からスロットの mutex を
// 保持して、**決定的に**カウンタが上がることを確かめる。
// -----------------------------------------------------------------------------
TEST_F(SHMPubSubRaceTest, ContentionCountersCountSlotsHeldLongerThanTheTimeout) {
  const std::string topic = "race_contention_held";

  irlab::shm::Publisher<BigMsg> pub(topic, 1);
  BigMsg msg;
  msg.fill(7);
  pub.publish(msg);

  irlab::shm::Subscriber<BigMsg> sub(topic);
  sub.setDataExpiryTime_us(0);
  {
    bool ok = false;
    sub.subscribe(&ok);
    ASSERT_TRUE(ok) << "前提: 競合が無ければ読めること";
  }
  sub.resetContentionCounts();

  // 唯一のスロットの mutex を外から保持する。allocateBuffer() ではなく
  // mutex を直接握るのは、発行番号を 0 に落とさず「有効なデータがあるのに
  // 読めない」状態を作るためである。
  irlab::shm::SharedMemoryPosix shm(topic, O_RDWR,
                                    static_cast<irlab::shm::PERM>(0));
  ASSERT_TRUE(shm.connect());
  unsigned char* ptr = shm.getPtr();
  const irlab::shm::ShmHeader* header =
      reinterpret_cast<const irlab::shm::ShmHeader*>(ptr);
  irlab::shm::SlotRecord* slot = reinterpret_cast<irlab::shm::SlotRecord*>(
      ptr + header->slot_offset);
  ASSERT_NE(slot->sequence.load(), 0u) << "前提: スロットに有効なデータがあること";
  ASSERT_EQ(pthread_mutex_lock(&slot->owner), 0);

  constexpr int ATTEMPTS = 3;
  for (int i = 0; i < ATTEMPTS; ++i) {
    bool ok = false;
    sub.subscribe(&ok);
    EXPECT_FALSE(ok) << "スロットが塞がっているのに成功を返した";
  }

  ASSERT_EQ(pthread_mutex_unlock(&slot->owner), 0);

  // subscribe 1 回につき MAX_READ_RETRY 回の再試行が起き、最後に失敗が 1 回計上される
  EXPECT_GT(sub.getContentionRetryCount(), 0u)
      << "スロットが塞がり続けたのに競合が一度も計上されなかった（カウンタが機能していない）";
  EXPECT_EQ(sub.getContentionFailureCount(), static_cast<uint64_t>(ATTEMPTS))
      << "読み出しの失敗が計上されていない";

  // 解放すれば元どおり読めること
  bool ok = false;
  const BigMsg& m = sub.subscribe(&ok);
  EXPECT_TRUE(ok);
  if (ok) {
    EXPECT_EQ(m.words[0], 7u);
  }

  sub.resetContentionCounts();
  EXPECT_EQ(sub.getContentionRetryCount(), 0u);
  EXPECT_EQ(sub.getContentionFailureCount(), 0u);
}

// -----------------------------------------------------------------------------
// 過負荷でも、スロットの排他が短時間で終わる限り競合は計上されない
//
// R03-F04 の相互排他が効いていることの裏返しの確認。ここが 0 でなくなったら、
// スロットの臨界区間が想定より長く滞留している。
// -----------------------------------------------------------------------------
TEST_F(SHMPubSubRaceTest, ShortCriticalSectionsDoNotShowUpAsContention) {
  PublishFailures failures;

  // 過負荷条件: 単一バッファ + 全力 publish
  {
    const std::string topic = "/race_contention_fast";
    std::atomic<bool> stop(false);

    std::thread pub_thread([&]() {
      try {
        irlab::shm::Publisher<BigMsg> pub(topic, 1);
        BigMsg msg;
        for (uint32_t seq = 1; !stop.load(std::memory_order_relaxed); ++seq) {
          msg.fill(seq);
          pub.publish(msg);
        }
      } catch (const std::exception& e) {
        failures.record(e);
      }
    });

    irlab::shm::Subscriber<BigMsg> sub(topic);
    uint64_t reads = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
    while (std::chrono::steady_clock::now() < end) {
      bool success = false;
      sub.subscribe(&success);
      if (success) ++reads;
    }
    stop.store(true);
    pub_thread.join();

    std::cout << "overload: reads=" << reads
              << " retry=" << sub.getContentionRetryCount()
              << " failure=" << sub.getContentionFailureCount() << std::endl;
    ASSERT_GT(reads, 0u) << "過負荷条件で一度も読めていない";
    EXPECT_EQ(sub.getContentionFailureCount(), 0u)
        << "スロットの排他が想定より長く滞留している";
  }

  // 正常レート条件: 3面バッファ + 5ms 間隔 publish
  {
    const std::string topic = "/race_contention_sane";
    std::atomic<bool> stop(false);

    std::thread pub_thread([&]() {
      try {
        irlab::shm::Publisher<BigMsg> pub(topic);  // buffer_num = 3
        BigMsg msg;
        uint32_t seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
          msg.fill(++seq);
          pub.publish(msg);
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      } catch (const std::exception& e) {
        failures.record(e);
      }
    });

    irlab::shm::Subscriber<BigMsg> sub(topic);
    uint64_t reads = 0;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (std::chrono::steady_clock::now() < end) {
      bool success = false;
      sub.subscribe(&success);
      if (success) ++reads;
    }
    stop.store(true);
    pub_thread.join();

    std::cout << "sane rate: reads=" << reads
              << " retry=" << sub.getContentionRetryCount()
              << " failure=" << sub.getContentionFailureCount() << std::endl;
    ASSERT_GT(reads, 0u);
    EXPECT_LE(sub.getContentionRetryCount(), reads / 100 + 1)
        << "正常なレートで競合が多発している";
    EXPECT_EQ(sub.getContentionFailureCount(), 0u);
  }

  EXPECT_EQ(failures.count.load(), 0u) << "ワーカースレッドが例外を投げた: " << failures.first;
}

// -----------------------------------------------------------------------------
// バグ5: subscribe() の失敗が直前の値を破壊する
//
// subscribe() は選択したバッファを return_buffer_ に直接コピーしてから
// タイムスタンプを再確認する。上書きを検出してリトライする際、コピー済みの
// 内容はそのまま残るため、最終的に失敗した場合 return_buffer_ には
// 「新旧が混ざった値」や「途中まで書き換わった値」が残る。
//
// 返り値は const T& なので、呼び出し側から見ると is_success を確認した前回の
// 値が、次の失敗した呼び出しによって黙って書き換えられることになる。
// 失敗時はスクラッチ領域へコピーし、一貫性を確認できたときだけ
// return_buffer_ と入れ替えるべき。
//
// 現行実装では失敗時に値が壊れるため FAIL する（テストファースト）。
// -----------------------------------------------------------------------------
TEST_F(SHMPubSubRaceTest, FailedSubscribeMustNotCorruptPreviousValue) {
  const std::string topic = "race_failed_read_clobber";

  // バッファ1面 = writer が常に唯一のバッファを奪うので確実に失敗を作れる
  irlab::shm::Publisher<BigMsg> pub(topic, 1);
  BigMsg initial;
  initial.fill(1);
  pub.publish(initial);

  irlab::shm::Subscriber<BigMsg> sub(topic);

  std::atomic<bool> stop(false);
  PublishFailures failures;
  std::thread pub_thread([&]() {
    try {
      BigMsg msg;
      for (uint32_t seq = 2; !stop.load(std::memory_order_relaxed); ++seq) {
        msg.fill(seq);
        pub.publish(msg);
      }
    } catch (const std::exception& e) {
      failures.record(e);
    }
  });

  uint64_t successes            = 0;
  uint64_t read_failures        = 0;
  uint64_t clobbered            = 0;  // 失敗時に直前の成功値と違っていた回数
  uint64_t torn_after_failure   = 0;  // 失敗時に内部矛盾した値が残っていた回数
  uint32_t last_good            = 0;
  bool     have_good            = false;

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (std::chrono::steady_clock::now() < deadline) {
    bool success = false;
    const BigMsg& m = sub.subscribe(&success);
    if (success) {
      ++successes;
      last_good = m.words[0];
      have_good = true;
      continue;
    }
    ++read_failures;
    if (!have_good) {
      continue;
    }
    if (m.firstInconsistentWord() >= 0) {
      ++torn_after_failure;
    }
    if (m.words[0] != last_good) {
      ++clobbered;
    }
  }

  stop.store(true);
  pub_thread.join();

  std::cout << "  success=" << successes << " failure=" << read_failures
            << " (失敗時に前回値が壊れた " << clobbered
            << " / うち内部矛盾 " << torn_after_failure << ")" << std::endl;

  EXPECT_EQ(failures.count.load(), 0u) << "ワーカースレッドが例外を投げた: " << failures.first;
  ASSERT_GT(read_failures, 0u) << "前提: 失敗を発生させられていない。テストの負荷設定を見直すこと";

  EXPECT_EQ(torn_after_failure, 0u) << "失敗した subscribe() が torn な値を返り値に残した";
  EXPECT_EQ(clobbered, 0u) << "失敗した subscribe() が直前の成功値を書き換えた";
}
