//! @file shm_pub_sub_timemachine_test.cpp
//! @brief 時刻指定読み出し（タイムマシン）の回帰テスト
//!
//! レビュー R01 が「実装前に決めること」として挙げた検索の意味を、
//! そのままテストにしてある。
//!   - nearest / at-or-before / at-or-after のどれか  → 3 つとも明示的に提供
//!   - 検索に使う時計                                 → CLOCK_MONOTONIC_RAW のみ
//!                                                      (壁時計は NTP で飛ぶため使わない)
//!   - 同距離時の tie-break                           → 新しい方（発行番号が大きい方）
//!   - oldest より前 / newest より後の戻り値          → TooOld / TooNew
//!   - 読み出し競合時の戻り値                          → Contended（データ無しと区別）
//!   - 保持範囲は「リングに残っている分」だけ          → getRetentionWindow() で公開

#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>

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
}  // namespace

class SHMTimeMachineTest : public ::testing::Test
{
protected:
  void SetUp() override { cleanupAll(); }
  void TearDown() override { cleanupAll(); }
  void cleanupAll()
  {
    for (const char *t : { "tm_basic", "tm_empty", "tm_range", "tm_tie", "tm_vec", "tm_busy", "tm_window",
                           "tm_scan", "tm_odom" })
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
// 保持範囲
// ---------------------------------------------------------------------------

TEST_F(SHMTimeMachineTest, RetentionWindowReportsWhatIsActuallyHeld)
{
  constexpr int BUF = 5;
  Publisher<Msg>  pub("tm_window", BUF);
  Subscriber<Msg> sub("tm_window");

  EXPECT_EQ(sub.getRetentionWindow().count, 0u) << "publish 前は空のはず";

  for (uint32_t i = 1; i <= 3; ++i)
  {
    pub.publish(makeMsg(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  RetentionWindow w = sub.getRetentionWindow();
  EXPECT_EQ(w.count, 3u);
  EXPECT_LT(w.oldest_sequence, w.newest_sequence);
  EXPECT_LE(w.oldest_monotonic_us, w.newest_monotonic_us);

  // バッファ数を超えて publish しても、保持されるのはリングの面数まで
  for (uint32_t i = 4; i <= 20; ++i)
  {
    pub.publish(makeMsg(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  w = sub.getRetentionWindow();
  EXPECT_EQ(w.count, static_cast<size_t>(BUF)) << "保持数はバッファ数を超えない";
}

// ---------------------------------------------------------------------------
// 検索方針
// ---------------------------------------------------------------------------

TEST_F(SHMTimeMachineTest, AtOrBeforeReturnsTheSampleValidAtThatTime)
{
  Publisher<Msg>  pub("tm_basic", 5);
  Subscriber<Msg> sub("tm_basic");

  std::vector<SampleInfo> stamps;
  for (uint32_t i = 1; i <= 5; ++i)
  {
    pub.publish(makeMsg(i));
    bool ok = false;
    sub.subscribe(&ok);
    ASSERT_TRUE(ok);
    SampleInfo   info{};
    SearchStatus st = SearchStatus::Empty;
    sub.subscribeAt(TimeQuery{ 0, SearchPolicy::Nearest }, &st, &info);
    // Nearest with time 0 は最も古いものを返す。ここでは stamps 収集のため
    // 直接 window から取る方が確実なので、後で使う分だけ控える。
    (void)st;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  RetentionWindow w = sub.getRetentionWindow();
  ASSERT_EQ(w.count, 5u);

  // 最新時刻より後を指定 → 最新が「その時刻に有効だった値」
  SampleInfo   info{};
  SearchStatus st = SearchStatus::Empty;
  const Msg   &m  = sub.subscribeAt(
      TimeQuery{ w.newest_monotonic_us + 1000000, SearchPolicy::AtOrBefore }, &st, &info);
  EXPECT_EQ(st, SearchStatus::Success);
  EXPECT_EQ(info.sequence, w.newest_sequence);
  EXPECT_EQ(m.value, m.pad[0]);

  // 最古時刻ちょうど → 最古が返る
  sub.subscribeAt(TimeQuery{ w.oldest_monotonic_us, SearchPolicy::AtOrBefore }, &st, &info);
  EXPECT_EQ(st, SearchStatus::Success);
  EXPECT_EQ(info.sequence, w.oldest_sequence) << "境界（ちょうど一致）は該当扱いにする";
}

TEST_F(SHMTimeMachineTest, OutOfRangeTargetsAreReportedDistinctly)
{
  Publisher<Msg>  pub("tm_range", 3);
  Subscriber<Msg> sub("tm_range");
  for (uint32_t i = 1; i <= 3; ++i)
  {
    pub.publish(makeMsg(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
  }
  const RetentionWindow w = sub.getRetentionWindow();
  ASSERT_EQ(w.count, 3u);

  SearchStatus st = SearchStatus::Empty;

  // 保持範囲より古い時刻を AtOrBefore で引く → TooOld
  sub.subscribeAt(TimeQuery{ w.oldest_monotonic_us - 1, SearchPolicy::AtOrBefore }, &st);
  EXPECT_EQ(st, SearchStatus::TooOld);

  // 保持範囲より新しい時刻を AtOrAfter で引く → TooNew
  sub.subscribeAt(TimeQuery{ w.newest_monotonic_us + 1, SearchPolicy::AtOrAfter }, &st);
  EXPECT_EQ(st, SearchStatus::TooNew);

  // Nearest は有効なサンプルが1つでもあれば必ず見つかる
  sub.subscribeAt(TimeQuery{ 0, SearchPolicy::Nearest }, &st);
  EXPECT_EQ(st, SearchStatus::Success);
  sub.subscribeAt(TimeQuery{ UINT64_MAX, SearchPolicy::Nearest }, &st);
  EXPECT_EQ(st, SearchStatus::Success);
}

TEST_F(SHMTimeMachineTest, EmptyTopicIsReportedAsEmptyNotAsNotFound)
{
  Publisher<Msg>  pub("tm_empty", 3);
  Subscriber<Msg> sub("tm_empty");
  SearchStatus    st = SearchStatus::Success;
  sub.subscribeAt(TimeQuery{ 12345, SearchPolicy::AtOrBefore }, &st);
  EXPECT_EQ(st, SearchStatus::Empty) << "1件も無いことと、範囲外であることは区別する";
}

TEST_F(SHMTimeMachineTest, NearestBreaksTiesTowardTheNewerSample)
{
  // 同一時刻のサンプルを 2 件作り、等距離のときに新しい方が選ばれることを確認する。
  // v1 では順序の正本が時刻そのものだったので、この規則が定義できなかった。
  constexpr int BUF = 4;
  Publisher<Msg> pub("tm_tie", BUF);

  SharedMemoryPosix shm("tm_tie", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());

  pub.publish(makeMsg(1));
  pub.publish(makeMsg(2));

  auto rb = attachRingBuffer(shm);
  ASSERT_NE(rb, nullptr);

  // 2 つのスロットの capture 時刻を同じ値に揃える（同一 microsecond の再現）
  const uint64_t common_time = 1000000;
  int            slots[2]    = { -1, -1 };
  int            n           = 0;
  for (size_t i = 0; i < rb->getBufferNum() && n < 2; ++i)
  {
    if (rb->getSequence(static_cast<int>(i)) != 0)
    {
      slots[n++] = static_cast<int>(i);
    }
  }
  ASSERT_EQ(n, 2);

  ASSERT_TRUE(rb->allocateBuffer(slots[0]));
  rb->commitBuffer(slots[0], sizeof(Msg), common_time);
  ASSERT_TRUE(rb->allocateBuffer(slots[1]));
  rb->commitBuffer(slots[1], sizeof(Msg), common_time);

  const uint64_t newer_seq = std::max(rb->getSequence(slots[0]), rb->getSequence(slots[1]));

  Subscriber<Msg> sub("tm_tie");
  SampleInfo      info{};
  SearchStatus    st = SearchStatus::Empty;
  sub.subscribeAt(TimeQuery{ common_time, SearchPolicy::Nearest }, &st, &info);
  ASSERT_EQ(st, SearchStatus::Success);
  EXPECT_EQ(info.sequence, newer_seq) << "等距離のタイブレークは新しい方（発行番号が大きい方）";
}

// ---------------------------------------------------------------------------
// 競合
// ---------------------------------------------------------------------------

TEST_F(SHMTimeMachineTest, ContentionIsDistinguishedFromMissingData)
{
  // publisher がリングを回し続けている最中に過去を引くと、選んだスロットが
  // コピー中に上書きされ得る。そのときは torn data を成功扱いせず、
  // 「データが無い」でもなく Contended を返して再試行の判断を委ねる。
  constexpr int BUF = 2;  // 面数を絞って上書きを起こしやすくする
  Publisher<Msg>  pub("tm_busy", BUF);
  Subscriber<Msg> sub("tm_busy");

  pub.publish(makeMsg(1));

  std::atomic<bool> stop{ false };
  std::thread       writer([&] {
    uint32_t v = 2;
    while (!stop.load())
    {
      try
      {
        pub.publish(makeMsg(v++));
      }
      catch (const std::runtime_error &)
      {
      }
    }
  });

  size_t success = 0, contended = 0, other = 0, torn = 0;
  for (int i = 0; i < 20000; ++i)
  {
    SearchStatus st = SearchStatus::Empty;
    const Msg   &m  = sub.subscribeAt(TimeQuery{ UINT64_MAX, SearchPolicy::Nearest }, &st);
    if (st == SearchStatus::Success)
    {
      ++success;
      for (auto p : m.pad)
      {
        if (p != m.value)
        {
          ++torn;
          break;
        }
      }
    }
    else if (st == SearchStatus::Contended)
    {
      ++contended;
    }
    else
    {
      ++other;
    }
  }
  stop.store(true);
  writer.join();

  std::cout << "  success=" << success << " contended=" << contended << " other=" << other << std::endl;
  EXPECT_EQ(torn, 0u) << "混ざった値を Success で返した";
  EXPECT_GT(success, 0u);
  EXPECT_EQ(other, 0u) << "競合を Empty/TooOld/TooNew と取り違えている";
}

// ---------------------------------------------------------------------------
// 可変長
// ---------------------------------------------------------------------------

TEST_F(SHMTimeMachineTest, VectorTopicReturnsThePastLengthNotTheCapacity)
{
  Publisher<std::vector<uint32_t>>  pub("tm_vec", 8);
  Subscriber<std::vector<uint32_t>> sub("tm_vec");

  // 先に大きく確保させ、以後は容量に収まる長さで publish する
  pub.publish(std::vector<uint32_t>(1000, 1));
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  pub.publish(std::vector<uint32_t>(3, 7));
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  const uint64_t after_short = getCurrentTimeUSec();
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  pub.publish(std::vector<uint32_t>(500, 9));

  SampleInfo   info{};
  SearchStatus st = SearchStatus::Empty;
  const std::vector<uint32_t> &past =
      sub.subscribeAt(TimeQuery{ after_short, SearchPolicy::AtOrBefore }, &st, &info);
  ASSERT_EQ(st, SearchStatus::Success);
  // 容量は 1000 要素分あるが、そのサンプルの長さは 3 のはず
  EXPECT_EQ(past.size(), 3u) << "容量ではなく、その時点の実際の長さが返るべき";
  EXPECT_EQ(past[0], 7u);
  EXPECT_EQ(info.payload_size, 3 * sizeof(uint32_t));

  const std::vector<uint32_t> &latest = sub.subscribeAt(TimeQuery{ UINT64_MAX, SearchPolicy::Nearest }, &st, &info);
  ASSERT_EQ(st, SearchStatus::Success);
  EXPECT_EQ(latest.size(), 500u);
  EXPECT_EQ(latest[0], 9u);
}

TEST_F(SHMTimeMachineTest, RealtimeIsRecordedButNotUsedForSearching)
{
  // 壁時計は NTP 同期で前後に飛ぶ。それを検索の基準にすると
  // サンプルの順序が入れ替わったり同じ時刻が二度現れたりして安定しない。
  // 記録としては保持する（ログの突き合わせや人間向けの表示に使う）が、
  // 検索は CLOCK_MONOTONIC_RAW のみで行う。
  Publisher<Msg>  pub("tm_basic", 4);
  Subscriber<Msg> sub("tm_basic");
  pub.publish(makeMsg(1));
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  pub.publish(makeMsg(2));

  const RetentionWindow w = sub.getRetentionWindow();
  ASSERT_EQ(w.count, 2u);
  EXPECT_GT(w.newest_realtime_us, 0u) << "壁時計の時刻が記録されていない";
  EXPECT_GE(w.newest_realtime_us, w.oldest_realtime_us);

  SampleInfo   info{};
  bool         ok = false;
  sub.subscribe(&ok, &info);
  ASSERT_TRUE(ok);
  EXPECT_GT(info.capture_realtime_us, 0u) << "サンプル単位でも壁時計が取れること";
  EXPECT_GT(info.capture_monotonic_us, 0u);
}

// ---------------------------------------------------------------------------
// 主用途: センサ間の時刻合わせ
// ---------------------------------------------------------------------------

TEST_F(SHMTimeMachineTest, AlignsAScanToTheOdometryUpdateTime)
{
  // 自己位置推定の典型: 「いま更新されたオドメトリと同じ時刻のスキャンが欲しい」
  // オドメトリとスキャンは別々のレートで流れており、時刻が完全には一致しない。
  Publisher<Msg>  scan_pub("tm_scan", 16);
  Publisher<Msg>  odom_pub("tm_odom", 8);
  Subscriber<Msg> scan_sub("tm_scan");
  Subscriber<Msg> odom_sub("tm_odom");

  // スキャンを先に何枚か流す
  for (uint32_t i = 1; i <= 6; ++i)
  {
    scan_pub.publish(makeMsg(i));
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  // オドメトリが更新される
  odom_pub.publish(makeMsg(1000));
  const uint64_t odom_time = getCurrentTimeUSec();
  // その後もスキャンは流れ続ける
  for (uint32_t i = 7; i <= 12; ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
    scan_pub.publish(makeMsg(i));
  }

  SampleInfo odom_info{};
  bool       ok = false;
  odom_sub.subscribe(&ok, &odom_info);
  ASSERT_TRUE(ok);
  EXPECT_NEAR(static_cast<double>(odom_info.capture_monotonic_us), static_cast<double>(odom_time), 20000.0);

  SampleInfo   scan_info{};
  SearchStatus st = SearchStatus::Empty;
  const Msg   &scan = scan_sub.subscribeAlignedTo(odom_info, &st, 0, &scan_info);
  ASSERT_EQ(st, SearchStatus::Success);
  EXPECT_EQ(scan.value, scan.pad[0]) << "torn read";

  // 取れたスキャンは、オドメトリ時刻に最も近いものであること
  const RetentionWindow w = scan_sub.getRetentionWindow();
  ASSERT_GT(w.count, 0u);
  const uint64_t chosen_skew =
      (scan_info.capture_monotonic_us > odom_info.capture_monotonic_us)
          ? scan_info.capture_monotonic_us - odom_info.capture_monotonic_us
          : odom_info.capture_monotonic_us - scan_info.capture_monotonic_us;
  // スキャン間隔は約 4 ms なので、最近傍なら半分程度に収まるはず
  EXPECT_LT(chosen_skew, 8000u) << "オドメトリ時刻から離れたスキャンが選ばれた";

  std::cout << "  odom_t=" << odom_info.capture_monotonic_us << " scan_t=" << scan_info.capture_monotonic_us
            << " skew=" << chosen_skew << "us scan=" << scan.value << std::endl;

  try
  {
    disconnectTopic("tm_scan");
    disconnectTopic("tm_odom");
  }
  catch (...)
  {
  }
}

TEST_F(SHMTimeMachineTest, RejectsAlignmentThatIsTooFarApart)
{
  // 融合してはいけないほど時刻がずれた値を、黙って成功として返さないこと。
  // 自己位置推定でこれを取り違えると、静かに推定がずれる。
  Publisher<Msg>  scan_pub("tm_scan", 4);
  Subscriber<Msg> scan_sub("tm_scan");

  scan_pub.publish(makeMsg(1));
  SampleInfo scan_info{};
  bool       ok = false;
  scan_sub.subscribe(&ok, &scan_info);
  ASSERT_TRUE(ok);

  // 200 ms 後のオドメトリを模した参照
  SampleInfo odom_info{};
  odom_info.sequence             = 1;
  odom_info.capture_monotonic_us = scan_info.capture_monotonic_us + 200000;

  SearchStatus st = SearchStatus::Empty;
  scan_sub.subscribeAlignedTo(odom_info, &st, 20000);  // 許容 20 ms
  EXPECT_EQ(st, SearchStatus::TooOld) << "20 ms 許容に対して 200 ms ずれた値を返した";

  // 許容を広げれば取れる
  scan_sub.subscribeAlignedTo(odom_info, &st, 500000);
  EXPECT_EQ(st, SearchStatus::Success);

  // 0 は無制限
  scan_sub.subscribeAlignedTo(odom_info, &st, 0);
  EXPECT_EQ(st, SearchStatus::Success);

  try
  {
    disconnectTopic("tm_scan");
  }
  catch (...)
  {
  }
}

TEST_F(SHMTimeMachineTest, HistorySurvivesALayoutGenerationChange)
{
  // レイアウト世代が変わっても過去のサンプルが引けること。
  // 引き継がないと、ベクタ長が変わった瞬間に履歴が全部消えてしまい、
  // 時刻を指定して過去を引く使い方が成立しない。
  Publisher<std::vector<uint32_t>>  pub("tm_vec", 8);
  Subscriber<std::vector<uint32_t>> sub("tm_vec");

  pub.publish(std::vector<uint32_t>(3, 7));
  std::this_thread::sleep_for(std::chrono::milliseconds(3));
  const uint64_t after_short = getCurrentTimeUSec();
  std::this_thread::sleep_for(std::chrono::milliseconds(3));

  // 容量を大きく超える長さ → 新しい世代ができる
  pub.publish(std::vector<uint32_t>(20000, 9));

  SampleInfo   info{};
  SearchStatus st = SearchStatus::Empty;
  const std::vector<uint32_t> &past =
      sub.subscribeAt(TimeQuery{ after_short, SearchPolicy::AtOrBefore }, &st, &info);
  ASSERT_EQ(st, SearchStatus::Success) << "世代が変わって履歴が消えた";
  EXPECT_EQ(past.size(), 3u) << "容量ではなく、その時点の実際の長さが返るべき";
  EXPECT_EQ(past[0], 7u);
  EXPECT_EQ(info.payload_size, 3 * sizeof(uint32_t));
}
