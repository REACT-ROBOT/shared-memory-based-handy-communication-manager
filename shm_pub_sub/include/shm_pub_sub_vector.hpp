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
  // 汎用テンプレートの static_assert は特殊化には適用されないため、
  // ここでも同じ制約を課す（R02-F01）。要素型が trivially copyable でないと、
  // 共有メモリへのバイトコピーで意味が壊れる。
  SHM_ASSERT_SHAREABLE(T, "shm::Publisher<std::vector<T>>");
  SHM_ASSERT_FORMAT_DECLARED(T, "shm::Publisher<std::vector<T>>");

public:
  Publisher(std::string name = "", int buffer_num = 3, PERM perm = DEFAULT_PERM);
  ~Publisher() = default;

  // コピーは禁止。同じ接続を二重に所有することになる。
  Publisher(const Publisher &)            = delete;
  Publisher &operator=(const Publisher &) = delete;

  // ムーブは許す。scalar 版には入っていたが vector 版に無く、
  // std::vector<Publisher<T>> に入れられるかどうかが型で違っていた（R04-F20）。
  Publisher(Publisher &&other) noexcept            = default;
  Publisher &operator=(Publisher &&other) noexcept = default;

  void publish(const std::vector<T> &data);
  void _publish(const std::vector<T> data);

private:
  //! 1 回分の publish。世代が切り替わっていたら false（publish 側で再試行）
  bool publishOnce(const std::vector<T> &data, uint64_t capture_monotonic_us);

public:

private:
  //! このトピックに何を入れるかの取り決め（R02-F01）
  static RingBuffer::TopicContract contractOf()
  {
    RingBuffer::TopicContract c;
    c.kind         = PayloadKind::Vector;
    c.element_size = sizeof(T);
    c.schema_id      = type_schema_id<T>();
    c.schema_version = schema_version_of<T>();
    c.alignment      = alignof(T);
    return c;
  }

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
  // 汎用テンプレートの static_assert は特殊化には適用されないため、
  // ここでも同じ制約を課す（R02-F01）。要素型が trivially copyable でないと、
  // 共有メモリへのバイトコピーで意味が壊れる。
  SHM_ASSERT_SHAREABLE(T, "shm::Subscriber<std::vector<T>>");
  SHM_ASSERT_FORMAT_DECLARED(T, "shm::Subscriber<std::vector<T>>");

public:
  Subscriber(std::string name = "");
  ~Subscriber() = default;

  // コピーは禁止。ムーブは許す（scalar 版と揃える。R04-F20）
  Subscriber(const Subscriber &)            = delete;
  Subscriber &operator=(const Subscriber &) = delete;
  Subscriber(Subscriber &&other) noexcept            = default;
  Subscriber &operator=(Subscriber &&other) noexcept = default;

  const std::vector<T> &subscribe(bool *is_success);
  //! @brief 最新のデータを読み、素性も受け取る（詳細は Subscriber<T> 本体のコメントを参照）
  const std::vector<T> &subscribe(bool *is_success, SampleInfo *info);
  //! @brief 別トピックのサンプルに時刻を合わせて読む（詳細は Subscriber<T> 本体のコメントを参照）
  const std::vector<T> &subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status,
                                           uint64_t max_skew_us, SampleInfo *info = nullptr);

  //! @brief 指定した時刻のデータを読む（詳細は Subscriber<T> 本体のコメントを参照）
  const std::vector<T> &subscribeAt(const TimeQuery &query, SearchStatus *status, SampleInfo *info = nullptr);
  //! @brief 現在保持している範囲（引ける時刻の範囲）
  RetentionWindow getRetentionWindow();
  //! @brief publisher 側の共有メモリが存在するか（scalar 版と揃える。R04-F20）
  bool existsPublisherMemory();

  bool                  waitFor(uint64_t timeout_usec);
  void                  setDataExpiryTime_us(uint64_t time_us);

  // 競合カウンタ（詳細は Subscriber<T> 本体のコメントを参照）
  uint64_t getContentionRetryCount() const { return core_.getContentionRetryCount(); }
  uint64_t getContentionFailureCount() const { return core_.getContentionFailureCount(); }
  void     resetContentionCounts() { core_.resetContentionCounters(); }

private:
  //! このトピックに何を入れるかの取り決め（R02-F01）
  static RingBuffer::TopicContract contractOf()
  {
    RingBuffer::TopicContract c;
    c.kind         = PayloadKind::Vector;
    c.element_size = sizeof(T);
    c.schema_id      = type_schema_id<T>();
    c.schema_version = schema_version_of<T>();
    c.alignment      = alignof(T);
    return c;
  }

  //! 指定スロットを、payload と素性が同じサンプルであることを保証して読む。
  //! **この特殊化で型に依存するのはここだけ**で、残りは SubscriberCore にある。
  bool readSlotInto(RingBuffer *ring_buffer, int slot, SampleInfo *info);

  //! SubscriberCore へ渡す呼び戻し。型消去のためにメンバ関数を関数ポインタへ包む。
  SubscriberCore::SlotReader slotReader()
  {
    return SubscriberCore::SlotReader{
      [](void *ctx, RingBuffer *ring_buffer, int slot, SampleInfo *info) -> bool {
        return static_cast<Subscriber<std::vector<T>> *>(ctx)->readSlotInto(ring_buffer, slot, info);
      },
      this
    };
  }

  std::string shm_name;
  //! 世代管理・再試行・時刻検索・期限・競合カウンタは全てここが持つ。
  SubscriberCore core_;
  // 返り値はダブルバッファで持つ。理由はスカラ版と同じ（失敗した subscribe() が
  // 直前に返した値を壊さないようにするため）。
  std::vector<T> return_buffers_[2];
  int            return_index_;
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
//! @details トピックの世代を用意し、要求容量を満たすセグメントへ接続する．
//!          スロットの mutex を初期化するのは RingBuffer で、
//!          条件変数はレイアウトに存在しない．
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
  if (!topic->ensureCapacity(0, shm_buf_num, alignof(T), contractOf()))
  {
    throw std::runtime_error("shm::Publisher: " + topic->lastError());
  }
}

//! @brief トピックの書き込み
//! @param [in] data
//! @return なし
//! @details 発行番号が最も小さい（＝最も古い）スロットを確保して書き込み、
//!          コミット時に新しい発行番号を採番する．
//!          待機側への通知に条件変数は使わない（レイアウトに存在しない）．
//!          `waitFor()` が発行番号をポーリングする．理由は
//!          ring_buffer.cpp の `signal()` のコメントを参照．
template <typename T>
void
Publisher<std::vector<T>>::publish(const std::vector<T> &data)
{
  // 世代切替と publish が競合した場合に備えて、コミット後に世代を確認し、
  // 切り替わっていたら新しい世代へ発行し直す。スカラ版には R02-F05 で入れたが
  // vector 版に入れ忘れており、切替の隙間に旧世代へ commit したサンプルが
  // 「成功したのに誰にも読まれない」ままになっていた（R03-F01）。
  // capture 時刻はここで 1 度だけ採り、再発行しても引き継ぐ。
  // 発行し直すたびに採り直すと、**同じ測定が別時刻に起きたように見える**
  // （R04-F12）。タイムマシンで時刻を合わせる用途では実害になる。
  const uint64_t capture_monotonic_us = getCurrentTimeUSec();

  constexpr int MAX_PUBLISH_ATTEMPTS = 4;
  for (int attempt = 0; attempt < MAX_PUBLISH_ATTEMPTS; ++attempt)
  {
    if (publishOnce(data, capture_monotonic_us))
    {
      return;
    }
  }
  throw std::runtime_error("shm::Publisher: the layout generation kept changing while publishing");
}

//! @brief 1 回分の publish。世代が切り替わっていたら false を返す（呼び出し側で再試行）
template <typename T>
bool
Publisher<std::vector<T>>::publishOnce(const std::vector<T> &data, uint64_t capture_monotonic_us)
{
  // 長さが変わっても、容量に収まる限り世代は作り直さない。
  // 収まらないときだけ ShmTopic が新しい世代を作る（容量は増やすだけ）。
  // 以前はベクタ長が変わるたびに稼働中のセグメントを作り直しており、
  // それが R01-F01 の TOCTOU の原因だった。
  vector_size = data.size();
  if (!topic->ensureCapacity(sizeof(T) * vector_size, shm_buf_num, alignof(T), contractOf()))
  {
    throw std::runtime_error("shm::Publisher: " + topic->lastError());
  }
  RingBuffer *ring_buffer = topic->ring();

  // コミット後にこの世代がまだ有効かを確かめるため、書く前に控えておく。
  const uint64_t generation_before = topic->generationTag();

  // 確保できないままの書き込みは、他の writer が書き込み途中のバッファを
  // 破壊し購読可能にしてしまうため許されない。確保成功を必須とする。
  // 書き込めるスロットを探す。古い順に**全スロット**を試すので、
  // どこか 1 つが reader に押さえられていても他が空いていれば通る（R04-F08）。
  // 1 巡目は待たないため、空きがあれば即座に決まる。
  // 全部塞がっていた場合だけ短く待ち、それでも駄目なら少し置いて数回だけ試す。
  // 最悪でも数 ms で決着する（100Hz の制御ループを止めないため）。
  constexpr int ALLOCATE_ATTEMPTS = 3;
  int           oldest_buffer     = -1;
  for (int attempt = 0; attempt < ALLOCATE_ATTEMPTS; ++attempt)
  {
    oldest_buffer = ring_buffer->acquireWritableSlot();
    if (oldest_buffer >= 0)
    {
      break;
    }
    usleep(1000);  // Wait for 1ms
  }
  const bool allocated = (oldest_buffer >= 0);
  if (!allocated)
  {
    throw std::runtime_error(
        "shm::Publisher: could not acquire a writable slot; every slot stayed busy. "
        "Increase buffer_num, or check for a subscriber that holds a sample for too long.");
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
  // 世代切替との競合を決定的に再現するためのフック（既定では何もしない）
  SHM_FIRE_TEST_HOOK_BEFORE_COMMIT();

  ring_buffer->commitBuffer(oldest_buffer, sizeof(T) * vector_size, capture_monotonic_us);

  ring_buffer->signal();

  // コミットの間に世代が切り替わっていたら、このサンプルは新しい世代の
  // 購読者には見えない。呼び出し側で発行し直す（R03-F01）。
  return topic->isGeneration(generation_before);
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
  // トピックの生成、名前の検証、期限の既定値（2 秒）は SubscriberCore が持つ。
  , core_(name, "shm::Subscriber")
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
}

//! @brief トピックを読み込む
//! @param なし
//! @return const T& 読み込んだトピックへのconst参照
//! @details 発行番号が最も大きい（＝最後に commit された）トピックを読み込む．
//!          「最新」を時刻で決めると、同一 microsecond に複数の publish があった
//!          ときにスロット番号で誤選択する（R01-F05）．
//! 後々可変長なクラスに拡張できるように、メモリへの直接的な参照を返すので、コピーコンストラクタや代入によってデータを複製することを推奨する．
//! @brief 指定スロットを、payload と素性が同じサンプルであることを保証して読む
//! @details 理由はスカラ版の同名関数のコメントを参照（R02-F03）。
//!          長さは容量ではなくスロットの payload_size から求めるが、その値が
//!          容量に収まり、かつ要素サイズで割り切れることを同じスナップショット内で
//!          確認する。確認しないと、メタデータの破損や別の要素型の Publisher に
//!          よって過大確保や範囲外読み出しになる（R02-F01）。
template <typename T>
bool
Subscriber<std::vector<T>>::readSlotInto(RingBuffer *ring_buffer, int slot, SampleInfo *info)
{
  const size_t capacity = ring_buffer->getElementSize();

  // 受け皿の大きさを決めるために、まず長さの目安を読む。
  // この値はロック外で読むので当てにはならないが、ロック内の実長と
  // 食い違えば下で弾かれるので、誤った長さを返すことはない。
  const size_t hint = static_cast<size_t>(ring_buffer->getPayloadSize(slot));
  if (hint > capacity || (hint % sizeof(T)) != 0)
  {
    return false;
  }

  // 今返していない側へ読み込む。失敗しても直前に返した値は壊れない。
  std::vector<T> &scratch = return_buffers_[1 - return_index_];
  scratch.resize(hint / sizeof(T));

  // スロットを排他して payload と素性を 1 つのスナップショットとして読む
  //（R02-F03 / R03-F04）。長さが hint を超えていれば readSample が失敗する。
  SampleInfo sample;
  if (!ring_buffer->readSample(slot, scratch.empty() ? nullptr : scratch.data(), hint, &sample))
  {
    return false;
  }
  if (sample.payload_size != hint)
  {
    // ロックを取るまでの間に別のサンプルへ置き換わり、長さが変わっていた。
    // 短い場合は scratch の後ろが前のサンプルのままなので、必ず捨てる。
    return false;
  }

  if (info != nullptr)
  {
    *info = sample;
  }
  return_index_ = 1 - return_index_;
  return true;
}

template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribe(bool *is_success)
{
  return subscribe(is_success, nullptr);
}

template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribe(bool *is_success, SampleInfo *info)
{
  if (is_success == nullptr)
  {
    throw std::invalid_argument("shm::Subscriber::subscribe(): 'is_success' must not be null");
  }
  *is_success = false;
  if (info != nullptr)
  {
    *info = SampleInfo{};
  }

  // 世代への追随・再試行・競合カウンタは SubscriberCore が持つ。
  // 失敗したときは直前に返した値をそのまま返すので、is_success を必ず確認すること。
  *is_success = core_.readNewest(contractOf(), slotReader(), info);
  return return_buffers_[return_index_];
}

template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status, uint64_t max_skew_us, SampleInfo *info)
{
  core_.readAlignedTo(contractOf(), reference, slotReader(), max_skew_us, status, info);
  return return_buffers_[return_index_];
}

template <typename T>
const std::vector<T> &
Subscriber<std::vector<T>>::subscribeAt(const TimeQuery &query, SearchStatus *status, SampleInfo *info)
{
  core_.readAt(contractOf(), query, slotReader(), status, info);
  return return_buffers_[return_index_];
}

template <typename T>
RetentionWindow
Subscriber<std::vector<T>>::getRetentionWindow()
{
  return core_.getRetentionWindow(contractOf());
}

template <typename T>
bool
Subscriber<std::vector<T>>::waitFor(uint64_t timeout_usec)
{
  return core_.waitFor(contractOf(), timeout_usec);
}

template <typename T>
void
Subscriber<std::vector<T>>::setDataExpiryTime_us(uint64_t time_us)
{
  core_.setDataExpiryTime_us(time_us);
}


//! @brief publisher 側の共有メモリが存在するか
//! @details トピックが作られているかを見るだけで、有効なデータがあるかは見ない。
template <typename T>
bool
Subscriber<std::vector<T>>::existsPublisherMemory()
{
  return core_.existsPublisherMemory(contractOf());
}

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_PS_VECTOR_LIB_H__ */