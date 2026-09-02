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

```bash
git clone <repository-url>
cd shared-memory-based-handy-communication-manager

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j$(nproc)
cmake --install build
```

This installs five things. **The shared libraries carry no `lib` prefix** (the build sets
`PREFIX ""`), so `-lshm_pub_sub` will not link.

```
$HOME/.local/lib/shm_base.so
$HOME/.local/lib/shm_pub_sub.so
$HOME/.local/include/shm_base.hpp, shm_pub_sub.hpp, shm_pub_sub_vector.hpp
$HOME/.local/bin/shm_tool
```

> There is no header-only mode. `shm_base.so` holds the segment, ring-buffer and
> generation logic, and `shm_pub_sub.so` the Python binding; copying the headers alone
> will not build.

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

**From CMake (recommended)**

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app CXX)
set(CMAKE_CXX_STANDARD 17)

# Find shm_base FIRST. The shm_pub_sub export refers to the target `shm_base`
# by name and carries no find_dependency(), so the reverse order fails.
find_package(shm_base    REQUIRED)
find_package(shm_pub_sub REQUIRED)

add_executable(sensor sensor.cpp)
target_link_libraries(sensor shm_pub_sub)   # include paths come along
```

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/.local
cmake --build build
```

**From g++ directly**

Link with the `-l:` form, because there is no `lib` prefix.

```bash
SHM=$HOME/.local
g++ -std=c++17 -I$SHM/include sensor.cpp \
    -L$SHM/lib -l:shm_pub_sub.so -l:shm_base.so -lrt -pthread -o sensor
g++ -std=c++17 -I$SHM/include monitor.cpp \
    -L$SHM/lib -l:shm_pub_sub.so -l:shm_base.so -lrt -pthread -o monitor
```

(Against an uninstalled build tree, use
`-I<repo>/shm_base/include -I<repo>/shm_pub_sub/include -L<repo>/build/lib`.)

**Run, in separate terminals**

```bash
export LD_LIBRARY_PATH=$HOME/.local/lib   # in both terminals

# Terminal 1:
./sensor
# Terminal 2:
./monitor
```

> `shm_pub_sub.so` records `shm_base.so` as a **bare** `DT_NEEDED` name and has no
> `RUNPATH`, so whichever copy comes first on `LD_LIBRARY_PATH` wins. A leftover build
> tree on that path silently supplies a stale `shm_base.so`. Check with
> `ldd ./monitor | grep shm`.

> `subscribe()` is **not a queue**. It returns the newest sample and keeps returning the
> same one until a newer one arrives (or it expires — 2 seconds by default, adjustable
> with `setDataExpiryTime_us()`). Polling faster than the publisher does not lose data,
> but it does repeat it.

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
shm_tool list            # see what exists
shm_tool doctor          # see whether it is healthy
shm_tool remove <topic>  # remove one topic and all of its generations
```

**Compilation errors:**
```bash
# Both include paths are needed, and the -l: form for the libraries
g++ -std=c++17 -I$HOME/.local/include your_file.cpp \
    -L$HOME/.local/lib -l:shm_pub_sub.so -l:shm_base.so -lrt -pthread
```

### Getting Help

- **[🐛 Troubleshooting Guide](troubleshooting_en.md)** - Detailed problem solving
- **[📋 API Reference](spec_en.md)** - Complete API documentation
- **[🤝 Community Support](reference_en.md)** - Get help from other users

---

**🎉 Congratulations!** You've mastered the basics of ultra-fast inter-process communication! Your applications will never be the same again! 🚀✨