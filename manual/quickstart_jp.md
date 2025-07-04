# 🚀 クイックスタートガイド
[[English](quickstart_en.md) | 日本語]

このガイドでは、5分でプロセス間通信を体験できるように簡単な例を紹介します。

## 📋 前提条件

- Ubuntu 18.04以降 または 互換Linux環境
- C++17対応コンパイラ（g++ 7.0以降）
- CMake 3.10以降

## ⚡ 最速体験：共有メモリ通信

### 1. 環境構築（30秒）

```bash
# プロジェクトディレクトリに移動
cd /path/to/shared-memory-based-handy-communication-manager

# ビルド
mkdir build && cd build
cmake ..
make
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

```bash
# コンパイル
g++ -std=c++17 -I../include sender.cpp -L. -lshm_pub_sub -o sender
g++ -std=c++17 -I../include receiver.cpp -L. -lshm_pub_sub -o receiver

# 実行（2つのターミナルで）
# ターミナル1
./receiver

# ターミナル2
./sender
```

### 🎉 結果

受信側で送信されたデータがリアルタイムに表示されます！

```
データを待機中...
受信: 0
受信: 1
受信: 2
...
```

## 🤝 Service通信の体験

次は、確実な要求応答通信を体験してみましょう：

### サーバープログラム（Service版）

```cpp
#include "shm_service.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace irlab::shm;

int main() {
    // intを受け取ってintを返すServiceサーバー
    ServiceServer<int, int> server("calc_service");
    
    std::cout << "計算サービスを開始..." << std::endl;
    
    while (true) {
        if (server.hasRequest()) {
            int request = server.getRequest();
            std::cout << "要求受信: " << request << std::endl;
            
            // 2倍にして返す
            int response = request * 2;
            server.sendResponse(response);
            std::cout << "応答送信: " << response << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    return 0;
}
```

### クライアントプログラム（Service版）

```cpp
#include "shm_service.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace irlab::shm;

int main() {
    // intを送ってintを受け取るServiceクライアント
    ServiceClient<int, int> client("calc_service");
    
    std::cout << "計算サービスにアクセス中..." << std::endl;
    
    for (int i = 1; i <= 5; ++i) {
        // 要求送信
        client.sendRequest(i);
        std::cout << "要求送信: " << i << std::endl;
        
        // 応答待機（1秒タイムアウト）
        if (client.waitForResponse(1000000)) {  // 1,000,000マイクロ秒 = 1秒
            int result = client.getResponse();
            std::cout << "結果受信: " << i << " x 2 = " << result << std::endl;
        } else {
            std::cout << "タイムアウト" << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

### コンパイル・実行（Service版）

```bash
# コンパイル
g++ -std=c++17 -I../include service_server.cpp -L. -lshm_service -o service_server
g++ -std=c++17 -I../include service_client.cpp -L. -lshm_service -o service_client

# 実行（2つのターミナルで）
# ターミナル1: サーバーを先に起動
./service_server

# ターミナル2: クライアントを後から起動
./service_client
```

## 🔍 違いを理解しよう

| 特徴 | Pub/Sub (shm_pub_sub) | Service (shm_service) |
|------|----------------------|----------------------|
| **通信パターン** | 📡 一対多ブロードキャスト | 🤝 一対一要求応答 |
| **速度** | ⚡ 超高速（~1μs） | 🚀 高速（~2-5μs） |
| **信頼性** | 📦 ベストエフォート | 🔒 確実な送受信 |
| **用途** | リアルタイムデータ配信 | 設定値取得・計算結果 |
| **同期/非同期** | 非同期 | 同期（ブロッキング） |

## 🎯 次のステップ

体験できましたか？次は以下を試してみましょう：

1. **[📝 基本チュートリアル](tutorials_jp.md)** - より詳しい使い方を学ぶ
2. **[📡 Pub/Sub通信](tutorials_shm_pub_sub_jp.md)** - 超高速ブロードキャストを極める
3. **[🤝 Service通信](tutorials_shm_service_jp.md)** - 確実な要求応答をマスター
4. **[⚡ Action通信](tutorials_shm_action_jp.md)** - 非同期処理をマスター
5. **[🐍 Python版](tutorials_python_jp.md)** - Pythonでも使ってみる

## 🐛 うまくいかない場合

### よくあるエラー

**コンパイルエラー**: ヘッダーファイルが見つからない
```bash
# includeパスを確認
ls ../include/
# shm_pub_sub.hpp や udp_comm.hpp があることを確認
```

**実行エラー**: ライブラリが見つからない
```bash
# ライブラリファイルを確認
ls *.so
# libshm_pub_sub.so や libudp_comm.so があることを確認

# LD_LIBRARY_PATHを設定
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
```

**通信できない**: 
- 共有メモリ: 同じトピック名を使用しているか確認
- Service: サーバーを先に起動してからクライアントを起動

### さらなるヘルプ

- **[🐛 トラブルシューティング](troubleshooting_jp.md)** - 詳細な解決方法
- **[📚 サンプルコード集](examples_jp.md)** - 動作確認済みの例

---

**準備完了！** さあ、本格的にプロセス間通信をマスターしましょう！