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
const PERM DEFAULT_PERM = static_cast<PERM>(PERM_USER_READ | PERM_USER_WRITE | PERM_GROUP_READ | PERM_GROUP_WRITE |
                                            PERM_OTHER_READ | PERM_OTHER_WRITE);

// ****************************************************************************
// Function Declarations
// 関数宣言
// ****************************************************************************

int      disconnectMemory(std::string name);
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
//! @class RingBuffer
//! @brief \~english     Class that is described ring-buffer used for shared memory
//!        \~japanese-en 共有メモリで使用するリングバッファを記述したクラス
//! @details
// ****************************************************************************
class RingBuffer
{
public:
  // ------------------------------------------------------------------------
  // 入力の上限
  //
  // 共有メモリ上のヘッダは今のところ magic も版も持たないため、破損した領域や
  // 別形式の領域を読むと任意の値が element_size / buf_num として出てくる。
  // 「現実的にあり得ない値」をここで弾き、範囲外ポインタの生成を防ぐ。
  // （恒久対策は形式 v2 の自己記述ヘッダ。R01-F06 参照）
  // ------------------------------------------------------------------------
  static constexpr size_t MAX_BUFFER_NUM  = 1024;
  static constexpr size_t MAX_ELEMENT_SIZE = 1ULL << 30;  // 1 GiB
  static constexpr size_t MAX_TOTAL_SIZE   = 1ULL << 32;  // 4 GiB

  static size_t getSize(size_t element_size, int buffer_num);
  static bool   checkInitialized(unsigned char *first_ptr);
  static bool   waitForInitialization(unsigned char *first_ptr, uint64_t timeout_usec);

  /*!
   * \~japanese-en 既存の共有メモリへ接続する前に、そのレイアウトが実際の
   *               マッピング長に収まっているかを検証する．
   *
   *               RingBuffer は共有メモリ上の element_size / buf_num を信じて
   *               ポインタを組み立てるが、マッピング長を知らないため
   *               「切り詰められた／壊れた共有メモリ」に接続すると
   *               マッピング外を指すポインタを作ってしまう（SIGSEGV）。
   *               接続側は必ずこの関数を先に通すこと．
   *
   * @param [in]  first_ptr    共有メモリ先頭
   * @param [in]  mapping_size 実際に mmap した長さ（SharedMemory::getSize()）
   * @param [out] reason       失敗理由（不要なら nullptr）
   * @return bool 接続してよければ真
   */
  static bool validateLayout(const unsigned char *first_ptr, size_t mapping_size, std::string *reason = nullptr);
  static size_t calculateAlignedLayout(size_t element_size, int buffer_num, size_t &mutex_offset, size_t &cond_offset,
                                       size_t &element_size_offset, size_t &buf_num_offset, size_t &timestamp_offset,
                                       size_t &data_offset);

  RingBuffer(unsigned char *first_ptr, size_t size = 0, int buffer_num = 0);
  ~RingBuffer();

  uint64_t       getTimestamp_us() const;
  uint64_t       getTimestamp_us(int buffer_num) const;
  static bool    isBeingWritten(uint64_t timestamp);
  void           setTimestamp_us(uint64_t input_time_us, int buffer_num);
  int            getNewestBufferNum();
  int            getOldestBufferNum();
  bool           allocateBuffer(int buffer_num);
  size_t         getElementSize() const;
  unsigned char *getDataList();
  void           signal();
  bool           waitFor(uint64_t timeout_usec);
  bool           isUpdated() const;
  void           setDataExpiryTime_us(uint64_t time_us);
  void           markAsInitialized();
  bool           isLayoutChanged() const;

private:
  void initializeExclusiveAccess();
  void initializeOrAttach(size_t element_size, int buffer_num);
  bool hasCompatibleLayout(size_t element_size, int buffer_num) const;
  void initializeContents(size_t element_size, int buffer_num);

  unsigned char *memory_ptr;

  std::atomic<uint32_t> *initialization_flag;
  std::atomic<uint32_t> *pthread_init_flag;
  // NOTE: mutex / condition / pthread_init_flag は現在どこからもロック・待機
  //       されていない（signal() をポーリングに置き換えた時点で役目を終えた）。
  //       レイアウト上の位置を占めるだけなので、共有メモリ形式を変更する
  //       タイミング（形式 v2）でスロット単位の robust mutex に置き換えて削除する。
  //       ここで消すとレイアウトが変わり新旧バイナリが混在できなくなるため、
  //       形式変更まではあえて残す。
  pthread_mutex_t       *mutex;
  pthread_cond_t        *condition;
  size_t                *element_size;
  size_t                *buf_num;
  std::atomic<uint64_t> *timestamp_list;
  unsigned char         *data_list;

  uint64_t timestamp_us;
  uint64_t data_expiry_time_us;

  // データ位置の計算に使ったレイアウト。共有メモリ上の値がこれと食い違ったら、
  // 別のプロセスが異なるレイアウトで初期化し直したということなので、
  // このインスタンスが持つオフセットは使えない（isLayoutChanged() 参照）。
  size_t expected_element_size;
  size_t expected_buf_num;

  static constexpr uint32_t INITIALIZED             = 1;
  static constexpr uint32_t NOT_INITIALIZED         = 0;
  // 初期化を実行中であることを示す中間状態。NOT_INITIALIZED からの CAS で
  // 一つの writer だけがこの状態に遷移でき、他は初期化の完了を待つ。
  // checkInitialized() は INITIALIZED との一致で判定するため、この状態は
  // 購読側からは「未初期化」として扱われる（＝初期化途中を読まない）。
  static constexpr uint32_t INITIALIZING           = 2;
  // 他プロセスによる初期化の完了を待つ上限。待ちきれなかった場合は
  // 初期化中に落ちたプロセスの残骸とみなして自分で初期化し直す。
  static constexpr uint64_t INIT_WAIT_TIMEOUT_US   = 500000;  // 500ms
  static constexpr uint32_t PTHREAD_INITIALIZED     = 1;
  static constexpr uint32_t PTHREAD_NOT_INITIALIZED = 0;

  // 「書き込み途中」マーカー: 最上位ビットを立て、下位に確保時刻[usec]を
  // 埋め込む（時刻は起動からの経過なので最上位ビットが立つことはない）。
  // 保持していた writer がクラッシュした場合、マーカーが STALE_WRITE_TIMEOUT_US
  // より古くなった時点で他の writer が奪って再利用する。
  // 旧形式のマーカー (UINT64_MAX) も最上位ビットが立っているため
  // 「確保時刻不明 = 十分古い」として回収対象になる。
  static constexpr uint64_t WRITING_FLAG           = 1ULL << 63;
  static constexpr uint64_t STALE_WRITE_TIMEOUT_US = 1000000;  // 1s
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
std::unique_ptr<RingBuffer> attachRingBuffer(SharedMemory &memory, std::string *reason = nullptr);

}  // namespace shm

}  // namespace irlab

#endif /* __SHM_BASE_LIB_H__ */
