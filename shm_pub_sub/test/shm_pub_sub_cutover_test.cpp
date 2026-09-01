//! @file shm_pub_sub_cutover_test.cpp
//! @brief 世代切替と publish の競合を**決定的に**検査する（R04-F24）
//!
//! R04 のレビューで、cutover 三点セット
//!   - R02-F05  scalar の publish 後の世代確認
//!   - R03-F01(b) 切替直後の取りこぼし回収
//!   - R03-F01(c) vector の publish 後の世代確認
//! を丸ごと取り消しても全テストが緑のままであることが実証された。
//! 外から叩くだけでは狙った順序で競合を起こせないためである。
//!
//! ここでは publish がスロットを確保した後・commit する直前にフックを差し込み、
//! その中で**別の Publisher に世代を進めさせる**。こうすると
//! 「旧世代へ commit する writer」が確実に作れる。
//!
//! 検査する契約は 1 つ:
//!   **publish が例外を投げずに戻ったなら、そのサンプルは購読者から見えること。**

#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace
{
struct Payload
{
  uint32_t value;
  uint32_t filler[16];
};

//! root の世代タグ（世代 + ノンス）
uint64_t
generationTagOfTopic(const std::string &topic)
{
  SharedMemoryPosix shm(topic, O_RDWR, DEFAULT_PERM);
  if (!shm.connect())
  {
    return 0;
  }
  return reinterpret_cast<const ShmHeader *>(shm.getPtr())->latest_generation.load();
}
}  // namespace

class SHMCutoverTest : public ::testing::Test
{
protected:
  void SetUp() override { cleanup(); }
  void TearDown() override
  {
    test_hooks::before_commit = nullptr;
    cleanup();
  }
  void cleanup()
  {
    for (const char *t : { "cut_scalar", "cut_vector" })
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
// vector: publish の最中に世代が進んでも、そのサンプルは購読者から見える
//
// 取り消すと落ちる修正: R03-F01(c)（vector の publish 後の世代確認と再発行）
// -----------------------------------------------------------------------------
TEST_F(SHMCutoverTest, AVectorSampleSurvivesAGenerationCutoverInTheMiddleOfPublishing)
{
  const std::string topic = "cut_vector";

  Publisher<std::vector<uint32_t>>  pub(topic, 8);
  Subscriber<std::vector<uint32_t>> sub(topic);
  sub.setDataExpiryTime_us(0);

  pub.publish(std::vector<uint32_t>(4, 1));
  {
    bool ok = false;
    ASSERT_TRUE((sub.subscribe(&ok), ok)) << "前提: 切替前は読めること";
  }

  const uint64_t before_tag = generationTagOfTopic(topic);

  // スロットを確保した後・commit する直前に、別の Publisher で世代を進める。
  // これで pub は「旧世代へ commit する writer」になる。
  // フックは再発行のたび、また grower 自身の publish でも呼ばれるので、
  // 「一度だけ世代を進める」ようにする。
  bool cutover_done = false;
  test_hooks::before_commit = [&] {
    if (cutover_done)
    {
      return;
    }
    cutover_done = true;
    Publisher<std::vector<uint32_t>> grower(topic, 8);
    grower.publish(std::vector<uint32_t>(40000, 9));  // 容量が足りず世代が進む
  };

  ASSERT_NO_THROW(pub.publish(std::vector<uint32_t>(4, 7)));
  test_hooks::before_commit = nullptr;

  ASSERT_TRUE(cutover_done) << "前提: フックが呼ばれていない";
  ASSERT_NE(generationTagOfTopic(topic), before_tag) << "前提: 世代が進んでいない";

  // publish は成功したのだから、購読者から見えなければならない
  bool                         ok = false;
  const std::vector<uint32_t> &v  = sub.subscribe(&ok);
  ASSERT_TRUE(ok) << "切替を跨いだ publish が誰にも読まれない";
  ASSERT_EQ(v.size(), 4u) << "旧世代の値が新世代へ渡っていない";
  EXPECT_EQ(v[0], 7u) << "publish したはずの値が見えない";
}

// -----------------------------------------------------------------------------
// scalar: 同じことを scalar でも検査する
//
// 取り消すと落ちる修正: R02-F05（scalar の publish 後の世代確認）
//
// scalar のトピックは容量が固定なので普通は世代が進まない。ここでは同じ
// トピックに vector の Publisher を繋げず、代わりに buf_num を増やす要求で
// 世代を進める（レイアウトは「増やすだけ」で収束する）。
// -----------------------------------------------------------------------------
TEST_F(SHMCutoverTest, AScalarSampleSurvivesAGenerationCutoverInTheMiddleOfPublishing)
{
  const std::string topic = "cut_scalar";

  Publisher<Payload>  pub(topic, 4);
  Subscriber<Payload> sub(topic);
  sub.setDataExpiryTime_us(0);

  pub.publish(Payload{ 1, {} });
  {
    bool ok = false;
    ASSERT_TRUE((sub.subscribe(&ok), ok)) << "前提: 切替前は読めること";
  }

  const uint64_t before_tag = generationTagOfTopic(topic);

  bool cutover_done = false;
  test_hooks::before_commit = [&] {
    if (cutover_done)
    {
      return;
    }
    cutover_done = true;
    // buf_num をもっと要求する Publisher が繋ぐと、レイアウトが足りず世代が進む
    Publisher<Payload> grower(topic, 32);
    grower.publish(Payload{ 9, {} });
  };

  ASSERT_NO_THROW(pub.publish(Payload{ 7, {} }));
  test_hooks::before_commit = nullptr;

  ASSERT_TRUE(cutover_done) << "前提: フックが呼ばれていない";
  ASSERT_NE(generationTagOfTopic(topic), before_tag) << "前提: 世代が進んでいない";

  bool           ok = false;
  const Payload &v  = sub.subscribe(&ok);
  ASSERT_TRUE(ok) << "切替を跨いだ publish が誰にも読まれない";
  EXPECT_EQ(v.value, 7u) << "publish したはずの値が見えない";
}

// -----------------------------------------------------------------------------
// 切替を跨いでも発行番号は重複しない
//
// 取り消すと落ちる修正: R03-F01(a)（採番元の root 一元化）
// -----------------------------------------------------------------------------
TEST_F(SHMCutoverTest, SequenceNumbersStayUniqueAcrossACutoverThatHappensDuringPublish)
{
  const std::string topic = "cut_vector";

  Publisher<std::vector<uint32_t>>  pub(topic, 8);
  Subscriber<std::vector<uint32_t>> sub(topic);
  sub.setDataExpiryTime_us(0);

  pub.publish(std::vector<uint32_t>(4, 1));
  SampleInfo before{};
  bool       ok = false;
  sub.subscribe(&ok, &before);
  ASSERT_TRUE(ok);

  bool cutover_done = false;
  test_hooks::before_commit = [&] {
    if (cutover_done)
    {
      return;
    }
    cutover_done = true;
    Publisher<std::vector<uint32_t>> grower(topic, 8);
    grower.publish(std::vector<uint32_t>(40000, 9));
  };

  ASSERT_NO_THROW(pub.publish(std::vector<uint32_t>(4, 7)));
  test_hooks::before_commit = nullptr;

  SampleInfo after{};
  sub.subscribe(&ok, &after);
  ASSERT_TRUE(ok);
  EXPECT_GT(after.sequence, before.sequence) << "切替を跨いで発行番号が巻き戻った";
}
