# 📚 参考文献とリソース
[[English](../md_manual_reference_en.html) | 日本語]

## 🔬 学術論文・研究資料

### [1] 共有メモリ通信フレームワーク
**入江清**. "ROS との相互運用性に配慮した共有メモリによる低遅延プロセス間通信フレームワーク." 第 35 回日本ロボット学会学術講演会予稿集, RSJ2017AC2B2-01 (2017).
- **URL**: <https://furo.org/irie/papers/rsj2017_irie.pdf>
- **概要**: 本ライブラリの設計思想とROS互換性について詳述

### [2] POSIX共有メモリ仕様
**Linux man-pages project**. "shm_overview - overview of POSIX shared memory"
- **URL**: <https://linuxjm.osdn.jp/html/LDP_man-pages/man7/shm_overview.7.html>
- **概要**: 共有メモリの基礎仕様とシステムコール

## 🤖 関連フレームワーク

### [3] ROS (Robot Operating System)
**Open Robotics**. "ROS.org - Robot Operating System"
- **URL**: <http://wiki.ros.org/ja/>
- **概要**: 本ライブラリの通信パターン（Pub/Sub、Service、Action）の参考元

### [4] 通信性能に関する研究
**尾崎功一**. "プロセス間通信の性能評価とロボット制御への応用"
- **所属**: 同志社大学理工学部
- **概要**: 共有メモリ通信のベンチマークとリアルタイム性能評価

## 🔧 技術仕様・標準

### [5] C++20 標準ライブラリ
**ISO/IEC 14882:2020**. "Programming languages — C++"
- **URL**: <https://isocpp.org/std/the-standard>
- **概要**: 本ライブラリで使用するC++テンプレートとメモリ管理

### [6] Boost.Python ドキュメント
**Boost C++ Libraries**. "Boost.Python"
- **URL**: <https://www.boost.org/doc/libs/1_80_0/libs/python/doc/html/index.html>
- **概要**: PythonバインディングでのC++連携

### [7] pthread 仕様
**POSIX.1-2008**. "IEEE Std 1003.1-2008 - POSIX Threads"
- **URL**: <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/pthread.h.html>
- **概要**: マルチスレッド環境での排他制御

## 📖 関連書籍

### [8] 並行プログラミング
**Anthony Williams**. "C++ Concurrency in Action: Practical Multithreading"
- **出版社**: Manning Publications
- **ISBN**: 978-1617294693
- **概要**: C++における並行プログラミングとスレッドセーフティ

### [9] システムプログラミング
**Michael Kerrisk**. "The Linux Programming Interface"
- **出版社**: No Starch Press
- **ISBN**: 978-1593272203
- **概要**: LinuxシステムプログラミングとIPCの詳細

### [10] リアルタイムシステム
**Giorgio Buttazzo**. "Hard Real-Time Computing Systems"
- **出版社**: Springer
- **ISBN**: 978-1461406754
- **概要**: リアルタイムシステムの設計と性能評価

## 🌐 オンラインリソース

### [11] 共有メモリ最適化手法
**Intel Developer Zone**. "Optimizing Shared Memory Performance"
- **URL**: <https://software.intel.com/content/www/us/en/develop/documentation/cpp-compiler-developer-guide-and-reference/top/optimization-and-programming-guide/shared-memory-parallel-programming.html>
- **概要**: CPUキャッシュ効率とメモリアクセス最適化

### [12] プロセス間通信比較
**Red Hat Developer**. "Inter-Process Communication in Linux"
- **URL**: <https://developers.redhat.com/articles/2023/05/10/inter-process-communication-linux>
- **概要**: 各種IPC方式の性能比較とユースケース

### [13] リアルタイム通信アーキテクチャ
**Real-Time eXecutive Interface (RTXI)**. "Real-Time Communication Patterns"
- **URL**: <http://rtxi.org/docs/tutorials/real-time-communication/>
- **概要**: リアルタイムシステムでの通信パターン設計

## 🛠️ 開発ツール・ライブラリ

### [14] Google Test Framework
**Google**. "GoogleTest - Google Testing and Mocking Framework"
- **URL**: <https://github.com/google/googletest>
- **概要**: 本ライブラリの単体テストで使用

### [15] Valgrind メモリ解析
**Valgrind Developers**. "Valgrind - Dynamic Analysis Tool"
- **URL**: <https://valgrind.org/>
- **概要**: メモリリーク検出とプロファイリング

### [16] perf 性能解析
**Linux kernel**. "perf - Linux profiling with performance counters"
- **URL**: <https://perf.wiki.kernel.org/index.php/Main_Page>
- **概要**: 通信性能測定とボトルネック分析

## 🎓 教育リソース

### [17] プロセス間通信チュートリアル
**GeeksforGeeks**. "Inter Process Communication (IPC)"
- **URL**: <https://www.geeksforgeeks.org/inter-process-communication-ipc/>
- **概要**: IPCの基礎概念と実装例

### [18] 共有メモリプログラミング入門
**LinuxHint**. "Shared Memory in Linux"
- **URL**: <https://linuxhint.com/shared-memory-linux/>
- **概要**: Linux環境での共有メモリ実装

## 🏆 ベンチマーク・性能評価

### [19] 通信ライブラリ性能比較
**Ozaki, K., et al.** "Performance Evaluation of Inter-Process Communication Libraries for Robotic Applications"
- **会議**: IEEE International Conference on Robotics and Automation (ICRA)
- **概要**: 各種IPC方式の定量的性能比較

### [20] リアルタイム通信レイテンシ測定
**Real-Time Systems Laboratory**. "Latency Measurement in Real-Time Communication"
- **URL**: <https://www.real-time.org/latency-measurement/>
- **概要**: マイクロ秒レベルでの通信遅延測定手法

---

## 🔍 関連用語・キーワード

**プロセス間通信 (IPC)**
- Shared Memory, Message Queue, Pipe, Socket
- POSIX IPC, System V IPC

**並行プログラミング**
- Thread Safety, Race Condition, Deadlock
- Mutex, Semaphore, Atomic Operations

**リアルタイムシステム**
- Hard Real-Time, Soft Real-Time
- Jitter, Latency, Throughput

**C++技術**
- Template Programming, RAII, Smart Pointers
- Memory Management, Exception Safety

**ロボティクス**
- ROS, Robot Control, Sensor Fusion
- Real-Time Processing, Distributed Systems

---

**💡 さらなる学習のために**: これらの参考文献から深い理解を得て、より高度なプロセス間通信システムを設計しましょう！