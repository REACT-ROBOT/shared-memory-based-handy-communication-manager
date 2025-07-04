# ⚡ Action通信完全ガイド - 長時間非同期処理をマスターしよう
[[English](tutorials_shm_action_en.md) | 日本語]

## 🎯 このガイドで学べること

- **Action通信の深い理解**: 非同期処理パターンの設計思想から実装詳細まで
- **長時間処理の管理**: 進捗監視、キャンセル機能、エラーハンドリング
- **高度なワークフロー設計**: 並列処理、タスクチェーン、状態管理
- **実践的な応用例**: ファイル処理、機械学習、大容量データ変換

## 🧠 Action通信の深い理解

### 🏗️ アーキテクチャ解説

```cpp
// Action通信の内部構造
┌─────────────────────────────────────────────────────────────┐
│                   共有メモリ空間                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              アクションキュー                        │    │
│  │ [Goal 0][Goal 1][Goal 2]...[Goal N-1]             │    │
│  │    ↑                           ↑                    │    │
│  │  実行位置                    追加位置                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              フィードバックキュー                    │    │
│  │ [FB 0][FB 1][FB 2]...[FB N-1]                     │    │
│  │    ↑                           ↑                    │    │
│  │  読取位置                    書込位置                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              結果キュー                             │    │
│  │ [Result 0][Result 1]...[Result N-1]               │    │
│  │    ↑                           ↑                    │    │
│  │  読取位置                    書込位置                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  アクション状態管理:                                         │
│  - ゴールID (一意識別子)                                     │
│  - 実行状態 (待機/実行中/完了/キャンセル/エラー)              │
│  - 進捗情報 (完了率、残り時間、処理詳細)                      │
│  - キャンセルフラグ (非同期キャンセル要求)                    │
└─────────────────────────────────────────────────────────────┘

Multiple Clients ← [shared memory] → Single Server
      │                                        │
   ゴール送信                             アクション実行エンジン
   フィードバック受信                           │
   結果受信                                 長時間処理
   キャンセル要求                            進捗報告
      │                                   結果生成
   非同期監視
```

### ⚡ なぜ長時間処理に最適なのか？

**1. 非同期実行とフィードバック**
```cpp
// アクションの基本パターン
ActionClient<GoalType, ResultType, FeedbackType> client("action_server");

// ゴール送信（非ブロッキング）
uint64_t goal_id = client.sendGoal(goal_data);

// 他の処理を継続...
while (true) {
    // フィードバック確認
    FeedbackType feedback;
    if (client.getFeedback(goal_id, feedback)) {
        std::cout << "進捗: " << feedback.progress_percent << "%" << std::endl;
    }
    
    // 完了確認
    if (client.isComplete(goal_id)) {
        ResultType result = client.getResult(goal_id);
        break;
    }
    
    // 必要に応じてキャンセル
    if (user_cancel_request) {
        client.cancelGoal(goal_id);
        break;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

**2. 状態管理と進捗監視**
```cpp
// 内部的な状態管理
enum class ActionStatus {
    PENDING,        // 待機中
    ACTIVE,         // 実行中
    PREEMPTED,      // 中断された
    SUCCEEDED,      // 成功完了
    ABORTED,        // 異常終了
    REJECTED,       // 拒否された
    RECALLED        // 取り消された
};

struct ActionState {
    uint64_t goal_id;
    ActionStatus status;
    uint64_t start_time;
    uint64_t last_feedback_time;
    float progress_percent;
    uint32_t estimated_duration_ms;
    char status_text[256];
};

// 状態追跡システム
class ActionStateTracker {
    std::unordered_map<uint64_t, ActionState> active_actions_;
    std::mutex state_mutex_;
    
public:
    void updateProgress(uint64_t goal_id, float progress, const std::string& text) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        auto& state = active_actions_[goal_id];
        state.progress_percent = progress;
        state.last_feedback_time = getCurrentTimeUs();
        strncpy(state.status_text, text.c_str(), 255);
        
        // 自動的な完了時間推定
        if (progress > 0.0f) {
            uint64_t elapsed = state.last_feedback_time - state.start_time;
            state.estimated_duration_ms = (elapsed / progress) * (100.0f - progress) / 1000;
        }
    }
};
```

**3. 高度なワークフロー管理**
```cpp
// 複数アクションの並列実行
class ParallelActionManager {
    std::vector<ActionClient<GoalType, ResultType, FeedbackType>> clients_;
    std::vector<uint64_t> active_goals_;
    
public:
    void executeParallelActions(const std::vector<GoalType>& goals) {
        active_goals_.clear();
        
        // すべてのゴールを並列送信
        for (size_t i = 0; i < goals.size(); ++i) {
            uint64_t goal_id = clients_[i].sendGoal(goals[i]);
            active_goals_.push_back(goal_id);
        }
        
        // 完了監視
        while (!active_goals_.empty()) {
            for (auto it = active_goals_.begin(); it != active_goals_.end();) {
                size_t client_index = it - active_goals_.begin();
                
                if (clients_[client_index].isComplete(*it)) {
                    auto result = clients_[client_index].getResult(*it);
                    processResult(result);
                    it = active_goals_.erase(it);
                } else {
                    // フィードバック処理
                    FeedbackType feedback;
                    if (clients_[client_index].getFeedback(*it, feedback)) {
                        updateProgressDisplay(client_index, feedback);
                    }
                    ++it;
                }
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};
```

## 🚀 基本的な使い方

### 1. 簡単なファイル処理アクション

```cpp
#include "shm_action.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>

using namespace irlab::shm;

// ファイル処理ゴール
struct FileProcessGoal {
    char input_path[512];
    char output_path[512];
    char operation[64];      // "compress", "encrypt", "convert"
    float quality;           // 品質設定 (0.0 - 1.0)
    bool preserve_metadata;
};

// ファイル処理結果
struct FileProcessResult {
    bool success;
    uint64_t input_size;
    uint64_t output_size;
    uint32_t processing_time_ms;
    float compression_ratio;
    char error_message[256];
    char final_path[512];
};

// ファイル処理フィードバック
struct FileProcessFeedback {
    float progress_percent;
    uint64_t bytes_processed;
    uint64_t bytes_remaining;
    uint32_t estimated_time_remaining_ms;
    char current_stage[128];
    float current_speed_mbps;
};

// ファイル処理サーバー
class FileProcessingServer {
private:
    ActionServer<FileProcessGoal, FileProcessResult, FileProcessFeedback> server_;
    std::thread processing_thread_;
    std::atomic<bool> running_;
    
public:
    FileProcessingServer() : server_("file_processor"), running_(false) {}
    
    void start() {
        running_ = true;
        processing_thread_ = std::thread(&FileProcessingServer::processLoop, this);
        std::cout << "ファイル処理サーバーを開始しました" << std::endl;
    }
    
    void stop() {
        running_ = false;
        if (processing_thread_.joinable()) {
            processing_thread_.join();
        }
        std::cout << "ファイル処理サーバーを停止しました" << std::endl;
    }
    
private:
    void processLoop() {
        while (running_) {
            if (server_.hasGoal()) {
                auto start_time = std::chrono::high_resolution_clock::now();
                
                uint64_t goal_id = server_.acceptGoal();
                FileProcessGoal goal = server_.getGoal(goal_id);
                
                std::cout << "ファイル処理開始: " << goal.input_path 
                          << " -> " << goal.output_path << std::endl;
                
                // 非同期でファイル処理を実行
                std::thread worker([this, goal_id, goal, start_time]() {
                    processFile(goal_id, goal, start_time);
                });
                worker.detach();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void processFile(uint64_t goal_id, const FileProcessGoal& goal, 
                    std::chrono::high_resolution_clock::time_point start_time) {
        FileProcessResult result;
        result.success = false;
        result.input_size = 0;
        result.output_size = 0;
        result.compression_ratio = 1.0f;
        
        try {
            // 入力ファイルサイズ取得
            std::ifstream input(goal.input_path, std::ios::binary | std::ios::ate);
            if (!input.is_open()) {
                strncpy(result.error_message, "入力ファイルを開けません", 255);
                server_.setAborted(goal_id, result);
                return;
            }
            
            result.input_size = input.tellg();
            input.seekg(0, std::ios::beg);
            
            // 出力ファイル準備
            std::ofstream output(goal.output_path, std::ios::binary);
            if (!output.is_open()) {
                strncpy(result.error_message, "出力ファイルを作成できません", 255);
                server_.setAborted(goal_id, result);
                return;
            }
            
            // ファイル処理実行
            const size_t BUFFER_SIZE = 64 * 1024;  // 64KB buffer
            std::vector<char> buffer(BUFFER_SIZE);
            uint64_t total_processed = 0;
            
            FileProcessFeedback feedback;
            
            while (input.read(buffer.data(), BUFFER_SIZE) || input.gcount() > 0) {
                // キャンセル確認
                if (server_.isPreemptRequested(goal_id)) {
                    result.success = false;
                    strncpy(result.error_message, "処理がキャンセルされました", 255);
                    server_.setPreempted(goal_id, result);
                    input.close();
                    output.close();
                    return;
                }
                
                size_t bytes_read = input.gcount();
                total_processed += bytes_read;
                
                // データ処理シミュレーション（実際の処理に置き換え）
                processData(buffer.data(), bytes_read, goal.operation, goal.quality);
                
                // 処理データの書き込み
                output.write(buffer.data(), bytes_read);
                
                // フィードバック更新
                feedback.progress_percent = (float)total_processed / result.input_size * 100.0f;
                feedback.bytes_processed = total_processed;
                feedback.bytes_remaining = result.input_size - total_processed;
                
                auto current_time = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    current_time - start_time);
                
                if (elapsed.count() > 0 && total_processed > 0) {
                    feedback.current_speed_mbps = (float)total_processed / elapsed.count() / 1024.0f;
                    if (feedback.progress_percent > 0) {
                        feedback.estimated_time_remaining_ms = 
                            elapsed.count() * (100.0f - feedback.progress_percent) / feedback.progress_percent;
                    }
                }
                
                snprintf(feedback.current_stage, 127, "%s処理中", goal.operation);
                
                server_.publishFeedback(goal_id, feedback);
                
                // 処理速度調整（実際の処理負荷に応じて）
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            input.close();
            output.close();
            
            // 結果設定
            std::ifstream output_check(goal.output_path, std::ios::binary | std::ios::ate);
            result.output_size = output_check.tellg();
            output_check.close();
            
            result.compression_ratio = (float)result.output_size / result.input_size;
            
            auto end_time = std::chrono::high_resolution_clock::now();
            result.processing_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();
            
            strncpy(result.final_path, goal.output_path, 511);
            result.success = true;
            
            server_.setSucceeded(goal_id, result);
            
            std::cout << "ファイル処理完了: " << result.processing_time_ms << "ms, "
                      << "圧縮率: " << result.compression_ratio << std::endl;
                      
        } catch (const std::exception& e) {
            strncpy(result.error_message, e.what(), 255);
            server_.setAborted(goal_id, result);
            std::cout << "ファイル処理エラー: " << e.what() << std::endl;
        }
    }
    
    void processData(char* data, size_t size, const char* operation, float quality) {
        // 実際のデータ処理アルゴリズム
        // この例では簡単な変換をシミュレート
        
        if (strcmp(operation, "encrypt") == 0) {
            // 簡単な暗号化シミュレーション
            for (size_t i = 0; i < size; ++i) {
                data[i] ^= 0x55;  // XOR暗号化
            }
        } else if (strcmp(operation, "compress") == 0) {
            // 圧縮シミュレーション（実際にはzlib等を使用）
            // quality値に基づいて処理時間を調整
            auto delay = std::chrono::microseconds((int)(100 * quality));
            std::this_thread::sleep_for(delay);
        }
    }
};

// ファイル処理クライアント
class FileProcessingClient {
private:
    ActionClient<FileProcessGoal, FileProcessResult, FileProcessFeedback> client_;
    
public:
    FileProcessingClient() : client_("file_processor") {}
    
    bool processFile(const std::string& input_path, const std::string& output_path,
                    const std::string& operation, float quality = 0.8f) {
        FileProcessGoal goal;
        strncpy(goal.input_path, input_path.c_str(), 511);
        strncpy(goal.output_path, output_path.c_str(), 511);
        strncpy(goal.operation, operation.c_str(), 63);
        goal.quality = quality;
        goal.preserve_metadata = true;
        
        std::cout << "ファイル処理要求送信..." << std::endl;
        uint64_t goal_id = client_.sendGoal(goal);
        
        // 進捗監視
        while (!client_.isComplete(goal_id)) {
            FileProcessFeedback feedback;
            if (client_.getFeedback(goal_id, feedback)) {
                std::cout << "\r進捗: " << std::fixed << std::setprecision(1) 
                          << feedback.progress_percent << "% ("
                          << feedback.current_stage << ", "
                          << feedback.current_speed_mbps << " MB/s, "
                          << "残り: " << feedback.estimated_time_remaining_ms / 1000 << "s)"
                          << std::flush;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        
        std::cout << std::endl;
        
        // 結果取得
        if (client_.getState(goal_id) == ActionStatus::SUCCEEDED) {
            FileProcessResult result = client_.getResult(goal_id);
            
            std::cout << "処理完了!" << std::endl;
            std::cout << "  入力サイズ: " << result.input_size << " bytes" << std::endl;
            std::cout << "  出力サイズ: " << result.output_size << " bytes" << std::endl;
            std::cout << "  処理時間: " << result.processing_time_ms << " ms" << std::endl;
            std::cout << "  圧縮率: " << result.compression_ratio << std::endl;
            
            return true;
        } else {
            FileProcessResult result = client_.getResult(goal_id);
            std::cout << "処理失敗: " << result.error_message << std::endl;
            return false;
        }
    }
    
    void cancelProcessing(uint64_t goal_id) {
        client_.cancelGoal(goal_id);
        std::cout << "処理キャンセル要求を送信しました" << std::endl;
    }
};

// 使用例
int main() {
    try {
        // サーバー開始
        FileProcessingServer server;
        server.start();
        
        // クライアントテスト
        FileProcessingClient client;
        
        // ファイル処理実行
        bool success = client.processFile(
            "/tmp/input.txt",
            "/tmp/output.enc",
            "encrypt",
            0.9f
        );
        
        if (success) {
            std::cout << "ファイル処理が成功しました" << std::endl;
        }
        
        // サーバー停止
        std::this_thread::sleep_for(std::chrono::seconds(2));
        server.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "エラー: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### 2. 機械学習訓練アクション

```cpp
// 機械学習訓練のデータ構造
struct MLTrainingGoal {
    char dataset_path[512];
    char model_type[64];         // "neural_network", "svm", "random_forest"
    char output_model_path[512];
    
    // ハイパーパラメータ
    float learning_rate;
    int epochs;
    int batch_size;
    float validation_split;
    
    // 設定フラグ
    bool use_gpu;
    bool early_stopping;
    int max_training_time_minutes;
};

struct MLTrainingResult {
    bool success;
    float final_accuracy;
    float final_loss;
    int epochs_completed;
    uint32_t training_time_ms;
    uint64_t model_size_bytes;
    char model_path[512];
    char error_details[512];
    
    // 詳細メトリクス
    float validation_accuracy;
    float validation_loss;
    float overfitting_score;
    char best_checkpoint_path[512];
};

struct MLTrainingFeedback {
    int current_epoch;
    int total_epochs;
    float current_accuracy;
    float current_loss;
    float validation_accuracy;
    float validation_loss;
    
    // 進捗情報
    float epoch_progress_percent;
    float overall_progress_percent;
    uint32_t epoch_time_ms;
    uint32_t estimated_remaining_ms;
    
    // リアルタイム統計
    float samples_per_second;
    char current_phase[64];      // "training", "validation", "checkpointing"
    bool is_improving;
};

// 機械学習訓練サーバー
class MLTrainingServer {
private:
    ActionServer<MLTrainingGoal, MLTrainingResult, MLTrainingFeedback> server_;
    std::thread training_thread_;
    std::atomic<bool> running_;
    
public:
    MLTrainingServer() : server_("ml_trainer"), running_(false) {}
    
    void start() {
        running_ = true;
        training_thread_ = std::thread(&MLTrainingServer::trainingLoop, this);
        std::cout << "機械学習訓練サーバーを開始しました" << std::endl;
    }
    
    void stop() {
        running_ = false;
        if (training_thread_.joinable()) {
            training_thread_.join();
        }
        std::cout << "機械学習訓練サーバーを停止しました" << std::endl;
    }
    
private:
    void trainingLoop() {
        while (running_) {
            if (server_.hasGoal()) {
                uint64_t goal_id = server_.acceptGoal();
                MLTrainingGoal goal = server_.getGoal(goal_id);
                
                std::cout << "訓練開始: " << goal.model_type 
                          << ", データセット: " << goal.dataset_path << std::endl;
                
                // 訓練を非同期で実行
                std::thread worker([this, goal_id, goal]() {
                    trainModel(goal_id, goal);
                });
                worker.detach();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void trainModel(uint64_t goal_id, const MLTrainingGoal& goal) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        MLTrainingResult result;
        result.success = false;
        result.epochs_completed = 0;
        result.final_accuracy = 0.0f;
        result.final_loss = std::numeric_limits<float>::max();
        
        try {
            // データセット読み込み
            if (!loadDataset(goal.dataset_path)) {
                strncpy(result.error_details, "データセットの読み込みに失敗", 511);
                server_.setAborted(goal_id, result);
                return;
            }
            
            // モデル初期化
            MLModel model = initializeModel(goal.model_type, goal);
            
            MLTrainingFeedback feedback;
            float best_accuracy = 0.0f;
            int epochs_without_improvement = 0;
            
            // エポックループ
            for (int epoch = 0; epoch < goal.epochs; ++epoch) {
                // キャンセル確認
                if (server_.isPreemptRequested(goal_id)) {
                    strncpy(result.error_details, "訓練がキャンセルされました", 511);
                    server_.setPreempted(goal_id, result);
                    return;
                }
                
                auto epoch_start = std::chrono::high_resolution_clock::now();
                
                // 訓練フェーズ
                feedback.current_epoch = epoch + 1;
                feedback.total_epochs = goal.epochs;
                strcpy(feedback.current_phase, "training");
                
                float epoch_loss = 0.0f;
                float epoch_accuracy = 0.0f;
                
                // バッチ処理ループ
                int total_batches = getDatasetSize() / goal.batch_size;
                for (int batch = 0; batch < total_batches; ++batch) {
                    // バッチデータ取得
                    auto batch_data = getBatch(batch, goal.batch_size);
                    
                    // フォワードパス
                    auto predictions = model.forward(batch_data.inputs);
                    
                    // 損失計算
                    float batch_loss = calculateLoss(predictions, batch_data.targets);
                    epoch_loss += batch_loss;
                    
                    // バックプロパゲーション
                    model.backward(batch_loss, goal.learning_rate);
                    
                    // 精度計算
                    float batch_accuracy = calculateAccuracy(predictions, batch_data.targets);
                    epoch_accuracy += batch_accuracy;
                    
                    // バッチレベルフィードバック
                    feedback.epoch_progress_percent = (float)(batch + 1) / total_batches * 100.0f;
                    feedback.overall_progress_percent = 
                        ((float)epoch + feedback.epoch_progress_percent / 100.0f) / goal.epochs * 100.0f;
                    
                    auto current_time = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        current_time - epoch_start);
                    feedback.samples_per_second = (float)(batch + 1) * goal.batch_size / 
                                                 (elapsed.count() / 1000.0f);
                    
                    server_.publishFeedback(goal_id, feedback);
                    
                    // 適度な間隔でフィードバック送信（CPU負荷軽減）
                    if (batch % 10 == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                
                epoch_loss /= total_batches;
                epoch_accuracy /= total_batches;
                
                // 検証フェーズ
                strcpy(feedback.current_phase, "validation");
                server_.publishFeedback(goal_id, feedback);
                
                auto validation_result = validateModel(model, goal.validation_split);
                
                feedback.current_accuracy = epoch_accuracy;
                feedback.current_loss = epoch_loss;
                feedback.validation_accuracy = validation_result.accuracy;
                feedback.validation_loss = validation_result.loss;
                
                auto epoch_end = std::chrono::high_resolution_clock::now();
                feedback.epoch_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    epoch_end - epoch_start).count();
                
                // 残り時間推定
                if (epoch > 0) {
                    feedback.estimated_remaining_ms = 
                        feedback.epoch_time_ms * (goal.epochs - epoch - 1);
                }
                
                // 改善判定
                feedback.is_improving = validation_result.accuracy > best_accuracy;
                if (feedback.is_improving) {
                    best_accuracy = validation_result.accuracy;
                    epochs_without_improvement = 0;
                    
                    // ベストモデルを保存
                    saveCheckpoint(model, goal.output_model_path, epoch);
                } else {
                    epochs_without_improvement++;
                }
                
                server_.publishFeedback(goal_id, feedback);
                
                // 早期停止チェック
                if (goal.early_stopping && epochs_without_improvement >= 10) {
                    std::cout << "早期停止: " << epochs_without_improvement 
                              << " エポック改善なし" << std::endl;
                    break;
                }
                
                result.epochs_completed = epoch + 1;
                result.final_accuracy = validation_result.accuracy;
                result.final_loss = validation_result.loss;
                
                std::cout << "エポック " << (epoch + 1) << "/" << goal.epochs 
                          << " - 損失: " << epoch_loss << ", 精度: " << epoch_accuracy
                          << ", 検証精度: " << validation_result.accuracy << std::endl;
            }
            
            // 最終結果設定
            auto end_time = std::chrono::high_resolution_clock::now();
            result.training_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time).count();
            
            // 最終モデル保存
            saveModel(model, goal.output_model_path);
            result.model_size_bytes = getFileSize(goal.output_model_path);
            strncpy(result.model_path, goal.output_model_path, 511);
            
            result.validation_accuracy = feedback.validation_accuracy;
            result.validation_loss = feedback.validation_loss;
            result.overfitting_score = calculateOverfittingScore(
                feedback.current_accuracy, feedback.validation_accuracy);
            
            result.success = true;
            server_.setSucceeded(goal_id, result);
            
            std::cout << "訓練完了: " << result.training_time_ms / 1000.0f << "秒, "
                      << "最終精度: " << result.final_accuracy << std::endl;
                      
        } catch (const std::exception& e) {
            strncpy(result.error_details, e.what(), 511);
            server_.setAborted(goal_id, result);
            std::cout << "訓練エラー: " << e.what() << std::endl;
        }
    }
    
    // 簡略化されたML関数群（実際の実装ではより高度な処理）
    bool loadDataset(const std::string& path) {
        // データセット読み込みシミュレーション
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        return true;
    }
    
    struct MLModel {
        void forward(const std::vector<float>& inputs) { /* ... */ }
        void backward(float loss, float learning_rate) { /* ... */ }
    };
    
    MLModel initializeModel(const std::string& type, const MLTrainingGoal& goal) {
        return MLModel{};
    }
    
    struct ValidationResult {
        float accuracy;
        float loss;
    };
    
    ValidationResult validateModel(const MLModel& model, float validation_split) {
        // 検証処理シミュレーション
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // ランダムな結果生成（実際の実装では真の検証）
        ValidationResult result;
        result.accuracy = 0.7f + (rand() % 300) / 1000.0f;  // 0.7-1.0
        result.loss = 1.0f - result.accuracy + (rand() % 100) / 1000.0f;
        return result;
    }
    
    void saveCheckpoint(const MLModel& model, const std::string& path, int epoch) {
        // チェックポイント保存
        std::string checkpoint_path = path + "_epoch_" + std::to_string(epoch);
        // 実際の保存処理...
    }
    
    void saveModel(const MLModel& model, const std::string& path) {
        // 最終モデル保存
        // 実際の保存処理...
    }
    
    size_t getFileSize(const std::string& path) {
        return 1024 * 1024;  // 1MB（例）
    }
    
    float calculateOverfittingScore(float train_acc, float val_acc) {
        return std::max(0.0f, train_acc - val_acc);
    }
};
```

## 🚀 実践的な使用例

### 1. 大容量データ変換パイプライン

```cpp
#include "shm_action.hpp"
#include <vector>
#include <queue>
#include <thread>
#include <future>

// データ変換パイプラインのゴール
struct DataPipelineGoal {
    char input_directory[512];
    char output_directory[512];
    char pipeline_config[1024];
    
    // パイプライン設定
    int max_parallel_jobs;
    int batch_size;
    float quality_threshold;
    bool skip_existing;
    
    // フィルタリング設定
    char file_pattern[128];
    uint64_t min_file_size;
    uint64_t max_file_size;
};

struct DataPipelineResult {
    bool success;
    uint32_t total_files;
    uint32_t processed_files;
    uint32_t failed_files;
    uint32_t skipped_files;
    
    uint64_t total_input_size;
    uint64_t total_output_size;
    uint32_t total_processing_time_ms;
    
    float average_processing_speed_mbps;
    float overall_compression_ratio;
    char summary_report[2048];
    char error_log_path[512];
};

struct DataPipelineFeedback {
    uint32_t current_file_index;
    uint32_t total_files;
    char current_file_name[256];
    
    // 全体進捗
    float overall_progress_percent;
    uint32_t files_completed;
    uint32_t files_failed;
    
    // 現在の処理
    float current_file_progress;
    char current_stage[128];
    
    // パフォーマンス統計
    float current_speed_mbps;
    uint32_t estimated_remaining_ms;
    uint32_t active_workers;
    
    // 品質統計
    float average_quality_score;
    uint32_t quality_failures;
};

// 高度なデータ処理パイプライン
class DataPipelineServer {
private:
    ActionServer<DataPipelineGoal, DataPipelineResult, DataPipelineFeedback> server_;
    std::thread pipeline_thread_;
    std::atomic<bool> running_;
    
public:
    DataPipelineServer() : server_("data_pipeline"), running_(false) {}
    
    void start() {
        running_ = true;
        pipeline_thread_ = std::thread(&DataPipelineServer::pipelineLoop, this);
        std::cout << "データパイプラインサーバーを開始しました" << std::endl;
    }
    
    void stop() {
        running_ = false;
        if (pipeline_thread_.joinable()) {
            pipeline_thread_.join();
        }
        std::cout << "データパイプラインサーバーを停止しました" << std::endl;
    }
    
private:
    void pipelineLoop() {
        while (running_) {
            if (server_.hasGoal()) {
                uint64_t goal_id = server_.acceptGoal();
                DataPipelineGoal goal = server_.getGoal(goal_id);
                
                std::cout << "パイプライン開始: " << goal.input_directory 
                          << " -> " << goal.output_directory << std::endl;
                
                // パイプライン実行を非同期で開始
                std::thread worker([this, goal_id, goal]() {
                    executePipeline(goal_id, goal);
                });
                worker.detach();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    void executePipeline(uint64_t goal_id, const DataPipelineGoal& goal) {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        DataPipelineResult result;
        result.success = false;
        result.total_files = 0;
        result.processed_files = 0;
        result.failed_files = 0;
        result.skipped_files = 0;
        
        try {
            // ファイルリスト取得
            std::vector<std::string> file_list = scanDirectory(goal.input_directory, goal);
            result.total_files = file_list.size();
            
            if (file_list.empty()) {
                strncpy(result.summary_report, "処理対象ファイルが見つかりませんでした", 2047);
                server_.setSucceeded(goal_id, result);
                return;
            }
            
            // 並列処理の準備
            std::vector<std::thread> workers;
            std::queue<std::string> file_queue;
            std::mutex queue_mutex;
            std::atomic<uint32_t> completed_files{0};
            std::atomic<uint32_t> failed_files{0};
            std::atomic<uint64_t> total_input_bytes{0};
            std::atomic<uint64_t> total_output_bytes{0};
            
            // ファイルキューに追加
            for (const auto& file : file_list) {
                file_queue.push(file);
            }
            
            DataPipelineFeedback feedback;
            feedback.total_files = result.total_files;
            feedback.active_workers = std::min((uint32_t)goal.max_parallel_jobs, result.total_files);
            
            // ワーカースレッド開始
            for (int i = 0; i < goal.max_parallel_jobs && !file_queue.empty(); ++i) {
                workers.emplace_back([&, i]() {
                    workerFunction(goal_id, goal, file_queue, queue_mutex, 
                                 completed_files, failed_files, 
                                 total_input_bytes, total_output_bytes, feedback);
                });
            }
            
            // 進捗監視
            while (completed_files + failed_files < result.total_files) {
                // キャンセル確認
                if (server_.isPreemptRequested(goal_id)) {
                    // すべてのワーカーに停止シグナル送信
                    for (auto& worker : workers) {
                        if (worker.joinable()) {
                            worker.join();
                        }
                    }
                    
                    result.processed_files = completed_files;
                    result.failed_files = failed_files;
                    strncpy(result.summary_report, "処理がキャンセルされました", 2047);
                    server_.setPreempted(goal_id, result);
                    return;
                }
                
                // フィードバック更新
                feedback.files_completed = completed_files;
                feedback.files_failed = failed_files;
                feedback.overall_progress_percent = 
                    (float)(completed_files + failed_files) / result.total_files * 100.0f;
                
                auto current_time = std::chrono::high_resolution_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    current_time - start_time);
                
                if (elapsed.count() > 0 && completed_files > 0) {
                    feedback.current_speed_mbps = 
                        (float)total_input_bytes / elapsed.count() / 1024.0f;
                    
                    if (feedback.overall_progress_percent > 0) {
                        feedback.estimated_remaining_ms = 
                            elapsed.count() * (100.0f - feedback.overall_progress_percent) / 
                            feedback.overall_progress_percent;
                    }
                }
                
                server_.publishFeedback(goal_id, feedback);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            
            // ワーカー終了待ち
            for (auto& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            
            // 最終結果設定
            result.processed_files = completed_files;
            result.failed_files = failed_files;
            result.total_input_size = total_input_bytes;
            result.total_output_size = total_output_bytes;
            
            auto end_time = std::chrono::high_resolution_clock::now();
            result.total_processing_time_ms = 
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time).count();
            
            if (result.total_processing_time_ms > 0) {
                result.average_processing_speed_mbps = 
                    (float)result.total_input_size / result.total_processing_time_ms / 1024.0f;
            }
            
            if (result.total_input_size > 0) {
                result.overall_compression_ratio = 
                    (float)result.total_output_size / result.total_input_size;
            }
            
            // サマリーレポート生成
            generateSummaryReport(result, goal);
            
            result.success = (result.failed_files == 0);
            server_.setSucceeded(goal_id, result);
            
            std::cout << "パイプライン完了: " << result.processed_files << "/" 
                      << result.total_files << " ファイル処理" << std::endl;
                      
        } catch (const std::exception& e) {
            snprintf(result.summary_report, 2047, "パイプラインエラー: %s", e.what());
            server_.setAborted(goal_id, result);
            std::cout << "パイプラインエラー: " << e.what() << std::endl;
        }
    }
    
    void workerFunction(uint64_t goal_id, const DataPipelineGoal& goal,
                       std::queue<std::string>& file_queue, std::mutex& queue_mutex,
                       std::atomic<uint32_t>& completed_files, std::atomic<uint32_t>& failed_files,
                       std::atomic<uint64_t>& total_input_bytes, std::atomic<uint64_t>& total_output_bytes,
                       DataPipelineFeedback& feedback) {
        
        while (true) {
            std::string current_file;
            
            // ファイル取得
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (file_queue.empty()) {
                    break;
                }
                current_file = file_queue.front();
                file_queue.pop();
            }
            
            try {
                // ファイル処理実行
                auto process_result = processFile(current_file, goal);
                
                if (process_result.success) {
                    completed_files++;
                    total_input_bytes += process_result.input_size;
                    total_output_bytes += process_result.output_size;
                } else {
                    failed_files++;
                }
                
                // 個別ファイルのフィードバック更新
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    strncpy(feedback.current_file_name, 
                           extractFileName(current_file).c_str(), 255);
                    feedback.current_file_index = completed_files + failed_files;
                }
                
            } catch (const std::exception& e) {
                failed_files++;
                std::cout << "ファイル処理エラー (" << current_file << "): " 
                          << e.what() << std::endl;
            }
        }
    }
    
    struct FileProcessResult {
        bool success;
        uint64_t input_size;
        uint64_t output_size;
        uint32_t processing_time_ms;
        float quality_score;
    };
    
    FileProcessResult processFile(const std::string& file_path, 
                                 const DataPipelineGoal& goal) {
        FileProcessResult result;
        result.success = false;
        result.input_size = 0;
        result.output_size = 0;
        result.quality_score = 0.0f;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // ファイルサイズ取得
        result.input_size = getFileSize(file_path);
        
        // 実際のファイル処理（例：画像変換、データ圧縮等）
        std::string output_path = generateOutputPath(file_path, goal.output_directory);
        
        // 処理シミュレーション
        std::this_thread::sleep_for(std::chrono::milliseconds(50 + rand() % 200));
        
        // 品質チェック
        result.quality_score = 0.8f + (rand() % 200) / 1000.0f;
        
        if (result.quality_score >= goal.quality_threshold) {
            result.output_size = result.input_size * 0.7f;  // 圧縮例
            result.success = true;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.processing_time_ms = 
            std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        return result;
    }
    
    std::vector<std::string> scanDirectory(const std::string& directory, 
                                          const DataPipelineGoal& goal) {
        // ディレクトリスキャン実装
        std::vector<std::string> files;
        
        // 例：100ファイルを生成
        for (int i = 0; i < 100; ++i) {
            files.push_back(directory + "/file_" + std::to_string(i) + ".dat");
        }
        
        return files;
    }
    
    void generateSummaryReport(DataPipelineResult& result, const DataPipelineGoal& goal) {
        snprintf(result.summary_report, 2047,
                "パイプライン処理完了\n"
                "総ファイル数: %u\n"
                "成功: %u\n"
                "失敗: %u\n"
                "スキップ: %u\n"
                "処理時間: %.2f秒\n"
                "平均速度: %.2f MB/s\n"
                "圧縮率: %.2f\n",
                result.total_files,
                result.processed_files,
                result.failed_files,
                result.skipped_files,
                result.total_processing_time_ms / 1000.0f,
                result.average_processing_speed_mbps,
                result.overall_compression_ratio);
    }
};
```

## 🛠️ パフォーマンス最適化テクニック

### 1. 状態管理の最適化

```cpp
#include <unordered_map>
#include <memory>
#include <atomic>

// 高効率な状態管理システム
class OptimizedActionStateManager {
private:
    struct ActionState {
        std::atomic<ActionStatus> status;
        std::atomic<float> progress;
        std::atomic<uint64_t> last_update_time;
        std::string status_text;
        std::mutex text_mutex;
    };
    
    std::unordered_map<uint64_t, std::unique_ptr<ActionState>> states_;
    std::shared_mutex states_mutex_;
    
    // 状態変更通知システム
    std::vector<std::function<void(uint64_t, ActionStatus)>> status_callbacks_;
    std::mutex callbacks_mutex_;
    
public:
    uint64_t createAction(uint64_t goal_id) {
        std::unique_lock<std::shared_mutex> lock(states_mutex_);
        
        auto state = std::make_unique<ActionState>();
        state->status.store(ActionStatus::PENDING);
        state->progress.store(0.0f);
        state->last_update_time.store(getCurrentTimeUs());
        
        states_[goal_id] = std::move(state);
        
        notifyStatusChange(goal_id, ActionStatus::PENDING);
        return goal_id;
    }
    
    void updateProgress(uint64_t goal_id, float progress, const std::string& text = "") {
        std::shared_lock<std::shared_mutex> lock(states_mutex_);
        
        auto it = states_.find(goal_id);
        if (it != states_.end()) {
            it->second->progress.store(progress);
            it->second->last_update_time.store(getCurrentTimeUs());
            
            if (!text.empty()) {
                std::lock_guard<std::mutex> text_lock(it->second->text_mutex);
                it->second->status_text = text;
            }
        }
    }
    
    void setStatus(uint64_t goal_id, ActionStatus status) {
        std::shared_lock<std::shared_mutex> lock(states_mutex_);
        
        auto it = states_.find(goal_id);
        if (it != states_.end()) {
            ActionStatus old_status = it->second->status.exchange(status);
            if (old_status != status) {
                notifyStatusChange(goal_id, status);
            }
        }
    }
    
    ActionStatus getStatus(uint64_t goal_id) const {
        std::shared_lock<std::shared_mutex> lock(states_mutex_);
        
        auto it = states_.find(goal_id);
        return (it != states_.end()) ? it->second->status.load() : ActionStatus::PENDING;
    }
    
    float getProgress(uint64_t goal_id) const {
        std::shared_lock<std::shared_mutex> lock(states_mutex_);
        
        auto it = states_.find(goal_id);
        return (it != states_.end()) ? it->second->progress.load() : 0.0f;
    }
    
    void addStatusCallback(std::function<void(uint64_t, ActionStatus)> callback) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        status_callbacks_.push_back(callback);
    }
    
    // 古い状態のクリーンアップ
    void cleanupExpiredStates(uint64_t expiry_time_us) {
        std::unique_lock<std::shared_mutex> lock(states_mutex_);
        
        auto current_time = getCurrentTimeUs();
        
        for (auto it = states_.begin(); it != states_.end();) {
            if (current_time - it->second->last_update_time.load() > expiry_time_us) {
                it = states_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
private:
    void notifyStatusChange(uint64_t goal_id, ActionStatus status) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        for (const auto& callback : status_callbacks_) {
            try {
                callback(goal_id, status);
            } catch (const std::exception& e) {
                std::cerr << "Status callback error: " << e.what() << std::endl;
            }
        }
    }
    
    uint64_t getCurrentTimeUs() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};
```

### 2. 高性能フィードバック配信

```cpp
// バッチ化フィードバックシステム
class BatchedFeedbackSystem {
private:
    struct FeedbackBatch {
        std::vector<uint64_t> goal_ids;
        std::vector<std::string> feedback_data;
        uint64_t timestamp;
    };
    
    std::queue<FeedbackBatch> pending_batches_;
    std::mutex batch_mutex_;
    std::condition_variable batch_cv_;
    std::thread delivery_thread_;
    std::atomic<bool> running_;
    
    // 設定パラメータ
    size_t max_batch_size_;
    uint32_t batch_timeout_ms_;
    
public:
    BatchedFeedbackSystem(size_t max_batch_size = 100, uint32_t batch_timeout_ms = 50)
        : max_batch_size_(max_batch_size), batch_timeout_ms_(batch_timeout_ms), running_(false) {}
    
    void start() {
        running_ = true;
        delivery_thread_ = std::thread(&BatchedFeedbackSystem::deliveryLoop, this);
    }
    
    void stop() {
        running_ = false;
        batch_cv_.notify_all();
        if (delivery_thread_.joinable()) {
            delivery_thread_.join();
        }
    }
    
    void addFeedback(uint64_t goal_id, const std::string& feedback) {
        std::lock_guard<std::mutex> lock(batch_mutex_);
        
        // 現在のバッチに追加または新しいバッチ作成
        if (pending_batches_.empty() || 
            pending_batches_.back().goal_ids.size() >= max_batch_size_) {
            
            FeedbackBatch new_batch;
            new_batch.timestamp = getCurrentTimeMs();
            pending_batches_.push(new_batch);
        }
        
        auto& current_batch = pending_batches_.back();
        current_batch.goal_ids.push_back(goal_id);
        current_batch.feedback_data.push_back(feedback);
        
        // バッチが満杯になったら即座に配信トリガー
        if (current_batch.goal_ids.size() >= max_batch_size_) {
            batch_cv_.notify_one();
        }
    }
    
private:
    void deliveryLoop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(batch_mutex_);
            
            // タイムアウトまたは新しいバッチ待機
            batch_cv_.wait_for(lock, std::chrono::milliseconds(batch_timeout_ms_),
                              [this] { return !pending_batches_.empty() || !running_; });
            
            if (!running_) break;
            
            // 配信対象バッチの収集
            std::vector<FeedbackBatch> batches_to_deliver;
            uint64_t current_time = getCurrentTimeMs();
            
            while (!pending_batches_.empty()) {
                auto& batch = pending_batches_.front();
                
                // タイムアウトまたはサイズ条件で配信
                if (batch.goal_ids.size() >= max_batch_size_ ||
                    (current_time - batch.timestamp) >= batch_timeout_ms_) {
                    
                    batches_to_deliver.push_back(std::move(batch));
                    pending_batches_.pop();
                } else {
                    break;  // まだ配信時期ではない
                }
            }
            
            lock.unlock();
            
            // バッチ配信実行
            for (const auto& batch : batches_to_deliver) {
                deliverBatch(batch);
            }
        }
    }
    
    void deliverBatch(const FeedbackBatch& batch) {
        try {
            // 実際のフィードバック配信処理
            // 共有メモリへの一括書き込み等
            
            std::cout << "フィードバックバッチ配信: " << batch.goal_ids.size() 
                      << " items" << std::endl;
                      
        } catch (const std::exception& e) {
            std::cerr << "Feedback delivery error: " << e.what() << std::endl;
        }
    }
    
    uint64_t getCurrentTimeMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
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

class ActionPerformanceBenchmark {
private:
    struct BenchmarkResult {
        std::vector<double> goal_submission_times;
        std::vector<double> feedback_latencies;
        std::vector<double> completion_times;
        std::vector<double> throughput_measurements;
    };
    
    BenchmarkResult results_;
    
public:
    void runActionBenchmark() {
        std::cout << "=== Action通信ベンチマーク開始 ===" << std::endl;
        
        // 1. ゴール送信レイテンシテスト
        measureGoalSubmissionLatency();
        
        // 2. フィードバック配信レイテンシテスト
        measureFeedbackLatency();
        
        // 3. 長時間アクションのスループットテスト
        measureLongRunningActionThroughput();
        
        // 4. 並列アクション処理テスト
        measureConcurrentActionProcessing();
        
        // 5. メモリ効率テスト
        measureMemoryEfficiency();
        
        // 結果解析
        analyzeResults();
    }
    
private:
    void measureGoalSubmissionLatency() {
        std::cout << "ゴール送信レイテンシ測定中..." << std::endl;
        
        ActionServer<int, int, float> server("latency_test");
        ActionClient<int, int, float> client("latency_test");
        
        // サーバー側スレッド
        std::thread server_thread([&server]() {
            for (int i = 0; i < 1000; ++i) {
                if (server.hasGoal()) {
                    uint64_t goal_id = server.acceptGoal();
                    int goal = server.getGoal(goal_id);
                    
                    // 即座に完了
                    server.setSucceeded(goal_id, goal * 2);
                }
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        });
        
        results_.goal_submission_times.clear();
        results_.goal_submission_times.reserve(1000);
        
        // クライアント側測定
        for (int i = 0; i < 1000; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            uint64_t goal_id = client.sendGoal(i);
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - start).count();
            
            results_.goal_submission_times.push_back(duration);
            
            // 完了待ち
            while (!client.isComplete(goal_id)) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        
        server_thread.join();
    }
    
    void measureFeedbackLatency() {
        std::cout << "フィードバックレイテンシ測定中..." << std::endl;
        
        ActionServer<int, int, float> server("feedback_test");
        ActionClient<int, int, float> client("feedback_test");
        
        std::atomic<bool> measurement_complete{false};
        results_.feedback_latencies.clear();
        
        // サーバー側スレッド
        std::thread server_thread([&]() {
            while (!measurement_complete) {
                if (server.hasGoal()) {
                    uint64_t goal_id = server.acceptGoal();
                    
                    // 定期的にフィードバック送信
                    for (int i = 0; i < 100; ++i) {
                        auto start = std::chrono::high_resolution_clock::now();
                        
                        server.publishFeedback(goal_id, (float)i);
                        
                        auto end = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                            end - start).count();
                        
                        {
                            static std::mutex latency_mutex;
                            std::lock_guard<std::mutex> lock(latency_mutex);
                            results_.feedback_latencies.push_back(duration);
                        }
                        
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                    
                    server.setSucceeded(goal_id, 100);
                    measurement_complete = true;
                }
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
        
        // クライアント側でフィードバック受信
        uint64_t goal_id = client.sendGoal(1);
        
        while (!client.isComplete(goal_id)) {
            float feedback;
            if (client.getFeedback(goal_id, feedback)) {
                // フィードバック受信時刻を記録（簡略化）
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        
        server_thread.join();
    }
    
    void measureLongRunningActionThroughput() {
        std::cout << "長時間アクションスループット測定中..." << std::endl;
        
        ActionServer<int, int, float> server("throughput_test");
        ActionClient<int, int, float> client("throughput_test");
        
        const int NUM_ACTIONS = 50;
        const int ACTION_DURATION_MS = 200;
        
        std::atomic<int> completed_actions{0};
        
        // サーバー側スレッド
        std::thread server_thread([&]() {
            while (completed_actions < NUM_ACTIONS) {
                if (server.hasGoal()) {
                    uint64_t goal_id = server.acceptGoal();
                    int goal = server.getGoal(goal_id);
                    
                    // 長時間処理をシミュレート
                    std::thread([&, goal_id, goal]() {
                        for (int i = 0; i < 10; ++i) {
                            server.publishFeedback(goal_id, (float)i * 10.0f);
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(ACTION_DURATION_MS / 10));
                        }
                        
                        server.setSucceeded(goal_id, goal);
                        completed_actions++;
                    }).detach();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // 複数のアクションを並列送信
        std::vector<uint64_t> goal_ids;
        for (int i = 0; i < NUM_ACTIONS; ++i) {
            goal_ids.push_back(client.sendGoal(i));
        }
        
        // すべての完了を待機
        for (uint64_t goal_id : goal_ids) {
            while (!client.isComplete(goal_id)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
        
        double throughput = (double)NUM_ACTIONS / total_duration.count() * 1000.0;  // actions/sec
        results_.throughput_measurements.push_back(throughput);
        
        server_thread.join();
        
        std::cout << "スループット: " << throughput << " actions/sec" << std::endl;
    }
    
    void measureConcurrentActionProcessing() {
        std::cout << "並列アクション処理性能測定中..." << std::endl;
        
        ActionServer<int, int, float> server("concurrent_test");
        
        std::vector<int> client_counts = {1, 2, 4, 8, 16};
        
        for (int client_count : client_counts) {
            auto start = std::chrono::high_resolution_clock::now();
            
            std::atomic<int> completed{0};
            std::vector<std::thread> client_threads;
            
            // サーバー側スレッド
            std::thread server_thread([&]() {
                while (completed < client_count * 10) {
                    if (server.hasGoal()) {
                        uint64_t goal_id = server.acceptGoal();
                        int goal = server.getGoal(goal_id);
                        
                        // 軽い処理
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        server.setSucceeded(goal_id, goal);
                        completed++;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(100));
                }
            });
            
            // 複数クライアントスレッド
            for (int i = 0; i < client_count; ++i) {
                client_threads.emplace_back([i]() {
                    ActionClient<int, int, float> client("concurrent_test");
                    
                    for (int j = 0; j < 10; ++j) {
                        uint64_t goal_id = client.sendGoal(i * 10 + j);
                        while (!client.isComplete(goal_id)) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                });
            }
            
            for (auto& thread : client_threads) {
                thread.join();
            }
            
            server_thread.join();
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - start).count();
            
            double concurrent_throughput = (double)(client_count * 10) / duration * 1000.0;
            
            std::cout << "同時クライアント数 " << client_count << ": "
                      << concurrent_throughput << " actions/sec" << std::endl;
        }
    }
    
    void measureMemoryEfficiency() {
        std::cout << "メモリ効率測定中..." << std::endl;
        
        // メモリ使用量測定の実装は環境依存
        // ここでは概念的な測定を示す
        
        ActionServer<std::vector<char>, std::vector<char>, float> server("memory_test");
        ActionClient<std::vector<char>, std::vector<char>, float> client("memory_test");
        
        std::vector<size_t> data_sizes = {1024, 10240, 102400, 1048576};  // 1KB to 1MB
        
        for (size_t size : data_sizes) {
            std::vector<char> large_data(size, 'X');
            
            auto start = std::chrono::high_resolution_clock::now();
            
            uint64_t goal_id = client.sendGoal(large_data);
            
            // サーバー側で処理
            std::thread([&]() {
                if (server.hasGoal()) {
                    uint64_t goal_id = server.acceptGoal();
                    auto goal = server.getGoal(goal_id);
                    
                    // データをそのまま返す
                    server.setSucceeded(goal_id, goal);
                }
            }).detach();
            
            while (!client.isComplete(goal_id)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - start).count();
            
            double bandwidth = (double)size / duration;  // bytes/μs = MB/s
            
            std::cout << "データサイズ " << size << " bytes: "
                      << bandwidth << " MB/s" << std::endl;
        }
    }
    
    void analyzeResults() {
        std::cout << "\n=== ベンチマーク結果解析 ===" << std::endl;
        
        // ゴール送信レイテンシ解析
        if (!results_.goal_submission_times.empty()) {
            analyzeLatencyVector(results_.goal_submission_times, "ゴール送信レイテンシ");
        }
        
        // フィードバックレイテンシ解析
        if (!results_.feedback_latencies.empty()) {
            analyzeLatencyVector(results_.feedback_latencies, "フィードバックレイテンシ");
        }
        
        // スループット解析
        if (!results_.throughput_measurements.empty()) {
            double avg_throughput = std::accumulate(
                results_.throughput_measurements.begin(),
                results_.throughput_measurements.end(), 0.0) / 
                results_.throughput_measurements.size();
            
            std::cout << "平均スループット: " << std::fixed << std::setprecision(2)
                      << avg_throughput << " actions/sec" << std::endl;
        }
    }
    
    void analyzeLatencyVector(const std::vector<double>& latencies, const std::string& name) {
        auto sorted_latencies = latencies;
        std::sort(sorted_latencies.begin(), sorted_latencies.end());
        
        double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        double min_val = sorted_latencies.front();
        double max_val = sorted_latencies.back();
        double p50 = sorted_latencies[sorted_latencies.size() * 0.5];
        double p95 = sorted_latencies[sorted_latencies.size() * 0.95];
        double p99 = sorted_latencies[sorted_latencies.size() * 0.99];
        
        std::cout << name << " (μs):" << std::endl;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  平均: " << mean << std::endl;
        std::cout << "  最小: " << min_val << std::endl;
        std::cout << "  最大: " << max_val << std::endl;
        std::cout << "  50%ile: " << p50 << std::endl;
        std::cout << "  95%ile: " << p95 << std::endl;
        std::cout << "  99%ile: " << p99 << std::endl;
        
        if (p99 < 1000.0) {
            std::cout << "  🏆 優秀: 99%のレイテンシが1ms以下" << std::endl;
        } else if (p99 < 10000.0) {
            std::cout << "  👍 良好: 99%のレイテンシが10ms以下" << std::endl;
        } else {
            std::cout << "  ⚠️  要改善: レイテンシが高めです" << std::endl;
        }
    }
};

// ベンチマーク実行
int main() {
    try {
        ActionPerformanceBenchmark benchmark;
        benchmark.runActionBenchmark();
        
    } catch (const std::exception& e) {
        std::cerr << "ベンチマークエラー: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

## ❓ よくある質問

### Q1. アクションが完了しない場合はどうすればよいですか？
**A**: 以下をチェックしてください：
- サーバーが正しく動作し、ゴールを受け付けているか
- サーバー側で例外やデッドロックが発生していないか
- アクション内でキャンセル確認を定期的に行っているか
- タイムアウト設定が適切か

### Q2. フィードバックが届かない場合の原因は？
**A**: 考えられる原因：
- フィードバック送信間隔が短すぎる（バッファオーバーフロー）
- 共有メモリのサイズ不足
- クライアント側でフィードバック読み取りが遅い
- ネットワーク遅延（リモート使用時）

### Q3. 長時間アクションの途中でプロセスがクラッシュした場合は？
**A**: Action通信では以下の対策があります：
- **チェックポイント保存**: 定期的に進捗を永続化
- **再開機能**: 中断点からの復旧機能
- **状態監視**: 別プロセスでアクション状態を監視
- **タイムアウト設定**: 無応答状態の自動検出

### Q4. 複数のアクションを並列実行する際の注意点は？
**A**: 以下に注意してください：
- **リソース競合**: ファイルやデバイスの排他制御
- **メモリ使用量**: 各アクションのメモリ消費量を考慮
- **CPU負荷**: 適切なワーカー数の設定
- **優先度制御**: 重要なアクションの優先実行

## 🔧 トラブルシューティング

### よくある問題と解決法

```cpp
// Action通信診断ツール
#include "shm_action.hpp"
#include <iostream>

void diagnose_action_communication() {
    using namespace irlab::shm;
    
    std::cout << "=== Action通信診断 ===" << std::endl;
    
    try {
        // 1. サーバー作成テスト
        ActionServer<int, int, float> server("diagnostic_action");
        std::cout << "✅ サーバー作成成功" << std::endl;
        
        // 2. クライアント作成テスト
        ActionClient<int, int, float> client("diagnostic_action");
        std::cout << "✅ クライアント作成成功" << std::endl;
        
        // 3. 通信テスト
        std::thread server_thread([&server]() {
            int test_count = 0;
            while (test_count < 3) {
                if (server.hasGoal()) {
                    uint64_t goal_id = server.acceptGoal();
                    int goal = server.getGoal(goal_id);
                    
                    std::cout << "サーバー: ゴール受信 " << goal << std::endl;
                    
                    // フィードバック送信
                    for (int i = 0; i < 5; ++i) {
                        server.publishFeedback(goal_id, (float)i * 20.0f);
                        std::cout << "サーバー: フィードバック送信 " << (i * 20) << "%" << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    
                    // 結果送信
                    server.setSucceeded(goal_id, goal * 2);
                    std::cout << "サーバー: 結果送信 " << (goal * 2) << std::endl;
                    test_count++;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // ゴール送信
        uint64_t goal_id = client.sendGoal(21);
        std::cout << "クライアント: ゴール送信 21" << std::endl;
        
        // フィードバック監視
        bool feedback_received = false;
        while (!client.isComplete(goal_id)) {
            float feedback;
            if (client.getFeedback(goal_id, feedback)) {
                std::cout << "クライアント: フィードバック受信 " << feedback << "%" << std::endl;
                feedback_received = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // 結果確認
        if (client.getState(goal_id) == ActionStatus::SUCCEEDED) {
            int result = client.getResult(goal_id);
            std::cout << "クライアント: 結果受信 " << result << std::endl;
            
            if (result == 42 && feedback_received) {
                std::cout << "✅ 通信テスト成功" << std::endl;
            } else {
                std::cout << "❌ 通信テスト失敗: 期待値42、実際値" << result 
                          << ", フィードバック受信: " << (feedback_received ? "○" : "×") << std::endl;
            }
        } else {
            std::cout << "❌ 通信テスト失敗: アクションが失敗" << std::endl;
        }
        
        server_thread.join();
        
    } catch (const std::exception& e) {
        std::cout << "❌ 診断エラー: " << e.what() << std::endl;
        std::cout << "以下を確認してください:" << std::endl;
        std::cout << "  - 共有メモリの権限設定" << std::endl;
        std::cout << "  - 他のプロセスがアクションを使用中でないか" << std::endl;
        std::cout << "  - システムリソースの不足" << std::endl;
        std::cout << "  - メモリサイズの設定" << std::endl;
    }
}

// 堅牢なアクションクライアントの例
class RobustActionClient {
private:
    ActionClient<GoalType, ResultType, FeedbackType> client_;
    std::string action_name_;
    
public:
    RobustActionClient(const std::string& action_name) 
        : client_(action_name), action_name_(action_name) {}
    
    bool executeGoalWithRetry(const GoalType& goal, ResultType& result, 
                             int max_retries = 3, int timeout_ms = 30000) {
        
        for (int retry = 0; retry < max_retries; ++retry) {
            try {
                std::cout << "ゴール実行 (試行 " << (retry + 1) << "/" << max_retries << ")" << std::endl;
                
                uint64_t goal_id = client_.sendGoal(goal);
                
                // タイムアウト付き完了待機
                auto start_time = std::chrono::high_resolution_clock::now();
                
                while (!client_.isComplete(goal_id)) {
                    auto current_time = std::chrono::high_resolution_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        current_time - start_time);
                    
                    if (elapsed.count() > timeout_ms) {
                        std::cout << "タイムアウト: ゴールをキャンセルします..." << std::endl;
                        client_.cancelGoal(goal_id);
                        break;
                    }
                    
                    // フィードバック表示
                    FeedbackType feedback;
                    if (client_.getFeedback(goal_id, feedback)) {
                        displayFeedback(feedback);
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                // 結果確認
                ActionStatus status = client_.getState(goal_id);
                if (status == ActionStatus::SUCCEEDED) {
                    result = client_.getResult(goal_id);
                    std::cout << "成功: ゴール完了" << std::endl;
                    return true;
                } else {
                    std::cout << "失敗: " << actionStatusToString(status) << std::endl;
                    
                    if (retry < max_retries - 1) {
                        std::cout << "再試行します..." << std::endl;
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
                
            } catch (const std::exception& e) {
                std::cout << "エラー: " << e.what() << std::endl;
                
                if (retry < max_retries - 1) {
                    std::cout << "再試行します..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                } else {
                    std::cout << "最大再試行回数に達しました" << std::endl;
                    throw;
                }
            }
        }
        
        return false;
    }
    
private:
    void displayFeedback(const FeedbackType& feedback) {
        // フィードバック表示の実装
        std::cout << "進捗更新..." << std::endl;
    }
    
    std::string actionStatusToString(ActionStatus status) {
        switch (status) {
            case ActionStatus::PENDING: return "待機中";
            case ActionStatus::ACTIVE: return "実行中";
            case ActionStatus::PREEMPTED: return "中断";
            case ActionStatus::SUCCEEDED: return "成功";
            case ActionStatus::ABORTED: return "異常終了";
            case ActionStatus::REJECTED: return "拒否";
            case ActionStatus::RECALLED: return "取り消し";
            default: return "不明";
        }
    }
};
```

## 📚 次のステップ

Action通信をマスターしたら、以下の高度なトピックに挑戦してみましょう：

1. **[📡 Pub/Sub通信](tutorials_shm_pub_sub_jp.md)** - 高速ブロードキャスト通信
2. **[🤝 Service通信](tutorials_shm_service_jp.md)** - 確実な要求応答通信
3. **[🐍 Python連携](tutorials_python_jp.md)** - PythonでAction通信

---

## 📄 ライセンス情報

本ドキュメントで紹介しているサンプルコードは、shared-memory-based-handy-communication-manager プロジェクトの一部として **Apache License 2.0** の下で提供されています。

- ✅ **商用利用可能**: サンプルコードを商業プロジェクトで自由に使用
- ✅ **改変可能**: ニーズに合わせてコードを修正・拡張
- ✅ **再配布可能**: ライセンス表示を保持して再配布

詳細は[LICENSEファイル](../LICENSE)をご確認ください。

---

この完全ガイドで、Action通信の真の力を引き出し、次世代の高度な非同期処理システムを構築しましょう！ 🚀✨