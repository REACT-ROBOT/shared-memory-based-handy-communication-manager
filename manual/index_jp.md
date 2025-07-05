# Shared Memory Based Communication Manager {#mainpage}
[[English](../index.html) | 日本語]

## 概要

**Shared Memory Based Communication Manager**は、同一PC内のプロセス間通信を超高速で実現するための包括的なC++ライブラリ集です。このライブラリは以下の3つの主要なコンポーネントから構成されています：

### 🧠 共有メモリベース通信ライブラリ
- **shm_pub_sub** - 高速な出版者/購読者モデル通信（ブロードキャスト型）
- **shm_service** - 信頼性の高いサーバー/クライアントモデル通信（要求応答型）
- **shm_action** - 高機能な非同期処理通信（長時間処理対応）

## 📚 ドキュメント目次

### 初心者向けガイド
- [📖 はじめに - 通信ライブラリの基礎知識](introduction_jp.md)
- [🚀 クイックスタートガイド](quickstart_jp.md)
- [⚙️ インストールと環境設定](installation_jp.md)

### チュートリアル
- [📝 基本チュートリアル(C++)](tutorials_jp.md)
  - [🔄 Pub/Sub通信の使い方](tutorials_shm_pub_sub_jp.md)
  - [🤝 Service通信の使い方](tutorials_shm_service_jp.md)
  - [⚡ Action通信の使い方](tutorials_shm_action_jp.md)
- [🐍 Pythonチュートリアル](tutorials_python_jp.md)
  - [🔄 Python Pub/Sub通信の使い方](tutorials_shm_pub_sub_python_jp.md)

### 詳細仕様書
- [📋 API仕様書](spec_jp.md)
- [🔧 詳細設定と応用](advanced_jp.md)
- [🐛 トラブルシューティング](troubleshooting_jp.md)

### 参考資料
- [📚 参考文献](reference_jp.md)
- [💡 サンプルコード集](examples_jp.md)

## 🎯 特徴

### 🚀 圧倒的な高速性
- ⚡ **マイクロ秒レベルの超低遅延** - メモリ直接アクセスによる最高速度
- 🎯 **ゼロコピー通信** - データコピーを最小限に抑えた効率的な転送
- 🔥 **CPUキャッシュ最適化** - メモリレイアウトを考慮した設計

### 🔒 安全性と信頼性
- 🛡️ **スレッドセーフ** - 自動的な排他制御とデッドロック回避
- 🔐 **型安全性** - C++テンプレートによる コンパイル時型チェック
- 🚨 **例外安全** - RAII設計による確実なリソース管理
- ✅ **データ整合性** - 原子操作による破損防止

### 🎛️ 使いやすさ
- 🎨 **直感的API** - ROSライクな分かりやすいインターフェース
- 📦 **自動メモリ管理** - メモリリークを防ぐスマートポインタ設計
- 🔧 **簡単セットアップ** - 複雑な設定不要、即座に利用開始
- 🐍 **多言語対応** - C++とPythonで同じAPIを提供

## 🏃 クイックスタート

### 1. 簡単なPub/Sub通信（共有メモリ）
```cpp
#include "shm_pub_sub.hpp"
using namespace irlab::shm;

// 送信側
Publisher<int> pub("my_topic");
pub.publish(42);

// 受信側
Subscriber<int> sub("my_topic");
bool state;
int data = sub.subscribe(&state);
if (state) {
    std::cout << "受信データ: " << data << std::endl;
}
```

### 2. 簡単なService通信（要求応答）
```cpp
#include "shm_service.hpp"
using namespace irlab::shm;

// サーバー側
ServiceServer<int, int> server("calc_service");
if (server.hasRequest()) {
    int request = server.getRequest();
    int response = request * 2;  // 2倍にして返す
    server.sendResponse(response);
}

// クライアント側
ServiceClient<int, int> client("calc_service");
client.sendRequest(21);
if (client.waitForResponse(1000000)) {  // 1秒待機
    int result = client.getResponse();
    std::cout << "計算結果: " << result << std::endl;  // 42
}
```

## 🎨 通信方式の選び方

| 用途 | 推奨ライブラリ | 特徴 | 適用例 |
|------|----------------|------|--------|
| **リアルタイムデータ配信** | shm_pub_sub | ⚡最高速度<br>📡ブロードキャスト<br>🔄連続データ | センサーデータ配信<br>画像ストリーミング<br>ロボット制御信号 |
| **確実なデータ交換** | shm_service | 🤝要求応答保証<br>⏰タイムアウト対応<br>🛡️エラーハンドリング | データベース操作<br>設定値取得<br>計算結果取得 |
| **長時間非同期処理** | shm_action | ⚡非同期実行<br>📊進捗監視<br>❌キャンセル機能 | ファイル処理<br>機械学習訓練<br>大容量データ変換 |

## 📊 性能比較

| 指標 | shm_pub_sub | shm_service | shm_action |
|------|-------------|-------------|------------|
| **遅延** | ~1μs | ~2-5μs | ~2-10μs |
| **スループット** | 非常に高い | 高い | 中程度 |
| **CPU使用率** | 最小 | 低い | 中程度 |
| **メモリ使用量** | 最小 | 少ない | 中程度 |

## 📞 サポート

- **🆎 オープンソース**: コントリビューション歓迎
- **👥 コミュニティサポート**: ユーザー同士の相互支援
- **🐛 バグレポート**: Issueトラッカーで報告

## 📄 ライセンス

**Apache License 2.0** 🆎

Copyright 2024 Shared Memory Communication Contributors

本ソフトウェアはApache License 2.0の下でオープンソースとして提供されています。商用利用、改変、再配布が可能です。

### 🛡️ ライセンスの特徴
- ✅ **商用利用可能**: 商業プロジェクトでも自由に使用
- ✅ **改変可能**: ソースコードの修正・拡張が可能
- ✅ **再配布可能**: ライセンス表示を保持して再配布可能
- ✅ **特許保護**: 貢献者の特許権が保護される

詳細は[LICENSEファイル](../LICENSE)をご確認ください。

---

**次のステップ**: [📖 はじめに](introduction_jp.md)で基本概念を学びましょう！