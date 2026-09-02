# Shared Memory Based Communication Manager {#mainpage}
[English | [日本語](docs_jp/index.html)]

## Overview

**Shared Memory Based Communication Manager** is a C++ library for achieving ultra-fast inter-process communication within the same PC.

### 🧠 Shared Memory-Based Communication Libraries
- **shm_pub_sub** - High-speed Publisher/Subscriber model communication (broadcast type)

> **Note**: Earlier versions also shipped `shm_service` (request-response) and
> `shm_action` (asynchronous task management). They had no users and their design
> did not handle pthread objects in shared memory safely, so they were removed in
> v2.0.0.

## 📚 Documentation Index

### Beginner's Guide
- [📖 Introduction - Communication Library Fundamentals](introduction_en.md)
- [🚀 Quick Start Guide](quickstart_en.md)
- [⚙️ Installation and Configuration](quickstart_en.md#-installation-2-minutes)

### Tutorials
- [📝 Basic Tutorials (C++)](tutorials_en.md)
  - [🔄 How to use Pub/Sub Communication](tutorials_shm_pub_sub_en.md)
- [🐍 Python Tutorials](tutorials_python_en.md)
  - [🔄 Python Pub/Sub Communication](tutorials_shm_pub_sub_python_en.md)

### Detailed Specifications
- [📋 API Specifications](spec_en.md)
- [🔧 Build options and operations](spec_en.md)
- [🐛 Troubleshooting](troubleshooting_en.md)

### References
- [📚 References](reference_en.md)
- [💡 Sample code](../shm_pub_sub/samples/) - in the repository

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

## 🎨 How to Choose Communication Methods

| Use Case | Recommended Library | Features | Applications |
|----------|---------------------|----------|-------------|
| **Real-time data distribution** | shm_pub_sub | ⚡Maximum speed<br>📡Broadcast<br>🔄Continuous data | Sensor data distribution<br>Image streaming<br>Robot control signals |

If you need request-response exchanges or long-running task management, build them
in a higher layer (for example ROS 2 services and actions) or implement the protocol
you need on top of Pub/Sub.

## 📊 Performance Comparison

| Metric | shm_pub_sub |
|--------|-------------|
| **Latency** | ~1μs |
| **Throughput** | Very High |
| **CPU Usage** | Minimal |
| **Memory Usage** | Minimal |

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
