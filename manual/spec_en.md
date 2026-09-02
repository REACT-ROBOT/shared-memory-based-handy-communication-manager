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

@htmlonly
<div class="mermaid">
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## Layer Architecture

@htmlonly
<div class="mermaid">
graph TB
    subgraph "Application Layer"
        APP[User Application]
    end
    
    subgraph "SHM API Layer"
        PUB["Publisher"]
        SUB["Subscriber"]
    end
    
    subgraph "Shared Memory Management Layer"
        SHM[SharedMemory]
        POSIX[SharedMemoryPosix]
        RB[RingBuffer]
    end
    
    subgraph "OS Layer"
        KERNEL[Linux Kernel]
        SHMFS["/dev/shm Filesystem"]
    end
    
    APP --> PUB
    APP --> SUB
    PUB --> SHM
    SUB --> SHM
    SHM --> POSIX
    POSIX --> RB
    POSIX --> KERNEL
    KERNEL --> SHMFS
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 🔧 Detailed Design

## Class Hierarchy Structure

@htmlonly
<div class="mermaid">
classDiagram
    class SharedMemory {
        &lt;&lt;abstract&gt;&gt;
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
        -ShmHeader* header
        -SlotRecord* slot_base
        -unsigned char* data_list
        -atomic_uint64_t* sequence_source
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
    
    class PublisherT {
        -string shm_name
        -int shm_buf_num
        -PERM shm_perm
        -unique_ptr_SharedMemory shared_memory
        -unique_ptr_RingBuffer ring_buffer
        -size_t data_size
        +Publisher(string name, int buffer_num, PERM perm)
        +publish(const T& data)
    }
    
    class SubscriberT {
        -string shm_name
        -unique_ptr_SharedMemory shared_memory
        -unique_ptr_RingBuffer ring_buffer
        -int current_reading_buffer
        -uint64_t data_expiry_time_us
        +Subscriber(string name)
        +subscribe(bool* state) T
        +waitFor(uint64_t timeout_usec) bool
        +setDataExpiryTime_us(uint64_t time_us)
    }
    
    SharedMemory <|-- SharedMemoryPosix
    PublisherT *-- SharedMemory
    PublisherT *-- RingBuffer
    SubscriberT *-- SharedMemory
    SubscriberT *-- RingBuffer
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## Shared Memory Layout

@htmlonly
<div class="mermaid">
graph TB
    subgraph "root segment /shm_&lt;topic&gt; (generation 1, also the directory)"
        HDR["ShmHeader (192 B)<br/>magic / abi_major / total_size<br/>element_capacity / buf_num / payload_alignment<br/>slot_offset / slot_size / data_offset<br/>generation / sequence / boot_id_hash<br/><b>latest_generation</b> (16-bit generation + 48-bit nonce)<br/>element_size / schema_id / payload_kind<br/>schema_version / segment_nonce"]
        subgraph "SlotRecord[] (128 B each, cache-line aligned)"
            SR0["sequence (atomic)<br/>payload_size (atomic)<br/>capture_monotonic_us (atomic)<br/>capture_realtime_us (atomic)<br/>owner (robust mutex)"]
            SRN["… buf_num entries"]
        end
        subgraph "payload area (payload_alignment)"
            D0["slot 0"]
            DN["… buf_num entries"]
        end
    end

    subgraph "generation segment /shm_&lt;topic&gt;#&lt;gen&gt;-&lt;nonce&gt;"
        G["Same structure. A layout change never rebuilds the<br/>live segment; it <b>creates a new generation</b>"]
    end

    HDR --> SR0
    SR0 --> SRN
    SRN --> D0
    D0 --> DN
    HDR -.->|pointed to by latest_generation| G
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## Data Flow

### Publish Process Flow

@htmlonly
<div class="mermaid">
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

### Subscribe Process Flow

@htmlonly
<div class="mermaid">
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

### waitFor Process Flow

@htmlonly
<div class="mermaid">
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
    Note over RB: Poll the sequence number at a short interval (no condition variable)
    
    alt Signal received before timeout
        RB-->>-Sub: true
        Sub-->>-App: true
    else Timeout
        RB-->>Sub: false
        Sub-->>App: false
    end
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 📡 Communication Protocol

## Ring Buffer Algorithm

### Buffer Selection (writer)

The point is that the writer **tries every slot, oldest first**. Aiming at a single
slot means a publish fails while a reader sits on that one slot, even when others are free.

1. `ensureCapacity()` — attach to a generation that satisfies the request; create a
   new generation if the current one is too small.
2. Remember the generation tag.
3. Walk the slots in increasing sequence order and `trylock` each **without waiting**.
4. If every slot is busy, wait on the oldest one for at most `SLOT_LOCK_TIMEOUT_US` (2 ms).
5. `commitBuffer()` — take a sequence number from the **root** counter and release the slot.
6. If the generation tag changed while committing, publish again into the new
   generation, **carrying the original capture time over**.

### Data Reading (reader)

1. `follow()` — move to the generation that is currently live. If the payload format
   disagrees, fail here **without touching the payload**.
2. Pick the slot with the largest sequence number.
3. If no slot is valid, distinguish `Empty` (nothing has ever been published, i.e. the
   topic-wide sequence counter is 0) from `Contended` (everything happened to be busy;
   **retrying is worthwhile**).
4. `readSample()` — lock the slot and read the payload and its metadata as a single
   snapshot, then unlock.
5. On failure retry up to 5 times, then return the previous value with a false flag.

## Synchronization Mechanism

### A robust mutex per slot; no condition variable

The original format guarded the whole segment with one mutex and signalled completion
through a condition variable. Neither is used any more.

- **A segment-wide mutex serialises publishers.** Each slot now owns its mutex, so a
  writer only has to take one free slot.
- **A condition variable cannot be recovered when its owner dies.**
  `RingBuffer::signal()` is now an empty function and `waitFor()` polls the sequence
  number at a short interval, so a subscriber never hangs because a publisher died.

Besides `PTHREAD_PROCESS_SHARED`, the slot mutex sets:

| Attribute | Why |
|---|---|
| `PTHREAD_MUTEX_ROBUST` | the kernel reports an owner's death as `EOWNERDEAD`, so liveness is **never guessed from a timestamp** |
| `PTHREAD_PRIO_INHERIT` | avoids priority inversion when a SCHED_FIFO control loop waits for a slot held by a lower-priority process |

### Give the ring more slots than there are participants

Because readers take a slot lock too, **the number of slots must exceed the number of
participants that read and write concurrently**. `buffer_num = 1` makes a writer and a
reader fight over the only slot, and under CPU pressure a publish can fail to acquire it
(it reports the failure rather than corrupting anything). The default is 3.

```
buffer_num >= ceil(publish_rate * required_history) + concurrent_writers + headroom(~2)
```

The first term comes from how far back consumers need to look. `shm_tool doctor` reports
the history actually held.

**Readers take this mutex too.** Comparing the sequence number before and after the copy
detects a torn sample, but the writer's `memcpy` and the reader's `memcpy` could still
touch the same ordinary memory concurrently, which is a data race — undefined behaviour —
under the C++ memory model. The critical section is one `memcpy` long.

# 🧬 Layout Generations

A topic whose payload length varies (a vector, an image) needs a different capacity over
time. The live segment is **never rebuilt**; a new generation is created as a separate
segment instead, so a participant still holding the old one keeps a valid mapping.

```
generation 1      : /shm_<topic>                   root; both data and directory
generation N >= 2 : /shm_<topic>#<N>-<nonce hex12>
```

The nonce in the name matters. With a fixed `#N`, the remains of a process that died
while creating the segment occupy the name, and `O_EXCL` fails forever after. Reclaiming
it "after a timeout" would kill a creator that is merely slow or stopped. A nonce removes
the contest for the name, so liveness never has to be guessed from elapsed time.

The live generation is published through the root header's `latest_generation`, which
packs the **generation (high 16 bits) and the nonce (low 48 bits) into one word** so that
a single CAS makes both visible at once.

Only segments that are provably not live are removed: an older generation, or the same
generation with a different nonce (the loser of a cutover race). A **higher** generation
number is never touched — it may be under construction.

# 🧾 Declaring the Payload Format

Comparing `element_size` lets a reordering of members through, because `sizeof` does not
change. Declare the layout so that such a change is caught:

```cpp
struct LidarScan { uint32_t count; float ranges[1081]; };
SHM_DECLARE_LAYOUT(LidarScan, count, ranges);
```

The hash is built at compile time from `sizeof(T)`, `alignof(T)` and every member's
`offsetof`/`sizeof`, so **no version number has to be maintained by hand**. Omitting a
member, or listing them out of declaration order, is a compile error.

| Case | Macro |
|---|---|
| POD payload | `SHM_DECLARE_LAYOUT(T, member...)` |
| Same layout, different meaning (a unit change) | `SHM_DECLARE_LAYOUT_REV(T, revision, member...)` |
| Wire format defined by `serialize()` | `SHM_DECLARE_SERIALIZED_FORMAT(T, revision)` |

Build with `-DSHM_REQUIRE_LAYOUT=ON` to make an undeclared payload type a compile error
(off by default, to allow an incremental migration).

# ⏱ Reading by Time (the time machine)

Built for "fetch the scan closest in time to this odometry update".

| Type | Meaning |
|---|---|
| `TimeQuery{time_us, policy}` | the time to search for, and the policy |
| `SearchPolicy` | `Nearest` / `AtOrBefore` / `AtOrAfter` |
| `SearchStatus` | `Success` / `NotConnected` / `Empty` / `TooOld` / `TooNew` / `Contended` / `InvalidReference` |
| `SampleInfo` | sequence number, capture times, payload size |
| `RetentionWindow` | the range of history currently held |

**Only `CLOCK_MONOTONIC_RAW` is used for searching.** The wall clock is recorded but never
searched, because NTP steps make it unusable as a reference.

**`Nearest` always returns the closest existing sample**, however far away it is, so it
never reports `TooOld`/`TooNew` by itself. State the bound through
`subscribeAlignedTo()`'s `max_skew_us` (a **required** argument). The `SampleInfo` from a
failed `subscribe()` is all zeros; passing it as the reference is rejected with
`InvalidReference` instead of silently searching for time 0.

```cpp
SampleInfo odom_info;
bool ok = false;
odom_sub.subscribe(&ok, &odom_info);

SearchStatus status;
SampleInfo   scan_info;
// reject anything more than 50 ms away from the odometry sample
const Scan &scan = scan_sub.subscribeAlignedTo(odom_info, &status, 50000, &scan_info);
if (status == SearchStatus::Success) { /* use scan */ }
```

History depth is `buf_num ÷ publish rate`. A 10 Hz sensor with `buf_num = 3` holds only
300 ms. `getRetentionWindow()` and `shm_tool doctor` report what is actually held.

## Naming a time directly — `subscribeAt()`

Use this when there is no reference sample and you already know the time.

```cpp
TimeQuery    q{ target_us, SearchPolicy::AtOrBefore };
SearchStatus status;
SampleInfo   info;
const Scan  &scan = scan_sub.subscribeAt(q, &status, &info);
```

`time_us` is a `CLOCK_MONOTONIC_RAW` reading. Take it from
`clock_gettime(CLOCK_MONOTONIC_RAW, ...)` or from another sample's
`SampleInfo::capture_monotonic_us`. `CLOCK_MONOTONIC` (without `_RAW`) and the wall clock
carry the same unit but a different origin, so passing one returns a quietly skewed result.

| `SearchPolicy` | Behaviour | When nothing matches |
|---|---|---|
| `Nearest` | closest sample, at any distance | `Empty` only |
| `AtOrBefore` | newest sample **at or before** the time | `TooOld` |
| `AtOrAfter` | oldest sample **at or after** the time | `TooNew` |

`TooOld` and `TooNew` describe **the time you asked for**, relative to what is
retained — not the direction the samples lie in.

- Nothing matches `AtOrBefore`: nothing older than your time is still held, so
  **that moment has already been overwritten** → `TooOld`
- Nothing matches `AtOrAfter`: nothing newer than your time exists yet, so
  **that moment has not been published yet** → `TooNew`

`subscribeAlignedTo()` is a thin wrapper over `Nearest` plus the skew check.

# 🛠 Operations

| Command | Purpose |
|---|---|
| `shm_tool list` | list `/dev/shm` |
| `shm_tool remove <topic>` | remove a topic including every generation |
| `shm_tool doctor [topic]` | read the headers and report whether the segments are usable |

`doctor` marks anything that needs action with ★ and exits 1: an uninitialised segment,
a foreign format, an ABI mismatch, remains from before the last reboot, a name whose
nonce disagrees with the header, and stale generations. "Format not declared" is only a
note, and leaves the exit code at 0.

When the shared-memory format changes incompatibly, **run `shm_tool remove` before
swapping binaries and restart every process on the topic together**. A leftover segment
makes the connection fail immediately with the reason and the recovery command — it never
stalls.

# ⚡ Performance Characteristics

## Memory Usage

Shared memory segment size is calculated by the following formula:

```
total_size = metadata_size + timestamp_array_size + data_array_size

metadata_size = sizeof(ShmHeader)               // fixed 192 B
              + sizeof(SlotRecord) * buffer_num  // 128 B per slot

timestamp_array_size = sizeof(uint64_t) * buffer_num

data_array_size = element_size * buffer_num
```

## Latency Characteristics

@htmlonly
<div class="mermaid">
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 🔒 Security Considerations

## Access Permissions

@htmlonly
<div class="mermaid">
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
        Default["DEFAULT_PERM = 0660<br/>(owner and group only)<br/>pass PERM_ALL = 0666 for the old behaviour"]
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## Data Integrity

Concurrent publishers do not serialise, because they take different slots.
Consistency is decided by the **sequence number, never by a timestamp**. Sequence
numbers come from a single atomic in the root segment via `fetch_add`, so they are
unique across the topic and are never reused.

If timestamps were used, two publishes within the same microsecond would be
indistinguishable, and an ABA would appear once the ring wrapped and the same slot was
re-acquired: the before/after values match while the contents are a different sample.
A monotonically increasing, never-reused sequence number makes that impossible.

# ❌ Error Handling

## Error Classification and Response

@htmlonly
<div class="mermaid">
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 🐍 Python Binding Design

## Boost.Python Wrapper Structure

@htmlonly
<div class="mermaid">
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
        +_subscribe() (bool,bool)
    }
    
    class SubscriberInt {
        +SubscriberInt(string name, int arg)
        +_subscribe() (int,bool)
    }
    
    class SubscriberFloat {
        +SubscriberFloat(string name, float arg)
        +_subscribe() (float,bool)
    }
    
    class Publisher {
        &lt;&lt;C++ Template&gt;&gt;
    }
    
    class Subscriber {
        &lt;&lt;C++ Template&gt;&gt;
    }
    
    Publisher <|-- PublisherBool
    Publisher <|-- PublisherInt
    Publisher <|-- PublisherFloat
    Subscriber <|-- SubscriberBool
    Subscriber <|-- SubscriberInt
    Subscriber <|-- SubscriberFloat
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## Python/C++ Data Conversion

@htmlonly
<div class="mermaid">
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 🔧 Extensibility Considerations

## Adding New Data Types

Which of three paths you take depends on the type. **All three require a format
declaration.**

| Type | What to do |
|---|---|
| fixed-size POD | write `SHM_DECLARE_LAYOUT()`; `Publisher<T>` works as is |
| `std::vector<T>` | include `shm_pub_sub_vector.hpp`; declare the element type |
| variable-size / non-POD | specialize `Publisher<T>` / `Subscriber<T>` and use `SHM_DECLARE_SERIALIZED_FORMAT()` |

### 1. Fixed-size POD

```cpp
// my_types.hpp
#ifndef MY_TYPES_HPP
#define MY_TYPES_HPP
#include "shm_pub_sub.hpp"

struct Pose { double x, y, theta; };
SHM_DECLARE_LAYOUT(Pose, x, y, theta);   // global scope, inside the include guard

#endif
```

That is all:

```cpp
irlab::shm::Publisher<Pose>  pub("pose", 8);
irlab::shm::Subscriber<Pose> sub("pose");
```

Omitting even one member is a compile error — `layout_covers_type()` spots the gap
between `offsetof` and `sizeof`. List every member, in declaration order.

Declare a nested type **before** the type that contains it; the outer declaration folds
in the inner one's version, so the reverse order is "specialization after instantiation",
which is ill-formed.

```cpp
struct Vec2 { double x, y; };
struct Path { Vec2 a; Vec2 b; };
SHM_DECLARE_LAYOUT(Vec2, x, y);      // first
SHM_DECLARE_LAYOUT(Path, a, b);      // second
```

A change that keeps the layout and alters only the **meaning** (say `float range` moving
from metres to millimetres) cannot be detected by the automatic hash. That is the one
case where you bump a revision by hand:

```cpp
SHM_DECLARE_LAYOUT_REV(Scan, 2, range, intensity);
```

### 2. `std::vector<T>`

Declare the element type with `SHM_DECLARE_LAYOUT()` and include
`shm_pub_sub_vector.hpp`. A change in length triggers a generation cut-over (a new
segment is allocated), so a POD holding a fixed-size array is faster when the length is
in fact fixed.

### 3. Variable-size / non-POD (a `cv::Mat`-like type)

Here the wire format is decided by `serialize()`, not by the struct's memory layout, so
nothing can be derived from the layout and the revision is carried by hand.

```cpp
// shm_pub_sub_my_scan.hpp
#ifndef SHM_PUB_SUB_MY_SCAN_HPP
#define SHM_PUB_SUB_MY_SCAN_HPP
#include "shm_pub_sub.hpp"
#include "my_scan.hpp"

// (1) inside the include guard, and (2) before the specializations below
SHM_DECLARE_SERIALIZED_FORMAT(MyScan, 1);   // bump to 2 when serialize() changes

namespace irlab { namespace shm {

template <> class Publisher<MyScan>  { /* ... */ };
template <> class Subscriber<MyScan> { /* ... */ };

}}  // namespace
#endif
```

**Both placement constraints are accepted by gcc 11 on x86** and only fail when you build
on a Raspberry Pi 4 (gcc 12), so follow them deliberately:

1. **Inside the include guard.** Outside it, a translation unit that pulls the header in
   twice transitively re-expands the macro and redefines `shm_schema<T>`.
2. **Before the `Publisher` / `Subscriber` specializations**, whose `contractOf()` calls
   `schema_version_of<T>()`. A declaration after them is ill-formed, and when
   instantiation happens varies by compiler version.

`shm_pub_sub/test/check_format_declaration_placement.py` checks both mechanically.

### Migration

Building with `-DSHM_REQUIRE_LAYOUT=ON` turns publishing or subscribing an undeclared
type into a compile error. The default is OFF, because the workspace holds more than 90
payload types and requiring them all at once would stop several repositories from
building simultaneously. Turn it on one package at a time. `shm_tool doctor` marks which
live topics are still undeclared.

### Python support

The Python binding covers `bool`, `int` and `float` only; custom types are not supported.
If you need one, add a wrapper to `shm_pub_sub_python.cpp` and register it in
`BOOST_PYTHON_MODULE`. See "Python Binding Design" above.

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
