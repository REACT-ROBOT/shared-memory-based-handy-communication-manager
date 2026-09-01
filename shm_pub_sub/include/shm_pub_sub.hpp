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

// ----------------------------------------------------------------------------
// 共有メモリに置ける型の制約（R01-F07-b）
//
// SHM_STRICT_TYPE_CHECK が定義されているとコンパイル時に検査する。
// 既定では定義されないので、従来どおり実行時の検査だけが働く。
//
// std::is_trivially_copyable<T>
//   「バイト列を memcpy するだけで正しく複製できる」という性質。
//   共有メモリは生のバイト列なので、これを満たさない型を置くと
//   コピーコンストラクタや代入演算子が本来やる処理が丸ごと飛ばされる。
//   これを壊すのは次の4つだけで、自前の「既定」コンストラクタは壊さない:
//     - 自前のコピー／ムーブコンストラクタ
//     - 自前のコピー／ムーブ代入演算子
//     - 自前のデストラクタ
//     - virtual 関数／virtual 基底
//   std::string や std::vector をメンバに持つ型は内部にポインタを抱えるため
//   当然これに該当する（そのポインタは他プロセスでは無意味なアドレスになる）。
//
// std::is_standard_layout<T>
//   メンバの「配置」が C と同じで予測可能という別の性質。
//   プロセス間で同じオフセットを前提にするために必要だが、
//   コピーの正しさは何も保証しない。両方が要る。
// ----------------------------------------------------------------------------
#ifdef SHM_STRICT_TYPE_CHECK
#define SHM_ASSERT_SHAREABLE(T, who)                                                                                  \
  static_assert(std::is_standard_layout<T>::value,                                                                     \
                who " : payload type must have standard layout to live in shared memory");                             \
  static_assert(std::is_trivially_copyable<T>::value,                                                                  \
                who " : payload type must be trivially copyable to live in shared memory. "                            \
                    "A user-provided copy/move constructor, copy/move assignment, destructor, "                        \
                    "or a virtual function breaks this. Members such as std::string / std::vector "                    \
                    "hold pointers that are meaningless in another process.")
#else
#define SHM_ASSERT_SHAREABLE(T, who)
#endif

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
  SHM_ASSERT_SHAREABLE(T, "shm::Publisher");

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
  bool publishOnce(const T &data);

public:

private:
  //! このトピックに何を入れるかの取り決め（R02-F01）。
  //! 購読側が接続時にこれと照合し、食い違ったら payload に触れずに失敗する。
  static RingBuffer::TopicContract contractOf()
  {
    RingBuffer::TopicContract c;
    c.kind         = PayloadKind::Scalar;
    c.element_size = sizeof(T);
    c.schema_id      = type_schema_id<T>();
    c.schema_version = schema_version_of<T>();
    c.alignment      = alignof(T);
    return c;
  }

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
  SHM_ASSERT_SHAREABLE(T, "shm::Subscriber");

public:
  Subscriber(std::string name = "");
  ~Subscriber() = default;

  // コピーは禁止
  Subscriber(const Subscriber &)            = delete;
  Subscriber &operator=(const Subscriber &) = delete;

  // ムーブコンストラクタ：ポインタを奪い、元を nullptr に
  Subscriber(Subscriber &&other) noexcept = default;

  const T &subscribe(bool *state);

  /*!
   * \~japanese-en 最新のデータを読み、そのサンプルの素性も受け取る．
   * \~japanese-en 別トピックと時刻を合わせるときは、ここで得た
   *               `SampleInfo::capture_monotonic_us` を基準時刻に使う．
   */
  const T &subscribe(bool *state, SampleInfo *info);

  /*!
   * \~japanese-en 別トピックのサンプルに時刻を合わせて読む．
   *
   *               主用途はセンサ間の時刻合わせである。たとえば自己位置推定で
   *               「いま更新されたオドメトリと同じ時刻のスキャンが欲しい」場合:
   * \code
   *   SampleInfo odom_info;
   *   bool ok = false;
   *   const Odometry& odom = odom_sub.subscribe(&ok, &odom_info);
   *
   *   SampleInfo   scan_info;
   *   SearchStatus st;
   *   // 20 ms 以上ずれていたら使わない
   *   const Scan& scan = scan_sub.subscribeAlignedTo(odom_info, &st, &scan_info, 20000);
   *   if (st == SearchStatus::Success) { ... }
   * \endcode
   *
   * @param [in]  reference    合わせる相手のサンプル（時刻だけを使う）
   * @param [out] status       結果の状態
   * @param [out] info         取得したサンプルの素性（不要なら nullptr）
   * @param [in]  max_skew_us  許容する時刻のずれ[usec]。0 なら無制限。
   *                           これを超えていたら、相手より古ければ TooOld、
   *                           新しければ TooNew を返す（融合してはいけない値を
   *                           黙って返さないため）
   * @return const T& 取得したデータ。status が Success 以外のときの内容は不定
   */
  const T &subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status, SampleInfo *info = nullptr,
                              uint64_t max_skew_us = 0);

  /*!
   * \~japanese-en 指定した時刻のデータを読む（タイムマシン）．
   *
   *               引けるのは「リングに残っている範囲」だけである。
   *               既定のバッファ数は 3 面なので、履歴として使うなら
   *               Publisher の buffer_num を用途に応じて増やすこと。
   *               保持範囲は getRetentionWindow() で確認できる。
   *
   *               `status` は次を区別する。
   *                 - Success    … 取得できた
   *                 - Empty      … 有効なサンプルが1件も無い
   *                 - TooOld     … 目標時刻が保持範囲より古い（上書き済み）
   *                 - TooNew     … 目標時刻が保持範囲より新しい（未 publish）
   *                 - Contended  … publisher が上書きし続けて一貫した
   *                                 スナップショットを取れなかった。**再試行の価値がある**
   *                 - NotConnected … トピックに接続できていない
   *
   * @param [in]  query  検索条件（時刻・時計・選択方針）
   * @param [out] status 結果の状態（不要なら nullptr）
   * @param [out] info   取得したサンプルの素性（不要なら nullptr）
   * @return const T& 取得したデータ。status が Success 以外のときの内容は不定
   * @note 期限（setDataExpiryTime_us）はこの経路には適用しない。
   *       期限は「最新値が十分新しいか」の判定であって、過去を引く検索の
   *       対象を狭めるものではないため。
   */
  const T &subscribeAt(const TimeQuery &query, SearchStatus *status, SampleInfo *info = nullptr);

  //! @brief 現在保持している範囲（引ける時刻の範囲）
  RetentionWindow getRetentionWindow();

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
  //! 購読側が期待するトピックの取り決め（R02-F01）。
  //! Publisher が記録したものと食い違ったら payload に一切触れずに失敗する。
  static RingBuffer::TopicContract contractOf()
  {
    RingBuffer::TopicContract c;
    c.kind         = PayloadKind::Scalar;
    c.element_size = sizeof(T);
    c.schema_id      = type_schema_id<T>();
    c.schema_version = schema_version_of<T>();
    c.alignment      = alignof(T);
    return c;
  }
  //! 指定スロットを、payload と素性が同じサンプルであることを保証して読む
  bool readSlotInto(RingBuffer *ring_buffer, int slot, SampleInfo *info);

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
  }

  // ペイロードのアライメント要求が、共有メモリのレイアウトで保証できる範囲に
  // 収まっているかを確認する。
  //
  // 以前はここに「ARM では alignof(T) が最大アライメントを超えたら拒否」という
  // 判定があった。v1 ではペイロード先頭を 8 バイト境界にしか揃えられず、
  // over-aligned な型を安全に置けなかったための措置である。
  // 形式 v2 以降は alignof(T) をヘッダに記録し、ペイロード先頭を
  // max(payload_alignment, 64) 境界に載せ、スロット間隔も payload_alignment の
  // 倍数に揃えるので、alignas(16/32/64) の型を正しく扱える。
  // 古い判定を残していると、扱えるはずの型を ARM でだけ拒否することになり、
  // x86 と ARM で受け付ける型が食い違う（実際 Raspberry Pi 4 で alignas(32) の
  // 型が弾かれて発覚した）。実際の上限はレイアウト側の MAX_PAYLOAD_ALIGNMENT
  // なので、プラットフォームによらずそれで判定する。
  if (alignof(T) > RingBuffer::MAX_PAYLOAD_ALIGNMENT)
  {
    throw std::runtime_error("shm::Publisher: Type requires alignment " + std::to_string(alignof(T)) +
                             ", which exceeds the maximum the shared memory layout can guarantee (" +
                             std::to_string(RingBuffer::MAX_PAYLOAD_ALIGNMENT) + ")");
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
    if (!topic->ensureCapacity(sizeof(T), shm_buf_num, alignof(T), contractOf()))
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
  // 世代切替と publish が競合した場合に備えて、コミット後に世代を確認し、
  // 切り替わっていたら新しい世代へ発行し直す（R02-F05）。
  // これをしないと、切替の隙間に旧世代へ commit したサンプルが
  // 「成功したのに誰にも読まれない」ことになる。
  constexpr int MAX_PUBLISH_ATTEMPTS = 4;
  for (int attempt = 0; attempt < MAX_PUBLISH_ATTEMPTS; ++attempt)
  {
    if (publishOnce(data))
    {
      return;
    }
  }
  throw std::runtime_error("shm::Publisher: the layout generation kept changing while publishing");
}

//! @brief 1 回分の publish。世代が切り替わっていたら false を返す（呼び出し側で再試行）
template <typename T>
bool
Publisher<T>::publishOnce(const T &data)
{
  if (!topic->ensureCapacity(sizeof(T), shm_buf_num, alignof(T), contractOf()))
  {
    throw std::runtime_error("shm::Publisher: " + topic->lastError());
  }
  RingBuffer *ring_buffer = topic->ring();
  const uint64_t generation_before = topic->generationTag();

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

  // コミットの間に世代が切り替わっていたら、このサンプルは新しい世代の
  // 購読者には見えない。呼び出し側で発行し直す。
  return topic->isGeneration(generation_before);
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
  }

  // ペイロードのアライメント要求が、共有メモリのレイアウトで保証できる範囲に
  // 収まっているかを確認する。
  //
  // 以前はここに「ARM では alignof(T) が最大アライメントを超えたら拒否」という
  // 判定があった。v1 ではペイロード先頭を 8 バイト境界にしか揃えられず、
  // over-aligned な型を安全に置けなかったための措置である。
  // 形式 v2 以降は alignof(T) をヘッダに記録し、ペイロード先頭を
  // max(payload_alignment, 64) 境界に載せ、スロット間隔も payload_alignment の
  // 倍数に揃えるので、alignas(16/32/64) の型を正しく扱える。
  // 古い判定を残していると、扱えるはずの型を ARM でだけ拒否することになり、
  // x86 と ARM で受け付ける型が食い違う（実際 Raspberry Pi 4 で alignas(32) の
  // 型が弾かれて発覚した）。実際の上限はレイアウト側の MAX_PAYLOAD_ALIGNMENT
  // なので、プラットフォームによらずそれで判定する。
  if (alignof(T) > RingBuffer::MAX_PAYLOAD_ALIGNMENT)
  {
    throw std::runtime_error("shm::Subscriber: Type requires alignment " + std::to_string(alignof(T)) +
                             ", which exceeds the maximum the shared memory layout can guarantee (" +
                             std::to_string(RingBuffer::MAX_PAYLOAD_ALIGNMENT) + ")");
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

//! @brief 指定スロットを、payload と素性が同じサンプルであることを保証して読む
//! @details 以前は成功判定の後に別操作として getSampleInfo() を呼んでいたため、
//!          その間に publisher が同じスロットを再確保でき、「payload は N、
//!          info は N+1」という組合せが返り得た（1 スロットで 63 万回中 1,869 回
//!          再現）。時刻合わせでは別時刻のセンサ値を「整列済み」として返すことに
//!          なり実害がある（R02-F03）。
//!
//!          さらに、発行番号の前後比較だけでは publisher の memcpy と reader の
//!          memcpy が同じ通常メモリへ同時に走る可能性が残り、C++ のメモリモデル上
//!          これは data race（未定義動作）だった。RingBuffer::readSample() は
//!          スロットの robust mutex を取って読むので、この競合自体が起きない
//!          （R03-F04）。
template <typename T>
bool
Subscriber<T>::readSlotInto(RingBuffer *ring_buffer, int slot, SampleInfo *info)
{
  // 今返していない側へ読み込む。失敗しても直前に返した値は壊れない。
  T &scratch = return_buffers_[1 - return_index_];

  SampleInfo sample;
  if (!ring_buffer->readSample(slot, &scratch, sizeof(T), &sample))
  {
    return false;
  }

  // 実際に書かれた長さが自分の型と一致すること（R02-F01）
  if (sample.payload_size != sizeof(T))
  {
    return false;
  }

  if (info != nullptr)
  {
    *info = sample;
  }
  current_reading_buffer = slot;
  return_index_          = 1 - return_index_;
  return true;
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
  return subscribe(is_success, nullptr);
}

//! @brief 最新のデータを読み、素性も受け取る
template <typename T>
const T &
Subscriber<T>::subscribe(bool *state, SampleInfo *info)
{
  // legacy の subscribe(bool*) は null を渡せない契約なので、ここで明示する。
  if (state == nullptr)
  {
    throw std::invalid_argument("shm::Subscriber::subscribe(): 'state' must not be null");
  }
  *state = false;
  if (info != nullptr)
  {
    *info = SampleInfo{};
  }

  RingBuffer::TopicContract contract = contractOf();
  // 現在有効な世代へ追随する。世代が進んでいれば新しいセグメントへ張り直す。
  // 型が食い違っていればここで失敗し、payload には一切触れない。
  if (!topic->follow(&contract))
  {
    return return_buffers_[return_index_];
  }
  RingBuffer *ring_buffer = topic->ring();
  ring_buffer->setDataExpiryTime_us(data_expiry_time_us);

  constexpr int MAX_READ_RETRY = 5;
  bool          no_data        = false;
  for (int attempt = 0; attempt < MAX_READ_RETRY; ++attempt)
  {
    const int newest_buffer = ring_buffer->getNewestBufferNum();
    if (newest_buffer < 0)
    {
      no_data = true;
      break;
    }
    if (readSlotInto(ring_buffer, newest_buffer, info))
    {
      *state = true;
      return return_buffers_[return_index_];
    }
    ++contention_retry_count_;
  }

  // 一貫したスナップショットを取得できなかった。返るのは直前に成功した値
  // （一度も成功していなければ T の既定値）なので、state を必ず確認すること。
  if (!no_data)
  {
    ++contention_failure_count_;
  }
  return return_buffers_[return_index_];
}

//! @brief 別トピックのサンプルに時刻を合わせて読む
//! @details 宣言側のコメントを参照．
template <typename T>
const T &
Subscriber<T>::subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status, SampleInfo *info,
                                  uint64_t max_skew_us)
{
  SampleInfo   found{};
  SearchStatus local_status = SearchStatus::Empty;
  const T     &value =
      subscribeAt(TimeQuery{ reference.capture_monotonic_us, SearchPolicy::Nearest }, &local_status, &found);

  if (local_status == SearchStatus::Success && max_skew_us != 0)
  {
    const uint64_t target = reference.capture_monotonic_us;
    const uint64_t t      = found.capture_monotonic_us;
    const uint64_t skew   = (t > target) ? (t - target) : (target - t);
    if (skew > max_skew_us)
    {
      // 融合してはいけないほどずれた値を、黙って成功として返さない。
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

//! @brief 指定した時刻のデータを読む
//! @details 宣言側のコメントを参照．
//!          スロットの選択とコピーの間に publisher が上書きすることがあるため、
//!          発行番号による整合性検証を通し、失敗したら検索からやり直す。
//!          リングの内容が変わっている以上、同じスロットを読み直しても
//!          意味が無いため。
template <typename T>
const T &
Subscriber<T>::subscribeAt(const TimeQuery &query, SearchStatus *status, SampleInfo *info)
{
  RingBuffer::TopicContract contract = contractOf();
  auto set_status = [status](SearchStatus value) {
    if (status != nullptr)
    {
      *status = value;
    }
  };

  if (!topic->follow(&contract))
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

    // payload と素性を一体で読む（R02-F03）
    if (readSlotInto(ring_buffer, found, info))
    {
      set_status(SearchStatus::Success);
      return return_buffers_[return_index_];
    }
    // コピー中に上書きされた → 検索からやり直す
    ++contention_retry_count_;
  }

  // 一貫したスナップショットを取れなかった。データが無いのとは違うので、
  // 呼び出し側が再試行を判断できるよう Contended を返す。
  ++contention_failure_count_;
  set_status(SearchStatus::Contended);
  return return_buffers_[return_index_];
}

template <typename T>
RetentionWindow
Subscriber<T>::getRetentionWindow()
{
  RingBuffer::TopicContract contract = contractOf();
  if (!topic->follow(&contract))
  {
    return RetentionWindow{};
  }
  return topic->ring()->getRetentionWindow();
}

template <typename T>
bool
Subscriber<T>::waitFor(uint64_t timeout_usec)
{
  RingBuffer::TopicContract contract = contractOf();
  if (!topic->follow(&contract))
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
  RingBuffer::TopicContract contract = contractOf();
  return topic != nullptr && topic->follow(&contract);
}

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_PS_LIB_H__ */
