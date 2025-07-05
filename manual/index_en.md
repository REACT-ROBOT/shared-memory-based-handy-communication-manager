# Shared Memory Based Communication Manager {#mainpage}
[English | [日本語](docs_jp/index.html)]

## Overview

**Shared Memory Based Communication Manager** is a comprehensive C++ library collection for achieving ultra-fast inter-process communication within the same PC. This library consists of three main components:

### 🧠 Shared Memory-Based Communication Libraries
- **shm_pub_sub** - High-speed Publisher/Subscriber model communication (broadcast type)
- **shm_service** - Reliable Server/Client model communication (request-response type)
- **shm_action** - Advanced asynchronous processing communication (long-running task support)

## 📚 Documentation Index

### Beginner's Guide
- [📖 Introduction - Communication Library Fundamentals](introduction_en.md)
- [🚀 Quick Start Guide](quickstart_en.md)
- [⚙️ Installation and Configuration](installation_en.md)

### Tutorials
- [📝 Basic Tutorials (C++)](tutorials_en.md)
  - [🔄 How to use Pub/Sub Communication](tutorials_shm_pub_sub_en.md)
  - [🤝 How to use Service Communication](tutorials_shm_service_en.md)
  - [⚡ How to use Action Communication](tutorials_shm_action_en.md)
- [🐍 Python Tutorials](tutorials_python_en.md)
  - [🔄 Python Pub/Sub Communication](tutorials_shm_pub_sub_python_en.md)

### Detailed Specifications
- [📋 API Specifications](spec_en.md)
- [🔧 Advanced Configuration and Applications](advanced_en.md)
- [🐛 Troubleshooting](troubleshooting_en.md)

### References
- [📚 References](reference_en.md)
- [💡 Sample Code Collection](examples_en.md)

## 🎯 Features

### 🚀 Exceptional Performance
- ⚡ **Microsecond-level ultra-low latency** - Maximum speed through direct memory access
- 🎯 **Zero-copy communication** - Efficient transfer with minimal data copying
- 🔥 **CPU cache optimization** - Design considering memory layout

### 🔒 Safety and Reliability
- 🛡️ **Thread-safe** - Automatic mutual exclusion and deadlock avoidance
- 🔐 **Type safety** - Compile-time type checking with C++ templates
- 🚨 **Exception safety** - Reliable resource management through RAII design
- ✅ **Data integrity** - Corruption prevention through atomic operations

### 🎛️ Ease of Use
- 🎨 **Intuitive API** - ROS-like easy-to-understand interface
- 📦 **Automatic memory management** - Smart pointer design preventing memory leaks
- 🔧 **Easy setup** - No complex configuration, immediate use
- 🐍 **Multi-language support** - Same API for C++ and Python

## 🏃 Quick Start

### 1. Simple Pub/Sub Communication (Shared Memory)
```cpp
#include "shm_pub_sub.hpp"
using namespace irlab::shm;

// Publisher
Publisher<int> pub("my_topic");
pub.publish(42);

// Subscriber
Subscriber<int> sub("my_topic");
bool state;
int data = sub.subscribe(&state);
if (state) {
    std::cout << "Received data: " << data << std::endl;
}
```

### 2. Simple Service Communication (Request-Response)
```cpp
#include "shm_service.hpp"
using namespace irlab::shm;

// Server
ServiceServer<int, int> server("calc_service");
if (server.hasRequest()) {
    int request = server.getRequest();
    int response = request * 2;  // Return double
    server.sendResponse(response);
}

// Client
ServiceClient<int, int> client("calc_service");
client.sendRequest(21);
if (client.waitForResponse(1000000)) {  // Wait 1 second
    int result = client.getResponse();
    std::cout << "Calculation result: " << result << std::endl;  // 42
}
```

## 🎨 How to Choose Communication Methods

| Use Case | Recommended Library | Features | Applications |
|----------|---------------------|----------|-------------|
| **Real-time data distribution** | shm_pub_sub | ⚡Maximum speed<br>📡Broadcast<br>🔄Continuous data | Sensor data distribution<br>Image streaming<br>Robot control signals |
| **Reliable data exchange** | shm_service | 🤝Request-response guarantee<br>⏰Timeout support<br>🛡️Error handling | Database operations<br>Configuration retrieval<br>Calculation results |
| **Long-running asynchronous processing** | shm_action | ⚡Asynchronous execution<br>📊Progress monitoring<br>❌Cancel functionality | File processing<br>Machine learning training<br>Large data conversion |

## 📊 Performance Comparison

| Metric | shm_pub_sub | shm_service | shm_action |
|--------|-------------|-------------|------------|
| **Latency** | ~1μs | ~2-5μs | ~2-10μs |
| **Throughput** | Very High | High | Medium |
| **CPU Usage** | Minimal | Low | Medium |
| **Memory Usage** | Minimal | Low | Medium |

## 📞 Support

- **🆎 Open Source**: Contributions welcome
- **👥 Community Support**: User mutual assistance
- **🐛 Bug Reports**: Report via Issue tracker

## 📄 License

**Apache License 2.0** 🆎

Copyright 2024 Shared Memory Communication Contributors

This software is provided as open source under the Apache License 2.0. Commercial use, modification, and redistribution are permitted.

### 🛡️ License Features
- ✅ **Commercial use allowed**: Free use in commercial projects
- ✅ **Modification allowed**: Source code modification and extension possible
- ✅ **Redistribution allowed**: Redistribution possible with license notice
- ✅ **Patent protection**: Contributors' patent rights protected

Please see the [LICENSE file](../LICENSE) for details.

---

**Next Step**: Learn the basic concepts in [📖 Introduction](introduction_en.md)!
