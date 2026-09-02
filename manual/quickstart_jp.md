# 🚀 クイックスタートガイド
[[English](../md_manual_quickstart_en.html) | 日本語]

このガイドでは、5分でプロセス間通信を体験できるように簡単な例を紹介します。

## 📋 前提条件

- Ubuntu 18.04以降 または 互換Linux環境
- C++17対応コンパイラ（g++ 7.0以降）
- CMake 3.10以降

## ⚡ 最速体験：共有メモリ通信

### 1. 環境構築（30秒）

```bash
cd /path/to/shared-memory-based-handy-communication-manager

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j$(nproc)
cmake --install build
```

作られるのは次の 5 つである。**共有ライブラリに `lib` の接頭辞は付かない**
（`PREFIX ""` を指定しているため）。`-lshm_pub_sub` は使えない。

```
$HOME/.local/lib/shm_base.so
$HOME/.local/lib/shm_pub_sub.so
$HOME/.local/include/shm_base.hpp, shm_pub_sub.hpp, shm_pub_sub_vector.hpp
$HOME/.local/bin/shm_tool
```

### 2. 送信プログラムの作成（1分）

`sender.cpp`を作成：

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace irlab::shm;

int main() {
    // "hello_topic"という名前でint型のPublisherを作成
    Publisher<int> pub("hello_topic");
    
    std::cout << "データを送信中..." << std::endl;
    
    for (int i = 0; i < 10; ++i) {
        pub.publish(i);
        std::cout << "送信: " << i << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

### 3. 受信プログラムの作成（1分）

`receiver.cpp`を作成：

```cpp
#include "shm_pub_sub.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace irlab::shm;

int main() {
    // "hello_topic"という名前でint型のSubscriberを作成
    Subscriber<int> sub("hello_topic");
    
    std::cout << "データを待機中..." << std::endl;
    
    while (true) {
        bool state;
        int data = sub.subscribe(&state);
        
        if (state) {
            std::cout << "受信: " << data << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

### 4. コンパイル・実行（1分）

**CMake から使う場合（推奨）**

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app CXX)
set(CMAKE_CXX_STANDARD 17)

# shm_base を先に見つけること。shm_pub_sub のエクスポートは
# ターゲット shm_base を名前で参照しており、find_dependency() を持たない。
# 順序を逆にすると find_package(shm_pub_sub) が失敗する。
find_package(shm_base    REQUIRED)
find_package(shm_pub_sub REQUIRED)

add_executable(sender sender.cpp)
target_link_libraries(sender shm_pub_sub)   # インクルードパスも一緒に付く
```

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=$HOME/.local
cmake --build build
```

**g++ を直接使う場合**

インクルードパスは **2 つ**要る（`shm_pub_sub.hpp` が `shm_base.hpp` を取り込む）。
リンクは `lib` 接頭辞が無いため `-l:` 形式で指定する。

```bash
SHM=$HOME/.local
g++ -std=c++17 -I$SHM/include sender.cpp \
    -L$SHM/lib -l:shm_pub_sub.so -l:shm_base.so -lrt -pthread -o sender
g++ -std=c++17 -I$SHM/include receiver.cpp \
    -L$SHM/lib -l:shm_pub_sub.so -l:shm_base.so -lrt -pthread -o receiver
```

（インストールせずビルドツリーを直接使う場合は
`-I<repo>/shm_base/include -I<repo>/shm_pub_sub/include -L<repo>/build/lib`）

**実行（2 つのターミナルで）**

```bash
export LD_LIBRARY_PATH=$HOME/.local/lib   # 両方のターミナルで

# ターミナル1
./receiver
# ターミナル2
./sender
```

> `shm_pub_sub.so` の `DT_NEEDED` は `shm_base.so` という**素の名前**で、
> `RUNPATH` を持たない。したがって `LD_LIBRARY_PATH` の**先頭にあるものが勝つ**。
> 別のビルドツリーがパスに残っていると、そちらの古い `shm_base.so` を掴む。
> `ldd ./receiver | grep shm` で解決先を必ず確認すること。

> `subscribe()` は**キューではない**。最新のサンプルを返し、次が来るまで
> **同じ値を返し続ける**（既定 2 秒で期限切れ、`setDataExpiryTime_us()` で変更可）。
> 上の受信側が 100ms 周期なら、10 回の送信に対して受信は 10 行にはならない。

### 🎉 結果

受信側で送信されたデータがリアルタイムに表示されます！

```
データを待機中...
受信: 0
受信: 1
受信: 2
...
```

## 🎯 次のステップ

体験できましたか？次は以下を試してみましょう：

1. **[📝 基本チュートリアル](tutorials_jp.md)** - より詳しい使い方を学ぶ
2. **[📡 Pub/Sub通信](tutorials_shm_pub_sub_jp.md)** - 超高速ブロードキャストを極める
3. **[🐍 Python版](tutorials_python_jp.md)** - Pythonでも使ってみる

## 🐛 うまくいかない場合

### よくあるエラー

**コンパイルエラー**: ヘッダーファイルが見つからない
```bash
# インストール済みなら 1 か所にまとまっている
ls $HOME/.local/include/
# shm_base.hpp  shm_pub_sub.hpp  shm_pub_sub_vector.hpp

# ビルドツリーを直接使う場合は 2 か所に分かれているので -I が 2 つ要る
ls shm_base/include shm_pub_sub/include
```

**リンクエラー**: `cannot find -lshm_pub_sub`
```bash
# lib の接頭辞は付かない。-l: 形式で指定すること。
ls $HOME/.local/lib/     # shm_base.so  shm_pub_sub.so
g++ ... -L$HOME/.local/lib -l:shm_pub_sub.so -l:shm_base.so -lrt -pthread
```

**実行エラー**: ライブラリが見つからない / undefined symbol
```bash
export LD_LIBRARY_PATH=$HOME/.local/lib:$LD_LIBRARY_PATH

# どの実装を掴んでいるか必ず確認する。古いビルドツリーが
# LD_LIBRARY_PATH に残っていると、そちらの shm_base.so が勝つ。
ldd ./your_program | grep shm
```

**通信できない**: 
- 共有メモリ: 同じトピック名を使用しているか確認
- Publisher と Subscriber で同じ型を使用しているか確認

### さらなるヘルプ

- **[🐛 トラブルシューティング](troubleshooting_jp.md)** - 詳細な解決方法
- **[📚 サンプルコード](https://github.com/REACT-ROBOT/shared-memory-based-handy-communication-manager/tree/main/shm_pub_sub/samples)** - リポジトリ内の動作確認済みの例

---

**準備完了！** さあ、本格的にプロセス間通信をマスターしましょう！