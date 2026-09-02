//! @file shm_pub_sub_conformance.hpp
//! @brief 5 つの特殊化が同じ振る舞いをすることを確かめる適合性スイート
//!
//! ## なぜ要るか
//!
//! `Publisher<T>` / `Subscriber<T>` には 5 つの実装がある。
//!
//!   1. scalar        shm_pub_sub.hpp
//!   2. vector        shm_pub_sub_vector.hpp
//!   3. cv::Mat       react_cv        （別リポジトリ）
//!   4. Lidar2dScan   sensor_daemons  （別リポジトリ）
//!   5. PointCloud2D  sensor_daemons  （別リポジトリ）
//!
//! このうち **型に一切依存しない約 150 行**（subscribeAt / subscribeAlignedTo /
//! waitFor / getRetentionWindow / setDataExpiryTime_us / subscribe のリトライ）が
//! 5 箇所にバイト等価でコピーされている。R01〜R05 の 45 コミットは、これを毎回
//! 手で 5 回適用する運用だった。
//!
//! 実際に漏れた。R04-F12（capture 時刻を publish の入口で一度だけ採る）は
//! 本体 2 つにしか入っておらず、**世代切替が最も頻繁に起きる外部 3 つ**に
//! 届いていなかった。60,000 点のスキャンで実測した遅れは 379us で、
//! subscribeAlignedTo() はこの値でずれを測るため融合精度に直接効いていた。
//!
//! このヘッダは「5 つが守るべき契約」を 1 箇所に書いたものである。
//! 各特殊化は Traits を 20 行ほど書いて実体化するだけでよい。
//! 次に誰かが片方だけ直したら、もう片方の CI が落ちる。
//!
//! ## 使い方
//!
//! CMake からは、ヘッダオンリーの INTERFACE ターゲットを link するだけでよい。
//!
//! @code
//! target_link_libraries(<自分のテスト> PRIVATE shm_pub_sub_conformance)
//! @endcode
//!
//! **`using namespace irlab::shm;` を書いてから実体化すること。**
//! `INSTANTIATE_TYPED_TEST_SUITE_P` はスイート名を識別子に連結するマクロなので、
//! `irlab::shm::SHMSpecializationConformance` のような修飾名を渡すと
//! `'gtest_suite_irlab' was not declared` という分かりにくいエラーになる。
//!
//! @code
//! #include "shm_pub_sub_conformance.hpp"
//! #include "shm_pub_sub_my_type.hpp"
//!
//! using namespace irlab::shm;
//!
//! struct MyTypeTraits
//! {
//!   using Payload = MyType;
//!   static const char *name() { return "MyType"; }
//!   //! 小さいペイロード。seed で内容が変わること
//!   static Payload makeSmall(uint32_t seed);
//!   //! 大きいペイロード。serialize / memcpy に実時間がかかるほど良い。
//!   //! 固定長でこれ以上大きくできない型は makeSmall と同じで構わない
//!   //! （その場合 capture 時刻の検査は「判定不能」として報告される）
//!   static Payload makeLarge(uint32_t seed);
//!   //! makeSmall / makeLarge が seed から作った内容を復元する
//!   static uint32_t seedOf(const Payload &p);
//!   //! 中身が等しいか
//!   static bool equals(const Payload &a, const Payload &b);
//! };
//!
//! INSTANTIATE_TYPED_TEST_SUITE_P(MyType, SHMSpecializationConformance, MyTypeTraits);
//! @endcode
//!
//! トピック名は `conf_<name()>_<テスト名>` で自動的に分けるので、
//! 複数の特殊化を同じプロセスで実体化しても衝突しない。

#ifndef SHM_PUB_SUB_CONFORMANCE_HPP
#define SHM_PUB_SUB_CONFORMANCE_HPP

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"

namespace irlab
{
namespace shm
{
namespace conformance
{

//! capture 時刻の検査が意味を持つために必要な publish() の所要時間[usec]。
//! これを下回ると「入口で打った」と「commit で打った」の差がノイズに埋もれる。
constexpr uint64_t DISCRIMINATION_THRESHOLD_US = 30;

}  // namespace conformance

template <typename Traits>
class SHMSpecializationConformance : public ::testing::Test
{
protected:
  using Payload = typename Traits::Payload;

  //! テストごとに別のトピック名を使う。特殊化を跨いでも衝突しない。
  std::string topicFor(const char *test_name) const
  {
    return std::string("conf_") + Traits::name() + "_" + test_name;
  }

  void TearDown() override
  {
    for (const auto &t : used_topics_)
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

  //! 後始末の対象に登録しつつトピック名を返す
  std::string topic(const char *test_name)
  {
    const std::string t = topicFor(test_name);
    used_topics_.push_back(t);
    return t;
  }

private:
  std::vector<std::string> used_topics_;
};

TYPED_TEST_SUITE_P(SHMSpecializationConformance);

// ---------------------------------------------------------------------------
// 基本: publish した内容がそのまま読めること
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, RoundTripsThePayload)
{
  using Traits = TypeParam;
  const std::string t = this->topic("roundtrip");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  const auto sent = Traits::makeSmall(11);
  pub.publish(sent);

  bool ok = false;
  const auto &got = sub.subscribe(&ok);
  ASSERT_TRUE(ok) << Traits::name() << ": publish 直後に読めない";
  EXPECT_TRUE(Traits::equals(sent, got)) << Traits::name() << ": 読めた内容が publish した内容と違う";
  EXPECT_EQ(Traits::seedOf(got), 11u) << Traits::name() << ": seed を復元できない";
}

// ---------------------------------------------------------------------------
// R04-F12: capture 時刻は publish() の入口で**一度だけ**採ること
//
// commitBuffer() に渡さないと commit 時点で打たれる。すると
//   (a) serialize / memcpy にかかった時間だけ、刻まれる時刻が系統的に遅れる
//   (b) 世代切替で再試行するたびに採り直され、同じ測定が別時刻になる
// この修正は本体 2 つにしか入っておらず、世代切替が最も頻繁に起きる
// 外部 3 つに届いていなかった。
//
// 入口で打ったなら capture は publish() の**開始側**に寄り、commit で打ったなら
// **終了側**に寄る。所要時間で正規化して見るので、機械の速さに依存しない。
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, CaptureTimeIsStampedAtPublishEntry)
{
  using Traits = TypeParam;
  const std::string t = this->topic("capture_at_entry");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  const auto large = Traits::makeLarge(7);

  // 1 回目は世代確保などの初期費用が乗るので捨てる
  pub.publish(large);
  {
    bool warmup_ok = false;
    sub.subscribe(&warmup_ok);
  }

  uint64_t best_elapsed = 0;
  uint64_t lag_at_best  = 0;
  for (int attempt = 0; attempt < 5; ++attempt)
  {
    const uint64_t before = getCurrentTimeUSec();
    pub.publish(large);
    const uint64_t after = getCurrentTimeUSec();

    bool       ok = false;
    SampleInfo info{};
    sub.subscribe(&ok, &info);
    ASSERT_TRUE(ok) << Traits::name() << ": publish 直後に読めない";
    ASSERT_GE(info.capture_monotonic_us, before)
        << Traits::name() << ": capture 時刻が publish() を呼ぶ前を指している";
    ASSERT_LE(info.capture_monotonic_us, after)
        << Traits::name() << ": capture 時刻が publish() から戻った後を指している";

    // 所要時間が長い回ほど、入口と commit の差がはっきり出る
    const uint64_t elapsed = after - before;
    if (elapsed >= best_elapsed)
    {
      best_elapsed = elapsed;
      lag_at_best  = info.capture_monotonic_us - before;
    }
  }

  if (best_elapsed < conformance::DISCRIMINATION_THRESHOLD_US)
  {
    // publish が速すぎて「入口」と「commit」を区別できない。
    // 固定長の小さな型では正常な状況なので、失敗にはしない。
    GTEST_SKIP() << Traits::name() << ": publish が " << best_elapsed
                 << "us しかかからず、capture 時刻の位置を判定できない"
                    "（makeLarge() をより大きくすると検査が効くようになる）";
  }

  EXPECT_LE(lag_at_best, best_elapsed / 2)
      << Traits::name() << ": capture 時刻が publish() の終了側に寄っている。"
      << "commitBuffer() に capture 時刻を渡さず、commit 時点で打っている疑いがある"
         "（R04-F12）。publish 所要 "
      << best_elapsed << "us に対し、入口からの遅れ " << lag_at_best << "us";
}

// ---------------------------------------------------------------------------
// R02-F03: payload と SampleInfo は必ず同じサンプルを指すこと
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, PayloadAndSampleInfoDescribeTheSameSample)
{
  using Traits = TypeParam;
  const std::string t = this->topic("pair");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  for (uint32_t seed = 1; seed <= 5; ++seed)
  {
    pub.publish(Traits::makeSmall(seed));

    bool       ok = false;
    SampleInfo info{};
    const auto &got = sub.subscribe(&ok, &info);
    ASSERT_TRUE(ok) << Traits::name() << ": seed=" << seed << " を読めない";
    EXPECT_EQ(Traits::seedOf(got), seed) << Traits::name() << ": 別のサンプルが返った";
    EXPECT_NE(info.sequence, 0u) << Traits::name() << ": 成功したのに発行番号が 0";
    EXPECT_NE(info.capture_monotonic_us, 0u) << Traits::name() << ": 成功したのに capture 時刻が 0";
    EXPECT_NE(info.payload_size, 0u) << Traits::name() << ": 成功したのに payload 長が 0";
  }
}

// ---------------------------------------------------------------------------
// 発行番号は単調増加し、再利用されないこと（取りこぼしの判定に使う）
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, SequenceIsMonotonic)
{
  using Traits = TypeParam;
  const std::string t = this->topic("sequence");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  uint64_t previous = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed)
  {
    pub.publish(Traits::makeSmall(seed));
    bool       ok = false;
    SampleInfo info{};
    sub.subscribe(&ok, &info);
    ASSERT_TRUE(ok) << Traits::name() << ": seed=" << seed << " を読めない";
    EXPECT_GT(info.sequence, previous) << Traits::name() << ": 発行番号が増えていない";
    previous = info.sequence;
  }
}

// ---------------------------------------------------------------------------
// subscribe() の返り値は参照なので、次の 1 回までは生きていること
//
// 内部はダブルバッファで、失敗した subscribe() が直前に返した値を壊さない
// ようになっている。この契約が 5 つで揃っていないと、ある型では動いて
// 別の型では黙って壊れる、という状況になる。
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, TheReturnedReferenceSurvivesOneMoreSubscribe)
{
  using Traits = TypeParam;
  const std::string t = this->topic("double_buffer");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  pub.publish(Traits::makeSmall(101));
  bool ok = false;
  const auto &held = sub.subscribe(&ok);
  ASSERT_TRUE(ok) << Traits::name();
  ASSERT_EQ(Traits::seedOf(held), 101u) << Traits::name();

  // もう 1 回読んでも、前回返した参照はまだ有効
  pub.publish(Traits::makeSmall(202));
  bool ok2 = false;
  sub.subscribe(&ok2);
  ASSERT_TRUE(ok2) << Traits::name();
  EXPECT_EQ(Traits::seedOf(held), 101u)
      << Traits::name() << ": 次の subscribe() で直前の返り値が壊れた（ダブルバッファが無い）";
}

// ---------------------------------------------------------------------------
// 期限切れの扱い
//
// 既定は 2 秒。0 を渡すと無効になる。これは「publish しているのに受信できない」
// の最有力原因なので、5 つで揃っていなければならない。
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, ExpiryIsHonouredAndZeroDisablesIt)
{
  using Traits = TypeParam;
  const std::string t = this->topic("expiry");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);

  pub.publish(Traits::makeSmall(5));

  // 極端に短い期限にすれば、待たずに期限切れを作れる
  sub.setDataExpiryTime_us(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  bool expired_ok = true;
  sub.subscribe(&expired_ok);
  EXPECT_FALSE(expired_ok) << Traits::name() << ": 期限を過ぎたデータを成功として返した";

  // 0 は「期限なし」
  sub.setDataExpiryTime_us(0);
  bool ok = false;
  const auto &got = sub.subscribe(&ok);
  EXPECT_TRUE(ok) << Traits::name() << ": 期限を無効にしても読めない";
  if (ok)
  {
    EXPECT_EQ(Traits::seedOf(got), 5u) << Traits::name();
  }
}

// ---------------------------------------------------------------------------
// waitFor(): 更新が無ければタイムアウトし、あれば返ること
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, WaitForTimesOutWithoutAPublish)
{
  using Traits = TypeParam;
  const std::string t = this->topic("waitfor");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  pub.publish(Traits::makeSmall(1));
  bool ok = false;
  sub.subscribe(&ok);  // 既読にする
  ASSERT_TRUE(ok) << Traits::name();

  const uint64_t before = getCurrentTimeUSec();
  const bool     updated = sub.waitFor(50000);  // 50ms
  const uint64_t elapsed = getCurrentTimeUSec() - before;

  EXPECT_FALSE(updated) << Traits::name() << ": 更新が無いのに waitFor が真を返した";
  EXPECT_GE(elapsed, 40000u) << Traits::name() << ": waitFor が指定より早く戻った（" << elapsed << "us）";

  // 新しい publish があれば真を返す
  pub.publish(Traits::makeSmall(2));
  EXPECT_TRUE(sub.waitFor(500000)) << Traits::name() << ": publish があったのに waitFor が偽を返した";
}

// ---------------------------------------------------------------------------
// getRetentionWindow(): publish した範囲を含むこと
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, RetentionWindowCoversWhatWasPublished)
{
  using Traits = TypeParam;
  const std::string t = this->topic("retention");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  // 接続前は空
  const RetentionWindow empty = sub.getRetentionWindow();
  EXPECT_EQ(empty.count, 0u) << Traits::name() << ": publish 前なのに保持範囲が空でない";

  uint64_t first_seq = 0, last_seq = 0;
  for (uint32_t seed = 1; seed <= 3; ++seed)
  {
    pub.publish(Traits::makeSmall(seed));
    bool       ok = false;
    SampleInfo info{};
    sub.subscribe(&ok, &info);
    ASSERT_TRUE(ok) << Traits::name();
    if (seed == 1)
    {
      first_seq = info.sequence;
    }
    last_seq = info.sequence;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  const RetentionWindow w = sub.getRetentionWindow();
  EXPECT_EQ(w.count, 3u) << Traits::name() << ": 保持しているサンプル数が合わない";
  EXPECT_LE(w.oldest_sequence, first_seq) << Traits::name();
  EXPECT_GE(w.newest_sequence, last_seq) << Traits::name();
  EXPECT_LE(w.oldest_monotonic_us, w.newest_monotonic_us) << Traits::name() << ": 保持範囲の時刻が逆転している";
}

// ---------------------------------------------------------------------------
// subscribeAt(): 記録した時刻でそのサンプルを引けること
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, SubscribeAtFindsAKnownSample)
{
  using Traits = TypeParam;
  const std::string t = this->topic("subscribe_at");

  Publisher<typename Traits::Payload>  pub(t, 8);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  SampleInfo wanted{};
  for (uint32_t seed = 1; seed <= 4; ++seed)
  {
    pub.publish(Traits::makeSmall(seed));
    bool       ok = false;
    SampleInfo info{};
    sub.subscribe(&ok, &info);
    ASSERT_TRUE(ok) << Traits::name();
    if (seed == 2)
    {
      wanted = info;  // 2 番目を後で引く
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  SearchStatus st = SearchStatus::Empty;
  SampleInfo   found{};
  const auto  &got = sub.subscribeAt(TimeQuery{ wanted.capture_monotonic_us, SearchPolicy::Nearest }, &st, &found);
  ASSERT_EQ(st, SearchStatus::Success) << Traits::name() << ": 記録した時刻でサンプルを引けない";
  EXPECT_EQ(found.sequence, wanted.sequence) << Traits::name() << ": 別のサンプルが返った";
  EXPECT_EQ(Traits::seedOf(got), 2u) << Traits::name() << ": payload が SampleInfo と食い違う";

  // 方針に合うサンプルが無いときのステータスは、**目標時刻が保持範囲に対して
  // どうか**を表す（サンプル側から見た向きではない）。
  //   AtOrAfter で未来を指す  → その時刻はまだ publish されていない → TooNew
  //   AtOrBefore で過去を指す → その時刻は既に上書きされている     → TooOld
  constexpr uint64_t ONE_HOUR_US = 3600ull * 1000000ull;

  SearchStatus future = SearchStatus::Success;
  sub.subscribeAt(TimeQuery{ found.capture_monotonic_us + ONE_HOUR_US, SearchPolicy::AtOrAfter }, &future);
  EXPECT_EQ(future, SearchStatus::TooNew)
      << Traits::name() << ": 1 時間先を AtOrAfter で引いたのに TooNew にならない";

  SearchStatus past = SearchStatus::Success;
  const uint64_t long_ago =
      (found.capture_monotonic_us > ONE_HOUR_US) ? (found.capture_monotonic_us - ONE_HOUR_US) : 0;
  sub.subscribeAt(TimeQuery{ long_ago, SearchPolicy::AtOrBefore }, &past);
  EXPECT_EQ(past, SearchStatus::TooOld)
      << Traits::name() << ": 1 時間前を AtOrBefore で引いたのに TooOld にならない";
}

// ---------------------------------------------------------------------------
// R04-F14: subscribeAlignedTo() のずれ判定
//
// Nearest は有効なサンプルがあれば必ず「最も近いもの」を返すので、
// 上限は呼び出し側が示す。0 は無制限で、これも 5 つで揃っていなければならない。
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, SubscribeAlignedToHonoursMaxSkew)
{
  using Traits = TypeParam;
  const std::string t = this->topic("aligned");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);

  pub.publish(Traits::makeSmall(1));
  bool       ok = false;
  SampleInfo published{};
  sub.subscribe(&ok, &published);
  ASSERT_TRUE(ok) << Traits::name();

  // 実際のサンプルより 500ms 新しい時刻を基準にする
  SampleInfo reference           = published;
  reference.capture_monotonic_us = published.capture_monotonic_us + 500000;

  SearchStatus st = SearchStatus::Success;
  sub.subscribeAlignedTo(reference, &st, 10000);  // 許容 10ms
  EXPECT_EQ(st, SearchStatus::TooOld) << Traits::name() << ": 500ms ずれているのに整列済みとして返した";

  st = SearchStatus::Empty;
  sub.subscribeAlignedTo(reference, &st, 1000000);  // 許容 1s
  EXPECT_EQ(st, SearchStatus::Success) << Traits::name() << ": 上限を広げても取れない";

  // 0 は無制限
  st = SearchStatus::Empty;
  sub.subscribeAlignedTo(reference, &st, 0);
  EXPECT_EQ(st, SearchStatus::Success) << Traits::name() << ": max_skew_us=0 が無制限になっていない";
}

// ---------------------------------------------------------------------------
// R04-F14: 失敗した subscribe() の SampleInfo は全ゼロ。
// それを基準に渡すと時刻 0 に対する検索になるので、弾かれること。
// ---------------------------------------------------------------------------
TYPED_TEST_P(SHMSpecializationConformance, SubscribeAlignedToRejectsAnInvalidReference)
{
  using Traits = TypeParam;
  const std::string t = this->topic("invalid_ref");

  Publisher<typename Traits::Payload>  pub(t, 4);
  Subscriber<typename Traits::Payload> sub(t);
  sub.setDataExpiryTime_us(0);
  pub.publish(Traits::makeSmall(1));

  SampleInfo   all_zero{};  // 失敗した subscribe() が返すもの
  SearchStatus st = SearchStatus::Success;
  sub.subscribeAlignedTo(all_zero, &st, 10000);
  EXPECT_EQ(st, SearchStatus::InvalidReference)
      << Traits::name() << ": 全ゼロの SampleInfo を基準として受け付けた（時刻 0 に対する検索になる）";
}

REGISTER_TYPED_TEST_SUITE_P(SHMSpecializationConformance,
                            RoundTripsThePayload,
                            CaptureTimeIsStampedAtPublishEntry,
                            PayloadAndSampleInfoDescribeTheSameSample,
                            SequenceIsMonotonic,
                            TheReturnedReferenceSurvivesOneMoreSubscribe,
                            ExpiryIsHonouredAndZeroDisablesIt,
                            WaitForTimesOutWithoutAPublish,
                            RetentionWindowCoversWhatWasPublished,
                            SubscribeAtFindsAKnownSample,
                            SubscribeAlignedToHonoursMaxSkew,
                            SubscribeAlignedToRejectsAnInvalidReference);

}  // namespace shm
}  // namespace irlab

#endif  // SHM_PUB_SUB_CONFORMANCE_HPP
