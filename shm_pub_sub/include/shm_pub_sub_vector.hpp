//!
//! @file shm_ps_vector.hpp
//! @brief メモリの格納方法を規定するクラスの定義
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
//!

#ifndef __SHM_PS_VECTOR_LIB_H__
#define __SHM_PS_VECTOR_LIB_H__

#include <iostream>
#include <limits>
#include <string>
#include <regex>
#include <stdexcept>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
#include <cstring>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"

namespace irlab
{

namespace shm
{

// ****************************************************************************
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
template <class T>
class Publisher<std::vector<T>>
{
public:
  Publisher(std::string name = "", int buffer_num = 3, PERM perm = DEFAULT_PERM);
  ~Publisher() = default;

  void publish(const std::vector<T> &data);
  void _publish(const std::vector<T> data);

private:
  std::string               shm_name;
  int                       shm_buf_num;
  PERM                      shm_perm;
  std::unique_ptr<ShmTopic> topic;

  size_t vector_size;
};

// ****************************************************************************
//! @brief 共有メモリからトピックを取得する購読者を表現するクラス
//! @details template classとして与えられた型またはクラスをトピックとして読み込むためのクラスである．
//! また、トピックが更新されるまで待機するAPIを持つ．
// ****************************************************************************
template <typename T>
class Subscriber<std::vector<T>>
{
public:
  Subscriber(std::string name = "");
  ~Subscriber() = default;

  const std::vector<T> &subscribe(bool *is_success);
  //! @brief 最新のデータを読み、素性も受け取る（詳細は Subscriber<T> 本体のコメントを参照）
  const std::vector<T> &subscribe(bool *is_success, SampleInfo *info);
  //! @brief 別トピックのサンプルに時刻を合わせて読む（詳細は Subscriber<T> 本体のコメントを参照）
  const std::vector<T> &subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status,
                                           SampleInfo *info = nullptr, uint64_t max_skew_us = 0);

  //! @brief 指定した時刻のデータを読む（詳細は Subscriber<T> 本体のコメントを参照）
  const std::vector<T> &subscribeAt(const TimeQuery &query, SearchStatus *status, SampleInfo *info = nullptr);
  //! @brief 現在保持している範囲（引ける時刻の範囲）
  RetentionWindow getRetentionWindow();

  bool                  waitFor(uint64_t timeout_usec);
  void                  setDataExpiryTime_us(uint64_t time_us);

  // 競合カウンタ（詳細は Subscriber<T> 本体のコメントを参照）
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

  size_t         vector_size;
  // 返り値はダブルバッファで持つ。理由はスカラ版と同じ（失敗した subscribe() が
  // 直前に返した値を壊さないようにするため）。
  std::vector<T> return_buffers_[2];
  int            return_index_;
  uint64_t       contention_retry_count_   = 0;
  uint64_t       contention_failure_count_ = 0;
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
Publisher<std::vector<T>>::Publisher(std::string name, int buffer_num, PERM perm)
  : shm_name(name)
  , shm_buf_num(buffer_num)
  , shm_perm(perm)
  , topic(nullptr)
  , vector_size(0)
{
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Publisher: Be setted not POD class in vector!");
  }

  // 要素のアライメント要求がレイアウトで保証できる範囲か確認する
  // （理由はスカラ版の同じ判定のコメントを参照）
  if (alignof(T) > RingBuffer::MAX_PAYLOAD_ALIGNMENT)
  {
    throw std::runtime_error("shm::Publisher: Element type requires alignment " + std::to_string(alignof(T)) +
                             ", which exceeds the maximum the shared memory layout can guarantee (" +
                             std::to_string(RingBuffer::MAX_PAYLOAD_ALIGNMENT) + ")");
  }

  if (name.empty())
  {
    throw std::runtime_error("shm::Publisher: Please set name!");
  }

  // 負値・過大値は境界で弾く（R01-F06）
  if (buffer_num <= 0 || static_cast<size_t>(buffer_num) > RingBuffer::MAX_BUFFER_NUM)
  {
    throw std::runtime_error("shm::Publisher: buffer_num must be in [1, " +
                             std::to_string(RingBuffer::MAX_BUFFER_NUM) + "], but got " +
                             std::to_string(buffer_num));
  }

  topic = std::make_unique<ShmTopic>(shm_name, shm_perm, true);
  // 長さは最初の publish で決まる。ここでは容量 0 で世代を用意しておく。
  if (!topic->ensureCapacity(0, shm_buf_num, alignof(T)))
  {
    throw std::runtime_error("shm::Publisher: " + topic->lastError());
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
Publisher<std::vector<T>>::publish(const std::vector<T> &data)
{
  // 長さが変わっても、容量に収まる限り世代は作り直さない。
  // 収まらないときだけ ShmTopic が新しい世代を作る（容量は増やすだけ）。
  // 以前はベクタ長が変わるたびに稼働中のセグメントを作り直しており、
  // それが R01-F01 の TOCTOU の原因だった。
  vector_size = data.size();
  if (!topic->ensureCapacity(sizeof(T) * vector_size, shm_buf_num, alignof(T)))
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

  unsigned char *data_ptr      = ring_buffer->getDataList();
  size_t         buffer_offset = static_cast<size_t>(oldest_buffer) * ring_buffer->getElementSize();

  // 空の vector では data() が nullptr を返す。長さ 0 でも memcpy に
  // ヌルポインタを渡すのは未定義動作なので、明示的に飛ばす（UBSan が検出）。
  if (vector_size > 0)
  {
    std::memcpy(data_ptr + buffer_offset, data.data(), sizeof(T) * vector_size);
  }

  // 実際に書いた長さをスロットに記録する。容量は要求より大きいことがあるので、
  // 読み手は容量ではなくこの値から要素数を求める。
  ring_buffer->commitBuffer(oldest_buffer, sizeof(T) * vector_size);

  ring_buffer->signal();
}

//! @brief トピックの書き込み（値渡し）
//! @param [in] data
//! @return なし
//! @details boost_python対応時に、参照渡しだと何故かエラーになったために追加した関数．
template <typename T>
void
Publisher<std::vector<T>>::_publish(const std::vector<T> data)
{
  publish(data);
}

//! @brief コンストラクタ
//! @param [in] 共有メモリ名
//! @return なし
//! @details 共有メモリへのアクセスを行う．
template <typename T>
Subscriber<std::vector<T>>::Subscriber(std::string name)
  : shm_name(name)
  , topic(nullptr)
  , current_reading_buffer(0)
  , data_expiry_time_us(2000000)
  , vector_size(0)
  , return_buffers_{}
  , return_index_(0)
{
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Subscriber: Be setted not POD class!");
  }

  // 要素のアライメント要求がレイアウトで保証できる範囲か確認する
  // （理由はスカラ版の同じ判定のコメントを参照）
  if (alignof(T) > RingBuffer::MAX_PAYLOAD_ALIGNMENT)
  {
    throw std::runtime_error("shm::Subscriber: Element type requires alignment " + std::to_string(alignof(T)) +
                             ", which exceeds the maximum the shared memory layout can guarantee (" +
                             std::to_string(RingBuffer::MAX_PAYLOAD_ALIGNMENT) + ")");
  }
  if (name.empty())
  {
    throw std::runtime_error("shm::Subscriber: Please set name!");
  }
  topic = std::make_unique<ShmTopic>(shm_name, static_cast<PERM>(0), false);
}

//! @brief トピックを読み込む
//! @param なし
//! @return const T& 読み込んだトピックへのconst参照
//! @details タイムスタンプが最も新しいトピックを読み込む．
//! 後々可変長なクラスに拡張できるように、メモリへの直接的な参照を返すので、コピーコンストラクタや代入によってデータを複製することを推奨する．
template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribe(bool *is_success)
{
  if (!topic->follow())
  {
    *is_success = false;
    return return_buffers_[return_index_];
  }
  RingBuffer *ring_buffer = topic->ring();
  ring_buffer->setDataExpiryTime_us(data_expiry_time_us);

  // seqlock 方式の読み出し: バッファ選択 → コピー → 発行番号の再確認。
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

    // 整合性の検証は時刻ではなく発行番号で行う（理由はスカラ版のコメント参照）
    uint64_t sequence_before = ring_buffer->getSequence(newest_buffer);
    if (sequence_before == 0)
    {
      ++contention_retry_count_;
      continue;
    }

    // 要素数は容量ではなくスロットの payload_size から求める。
    // 容量は「増やすだけ」で運用するため、実際の長さより大きいことがある。
    const size_t payload_bytes = static_cast<size_t>(ring_buffer->getPayloadSize(newest_buffer));
    vector_size                = payload_bytes / sizeof(T);

    unsigned char *data_ptr      = ring_buffer->getDataList();
    size_t         buffer_offset = static_cast<size_t>(newest_buffer) * ring_buffer->getElementSize();

    // 今返していない側へ読み込む。失敗しても直前に返した値は壊れない。
    std::vector<T> &scratch = return_buffers_[1 - return_index_];
    scratch.resize(vector_size);

    // 空の vector では data() が nullptr。長さ 0 の memcpy でもヌルは未定義動作
    if (vector_size > 0)
    {
      std::memcpy(scratch.data(), data_ptr + buffer_offset, sizeof(T) * vector_size);
    }

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
    ++contention_retry_count_;
  }

  // 一貫したスナップショットを取得できなかった。
  // 返るのは直前に成功した値なので、is_success を必ず確認すること。
  if (!no_data)
  {
    ++contention_failure_count_;
  }
  *is_success = false;
  return return_buffers_[return_index_];
}

template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribe(bool *is_success, SampleInfo *info)
{
  const std::vector<T> &value = subscribe(is_success);
  if (info != nullptr)
  {
    *info = SampleInfo{};
    if (*is_success && topic->ring() != nullptr)
    {
      *info = topic->ring()->getSampleInfo(current_reading_buffer);
    }
  }
  return value;
}

template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status, SampleInfo *info,
                                               uint64_t max_skew_us)
{
  SampleInfo   found{};
  SearchStatus local_status = SearchStatus::Empty;
  const std::vector<T> &value =
      subscribeAt(TimeQuery{ reference.capture_monotonic_us, SearchPolicy::Nearest }, &local_status, &found);

  if (local_status == SearchStatus::Success && max_skew_us != 0)
  {
    const uint64_t target = reference.capture_monotonic_us;
    const uint64_t t      = found.capture_monotonic_us;
    const uint64_t skew   = (t > target) ? (t - target) : (target - t);
    if (skew > max_skew_us)
    {
      local_status = (t < target) ? SearchStatus::TooOld : SearchStatus::TooNew;
    }
  }

  if (status != nullptr)
  {
    *status = local_status;
  }
  if (info != nullptr)
  {
    *info = (local_status == SearchStatus::Success) ? found : SampleInfo{};
  }
  return value;
}

template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribeAt(const TimeQuery &query, SearchStatus *status, SampleInfo *info)
{
  auto set_status = [status](SearchStatus value) {
    if (status != nullptr)
    {
      *status = value;
    }
  };

  if (!topic->follow())
  {
    set_status(SearchStatus::NotConnected);
    return return_buffers_[return_index_];
  }
  RingBuffer *ring_buffer = topic->ring();

  constexpr int MAX_READ_RETRY = 5;
  SearchStatus  search_status  = SearchStatus::Empty;
  for (int attempt = 0; attempt < MAX_READ_RETRY; ++attempt)
  {
    const int found = ring_buffer->findBufferNum(query, &search_status);
    if (found < 0)
    {
      if (search_status == SearchStatus::Contended)
      {
        // 全スロットがたまたま書き込み中だっただけ。少し待てば読める。
        ++contention_retry_count_;
        continue;
      }
      set_status(search_status);
      return return_buffers_[return_index_];
    }

    const uint64_t sequence_before = ring_buffer->getSequence(found);
    if (sequence_before == 0)
    {
      ++contention_retry_count_;
      continue;
    }

    // 要素数は容量ではなくスロットの payload_size から求める
    const size_t payload_bytes = static_cast<size_t>(ring_buffer->getPayloadSize(found));
    vector_size                = payload_bytes / sizeof(T);

    unsigned char *data_ptr      = ring_buffer->getDataList();
    size_t         buffer_offset = static_cast<size_t>(found) * ring_buffer->getElementSize();

    std::vector<T> &scratch = return_buffers_[1 - return_index_];
    scratch.resize(vector_size);
    if (vector_size > 0)
    {
      std::memcpy(scratch.data(), data_ptr + buffer_offset, sizeof(T) * vector_size);
    }

    std::atomic_thread_fence(std::memory_order_acquire);

    if (ring_buffer->getSequence(found) == sequence_before)
    {
      if (info != nullptr)
      {
        *info = ring_buffer->getSampleInfo(found);
      }
      current_reading_buffer = found;
      return_index_          = 1 - return_index_;
      set_status(SearchStatus::Success);
      return return_buffers_[return_index_];
    }
    ++contention_retry_count_;
  }

  ++contention_failure_count_;
  set_status(SearchStatus::Contended);
  return return_buffers_[return_index_];
}

template <typename T>
RetentionWindow
Subscriber<std::vector<T>>::getRetentionWindow()
{
  if (!topic->follow())
  {
    return RetentionWindow{};
  }
  return topic->ring()->getRetentionWindow();
}

template <typename T>
bool
Subscriber<std::vector<T>>::waitFor(uint64_t timeout_usec)
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
Subscriber<std::vector<T>>::setDataExpiryTime_us(uint64_t time_us)
{
  data_expiry_time_us = time_us;
  if (topic->ring() != nullptr)
  {
    topic->ring()->setDataExpiryTime_us(data_expiry_time_us);
  }
}

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_PS_VECTOR_LIB_H__ */
