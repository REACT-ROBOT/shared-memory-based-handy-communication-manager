# Using the C Language Library

[English | [日本語](docs_jp/md_manual_tutorials_c_language_jp.html)]

## Overview

This library provides a C language API in addition to the C++ version.
The C language version consists of two libraries: `shm_base_c` and `shm_pub_sub_c`.

**Key Feature**: The C language version has **memory layout compatibility** with the C++ version.
This enables inter-process communication between C programs and C++ programs.

## Library Components

| Library | Description |
|---------|-------------|
| `shm_base_c` | Low-level operations for shared memory and ring buffer |
| `shm_pub_sub_c` | High-level Publisher/Subscriber API |

## How to Build

```bash
cd {workspace}/build
cmake ..
make
```

To also build sample programs:
```bash
cmake .. -DBUILD_EXAMPLES=ON
make
```

## Basic Usage

### Publisher

```c
#include "shm_pub_sub_c.h"

typedef struct {
    int32_t value;
    float data;
} MyData;

int main() {
    shm_publisher_t pub;

    // Create publisher (topic name, data size, buffer count)
    if (shm_publisher_create(&pub, "/my_topic", sizeof(MyData), 3) != SHM_SUCCESS) {
        fprintf(stderr, "Failed to create publisher\n");
        return 1;
    }

    // Publish data
    MyData data = {42, 3.14f};
    shm_publish(&pub, &data);

    // Cleanup
    shm_publisher_destroy(&pub);
    return 0;
}
```

### Subscriber

```c
#include "shm_pub_sub_c.h"

typedef struct {
    int32_t value;
    float data;
} MyData;

int main() {
    shm_subscriber_t sub;

    // Create subscriber
    if (shm_subscriber_create(&sub, "/my_topic", sizeof(MyData)) != SHM_SUCCESS) {
        fprintf(stderr, "Failed to create subscriber\n");
        return 1;
    }

    // Set data expiry time to 2 seconds
    shm_subscriber_set_expiry_time(&sub, 2000000);

    // Subscribe to data
    MyData data;
    bool success;
    int ret = shm_subscribe(&sub, &data, &success);

    if (ret == SHM_SUCCESS && success) {
        printf("Received: value=%d, data=%.2f\n", data.value, data.data);
    }

    // Cleanup
    shm_subscriber_destroy(&sub);
    return 0;
}
```

---

## Interoperability with C++ Programs

The most important feature of the C language version is its ability to communicate with C++ programs.
However, certain conditions must be met.

### Requirements for Compatible Data Structures

C++ classes/structs can be read/written from C if they meet the following conditions:

1. **Standard Layout**
2. **Trivially Copyable**
3. **Fixed Size** (no dynamic memory allocation)

#### Compatible Examples

```cpp
// C++ side: Simple struct
struct SensorData {
    int32_t sensor_id;
    float temperature;
    float humidity;
    uint64_t timestamp;
};
```

```c
// C side: Struct with identical layout
typedef struct {
    int32_t sensor_id;
    float temperature;
    float humidity;
    uint64_t timestamp;
} SensorData;
```

#### Incompatible Examples

```cpp
// NG: Class with virtual functions
class BadExample1 {
    virtual void update();  // vtable changes the layout
    int data;
};

// NG: Contains STL containers
struct BadExample2 {
    std::string name;       // Uses pointers internally
    std::vector<int> data;  // Dynamic array
};

// NG: Contains pointer members
struct BadExample3 {
    int* data_ptr;  // Addresses are invalid across processes
};

// NG: Contains reference members
struct BadExample4 {
    int& ref;  // References are also address-based
};
```

### Recommended Pattern: Using Common Header Files

We recommend creating a common header file that can be used from both C and C++.

```c
// common_types.h
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Robot pose data
typedef struct {
    // Position [mm]
    int32_t x;
    int32_t y;
    int32_t z;

    // Orientation [rad * 1000]
    int32_t roll;
    int32_t pitch;
    int32_t yaw;

    // Timestamp [usec]
    uint64_t timestamp;
} RobotPose;

// Sensor data
typedef struct {
    int32_t sensor_id;
    float values[8];  // Fixed-length array
    uint32_t status;
    uint64_t timestamp;
} SensorReading;

// Control command
typedef struct {
    float linear_velocity;   // [m/s]
    float angular_velocity;  // [rad/s]
    uint32_t flags;
} ControlCommand;

#ifdef __cplusplus
}
#endif

#endif // COMMON_TYPES_H
```

### Example: C++ Publisher → C Subscriber

**C++ Publisher**

```cpp
#include "common_types.h"
#include <shm_pub_sub.hpp>

int main() {
    irlab::shm::Publisher<RobotPose> pub("/robot_pose");

    while (true) {
        RobotPose pose;
        pose.x = 1000;
        pose.y = 2000;
        pose.z = 0;
        pose.roll = 0;
        pose.pitch = 0;
        pose.yaw = 1570;  // Approximately 90 degrees
        pose.timestamp = irlab::shm::getCurrentTimeUSec();

        pub.publish(pose);
        usleep(100000);  // 100ms
    }

    return 0;
}
```

**C Subscriber**

```c
#include "common_types.h"
#include "shm_pub_sub_c.h"
#include <stdio.h>

int main() {
    shm_subscriber_t sub;
    shm_subscriber_create(&sub, "/robot_pose", sizeof(RobotPose));
    shm_subscriber_set_expiry_time(&sub, 1000000);  // 1 second

    while (1) {
        RobotPose pose;
        bool success;

        if (shm_subscribe(&sub, &pose, &success) == SHM_SUCCESS && success) {
            printf("Position: (%d, %d, %d) mm\n", pose.x, pose.y, pose.z);
            printf("Orientation: (%.2f, %.2f, %.2f) rad\n",
                   pose.roll / 1000.0, pose.pitch / 1000.0, pose.yaw / 1000.0);
        }

        usleep(10000);  // 10ms
    }

    shm_subscriber_destroy(&sub);
    return 0;
}
```

### Example: C Publisher → C++ Subscriber

**C Publisher**

```c
#include "common_types.h"
#include "shm_pub_sub_c.h"

int main() {
    shm_publisher_t pub;
    shm_publisher_create(&pub, "/control_cmd", sizeof(ControlCommand), 3);

    while (1) {
        ControlCommand cmd;
        cmd.linear_velocity = 0.5f;
        cmd.angular_velocity = 0.1f;
        cmd.flags = 0x01;

        shm_publish(&pub, &cmd);
        usleep(50000);  // 50ms, 20Hz
    }

    shm_publisher_destroy(&pub);
    return 0;
}
```

**C++ Subscriber**

```cpp
#include "common_types.h"
#include <shm_pub_sub.hpp>
#include <iostream>

int main() {
    irlab::shm::Subscriber<ControlCommand> sub("/control_cmd");

    while (true) {
        bool success;
        const ControlCommand& cmd = sub.subscribe(&success);

        if (success) {
            std::cout << "Linear: " << cmd.linear_velocity
                      << ", Angular: " << cmd.angular_velocity
                      << std::endl;
        }

        usleep(10000);
    }

    return 0;
}
```

---

## Converting C++ Classes for C Communication

When using rich classes on the C++ side, convert them to POD structures for communication.

```cpp
// C++ internal class (not used for communication)
class RobotController {
public:
    void setVelocity(float linear, float angular);
    void update();

    // Get data for communication
    ControlCommand getCommandData() const {
        ControlCommand cmd;
        cmd.linear_velocity = linear_vel_;
        cmd.angular_velocity = angular_vel_;
        cmd.flags = status_flags_;
        return cmd;
    }

    // Set from communication data
    void setFromCommandData(const ControlCommand& cmd) {
        linear_vel_ = cmd.linear_velocity;
        angular_vel_ = cmd.angular_velocity;
        status_flags_ = cmd.flags;
    }

private:
    float linear_vel_;
    float angular_vel_;
    uint32_t status_flags_;
};

// Publisher side
irlab::shm::Publisher<ControlCommand> pub("/cmd");
RobotController controller;
controller.setVelocity(1.0f, 0.0f);
pub.publish(controller.getCommandData());  // Convert to POD struct and send
```

---

## Importance of Size Verification

Before starting communication, always verify that the data structure sizes match.

**Verification code (C++ side)**

```cpp
#include <iostream>
#include "common_types.h"

int main() {
    std::cout << "=== Data Structure Sizes ===" << std::endl;
    std::cout << "RobotPose: " << sizeof(RobotPose) << " bytes" << std::endl;
    std::cout << "SensorReading: " << sizeof(SensorReading) << " bytes" << std::endl;
    std::cout << "ControlCommand: " << sizeof(ControlCommand) << " bytes" << std::endl;
    return 0;
}
```

**Verification code (C side)**

```c
#include <stdio.h>
#include "common_types.h"

int main() {
    printf("=== Data Structure Sizes ===\n");
    printf("RobotPose: %zu bytes\n", sizeof(RobotPose));
    printf("SensorReading: %zu bytes\n", sizeof(SensorReading));
    printf("ControlCommand: %zu bytes\n", sizeof(ControlCommand));
    return 0;
}
```

If both programs display the same sizes, communication is safe.

---

## Notes

### 1. Padding and Alignment

Padding may differ between compilers.
To ensure identical layout, use one of the following methods:

```c
// Method 1: Specify packing with pragma
#pragma pack(push, 1)
typedef struct {
    int8_t a;
    int32_t b;  // Placed without padding
} PackedStruct;
#pragma pack(pop)

// Method 2: Add explicit padding
typedef struct {
    int8_t a;
    int8_t padding[3];  // Explicit padding
    int32_t b;
} AlignedStruct;
```

### 2. Endianness

Communication between systems with different endianness is not supported.
(This is typically not an issue for communication on the same machine.)

### 3. Alternative to waitFor Function

The `waitFor()` function available in the C++ version is not provided in the C language version.
Use polling instead:

```c
// Polling-based waiting
while (1) {
    bool success;
    if (shm_subscribe(&sub, &data, &success) == SHM_SUCCESS && success) {
        // New data received
        break;
    }
    usleep(1000);  // Wait 1ms
}
```

---

## Summary

The C language library supports the following use cases:

1. **Embedded Systems**: Shared memory communication in environments where only C is available
2. **Legacy Code Integration**: Adding Publish/Subscribe functionality to existing C codebases
3. **Cross-Language Systems**: Communication between C++ main programs and C modules
4. **Lightweight Implementation**: Works without Boost or other dependencies
