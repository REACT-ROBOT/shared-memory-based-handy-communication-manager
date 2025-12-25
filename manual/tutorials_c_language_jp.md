# C言語版ライブラリの使用方法

[[English](../md_manual_tutorials_c_language_en.html) | 日本語]

## 概要

本ライブラリはC++版に加えて、C言語版のAPIを提供しています。
C言語版は`shm_base_c`と`shm_pub_sub_c`の2つのライブラリで構成されています。

**重要な特徴**: C言語版はC++版と**メモリレイアウト互換**があります。
これにより、CプログラムとC++プログラム間での相互通信が可能です。

## ライブラリ構成

| ライブラリ | 説明 |
|-----------|------|
| `shm_base_c` | 共有メモリとリングバッファの低レベル操作 |
| `shm_pub_sub_c` | Publisher/Subscriberの高レベルAPI |

## ビルド方法

```bash
cd {ワークスペース}/build
cmake ..
make
```

サンプルプログラムもビルドする場合:
```bash
cmake .. -DBUILD_EXAMPLES=ON
make
```

## 基本的な使用方法

### Publisher (パブリッシャ)

```c
#include "shm_pub_sub_c.h"

typedef struct {
    int32_t value;
    float data;
} MyData;

int main() {
    shm_publisher_t pub;

    // パブリッシャの作成（トピック名、データサイズ、バッファ数）
    if (shm_publisher_create(&pub, "/my_topic", sizeof(MyData), 3) != SHM_SUCCESS) {
        fprintf(stderr, "Failed to create publisher\n");
        return 1;
    }

    // データのパブリッシュ
    MyData data = {42, 3.14f};
    shm_publish(&pub, &data);

    // 後片付け
    shm_publisher_destroy(&pub);
    return 0;
}
```

### Subscriber (サブスクライバ)

```c
#include "shm_pub_sub_c.h"

typedef struct {
    int32_t value;
    float data;
} MyData;

int main() {
    shm_subscriber_t sub;

    // サブスクライバの作成
    if (shm_subscriber_create(&sub, "/my_topic", sizeof(MyData)) != SHM_SUCCESS) {
        fprintf(stderr, "Failed to create subscriber\n");
        return 1;
    }

    // データ有効期限を2秒に設定
    shm_subscriber_set_expiry_time(&sub, 2000000);

    // データの購読
    MyData data;
    bool success;
    int ret = shm_subscribe(&sub, &data, &success);

    if (ret == SHM_SUCCESS && success) {
        printf("Received: value=%d, data=%.2f\n", data.value, data.data);
    }

    // 後片付け
    shm_subscriber_destroy(&sub);
    return 0;
}
```

---

## C++プログラムとの相互通信

C言語版の最大の特徴は、C++版との相互通信が可能なことです。
ただし、いくつかの条件を満たす必要があります。

### 通信可能なデータ構造の条件

C++のクラス/構造体が以下の条件を満たせば、C言語から読み書きできます：

1. **Standard Layout (標準レイアウト)** である
2. **Trivially Copyable (自明コピー可能)** である
3. **固定サイズ**である（動的メモリ割り当てを含まない）

#### 通信可能な例

```cpp
// C++側: シンプルな構造体
struct SensorData {
    int32_t sensor_id;
    float temperature;
    float humidity;
    uint64_t timestamp;
};
```

```c
// C側: 同じレイアウトの構造体
typedef struct {
    int32_t sensor_id;
    float temperature;
    float humidity;
    uint64_t timestamp;
} SensorData;
```

#### 通信不可能な例

```cpp
// NG: 仮想関数を持つクラス
class BadExample1 {
    virtual void update();  // vtableが追加されるためレイアウトが変わる
    int data;
};

// NG: STLコンテナを含む
struct BadExample2 {
    std::string name;       // 内部でポインタを使用
    std::vector<int> data;  // 動的配列
};

// NG: ポインタメンバを含む
struct BadExample3 {
    int* data_ptr;  // アドレスはプロセス間で無効
};

// NG: 参照メンバを含む
struct BadExample4 {
    int& ref;  // 参照もアドレスベース
};
```

### 推奨パターン: 共通ヘッダファイルの使用

C/C++両方から使用できる共通ヘッダファイルを作成することを推奨します。

```c
// common_types.h
#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ロボットの位置姿勢データ
typedef struct {
    // 位置 [mm]
    int32_t x;
    int32_t y;
    int32_t z;

    // 姿勢 [rad * 1000]
    int32_t roll;
    int32_t pitch;
    int32_t yaw;

    // タイムスタンプ [usec]
    uint64_t timestamp;
} RobotPose;

// センサーデータ
typedef struct {
    int32_t sensor_id;
    float values[8];  // 固定長配列
    uint32_t status;
    uint64_t timestamp;
} SensorReading;

// 制御指令
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

### C++ Publisher → C Subscriber の例

**C++ Publisher側**

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
        pose.yaw = 1570;  // 約90度
        pose.timestamp = irlab::shm::getCurrentTimeUSec();

        pub.publish(pose);
        usleep(100000);  // 100ms
    }

    return 0;
}
```

**C Subscriber側**

```c
#include "common_types.h"
#include "shm_pub_sub_c.h"
#include <stdio.h>

int main() {
    shm_subscriber_t sub;
    shm_subscriber_create(&sub, "/robot_pose", sizeof(RobotPose));
    shm_subscriber_set_expiry_time(&sub, 1000000);  // 1秒

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

### C Publisher → C++ Subscriber の例

**C Publisher側**

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

**C++ Subscriber側**

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

## C++クラスをC言語から扱う場合の変換

C++側でリッチなクラスを使用している場合、通信用のPOD構造体に変換します。

```cpp
// C++側の内部クラス（通信には使用しない）
class RobotController {
public:
    void setVelocity(float linear, float angular);
    void update();

    // 通信用データを取得
    ControlCommand getCommandData() const {
        ControlCommand cmd;
        cmd.linear_velocity = linear_vel_;
        cmd.angular_velocity = angular_vel_;
        cmd.flags = status_flags_;
        return cmd;
    }

    // 通信データから設定
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

// Publisher側
irlab::shm::Publisher<ControlCommand> pub("/cmd");
RobotController controller;
controller.setVelocity(1.0f, 0.0f);
pub.publish(controller.getCommandData());  // POD構造体に変換して送信
```

---

## サイズ確認の重要性

通信を開始する前に、必ずデータ構造のサイズが一致していることを確認してください。

**確認用コード（C++側）**

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

**確認用コード（C側）**

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

両方のプログラムで同じサイズが表示されれば、安全に通信できます。

---

## 注意事項

### 1. パディングとアライメント

コンパイラによってパディングが異なる場合があります。
確実に同じレイアウトを保証するには、以下の方法があります：

```c
// 方法1: プラグマでパッキングを指定
#pragma pack(push, 1)
typedef struct {
    int8_t a;
    int32_t b;  // パディングなしで配置
} PackedStruct;
#pragma pack(pop)

// 方法2: 明示的にパディングを入れる
typedef struct {
    int8_t a;
    int8_t padding[3];  // 明示的なパディング
    int32_t b;
} AlignedStruct;
```

### 2. エンディアン

異なるエンディアンのシステム間では正しく通信できません。
（通常、同一マシン上での通信なので問題になりません）

### 3. waitFor機能の代替

C++版にある`waitFor()`関数はC言語版では提供されていません。
代わりにポーリングで実装してください：

```c
// ポーリングによる待機
while (1) {
    bool success;
    if (shm_subscribe(&sub, &data, &success) == SHM_SUCCESS && success) {
        // 新しいデータを受信
        break;
    }
    usleep(1000);  // 1ms待機
}
```

---

## まとめ

C言語版ライブラリを使用することで、以下のユースケースに対応できます：

1. **組み込みシステム**: C言語のみ使用可能な環境でも共有メモリ通信が可能
2. **レガシーコードとの統合**: 既存のC言語コードベースにPublish/Subscribe機能を追加
3. **クロス言語システム**: C++で書かれたメインプログラムとCで書かれたモジュール間の通信
4. **軽量実装**: Boost等の依存なしで動作
