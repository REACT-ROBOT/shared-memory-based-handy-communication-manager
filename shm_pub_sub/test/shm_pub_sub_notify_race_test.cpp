/**
 * @file shm_pub_sub_notify_race_test.cpp
 * @brief publish の通知（futex）の起こし損ねの回帰テスト
 *
 * waitForNotify() は「通知の値を読む → follow() → 更新を確認 → 値が変わっていなければ寝る」で待つ。
 * 通知の値を follow() の後に読んでいたときは、その間に「世代切替＋新世代への commit」が入ると、
 * 旧世代のリングで更新なしと判定したうえ、進んだ後の値で寝てしまい、次の publish か
 * 確認し直し（100 ms）まで起きなかった。
 *
 * この窓は外から publish するだけでは狙えない（確率試験 300 回でも当たらなかった）ので、
 * SHM_ENABLE_TEST_HOOKS のフックで待ちループの各点に publish を差し込んで決定的に検査する。
 */
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <sys/mman.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

#ifndef SHM_ENABLE_TEST_HOOKS
#error "shm_pub_sub_notify_race_test needs SHM_ENABLE_TEST_HOOKS (BUILD_TESTS=ON turns it on)"
#endif

using namespace irlab::shm;
using Clock          = std::chrono::steady_clock;
using NotifyWaitPoint = test_hooks::NotifyWaitPoint;

namespace
{

//! 起こし損ねと見なす遅れ。確認し直しは 100 ms なので、それよりずっと短く取る
constexpr long LATE_MS = 10;

uint64_t
nowUs()
{
  return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

const char *
pointName(NotifyWaitPoint p)
{
  switch (p)
  {
    case NotifyWaitPoint::BeforeFollow:
      return "BeforeFollow";
    case NotifyWaitPoint::AfterFollow:
      return "AfterFollow";
    case NotifyWaitPoint::BeforeSleep:
      return "BeforeSleep";
  }
  return "?";
}

class SHMNotifyRaceTest : public ::testing::Test
{
protected:
  void
  TearDown() override
  {
    test_hooks::notify_wait_point         = nullptr;
    test_hooks::notify_read_before_follow = true;
  }

  //! waitForNotify() の待ちループの point で 1 回だけ publish させ、起きるまでの時間を返す
  long
  wakeLatencyMs(NotifyWaitPoint point, bool switch_generation)
  {
    const std::string name = "notify_race_" + std::to_string(getpid()) + "_" + std::to_string(serial_++);
    Publisher<std::vector<uint8_t>> pub(name);
    pub.publish(std::vector<uint8_t>(16, 1));
    Subscriber<std::vector<uint8_t>> sub(name);
    bool                             ok = false;
    sub.subscribe(&ok);  // 最初の 1 つを読んだことにする
    EXPECT_TRUE(ok);

    // 64 KiB は今の容量に収まらないので、新しい世代へ移ってから commit される
    const size_t next_size = switch_generation ? 64 * 1024 : 16;
    bool         fired     = false;
    test_hooks::notify_wait_point = [&](NotifyWaitPoint p) {
      if (!fired && p == point)
      {
        fired = true;
        pub.publish(std::vector<uint8_t>(next_size, 2));
      }
    };
    const auto t0      = Clock::now();
    const bool updated = sub.waitForNotify(1000000);
    const long ms      = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    test_hooks::notify_wait_point = nullptr;

    EXPECT_TRUE(fired) << pointName(point) << " を通らなかった";
    EXPECT_TRUE(updated);
    const auto &v = sub.subscribe(&ok);
    EXPECT_TRUE(ok);
    EXPECT_EQ(v.size(), next_size) << "最新を読めていない";
    ShmTopic::removeAllGenerations(name);
    return ms;
  }

  int serial_ = 0;
};

const NotifyWaitPoint ALL_POINTS[] = { NotifyWaitPoint::BeforeFollow, NotifyWaitPoint::AfterFollow,
                                       NotifyWaitPoint::BeforeSleep };

}  // namespace

// 待ちループのどの点で publish されても、すぐ起きる（同世代・世代切替の両方）
TEST_F(SHMNotifyRaceTest, WakesImmediatelyWhereverThePublishLands)
{
  for (const bool sw : { false, true })
  {
    for (const NotifyWaitPoint point : ALL_POINTS)
    {
      const long ms = wakeLatencyMs(point, sw);
      EXPECT_LT(ms, LATE_MS) << (sw ? "世代切替" : "同世代") << " / " << pointName(point) << " で " << ms
                             << " ms 寝た（起こし損ね）";
    }
  }
}

// 上のテストが本当に窓を突けていることの確認：修正前の順序に戻すと、
// 「世代切替 × follow 直後」だけが確認し直しまで寝る（R04-F24 と同じ考え方）
TEST_F(SHMNotifyRaceTest, OldOrderMissesTheGenerationSwitchAfterFollow)
{
  test_hooks::notify_read_before_follow = false;
  for (const bool sw : { false, true })
  {
    for (const NotifyWaitPoint point : ALL_POINTS)
    {
      const long ms        = wakeLatencyMs(point, sw);
      const bool expect_lost = sw && point == NotifyWaitPoint::AfterFollow;
      if (expect_lost)
      {
        EXPECT_GE(ms, 90) << "修正前の順序なのに起こし損ねを再現できない（テストが窓を突けていない）";
      }
      else
      {
        EXPECT_LT(ms, LATE_MS) << (sw ? "世代切替" : "同世代") << " / " << pointName(point);
      }
    }
  }
}

// 初回にトピックがまだ無い（通知の値を follow 前に読めない）経路でも、publish で起きる
TEST_F(SHMNotifyRaceTest, WakesWhenTheTopicAppearsLater)
{
  const std::string name = "notify_race_absent_" + std::to_string(getpid());
  Subscriber<int>   sub(name);
  std::thread       th([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    Publisher<int> pub(name);
    pub.publish(5);
  });
  const auto t0 = Clock::now();
  EXPECT_TRUE(sub.waitForNotify(1000000));
  const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
  th.join();
  EXPECT_LT(ms, 30 + 30);
  ShmTopic::removeAllGenerations(name);
}

// 別プロセスが、時々容量を増やして世代を進めながら publish を連打しても、起こし損ねない
TEST_F(SHMNotifyRaceTest, NoLostWakeupUnderCrossProcessStressWithGenerationSwitches)
{
  const std::string                name = "notify_race_stress_" + std::to_string(getpid());
  constexpr int                    N    = 20000;
  Publisher<std::vector<uint64_t>> pub0(name);
  pub0.publish(std::vector<uint64_t>(1, 0));
  Subscriber<std::vector<uint64_t>> sub(name);
  bool                              ok = false;
  sub.subscribe(&ok);

  // 1 publish ずつ起床の遅れを測るため、購読側が読み終えるまで子は次を送らない
  auto *turn = static_cast<std::atomic<int> *>(
      mmap(nullptr, sizeof(std::atomic<int>), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
  ASSERT_NE(turn, MAP_FAILED);
  turn->store(0);

  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0)
  {
    Publisher<std::vector<uint64_t>> pub(name);
    unsigned                         r    = 777;
    size_t                           size = 1;
    for (int i = 1; i <= N; ++i)
    {
      while (turn->load() != i - 1)
      {
      }
      // 0〜数十 us ずらして「確認 → 寝る」の間に当てる
      r = r * 1103515245u + 12345u;
      for (volatile int k = 0, spin = (r >> 16) % 400; k < spin; ++k)
      {
      }
      if (i % 200 == 0 && size < 4096)
      {
        size *= 2;  // 容量が足りなくなり、新しい世代へ移る
      }
      std::vector<uint64_t> v(size, 0);
      v[0] = nowUs();
      pub.publish(v);
    }
    _exit(0);
  }

  int      late  = 0;
  uint64_t worst = 0;
  for (int i = 1; i <= N; ++i)
  {
    turn->store(i - 1);
    if (!sub.waitForNotify(2000000))
    {
      ADD_FAILURE() << i << " 回目の publish を 2 秒待っても起きなかった";
      break;
    }
    const uint64_t woke = nowUs();
    const auto    &v    = sub.subscribe(&ok);
    const uint64_t lat  = woke > v[0] ? woke - v[0] : 0;
    worst               = std::max(worst, lat);
    late += (lat > 50000);
  }
  turn->store(N);
  int status = 0;
  waitpid(child, &status, 0);
  munmap(turn, sizeof(std::atomic<int>));
  EXPECT_EQ(late, 0) << "50 ms を超えて遅れた回数（最悪 " << worst << " us）";
  ShmTopic::removeAllGenerations(name);
}
