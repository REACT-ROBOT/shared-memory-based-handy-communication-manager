# 🐛 Troubleshooting Guide
[English | [日本語](docs_jp/md_manual_troubleshooting_jp.html)]

## 🎯 Quick Problem Resolution

This guide helps you quickly identify and resolve common issues with the Shared Memory Communication Manager.

## 🚨 Common Issues & Solutions

### 1. 🔒 Permission and Access Issues

#### "Permission denied" when accessing shared memory
```bash
# Error message
terminate called after throwing an instance of 'std::runtime_error'
  what():  Failed to create shared memory: Permission denied
```

**🔧 Solutions:**
```bash
# Method 1: Fix shared memory permissions
sudo chmod 666 /dev/shm/*

# Method 2: Run with appropriate permissions
sudo ./your_program

# Method 3: Add user to shared memory group
sudo usermod -a -G shm $USER
newgrp shm
```

#### "Address already in use" error
```bash
# Error message
std::runtime_error: Shared memory segment already exists
```

**🔧 Solutions:**
```bash
# See what exists, and whether it is healthy
shm_tool list
shm_tool doctor

# Remove one topic, including every generation segment it owns
shm_tool remove <topic>
```

> Do not delete the files by hand. A topic is `/dev/shm/shm_<topic>` **plus** one
> `shm_<topic>#<gen>-<nonce>` per layout generation. Removing only the first leaves
> orphaned generations behind, which `shm_tool doctor` then reports as
> "root segment missing". `shm_tool remove` takes the whole set.

> `rm -f /dev/shm/*` destroys other applications' segments too — Chrome, PostgreSQL and
> systemd all live there. Never run it.

### 2. 🔌 Connection and Communication Issues

#### Publisher/Subscriber not communicating
```bash
# Symptoms
- Publisher sends data but Subscriber receives nothing
- subscribe() returns false consistently
- No error messages shown
```

**🔧 Diagnostic Steps:**
```cpp
// 1. Check topic names match exactly
Publisher<int> pub("sensor_data");    // Topic name
Subscriber<int> sub("sensor_data");   // Must match exactly

// 2. Verify data types match
Publisher<float> pub("data");         // float type
Subscriber<float> sub("data");        // Must be same type

// 3. Check shared memory existence
#include <sys/stat.h>
struct stat buffer;
if (stat("/dev/shm/shm_sensor_data", &buffer) == 0) {
    std::cout << "Shared memory exists\n";
} else {
    std::cout << "Shared memory not found\n";
}
```

**🔧 Solutions:**
```cpp
// Add error checking
bool success;
int data = sub.subscribe(&success);
if (!success) {
    std::cout << "Failed to receive data - check publisher\n";
}

// Use proper timing
std::this_thread::sleep_for(std::chrono::milliseconds(10));
```

### 3. 🧠 Memory and Performance Issues

#### Memory leaks detected
```bash
# Valgrind output
==12345== LEAK SUMMARY:
==12345==    definitely lost: 1,024 bytes in 1 blocks
==12345==    indirectly lost: 0 bytes in 0 blocks
```

**🔧 Solutions:**
```cpp
// 1. Use RAII properly
{
    Publisher<int> pub("topic");
    pub.publish(42);
    // Automatic cleanup when leaving scope
}

// 2. Don't use raw pointers
// ❌ Bad
Publisher<int>* pub = new Publisher<int>("topic");
// Never call delete, causes memory leak

// ✅ Good
Publisher<int> pub("topic");
// Automatic memory management
```

#### High CPU usage
```bash
# Symptoms
- CPU usage constantly high (>50%)
- System becomes unresponsive
- Excessive context switching
```

**🔧 Solutions:**
```cpp
// 1. Add proper delays in loops
while (true) {
    data, success = sub.subscribe();
    if (success) {
        process_data(data);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));  // Add delay
}

// 2. Use condition variables for blocking
// Instead of polling continuously
bool success;
do {
    data = sub.subscribe(&success);
    if (!success) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
} while (!success);
```

### 4. 📦 Compilation and Build Issues

#### Header not found
```bash
# Error message
fatal error: shm_pub_sub.hpp: No such file or directory
```

**🔧 Solutions:**
```bash
# 1. Check include paths
g++ -std=c++17 -I./shm_base/include -I./shm_pub_sub/include your_file.cpp

# 2. Copy headers to system location
sudo cp include/*.hpp /usr/local/include/

# 3. Use full path
#include "/full/path/to/shm_pub_sub.hpp"
```

#### Linker errors
```bash
# Error message
undefined reference to `irlab::shm::Publisher<int>::Publisher(std::string const&)'
```

**🔧 Solutions:**
```bash
# 1. Link pthread library
g++ -pthread your_file.cpp

# 2. Use correct C++ standard
g++ -std=c++17 your_file.cpp

# 3. Link rt library (if needed)
g++ -lrt your_file.cpp
```

### 5. 🐍 Python Binding Issues

#### Import errors
```python
# Error message
ImportError: No module named 'shm_pub_sub'
```

**🔧 Solutions:**
```bash
# 1. Check Python path
export PYTHONPATH=$PYTHONPATH:/path/to/shm/python/bindings

# 2. Install with pip
pip install ./python_bindings

# 3. Build Python module
cd python_bindings
python setup.py build
python setup.py install
```

#### Type conversion errors
```python
# Error message
TypeError: No to_python (by-value) converter found
```

**🔧 Solutions:**
```python
# 1. Use correct data types
pub = shm_pub_sub.Publisher("topic", 0, 3)    # int default
pub.publish(42)  # Use int, not float

# 2. Explicit type conversion
pub.publish(int(42.0))  # Convert float to int
```

### 6. 🔧 Runtime and Logic Issues

#### Data corruption or unexpected values
```bash
# Symptoms
- Received data is random garbage
- Values change unexpectedly
- Type casting errors
```

**🔧 Solutions:**
```cpp
// 1. Ensure type consistency
struct SensorData {
    float temperature;
    int timestamp;
};

Publisher<SensorData> pub("sensor");
Subscriber<SensorData> sub("sensor");  // Same struct

// 2. Check data alignment
struct alignas(8) AlignedData {
    double value;
    int32_t timestamp;
};

// 3. Validate received data
bool success;
SensorData data = sub.subscribe(&success);
if (success && data.temperature > -100 && data.temperature < 200) {
    // Data seems valid
    process_data(data);
}
```

#### Race conditions
```bash
# Symptoms
- Intermittent crashes
- Inconsistent behavior
- Segmentation faults
```

**🔧 Solutions:**
```cpp
// 1. Use proper synchronization
std::mutex data_mutex;
std::lock_guard<std::mutex> lock(data_mutex);

// 2. Initialize objects in correct order
Publisher<int> pub("topic");  // Create publisher first
std::this_thread::sleep_for(std::chrono::milliseconds(10));
Subscriber<int> sub("topic"); // Then subscriber

// 3. Use thread-safe operations
std::atomic<bool> running{true};
while (running.load()) {
    // Safe loop
}
```

## 🩺 Shared Memory Segments

### Run `shm_tool doctor` first

Before reading any code, run this. It reads the header of every segment in `/dev/shm`
and reports whether it is usable.

```bash
shm_tool doctor            # everything
shm_tool doctor lidar_scan # one topic
```

Anything that needs action is marked ★ and makes the exit code 1.

| Report | Meaning | What to do |
|---|---|---|
| ★ABI n (this build is m) | a segment of an older format is still there | `shm_tool remove <topic>`, then restart every process on the topic |
| ★uninitialised | remains of a creator that died | same |
| ★created before the last reboot | `/dev/shm` survived a reboot | same |
| ★stale generation | a cutover was not cleaned up | same (harmless if left) |
| ★name and header disagree | copied or renamed by hand | same |
| format not declared (note) | `SHM_DECLARE_LAYOUT` not added yet | works as is; adding it also catches a same-size reordering |

### "ABI major version mismatch" on publish / subscribe

Segments outlive the processes that made them, so one from a previous release is still
around.

```
shm::Publisher: root segment is not usable: ABI major version mismatch
(segment 3, this build 4) [topic 'lidar_scan']. Remove the segment with
'shm_tool remove lidar_scan' and restart every process on this topic
```

Do exactly that, and **restart every process on that topic together**. Swapping only
some of them lets an old process recreate the segment in the old format. A reboot also
clears `/dev/shm`.

### "the segment was created by a build that did not declare this payload's format"

This appears after adding `SHM_DECLARE_LAYOUT` to a type: a segment created before the
declaration is still there, so its layout cannot be verified. Remove it once.

### A time-aligned read says there is no data, or returns something old

History depth is `buf_num ÷ publish rate`. A 10 Hz sensor with `buf_num = 3` keeps only
**300 ms**, so a consumer running at 1 Hz cannot reach an odometry sample from a second
ago. The retention column of `shm_tool doctor` shows what is actually held; raise the
publisher's `buffer_num` if it is too short.

Note that `SearchPolicy::Nearest` always returns the closest existing sample, however far
away it is, so it never reports `TooOld`. Pass `max_skew_us` to `subscribeAlignedTo()` to
bound the skew.

## 🔍 Debugging Tools and Techniques

### 1. 📊 Memory Inspection
```bash
# Check shared memory usage
ls -la /dev/shm/
df -h /dev/shm/

# Monitor memory usage
watch -n 1 'ls -la /dev/shm/ | grep shm_'
```

### 2. 🔬 Process Monitoring
```bash
# Monitor CPU usage
top -p $(pgrep your_program)

# Check file descriptors
lsof -p $(pgrep your_program)

# System calls tracing
strace -p $(pgrep your_program)
```

### 3. 🧰 Debug Build
```bash
# Compile with debug info
g++ -g -O0 -DDEBUG your_file.cpp

# Run with GDB
gdb ./your_program
(gdb) run
(gdb) bt  # Backtrace on crash
```

### 4. 📝 Logging and Diagnostics
```cpp
// Add debug output
#ifdef DEBUG
    std::cout << "Publisher created for topic: " << topic_name << std::endl;
    std::cout << "Shared memory size: " << shm_size << std::endl;
#endif

// Performance measurement
auto start = std::chrono::high_resolution_clock::now();
pub.publish(data);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
std::cout << "Publish time: " << duration.count() << " μs" << std::endl;
```

## 🆘 Emergency Procedures

### System Cleanup
```bash
#!/bin/bash
# emergency_cleanup.sh
echo "Stopping the processes first — a live publisher recreates its topic..."
pkill -f your_program_name
echo "Removing this application's topics..."
for topic in $(shm_tool list | awk 'NR>1 {print $NF}' | cut -d'#' -f1 | sort -u); do
  shm_tool remove "$topic"
done
shm_tool doctor
echo "Cleanup complete!"
```

### Recovery Steps
```bash
# 1. Stop all processes
pkill -f your_program_name

# 2. Clean shared memory
shm_tool remove <topic>     # per topic; see what is there with shm_tool list

# 3. If /dev/shm is full, raise the tmpfs size — NOT kernel.shm* .
#    kernel.shmmax and friends govern System V shared memory, which this
#    library never uses. POSIX segments are limited by the /dev/shm mount.
df -h /dev/shm
sudo mount -o remount,size=2G /dev/shm

# 4. Restart processes
./your_program
```

## 📞 Getting Help

### 🔍 Self-Diagnosis Checklist
- [ ] Are topic names exactly matching?
- [ ] Are data types identical?
- [ ] Is shared memory accessible?
- [ ] Are permissions correct?
- [ ] Is there enough memory?
- [ ] Are both processes running?

### 📋 Report Template
When reporting issues, include:
```
**Environment:**
- OS: Ubuntu 20.04
- Compiler: GCC 9.3.0
- Library version: 

**Problem:**
- What you were trying to do
- What happened instead
- Error messages (full text)

**Code:**
- Minimal reproducing example
- Compilation command used

**System Info:**
- ls -la /dev/shm/
- free -h
- ulimit -a
```

### 🤝 Community Support
- **GitHub Issues**: Report bugs and get help
- **Documentation**: [Complete API Reference](spec_en.md)
- **Examples**: [Sample Code Collection](examples_en.md)

---

**💡 Pro Tip**: Most issues are solved by checking topic names, data types, and shared memory permissions. Start with these basics before diving deeper! 🚀