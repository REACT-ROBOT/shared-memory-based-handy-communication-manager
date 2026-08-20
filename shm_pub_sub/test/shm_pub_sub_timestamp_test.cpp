// =============================================================================
// タイムスタンプ整合性テスト
//
// タイムスタンプ関連の修正は過去に5件あるが（CLOCK_REALTIME→MONOTONIC、
// getCurrentTimeUSec() 導入、MONOTONIC_RAW 化、isUpdated の誤タイムスタンプ
// 参照など）、いずれも回帰テストを伴っていなかった。
// このファイルは以下の仕様を固定する:
//
//   1. publish() はライブラリ規定のクロック (getCurrentTimeUSec =
//      CLOCK_MONOTONIC_RAW) でスタンプする
//   2. タイムスタンプは publish 順に単調非減少で、常に最新が選択される
//   3. データ期限切れは公開 API (Subscriber) 経由で実際に発動する
//   4. 未来のタイムスタンプ（クロック混在の典型症状）は「期限切れ」として
//      安全側に倒される
// =============================================================================

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"

using namespace irlab::shm;

namespace {
struct StampedMsg {
  uint32_t seq;
  uint32_t payload[15];
};
}  // namespace

class SHMTimestampTest : public ::testing::Test {
protected:
  void TearDown() override {
    disconnectMemory("ts_clock_domain");
    disconnectMemory("ts_ordering");
    disconnectMemory("ts_expiry_api");
    disconnectMemory("ts_expiry_disabled");
    disconnectMemory("ts_future");
  }
};

// -----------------------------------------------------------------------------
// 仕様1: publish() のスタンプはライブラリ規定のクロックで打たれる
//
// publish の前後で getCurrentTimeUSec() を取り、共有メモリ上のタイムスタンプが
// その区間に入っていることを確認する。別のクロック（CLOCK_REALTIME や
// steady_clock の epoch）でスタンプする退行が入ると大きく区間を外れて failする。
// 注: CLOCK_MONOTONIC と MONOTONIC_RAW のずれは NTP スルー補正の累積分なので、
//     起動直後の環境ではこのテストをすり抜け得る（実行時間の長い実機では発現）。
// -----------------------------------------------------------------------------
TEST_F(SHMTimestampTest, PublishStampsWithLibraryClock) {
  const std::string topic = "/ts_clock_domain";
  Publisher<StampedMsg> pub(topic);

  StampedMsg msg = {};
  msg.seq = 1;

  uint64_t before = getCurrentTimeUSec();
  pub.publish(msg);
  uint64_t after = getCurrentTimeUSec();

  // 共有メモリに直接アクセスしてスタンプを検証する
  SharedMemoryPosix shm(topic, O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(shm.connect());
  RingBuffer rb(shm.getPtr());

  int newest = rb.getNewestBufferNum();
  ASSERT_GE(newest, 0);
  uint64_t ts = rb.getTimestamp_us(newest);

  EXPECT_GE(ts, before) << "スタンプが publish 前の時刻より古い（クロック不一致の疑い）";
  EXPECT_LE(ts, after) << "スタンプが publish 後の時刻より新しい（クロック不一致の疑い）";
}

// -----------------------------------------------------------------------------
// 仕様2: タイムスタンプは publish 順に単調非減少で、常に最新が選択される
//
// 既知の限界: タイムスタンプは 1µs 分解能なので、1µs 以内に複数回 publish
// すると同値となり、同値の場合の選択はスロット番号順（publish 順とは限らない）
// になる。実運用のレート (〜kHz) では発生しないため、このテストは
// タイムスタンプが必ず進む条件（publish 間でクロックの前進を待つ）で
// 順序性を検証する。
// -----------------------------------------------------------------------------
TEST_F(SHMTimestampTest, TimestampsAreMonotonicAndNewestWins) {
  const std::string topic = "/ts_ordering";
  Publisher<StampedMsg> pub(topic);
  Subscriber<StampedMsg> sub(topic);

  SharedMemoryPosix shm(topic, O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(shm.connect());
  RingBuffer rb(shm.getPtr());

  uint64_t prev_ts = 0;
  for (uint32_t i = 1; i <= 20; ++i) {
    // 直前のスタンプと同値にならないようクロックの前進を待つ
    while (getCurrentTimeUSec() <= prev_ts) {
    }

    StampedMsg msg = {};
    msg.seq = i;
    pub.publish(msg);

    int newest = rb.getNewestBufferNum();
    ASSERT_GE(newest, 0);
    uint64_t ts = rb.getTimestamp_us(newest);
    EXPECT_GE(ts, prev_ts) << "タイムスタンプが逆行した (publish #" << i << ")";
    prev_ts = ts;

    // 購読者は常に最後に publish した値を読めること
    bool success = false;
    const StampedMsg& r = sub.subscribe(&success);
    ASSERT_TRUE(success);
    EXPECT_EQ(r.seq, i) << "最新でないデータが選択された";
  }
}

// -----------------------------------------------------------------------------
// 仕様3a: データ期限切れは公開 API 経由で実際に発動する
//
// setDataExpiryTime_us() を呼ぶテストはこれまで存在せず、期限切れが
// is_success=false として現れることは一度も検証されていなかった。
// -----------------------------------------------------------------------------
TEST_F(SHMTimestampTest, ExpiryTriggersThroughPublicApi) {
  const std::string topic = "/ts_expiry_api";
  Publisher<StampedMsg> pub(topic);
  Subscriber<StampedMsg> sub(topic);
  sub.setDataExpiryTime_us(100000);  // 100ms

  StampedMsg msg = {};
  msg.seq = 42;
  pub.publish(msg);

  // 期限内は読める
  bool success = false;
  const StampedMsg& r = sub.subscribe(&success);
  ASSERT_TRUE(success);
  EXPECT_EQ(r.seq, 42u);

  // 期限を過ぎると失敗になる
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  success = true;
  sub.subscribe(&success);
  EXPECT_FALSE(success) << "期限切れ (100ms) を過ぎたデータが読めてしまった";

  // 再 publish で復活する
  pub.publish(msg);
  success = false;
  sub.subscribe(&success);
  EXPECT_TRUE(success);
}

// -----------------------------------------------------------------------------
// 仕様3b: 期限切れ無効 (0) なら古いデータも読み続けられる
// -----------------------------------------------------------------------------
TEST_F(SHMTimestampTest, ExpiryDisabledKeepsDataReadable) {
  const std::string topic = "/ts_expiry_disabled";
  Publisher<StampedMsg> pub(topic);
  Subscriber<StampedMsg> sub(topic);
  sub.setDataExpiryTime_us(0);  // 期限切れ無効

  StampedMsg msg = {};
  msg.seq = 7;
  pub.publish(msg);

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  bool success = false;
  const StampedMsg& r = sub.subscribe(&success);
  EXPECT_TRUE(success);
  EXPECT_EQ(r.seq, 7u);
}

// -----------------------------------------------------------------------------
// 仕様4: 未来のタイムスタンプは「期限切れ」として安全側に倒される
//
// 別クロックでスタンプする古いバイナリとの混在や、クロック設定ミスの典型
// 症状は「読み手から見て未来のタイムスタンプ」になる。実装は符号なし減算の
// アンダーフローにより、これを期限切れ扱い（読めない）にする。壊れた
// スタンプのデータを平然と返すより安全なので、この挙動を仕様として固定する。
// （期限切れ無効時はこの防御も無効になる点に注意）
// -----------------------------------------------------------------------------
TEST_F(SHMTimestampTest, FutureTimestampIsTreatedAsExpired) {
  const std::string topic = "/ts_future";
  Publisher<StampedMsg> pub(topic);
  Subscriber<StampedMsg> sub(topic);

  StampedMsg msg = {};
  msg.seq = 9;
  pub.publish(msg);

  // スタンプを 10 秒未来に書き換える
  SharedMemoryPosix shm(topic, O_RDWR, static_cast<PERM>(0));
  ASSERT_TRUE(shm.connect());
  RingBuffer rb(shm.getPtr());
  int newest = rb.getNewestBufferNum();
  ASSERT_GE(newest, 0);
  rb.setTimestamp_us(getCurrentTimeUSec() + 10000000, newest);

  bool success = true;
  sub.subscribe(&success);
  EXPECT_FALSE(success) << "未来のタイムスタンプのデータが有効扱いされた";
}
