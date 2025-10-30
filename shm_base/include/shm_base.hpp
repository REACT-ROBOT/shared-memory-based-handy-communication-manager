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
  constexpr bool is_arm_platform() {
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
  template<typename T>
  constexpr size_t get_alignment() {
    if constexpr (is_arm_platform()) {
      // ARM requires strict alignment - use 8-byte minimum for safety
      return std::max({alignof(T), sizeof(void*), static_cast<size_t>(8)});
    } else {
      // x86/x64 is more lenient
      return alignof(T);
    }
  }
  
  /*!
   * \~english     Align pointer to required boundary
   * \~japanese-en ポインタを必要な境界にアライン
   */
  template<typename T>
  inline T* align_pointer(void* ptr) {
    const size_t alignment = get_alignment<T>();
    const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    
    // Additional safety check for ARM
    if constexpr (is_arm_platform()) {
      if ((aligned_addr % alignment) != 0) {
        throw std::runtime_error("ARM alignment failure: unable to align pointer properly");
      }
    }
    
    return reinterpret_cast<T*>(aligned_addr);
  }
  
  /*!
   * \~english     Calculate aligned size for type T
   * \~japanese-en 型Tのアライン済みサイズを計算
   */
  template<typename T>
  constexpr size_t get_aligned_size(size_t count = 1) {
    const size_t alignment = get_alignment<T>();
    const size_t size = sizeof(T) * count;
    return (size + alignment - 1) & ~(alignment - 1);
  }
  
  /*!
   * \~english     Check if pointer is properly aligned for type T
   * \~japanese-en ポインタが型Tに対して適切にアライメントされているかチェック
   */
  template<typename T>
  inline bool is_aligned(const void* ptr) {
    if constexpr (!is_arm_platform()) {
      // x86/x64: Always return true for compatibility
      return true;
    } else {
      // ARM: Strict alignment checking
      if (ptr == nullptr) {
        return false;
      }
      const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
      const size_t alignment = get_alignment<T>();
      bool aligned = (addr % alignment) == 0;
      
      // Additional check for double types on ARM
      if constexpr (std::is_same_v<T, double> || sizeof(T) == sizeof(double)) {
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
    PERM_USER_READ   = S_IRUSR, /*!< 
				 * \~english     Owner readable
				 * \~japanese-en 所有者の読み込み許可
				 */
    PERM_USER_WRITE  = S_IWUSR, /*!< 
				 * \~english     Owner writable
				 * \~japanese-en 所有者の書き込み許可
				 */
    PERM_GROUP_READ  = S_IRGRP, /*!<
				 * \~english     Group that owner belong readable
				 * \~japanese-en 所有者のグループの読み込み許可
				 */
    PERM_GROUP_WRITE = S_IWGRP, /*!<
				 * \~english     Group that owner belong writable
				 * \~japanese-en 所有者のグループの書き込み許可
				 */
    PERM_OTHER_READ  = S_IROTH, /*!<
				 * \~english     Others readable
				 * \~japanese-en その他の読み込み許可
				 */
    PERM_OTHER_WRITE = S_IWOTH, /*!<
				 * \~english     Others writable
				 * \~japanese-en その他の書き込み許可
				 */
  };
  const PERM DEFAULT_PERM = static_cast<PERM>(PERM_USER_READ | PERM_USER_WRITE | PERM_GROUP_READ | PERM_GROUP_WRITE | PERM_OTHER_READ | PERM_OTHER_WRITE);


// ****************************************************************************
// Function Declarations
// 関数宣言
// ****************************************************************************

int disconnectMemory(std::string name);
int disconnectMemory(int id);

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

  virtual bool connect(size_t size = 0) = 0;
  virtual int disconnect() = 0;
  virtual int disconnectAndUnlink() = 0;
  size_t getSize() const;
  unsigned char* getPtr();

  virtual bool isDisconnected() const = 0;

protected:
  int shm_fd;
  int shm_oflag;
  PERM shm_perm;
  size_t shm_size;
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
  virtual int disconnect();
  virtual int disconnectAndUnlink();

  virtual bool isDisconnected() const;

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
  static size_t getSize(size_t element_size, int buffer_num);
  static bool checkInitialized(unsigned char* first_ptr);
  static bool waitForInitialization(unsigned char* first_ptr, uint64_t timeout_usec);
  static size_t calculateAlignedLayout(size_t element_size, int buffer_num, 
                                       size_t& mutex_offset, size_t& cond_offset,
                                       size_t& element_size_offset, size_t& buf_num_offset,
                                       size_t& timestamp_offset, size_t& data_offset);

  RingBuffer(unsigned char* first_ptr, size_t size = 0, int buffer_num = 0);
  ~RingBuffer();
  
  const uint64_t getTimestamp_us() const;
  void setTimestamp_us(uint64_t input_time_us, int buffer_num);
  int getNewestBufferNum();
  int getOldestBufferNum();
  bool allocateBuffer(int buffer_num);
  size_t getElementSize() const;
  unsigned char* getDataList();
  void signal();
  bool waitFor(uint64_t timeout_usec);
  bool isUpdated() const;
  void setDataExpiryTime_us(uint64_t time_us);
  void markAsInitialized();
  
private:
  void initializeExclusiveAccess();
  void initializeAlignedPointers();
  bool waitForPthreadInitialization(uint64_t timeout_usec);
  
  unsigned char *memory_ptr;
  
  std::atomic<uint32_t> *initialization_flag;
  std::atomic<uint32_t> *pthread_init_flag;
  pthread_mutex_t *mutex;
  pthread_cond_t *condition;
  size_t *element_size;
  size_t *buf_num;
  std::atomic<uint64_t> *timestamp_list;
  unsigned char *data_list;
  
  uint64_t timestamp_us;
  uint64_t data_expiry_time_us;
  
  static constexpr uint32_t INITIALIZED = 1;
  static constexpr uint32_t NOT_INITIALIZED = 0;
  static constexpr uint32_t PTHREAD_INITIALIZED = 1;
  static constexpr uint32_t PTHREAD_NOT_INITIALIZED = 0;
};

}

}

#endif /* __SHM_BASE_LIB_H__ */
