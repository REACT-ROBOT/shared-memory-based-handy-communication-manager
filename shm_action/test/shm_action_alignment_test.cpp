// =============================================================================
// 共有メモリ配置のアライメント回帰テスト
//
// このファイルのテストは「既知の未修正バグ」を再現するもので、
// 現行実装では FAIL することを確認済み（テストファースト）。
//
// 対象バグ:
//   ActionServer / ActionClient は共有メモリ上の配置を sizeof の総和で
//   決めており、各要素の境界に切り上げていない。このため sizeof(Goal) や
//   sizeof(Result) が 8 の倍数でないと、後続の result_mutex /
//   result_condition / result_timestamp_us / cancel_timestamp_us が
//   非アラインになる。特に status_ptr は ACTION_STATUS (uint8_t) なので、
//   その直後に置かれる cancel_timestamp_us はほぼ常に非アラインになる。
//   x86 では動いてしまうが、ARM (Raspberry Pi 4) では 8 バイト境界でない
//   アドレスへの 64bit アクセスが SIGBUS になる。
//
// 検出方法:
//   ARM 実機が無くても検出できるように、共有メモリ上に書かれた
//   タイムスタンプの位置を調べる。ActionServer のコンストラクタは
//   cancel / goal / result の 3 つのタイムスタンプに同じ値を書くので、
//   その値が置かれているオフセットを走査すれば、実装が実際に使っている
//   uint64_t の配置が分かる。すべて 8 バイト境界にあることを要求する。
//
//   なお -fsanitize=alignment を有効にしてビルドすると、x86 でも
//   「store to misaligned address ... for type 'uint64_t'」として
//   同じ不具合が報告される。
// =============================================================================

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <fcntl.h>
}

#include "shm_base.hpp"
#include "shm_action.hpp"

using namespace irlab::shm;

namespace {

// sizeof が 8 の倍数にならない型（後続の配置を崩す）
struct OddGoal {
  char a[3];
};
struct OddResult {
  char a[5];
};
struct OddFeedback {
  char a[1];
};

//! 共有メモリ全体から uint64_t 値 value が置かれているオフセットを集める。
std::vector<size_t> findValueOffsets(const std::string& topic, uint64_t value) {
  std::vector<size_t> offsets;

  SharedMemoryPosix shm(topic, O_RDWR, DEFAULT_PERM);
  if (!shm.connect(0)) {
    return offsets;
  }
  const unsigned char* base = shm.getPtr();
  const size_t         size = shm.getSize();

  if (base != nullptr && size >= sizeof(uint64_t)) {
    for (size_t offset = 0; offset + sizeof(uint64_t) <= size; ++offset) {
      uint64_t candidate = 0;
      std::memcpy(&candidate, base + offset, sizeof(uint64_t));
      if (candidate == value) {
        offsets.push_back(offset);
      }
    }
  }

  shm.disconnect();
  return offsets;
}

template <class Goal, class Result, class Feedback>
std::vector<size_t> constructAndFindTimestamps(const std::string& topic) {
  const uint64_t before = getCurrentTimeUSec();
  ActionServer<Goal, Result, Feedback> server("/" + topic);
  const uint64_t after = getCurrentTimeUSec();

  for (uint64_t value = before; value <= after; ++value) {
    std::vector<size_t> offsets = findValueOffsets(topic, value);
    if (!offsets.empty()) {
      return offsets;
    }
  }
  return {};
}

}  // namespace

class SHMActionAlignmentTest : public ::testing::Test {
protected:
  void SetUp() override { cleanup(); }
  void TearDown() override { cleanup(); }

  void cleanup() {
    disconnectMemory("align_action_int");
    disconnectMemory("align_action_odd");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
};

// -----------------------------------------------------------------------------
// 仕様: 共有メモリ上の uint64_t は 8 バイト境界に置かれなければならない。
// -----------------------------------------------------------------------------
TEST_F(SHMActionAlignmentTest, TimestampsMustBeEightByteAligned) {
  const std::vector<size_t> offsets = constructAndFindTimestamps<int, int, int>("align_action_int");

  ASSERT_FALSE(offsets.empty()) << "前提: コンストラクタが書いたタイムスタンプを見つけられない";

  for (size_t offset : offsets) {
    std::cout << "  timestamp offset=" << offset << " (mod 8 = " << (offset % 8) << ")" << std::endl;
  }

  for (size_t offset : offsets) {
    EXPECT_EQ(offset % 8, 0u) << "共有メモリ上の uint64_t が 8 バイト境界に無い (offset=" << offset
                              << ")。ARM では 64bit アクセスが SIGBUS になる";
  }
}

// -----------------------------------------------------------------------------
// 仕様: 8 の倍数でないサイズの型でも配置は崩れてはならない。
// -----------------------------------------------------------------------------
TEST_F(SHMActionAlignmentTest, TimestampsMustBeAlignedWithOddSizedTypes) {
  const std::vector<size_t> offsets =
      constructAndFindTimestamps<OddGoal, OddResult, OddFeedback>("align_action_odd");

  ASSERT_FALSE(offsets.empty()) << "前提: コンストラクタが書いたタイムスタンプを見つけられない";

  for (size_t offset : offsets) {
    EXPECT_EQ(offset % 8, 0u) << "8 の倍数でないサイズの型で共有メモリ上の uint64_t が"
                                 "8 バイト境界から外れた (offset="
                              << offset << ")";
  }
}
