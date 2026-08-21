// =============================================================================
// 後発 Publisher による共有メモリ再初期化の回帰テスト
//
// このファイルのテストは「既知の未修正バグ」を再現するもので、
// 現行実装では FAIL することを確認済み（テストファースト）。
//
// 対象バグ:
//   Publisher のコンストラクタは共有メモリが既に存在し初期化済みであっても
//   無条件に RingBuffer(ptr, sizeof(T), buffer_num) を構築する。この経路は
//     (a) initialization_flag を NOT_INITIALIZED に落とす
//     (b) mutex / condition variable を作り直す
//     (c) element_size / buf_num を書き戻す
//     (d) 全バッファのタイムスタンプを 0 にクリアする
//   を行う。このため、先に起動して publish 済みの共有メモリに対して後から
//   同名トピックの Publisher が生成されると、データ本体は残るがタイムスタンプ
//   が全消しされ、getNewestBufferNum() が -1 を返して subscribe() が失敗する
//   ようになる（次の publish が来るまでトピックが「消える」）。
//   同一プロセス内・別プロセスのどちらでも発現する。
//
// 期待する仕様:
//   既に初期化済みの共有メモリに接続した Publisher は、レイアウトが一致する
//   限り再初期化してはならない（タイムスタンプと初期化フラグを保存する）。
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

extern "C" {
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
}

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace {

struct Msg {
  uint32_t seq;
  uint32_t payload[7];
};

//! 共有メモリの生の状態（RingBuffer を構築せずに覗く）。
//! Publisher/Subscriber の API 越しでは「消えた」ことしか分からないため、
//! タイムスタンプとデータ本体を直接読んで変化を切り分ける。
struct RawRingState {
  uint32_t              init_flag = 0;
  size_t                element_size = 0;
  size_t                buf_num = 0;
  std::vector<uint64_t> timestamps;
  std::vector<Msg>      data;

  bool allTimestampsZero() const {
    for (uint64_t ts : timestamps) {
      if (ts != 0) {
        return false;
      }
    }
    return true;
  }

  size_t countNonZeroTimestamps() const {
    size_t n = 0;
    for (uint64_t ts : timestamps) {
      if (ts != 0) {
        ++n;
      }
    }
    return n;
  }

  void print(const std::string& tag) const {
    std::cout << "  [" << tag << "] init_flag=" << init_flag << " element_size=" << element_size
              << " buf_num=" << buf_num << std::endl;
    for (size_t i = 0; i < timestamps.size(); ++i) {
      std::cout << "    buf[" << i << "] ts=" << timestamps[i]
                << (RingBuffer::isBeingWritten(timestamps[i]) ? " (WRITING)" : "")
                << " data.seq=" << data[i].seq << " data.payload[0]=" << data[i].payload[0] << std::endl;
    }
  }
};

RawRingState peekRawRing(const std::string& topic, int buffer_num) {
  RawRingState state;

  SharedMemoryPosix shm(topic, O_RDWR, DEFAULT_PERM);
  if (!shm.connect(0)) {
    return state;
  }
  unsigned char* ptr = shm.getPtr();

  size_t mutex_offset, cond_offset, element_size_offset, buf_num_offset, timestamp_offset, data_offset;
  RingBuffer::calculateAlignedLayout(sizeof(Msg), buffer_num, mutex_offset, cond_offset, element_size_offset,
                                     buf_num_offset, timestamp_offset, data_offset);

  state.init_flag    = *reinterpret_cast<uint32_t*>(ptr);
  state.element_size = *reinterpret_cast<size_t*>(ptr + element_size_offset);
  state.buf_num      = *reinterpret_cast<size_t*>(ptr + buf_num_offset);

  for (size_t i = 0; i < state.buf_num; ++i) {
    state.timestamps.push_back(*reinterpret_cast<uint64_t*>(ptr + timestamp_offset + i * sizeof(uint64_t)));
    Msg m;
    std::memcpy(&m, ptr + data_offset + i * sizeof(Msg), sizeof(Msg));
    state.data.push_back(m);
  }

  shm.disconnect();
  return state;
}

Msg makeMsg(uint32_t seq) {
  Msg m{};
  m.seq = seq;
  for (size_t i = 0; i < 7; ++i) {
    m.payload[i] = seq;
  }
  return m;
}

}  // namespace

class SHMLatePublisherTest : public ::testing::Test {
protected:
  void SetUp() override {
    for (const auto& topic : topics_) {
      disconnectMemory(topic);
    }
  }
  void TearDown() override {
    for (const auto& topic : topics_) {
      disconnectMemory(topic);
    }
  }

  const std::vector<std::string> topics_ = {
    "late_pub_same_process", "late_pub_other_process", "late_pub_subscriber_view", "late_pub_init_flag",
    "late_pub_observation",   "late_pub_vector",       "late_pub_concurrent",
  };
};

// -----------------------------------------------------------------------------
// 仕様: 同一プロセス内で後から同名トピックの Publisher を生成しても、
//       既に書き込まれているタイムスタンプは保存されなければならない。
//
// 現行実装: 後発 Publisher のコンストラクタが全タイムスタンプを 0 に戻すため
//           FAIL する。データ本体は残っているので「値は生きているのに
//           タイムスタンプだけ消える」という症状になる。
// -----------------------------------------------------------------------------
TEST_F(SHMLatePublisherTest, LatePublisherMustNotWipeTimestamps) {
  const std::string topic      = "late_pub_same_process";
  const int         buffer_num = 3;

  Publisher<Msg> pub_first(topic, buffer_num);
  pub_first.publish(makeMsg(111));
  pub_first.publish(makeMsg(222));

  const RawRingState before = peekRawRing(topic, buffer_num);
  before.print("後発 Publisher 生成前");
  ASSERT_EQ(before.countNonZeroTimestamps(), 2u) << "前提: 2 バッファ分のタイムスタンプが立っているはず";

  {
    Publisher<Msg> pub_late(topic, buffer_num);  // 後から起動した publisher
    const RawRingState after = peekRawRing(topic, buffer_num);
    after.print("後発 Publisher 生成後");

    // データ本体は残っている（＝再初期化が消しているのはタイムスタンプ）
    EXPECT_EQ(after.data[0].seq, before.data[0].seq);
    EXPECT_EQ(after.data[1].seq, before.data[1].seq);

    EXPECT_FALSE(after.allTimestampsZero())
        << "後発 Publisher の生成で全タイムスタンプが 0 にクリアされた";
    for (size_t i = 0; i < before.timestamps.size(); ++i) {
      EXPECT_EQ(after.timestamps[i], before.timestamps[i]) << "buf[" << i << "] のタイムスタンプが変化した";
    }
  }
}

// -----------------------------------------------------------------------------
// 仕様: 別プロセスの Publisher が後から起動しても、先発 publisher が書き込んだ
//       タイムスタンプは保存されなければならない。
//
// 現行実装: 同一プロセスの場合と同様に FAIL する。実運用で報告されている
//           「後から起動したノードに共有メモリを初期化される」症状はこれ。
//           後発プロセスは publish を一切していない点に注意（生成しただけ）。
// -----------------------------------------------------------------------------
TEST_F(SHMLatePublisherTest, LatePublisherInOtherProcessMustNotWipeTimestamps) {
  const std::string topic      = "late_pub_other_process";
  const int         buffer_num = 3;

  Publisher<Msg> pub_first(topic, buffer_num);
  pub_first.publish(makeMsg(555));

  const RawRingState before = peekRawRing(topic, buffer_num);
  before.print("別プロセス Publisher 起動前");
  ASSERT_EQ(before.countNonZeroTimestamps(), 1u);

  pid_t pid = fork();
  ASSERT_GE(pid, 0) << "fork に失敗";
  if (pid == 0) {
    // 子プロセス: Publisher を生成するだけで publish はしない
    Publisher<Msg> pub_late(topic, buffer_num);
    _exit(0);
  }
  int status = 0;
  ASSERT_EQ(waitpid(pid, &status, 0), pid);
  ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << "子プロセスの Publisher 生成が失敗";

  const RawRingState after = peekRawRing(topic, buffer_num);
  after.print("別プロセス Publisher 起動後");

  EXPECT_EQ(after.data[0].seq, before.data[0].seq) << "データ本体は残るはず";
  EXPECT_EQ(after.timestamps[0], before.timestamps[0])
      << "後から起動した別プロセスの Publisher にタイムスタンプを消された";
}

// -----------------------------------------------------------------------------
// 仕様: 既存の Subscriber は、後発 Publisher の生成をまたいでも直前のデータを
//       読み続けられなければならない。
//
// 現行実装: タイムスタンプが 0 になり getNewestBufferNum() が -1 を返すため、
//           subscribe() が失敗し waitFor() もタイムアウトする。次の publish が
//           来るまでトピックが「無い」状態になる。
// -----------------------------------------------------------------------------
TEST_F(SHMLatePublisherTest, ExistingSubscriberMustKeepReadingAcrossLatePublisher) {
  const std::string topic      = "late_pub_subscriber_view";
  const int         buffer_num = 3;

  Publisher<Msg>  pub_first(topic, buffer_num);
  Subscriber<Msg> sub(topic);

  pub_first.publish(makeMsg(777));

  bool       ok  = false;
  const Msg& got = sub.subscribe(&ok);
  ASSERT_TRUE(ok) << "前提: 後発 Publisher 生成前は読めているはず";
  ASSERT_EQ(got.seq, 777u);

  {
    Publisher<Msg> pub_late(topic, buffer_num);

    bool       ok_after  = false;
    const Msg& got_after = sub.subscribe(&ok_after);
    std::cout << "  後発 Publisher 生成後の subscribe: success=" << ok_after
              << " seq=" << (ok_after ? got_after.seq : 0u) << std::endl;

    EXPECT_TRUE(ok_after) << "後発 Publisher の生成で購読中のデータが読めなくなった";
    if (ok_after) {
      EXPECT_EQ(got_after.seq, 777u);
    }

    // 既存データが残っていれば「読める」状態は維持されるはずで、
    // 現行実装ではタイムスタンプが消えるため waitFor もタイムアウトする。
    EXPECT_TRUE(ok_after || sub.waitFor(100000))
        << "後発 Publisher の生成後、既存データも更新も検知できない";
  }

  // 次の publish が来ればデータは復帰する（＝消えるのは publish までの区間）
  pub_first.publish(makeMsg(888));
  bool       ok_republish  = false;
  const Msg& got_republish = sub.subscribe(&ok_republish);
  EXPECT_TRUE(ok_republish);
  EXPECT_EQ(got_republish.seq, 888u);
}

// -----------------------------------------------------------------------------
// 仕様: 初期化済み共有メモリの initialization_flag は、後発 Publisher の生成中も
//       INITIALIZED のまま保たれなければならない。
//
// 現行実装: RingBuffer のコンストラクタが一旦 NOT_INITIALIZED を書いてから
//           pthread 構造体を作り直すため、その間に checkInitialized() を見た
//           プロセスは「未初期化」と判定する。窓は数十 usec と短いが、
//           mutex/cond の作り直しと重なるため観測できる。
// -----------------------------------------------------------------------------
TEST_F(SHMLatePublisherTest, LatePublisherMustNotClearInitializedFlag) {
  const std::string topic      = "late_pub_init_flag";
  const int         buffer_num = 3;

  Publisher<Msg> pub_first(topic, buffer_num);
  pub_first.publish(makeMsg(1));

  SharedMemoryPosix watcher_shm(topic, O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(watcher_shm.connect(0));
  unsigned char* ptr = watcher_shm.getPtr();
  ASSERT_TRUE(RingBuffer::checkInitialized(ptr));

  std::atomic<bool>     stop{ false };
  std::atomic<uint64_t> saw_not_initialized{ 0 };
  std::atomic<uint64_t> polls{ 0 };

  std::thread watcher([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      polls.fetch_add(1, std::memory_order_relaxed);
      if (!RingBuffer::checkInitialized(ptr)) {
        saw_not_initialized.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  constexpr int kLatePublisherCount = 200;
  for (int i = 0; i < kLatePublisherCount; ++i) {
    Publisher<Msg> pub_late(topic, buffer_num);
  }

  stop.store(true, std::memory_order_relaxed);
  watcher.join();

  std::cout << "  後発 Publisher " << kLatePublisherCount << " 回生成中に NOT_INITIALIZED を観測: "
            << saw_not_initialized.load() << " / " << polls.load() << " polls" << std::endl;

  watcher_shm.disconnect();

  EXPECT_EQ(saw_not_initialized.load(), 0u)
      << "後発 Publisher の生成中に initialization_flag が NOT_INITIALIZED に落ちた";
}

// -----------------------------------------------------------------------------
// 仕様: vector 特殊化でも後発 Publisher が既存の値を消してはならない。
//
// 現行実装: Publisher<std::vector<T>> のコンストラクタは vector_size = 0 で
//           リングバッファを構築するため、既存の共有メモリの element_size を
//           0 に書き換えてしまい、スカラ版と同じくタイムスタンプが全消しされる。
//           コンストラクタで既存の要素サイズを引き継げば接続のみで済む。
// -----------------------------------------------------------------------------
TEST_F(SHMLatePublisherTest, LateVectorPublisherMustNotWipeTimestamps) {
  const std::string      topic      = "late_pub_vector";
  const int              buffer_num = 3;
  const std::vector<int> value      = { 7, 8, 9, 10 };

  Publisher<std::vector<int>> pub_first(topic, buffer_num);
  pub_first.publish(value);

  Subscriber<std::vector<int>> sub(topic);
  bool                         ok = false;
  ASSERT_EQ(sub.subscribe(&ok), value);
  ASSERT_TRUE(ok) << "前提: 後発 Publisher 生成前は読めているはず";

  Publisher<std::vector<int>> pub_late(topic, buffer_num);

  bool ok_after = false;
  auto after    = sub.subscribe(&ok_after);
  EXPECT_TRUE(ok_after) << "後発 vector Publisher の生成で既存の値が読めなくなった";
  if (ok_after) {
    EXPECT_EQ(after, value);
  }

  // 後発 Publisher 生成後に作られた Subscriber からも読めること
  Subscriber<std::vector<int>> fresh_sub(topic);
  bool                         ok_fresh = false;
  auto                         fresh    = fresh_sub.subscribe(&ok_fresh);
  EXPECT_TRUE(ok_fresh) << "後発 vector Publisher の後に作った Subscriber が読めない";
  if (ok_fresh) {
    EXPECT_EQ(fresh, value);
  }
}

// -----------------------------------------------------------------------------
// 仕様: publish が続いている最中に Publisher が次々と生成されても、購読側から
//       トピックが消えてはならない。
//
// subscribe() の失敗には「データ無し」と「競合によるリトライ上限超過」の 2 種類が
// あり、後者は競合カウンタで数えられる。ここで検出したいのは前者（＝トピックが
// 消えた）だけなので、総失敗数から競合失敗数を引いた値を 0 と比較する。
// これなら負荷の高いマシンで競合が起きても偽陽性にならない。
//
// 現行実装: 後発 Publisher がタイムスタンプを消すたびにデータ無しになるため、
//           失敗が大量に出て FAIL する（実測で subscribe の約半数が失敗）。
// -----------------------------------------------------------------------------
TEST_F(SHMLatePublisherTest, ConcurrentPublisherStartupMustNotHideTopic) {
  const std::string topic      = "late_pub_concurrent";
  const int         buffer_num = 3;

  Publisher<Msg>  pub_first(topic, buffer_num);
  Subscriber<Msg> sub(topic);

  pub_first.publish(makeMsg(1));
  bool ok = false;
  sub.subscribe(&ok);
  ASSERT_TRUE(ok) << "前提: 計測開始前に一度は読めているはず";
  sub.resetContentionCounts();

  std::atomic<bool>     stop{ false };
  std::atomic<uint64_t> published{ 0 };
  std::atomic<uint64_t> read_ok{ 0 };
  std::atomic<uint64_t> read_fail{ 0 };

  std::thread writer([&] {
    uint32_t seq = 2;
    while (!stop.load(std::memory_order_relaxed)) {
      try {
        pub_first.publish(makeMsg(seq++));
        published.fetch_add(1, std::memory_order_relaxed);
      } catch (const std::runtime_error&) {
        // 全バッファが使用中: 本テストの対象外
      }
    }
  });

  std::thread reader([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      bool success = false;
      sub.subscribe(&success);
      (success ? read_ok : read_fail).fetch_add(1, std::memory_order_relaxed);
    }
  });

  constexpr int kLatePublisherCount = 300;
  for (int i = 0; i < kLatePublisherCount; ++i) {
    Publisher<Msg> pub_late(topic, buffer_num);
    std::this_thread::sleep_for(std::chrono::microseconds(200));
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
  reader.join();

  const uint64_t contention_failures = sub.getContentionFailureCount();
  const uint64_t total_failures      = read_fail.load();
  const uint64_t no_data_failures    = total_failures >= contention_failures ? total_failures - contention_failures : 0;

  std::cout << "  published=" << published.load() << " subscribe ok=" << read_ok.load()
            << " fail=" << total_failures << " (うち競合 " << contention_failures << " / データ無し "
            << no_data_failures << ")" << std::endl;

  EXPECT_EQ(no_data_failures, 0u)
      << "publish が続いているのに、後発 Publisher の生成でトピックが消えた回数";
}

// -----------------------------------------------------------------------------
// 観測用（常に PASS）: 値とタイムスタンプが「いつ・どう変わるか」の記録。
// 修正後もこのテストは PASS したまま出力だけが変わるので、挙動比較に使う。
// -----------------------------------------------------------------------------
TEST_F(SHMLatePublisherTest, ObserveTimestampAndValueTransition) {
  const std::string topic      = "late_pub_observation";
  const int         buffer_num = 3;

  Publisher<Msg> pub_first(topic, buffer_num);
  pub_first.publish(makeMsg(11));
  pub_first.publish(makeMsg(22));
  peekRawRing(topic, buffer_num).print("1. 先発 publisher が 2 回 publish");

  {
    Publisher<Msg> pub_late(topic, buffer_num);
    peekRawRing(topic, buffer_num).print("2. 後発 publisher を生成（publish はしない）");

    pub_first.publish(makeMsg(33));
    peekRawRing(topic, buffer_num).print("3. 先発 publisher がさらに publish");

    pub_late.publish(makeMsg(44));
    peekRawRing(topic, buffer_num).print("4. 後発 publisher が publish");
  }

  SUCCEED();
}
