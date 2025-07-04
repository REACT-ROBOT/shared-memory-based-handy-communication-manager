# 🤝 Service通信完全ガイド - 確実な要求応答通信をマスターしよう
[[English](tutorials_shm_service_en.md) | 日本語]

## 🎯 このガイドで学べること

- **Service通信の深い理解**: 要求応答パターンの設計思想から実装詳細まで
- **信頼性の高いデータ交換**: タイムアウト、エラーハンドリング、再試行機構
- **高性能サーバー設計**: 並行処理、負荷分散、メモリ効率化
- **実践的な応用例**: データベース操作、計算サービス、設定管理

## 🧠 Service通信の深い理解

### 🏗️ アーキテクチャ解説

```cpp
// Service通信の内部構造
┌─────────────────────────────────────────────────────────────┐
│                   共有メモリ空間                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              要求キュー                             │    │
│  │ [Req 0][Req 1][Req 2]...[Req N-1]                 │    │
│  │    ↑                           ↑                    │    │
│  │  処理位置                    追加位置                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              応答キュー                             │    │
│  │ [Res 0][Res 1][Res 2]...[Res N-1]                 │    │
│  │    ↑                           ↑                    │    │
│  │  読取位置                    書込位置                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  要求応答マッピング:                                         │
│  - 要求ID (一意識別子)                                       │
│  - タイムスタンプ (タイムアウト管理)                          │
│  - 処理状態 (待機/処理中/完了)                               │
│  - エラーコード (障害時の詳細情報)                           │
└─────────────────────────────────────────────────────────────┘

Multiple Clients ← [shared memory] → Single Server
      │                                        │
   要求送信                                処理エンジン
   応答受信                                   │
      │                                   並列処理
   タイムアウト管理                        結果生成
```

### ⚡ なぜ確実で高速なのか？

**1. 同期・非同期両対応**
```cpp
// 同期通信（簡単）
ServiceClient<int, int> client("calc_service");
client.sendRequest(42);
if (client.waitForResponse(5000000)) {  // 5秒タイムアウト
    int result = client.getResponse();
    std::cout << "結果: " << result << std::endl;
}

// 非同期通信（高性能）
client.sendRequestAsync(42);
// 他の処理を実行...
if (client.checkResponse()) {
    int result = client.getResponse();
    std::cout << "結果: " << result << std::endl;
}
```

**2. 確実な要求応答マッピング**
```cpp
// 内部的な要求管理
struct RequestHeader {
    uint64_t request_id;        // 一意の要求ID
    uint64_t timestamp_us;      // 送信時刻
    uint32_t timeout_ms;        // タイムアウト時間
    uint32_t retry_count;       // 再試行回数
    uint32_t priority;          // 優先度
    RequestStatus status;       // 処理状態
};

// 要求と応答の確実な対応付け
class RequestTracker {
    std::unordered_map<uint64_t, RequestInfo> pending_requests_;
    std::mutex requests_mutex_;
    
public:
    uint64_t addRequest(const RequestInfo& info) {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        uint64_t id = generateUniqueId();
        pending_requests_[id] = info;
        return id;
    }
    
    bool checkResponse(uint64_t request_id, ResponseInfo& response) {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        auto it = pending_requests_.find(request_id);
        if (it != pending_requests_.end() && it->second.status == COMPLETED) {
            response = it->second.response;
            pending_requests_.erase(it);
            return true;
        }
        return false;
    }
};
```

**3. 高効率な並行処理**
```cpp
// サーバー側の並行要求処理
class ConcurrentServiceServer {
    std::vector<std::thread> worker_threads_;
    std::queue<RequestInfo> request_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> running_;
    
public:
    void startWorkers(int num_workers) {
        running_ = true;
        for (int i = 0; i < num_workers; ++i) {
            worker_threads_.emplace_back(&ConcurrentServiceServer::workerLoop, this);
        }
    }
    
private:
    void workerLoop() {
        while (running_) {
            RequestInfo request;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait(lock, [this] { return !request_queue_.empty() || !running_; });
                
                if (!running_) break;
                
                request = request_queue_.front();
                request_queue_.pop();
            }
            
            // 要求を並列処理
            processRequest(request);
        }
    }
};
```

## 🚀 基本的な使い方

### 1. 簡単な計算サービス

```cpp
#include "shm_service.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace irlab::shm;

// 計算要求データ
struct CalcRequest {
    int a, b;
    char operation;  // '+', '-', '*', '/'
};

// 計算結果データ
struct CalcResponse {
    double result;
    bool success;
    char error_message[256];
};

// サーバー側プログラム
int main() {
    ServiceServer<CalcRequest, CalcResponse> server("calculator");
    
    std::cout << "計算サーバーを開始しました..." << std::endl;
    
    while (true) {
        if (server.hasRequest()) {
            CalcRequest request = server.getRequest();
            CalcResponse response;
            
            std::cout << "要求受信: " << request.a << " " << request.operation 
                      << " " << request.b << std::endl;
            
            // 計算処理
            switch (request.operation) {
                case '+':
                    response.result = request.a + request.b;
                    response.success = true;
                    break;
                case '-':
                    response.result = request.a - request.b;
                    response.success = true;
                    break;
                case '*':
                    response.result = request.a * request.b;
                    response.success = true;
                    break;
                case '/':
                    if (request.b != 0) {
                        response.result = (double)request.a / request.b;
                        response.success = true;
                    } else {
                        response.success = false;
                        strncpy(response.error_message, "Division by zero", 255);
                    }
                    break;
                default:
                    response.success = false;
                    strncpy(response.error_message, "Unknown operation", 255);
            }
            
            server.sendResponse(response);
            
            if (response.success) {
                std::cout << "結果送信: " << response.result << std::endl;
            } else {
                std::cout << "エラー送信: " << response.error_message << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return 0;
}
```

```cpp
// クライアント側プログラム
int main() {
    ServiceClient<CalcRequest, CalcResponse> client("calculator");
    
    std::cout << "計算クライアントを開始しました..." << std::endl;
    
    while (true) {
        CalcRequest request;
        std::cout << "数値1を入力: ";
        std::cin >> request.a;
        std::cout << "演算子を入力 (+, -, *, /): ";
        std::cin >> request.operation;
        std::cout << "数値2を入力: ";
        std::cin >> request.b;
        
        // 要求送信
        client.sendRequest(request);
        std::cout << "要求送信完了..." << std::endl;
        
        // 応答待機（最大5秒）
        if (client.waitForResponse(5000000)) {  // 5秒 = 5,000,000マイクロ秒
            CalcResponse response = client.getResponse();
            
            if (response.success) {
                std::cout << "計算結果: " << response.result << std::endl;
            } else {
                std::cout << "エラー: " << response.error_message << std::endl;
            }
        } else {
            std::cout << "タイムアウト: サーバーが応答しませんでした" << std::endl;
        }
        
        std::cout << "続行しますか？ (y/n): ";
        char cont;
        std::cin >> cont;
        if (cont != 'y' && cont != 'Y') break;
    }
    
    return 0;
}
```

### 2. 複合データ型の通信

```cpp
// 複雑なデータ構造の例
struct ImageProcessRequest {
    uint32_t width, height;
    uint8_t format;           // 0=RGB, 1=RGBA, 2=GRAY
    uint32_t data_size;
    char image_data[1024*1024];  // 最大1MB
    
    // 処理パラメータ
    float brightness;
    float contrast;
    float saturation;
    bool apply_blur;
    float blur_radius;
};

struct ImageProcessResponse {
    bool success;
    uint32_t processed_size;
    char processed_data[1024*1024];
    
    // 処理結果情報
    uint32_t processing_time_ms;
    char algorithm_used[128];
    float quality_score;
    char error_details[512];
};

// 画像処理サーバー
class ImageProcessingServer {
private:
    ServiceServer<ImageProcessRequest, ImageProcessResponse> server_;
    std::thread processing_thread_;
    std::atomic<bool> running_;
    
public:
    ImageProcessingServer() : server_("image_processor"), running_(false) {}
    
    void start() {
        running_ = true;
        processing_thread_ = std::thread(&ImageProcessingServer::processLoop, this);
        std::cout << "画像処理サーバーを開始しました" << std::endl;
    }
    
    void stop() {
        running_ = false;
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }
        std::cout << "画像処理サーバーを停止しました" << std::endl;
    }
    
private:
    void processLoop() {
        while (running_) {
            if (server_.hasRequest()) {
                auto start_time = std::chrono::high_resolution_clock::now();
                
                ImageProcessRequest request = server_.getRequest();
                ImageProcessResponse response;
                
                std::cout << "画像処理要求受信: " << request.width << "x" << request.height
                          << ", サイズ: " << request.data_size << " bytes" << std::endl;
                
                // 画像処理実行
                bool success = processImage(request, response);
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);
                
                response.processing_time_ms = duration.count();
                response.success = success;
                
                if (success) {
                    strncpy(response.algorithm_used, "Advanced Filter v2.1", 127);
                    response.quality_score = calculateQualityScore(response);
                    std::cout << "処理完了: " << response.processing_time_ms << "ms, "
                              << "品質スコア: " << response.quality_score << std::endl;
                } else {
                    strncpy(response.error_details, "画像処理に失敗しました", 511);
                    std::cout << "処理失敗: " << response.error_details << std::endl;
                }
                
                server_.sendResponse(response);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    
    bool processImage(const ImageProcessRequest& request, ImageProcessResponse& response) {
        try {
            // 入力検証
            if (request.data_size == 0 || request.width == 0 || request.height == 0) {
                strncpy(response.error_details, "無効な画像データ", 511);
                return false;
            }
            
            // 実際の画像処理アルゴリズム
            // この例では簡単な処理をシミュレート
            
            // 明度調整
            float brightness_factor = 1.0f + request.brightness;
            
            // コントラスト調整
            float contrast_factor = request.contrast;
            
            // 色の処理（この例では単純化）
            for (uint32_t i = 0; i < request.data_size; ++i) {
                uint8_t pixel = request.image_data[i];
                
                // 明度調整
                float adjusted = pixel * brightness_factor;
                
                // コントラスト調整
                adjusted = (adjusted - 128.0f) * contrast_factor + 128.0f;
                
                // 範囲クリッピング
                adjusted = std::max(0.0f, std::min(255.0f, adjusted));
                
                response.processed_data[i] = static_cast<uint8_t>(adjusted);
            }
            
            response.processed_size = request.data_size;
            
            // ブラー処理（要求があれば）
            if (request.apply_blur) {
                applyBlur(response.processed_data, request.width, request.height, 
                         request.blur_radius);
            }
            
            return true;
            
        } catch (const std::exception& e) {
            strncpy(response.error_details, e.what(), 511);
            return false;
        }
    }
    
    void applyBlur(char* data, uint32_t width, uint32_t height, float radius) {
        // 簡単なボックスブラーの実装
        // 実際の実装ではより高度なアルゴリズムを使用
        
        int blur_size = static_cast<int>(radius);
        if (blur_size <= 0) return;
        
        // 水平方向のブラー
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = blur_size; x < width - blur_size; ++x) {
                int sum = 0;
                for (int dx = -blur_size; dx <= blur_size; ++dx) {
                    sum += static_cast<uint8_t>(data[y * width + x + dx]);
                }
                data[y * width + x] = sum / (2 * blur_size + 1);
            }
        }
    }
    
    float calculateQualityScore(const ImageProcessResponse& response) {
        // 品質スコアの計算（簡略化）
        float score = 0.8f + (response.processing_time_ms < 100 ? 0.2f : 0.1f);
        return std::min(1.0f, score);
    }
};

// 使用例
int main() {
    ImageProcessingServer server;
    
    try {
        server.start();
        
        // サーバーを10秒間実行
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        server.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## 🚀 実践的な使用例

### 1. 高性能データベースサービス

```cpp
#include "shm_service.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <vector>
#include <thread>

// データベース操作の定義
enum class DbOperation {
    INSERT,
    SELECT,
    UPDATE,
    DELETE,
    CREATE_TABLE,
    DROP_TABLE
};

// データベース要求
struct DatabaseRequest {
    DbOperation operation;
    char table_name[64];
    char key[128];
    char value[1024];
    char condition[256];
    
    // メタデータ
    uint64_t client_id;
    uint32_t priority;
    bool require_transaction;
};

// データベース応答
struct DatabaseResponse {
    bool success;
    uint32_t affected_rows;
    char result_data[2048];
    char error_message[512];
    
    // パフォーマンス情報
    uint64_t execution_time_us;
    uint32_t memory_used;
    uint32_t disk_reads;
    uint32_t disk_writes;
};

// 高性能インメモリデータベース
class HighPerformanceDatabase {
private:
    // データストレージ
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> tables_;
    std::shared_mutex data_mutex_;
    
    // パフォーマンス監視
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> total_execution_time_{0};
    std::atomic<uint32_t> active_connections_{0};
    
    // サービス管理
    ServiceServer<DatabaseRequest, DatabaseResponse> server_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> running_{false};
    
public:
    HighPerformanceDatabase() : server_("database_service") {}
    
    void start(int num_workers = 4) {
        running_ = true;
        
        // ワーカースレッドを開始
        for (int i = 0; i < num_workers; ++i) {
            worker_threads_.emplace_back(&HighPerformanceDatabase::workerLoop, this, i);
        }
        
        std::cout << "データベースサービスを開始しました (" << num_workers << " workers)" << std::endl;
        
        // 統計情報スレッドを開始
        worker_threads_.emplace_back(&HighPerformanceDatabase::statisticsLoop, this);
    }
    
    void stop() {
        running_ = false;
        
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        std::cout << "データベースサービスを停止しました" << std::endl;
    }
    
private:
    void workerLoop(int worker_id) {
        std::cout << "Worker " << worker_id << " を開始しました" << std::endl;
        
        while (running_) {
            if (server_.hasRequest()) {
                active_connections_++;
                
                auto start_time = std::chrono::high_resolution_clock::now();
                
                DatabaseRequest request = server_.getRequest();
                DatabaseResponse response = processRequest(request);
                
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                    end_time - start_time);
                
                response.execution_time_us = duration.count();
                
                server_.sendResponse(response);
                
                // 統計情報更新
                total_requests_++;
                total_execution_time_ += duration.count();
                active_connections_--;
                
                std::cout << "Worker " << worker_id << " 処理完了: " 
                          << response.execution_time_us << "μs" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
    DatabaseResponse processRequest(const DatabaseRequest& request) {
        DatabaseResponse response;
        response.success = false;
        response.affected_rows = 0;
        response.memory_used = 0;
        response.disk_reads = 0;
        response.disk_writes = 0;
        
        try {
            switch (request.operation) {
                case DbOperation::CREATE_TABLE:
                    response = createTable(request);
                    break;
                case DbOperation::INSERT:
                    response = insertData(request);
                    break;
                case DbOperation::SELECT:
                    response = selectData(request);
                    break;
                case DbOperation::UPDATE:
                    response = updateData(request);
                    break;
                case DbOperation::DELETE:
                    response = deleteData(request);
                    break;
                case DbOperation::DROP_TABLE:
                    response = dropTable(request);
                    break;
                default:
                    strncpy(response.error_message, "未知の操作", 511);
            }
        } catch (const std::exception& e) {
            strncpy(response.error_message, e.what(), 511);
        }
        
        return response;
    }
    
    DatabaseResponse createTable(const DatabaseRequest& request) {
        DatabaseResponse response;
        
        std::unique_lock<std::shared_mutex> lock(data_mutex_);
        
        std::string table_name = request.table_name;
        
        if (tables_.find(table_name) != tables_.end()) {
            strncpy(response.error_message, "テーブルが既に存在します", 511);
            return response;
        }
        
        tables_[table_name] = std::unordered_map<std::string, std::string>();
        response.success = true;
        response.affected_rows = 1;
        response.disk_writes = 1;
        
        std::cout << "テーブル作成: " << table_name << std::endl;
        
        return response;
    }
    
    DatabaseResponse insertData(const DatabaseRequest& request) {
        DatabaseResponse response;
        
        std::unique_lock<std::shared_mutex> lock(data_mutex_);
        
        std::string table_name = request.table_name;
        std::string key = request.key;
        std::string value = request.value;
        
        auto table_it = tables_.find(table_name);
        if (table_it == tables_.end()) {
            strncpy(response.error_message, "テーブルが存在しません", 511);
            return response;
        }
        
        table_it->second[key] = value;
        response.success = true;
        response.affected_rows = 1;
        response.disk_writes = 1;
        
        return response;
    }
    
    DatabaseResponse selectData(const DatabaseRequest& request) {
        DatabaseResponse response;
        
        std::shared_lock<std::shared_mutex> lock(data_mutex_);
        
        std::string table_name = request.table_name;
        std::string key = request.key;
        
        auto table_it = tables_.find(table_name);
        if (table_it == tables_.end()) {
            strncpy(response.error_message, "テーブルが存在しません", 511);
            return response;
        }
        
        if (key.empty()) {
            // 全レコード取得
            std::string result;
            for (const auto& pair : table_it->second) {
                result += pair.first + "=" + pair.second + "\n";
            }
            strncpy(response.result_data, result.c_str(), 2047);
            response.affected_rows = table_it->second.size();
        } else {
            // 特定キーの取得
            auto data_it = table_it->second.find(key);
            if (data_it != table_it->second.end()) {
                strncpy(response.result_data, data_it->second.c_str(), 2047);
                response.affected_rows = 1;
            } else {
                strncpy(response.error_message, "キーが見つかりません", 511);
                return response;
            }
        }
        
        response.success = true;
        response.disk_reads = 1;
        
        return response;
    }
    
    DatabaseResponse updateData(const DatabaseRequest& request) {
        DatabaseResponse response;
        
        std::unique_lock<std::shared_mutex> lock(data_mutex_);
        
        std::string table_name = request.table_name;
        std::string key = request.key;
        std::string value = request.value;
        
        auto table_it = tables_.find(table_name);
        if (table_it == tables_.end()) {
            strncpy(response.error_message, "テーブルが存在しません", 511);
            return response;
        }
        
        auto data_it = table_it->second.find(key);
        if (data_it == table_it->second.end()) {
            strncpy(response.error_message, "キーが見つかりません", 511);
            return response;
        }
        
        data_it->second = value;
        response.success = true;
        response.affected_rows = 1;
        response.disk_writes = 1;
        
        return response;
    }
    
    DatabaseResponse deleteData(const DatabaseRequest& request) {
        DatabaseResponse response;
        
        std::unique_lock<std::shared_mutex> lock(data_mutex_);
        
        std::string table_name = request.table_name;
        std::string key = request.key;
        
        auto table_it = tables_.find(table_name);
        if (table_it == tables_.end()) {
            strncpy(response.error_message, "テーブルが存在しません", 511);
            return response;
        }
        
        auto data_it = table_it->second.find(key);
        if (data_it == table_it->second.end()) {
            strncpy(response.error_message, "キーが見つかりません", 511);
            return response;
        }
        
        table_it->second.erase(data_it);
        response.success = true;
        response.affected_rows = 1;
        response.disk_writes = 1;
        
        return response;
    }
    
    DatabaseResponse dropTable(const DatabaseRequest& request) {
        DatabaseResponse response;
        
        std::unique_lock<std::shared_mutex> lock(data_mutex_);
        
        std::string table_name = request.table_name;
        
        auto table_it = tables_.find(table_name);
        if (table_it == tables_.end()) {
            strncpy(response.error_message, "テーブルが存在しません", 511);
            return response;
        }
        
        tables_.erase(table_it);
        response.success = true;
        response.affected_rows = 1;
        response.disk_writes = 1;
        
        return response;
    }
    
    void statisticsLoop() {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            uint64_t total_reqs = total_requests_.load();
            uint64_t total_time = total_execution_time_.load();
            uint32_t active_conns = active_connections_.load();
            
            if (total_reqs > 0) {
                double avg_time = (double)total_time / total_reqs;
                double reqs_per_sec = total_reqs / 5.0;  // 5秒間隔
                
                std::cout << "=== データベース統計 ===" << std::endl;
                std::cout << "総要求数: " << total_reqs << std::endl;
                std::cout << "平均処理時間: " << avg_time << "μs" << std::endl;
                std::cout << "要求/秒: " << reqs_per_sec << std::endl;
                std::cout << "アクティブ接続: " << active_conns << std::endl;
                std::cout << "テーブル数: " << tables_.size() << std::endl;
                
                // 統計リセット
                total_requests_ = 0;
                total_execution_time_ = 0;
            }
        }
    }
};

// データベースクライアント
class DatabaseClient {
private:
    ServiceClient<DatabaseRequest, DatabaseResponse> client_;
    
public:
    DatabaseClient() : client_("database_service") {}
    
    bool createTable(const std::string& table_name) {
        DatabaseRequest request;
        request.operation = DbOperation::CREATE_TABLE;
        strncpy(request.table_name, table_name.c_str(), 63);
        
        return executeRequest(request);
    }
    
    bool insert(const std::string& table, const std::string& key, const std::string& value) {
        DatabaseRequest request;
        request.operation = DbOperation::INSERT;
        strncpy(request.table_name, table.c_str(), 63);
        strncpy(request.key, key.c_str(), 127);
        strncpy(request.value, value.c_str(), 1023);
        
        return executeRequest(request);
    }
    
    std::string select(const std::string& table, const std::string& key = "") {
        DatabaseRequest request;
        request.operation = DbOperation::SELECT;
        strncpy(request.table_name, table.c_str(), 63);
        strncpy(request.key, key.c_str(), 127);
        
        DatabaseResponse response;
        if (executeRequest(request, &response)) {
            return std::string(response.result_data);
        }
        return "";
    }
    
    bool update(const std::string& table, const std::string& key, const std::string& value) {
        DatabaseRequest request;
        request.operation = DbOperation::UPDATE;
        strncpy(request.table_name, table.c_str(), 63);
        strncpy(request.key, key.c_str(), 127);
        strncpy(request.value, value.c_str(), 1023);
        
        return executeRequest(request);
    }
    
    bool deleteRecord(const std::string& table, const std::string& key) {
        DatabaseRequest request;
        request.operation = DbOperation::DELETE;
        strncpy(request.table_name, table.c_str(), 63);
        strncpy(request.key, key.c_str(), 127);
        
        return executeRequest(request);
    }
    
private:
    bool executeRequest(const DatabaseRequest& request, DatabaseResponse* response = nullptr) {
        client_.sendRequest(request);
        
        if (client_.waitForResponse(10000000)) {  // 10秒タイムアウト
            DatabaseResponse resp = client_.getResponse();
            
            if (response) {
                *response = resp;
            }
            
            if (!resp.success) {
                std::cerr << "データベースエラー: " << resp.error_message << std::endl;
            }
            
            return resp.success;
        } else {
            std::cerr << "データベースタイムアウト" << std::endl;
            return false;
        }
    }
};

// 使用例
int main() {
    try {
        // データベースサーバーを開始
        HighPerformanceDatabase db;
        db.start(4);  // 4ワーカー
        
        // クライアントテスト
        DatabaseClient client;
        
        // テーブル作成
        if (client.createTable("users")) {
            std::cout << "テーブル作成成功" << std::endl;
        }
        
        // データ挿入
        client.insert("users", "user1", "Alice");
        client.insert("users", "user2", "Bob");
        client.insert("users", "user3", "Charlie");
        
        // データ取得
        std::string result = client.select("users");
        std::cout << "全ユーザー:\n" << result << std::endl;
        
        // 特定データ取得
        std::string alice = client.select("users", "user1");
        std::cout << "user1: " << alice << std::endl;
        
        // データ更新
        client.update("users", "user1", "Alice Smith");
        
        // データ削除
        client.deleteRecord("users", "user3");
        
        // 最終結果確認
        result = client.select("users");
        std::cout << "最終結果:\n" << result << std::endl;
        
        // サーバー停止
        std::this_thread::sleep_for(std::chrono::seconds(2));
        db.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## 🛠️ パフォーマンス最適化テクニック

### 1. 非同期処理とタイムアウト管理

```cpp
#include <future>
#include <chrono>

class AsyncServiceClient {
private:
    ServiceClient<RequestType, ResponseType> client_;
    std::unordered_map<uint64_t, std::future<ResponseType>> pending_requests_;
    std::mutex requests_mutex_;
    std::atomic<uint64_t> request_counter_{0};
    
public:
    AsyncServiceClient(const std::string& service_name) : client_(service_name) {}
    
    // 非同期要求送信
    uint64_t sendRequestAsync(const RequestType& request) {
        uint64_t request_id = request_counter_++;
        
        // 非同期タスクを作成
        auto future = std::async(std::launch::async, [this, request]() {
            client_.sendRequest(request);
            if (client_.waitForResponse(5000000)) {  // 5秒タイムアウト
                return client_.getResponse();
            }
            throw std::runtime_error("Request timeout");
        });
        
        {
            std::lock_guard<std::mutex> lock(requests_mutex_);
            pending_requests_[request_id] = std::move(future);
        }
        
        return request_id;
    }
    
    // 応答確認（ノンブロッキング）
    bool checkResponse(uint64_t request_id, ResponseType& response) {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        
        auto it = pending_requests_.find(request_id);
        if (it == pending_requests_.end()) {
            return false;
        }
        
        // 完了確認
        if (it->second.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                response = it->second.get();
                pending_requests_.erase(it);
                return true;
            } catch (const std::exception& e) {
                pending_requests_.erase(it);
                throw;
            }
        }
        
        return false;
    }
    
    // 応答待機（ブロッキング）
    ResponseType waitForResponse(uint64_t request_id, 
                                std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        
        auto it = pending_requests_.find(request_id);
        if (it == pending_requests_.end()) {
            throw std::runtime_error("Request not found");
        }
        
        // タイムアウト付き待機
        if (it->second.wait_for(timeout) == std::future_status::ready) {
            try {
                ResponseType response = it->second.get();
                pending_requests_.erase(it);
                return response;
            } catch (const std::exception& e) {
                pending_requests_.erase(it);
                throw;
            }
        } else {
            pending_requests_.erase(it);
            throw std::runtime_error("Request timeout");
        }
    }
    
    // 一括要求送信
    std::vector<uint64_t> sendBatchRequests(const std::vector<RequestType>& requests) {
        std::vector<uint64_t> request_ids;
        request_ids.reserve(requests.size());
        
        for (const auto& request : requests) {
            request_ids.push_back(sendRequestAsync(request));
        }
        
        return request_ids;
    }
    
    // 一括応答受信
    std::vector<ResponseType> waitForBatchResponses(const std::vector<uint64_t>& request_ids,
                                                   std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
        std::vector<ResponseType> responses;
        responses.reserve(request_ids.size());
        
        for (uint64_t request_id : request_ids) {
            responses.push_back(waitForResponse(request_id, timeout));
        }
        
        return responses;
    }
    
    // 保留中の要求数
    size_t getPendingRequestCount() const {
        std::lock_guard<std::mutex> lock(requests_mutex_);
        return pending_requests_.size();
    }
};
```

### 2. 接続プールとロードバランシング

```cpp
class ServiceConnectionPool {
private:
    std::vector<std::unique_ptr<ServiceClient<RequestType, ResponseType>>> clients_;
    std::atomic<size_t> round_robin_index_{0};
    std::mutex pool_mutex_;
    
public:
    ServiceConnectionPool(const std::string& service_name, size_t pool_size = 4) {
        clients_.reserve(pool_size);
        
        for (size_t i = 0; i < pool_size; ++i) {
            clients_.push_back(std::make_unique<ServiceClient<RequestType, ResponseType>>(service_name));
        }
    }
    
    // ラウンドロビン方式でクライアント選択
    ResponseType sendRequestRoundRobin(const RequestType& request) {
        size_t index = round_robin_index_++ % clients_.size();
        return sendRequestToClient(index, request);
    }
    
    // 最も負荷の少ないクライアントを選択
    ResponseType sendRequestLeastLoaded(const RequestType& request) {
        // 実装では各クライアントの負荷を監視
        // この例では簡略化してランダム選択
        size_t index = rand() % clients_.size();
        return sendRequestToClient(index, request);
    }
    
    // 並列要求処理
    std::vector<ResponseType> sendParallelRequests(const std::vector<RequestType>& requests) {
        std::vector<std::future<ResponseType>> futures;
        futures.reserve(requests.size());
        
        for (size_t i = 0; i < requests.size(); ++i) {
            size_t client_index = i % clients_.size();
            
            futures.push_back(std::async(std::launch::async, [this, client_index, &requests, i]() {
                return sendRequestToClient(client_index, requests[i]);
            }));
        }
        
        std::vector<ResponseType> responses;
        responses.reserve(requests.size());
        
        for (auto& future : futures) {
            responses.push_back(future.get());
        }
        
        return responses;
    }
    
private:
    ResponseType sendRequestToClient(size_t client_index, const RequestType& request) {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        
        auto& client = clients_[client_index];
        client->sendRequest(request);
        
        if (client->waitForResponse(5000000)) {  // 5秒タイムアウト
            return client->getResponse();
        }
        
        throw std::runtime_error("Service request timeout");
    }
};
```

## 📊 パフォーマンス測定とベンチマーク

### 詳細ベンチマーク

```cpp
#include <chrono>
#include <algorithm>
#include <numeric>
#include <iomanip>

class ServicePerformanceBenchmark {
private:
    std::vector<double> latencies_;
    std::vector<double> throughputs_;
    
public:
    void runServiceBenchmark() {
        std::cout << "=== Service通信ベンチマーク開始 ===" << std::endl;
        
        // 1. レイテンシテスト
        measureLatency();
        
        // 2. スループットテスト
        measureThroughput();
        
        // 3. 同時接続テスト
        measureConcurrency();
        
        // 4. 長時間ストレステスト
        measureLongTermStability();
    }
    
private:
    void measureLatency() {
        std::cout << "レイテンシ測定中..." << std::endl;
        
        ServiceServer<int, int> server("latency_test");
        ServiceClient<int, int> client("latency_test");
        
        // サーバー側スレッド
        std::thread server_thread([&server]() {
            for (int i = 0; i < 1000; ++i) {
                if (server.hasRequest()) {
                    int request = server.getRequest();
                    server.sendResponse(request * 2);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
        
        latencies_.clear();
        latencies_.reserve(1000);
        
        // クライアント側測定
        for (int i = 0; i < 1000; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            client.sendRequest(i);
            if (client.waitForResponse(1000000)) {  // 1秒タイムアウト
                int response = client.getResponse();
                
                auto end = std::chrono::high_resolution_clock::now();
                auto latency = std::chrono::duration_cast<std::chrono::microseconds>(
                    end - start).count();
                
                latencies_.push_back(latency);
            }
        }
        
        server_thread.join();
        analyzeLatency();
    }
    
    void measureThroughput() {
        std::cout << "スループット測定中..." << std::endl;
        
        ServiceServer<std::vector<int>, std::vector<int>> server("throughput_test");
        ServiceClient<std::vector<int>, std::vector<int>> client("throughput_test");
        
        std::vector<size_t> data_sizes = {10, 100, 1000, 10000};
        
        for (size_t size : data_sizes) {
            measureThroughputForSize(server, client, size);
        }
    }
    
    void measureThroughputForSize(ServiceServer<std::vector<int>, std::vector<int>>& server,
                                 ServiceClient<std::vector<int>, std::vector<int>>& client,
                                 size_t data_size) {
        
        std::vector<int> test_data(data_size);
        std::iota(test_data.begin(), test_data.end(), 1);
        
        const int iterations = 100;
        
        // サーバー側スレッド
        std::thread server_thread([&server, iterations]() {
            for (int i = 0; i < iterations; ++i) {
                if (server.hasRequest()) {
                    auto request = server.getRequest();
                    // 簡単な処理（要素の2倍）
                    for (auto& val : request) {
                        val *= 2;
                    }
                    server.sendResponse(request);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // クライアント側測定
        for (int i = 0; i < iterations; ++i) {
            client.sendRequest(test_data);
            if (client.waitForResponse(5000000)) {  // 5秒タイムアウト
                auto response = client.getResponse();
                // 応答確認
                assert(response.size() == data_size);
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        server_thread.join();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end - start).count();
        
        double throughput = (double)(iterations * data_size * sizeof(int)) / duration / 1024.0;  // KB/s
        double message_rate = (double)iterations / duration * 1000.0;  // msg/s
        
        std::cout << "データサイズ " << data_size << " 要素:" << std::endl;
        std::cout << "  スループット: " << std::fixed << std::setprecision(1) 
                  << throughput << " KB/s" << std::endl;
        std::cout << "  メッセージレート: " << std::setprecision(0) 
                  << message_rate << " msg/s" << std::endl;
    }
    
    void measureConcurrency() {
        std::cout << "同時接続性能測定中..." << std::endl;
        
        ServiceServer<int, int> server("concurrency_test");
        
        // サーバー側スレッド
        std::thread server_thread([&server]() {
            for (int i = 0; i < 1000; ++i) {
                if (server.hasRequest()) {
                    int request = server.getRequest();
                    // 計算負荷のシミュレート
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                    server.sendResponse(request + 1);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
        
        std::vector<int> client_counts = {1, 2, 4, 8, 16};
        
        for (int client_count : client_counts) {
            auto start = std::chrono::high_resolution_clock::now();
            
            std::vector<std::thread> client_threads;
            std::atomic<int> completed_requests{0};
            
            for (int i = 0; i < client_count; ++i) {
                client_threads.emplace_back([&completed_requests, i]() {
                    ServiceClient<int, int> client("concurrency_test");
                    
                    for (int j = 0; j < 10; ++j) {
                        client.sendRequest(i * 10 + j);
                        if (client.waitForResponse(2000000)) {  // 2秒タイムアウト
                            completed_requests++;
                        }
                    }
                });
            }
            
            for (auto& thread : client_threads) {
                thread.join();
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start).count();
            
            double reqs_per_sec = (double)completed_requests / duration * 1000.0;
            
            std::cout << "同時クライアント数 " << client_count << ":" << std::endl;
            std::cout << "  完了要求数: " << completed_requests << std::endl;
            std::cout << "  要求/秒: " << std::fixed << std::setprecision(1) 
                      << reqs_per_sec << std::endl;
        }
        
        server_thread.join();
    }
    
    void measureLongTermStability() {
        std::cout << "長期安定性測定中..." << std::endl;
        
        ServiceServer<int, int> server("stability_test");
        ServiceClient<int, int> client("stability_test");
        
        std::atomic<bool> running{true};
        std::atomic<int> success_count{0};
        std::atomic<int> failure_count{0};
        
        // サーバー側スレッド
        std::thread server_thread([&server, &running]() {
            while (running) {
                if (server.hasRequest()) {
                    int request = server.getRequest();
                    server.sendResponse(request);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
        
        // クライアント側スレッド
        std::thread client_thread([&client, &running, &success_count, &failure_count]() {
            int request_id = 0;
            while (running) {
                client.sendRequest(request_id++);
                if (client.waitForResponse(1000000)) {  // 1秒タイムアウト
                    success_count++;
                } else {
                    failure_count++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        
        // 30秒間実行
        std::this_thread::sleep_for(std::chrono::seconds(30));
        running = false;
        
        server_thread.join();
        client_thread.join();
        
        int total_requests = success_count + failure_count;
        double success_rate = (double)success_count / total_requests * 100.0;
        
        std::cout << "長期安定性結果:" << std::endl;
        std::cout << "  総要求数: " << total_requests << std::endl;
        std::cout << "  成功数: " << success_count << std::endl;
        std::cout << "  失敗数: " << failure_count << std::endl;
        std::cout << "  成功率: " << std::fixed << std::setprecision(2) 
                  << success_rate << "%" << std::endl;
    }
    
    void analyzeLatency() {
        if (latencies_.empty()) return;
        
        std::sort(latencies_.begin(), latencies_.end());
        
        double mean = std::accumulate(latencies_.begin(), latencies_.end(), 0.0) / latencies_.size();
        double min_val = latencies_.front();
        double max_val = latencies_.back();
        double p50 = latencies_[latencies_.size() * 0.5];
        double p95 = latencies_[latencies_.size() * 0.95];
        double p99 = latencies_[latencies_.size() * 0.99];
        
        std::cout << "=== Service通信レイテンシ統計 (μs) ===" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "平均: " << mean << std::endl;
        std::cout << "最小: " << min_val << std::endl;
        std::cout << "最大: " << max_val << std::endl;
        std::cout << "50%ile: " << p50 << std::endl;
        std::cout << "95%ile: " << p95 << std::endl;
        std::cout << "99%ile: " << p99 << std::endl;
        
        if (p99 < 100.0) {
            std::cout << "🏆 優秀: 99%のレイテンシが100μs以下" << std::endl;
        } else if (p99 < 1000.0) {
            std::cout << "👍 良好: 99%のレイテンシが1ms以下" << std::endl;
        } else {
            std::cout << "⚠️  要改善: レイテンシが高めです" << std::endl;
        }
    }
};

// ベンチマーク実行
int main() {
    try {
        ServicePerformanceBenchmark benchmark;
        benchmark.runServiceBenchmark();
        
    } catch (const std::exception& e) {
        std::cerr << "ベンチマークエラー: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## ❓ よくある質問

### Q1. 要求に対する応答が返ってこない場合はどうすればよいですか？
**A**: 以下をチェックしてください：
- サーバーが正しく動作しているか
- タイムアウト時間が適切か
- サーバー側で例外が発生していないか
- トピック名が一致しているか

### Q2. 複数のクライアントが同時に要求した場合の処理順序は？
**A**: 基本的にFIFO（先入れ先出し）で処理されますが、サーバーの実装によって異なります。優先度が必要な場合は、カスタムキューを実装してください。

### Q3. サーバーがクラッシュした場合、クライアントはどうなりますか？
**A**: クライアントはタイムアウトエラーを受け取ります。再接続機能を実装することで、自動的に復旧できます。

### Q4. 大きなデータの送受信は可能ですか？
**A**: 可能ですが、共有メモリのサイズ制限があります。大きなデータは分割して送信するか、ファイルパスを送信してファイル経由で交換することをお勧めします。

## 🔧 トラブルシューティング

### よくある問題と解決法

```cpp
// Service通信診断ツール
#include "shm_service.hpp"
#include <iostream>

void diagnose_service_communication() {
    using namespace irlab::shm;
    
    std::cout << "=== Service通信診断 ===" << std::endl;
    
    try {
        // 1. サーバー作成テスト
        ServiceServer<int, int> server("diagnostic_service");
        std::cout << "✅ サーバー作成成功" << std::endl;
        
        // 2. クライアント作成テスト
        ServiceClient<int, int> client("diagnostic_service");
        std::cout << "✅ クライアント作成成功" << std::endl;
        
        // 3. 通信テスト
        std::thread server_thread([&server]() {
            for (int i = 0; i < 3; ++i) {
                if (server.hasRequest()) {
                    int request = server.getRequest();
                    std::cout << "サーバー: 要求受信 " << request << std::endl;
                    server.sendResponse(request * 2);
                    std::cout << "サーバー: 応答送信 " << (request * 2) << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        client.sendRequest(21);
        std::cout << "クライアント: 要求送信 21" << std::endl;
        
        if (client.waitForResponse(2000000)) {  // 2秒タイムアウト
            int response = client.getResponse();
            std::cout << "クライアント: 応答受信 " << response << std::endl;
            
            if (response == 42) {
                std::cout << "✅ 通信テスト成功" << std::endl;
            } else {
                std::cout << "❌ 通信テスト失敗: 期待値42、実際値" << response << std::endl;
            }
        } else {
            std::cout << "❌ 通信テスト失敗: タイムアウト" << std::endl;
        }
        
        server_thread.join();
        
    } catch (const std::exception& e) {
        std::cout << "❌ 診断エラー: " << e.what() << std::endl;
        std::cout << "以下を確認してください:" << std::endl;
        std::cout << "  - 共有メモリの権限設定" << std::endl;
        std::cout << "  - 他のプロセスがサービスを使用中でないか" << std::endl;
        std::cout << "  - システムリソースの不足" << std::endl;
    }
}

// エラー処理パターン
void robust_service_client_example() {
    using namespace irlab::shm;
    
    ServiceClient<int, int> client("robust_service");
    
    const int MAX_RETRIES = 3;
    const int TIMEOUT_MS = 5000;
    
    for (int retry = 0; retry < MAX_RETRIES; ++retry) {
        try {
            std::cout << "要求送信 (試行 " << (retry + 1) << "/" << MAX_RETRIES << ")" << std::endl;
            
            client.sendRequest(42);
            
            if (client.waitForResponse(TIMEOUT_MS * 1000)) {  // マイクロ秒に変換
                int response = client.getResponse();
                std::cout << "成功: 応答受信 " << response << std::endl;
                return;  // 成功
            } else {
                std::cout << "タイムアウト: 再試行します..." << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cout << "エラー: " << e.what() << std::endl;
            
            if (retry < MAX_RETRIES - 1) {
                std::cout << "再試行します..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            } else {
                std::cout << "最大再試行回数に達しました" << std::endl;
                throw;
            }
        }
    }
}
```

## 📚 次のステップ

Service通信をマスターしたら、以下の高度なトピックに挑戦してみましょう：

1. **[📡 Pub/Sub通信](tutorials_shm_pub_sub_jp.md)** - 高速ブロードキャスト通信
2. **[⚡ Action通信](tutorials_shm_action_jp.md)** - 長時間非同期処理
3. **[🐍 Python連携](tutorials_python_jp.md)** - PythonでService通信

---

## 📄 ライセンス情報

本ドキュメントで紹介しているサンプルコードは、shared-memory-based-handy-communication-manager プロジェクトの一部として **Apache License 2.0** の下で提供されています。

- ✅ **商用利用可能**: サンプルコードを商業プロジェクトで自由に使用
- ✅ **改変可能**: ニーズに合わせてコードを修正・拡張
- ✅ **再配布可能**: ライセンス表示を保持して再配布

詳細は[LICENSEファイル](../LICENSE)をご確認ください。

---

この完全ガイドで、Service通信の力を最大限に活用し、信頼性の高い分散システムを構築しましょう！ 🚀✨