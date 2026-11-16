/**
 * @file shm_pub_sub_notify_test.cpp
 * @brief publish の通知（futex）で待つ waitForNotify() / TopicWatcher のテスト（ABI 4.1）
 */
#include <gtest/gtest.h>

#include <chrono>
#include <ctime>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace
{

uint64_t
processCpuUs()
{
  timespec ts;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000ULL;
}

class SHMNotifyTest : public ::testing::Test
{
protected:
  void
  SetUp() override
  {
    name_ = "notify_test_" + std::to_string(getpid()) + "_" +
            ::testing::UnitTest::GetInstance()->current_test_info()->name();
  }
  void
  TearDown() override
  {
    ShmTopic::removeAllGenerations(name_);
  }
  std::string name_;
};

}  // namespace

// 別プロセスの publish で起こされ、待っている間は CPU を使わない
TEST_F(SHMNotifyTest, WakesOnPublishFromAnotherProcessWithoutSpinning)
{
  Publisher<int> pub(name_);
  pub.publish(1);
  Subscriber<int> sub(name_);
  bool            ok = false;
  sub.subscribe(&ok);  // 最初の 1 つを読んだことにする
  ASSERT_TRUE(ok);

  constexpr int DELAY_MS = 200;
  const pid_t   child    = fork();
  ASSERT_GE(child, 0);
  if (child == 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(DELAY_MS));
    Publisher<int> child_pub(name_);
    child_pub.publish(2);
    _exit(0);
  }

  const auto     start     = std::chrono::steady_clock::now();
  const uint64_t cpu_start = processCpuUs();
  const bool     updated   = sub.waitForNotify(2000000);
  const uint64_t cpu_used  = processCpuUs() - cpu_start;
  const auto     elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  int status = 0;
  waitpid(child, &status, 0);

  EXPECT_TRUE(updated);
  EXPECT_GE(elapsed_ms, DELAY_MS - 20) << "publish より前に戻った";
  EXPECT_LE(elapsed_ms, DELAY_MS + 100) << "publish から起きるまでが遅い";
  // 1 ms ごとに確認すると 200 ms で 200 回起きる。通知なら起きるのは publish の 1 回と
  // 100 ms ごとの確認し直しだけなので、CPU はほとんど使わない
  EXPECT_LT(cpu_used, 5000u) << "待っている間に CPU を " << cpu_used << " us 使った";
  sub.subscribe(&ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(sub.subscribe(&ok), 2);
}

// publish されなければ、時間切れで false を返す
TEST_F(SHMNotifyTest, TimesOutWithoutPublish)
{
  Publisher<int> pub(name_);
  pub.publish(1);
  Subscriber<int> sub(name_);
  bool            ok = false;
  sub.subscribe(&ok);

  const auto start = std::chrono::steady_clock::now();
  EXPECT_FALSE(sub.waitForNotify(150000));
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_GE(elapsed_ms, 140);
  EXPECT_LE(elapsed_ms, 300);
}

// すでに新しいデータがあれば待たずに戻る（読んだことにはしない）
TEST_F(SHMNotifyTest, ReturnsImmediatelyWhenAlreadyUpdated)
{
  Publisher<int>  pub(name_);
  Subscriber<int> sub(name_);
  pub.publish(7);
  const auto start = std::chrono::steady_clock::now();
  EXPECT_TRUE(sub.waitForNotify(1000000));
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(),
            10);
  EXPECT_TRUE(sub.waitForNotify(0)) << "waitForNotify は読んだことにしない";
}

// 型を知らずに待つ TopicWatcher は、待てたらその publish を読んだことにする
TEST_F(SHMNotifyTest, TopicWatcherMarksTheUpdateAsRead)
{
  Publisher<double> pub(name_);
  pub.publish(1.0);
  TopicWatcher watcher(name_);
  EXPECT_TRUE(watcher.waitForUpdate(0));
  EXPECT_FALSE(watcher.waitForUpdate(0)) << "同じ publish でもう一度 true になった";

  std::thread publisher([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pub.publish(2.0);
  });
  EXPECT_TRUE(watcher.waitForUpdate(1000000));
  publisher.join();
}

// トピックがまだ無くても待てて、後から publish されたら起きる
TEST_F(SHMNotifyTest, WaitsForATopicThatDoesNotExistYet)
{
  Subscriber<int> sub(name_);
  std::thread     publisher([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Publisher<int> pub(name_);
    pub.publish(3);
  });
  EXPECT_TRUE(sub.waitForNotify(1000000));
  publisher.join();
}

// 容量が増えて新しい世代のセグメントに移っても、root で通知するので起こされる
TEST_F(SHMNotifyTest, WakesAcrossALayoutGeneration)
{
  Publisher<std::vector<uint8_t>> pub(name_);
  pub.publish(std::vector<uint8_t>(16, 1));
  Subscriber<std::vector<uint8_t>> sub(name_);
  bool                             ok = false;
  sub.subscribe(&ok);
  ASSERT_TRUE(ok);

  std::thread publisher([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    pub.publish(std::vector<uint8_t>(64 * 1024, 2));  // 容量が足りず新しい世代になる
  });
  const auto start = std::chrono::steady_clock::now();
  EXPECT_TRUE(sub.waitForNotify(1000000));
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(),
            150);
  publisher.join();
  const auto &v = sub.subscribe(&ok);
  EXPECT_TRUE(ok);
  EXPECT_EQ(v.size(), 64u * 1024u);
}
