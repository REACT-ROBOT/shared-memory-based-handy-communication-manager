// =============================================================================
// 共有メモリ配置のアライメント回帰テスト
//
// このファイルのテストは「既知の未修正バグ」を再現するもので、
// 現行実装では FAIL することを確認済み（テストファースト）。
//
// 対象バグ:
//   ServiceServer / ServiceClient は共有メモリ上の配置を
//     sizeof(pthread_mutex_t) + sizeof(pthread_cond_t) + sizeof(uint64_t)
//     + sizeof(Req) + ...
//   という素朴な sizeof の総和で決めており、各要素の境界に切り上げていない。
//   このため sizeof(Req) が 8 の倍数でないと、後続の response_mutex /
//   response_condition / response_timestamp_usec がすべて非アラインになる。
//   x86 では動いてしまうが、ARM (Raspberry Pi 4) では 8 バイト境界でない
//   アドレスへの 64bit アクセスが SIGBUS になる。
//
//   例: ServiceServer<int, int> (x86_64)
//     request_mutex            offset=  0  (8 の倍数)
//     request_condition        offset= 40  (8 の倍数)
//     request_timestamp_usec   offset= 88  (8 の倍数)
//     request_ptr              offset= 96
//     response_mutex           offset=100  <-- 非アライン
//     response_condition       offset=140  <-- 非アライン
//     response_timestamp_usec  offset=188  <-- 非アライン
//
// 検出方法:
//   ARM 実機が無くても検出できるように、共有メモリ上に書かれた
//   タイムスタンプの位置を調べる。ServiceServer のコンストラクタは
//   request/response の両タイムスタンプに同じ値を書くので、その値が
//   置かれているオフセットを走査すれば、実装が実際に使っている
//   uint64_t の配置が分かる。すべて 8 バイト境界にあることを要求する。
//
//   なお -fsanitize=alignment を有効にしてビルドすると、x86 でも
//   「store to misaligned address ... for type 'uint64_t'」として
//   同じ不具合が報告される。
// =============================================================================

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <fcntl.h>
}

#include "shm_base.hpp"
#include "shm_service.hpp"

using namespace irlab::shm;

namespace {

// sizeof が 8 の倍数にならない型（後続の配置を崩す）
struct OddSized1 {
  char a[1];
};
struct OddSized3 {
  char a[3];
};
struct OddSized5 {
  char a[5];
};

int passThrough1(OddSized1 request) { return static_cast<int>(request.a[0]); }

//! 共有メモリ全体から uint64_t 値 value が置かれているオフセットを集める。
//! 1 バイトずつずらして走査するので、実装が非アラインな位置に書いていれば
//! そのオフセットがそのまま出てくる。
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

}  // namespace

class SHMServiceAlignmentTest : public ::testing::Test {
protected:
  void SetUp() override { cleanup(); }
  void TearDown() override { cleanup(); }

  void cleanup() {
    disconnectMemory("align_service_int");
    disconnectMemory("align_service_odd");
    disconnectMemory("align_service_e2e");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
};

// -----------------------------------------------------------------------------
// 仕様: 共有メモリ上の uint64_t は 8 バイト境界に置かれなければならない。
//       pthread_mutex_t / pthread_cond_t も同様だが、これらは内部に 64bit の
//       フィールドを持つため、タイムスタンプの位置を見れば同じ配置の崩れが
//       検出できる。
// -----------------------------------------------------------------------------
TEST_F(SHMServiceAlignmentTest, TimestampsMustBeEightByteAligned) {
  const std::string topic = "align_service_int";

  const uint64_t before = getCurrentTimeUSec();
  ServiceServer<int, int> server("/" + topic, [](int request) { return request + 1; });
  const uint64_t after = getCurrentTimeUSec();

  // コンストラクタが書いたタイムスタンプの値を特定する
  std::vector<size_t> offsets;
  for (uint64_t value = before; value <= after; ++value) {
    offsets = findValueOffsets(topic, value);
    if (!offsets.empty()) {
      break;
    }
  }

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
//       Req のサイズが後続の response 側の配置をずらすため、
//       1 / 3 / 5 バイトの型で確認する。
// -----------------------------------------------------------------------------
TEST_F(SHMServiceAlignmentTest, TimestampsMustBeAlignedWithOddSizedTypes) {
  const std::string topic = "align_service_odd";

  const uint64_t before = getCurrentTimeUSec();
  ServiceServer<OddSized3, OddSized5> server("/" + topic, [](OddSized3) { return OddSized5{}; });
  const uint64_t after = getCurrentTimeUSec();

  std::vector<size_t> offsets;
  for (uint64_t value = before; value <= after; ++value) {
    offsets = findValueOffsets(topic, value);
    if (!offsets.empty()) {
      break;
    }
  }

  ASSERT_FALSE(offsets.empty()) << "前提: コンストラクタが書いたタイムスタンプを見つけられない";

  for (size_t offset : offsets) {
    EXPECT_EQ(offset % 8, 0u) << "sizeof(Req)=3 で共有メモリ上の uint64_t が 8 バイト境界から外れた (offset="
                              << offset << ")";
  }
}

// -----------------------------------------------------------------------------
// 仕様: 8 の倍数でないサイズの型でもサービス呼び出しが成立しなければならない。
//       x86 では配置が崩れていても動いてしまうため、この検査は ARM 実機での
//       SIGBUS に対する保護として機能する。
// -----------------------------------------------------------------------------
TEST_F(SHMServiceAlignmentTest, ServiceCallWorksWithOddSizedTypes) {
  const std::string topic = "align_service_e2e";

  std::atomic<bool> server_ready(false);
  std::atomic<bool> test_done(false);

  std::thread server_thread([&] {
    try {
      ServiceServer<OddSized1, int> server("/" + topic, passThrough1);
      server_ready.store(true);
      while (!test_done.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
    } catch (const std::exception& e) {
      std::cerr << "Server thread exception: " << e.what() << std::endl;
    }
  });

  while (!server_ready.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  ServiceClient<OddSized1, int> client("/" + topic);

  for (int i = 1; i <= 20; ++i) {
    OddSized1 request{};
    request.a[0] = static_cast<char>(i);
    int response = -1;
    ASSERT_TRUE(client.call(request, &response, 500000)) << "i=" << i;
    EXPECT_EQ(response, i);
  }

  test_done.store(true);
  server_thread.join();
}
