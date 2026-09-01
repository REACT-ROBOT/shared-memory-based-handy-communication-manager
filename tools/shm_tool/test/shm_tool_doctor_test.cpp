//! @file shm_tool_doctor_test.cpp
//! @brief `shm_tool doctor` の回帰テスト（R04 / 制約 D）
//!
//! ABI 4 への移行で運用者が最初に必要とするのは「どのセグメントが古いか」で、
//! 従来の `list` は `ls -l /dev/shm` を整形するだけでヘッダを読まなかった（R04-F18）。

#include <gtest/gtest.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>

#include "shm_pub_sub.hpp"

using namespace irlab::shm;

namespace
{
struct ToolResult
{
  std::string output;
  int         exit_code = -1;
};

//! shm_tool を実行して標準出力と終了コードを得る
ToolResult
runTool(const std::string &args)
{
  ToolResult        result;
  const char       *path    = std::getenv("SHM_TOOL_PATH");
  const std::string command = std::string(path != nullptr ? path : "./shm_tool") + " " + args + " 2>&1";

  FILE *pipe = popen(command.c_str(), "r");
  if (pipe == nullptr)
  {
    return result;
  }
  std::array<char, 512> buffer{};
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
  {
    result.output += buffer.data();
  }
  const int status = pclose(pipe);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}
}  // namespace

class SHMToolDoctorTest : public ::testing::Test
{
protected:
  void SetUp() override { cleanup(); }
  void TearDown() override { cleanup(); }
  void cleanup()
  {
    for (const char *t : { "doctor_ok", "doctor_abi" })
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
// 正常なトピックだけなら「対処が必要 0 件」で終了コード 0
// -----------------------------------------------------------------------------
TEST_F(SHMToolDoctorTest, HealthySegmentsAreReportedAsOk)
{
  Publisher<int> pub("doctor_ok", 3);
  pub.publish(1);

  // 他のテストが残したセグメントに影響されないよう、トピックを指定して見る
  const ToolResult r = runTool("doctor doctor_ok");
  ASSERT_NE(r.exit_code, -1) << "shm_tool を実行できない。SHM_TOOL_PATH を確認すること";

  EXPECT_NE(r.output.find("doctor_ok"), std::string::npos) << r.output;
  EXPECT_NE(r.output.find("対処が必要 0 件"), std::string::npos) << r.output;
  EXPECT_EQ(r.exit_code, 0) << r.output;
}

// -----------------------------------------------------------------------------
// 古い ABI のセグメントを見つけ、終了コードで知らせる
//
// これが ABI 4 への移行で最初に必要になる情報である。
// -----------------------------------------------------------------------------
TEST_F(SHMToolDoctorTest, AnOldAbiSegmentIsReportedAndChangesTheExitCode)
{
  {
    Publisher<int> pub("doctor_abi", 3);
    pub.publish(1);
  }
  {
    SharedMemoryPosix shm("doctor_abi", O_RDWR, static_cast<PERM>(0));
    ASSERT_TRUE(shm.connect());
    reinterpret_cast<ShmHeader *>(shm.getPtr())->abi_major = RingBuffer::ABI_MAJOR - 1;
  }

  const ToolResult r = runTool("doctor doctor_abi");
  ASSERT_NE(r.exit_code, -1);

  EXPECT_NE(r.output.find("doctor_abi"), std::string::npos) << r.output;
  EXPECT_NE(r.output.find("ABI"), std::string::npos) << "古い ABI を報告していない\n" << r.output;
  EXPECT_NE(r.output.find("shm_tool remove"), std::string::npos) << "復旧手順を案内していない\n" << r.output;
  EXPECT_EQ(r.exit_code, 1) << "問題があるのに終了コードが 0\n" << r.output;
}

// -----------------------------------------------------------------------------
// 書式が未宣言でも「動作はしている」ので終了コードは 0（注記に留める）
//
// 移行の途中で全トピックが赤くなると、本当に対処が要るものが埋もれる。
// -----------------------------------------------------------------------------
TEST_F(SHMToolDoctorTest, AnUndeclaredFormatIsANoteNotAProblem)
{
  Publisher<int> pub("doctor_ok", 3);
  pub.publish(1);

  const ToolResult r = runTool("doctor doctor_ok");
  ASSERT_NE(r.exit_code, -1);

  EXPECT_NE(r.output.find("未宣言"), std::string::npos) << "未宣言を表示していない\n" << r.output;
  EXPECT_EQ(r.exit_code, 0) << "注記だけで終了コードが 1 になっている\n" << r.output;
}

// -----------------------------------------------------------------------------
// 存在しないトピックの remove は成功扱いにしない
// -----------------------------------------------------------------------------
TEST_F(SHMToolDoctorTest, RemovingAMissingTopicFails)
{
  const ToolResult r = runTool("remove doctor_no_such_topic");
  ASSERT_NE(r.exit_code, -1);
  EXPECT_EQ(r.exit_code, 1) << r.output;
}
