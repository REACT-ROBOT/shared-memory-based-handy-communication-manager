# SHM 仕様書
[[English](../md_manual_spec_en.html) | 日本語]

\tableofcontents

# 目的
SHM (Shared-memory based Handy-communication Manager)の目的は，異なるプロセス間でできるだけ安全かつ高速な通信を行うことである．また、学生が使用しやすいように、考慮して設計したはずである．インストール方法などは，README.mdを参照すること．

# 概要
## フレームワークのコンテクスト
宇都宮大学計測・ロボット工学研究室では，一般にプログラムで利用されるローカルメモリに加えて，プログラム間のデータのやり取りに利用できる共有メモリを使用する．
共有メモリはローカルメモリとは異なり，開発者が確保したメモリを開放する必要がない（不用意に開放すると他のプログラムにデータが渡せなくなる）ことやポインタを利用するために初学者に対する敷居が微妙に高いこと，設計者が新しいライブラリを作成する度にそれぞれのメモリに合わせて処理を作成する必要があることなど問題点があった．
本フレームワークは，共有メモリによるデータのやり取りを隠蔽し，初学者にもわかりやすいプロセス間通信を提供する．

## システム機能

### メモリ管理処理の隠蔽
共有メモリの領域確保やバッファへのアクセスをクラスに隠蔽することで、容易にプロセス間通信する機能を実現した。ただし、標準では標準レイアウト型のクラスのみをサポートする。その他のクラスについては都度特殊化したPublisher/Subscriberを定義することで対応できる．詳細はサンプルを参照のこと．

### ポインタレスコーディング
基本的に、ローカルメモリに確保した変数を出版者（Publisher）に渡したり、購読者（Subscriber）からのトピックを受け取るのみであり、従来のように共有メモリのポインタを意識してコーディングすることがなくなった．

## ユーザ特性
### 開発者
開発者とは，本ライブラリなどの研究室内外のライブラリを利用して新しいプログラムを作成するものを指す．主に、学部４年生などのプログラミング初学者を対象としている．

### 設計者
設計者とは，本ライブラリを利用して新しいライブラリを作成し，現状のノウハウを後輩に受け継ぐものを指す．主に，修士２年生を対象としている．

## 定義・用語
### ローカルメモリ
ローカルメモリとは，１プロセス内でアクセスできる仮想記憶領域を指す．通常のプログラミング時に利用される記憶領域であり，利用後にちゃんと開放しないと将来的に大変な（プログラムがしばらく正常に動いていたのに，急に動かなくなる）ことになる領域である．

### 共有メモリ
共有メモリとは，プロセス間で共通して利用可能な記憶領域を指す．特殊な手段によって確保される記憶領域であり，様々な実装方法があるが、今回はPOSIXのファイルマップドメモリを採用している．これは、共有メモリに格納するデータをファイルとして扱う方式であり，Linuxでは/dev/shmの直下に確保したメモリ領域が確認できる．

### 標準レイアウト型
クラスまたは構造体にC言語にはない仮想関数のような特定のC++言語の機能が含まれておらず，すべてのメンバーに同じアクセス制御が含まれている場合、それは標準レイアウト型である．memcpyが可能で，Cプログラムで使用できるようにレイアウトが明確に定義されている．標準レイアウト型は，ユーザー定義された特殊なメンバー関数を持つことができる．さらに，標準レイアウト型には，次のような特性がある．
- 仮想関数または仮想基底クラスがない
- すべての非静的データ メンバーに同じアクセス制御が含まれている
- クラス型のすべての非静的メンバーが標準レイアウトである
- すべての基底クラスが標準レイアウトである
- 最初の非静的データ メンバーと同じ型の基底クラスがない
- 次のいずれかの条件を満たしている．
  - 最派生クラスに非静的データ メンバーがなく、非静的データ メンバーの基底クラスが 1 つしかない
  - 非静的データ メンバーを含む基底クラスがない

# アーキテクチャ設計

## システム全体構成

@htmlonly
<div class="mermaid">
graph TB
    subgraph "プロセス A"
        PA[アプリケーション A]
        PubA[Publisher A]
    end
    
    subgraph "プロセス B"
        PB[アプリケーション B]
        SubB[Subscriber B]
    end
    
    subgraph "プロセス C"
        PC[アプリケーション C]
        SubC[Subscriber C]
    end
    
    subgraph "共有メモリ領域"
        SM[共有メモリセグメント]
        RB[リングバッファ]
        Meta[メタデータ]
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

## レイヤ構成

@htmlonly
<div class="mermaid">
graph TB
    subgraph "アプリケーション層"
        APP[ユーザアプリケーション]
    end
    
    subgraph "SHM API層"
        PUB[Publisher&lt;T&gt;]
        SUB[Subscriber&lt;T&gt;]
    end
    
    subgraph "共有メモリ管理層"
        SHM[SharedMemory]
        POSIX[SharedMemoryPosix]
        RB[RingBuffer]
    end
    
    subgraph "OS層"
        KERNEL[Linux Kernel]
        SHMFS[/dev/shm ファイルシステム]
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

# 詳細設計

## クラス階層構造

@htmlonly
<div class="mermaid">
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## 共有メモリレイアウト

@htmlonly
<div class="mermaid">
graph TB
    subgraph "共有メモリセグメント"
        subgraph "メタデータ領域"
            MUTEX[pthread_mutex_t]
            COND[pthread_cond_t]
            ESIZE[element_size]
            BUFNUM[buffer_num]
        end
        
        subgraph "タイムスタンプ領域"
            TS0[timestamp[0]]
            TS1[timestamp[1]]
            TS2[timestamp[2]]
            TSN[timestamp[n-1]]
        end
        
        subgraph "データ領域"
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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## データフロー

### Publish処理フロー

@htmlonly
<div class="mermaid">
sequenceDiagram
    participant App as アプリケーション
    participant Pub as Publisher
    participant RB as RingBuffer
    participant SM as SharedMemory
    
    App->>+Pub: publish(data)
    Pub->>+RB: getOldestBufferNum()
    RB-->>-Pub: buffer_index
    
    loop 最大10回リトライ
        Pub->>+RB: allocateBuffer(buffer_index)
        alt バッファ確保成功
            RB-->>-Pub: true
        else バッファ確保失敗
            RB-->>Pub: false
            Note over Pub: 1ms待機
            Pub->>RB: getOldestBufferNum()
            RB-->>Pub: 新しいbuffer_index
        end
    end
    
    Pub->>SM: データをバッファにコピー
    Pub->>RB: setTimestamp_us(current_time, buffer_index)
    Pub->>RB: signal()
    Note over RB: 待機中のSubscriberに通知
    Pub-->>-App: 処理完了
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

### Subscribe処理フロー

@htmlonly
<div class="mermaid">
sequenceDiagram
    participant App as アプリケーション
    participant Sub as Subscriber
    participant RB as RingBuffer
    participant SM as SharedMemory
    
    App->>+Sub: subscribe(&is_success)
    
    alt 共有メモリが切断されている場合
        Sub->>+SM: connect()
        SM-->>-Sub: 接続結果
        alt 接続失敗
            Sub-->>App: (default_value, false)
        end
        Sub->>RB: 新しいRingBufferインスタンス作成
    end
    
    Sub->>+RB: getNewestBufferNum()
    RB-->>-Sub: buffer_index
    
    alt 有効なバッファが見つからない
        Sub-->>App: (前回の値, false)
    else 有効なバッファが見つかった
        Sub->>SM: バッファからデータをコピー
        Sub-->>-App: (data, true)
    end
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

### waitFor処理フロー

@htmlonly
<div class="mermaid">
sequenceDiagram
    participant App as アプリケーション
    participant Sub as Subscriber
    participant RB as RingBuffer
    
    App->>+Sub: waitFor(timeout_usec)
    
    alt 共有メモリが切断されている場合
        Sub->>Sub: 再接続処理
        alt 再接続失敗
            Sub-->>App: false
        end
    end
    
    Sub->>+RB: waitFor(timeout_usec)
    Note over RB: pthread_cond_timedwait で待機
    
    alt タイムアウト前にシグナル受信
        RB-->>-Sub: true
        Sub-->>-App: true
    else タイムアウト
        RB-->>Sub: false
        Sub-->>App: false
    end
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 通信プロトコル

## リングバッファアルゴリズム

### バッファ選択アルゴリズム

@htmlonly
<div class="mermaid">
flowchart TD
    Start([開始]) --> GetOldest[最古のタイムスタンプを持つバッファを特定]
    GetOldest --> TryAlloc{バッファ確保を試行}
    TryAlloc -->|成功| WriteData[データ書き込み]
    TryAlloc -->|失敗| CheckRetry{リトライ回数 < 10?}
    CheckRetry -->|Yes| Wait[1ms待機]
    Wait --> GetOldest
    CheckRetry -->|No| Error[エラー: バッファ確保失敗]
    WriteData --> UpdateTime[タイムスタンプ更新]
    UpdateTime --> Signal[条件変数でシグナル送信]
    Signal --> End([終了])
    Error --> End
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

### データ読み取りアルゴリズム

@htmlonly
<div class="mermaid">
flowchart TD
    Start([開始]) --> CheckConn{共有メモリ接続済み?}
    CheckConn -->|No| Reconnect[再接続試行]
    Reconnect --> ConnSuccess{接続成功?}
    ConnSuccess -->|No| ReturnFail[失敗を返す]
    ConnSuccess -->|Yes| GetNewest
    CheckConn -->|Yes| GetNewest[最新のタイムスタンプを持つバッファを特定]
    GetNewest --> ValidBuffer{有効なバッファ?}
    ValidBuffer -->|No| ReturnOld[前回値と失敗フラグを返す]
    ValidBuffer -->|Yes| CheckExpiry{データが有効期限内?}
    CheckExpiry -->|No| ReturnOld
    CheckExpiry -->|Yes| ReadData[データ読み取り]
    ReadData --> ReturnSuccess[データと成功フラグを返す]
    ReturnFail --> End([終了])
    ReturnOld --> End
    ReturnSuccess --> End
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## 同期機構

### Mutex とCondition Variable

@htmlonly
<div class="mermaid">
stateDiagram-v2
    [*] --> Unlocked : 初期状態
    
    state Publisher {
        Unlocked --> Locked : pthread_mutex_lock
        Locked --> Writing : バッファ確保成功
        Writing --> Unlocked : pthread_mutex_unlock + pthread_cond_signal
        Locked --> Unlocked : バッファ確保失敗 + pthread_mutex_unlock
    }
    
    state Subscriber {
        Unlocked --> Waiting : waitFor() 呼び出し
        Waiting --> Unlocked : タイムアウト
        Waiting --> Processing : シグナル受信
        Processing --> Unlocked : データ読み取り完了
    }
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# パフォーマンス特性

## メモリ使用量

共有メモリセグメントのサイズは以下の式で計算される：

```
total_size = metadata_size + timestamp_array_size + data_array_size

metadata_size = sizeof(pthread_mutex_t) + sizeof(pthread_cond_t) + 
                sizeof(size_t) + sizeof(size_t)

timestamp_array_size = sizeof(uint64_t) * buffer_num

data_array_size = element_size * buffer_num
```

## レイテンシ特性

@htmlonly
<div class="mermaid">
graph LR
    subgraph "レイテンシ構成要素"
        A[アプリケーション処理] --> B[Publisher処理]
        B --> C[Mutex獲得]
        C --> D[メモリコピー]
        D --> E[タイムスタンプ更新]
        E --> F[Signal送信]
        F --> G[Subscriber処理]
        G --> H[アプリケーション処理]
    end
    
    subgraph "典型的な時間"
        T1[アプリ: ~1μs]
        T2[Pub: ~2μs]
        T3[Mutex: ~0.5μs]
        T4[Copy: ~0.1μs]
        T5[Time: ~0.1μs]
        T6[Signal: ~0.5μs]
        T7[Sub: ~2μs]
        T8[アプリ: ~1μs]
    end
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# セキュリティ考慮事項

## アクセス権限

@htmlonly
<div class="mermaid">
graph TB
    subgraph "POSIX権限モデル"
        Owner[所有者]
        Group[グループ]
        Others[その他]
    end
    
    subgraph "権限種別"
        Read[読み取り: S_IRUSR/S_IRGRP/S_IROTH]
        Write[書き込み: S_IWUSR/S_IWGRP/S_IWOTH]
    end
    
    subgraph "デフォルト設定"
        Default["DEFAULT_PERM = 0666<br/>(全ユーザ読み書き可能)"]
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

## データ整合性

@htmlonly
<div class="mermaid">
sequenceDiagram
    participant P1 as Publisher 1
    participant P2 as Publisher 2
    participant Mutex as Mutex
    participant Buffer as SharedBuffer
    participant S as Subscriber
    
    Note over P1,S: 複数Publisherからの同時書き込み
    
    P1->>+Mutex: lock()
    P2->>Mutex: lock() (ブロック)
    Mutex-->>-P1: 獲得成功
    
    P1->>Buffer: データ書き込み
    P1->>Buffer: タイムスタンプ更新
    P1->>+Mutex: unlock() + signal()
    Mutex-->>-P2: 獲得成功
    
    P2->>Buffer: データ書き込み
    P2->>Buffer: タイムスタンプ更新
    P2->>Mutex: unlock() + signal()
    
    S->>Buffer: 最新データ読み取り
    Note over S: P2のデータを取得
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# エラーハンドリング

## エラー分類と対処

@htmlonly
<div class="mermaid">
flowchart TD
    Error([エラー発生]) --> CheckType{エラー種別}
    
    CheckType -->|初期化エラー| InitError[初期化エラー]
    CheckType -->|通信エラー| CommError[通信エラー]
    CheckType -->|メモリエラー| MemError[メモリエラー]
    CheckType -->|タイムアウト| TimeoutError[タイムアウトエラー]
    
    InitError --> InitActions[・名前の確認<br/>・権限の確認<br/>・POD型の確認]
    CommError --> CommActions[・共有メモリ再接続<br/>・Publisher側確認<br/>・プロセス生存確認]
    MemError --> MemActions[・メモリ不足確認<br/>・セグメント削除<br/>・システム再起動]
    TimeoutError --> TimeoutActions[・タイムアウト値調整<br/>・Publisher頻度確認<br/>・システム負荷確認]
    
    InitActions --> LogError[エラーログ出力]
    CommActions --> LogError
    MemActions --> LogError
    TimeoutActions --> LogError
    
    LogError --> Recovery{回復可能?}
    Recovery -->|Yes| Retry[リトライ処理]
    Recovery -->|No| Abort[処理中断]
    
    Retry --> Success{成功?}
    Success -->|Yes| End([正常終了])
    Success -->|No| Recovery
    Abort --> End
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# Python バインディング設計

## Boost.Python ラッパー構造

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
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## Python/C++ データ変換

@htmlonly
<div class="mermaid">
sequenceDiagram
    participant Py as Python App
    participant Boost as Boost.Python
    participant Wrapper as C++ Wrapper
    participant Core as SHM Core
    
    Note over Py,Core: Publish処理
    Py->>+Boost: pub.publish(data)
    Boost->>+Wrapper: _publish(converted_data)
    Note over Boost: Python型 → C++型変換
    Wrapper->>+Core: publish(data)
    Core-->>-Wrapper: void
    Wrapper-->>-Boost: void
    Boost-->>-Py: None
    
    Note over Py,Core: Subscribe処理
    Py->>+Boost: data, success = sub.subscribe()
    Boost->>+Wrapper: _subscribe()
    Wrapper->>+Core: subscribe(&is_success)
    Core-->>-Wrapper: result_data
    Wrapper-->>-Boost: make_tuple(result_data, is_success)
    Note over Boost: C++型 → Python型変換
    Boost-->>-Py: (data, success)
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 拡張性考慮

## 新しいデータ型の追加

@htmlonly
<div class="mermaid">
flowchart TD
    Start([新しい型Tを追加]) --> CheckPOD{POD型?}
    CheckPOD -->|Yes| UseTemplate[既存テンプレートを使用]
    CheckPOD -->|No| Specialize[テンプレート特殊化]
    
    UseTemplate --> InstantiateC[C++でPublisher&lt;T&gt;,<br/>Subscriber&lt;T&gt;をインスタンス化]
    Specialize --> CustomImpl[カスタム実装<br/>・シリアライゼーション<br/>・デシリアライゼーション]
    
    CustomImpl --> InstantiateC
    InstantiateC --> PythonNeeded{Python対応必要?}
    
    PythonNeeded -->|Yes| CreateWrapper[Boost.Pythonラッパー作成<br/>・PublisherT<br/>・SubscriberT]
    PythonNeeded -->|No| TestC[C++テスト実装]
    
    CreateWrapper --> UpdateModule[BOOST_PYTHON_MODULEに追加]
    UpdateModule --> TestPy[Pythonテスト実装]
    TestPy --> TestC
    TestC --> Document[ドキュメント更新]
    Document --> End([完了])
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# 参照
## man shm_overview
Posix共有メモリの概要が記載されているURLを以下に示す．
<https://linuxjm.osdn.jp/html/LDP_man-pages/man7/shm_overview.7.html>

## 関連技術仕様
- POSIX.1-2001 共有メモリオブジェクト
- POSIX.1-2001 pthread mutexとcondition variables
- C++11 標準レイアウト型
- Boost.Python 1.75+
