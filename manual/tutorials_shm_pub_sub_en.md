# 🔄 Pub/Sub Communication Complete Guide - Master High-Speed Broadcast Communication
[English | [日本語](docs_jp/md_manual_tutorials_shm_pub_sub_jp.html)]

## 🎯 What You'll Learn in This Guide

- **Deep Understanding of Pub/Sub Communication**: From design philosophy to implementation details of broadcast patterns
- **Ultra-High Speed Data Streaming**: Zero-copy techniques, ring buffer optimization, memory layout considerations
- **Multi-Subscriber Architecture**: Efficient one-to-many communication, subscriber management, data consistency
- **Practical Applications**: Sensor networks, real-time monitoring, high-frequency data distribution

## 🧠 Deep Understanding of Pub/Sub Communication

### 🏗️ Architecture Overview

```cpp
// Pub/Sub communication internal structure
┌─────────────────────────────────────────────────────────────┐
│                   Shared Memory Space                        │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              Ring Buffer                            │    │
│  │ [Data 0][Data 1][Data 2]...[Data N-1]             │    │
│  │    ↑                           ↑                    │    │
│  │  Read Pos                   Write Pos               │    │
│  │  (Subscribers)              (Publisher)             │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  Synchronization Mechanism:                                 │
│  - Atomic operations (lock-free for maximum speed)          │
│  - Memory barriers (ensuring data consistency)              │
│  - Cache-line alignment (optimal CPU cache usage)           │
│  - Write sequence numbers (detecting data freshness)        │
└─────────────────────────────────────────────────────────────┘

Single Publisher ← [shared memory] → Multiple Subscribers
      │                                        │
   Publish Data                          Subscribe Data
   Atomic Write                          Atomic Read
      │                                        │
   Zero-Copy Transfer                    Lock-Free Access
```

### ⚡ Why is it Incredibly Fast?

**1. Zero-Copy Direct Memory Access**
```cpp
// Traditional approach (slow): Multiple copies
char buffer[1024];
serialize_data(sensor_data, buffer);     // Copy 1: Object → Buffer
write_to_socket(buffer, 1024);          // Copy 2: Buffer → Network
// Receiver side
read_from_socket(buffer, 1024);         // Copy 3: Network → Buffer
deserialize_data(buffer, sensor_data);  // Copy 4: Buffer → Object

// SHM approach (fast): Zero-copy
Publisher<SensorData> pub("sensors");
pub.publish(sensor_data);               // Direct memory write
// Subscribers read directly from shared memory - no copying!
```

**2. Lock-Free Ring Buffer Design**
```cpp
// Internal ring buffer implementation (simplified)
template<typename T>
class LockFreeRingBuffer {
private:
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};
    alignas(64) T data_[BUFFER_SIZE];    // Cache-line aligned
    
public:
    bool publish(const T& item) {
        size_t current_write = write_pos_.load(std::memory_order_relaxed);
        size_t next_write = (current_write + 1) % BUFFER_SIZE;
        
        // Check if buffer is full
        if (next_write == read_pos_.load(std::memory_order_acquire)) {
            return false;  // Buffer full
        }
        
        // Write data (no locks needed!)
        data_[current_write] = item;
        
        // Update write position atomically
        write_pos_.store(next_write, std::memory_order_release);
        return true;
    }
    
    bool subscribe(T& item) {
        size_t current_read = read_pos_.load(std::memory_order_relaxed);
        
        // Check if data available
        if (current_read == write_pos_.load(std::memory_order_acquire)) {
            return false;  // No data available
        }
        
        // Read data (no locks needed!)
        item = data_[current_read];
        
        // Update read position atomically
        read_pos_.store((current_read + 1) % BUFFER_SIZE, std::memory_order_release);
        return true;
    }
};
```

## 🚀 Basic Usage Examples

### 1. Simple Sensor Data Publisher

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <thread>
#include <random>

using namespace irlab::shm;

// Custom sensor data structure
struct SensorReading {
    double temperature;
    double humidity;
    uint64_t timestamp_us;
    int sensor_id;
};

int main() {
    // Create publisher with ring buffer size of 10
    Publisher<SensorReading> sensor_pub("environment_sensors", 10);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> temp_dist(15.0, 35.0);
    std::uniform_real_distribution<> humidity_dist(30.0, 80.0);
    
    std::cout << "🌡️ Environmental sensor publisher started!" << std::endl;
    
    for (int i = 0; i < 1000; ++i) {
        SensorReading reading;
        reading.temperature = temp_dist(gen);
        reading.humidity = humidity_dist(gen);
        reading.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        reading.sensor_id = 1;
        
        sensor_pub.publish(reading);
        
        std::cout << "📊 Published: Temp=" << reading.temperature 
                  << "°C, Humidity=" << reading.humidity << "%" << std::endl;
        
        // High frequency publishing (10Hz)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

### 2. Multiple Subscribers with Different Processing

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <thread>
#include <vector>

using namespace irlab::shm;

// Alert monitor subscriber
void temperature_monitor() {
    Subscriber<SensorReading> sub("environment_sensors");
    
    std::cout << "🚨 Temperature alert monitor started!" << std::endl;
    
    while (true) {
        bool success;
        SensorReading reading = sub.subscribe(&success);
        
        if (success) {
            if (reading.temperature > 30.0) {
                std::cout << "🔥 HIGH TEMP ALERT: " << reading.temperature 
                          << "°C at sensor " << reading.sensor_id << std::endl;
            } else if (reading.temperature < 18.0) {
                std::cout << "🧊 LOW TEMP ALERT: " << reading.temperature 
                          << "°C at sensor " << reading.sensor_id << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Data logger subscriber
void data_logger() {
    Subscriber<SensorReading> sub("environment_sensors");
    
    std::cout << "📝 Data logger started!" << std::endl;
    
    while (true) {
        bool success;
        SensorReading reading = sub.subscribe(&success);
        
        if (success) {
            // Log to file or database
            std::cout << "📋 LOG: [" << reading.timestamp_us << "] "
                      << "Sensor" << reading.sensor_id << " - "
                      << "T:" << reading.temperature << "°C, "
                      << "H:" << reading.humidity << "%" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Statistics calculator subscriber
void statistics_calculator() {
    Subscriber<SensorReading> sub("environment_sensors");
    
    std::cout << "📊 Statistics calculator started!" << std::endl;
    
    std::vector<double> temperatures;
    std::vector<double> humidities;
    
    while (true) {
        bool success;
        SensorReading reading = sub.subscribe(&success);
        
        if (success) {
            temperatures.push_back(reading.temperature);
            humidities.push_back(reading.humidity);
            
            // Calculate running averages every 10 readings
            if (temperatures.size() % 10 == 0) {
                double avg_temp = std::accumulate(temperatures.end() - 10, 
                                                temperatures.end(), 0.0) / 10.0;
                double avg_humidity = std::accumulate(humidities.end() - 10, 
                                                    humidities.end(), 0.0) / 10.0;
                
                std::cout << "📈 10-reading average: Temp=" << avg_temp 
                          << "°C, Humidity=" << avg_humidity << "%" << std::endl;
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

int main() {
    // Start multiple subscriber threads
    std::vector<std::thread> subscribers;
    
    subscribers.emplace_back(temperature_monitor);
    subscribers.emplace_back(data_logger);
    subscribers.emplace_back(statistics_calculator);
    
    // Wait for all subscribers
    for (auto& t : subscribers) {
        t.join();
    }
    
    return 0;
}
```

## 🔧 Advanced Features and Optimization

### 3. High-Performance Multi-Threaded Publisher

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace irlab::shm;

// High-performance publisher with worker threads
class HighPerformancePublisher {
private:
    Publisher<SensorReading> publisher_;
    std::atomic<bool> running_{true};
    std::queue<SensorReading> data_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::vector<std::thread> worker_threads_;
    
public:
    HighPerformancePublisher(const std::string& topic_name, int num_workers = 4) 
        : publisher_(topic_name, 100) {  // Large ring buffer for high throughput
        
        // Start worker threads
        for (int i = 0; i < num_workers; ++i) {
            worker_threads_.emplace_back([this, i]() {
                publisherWorker(i);
            });
        }
        
        std::cout << "🚀 High-performance publisher started with " 
                  << num_workers << " workers" << std::endl;
    }
    
    ~HighPerformancePublisher() {
        running_ = false;
        queue_cv_.notify_all();
        
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
    void addData(const SensorReading& reading) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            data_queue_.push(reading);
        }
        queue_cv_.notify_one();
    }
    
private:
    void publisherWorker(int worker_id) {
        while (running_) {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() { 
                return !data_queue_.empty() || !running_; 
            });
            
            if (!running_) break;
            
            SensorReading reading = data_queue_.front();
            data_queue_.pop();
            lock.unlock();
            
            // Publish data (non-blocking)
            bool success = publisher_.publish(reading);
            if (!success) {
                std::cout << "⚠️ Worker " << worker_id 
                          << ": Buffer full, data dropped" << std::endl;
            } else {
                // Optional: Log successful publishes
                if (reading.sensor_id % 100 == 0) {  // Log every 100th reading
                    std::cout << "✅ Worker " << worker_id 
                              << " published reading " << reading.sensor_id << std::endl;
                }
            }
        }
    }
};

// Usage example
int main() {
    HighPerformancePublisher hp_pub("high_freq_sensors", 8);
    
    // Simulate high-frequency data generation
    std::thread data_generator([&hp_pub]() {
        int sensor_id = 0;
        while (sensor_id < 10000) {
            SensorReading reading;
            reading.temperature = 20.0 + (sensor_id % 20);
            reading.humidity = 50.0 + (sensor_id % 30);
            reading.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            reading.sensor_id = sensor_id++;
            
            hp_pub.addData(reading);
            
            // Very high frequency - 1000Hz
            std::this_thread::sleep_for(std::chrono::microseconds(1000));
        }
    });
    
    data_generator.join();
    
    return 0;
}
```

### 4. Intelligent Subscriber with Data Filtering

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <functional>
#include <chrono>

using namespace irlab::shm;

// Smart subscriber with filtering and processing capabilities
template<typename T>
class SmartSubscriber {
private:
    Subscriber<T> subscriber_;
    std::function<bool(const T&)> filter_;
    std::function<void(const T&)> processor_;
    std::chrono::microseconds poll_interval_;
    
public:
    SmartSubscriber(const std::string& topic_name, 
                   std::chrono::microseconds poll_interval = std::chrono::microseconds(1000))
        : subscriber_(topic_name), poll_interval_(poll_interval) {}
    
    void setFilter(std::function<bool(const T&)> filter) {
        filter_ = filter;
    }
    
    void setProcessor(std::function<void(const T&)> processor) {
        processor_ = processor;
    }
    
    void startProcessing() {
        std::cout << "🎯 Smart subscriber started processing" << std::endl;
        
        while (true) {
            bool success;
            T data = subscriber_.subscribe(&success);
            
            if (success) {
                // Apply filter if set
                if (!filter_ || filter_(data)) {
                    // Process data if processor is set
                    if (processor_) {
                        processor_(data);
                    }
                }
            }
            
            std::this_thread::sleep_for(poll_interval_);
        }
    }
};

// Specialized filters and processors
namespace Filters {
    auto temperatureRange(double min_temp, double max_temp) {
        return [min_temp, max_temp](const SensorReading& reading) {
            return reading.temperature >= min_temp && reading.temperature <= max_temp;
        };
    }
    
    auto sensorId(int target_id) {
        return [target_id](const SensorReading& reading) {
            return reading.sensor_id == target_id;
        };
    }
    
    auto timeWindow(uint64_t start_time_us, uint64_t end_time_us) {
        return [start_time_us, end_time_us](const SensorReading& reading) {
            return reading.timestamp_us >= start_time_us && 
                   reading.timestamp_us <= end_time_us;
        };
    }
}

namespace Processors {
    auto alertProcessor(const std::string& alert_name) {
        return [alert_name](const SensorReading& reading) {
            std::cout << "🚨 " << alert_name << " ALERT: "
                      << "Sensor" << reading.sensor_id 
                      << " Temp=" << reading.temperature << "°C "
                      << "Humidity=" << reading.humidity << "%" << std::endl;
        };
    }
    
    auto csvLogger(const std::string& filename) {
        return [filename](const SensorReading& reading) {
            // In real implementation, you'd write to actual CSV file
            std::cout << "📄 CSV LOG [" << filename << "]: "
                      << reading.timestamp_us << ","
                      << reading.sensor_id << ","
                      << reading.temperature << ","
                      << reading.humidity << std::endl;
        };
    }
    
    auto statisticsAggregator() {
        return [](const SensorReading& reading) {
            static double temp_sum = 0.0;
            static double humidity_sum = 0.0;
            static int count = 0;
            
            temp_sum += reading.temperature;
            humidity_sum += reading.humidity;
            count++;
            
            if (count % 50 == 0) {  // Report every 50 readings
                std::cout << "📊 Running averages (n=" << count << "): "
                          << "Temp=" << (temp_sum / count) << "°C, "
                          << "Humidity=" << (humidity_sum / count) << "%" << std::endl;
            }
        };
    }
}

// Usage example
int main() {
    // High-temperature alert subscriber
    SmartSubscriber<SensorReading> high_temp_subscriber("environment_sensors");
    high_temp_subscriber.setFilter(Filters::temperatureRange(28.0, 100.0));
    high_temp_subscriber.setProcessor(Processors::alertProcessor("HIGH TEMPERATURE"));
    
    // Specific sensor logger
    SmartSubscriber<SensorReading> sensor_1_logger("environment_sensors");
    sensor_1_logger.setFilter(Filters::sensorId(1));
    sensor_1_logger.setProcessor(Processors::csvLogger("sensor_1_data.csv"));
    
    // Statistics aggregator
    SmartSubscriber<SensorReading> stats_subscriber("environment_sensors");
    stats_subscriber.setProcessor(Processors::statisticsAggregator());
    
    // Start processing in separate threads
    std::vector<std::thread> processors;
    processors.emplace_back([&high_temp_subscriber]() { 
        high_temp_subscriber.startProcessing(); 
    });
    processors.emplace_back([&sensor_1_logger]() { 
        sensor_1_logger.startProcessing(); 
    });
    processors.emplace_back([&stats_subscriber]() { 
        stats_subscriber.startProcessing(); 
    });
    
    // Wait for all processors
    for (auto& t : processors) {
        t.join();
    }
    
    return 0;
}
```

## 📊 Performance Measurement and Benchmarking

### 5. Comprehensive Performance Testing

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>

using namespace irlab::shm;

class PubSubBenchmark {
private:
    std::vector<double> publish_latencies_;
    std::vector<double> subscribe_latencies_;
    std::vector<double> end_to_end_latencies_;
    
public:
    void runLatencyBenchmark(int num_samples = 10000) {
        std::cout << "🏁 Starting latency benchmark with " << num_samples << " samples" << std::endl;
        
        Publisher<uint64_t> pub("benchmark_topic", 100);
        Subscriber<uint64_t> sub("benchmark_topic");
        
        // Warm up
        for (int i = 0; i < 100; ++i) {
            pub.publish(i);
            bool success;
            sub.subscribe(&success);
        }
        
        publish_latencies_.clear();
        subscribe_latencies_.clear();
        end_to_end_latencies_.clear();
        
        for (int i = 0; i < num_samples; ++i) {
            // Measure publish latency
            auto publish_start = std::chrono::high_resolution_clock::now();
            uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                publish_start.time_since_epoch()).count();
            pub.publish(timestamp);
            auto publish_end = std::chrono::high_resolution_clock::now();
            
            double publish_latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                publish_end - publish_start).count();
            publish_latencies_.push_back(publish_latency);
            
            // Measure subscribe latency
            auto subscribe_start = std::chrono::high_resolution_clock::now();
            bool success;
            uint64_t received_timestamp = sub.subscribe(&success);
            auto subscribe_end = std::chrono::high_resolution_clock::now();
            
            if (success) {
                double subscribe_latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    subscribe_end - subscribe_start).count();
                subscribe_latencies_.push_back(subscribe_latency);
                
                // Calculate end-to-end latency
                double end_to_end = subscribe_end.time_since_epoch().count() - received_timestamp;
                end_to_end_latencies_.push_back(end_to_end);
            }
            
            // Small delay to avoid overwhelming
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        
        printLatencyStatistics();
    }
    
    void runThroughputBenchmark(int duration_seconds = 10) {
        std::cout << "🚀 Starting throughput benchmark for " << duration_seconds << " seconds" << std::endl;
        
        Publisher<int> pub("throughput_topic", 1000);  // Large buffer
        std::atomic<int> published_count{0};
        std::atomic<int> received_count{0};
        std::atomic<bool> running{true};
        
        // Publisher thread
        std::thread publisher([&pub, &published_count, &running]() {
            int counter = 0;
            while (running) {
                if (pub.publish(counter++)) {
                    published_count++;
                }
            }
        });
        
        // Subscriber thread
        std::thread subscriber([&pub, &received_count, &running]() {
            Subscriber<int> sub("throughput_topic");
            while (running) {
                bool success;
                sub.subscribe(&success);
                if (success) {
                    received_count++;
                }
            }
        });
        
        // Run for specified duration
        std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
        running = false;
        
        publisher.join();
        subscriber.join();
        
        double pub_throughput = published_count.load() / static_cast<double>(duration_seconds);
        double sub_throughput = received_count.load() / static_cast<double>(duration_seconds);
        
        std::cout << "\n📈 Throughput Results:" << std::endl;
        std::cout << "Published: " << published_count.load() << " messages" << std::endl;
        std::cout << "Received: " << received_count.load() << " messages" << std::endl;
        std::cout << "Publisher throughput: " << pub_throughput << " msg/sec" << std::endl;
        std::cout << "Subscriber throughput: " << sub_throughput << " msg/sec" << std::endl;
        std::cout << "Data loss: " << ((published_count - received_count) * 100.0 / published_count) << "%" << std::endl;
    }
    
private:
    void printLatencyStatistics() {
        auto printStats = [](const std::string& name, const std::vector<double>& data) {
            if (data.empty()) return;
            
            auto sorted_data = data;
            std::sort(sorted_data.begin(), sorted_data.end());
            
            double avg = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
            double median = sorted_data[sorted_data.size() / 2];
            double min_val = sorted_data.front();
            double max_val = sorted_data.back();
            double p95 = sorted_data[static_cast<size_t>(sorted_data.size() * 0.95)];
            double p99 = sorted_data[static_cast<size_t>(sorted_data.size() * 0.99)];
            
            std::cout << "\n📊 " << name << " Statistics (nanoseconds):" << std::endl;
            std::cout << "  Average: " << avg << " ns" << std::endl;
            std::cout << "  Median: " << median << " ns" << std::endl;
            std::cout << "  Min: " << min_val << " ns" << std::endl;
            std::cout << "  Max: " << max_val << " ns" << std::endl;
            std::cout << "  95th percentile: " << p95 << " ns" << std::endl;
            std::cout << "  99th percentile: " << p99 << " ns" << std::endl;
        };
        
        printStats("Publish Latency", publish_latencies_);
        printStats("Subscribe Latency", subscribe_latencies_);
        printStats("End-to-End Latency", end_to_end_latencies_);
    }
};

int main() {
    PubSubBenchmark benchmark;
    
    std::cout << "Starting comprehensive Pub/Sub performance evaluation...\n" << std::endl;
    
    // Run latency benchmark
    benchmark.runLatencyBenchmark(5000);
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    
    // Run throughput benchmark
    benchmark.runThroughputBenchmark(5);
    
    return 0;
}
```

## 🛠️ Real-World Applications

### 6. Robot Sensor Network

```cpp
// Complete robot sensor network example
#include "shm_pub_sub.hpp"
#include <iostream>
#include <thread>
#include <vector>
#include <random>

using namespace irlab::shm;

// Robot sensor data structures
struct LidarReading {
    std::vector<float> ranges;
    float angle_min;
    float angle_max;
    float angle_increment;
    uint64_t timestamp_us;
};

struct CameraFrame {
    int width;
    int height;
    uint64_t frame_id;
    uint64_t timestamp_us;
    // In real implementation, would include image data pointer
};

struct IMUReading {
    float accel_x, accel_y, accel_z;
    float gyro_x, gyro_y, gyro_z;
    float mag_x, mag_y, mag_z;
    uint64_t timestamp_us;
};

// Sensor simulation
class LidarSimulator {
private:
    Publisher<LidarReading> publisher_;
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_real_distribution<> range_dist_;
    
public:
    LidarSimulator() : publisher_("robot_lidar", 20), gen_(rd_()), range_dist_(0.1, 10.0) {}
    
    void startPublishing() {
        std::cout << "🤖 LIDAR simulator started" << std::endl;
        
        while (true) {
            LidarReading reading;
            reading.angle_min = -M_PI;
            reading.angle_max = M_PI;
            reading.angle_increment = M_PI / 180.0;  // 1 degree
            reading.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            // Generate 360 range readings
            reading.ranges.resize(360);
            for (int i = 0; i < 360; ++i) {
                reading.ranges[i] = range_dist_(gen_);
            }
            
            publisher_.publish(reading);
            
            // 10Hz LIDAR
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
};

class CameraSimulator {
private:
    Publisher<CameraFrame> publisher_;
    uint64_t frame_counter_;
    
public:
    CameraSimulator() : publisher_("robot_camera", 10), frame_counter_(0) {}
    
    void startPublishing() {
        std::cout << "📷 Camera simulator started" << std::endl;
        
        while (true) {
            CameraFrame frame;
            frame.width = 640;
            frame.height = 480;
            frame.frame_id = frame_counter_++;
            frame.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            publisher_.publish(frame);
            
            // 30Hz camera
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
    }
};

class IMUSimulator {
private:
    Publisher<IMUReading> publisher_;
    std::random_device rd_;
    std::mt19937 gen_;
    std::normal_distribution<> accel_dist_;
    std::normal_distribution<> gyro_dist_;
    
public:
    IMUSimulator() : publisher_("robot_imu", 50), gen_(rd_), 
                     accel_dist_(0.0, 0.1), gyro_dist_(0.0, 0.05) {}
    
    void startPublishing() {
        std::cout << "📐 IMU simulator started" << std::endl;
        
        while (true) {
            IMUReading reading;
            reading.accel_x = accel_dist_(gen_);
            reading.accel_y = accel_dist_(gen_);
            reading.accel_z = 9.81 + accel_dist_(gen_);  // Gravity + noise
            reading.gyro_x = gyro_dist_(gen_);
            reading.gyro_y = gyro_dist_(gen_);
            reading.gyro_z = gyro_dist_(gen_);
            reading.mag_x = 0.2 + accel_dist_(gen_);
            reading.mag_y = 0.1 + accel_dist_(gen_);
            reading.mag_z = 0.4 + accel_dist_(gen_);
            reading.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            
            publisher_.publish(reading);
            
            // 100Hz IMU
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
};

// Robot control system that processes all sensor data
class RobotController {
private:
    Subscriber<LidarReading> lidar_sub_;
    Subscriber<CameraFrame> camera_sub_;
    Subscriber<IMUReading> imu_sub_;
    
public:
    RobotController() : lidar_sub_("robot_lidar"), 
                       camera_sub_("robot_camera"), 
                       imu_sub_("robot_imu") {}
    
    void startProcessing() {
        std::cout << "🧠 Robot controller started" << std::endl;
        
        while (true) {
            // Process LIDAR data
            bool lidar_success;
            LidarReading lidar = lidar_sub_.subscribe(&lidar_success);
            if (lidar_success) {
                processLidarData(lidar);
            }
            
            // Process camera data
            bool camera_success;
            CameraFrame camera = camera_sub_.subscribe(&camera_success);
            if (camera_success) {
                processCameraData(camera);
            }
            
            // Process IMU data
            bool imu_success;
            IMUReading imu = imu_sub_.subscribe(&imu_success);
            if (imu_success) {
                processIMUData(imu);
            }
            
            // Control loop at 50Hz
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    
private:
    void processLidarData(const LidarReading& lidar) {
        // Find minimum distance for obstacle avoidance
        float min_range = *std::min_element(lidar.ranges.begin(), lidar.ranges.end());
        
        if (min_range < 0.5) {  // 50cm obstacle threshold
            std::cout << "🚨 OBSTACLE DETECTED: " << min_range << "m away!" << std::endl;
        }
    }
    
    void processCameraData(const CameraFrame& camera) {
        // Image processing would go here
        if (camera.frame_id % 30 == 0) {  // Log every 30th frame
            std::cout << "📸 Processing frame " << camera.frame_id 
                      << " (" << camera.width << "x" << camera.height << ")" << std::endl;
        }
    }
    
    void processIMUData(const IMUReading& imu) {
        // Calculate total acceleration
        float total_accel = std::sqrt(imu.accel_x * imu.accel_x + 
                                    imu.accel_y * imu.accel_y + 
                                    imu.accel_z * imu.accel_z);
        
        if (total_accel > 15.0) {  // High acceleration threshold
            std::cout << "⚡ HIGH ACCELERATION: " << total_accel << " m/s²" << std::endl;
        }
    }
};

int main() {
    std::cout << "🤖 Starting robot sensor network simulation..." << std::endl;
    
    // Start all sensor simulators
    std::vector<std::thread> sensors;
    sensors.emplace_back([]() { LidarSimulator().startPublishing(); });
    sensors.emplace_back([]() { CameraSimulator().startPublishing(); });
    sensors.emplace_back([]() { IMUSimulator().startPublishing(); });
    
    // Start robot controller
    std::thread controller([]() { RobotController().startProcessing(); });
    
    // Wait for all threads
    for (auto& sensor : sensors) {
        sensor.join();
    }
    controller.join();
    
    return 0;
}
```

## 🎯 Best Practices and Performance Tips

### Design Guidelines

1. **Choose Appropriate Ring Buffer Sizes**
   ```cpp
   // High-frequency, low-latency: smaller buffer
   Publisher<SensorData> high_freq_pub("sensors", 10);
   
   // Bursty data, higher tolerance: larger buffer
   Publisher<ImageData> image_pub("images", 100);
   ```

2. **Use Appropriate Data Types**
   ```cpp
   // ✅ Good - Fixed-size, cache-friendly
   struct OptimalSensorData {
       float temperature;
       float humidity;
       uint32_t timestamp;
       uint16_t sensor_id;
   };
   
   // ❌ Avoid - Variable size, cache-unfriendly
   struct ProblematicData {
       std::string description;
       std::vector<float> values;
   };
   ```

3. **Memory Layout Optimization**
   ```cpp
   // Align structures for optimal memory access
   struct alignas(64) CacheOptimizedData {  // 64-byte cache line
       double value1;
       double value2;
       uint64_t timestamp;
       uint32_t id;
       char padding[28];  // Pad to cache line boundary
   };
   ```

### Performance Optimization

1. **CPU Affinity for Critical Threads**
2. **Lock-Free Programming Patterns**
3. **Memory Prefetching for Predictable Access**
4. **NUMA-Aware Memory Allocation**

## 🔍 Troubleshooting Common Issues

### Performance Issues

1. **Buffer Overflow**: Increase ring buffer size or reduce publish frequency
2. **Cache Misses**: Align data structures and use appropriate padding
3. **False Sharing**: Ensure different threads access different cache lines

### Data Consistency Issues

1. **Torn Reads**: Use atomic operations for critical data
2. **Stale Data**: Check timestamp validity
3. **Missing Data**: Implement sequence number checking

## 📚 Next Steps

- **[🤝 Service Communication](tutorials_shm_service_en.md)** - For reliable request-response patterns
- **[⚡ Action Communication](tutorials_shm_action_en.md)** - For long-running asynchronous tasks
- **[🐍 Python Integration](tutorials_python_en.md)** - Multi-language development

---

**🎉 Congratulations!** You've mastered ultra-high-speed broadcast communication! Your applications can now stream data at microsecond latencies! 🚀✨
