//!
//! @file shm_base.hpp
//! @brief \~english     Basic class definitions for accessing shared memory, ring buffers, etc.
//!        \~japanese-en 共有メモリへのアクセス方法やリングバッファなどの基本的なクラスの定義
//! @note \~english     The notation is complianted ROS Cpp style guide.
//!       \~japanese-en 記法はROSに準拠する
//!       \~            http://wiki.ros.org/ja/CppStyleGuide
//!

#ifndef __SHM_BASE_LIB_H__
#define __SHM_BASE_LIB_H__

#include <iostream>
#include <limits>
#include <string>
#include <regex>
#include <stdexcept>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <type_traits>
#include <memory>
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

namespace irlab
{

namespace shm
{
// ****************************************************************************
// Cross-platform alignment utilities
// クロスプラットフォーム対応アライメントユーティリティ
// ****************************************************************************

/*!
 * \~english     Platform detection
 * \~japanese-en プラットフォーム検出
 */
constexpr bool
is_arm_platform()
{
#if defined(__ARM_ARCH) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
  return true;
#else
  return false;
#endif
}

/*!
 * \~english     Get required alignment for type T
 * \~japanese-en 型Tに必要なアライメントを取得
 */
template <typename T>
constexpr size_t
get_alignment()
{
  if constexpr (is_arm_platform())
  {
    // ARM requires strict alignment - use 8-byte minimum for safety
    return std::max({ alignof(T), sizeof(void *), static_cast<size_t>(8) });
  }
  else
  {
    // x86/x64 is more lenient
    return alignof(T);
  }
}

/*!
 * \~english     Align pointer to required boundary
 * \~japanese-en ポインタを必要な境界にアライン
 */
template <typename T>
inline T *
align_pointer(void *ptr)
{
  const size_t    alignment    = get_alignment<T>();
  const uintptr_t addr         = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);

  // Additional safety check for ARM
  if constexpr (is_arm_platform())
  {
    if ((aligned_addr % alignment) != 0)
    {
      throw std::runtime_error("ARM alignment failure: unable to align pointer properly");
    }
  }

  return reinterpret_cast<T *>(aligned_addr);
}

/*!
 * \~english     Calculate aligned size for type T
 * \~japanese-en 型Tのアライン済みサイズを計算
 */
template <typename T>
constexpr size_t
get_aligned_size(size_t count = 1)
{
  const size_t alignment = get_alignment<T>();
  const size_t size      = sizeof(T) * count;
  return (size + alignment - 1) & ~(alignment - 1);
}

/*!
 * \~english     Reserve space for `count` objects of type T in a shared-memory
 *               layout, aligning the current offset first. Returns the offset
 *               the objects were placed at and advances `offset` past them.
 * \~japanese-en 共有メモリ上の配置を組み立てるためのヘルパ．
 *               現在のオフセットを型 T の境界へ切り上げてから T を count 個分
 *               確保し、確保した先頭オフセットを返す（offset は末尾へ進む）．
 * \~japanese-en sizeof の総和で配置を決めると、8 の倍数でないサイズの型が
 *               挟まった時点で以降の要素がすべて非アラインになる．
 *               x86 では動いてしまうが ARM では 64bit アクセスが SIGBUS に
 *               なるため、必ずこのヘルパを通して配置すること．
 */
template <typename T>
inline size_t
reserve_aligned(size_t &offset, size_t count = 1)
{
  const size_t alignment = get_alignment<T>();
  offset                 = (offset + alignment - 1) & ~(alignment - 1);
  const size_t reserved  = offset;
  offset += sizeof(T) * count;
  return reserved;
}

/*!
 * \~english     Check if pointer is properly aligned for type T
 * \~japanese-en ポインタが型Tに対して適切にアライメントされているかチェック
 */
template <typename T>
inline bool
is_aligned(const void *ptr)
{
  if constexpr (!is_arm_platform())
  {
    // x86/x64: Always return true for compatibility
    return true;
  }
  else
  {
    // ARM: Strict alignment checking
    if (ptr == nullptr)
    {
      return false;
    }
    const uintptr_t addr      = reinterpret_cast<uintptr_t>(ptr);
    const size_t    alignment = get_alignment<T>();
    bool            aligned   = (addr % alignment) == 0;

    // Additional check for double types on ARM
    if constexpr (std::is_same_v<T, double> || sizeof(T) == sizeof(double))
    {
      // Ensure 8-byte alignment for double-sized types on ARM
      aligned = aligned && (addr % 8) == 0;
    }

    return aligned;
  }
}

/*!
 * \~english     Permissions for shared memory
 * \~japanese-en 共有メモリに付与する権限を表す
 */
enum PERM : mode_t
{
  PERM_USER_READ = S_IRUSR,   /*!<
                               * \~english     Owner readable
                               * \~japanese-en 所有者の読み込み許可
                               */
  PERM_USER_WRITE = S_IWUSR,  /*!<
                               * \~english     Owner writable
                               * \~japanese-en 所有者の書き込み許可
                               */
  PERM_GROUP_READ = S_IRGRP,  /*!<
                               * \~english     Group that owner belong readable
                               * \~japanese-en 所有者のグループの読み込み許可
                               */
  PERM_GROUP_WRITE = S_IWGRP, /*!<
                               * \~english     Group that owner belong writable
                               * \~japanese-en 所有者のグループの書き込み許可
                               */
  PERM_OTHER_READ = S_IROTH,  /*!<
                               * \~english     Others readable
                               * \~japanese-en その他の読み込み許可
                               */
  PERM_OTHER_WRITE = S_IWOTH, /*!<
                               * \~english     Others writable
                               * \~japanese-en その他の書き込み許可
                               */
};
//! \~japanese-en 既定の権限（0660: 所有者とグループのみ読み書き）．
//! \~japanese-en 以前は other にも読み書きを許す 0666 だった。共有メモリは
//!               ネットワーク越しには届かないが、同じホストの別ユーザーや、
//!               トピック名を取り違えた別プロセスがヘッダやペイロードを
//!               書き換えられる状態だった。other を落として最小限にする。
//!               別ユーザー間で共有する必要がある場合は、同じグループに
//!               所属させるか、`PERM_ALL` を明示的に渡すこと。
const PERM DEFAULT_PERM = static_cast<PERM>(PERM_USER_READ | PERM_USER_WRITE | PERM_GROUP_READ | PERM_GROUP_WRITE);

//! \~japanese-en 全ユーザーに読み書きを許す権限（従来の既定）．
//! \~japanese-en 信頼境界を広げるので、必要な場合だけ明示的に指定すること。
const PERM PERM_ALL = static_cast<PERM>(PERM_USER_READ | PERM_USER_WRITE | PERM_GROUP_READ | PERM_GROUP_WRITE |
                                        PERM_OTHER_READ | PERM_OTHER_WRITE);

// ****************************************************************************
// Function Declarations
// 関数宣言
// ****************************************************************************

int      disconnectMemory(std::string name);
//! @brief トピックの共有メモリを世代ごと破棄する（形式 v3 の世代セグメントを含む）
int      disconnectTopic(const std::string &name);
uint64_t getCurrentTimeUSec();
void     validateShmName(const std::string &name, const char *context);


// ****************************************************************************
//! @class SharedMemory
//! @brief \~english     Class that abstracts the method of accessing shared memory
//!        \~japanese-en 共有メモリへのアクセス方法を抽象化したクラス
//! @details
// ****************************************************************************
class SharedMemory
{
public:
  SharedMemory(int oflag, PERM perm);
  virtual ~SharedMemory() noexcept = default;

  virtual bool   connect(size_t size = 0) = 0;
  virtual int    disconnect()             = 0;
  virtual int    disconnectAndUnlink()    = 0;
  size_t         getSize() const;
  unsigned char *getPtr();

  virtual bool isDisconnected() const                         = 0;
  virtual bool isExists(uint64_t timeout_usec = 500000) const = 0;

protected:
  int            shm_fd;
  int            shm_oflag;
  PERM           shm_perm;
  size_t         shm_size;
  unsigned char *shm_ptr;
};

// ****************************************************************************
//! @class SharedMemoryPosix
//! @brief \~english     Class that is described the method of accessing POSIX shared memory
//!        \~japanese-en Posix方式の共有メモリのアクセス方法を記述したクラス
//! @details
// ****************************************************************************
class SharedMemoryPosix : public SharedMemory
{
public:
  SharedMemoryPosix(std::string name, int oflag, PERM perm);
  ~SharedMemoryPosix();

  virtual bool connect(size_t size = 0);
  virtual int  disconnect();
  virtual int  disconnectAndUnlink();

  virtual bool isDisconnected() const;

  /**
   * @brief Check if shared memory exists and is initialized
   * @param timeout_usec Timeout in microseconds for waiting initialization (0 = no wait)
   * @return true if shared memory exists and is initialized, false otherwise
   */
  bool isExists(uint64_t timeout_usec = 500000) const;

protected:
  std::string shm_name;
};

// ****************************************************************************
// 過去のデータを時刻で引くための型（タイムマシン機能）
//
// レビュー R01 は「実装前に検索の意味を決めること」を求めていた。ここで
// 決めた内容をそのまま型にしてある。曖昧なまま API を足すと、後から
// 共有メモリ形式を変えることになるため。
// ****************************************************************************

/*!
 * \~japanese-en 目標時刻に対してどのサンプルを選ぶか．
 *
 * \~japanese-en 検索に使う時刻は **CLOCK_MONOTONIC_RAW のみ** である．
 *               壁時計（CLOCK_REALTIME）は NTP 同期で前後に飛ぶため、
 *               それを検索の基準にすると「時刻が巻き戻ってサンプルの順序が
 *               入れ替わる」「同じ時刻が二度現れる」といった事態が起き、
 *               安定して引けない。壁時計の値は記録として保持するが
 *               （ログの突き合わせや人間向けの表示に使う）、検索には使わない。
 */
enum class SearchPolicy
{
  //! 目標時刻に最も近いもの。等距離なら新しい方（発行番号が大きい方）。
  //! 有効なサンプルが1つでもあれば必ず見つかる。
  //! センサ間の時刻合わせ（オドメトリの更新時刻に一番近いスキャンを取る等）は
  //! これを使う。**既定値**。
  Nearest,
  //! 目標時刻以前で最も新しいもの。「その時刻に有効だった値」を得たいとき。
  //! 未来のデータを絶対に使いたくない再生用途向け。
  //! 該当が無い（保持している全てが目標より新しい）場合は TooOld。
  AtOrBefore,
  //! 目標時刻以降で最も古いもの。該当が無い場合は TooNew。
  AtOrAfter,
};

/*!
 * \~japanese-en 検索結果の状態．
 */
enum class SearchStatus
{
  Success,
  //! トピックに接続できていない
  NotConnected,
  //! 有効なサンプルが1件も無い
  Empty,
  //! 目標時刻が、保持している範囲より古い（既に上書きされた）
  TooOld,
  //! 目標時刻が、保持している範囲より新しい（まだ publish されていない）
  TooNew,
  //! 読み出し中に publisher が同じスロットを上書きし続け、
  //! 一貫したスナップショットを取れなかった。**再試行する価値がある**。
  //! データが無いこと（Empty / TooOld / TooNew）とは区別すること。
  Contended,
};

/*!
 * \~japanese-en 取得したサンプルの素性．
 */
struct SampleInfo
{
  uint64_t sequence             = 0;  //!< 発行番号（トピック内で一意・単調増加）
  //! CLOCK_MONOTONIC_RAW。**検索に使うのはこちらだけ**。
  uint64_t capture_monotonic_us = 0;
  //! CLOCK_REALTIME。記録用（ログの突き合わせや表示）。
  //! NTP で飛ぶため検索の基準には使わない。
  uint64_t capture_realtime_us  = 0;
  uint64_t payload_size         = 0;
};

/*!
 * \~japanese-en 現在保持している範囲．
 * \~japanese-en 「任意の時刻を引ける」わけではなく、リングに残っている分しか
 *               引けない。呼び出し側が保持期間を把握できるように公開する。
 */
struct RetentionWindow
{
  size_t   count                = 0;  //!< 有効なサンプル数（0 なら空）
  uint64_t oldest_sequence      = 0;
  uint64_t newest_sequence      = 0;
  //! 検索に使う時刻（CLOCK_MONOTONIC_RAW）の範囲
  uint64_t oldest_monotonic_us  = 0;
  uint64_t newest_monotonic_us  = 0;
  //! 記録用の壁時計。範囲の表示に使う（検索には使わない）
  uint64_t oldest_realtime_us   = 0;
  uint64_t newest_realtime_us   = 0;
};

/*!
 * \~japanese-en 時刻検索の指定．
 * \~japanese-en 時刻は CLOCK_MONOTONIC_RAW（getCurrentTimeUSec() と同じ時計）．
 */
struct TimeQuery
{
  uint64_t     time_us = 0;
  SearchPolicy policy  = SearchPolicy::Nearest;
};

// ****************************************************************************
// 共有メモリ形式 v2
//
// v1 には自己記述的な情報が一切無く、共有メモリ上の element_size / buf_num を
// 無検証で信じてポインタを組み立てていた。壊れた領域・切り詰められた領域・
// 別形式の領域を読むと範囲外ポインタが生まれ、実際に SIGSEGV していた。
// また「発行順」の正本が microsecond 時刻そのものだったため、同一時刻の
// publish で最新値を誤選択したり、seqlock の検証が ABA で素通りしたりした。
//
// v2 では固定長ヘッダとスロットレコードを置き、次を持たせる。
//   - magic / ABI 版 / ヘッダ長 / 総サイズ  … 別形式・別版・破損の検出
//   - payload_alignment                     … alignas(16) 以上の型の正しい配置
//   - generation                            … レイアウト世代（P3 で使う）
//   - sequence                              … 発行順の正本。時刻と分離する
//   - スロット単位の robust mutex           … 所有権。owner death をカーネルが検出
//
// 「書き込み中」を時刻で判定して 1 秒で奪う仕組みは廃止した。停止していただけの
// 生きた writer からスロットを奪うと、再開した旧 writer が新 writer の payload を
// 上書きし、壊れた値が有効データとして公開され得たため（R01-F04）。
// ****************************************************************************

//! @brief 共有メモリ先頭に置く固定長ヘッダ（128 バイト）
//! @details element_capacity / buf_num に依存しない固定長。
/*!
 * \~japanese-en ペイロードの持ち方．
 * \~japanese-en 同じトピックに別の持ち方で接続すると、長さの解釈が食い違って
 *               範囲外アクセスになる。ヘッダに記録して接続時に照合する。
 */
enum class PayloadKind : uint32_t
{
  Unknown    = 0,
  //! 固定長の 1 要素（Publisher<T> / Subscriber<T>）
  Scalar     = 1,
  //! 同じ要素型の可変長配列（Publisher<std::vector<T>>）
  Vector     = 2,
  //! 利用者がシリアライズしたバイト列（cv::Mat / Lidar2dScanData などの特殊化）
  Serialized = 3,
};

/*!
 * \~japanese-en 型を識別する ID を、型名の文字列から求める．
 *
 * \~japanese-en 同じサイズの別の型を取り違えると、落ちはしないが誤った値を
 *               success として返す。それを検出するために型名を畳み込んだ値を
 *               ヘッダへ記録する。
 * \~japanese-en `typeid().hash_code()` は同一プログラム内でしか意味を持たないと
 *               規定されているため使わない。ここではコンパイラが埋め込む
 *               関数シグネチャ（型名を含む）を畳み込む。
 * \~japanese-en **同じツールチェインでビルドされたプロセス間でのみ一致する。**
 *               共有メモリは同一マシン内の通信なので実用上これで足りるが、
 *               別のコンパイラでビルドしたプロセスと接続する運用は想定しない。
 */
template <typename T>
constexpr uint64_t
type_schema_id()
{
#if defined(__GNUC__) || defined(__clang__)
  const char *name = __PRETTY_FUNCTION__;
#else
  const char *name = "unsupported-compiler";
#endif
  uint64_t hash = 1469598103934665603ULL;  // FNV-1a offset basis
  for (const char *p = name; *p != '\0'; ++p)
  {
    hash ^= static_cast<uint64_t>(static_cast<unsigned char>(*p));
    hash *= 1099511628211ULL;
  }
  // 0 は「未設定」を表すので避ける
  return hash == 0 ? 1ULL : hash;
}

/*!
 * \~japanese-en トピックのペイロード書式の版を、利用者が明示するためのトレイト．
 *
 * \~japanese-en type_schema_id<T>() は `__PRETTY_FUNCTION__` の hash なので、
 *               **同じツールチェインでしか一致しない**。コンパイラや標準ライブラリ、
 *               言語バインディングが違えば同じ型でも別の値になり得るし、逆に
 *               「型名は同じだがメンバを足した」という**互換性を壊す変更**は
 *               検出できない（R03-F05）。
 *
 * \~japanese-en 別ビルドのプロセスと通信する構造体には、この版を明示すること。
 *               版は両者が 0 以外を指定したときだけ照合され、食い違えば接続を拒む。
 *               メンバの追加・削除・並べ替え・型変更のたびに増やす。
 *
 * @code
 * struct ScanHeader { uint32_t count; float angle_min; };
 * namespace irlab { namespace shm {
 * template <> struct shm_schema<ScanHeader> { static constexpr uint32_t version = 3; };
 * }}
 * @endcode
 */
template <typename T>
struct shm_schema
{
  //! 0 は「未指定」。その場合は type_schema_id<T>() だけで照合する。
  static constexpr uint32_t version = 0;
};

//! @brief shm_schema<T>::version を取り出す
template <typename T>
constexpr uint32_t
schema_version_of()
{
  return shm_schema<T>::version;
}

//!          レイアウトを読む前にこのヘッダだけで妥当性を判定できる。
struct ShmHeader
{
  uint32_t              magic;              //!< SHM_MAGIC。別形式・旧版の検出
  std::atomic<uint32_t> state;              //!< NOT_INITIALIZED / INITIALIZING / INITIALIZED
  uint16_t              abi_major;          //!< 非互換変更で増える
  uint16_t              abi_minor;          //!< 後方互換な追加で増える
  uint32_t              header_size;        //!< sizeof(ShmHeader)。前方互換用
  uint64_t              total_size;         //!< レイアウト全体のバイト数
  uint64_t              element_capacity;   //!< スロット1つ分の確保量
  uint64_t              buf_num;            //!< スロット数
  uint64_t              payload_alignment;  //!< ペイロードの境界
  uint64_t              slot_offset;        //!< SlotRecord[] の先頭
  uint64_t              slot_size;          //!< sizeof(SlotRecord)。ABI 差の検出
  uint64_t              data_offset;        //!< ペイロード先頭
  uint64_t              generation;         //!< このセグメント自身の世代（不変）
  std::atomic<uint64_t> sequence;           //!< 発行順の正本。単調増加
  uint64_t              boot_id_hash;       //!< 再起動をまたいだ残骸の検出
  //! トピック全体で現在有効な世代とノンスを 1 語に詰めたもの。
  //! **世代 1 のセグメントのものだけが正本**で、世代を進める側が CAS で更新する。
  //! 上位 16 bit が世代番号、下位 48 bit がその世代のノンス。
  //!
  //! 1 語にまとめてあるのは、世代とノンスを **不可分に** 公開するためである。
  //! 別々のフィールドにすると「新しい世代番号と古いノンス」の組を読み得る。
  //! packGeneration() / unpackGeneration() / unpackNonce() を使うこと。
  std::atomic<uint64_t> latest_generation;

  // ------------------------------------------------------------------------
  // topic contract（R02-F01）
  //
  // 「このトピックには何が入っているか」を記録する。購読側は接続時にこれを
  // 自分の期待と照合し、食い違ったら payload に一切触れずに失敗する。
  // これが無いと、たとえば Publisher<uint8_t> のトピックへ 1 MiB の型で
  // 接続した購読側が容量を超えて memcpy し、SIGSEGV する。
  // ------------------------------------------------------------------------
  uint64_t element_size;    //!< 要素 1 個のバイト数（Vector なら要素型のサイズ）
  uint64_t schema_id;       //!< 型を識別する ID。0 は未設定
  uint32_t payload_kind;    //!< PayloadKind
  uint32_t schema_version;  //!< 利用者が指定した schema の版。0 は未設定

  //! このセグメント自身のノンス。名前に含まれる値と一致することを接続時に確認する。
  //! 世代 1（root）は 0。
  uint64_t segment_nonce;
  uint64_t reserved[7];
};

//! @brief 世代番号とノンスを 1 語に詰める
//! @details 上位 16 bit が世代、下位 48 bit がノンス。
//!          不可分に公開するために 1 語へまとめる。
constexpr uint64_t
packGeneration(uint64_t generation, uint64_t nonce)
{
  return (generation << 48) | (nonce & 0x0000FFFFFFFFFFFFULL);
}
constexpr uint64_t
unpackGeneration(uint64_t packed)
{
  return packed >> 48;
}
constexpr uint64_t
unpackNonce(uint64_t packed)
{
  return packed & 0x0000FFFFFFFFFFFFULL;
}
//! 世代番号の上限（16 bit）。容量は増やすだけなので実運用では到達しない。
constexpr uint64_t MAX_GENERATION = 0xFFFFULL;

//! @brief スロット1つ分のメタデータ
//! @details 1スロット = 1キャッシュラインに載せ、Publisher 同士の false sharing を避ける。
struct alignas(64) SlotRecord
{
  //! 発行番号。0 は「有効なデータが無い」を意味する。
  //! 書き込み開始時に 0 へ落とし、完了時に header.sequence から採番した値を
  //! release store する。途中で writer が死んでも 0 のままなので読み手には見えない。
  std::atomic<uint64_t> sequence;
  //! 以下 3 つは、スロットの排他を取らずに読む経路がある（読むスロットの選択や
  //! 時刻検索）。plain な整数だと writer の書き込みと同時アクセスになり、
  //! C++ のメモリモデル上 data race（未定義動作）なので atomic にしてある
  //! （R03-F04）。値の整合性はロックを取る readSample() で保証する。
  std::atomic<uint64_t> payload_size;          //!< 実際に書かれた長さ
  std::atomic<uint64_t> capture_monotonic_us;  //!< CLOCK_MONOTONIC_RAW。期限判定用
  //! CLOCK_REALTIME。**記録専用で、検索には使わない**。
  //! NTP 同期で前後に飛ぶため検索の基準にできない（SearchPolicy のコメント参照）。
  std::atomic<uint64_t> capture_realtime_us;
  //! スロットの所有権。PTHREAD_PROCESS_SHARED かつ PTHREAD_MUTEX_ROBUST。
  //! trylock が EBUSY なら「生きている writer が使用中」なので絶対に奪わない。
  //! EOWNERDEAD（カーネルが所有者の死を確定）のときだけ回収する。
  pthread_mutex_t       owner;
};

// ****************************************************************************
//! @class RingBuffer
//! @brief \~english     Class that is described ring-buffer used for shared memory
//!        \~japanese-en 共有メモリで使用するリングバッファを記述したクラス
// ****************************************************************************
class RingBuffer
{
public:
  //! 'SHM2'。v1 の先頭 4 バイトは初期化フラグ (0/1/2) なので、
  //! 新しいコードが v1 の領域を読んでも必ず不一致になる。
  static constexpr uint32_t SHM_MAGIC = 0x324D4853;
  static constexpr uint16_t ABI_MAJOR = 4;
  static constexpr uint16_t ABI_MINOR = 0;

  // ------------------------------------------------------------------------
  // 入力の上限。現実的にあり得ない値を弾き、範囲外ポインタの生成を防ぐ。
  // ------------------------------------------------------------------------
  static constexpr size_t MAX_BUFFER_NUM   = 1024;
  static constexpr size_t MAX_ELEMENT_SIZE = 1ULL << 30;  // 1 GiB
  static constexpr size_t MAX_TOTAL_SIZE   = 1ULL << 32;  // 4 GiB
  //! 既定のペイロード境界。1 = 制約なし（バイト列として扱う）。
  //! 型が分かっている呼び出し側は alignof(T) を渡すこと。
  //! ペイロード領域の先頭は常に max(payload_alignment, 64) に載るので、
  //! element_size が payload_alignment の倍数である限り全スロットが整列する。
  static constexpr size_t DEFAULT_PAYLOAD_ALIGNMENT = 1;
  //! ページ境界を超えるアライメント要求は共有メモリでは満たせない
  //! スロットの robust mutex を待つ上限[usec]。
  //! 臨界区間は memcpy 1 回ぶんなので、これを超えるのは異常事態。
  static constexpr uint64_t SLOT_LOCK_TIMEOUT_US = 2000;
  static constexpr size_t MAX_PAYLOAD_ALIGNMENT = 4096;

  /*!
   * \~japanese-en トピックに何が入っているかの取り決め（R02-F01）．
   * \~japanese-en Publisher が記録し、Subscriber が接続時に照合する。
   *               食い違ったら payload に一切触れずに失敗する。
   */
  struct TopicContract
  {
    PayloadKind kind         = PayloadKind::Unknown;
    uint64_t    element_size = 0;  //!< 要素 1 個のバイト数
    uint64_t    schema_id    = 0;
    uint32_t    schema_version = 0;  //!< shm_schema<T>::version。0 は照合しない
    size_t      alignment    = DEFAULT_PAYLOAD_ALIGNMENT;

    bool matches(const TopicContract &other, std::string *reason = nullptr) const;
  };

  static size_t getSize(size_t element_size, int buffer_num,
                        size_t payload_alignment = DEFAULT_PAYLOAD_ALIGNMENT);
  static bool   checkInitialized(unsigned char *first_ptr);
  static bool   waitForInitialization(unsigned char *first_ptr, uint64_t timeout_usec);

  /*!
   * \~japanese-en 既存の共有メモリへ接続する前に、ヘッダとレイアウトが
   *               実マッピング長に収まっているかを検証する．
   * @param [in]  first_ptr    共有メモリ先頭
   * @param [in]  mapping_size 実際に mmap した長さ（SharedMemory::getSize()）
   * @param [out] reason       失敗理由（不要なら nullptr）
   * @return bool 接続してよければ真
   */
  static bool validateLayout(const unsigned char *first_ptr, size_t mapping_size, std::string *reason = nullptr,
                             const TopicContract *expected = nullptr, uint64_t expected_generation = 0);

  RingBuffer(unsigned char *first_ptr, size_t size = 0, int buffer_num = 0,
             size_t payload_alignment = DEFAULT_PAYLOAD_ALIGNMENT, const TopicContract *contract = nullptr,
             uint64_t own_generation = 1, uint64_t own_nonce = 0);
  ~RingBuffer();

  // --- 読み出し ---
  uint64_t getTimestamp_us() const;
  uint64_t getTimestamp_us(int buffer_num) const;
  //! 発行番号。0 は無効。順序判定はこちらを使うこと（時刻は重複し得る）
  uint64_t getSequence(int buffer_num) const;
  uint64_t getPayloadSize(int buffer_num) const;
  uint64_t getCaptureRealtime_us(int buffer_num) const;
  //! @brief スロットの素性をまとめて取得する
  SampleInfo getSampleInfo(int buffer_num) const;
  //! @brief 現在保持している範囲
  RetentionWindow getRetentionWindow() const;
  /*!
   * \~japanese-en 指定した時刻に対応するスロットを探す．
   * @param [in]  query  検索条件
   * @param [out] status 見つからなかった理由（不要なら nullptr）
   * @return int スロット番号。見つからなければ -1
   * @note 期限（setDataExpiryTime_us）はここでは適用しない。
   *       期限は「最新値が十分新しいか」の判定であって、
   *       過去を引く検索の対象を狭めるものではないため。
   */
  int findBufferNum(const TimeQuery &query, SearchStatus *status = nullptr) const;
  uint64_t getGeneration() const;
  //! @brief 共有メモリに記録されている topic contract
  TopicContract getContract() const;
  //! @brief トピック全体で現在有効な世代（世代 1 のセグメントのものが正本）
  //! @brief 現在有効な世代とノンスを詰めたタグ（root セグメントのものが正本）
  uint64_t getGenerationTag() const;
  //! @brief 現在有効な世代番号
  uint64_t getLatestGeneration() const;
  //! @brief このセグメント自身のノンス
  uint64_t getSegmentNonce() const;
  void     setGenerationTag(uint64_t tag);
  //! @brief 現在有効な世代を CAS で進める。成功したら真
  bool     tryAdvanceGenerationTag(uint64_t expected_tag, uint64_t desired_tag);
  //! @deprecated getTimestamp_us(int) が「無効」を表す値を返したかの判定。
  //!             v2 では sequence が 0 のスロットに対してこの値が返る。
  static bool    isBeingWritten(uint64_t timestamp);
  int            getNewestBufferNum();
  int            getOldestBufferNum();
  size_t         getElementSize() const;
  size_t         getBufferNum() const;
  size_t         getPayloadAlignment() const;
  unsigned char *getDataList();
  bool           isUpdated() const;
  void           setDataExpiryTime_us(uint64_t time_us);
  bool           waitFor(uint64_t timeout_usec);
  void           signal();
  bool           isLayoutChanged() const;
  void           markAsInitialized();
  //! @brief 読み終えた発行番号を記録する（isUpdated() / waitFor() の基準）
  void           markAsRead(uint64_t sequence);

  // --- 書き込み ---
  //! @brief スロットを確保する（robust mutex を取得し、内容を無効化する）
  //! @return bool 確保できたら真。他の生きた writer が使用中なら偽
  bool allocateBuffer(int buffer_num);
  //! @brief 書き込みを確定する（発行番号を採番し、スロットを手放す）
  //! @param [in] capture_monotonic_us 0 なら現在時刻を打つ
  void commitBuffer(int buffer_num, size_t payload_size, uint64_t capture_monotonic_us = 0);
  //! @brief 書かずにスロットを手放す
  void releaseBuffer(int buffer_num);
  /*!
   * \~japanese-en 別のリングバッファから取り出したサンプルを、素性を保ったまま取り込む．
   *               レイアウト世代を切り替えるときに履歴を引き継ぐために使う．
   *               発行番号・capture 時刻をそのまま維持し、ヘッダの発行番号カウンタを
   *               取り込んだ値以上へ進める（以後の publish が過去の番号を再利用しないため）．
   * @return bool 空きスロットが無ければ偽
   */
  bool adoptSample(const SampleInfo &info, const void *payload, size_t bytes);

  //! @brief 発行番号カウンタの現在値
  uint64_t getSequenceCounter() const;

  /*!
   * \~japanese-en 発行番号の採番元を外部（root セグメント）に差し替える．
   *
   * \~japanese-en 発行番号は**トピック内で一意**でなければならない。世代ごとに
   *               カウンタを持つと、旧世代の in-flight commit と新世代の最初の
   *               commit が同じ番号を採り得る（R03-F01）。
   *               全世代が root の 1 つのカウンタを共有すればこれが起きない。
   * @param [in] source 採番に使う atomic。nullptr で自前のカウンタに戻る
   */
  void setSequenceSource(std::atomic<uint64_t> *source);
  //! @brief このリングのヘッダにある発行番号カウンタ（root の正本を取り出す用）
  std::atomic<uint64_t> *sequenceCounter();

  /*!
   * \~japanese-en スロットを排他して、payload と素性を 1 つのスナップショットとして読む．
   *
   * \~japanese-en 発行番号の前後比較だけでは、writer と reader が同じ通常メモリへ
   *               同時に memcpy する可能性が残る。片方が書き込みで、atomic でも
   *               mutex でも同期されていないため、C++ のメモリモデル上これは
   *               data race であり未定義動作である（R03-F04）。
   *               スロットの robust mutex を取って読むことで、writer との
   *               相互排他と happens-before が成立する。
   * \~japanese-en writer 側は trylock で空いているスロットを選ぶので、
   *               読み手がスロットを保持していても writer が待たされることはない。
   *
   * @param [out] dst      payload の書き込み先
   * @param [in]  dst_size dst の容量。payload_size がこれを超えたら失敗する
   * @param [out] info     取得したサンプルの素性（不要なら nullptr）
   * @return bool 読めたら真。書き込み中・空・容量不足なら偽
   */
  bool readSample(int buffer_num, void *dst, size_t dst_size, SampleInfo *info);

  //! @deprecated commitBuffer() を使うこと。
  //!             互換のため残している。input_time_us は capture 時刻として記録する。
  void setTimestamp_us(uint64_t input_time_us, int buffer_num);

private:
  void        initializeExclusiveAccess();
  void        initializeOrAttach(size_t element_size, int buffer_num, size_t payload_alignment,
                                 const TopicContract *contract, uint64_t own_generation, uint64_t own_nonce);
  bool        hasCompatibleLayout(size_t element_size, int buffer_num, size_t payload_alignment,
                                  const TopicContract *contract) const;
  void        initializeContents(size_t element_size, int buffer_num, size_t payload_alignment,
                                 const TopicContract *contract, uint64_t own_generation, uint64_t own_nonce);
  void        bindPointers();
  SlotRecord *slot(int i) const;
  bool        ownsSlot(int i) const;
  void        setSlotOwned(int i, bool owned);

  unsigned char *memory_ptr;
  ShmHeader     *header;

  //! 発行番号の採番元。nullptr なら自分のヘッダのカウンタを使う。
  //! 世代をまたいで一意にするため、通常は root のカウンタを指す（R03-F01）。
  std::atomic<uint64_t> *sequence_source = nullptr;

  //! このインスタンスが robust mutex を保持しているスロット。
  //! allocateBuffer() を通さずに commitBuffer() / setTimestamp_us() を呼ぶ
  //! 使い方（テストや、確保せずに時刻だけ書き換える経路）があるため、
  //! 保持していない mutex を unlock しないようにここで追跡する。
  std::unique_ptr<std::atomic<bool>[]> owned_slots;
  unsigned char *slot_base;
  unsigned char *data_list;

  // このインスタンスが最後に選んだスロットの情報。共有メモリ上ではなく
  // プロセス側の状態なので、レイアウトには影響しない。
  // 1つの Publisher / Subscriber を複数スレッドから使うと読み書きが競合するため
  // atomic にしている（ThreadSanitizer で検出済み）。
  std::atomic<uint64_t> timestamp_us;
  std::atomic<uint64_t> last_sequence;
  std::atomic<uint64_t> data_expiry_time_us;

  // データ位置の計算に使ったレイアウト。共有メモリ上の値がこれと食い違ったら、
  // 別のプロセスが異なるレイアウトで初期化し直したということなので、
  // このインスタンスが持つオフセットは使えない（isLayoutChanged() 参照）。
  // 検証済みのレイアウト。**接続後はここだけを使う**（R02-F07）。
  // 共有ヘッダの live 値からポインタや長さを再計算すると、接続後に他プロセスが
  // ヘッダを書き換えた場合に「検証済み」という前提が崩れる。
  size_t   expected_element_size;
  size_t   expected_buf_num;
  size_t   expected_payload_alignment;
  uint64_t expected_generation;

  static constexpr uint32_t INITIALIZED     = 1;
  static constexpr uint32_t NOT_INITIALIZED = 0;
  // 初期化を実行中であることを示す中間状態。NOT_INITIALIZED からの CAS で
  // 一つの writer だけがこの状態に遷移でき、他は初期化の完了を待つ。
  static constexpr uint32_t INITIALIZING = 2;
  // 他プロセスによる初期化の完了を待つ上限。
  //
  // **待ちきれなくても takeover はしない**。時間の経過は相手の死の証明にならず、
  // 単に遅い／SIGSTOP で止まっているだけのプロセスが初期化している最中の
  // セグメントを奪うと、そのプロセスが書きかけの mutex やレイアウトを
  // 壊すことになる。待ち切れなかった場合は例外を投げて呼び出し側に返す。
  // （このコメントは実装と食い違っていた。将来 takeover を復活させないこと）
  static constexpr uint64_t INIT_WAIT_TIMEOUT_US = 500000;  // 500ms

  // getTimestamp_us(int) が「有効なデータが無い」ことを表すために返す値。
  // v1 の「書き込み途中」マーカーと同じビットなので、isBeingWritten() を
  // 使っている既存コードがそのまま動く。
  static constexpr uint64_t WRITING_FLAG = 1ULL << 63;
};

/*!
 * \~japanese-en 接続済みの共有メモリに、レイアウトが実マッピング長に収まって
 *               いることを確認してから RingBuffer を構築する．
 *
 *               既存の共有メモリへ接続するときは必ずこの関数を通すこと．
 *               `RingBuffer` を直接構築すると、切り詰められた／別形式の
 *               共有メモリに対してマッピング外を指すポインタが作られる
 *               （R01-F06。実測で SIGSEGV を確認済み）．
 *
 * @param [in]  memory 接続済みの共有メモリ
 * @param [out] reason 失敗理由（不要なら nullptr）
 * @return std::unique_ptr<RingBuffer> 失敗時は nullptr
 */
std::unique_ptr<RingBuffer> attachRingBuffer(SharedMemory &memory, std::string *reason = nullptr,
                                            const RingBuffer::TopicContract *expected = nullptr,
                                            uint64_t expected_generation = 0);

// ****************************************************************************
//! @class ShmTopic
//! @brief \~japanese-en 1つのトピックを表し、レイアウト世代の切り替えを引き受けるクラス
//! @details
//! 稼働中のセグメントを ftruncate して作り直すと、「レイアウト変更の確認」と
//! 「実際のデータアクセス」の間に必ず窓が空く。確認を増やしても窓は消えない。
//! 窓を消すには **一度公開したセグメントのレイアウトを二度と変えない** しかない。
//!
//! そこで、レイアウトを変えたくなったら既存セグメントには一切触れず、
//! 新しい世代のセグメントを別名で作って初期化し、最後に「現在有効な世代」を
//! CAS で進める。古い世代を掴んだままのプロセスは、有効なマッピングの中に
//! 読み書きし続けるだけで範囲外アクセスにはならず、次の呼び出しで世代の変化に
//! 気付いて張り直す。最悪でも「一時的に stale」で済み、「破損」にはならない。
//!
//! セグメントの名前:
//! \code
//!   /shm_<topic>        世代 1。データ本体であると同時にディレクトリを兼ねる。
//!                       ヘッダの latest_generation がトピック全体の正本。
//!   /shm_<topic>#<N>    世代 N (N >= 2)。
//! \endcode
//!
//! 世代 1 をディレクトリと兼用するのは、余分なマッピングを増やさないためと、
//! レイアウト変更が起きないトピック（スカラ型はこれに当たる）で
//! 従来と全く同じ構成のままにするため。
// ****************************************************************************
class ShmTopic
{
public:
  //! 世代の取り違えが無限に続かないための上限
  static constexpr int MAX_GENERATION_ATTEMPTS = 8;
  //! 世代セグメントの作成者が初期化を終えるのを待つ上限[usec]。
  //! ここで待ち切れなくても回収はしない（時間で生死を判定しない：R03-F03）。
  static constexpr uint64_t INIT_WAIT_TIMEOUT_US = 500000;  // 0.5s
  //! 切り替え直後に旧世代へ滑り込んだサンプルを拾い直す回数（R03-F01）
  static constexpr int MAX_DRAIN_ROUNDS = 4;

  ShmTopic(std::string name, PERM perm, bool create);
  ~ShmTopic();

  ShmTopic(const ShmTopic &)            = delete;
  ShmTopic &operator=(const ShmTopic &) = delete;

  /*!
   * \~japanese-en 要求する容量を満たす世代へ接続する（Publisher 用）．
   *               現世代が足りていればそのまま使い、足りなければ新しい世代を作る．
   * @param [in] required_capacity  1スロットに必要なバイト数
   * @param [in] buf_num            スロット数
   * @param [in] payload_alignment  ペイロードに要求する境界
   * @return bool 使える状態になったら真
   */
  bool ensureCapacity(size_t required_capacity, int buf_num, size_t payload_alignment,
                      const RingBuffer::TopicContract &contract);

  /*!
   * \~japanese-en 現在有効な世代へ追随する（Subscriber 用）．新しい世代は作らない．
   * @return bool 接続できたら真
   */
  bool follow(const RingBuffer::TopicContract *expected = nullptr);

  //! @brief 現世代のリングバッファ。未接続なら nullptr
  RingBuffer *ring() const { return ring_.get(); }
  //! @brief 現在接続している世代
  uint64_t generation() const { return unpackGeneration(current_tag_); }
  //! @brief 現在接続している世代タグ（世代 + ノンス）
  uint64_t generationTag() const { return current_tag_; }
  //! @brief 接続世代がまだ有効か（root の世代タグと一致するか）
  bool     isGeneration(uint64_t tag) const;
  //! @brief 直近の失敗理由
  const std::string &lastError() const { return last_error_; }
  //! @brief 全世代のセグメントを削除する
  static int  removeAllGenerations(const std::string &name);
  //! @brief 世代タグに対応するセグメント名を返す
  //! @details 世代 1 はトピック名そのもの。世代 N>=2 は "<topic>#<N>-<nonce16進>"。
  //!          ノンスを名前に入れることで、同じ世代番号でも作成者ごとに
  //!          別の名前になり、固定名 "#N" の取り合いが起きない（R03-F03）。
  static std::string generationName(const std::string &name, uint64_t tag);

private:
  bool openRoot(bool create, size_t initial_capacity, int buf_num, size_t payload_alignment,
                const RingBuffer::TopicContract *contract);
  bool attachGeneration(uint64_t tag, const RingBuffer::TopicContract *expected);
  bool createNextGeneration(uint64_t from_tag, size_t capacity, int buf_num, size_t payload_alignment,
                            const RingBuffer::TopicContract &contract);
  //! 現役でないと**確実に言える**世代セグメントだけを削除する（R03-F03）
  void unlinkStaleGenerations(uint64_t live_tag);
  //! 旧世代の有効なサンプルを新世代へ引き継ぐ（履歴を切らさないため）
  //! @return 引き継いだ中で最大の発行番号
  uint64_t migrateHistory(RingBuffer &source, RingBuffer &destination, uint64_t after_sequence);
  //! 世代の作り直しを繰り返さないよう、容量は増やすだけにし余裕を持たせる
  static size_t growCapacity(size_t current, size_t required, size_t alignment);

  std::string                        name_;
  PERM                               perm_;
  std::unique_ptr<SharedMemoryPosix> root_;  //!< /shm_<topic>（世代 1 兼ディレクトリ）
  //! root_ に対する RingBuffer。latest_generation を読むためだけに使う。
  //! publish/subscribe のたびに構築すると毎回ヒープ確保が走るのでキャッシュする。
  std::unique_ptr<RingBuffer>        root_ring_;
  std::unique_ptr<SharedMemoryPosix> data_;  //!< 現世代（世代 1 のときは nullptr）
  std::unique_ptr<RingBuffer>        ring_;
  uint64_t                           current_tag_;  //!< 現在接続している世代タグ
  std::string                        last_error_;
};

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_BASE_LIB_H__ */
