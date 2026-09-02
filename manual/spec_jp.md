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
        PUB["Publisher"]
        SUB["Subscriber"]
    end
    
    subgraph "共有メモリ管理層"
        SHM[SharedMemory]
        POSIX[SharedMemoryPosix]
        RB[RingBuffer]
    end
    
    subgraph "OS層"
        KERNEL["Linux Kernel"]
        SHMFS["/dev/shm ファイルシステム"]
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

## 共有メモリレイアウト

@htmlonly
<div class="mermaid">
graph TB
    subgraph "root セグメント /shm_&lt;topic&gt;（世代 1・ディレクトリ兼用）"
        HDR["ShmHeader (192 B)<br/>magic / abi_major / total_size<br/>element_capacity / buf_num / payload_alignment<br/>slot_offset / slot_size / data_offset<br/>generation / sequence / boot_id_hash<br/><b>latest_generation</b> (世代16bit + ノンス48bit)<br/>element_size / schema_id / payload_kind<br/>schema_version / segment_nonce"]
        subgraph "SlotRecord[] (1 スロット 128 B・キャッシュライン境界)"
            SR0["sequence (atomic)<br/>payload_size (atomic)<br/>capture_monotonic_us (atomic)<br/>capture_realtime_us (atomic)<br/>owner (robust mutex)"]
            SRN["… buf_num 個"]
        end
        subgraph "ペイロード領域 (payload_alignment 境界)"
            D0["slot 0"]
            DN["… buf_num 個"]
        end
    end

    subgraph "世代セグメント /shm_&lt;topic&gt;#&lt;世代&gt;-&lt;ノンス&gt;"
        G["同じ構成。レイアウトを変えるときは<br/>既存セグメントを作り直さず<br/><b>新しい世代を別セグメントとして作る</b>"]
    end

    HDR --> SR0
    SR0 --> SRN
    SRN --> D0
    D0 --> DN
    HDR -.->|latest_generation が指す| G
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
    Note over RB: 発行番号の変化を短い間隔で見に行く（条件変数は使わない）
    
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

### バッファ選択アルゴリズム（writer）

**古い順に全スロットを試す**のが要点である。1 つのスロットだけを狙うと、
そこに reader が張り付いている間、他が空いていても publish が失敗する。

@htmlonly
<div class="mermaid">
flowchart TD
    Start([publish]) --> Ensure[ensureCapacity: 要求を満たす世代へ接続<br/>足りなければ新しい世代を作る]
    Ensure --> Tag[世代タグを控える]
    Tag --> Scan[発行番号の小さい順に未試行のスロットを選ぶ]
    Scan --> Try{trylock<br/>（1 巡目は待たない）}
    Try -->|成功| Write[payload を書く]
    Try -->|失敗| More{未試行のスロットが残っている?}
    More -->|Yes| Scan
    More -->|No| Wait[最も古いスロットだけ 2ms 待つ]
    Wait --> Try2{確保できた?}
    Try2 -->|Yes| Write
    Try2 -->|No| Retry{再試行 < 3?}
    Retry -->|Yes| Sleep[1ms 待つ] --> Scan
    Retry -->|No| Error[失敗を通知]
    Write --> Commit[commitBuffer: root のカウンタから<br/>発行番号を採番して解放]
    Commit --> Check{世代タグは変わっていない?}
    Check -->|変わっていない| End([完了])
    Check -->|変わった| Republish[新しい世代へ発行し直す<br/>capture 時刻は引き継ぐ]
    Republish --> Ensure
    Error --> End
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

### データ読み取りアルゴリズム（reader）

@htmlonly
<div class="mermaid">
flowchart TD
    Start([subscribe]) --> Follow[follow: 現在有効な世代へ追随<br/>書式が食い違えば payload に触れず失敗]
    Follow --> Pick[発行番号が最大のスロットを選ぶ]
    Pick --> Valid{有効なスロットがある?}
    Valid -->|無い| Why{採番カウンタ == 0?}
    Why -->|Yes| Empty[Empty: まだ publish されていない]
    Why -->|No| Contended[Contended: 今は読めないだけ<br/>再試行の価値がある]
    Valid -->|ある| Expiry{有効期限内?}
    Expiry -->|No| Old[前回値と失敗フラグを返す]
    Expiry -->|Yes| Read[readSample: スロットを排他して<br/>payload と素性を 1 回で読む]
    Read --> Ok{読めた?}
    Ok -->|Yes| Success[データと成功フラグを返す]
    Ok -->|No| Retry{再試行 < 5?}
    Retry -->|Yes| Pick
    Retry -->|No| Old
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

## 同期機構

### スロット単位の robust mutex（条件変数は使わない）

初期の形式は「セグメント全体を 1 つの mutex で守り、書き込み完了を条件変数で
知らせる」構成だった。現在の形式は次の理由でどちらも採っていない。

- **セグメント全体の mutex は publisher 同士を直列化する。** 現在はスロットごとに
  mutex を持ち、writer は空いているスロットを 1 つ確保するだけで済む。
- **条件変数は所有者が死ぬと復旧できない。** `RingBuffer::signal()` は現在
  何もしない空関数で、`waitFor()` は発行番号の変化を短い間隔で見に行く。
  これにより「待っている購読者が居るまま publisher が死ぬ」状況でも固まらない。

mutex には `PTHREAD_PROCESS_SHARED` に加えて次を設定している。

| 属性 | 目的 |
|---|---|
| `PTHREAD_MUTEX_ROBUST` | 所有者プロセスの死をカーネルが検出し、次に lock した者へ `EOWNERDEAD` を返す。**時刻で生死を推測しないため**に必須 |
| `PTHREAD_PRIO_INHERIT` | SCHED_FIFO の制御ループが、低優先度のプロセスが握ったスロットを待つときの優先度逆転を避ける |

### バッファ数は同時参加者数より多くすること

reader もスロットを排他するので、**スロット数は「同時に読み書きする参加者の数」より
多くなければならない**。`buffer_num = 1` は writer と reader が唯一のスロットを
奪い合う構成で、CPU が過負荷になると writer が確保できずに publish が失敗し得る
（黙って壊すのではなく例外で知らせる）。

既定は 3 である。必要な数の目安は

```
buffer_num >= ceil(発行レート × 必要な履歴長) + 同時 writer 数 + 余裕(2 程度)
```

で、前半の項は「どれだけ過去まで遡れる必要があるか」から決まる。
実際に保持できている時間幅は `shm_tool doctor` の「履歴」列で確認できる。

**reader もこの mutex を取る。** 発行番号の前後比較だけでは writer の `memcpy` と
reader の `memcpy` が同じ通常メモリへ同時アクセスし得て、C++ のメモリモデル上は
data race（未定義動作）になるためである。臨界区間は `memcpy` 1 回ぶんしかない。

@htmlonly
<div class="mermaid">
stateDiagram-v2
    [*] --> Free : 初期状態

    state "writer" as W {
        Free --> Owned : acquireWritableSlot()<br/>古い順に全スロットを trylock
        Owned --> Free : commitBuffer()<br/>発行番号を採番して解放
        Owned --> Free : releaseBuffer()<br/>書けなかった場合
    }

    state "reader" as R {
        Free --> Reading : readSample()<br/>trylock（上限 2ms まで待つ）
        Reading --> Free : payload と素性を 1 回で読み終える
    }

    state "所有者が死んだ場合" as D {
        Owned --> Recovered : EOWNERDEAD
        Recovered --> Free : pthread_mutex_consistent()
    }
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

# レイアウト世代

## 稼働中のセグメントは決して作り直さない

vector や画像のように長さが変わるトピックでは、必要な容量が publish のたびに変わる。
初期の形式は「足りなくなったら共有メモリを破棄して作り直す」方式だったが、
これには 2 つの実害があった。

- 作り直しと他プロセスのアクセスの間に窓が空き、範囲外アクセスになり得た
- 作り直しに気づかなかった publisher が、誰にも読まれない領域へ publish し続けた

現在はレイアウトを変えるとき、**既存セグメントには一切触れず、新しい世代を
別のセグメントとして作る**。古い世代を掴んだままの参加者もマッピングは有効なので、
範囲外アクセスにならない。

## セグメント名

```
世代 1      : /shm_<topic>                       root。データ本体とディレクトリを兼ねる
世代 N >= 2 : /shm_<topic>#<N>-<ノンス16進12桁>
```

世代 2 以降の名前に**ノンスを含める**のが要点である。固定名 `#N` だと、作成途中で
死んだプロセスの残骸が名前を占有し、以後 `O_EXCL` が必ず失敗して容量拡張が
できなくなる。「一定時間待って未初期化なら消す」方式は、単に遅い、あるいは
SIGSTOP で止められているだけの**生きた作成者を消してしまう**。名前にノンスを
入れれば名前の取り合い自体が起きないので、時間で生死を判定する必要がなくなる。

## 切り替え

現在有効な世代は root ヘッダの `latest_generation` が正本で、
**世代番号（上位 16bit）とノンス（下位 48bit）を 1 語に詰めてある**。
別々のフィールドにすると「新しい世代番号と古いノンス」の組を読み得るため、
1 回の CAS で不可分に公開する。

1. 新しいセグメントを `O_EXCL` で作る（ノンス付きなので名前の衝突が起きない）
2. 初期化し、旧世代の履歴を**発行番号と capture 時刻をそのまま**引き継ぐ
3. root の世代タグを CAS で進める。ここで初めて他プロセスから見える
4. 現役でないと確定できるセグメントだけを片付ける

4 の判定は時刻を見ない。消してよいのは「現世代より古い世代」と
「現世代と同じ番号でノンスが違うもの（切り替え競争に負けた残骸）」だけで、
**より新しい世代番号のものは作成中かもしれないので絶対に触らない**。

## 切り替えを跨いだ publish

publish はスロットを確保する前に世代タグを控え、commit 後に確認する。
変わっていれば新しい世代へ発行し直す。このとき **capture 時刻は引き継ぐ**。
採り直すと、同じ測定が別時刻に起きたように見えてしまう。

# ペイロード書式の宣言

## なぜ必要か

`element_size`（`sizeof`）の照合は「同サイズのままメンバを並べ替えた」変更を
通してしまう。再デプロイ後に古いプロセスが生き残っている場合や、古い
セグメントが残っている場合に、別物を同じ形式として読み合うことになる。

## 書き方

```cpp
struct LidarScan { uint32_t count; float ranges[1081]; };

// メンバを並べるだけ。版番号は書かない。
SHM_DECLARE_LAYOUT(LidarScan, count, ranges);
```

`sizeof(T)` / `alignof(T)` と各メンバの `offsetof` / `sizeof` から
`constexpr` のハッシュを作る。並べ替え・型入れ替え・追加・削除・
アライメント変更がすべてここに出るので、**版番号を維持する判断が要らない**。

メンバを書き漏らしたり、宣言順と違う順で並べたりすると
**コンパイルエラーになる**（並べたメンバが型を隙間なく覆っているかを検査する）。
唯一検出できないのは、書き漏らしたメンバが「どのみち必要なパディング」に
ちょうど収まる場合だけである。

| 用途 | マクロ |
|---|---|
| POD ペイロード | `SHM_DECLARE_LAYOUT(T, member...)` |
| レイアウトは同じで意味だけ変えた（単位の変更など） | `SHM_DECLARE_LAYOUT_REV(T, revision, member...)` |
| ワイヤ形式が `serialize()` の実装で決まる型 | `SHM_DECLARE_SERIALIZED_FORMAT(T, revision)` |

`-DSHM_REQUIRE_LAYOUT=ON` でビルドすると、宣言していない型を publish /
subscribe した時点でコンパイルエラーになる（既定 OFF。移行のため）。

# 時刻指定の読み出し（タイムマシン）

「オドメトリの更新に最も時刻の近いスキャンを取る」ための API である。

```cpp
SampleInfo odom_info;
bool ok = false;
odom_sub.subscribe(&ok, &odom_info);

SearchStatus status;
const Scan &scan = scan_sub.subscribeAlignedTo(odom_info, &status, nullptr, 50000);
```

| 型 | 意味 |
|---|---|
| `TimeQuery{time_us, policy}` | 検索する時刻と方針 |
| `SearchPolicy` | `Nearest` / `AtOrBefore` / `AtOrAfter` |
| `SearchStatus` | `Success` / `NotConnected` / `Empty` / `TooOld` / `TooNew` / `Contended` |
| `SampleInfo` | 発行番号・capture 時刻・payload 長 |
| `RetentionWindow` | 現在保持している履歴の範囲 |

**検索に使う時刻は `CLOCK_MONOTONIC_RAW` だけ**である。壁時計（`CLOCK_REALTIME`）も
記録はするが検索には使わない。NTP の補正で前後に飛ぶため、基準にできない。

`Contended` は「今は読めないだけ」なので再試行の価値がある。
`Empty`（まだ publish されていない）と混同しないこと。

**`Nearest` は有効なサンプルが 1 つでもあれば必ず「最も近いもの」を返す。**
どれだけ離れていても `TooOld` / `TooNew` にはならない。ずれの上限は
`subscribeAlignedTo()` の `max_skew_us` で示すこと（**必須引数**）。
`subscribe()` が失敗したときの `SampleInfo` は全ゼロなので、それを基準に渡すと
時刻 0 に対する検索になる。これは `InvalidReference` で弾かれる。

保持できる履歴の長さは `buf_num ÷ 発行レート` である。10Hz のセンサを
`buf_num = 3` で持つと履歴は 300ms しかない。実際に保持している時間幅は
`getRetentionWindow()` か `shm_tool doctor` で確認できる。

# 運用

## shm_tool

| コマンド | 用途 |
|---|---|
| `shm_tool list` | `/dev/shm` の一覧 |
| `shm_tool remove <topic>` | トピックを全世代まとめて削除 |
| `shm_tool doctor [topic]` | ヘッダを読んで、そのまま使えるかを報告する |

`doctor` は ABI・世代・書式・buf_num・実際に保持している履歴長を表示し、
対処が必要なものに ★ を付ける（終了コード 1）。

- 未初期化 / 別形式 / ABI 不一致 / 再起動前の残骸
- 名前とヘッダのノンスが食い違う
- 古い世代の残骸 / 切り替え競争に負けた残骸

「書式が未宣言」は動作はしているので注記に留める（終了コード 0）。

## 版を上げたときの移行

共有メモリの形式が非互換に変わった場合は、**入れ替え前に
`shm_tool remove` で既存のセグメントを消し、そのトピックを使う全プロセスを
同時に入れ替える**こと。古いセグメントが残っていると、
接続時に理由と復旧手順を書いた例外で失敗する（待たされることはない）。

# パフォーマンス特性

## メモリ使用量

共有メモリセグメントのサイズは以下の式で計算される：

```
total_size = sizeof(ShmHeader)                 // 192 B 固定
           + sizeof(SlotRecord) * buffer_num   // 1 スロット 128 B
           + padding_to(payload_alignment)
           + element_capacity * buffer_num

// element_capacity は「増やすだけ」で運用するため、
// 実際に publish した長さより大きいことがある。
// 実際の長さは各スロットの payload_size に入っている。
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
        Read["読み取り: S_IRUSR/S_IRGRP/S_IROTH"]
        Write["書き込み: S_IWUSR/S_IWGRP/S_IWOTH"]
    end
    
    subgraph "デフォルト設定"
        Default["DEFAULT_PERM = 0660<br/>(所有者とグループのみ)<br/>PERM_ALL = 0666 を明示すれば従来どおり"]
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

同時に書く publisher はスロット単位で分かれるので直列化しない。
整合性の判定に使うのは**時刻ではなく発行番号**である。発行番号は
root セグメントの単一の atomic から `fetch_add` で採るので、
トピック全体で一意であり、再利用されない。

@htmlonly
<div class="mermaid">
sequenceDiagram
    participant P1 as Publisher 1
    participant P2 as Publisher 2
    participant S0 as slot 0
    participant S1 as slot 1
    participant Root as root の採番カウンタ
    participant S as Subscriber

    Note over P1,S: 複数 Publisher は別々のスロットへ同時に書ける

    P1->>S0: trylock 成功（sequence を 0 にする）
    P2->>S1: trylock 成功（別スロットなので待たない）

    P1->>S0: payload を書く
    P2->>S1: payload を書く

    P1->>Root: fetch_add → 発行番号 N
    P1->>S0: commit（N と capture 時刻を書いて unlock）
    P2->>Root: fetch_add → 発行番号 N+1
    P2->>S1: commit（N+1 と capture 時刻を書いて unlock）

    S->>S1: 発行番号が最大のスロットを選ぶ
    S->>S1: trylock して payload と素性を 1 回で読む
    Note over S: P2 のデータを取得（発行番号 N+1）
</div>
<script type="module">
  import mermaid from 'https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.esm.min.mjs';
  mermaid.initialize({ startOnLoad: true });
</script>
@endhtmlonly

**時刻を使わない理由**: 同じマイクロ秒に 2 つの publish が起きると時刻では
前後が決まらず、リングが一周して同じスロットが再確保されたときに
「前後の値が一致するが中身は別物」という ABA が起きる。発行番号は
単調増加で再利用されないので、これが原理的に起きない。

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
    
    InitError --> InitActions["・名前の確認<br/>・権限の確認<br/>・POD型の確認"]
    CommError --> CommActions["・共有メモリ再接続<br/>・Publisher側確認<br/>・プロセス生存確認"]
    MemError --> MemActions["・メモリ不足確認<br/>・セグメント削除<br/>・システム再起動"]
    TimeoutError --> TimeoutActions["・タイムアウト値調整<br/>・Publisher頻度確認<br/>・システム負荷確認"]
    
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
    
    UseTemplate --> InstantiateC["C++でPublisherT,<br/>SubscriberTをインスタンス化"]
    Specialize --> CustomImpl["カスタム実装<br/>・シリアライゼーション<br/>・デシリアライゼーション"]
    
    CustomImpl --> InstantiateC
    InstantiateC --> PythonNeeded{Python対応必要?}
    
    PythonNeeded -->|Yes| CreateWrapper["Boost.Pythonラッパー作成<br/>・PublisherT<br/>・SubscriberT"]
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
