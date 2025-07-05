# 📡 Pub/Sub通信完全ガイド - 超高速ブロードキャスト通信をマスターしよう
[[English](../md_manual_tutorials_shm_pub_sub_en.html) | 日本語]

## 🎯 このガイドで学べること

- **Pub/Sub通信の深い理解**: 設計思想から実装詳細まで
- **高頻度通信の実現**: 1kHz以上のリアルタイム通信
- **メモリ効率化**: 大容量データの高速転送テクニック
- **実践的な応用例**: ロボット制御、画像処理、センサーネットワーク

## 🧠 Pub/Sub通信の深い理解

### 🏗️ アーキテクチャ解説

```cpp
// 内部構造の概念図
┌─────────────────────────────────────────────────────────────┐
│                   共有メモリ空間                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              リングバッファ                          │    │
│  │ [Data 0][Data 1][Data 2]...[Data N-1]              │    │
│  │    ↑                           ↑                    │    │
│  │  読取位置                    書込位置                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ヘッダー情報:                                               │
│  - シーケンス番号 (データの順序管理)                          │
│  - タイムスタンプ (データの新鮮度)                           │
│  - データサイズ (可変長データ対応)                           │
│  - CRC32チェック (データ整合性)                              │
└─────────────────────────────────────────────────────────────┘

Multiple Subscribers ← [shared memory] ← Single Publisher
      │                                        │
   受信プロセス1                           送信プロセス
   受信プロセス2                              │
   受信プロセス3                        ゼロコピー高速書込
      │
   並列データ処理
```

### ⚡ なぜ超高速なのか？

**1. ゼロコピー設計**
```cpp
// ❌ 従来の方法: データコピーが発生
char buffer[1024];
read(socket_fd, buffer, 1024);    // カーネル→ユーザー空間コピー
memcpy(data_ptr, buffer, 1024);   // バッファ→データ構造コピー

// ✅ 共有メモリ: 直接アクセス
Publisher<SensorData> pub("sensors");
pub.publish(sensor_data);  // メモリに直接書込、コピーなし
```

**2. 効率的なリングバッファ**
```cpp
// リングバッファの利点
class RingBuffer {
    atomic<size_t> write_index;  // 原子操作で高速
    atomic<size_t> read_index;   // ロックフリー設計
    
    // 書込み: O(1)の一定時間
    bool write(const T& data) {
        size_t next = (write_index + 1) % buffer_size;
        if (next != read_index) {  // オーバーフロー検出
            buffer[write_index] = data;
            write_index = next;
            return true;
        }
        return false;  // バッファフル
    }
};
```

**3. CPUキャッシュ最適化**
```cpp
// メモリアクセスパターンの最適化
struct alignas(64) CacheOptimizedData {  // キャッシュライン境界
    atomic<uint64_t> sequence;
    uint64_t timestamp;
    uint32_t data_size;
    uint32_t checksum;
    char data[MAX_DATA_SIZE];
} __attribute__((packed));
```

## 🚀 基本的な使い方

### 1. 簡単な整数データ通信

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace irlab::shm;

// 送信プログラム
int main() {
    // "test_topic"という名前でint型のPublisherを作成
    Publisher<int> pub("test_topic");
    
    std::cout << "データを送信中..." << std::endl;
    
    for (int i = 0; i < 10; ++i) {
        pub.publish(i);
        std::cout << "送信: " << i << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

```cpp
// 受信プログラム
int main() {
    // "test_topic"という名前でint型のSubscriberを作成
    Subscriber<int> sub("test_topic");
    
    std::cout << "データを待機中..." << std::endl;
    
    while (true) {
        bool state;
        int data = sub.subscribe(&state);
        
        if (state) {
            std::cout << "受信: " << data << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

### 2. カスタム構造体の通信

```cpp
// カスタムデータ構造の定義
struct SensorData {
    float temperature;    // 温度
    float humidity;       // 湿度
    float pressure;       // 気圧
    uint64_t timestamp;   // タイムスタンプ
    int sensor_id;        // センサーID
    bool is_valid;        // データ有効性
};

// 送信側
int main() {
    Publisher<SensorData> sensor_pub("weather_data");
    
    while (true) {
        SensorData data;
        data.temperature = 25.5f;
        data.humidity = 60.0f;
        data.pressure = 1013.25f;
        data.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        data.sensor_id = 1;
        data.is_valid = true;
        
        sensor_pub.publish(data);
        std::cout << "センサーデータ送信: 温度=" << data.temperature << "℃" << std::endl;
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    return 0;
}

// 受信側
int main() {
    Subscriber<SensorData> sensor_sub("weather_data");
    
    while (true) {
        bool state;
        SensorData data = sensor_sub.subscribe(&state);
        
        if (state && data.is_valid) {
            std::cout << "受信データ:" << std::endl;
            std::cout << "  温度: " << data.temperature << "℃" << std::endl;
            std::cout << "  湿度: " << data.humidity << "%" << std::endl;
            std::cout << "  気圧: " << data.pressure << "hPa" << std::endl;
            std::cout << "  センサーID: " << data.sensor_id << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    return 0;
}
```

## 🚀 実践的な使用例

### 1. 高頻度センサーデータ配信 (1kHz)

```cpp
#include "shm_pub_sub.hpp"
#include <chrono>
#include <thread>

// 高解像度センサーデータ構造
struct HighFreqSensorData {
    uint64_t timestamp_us;      // マイクロ秒タイムスタンプ
    float position[3];          // X, Y, Z位置
    float velocity[3];          // X, Y, Z速度
    float acceleration[3];      // X, Y, Z加速度
    float quaternion[4];        // 姿勢（クォータニオン）
    uint32_t sequence_number;   // シーケンス番号
    uint8_t sensor_status;      // センサー状態
};

class HighFrequencyPublisher {
private:
    irlab::shm::Publisher<HighFreqSensorData> publisher_;
    std::thread publishing_thread_;
    std::atomic<bool> running_;
    
public:
    HighFrequencyPublisher(const std::string& topic) 
        : publisher_(topic), running_(false) {}
    
    void startPublishing() {
        running_ = true;
        publishing_thread_ = std::thread(&HighFrequencyPublisher::publishLoop, this);
    }
    
    void stopPublishing() {
        running_ = false;
        if (publishing_thread_.joinable()) {
            publishing_thread_.join();
        }
    }
    
private:
    void publishLoop() {
        uint32_t sequence = 0;
        
        // 1kHz = 1000回/秒 = 1ms間隔
        auto next_time = std::chrono::high_resolution_clock::now();
        const auto interval = std::chrono::microseconds(1000);  // 1ms
        
        while (running_) {
            HighFreqSensorData data;
            
            // 高精度タイムスタンプ
            auto now = std::chrono::high_resolution_clock::now();
            data.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            
            // センサーデータ読取（実際のセンサーAPIに置き換え）
            readSensorData(data);
            data.sequence_number = sequence++;
            
            // 超高速送信（通常1-2マイクロ秒）
            publisher_.publish(data);
            
            // 精密なタイミング制御
            next_time += interval;
            std::this_thread::sleep_until(next_time);
        }
    }
    
    void readSensorData(HighFreqSensorData& data) {
        // 実際のセンサーからデータを読取
        // この部分は使用するセンサーAPIに依存
        
        // ダミーデータ例
        static float t = 0.0f;
        t += 0.001f;  // 1ms刻み
        
        // サイン波のモーションシミュレーション
        data.position[0] = std::sin(t);
        data.position[1] = std::cos(t);
        data.position[2] = std::sin(t * 2.0f) * 0.5f;
        
        // 速度（位置の微分）
        data.velocity[0] = std::cos(t);
        data.velocity[1] = -std::sin(t);
        data.velocity[2] = std::cos(t * 2.0f);
        
        // 加速度（速度の微分）
        data.acceleration[0] = -std::sin(t);
        data.acceleration[1] = -std::cos(t);
        data.acceleration[2] = -2.0f * std::sin(t * 2.0f);
        
        // クォータニオン（単位クォータニオン）
        data.quaternion[0] = std::cos(t * 0.5f);  // w
        data.quaternion[1] = std::sin(t * 0.5f);  // x
        data.quaternion[2] = 0.0f;                // y
        data.quaternion[3] = 0.0f;                // z
        
        data.sensor_status = 0x01;  // 正常状態
    }
};

// 高頻度受信と処理
class HighFrequencySubscriber {
private:
    irlab::shm::Subscriber<HighFreqSensorData> subscriber_;
    std::thread processing_thread_;
    std::atomic<bool> running_;
    
    // パフォーマンス統計
    std::atomic<uint64_t> total_received_;
    std::atomic<uint64_t> missed_packets_;
    uint32_t last_sequence_;
    
public:
    HighFrequencySubscriber(const std::string& topic)
        : subscriber_(topic), running_(false), 
          total_received_(0), missed_packets_(0), last_sequence_(0) {}
    
    void startProcessing() {
        running_ = true;
        processing_thread_ = std::thread(&HighFrequencySubscriber::processLoop, this);
    }
    
    void stopProcessing() {
        running_ = false;
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }
    }
    
    void printStatistics() {
        uint64_t received = total_received_.load();
        uint64_t missed = missed_packets_.load();
        double packet_loss = received > 0 ? (double)missed / received * 100.0 : 0.0;
        
        std::cout << "=== 受信統計 ===" << std::endl;
        std::cout << "総受信数: " << received << std::endl;
        std::cout << "欠損数: " << missed << std::endl;
        std::cout << "パケット欠損率: " << std::fixed << std::setprecision(2) 
                  << packet_loss << "%" << std::endl;
    }
    
private:
    void processLoop() {
        while (running_) {
            bool state;
            HighFreqSensorData data = subscriber_.subscribe(&state);
            
            if (state) {
                total_received_++;
                
                // シーケンス番号でパケット欠損検出
                if (total_received_ > 1) {
                    uint32_t expected = last_sequence_ + 1;
                    if (data.sequence_number != expected) {
                        missed_packets_ += (data.sequence_number - expected);
                    }
                }
                last_sequence_ = data.sequence_number;
                
                // 遅延測定
                auto now = std::chrono::high_resolution_clock::now();
                auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    now.time_since_epoch()).count();
                int64_t latency = now_us - data.timestamp_us;
                
                // リアルタイム処理（例：制御計算）
                processRealtimeControl(data, latency);
                
                // 統計情報表示（低頻度）
                if (total_received_ % 1000 == 0) {  // 1秒毎
                    std::cout << "レイテンシ: " << latency << "μs, "
                              << "シーケンス: " << data.sequence_number << std::endl;
                }
            }
            
            // 高頻度処理のため最小限の待機
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    void processRealtimeControl(const HighFreqSensorData& data, int64_t latency) {
        // リアルタイム制御処理の例
        
        // 低遅延要求：10ms以下
        if (latency > 10000) {  // 10ms = 10,000μs
            std::cerr << "⚠️ 高遅延検出: " << latency << "μs" << std::endl;
        }
        
        // 実際の制御計算（例：PID制御）
        // この部分で制御アルゴリズムを実装
        calculatePIDControl(data.position, data.velocity);
    }
    
    void calculatePIDControl(const float position[3], const float velocity[3]) {
        // PID制御計算の例（簡略化）
        static float integral[3] = {0, 0, 0};
        static float last_error[3] = {0, 0, 0};
        
        const float target[3] = {1.0f, 0.0f, 0.5f};  // 目標位置
        const float kp = 2.0f, ki = 0.1f, kd = 0.05f;
        
        for (int i = 0; i < 3; i++) {
            float error = target[i] - position[i];
            integral[i] += error * 0.001f;  // 1ms間隔
            float derivative = (error - last_error[i]) / 0.001f;
            
            float control_output = kp * error + ki * integral[i] + kd * derivative;
            
            // 制御出力の適用（実際のアクチュエータに送信）
            // sendControlCommand(i, control_output);
            
            last_error[i] = error;
        }
    }
};

// 使用例
int main() {
    try {
        // 高頻度パブリッシャー開始
        HighFrequencyPublisher publisher("high_freq_sensors");
        publisher.startPublishing();
        
        // 複数のサブスクライバー（並列処理）
        std::vector<std::unique_ptr<HighFrequencySubscriber>> subscribers;
        
        // 制御用サブスクライバー
        subscribers.push_back(std::make_unique<HighFrequencySubscriber>("high_freq_sensors"));
        subscribers.back()->startProcessing();
        
        // ログ記録用サブスクライバー
        subscribers.push_back(std::make_unique<HighFrequencySubscriber>("high_freq_sensors"));
        subscribers.back()->startProcessing();
        
        // 10秒間実行
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // 停止と統計表示
        publisher.stopPublishing();
        for (auto& sub : subscribers) {
            sub->stopProcessing();
            sub->printStatistics();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### 2. ベクトルデータの通信

```cpp
#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"
#include <vector>

// ベクトルデータの送信
int main() {
    using namespace irlab::shm;
    
    Publisher<std::vector<float>> vector_pub("vector_data");
    
    while (true) {
        // 動的サイズのベクトルデータ
        std::vector<float> sensor_array;
        
        // 複数センサーの値を収集
        for (int i = 0; i < 10; ++i) {
            float value = std::sin(i * 0.1f) * 100.0f;
            sensor_array.push_back(value);
        }
        
        vector_pub.publish(sensor_array);
        std::cout << "ベクトルデータ送信: " << sensor_array.size() << "要素" << std::endl;
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    return 0;
}

// ベクトルデータの受信
int main() {
    using namespace irlab::shm;
    
    Subscriber<std::vector<float>> vector_sub("vector_data");
    
    while (true) {
        bool state;
        std::vector<float> data = vector_sub.subscribe(&state);
        
        if (state) {
            std::cout << "受信ベクトル (" << data.size() << "要素): ";
            for (size_t i = 0; i < std::min(data.size(), (size_t)5); ++i) {
                std::cout << data[i] << " ";
            }
            if (data.size() > 5) std::cout << "...";
            std::cout << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

## 🛠️ パフォーマンス最適化テクニック

### 1. CPU親和性設定

```cpp
#include <pthread.h>
#include <sched.h>

class OptimizedPublisher {
public:
    void setCPUAffinity(int cpu_core) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_core, &cpuset);
        
        int result = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (result != 0) {
            throw std::runtime_error("CPU親和性設定に失敗");
        }
        
        std::cout << "CPU" << cpu_core << "に固定しました" << std::endl;
    }
    
    void setRealtimePriority() {
        struct sched_param param;
        param.sched_priority = 99;  // 最高優先度
        
        int result = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
        if (result != 0) {
            std::cerr << "リアルタイム優先度設定に失敗 (要root権限)" << std::endl;
        }
    }
};
```

### 2. メモリプール最適化

```cpp
#include <memory_resource>

class MemoryOptimizedPublisher {
private:
    // メモリプールでアロケーション最適化
    std::array<std::byte, 64 * 1024> buffer_;  // 64KB固定プール
    std::pmr::monotonic_buffer_resource pool_{buffer_.data(), buffer_.size()};
    
    // カスタムアロケータ使用のコンテナ
    std::pmr::vector<uint8_t> reusable_buffer_{&pool_};
    
public:
    void optimizedPublish() {
        // メモリ確保が超高速（プールから切り出し）
        reusable_buffer_.resize(1024);
        
        // データ準備
        fillData(reusable_buffer_);
        
        // 送信
        publisher_.publish(reusable_buffer_.data(), reusable_buffer_.size());
        
        // メモリ再利用のためクリア
        reusable_buffer_.clear();
    }
};
```

## 📊 パフォーマンス測定とベンチマーク

### 詳細ベンチマーク

```cpp
#include <chrono>
#include <algorithm>
#include <numeric>

class PerformanceBenchmark {
private:
    std::vector<double> latencies_;
    std::vector<double> throughputs_;
    
public:
    void runLatencyBenchmark() {
        std::cout << "=== レイテンシベンチマーク開始 ===" << std::endl;
        
        irlab::shm::Publisher<uint64_t> pub("latency_test");
        irlab::shm::Subscriber<uint64_t> sub("latency_test");
        
        const int iterations = 10000;
        latencies_.reserve(iterations);
        
        for (int i = 0; i < iterations; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            // タイムスタンプ送信
            auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                start.time_since_epoch()).count();
            pub.publish(timestamp);
            
            // 受信待ち
            bool state;
            uint64_t received_timestamp;
            do {
                received_timestamp = sub.subscribe(&state);
            } while (!state);
            
            auto end = std::chrono::high_resolution_clock::now();
            
            // レイテンシ計算（ナノ秒）
            auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start).count();
            latencies_.push_back(latency_ns / 1000.0);  // マイクロ秒に変換
        }
        
        analyzeLatency();
    }
    
    void runThroughputBenchmark() {
        std::cout << "=== スループットベンチマーク開始 ===" << std::endl;
        
        // 様々なデータサイズでテスト
        std::vector<size_t> data_sizes = {64, 256, 1024, 4096, 16384, 65536};
        
        for (size_t size : data_sizes) {
            measureThroughput(size);
        }
    }
    
private:
    void analyzeLatency() {
        if (latencies_.empty()) return;
        
        std::sort(latencies_.begin(), latencies_.end());
        
        double mean = std::accumulate(latencies_.begin(), latencies_.end(), 0.0) / latencies_.size();
        double min_val = latencies_.front();
        double max_val = latencies_.back();
        double p50 = latencies_[latencies_.size() * 0.5];
        double p95 = latencies_[latencies_.size() * 0.95];
        double p99 = latencies_[latencies_.size() * 0.99];
        
        // 分散計算
        double variance = 0.0;
        for (double latency : latencies_) {
            variance += (latency - mean) * (latency - mean);
        }
        variance /= latencies_.size();
        double stddev = std::sqrt(variance);
        
        std::cout << "=== レイテンシ統計 (μs) ===" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "平均: " << mean << std::endl;
        std::cout << "最小: " << min_val << std::endl;
        std::cout << "最大: " << max_val << std::endl;
        std::cout << "標準偏差: " << stddev << std::endl;
        std::cout << "50%ile: " << p50 << std::endl;
        std::cout << "95%ile: " << p95 << std::endl;
        std::cout << "99%ile: " << p99 << std::endl;
        
        // パフォーマンス判定
        if (p99 < 10.0) {
            std::cout << "🏆 優秀: 99%のレイテンシが10μs以下" << std::endl;
        } else if (p99 < 100.0) {
            std::cout << "👍 良好: 99%のレイテンシが100μs以下" << std::endl;
        } else {
            std::cout << "⚠️  要改善: レイテンシが高めです" << std::endl;
        }
    }
    
    void measureThroughput(size_t data_size) {
        std::vector<uint8_t> test_data(data_size, 0x55);  // テストパターン
        
        irlab::shm::Publisher<std::vector<uint8_t>> pub("throughput_test");
        irlab::shm::Subscriber<std::vector<uint8_t>> sub("throughput_test");
        
        const int iterations = 1000;
        auto start = std::chrono::high_resolution_clock::now();
        
        // 送信側スレッド
        std::thread sender([&]() {
            for (int i = 0; i < iterations; ++i) {
                pub.publish(test_data);
            }
        });
        
        // 受信側カウント
        int received_count = 0;
        while (received_count < iterations) {
            bool state;
            auto data = sub.subscribe(&state);
            if (state) {
                received_count++;
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        sender.join();
        
        // スループット計算
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start).count();
        
        double throughput_mbps = (double)(iterations * data_size) / duration_ms / 1024.0;  // MB/s
        double message_rate = (double)iterations / duration_ms * 1000.0;  // msg/s
        
        std::cout << "データサイズ " << data_size << " bytes:" << std::endl;
        std::cout << "  スループット: " << std::fixed << std::setprecision(1) 
                  << throughput_mbps << " MB/s" << std::endl;
        std::cout << "  メッセージレート: " << std::setprecision(0) 
                  << message_rate << " msg/s" << std::endl;
    }
};

// ベンチマーク実行
int main() {
    try {
        PerformanceBenchmark benchmark;
        
        benchmark.runLatencyBenchmark();
        std::cout << std::endl;
        benchmark.runThroughputBenchmark();
        
    } catch (const std::exception& e) {
        std::cerr << "ベンチマークエラー: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## ❓ よくある質問

### Q1. 同時に接続できるSubscriberの数に制限はありますか？
**A**: 基本的に制限はありません。共有メモリの読み取りは複数プロセスから同時に行えます。ただし、システムのメモリ量やプロセス数の制限に依存します。

### Q2. データの順序は保証されますか？
**A**: 同一Publisher内では順序が保証されます。ただし、複数のPublisherがある場合や、ネットワーク遅延がある場合は順序が前後する可能性があります。

### Q3. プロセスがクラッシュした場合はどうなりますか？
**A**: 
- **Publisher**: 他のSubscriberに影響なし
- **Subscriber**: 他のプロセスに影響なし
- **共有メモリ**: システム再起動まで残存（手動削除可能）

## 🔧 トラブルシューティング

### よくある問題と解決法

```cpp
// デバッグ用の診断コード
#include "shm_pub_sub.hpp"
#include <iostream>

void diagnose_pub_sub_communication() {
    using namespace irlab::shm;
    
    std::cout << "=== Pub/Sub通信診断 ===" << std::endl;
    
    try {
        // 1. Publisher作成テスト
        Publisher<int> pub("debug_topic");
        std::cout << "✅ Publisher作成成功" << std::endl;
        
        pub.publish(42);
        std::cout << "✅ データ送信成功" << std::endl;
        
        // 2. Subscriber作成テスト
        Subscriber<int> sub("debug_topic");
        std::cout << "✅ Subscriber作成成功" << std::endl;
        
        bool state;
        int data = sub.subscribe(&state);
        std::cout << "受信結果: state=" << state << ", data=" << data << std::endl;
        
        if (!state) {
            std::cout << "❌ データが無効 - 以下を確認:" << std::endl;
            std::cout << "   - 送信プログラムが動作中か？" << std::endl;
            std::cout << "   - トピック名が一致しているか？" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ エラー: " << e.what() << std::endl;
    }
}
```

## 📚 次のステップ

Pub/Sub通信をマスターしたら、以下の高度なトピックに挑戦してみましょう：

1. **[🤝 Service通信](tutorials_shm_service_jp.md)** - 確実な要求応答通信
2. **[⚡ Action通信](tutorials_shm_action_jp.md)** - 長時間非同期処理
3. **[🐍 Python連携](tutorials_python_jp.md)** - PythonでPub/Sub通信

---

## 📄 ライセンス情報

本ドキュメントで紹介しているサンプルコードは、shared-memory-based-handy-communication-manager プロジェクトの一部として **Apache License 2.0** の下で提供されています。

- ✅ **商用利用可能**: サンプルコードを商業プロジェクトで自由に使用
- ✅ **改変可能**: ニーズに合わせてコードを修正・拡張
- ✅ **再配布可能**: ライセンス表示を保持して再配布

詳細は[LICENSEファイル](../LICENSE)をご確認ください。

---

この完全ガイドで、Pub/Sub通信の真の力を引き出し、次世代の高性能アプリケーションを構築しましょう！ 🚀✨