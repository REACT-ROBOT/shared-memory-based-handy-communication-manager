# 📖 Introduction - Communication Library Fundamentals
[English | [日本語](docs_jp/md_manual_introduction_jp.html)]

## 🌟 Welcome to the World of Inter-Process Communication!

**Shared Memory Based Handy Communication Manager** is a comprehensive library collection that makes inter-process communication easy to implement. With this library, you can achieve complex communication processing with just a few lines of code.

## 🤔 What is Inter-Process Communication?

### Understanding Through Familiar Examples

Inter-process communication means **different programs exchanging information with each other**.

```
📱 Smartphone Example
┌─────────────────┐    Data    ┌─────────────────┐
│   Camera App    │ ────────────▶ │ Photo Edit App  │
│ (Capture Data)  │              │ (Processing)    │
└─────────────────┘              └─────────────────┘

🏭 Factory Example  
┌─────────────────┐  Sensor Data ┌─────────────────┐
│ Sensor Monitor  │ ────────────▶ │ Control System  │
│   Program       │              │   Program       │
└─────────────────┘              └─────────────────┘
```

### Why is Inter-Process Communication Necessary?

**🔹 Efficiency Through Division of Labor**
- Each program focuses on specialized processing
- Development, maintenance, and testing become easier

**🔹 Improved Stability**  
- One program crashing doesn't affect others
- Partial updates and restarts are possible

**🔹 Scalability**
- Adjust number of processes according to processing capacity
- Distributed processing across multiple computers

## 🧠 Problems This Library Solves

### Traditional Challenges
```cpp
// ❌ Traditional method: Complex and dangerous
void* shared_mem = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
int* data = reinterpret_cast<int*>(shared_mem);  // Dangerous type conversion
pthread_mutex_lock(&mutex);                      // Manual mutual exclusion
*data = 42;
pthread_mutex_unlock(&mutex);
munmap(shared_mem, size);                        // Manual memory management
```

### Our Library's Solution
```cpp
// ✅ Our library: Simple and safe
Publisher<int> pub("my_topic");
pub.publish(42);  // That's all!
```

## 🚀 Features of Three Communication Methods

**Use Case**: Real-time communication within the same PC

| Library | Features | Applications |
|---------|----------|-------------|
| **shm_pub_sub** | 📡 Publisher/Subscriber model<br>⚡ Microsecond-level ultra-low latency | Robot control, real-time image processing |
| **shm_service** | 🤝 Request/Response model<br>🔒 Guaranteed send/receive reliability | Database operations, file processing |
| **shm_action** | ⚡ Asynchronous processing model<br>📊 Progress monitoring & cancel functionality | Long computations, file downloads |

```cpp
// Example: Robot sensor data distribution
Publisher<SensorData> sensor_pub("robot_sensors");
sensor_pub.publish(sensor_reading);  // Ultra-fast distribution
```

## 🎯 Which Communication Method Should You Choose?

### 📊 Communication Method Selection Flowchart

```
What is your use case?
│
├─ Need maximum speed (microseconds)
│  └─ 📡 shm_pub_sub (Pub/Sub)
│
├─ Want reliable data send/receive
│  └─ 🤝 shm_service (Service)
│
└─ Want to monitor time-consuming processes
   └─ ⚡ shm_action (Action)
```

### 🔍 Detailed Comparison Table

| Feature | shm_pub_sub | shm_service | shm_action |
|---------|-------------|-------------|------------|
| **Communication Range** | Same PC | Same PC | Same PC |
| **Speed** | ⚡⚡⚡ Fastest | ⚡⚡ Fast | ⚡⚡ Fast |
| **Reliability** | 📦 Best Effort | 🔒 Guaranteed | 🔒 Guaranteed |
| **Communication Pattern** | 1:N (Broadcast) | 1:1 (Request-Response) | 1:1 (Asynchronous) |
| **Data Size** | Any | Any | Any |
| **Setup Simplicity** | 🟢 Very Easy | 🟢 Very Easy | 🟡 Easy |

## 🛠️ Development History and Design Philosophy

### 🏛️ Library Lineage

This library is based on a C library created by **Professor Koichi Ozaki** for robot development in his laboratory.

**Evolution Process**:
```
🕰️ Initial version (C language)
   ↓ Feature additions and improvements
🔧 C++ version (Object-oriented)
```

### 🎯 Design Philosophy

**1. 🎛️ Simple API**
```cpp
// Data transmission (publish) example
Publisher<int> pub("topic");
pub.publish(42);
```

**2. 🔒 Safety**
- Type-safe template design
- Automatic memory management
- Proper error handling through exceptions

**3. 🚀 Performance**
- Zero-copy design (shared memory)
- Efficient data structures (ring buffers)
- Minimal overhead

**4. 🔧 Extensibility and Compatibility**
- **ROS-compatible API**: Intuitive design based on ROS concepts
- **Custom data types**: Support for arbitrary C++ structures
- **Python bindings**: Same API for C++ and Python
- **Platform support**: Linux, Windows (WSL) support
- **Compiler support**: GCC, Clang, MSVC support

## 🎓 API Design Features

### Organization by Namespaces

```cpp
// Shared memory communication
namespace irlab::shm {
    Publisher<T>, Subscriber<T>         // Pub/Sub model
    ServiceClient<T>, ServiceServer<T>  // Service model  
    ActionClient<T>, ActionServer<T>    // Action model
}

// Common base functionality
namespace irlab::shm_base {
    // Shared memory foundation features (memory mapping, mutual exclusion, etc.)
}
```

### Consistent Design Patterns

**🔹 Sender side**: Naming pattern for data senders
- `Publisher` (publishes data)
- `ServiceClient` (requests service)
- `ActionClient` (delegates action)

**🔹 Receiver side**: Naming pattern for data receivers  
- `Subscriber` (subscribes to data)
- `ServiceServer` (provides service)
- `ActionServer` (executes action)

### Automatic Resource Management

```cpp
{
    Publisher<int> pub("topic");  // ← Automatically allocates memory
    pub.publish(42);
    // ← Resources automatically released when leaving scope
}
// No memory leaks!
```

## 🎁 Referenced Systems

### 1. fuRom [1]
- Low-latency communication through shared memory
- Efficient ring buffer implementation

### 2. ROS (Robot Operating System) [2]
- Communication patterns of Pub/Sub, Service, Action
- Topic-based namespace management

### 3. Modern C++ Design
- Resource management through RAII
- Type safety through templates
- Proper error handling through exceptions

## 🚀 Next Steps

Do you understand the basic concepts? Now let's get hands-on experience!

### 🏃 For Those Who Want to Try Right Now
- **[🚀 Quick Start Guide](quickstart_en.md)** - Experience communication in 5 minutes

### 📚 For Those Who Want to Learn Thoroughly  
- **[📝 Basic Tutorials](tutorials_en.md)** - Learn features step by step

### 🔧 For Those Who Want to Know Specific Features
- **[🔄 Pub/Sub Communication](tutorials_shm_pub_sub_en.md)** - High-speed broadcast
- **[🤝 Service Communication](tutorials_shm_service_en.md)** - Reliable request-response
- **[⚡ Action Communication](tutorials_shm_action_en.md)** - Asynchronous process management

### 🐍 For Python Developers
- **[🐍 Python Basics](tutorials_python_en.md)** - Python API fundamentals
- **[🔄 Python Pub/Sub](tutorials_shm_pub_sub_python_en.md)** - Python version publisher/subscriber

---

## 📚 References

[1] Irie, Kiyoshi. "ROS との相互運用性に配慮した共有メモリによる低遅延プロセス間通信フレームワーク." 第 35 回日本ロボット学会学術講演会予稿集, RSJ2017AC2B2-01 (2017).
    <https://furo.org/irie/papers/rsj2017_irie.pdf>

[2] Open Robotics, "ROS.org", <http://wiki.ros.org/>

---

**Are you ready?** Let's master inter-process communication! 🚀✨