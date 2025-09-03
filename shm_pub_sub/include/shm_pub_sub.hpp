//!
//! @file shm_pub_sub.hpp
//! @brief \~english     Class definitions for topic communication with publisher/subscriber model.
//!        \~japanese-en 出版/購読モデルによるトピック通信を規定するクラスの定義
//! @note \~english     The notation is complianted ROS Cpp style guide.
//!       \~japanese-en 記法はROSに準拠する
//!       \~            http://wiki.ros.org/ja/CppStyleGuide
//! 
//! @example test1.hpp
//! 共有メモリに関するテスト
//! @example test1.cpp
//! 共有メモリに関するテスト
//!

#ifndef __SHM_PS_LIB_H__
#define __SHM_PS_LIB_H__

#include <iostream>
#include <limits>
#include <string>
#include <regex>
#include <stdexcept>
#include <mutex>
#include <chrono>
#include <thread>
#include <cstdint>
#include <type_traits>
extern "C" {
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
}
#include "shm_base.hpp"

namespace irlab
{

namespace shm
{

// ****************************************************************************
//! @class Publisher
//! @brief   \~english     Class representing a publisher that outputs topics to shared memory
//!          \~japanese-en 共有メモリにトピックを出力する出版者を表現するクラス
//! @details \~english     This class is used to output the type or class given as template class as a topic.
//!          \~japanese-en template classとして与えられた型またはクラスをトピックとして出力するためのクラスである．
//!          \~japanese-en sizeofによってメモリの使用量が把握できる型およびクラスに対応している．
//!          \~japanese-en また、特殊なものはtemplate classを特殊化して対応する．
//!  
//! @note \~japanese-en 通常であれば、生成された共有メモリはデストラクタで破棄されるべきだと考えるのが自然であるが、
//!       \~japanese-en 意図せずプログラムが再起動したような場合に共有メモリが破棄されてしまうと、値の更新が読み取れなかったり
//!       \~japanese-en 以前に送っていた指令が読み取れなくなったりするなどの問題が生じる可能性があるため、あえて破棄していない．
//!       \~japanese-en 一度確保した共有メモリにサイズの異なるデータを格納しようとするとデータが破損するため、
//!       \~japanese-en システムを再度立ち上げ直す際には共有メモリを破棄する操作を行うことを推奨する．
// ****************************************************************************
template <typename T>
class Publisher
{
public:
  Publisher(std::string name = "", int buffer_num = 3, PERM perm = DEFAULT_PERM);
  ~Publisher() = default;

    // コピーは禁止
  Publisher(const Publisher&) = delete;
  Publisher& operator=(const Publisher&) = delete;

  // ムーブコンストラクタ：ポインタを奪い、元を nullptr に
  Publisher(Publisher&& other) noexcept = default;

  void publish(const T& data);

private:
  std::string shm_name;
  int shm_buf_num;
  PERM shm_perm;
  std::unique_ptr<SharedMemory> shared_memory;
  std::unique_ptr<RingBuffer> ring_buffer;

  size_t data_size;
};

// ****************************************************************************
//! @class Subscriber
//! @brief   \~english     Class representing a subscriber that retrieves topics from shared memory
//!          \~japanese-en 共有メモリからトピックを取得する購読者を表現するクラス
//! @details \~english     This class is used to load a type or class given as template class as a topic.
//!          \~english     It also has an API that waits until the topic is updated.
//!          \~japanese-en template classとして与えられた型またはクラスをトピックとして読み込むためのクラスである．
//!          \~japanese-en また、トピックが更新されるまで待機するAPIを持つ．
// ****************************************************************************
template <typename T>
class Subscriber
{
public:
  Subscriber(std::string name = "");
  ~Subscriber() = default;

  // コピーは禁止
  Subscriber(const Subscriber&) = delete;
  Subscriber& operator=(const Subscriber&) = delete;

  // ムーブコンストラクタ：ポインタを奪い、元を nullptr に
  Subscriber(Subscriber&& other) noexcept = default;
  
  const T subscribe(bool *state);
  bool waitFor(uint64_t timeout_usec);
  void setDataExpiryTime_us(uint64_t time_us);
  
private:
  std::string shm_name;
  std::unique_ptr<SharedMemory> shared_memory;
  std::unique_ptr<RingBuffer> ring_buffer;
  int current_reading_buffer;
  uint64_t data_expiry_time_us;
};

// ****************************************************************************
// Function Definications
// 関数定義
// （テンプレートクラス内の関数の定義はコンパイル時に実体化するのでヘッダに書く）
// ****************************************************************************

//! @brief \~english     Constructor
//!        \~japanese-en コンストラクタ
//! @param [in] name       \~english     Shared-memory name
//!                        \~japanese-en 共有メモリ名
//! @param [in] buffer_num \~english     Number of Buffers
//!                        \~japanese-en バッファ数
//! @param [in] perm       \~english     Permission infomation
//!                        \~japanese-en 権限情報
//! @return                \~english     None
//!                        \~japanese-en なし
//! @details \~english     Create shared memory objects and initialize mutex and condition variables.
//!          \~japanese-en 共有メモリオブジェクトの生成、mutexや条件変数の初期化を行う．
template <typename T>
Publisher<T>::Publisher(std::string name, int buffer_num, PERM perm)
: shm_name(name)
, shm_buf_num(buffer_num)
, shm_perm(perm)
, shared_memory(nullptr)
, ring_buffer(nullptr)
, data_size(sizeof(T))
{
  // アライメントチェックを追加
  static_assert(alignof(T) <= 8, "Type alignment must be <= 8 bytes for shared memory compatibility");
  
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Publisher: Be setted not POD class!");
  }

  if (name.empty())
  {
    throw std::runtime_error("shm::Publisher: Please set name!");
  }

  try {
    shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR | O_CREAT, shm_perm);
    shared_memory->connect(RingBuffer::getSize(sizeof(T), shm_buf_num));

    if (shared_memory->isDisconnected())
    {
      throw std::runtime_error("shm::Publisher: Cannot get memory!");
    }

    // メモリポインタの有効性チェックを強化
    void* mem_ptr = shared_memory->getPtr();
    if (mem_ptr == nullptr) {
      throw std::runtime_error("shm::Publisher: Shared memory pointer is null!");
    }
    
    // アライメントチェック
    if (reinterpret_cast<uintptr_t>(mem_ptr) % alignof(std::max_align_t) != 0) {
      throw std::runtime_error("shm::Publisher: Shared memory is not properly aligned!");
    }

    ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr(), sizeof(T), shm_buf_num);
    
    // 初期化完了まで待機
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    
    // データリストポインタの有効性確認
    void* data_list = ring_buffer->getDataList();
    if (data_list == nullptr) {
      throw std::runtime_error("shm::Publisher: Ring buffer data list is null!");
    }
    
  } catch (const std::runtime_error& e) {
    throw std::runtime_error("shm::Publisher: " + std::string(e.what()));
  }
}


//! @brief \~english     Publish a topic
//!        \~japanese-en トピックの書き込み
//! @param [in] data
//! @return  \~english     None
//!          \~japanese-en なし
//! @details \~english     Writes the topic to the buffer with the oldest timestamp and updates the timestamp.
//!          \~english     It also sends a resume signal to the waiting process via a pthread condition variable.
//!          \~japanese-en タイムスタンプが最も古いバッファにトピックを書き込み、タイムスタンプを更新する．
//!          \~japanese-en また、pthreadの条件変数を介して、待機中のプロセスに再開信号を送る．
template <typename T>
void
Publisher<T>::publish(const T& data)
{
  if (!ring_buffer || !shared_memory || shared_memory->isDisconnected()) {
    throw std::runtime_error("shm::Publisher: Not connected to shared memory!");
  }
  
  int oldest_buffer = ring_buffer->getOldestBufferNum();
  for (size_t i = 0; i < 10; i++)
  {
    if (ring_buffer->allocateBuffer(oldest_buffer))
    {
      break;
    }
    usleep(1000); // Wait for 1ms
    oldest_buffer = ring_buffer->getOldestBufferNum();
  }

  // バッファ番号の範囲チェック
  if (oldest_buffer < 0 || oldest_buffer >= shm_buf_num) {
    throw std::runtime_error("shm::Publisher: Invalid buffer number!");
  }

  // データリストの有効性チェック
  void* data_list_ptr = ring_buffer->getDataList();
  if (data_list_ptr == nullptr) {
    throw std::runtime_error("shm::Publisher: Data list pointer is null!");
  }

  // 安全なキャストとコピー
  T* typed_data_list = reinterpret_cast<T*>(data_list_ptr);
  try {
    typed_data_list[oldest_buffer] = data;
  } catch (const std::exception& e) {
    throw std::runtime_error("shm::Publisher: Failed to write data - " + std::string(e.what()));
  }

  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC_RAW, &t);
  ring_buffer->setTimestamp_us(((uint64_t) t.tv_sec * 1000000L) + ((uint64_t) t.tv_nsec / 1000L), oldest_buffer);
  
  ring_buffer->signal();
}


//! @brief \~english     Constructor
//!        \~japanese-en コンストラクタ
//! @param [in] name \~english     Shared-memory name
//!                  \~japanese-en 共有メモリ名
//! @return  \~english     None
//!          \~japanese-en なし
//! @details \~english     Access to shared memory.
//!          \~japanese-en 共有メモリへのアクセスを行う．
template <typename T>
Subscriber<T>::Subscriber(std::string name)
: shm_name(name)
, shared_memory(nullptr)
, ring_buffer(nullptr)
, current_reading_buffer(0)
, data_expiry_time_us(2000000)
{
  // アライメントチェック
  static_assert(alignof(T) <= 8, "Type alignment must be <= 8 bytes for shared memory compatibility");
  
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Subscriber: Be setted not POD class!");
  }

  if (name.empty())
  {
    throw std::runtime_error("shm::Subscriber: Please set name!");
  }

  try {
    shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR, static_cast<PERM>(0));
  } catch (const std::runtime_error& e) {
    throw std::runtime_error("shm::Subscriber: " + std::string(e.what()));
  }
}


//! @brief \~english     Subscribe a topic
//!        \~japanese-en トピックを読み込む
//! @param None \~japanese-en なし
//! @return const T& \~english     Const reference to the loaded topic.
//!                  \~japanese-en 読み込んだトピックへのconst参照
//! @details         \~english     The topic with the most recent timestamp is loaded.
//!                  \~english     It is recommended to duplicate the data by copy constructor or assignment, since it returns a direct reference to memory so that it can be later extended to variable-length classes.
//!                  \~japanese-en タイムスタンプが最も新しいトピックを読み込む．
//!                  \~japanese-en 後々可変長なクラスに拡張できるように、メモリへの直接的な参照を返すので、コピーコンストラクタや代入によってデータを複製することを推奨する．
template <typename T>
const T
Subscriber<T>::subscribe(bool *is_success)
{
  if (shared_memory->isDisconnected())
  {
    if (ring_buffer != nullptr)
    {
      ring_buffer.reset();
    }
    shared_memory->connect();
    if (shared_memory->isDisconnected())
    {
      *is_success = false;
      return T();
    }
    try {
      void* mem_ptr = shared_memory->getPtr();
      if (mem_ptr == nullptr) {
        *is_success = false;
        return T();
      }
      
      // アライメントチェック
      if (reinterpret_cast<uintptr_t>(mem_ptr) % alignof(std::max_align_t) != 0) {
        *is_success = false;
        return T();
      }
      
      // 初期化完了まで待機（タイムアウト時間を増加）
      if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000)) { // 500ms timeout (increased)
        *is_success = false;
        return T();
      }
      ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr());
      
      // データリストの有効性確認
      void* data_list = ring_buffer->getDataList();
      if (data_list == nullptr) {
        *is_success = false;
        return T();
      }
      
    } catch (const std::exception& e)
    {
      *is_success = false;
      return T();
    }
    ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  }
  
  int newest_buffer = ring_buffer->getNewestBufferNum();
  if (newest_buffer < 0)
  {
    *is_success = false;
    // 現在のバッファ番号の範囲チェック
    if (current_reading_buffer < 0) {
      return T();
    }
    void* data_list_ptr = ring_buffer->getDataList();
    if (data_list_ptr == nullptr) {
      return T();
    }
    return (reinterpret_cast<T*>(data_list_ptr))[current_reading_buffer];
  }
  
  *is_success = true;
  current_reading_buffer = newest_buffer;
  
  void* data_list_ptr = ring_buffer->getDataList();
  if (data_list_ptr == nullptr) {
    *is_success = false;
    return T();
  }
  
  return (reinterpret_cast<T*>(data_list_ptr))[current_reading_buffer];
}


template <typename T>
bool
Subscriber<T>::waitFor(uint64_t timeout_usec)
{
  if (shared_memory->isDisconnected())
  {
    if (ring_buffer != nullptr)
    {
      ring_buffer.reset();
    }
    shared_memory->connect();
    if (shared_memory->isDisconnected())
    {
      return false;
    }
    
    // メモリポインタの有効性チェック
    void* mem_ptr = shared_memory->getPtr();
    if (mem_ptr == nullptr) {
      return false;
    }
    
    // アライメントチェック
    if (reinterpret_cast<uintptr_t>(mem_ptr) % alignof(std::max_align_t) != 0) {
      return false;
    }
    
    // Wait for initialization to complete
    if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000)) { // 500ms timeout (increased)
      return false;
    }

    try {
      ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr());
    } catch (const std::exception& e) {
      return false;
    }
    ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  }

  return ring_buffer->waitFor(timeout_usec);
}


template <typename T>
void
Subscriber<T>::setDataExpiryTime_us(uint64_t time_us)
{
  data_expiry_time_us = time_us;
  if (ring_buffer != nullptr)
  {
    ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  }
}


}

}

#endif /* __SHM_PS_LIB_H__ */
