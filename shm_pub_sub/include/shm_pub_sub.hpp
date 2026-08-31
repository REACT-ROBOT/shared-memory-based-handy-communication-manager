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
  std::string               shm_name;
  int                       shm_buf_num;
  PERM                      shm_perm;
  //! 共有メモリの世代管理は ShmTopic が引き受ける。
  //! connect / disconnect / RingBuffer の作り直しをここに書かないこと
  //! （4 箇所に複製されて食い違う原因になっていた）。
  std::unique_ptr<ShmTopic> topic;

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
  std::string               shm_name;
  std::unique_ptr<ShmTopic> topic;
  int                       current_reading_buffer;
  uint64_t                  data_expiry_time_us;
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
  , topic(nullptr)
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
    topic = std::make_unique<ShmTopic>(shm_name, shm_perm, true);
    if (!topic->ensureCapacity(sizeof(T), shm_buf_num, alignof(T)))
    {
      throw std::runtime_error(topic->lastError());
    }
  }
  catch (const std::runtime_error &e)
  {
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
Publisher<T>::publish(const T &data)
{
  // 世代の追随はここに集約されている。以前は「レイアウト変更を検知したら
  // 自分で disconnect して作り直す」処理を publish の冒頭に書いていたが、
  // 検知と実データアクセスの間に必ず窓が空いた（R01-F01 の TOCTOU）。
  // 形式 v3 では稼働中のセグメントを作り直さず、新しい世代を別セグメントとして
  // 作るので、古い世代を掴んだままでも範囲外アクセスにはならない。
  if (!topic->ensureCapacity(sizeof(T), shm_buf_num, alignof(T)))
  {
    throw std::runtime_error("shm::Publisher: " + topic->lastError());
  }
  RingBuffer *ring_buffer = topic->ring();

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
    throw std::runtime_error(
        "shm::Publisher: Could not allocate a buffer (all buffers are in use). "
        "buffer_num must be greater than the number of concurrent publishers on this topic.");
  }

  // 書き込みは memcpy に統一する。
  // 以前は x86 で *reinterpret_cast<T*>(ptr) = data としていたが、これは
  // T が構築されていない領域に対して代入演算子を走らせる未定義動作であり、
  // かつコンパイラが aligned 命令を選ぶと alignas(16) 以上の型で SIGSEGV し得た。
  // trivially copyable な型に対しては memcpy が正しい操作で、アライメント要求も
  // 無い。結果として ARM/x86 の分岐そのものが不要になる（R01-F07-a）。
  unsigned char *data_ptr      = ring_buffer->getDataList();
  size_t         buffer_offset = static_cast<size_t>(oldest_buffer) * ring_buffer->getElementSize();
  std::memcpy(data_ptr + buffer_offset, &data, sizeof(T));

  // 発行番号の採番とスロットの解放。番号はここで採るので、
  // 「番号が小さい＝先にコミットされた」が常に成り立つ。
  ring_buffer->commitBuffer(oldest_buffer, sizeof(T));

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
  , topic(nullptr)
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
    topic = std::make_unique<ShmTopic>(shm_name, static_cast<PERM>(0), false);
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
  // 現在有効な世代へ追随する。世代が進んでいれば新しいセグメントへ張り直す。
  // 古い世代を掴んだままでもマッピングは有効なので、範囲外アクセスにはならない。
  if (!topic->follow())
  {
    *is_success = false;
    return return_buffers_[return_index_];
  }
  RingBuffer *ring_buffer = topic->ring();
  ring_buffer->setDataExpiryTime_us(data_expiry_time_us);

  // seqlock 方式の読み出し: バッファ選択 → コピー → 発行番号の再確認。
  // コピー中に publisher がリングを一周して同じバッファを再確保・上書きすると
  // 発行番号が変化するため、変化を検出したら選択からやり直す。
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

  // 整合性の検証には時刻ではなく発行番号を使う。発行番号は単一の atomic から
    // fetch_add で採番され再利用されないので、同一 microsecond の publish で
    // 前後の値が一致してしまう ABA が原理的に起きない（R01-F05）。
    uint64_t sequence_before = ring_buffer->getSequence(newest_buffer);
    if (sequence_before == 0)
    {
      // 選択直後に書き換えが始まった（または初期化された）
      ++contention_retry_count_;
      continue;
    }

    unsigned char *data_ptr      = ring_buffer->getDataList();
    size_t         buffer_offset = static_cast<size_t>(newest_buffer) * ring_buffer->getElementSize();

    // 今返していない側へ読み込む。失敗しても直前に返した値は壊れない。
    T &scratch = return_buffers_[1 - return_index_];
    std::memcpy(&scratch, data_ptr + buffer_offset, sizeof(T));

    // コピーの読み込みが完了してから発行番号を再読みする（load-load 順序の固定）
    std::atomic_thread_fence(std::memory_order_acquire);

    if (ring_buffer->getSequence(newest_buffer) == sequence_before)
    {
      *is_success            = true;
      current_reading_buffer = newest_buffer;
      return_index_          = 1 - return_index_;
      ring_buffer->markAsRead(sequence_before);
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
  if (!topic->follow())
  {
    return false;
  }
  RingBuffer *ring_buffer = topic->ring();
  ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  return ring_buffer->waitFor(timeout_usec);
}

template <typename T>
void
Subscriber<T>::setDataExpiryTime_us(uint64_t time_us)
{
  data_expiry_time_us = time_us;
  if (topic->ring() != nullptr)
  {
    topic->ring()->setDataExpiryTime_us(data_expiry_time_us);
  }
}

template <typename T>
bool
Subscriber<T>::existsPublisherMemory()
{
  // 共有メモリが存在し、有効な世代が公開されているかを確認する
  return topic != nullptr && topic->follow();
}

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_PS_LIB_H__ */
