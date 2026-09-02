# チュートリアル(C++)
[[English](../md_manual_tutorials_en.html) | 日本語]

## 🎯 学習パス

SHMライブラリの習得には以下の順序がおすすめです：

### 📚 基礎編
- **[SHMの導入](md_manual_tutorials_introduction_jp.html)** - ライブラリの概要と基本概念を理解

### 🚀 実践編

#### 1. **[📡 Pub/Sub通信](md_manual_tutorials_shm_pub_sub_jp.html)** - 超高速ブロードキャスト通信
   - **難易度**: ⭐⭐☆☆☆ (初級)
   - **用途**: センサーデータ配信、状態更新、リアルタイム通信
   - **特徴**: 1対多通信、高頻度配信(1kHz+)、低遅延(μs級)
   ```cpp
   Publisher<SensorData> pub("sensors");
   pub.publish(sensor_data);  // 瞬時配信
   ```

### 🐍 言語連携編
- **[Python統合](md_manual_tutorials_python_jp.html)** - PythonからC++ライブラリを活用

## 📊 通信方式比較表

| 方式 | レイテンシ | スループット | 応答保証 | 進捗監視 | 用途例 |
|------|------------|--------------|----------|----------|--------|
| **Pub/Sub** | 1-10μs | 非常に高い | ❌ | ❌ | センサーデータ、状態更新 |

> **注記**: v1.x の `shm_service` / `shm_action` は v2.0.0 で削除しました。
> 要求応答や長時間処理の管理が必要な場合は上位層で組み立ててください。

## 🎨 使用例マトリックス

### 🔄 リアルタイム系 (< 10ms)
```cpp
// 高頻度センサーデータ → Pub/Sub
Publisher<IMUData> imu_pub("imu");
imu_pub.publish(imu_data);  // 1kHz配信
```

### 🌐 状態と指令を分けた構成
```cpp
// 配信するトピックを役割ごとに分ける
class RobotController {
    Publisher<RobotState> state_pub_;   // 状態ブロードキャスト
    Subscriber<MotionCommand> command_; // 上位からの動作指令

public:
    void operateRobot() {
        // 1. 状態を定期配信
        state_pub_.publish(getCurrentState());

        // 2. 動作指令を購読して反映
        bool is_success = false;
        MotionCommand command = command_.subscribe(&is_success);
        if (is_success) {
            applyCommand(command);
        }
    }
};
```

## 🎯 レベル別学習目標

### 🥉 ブロンズレベル
- [ ] Pub/Sub通信で基本的なデータ配信ができる
- [ ] 基本的なエラーハンドリングを実装できる

### 🥈 シルバーレベル  
- [ ] 高頻度通信(1kHz+)を安定して実現できる
- [ ] 並列処理とスレッドセーフティを考慮できる

### 🥇 ゴールドレベル
- [ ] Pub/Subの上に必要な同期手順を自前で組み立てられる
- [ ] パフォーマンス最適化とベンチマークができる
- [ ] 大規模システムでの運用設計ができる

## 🛠️ 開発環境セットアップ

### 必要な知識
- **C++14以上**: STL、スマートポインタ、ラムダ式
- **CMake**: ビルドシステムの基本操作
- **Linux基礎**: プロセス、メモリ、権限の概念

### 推奨ツール
```bash
# コンパイラ
sudo apt install build-essential cmake

# デバッグツール  
sudo apt install gdb valgrind

# プロファイリング
sudo apt install linux-tools-generic

# 共有メモリ監視
shm_tool doctor  # トピックの健全性を確認（ipcs は System V 用で無関係）
```

## 🚀 次のステップ

1. **まずは基礎から**: [SHMの導入](md_manual_tutorials_introduction_jp.html)で概念を理解
2. **実践練習**: [Pub/Sub通信](md_manual_tutorials_shm_pub_sub_jp.html)で手を動かす  
3. **言語統合**: [Python連携](md_manual_tutorials_python_jp.html)で開発効率向上

---

**🎓 学習のコツ**: 各チュートリアルのサンプルコードを実際に動かして、パフォーマンス特性を体感することが重要です。理論と実践を組み合わせて、真の理解を深めましょう！