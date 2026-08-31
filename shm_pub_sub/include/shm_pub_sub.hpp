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
#include <cstring>
#include <regex>
#include <stdexcept>
#include <mutex>
#include <chrono>
#include <thread>
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
//!       \~japanese-en
//!       意図せずプログラムが再起動したような場合に共有メモリが破棄されてしまうと、値の更新が読み取れなかったり
//!       \~japanese-en
//!       以前に送っていた指令が読み取れなくなったりするなどの問題が生じる可能性があるため、あえて破棄していない．
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
  Publisher(const Publisher &)            = delete;
  Publisher &operator=(const Publisher &) = delete;

  // ムーブコンストラクタ：ポインタを奪い、元を nullptr に
  Publisher(Publisher &&other) noexcept = default;

  void publish(const T &data);

private:
  void connectAndPrepare();

  std::string                   shm_name;
  int                           shm_buf_num;
  PERM                          shm_perm;
  std::unique_ptr<SharedMemory> shared_memory;
  std::unique_ptr<RingBuffer>   ring_buffer;

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
  Subscriber(const Subscriber &)            = delete;
  Subscriber &operator=(const Subscriber &) = delete;

  // ムーブコンストラクタ：ポインタを奪い、元を nullptr に
  Subscriber(Subscriber &&other) noexcept = default;

  const T &subscribe(bool *state);
  bool     waitFor(uint64_t timeout_usec);
  void     setDataExpiryTime_us(uint64_t time_us);
  // 共有メモリが存在し、初期化済みかを確認。未接続なら接続を試み、初期化を待つ。ring_bufferは作らない。
  bool existsPublisherMemory();

  // 競合カウンタ: publisher の書き込みが購読側の読み出しに対して速すぎる
  // （コピー中にリングを一周して上書きされる）状況の検出用。
  // retry はコピーのやり直しが起きた累積回数、failure はリトライ上限まで
  // 一貫した読み出しができず subscribe が失敗した累積回数。
  // 正常なレート設計ではどちらも 0 に留まる。
  uint64_t getContentionRetryCount() const { return contention_retry_count_; }
  uint64_t getContentionFailureCount() const { return contention_failure_count_; }
  void     resetContentionCounts()
  {
    contention_retry_count_   = 0;
    contention_failure_count_ = 0;
  }

private:
  std::string                   shm_name;
  std::unique_ptr<SharedMemory> shared_memory;
  std::unique_ptr<RingBuffer>   ring_buffer;
  int                           current_reading_buffer;
  uint64_t                      data_expiry_time_us;
  // 返り値はダブルバッファで持つ。読み出しは常に「今返していない方」へ行い、
  // 一貫性を確認できたときだけ有効な側を入れ替える。こうしないと、失敗した
  // subscribe() が直前に返した値を上書きしてしまう（const T& を返すため、
  // 呼び出し側が保持している参照の中身が黙って壊れる）。
  T                             return_buffers_[2];
  int                           return_index_;
  uint64_t                      contention_retry_count_   = 0;
  uint64_t                      contention_failure_count_ = 0;
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
  // Enhanced type checking for shared memory compatibility
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Publisher: Type must have standard layout for shared memory!");
  }

  // Only enforce strict requirements on ARM platforms
  if constexpr (is_arm_platform())
  {
    if (!std::is_trivially_copyable<T>::value)
    {
      throw std::runtime_error("shm::Publisher: Type must be trivially copyable for ARM compatibility!");
    }

    // Check alignment requirements for ARM processors
    if (get_alignment<T>() > alignof(::max_align_t))
    {
      throw std::runtime_error("shm::Publisher: Type requires alignment beyond max_align_t on ARM!");
    }
  }

  if (name.empty())
  {
    throw std::runtime_error("shm::Publisher: Please set name!");
  }

  // 負値は size_t へ変換されて巨大なループ／オフセットになり、0 は未初期化の
  // ヘッダを読む経路に落ちる。どちらも境界で弾く（R01-F06）。
  if (buffer_num <= 0 || static_cast<size_t>(buffer_num) > RingBuffer::MAX_BUFFER_NUM)
  {
    throw std::runtime_error("shm::Publisher: buffer_num must be in [1, " +
                             std::to_string(RingBuffer::MAX_BUFFER_NUM) + "], but got " +
                             std::to_string(buffer_num));
  }

  try
  {
    shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR | O_CREAT, shm_perm);
    connectAndPrepare();
  }
  catch (const std::runtime_error &e)
  {
    throw std::runtime_error("shm::Publisher: " + std::string(e.what()));
  }
}

//! @brief \~japanese-en 共有メモリへ接続し、リングバッファを用意する
//! @return  \~japanese-en なし
//! @details \~japanese-en コンストラクタと、レイアウト変更を検知した publish() から呼ばれる．
template <typename T>
void
Publisher<T>::connectAndPrepare()
{
  shared_memory->connect(RingBuffer::getSize(sizeof(T), shm_buf_num));

  if (shared_memory->isDisconnected())
  {
    throw std::runtime_error("shm::Publisher: Cannot get memory!");
  }

  ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr(), sizeof(T), shm_buf_num);

  // Enhanced initialization synchronization for ARM processors
  // Wait for pthread structures to be properly initialized
  uint64_t       start_time = getCurrentTimeUSec();
  const uint64_t timeout    = 1000;  // 1 second timeout

  while (getCurrentTimeUSec() - start_time < timeout)
  {
    if (RingBuffer::checkInitialized(shared_memory->getPtr()))
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  if (!RingBuffer::checkInitialized(shared_memory->getPtr()))
  {
    throw std::runtime_error("shm::Publisher: RingBuffer initialization timeout");
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
Publisher<T>::publish(const T &data)
{
  // 別のプロセスが異なるレイアウトで初期化し直していないか確認する。
  // バッファ数は共有メモリ上の値 (*buf_num) を見て走査する一方、書き込み位置は
  // 自分が構築時に計算したオフセットを使うため、食い違ったまま書くと
  // 確保していない位置——場合によってはマッピングの外——へ書き込むことになる。
  // バッファ数が増えていればファイルも ftruncate で伸びているので、
  // リングバッファだけでなく共有メモリごと張り直す。
  if (shared_memory->isDisconnected() || ring_buffer == nullptr || ring_buffer->isLayoutChanged())
  {
    ring_buffer.reset();
    shared_memory->disconnect();
    connectAndPrepare();
  }

  // 確保できないままの書き込みは、他の writer が書き込み途中のバッファを
  // 破壊し購読可能にしてしまうため許されない。確保成功を必須とする。
  int  oldest_buffer = -1;
  bool allocated     = false;
  for (size_t i = 0; i < 10; i++)
  {
    oldest_buffer = ring_buffer->getOldestBufferNum();
    if (ring_buffer->allocateBuffer(oldest_buffer))
    {
      allocated = true;
      break;
    }
    usleep(1000);  // Wait for 1ms
  }
  if (!allocated)
  {
    throw std::runtime_error("shm::Publisher: Could not allocate a buffer (all buffers are in use)!");
  }

  // Cross-platform aligned memory access
  unsigned char *data_ptr      = ring_buffer->getDataList();
  size_t         buffer_offset = oldest_buffer * sizeof(T);

  if constexpr (is_arm_platform())
  {
    // ARM: Use memcpy for safer memory access
    if (!irlab::shm::is_aligned<T>(data_ptr + buffer_offset))
    {
      // Use memcpy for unaligned access on ARM
      std::memcpy(data_ptr + buffer_offset, &data, sizeof(T));
    }
    else
    {
      T *typed_ptr = irlab::shm::align_pointer<T>(data_ptr + buffer_offset);
      *typed_ptr   = data;
    }
  }
  else
  {
    // x86/x64: Direct cast is safe
    T *typed_ptr = reinterpret_cast<T *>(data_ptr + buffer_offset);
    *typed_ptr   = data;
  }

  // struct timespec t;
  // clock_gettime(CLOCK_MONOTONIC_RAW, &t);
  // ring_buffer->setTimestamp_us(((uint64_t) t.tv_sec * 1000000L) + ((uint64_t) t.tv_nsec / 1000L), oldest_buffer);

  uint64_t current_time_us = getCurrentTimeUSec();
  ring_buffer->setTimestamp_us(current_time_us, oldest_buffer);

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
  , return_buffers_{}
  , return_index_(0)
{
  // Enhanced type checking for shared memory compatibility
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Subscriber: Type must have standard layout for shared memory!");
  }

  // Only enforce strict requirements on ARM platforms
  if constexpr (is_arm_platform())
  {
    if (!std::is_trivially_copyable<T>::value)
    {
      throw std::runtime_error("shm::Subscriber: Type must be trivially copyable for ARM compatibility!");
    }

    // Check alignment requirements for ARM processors
    if (get_alignment<T>() > alignof(::max_align_t))
    {
      throw std::runtime_error("shm::Subscriber: Type requires alignment beyond max_align_t on ARM!");
    }
  }

  if (name.empty())
  {
    throw std::runtime_error("shm::Subscriber: Please set name!");
  }

  try
  {
    shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR, static_cast<PERM>(0));
  }
  catch (const std::runtime_error &e)
  {
    throw std::runtime_error("shm::Subscriber: " + std::string(e.what()));
  }
}

//! @brief \~english     Subscribe a topic
//!        \~japanese-en トピックを読み込む
//! @param None \~japanese-en なし
//! @return const T& \~english     Const reference to the loaded topic.
//!                  \~japanese-en 読み込んだトピックへのconst参照
//! @details         \~english     The topic with the most recent timestamp is loaded.
//!                  \~english     It is recommended to duplicate the data by copy constructor or assignment, since it
//!                  returns a direct reference to memory so that it can be later extended to variable-length classes.
//!                  \~japanese-en タイムスタンプが最も新しいトピックを読み込む．
//!                  \~japanese-en
//!                  後々可変長なクラスに拡張できるように、メモリへの直接的な参照を返すので、コピーコンストラクタや代入によってデータを複製することを推奨する．
template <typename T>
const T &
Subscriber<T>::subscribe(bool *is_success)
{
  // 別のプロセスが異なるレイアウトで初期化し直していないか確認する。
  // バッファ数が増えていると共有メモリのファイル自体が ftruncate で伸びており、
  // 古いマッピングのままでは新しいデータ位置がマッピングの外に出る。
  // リングバッファだけでなく共有メモリごと張り直す必要がある。
  if (!shared_memory->isDisconnected() && ring_buffer != nullptr && ring_buffer->isLayoutChanged())
  {
    ring_buffer.reset();
    shared_memory->disconnect();
  }

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
      return return_buffers_[return_index_];
    }
    try
    {
      if (shared_memory->getPtr() == nullptr)
      {
        *is_success = false;
        return return_buffers_[return_index_];
      }
      // Wait for initialization to complete
      if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000))
      {  // 500ms timeout (increased)
        *is_success = false;
        return return_buffers_[return_index_];
      }
      // 切り詰められた／別形式の共有メモリに接続するとマッピング外を指す
      // ポインタが作られるため、必ず検証つきの接続を通す（R01-F06）
      ring_buffer = attachRingBuffer(*shared_memory);
      if (ring_buffer == nullptr)
      {
        // マッピングが古い（他プロセスがレイアウトを広げた）可能性があるため、
        // 共有メモリごと切断して次回に張り直させる。ここで居座ると
        // ring_buffer == nullptr のまま isLayoutChanged() の経路に入れない。
        shared_memory->disconnect();
        *is_success = false;
        return return_buffers_[return_index_];
      }
    }
    catch (const std::bad_alloc &e)
    {
      *is_success = false;
      return return_buffers_[return_index_];
    }
    ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  }
  // 既に接続済みだが ring_buffer が未初期化の場合に対応
  else if (ring_buffer == nullptr)
  {
    // 初期化途中のレイアウトを読むと誤ったオフセットを掴むため完了を待つ
    if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000))
    {
      *is_success = false;
      return return_buffers_[return_index_];
    }
    try
    {
      ring_buffer = attachRingBuffer(*shared_memory);
      if (ring_buffer == nullptr)
      {
        // マッピングが古い（他プロセスがレイアウトを広げた）可能性があるため、
        // 共有メモリごと切断して次回に張り直させる。ここで居座ると
        // ring_buffer == nullptr のまま isLayoutChanged() の経路に入れない。
        shared_memory->disconnect();
        *is_success = false;
        return return_buffers_[return_index_];
      }
      ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
    }
    catch (const std::bad_alloc &e)
    {
      *is_success = false;
      return return_buffers_[return_index_];
    }
  }

  // seqlock 方式の読み出し: バッファ選択 → コピー → タイムスタンプ再確認。
  // コピー中に publisher がリングを一周して同じバッファを再確保・上書きすると
  // タイムスタンプが変化するため、変化を検出したら選択からやり直す。
  // これを行わないと新旧データの混ざった値 (torn read) を返すことがある。
  constexpr int MAX_READ_RETRY = 5;
  bool          no_data        = false;
  for (int attempt = 0; attempt < MAX_READ_RETRY; ++attempt)
  {
    int newest_buffer = ring_buffer->getNewestBufferNum();
    if (newest_buffer < 0)
    {
      no_data = true;
      break;
    }

    uint64_t timestamp_before = ring_buffer->getTimestamp_us(newest_buffer);
    if (RingBuffer::isBeingWritten(timestamp_before) || timestamp_before == 0)
    {
      // 選択直後に書き換えが始まった（または初期化された）
      ++contention_retry_count_;
      continue;
    }

    // Cross-platform aligned memory access
    unsigned char *data_ptr      = ring_buffer->getDataList();
    size_t         buffer_offset = newest_buffer * sizeof(T);

    // 今返していない側へ読み込む。失敗しても直前に返した値は壊れない。
    T &scratch = return_buffers_[1 - return_index_];

    if constexpr (is_arm_platform())
    {
      // ARM: Use safer memory copy approach
      if (!irlab::shm::is_aligned<T>(data_ptr + buffer_offset))
      {
        // Use memcpy for unaligned access on ARM
        std::memcpy(&scratch, data_ptr + buffer_offset, sizeof(T));
      }
      else
      {
        T *typed_ptr = irlab::shm::align_pointer<T>(data_ptr + buffer_offset);
        scratch      = *typed_ptr;
      }
    }
    else
    {
      // x86/x64: Direct cast is safe
      T *typed_ptr = reinterpret_cast<T *>(data_ptr + buffer_offset);
      scratch      = *typed_ptr;
    }

    // コピーの読み込みが完了してからタイムスタンプを再読みする（load-load 順序の固定）
    std::atomic_thread_fence(std::memory_order_acquire);

    if (ring_buffer->getTimestamp_us(newest_buffer) == timestamp_before)
    {
      *is_success            = true;
      current_reading_buffer = newest_buffer;
      return_index_          = 1 - return_index_;
      return return_buffers_[return_index_];
    }
    // コピー中に上書きされた → やり直し
    ++contention_retry_count_;
  }

  // 一貫したスナップショットを取得できなかった。返るのは直前に成功した値
  // （一度も成功していなければ T の既定値）なので、is_success を必ず確認すること。
  if (!no_data)
  {
    ++contention_failure_count_;
  }
  *is_success = false;
  return return_buffers_[return_index_];
}

template <typename T>
bool
Subscriber<T>::waitFor(uint64_t timeout_usec)
{
  // 別のプロセスが異なるレイアウトで初期化し直していないか確認する。
  // バッファ数が増えていると共有メモリのファイル自体が ftruncate で伸びており、
  // 古いマッピングのままでは新しいデータ位置がマッピングの外に出る。
  // リングバッファだけでなく共有メモリごと張り直す必要がある。
  if (!shared_memory->isDisconnected() && ring_buffer != nullptr && ring_buffer->isLayoutChanged())
  {
    ring_buffer.reset();
    shared_memory->disconnect();
  }

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

    // Wait for initialization to complete
    if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000))
    {  // 500ms timeout (increased)
      return false;
    }

    ring_buffer = attachRingBuffer(*shared_memory);
    if (ring_buffer == nullptr)
    {
      // 理由は subscribe() 側の同じ箇所のコメントを参照
      shared_memory->disconnect();
      return false;
    }
    ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  }
  // 既に接続済みだが ring_buffer が未初期化の場合に対応
  else if (ring_buffer == nullptr)
  {
    // 初期化途中のレイアウトを読むと誤ったオフセットを掴むため完了を待つ
    if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000))
    {
      return false;
    }
    try
    {
      ring_buffer = attachRingBuffer(*shared_memory);
      if (ring_buffer == nullptr)
      {
        // マッピングが古い（他プロセスがレイアウトを広げた）可能性があるため、
        // 共有メモリごと切断して次回に張り直させる。ここで居座ると
        // ring_buffer == nullptr のまま isLayoutChanged() の経路に入れない。
        shared_memory->disconnect();
        return false;
      }
      ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
    }
    catch (const std::bad_alloc &e)
    {
      return false;
    }
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

template <typename T>
bool
Subscriber<T>::existsPublisherMemory()
{
  // safety check
  if (shared_memory == nullptr)
  {
    return false;
  }

  // 既に接続済みで初期化済みなら true
  if (!shared_memory->isDisconnected())
  {
    unsigned char *ptr = shared_memory->getPtr();
    if (ptr != nullptr && RingBuffer::checkInitialized(ptr))
    {
      return true;
    }
  }

  // 未接続の場合：OS レベルで共有メモリの存在確認のみ
  // connect() は呼ばず、exists() で存在と初期化を確認
  // ファイルが存在して未初期化なら、タイムアウト付きで初期化待ち
  return shared_memory->isExists(500000);  // 500ms timeout
}

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_PS_LIB_H__ */
