//!
//! @file shm_ps.hpp
//! @brief メモリの格納方法を規定するクラスの定義
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
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
//! @brief 共有メモリにトピックを出力する出版者を表現するクラス
//! @details template classとして与えられた型またはクラスをトピックとして出力するためのクラスである．
//! sizeofによってメモリの使用量が把握できる型およびクラスに対応している．
//! また、特殊なものはtemplate classを特殊化して対応する．
//!  
//! @note 通常であれば、生成された共有メモリはデストラクタで破棄されるべきだと考えるのが自然であるが、
//! 意図せずプログラムが再起動したような場合に共有メモリが破棄されてしまうと、値の更新が読み取れなかったり
//! 以前に送っていた指令が読み取れなくなったりするなどの問題が生じる可能性があるため、あえて破棄していない．
//! 一度確保した共有メモリにサイズの異なるデータを格納しようとするとデータが破損するため、
//! システムを再度立ち上げ直す際には共有メモリを破棄する操作を行うことを推奨する．
// ****************************************************************************
template <typename T>
class Publisher
{
public:
  Publisher(std::string name = "", int buffer_num = 3, PERM perm = DEFAULT_PERM, bool legacy = false);
  ~Publisher();
  
  void publish(const T& data);

private:
  std::string shm_name;
  int shm_buf_num;
  PERM shm_perm;
  SharedMemory *shared_memory;
  RingBuffer *ring_buffer;
  
  size_t data_size;

  //For legacy system
  uint8_t *data_ptr;
  bool is_legacy;
};

// ****************************************************************************
//! @class Subscriber
//! @brief 共有メモリからトピックを取得する購読者を表現するクラス
//! @details template classとして与えられた型またはクラスをトピックとして読み込むためのクラスである．
//! また、トピックが更新されるまで待機するAPIを持つ．
// ****************************************************************************
template <typename T>
class Subscriber
{
public:
  Subscriber(std::string name = "", bool legacy = false);
  ~Subscriber();
  
  const T subscribe(bool *state);
  bool waitFor(uint64_t timeout_usec);
  void setDataExpiryTime_us(uint64_t time_us);
  
private:
  std::string shm_name;
  SharedMemory *shared_memory;
  RingBuffer *ring_buffer;
  int current_reading_buffer;
  uint64_t data_expiry_time_us;

  //For legacy system
  uint8_t *data_ptr;
  bool is_legacy;
};

// ****************************************************************************
// 関数定義
// （テンプレートクラス内の関数の定義はコンパイル時に実体化するのでヘッダに書く）
// ****************************************************************************

//! @brief コンストラクタ
//! @param [in] name 共有メモリ名
//! @param [in] buffer_num バッファ数
//! @param [in] perm 権限情報
//! @return なし
//! @details 共有メモリオブジェクトの生成、mutexや条件変数の初期化を行う．
template <typename T>
Publisher<T>::Publisher(std::string name, int buffer_num, PERM perm, bool legacy)
: shm_name(name)
, shm_buf_num(buffer_num)
, shm_perm(perm)
, shared_memory(nullptr)
, ring_buffer(nullptr)
, data_size(sizeof(T))
, data_ptr(nullptr)
, is_legacy(legacy)
{
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Publisher: Be setted not POD class!");
  }

  if (name.empty())
  {
    throw std::runtime_error("shm::Publisher: Please set name!");
  }

  shared_memory = new SharedMemoryPosix(shm_name, O_RDWR|O_CREAT, shm_perm);
  if (!is_legacy)
  {
    shared_memory->connect(RingBuffer::getSize(sizeof(T), shm_buf_num));
  }
  else
  {
    shared_memory->connect(sizeof(T));
  }
  if (shared_memory->isDisconnected())
  {
    throw std::runtime_error("shm::Publisher: Cannot get memory!");
  }

  if (!is_legacy)
  {
    ring_buffer = new RingBuffer(shared_memory->getPtr(), sizeof(T), shm_buf_num);
  }
  else
  {
    data_ptr = shared_memory->getPtr();
  }
}


//! @brief デストラクタ
//! @return なし
//! @details 終了時の処理として共有メモリの切断を行う．
//! 
//! 
template <typename T>
Publisher<T>::~Publisher()
{
  if (ring_buffer != nullptr)
  {
    delete ring_buffer;
  }
  if (shared_memory != nullptr)
  {
    delete shared_memory;
  }
}

//! @brief トピックの書き込み
//! @param [in] data
//! @return なし
//! @details タイムスタンプが最も古いバッファにトピックを書き込み、タイムスタンプを更新する．
//! また、pthreadの条件変数を介して、待機中のプロセスに再開信号を送る．
//! @note python対応のために、boostを使用しているので、boostの条件変数でも良いかもしれない。
template <typename T>
void
Publisher<T>::publish(const T& data)
{
/*
  if (data_size != sizeof(T))
  {
    delete ring_buffer;
    ring_buffer = nullptr;
    shared_memory->disconnect();
    shared_memory->connect(RingBuffer::getSize(sizeof(T), shm_buf_num));
    ring_buffer = new RingBuffer(shared_memory->getPtr(), sizeof(T), shm_buf_num);
  }
*/
  if (!is_legacy)
  {
    int oldest_buffer = ring_buffer->getOldestBufferNum();

    (reinterpret_cast<T *>(ring_buffer->getDataList()))[oldest_buffer] = data;

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    ring_buffer->setTimestamp_us(((uint64_t) t.tv_sec * 1000000L) + ((uint64_t) t.tv_nsec / 1000L), oldest_buffer);
  
    ring_buffer->signal();
  }
  else
  {
    memcpy(data_ptr, &data, sizeof(T));
  }
}


//! @brief コンストラクタ
//! @param [in] 共有メモリ名
//! @return なし
//! @details 共有メモリへのアクセスを行う．
template <typename T>
Subscriber<T>::Subscriber(std::string name, bool legacy)
: shm_name(name)
, shared_memory(nullptr)
, ring_buffer(nullptr)
, current_reading_buffer(0)
, data_expiry_time_us(2000000)
, data_ptr(nullptr)
, is_legacy(legacy)
{
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Subscriber: Be setted not POD class!");
  }

  if (name.empty())
  {
    throw std::runtime_error("shm::Subscriber: Please set name!");
  }

  shared_memory = new SharedMemoryPosix(shm_name, O_RDWR, static_cast<PERM>(0));
}


template <typename T>
Subscriber<T>::~Subscriber()
{
  if (ring_buffer != nullptr)
  {
    delete ring_buffer;
  }
  if (shared_memory != nullptr)
  {
    delete shared_memory;
  }
}


//! @brief トピックを読み込む
//! @param なし
//! @return const T& 読み込んだトピックへのconst参照
//! @details タイムスタンプが最も新しいトピックを読み込む．
//! 後々可変長なクラスに拡張できるように、メモリへの直接的な参照を返すので、コピーコンストラクタや代入によってデータを複製することを推奨する．
template <typename T>
const T
Subscriber<T>::subscribe(bool *is_success)
{
  if (!is_legacy)
  {
    if (shared_memory->isDisconnected())
    {
      if (ring_buffer != nullptr)
      {
        delete ring_buffer;
        ring_buffer = nullptr;
      }
      shared_memory->connect();
      if (shared_memory->isDisconnected())
      {
        *is_success = false;
        return T();
      }
      ring_buffer = new RingBuffer(shared_memory->getPtr());
      ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
    }
    int newest_buffer = ring_buffer->getNewestBufferNum();
    if (newest_buffer < 0)
    {
      *is_success = false;
      return (reinterpret_cast<T*>(ring_buffer->getDataList()))[current_reading_buffer];
    }
    *is_success = true;
    current_reading_buffer = newest_buffer;
    return (reinterpret_cast<T*>(ring_buffer->getDataList()))[current_reading_buffer];
  }
  else
  {
    if (shared_memory->isDisconnected())
    {
      shared_memory->connect();
      if (shared_memory->isDisconnected())
      {
        *is_success = false;
        return T();
      }
      data_ptr = shared_memory->getPtr();
    }
    *is_success = true;
    return *reinterpret_cast<T*>(data_ptr);
  }
}


template <typename T>
bool
Subscriber<T>::waitFor(uint64_t timeout_usec)
{
  if (is_legacy)
  {
    return false;
  }

  if (shared_memory->isDisconnected())
  {
    if (ring_buffer != nullptr)
    {
      delete ring_buffer;
      ring_buffer = nullptr;
    }
    shared_memory->connect();
    if (shared_memory->isDisconnected())
    {
      return false;
    }
    ring_buffer = new RingBuffer(shared_memory->getPtr());
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
