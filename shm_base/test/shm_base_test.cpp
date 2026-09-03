#include <gtest/gtest.h>
#include <cerrno>
#include <thread>
#include <fstream>
#include <cstdio>
#include <chrono>
#include <vector>
#include <atomic>
#include <cstring>
#include <csignal>
#include <sys/wait.h>
#include <gnu/libc-version.h>

#include "shm_base.hpp"

using namespace irlab::shm;

// Test fixture for SharedMemoryPosix tests
class SharedMemoryPosixTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_name = "/test_shm_memory";
        test_size = 4096;
    }
    
    void TearDown() override {
        // Clean up any shared memory that might be left
        disconnectMemory("test_shm_memory");
        disconnectMemory("test_shm_memory2");
        disconnectMemory("test_large_memory");
        disconnectMemory("test_readonly_memory");
        disconnectMemory("test_partial_unmap");
    }
    
    std::string test_name;
    size_t test_size;
};

// Test fixture for RingBuffer tests
class RingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_name = "/test_ring_buffer";
        element_size = sizeof(int);
        buffer_num = 3;
        total_size = RingBuffer::getSize(element_size, buffer_num);
        
        // Create shared memory for ring buffer
        shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR | O_CREAT, DEFAULT_PERM);
        shared_memory->connect(total_size);
        ASSERT_FALSE(shared_memory->isDisconnected());
        
        ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr(), element_size, buffer_num);
    }
    
    void TearDown() override {
        ring_buffer.reset();
        shared_memory.reset();
        disconnectMemory("test_ring_buffer");
        disconnectMemory("test_ring_buffer2");
        disconnectMemory("test_ring_multithread");
        disconnectMemory("test_ring_timeout");
        disconnectMemory("test_ring_expiry");
    }
    
    std::string shm_name;
    size_t element_size;
    int buffer_num;
    size_t total_size;
    std::unique_ptr<SharedMemoryPosix> shared_memory;
    std::unique_ptr<RingBuffer> ring_buffer;
};

// Basic SharedMemoryPosix functionality tests
TEST_F(SharedMemoryPosixTest, BasicFunctionality) {
    // Test basic connection and disconnection
    SharedMemoryPosix shm(test_name, O_RDWR | O_CREAT, DEFAULT_PERM);
    
    // Initially should be disconnected
    EXPECT_TRUE(shm.isDisconnected());
    
    // Connect with specified size
    EXPECT_TRUE(shm.connect(test_size));
    EXPECT_FALSE(shm.isDisconnected());
    EXPECT_GE(shm.getSize(), test_size);
    EXPECT_NE(shm.getPtr(), nullptr);
    
    // Test writing and reading data
    unsigned char* ptr = shm.getPtr();
    const char* test_data = "Hello, shared memory!";
    strcpy(reinterpret_cast<char*>(ptr), test_data);
    EXPECT_STREQ(reinterpret_cast<char*>(ptr), test_data);
    
    // Test disconnection
    EXPECT_EQ(shm.disconnect(), 0);
    EXPECT_TRUE(shm.isDisconnected());
}

TEST_F(SharedMemoryPosixTest, NameFormatting) {
    // Test different name formats
    std::vector<std::string> names = {
        "/test_name",
        "test_name",
        "/path/to/test_name",
        "path/to/test_name"
    };
    
    for (const auto& name : names) {
        SharedMemoryPosix shm(name, O_RDWR | O_CREAT, DEFAULT_PERM);
        EXPECT_TRUE(shm.connect(1024));
        EXPECT_FALSE(shm.isDisconnected());
        EXPECT_EQ(shm.disconnect(), 0);
        
        // Clean up
        std::string clean_name = name;
        if (clean_name.front() == '/') clean_name = clean_name.substr(1);
        std::replace(clean_name.begin(), clean_name.end(), '/', '_');
        disconnectMemory(clean_name);
    }
}

TEST_F(SharedMemoryPosixTest, MultipleConnections) {
    // Test multiple connections to the same shared memory
    SharedMemoryPosix shm1(test_name, O_RDWR | O_CREAT, DEFAULT_PERM);
    SharedMemoryPosix shm2(test_name, O_RDWR, DEFAULT_PERM);
    
    // First connection creates the memory
    EXPECT_TRUE(shm1.connect(test_size));
    EXPECT_FALSE(shm1.isDisconnected());
    
    // Second connection should access the same memory
    EXPECT_TRUE(shm2.connect());
    EXPECT_FALSE(shm2.isDisconnected());
    EXPECT_EQ(shm1.getSize(), shm2.getSize());
    
    // Test data sharing
    const char* test_data = "Shared data test";
    strcpy(reinterpret_cast<char*>(shm1.getPtr()), test_data);
    EXPECT_STREQ(reinterpret_cast<char*>(shm2.getPtr()), test_data);
    
    // Note: disconnect() no longer calls shm_unlink(), it only unmaps memory and closes fd
    // Both disconnects should succeed (return 0)
    int result1 = shm1.disconnect();
    int result2 = shm2.disconnect();

    // Both should succeed since disconnect() only cleans up local resources
    EXPECT_EQ(result1, 0);
    EXPECT_EQ(result2, 0);

    // Clean up the shared memory explicitly
    disconnectMemory("/test_multiple_connections");
}

TEST_F(SharedMemoryPosixTest, MemoryReuse) {
    // Test that shared memory can be reused properly
    // This replaces the problematic PermissionHandling test
    
    // Create and use shared memory
    {
        SharedMemoryPosix shm("/test_reuse_memory", O_RDWR | O_CREAT, DEFAULT_PERM);
        EXPECT_TRUE(shm.connect(test_size));
        EXPECT_FALSE(shm.isDisconnected());
        EXPECT_NE(shm.getPtr(), nullptr);
        EXPECT_GE(shm.getSize(), test_size);
        
        // Write test data
        const char* test_data = "Reuse test data";
        strcpy(reinterpret_cast<char*>(shm.getPtr()), test_data);
        
        // Verify data was written correctly
        EXPECT_STREQ(reinterpret_cast<char*>(shm.getPtr()), test_data);
    }
    
    // Clean up the memory explicitly
    disconnectMemory("test_reuse_memory");
    
    // Create new shared memory with same name (should be fresh)
    {
        SharedMemoryPosix shm_new("/test_reuse_memory", O_RDWR | O_CREAT, DEFAULT_PERM);
        EXPECT_TRUE(shm_new.connect(test_size));
        EXPECT_FALSE(shm_new.isDisconnected());
        EXPECT_NE(shm_new.getPtr(), nullptr);
        EXPECT_GE(shm_new.getSize(), test_size);
        
        // Write different test data
        const char* new_test_data = "New test data";
        strcpy(reinterpret_cast<char*>(shm_new.getPtr()), new_test_data);
        
        // Verify new data
        EXPECT_STREQ(reinterpret_cast<char*>(shm_new.getPtr()), new_test_data);
    }
    
    // Final cleanup
    disconnectMemory("test_reuse_memory");
}

TEST_F(SharedMemoryPosixTest, ErrorHandling) {
    // Test accessing non-existent memory without O_CREAT
    SharedMemoryPosix shm_nonexistent("/nonexistent_memory_123", O_RDWR, DEFAULT_PERM);
    EXPECT_FALSE(shm_nonexistent.connect());
    EXPECT_TRUE(shm_nonexistent.isDisconnected());
    
    // Test with invalid flags (this may still succeed depending on implementation)
    SharedMemoryPosix shm_invalid("/test_invalid", O_RDONLY, DEFAULT_PERM);
    bool connect_result = shm_invalid.connect(test_size);
    // Note: This may succeed or fail depending on whether the memory already exists
    if (!connect_result) {
        EXPECT_TRUE(shm_invalid.isDisconnected());
    }
    
    // Clean up if connection succeeded
    if (connect_result) {
        disconnectMemory("test_invalid");
    }
}

// -----------------------------------------------------------------------------
// connect(size) は要求サイズより大きい既存の共有メモリに接続したとき、
// マッピング全体を記録しなければならない。
//
// connect() は mmap を stat.st_size（＝ファイル全長）で行う一方、shm_size には
// 引数の要求サイズを記録する。要求サイズが既存ファイルより小さいと
// shm_size < マッピング長 となり、disconnect() の munmap(shm_ptr, shm_size) が
// マッピングの末尾を解放し損ねる。connect/disconnect を繰り返すと
// アドレス空間が単調に増加する。
//
// この経路は実際に踏まれる: Publisher<std::vector<T>> のコンストラクタは
// vector_size = 0 で connect するため、既に大きなベクタが publish されている
// 共有メモリに接続するたびに漏れる。バッファ数や型サイズの異なる Publisher が
// 同じトピックに接続した場合も同様。
//
// 現行実装ではリークするため FAIL する（テストファースト）。
// -----------------------------------------------------------------------------
namespace {
long readVmSizeKb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmSize:", 0) == 0) {
            long value_kb = 0;
            if (std::sscanf(line.c_str(), "VmSize: %ld", &value_kb) == 1) {
                return value_kb;
            }
        }
    }
    return -1;
}
}  // namespace

TEST_F(SharedMemoryPosixTest, ConnectSmallerThanExistingMustNotLeakMapping) {
    const std::string name = "test_partial_unmap";
    const size_t large_size = 4 * 1024 * 1024;  // 4MB
    const size_t small_size = 4096;

    // 大きな共有メモリを作っておく
    {
        SharedMemoryPosix creator("/" + name, O_RDWR | O_CREAT, DEFAULT_PERM);
        ASSERT_TRUE(creator.connect(large_size));
        creator.disconnect();
    }

    const long baseline_kb = readVmSizeKb();
    ASSERT_GT(baseline_kb, 0) << "VmSize を読めない環境ではこのテストは成立しない";

    constexpr int kIterations = 50;
    for (int i = 0; i < kIterations; ++i) {
        SharedMemoryPosix shm("/" + name, O_RDWR, DEFAULT_PERM);
        ASSERT_TRUE(shm.connect(small_size));
        EXPECT_EQ(shm.disconnect(), 0);
    }

    const long growth_kb = readVmSizeKb() - baseline_kb;
    const long leak_per_iteration_kb = static_cast<long>((large_size - small_size) / 1024);

    std::cout << "  VmSize 増加: " << growth_kb << " KB ("
              << kIterations << " 回の connect/disconnect、"
              << "解放し損ねる量は 1 回あたり " << leak_per_iteration_kb << " KB)" << std::endl;

    // 1 回分の漏れも許容しない。多少の malloc 変動は許すため 1 回分未満を閾値にする。
    EXPECT_LT(growth_kb, leak_per_iteration_kb)
        << "connect(size) が要求サイズしか munmap せず、マッピングが解放されずに残っている";

    disconnectMemory(name);
}

// RingBuffer size calculation tests
TEST_F(RingBufferTest, SizeCalculation) {
    // Test size calculation for different configurations
    EXPECT_GT(RingBuffer::getSize(sizeof(int), 1), sizeof(int));
    EXPECT_GT(RingBuffer::getSize(sizeof(int), 3), RingBuffer::getSize(sizeof(int), 1));
    // 形式 v2 ではペイロード先頭をキャッシュライン境界に載せるため、要素サイズが
    // 小さいうちは切り上げに吸収されて総サイズが変わらない。
    // 差が丸めを超える大きさで比較する。
    EXPECT_GT(RingBuffer::getSize(1024, 3), RingBuffer::getSize(sizeof(int), 3));
    EXPECT_GE(RingBuffer::getSize(sizeof(double), 3), RingBuffer::getSize(sizeof(int), 3));
    
    // element_size == 0 は正当な入力（空の vector を publish したトピック）
    EXPECT_GT(RingBuffer::getSize(0, 1), 0);

    // buffer_num == 0 と負値は API 境界で拒否する（R01-F06）。
    // 以前は「そのまま計算して何らかの値を返す」挙動で、
    //   - 0  … 呼び出し側が attach 経路に落ちて未初期化のヘッダを読む
    //   - 負 … size_t へ変換されて巨大なオフセットになり SIGSEGV
    // となっていた。返り値で誤魔化さず例外にする。
    EXPECT_THROW(RingBuffer::getSize(sizeof(int), 0), std::invalid_argument);
    EXPECT_THROW(RingBuffer::getSize(sizeof(int), -1), std::invalid_argument);
    EXPECT_THROW(RingBuffer::getSize(sizeof(int), static_cast<int>(RingBuffer::MAX_BUFFER_NUM) + 1),
                 std::invalid_argument);
}

// getSize() の返り値は 8 バイト境界の倍数でなければならない。
// MultipleRingBuffers テストのように getSize() を使って複数のリングバッファを
// 同一共有メモリ上に連結配置すると、次のリングの先頭がこの返り値の分だけ
// オフセットされる。8 の倍数でないと atomic<uint64_t> や pthread 構造体が
// 非アライン配置になり、ARM (Raspberry Pi) では SIGBUS で即死する
// （x86 は非アラインでも動くため CI では検出できない）。
TEST_F(RingBufferTest, SizeIsAlignmentSafeForTiling) {
    const size_t element_sizes[] = {1, 2, 3, 4, 7, 8, 12, 100, sizeof(double)};
    for (size_t es : element_sizes) {
        for (int bn = 1; bn <= 5; ++bn) {
            EXPECT_EQ(RingBuffer::getSize(es, bn) % 8, 0u)
                << "getSize(" << es << ", " << bn << ") が 8 の倍数でない: "
                << "連結配置した次のリングバッファが ARM で Bus error になる";
        }
    }
}

TEST_F(RingBufferTest, BasicOperations) {
    EXPECT_EQ(ring_buffer->getElementSize(), element_size);
    EXPECT_NE(ring_buffer->getDataList(), nullptr);
    
    // Initially no buffers should be allocated
    EXPECT_LT(ring_buffer->getNewestBufferNum(), 0);
    EXPECT_GE(ring_buffer->getOldestBufferNum(), 0);
    
    // Test buffer allocation
    int oldest = ring_buffer->getOldestBufferNum();
    EXPECT_TRUE(ring_buffer->allocateBuffer(oldest));
    
    // Test timestamp operations
    // NOTE: ライブラリの期限判定は getCurrentTimeUSec() (CLOCK_MONOTONIC_RAW)
    // と比較されるため、テストのスタンプも同じクロックで打つこと
    uint64_t current_time = getCurrentTimeUSec();
    ring_buffer->setTimestamp_us(current_time, oldest);

    // After setting timestamp, the newest buffer must be valid
    int newest = ring_buffer->getNewestBufferNum();
    ASSERT_GE(newest, 0);
    EXPECT_EQ(newest, oldest);
    EXPECT_EQ(ring_buffer->getTimestamp_us(), current_time);
}

TEST_F(RingBufferTest, TimestampManagement) {
    // ライブラリの期限判定と同じクロック getCurrentTimeUSec() でスタンプする。
    // 未来のタイムスタンプは期限切れ扱いになるため、過去に向かって並べる。
    uint64_t base_time_us = getCurrentTimeUSec();

    std::vector<uint64_t> timestamps = {
        base_time_us - 30000,   // -30ms
        base_time_us - 20000,   // -20ms
        base_time_us - 10000    // -10ms (最新)
    };

    for (int i = 0; i < buffer_num; ++i) {
        int buffer_id = ring_buffer->getOldestBufferNum();
        EXPECT_TRUE(ring_buffer->allocateBuffer(buffer_id));
        ring_buffer->setTimestamp_us(timestamps[i], buffer_id);

        // Write test data
        int* data_ptr = reinterpret_cast<int*>(ring_buffer->getDataList());
        data_ptr[buffer_id] = 100 + i;
    }

    // Newest buffer must be the one with the latest timestamp
    int newest = ring_buffer->getNewestBufferNum();
    ASSERT_GE(newest, 0);
    EXPECT_EQ(ring_buffer->getTimestamp_us(), timestamps[2]); // Latest timestamp

    // Verify data integrity
    int* data_ptr = reinterpret_cast<int*>(ring_buffer->getDataList());
    EXPECT_EQ(data_ptr[newest], 102); // Should be the data from the newest buffer
}

TEST_F(RingBufferTest, DataExpiration) {
    // Test data expiration functionality
    ring_buffer->setDataExpiryTime_us(100000); // 100ms expiration
    
    int buffer_id = ring_buffer->getOldestBufferNum();
    EXPECT_TRUE(ring_buffer->allocateBuffer(buffer_id));

    // ライブラリの期限判定と同じクロックでスタンプする
    ring_buffer->setTimestamp_us(getCurrentTimeUSec(), buffer_id);

    // Data must be valid initially (check immediately)
    int newest_before = ring_buffer->getNewestBufferNum();
    ASSERT_GE(newest_before, 0);
    EXPECT_EQ(newest_before, buffer_id);

    // Wait for expiration
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // Data must be expired now
    int newest_after = ring_buffer->getNewestBufferNum();
    EXPECT_LT(newest_after, 0);
}

TEST_F(RingBufferTest, ConcurrentAccess) {
    constexpr int NUM_THREADS = 4;
    constexpr int OPERATIONS_PER_THREAD = 10;
    std::atomic<int> success_count(0);
    std::vector<std::thread> threads;
    
    // Create multiple threads that write to the ring buffer
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < OPERATIONS_PER_THREAD; ++i) {
                int buffer_id = ring_buffer->getOldestBufferNum();
                if (ring_buffer->allocateBuffer(buffer_id)) {
                    // Write unique data
                    int* data_ptr = reinterpret_cast<int*>(ring_buffer->getDataList());
                    data_ptr[buffer_id] = t * 1000 + i;
                    
                    // Set timestamp
                    auto timestamp_us = getCurrentTimeUSec();  // ライブラリの期限判定と同じクロック
                    ring_buffer->setTimestamp_us(timestamp_us, buffer_id);
                    
                    ring_buffer->signal(); // Notify waiting threads
                    success_count.fetch_add(1);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Should have some successful operations
    EXPECT_GT(success_count.load(), 0);
    
    // Verify ring buffer is still in valid state
    EXPECT_GE(ring_buffer->getOldestBufferNum(), 0);
}

TEST_F(RingBufferTest, WaitForTimeout) {
    // Test waitFor with timeout
    auto start_time = std::chrono::steady_clock::now();
    bool result = ring_buffer->waitFor(50000); // 50ms timeout
    auto end_time = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    // Should timeout since no data is available
    EXPECT_FALSE(result);
    EXPECT_GE(duration.count(), 40); // Allow some tolerance
    EXPECT_LE(duration.count(), 100);
}

TEST_F(RingBufferTest, WaitForWithData) {
    // Start a thread that will write data after a delay
    std::thread writer_thread([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        
        int buffer_id = ring_buffer->getOldestBufferNum();
        if (ring_buffer->allocateBuffer(buffer_id)) {
            auto timestamp_us = getCurrentTimeUSec();  // ライブラリの期限判定と同じクロック
            ring_buffer->setTimestamp_us(timestamp_us, buffer_id);
            ring_buffer->signal();
        }
    });
    
    // Wait for data with timeout
    auto start_time = std::chrono::steady_clock::now();
    bool result = ring_buffer->waitFor(100000); // 100ms timeout
    auto end_time = std::chrono::steady_clock::now();
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    writer_thread.join();
    
    // Should return true when data becomes available
    EXPECT_TRUE(result);
    EXPECT_LT(duration.count(), 80); // Should return before timeout
}

TEST_F(RingBufferTest, IsUpdated) {
    // Initially no updates
    EXPECT_FALSE(ring_buffer->isUpdated());
    
    // Add data
    int buffer_id = ring_buffer->getOldestBufferNum();
    EXPECT_TRUE(ring_buffer->allocateBuffer(buffer_id));
    
    auto timestamp_us = getCurrentTimeUSec();  // ライブラリの期限判定と同じクロック
    ring_buffer->setTimestamp_us(timestamp_us, buffer_id);
    
    // Should detect update
    EXPECT_TRUE(ring_buffer->isUpdated());

    // 「どのスロットが最新か」を選ぶだけでは既読にならない（R05-L4）。
    //
    // 以前は getNewestBufferNum() が markAsRead() も兼ねており、この行のあとに
    // isUpdated() が偽になっていた。しかしそれでは、選んだあとに読み出しが
    // 全リトライ失敗しても既読になってしまい、waitFor() がその 1 更新を
    // 取りこぼす。既読にするのは「実際に読めた」ことを知っている側の仕事である。
    const int newest = ring_buffer->getNewestBufferNum();
    ASSERT_GE(newest, 0);
    EXPECT_TRUE(ring_buffer->isUpdated()) << "選んだだけで既読になっている";

    // 読めたことを記録して初めて既読になる
    ring_buffer->markAsRead(ring_buffer->getSequence(newest));
    EXPECT_FALSE(ring_buffer->isUpdated());
}

// Integration tests combining SharedMemory and RingBuffer
TEST(SHMBaseIntegrationTest, MultipleRingBuffers) {
    const std::string shm_name = "/test_multiple_rings";
    const size_t element_size1 = sizeof(int);
    const size_t element_size2 = sizeof(double);
    const int buffer_num = 3;
    
    // Calculate total size for two ring buffers
    size_t ring1_size = RingBuffer::getSize(element_size1, buffer_num);
    size_t ring2_size = RingBuffer::getSize(element_size2, buffer_num);
    size_t total_size = ring1_size + ring2_size;
    
    // Create shared memory
    SharedMemoryPosix shm(shm_name, O_RDWR | O_CREAT, DEFAULT_PERM);
    ASSERT_TRUE(shm.connect(total_size));
    
    // Create two ring buffers in the same shared memory
    unsigned char* base_ptr = shm.getPtr();
    RingBuffer ring1(base_ptr, element_size1, buffer_num);
    RingBuffer ring2(base_ptr + ring1_size, element_size2, buffer_num);
    
    // Test that both ring buffers work independently
    int buffer_id1 = ring1.getOldestBufferNum();
    int buffer_id2 = ring2.getOldestBufferNum();
    
    EXPECT_TRUE(ring1.allocateBuffer(buffer_id1));
    EXPECT_TRUE(ring2.allocateBuffer(buffer_id2));
    
    // Write different data to each ring buffer
    int* data1 = reinterpret_cast<int*>(ring1.getDataList());
    double* data2 = reinterpret_cast<double*>(ring2.getDataList());
    
    data1[buffer_id1] = 42;
    data2[buffer_id2] = 3.14159;
    
    // Set timestamps
    uint64_t timestamp = 1000000;
    ring1.setTimestamp_us(timestamp, buffer_id1);
    ring2.setTimestamp_us(timestamp + 1000, buffer_id2);
    
    // Verify data integrity
    EXPECT_EQ(data1[buffer_id1], 42);
    EXPECT_DOUBLE_EQ(data2[buffer_id2], 3.14159);

    // Clean up: disconnect() only unmaps memory, then explicitly unlink
    shm.disconnect();
    disconnectMemory("test_multiple_rings");
}

TEST(SHMBaseIntegrationTest, UtilityFunctions) {
    const std::string test_name = "/test_utility_memory";
    
    // Create shared memory
    {
        SharedMemoryPosix shm(test_name, O_RDWR | O_CREAT, DEFAULT_PERM);
        EXPECT_TRUE(shm.connect(1024));
        EXPECT_FALSE(shm.isDisconnected());
        // shm destructor will clean up when going out of scope
    }
    
    // Test disconnectMemory utility function
    int result = disconnectMemory("test_utility_memory");
    EXPECT_EQ(result, 0); // Should succeed
    
    // 存在しない名前を消そうとしたら失敗を返すこと。
    // 「might fail, which is expected」とだけ書いて何も検査していなかったが、
    // これは曖昧にしてよい契約ではない。shm_tool remove は戻り値を見て
    // 「消すものが無かった」を終了コードに反映しており（R04）、
    // disconnectMemory() が黙って成功を返すようになるとそれが崩れる。
    errno  = 0;
    result = disconnectMemory("non_existent_memory");
    EXPECT_EQ(result, -1) << "存在しない共有メモリの削除が成功を返した";
    EXPECT_EQ(errno, ENOENT) << "失敗したが errno が ENOENT ではない";
}

// Performance tests (optional, can be disabled for regular testing)
TEST(SHMBasePerformanceTest, RingBufferThroughput) {
    const std::string shm_name = "/test_performance";
    const size_t element_size = sizeof(int);
    const int buffer_num = 10;
    const int num_operations = 1000;
    
    // Create shared memory and ring buffer
    SharedMemoryPosix shm(shm_name, O_RDWR | O_CREAT, DEFAULT_PERM);
    size_t total_size = RingBuffer::getSize(element_size, buffer_num);
    ASSERT_TRUE(shm.connect(total_size));
    
    RingBuffer ring_buffer(shm.getPtr(), element_size, buffer_num);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Perform many write operations
    for (int i = 0; i < num_operations; ++i) {
        int buffer_id = ring_buffer.getOldestBufferNum();
        if (ring_buffer.allocateBuffer(buffer_id)) {
            int* data_ptr = reinterpret_cast<int*>(ring_buffer.getDataList());
            data_ptr[buffer_id] = i;
            
            auto timestamp_us = getCurrentTimeUSec();  // ライブラリの期限判定と同じクロック
            ring_buffer.setTimestamp_us(timestamp_us, buffer_id);
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    // Performance check: should be able to do 1000 operations reasonably quickly
    EXPECT_LT(duration.count(), 100000); // Less than 100ms for 1000 operations

    // Clean up: disconnect() only unmaps memory, then explicitly unlink
    shm.disconnect();
    disconnectMemory("test_performance");
}

// =============================================================================
// pthread_cond_broadcast 永久ブロック再現テスト
//
// signal() コメントに記載されている問題の再現:
//   Subscriberプロセスが pthread_cond_timedwait の内部プロトコル実行中に終了すると、
//   condition variable の内部状態 (waiterカウンタ) が壊れ、
//   次の pthread_cond_broadcast が __condvar_quiesce_and_switch_g1 内の
//   futex_wait で永久にブロックする。
//
// glibc の condvar 実装 (2.25 以降 nptl/pthread_cond_common.c):
//   broadcast は G1/G2 グループを切り替え、旧 G1 の全 waiter が
//   グループ切り替えプロトコルを完了するのを待つ (quiesce)。
//   waiter が SIGKILL されると、このプロトコルが完了せず
//   broadcast が永久にブロックする。
//
//   この問題の再現にはプロセスが condvar 内部プロトコルの
//   特定区間にいるタイミングで kill される必要があり、
//   ウィンドウが非常に狭いためタイミング依存である。
//   再現率を上げるため以下の手法を用いる:
//   - 大量の waiter プロセスを並行起動
//   - signal/broadcast の直後に kill して遷移中を狙う
//   - 複数ラウンド繰り返す
// =============================================================================

// テスト用の共有メモリ構造体
struct CondVarTestData {
    pthread_mutex_t  mutex;
    pthread_cond_t   cond;
    std::atomic<int> waiters_entered;   // timedwait に入った子プロセス数
    std::atomic<int> ready_to_kill;     // kill してよいシグナル
};

class CondVarCorruptionTest : public ::testing::Test {
protected:
    static constexpr const char* SHM_NAME = "/test_condvar_corruption";

    void SetUp() override {
        disconnectMemory("test_condvar_corruption");
        disconnectMemory("test_polling_immune");
    }

    void TearDown() override {
        disconnectMemory("test_condvar_corruption");
        disconnectMemory("test_polling_immune");
    }

    // 共有メモリ上に CondVarTestData を初期化する
    CondVarTestData* createSharedTestData() {
        size_t size = sizeof(CondVarTestData);
        shm_ = std::make_unique<SharedMemoryPosix>(SHM_NAME, O_RDWR | O_CREAT, DEFAULT_PERM);
        shm_->connect(size);
        auto* data = reinterpret_cast<CondVarTestData*>(shm_->getPtr());

        // プロセス間共有属性で mutex を初期化
        pthread_mutexattr_t m_attr;
        pthread_mutexattr_init(&m_attr);
        pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&m_attr, PTHREAD_MUTEX_ROBUST);
        pthread_mutex_init(&data->mutex, &m_attr);
        pthread_mutexattr_destroy(&m_attr);

        // プロセス間共有属性で condition variable を初期化
        pthread_condattr_t c_attr;
        pthread_condattr_init(&c_attr);
        pthread_condattr_setpshared(&c_attr, PTHREAD_PROCESS_SHARED);
        pthread_condattr_setclock(&c_attr, CLOCK_MONOTONIC);
        pthread_cond_init(&data->cond, &c_attr);
        pthread_condattr_destroy(&c_attr);

        data->waiters_entered.store(0);
        data->ready_to_kill.store(0);

        return data;
    }

    // broadcast がタイムアウト内に完了するかチェック
    // returns: true=完了, false=ブロック
    //
    // 以前はスレッドで broadcast し、ブロックしたら detach していた。
    // detach したスレッドは共有メモリ上の condvar を掴んだまま生き続けるため、
    // その後の TearDown による munmap / shm_unlink と競合し、
    // 「テストは緑だが後続テストが不定に壊れる」状態になり得た（R01-F10）。
    //
    // 子プロセスで broadcast させれば、ブロックしても SIGKILL で確実に始末でき、
    // 親のマッピングやフィクスチャと競合しない。
    bool tryBroadcast(CondVarTestData* data, int timeout_ms) {
        (void)data;  // 子プロセスは自前で共有メモリに接続する

        pid_t pid = fork();
        if (pid == 0) {
            // 子: 親と同じ共有メモリに繋いで broadcast する
            SharedMemoryPosix child_shm(SHM_NAME, O_RDWR, DEFAULT_PERM);
            if (!child_shm.connect()) {
                _exit(2);
            }
            auto* child_data = reinterpret_cast<CondVarTestData*>(child_shm.getPtr());
            pthread_cond_broadcast(&child_data->cond);
            _exit(0);
        }
        if (pid < 0) {
            ADD_FAILURE() << "fork() に失敗した";
            return false;
        }

        // タイムアウトまでポーリングし、終わらなければ確実に殺す
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid) {
                return true;   // broadcast は完了した
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        return false;  // ブロックした（＝ condvar の内部状態が壊れている）
    }

    // ROBUST mutex を回復する
    //
    // NOTE: 以前は pthread_mutex_lock の戻り値を見ずに必ず unlock していた。
    //       lock が EOWNERDEAD 以外で失敗した場合（ENOTRECOVERABLE など）は
    //       ロックを保持していないため、「保持していない mutex の unlock」に
    //       なる。ThreadSanitizer が検出した（R01-F10）。
    void recoverMutex(CondVarTestData* data) {
        int ret = pthread_mutex_lock(&data->mutex);
        if (ret == EOWNERDEAD) {
            // 前の所有者が死んだ。一貫性を宣言すればロックは保持している。
            pthread_mutex_consistent(&data->mutex);
            ret = 0;
        }
        if (ret != 0) {
            // 回復不能（ENOTRECOVERABLE 等）。ロックしていないので unlock しない。
            return;
        }
        pthread_mutex_unlock(&data->mutex);
    }

    std::unique_ptr<SharedMemoryPosix> shm_;
};

// ---------------------------------------------------------------------------
// NOTE: ここには condition variable の破損を調べる 3 件
//       （SingleProcessKillDuringTimedwait / KillDuringGroupTransition /
//        MassiveWaitersKillDuringTransition）があったが、削除した。
//
//       3 件とも末尾が SUCCEED() だけで EXPECT / ASSERT が 1 つも無く、
//       ブロック回数を出力して必ず PASS するだけだった。しかも検査対象は
//       テストが自前で作った pthread_cond_t であって、このライブラリの
//       コードではない。**共有メモリのレイアウトに condition variable は
//       存在せず**、RingBuffer::signal() は no-op である（その理由は
//       ring_buffer.cpp の同関数のコメントにある）。
//
//       つまり 3 件は「なぜポーリング方式を選んだか」を調べた glibc の
//       調査記録であって、回帰テストではなかった。その結論を検証している
//       のは下の PollingApproachIsImmune で、これは RingBuffer::waitFor()
//       を実際に叩いているので残してある。
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// テスト4: 現行のポーリング方式がプロセス異常終了の影響を受けないことの確認
//
// RingBuffer の waitFor() はポーリング (sleep + isUpdated) で実装されており、
// pthread_cond_timedwait を使用しない。
// subscriber プロセスが waitFor 中に SIGKILL されても、
// 共有メモリ上の状態は一切壊れないことを検証する。
// ---------------------------------------------------------------------------
TEST_F(CondVarCorruptionTest, PollingApproachIsImmune) {
    const std::string rb_shm_name = "/test_polling_immune";
    const size_t elem_size = sizeof(int);
    const int buf_num = 3;
    size_t total_size = RingBuffer::getSize(elem_size, buf_num);

    SharedMemoryPosix shm(rb_shm_name, O_RDWR | O_CREAT, DEFAULT_PERM);
    ASSERT_TRUE(shm.connect(total_size));
    RingBuffer rb(shm.getPtr(), elem_size, buf_num);

    // 複数回の subscriber kill を繰り返しても壊れないことを確認
    constexpr int NUM_KILLS = 10;
    for (int i = 0; i < NUM_KILLS; ++i) {
        pid_t pid = fork();
        if (pid == 0) {
            SharedMemoryPosix child_shm(rb_shm_name, O_RDWR, DEFAULT_PERM);
            child_shm.connect();
            RingBuffer child_rb(child_shm.getPtr());
            child_rb.setDataExpiryTime_us(0);

            // ポーリングベースの waitFor (内部で sleep_for + isUpdated のみ)
            child_rb.waitFor(60000000);
            _exit(0);
        }

        // ポーリングループに入る時間を確保
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
    }

    // kill を繰り返した後でもデータの書き込み・読み取りが正常に動作すること
    rb.setDataExpiryTime_us(0);

    int buffer_id = rb.getOldestBufferNum();
    EXPECT_TRUE(rb.allocateBuffer(buffer_id));

    auto ts = getCurrentTimeUSec();  // ライブラリの期限判定と同じクロック
    rb.setTimestamp_us(ts, buffer_id);

    // signal() は no-op だが呼び出しても問題なし
    rb.signal();

    // isUpdated() がデータの存在を正しく検知できること
    EXPECT_TRUE(rb.isUpdated());

    // データの読み取りが正常に動作すること
    int newest = rb.getNewestBufferNum();
    EXPECT_GE(newest, 0);
    EXPECT_EQ(newest, buffer_id);

    std::cerr << "[CONFIRMED] ポーリング方式は " << NUM_KILLS
              << " 回の subscriber kill 後も正常に動作" << std::endl;

    shm.disconnect();
    disconnectMemory("test_polling_immune");
}