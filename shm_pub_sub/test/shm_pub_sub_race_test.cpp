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
#include <thread>
#include <vector>

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

TornReadResult runTornReadStress(const std::string& topic, int buffer_num,
                                 std::chrono::milliseconds duration) {
  std::atomic<bool> stop(false);
  TornReadResult result;

  std::thread pub_thread([&]() {
    irlab::shm::Publisher<BigMsg> pub(topic, buffer_num);
    BigMsg msg;
    for (uint32_t seq = 1; !stop.load(std::memory_order_relaxed); ++seq) {
      msg.fill(seq);
      pub.publish(msg);
    }
  });

  std::thread sub_thread([&]() {
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
  TornReadResult r =
      runTornReadStress("/race_torn_default", 3, std::chrono::milliseconds(4000));

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
  TornReadResult r =
      runTornReadStress("/race_torn_single", 1, std::chrono::milliseconds(2000));

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

  // データ領域を既知のパターンで埋め、確保中マークを立てる
  unsigned char* data = rb.getDataList();
  const size_t data_bytes = sizeof(BigMsg) * buffer_num;
  std::memset(data, 0xDD, data_bytes);
  for (int i = 0; i < buffer_num; ++i) {
    rb.setTimestamp_us(std::numeric_limits<uint64_t>::max(), i);
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
  const std::string topic = "/race_slot_reclaim";
  const int buffer_num = 3;

  irlab::shm::Publisher<BigMsg> pub(topic, buffer_num);

  irlab::shm::SharedMemoryPosix shm(topic, O_RDWR,
                                    static_cast<irlab::shm::PERM>(0));
  ASSERT_TRUE(shm.connect());
  irlab::shm::RingBuffer rb(shm.getPtr());

  // バッファ 1, 2 を「クラッシュした writer が確保したまま」の状態にし、
  // データ領域に番兵パターンを書いておく
  unsigned char* data = rb.getDataList();
  std::memset(data + sizeof(BigMsg) * 1, 0xEE, sizeof(BigMsg) * 2);
  rb.setTimestamp_us(std::numeric_limits<uint64_t>::max(), 1);
  rb.setTimestamp_us(std::numeric_limits<uint64_t>::max(), 2);

  // 生きている publisher が発行を続ける（回収の猶予として約2秒間）
  BigMsg msg;
  for (int i = 1; i <= 100; ++i) {
    msg.fill(static_cast<uint32_t>(i));
    pub.publish(msg);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  // リークした2面のうち少なくとも1面は再利用され、番兵パターンが
  // 上書きされているべき
  auto slotStillLeaked = [&](int slot) {
    const unsigned char* p = data + sizeof(BigMsg) * slot;
    for (size_t i = 0; i < sizeof(BigMsg); ++i) {
      if (p[i] != 0xEE) {
        return false;
      }
    }
    return true;
  };
  EXPECT_FALSE(slotStillLeaked(1) && slotStillLeaked(2))
      << "クラッシュ writer が残した確保中バッファが一切回収されていない";
}
