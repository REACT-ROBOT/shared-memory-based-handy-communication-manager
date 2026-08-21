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

//! @brief 直前の値より必ず大きいタイムスタンプを返す
//! @param [in] previous 共有メモリ上の現在のタイムスタンプ
//! @return uint64_t 新しいタイムスタンプ
//! @details getCurrentTimeUSec() は usec 分解能なので、同一 usec 内に 2 回
//!          更新すると値が変わらず、待ち手の「前回より新しいか」という述語が
//!          成立したままになって更新を取りこぼす。単調増加を保証する。
inline uint64_t
nextTimestamp(uint64_t previous)
{
  const uint64_t now = getCurrentTimeUSec();
  return (now > previous) ? now : (previous + 1);
}

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
  // pthread_cond_timedwait は絶対時刻を取るため、待ち時刻の計算に使う時計を
  // condvar 側にも明示しておく。既定の CLOCK_REALTIME は NTP や手動設定で
  // 飛ぶことがあり、待ち時間がその分だけ狂う。
  pthread_condattr_t request_cond_attr;
  pthread_condattr_init(&request_cond_attr);
  pthread_condattr_setpshared(&request_cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_condattr_setclock(&request_cond_attr, CLOCK_MONOTONIC);
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
  pthread_condattr_setclock(&response_cond_attr, CLOCK_MONOTONIC);
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
    
    // 応答本体と述語（タイムスタンプ）の更新は必ず mutex を保持して行う。
    // 待ち手も mutex を保持して述語を評価するため、こうしておけば
    // broadcast 自体は解放後に出しても取りこぼされない
    // （待ち手は待ちに入る前に必ず更新後の述語を見る）。
    // 保持したまま broadcast すると、起こされた待ち手がすぐ mutex を取れず
    // 余計なコンテキストスイッチが入るため、解放してから通知する。
    pthread_mutex_lock(response_mutex);
    *response_ptr = *result_ptr;
    // タイムスタンプは usec 分解能なので、応答が同一 usec 内に収まると
    // 待ち手の述語 (current >= *response_timestamp_usec) が成立したままになり
    // 更新を検出できない。必ず前回より大きい値にする。
    *response_timestamp_usec = nextTimestamp(*response_timestamp_usec);
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

  // リクエストの書き込みとタイムスタンプ更新は request_mutex を保持して行う。
  // 以前は保持せずに更新して broadcast していたため、サーバが mutex を保持して
  // 述語を評価してから pthread_cond_wait に入るまでの隙間に割り込むと通知が
  // 捨てられ、サーバは次のリクエストが来るまで眠り続けていた（要求と応答が
  // 交互に進む使い方では次のリクエストが発行されないので、クライアントが
  // タイムアウトするまで戻らない）。
  pthread_mutex_lock(request_mutex);
  *request_ptr = request;
  // タイムスタンプは usec 分解能なので、複数のクライアントが同一 usec 内に
  // リクエストを出すとサーバの述語が成立したままになり取りこぼす。
  // 必ず前回より大きい値にする。
  *request_timestamp_usec = nextTimestamp(*request_timestamp_usec);
  pthread_mutex_unlock(request_mutex);

  // 述語の更新を mutex 内で済ませてあるので、broadcast は解放後で取りこぼさない。
  pthread_cond_broadcast(request_condition);

  // 応答待ち。述語の評価も response_mutex を保持したまま行う。
  const uint64_t end_time = getCurrentTimeUSec() + timeout_usec;

  pthread_mutex_lock(response_mutex);
  while (current_response_timestamp_usec >= *response_timestamp_usec)
  {
    const uint64_t now = getCurrentTimeUSec();
    if (now >= end_time)
    {
      pthread_mutex_unlock(response_mutex);
      return false;  // Timeout
    }

    // pthread_cond_timedwait は「絶対時刻」を取る。以前はここに相対時間
    // (0 秒 + 10ms) を渡していたため、エポックから 10ms という過去の時刻となり
    // 常に即座に ETIMEDOUT で戻る＝ビジーループになっていた。
    // condvar の時計は CLOCK_MONOTONIC に設定してあるので、それに合わせる。
    struct timespec wait_time;
    clock_gettime(CLOCK_MONOTONIC, &wait_time);
    uint64_t remaining_usec = end_time - now;
    if (remaining_usec > 10000)
    {
      remaining_usec = 10000;  // 最大 10ms ごとに起きてタイムアウトを再評価する
    }
    wait_time.tv_nsec += static_cast<long>(remaining_usec * 1000);
    wait_time.tv_sec += wait_time.tv_nsec / 1000000000L;
    wait_time.tv_nsec %= 1000000000L;

    pthread_cond_timedwait(response_condition, response_mutex, &wait_time);
  }
  current_response_timestamp_usec = *response_timestamp_usec;

  // Get response from shared memory
  *response = *response_ptr;
  pthread_mutex_unlock(response_mutex);

  return true;
}

}

}

#endif //__SHM_SERVICE_LIB_H__