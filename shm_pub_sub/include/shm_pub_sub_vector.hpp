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
  std::string                   shm_name;
  int                           shm_buf_num;
  PERM                          shm_perm;
  std::unique_ptr<SharedMemory> shared_memory;
  std::unique_ptr<RingBuffer>   ring_buffer;

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
  std::string                   shm_name;
  std::unique_ptr<SharedMemory> shared_memory;
  std::unique_ptr<RingBuffer>   ring_buffer;
  int                           current_reading_buffer;
  uint64_t                      data_expiry_time_us;

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
  , shared_memory(nullptr)
  , ring_buffer(nullptr)
  , vector_size(0)
{
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Publisher: Be setted not POD class in vector!");
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

  shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR | O_CREAT, shm_perm);
  shared_memory->connect(RingBuffer::getSize(sizeof(T) * vector_size, shm_buf_num, alignof(T)));
  if (shared_memory->isDisconnected())
  {
    throw std::runtime_error("shm::Publisher: Cannot get memory!");
  }

  // 既に初期化済みの共有メモリがある場合は、その要素サイズを引き継ぐ。
  // 引き継がずに vector_size = 0 のままリングバッファを構築すると、
  // element_size が 0 に書き換わりレイアウト不一致とみなされて作り直しになり、
  // 既に publish 済みの値とタイムスタンプが失われる（後発 Publisher 問題）。
  // 引き継いでおけば要素サイズが一致し、接続のみで済む。
  unsigned char *first_ptr = shared_memory->getPtr();
  if (RingBuffer::checkInitialized(first_ptr))
  {
    // 再初期化しない読み出し用の接続で既存の要素サイズを確認する。
    // 壊れた／別形式の共有メモリを掴んでいる場合は nullptr が返るので、
    // 引き継ぎを諦めて新しいレイアウトで作り直す（R01-F06）。
    std::unique_ptr<RingBuffer> probe = attachRingBuffer(*shared_memory);
    if (probe != nullptr)
    {
      const size_t existing_element_size = probe->getElementSize();
      if (existing_element_size != 0 && (existing_element_size % sizeof(T)) == 0)
      {
        vector_size = existing_element_size / sizeof(T);
      }
    }
  }

  ring_buffer = std::make_unique<RingBuffer>(first_ptr, sizeof(T) * vector_size, shm_buf_num, alignof(T));
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
  // ベクタ長が変わった場合と、別のプロセスが異なるレイアウトで初期化し直した
  // 場合に、共有メモリを張り直してリングバッファを作り直す。
  //
  // 以前はベクタ長が変わるたびに disconnectAndUnlink() で共有メモリを破棄して
  // 作り直していた。しかし unlink は名前を消すだけで、同じトピックの他の
  // Publisher が掴んでいるマッピングはそのまま生き続けるため、その Publisher は
  // 誰にも読まれない領域へ publish を続けることになっていた（例外も出ない）。
  // 破棄せずにその場で作り直せば、他の Publisher や Subscriber は
  // RingBuffer::isLayoutChanged() でレイアウトの変化に気付いて張り直せる。
  // 共有メモリのファイルは connect() の ftruncate で伸びるだけなので、
  // 張り直した側のマッピングは常に全体を覆う。
  if (data.size() != vector_size || shared_memory->isDisconnected() || ring_buffer == nullptr ||
      ring_buffer->isLayoutChanged())
  {
    vector_size = data.size();
    ring_buffer.reset();
    shared_memory->disconnect();
    shared_memory->connect(RingBuffer::getSize(sizeof(T) * vector_size, shm_buf_num, alignof(T)));

    if (shared_memory->isDisconnected())
    {
      throw std::runtime_error("shm::Publisher: Cannot allocate shared memory!");
    }

    ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr(), sizeof(T) * vector_size, shm_buf_num, alignof(T));
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

  // Cross-platform aligned memory access for vectors
  unsigned char *data_ptr      = ring_buffer->getDataList();
  size_t         buffer_offset = oldest_buffer * vector_size * sizeof(T);

  // 空の vector では data() が nullptr を返す。長さ 0 でも memcpy に
  // ヌルポインタを渡すのは未定義動作なので、明示的に飛ばす（UBSan が検出）。
  if (vector_size > 0)
  {
    std::memcpy(data_ptr + buffer_offset, data.data(), sizeof(T) * vector_size);
  }

  // 発行番号の採番とスロットの解放（理由はスカラ版のコメント参照）
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
  , shared_memory(nullptr)
  , ring_buffer(nullptr)
  , current_reading_buffer(0)
  , data_expiry_time_us(2000000)
  , return_buffers_{}
  , return_index_(0)
{
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Subscriber: Be setted not POD class!");
  }
  shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR, static_cast<PERM>(0));
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
  // 別のプロセスが異なるレイアウトで初期化し直していないか確認する。
  // ベクタ長が変わると要素サイズもデータ位置も変わるため、気付かずに古い
  // vector_size とオフセットで読むと、長さの違う値や無関係な領域を返してしまう。
  // 共有メモリのファイルは伸びている可能性があるので、ごと張り直す。
  if (shared_memory != nullptr && !shared_memory->isDisconnected() && ring_buffer != nullptr &&
      ring_buffer->isLayoutChanged())
  {
    ring_buffer.reset();
    shared_memory->disconnect();
  }

  if (shared_memory == nullptr || shared_memory->isDisconnected())
  {
    if (ring_buffer != nullptr)
    {
      ring_buffer.reset();
    }

    // Clean up old connection before reconnecting
    shared_memory->disconnect();
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

      std::cerr << "[Subscriber::subscribe] Waiting for initialization..." << std::endl;
      // Wait for initialization to complete
      if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000))
      {  // 500ms timeout (increased)
        *is_success = false;
        return return_buffers_[return_index_];
      }

      // 検証つきの接続（R01-F06）
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
      size_t element_size = ring_buffer->getElementSize();
      vector_size         = element_size / sizeof(T);
      return_buffers_[0].resize(vector_size);
      return_buffers_[1].resize(vector_size);
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
      size_t element_size = ring_buffer->getElementSize();
      vector_size         = element_size / sizeof(T);
      return_buffers_[0].resize(vector_size);
      return_buffers_[1].resize(vector_size);
      ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
    }
    catch (const std::bad_alloc &e)
    {
      *is_success = false;
      return return_buffers_[return_index_];
    }
  }
  // seqlock 方式の読み出し: バッファ選択 → コピー → タイムスタンプ再確認。
  // コピー中の上書き (torn read) を検出したら選択からやり直す。
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

    // Cross-platform aligned memory access
    unsigned char *data_ptr      = ring_buffer->getDataList();
    size_t         buffer_offset = newest_buffer * vector_size * sizeof(T);

    // 今返していない側へ読み込む。失敗しても直前に返した値は壊れない。
    std::vector<T> &scratch = return_buffers_[1 - return_index_];

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
bool
Subscriber<std::vector<T>>::waitFor(uint64_t timeout_usec)
{
  // 別のプロセスが異なるレイアウトで初期化し直していないか確認する。
  // ベクタ長が変わると要素サイズもデータ位置も変わるため、気付かずに古い
  // vector_size とオフセットで読むと、長さの違う値や無関係な領域を返してしまう。
  // 共有メモリのファイルは伸びている可能性があるので、ごと張り直す。
  if (shared_memory != nullptr && !shared_memory->isDisconnected() && ring_buffer != nullptr &&
      ring_buffer->isLayoutChanged())
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
    // Clean up old connection before reconnecting
    shared_memory->disconnect();
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
    size_t element_size = ring_buffer->getElementSize();
    vector_size         = element_size / sizeof(T);
    return_buffers_[0].resize(vector_size);
    return_buffers_[1].resize(vector_size);
    ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  }
  // 既に接続済みだが ring_buffer が未初期化の場合に対応
  else if (ring_buffer == nullptr)
  {
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
      size_t element_size = ring_buffer->getElementSize();
      vector_size         = element_size / sizeof(T);
      return_buffers_[0].resize(vector_size);
      return_buffers_[1].resize(vector_size);
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
Subscriber<std::vector<T>>::setDataExpiryTime_us(uint64_t time_us)
{
  data_expiry_time_us = time_us;
  if (ring_buffer != nullptr)
  {
    ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
  }
}

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_PS_VECTOR_LIB_H__ */
