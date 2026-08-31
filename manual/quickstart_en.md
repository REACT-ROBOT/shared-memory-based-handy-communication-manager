# 🚀 Quick Start Guide
[English | [日本語](docs_jp/md_manual_quickstart_jp.html)]

## 🎯 Get Started in 5 Minutes!

This guide will help you experience the power of shared memory communication in just 5 minutes. Let's build a simple temperature monitoring system!

## 📋 Prerequisites

### System Requirements
- **OS**: Linux (Ubuntu 18.04+, CentOS 7+) or Windows with WSL2
- **Compiler**: GCC 7.0+ or Clang 6.0+
- **CMake**: 3.10+
- **Memory**: 1GB+ available RAM

### Quick Installation Check
```bash
# Check your system
g++ --version      # Should be 7.0+
cmake --version    # Should be 3.10+
free -h           # Check available memory
```

## 🔧 Installation (2 minutes)

### Method 1: Quick CMake Build
```bash
# Clone and build
git clone <repository-url>
cd shared-memory-based-handy-communication-manager
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Method 2: Header-Only Integration
```bash
# Copy headers to your project
cp -r include/shm_pub_sub.hpp /your/project/include/
```

## 🌡️ Your First Communication (3 minutes)

Let's create a temperature monitoring system with Publisher/Subscriber pattern!

### Step 1: Create the Temperature Sensor (Publisher)
```cpp
// sensor.cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <random>

using namespace irlab::shm;

int main() {
    // Create temperature publisher
    Publisher<float> temp_pub("temperature_sensor");
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(20.0, 30.0);
    
    std::cout << "🌡️ Temperature sensor started!\n";
    
    for (int i = 0; i < 50; ++i) {
        float temperature = dis(gen);
        temp_pub.publish(temperature);
        
        std::cout << "Published: " << temperature << "°C\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

### Step 2: Create the Monitor (Subscriber)
```cpp
// monitor.cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace irlab::shm;

int main() {
    // Create temperature subscriber
    Subscriber<float> temp_sub("temperature_sensor");
    
    std::cout << "🖥️ Temperature monitor started!\n";
    
    for (int i = 0; i < 50; ++i) {
        bool success;
        float temperature = temp_sub.subscribe(&success);
        
        if (success) {
            std::cout << "Received: " << temperature << "°C";
            
            // Alert for high temperature
            if (temperature > 28.0) {
                std::cout << " ⚠️ WARNING: High temperature!";
            }
            std::cout << "\n";
        } else {
            std::cout << "No data received\n";
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

### Step 3: Build and Run
```bash
# Compile
g++ -std=c++17 -pthread sensor.cpp -o sensor
g++ -std=c++17 -pthread monitor.cpp -o monitor

# Run in separate terminals
# Terminal 1:
./sensor

# Terminal 2:
./monitor
```

## 🎉 Expected Output

**Sensor Terminal:**
```
🌡️ Temperature sensor started!
Published: 23.4°C
Published: 27.1°C
Published: 28.9°C
Published: 25.2°C
...
```

**Monitor Terminal:**
```
🖥️ Temperature monitor started!
Received: 23.4°C
Received: 27.1°C
Received: 28.9°C ⚠️ WARNING: High temperature!
Received: 25.2°C
...
```

## 🚀 Performance Test

Want to see the incredible speed? Let's measure latency!

```cpp
// latency_test.cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <chrono>
#include <vector>

using namespace irlab::shm;
using namespace std::chrono;

int main() {
    Publisher<int> pub("speed_test");
    Subscriber<int> sub("speed_test");
    
    std::vector<double> latencies;
    
    for (int i = 0; i < 1000; ++i) {
        auto start = high_resolution_clock::now();
        pub.publish(i);
        
        bool success;
        int data = sub.subscribe(&success);
        auto end = high_resolution_clock::now();
        
        if (success) {
            auto duration = duration_cast<microseconds>(end - start);
            latencies.push_back(duration.count());
        }
    }
    
    double avg_latency = 0;
    for (double lat : latencies) {
        avg_latency += lat;
    }
    avg_latency /= latencies.size();
    
    std::cout << "Average latency: " << avg_latency << " microseconds\n";
    std::cout << "🚀 That's " << (1000000.0 / avg_latency) << " messages per second!\n";
    
    return 0;
}
```

## 🎨 Next Steps

Congratulations! You've just experienced microsecond-level communication! 

### 🔥 Try More Advanced Features

**Python Integration:**
```python
import shm_pub_sub

# Publisher
pub = shm_pub_sub.Publisher("python_topic", 0, 3)
pub.publish(42)

# Subscriber
sub = shm_pub_sub.Subscriber("python_topic", 0)
data, success = sub.subscribe()
if success:
    print(f"Received: {data}")
```

### 📚 Learn More

- **[📝 Complete Tutorials](tutorials_en.md)** - Comprehensive guides for all features
- **[🔄 Pub/Sub Deep Dive](tutorials_shm_pub_sub_en.md)** - Master broadcast communication
- **[🐍 Python API](tutorials_python_en.md)** - Python development guide

### 💡 Real-World Examples

**Robot Control System:**
```cpp
// Ultra-fast robot joint control
Publisher<JointCommand> joint_pub("robot_joints");
JointCommand cmd = {.position = 1.57, .velocity = 0.5};
joint_pub.publish(cmd);
```

**Real-Time Image Processing:**
```cpp
// Stream processed images
Publisher<cv::Mat> image_pub("processed_images");
cv::Mat processed_image = process_camera_frame();
image_pub.publish(processed_image);
```

## 🆘 Troubleshooting

### Common Issues

**"Permission denied" errors:**
```bash
# Fix shared memory permissions
sudo chmod 666 /dev/shm/*
```

**"Address already in use":**
```bash
# Clean up shared memory
sudo rm -f /dev/shm/shm_*
```

**Compilation errors:**
```bash
# Make sure you have the right compiler flags
g++ -std=c++17 -pthread -I./include your_file.cpp
```

### Getting Help

- **[🐛 Troubleshooting Guide](troubleshooting_en.md)** - Detailed problem solving
- **[📋 API Reference](spec_en.md)** - Complete API documentation
- **[🤝 Community Support](reference_en.md)** - Get help from other users

---

**🎉 Congratulations!** You've mastered the basics of ultra-fast inter-process communication! Your applications will never be the same again! 🚀✨