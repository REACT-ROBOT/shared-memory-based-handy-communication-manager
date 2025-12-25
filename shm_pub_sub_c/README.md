# shm_pub_sub_c (C言語版 共有メモリ Publish/Subscribe ライブラリ)

## 概要 / About

C言語版の共有メモリベースPublish/Subscribeライブラリです。
C++版の`shm_pub_sub`と**メモリレイアウト互換**があり、C言語プログラムとC++プログラム間で通信が可能です。

This is a C language version of the shared memory based Publish/Subscribe library.
It has **memory layout compatibility** with the C++ version `shm_pub_sub`, enabling communication between C and C++ programs.

## 特徴 / Features

- **C++互換メモリレイアウト**: C++版と同じリングバッファ構造を使用
- **クロス言語通信**: C Publisher → C++ Subscriber、C++ Publisher → C Subscriber が可能
- **シンプルなAPI**: C言語らしい関数ベースのインターフェース
- **軽量実装**: Boost等の依存なし（条件変数待機機能は簡略化）

## ビルド方法 / How to Build

```bash
cd {ワークスペース}/build
cmake .. -DBUILD_EXAMPLES=ON
make
```

## 使用方法 / How to Use

### Publisher (パブリッシャ)

```c
#include "shm_pub_sub_c.h"

// データ構造の定義
typedef struct {
    int32_t counter;
    float value;
    char message[64];
} my_data_t;

int main() {
    shm_publisher_t pub;

    // パブリッシャの作成
    int ret = shm_publisher_create(&pub, "/my_topic", sizeof(my_data_t), 3);
    if (ret != SHM_SUCCESS) {
        return 1;
    }

    // データのパブリッシュ
    my_data_t data = {.counter = 1, .value = 3.14f};
    shm_publish(&pub, &data);

    // クリーンアップ
    shm_publisher_destroy(&pub);
    return 0;
}
```

### Subscriber (サブスクライバ)

```c
#include "shm_pub_sub_c.h"

typedef struct {
    int32_t counter;
    float value;
    char message[64];
} my_data_t;

int main() {
    shm_subscriber_t sub;

    // サブスクライバの作成
    int ret = shm_subscriber_create(&sub, "/my_topic", sizeof(my_data_t));
    if (ret != SHM_SUCCESS) {
        return 1;
    }

    // データ有効期限の設定（2秒）
    shm_subscriber_set_expiry_time(&sub, 2000000);

    // データのサブスクライブ
    my_data_t data;
    bool success;
    ret = shm_subscribe(&sub, &data, &success);
    if (ret == SHM_SUCCESS && success) {
        printf("Received: counter=%d, value=%.2f\n", data.counter, data.value);
    }

    // クリーンアップ
    shm_subscriber_destroy(&sub);
    return 0;
}
```

### 便利なマクロ

```c
// 型を指定してPublisher/Subscriberを作成
shm_publisher_t pub;
SHM_PUBLISHER_CREATE(pub, "/topic", my_data_t);
SHM_PUBLISH(pub, &data);

shm_subscriber_t sub;
SHM_SUBSCRIBER_CREATE(sub, "/topic", my_data_t);
SHM_SUBSCRIBE(sub, &data, &success);
```

---

## C++との相互通信 / Interoperability with C++

### 重要: データ構造の互換性

C言語とC++間で通信するには、**同じメモリレイアウトを持つデータ構造**を使用する必要があります。

#### 通信可能な条件

1. **Standard Layout (標準レイアウト)** であること
2. **Trivially Copyable (自明にコピー可能)** であること
3. **同じサイズとアライメント**を持つこと

#### 通信可能な例

**C++側 (Publisher)**
```cpp
// C++のクラスでも、メンバがPOD型のみで仮想関数がなければOK
struct SensorData {
    int32_t id;
    float temperature;
    float humidity;
    uint64_t timestamp;
};

// または明示的にC互換を保証
#pragma pack(push, 1)  // パディングを制御する場合
struct PackedData {
    int32_t x;
    int32_t y;
};
#pragma pack(pop)

irlab::shm::Publisher<SensorData> pub("/sensor_data");
SensorData data = {1, 25.5f, 60.0f, 12345678};
pub.publish(data);
```

**C側 (Subscriber)**
```c
// C++側と完全に同じレイアウトの構造体
typedef struct {
    int32_t id;
    float temperature;
    float humidity;
    uint64_t timestamp;
} SensorData;

shm_subscriber_t sub;
shm_subscriber_create(&sub, "/sensor_data", sizeof(SensorData));

SensorData data;
bool success;
shm_subscribe(&sub, &data, &success);
if (success) {
    printf("ID: %d, Temp: %.1f, Humidity: %.1f\n",
           data.id, data.temperature, data.humidity);
}
```

#### 通信不可能な例

```cpp
// NG: 仮想関数を持つクラス（vtableがあるためメモリレイアウトが異なる）
class BadExample1 {
    virtual void update();  // 仮想関数
    int data;
};

// NG: std::stringなどの動的メモリを使用する型
struct BadExample2 {
    std::string name;  // 内部でポインタを持つ
    std::vector<int> values;  // 動的配列
};

// NG: ポインタメンバ（アドレスはプロセス間で共有できない）
struct BadExample3 {
    int* data_ptr;
    char* name;
};
```

### C++クラスとの連携パターン

#### パターン1: シンプルな構造体（推奨）

最もシンプルで安全な方法です。

```cpp
// shared_types.h - C/C++共通ヘッダ
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
} Position;

typedef struct {
    float roll;
    float pitch;
    float yaw;
} Orientation;

typedef struct {
    Position position;
    Orientation orientation;
    uint64_t timestamp_us;
} RobotPose;

#ifdef __cplusplus
}
#endif
```

```cpp
// C++ Publisher
#include "shared_types.h"
#include <shm_pub_sub.hpp>

irlab::shm::Publisher<RobotPose> pub("/robot_pose");
RobotPose pose = {{100, 200, 0}, {0.0f, 0.0f, 1.57f}, getCurrentTime()};
pub.publish(pose);
```

```c
// C Subscriber
#include "shared_types.h"
#include "shm_pub_sub_c.h"

shm_subscriber_t sub;
shm_subscriber_create(&sub, "/robot_pose", sizeof(RobotPose));

RobotPose pose;
bool success;
shm_subscribe(&sub, &pose, &success);
```

#### パターン2: C++クラスをPOD構造体でラップ

C++側でクラスを使いたい場合、通信用のPOD構造体に変換します。

```cpp
// C++側のリッチなクラス
class RobotState {
public:
    void updateVelocity(float v, float omega);
    float getSpeed() const;

private:
    float velocity_;
    float angular_velocity_;
    int32_t mode_;
};

// 通信用のPOD構造体
struct RobotStateData {
    float velocity;
    float angular_velocity;
    int32_t mode;
};

// 変換関数
RobotStateData toData(const RobotState& state) {
    return {state.velocity_, state.angular_velocity_, state.mode_};
}

// Publisher
irlab::shm::Publisher<RobotStateData> pub("/robot_state");
RobotState state;
state.updateVelocity(1.0f, 0.5f);
pub.publish(toData(state));
```

#### パターン3: 固定長配列を含む構造体

```cpp
// C/C++共通
#define MAX_POINTS 100

typedef struct {
    int32_t count;
    struct {
        float x;
        float y;
    } points[MAX_POINTS];
} PointCloud;
```

### サイズとアライメントの確認

通信前にデータ構造のサイズが一致していることを確認してください。

**C++側**
```cpp
#include <iostream>
std::cout << "sizeof(MyData) = " << sizeof(MyData) << std::endl;
std::cout << "alignof(MyData) = " << alignof(MyData) << std::endl;
```

**C側**
```c
#include <stdio.h>
#include <stdalign.h>
printf("sizeof(MyData) = %zu\n", sizeof(MyData));
printf("alignof(MyData) = %zu\n", alignof(MyData));
```

両方の出力が同じであれば通信可能です。

---

## API リファレンス / API Reference

### エラーコード

| 値 | 名前 | 説明 |
|---|------|------|
| 0 | `SHM_SUCCESS` | 成功 |
| -1 | `SHM_ERROR_INVALID_ARG` | 不正な引数 |
| -2 | `SHM_ERROR_SHM_OPEN` | 共有メモリのオープン失敗 |
| -3 | `SHM_ERROR_MMAP` | mmapの失敗 |
| -4 | `SHM_ERROR_FTRUNCATE` | ftruncateの失敗 |
| -5 | `SHM_ERROR_NOT_CONNECTED` | 未接続 |
| -6 | `SHM_ERROR_DATA_EXPIRED` | データ期限切れ |
| -7 | `SHM_ERROR_NO_DATA` | データなし |

### Publisher関数

```c
int shm_publisher_create(shm_publisher_t* pub, const char* name,
                         size_t data_size, int buffer_num);
void shm_publisher_destroy(shm_publisher_t* pub);
int shm_publish(shm_publisher_t* pub, const void* data);
```

### Subscriber関数

```c
int shm_subscriber_create(shm_subscriber_t* sub, const char* name, size_t data_size);
void shm_subscriber_destroy(shm_subscriber_t* sub);
void shm_subscriber_set_expiry_time(shm_subscriber_t* sub, uint64_t time_us);
int shm_subscribe(shm_subscriber_t* sub, void* data, bool* is_success);
bool shm_subscriber_is_connected(const shm_subscriber_t* sub);
int shm_subscriber_connect(shm_subscriber_t* sub);
uint64_t shm_subscriber_get_timestamp(const shm_subscriber_t* sub);
```

### ユーティリティ関数

```c
uint64_t shm_get_current_time_usec(void);  // 現在時刻（マイクロ秒）
int shm_unlink_by_name(const char* name);  // 共有メモリの削除
```

---

## サンプルの実行 / Running Samples

```bash
# ターミナル1: Publisher
./sample_c_publisher /test_topic

# ターミナル2: Subscriber
./sample_c_subscriber /test_topic
```

### C++ Publisher + C Subscriber のテスト

```bash
# ターミナル1: C++ Publisher (shm_pub_sub_sampleを使用)
./shm_pub_sub_sample -w

# ターミナル2: C Subscriber (データ構造を合わせる必要あり)
./sample_c_subscriber
```

---

## 制限事項 / Limitations

1. **waitFor機能なし**: C++版にある条件変数を使った待機機能(`waitFor`)は実装されていません。ポーリングで代替してください。

2. **可変長データ非対応**: `std::vector`のような可変長データは通信できません。固定長配列を使用してください。

3. **エンディアン**: 異なるエンディアンのシステム間での通信は保証されません。

---

## ライセンス / License

このライブラリはshared-memory-based-handy-communication-managerプロジェクトの一部です。
