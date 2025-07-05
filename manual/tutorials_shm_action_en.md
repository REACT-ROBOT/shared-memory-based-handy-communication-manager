# ⚡ Action Communication Complete Guide - Master Long-Running Asynchronous Processing
[English | [日本語](docs_jp/md_manual_tutorials_shm_action_jp.html)]

## 🎯 What You'll Learn in This Guide

- **Deep Understanding of Action Communication**: From design philosophy to implementation details of asynchronous processing patterns
- **Long-Running Process Management**: Progress monitoring, cancellation functionality, error handling
- **Advanced Workflow Design**: Parallel processing, task chains, state management
- **Practical Applications**: File processing, machine learning, large-scale data transformation

## 🧠 Deep Understanding of Action Communication

### 🏗️ Architecture Overview

```cpp
// Action communication internal structure
┌─────────────────────────────────────────────────────────────┐
│                   Shared Memory Space                        │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Action Queue                           │    │
│  │ [Goal 0][Goal 1][Goal 2]...[Goal N-1]             │    │
│  │    ↑                           ↑                    │    │
│  │  Execute Pos                 Add Pos                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Feedback Queue                         │    │
│  │ [FB 0][FB 1][FB 2]...[FB N-1]                     │    │
│  │    ↑                           ↑                    │    │
│  │  Read Pos                   Write Pos               │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Result Queue                           │    │
│  │ [Result 0][Result 1]...[Result N-1]               │    │
│  │    ↑                           ↑                    │    │
│  │  Read Pos                   Write Pos               │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  Action State Management:                                   │
│  - Goal ID (unique identifier)                             │
│  - Execution State (waiting/active/completed/cancelled/error) │
│  - Progress Info (completion rate, remaining time, details) │
│  - Cancel Flag (asynchronous cancellation request)         │
└─────────────────────────────────────────────────────────────┘

Multiple Clients ← [shared memory] → Single Server
      │                                        │
   Send Goal                          Action Execution Engine
   Receive Feedback                           │
   Receive Result                         Long-Running Processing
   Cancel Request                         Progress Reporting
      │                                   Result Generation
   Asynchronous Monitoring
```

### ⚡ Why is it Optimal for Long-Running Processes?

**1. Asynchronous Execution and Feedback**
```cpp
// Basic action pattern
ActionClient<GoalType, ResultType, FeedbackType> client("action_server");

// Send goal (non-blocking)
uint64_t goal_id = client.sendGoal(goal_data);

// Continue other processing...
while (true) {
    // Check feedback
    FeedbackType feedback;
    if (client.getFeedback(goal_id, feedback)) {
        std::cout << "Progress: " << feedback.progress_percent << "%" << std::endl;
    }
    
    // Check completion
    if (client.isComplete(goal_id)) {
        ResultType result = client.getResult(goal_id);
        break;
    }
    
    // Cancel if needed
    if (user_cancel_request) {
        client.cancelGoal(goal_id);
        break;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}
```

**2. State Management and Progress Monitoring**
```cpp
// Internal state management
enum class ActionStatus {
    PENDING,        // Waiting
    ACTIVE,         // Executing
    PREEMPTED,      // Interrupted
    SUCCEEDED,      // Successfully completed
    ABORTED,        // Abnormally terminated
    REJECTED,       // Rejected
    PREEMPTING,     // Cancellation in progress
    RECALLING       // Goal recall in progress
};

// Progress tracking structure
struct ActionProgress {
    uint64_t goal_id;
    ActionStatus status;
    float progress_percent;
    uint64_t estimated_remaining_time_us;
    std::string status_message;
    uint64_t last_update_time_us;
};

// State transition management
class ActionStateMachine {
public:
    bool transitionTo(ActionStatus new_status) {
        // Validate state transitions
        if (isValidTransition(current_status_, new_status)) {
            current_status_ = new_status;
            last_update_time_ = getCurrentTimeUs();
            return true;
        }
        return false;
    }
    
private:
    bool isValidTransition(ActionStatus from, ActionStatus to) {
        // Define valid state transitions
        static const std::map<ActionStatus, std::set<ActionStatus>> valid_transitions = {
            {PENDING, {ACTIVE, REJECTED, PREEMPTING}},
            {ACTIVE, {SUCCEEDED, ABORTED, PREEMPTING}},
            {PREEMPTING, {PREEMPTED}},
            // ... other transitions
        };
        
        auto it = valid_transitions.find(from);
        return it != valid_transitions.end() && 
               it->second.find(to) != it->second.end();
    }
};
```

## 🚀 Basic Usage Examples

### 1. File Processing Action

**Server Side:**
```cpp
#include "shm_action.hpp"
#include <iostream>
#include <thread>
#include <fstream>

using namespace irlab::shm;

// Define action data structures
struct FileProcessingGoal {
    char input_filename[256];
    char output_filename[256];
    int processing_type;  // 0: compress, 1: encrypt, 2: convert
};

struct FileProcessingResult {
    bool success;
    uint64_t input_file_size;
    uint64_t output_file_size;
    uint64_t processing_time_us;
    char error_message[512];
};

struct FileProcessingFeedback {
    float progress_percent;
    uint64_t bytes_processed;
    uint64_t estimated_remaining_time_us;
    char current_operation[128];
};

class FileProcessingServer {
private:
    ActionServer<FileProcessingGoal, FileProcessingResult, FileProcessingFeedback> server_;
    
public:
    FileProcessingServer() : server_("file_processor") {}
    
    void run() {
        std::cout << "📁 File Processing Action Server started!" << std::endl;
        
        while (true) {
            if (server_.hasGoal()) {
                FileProcessingGoal goal = server_.getGoal();
                std::cout << "📥 New goal: Process " << goal.input_filename << std::endl;
                
                // Start processing in separate thread
                std::thread processing_thread([this, goal]() {
                    processFile(goal);
                });
                processing_thread.detach();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
private:
    void processFile(const FileProcessingGoal& goal) {
        FileProcessingResult result;
        result.success = true;
        result.processing_time_us = 0;
        
        auto start_time = std::chrono::steady_clock::now();
        
        try {
            // Simulate file processing with progress updates
            std::ifstream input(goal.input_filename, std::ios::binary | std::ios::ate);
            if (!input.is_open()) {
                throw std::runtime_error("Cannot open input file");
            }
            
            result.input_file_size = input.tellg();
            input.seekg(0);
            
            // Process file in chunks with progress reporting
            const size_t chunk_size = 1024 * 1024;  // 1MB chunks
            std::vector<char> buffer(chunk_size);
            size_t total_processed = 0;
            
            std::ofstream output(goal.output_filename, std::ios::binary);
            if (!output.is_open()) {
                throw std::runtime_error("Cannot create output file");
            }
            
            while (total_processed < result.input_file_size) {
                // Check for cancellation
                if (server_.isCancellationRequested()) {
                    std::cout << "🚫 Processing cancelled by client" << std::endl;
                    server_.setAborted();
                    return;
                }
                
                // Read chunk
                size_t to_read = std::min(chunk_size, 
                                        result.input_file_size - total_processed);
                input.read(buffer.data(), to_read);
                
                // Simulate processing time
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                // Write processed chunk
                output.write(buffer.data(), to_read);
                total_processed += to_read;
                
                // Send feedback
                FileProcessingFeedback feedback;
                feedback.progress_percent = (float)total_processed / result.input_file_size * 100.0f;
                feedback.bytes_processed = total_processed;
                feedback.estimated_remaining_time_us = 
                    calculateRemainingTime(total_processed, result.input_file_size, start_time);
                strcpy(feedback.current_operation, "Processing data chunk");
                
                server_.sendFeedback(feedback);
                
                std::cout << "📊 Progress: " << feedback.progress_percent << "%" << std::endl;
            }
            
            result.output_file_size = total_processed;
            
        } catch (const std::exception& e) {
            result.success = false;
            strcpy(result.error_message, e.what());
            server_.setAborted();
            return;
        }
        
        auto end_time = std::chrono::steady_clock::now();
        result.processing_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        
        server_.setSucceeded(result);
        std::cout << "✅ File processing completed successfully!" << std::endl;
    }
    
    uint64_t calculateRemainingTime(size_t processed, size_t total, 
                                   std::chrono::steady_clock::time_point start_time) {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        
        if (processed == 0) return 0;
        
        uint64_t total_estimated_time = (elapsed_us * total) / processed;
        return total_estimated_time - elapsed_us;
    }
};
```

**Client Side:**
```cpp
#include "shm_action.hpp"
#include <iostream>
#include <thread>

using namespace irlab::shm;

class FileProcessingClient {
private:
    ActionClient<FileProcessingGoal, FileProcessingResult, FileProcessingFeedback> client_;
    
public:
    FileProcessingClient() : client_("file_processor") {}
    
    void processFileAsync(const std::string& input_file, const std::string& output_file, int type) {
        FileProcessingGoal goal;
        strcpy(goal.input_filename, input_file.c_str());
        strcpy(goal.output_filename, output_file.c_str());
        goal.processing_type = type;
        
        std::cout << "🚀 Sending file processing goal..." << std::endl;
        uint64_t goal_id = client_.sendGoal(goal);
        
        // Monitor progress
        while (!client_.isComplete(goal_id)) {
            FileProcessingFeedback feedback;
            if (client_.getFeedback(goal_id, feedback)) {
                std::cout << "📈 Progress: " << feedback.progress_percent << "% - "
                          << feedback.current_operation << std::endl;
                std::cout << "📊 Processed: " << feedback.bytes_processed << " bytes" << std::endl;
                std::cout << "⏱️ Estimated remaining: " 
                          << (feedback.estimated_remaining_time_us / 1000000.0) << " seconds" << std::endl;
            }
            
            // Check for user input to cancel
            // In real application, you'd check for actual user input
            static int counter = 0;
            if (++counter > 50) {  // Simulate cancel after 5 seconds
                std::cout << "❌ User requested cancellation" << std::endl;
                client_.cancelGoal(goal_id);
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Get final result
        if (client_.getState(goal_id) == ActionState::SUCCEEDED) {
            FileProcessingResult result = client_.getResult(goal_id);
            std::cout << "🎉 File processing completed successfully!" << std::endl;
            std::cout << "📁 Input size: " << result.input_file_size << " bytes" << std::endl;
            std::cout << "📁 Output size: " << result.output_file_size << " bytes" << std::endl;
            std::cout << "⏱️ Processing time: " << (result.processing_time_us / 1000000.0) << " seconds" << std::endl;
        } else {
            std::cout << "❌ File processing failed or was cancelled" << std::endl;
        }
    }
};

int main() {
    FileProcessingClient client;
    client.processFileAsync("input.txt", "output.txt", 0);
    return 0;
}
```

### 2. Machine Learning Training Action

```cpp
// Advanced ML training example
#include "shm_action.hpp"
#include <iostream>
#include <thread>
#include <random>

using namespace irlab::shm;

// ML Training data structures
struct MLTrainingGoal {
    char dataset_path[256];
    char model_type[64];
    int epochs;
    float learning_rate;
    int batch_size;
};

struct MLTrainingResult {
    bool success;
    float final_accuracy;
    float final_loss;
    uint64_t training_time_us;
    char model_save_path[256];
    char error_message[512];
};

struct MLTrainingFeedback {
    int current_epoch;
    int total_epochs;
    float current_accuracy;
    float current_loss;
    float progress_percent;
    uint64_t estimated_remaining_time_us;
    char phase[64];  // "loading", "training", "validation", "saving"
};

class MLTrainingServer {
private:
    ActionServer<MLTrainingGoal, MLTrainingResult, MLTrainingFeedback> server_;
    std::random_device rd_;
    std::mt19937 gen_;
    
public:
    MLTrainingServer() : server_("ml_trainer"), gen_(rd_()) {}
    
    void run() {
        std::cout << "🧠 ML Training Action Server started!" << std::endl;
        
        while (true) {
            if (server_.hasGoal()) {
                MLTrainingGoal goal = server_.getGoal();
                std::cout << "🎯 New training goal: " << goal.model_type 
                          << " for " << goal.epochs << " epochs" << std::endl;
                
                std::thread training_thread([this, goal]() {
                    trainModel(goal);
                });
                training_thread.detach();
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
private:
    void trainModel(const MLTrainingGoal& goal) {
        MLTrainingResult result;
        result.success = true;
        
        auto start_time = std::chrono::steady_clock::now();
        
        try {
            // Phase 1: Data Loading
            sendProgressUpdate(0, goal.epochs, 0.0f, 0.0f, 0.0f, "loading", start_time);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Phase 2: Training Loop
            std::uniform_real_distribution<> accuracy_dist(0.1, 0.95);
            std::uniform_real_distribution<> loss_dist(0.05, 2.0);
            
            float best_accuracy = 0.0f;
            float current_loss = 2.0f;
            
            for (int epoch = 1; epoch <= goal.epochs; ++epoch) {
                // Check for cancellation
                if (server_.isCancellationRequested()) {
                    std::cout << "🚫 Training cancelled at epoch " << epoch << std::endl;
                    server_.setAborted();
                    return;
                }
                
                // Simulate training progress
                float epoch_progress = (float)epoch / goal.epochs;
                
                // Simulate improving accuracy and decreasing loss
                float current_accuracy = accuracy_dist(gen_) * epoch_progress + 0.1f;
                current_loss = loss_dist(gen_) * (1.0f - epoch_progress * 0.8f);
                
                if (current_accuracy > best_accuracy) {
                    best_accuracy = current_accuracy;
                }
                
                // Send progress update
                sendProgressUpdate(epoch, goal.epochs, current_accuracy, current_loss, 
                                 epoch_progress * 100.0f, "training", start_time);
                
                std::cout << "📈 Epoch " << epoch << "/" << goal.epochs 
                          << " - Accuracy: " << current_accuracy 
                          << ", Loss: " << current_loss << std::endl;
                
                // Simulate epoch training time
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            
            // Phase 3: Model Saving
            sendProgressUpdate(goal.epochs, goal.epochs, best_accuracy, current_loss, 
                             95.0f, "saving", start_time);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            // Set final results
            result.final_accuracy = best_accuracy;
            result.final_loss = current_loss;
            strcpy(result.model_save_path, "/models/trained_model.pkl");
            
        } catch (const std::exception& e) {
            result.success = false;
            strcpy(result.error_message, e.what());
            server_.setAborted();
            return;
        }
        
        auto end_time = std::chrono::steady_clock::now();
        result.training_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();
        
        server_.setSucceeded(result);
        std::cout << "🎉 Model training completed! Final accuracy: " 
                  << result.final_accuracy << std::endl;
    }
    
    void sendProgressUpdate(int current_epoch, int total_epochs, float accuracy, float loss,
                           float progress_percent, const char* phase,
                           std::chrono::steady_clock::time_point start_time) {
        MLTrainingFeedback feedback;
        feedback.current_epoch = current_epoch;
        feedback.total_epochs = total_epochs;
        feedback.current_accuracy = accuracy;
        feedback.current_loss = loss;
        feedback.progress_percent = progress_percent;
        strcpy(feedback.phase, phase);
        
        // Estimate remaining time
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        
        if (progress_percent > 0) {
            uint64_t total_estimated_time = (elapsed_us * 100) / progress_percent;
            feedback.estimated_remaining_time_us = total_estimated_time - elapsed_us;
        } else {
            feedback.estimated_remaining_time_us = 0;
        }
        
        server_.sendFeedback(feedback);
    }
};

// Client usage
void trainNeuralNetwork() {
    ActionClient<MLTrainingGoal, MLTrainingResult, MLTrainingFeedback> client("ml_trainer");
    
    MLTrainingGoal goal;
    strcpy(goal.dataset_path, "/data/training_set.csv");
    strcpy(goal.model_type, "neural_network");
    goal.epochs = 50;
    goal.learning_rate = 0.001f;
    goal.batch_size = 32;
    
    std::cout << "🚀 Starting neural network training..." << std::endl;
    uint64_t goal_id = client.sendGoal(goal);
    
    // Monitor training progress
    while (!client.isComplete(goal_id)) {
        MLTrainingFeedback feedback;
        if (client.getFeedback(goal_id, feedback)) {
            std::cout << "🧠 [" << feedback.phase << "] Epoch " 
                      << feedback.current_epoch << "/" << feedback.total_epochs
                      << " - Progress: " << feedback.progress_percent << "%" << std::endl;
            std::cout << "📊 Accuracy: " << feedback.current_accuracy 
                      << ", Loss: " << feedback.current_loss << std::endl;
            std::cout << "⏱️ ETA: " << (feedback.estimated_remaining_time_us / 1000000.0) 
                      << " seconds" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Get final results
    if (client.getState(goal_id) == ActionState::SUCCEEDED) {
        MLTrainingResult result = client.getResult(goal_id);
        std::cout << "🎉 Training completed successfully!" << std::endl;
        std::cout << "🎯 Final accuracy: " << result.final_accuracy << std::endl;
        std::cout << "📉 Final loss: " << result.final_loss << std::endl;
        std::cout << "💾 Model saved to: " << result.model_save_path << std::endl;
        std::cout << "⏱️ Total training time: " << (result.training_time_us / 1000000.0) 
                  << " seconds" << std::endl;
    } else {
        std::cout << "❌ Training failed or was cancelled" << std::endl;
    }
}
```

## 🔧 Advanced Features and Patterns

### 3. Multi-Goal Action Server

```cpp
// Advanced server that can handle multiple concurrent goals
#include "shm_action.hpp"
#include <iostream>
#include <thread>
#include <map>
#include <atomic>

using namespace irlab::shm;

class ConcurrentActionServer {
private:
    ActionServer<FileProcessingGoal, FileProcessingResult, FileProcessingFeedback> server_;
    std::map<uint64_t, std::thread> active_goals_;
    std::mutex goals_mutex_;
    std::atomic<bool> running_{true};
    
public:
    ConcurrentActionServer() : server_("concurrent_processor") {}
    
    ~ConcurrentActionServer() {
        running_ = false;
        
        // Wait for all active goals to complete
        std::lock_guard<std::mutex> lock(goals_mutex_);
        for (auto& [goal_id, thread] : active_goals_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    void run() {
        std::cout << "🔄 Concurrent Action Server started!" << std::endl;
        
        while (running_) {
            if (server_.hasGoal()) {
                FileProcessingGoal goal = server_.getGoal();
                uint64_t goal_id = server_.getCurrentGoalId();
                
                std::cout << "📥 New goal " << goal_id << ": " << goal.input_filename << std::endl;
                
                // Start processing in separate thread
                std::lock_guard<std::mutex> lock(goals_mutex_);
                active_goals_[goal_id] = std::thread([this, goal, goal_id]() {
                    processGoal(goal, goal_id);
                    
                    // Clean up completed goal
                    std::lock_guard<std::mutex> lock(goals_mutex_);
                    active_goals_.erase(goal_id);
                });
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
private:
    void processGoal(const FileProcessingGoal& goal, uint64_t goal_id) {
        std::cout << "🚀 Starting processing for goal " << goal_id << std::endl;
        
        // Simulate processing with regular feedback
        for (int progress = 0; progress <= 100; progress += 10) {
            if (!running_ || server_.isCancellationRequested(goal_id)) {
                std::cout << "🚫 Goal " << goal_id << " cancelled" << std::endl;
                server_.setAborted(goal_id);
                return;
            }
            
            FileProcessingFeedback feedback;
            feedback.progress_percent = progress;
            feedback.bytes_processed = progress * 1000;
            feedback.estimated_remaining_time_us = (100 - progress) * 100000;
            strcpy(feedback.current_operation, "Processing chunk");
            
            server_.sendFeedback(feedback, goal_id);
            std::cout << "📊 Goal " << goal_id << " progress: " << progress << "%" << std::endl;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        
        FileProcessingResult result;
        result.success = true;
        result.input_file_size = 100000;
        result.output_file_size = 95000;
        result.processing_time_us = 5000000;  // 5 seconds
        strcpy(result.error_message, "");
        
        server_.setSucceeded(result, goal_id);
        std::cout << "✅ Goal " << goal_id << " completed successfully!" << std::endl;
    }
};
```

### 4. Action Chain Workflow

```cpp
// Example of chaining multiple actions together
#include "shm_action.hpp"
#include <iostream>
#include <vector>
#include <functional>

using namespace irlab::shm;

class ActionWorkflow {
private:
    struct WorkflowStep {
        std::string action_name;
        std::function<void()> execute;
        std::function<bool()> check_completion;
        std::function<void()> on_success;
        std::function<void()> on_failure;
    };
    
    std::vector<WorkflowStep> steps_;
    size_t current_step_{0};
    bool workflow_running_{false};
    
public:
    void addStep(const std::string& name, 
                std::function<void()> execute,
                std::function<bool()> check_completion,
                std::function<void()> on_success = nullptr,
                std::function<void()> on_failure = nullptr) {
        steps_.push_back({name, execute, check_completion, on_success, on_failure});
    }
    
    void executeWorkflow() {
        std::cout << "🔗 Starting workflow with " << steps_.size() << " steps" << std::endl;
        workflow_running_ = true;
        current_step_ = 0;
        
        while (workflow_running_ && current_step_ < steps_.size()) {
            auto& step = steps_[current_step_];
            std::cout << "▶️ Executing step " << (current_step_ + 1) 
                      << ": " << step.action_name << std::endl;
            
            // Execute step
            step.execute();
            
            // Wait for completion
            while (workflow_running_ && !step.check_completion()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            if (!workflow_running_) {
                std::cout << "🚫 Workflow cancelled" << std::endl;
                break;
            }
            
            // Handle success
            if (step.on_success) {
                step.on_success();
            }
            
            std::cout << "✅ Step " << (current_step_ + 1) << " completed" << std::endl;
            current_step_++;
        }
        
        if (workflow_running_ && current_step_ >= steps_.size()) {
            std::cout << "🎉 Workflow completed successfully!" << std::endl;
        }
        
        workflow_running_ = false;
    }
    
    void cancelWorkflow() {
        workflow_running_ = false;
    }
};

// Example usage
void runDataProcessingWorkflow() {
    ActionWorkflow workflow;
    
    // Step 1: Download data
    ActionClient<FileProcessingGoal, FileProcessingResult, FileProcessingFeedback> download_client("downloader");
    uint64_t download_goal_id;
    
    workflow.addStep("Download Data",
        [&]() {
            FileProcessingGoal goal;
            strcpy(goal.input_filename, "http://example.com/data.zip");
            strcpy(goal.output_filename, "/tmp/data.zip");
            goal.processing_type = 0;
            download_goal_id = download_client.sendGoal(goal);
        },
        [&]() { return download_client.isComplete(download_goal_id); },
        [&]() { std::cout << "📥 Download completed" << std::endl; },
        [&]() { std::cout << "❌ Download failed" << std::endl; }
    );
    
    // Step 2: Extract data
    ActionClient<FileProcessingGoal, FileProcessingResult, FileProcessingFeedback> extract_client("extractor");
    uint64_t extract_goal_id;
    
    workflow.addStep("Extract Data",
        [&]() {
            FileProcessingGoal goal;
            strcpy(goal.input_filename, "/tmp/data.zip");
            strcpy(goal.output_filename, "/tmp/extracted/");
            goal.processing_type = 1;
            extract_goal_id = extract_client.sendGoal(goal);
        },
        [&]() { return extract_client.isComplete(extract_goal_id); },
        [&]() { std::cout << "📂 Extraction completed" << std::endl; }
    );
    
    // Step 3: Process data
    ActionClient<MLTrainingGoal, MLTrainingResult, MLTrainingFeedback> ml_client("ml_trainer");
    uint64_t ml_goal_id;
    
    workflow.addStep("Train Model",
        [&]() {
            MLTrainingGoal goal;
            strcpy(goal.dataset_path, "/tmp/extracted/dataset.csv");
            strcpy(goal.model_type, "neural_network");
            goal.epochs = 100;
            goal.learning_rate = 0.001f;
            goal.batch_size = 32;
            ml_goal_id = ml_client.sendGoal(goal);
        },
        [&]() { return ml_client.isComplete(ml_goal_id); },
        [&]() { std::cout << "🧠 Model training completed" << std::endl; }
    );
    
    // Execute the workflow
    workflow.executeWorkflow();
}
```

## 📊 Performance Monitoring and Metrics

### 5. Action Performance Analytics

```cpp
// Performance monitoring for action servers
#include "shm_action.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <map>

using namespace irlab::shm;

class ActionPerformanceMonitor {
private:
    struct ActionMetrics {
        uint64_t total_goals;
        uint64_t successful_goals;
        uint64_t failed_goals;
        uint64_t cancelled_goals;
        std::vector<uint64_t> execution_times_us;
        uint64_t total_execution_time_us;
        std::chrono::steady_clock::time_point start_time;
    };
    
    std::map<std::string, ActionMetrics> metrics_;
    std::mutex metrics_mutex_;
    
public:
    void startGoal(const std::string& action_name, uint64_t goal_id) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        auto& metrics = metrics_[action_name];
        metrics.total_goals++;
        metrics.start_time = std::chrono::steady_clock::now();
    }
    
    void goalCompleted(const std::string& action_name, uint64_t goal_id, 
                      ActionState final_state) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        auto& metrics = metrics_[action_name];
        
        auto execution_time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - metrics.start_time).count();
        
        metrics.execution_times_us.push_back(execution_time);
        metrics.total_execution_time_us += execution_time;
        
        switch (final_state) {
            case ActionState::SUCCEEDED:
                metrics.successful_goals++;
                break;
            case ActionState::ABORTED:
                metrics.failed_goals++;
                break;
            case ActionState::PREEMPTED:
                metrics.cancelled_goals++;
                break;
        }
    }
    
    void printStatistics() {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        
        std::cout << "\n📊 Action Performance Statistics:" << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        
        for (const auto& [action_name, metrics] : metrics_) {
            std::cout << "🎯 Action: " << action_name << std::endl;
            std::cout << "  Total Goals: " << metrics.total_goals << std::endl;
            std::cout << "  Successful: " << metrics.successful_goals 
                      << " (" << (metrics.successful_goals * 100.0 / metrics.total_goals) << "%)" << std::endl;
            std::cout << "  Failed: " << metrics.failed_goals 
                      << " (" << (metrics.failed_goals * 100.0 / metrics.total_goals) << "%)" << std::endl;
            std::cout << "  Cancelled: " << metrics.cancelled_goals 
                      << " (" << (metrics.cancelled_goals * 100.0 / metrics.total_goals) << "%)" << std::endl;
            
            if (!metrics.execution_times_us.empty()) {
                auto avg_time = metrics.total_execution_time_us / metrics.execution_times_us.size();
                auto min_time = *std::min_element(metrics.execution_times_us.begin(), 
                                                metrics.execution_times_us.end());
                auto max_time = *std::max_element(metrics.execution_times_us.begin(), 
                                                metrics.execution_times_us.end());
                
                std::cout << "  Average execution time: " << (avg_time / 1000000.0) << " seconds" << std::endl;
                std::cout << "  Min execution time: " << (min_time / 1000000.0) << " seconds" << std::endl;
                std::cout << "  Max execution time: " << (max_time / 1000000.0) << " seconds" << std::endl;
            }
            
            std::cout << std::endl;
        }
    }
};
```

## 🎯 Best Practices and Design Patterns

### Design Guidelines

1. **Keep Goals and Results Focused**
   ```cpp
   // ✅ Good - Focused, specific goal
   struct ImageProcessingGoal {
       char input_path[256];
       int filter_type;
       float parameter;
   };
   
   // ❌ Avoid - Too many responsibilities
   struct MegaProcessingGoal {
       char files[100][256];
       int operations[50];
       float parameters[200];
   };
   ```

2. **Provide Meaningful Feedback**
   ```cpp
   struct ProcessingFeedback {
       float progress_percent;      // 0-100
       char current_operation[128]; // Human-readable description
       uint64_t estimated_time_us;  // Remaining time estimate
       uint64_t processed_items;    // Concrete progress measure
   };
   ```

3. **Handle Cancellation Gracefully**
   ```cpp
   void processLongRunningTask() {
       for (int i = 0; i < total_items; ++i) {
           // Check for cancellation regularly
           if (server_.isCancellationRequested()) {
               cleanupPartialWork();
               server_.setAborted();
               return;
           }
           
           processItem(i);
           sendProgressUpdate(i, total_items);
       }
   }
   ```

### Error Handling Strategies

1. **Graceful Degradation**
2. **Retry Mechanisms for Transient Failures**
3. **Detailed Error Reporting**
4. **Partial Result Preservation**

## 🔍 Troubleshooting Common Issues

### Action State Issues

1. **Stuck in PENDING State**: Check if action server is running and processing goals
2. **Premature ABORTED State**: Verify error handling and exception management
3. **Missing Feedback**: Ensure regular feedback updates in long-running operations

### Performance Issues

1. **Slow Action Execution**: Profile bottlenecks in goal processing
2. **Memory Leaks**: Verify proper cleanup in action callbacks
3. **Deadlocks**: Review threading and synchronization patterns

## 📚 Next Steps

- **[🔄 Pub/Sub Communication](tutorials_shm_pub_sub_en.md)** - For high-speed broadcasting
- **[🤝 Service Communication](tutorials_shm_service_en.md)** - For request-response patterns
- **[🐍 Python Integration](tutorials_python_en.md)** - Multi-language development

---

**🎉 Congratulations!** You've mastered advanced asynchronous communication! Your applications can now handle complex, long-running workflows with full monitoring and control! 🚀✨
