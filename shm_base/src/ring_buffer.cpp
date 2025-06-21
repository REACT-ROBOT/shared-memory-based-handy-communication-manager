#include <shm_base.hpp>
#include <condition_variable>
#include <limits>

namespace irlab
{

namespace shm
{

size_t
RingBuffer::getSize(size_t element_size, int buffer_num)
{
  return (sizeof(size_t) + sizeof(std::mutex) + sizeof(std::condition_variable) + sizeof(size_t) + (sizeof(std::atomic<uint64_t>)+element_size)*buffer_num);
}

//! @brief コンストラクタ
//! @param [in] 共有メモリ名
//! @return なし
//! @details 共有メモリへのアクセスを行う．
RingBuffer::RingBuffer(unsigned char* first_ptr, size_t size, int buffer_num)
: memory_ptr(first_ptr)
, timestamp_us(0)
, data_expiry_time_us(2000000)
{
  unsigned char* temp_ptr = memory_ptr;
  mutex          = reinterpret_cast<pthread_mutex_t *>(temp_ptr);
  temp_ptr      += sizeof(pthread_mutex_t);
  condition      = reinterpret_cast<pthread_cond_t *>(temp_ptr);
  temp_ptr      += sizeof(pthread_cond_t);
  element_size   = reinterpret_cast<size_t *>(temp_ptr);
  if (size != 0)
  {
    *element_size = size;
  }
  temp_ptr      += sizeof(size_t);
  buf_num        = reinterpret_cast<size_t *>(temp_ptr);
  if (buffer_num != 0)
  {
    *buf_num = buffer_num;
  }
  temp_ptr      += sizeof(size_t);
  timestamp_list = reinterpret_cast<std::atomic<uint64_t> *>(temp_ptr);
  temp_ptr += sizeof(std::atomic<uint64_t>) * *buf_num;
  data_list      = temp_ptr;

  if (buffer_num != 0)
  {
    initializeExclusiveAccess();
  }
}

RingBuffer::~RingBuffer()
{
}


void
RingBuffer::initializeExclusiveAccess()
{
  pthread_condattr_t cond_attr;
  pthread_condattr_init(&cond_attr);
  pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(condition, &cond_attr);
  pthread_condattr_destroy(&cond_attr);

  pthread_mutexattr_t m_attr;
  pthread_mutexattr_init(&m_attr);
  pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(mutex, &m_attr);
  pthread_mutexattr_destroy(&m_attr);
}


size_t
RingBuffer::getElementSize() const
{
  return *element_size;
}

unsigned char*
RingBuffer::getDataList()
{
  return data_list;
}

//! @brief タイムスタンプ取得
//! @param なし
//! @return なし
//! @details 直近で読み込んだトピックのタイムスタンプを返す．
const uint64_t
RingBuffer::getTimestamp_us() const
{
  return timestamp_us;
}


//! @brief タイムスタンプ取得
//! @param なし
//! @return なし
//! @details 直近で読み込んだトピックのタイムスタンプを返す．
void
RingBuffer::setTimestamp_us(uint64_t input_time_us, int buffer_num)
{
  timestamp_list[buffer_num] = input_time_us;
}


int
RingBuffer::getNewestBufferNum()
{
  uint64_t timestamp_buf = 0;
  size_t newest_buffer = -1;
  for (size_t i = 0; i < *buf_num; i++)
  {
    if (timestamp_list[i] != std::numeric_limits<uint64_t>::max() && timestamp_list[i] > timestamp_buf)
    {
      timestamp_buf = timestamp_list[i];
      newest_buffer = i;
    }
  }
  timestamp_us = timestamp_buf;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  uint64_t current_time_us = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);
  if (current_time_us - timestamp_us < data_expiry_time_us)
  {
    return newest_buffer;
  }
  return -1;
}


int
RingBuffer::getOldestBufferNum()
{
  if (timestamp_us == 0)
  {
    timestamp_us = UINT64_MAX;
  }
  uint64_t timestamp_buf = timestamp_list[0];
  size_t oldest_buffer = 0;
  for (size_t i = 0; i < *buf_num; i++)
  {
    if (timestamp_list[i] < timestamp_buf)
    {
      timestamp_buf = timestamp_list[i];
      oldest_buffer = i;
    }
  }
  
  timestamp_us = timestamp_buf;
  return oldest_buffer;
}


bool
RingBuffer::allocateBuffer(int buffer_num)
{
  if (buffer_num < 0 || buffer_num >= *buf_num)
  {
    return false;
  }
  uint64_t temp_buffer_timestamp = timestamp_list[buffer_num].load(std::memory_order_acquire);
  return timestamp_list[buffer_num].compare_exchange_weak(temp_buffer_timestamp,
                                                          std::numeric_limits<uint64_t>::max(),
                                                          std::memory_order_relaxed);
}

void
RingBuffer::signal()
{
  pthread_cond_broadcast(condition);
}

//! @brief トピックの更新待ち
//! @param timeout_usec 待ち時間[usec]
//! @return bool トピックが更新されたかどうか
//! @details 待ち時間の間、トピックの更新を待ち続ける．更新された場合または待ち時間が経過した場合、関数を終了する．
//! @note 単純なループによる待ち方に比べて動作が軽量となるはずであるが、未確認
bool
RingBuffer::waitFor(uint64_t timeout_usec)
{
  struct timespec ts;
  long sec  = static_cast<long>(timeout_usec / 1000000);
  long mod_sec = static_cast<long>(timeout_usec % 1000000);
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += sec;
  ts.tv_nsec+= mod_sec * 1000;
  if (1000000000 <= ts.tv_nsec)
  {
    ts.tv_nsec -= 1000000000;
    ts.tv_sec  += 1;
  }
  
  while (!isUpdated())
  {
    // Wait on the condvar
    pthread_mutex_lock(mutex);
    int ret = pthread_cond_timedwait(condition, mutex, &ts);
    pthread_mutex_unlock(mutex);
    if (ret == ETIMEDOUT)
    {
      return false;
    }
  }

  return true;
}


//! @brief 共有メモリの更新確認
//! @param なし
//! @return bool
//! @details 直近で読み込んだタイムスタンプより新しいタイムスタンプが書き込まれたか確認する．
//! 更新があった場合には真を、ない場合には偽を返す．
bool
RingBuffer::isUpdated() const
{
  for (size_t i = 0; i < *buf_num; i++)
  {
    if (timestamp_us < timestamp_list[i])
    {
      return true;
    }
  }
  return false;
}


void
RingBuffer::setDataExpiryTime_us(uint64_t time_us)
{
  data_expiry_time_us = time_us;
}


}

}

