//! @file shm_pub_sub_validation_test.cpp
//! @brief R01-F06 / R01-F09 の回帰テスト
//!
//! F06: 不正な入力と壊れた共有メモリで落ちないこと
//!      対策前は以下がいずれも SIGSEGV / 無検査だった（実測済み）:
//!        - Publisher<int>("t", -1)                  → SIGSEGV
//!        - ヘッダだけ残して truncate した shm に接続 → SIGSEGV
//!        - buffer_num = 1<<28 (約1GB)               → 例外なく成功
//!        - disconnectMemory("")                     → name[0] の範囲外参照
//! F09: SharedMemoryPosix のデストラクタが munmap すること

#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace
{

//! 現在のプロセスのマッピング数を数える（F09 の検証用）
size_t countMappings()
{
  std::ifstream maps("/proc/self/maps");
  size_t        count = 0;
  std::string   line;
  while (std::getline(maps, line))
  {
    ++count;
  }
  return count;
}

//! /dev/shm 上の実ファイル名
std::string shmPath(const std::string &topic)
{
  return "/dev/shm/shm_" + topic;
}

}  // namespace

class SHMValidationTest : public ::testing::Test
{
protected:
  void TearDown() override
  {
    for (const char *name : { "valid_neg", "valid_zero", "valid_huge", "valid_trunc", "valid_leak", "valid_ok" })
    {
      ::unlink(shmPath(name).c_str());
    }
  }
};

// ---------------------------------------------------------------------------
// F06: API 境界の入力検証
// ---------------------------------------------------------------------------

TEST_F(SHMValidationTest, NegativeBufferNumIsRejected)
{
  // 対策前: size_t へ変換されて巨大なループ／オフセットになり SIGSEGV
  EXPECT_THROW(Publisher<int>("valid_neg", -1), std::runtime_error);
  EXPECT_THROW(Publisher<std::vector<int>>("valid_neg", -1), std::runtime_error);
}

TEST_F(SHMValidationTest, ZeroBufferNumIsRejected)
{
  // 対策前: attach 経路に落ちて未初期化のヘッダを読んでいた
  EXPECT_THROW(Publisher<int>("valid_zero", 0), std::runtime_error);
  EXPECT_THROW(Publisher<std::vector<int>>("valid_zero", 0), std::runtime_error);
}

TEST_F(SHMValidationTest, AbsurdlyLargeBufferNumIsRejected)
{
  // 対策前: 約1GB の共有メモリを例外なく確保していた（上限が無かった）
  EXPECT_THROW(Publisher<int>("valid_huge", 1 << 28), std::runtime_error);
  EXPECT_THROW(RingBuffer::getSize(sizeof(int), 1 << 28), std::invalid_argument);
}

TEST_F(SHMValidationTest, EmptyShmNameIsRejected)
{
  // 対策前: name[0] を無条件に参照していた
  EXPECT_THROW(disconnectMemory(""), std::invalid_argument);
  EXPECT_THROW(SharedMemoryPosix("", O_RDWR, DEFAULT_PERM), std::invalid_argument);
  EXPECT_THROW(disconnectMemory("/"), std::invalid_argument);
}

TEST_F(SHMValidationTest, ShmNameWithParentDirectoryIsRejected)
{
  EXPECT_THROW(SharedMemoryPosix("../escape", O_RDWR, DEFAULT_PERM), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// F06: 壊れた／切り詰められた共有メモリ
// ---------------------------------------------------------------------------

TEST_F(SHMValidationTest, TruncatedSharedMemoryIsRejectedWithoutCrashing)
{
  struct Big
  {
    char blob[65536];
  };

  {
    Publisher<Big> pub("valid_trunc", 3);
    Big            data{};
    data.blob[0] = 42;
    pub.publish(data);
  }

  // ヘッダ（element_size / buf_num）は残るが、データ領域が入らない長さへ切り詰める。
  // 対策前はここで組み立てたポインタがマッピング外を指し SIGSEGV していた。
  int fd = ::shm_open("/shm_valid_trunc", O_RDWR, 0666);
  ASSERT_GE(fd, 0);
  ASSERT_EQ(::ftruncate(fd, 4096), 0);
  ::close(fd);

  Subscriber<Big> sub("valid_trunc");
  bool            is_success = true;
  sub.subscribe(&is_success);
  EXPECT_FALSE(is_success) << "切り詰められた共有メモリを success で返してはならない";
}

TEST_F(SHMValidationTest, ValidateLayoutRejectsOutOfRangeHeader)
{
  Publisher<int> pub("valid_ok", 3);
  pub.publish(1);

  SharedMemoryPosix shm("valid_ok", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());

  std::string reason;
  EXPECT_TRUE(RingBuffer::validateLayout(shm.getPtr(), shm.getSize(), &reason)) << reason;

  // 実際のマッピングより大きなレイアウトを主張する状況を、
  // mapping_size を小さく偽ることで再現する
  EXPECT_FALSE(RingBuffer::validateLayout(shm.getPtr(), 64, &reason));
  EXPECT_FALSE(reason.empty());

  EXPECT_FALSE(RingBuffer::validateLayout(nullptr, 4096, &reason));
}

// ---------------------------------------------------------------------------
// F09: デストラクタで munmap されること
// ---------------------------------------------------------------------------

TEST_F(SHMValidationTest, DestructorUnmapsSharedMemory)
{
  // 大きめのペイロードで Publisher/Subscriber の生成破棄を繰り返し、
  // マッピングが積み上がらないことを確認する。
  // 対策前はデストラクタが close(fd) しかしておらず、マッピングは
  // プロセス終了まで残っていた。
  struct Payload
  {
    char blob[1 << 20];  // 1 MiB
  };

  // 初回は共有メモリの作成やアロケータのウォームアップでマッピングが増えるため、
  // 数回回してから基準を取る
  for (int i = 0; i < 3; ++i)
  {
    Publisher<Payload>  pub("valid_leak", 3);
    Subscriber<Payload> sub("valid_leak");
    Payload             data{};
    pub.publish(data);
    bool ok = false;
    sub.subscribe(&ok);
  }

  const size_t baseline = countMappings();

  constexpr int ITERATIONS = 50;
  for (int i = 0; i < ITERATIONS; ++i)
  {
    Publisher<Payload>  pub("valid_leak", 3);
    Subscriber<Payload> sub("valid_leak");
    Payload             data{};
    data.blob[0] = static_cast<char>(i);
    pub.publish(data);
    bool ok = false;
    sub.subscribe(&ok);
  }

  const size_t after = countMappings();

  // 1回あたり Publisher+Subscriber で 2 マッピング漏れるので、
  // 漏れていれば 100 行前後増える。多少の揺らぎは許容する。
  EXPECT_LE(after, baseline + 8) << "マッピングが " << (after - baseline) << " 件増えた（" << ITERATIONS
                                 << " 回の生成破棄）。デストラクタが munmap していない可能性がある";
}
