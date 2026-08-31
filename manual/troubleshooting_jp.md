# 🐛 トラブルシューティング
[[English](../md_manual_troubleshooting_en.html) | 日本語]

このガイドでは、共有メモリ通信ライブラリでよくある問題とその解決方法を詳しく説明します。問題が発生した時は、該当する症状を見つけて順番に対処法を試してください。

## 📋 目次

1. [ビルド・コンパイル問題](#ビルド・コンパイル問題)
2. [共有メモリ通信の問題](#共有メモリ通信の問題)
3. [パフォーマンス問題](#パフォーマンス問題)
4. [環境設定問題](#環境設定問題)
5. [デバッグツール](#デバッグツール)

## 🔨 ビルド・コンパイル問題

### ❌ ヘッダーファイルが見つからない

**症状**:
```bash
fatal error: shm_pub_sub.hpp: No such file or directory
```

**原因と対処法**:

```bash
# 1. インクルードパスの確認
ls include/
# shm_pub_sub.hpp, udp_comm.hpp などがあることを確認

# 2. コンパイル時のインクルードパス指定
g++ -I./include your_program.cpp

# 3. CMakeLists.txtでの設定
target_include_directories(your_target PRIVATE include)

# 4. 必要なヘッダーファイルの確認
find /path/to/project -name "*.hpp" | grep shm
```

### ❌ リンクエラー

**症状**:
```bash
undefined reference to `irlab::shm::Publisher<int>::publish(int const&)'
```

**原因と対処法**:

```bash
# 1. ライブラリファイルの確認
ls build/
# libshm_pub_sub.so などがあることを確認

# 2. リンク時のライブラリ指定
g++ your_program.cpp -L./build -lshm_pub_sub

# 3. 実行時のライブラリパス設定
export LD_LIBRARY_PATH=./build:$LD_LIBRARY_PATH
```

### ❌ C++17エラー

**症状**:
```bash
error: 'std::is_standard_layout_v' was not declared in this scope
```

**対処法**:
```bash
# C++17対応コンパイラを使用
g++ -std=c++17 your_program.cpp

# またはCMakeで
set(CMAKE_CXX_STANDARD 17)
```

## 🧠 共有メモリ通信の問題

### ❌ データが受信されない（共有メモリ）

**症状**: `subscribe()`で`state`が常に`false`

**診断手順**:

```cpp
// デバッグ用の診断コード
#include "shm_pub_sub.hpp"
#include <iostream>

void diagnose_shm_communication() {
    using namespace irlab::shm;
    
    std::cout << "=== 共有メモリ通信診断 ===" << std::endl;
    
    // 1. 送信側テスト
    try {
        Publisher<int> pub("debug_topic");
        std::cout << "✅ Publisher作成成功" << std::endl;
        
        pub.publish(42);
        std::cout << "✅ データ送信成功" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "❌ Publisher失敗: " << e.what() << std::endl;
        return;
    }
    
    // 2. 受信側テスト
    try {
        Subscriber<int> sub("debug_topic");
        std::cout << "✅ Subscriber作成成功" << std::endl;
        
        bool state;
        int data = sub.subscribe(&state);
        std::cout << "受信結果: state=" << state << ", data=" << data << std::endl;
        
        if (!state) {
            std::cout << "❌ データが無効 - 以下を確認:" << std::endl;
            std::cout << "   - 送信プログラムが動作中か？" << std::endl;
            std::cout << "   - トピック名が一致しているか？" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "❌ Subscriber失敗: " << e.what() << std::endl;
    }
}
```

**よくある原因と対処法**:

| 原因 | 対処法 |
|------|--------|
| **トピック名の不一致** | 送信側と受信側で同じ文字列を使用 |
| **データ型の不一致** | `Publisher<int>`と`Subscriber<int>`で型を統一 |
| **送信プログラムが動いていない** | 送信プログラムを先に起動 |
| **権限問題** | `sudo`での実行または権限設定 |

### ❌ 共有メモリアクセスエラー

**症状**:
```bash
terminate called after throwing an instance of 'std::runtime_error'
what(): Failed to create shared memory
```

**対処法**:

```bash
# 1. 既存の共有メモリセグメントを確認・削除
ipcs -m
# 不要な共有メモリを削除
ipcrm -m [shmid]

# 2. 権限の確認
ls -la /dev/shm/
# shm_* ファイルの権限を確認

# 3. 権限変更（必要に応じて）
sudo chmod 666 /dev/shm/shm_*
```

## ⚡ パフォーマンス問題

### ❌ 通信が遅い

**症状**: 期待より通信速度が遅い

**診断と対処法**:

```cpp
// パフォーマンス測定コード
#include <chrono>
#include <vector>

void measure_performance() {
    using namespace std::chrono;
    
    // 共有メモリ性能測定
    {
        using namespace irlab::shm;
        Publisher<int> pub("perf_test");
        Subscriber<int> sub("perf_test");
        
        const int iterations = 10000;
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            pub.publish(i);
            bool state;
            sub.subscribe(&state);
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(end - start);
        
        std::cout << "共有メモリ通信:" << std::endl;
        std::cout << "  総時間: " << duration.count() << " μs" << std::endl;
        std::cout << "  1回あたり: " << duration.count() / iterations << " μs" << std::endl;
    }
    
    // UDP性能測定
    {
        using namespace irlab::udp;
        Sender<int> sender("perf_test");
        Receiver<int> receiver("perf_test");
        
        const int iterations = 1000;  // UDPは少なめ
        auto start = high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            sender.send(i);
            receiver.waitFor(10000);  // 10ms待機
            bool state;
            receiver.receive(&state);
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);
        
        std::cout << "UDP通信:" << std::endl;
        std::cout << "  総時間: " << duration.count() << " ms" << std::endl;
        std::cout << "  1回あたり: " << duration.count() / (double)iterations << " ms" << std::endl;
    }
}
```

**パフォーマンス改善方法**:

| 通信方式 | 改善方法 |
|----------|----------|
| **共有メモリ** | ・CPUアフィニティ設定<br>・リアルタイムプロセス優先度<br>・不要なシステム負荷削減 |
| **UDP** | ・送信間隔の調整<br>・バッファサイズ最適化<br>・ネットワーク設定確認 |

## 🔧 環境設定問題

### ❌ 権限エラー

**症状**:
```bash
Permission denied
Failed to create shared memory segment
```

**対処法**:

```bash
# 1. ユーザーをshm可能グループに追加
sudo usermod -a -G audio $USER  # または適切なグループ

# 2. 一時的な権限変更
sudo chmod 777 /dev/shm

# 3. systemd設定（永続的）
# /etc/systemd/system.conf に追加
DefaultLimitMEMLOCK=infinity
```

### ❌ 依存関係問題

**症状**: 必要なライブラリが見つからない

**対処法**:

```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential cmake libboost-all-dev python3-dev

# CentOS/RHEL
sudo yum groupinstall "Development Tools"
sudo yum install cmake boost-devel python3-devel

# 依存関係確認
ldd your_program
```

## 🔍 デバッグツール

### 共有メモリ監視ツール

```bash
#!/bin/bash
# shm_monitor.sh - 共有メモリ監視スクリプト

echo "=== 共有メモリ使用状況 ==="
echo "システム全体:"
ipcs -m

echo ""
echo "SHMライブラリ関連:"
ls -la /dev/shm/ | grep shm_

echo ""
echo "プロセス監視:"
ps aux | grep -E "(your_program_name|shm)"
```

## 📞 サポートが必要な場合

### 情報収集

問題が解決しない場合は、以下の情報を収集してサポートチームにお知らせください：

```bash
# システム情報収集スクリプト
#!/bin/bash
# collect_info.sh

echo "=== システム情報 ==="
uname -a
lsb_release -a 2>/dev/null || cat /etc/os-release

echo ""
echo "=== コンパイラ情報 ==="
g++ --version
cmake --version

echo ""
echo "=== ライブラリ情報 ==="
find . -name "*.so" -exec ls -la {} \;

echo ""
echo "=== 共有メモリ状況 ==="
ipcs -m
ls -la /dev/shm/ | grep shm_

echo ""
echo "=== システムリソース ==="
free -h
df -h /dev/shm

echo ""
echo "=== 関連プロセス状況 ==="
ps aux | grep -E "(your_program|shm|service|action)" | grep -v grep
```

### エラーレポート形式

```
【問題の症状】
・どのような動作を期待していたか
・実際にはどうなったか
・エラーメッセージ（あれば）

【環境情報】
・OS: Ubuntu 20.04
・コンパイラ: g++ 9.4.0
・使用ライブラリ: shm_pub_sub v2.0

【再現手順】
1. プログラムをコンパイル
2. 送信プログラムを実行
3. 受信プログラムを実行
4. → データが受信されない

【試したこと】
・共有メモリセグメントの確認 (ipcs -m)
・トピック名の確認
・プロセスの起動順序確認
・...
```

---

このガイドで問題が解決しない場合は、遠慮なくサポートチームまでお問い合わせください！ 🚀