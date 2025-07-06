# チュートリアル(Python)
[[English](../md_manual_tutorials_python_en.html) | 日本語]

## 概要

Shared Memory Based Handy Communication Manager（SHM）は、共有メモリを使用した高速なプロセス間通信を提供するライブラリです。このライブラリは、出版者/購読者（Publisher/Subscriber）モデルを使用してトピックベースの通信を可能にします。

## 特徴

- **高速通信**: 共有メモリを使用することで、低遅延・高スループットの通信を実現
- **リングバッファ**: 複数のバッファを使用して、読み書きの競合を最小化
- **型安全**: テンプレートベースの設計により、型安全な通信を提供
- **Python対応**: Boost.Pythonを使用したPythonバインディング

## 対応データ型

現在、以下のデータ型をサポートしています：

- `bool`
- `int`  
- `float`

## Python API

### Publisher（出版者）

データを共有メモリに書き込むためのクラスです。

#### コンストラクタ

```python
# bool型のPublisher
pub_bool = shm_pub_sub.Publisher(name: str, default_value: bool, buffer_num: int)

# int型のPublisher  
pub_int = shm_pub_sub.Publisher(name: str, default_value: int, buffer_num: int)

# float型のPublisher
pub_float = shm_pub_sub.Publisher(name: str, default_value: float, buffer_num: int)
```

**パラメータ:**
- `name`: 共有メモリの名前（文字列）
- `default_value`: デフォルト値（型に応じて bool/int/float）
- `buffer_num`: バッファ数（デフォルト: 3）

#### メソッド

##### publish(data)

データを共有メモリに書き込みます。

```python
# bool型の場合
pub_bool.publish(True)

# int型の場合  
pub_int.publish(42)

# float型の場合
pub_float.publish(3.14)
```

### Subscriber（購読者）

共有メモリからデータを読み取るためのクラスです。

#### コンストラクタ

```python
# bool型のSubscriber
sub_bool = shm_pub_sub.Subscriber(name: str, default_value: bool)

# int型のSubscriber
sub_int = shm_pub_sub.Subscriber(name: str, default_value: int)

# float型のSubscriber  
sub_float = shm_pub_sub.Subscriber(name: str, default_value: float)
```

**パラメータ:**
- `name`: 共有メモリの名前（文字列）
- `default_value`: デフォルト値（型に応じて bool/int/float）

#### メソッド

##### subscribe()

共有メモリからデータを読み取ります。

```python
# bool型の場合
data, is_success = sub_bool.subscribe()

# int型の場合
data, is_success = sub_int.subscribe()

# float型の場合
data, is_success = sub_float.subscribe()
```

**戻り値:**
- `data`: 読み取ったデータ（bool/int/float）
- `is_success`: 読み取り成功フラグ（bool）

## ビルドとインストール

### 前提条件

- CMake
- Boost.Python
- Python 3.6以上

### ビルド手順

```bash
# ビルドディレクトリを作成
mkdir build && cd build

# CMakeを実行
cmake ..

# ビルドを実行
make

# 実行ファイル・ライブラリをインストール
make install

# Pythonライブラリのパスを登録
export PYTHONPATH=$PYTHONPATH:$(pwd)/python
```

## 制限事項

1. **PODクラスのみ対応**: 現在は、Plain Old Data（POD）クラスのみサポートしています
2. **固定サイズ**: 可変長データの送信はサポートしていません
3. **プロセス間通信**: 同一マシン上のプロセス間通信のみサポート

## エラーハンドリング

### 一般的なエラー

1. **共有メモリの作成失敗**
   - 原因: 権限不足、メモリ不足
   - 対処: 実行権限の確認、システムリソースの確認

2. **データの読み取り失敗**
   - 原因: Publisher側でデータが送信されていない、タイムアウト
   - 対処: Publisher側の動作確認、タイムアウト時間の調整

3. **名前の競合**
   - 原因: 同じ名前の共有メモリが既に存在
   - 対処: 一意な名前の使用、既存の共有メモリの削除

## パフォーマンス

### 推奨設定

- **バッファ数**: 3-5個（デフォルト: 3）
- **送信頻度**: 用途に応じて調整
- **データサイズ**: 小さいデータ（数十バイト）での使用を推奨

### ベンチマーク

典型的な使用ケースでの性能（参考値）：

- **遅延**: 数マイクロ秒程度
- **スループット**: 数十万メッセージ/秒
- **メモリ使用量**: データサイズ × バッファ数

## トラブルシューティング

### よくある問題

1. **Pythonモジュールが見つからない**
   ```
   ImportError: No module named 'shm_pub_sub'
   ```
   - 解決方法: setup.pyでのインストールを確認

2. **共有メモリへのアクセス権限エラー**
   ```
   Permission denied
   ```
   - 解決方法: 実行権限の確認、sudoでの実行

3. **データの不整合**
   - 原因: 読み書きのタイミングの問題
   - 解決方法: 適切な待機時間の設定

### デバッグ方法

1. **ログの確認**: システムログでエラーメッセージを確認
2. **共有メモリの確認**: `ipcs`コマンドで共有メモリの状態を確認
3. **プロセス間通信の確認**: Publisher/Subscriberの動作を個別に確認

## 関連ドキュメント

- [SHMの導入](md_manual_tutorials_introduction_jp.html)
- [出版者/購読者モデル（Pub/Sub通信）の使い方](md_manual_tutorials_shm_pub_sub_python_jp.html)