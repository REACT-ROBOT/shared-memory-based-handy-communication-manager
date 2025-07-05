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

#### 2. **[🤝 Service通信](md_manual_tutorials_shm_service_jp.html)** - 確実な要求応答通信  
   - **難易度**: ⭐⭐⭐☆☆ (中級)
   - **用途**: データベース操作、計算サービス、設定変更
   - **特徴**: 1対1通信、確実な応答、タイムアウト管理
   ```cpp
   ServiceClient<Request, Response> client("database");
   client.sendRequest(query);
   Response result = client.getResponse();  // 確実な応答
   ```

#### 3. **[⚡ Action通信](md_manual_tutorials_shm_action_jp.html)** - 長時間非同期処理
   - **難易度**: ⭐⭐⭐⭐☆ (上級)
   - **用途**: ファイル処理、機械学習、大容量データ変換
   - **特徴**: 非同期実行、進捗監視、キャンセル可能
   ```cpp
   ActionClient<Goal, Result, Feedback> client("processor");
   uint64_t goal_id = client.sendGoal(large_task);
   // 進捗監視しながら他の処理継続
   ```

### 🐍 言語連携編
- **[Python統合](md_manual_tutorials_python_jp.html)** - PythonからC++ライブラリを活用

## 📊 通信方式比較表

| 方式 | レイテンシ | スループット | 応答保証 | 進捗監視 | 用途例 |
|------|------------|--------------|----------|----------|--------|
| **Pub/Sub** | 1-10μs | 非常に高い | ❌ | ❌ | センサーデータ、状態更新 |
| **Service** | 10-100μs | 高い | ✅ | ❌ | データベース、計算処理 |
| **Action** | 100μs-1ms | 中程度 | ✅ | ✅ | ファイル処理、ML訓練 |

## 🎨 使用例マトリックス

### 🔄 リアルタイム系 (< 10ms)
```cpp
// 高頻度センサーデータ → Pub/Sub
Publisher<IMUData> imu_pub("imu");
imu_pub.publish(imu_data);  // 1kHz配信

// 即座の計算要求 → Service  
ServiceClient<Position, Force> physics("physics_engine");
Force result = physics.call(current_position);
```

### 📈 バッチ処理系 (> 1秒)
```cpp
// 大容量データ処理 → Action
ActionClient<Dataset, Model, Progress> ml("trainer");
uint64_t job = ml.sendGoal(training_data);
while (!ml.isComplete(job)) {
    Progress progress = ml.getFeedback(job);
    updateProgressBar(progress.percent);
}
```

### 🌐 混合アーキテクチャ
```cpp
// 複数通信方式の組み合わせ
class RobotController {
    Publisher<RobotState> state_pub_;      // 状態ブロードキャスト
    ServiceClient<Motion, Result> motion_; // 動作指令
    ActionClient<Task, Result, Progress> task_; // 長時間タスク
    
public:
    void operateRobot() {
        // 1. 状態を定期配信
        state_pub_.publish(getCurrentState());
        
        // 2. 即座の動作指令
        motion_.call(move_command);
        
        // 3. 長時間タスクを並行実行
        if (!task_.isActive()) {
            task_.sendGoal(navigation_task);
        }
    }
};
```

## 🎯 レベル別学習目標

### 🥉 ブロンズレベル
- [ ] Pub/Sub通信で基本的なデータ配信ができる
- [ ] Service通信で簡単な要求応答処理ができる
- [ ] 基本的なエラーハンドリングを実装できる

### 🥈 シルバーレベル  
- [ ] 高頻度通信(1kHz+)を安定して実現できる
- [ ] 複数の通信方式を適切に使い分けられる
- [ ] 並列処理とスレッドセーフティを考慮できる

### 🥇 ゴールドレベル
- [ ] Action通信で複雑な非同期処理を実装できる
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
ipcs -m  # 共有メモリセグメント確認
```

## 🚀 次のステップ

1. **まずは基礎から**: [SHMの導入](md_manual_tutorials_introduction_jp.html)で概念を理解
2. **実践練習**: [Pub/Sub通信](md_manual_tutorials_shm_pub_sub_jp.html)で手を動かす  
3. **応用発展**: [Service](md_manual_tutorials_shm_service_jp.html)、[Action](md_manual_tutorials_shm_action_jp.html)で高度な機能を習得
4. **言語統合**: [Python連携](md_manual_tutorials_python_jp.html)で開発効率向上

---

**🎓 学習のコツ**: 各チュートリアルのサンプルコードを実際に動かして、パフォーマンス特性を体感することが重要です。理論と実践を組み合わせて、真の理解を深めましょう！