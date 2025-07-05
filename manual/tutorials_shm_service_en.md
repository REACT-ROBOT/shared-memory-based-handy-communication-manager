# 🤝 Service Communication Complete Guide - Master Reliable Request-Response Communication
[English | [日本語](docs_jp/md_manual_tutorials_shm_service_jp.html)]

## 🎯 What You'll Learn in This Guide

- **Deep Understanding of Service Communication**: From design philosophy to implementation details of request-response patterns
- **Reliable Data Exchange**: Timeout handling, error management, and retry mechanisms
- **High-Performance Server Design**: Concurrent processing, load balancing, and memory efficiency
- **Practical Applications**: Database operations, computation services, and configuration management

## 🧠 Deep Understanding of Service Communication

### 🏗️ Architecture Overview

```cpp
// Service communication internal structure
┌─────────────────────────────────────────────────────────────┐
│                   Shared Memory Space                        │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Request Queue                          │    │
│  │ [Req 0][Req 1][Req 2]...[Req N-1]                 │    │
│  │    ↑                           ↑                    │    │
│  │  Process Pos                 Add Pos                │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Response Queue                         │    │
│  │ [Res 0][Res 1][Res 2]...[Res N-1]                 │    │
│  │    ↑                           ↑                    │    │
│  │  Read Pos                   Write Pos               │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  Request-Response Mapping:                                  │
│  - Request ID (unique identifier)                           │
│  - Timestamp (timeout management)                           │
│  - Process State (waiting/processing/completed)             │
│  - Error Code (detailed failure information)                │
└─────────────────────────────────────────────────────────────┘

Multiple Clients ← [shared memory] → Single Server
      │                                        │
   Send Request                        Processing Engine
   Receive Response                           │
      │                                   Parallel Processing
   Timeout Management                     Result Generation
```

### ⚡ Why is it Reliable and Fast?

**1. Both Synchronous and Asynchronous Support**
```cpp
// Synchronous communication (simple)
ServiceClient<int, int> client("calc_service");
client.sendRequest(42);
if (client.waitForResponse(5000000)) {  // 5 second timeout
    int result = client.getResponse();
    std::cout << "Result: " << result << std::endl;
}

// Asynchronous communication (high performance)
client.sendRequestAsync(42);
// Execute other processing...
if (client.checkResponse()) {
    int result = client.getResponse();
    std::cout << "Result: " << result << std::endl;
}
```

**2. Reliable Request-Response Mapping**
```cpp
// Internal request management
struct RequestHeader {
    uint64_t request_id;        // Unique request ID
    uint64_t timestamp_us;      // Send timestamp
    uint32_t timeout_ms;        // Timeout duration
    uint32_t retry_count;       // Retry count
    uint32_t priority;          // Priority
    RequestStatus status;       // Processing status
};

// Reliable request-response pairing
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

## 🚀 Basic Usage Examples

### 1. Simple Calculator Service

**Server Side:**
```cpp
#include "shm_service.hpp"
#include <iostream>
#include <thread>

using namespace irlab::shm;

int main() {
    // Create calculation service server
    ServiceServer<int, int> calc_server("calculator");
    
    std::cout << "🖥️ Calculator service started!" << std::endl;
    
    while (true) {
        if (calc_server.hasRequest()) {
            int input = calc_server.getRequest();
            
            std::cout << "Processing request: " << input << std::endl;
            
            // Perform calculation (square)
            int result = input * input;
            
            // Send response
            calc_server.sendResponse(result);
            
            std::cout << "Sent result: " << result << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    return 0;
}
```

**Client Side:**
```cpp
#include "shm_service.hpp"
#include <iostream>
#include <thread>

using namespace irlab::shm;

int main() {
    ServiceClient<int, int> calc_client("calculator");
    
    std::cout << "🔢 Calculator client started!" << std::endl;
    
    for (int i = 1; i <= 10; ++i) {
        std::cout << "Sending request: " << i << std::endl;
        
        calc_client.sendRequest(i);
        
        // Wait for response (5 second timeout)
        if (calc_client.waitForResponse(5000000)) {
            int result = calc_client.getResponse();
            std::cout << "✅ " << i << "² = " << result << std::endl;
        } else {
            std::cout << "❌ Timeout for request: " << i << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    return 0;
}
```

### 2. String Processing Service

```cpp
// Custom data structures
struct StringRequest {
    char text[256];
    int operation;  // 0: uppercase, 1: lowercase, 2: reverse
};

struct StringResponse {
    char result[256];
    bool success;
    int error_code;
};

// Server implementation
class StringProcessorServer {
private:
    ServiceServer<StringRequest, StringResponse> server_;
    
public:
    StringProcessorServer() : server_("string_processor") {}
    
    void run() {
        std::cout << "📝 String Processor Service started!" << std::endl;
        
        while (true) {
            if (server_.hasRequest()) {
                StringRequest req = server_.getRequest();
                StringResponse resp = processString(req);
                server_.sendResponse(resp);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
private:
    StringResponse processString(const StringRequest& req) {
        StringResponse resp;
        resp.success = true;
        resp.error_code = 0;
        
        std::string text(req.text);
        
        try {
            switch (req.operation) {
                case 0: // uppercase
                    std::transform(text.begin(), text.end(), text.begin(), ::toupper);
                    break;
                case 1: // lowercase
                    std::transform(text.begin(), text.end(), text.begin(), ::tolower);
                    break;
                case 2: // reverse
                    std::reverse(text.begin(), text.end());
                    break;
                default:
                    resp.success = false;
                    resp.error_code = 1; // Invalid operation
                    strcpy(resp.result, "Invalid operation");
                    return resp;
            }
            
            strcpy(resp.result, text.c_str());
        } catch (const std::exception& e) {
            resp.success = false;
            resp.error_code = 2; // Processing error
            strcpy(resp.result, e.what());
        }
        
        return resp;
    }
};

// Client usage
void useStringProcessor() {
    ServiceClient<StringRequest, StringResponse> client("string_processor");
    
    StringRequest req;
    strcpy(req.text, "Hello World");
    req.operation = 0; // uppercase
    
    client.sendRequest(req);
    
    if (client.waitForResponse(3000000)) { // 3 second timeout
        StringResponse resp = client.getResponse();
        if (resp.success) {
            std::cout << "Result: " << resp.result << std::endl; // "HELLO WORLD"
        } else {
            std::cout << "Error: " << resp.result << " (code: " << resp.error_code << ")" << std::endl;
        }
    }
}
```

## 🔧 Advanced Features and Error Handling

### 3. Robust Service with Timeout and Retry

```cpp
class RobustServiceClient {
private:
    ServiceClient<int, int> client_;
    static const int MAX_RETRIES = 3;
    static const int TIMEOUT_US = 2000000; // 2 seconds
    
public:
    RobustServiceClient(const std::string& service_name) : client_(service_name) {}
    
    bool callService(int input, int& output) {
        for (int retry = 0; retry < MAX_RETRIES; ++retry) {
            std::cout << "Attempt " << (retry + 1) << "/" << MAX_RETRIES << std::endl;
            
            // Send request
            client_.sendRequest(input);
            
            // Wait for response with timeout
            if (client_.waitForResponse(TIMEOUT_US)) {
                output = client_.getResponse();
                std::cout << "✅ Service call successful" << std::endl;
                return true;
            } else {
                std::cout << "⏰ Timeout on attempt " << (retry + 1) << std::endl;
                
                // Wait before retry
                if (retry < MAX_RETRIES - 1) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
        
        std::cout << "❌ Service call failed after " << MAX_RETRIES << " attempts" << std::endl;
        return false;
    }
};

// Usage
int main() {
    RobustServiceClient client("robust_calc");
    
    int result;
    if (client.callService(42, result)) {
        std::cout << "Final result: " << result << std::endl;
    } else {
        std::cout << "Service unavailable" << std::endl;
    }
    
    return 0;
}
```

### 4. High-Performance Concurrent Server

```cpp
class ConcurrentCalculatorServer {
private:
    ServiceServer<int, int> server_;
    std::atomic<bool> running_;
    std::vector<std::thread> worker_threads_;
    std::queue<std::pair<int, std::promise<int>>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
public:
    ConcurrentCalculatorServer(int num_workers = 4) 
        : server_("concurrent_calc"), running_(true) {
        
        // Start worker threads
        for (int i = 0; i < num_workers; ++i) {
            worker_threads_.emplace_back([this, i]() {
                workerLoop(i);
            });
        }
        
        std::cout << "🚀 Concurrent server started with " << num_workers << " workers" << std::endl;
    }
    
    ~ConcurrentCalculatorServer() {
        running_ = false;
        queue_cv_.notify_all();
        
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    void run() {
        while (running_) {
            if (server_.hasRequest()) {
                int request = server_.getRequest();
                
                // Create a promise for the result
                std::promise<int> result_promise;
                auto result_future = result_promise.get_future();
                
                // Add task to queue
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    task_queue_.emplace(request, std::move(result_promise));
                }
                queue_cv_.notify_one();
                
                // Wait for result and send response
                int result = result_future.get();
                server_.sendResponse(result);
            }
            
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    
private:
    void workerLoop(int worker_id) {
        while (running_) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { return !task_queue_.empty() || !running_; });
            
            if (!running_) break;
            
            auto task = std::move(task_queue_.front());
            task_queue_.pop();
            lock.unlock();
            
            // Process the task (expensive computation)
            int input = task.first;
            int result = performComplexCalculation(input, worker_id);
            
            // Set the result
            task.second.set_value(result);
        }
    }
    
    int performComplexCalculation(int input, int worker_id) {
        // Simulate complex computation
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        std::cout << "Worker " << worker_id << " processed: " << input << std::endl;
        return input * input + input; // Simple formula
    }
};
```

## 📊 Performance Measurement and Optimization

### 5. Latency Benchmarking

```cpp
#include <chrono>
#include <vector>
#include <algorithm>

class ServiceBenchmark {
private:
    ServiceClient<int, int> client_;
    std::vector<double> latencies_;
    
public:
    ServiceBenchmark(const std::string& service_name) : client_(service_name) {}
    
    void runBenchmark(int num_requests = 1000) {
        std::cout << "🏁 Starting benchmark with " << num_requests << " requests..." << std::endl;
        
        latencies_.clear();
        latencies_.reserve(num_requests);
        
        for (int i = 0; i < num_requests; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            client_.sendRequest(i);
            
            if (client_.waitForResponse(1000000)) { // 1 second timeout
                auto end = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                latencies_.push_back(duration.count());
                client_.getResponse(); // Consume response
            } else {
                std::cout << "❌ Timeout on request " << i << std::endl;
            }
            
            // Small delay to avoid overwhelming
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        
        printStatistics();
    }
    
private:
    void printStatistics() {
        if (latencies_.empty()) {
            std::cout << "No successful requests" << std::endl;
            return;
        }
        
        std::sort(latencies_.begin(), latencies_.end());
        
        double avg = std::accumulate(latencies_.begin(), latencies_.end(), 0.0) / latencies_.size();
        double median = latencies_[latencies_.size() / 2];
        double min_latency = latencies_.front();
        double max_latency = latencies_.back();
        double p95 = latencies_[static_cast<size_t>(latencies_.size() * 0.95)];
        double p99 = latencies_[static_cast<size_t>(latencies_.size() * 0.99)];
        
        std::cout << "\n📊 Benchmark Results:" << std::endl;
        std::cout << "Successful requests: " << latencies_.size() << std::endl;
        std::cout << "Average latency: " << avg << " μs" << std::endl;
        std::cout << "Median latency: " << median << " μs" << std::endl;
        std::cout << "Min latency: " << min_latency << " μs" << std::endl;
        std::cout << "Max latency: " << max_latency << " μs" << std::endl;
        std::cout << "95th percentile: " << p95 << " μs" << std::endl;
        std::cout << "99th percentile: " << p99 << " μs" << std::endl;
        std::cout << "Throughput: " << (1000000.0 / avg) << " requests/second" << std::endl;
    }
};
```

## 🛠️ Real-World Applications

### 6. Database Service Proxy

```cpp
struct DatabaseQuery {
    char sql[512];
    int query_type; // 0: SELECT, 1: INSERT, 2: UPDATE, 3: DELETE
    int timeout_ms;
};

struct DatabaseResult {
    char data[1024];
    int row_count;
    bool success;
    int error_code;
    char error_message[256];
};

class DatabaseServiceProxy {
private:
    ServiceServer<DatabaseQuery, DatabaseResult> server_;
    // Assume some database connection
    
public:
    DatabaseServiceProxy() : server_("database_service") {}
    
    void run() {
        while (true) {
            if (server_.hasRequest()) {
                DatabaseQuery query = server_.getRequest();
                DatabaseResult result = executeQuery(query);
                server_.sendResponse(result);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
private:
    DatabaseResult executeQuery(const DatabaseQuery& query) {
        DatabaseResult result;
        result.success = true;
        result.error_code = 0;
        result.row_count = 0;
        
        try {
            // Simulate database operation
            std::this_thread::sleep_for(std::chrono::milliseconds(query.timeout_ms / 10));
            
            switch (query.query_type) {
                case 0: // SELECT
                    strcpy(result.data, "[{\"id\":1,\"name\":\"test\"}]");
                    result.row_count = 1;
                    break;
                case 1: // INSERT
                    strcpy(result.data, "INSERT successful");
                    result.row_count = 1;
                    break;
                default:
                    strcpy(result.data, "Operation completed");
                    result.row_count = 0;
            }
        } catch (const std::exception& e) {
            result.success = false;
            result.error_code = 500;
            strcpy(result.error_message, e.what());
        }
        
        return result;
    }
};
```

## 🎯 Best Practices and Guidelines

### Design Principles

1. **Keep Request/Response Data Small**
   ```cpp
   // ✅ Good - Small, focused data
   struct CalculationRequest {
       double value;
       int operation;
   };
   
   // ❌ Avoid - Large data structures
   struct HugeRequest {
       char large_buffer[1024*1024]; // 1MB - too large
   };
   ```

2. **Use Appropriate Timeouts**
   ```cpp
   // Different timeouts for different operations
   const int QUICK_OPERATION_TIMEOUT = 100000;    // 100ms
   const int NORMAL_OPERATION_TIMEOUT = 1000000;  // 1s
   const int LONG_OPERATION_TIMEOUT = 10000000;   // 10s
   ```

3. **Implement Proper Error Handling**
   ```cpp
   enum class ServiceError {
       SUCCESS = 0,
       TIMEOUT = 1,
       INVALID_REQUEST = 2,
       PROCESSING_ERROR = 3,
       SERVICE_UNAVAILABLE = 4
   };
   ```

### Performance Tips

1. **Minimize Memory Allocations**
2. **Use Async Pattern for High Throughput**  
3. **Implement Connection Pooling for Multiple Services**
4. **Monitor Service Health and Performance**

## 🔍 Troubleshooting Common Issues

### Common Problems and Solutions

1. **Service Server Not Responding**
   - Check if server process is running
   - Verify service name matches exactly
   - Ensure shared memory permissions

2. **Client Timeout Issues**
   - Increase timeout value
   - Check server processing time
   - Implement retry mechanism

3. **Memory Issues**
   - Monitor shared memory usage
   - Implement proper cleanup
   - Use appropriate buffer sizes

## 📚 Next Steps

- **[⚡ Action Communication](tutorials_shm_action_en.md)** - For long-running asynchronous tasks
- **[🔄 Pub/Sub Communication](tutorials_shm_pub_sub_en.md)** - For broadcast communication
- **[🐍 Python Integration](tutorials_python_en.md)** - Multi-language development

---

**🎉 Congratulations!** You've mastered reliable request-response communication! Your distributed applications now have rock-solid service communication! 🚀✨
