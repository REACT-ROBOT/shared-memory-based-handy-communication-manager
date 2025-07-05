# 出版者/購読者モデル（Pub/Sub通信）の使い方 (Python)
[[English](../md_manual_tutorials_shm_pub_sub_python_en.html) | 日本語]

## 概要

このチュートリアルでは、Shared Memory Based Handy Communication Manager（SHM）のPython APIを使用した出版者/購読者モデルの実装方法を説明します。

## 基本的な使用例

### 1. 基本的なPublisher（送信側）

```python
import shm_pub_sub
import time

# int型のPublisherを作成
pub = shm_pub_sub.Publisher("test_topic", 0, 3)

# データを定期的に送信
for i in range(10):
    pub.publish(i)
    print(f"Published: {i}")
    time.sleep(1)
```

### 2. 基本的なSubscriber（受信側）

```python
import shm_pub_sub
import time

# int型のSubscriberを作成
sub = shm_pub_sub.Subscriber("test_topic", 0)

# データを受信
for i in range(10):
    data, is_success = sub.subscribe()
    if is_success:
        print(f"Received: {data}")
    else:
        print("Failed to receive data")
    time.sleep(1)
```

## 複数データ型の使用例

### 3. 複数型データの送受信

```python
import shm_pub_sub
import time

# 複数の型のPublisherを作成
pub_bool = shm_pub_sub.Publisher("bool_topic", False, 3)
pub_int = shm_pub_sub.Publisher("int_topic", 0, 3)
pub_float = shm_pub_sub.Publisher("float_topic", 0.0, 3)

# 複数の型のSubscriberを作成
sub_bool = shm_pub_sub.Subscriber("bool_topic", False)
sub_int = shm_pub_sub.Subscriber("int_topic", 0)
sub_float = shm_pub_sub.Subscriber("float_topic", 0.0)

# データの送受信
for i in range(5):
    # データを送信
    pub_bool.publish(i % 2 == 0)
    pub_int.publish(i * 10)
    pub_float.publish(i * 3.14)
    
    # データを受信
    bool_data, bool_success = sub_bool.subscribe()
    int_data, int_success = sub_int.subscribe()
    float_data, float_success = sub_float.subscribe()
    
    if bool_success:
        print(f"Bool: {bool_data}")
    if int_success:
        print(f"Int: {int_data}")
    if float_success:
        print(f"Float: {float_data}")
        
    time.sleep(1)
```

## 実践的な使用例

### 4. センサーデータの送受信

```python
import shm_pub_sub
import time
import random

# センサーデータ送信側
def sensor_publisher():
    """温度センサーのデータを送信"""
    temp_pub = shm_pub_sub.Publisher("temperature", 0.0, 5)
    humidity_pub = shm_pub_sub.Publisher("humidity", 0.0, 5)
    
    for i in range(100):
        # 模擬センサーデータを生成
        temperature = 20.0 + random.uniform(-5.0, 15.0)
        humidity = 50.0 + random.uniform(-20.0, 30.0)
        
        # データを送信
        temp_pub.publish(temperature)
        humidity_pub.publish(humidity)
        
        print(f"Sensor data sent - Temp: {temperature:.2f}°C, Humidity: {humidity:.2f}%")
        time.sleep(0.5)

# センサーデータ受信側
def sensor_subscriber():
    """温度・湿度データを受信して処理"""
    temp_sub = shm_pub_sub.Subscriber("temperature", 0.0)
    humidity_sub = shm_pub_sub.Subscriber("humidity", 0.0)
    
    for i in range(100):
        # データを受信
        temp, temp_success = temp_sub.subscribe()
        humidity, humidity_success = humidity_sub.subscribe()
        
        if temp_success and humidity_success:
            # 警告条件をチェック
            if temp > 30.0:
                print(f"WARNING: High temperature detected: {temp:.2f}°C")
            if humidity > 80.0:
                print(f"WARNING: High humidity detected: {humidity:.2f}%")
            
            print(f"Monitor - Temp: {temp:.2f}°C, Humidity: {humidity:.2f}%")
        else:
            print("Failed to receive sensor data")
        
        time.sleep(0.5)

# 使用例
if __name__ == "__main__":
    import threading
    
    # 送信側と受信側を別スレッドで実行
    pub_thread = threading.Thread(target=sensor_publisher)
    sub_thread = threading.Thread(target=sensor_subscriber)
    
    pub_thread.start()
    sub_thread.start()
    
    pub_thread.join()
    sub_thread.join()
```

### 5. 制御システムの例

```python
import shm_pub_sub
import time

# 制御指令送信側
def control_publisher():
    """制御指令を送信"""
    speed_pub = shm_pub_sub.Publisher("motor_speed", 0.0, 3)
    direction_pub = shm_pub_sub.Publisher("motor_direction", 0, 3)  # 0: stop, 1: forward, -1: backward
    enable_pub = shm_pub_sub.Publisher("motor_enable", False, 3)
    
    # 制御シーケンス
    commands = [
        (True, 10.0, 1),   # 有効化、速度10、前進
        (True, 20.0, 1),   # 速度20、前進
        (True, 15.0, -1),  # 速度15、後退
        (True, 0.0, 0),    # 停止
        (False, 0.0, 0),   # 無効化
    ]
    
    for enable, speed, direction in commands:
        enable_pub.publish(enable)
        speed_pub.publish(speed)
        direction_pub.publish(direction)
        
        print(f"Control command: Enable={enable}, Speed={speed}, Direction={direction}")
        time.sleep(2)

# 制御システム受信側
def control_subscriber():
    """制御指令を受信してモーターを制御"""
    speed_sub = shm_pub_sub.Subscriber("motor_speed", 0.0)
    direction_sub = shm_pub_sub.Subscriber("motor_direction", 0)
    enable_sub = shm_pub_sub.Subscriber("motor_enable", False)
    
    for i in range(20):
        # 制御指令を受信
        enable, enable_success = enable_sub.subscribe()
        speed, speed_success = speed_sub.subscribe()
        direction, direction_success = direction_sub.subscribe()
        
        if enable_success and speed_success and direction_success:
            if enable:
                direction_str = "FORWARD" if direction == 1 else "BACKWARD" if direction == -1 else "STOP"
                print(f"Motor control: Speed={speed}, Direction={direction_str}")
                
                # 実際のモーター制御処理をここに記述
                # motor_controller.set_speed(speed)
                # motor_controller.set_direction(direction)
            else:
                print("Motor disabled")
                # motor_controller.disable()
        else:
            print("Failed to receive control commands")
        
        time.sleep(0.5)

# 使用例
if __name__ == "__main__":
    import threading
    
    # 制御側と受信側を別スレッドで実行
    control_thread = threading.Thread(target=control_publisher)
    motor_thread = threading.Thread(target=control_subscriber)
    
    control_thread.start()
    motor_thread.start()
    
    control_thread.join()
    motor_thread.join()
```

## エラーハンドリングの例

### 6. 堅牢なエラーハンドリング

```python
import shm_pub_sub
import time

def robust_publisher():
    """エラーハンドリングを含むPublisher"""
    try:
        pub = shm_pub_sub.Publisher("robust_topic", 0, 3)
        
        for i in range(10):
            try:
                pub.publish(i)
                print(f"Successfully published: {i}")
            except Exception as e:
                print(f"Failed to publish data {i}: {e}")
                # 再試行logic
                time.sleep(0.1)
                try:
                    pub.publish(i)
                    print(f"Retry successful: {i}")
                except Exception as e2:
                    print(f"Retry failed: {e2}")
            
            time.sleep(1)
            
    except Exception as e:
        print(f"Publisher initialization failed: {e}")

def robust_subscriber():
    """エラーハンドリングを含むSubscriber"""
    try:
        sub = shm_pub_sub.Subscriber("robust_topic", 0)
        consecutive_failures = 0
        
        for i in range(10):
            try:
                data, is_success = sub.subscribe()
                
                if is_success:
                    print(f"Successfully received: {data}")
                    consecutive_failures = 0
                else:
                    consecutive_failures += 1
                    print(f"Failed to receive data (attempt {consecutive_failures})")
                    
                    # 連続失敗が多い場合は長めに待機
                    if consecutive_failures > 3:
                        print("Too many consecutive failures, waiting longer...")
                        time.sleep(2)
                        
            except Exception as e:
                print(f"Subscriber error: {e}")
                consecutive_failures += 1
            
            time.sleep(1)
            
    except Exception as e:
        print(f"Subscriber initialization failed: {e}")

# 使用例
if __name__ == "__main__":
    import threading
    
    pub_thread = threading.Thread(target=robust_publisher)
    sub_thread = threading.Thread(target=robust_subscriber)
    
    pub_thread.start()
    sub_thread.start()
    
    pub_thread.join()
    sub_thread.join()
```

## パフォーマンス測定の例

### 7. レイテンシ測定

```python
import shm_pub_sub
import time

def latency_test():
    """通信レイテンシを測定"""
    pub = shm_pub_sub.Publisher("latency_test", 0, 3)
    sub = shm_pub_sub.Subscriber("latency_test", 0)
    
    latencies = []
    
    for i in range(100):
        # 送信時刻を記録
        start_time = time.time()
        pub.publish(i)
        
        # 受信まで待機
        data, is_success = sub.subscribe()
        end_time = time.time()
        
        if is_success:
            latency = (end_time - start_time) * 1000  # ms
            latencies.append(latency)
            print(f"Data {i}: Latency {latency:.3f} ms")
        else:
            print(f"Failed to receive data {i}")
        
        time.sleep(0.01)
    
    if latencies:
        avg_latency = sum(latencies) / len(latencies)
        min_latency = min(latencies)
        max_latency = max(latencies)
        
        print(f"\nLatency Statistics:")
        print(f"Average: {avg_latency:.3f} ms")
        print(f"Minimum: {min_latency:.3f} ms")
        print(f"Maximum: {max_latency:.3f} ms")

if __name__ == "__main__":
    latency_test()
```

## 注意点とベストプラクティス

### 8. 使用時の注意点

1. **適切な待機時間の設定**
   ```python
   # 適切な待機時間を設定
   time.sleep(0.01)  # 10ms待機（用途に応じて調整）
   ```

2. **エラーハンドリングの実装**
   ```python
   data, is_success = sub.subscribe()
   if not is_success:
       # エラー処理を実装
       print("Data reception failed")
   ```

3. **リソースの適切な管理**
   ```python
   # Publisherは自動的にクリーンアップされるが、
   # 明示的にスコープを管理することを推奨
   with pub_context():
       pub = shm_pub_sub.Publisher("topic", 0, 3)
       # 使用後は自動的にクリーンアップ
   ```

### 9. デバッグ用ユーティリティ

```python
import shm_pub_sub
import time

def debug_monitor(topic_name, data_type_default):
    """トピックの状態をモニタリング"""
    sub = shm_pub_sub.Subscriber(topic_name, data_type_default)
    
    print(f"Monitoring topic: {topic_name}")
    print("Press Ctrl+C to stop")
    
    try:
        while True:
            data, is_success = sub.subscribe()
            timestamp = time.strftime("%H:%M:%S")
            
            if is_success:
                print(f"[{timestamp}] {topic_name}: {data}")
            else:
                print(f"[{timestamp}] {topic_name}: NO DATA")
            
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("\nMonitoring stopped")

# 使用例
if __name__ == "__main__":
    debug_monitor("temperature", 0.0)
```

## まとめ

このチュートリアルでは、SHMのPython APIを使用した様々な使用例を紹介しました。基本的な送受信から、実践的なセンサーデータ処理、制御システム、エラーハンドリング、パフォーマンス測定まで、幅広い用途に対応できることを確認できます。

実際の使用では、用途に応じて適切なバッファ数、データ型、エラーハンドリング戦略を選択してください。
