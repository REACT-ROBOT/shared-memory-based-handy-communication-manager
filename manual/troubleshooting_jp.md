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
#    ヘッダは 2 か所に分かれている（shm_pub_sub.hpp が shm_base.hpp を取り込む）
ls shm_base/include shm_pub_sub/include
# shm_base.hpp / shm_pub_sub.hpp, shm_pub_sub_vector.hpp

# 2. コンパイル時のインクルードパス指定（ビルドツリーを直接使う場合）
g++ -std=c++17 -I./shm_base/include -I./shm_pub_sub/include your_program.cpp
# インストール済みなら include/ 1 つにまとまる
g++ -std=c++17 -I$HOME/.local/include your_program.cpp

# 3. CMake なら find_package が両方のパスを付けてくれる
#    （shm_base を先に見つけること。逆順だと find_package(shm_pub_sub) が失敗する）
find_package(shm_base    REQUIRED)
find_package(shm_pub_sub REQUIRED)
target_link_libraries(your_target shm_pub_sub)
```

### ❌ リンクエラー

**症状**:
```bash
undefined reference to `irlab::shm::Publisher<int>::publish(int const&)'
```

**原因と対処法**:

```bash
# 1. ライブラリファイルの確認
ls build/lib/
# shm_base.so, shm_pub_sub.so —— lib の接頭辞は付かない（PREFIX "" 指定のため）

# 2. リンク時のライブラリ指定
#    -lshm_pub_sub は libshm_pub_sub.so を探すので通らない。-l: 形式を使う。
g++ -std=c++17 your_program.cpp \
    -L./build/lib -l:shm_pub_sub.so -l:shm_base.so -lrt -pthread

# 3. 実行時のライブラリパス設定
export LD_LIBRARY_PATH=$PWD/build/lib:$LD_LIBRARY_PATH

# 4. どの実装を掴んでいるか確認する
#    shm_pub_sub.so は shm_base.so を素の名前で DT_NEEDED に持ち RUNPATH が無い。
#    LD_LIBRARY_PATH の先頭にある実装が勝つので、古いビルドツリーが残っていると
#    そちらを掴んで undefined symbol になる。
ldd ./your_program | grep shm
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

### ❌ まず `shm_tool doctor` を実行する

共有メモリまわりで様子がおかしいときは、コードを読む前にこれを実行する。
`/dev/shm` の全セグメントのヘッダを読み、そのまま使えるかを報告する。

```bash
shm_tool doctor            # 全部
shm_tool doctor lidar_scan # トピックを絞る
```

対処が必要なものには ★ が付き、終了コードが 1 になる。

| 表示 | 意味 | 対処 |
|---|---|---|
| ★ABI n（このビルドは m） | 古い形式のセグメントが残っている | `shm_tool remove <topic>` してから全プロセスを起動し直す |
| ★未初期化 | 作成途中で止まった残骸 | 同上 |
| ★再起動前に作られた残骸 | `/dev/shm` が再起動で消えなかった | 同上 |
| ★古い世代の残骸 / 切り替え競争に負けた残骸 | 世代切替の後始末が済んでいない | 同上（放置しても動作はする） |
| ★名前とヘッダが食い違う | 手で複製・改名した可能性 | 同上 |
| 書式が未宣言（注記） | `SHM_DECLARE_LAYOUT` がまだ付いていない | 動作はする。付けると同サイズの並べ替えも検出できる |

### ❌ 「ABI major version mismatch」で publish / subscribe が失敗する

共有メモリの形式が非互換に変わったときに出る。プロセス終了では
セグメントが消えないので、前の版のものが残っている。

```
shm::Publisher: root segment is not usable: ABI major version mismatch
(segment 3, this build 4) [topic 'lidar_scan']. Remove the segment with
'shm_tool remove lidar_scan' and restart every process on this topic
```

メッセージのとおり `shm_tool remove` してから、**そのトピックを使う全プロセスを
同時に起動し直す**こと。片方だけ入れ替えると、古いプロセスが古い形式のまま
セグメントを作り直してしまう。ロボットの再起動でも `/dev/shm` は消える。

### ❌ 「the segment was created by a build that did not declare this payload's format」

その型に `SHM_DECLARE_LAYOUT` を後から足したときに出る。宣言が無かった頃の
セグメントが残っていて、レイアウトが一致しているか確かめる手段が無い状態である。
`shm_tool remove <topic>` で一度消せばよい。

### ❌ 時刻を合わせた読み出しが「データが無い」と言う / 古い値が返る

保持できる履歴の長さは `buf_num ÷ 発行レート` である。10Hz のセンサを
`buf_num = 3` で持つと **300ms 分しか残らない**ので、1Hz で動く消費者が
1 秒前のオドメトリに合わせようとすると届かない。

`shm_tool doctor` の「履歴」列に、実際に保持している時間幅が出る。
足りなければ Publisher の `buffer_num` を増やすこと。

なお `SearchPolicy::Nearest` は有効なサンプルが 1 つでもあれば必ず
「最も近いもの」を返すので、どれだけ離れていても `TooOld` にならない。
ずれの上限を決めたい場合は `subscribeAlignedTo()` の `max_skew_us` を指定する。



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
# 1. 既存のセグメントを確認する
#    ipcs / ipcrm は System V 用で、このライブラリには使えない。
#    使うのは POSIX 共有メモリ（shm_open / /dev/shm）である。
shm_tool list
shm_tool doctor          # ヘッダを読んで、古い ABI や壊れたものを指摘する

# 2. 不要なトピックを消す（世代セグメントごと片付く）
shm_tool remove <topic>

# 3. 権限の確認
ls -la /dev/shm/
```

セグメントの権限が想定より狭いことがある。既定は `0660` だが、**作成時の
`umask` が引かれる**ので、`umask 0022` の環境では `0640` になり、
別ユーザ（同一グループ）から書けない。同じグループで読み書きさせたい場合は
publisher を起こす前に `umask 0002` にしておくこと。

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
echo "このライブラリのトピック:"
shm_tool doctor

echo ""
echo "/dev/shm の実体:"
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
shm_tool doctor
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
・共有メモリセグメントの確認 (shm_tool doctor)
・トピック名の確認
・プロセスの起動順序確認
・...
```

---

このガイドで問題が解決しない場合は、遠慮なくサポートチームまでお問い合わせください！ 🚀