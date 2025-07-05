# 📚 References and Resources
[English | [日本語](docs_jp/md_manual_reference_jp.html)]

## 🔬 Academic Papers & Research Materials

### [1] Shared Memory Communication Framework
**Irie, Kiyoshi**. "ROS との相互運用性に配慮した共有メモリによる低遅延プロセス間通信フレームワーク." 第 35 回日本ロボット学会学術講演会予稿集, RSJ2017AC2B2-01 (2017).
- **URL**: <https://furo.org/irie/papers/rsj2017_irie.pdf>
- **Overview**: Detailed description of this library's design philosophy and ROS compatibility

### [2] POSIX Shared Memory Specification
**Linux man-pages project**. "shm_overview - overview of POSIX shared memory"
- **URL**: <https://man7.org/linux/man-pages/man7/shm_overview.7.html>
- **Overview**: Basic specifications and system calls for shared memory

## 🤖 Related Frameworks

### [3] ROS (Robot Operating System)
**Open Robotics**. "ROS.org - Robot Operating System"
- **URL**: <http://wiki.ros.org/>
- **Overview**: Reference source for communication patterns (Pub/Sub, Service, Action) used in this library

### [4] Communication Performance Research
**Ozaki, Koichi**. "Performance Evaluation of Inter-Process Communication and Applications to Robot Control"
- **Affiliation**: Doshisha University, Faculty of Science and Engineering
- **Overview**: Shared memory communication benchmarks and real-time performance evaluation

## 🔧 Technical Specifications & Standards

### [5] C++20 Standard Library
**ISO/IEC 14882:2020**. "Programming languages — C++"
- **URL**: <https://isocpp.org/std/the-standard>
- **Overview**: C++ templates and memory management used in this library

### [6] Boost.Python Documentation
**Boost C++ Libraries**. "Boost.Python"
- **URL**: <https://www.boost.org/doc/libs/1_80_0/libs/python/doc/html/index.html>
- **Overview**: C++ integration with Python bindings

### [7] pthread Specification
**POSIX.1-2008**. "IEEE Std 1003.1-2008 - POSIX Threads"
- **URL**: <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/pthread.h.html>
- **Overview**: Mutual exclusion in multi-threaded environments

## 📖 Related Books

### [8] Concurrent Programming
**Anthony Williams**. "C++ Concurrency in Action: Practical Multithreading"
- **Publisher**: Manning Publications
- **ISBN**: 978-1617294693
- **Overview**: Concurrent programming and thread safety in C++

### [9] Systems Programming
**Michael Kerrisk**. "The Linux Programming Interface"
- **Publisher**: No Starch Press
- **ISBN**: 978-1593272203
- **Overview**: Linux systems programming and IPC details

### [10] Real-Time Systems
**Giorgio Buttazzo**. "Hard Real-Time Computing Systems"
- **Publisher**: Springer
- **ISBN**: 978-1461406754
- **Overview**: Real-time system design and performance evaluation

## 🌐 Online Resources

### [11] Shared Memory Optimization Techniques
**Intel Developer Zone**. "Optimizing Shared Memory Performance"
- **URL**: <https://software.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/optimization-and-programming-guide/shared-memory-parallel-programming.html>
- **Overview**: CPU cache efficiency and memory access optimization

### [12] Inter-Process Communication Comparison
**Red Hat Developer**. "Inter-Process Communication in Linux"
- **URL**: <https://developers.redhat.com/articles/2023/05/10/inter-process-communication-linux>
- **Overview**: Performance comparison and use cases for various IPC methods

### [13] Real-Time Communication Architecture
**Real-Time eXecutive Interface (RTXI)**. "Real-Time Communication Patterns"
- **URL**: <http://rtxi.org/docs/tutorials/real-time-communication/>
- **Overview**: Communication pattern design in real-time systems

## 🛠️ Development Tools & Libraries

### [14] Google Test Framework
**Google**. "GoogleTest - Google Testing and Mocking Framework"
- **URL**: <https://github.com/google/googletest>
- **Overview**: Used for unit testing in this library

### [15] Valgrind Memory Analysis
**Valgrind Developers**. "Valgrind - Dynamic Analysis Tool"
- **URL**: <https://valgrind.org/>
- **Overview**: Memory leak detection and profiling

### [16] perf Performance Analysis
**Linux kernel**. "perf - Linux profiling with performance counters"
- **URL**: <https://perf.wiki.kernel.org/index.php/Main_Page>
- **Overview**: Communication performance measurement and bottleneck analysis

## 🎓 Educational Resources

### [17] Inter-Process Communication Tutorial
**GeeksforGeeks**. "Inter Process Communication (IPC)"
- **URL**: <https://www.geeksforgeeks.org/inter-process-communication-ipc/>
- **Overview**: Basic IPC concepts and implementation examples

### [18] Shared Memory Programming Introduction
**LinuxHint**. "Shared Memory in Linux"
- **URL**: <https://linuxhint.com/shared-memory-linux/>
- **Overview**: Shared memory implementation in Linux environments

## 🏆 Benchmarks & Performance Evaluation

### [19] Communication Library Performance Comparison
**Ozaki, K., et al.** "Performance Evaluation of Inter-Process Communication Libraries for Robotic Applications"
- **Conference**: IEEE International Conference on Robotics and Automation (ICRA)
- **Overview**: Quantitative performance comparison of various IPC methods

### [20] Real-Time Communication Latency Measurement
**Real-Time Systems Laboratory**. "Latency Measurement in Real-Time Communication"
- **URL**: <https://www.real-time.org/latency-measurement/>
- **Overview**: Microsecond-level communication delay measurement techniques

---

## 🔍 Related Terms & Keywords

**Inter-Process Communication (IPC)**
- Shared Memory, Message Queue, Pipe, Socket
- POSIX IPC, System V IPC

**Concurrent Programming**
- Thread Safety, Race Condition, Deadlock
- Mutex, Semaphore, Atomic Operations

**Real-Time Systems**
- Hard Real-Time, Soft Real-Time
- Jitter, Latency, Throughput

**C++ Technology**
- Template Programming, RAII, Smart Pointers
- Memory Management, Exception Safety

**Robotics**
- ROS, Robot Control, Sensor Fusion
- Real-Time Processing, Distributed Systems

---

**💡 For Further Learning**: Gain deep understanding from these references and design more advanced inter-process communication systems!