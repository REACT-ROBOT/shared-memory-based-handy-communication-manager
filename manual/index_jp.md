# Shared Memory Based Communication Manager {#mainpage}
[[English](../index.html) | 日本語]

## 概要

**Shared Memory Based Communication Manager**は、同一PC内のプロセス間通信を超高速で実現するためのC++ライブラリです。

### 🧠 共有メモリベース通信ライブラリ
- **shm_pub_sub** - 高速な出版者/購読者モデル通信（ブロードキャスト型）

> **注記**: 以前の版には要求応答型の `shm_service` と非同期処理型の `shm_action` が
> 含まれていましたが、利用実績が無く、共有メモリ上の pthread オブジェクトを安全に
> 扱えていない設計上の問題を抱えていたため、v2.0.0 で削除しました。

## 📚 ドキュメント目次

### 初心者向けガイド
- [📖 はじめに - 通信ライブラリの基礎知識](introduction_jp.md)
- [🚀 クイックスタートガイド](quickstart_jp.md)
- [⚙️ インストールと環境設定](installation_jp.md)

### チュートリアル
- [📝 基本チュートリアル(C++)](tutorials_jp.md)
  - [🔄 Pub/Sub通信の使い方](tutorials_shm_pub_sub_jp.md)
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

## 🎨 通信方式の選び方

| 用途 | 推奨ライブラリ | 特徴 | 適用例 |
|------|----------------|------|--------|
| **リアルタイムデータ配信** | shm_pub_sub | ⚡最高速度<br>📡ブロードキャスト<br>🔄連続データ | センサーデータ配信<br>画像ストリーミング<br>ロボット制御信号 |

要求応答や長時間処理の管理が必要な場合は、上位層（ROS 2 のサービス/アクションなど）で
組み立てるか、Pub/Sub の上に用途に合わせた手順を実装してください。

## 📊 性能比較

| 指標 | shm_pub_sub |
|------|-------------|
| **遅延** | ~1μs |
| **スループット** | 非常に高い |
| **CPU使用率** | 最小 |
| **メモリ使用量** | 最小 |

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