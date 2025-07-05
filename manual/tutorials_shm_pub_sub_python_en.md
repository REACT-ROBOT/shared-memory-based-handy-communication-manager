# How to use Publisher/Subscriber model (Pub/Sub communication) (Python)
[English | [日本語](docs_jp/md_manual_tutorials_shm_pub_sub_python_jp.html)]

## Overview

This tutorial explains how to implement the Publisher/Subscriber model using the Python API of the Shared Memory Based Handy Communication Manager (SHM).

## Basic Usage Examples

### 1. Basic Publisher (Sender)

```python
import shm_pub_sub
import time

# Create int-type Publisher
pub = shm_pub_sub.Publisher("test_topic", 0, 3)

# Send data periodically
for i in range(10):
    pub.publish(i)
    print(f"Published: {i}")
    time.sleep(1)
```

### 2. Basic Subscriber (Receiver)

```python
import shm_pub_sub
import time

# Create int-type Subscriber
sub = shm_pub_sub.Subscriber("test_topic", 0)

# Receive data
for i in range(10):
    data, is_success = sub.subscribe()
    if is_success:
        print(f"Received: {data}")
    else:
        print("Failed to receive data")
    time.sleep(1)
```

## Multiple Data Types Usage

### 3. Multiple Data Type Communication

```python
import shm_pub_sub
import time

# Create Publishers for multiple types
pub_bool = shm_pub_sub.Publisher("bool_topic", False, 3)
pub_int = shm_pub_sub.Publisher("int_topic", 0, 3)
pub_float = shm_pub_sub.Publisher("float_topic", 0.0, 3)

# Create Subscribers for multiple types
sub_bool = shm_pub_sub.Subscriber("bool_topic", False)
sub_int = shm_pub_sub.Subscriber("int_topic", 0)
sub_float = shm_pub_sub.Subscriber("float_topic", 0.0)

# Send and receive data
for i in range(5):
    # Send data
    pub_bool.publish(i % 2 == 0)
    pub_int.publish(i * 10)
    pub_float.publish(i * 3.14)
    
    # Receive data
    bool_data, bool_success = sub_bool.subscribe()
    int_data, int_success = sub_int.subscribe()
    float_data, float_success = sub_float.subscribe()
    
    if bool_success:
        print(f"Bool: {bool_data}")
    if int_success:
        print(f"Int: {int_data}")
    if float_success:
        print(f"Float: {float_data}")
        
    time.sleep(1)
```

## Practical Usage Examples

### 4. Sensor Data Communication

```python
import shm_pub_sub
import time
import random

# Sensor data sender
def sensor_publisher():
    """Send temperature sensor data"""
    temp_pub = shm_pub_sub.Publisher("temperature", 0.0, 5)
    humidity_pub = shm_pub_sub.Publisher("humidity", 0.0, 5)
    
    for i in range(100):
        # Generate mock sensor data
        temperature = 20.0 + random.uniform(-5.0, 15.0)
        humidity = 50.0 + random.uniform(-20.0, 30.0)
        
        # Send data
        temp_pub.publish(temperature)
        humidity_pub.publish(humidity)
        
        print(f"Sensor data sent - Temp: {temperature:.2f}°C, Humidity: {humidity:.2f}%")
        time.sleep(0.5)

# Sensor data receiver
def sensor_subscriber():
    """Receive and process temperature/humidity data"""
    temp_sub = shm_pub_sub.Subscriber("temperature", 0.0)
    humidity_sub = shm_pub_sub.Subscriber("humidity", 0.0)
    
    for i in range(100):
        # Receive data
        temp, temp_success = temp_sub.subscribe()
        humidity, humidity_success = humidity_sub.subscribe()
        
        if temp_success and humidity_success:
            # Check warning conditions
            if temp > 30.0:
                print(f"WARNING: High temperature detected: {temp:.2f}°C")
            if humidity > 80.0:
                print(f"WARNING: High humidity detected: {humidity:.2f}%")
            
            print(f"Monitor - Temp: {temp:.2f}°C, Humidity: {humidity:.2f}%")
        else:
            print("Failed to receive sensor data")
        
        time.sleep(0.5)

# Usage example
if __name__ == "__main__":
    import threading
    
    # Run sender and receiver in separate threads
    pub_thread = threading.Thread(target=sensor_publisher)
    sub_thread = threading.Thread(target=sensor_subscriber)
    
    pub_thread.start()
    sub_thread.start()
    
    pub_thread.join()
    sub_thread.join()
```

### 5. Control System Example

```python
import shm_pub_sub
import time

# Control command sender
def control_publisher():
    """Send control commands"""
    speed_pub = shm_pub_sub.Publisher("motor_speed", 0.0, 3)
    direction_pub = shm_pub_sub.Publisher("motor_direction", 0, 3)  # 0: stop, 1: forward, -1: backward
    enable_pub = shm_pub_sub.Publisher("motor_enable", False, 3)
    
    # Control sequence
    commands = [
        (True, 10.0, 1),   # Enable, speed 10, forward
        (True, 20.0, 1),   # Speed 20, forward
        (True, 15.0, -1),  # Speed 15, backward
        (True, 0.0, 0),    # Stop
        (False, 0.0, 0),   # Disable
    ]
    
    for enable, speed, direction in commands:
        enable_pub.publish(enable)
        speed_pub.publish(speed)
        direction_pub.publish(direction)
        
        print(f"Control command: Enable={enable}, Speed={speed}, Direction={direction}")
        time.sleep(2)

# Control system receiver
def control_subscriber():
    """Receive control commands and control motor"""
    speed_sub = shm_pub_sub.Subscriber("motor_speed", 0.0)
    direction_sub = shm_pub_sub.Subscriber("motor_direction", 0)
    enable_sub = shm_pub_sub.Subscriber("motor_enable", False)
    
    for i in range(20):
        # Receive control commands
        enable, enable_success = enable_sub.subscribe()
        speed, speed_success = speed_sub.subscribe()
        direction, direction_success = direction_sub.subscribe()
        
        if enable_success and speed_success and direction_success:
            if enable:
                direction_str = "FORWARD" if direction == 1 else "BACKWARD" if direction == -1 else "STOP"
                print(f"Motor control: Speed={speed}, Direction={direction_str}")
                
                # Add actual motor control processing here
                # motor_controller.set_speed(speed)
                # motor_controller.set_direction(direction)
            else:
                print("Motor disabled")
                # motor_controller.disable()
        else:
            print("Failed to receive control commands")
        
        time.sleep(0.5)

# Usage example
if __name__ == "__main__":
    import threading
    
    # Run control and motor sides in separate threads
    control_thread = threading.Thread(target=control_publisher)
    motor_thread = threading.Thread(target=control_subscriber)
    
    control_thread.start()
    motor_thread.start()
    
    control_thread.join()
    motor_thread.join()
```

## Error Handling Examples

### 6. Robust Error Handling

```python
import shm_pub_sub
import time

def robust_publisher():
    """Publisher with error handling"""
    try:
        pub = shm_pub_sub.Publisher("robust_topic", 0, 3)
        
        for i in range(10):
            try:
                pub.publish(i)
                print(f"Successfully published: {i}")
            except Exception as e:
                print(f"Failed to publish data {i}: {e}")
                # Retry logic
                time.sleep(0.1)
                try:
                    pub.publish(i)
                    print(f"Retry successful: {i}")
                except Exception as e2:
                    print(f"Retry failed: {e2}")
            
            time.sleep(1)
            
    except Exception as e:
        print(f"Publisher initialization failed: {e}")

def robust_subscriber():
    """Subscriber with error handling"""
    try:
        sub = shm_pub_sub.Subscriber("robust_topic", 0)
        consecutive_failures = 0
        
        for i in range(10):
            try:
                data, is_success = sub.subscribe()
                
                if is_success:
                    print(f"Successfully received: {data}")
                    consecutive_failures = 0
                else:
                    consecutive_failures += 1
                    print(f"Failed to receive data (attempt {consecutive_failures})")
                    
                    # Wait longer for consecutive failures
                    if consecutive_failures > 3:
                        print("Too many consecutive failures, waiting longer...")
                        time.sleep(2)
                        
            except Exception as e:
                print(f"Subscriber error: {e}")
                consecutive_failures += 1
            
            time.sleep(1)
            
    except Exception as e:
        print(f"Subscriber initialization failed: {e}")

# Usage example
if __name__ == "__main__":
    import threading
    
    pub_thread = threading.Thread(target=robust_publisher)
    sub_thread = threading.Thread(target=robust_subscriber)
    
    pub_thread.start()
    sub_thread.start()
    
    pub_thread.join()
    sub_thread.join()
```

## Performance Measurement Examples

### 7. Latency Measurement

```python
import shm_pub_sub
import time

def latency_test():
    """Measure communication latency"""
    pub = shm_pub_sub.Publisher("latency_test", 0, 3)
    sub = shm_pub_sub.Subscriber("latency_test", 0)
    
    latencies = []
    
    for i in range(100):
        # Record send time
        start_time = time.time()
        pub.publish(i)
        
        # Wait for reception
        data, is_success = sub.subscribe()
        end_time = time.time()
        
        if is_success:
            latency = (end_time - start_time) * 1000  # ms
            latencies.append(latency)
            print(f"Data {i}: Latency {latency:.3f} ms")
        else:
            print(f"Failed to receive data {i}")
        
        time.sleep(0.01)
    
    if latencies:
        avg_latency = sum(latencies) / len(latencies)
        min_latency = min(latencies)
        max_latency = max(latencies)
        
        print(f"\nLatency Statistics:")
        print(f"Average: {avg_latency:.3f} ms")
        print(f"Minimum: {min_latency:.3f} ms")
        print(f"Maximum: {max_latency:.3f} ms")

if __name__ == "__main__":
    latency_test()
```

## Best Practices and Notes

### 8. Usage Guidelines

1. **Set appropriate wait times**
   ```python
   # Set appropriate wait time
   time.sleep(0.01)  # 10ms wait (adjust according to usage)
   ```

2. **Implement error handling**
   ```python
   data, is_success = sub.subscribe()
   if not is_success:
       # Implement error handling
       print("Data reception failed")
   ```

3. **Proper resource management**
   ```python
   # Publishers are automatically cleaned up, but
   # explicit scope management is recommended
   with pub_context():
       pub = shm_pub_sub.Publisher("topic", 0, 3)
       # Automatically cleaned up after use
   ```

### 9. Debug Utility

```python
import shm_pub_sub
import time

def debug_monitor(topic_name, data_type_default):
    """Monitor topic status"""
    sub = shm_pub_sub.Subscriber(topic_name, data_type_default)
    
    print(f"Monitoring topic: {topic_name}")
    print("Press Ctrl+C to stop")
    
    try:
        while True:
            data, is_success = sub.subscribe()
            timestamp = time.strftime("%H:%M:%S")
            
            if is_success:
                print(f"[{timestamp}] {topic_name}: {data}")
            else:
                print(f"[{timestamp}] {topic_name}: NO DATA")
            
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("\nMonitoring stopped")

# Usage example
if __name__ == "__main__":
    debug_monitor("temperature", 0.0)
```

## Summary

This tutorial introduced various usage examples of the SHM Python API. From basic send/receive operations to practical sensor data processing, control systems, error handling, and performance measurement, you can see that it supports a wide range of applications.

In actual usage, please select appropriate buffer numbers, data types, and error handling strategies according to your specific use case.
