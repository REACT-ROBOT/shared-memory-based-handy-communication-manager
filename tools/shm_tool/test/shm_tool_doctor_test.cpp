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
#include <vector>
#include <sys/wait.h>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

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
    for (const char *t : { "doctor_ok", "doctor_abi", "doctor_bad", "list_small", "list_wide" })
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
//
// 書式が未宣言でも「動作はしている」ので終了コードは 0 のままである（注記に留める）。
// 移行の途中で全トピックが赤くなると、本当に対処が要るものが埋もれるためで、
// Publisher<int> は書式を宣言していないので同じセットアップで両方を見られる。
//
// NOTE: 以前は AnUndeclaredFormatIsANoteNotAProblem という別テストがあったが、
//       セットアップが 1 文字も違わず（同じトピック名・同じコマンド・同じ
//       終了コードの期待）、検索する文字列だけが違っていた。shm_tool は毎回
//       popen で別プロセスを起こすので、統合して実行を 1 回減らしてある。
// -----------------------------------------------------------------------------
TEST_F(SHMToolDoctorTest, HealthySegmentsAreOkAndAnUndeclaredFormatIsOnlyANote)
{
  Publisher<int> pub("doctor_ok", 3);
  pub.publish(1);

  // 他のテストが残したセグメントに影響されないよう、トピックを指定して見る
  const ToolResult r = runTool("doctor doctor_ok");
  ASSERT_NE(r.exit_code, -1) << "shm_tool を実行できない。SHM_TOOL_PATH を確認すること";

  EXPECT_NE(r.output.find("doctor_ok"), std::string::npos) << r.output;
  EXPECT_NE(r.output.find("対処が必要 0 件"), std::string::npos) << r.output;
  EXPECT_NE(r.output.find("未宣言"), std::string::npos) << "未宣言を表示していない\n" << r.output;
  EXPECT_EQ(r.exit_code, 0) << "注記だけで終了コードが 1 になっている\n" << r.output;
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
// 存在しないトピックの remove は成功扱いにしない
// -----------------------------------------------------------------------------
TEST_F(SHMToolDoctorTest, RemovingAMissingTopicFails)
{
  const ToolResult r = runTool("remove doctor_no_such_topic");
  ASSERT_NE(r.exit_code, -1);
  EXPECT_EQ(r.exit_code, 1) << r.output;
}

// -----------------------------------------------------------------------------
// R05: 壊れたヘッダで doctor 自身が落ちてはならない
//
// doctor はまさに「壊れたセグメントを調べる」ために使うものなので、
// 一番使いたい場面で落ちるのは目的そのものの取りこぼしである。
//
// 以前は自前の緩い検査しかしておらず、
//   - buf_num * slot_size が uint64 で折り返ってマッピング外を読む（SEGV）
//   - slot_offset が 64 バイト境界に載らず非アラインな atomic を読む
//     （x86 では動くが aarch64 では SIGBUS）
//   - ライブラリが接続を拒否する状態を「OK」と報告する
// という 3 つの問題があった。RingBuffer::inspectLayout() を通すことで
// 3 件とも塞いだ。
// -----------------------------------------------------------------------------
TEST_F(SHMToolDoctorTest, CorruptedHeadersAreReportedInsteadOfCrashingTheTool)
{
  struct Poison
  {
    const char *what;
    void (*apply)(ShmHeader *);
  };
  const Poison poisons[] = {
    { "buf_num の乗算が折り返る", [](ShmHeader *h) { h->buf_num = (UINT64_MAX / sizeof(SlotRecord)) + 2; } },
    { "slot_offset の加算が折り返る", [](ShmHeader *h) { h->slot_offset = UINT64_MAX - sizeof(SlotRecord) + 1; } },
    { "slot_offset が非アライン", [](ShmHeader *h) { h->slot_offset = sizeof(ShmHeader) + 1; } },
    { "buf_num が上限超過", [](ShmHeader *h) { h->buf_num = 100000; } },
    { "element_capacity が過大", [](ShmHeader *h) { h->element_capacity = UINT64_MAX; } },
    { "total_size が過大", [](ShmHeader *h) { h->total_size = UINT64_MAX; } },
  };

  for (const Poison &poison : poisons)
  {
    try
    {
      disconnectTopic("doctor_bad");
    }
    catch (const std::exception &)
    {
    }
    {
      Publisher<int> pub("doctor_bad", 3);
      pub.publish(1);
    }
    {
      SharedMemoryPosix shm("doctor_bad", O_RDWR, static_cast<PERM>(0));
      ASSERT_TRUE(shm.connect()) << poison.what;
      poison.apply(reinterpret_cast<ShmHeader *>(shm.getPtr()));
    }

    const ToolResult r = runTool("doctor doctor_bad");
    ASSERT_NE(r.exit_code, -1) << poison.what;
    // 落ちていない（SEGV なら 139、abort なら 134）ことがまず重要
    EXPECT_LT(r.exit_code, 128) << poison.what << " で doctor が異常終了した\n" << r.output;
    // そのうえで、使えない状態だと報告していること
    EXPECT_EQ(r.exit_code, 1) << poison.what << " を OK と報告した\n" << r.output;
    EXPECT_NE(r.output.find("★"), std::string::npos) << poison.what << "\n" << r.output;
  }
}

// -----------------------------------------------------------------------------
// `list` の列ずれ（ドキュメント整備中に発見）
// -----------------------------------------------------------------------------
//
// `ls -l` はサイズ欄を**右詰め**する。幅の広い行が 1 つでもあると、狭い行の
// サイズの前に空白が複数入る。旧実装は区切りを空白 1 文字と決めうちして
// 10 回 find(" ") を回していたため、空のフィールドが生まれて列が丸ごと右へずれ、
// **セグメント名が表示されなくなっていた**。さらに 10 フィールドを最後まで
// 回った行は改行を出さず、次の行と繋がって表示されていた。
TEST_F(SHMToolDoctorTest, ListShowsEveryNameEvenWhenTheSizeColumnIsPadded)
{
  // 幅の違う 2 つを並べて、ls -l にサイズ欄を右詰めさせる
  Publisher<uint8_t>              small("list_small", 1);
  Publisher<std::vector<uint8_t>> wide("list_wide", 32);
  small.publish(1);
  wide.publish(std::vector<uint8_t>(8192, 7));

  const ToolResult r = runTool("list");
  ASSERT_EQ(r.exit_code, 0) << r.output;

  EXPECT_NE(r.output.find("list_small"), std::string::npos)
      << "サイズ欄の右詰めで列がずれ、名前が消えた:\n"
      << r.output;
  EXPECT_NE(r.output.find("list_wide"), std::string::npos) << r.output;

  // 1 セグメント 1 行になっていること（改行漏れで連結されない）
  size_t rows = 0;
  for (size_t pos = r.output.find("-rw"); pos != std::string::npos; pos = r.output.find("-rw", pos + 1))
  {
    ++rows;
  }
  size_t newlines = 0;
  for (const char c : r.output)
  {
    if (c == '\n')
    {
      ++newlines;
    }
  }
  EXPECT_GE(newlines, rows + 1) << "行が改行されずに連結された:\n" << r.output;
}
