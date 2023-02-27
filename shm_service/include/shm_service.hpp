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

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  *request_timestamp_usec = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);
  *response_timestamp_usec = *request_timestamp_usec;
  current_request_timestamp_usec = *request_timestamp_usec;

  pthread_create(&thread, NULL, reinterpret_cast<void* (*)(void*)>(&ServiceServer<Req, Res>::called_loop), this);
}

template <class Req, class Res>
ServiceServer<Req, Res>::~ServiceServer()
{
  pthread_cancel(thread);

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
  while (1)
  {
    while (current_request_timestamp_usec >= *request_timestamp_usec)
    {
      // Wait on the condvar
      pthread_mutex_lock(request_mutex);
      pthread_cond_wait(request_condition, request_mutex);
      pthread_mutex_unlock(request_mutex);
    }
    current_request_timestamp_usec = *request_timestamp_usec;

    *response_ptr = func(*request_ptr);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    *response_timestamp_usec = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);

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

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  current_response_timestamp_usec = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);
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
  // Check the service shared memory existance.
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
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  *request_timestamp_usec = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);

  pthread_cond_broadcast(request_condition);

  while (current_response_timestamp_usec >= *response_timestamp_usec)
  {
    // Wait on the condvar
    pthread_mutex_lock(response_mutex);
    pthread_cond_wait(response_condition, response_mutex);
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