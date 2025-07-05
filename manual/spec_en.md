# 📋 SHM Technical Specification - Complete System Architecture Documentation
[English | [日本語](docs_jp/md_manual_spec_jp.html)]

\tableofcontents

# 🎯 Purpose

The purpose of SHM (Shared-memory based Handy-communication Manager) is to provide the most secure and fastest possible communication between different processes. It is also designed with careful consideration to make it easy for students to use. Please refer to README.md for installation instructions.

# 📖 Abstract

## Framework Context

The Instrumentation and Robotics Laboratory at Utsunomiya University uses shared memory for data exchange between programs, in addition to local memory generally used by programs.

Shared memory differs from local memory in several key aspects:
- **Memory Management**: Developers must not release allocated memory (inadvertent release prevents data passing to other programs)
- **Programming Complexity**: Higher barrier for novice programmers due to pointer usage
- **Development Overhead**: Designers must create custom processes for each memory type when creating new libraries

This framework **hides data exchange using shared memory** and provides **easy-to-understand inter-process communication** for novice programmers.

## 🚀 System Functions

### Memory Management Process Hiding
Easy inter-process communication is achieved by hiding shared memory area allocation and buffer access within classes. By default, only **standard layout type classes** are supported. Other classes can be supported by defining specialized Publishers/Subscribers for each case. See samples for details.

### Pointer-Free Coding
The system fundamentally only requires passing variables allocated in local memory to Publishers or receiving topics from Subscribers, **eliminating the need to code with shared memory pointers** as in traditional approaches.

## 👥 User Characteristics

### 🎓 Developer
Developers create new programs using internal and external libraries, including this library. Primarily intended for **first-time programming students** such as fourth-year undergraduates.

### 🏗️ Designer  
Designers create new libraries using this framework and transfer current know-how to junior members. Primarily intended for **second-year master students**.

## 📚 Definitions and Terms

### Local Memory
Local memory is a **virtual storage area accessible within a process**. It's the storage area used during normal programming. If not properly released after use, it may cause future problems (programs working correctly for a while may suddenly stop).

### Shared Memory
Shared memory is a **storage area that can be used commonly among processes**. It's allocated by special means and can be implemented in various ways. This implementation uses **POSIX file-mapped memory**, where data stored in shared memory is treated as a file. In Linux, the allocated memory area can be confirmed directly under `/dev/shm`.

### Standard Layout Type
A class or structure that:
- Contains no specific C++ language features (like virtual functions) not found in C
- Has all members with the same access control
- Enables `memcpy` operations
- Has clearly defined layout for use in C programs

**Standard layout types have the following characteristics:**
- No virtual functions or virtual base classes
- All non-static data members have the same access control
- All non-static members of class type are standard layout
- All base classes have standard layout
- No base class of the same type as the first non-static data member
- Meets one of the following conditions:
  - The most derived class has no non-static data members and only one base class with non-static data members
  - No base class contains non-static data members

# 🏗️ Architecture Design

## Overall System Architecture

```mermaid
graph TB
    subgraph "Process A"
        PA[Application A]
        PubA[Publisher A]
    end
    
    subgraph "Process B"
        PB[Application B]
        SubB[Subscriber B]
    end
    
    subgraph "Process C"
        PC[Application C]
        SubC[Subscriber C]
    end
    
    subgraph "Shared Memory Area"
        SM[Shared Memory Segment]
        RB[Ring Buffer]
        Meta[Metadata]
    end
    
    PA --> PubA
    PubA --> SM
    SM --> RB
    RB --> SubB
    RB --> SubC
    SubB --> PB
    SubC --> PC
    
    SM --> Meta
```

## Layer Architecture

```mermaid
graph TB
    subgraph "Application Layer"
        APP[User Application]
    end
    
    subgraph "SHM API Layer"
        PUB[Publisher<T>]
        SUB[Subscriber<T>]
    end
    
    subgraph "Shared Memory Management Layer"
        SHM[SharedMemory]
        POSIX[SharedMemoryPosix]
        RB[RingBuffer]
    end
    
    subgraph "OS Layer"
        KERNEL[Linux Kernel]
        SHMFS[/dev/shm Filesystem]
    end
    
    APP --> PUB
    APP --> SUB
    PUB --> SHM
    SUB --> SHM
    SHM --> POSIX
    POSIX --> RB
    POSIX --> KERNEL
    KERNEL --> SHMFS
```

# 🔧 Detailed Design

## Class Hierarchy Structure

```mermaid
classDiagram
    class SharedMemory {
        <<abstract>>
        #int shm_fd
        #int shm_oflag
        #PERM shm_perm
        #size_t shm_size
        #unsigned char* shm_ptr
        +SharedMemory(int oflag, PERM perm)
        +getSize() size_t
        +getPtr() unsigned char*
        +connect(size_t size)* bool
        +disconnect()* int
        +isDisconnected()* bool
    }
    
    class SharedMemoryPosix {
        -string shm_name
        +SharedMemoryPosix(string name, int oflag, PERM perm)
        +connect(size_t size) bool
        +disconnect() int
        +isDisconnected() bool
    }
    
    class RingBuffer {
        -unsigned char* memory_ptr
        -pthread_mutex_t* mutex
        -pthread_cond_t* condition
        -size_t* element_size
        -size_t* buf_num
        -atomic~uint64_t~* timestamp_list
        -unsigned char* data_list
        -uint64_t timestamp_us
        -uint64_t data_expiry_time_us
        +RingBuffer(unsigned char* first_ptr, size_t size, int buffer_num)
        +getSize(size_t element_size, int buffer_num)$ size_t
        +getTimestamp_us() uint64_t
        +setTimestamp_us(uint64_t input_time_us, int buffer_num)
        +getNewestBufferNum() int
        +getOldestBufferNum() int
        +allocateBuffer(int buffer_num) bool
        +getElementSize() size_t
        +getDataList() unsigned char*
        +signal()
        +waitFor(uint64_t timeout_usec) bool
        +isUpdated() bool
        +setDataExpiryTime_us(uint64_t time_us)
    }
    
    class Publisher~T~ {
        -string shm_name
        -int shm_buf_num
        -PERM shm_perm
        -unique_ptr~SharedMemory~ shared_memory
        -unique_ptr~RingBuffer~ ring_buffer
        -size_t data_size
        +Publisher(string name, int buffer_num, PERM perm)
        +publish(const T& data)
    }
    
    class Subscriber~T~ {
        -string shm_name
        -unique_ptr~SharedMemory~ shared_memory
        -unique_ptr~RingBuffer~ ring_buffer
        -int current_reading_buffer
        -uint64_t data_expiry_time_us
        +Subscriber(string name)
        +subscribe(bool* state) T
        +waitFor(uint64_t timeout_usec) bool
        +setDataExpiryTime_us(uint64_t time_us)
    }
    
    SharedMemory <|-- SharedMemoryPosix
    Publisher~T~ *-- SharedMemory
    Publisher~T~ *-- RingBuffer
    Subscriber~T~ *-- SharedMemory
    Subscriber~T~ *-- RingBuffer
```

## Shared Memory Layout

```mermaid
graph TB
    subgraph "Shared Memory Segment"
        subgraph "Metadata Area"
            MUTEX[pthread_mutex_t]
            COND[pthread_cond_t]
            ESIZE[element_size]
            BUFNUM[buffer_num]
        end
        
        subgraph "Timestamp Area"
            TS0[timestamp[0]]
            TS1[timestamp[1]]
            TS2[timestamp[2]]
            TSN[timestamp[n-1]]
        end
        
        subgraph "Data Area"
            DATA0[data_buffer[0]]
            DATA1[data_buffer[1]]
            DATA2[data_buffer[2]]
            DATAN[data_buffer[n-1]]
        end
    end
    
    MUTEX --> COND
    COND --> ESIZE
    ESIZE --> BUFNUM
    BUFNUM --> TS0
    TS0 --> TS1
    TS1 --> TS2
    TS2 --> TSN
    TSN --> DATA0
    DATA0 --> DATA1
    DATA1 --> DATA2
    DATA2 --> DATAN
```

## Data Flow

### Publish Process Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant Pub as Publisher
    participant RB as RingBuffer
    participant SM as SharedMemory
    
    App->>+Pub: publish(data)
    Pub->>+RB: getOldestBufferNum()
    RB-->>-Pub: buffer_index
    
    loop Maximum 10 retries
        Pub->>+RB: allocateBuffer(buffer_index)
        alt Buffer allocation success
            RB-->>-Pub: true
        else Buffer allocation failure
            RB-->>Pub: false
            Note over Pub: Wait 1ms
            Pub->>RB: getOldestBufferNum()
            RB-->>Pub: new buffer_index
        end
    end
    
    Pub->>SM: Copy data to buffer
    Pub->>RB: setTimestamp_us(current_time, buffer_index)
    Pub->>RB: signal()
    Note over RB: Notify waiting Subscribers
    Pub-->>-App: Process complete
```

### Subscribe Process Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant Sub as Subscriber
    participant RB as RingBuffer
    participant SM as SharedMemory
    
    App->>+Sub: subscribe(&is_success)
    
    alt Shared memory disconnected
        Sub->>+SM: connect()
        SM-->>-Sub: Connection result
        alt Connection failed
            Sub-->>App: (default_value, false)
        end
        Sub->>RB: Create new RingBuffer instance
    end
    
    Sub->>+RB: getNewestBufferNum()
    RB-->>-Sub: buffer_index
    
    alt No valid buffer found
        Sub-->>App: (previous_value, false)
    else Valid buffer found
        Sub->>SM: Copy data from buffer
        Sub-->>-App: (data, true)
    end
```

### waitFor Process Flow

```mermaid
sequenceDiagram
    participant App as Application
    participant Sub as Subscriber
    participant RB as RingBuffer
    
    App->>+Sub: waitFor(timeout_usec)
    
    alt Shared memory disconnected
        Sub->>Sub: Reconnection process
        alt Reconnection failed
            Sub-->>App: false
        end
    end
    
    Sub->>+RB: waitFor(timeout_usec)
    Note over RB: Wait with pthread_cond_timedwait
    
    alt Signal received before timeout
        RB-->>-Sub: true
        Sub-->>-App: true
    else Timeout
        RB-->>Sub: false
        Sub-->>App: false
    end
```

# 📡 Communication Protocol

## Ring Buffer Algorithm

### Buffer Selection Algorithm

```mermaid
flowchart TD
    Start([Start]) --> GetOldest[Identify buffer with oldest timestamp]
    GetOldest --> TryAlloc{Try buffer allocation}
    TryAlloc -->|Success| WriteData[Write data]
    TryAlloc -->|Failure| CheckRetry{Retry count < 10?}
    CheckRetry -->|Yes| Wait[Wait 1ms]
    Wait --> GetOldest
    CheckRetry -->|No| Error[Error: Buffer allocation failed]
    WriteData --> UpdateTime[Update timestamp]
    UpdateTime --> Signal[Send signal via condition variable]
    Signal --> End([End])
    Error --> End
```

### Data Reading Algorithm

```mermaid
flowchart TD
    Start([Start]) --> CheckConn{Shared memory connected?}
    CheckConn -->|No| Reconnect[Try reconnection]
    Reconnect --> ConnSuccess{Connection success?}
    ConnSuccess -->|No| ReturnFail[Return failure]
    ConnSuccess -->|Yes| GetNewest
    CheckConn -->|Yes| GetNewest[Identify buffer with newest timestamp]
    GetNewest --> ValidBuffer{Valid buffer?}
    ValidBuffer -->|No| ReturnOld[Return previous value and failure flag]
    ValidBuffer -->|Yes| CheckExpiry{Data within expiry time?}
    CheckExpiry -->|No| ReturnOld
    CheckExpiry -->|Yes| ReadData[Read data]
    ReadData --> ReturnSuccess[Return data and success flag]
    ReturnFail --> End([End])
    ReturnOld --> End
    ReturnSuccess --> End
```

## Synchronization Mechanism

### Mutex and Condition Variable

```mermaid
stateDiagram-v2
    [*] --> Unlocked : Initial state
    
    state Publisher {
        Unlocked --> Locked : pthread_mutex_lock
        Locked --> Writing : Buffer allocation success
        Writing --> Unlocked : pthread_mutex_unlock + pthread_cond_signal
        Locked --> Unlocked : Buffer allocation failure + pthread_mutex_unlock
    }
    
    state Subscriber {
        Unlocked --> Waiting : waitFor() called
        Waiting --> Unlocked : Timeout
        Waiting --> Processing : Signal received
        Processing --> Unlocked : Data reading complete
    }
```

# ⚡ Performance Characteristics

## Memory Usage

Shared memory segment size is calculated by the following formula:

```
total_size = metadata_size + timestamp_array_size + data_array_size

metadata_size = sizeof(pthread_mutex_t) + sizeof(pthread_cond_t) + 
                sizeof(size_t) + sizeof(size_t)

timestamp_array_size = sizeof(uint64_t) * buffer_num

data_array_size = element_size * buffer_num
```

## Latency Characteristics

```mermaid
graph LR
    subgraph "Latency Components"
        A[Application Processing] --> B[Publisher Processing]
        B --> C[Mutex Acquisition]
        C --> D[Memory Copy]
        D --> E[Timestamp Update]
        E --> F[Signal Transmission]
        F --> G[Subscriber Processing]
        G --> H[Application Processing]
    end
    
    subgraph "Typical Times"
        T1[App: ~1μs]
        T2[Pub: ~2μs]
        T3[Mutex: ~0.5μs]
        T4[Copy: ~0.1μs]
        T5[Time: ~0.1μs]
        T6[Signal: ~0.5μs]
        T7[Sub: ~2μs]
        T8[App: ~1μs]
    end
```

# 🔒 Security Considerations

## Access Permissions

```mermaid
graph TB
    subgraph "POSIX Permission Model"
        Owner[Owner]
        Group[Group]
        Others[Others]
    end
    
    subgraph "Permission Types"
        Read[Read: S_IRUSR/S_IRGRP/S_IROTH]
        Write[Write: S_IWUSR/S_IWGRP/S_IWOTH]
    end
    
    subgraph "Default Settings"
        Default["DEFAULT_PERM = 0666<br/>(All users read/write)"]
    end
    
    Owner --> Read
    Owner --> Write
    Group --> Read
    Group --> Write
    Others --> Read
    Others --> Write
    
    Default -.-> Owner
    Default -.-> Group
    Default -.-> Others
```

## Data Integrity

```mermaid
sequenceDiagram
    participant P1 as Publisher 1
    participant P2 as Publisher 2
    participant Mutex as Mutex
    participant Buffer as SharedBuffer
    participant S as Subscriber
    
    Note over P1,S: Concurrent writes from multiple Publishers
    
    P1->>+Mutex: lock()
    P2->>Mutex: lock() (blocked)
    Mutex-->>-P1: Acquisition success
    
    P1->>Buffer: Write data
    P1->>Buffer: Update timestamp
    P1->>+Mutex: unlock() + signal()
    Mutex-->>-P2: Acquisition success
    
    P2->>Buffer: Write data
    P2->>Buffer: Update timestamp
    P2->>Mutex: unlock() + signal()
    
    S->>Buffer: Read latest data
    Note over S: Gets P2's data
```

# ❌ Error Handling

## Error Classification and Response

```mermaid
flowchart TD
    Error([Error Occurred]) --> CheckType{Error Type}
    
    CheckType -->|Initialization Error| InitError[Initialization Error]
    CheckType -->|Communication Error| CommError[Communication Error]
    CheckType -->|Memory Error| MemError[Memory Error]
    CheckType -->|Timeout| TimeoutError[Timeout Error]
    
    InitError --> InitActions[・Name verification<br/>・Permission check<br/>・POD type verification]
    CommError --> CommActions[・Shared memory reconnection<br/>・Publisher side check<br/>・Process liveness check]
    MemError --> MemActions[・Memory shortage check<br/>・Segment deletion<br/>・System restart]
    TimeoutError --> TimeoutActions[・Timeout value adjustment<br/>・Publisher frequency check<br/>・System load check]
    
    InitActions --> LogError[Output error log]
    CommActions --> LogError
    MemActions --> LogError
    TimeoutActions --> LogError
    
    LogError --> Recovery{Recoverable?}
    Recovery -->|Yes| Retry[Retry process]
    Recovery -->|No| Abort[Abort process]
    
    Retry --> Success{Success?}
    Success -->|Yes| End([Normal termination])
    Success -->|No| Recovery
    Abort --> End
```

# 🐍 Python Binding Design

## Boost.Python Wrapper Structure

```mermaid
classDiagram
    class PublisherBool {
        +PublisherBool(string name, bool arg, int buffer_num)
        +_publish(bool data)
    }
    
    class PublisherInt {
        +PublisherInt(string name, int arg, int buffer_num)
        +_publish(int data)
    }
    
    class PublisherFloat {
        +PublisherFloat(string name, float arg, int buffer_num)
        +_publish(float data)
    }
    
    class SubscriberBool {
        +SubscriberBool(string name, bool arg)
        +_subscribe() tuple~bool, bool~
    }
    
    class SubscriberInt {
        +SubscriberInt(string name, int arg)
        +_subscribe() tuple~int, bool~
    }
    
    class SubscriberFloat {
        +SubscriberFloat(string name, float arg)
        +_subscribe() tuple~float, bool~
    }
    
    class Publisher~T~ {
        <<C++ Template>>
    }
    
    class Subscriber~T~ {
        <<C++ Template>>
    }
    
    Publisher~T~ <|-- PublisherBool
    Publisher~T~ <|-- PublisherInt
    Publisher~T~ <|-- PublisherFloat
    Subscriber~T~ <|-- SubscriberBool
    Subscriber~T~ <|-- SubscriberInt
    Subscriber~T~ <|-- SubscriberFloat
```

## Python/C++ Data Conversion

```mermaid
sequenceDiagram
    participant Py as Python App
    participant Boost as Boost.Python
    participant Wrapper as C++ Wrapper
    participant Core as SHM Core
    
    Note over Py,Core: Publish Process
    Py->>+Boost: pub.publish(data)
    Boost->>+Wrapper: _publish(converted_data)
    Note over Boost: Python type → C++ type conversion
    Wrapper->>+Core: publish(data)
    Core-->>-Wrapper: void
    Wrapper-->>-Boost: void
    Boost-->>-Py: None
    
    Note over Py,Core: Subscribe Process
    Py->>+Boost: data, success = sub.subscribe()
    Boost->>+Wrapper: _subscribe()
    Wrapper->>+Core: subscribe(&is_success)
    Core-->>-Wrapper: result_data
    Wrapper-->>-Boost: make_tuple(result_data, is_success)
    Note over Boost: C++ type → Python type conversion
    Boost-->>-Py: (data, success)
```

# 🔧 Extensibility Considerations

## Adding New Data Types

```mermaid
flowchart TD
    Start([Add new type T]) --> CheckPOD{POD type?}
    CheckPOD -->|Yes| UseTemplate[Use existing templates]
    CheckPOD -->|No| Specialize[Template specialization]
    
    UseTemplate --> InstantiateC[Instantiate Publisher<T>,<br/>Subscriber<T> in C++]
    Specialize --> CustomImpl[Custom implementation<br/>・Serialization<br/>・Deserialization]
    
    CustomImpl --> InstantiateC
    InstantiateC --> PythonNeeded{Python support needed?}
    
    PythonNeeded -->|Yes| CreateWrapper[Create Boost.Python wrapper<br/>・PublisherT<br/>・SubscriberT]
    PythonNeeded -->|No| TestC[Implement C++ tests]
    
    CreateWrapper --> UpdateModule[Add to BOOST_PYTHON_MODULE]
    UpdateModule --> TestPy[Implement Python tests]
    TestPy --> TestC
    TestC --> Document[Update documentation]
    Document --> End([Complete])
```

# 📚 References

## man shm_overview
The following URL provides an overview of POSIX shared memory:
<https://linuxjm.osdn.jp/html/LDP_man-pages/man7/shm_overview.7.html>

## Related Technical Specifications
- **POSIX.1-2001** Shared memory objects
- **POSIX.1-2001** pthread mutex and condition variables  
- **C++11** Standard layout types
- **Boost.Python 1.75+** Python bindings

## Additional Resources
- **Linux Kernel Documentation** - `/dev/shm` filesystem implementation
- **POSIX Real-time Extensions** - Inter-process synchronization
- **C++ Core Guidelines** - Memory safety and RAII patterns
- **Boost Documentation** - Python/C++ integration best practices

---

**📋 Technical Note**: This specification provides comprehensive documentation for the SHM library architecture, enabling developers and designers to understand, extend, and maintain the system effectively. For implementation examples, see the tutorial documentation.
