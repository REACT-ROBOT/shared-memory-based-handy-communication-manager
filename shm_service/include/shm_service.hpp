//!
//! @file shm_service.hpp
//! @brief メモリの格納方法を規定するクラスの定義
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
//! 
//! @example test1.hpp
//! 共有メモリに関するテスト
//! @example test1.cpp
//! 共有メモリに関するテスト
//!

#ifndef __SHM_SERVICE_LIB_H__
#define __SHM_SERVICE_LIB_H__

#include <string>
#include <thread>
#include <memory>
#include "shm_base.hpp"

namespace irlab
{

namespace shm
{

// ****************************************************************************
//! @class ServiceServer
//! @brief 共有メモリで受信したリクエストからレスポンスを返すサーバーを表現するクラス
//! @details template classとして与えられた型またはクラスをリクエストおよびレスポンスとしてリクエストからレスポンスを出力するクラスである．
//! sizeofによってメモリの使用量が把握できる型およびクラスに対応している．
//! また、特殊なものはtemplate classを特殊化して対応する．
// ****************************************************************************
template <class Req, class Res>
class ServiceServer
{
public:
  ServiceServer(std::string name, Res (*input_func)(Req request), PERM perm = DEFAULT_PERM);
  ~ServiceServer();

private:
  void initializeExclusiveAccess();
  void loop();
  static void called_loop(ServiceServer& ref)
  {
    ref.loop();
  }

  Res (*func)(Req request);
  pthread_t thread;
  volatile bool shutdown_requested;

  std::string shm_name;
  PERM shm_perm;
  SharedMemory *shared_memory;

  uint8_t *memory_ptr;

  pthread_mutex_t *request_mutex;
  pthread_cond_t *request_condition;
  uint64_t *request_timestamp_usec;
  Req *request_ptr;
  pthread_mutex_t *response_mutex;
  pthread_cond_t *response_condition;
  uint64_t *response_timestamp_usec;
  Res *response_ptr;

  uint64_t current_request_timestamp_usec;
};

// ****************************************************************************
//! @class ServiceClient
//! @brief 共有メモリからトピックを取得する購読者を表現するクラス
//! @details template classとして与えられた型またはクラスをトピックとして読み込むためのクラスである．
//! また、トピックが更新されるまで待機するAPIを持つ．
// ****************************************************************************
template <class Req, class Res>
class ServiceClient
{
public:
  ServiceClient(std::string name);
  ~ServiceClient();

  bool call(Req request, Res *response);
  bool call(Req request, Res *response, unsigned long timeout_usec);

private:
  std::string shm_name;
  SharedMemory *shared_memory;

  uint8_t *memory_ptr;

  pthread_mutex_t *request_mutex;
  pthread_cond_t *request_condition;
  uint64_t *request_timestamp_usec;
  Req *request_ptr;
  pthread_mutex_t *response_mutex;
  pthread_cond_t *response_condition;
  uint64_t *response_timestamp_usec;
  Res *response_ptr;

  uint64_t current_response_timestamp_usec;
};

// ****************************************************************************
// 関数定義
// （テンプレートクラス内の関数の定義はコンパイル時に実体化するのでヘッダに書く）
// ****************************************************************************
template <class Req, class Res>
ServiceServer<Req, Res>::ServiceServer(std::string name, Res (*input_func)(Req request), PERM perm)
: func(input_func)
, shutdown_requested(false)
, shm_name(name)
, shm_perm(perm)
, shared_memory(nullptr)
, memory_ptr(nullptr)
{
  if (!std::is_standard_layout<Req>::value || !std::is_standard_layout<Res>::value)
  {
    throw std::runtime_error("shm::ServiceServer: Be setted not POD class!");
  }
  
  shared_memory = new SharedMemoryPosix(shm_name, O_RDWR|O_CREAT, shm_perm);
  shared_memory->connect( (sizeof(pthread_mutex_t)+sizeof(pthread_cond_t)+sizeof(uint64_t)) * 2 + sizeof(Req) + sizeof(Res));
  if (shared_memory->isDisconnected())
  {
    throw std::runtime_error("shm::Publisher: Cannot get memory!");
  }

  uint8_t *data_ptr = shared_memory->getPtr();
  memory_ptr = data_ptr;
  request_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
  data_ptr += sizeof(pthread_mutex_t);
  request_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
  data_ptr += sizeof(pthread_cond_t);
  request_timestamp_usec = reinterpret_cast<uint64_t *>(data_ptr);
  data_ptr += sizeof(uint64_t);
  request_ptr = reinterpret_cast<Req *>(data_ptr);
  data_ptr += sizeof(Req);
  response_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
  data_ptr += sizeof(pthread_mutex_t);
  response_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
  data_ptr += sizeof(pthread_cond_t);
  response_timestamp_usec = reinterpret_cast<uint64_t *>(data_ptr);
  data_ptr += sizeof(uint64_t);
  response_ptr = reinterpret_cast<Res *>(data_ptr);

  initializeExclusiveAccess();

  *request_timestamp_usec = getCurrentTimeUSec();
  *response_timestamp_usec = *request_timestamp_usec;
  current_request_timestamp_usec = *request_timestamp_usec;

  pthread_create(&thread, NULL, reinterpret_cast<void* (*)(void*)>(&ServiceServer<Req, Res>::called_loop), this);
}

template <class Req, class Res>
ServiceServer<Req, Res>::~ServiceServer()
{
  // Request graceful shutdown
  shutdown_requested = true;
  
  // Wake up the thread
  pthread_cond_broadcast(request_condition);
  
  // Wait for thread to finish gracefully, then force if needed
  pthread_cancel(thread);
  pthread_join(thread, nullptr);

  shared_memory->disconnect();
  if (shared_memory != nullptr)
  {
    delete shared_memory;
  }
}

template <class Req, class Res>
void
ServiceServer<Req, Res>::initializeExclusiveAccess()
{
  pthread_condattr_t request_cond_attr;
  pthread_condattr_init(&request_cond_attr);
  pthread_condattr_setpshared(&request_cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(request_condition, &request_cond_attr);
  pthread_condattr_destroy(&request_cond_attr);

  pthread_mutexattr_t request_m_attr;
  pthread_mutexattr_init(&request_m_attr);
  pthread_mutexattr_setpshared(&request_m_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(request_mutex, &request_m_attr);
  pthread_mutexattr_destroy(&request_m_attr);

  pthread_condattr_t response_cond_attr;
  pthread_condattr_init(&response_cond_attr);
  pthread_condattr_setpshared(&response_cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(response_condition, &response_cond_attr);
  pthread_condattr_destroy(&response_cond_attr);

  pthread_mutexattr_t response_m_attr;
  pthread_mutexattr_init(&response_m_attr);
  pthread_mutexattr_setpshared(&response_m_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(response_mutex, &response_m_attr);
  pthread_mutexattr_destroy(&response_m_attr);
}

template <class Req, class Res>
void
ServiceServer<Req, Res>::loop()
{
  // Pre-allocate objects to avoid repeated allocation/deallocation
  std::unique_ptr<Req> current_request_ptr = std::make_unique<Req>();
  std::unique_ptr<Res> result_ptr = std::make_unique<Res>();
  
  while (!shutdown_requested)
  {
    // Fix race condition: Check timestamp inside mutex
    pthread_mutex_lock(request_mutex);
    while (current_request_timestamp_usec >= *request_timestamp_usec && !shutdown_requested)
    {
      // Wait on the condvar while holding the mutex
      pthread_cond_wait(request_condition, request_mutex);
    }
    
    // Check for shutdown request
    if (shutdown_requested)
    {
      pthread_mutex_unlock(request_mutex);
      break;
    }
    
    // Update current timestamp and copy request data while holding mutex
    current_request_timestamp_usec = *request_timestamp_usec;
    *current_request_ptr = *request_ptr;
    pthread_mutex_unlock(request_mutex);

    // Process request outside of mutex to avoid blocking other clients
    *result_ptr = func(*current_request_ptr);
    
    // Check again for shutdown before responding
    if (shutdown_requested)
    {
      break;
    }
    
    // Update response under mutex protection
    pthread_mutex_lock(response_mutex);
    *response_ptr = *result_ptr;
    *response_timestamp_usec = getCurrentTimeUSec();
    pthread_mutex_unlock(response_mutex);

    pthread_cond_broadcast(response_condition);
  }
}


template <class Req, class Res>
ServiceClient<Req, Res>::ServiceClient(std::string name)
: shm_name(name)
, shared_memory(nullptr)
{
  if (!std::is_standard_layout<Req>::value || !std::is_standard_layout<Res>::value)
  {
    throw std::runtime_error("shm::ServiceClient: Be setted not POD class!");
  }
  shared_memory = new SharedMemoryPosix(shm_name, O_RDWR, static_cast<PERM>(0));

  current_response_timestamp_usec = getCurrentTimeUSec();
}

template <class Req, class Res>
ServiceClient<Req, Res>::~ServiceClient()
{
  if (shared_memory != nullptr)
  {
    delete shared_memory;
  }
}

template <class Req, class Res>
bool
ServiceClient<Req, Res>::call(Req request, Res *response)
{
  // Default timeout of 5 seconds
  return call(request, response, 5000000);
}

template <class Req, class Res>
bool
ServiceClient<Req, Res>::call(Req request, Res *response, unsigned long timeout_usec)
{
  // Check the service shared memory existence.
  if (shared_memory->isDisconnected())
  {
    shared_memory->connect();
    if (shared_memory->isDisconnected())
    {
      return false;
    }
    uint8_t *data_ptr = shared_memory->getPtr();
    memory_ptr = data_ptr;
    request_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
    data_ptr += sizeof(pthread_mutex_t);
    request_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
    data_ptr += sizeof(pthread_cond_t);
    request_timestamp_usec = reinterpret_cast<uint64_t *>(data_ptr);
    data_ptr += sizeof(uint64_t);
    request_ptr = reinterpret_cast<Req *>(data_ptr);
    data_ptr += sizeof(Req);
    response_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
    data_ptr += sizeof(pthread_mutex_t);
    response_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
    data_ptr += sizeof(pthread_cond_t);
    response_timestamp_usec = reinterpret_cast<uint64_t *>(data_ptr);
    data_ptr += sizeof(uint64_t);
    response_ptr = reinterpret_cast<Res *>(data_ptr);
  }

  // Set request to shared memory
  *request_ptr = request;
  *request_timestamp_usec = getCurrentTimeUSec();

  pthread_cond_broadcast(request_condition);

  // Simple timeout implementation using loop with small delays
  uint64_t start_time = *request_timestamp_usec;
  uint64_t end_time = start_time + timeout_usec;

  while (current_response_timestamp_usec >= *response_timestamp_usec)
  {
    // Check timeout
    uint64_t current_time = getCurrentTimeUSec();
    if (current_time > end_time)
    {
      return false; // Timeout
    }
    
    // Wait on the condvar with short timeout
    pthread_mutex_lock(response_mutex);
    struct timespec wait_time;
    wait_time.tv_sec = 0;
    wait_time.tv_nsec = 10000000; // 10ms
    pthread_cond_timedwait(response_condition, response_mutex, &wait_time);
    pthread_mutex_unlock(response_mutex);
  }
  current_response_timestamp_usec = *response_timestamp_usec;

  // Get response from shared memory
  *response = *response_ptr;

  return true;
}

}

}

#endif //__SHM_SERVICE_LIB_H__