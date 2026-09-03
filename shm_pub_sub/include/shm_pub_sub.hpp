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

// ペイロードの書式が宣言されていることを要求する（既定 OFF）。
//
// 宣言があると、`element_size`（sizeof）が同じままメンバを並べ替えただけの
// 変更も検出できる。再デプロイ後に古いプロセスが生き残っている場合や、
// 古いセグメントが残っている場合に効く。
//
// 既定 OFF なのは移行のためである。workspace には 90 種類以上のペイロード型が
// あり、一斉に必須化すると複数リポジトリのビルドが同時に止まる。
// パッケージ単位で ON にして順に移行し、全部済んだら既定を ON にする。
#ifdef SHM_REQUIRE_LAYOUT
#define SHM_ASSERT_FORMAT_DECLARED(T, who)                                                                             \
  static_assert(::irlab::shm::shm_schema<T>::declared || std::is_arithmetic<T>::value || std::is_enum<T>::value,        \
                who " : this payload type has no format declaration. Declare it so that a stale process or a "          \
                    "leftover segment with a different member order is rejected instead of being read as if it "        \
                    "matched:\n"                                                                                        \
                    "    SHM_DECLARE_LAYOUT(YourType, member1, member2, ...);            // POD payload\n"               \
                    "    SHM_DECLARE_SERIALIZED_FORMAT(YourType, 1);                     // serialize() defines it")
#else
#define SHM_ASSERT_FORMAT_DECLARED(T, who)
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
  SHM_ASSERT_FORMAT_DECLARED(T, "shm::Publisher");

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

  //! SlotWriter へ渡す文脈。書く対象と、書くべきバイト数を持つ。
  struct WriteContext
  {
    const T *data;
  };

  std::string shm_name;
  //! 世代管理・容量確保・スロット確保・コミット・世代確認は全てここが持つ。
  //! これらは以前 5 つの特殊化にコピーされており、片方だけ直す漏れが起きた。
  PublisherCore core_;
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
  SHM_ASSERT_FORMAT_DECLARED(T, "shm::Subscriber");

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
   *   const Scan& scan = scan_sub.subscribeAlignedTo(odom_info, &st, 20000, &scan_info);
   *   if (st == SearchStatus::Success) { ... }
   * \endcode
   *
   * @param [in]  reference    合わせる相手のサンプル（時刻だけを使う）
   * @param [out] status       結果の状態
   * @param [in]  max_skew_us  許容する時刻のずれ[usec]。**必須**。
   *                           これを超えていたら、相手より古ければ TooOld、
   *                           新しければ TooNew を返す（融合してはいけない値を
   *                           黙って返さないため）。
   *                           0 は「無制限」だが、**既定にはしない**。
   *                           Nearest は有効なサンプルがあれば必ず何かを返すので、
   *                           上限を書き忘れると数百 ms ずれた値を Success として
   *                           受け取ることになる。センサの周期から決めて明示すること
   *                           （R04-F14）。
   * @param [out] info         取得したサンプルの素性（不要なら nullptr）
   * @return const T& 取得したデータ。status が Success 以外のときの内容は不定
   */
  const T &subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status, uint64_t max_skew_us, SampleInfo *info = nullptr);

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
   * @param [in]  query  検索条件（時刻と選択方針。時計は常に CLOCK_MONOTONIC_RAW）
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
  uint64_t getContentionRetryCount() const { return core_.getContentionRetryCount(); }
  uint64_t getContentionFailureCount() const { return core_.getContentionFailureCount(); }
  void     resetContentionCounts() { core_.resetContentionCounters(); }

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
  //! 指定スロットを、payload と素性が同じサンプルであることを保証して読む。
  //! **この特殊化で型に依存するのはここだけ**で、残りは SubscriberCore にある。
  bool readSlotInto(RingBuffer *ring_buffer, int slot, SampleInfo *info);

  //! SubscriberCore へ渡す呼び戻し。型消去のためにメンバ関数を関数ポインタへ包む。
  SubscriberCore::SlotReader slotReader()
  {
    return SubscriberCore::SlotReader{
      [](void *ctx, RingBuffer *ring_buffer, int slot, SampleInfo *info) -> bool {
        return static_cast<Subscriber<T> *>(ctx)->readSlotInto(ring_buffer, slot, info);
      },
      this
    };
  }

  std::string shm_name;
  //! 世代管理・再試行・時刻検索・期限・競合カウンタは全てここが持つ。
  //! これらは以前 5 つの特殊化にコピーされており、片方だけ直す漏れが起きた。
  SubscriberCore core_;
  // 返り値はダブルバッファで持つ。読み出しは常に「今返していない方」へ行い、
  // 一貫性を確認できたときだけ有効な側を入れ替える。こうしないと、失敗した
  // subscribe() が直前に返した値を上書きしてしまう（const T& を返すため、
  // 呼び出し側が保持している参照の中身が黙って壊れる）。
  T   return_buffers_[2];
  int return_index_;
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
//! @details \~english     Prepares the topic's generation and attaches to a segment that satisfies the
//!          \~english     requested capacity. Slot mutexes are initialized by the RingBuffer; there are
//!          \~english     no condition variables in the layout.
//!          \~japanese-en トピックの世代を用意し、要求容量を満たすセグメントへ接続する．
//!          \~japanese-en スロットの mutex を初期化するのは RingBuffer で、
//!          \~japanese-en 条件変数はレイアウトに存在しない．
template <typename T>
Publisher<T>::Publisher(std::string name, int buffer_num, PERM perm)
  : shm_name(name)
  , core_(name, buffer_num, perm, "shm::Publisher")
{
  // 型に関する検査だけがこの特殊化の仕事である。
  // 名前と buffer_num の検証、ShmTopic の生成は PublisherCore が持つ。
  if (!std::is_standard_layout<T>::value)
  {
    throw std::runtime_error("shm::Publisher: Type must have standard layout for shared memory!");
  }
  if constexpr (is_arm_platform())
  {
    if (!std::is_trivially_copyable<T>::value)
    {
      throw std::runtime_error("shm::Publisher: Type must be trivially copyable for ARM compatibility!");
    }
  }
  if (alignof(T) > RingBuffer::MAX_PAYLOAD_ALIGNMENT)
  {
    throw std::runtime_error("shm::Publisher: Type requires alignment " + std::to_string(alignof(T)) +
                             ", which exceeds the maximum the shared memory layout can guarantee (" +
                             std::to_string(RingBuffer::MAX_PAYLOAD_ALIGNMENT) + ")");
  }

  // 最初の publish を待たずに確保しておく。固定長なので必要量が決まっている。
  if (!core_.topic()->ensureCapacity(sizeof(T), buffer_num, alignof(T), contractOf()))
  {
    throw std::runtime_error("shm::Publisher: " + core_.topic()->lastError());
  }
}

//! @brief \~english     Publish a topic
//!        \~japanese-en トピックの書き込み
//! @param [in] data
//! @return  \~english     None
//!          \~japanese-en なし
//! @details \~english     Acquires the slot with the smallest sequence number (the oldest), writes the
//!          \~english     payload into it, and allocates a new sequence number on commit.
//!          \~english     Waiters are not signalled through a condition variable: there is none in the
//!          \~english     layout. `waitFor()` polls the sequence number instead.
//!          \~japanese-en 発行番号が最も小さい（＝最も古い）スロットを確保して書き込み、
//!          \~japanese-en コミット時に新しい発行番号を採番する．
//!          \~japanese-en 待機側への通知に条件変数は使わない（レイアウトに存在しない）．
//!          \~japanese-en `waitFor()` が発行番号をポーリングする．理由は
//!          \~japanese-en ring_buffer.cpp の `signal()` のコメントを参照．
template <typename T>
void
Publisher<T>::publish(const T &data)
{
  // 書き込みは memcpy に統一する。以前は x86 で *reinterpret_cast<T*>(ptr) = data
  // としていたが、これは T が構築されていない領域に代入演算子を走らせる未定義動作で、
  // かつコンパイラが aligned 命令を選ぶと alignas(16) 以上の型で SIGSEGV し得た。
  // trivially copyable な型には memcpy が正しく、アライメント要求も無い（R01-F07-a）。
  WriteContext             ctx{ &data };
  PublisherCore::SlotWriter writer{
    [](void *context, unsigned char *slot_ptr, size_t slot_capacity) -> long long {
      if (slot_capacity < sizeof(T))
      {
        return -1;
      }
      std::memcpy(slot_ptr, static_cast<WriteContext *>(context)->data, sizeof(T));
      return static_cast<long long>(sizeof(T));
    },
    &ctx
  };

  core_.publishOrThrow(sizeof(T), alignof(T), contractOf(), writer);
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
  // トピックの生成、名前の検証、期限の既定値（2 秒）は SubscriberCore が持つ。
  // 型に関する検査だけがこの特殊化の仕事である。
  , core_(name, "shm::Subscriber")
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
  return_index_ = 1 - return_index_;
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
//!                  \~japanese-en 発行番号が最も大きい（＝最後に commit された）トピックを読み込む．
//!                  \~japanese-en 「最新」を時刻で決めると、同一 microsecond に複数の publish が
//!                  \~japanese-en あったときにスロット番号で誤選択する（R01-F05）．
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
  if (state == nullptr)
  {
    throw std::invalid_argument("shm::Subscriber::subscribe(): 'state' must not be null");
  }
  *state = false;
  if (info != nullptr)
  {
    *info = SampleInfo{};
  }

  // 世代への追随・再試行・競合カウンタは SubscriberCore が持つ。
  // 失敗したときは直前に返した値をそのまま返す（一度も成功していなければ
  // T の既定値）ので、呼び出し側は state を必ず確認すること。
  *state = core_.readNewest(contractOf(), slotReader(), info);
  return return_buffers_[return_index_];
}

//! @brief 別トピックのサンプルに時刻を合わせて読む
//! @details 宣言側のコメントを参照．
template <typename T>
const T &
Subscriber<T>::subscribeAlignedTo(const SampleInfo &reference, SearchStatus *status, uint64_t max_skew_us, SampleInfo *info)
{
  core_.readAlignedTo(contractOf(), reference, slotReader(), max_skew_us, status, info);
  return return_buffers_[return_index_];
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
  core_.readAt(contractOf(), query, slotReader(), status, info);
  return return_buffers_[return_index_];
}

template <typename T>
RetentionWindow
Subscriber<T>::getRetentionWindow()
{
  return core_.getRetentionWindow(contractOf());
}

template <typename T>
bool
Subscriber<T>::waitFor(uint64_t timeout_usec)
{
  return core_.waitFor(contractOf(), timeout_usec);
}

template <typename T>
void
Subscriber<T>::setDataExpiryTime_us(uint64_t time_us)
{
  core_.setDataExpiryTime_us(time_us);
}

template <typename T>
bool
Subscriber<T>::existsPublisherMemory()
{
  return core_.existsPublisherMemory(contractOf());
}

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_PS_LIB_H__ */
