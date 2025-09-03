#include <shm_base.hpp>
#include <condition_variable>
#include <limits>
#include <chrono>
#include <thread>

namespace irlab
{

namespace shm
{

size_t
RingBuffer::getSize(size_t element_size, int buffer_num)
{
  // Use aligned layout calculation for accurate size
  size_t mutex_offset, cond_offset, element_size_offset, buf_num_offset, timestamp_offset, data_offset;
  return calculateAlignedLayout(element_size, buffer_num, mutex_offset, cond_offset,
                               element_size_offset, buf_num_offset, timestamp_offset, data_offset);
}

bool
RingBuffer::checkInitialized(unsigned char* first_ptr)
{
  if (first_ptr == nullptr)
  {
    return false;
  }

  std::atomic<uint32_t> *initialization_flag =
    reinterpret_cast<std::atomic<uint32_t> *>(first_ptr);
  return (initialization_flag->load(std::memory_order_relaxed) == RingBuffer::INITIALIZED);
}

bool
RingBuffer::waitForInitialization(unsigned char* first_ptr, uint64_t timeout_usec)
{
  auto start_time = std::chrono::steady_clock::now();
  auto timeout_duration = std::chrono::microseconds(timeout_usec);
  
  while (!RingBuffer::checkInitialized(first_ptr)) {
    auto current_time = std::chrono::steady_clock::now();
    if (current_time - start_time >= timeout_duration) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50)); // Reduced wait interval for faster response
  }
  
  return true;
}

size_t
RingBuffer::calculateAlignedLayout(size_t element_size, int buffer_num, 
                                   size_t& mutex_offset, size_t& cond_offset,
                                   size_t& element_size_offset, size_t& buf_num_offset,
                                   size_t& timestamp_offset, size_t& data_offset)
{
  size_t current_offset = 0;
  
  // 1. initialization_flag (std::atomic<uint32_t>) - starts at beginning
  current_offset = 0;
  
  // 2. pthread_init_flag (std::atomic<uint32_t>) - aligned to 8 bytes for ARM  
  current_offset += get_aligned_size<std::atomic<uint32_t>>(1);
  
  // 3. mutex (pthread_mutex_t) - aligned to 8 bytes for ARM
  mutex_offset = (current_offset + get_alignment<pthread_mutex_t>() - 1) & ~(get_alignment<pthread_mutex_t>() - 1);
  current_offset = mutex_offset + sizeof(pthread_mutex_t);
  
  // 4. condition (pthread_cond_t) - aligned to 8 bytes for ARM
  cond_offset = (current_offset + get_alignment<pthread_cond_t>() - 1) & ~(get_alignment<pthread_cond_t>() - 1);
  current_offset = cond_offset + sizeof(pthread_cond_t);
  
  // 5. element_size (size_t) - aligned to 8 bytes for ARM
  element_size_offset = (current_offset + get_alignment<size_t>() - 1) & ~(get_alignment<size_t>() - 1);
  current_offset = element_size_offset + sizeof(size_t);
  
  // 6. buf_num (size_t) - aligned to 8 bytes for ARM
  buf_num_offset = (current_offset + get_alignment<size_t>() - 1) & ~(get_alignment<size_t>() - 1);
  current_offset = buf_num_offset + sizeof(size_t);
  
  // 7. timestamp_list (std::atomic<uint64_t> * buffer_num) - aligned to 8 bytes for ARM
  timestamp_offset = (current_offset + get_alignment<std::atomic<uint64_t>>() - 1) & ~(get_alignment<std::atomic<uint64_t>>() - 1);
  current_offset = timestamp_offset + sizeof(std::atomic<uint64_t>) * buffer_num;
  
  // 8. data_list (aligned for element type) - use maximum alignment for safety
  const size_t data_alignment = std::max(get_alignment<uint64_t>(), static_cast<size_t>(8)); // At least 8-byte aligned on ARM
  data_offset = (current_offset + data_alignment - 1) & ~(data_alignment - 1);
  current_offset = data_offset + element_size * buffer_num;
  
  return current_offset;
}

//! @brief コンストラクタ
//! @param [in] 共有メモリ名
//! @return なし
//! @details 共有メモリへのアクセスを行う．
RingBuffer::RingBuffer(unsigned char *first_ptr, size_t size, int buffer_num)
  : memory_ptr(first_ptr)
  , timestamp_us(0)
  , data_expiry_time_us(2000000)
{
  // Use aligned layout calculation for ARM compatibility
  size_t mutex_offset, cond_offset, element_size_offset, buf_num_offset, timestamp_offset, data_offset;
  
  if (buffer_num != 0 && size != 0) {
    // Calculate aligned layout for new buffer creation
    calculateAlignedLayout(size, buffer_num, mutex_offset, cond_offset,
                          element_size_offset, buf_num_offset, timestamp_offset, data_offset);
  } else {
    // Reading existing buffer - need to extract parameters first
    element_size = reinterpret_cast<size_t *>(memory_ptr + sizeof(std::atomic<uint32_t>) + sizeof(std::atomic<uint32_t>) + sizeof(pthread_mutex_t) + sizeof(pthread_cond_t));
    buf_num = reinterpret_cast<size_t *>(memory_ptr + sizeof(std::atomic<uint32_t>) + sizeof(std::atomic<uint32_t>) + sizeof(pthread_mutex_t) + sizeof(pthread_cond_t) + sizeof(size_t));
    
    // Calculate aligned layout based on existing parameters
    calculateAlignedLayout(*element_size, *buf_num, mutex_offset, cond_offset,
                          element_size_offset, buf_num_offset, timestamp_offset, data_offset);
  }

  // Initialize pointers using calculated aligned offsets
  initialization_flag = reinterpret_cast<std::atomic<uint32_t> *>(memory_ptr);
  pthread_init_flag = reinterpret_cast<std::atomic<uint32_t> *>(memory_ptr + get_aligned_size<std::atomic<uint32_t>>(1));
  mutex = reinterpret_cast<pthread_mutex_t *>(memory_ptr + mutex_offset);
  condition = reinterpret_cast<pthread_cond_t *>(memory_ptr + cond_offset);
  element_size = reinterpret_cast<size_t *>(memory_ptr + element_size_offset);
  buf_num = reinterpret_cast<size_t *>(memory_ptr + buf_num_offset);
  timestamp_list = reinterpret_cast<std::atomic<uint64_t> *>(memory_ptr + timestamp_offset);
  data_list = memory_ptr + data_offset;

  // Initialize values for new buffers
  if (buffer_num != 0)
  {
    *element_size = size;
  }
  if (buffer_num != 0)
  {
    *buf_num = buffer_num;
  }

  if (buffer_num != 0)
  {
    // Mark as not initialized first
    initialization_flag->store(NOT_INITIALIZED, std::memory_order_relaxed);

    initializeExclusiveAccess();

    // Initialize all timestamp buffers to 0
    for (size_t i = 0; i < *buf_num; ++i)
    {
      timestamp_list[i].store(0, std::memory_order_relaxed);
    }

    // Ensure all memory operations are complete before marking as initialized
    std::atomic_thread_fence(std::memory_order_release);

    // Mark as initialized after all setup is complete
    initialization_flag->store(INITIALIZED, std::memory_order_release);
  }
  else
  {
    // For subscriber accessing existing memory, just set up pointers
    // Initialization check will be done via checkInitialized()
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

unsigned char *
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
  uint64_t timestamp_buf         = 0;
  size_t   newest_buffer         = -1;
  bool     found_valid_timestamp = false;

  for (size_t i = 0; i < *buf_num; i++)
  {
    if (timestamp_list[i] != std::numeric_limits<uint64_t>::max() && timestamp_list[i] > 0 &&
        timestamp_list[i] >= timestamp_buf)
    {
      timestamp_buf         = timestamp_list[i];
      newest_buffer         = i;
      found_valid_timestamp = true;
    }
  }

  // If no valid timestamp found, return -1
  if (!found_valid_timestamp)
  {
    return -1;
  }

  timestamp_us = timestamp_buf;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  uint64_t current_time_us = ((uint64_t)ts.tv_sec * 1000000L) + ((uint64_t)ts.tv_nsec / 1000L);
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
  size_t   oldest_buffer = 0;
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
  if (temp_buffer_timestamp == std::numeric_limits<uint64_t>::max())
  {
    // The buffer is already allocated
    return false;
  }
  return timestamp_list[buffer_num].compare_exchange_weak(temp_buffer_timestamp, std::numeric_limits<uint64_t>::max(),
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
  long            sec     = static_cast<long>(timeout_usec / 1000000);
  long            mod_sec = static_cast<long>(timeout_usec % 1000000);
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += sec;
  ts.tv_nsec += mod_sec * 1000;
  if (1000000000 <= ts.tv_nsec)
  {
    ts.tv_nsec -= 1000000000;
    ts.tv_sec += 1;
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

void
RingBuffer::markAsInitialized()
{
  initialization_flag->store(INITIALIZED, std::memory_order_release);
}

}  // namespace shm

}  // namespace irlab
