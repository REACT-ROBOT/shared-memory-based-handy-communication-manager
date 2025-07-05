# 🐍 Python Tutorials - Master Inter-Process Communication with Python
[English | [日本語](docs_jp/md_manual_tutorials_python_jp.html)]

## 🌟 Overview

The Shared Memory Based Handy Communication Manager (SHM) is a library that provides high-speed inter-process communication using shared memory. This library enables topic-based communication using the Publisher/Subscriber model with seamless Python integration.

## ✨ Key Features

- **⚡ High-Speed Communication**: Achieves low-latency and high-throughput communication using shared memory
- **🔄 Ring Buffer Architecture**: Utilizes multiple buffers to minimize read/write conflicts
- **🛡️ Type Safety**: Provides type-safe communication through template-based design
- **🐍 Python Support**: Full Python bindings using Boost.Python for seamless C++/Python integration

## 📊 Supported Data Types

The library currently supports the following data types:

- `bool` - Boolean values
- `int` - Integer values  
- `float` - Floating-point values

## 🔧 Python API Reference

### 📤 Publisher (Data Sender)

The Publisher class writes data to shared memory for broadcast communication.

#### Constructor

```python
# Boolean Publisher
pub_bool = shm_pub_sub.Publisher(name: str, default_value: bool, buffer_num: int)

# Integer Publisher  
pub_int = shm_pub_sub.Publisher(name: str, default_value: int, buffer_num: int)

# Float Publisher
pub_float = shm_pub_sub.Publisher(name: str, default_value: float, buffer_num: int)
```

**Parameters:**
- `name`: Shared memory name (string identifier)
- `default_value`: Default value (bool/int/float depending on type)
- `buffer_num`: Number of buffers (default: 3)

#### Methods

##### publish(data)

Writes data to shared memory for immediate broadcast to all subscribers.

```python
# Boolean data publishing
pub_bool.publish(True)

# Integer data publishing  
pub_int.publish(42)

# Float data publishing
pub_float.publish(3.14159)
```

**Example: Sensor Data Publisher**
```python
import shm_pub_sub
import time
import random

# Create temperature sensor publisher
temp_pub = shm_pub_sub.Publisher("temperature_sensor", 0.0, 5)

# Publish sensor readings
for i in range(100):
    temperature = 20.0 + random.uniform(-5.0, 15.0)
    temp_pub.publish(temperature)
    print(f"Published temperature: {temperature:.2f}°C")
    time.sleep(0.1)  # 10Hz publishing rate
```

### 📥 Subscriber (Data Receiver)

The Subscriber class reads data from shared memory published by Publishers.

#### Constructor

```python
# Boolean Subscriber
sub_bool = shm_pub_sub.Subscriber(name: str, default_value: bool)

# Integer Subscriber
sub_int = shm_pub_sub.Subscriber(name: str, default_value: int)

# Float Subscriber  
sub_float = shm_pub_sub.Subscriber(name: str, default_value: float)
```

**Parameters:**
- `name`: Shared memory name (string identifier, must match Publisher)
- `default_value`: Default value (bool/int/float depending on type)

#### Methods

##### subscribe()

Reads data from shared memory with success indication.

```python
# Boolean data subscription
data, is_success = sub_bool.subscribe()

# Integer data subscription
data, is_success = sub_int.subscribe()

# Float data subscription
data, is_success = sub_float.subscribe()
```

**Return Values:**
- `data`: Retrieved data (bool/int/float)
- `is_success`: Success flag (bool) - True if new data was available

**Example: Multi-Subscriber System**
```python
import shm_pub_sub
import time
import threading

def temperature_monitor():
    """Monitor temperature and alert on extremes"""
    temp_sub = shm_pub_sub.Subscriber("temperature_sensor", 0.0)
    
    while True:
        temp, success = temp_sub.subscribe()
        if success:
            if temp > 30.0:
                print(f"🔥 HIGH TEMPERATURE ALERT: {temp:.2f}°C")
            elif temp < 10.0:
                print(f"🧊 LOW TEMPERATURE ALERT: {temp:.2f}°C")
        time.sleep(0.05)

def data_logger():
    """Log all temperature readings"""
    temp_sub = shm_pub_sub.Subscriber("temperature_sensor", 0.0)
    
    while True:
        temp, success = temp_sub.subscribe()
        if success:
            timestamp = time.time()
            print(f"📊 LOG: [{timestamp:.3f}] Temperature: {temp:.2f}°C")
        time.sleep(0.1)

# Start monitoring threads
monitor_thread = threading.Thread(target=temperature_monitor)
logger_thread = threading.Thread(target=data_logger)

monitor_thread.start()
logger_thread.start()
```

## 🏗️ Build and Installation

### 📋 Prerequisites

- **CMake** - Build system
- **Boost.Python** - Python bindings library
- **Python 3.6+** - Python interpreter
- **C++ Compiler** - GCC 7.0+ or equivalent

### 🔨 Build Instructions

```bash
# Navigate to the CMake directory
cd shm_ws/src/shared-memory-based-handy-communication-manager

# Create build directory
mkdir build && cd build

# Run CMake configuration
cmake ..

# Build the project
make

# Install Python package
cd ../shm_pub_sub/scripts
python setup.py install
```

**Alternative Installation (Development Mode):**
```bash
# Install in development mode for easier debugging
python setup.py develop
```

## 🚨 Limitations and Considerations

### Current Limitations

1. **POD Classes Only**: Currently supports Plain Old Data (POD) classes only
2. **Fixed Size Data**: Variable-length data transmission is not supported
3. **Inter-Process Communication**: Supports communication between processes on the same machine only
4. **Data Types**: Limited to bool, int, and float types

### Future Enhancements

- Support for custom data structures
- Cross-network communication capabilities
- Additional primitive data types
- Enhanced error reporting

## 🛠️ Error Handling

### Common Errors and Solutions

#### 1. Shared Memory Creation Failure
```python
# Error symptoms
RuntimeError: Failed to create shared memory

# Common causes and solutions
- **Insufficient permissions**: Run with appropriate privileges
- **Memory shortage**: Check system memory availability
- **Name conflicts**: Use unique topic names
```

#### 2. Data Reading Failure
```python
# Error symptoms
data, success = subscriber.subscribe()  # success = False

# Common causes and solutions
- **No publisher**: Ensure publisher is running and publishing data
- **Timing issues**: Publisher may not have sent data yet
- **Topic name mismatch**: Verify exact topic name matching
```

#### 3. Name Conflicts
```python
# Error symptoms
RuntimeError: Shared memory already exists

# Solutions
- **Use unique names**: Employ descriptive, unique topic names
- **Clean up resources**: Remove existing shared memory segments
- **Check running processes**: Verify no duplicate publishers exist
```

## 📈 Performance Optimization

### Recommended Settings

- **Buffer Count**: 3-5 buffers (default: 3)
  - More buffers = better handling of burst traffic
  - Fewer buffers = lower memory usage
- **Publishing Frequency**: Adjust based on application needs
- **Data Size**: Optimal for small data (tens of bytes)

### Performance Benchmarks

Typical performance characteristics (reference values):

- **Latency**: Several microseconds
- **Throughput**: Hundreds of thousands of messages/second
- **Memory Usage**: Data size × Buffer count × Number of topics

**Example: Performance Testing**
```python
import shm_pub_sub
import time

def benchmark_publish_subscribe():
    """Benchmark publish/subscribe performance"""
    pub = shm_pub_sub.Publisher("benchmark", 0, 10)
    sub = shm_pub_sub.Subscriber("benchmark", 0)
    
    # Warm up
    for i in range(100):
        pub.publish(i)
        data, success = sub.subscribe()
    
    # Benchmark
    start_time = time.time()
    messages_sent = 10000
    
    for i in range(messages_sent):
        pub.publish(i)
    
    # Measure receive performance
    received_count = 0
    while received_count < messages_sent:
        data, success = sub.subscribe()
        if success:
            received_count += 1
    
    end_time = time.time()
    duration = end_time - start_time
    
    print(f"📊 Performance Results:")
    print(f"   Messages: {messages_sent}")
    print(f"   Duration: {duration:.3f} seconds")
    print(f"   Throughput: {messages_sent/duration:.0f} messages/second")
    print(f"   Average latency: {duration/messages_sent*1000:.3f} ms")

benchmark_publish_subscribe()
```

## 🔍 Troubleshooting

### Common Issues

#### 1. Python Module Not Found
```bash
ImportError: No module named 'shm_pub_sub'
```
**Solution:**
- Verify setup.py installation was successful
- Check Python path configuration
- Ensure build process completed without errors

#### 2. Shared Memory Permission Errors
```bash
Permission denied
```
**Solution:**
- Check execution permissions
- Run with sudo if necessary (not recommended for production)
- Verify user group memberships

#### 3. Data Inconsistency
**Symptoms:** Retrieved data doesn't match published data
**Solution:**
- Add appropriate sleep/wait times between operations
- Verify buffer sizes are adequate
- Check for race conditions in multi-threaded code

### Debug Methods

#### 1. System Log Analysis
```bash
# Check system logs for error messages
tail -f /var/log/syslog | grep shm
```

#### 2. Shared Memory Inspection
```bash
# Check shared memory segments
ipcs -m

# Remove specific shared memory segment if needed
ipcrm -m <shmid>
```

#### 3. Process Communication Verification
```python
# Test publisher/subscriber independently
import shm_pub_sub

# Test publisher only
pub = shm_pub_sub.Publisher("test_topic", 0, 3)
for i in range(10):
    pub.publish(i)
    print(f"Published: {i}")

# Test subscriber only (run in separate process)
sub = shm_pub_sub.Subscriber("test_topic", 0)
for i in range(10):
    data, success = sub.subscribe()
    print(f"Received: {data}, Success: {success}")
```

## 🎯 Real-World Applications

### 1. Robot Sensor Network
```python
import shm_pub_sub
import time
import random
import threading

class RobotSensorNetwork:
    def __init__(self):
        # Create publishers for different sensors
        self.lidar_pub = shm_pub_sub.Publisher("lidar_distance", 0.0, 5)
        self.camera_pub = shm_pub_sub.Publisher("camera_fps", 0.0, 3)
        self.imu_pub = shm_pub_sub.Publisher("imu_acceleration", 0.0, 10)
        
        # Create control system subscriber
        self.control_sub = shm_pub_sub.Subscriber("control_command", 0)
        
    def simulate_lidar(self):
        """Simulate LIDAR sensor readings"""
        while True:
            distance = random.uniform(0.1, 10.0)  # 10cm to 10m range
            self.lidar_pub.publish(distance)
            time.sleep(0.1)  # 10Hz
    
    def simulate_camera(self):
        """Simulate camera frame rate monitoring"""
        while True:
            fps = random.uniform(25.0, 35.0)  # 25-35 FPS
            self.camera_pub.publish(fps)
            time.sleep(0.033)  # ~30Hz
    
    def simulate_imu(self):
        """Simulate IMU acceleration readings"""
        while True:
            acceleration = random.uniform(-2.0, 2.0)  # ±2g
            self.imu_pub.publish(acceleration)
            time.sleep(0.01)  # 100Hz
    
    def start_sensors(self):
        """Start all sensor simulation threads"""
        sensors = [
            threading.Thread(target=self.simulate_lidar),
            threading.Thread(target=self.simulate_camera),
            threading.Thread(target=self.simulate_imu)
        ]
        
        for sensor in sensors:
            sensor.daemon = True
            sensor.start()
        
        return sensors

# Usage
robot = RobotSensorNetwork()
sensor_threads = robot.start_sensors()

# Keep main thread alive
for thread in sensor_threads:
    thread.join()
```

### 2. Real-Time Data Analytics
```python
import shm_pub_sub
import time
import statistics
import threading
from collections import deque

class DataAnalytics:
    def __init__(self):
        self.data_buffer = deque(maxlen=100)
        self.analytics_pub = shm_pub_sub.Publisher("analytics_result", 0.0, 5)
        self.data_sub = shm_pub_sub.Subscriber("sensor_data", 0.0)
        
    def collect_data(self):
        """Continuously collect sensor data"""
        while True:
            data, success = self.data_sub.subscribe()
            if success:
                self.data_buffer.append(data)
            time.sleep(0.01)
    
    def analyze_data(self):
        """Perform real-time analytics"""
        while True:
            if len(self.data_buffer) >= 10:
                # Calculate moving average
                recent_data = list(self.data_buffer)[-10:]
                moving_avg = statistics.mean(recent_data)
                
                # Publish analytics result
                self.analytics_pub.publish(moving_avg)
                
                print(f"📊 Analytics: Moving average = {moving_avg:.3f}")
            
            time.sleep(0.1)
    
    def start_analytics(self):
        """Start analytics threads"""
        threads = [
            threading.Thread(target=self.collect_data),
            threading.Thread(target=self.analyze_data)
        ]
        
        for thread in threads:
            thread.daemon = True
            thread.start()
        
        return threads

# Usage
analytics = DataAnalytics()
analytics_threads = analytics.start_analytics()

# Keep running
for thread in analytics_threads:
    thread.join()
```

## 🔗 Related Documentation

### Core Documentation
- **[🚀 Introduction to SHM](introduction_en.md)** - Fundamental concepts and overview
- **[📝 C++ Tutorials](tutorials_en.md)** - Complete C++ API documentation
- **[🔄 Publisher/Subscriber Model](tutorials_shm_pub_sub_en.md)** - Advanced Pub/Sub patterns

### Python-Specific Guides
- **[📖 Python Pub/Sub Examples](tutorials_shm_pub_sub_python_en.md)** - Detailed Python examples
- **[🐍 Python API Reference](spec_en.md)** - Complete API specification

### Advanced Topics
- **[🤝 Service Communication](tutorials_shm_service_en.md)** - Request-response patterns
- **[⚡ Action Communication](tutorials_shm_action_en.md)** - Asynchronous task management

## 💡 Best Practices

### Design Patterns

1. **Topic Naming Conventions**
   ```python
   # Good - descriptive and hierarchical
   pub = shm_pub_sub.Publisher("robot/sensors/lidar/distance", 0.0, 5)
   
   # Better - include units and context
   pub = shm_pub_sub.Publisher("robot_main/lidar_front/distance_meters", 0.0, 5)
   ```

2. **Error Handling**
   ```python
   def robust_subscriber():
       sub = shm_pub_sub.Subscriber("data_topic", 0.0)
       
       while True:
           try:
               data, success = sub.subscribe()
               if success:
                   process_data(data)
               else:
                   # Handle no-data case gracefully
                   time.sleep(0.01)
           except Exception as e:
               print(f"❌ Error in subscriber: {e}")
               time.sleep(0.1)  # Brief pause before retry
   ```

3. **Resource Management**
   ```python
   class ManagedPublisher:
       def __init__(self, topic, default_value, buffer_num=3):
           self.pub = shm_pub_sub.Publisher(topic, default_value, buffer_num)
           
       def __enter__(self):
           return self.pub
           
       def __exit__(self, exc_type, exc_val, exc_tb):
           # Cleanup if needed
           pass
   
   # Usage
   with ManagedPublisher("my_topic", 0.0) as pub:
       pub.publish(42.0)
   ```

---

**🎉 Congratulations!** You've mastered Python inter-process communication with shared memory! Start building lightning-fast, distributed Python applications! 🚀✨