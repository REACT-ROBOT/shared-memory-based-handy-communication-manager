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
  
private:
  void initializeExclusiveAccess();
  
  unsigned char *memory_ptr;

  pthread_mutex_t *mutex;
  pthread_cond_t *condition;
  size_t *element_size;
  size_t *buf_num;
  std::atomic<uint64_t> *timestamp_list;
  unsigned char *data_list;
  
  uint64_t timestamp_us;
  uint64_t data_expiry_time_us;
};

}

}

#endif /* __SHM_BASE_LIB_H__ */
