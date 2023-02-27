//!
//! @file shm_action.hpp
//! @brief メモリの格納方法を規定するクラスの定義
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
//! 
//! @example test1.hpp
//! 共有メモリに関するテスト
//! @example test1.cpp
//! 共有メモリに関するテスト
//!

#ifndef __SHM_ACTION_LIB_H__
#define __SHM_ACTION_LIB_H__

#include <string>
#include <thread>
#include "shm_base.hpp"

namespace irlab
{

namespace shm
{

enum ACTION_STATUS : uint8_t
{
  ACTIVE,
  REJECTED,
  SUCCEEDED,
  PREEMPTED,
};

// ****************************************************************************
//! @class ActionServer
//! @brief 共有メモリで受信したリクエストからレスポンスを返すサーバーを表現するクラス
//! @details template classとして与えられた型またはクラスをリクエストおよびレスポンスとしてリクエストからレスポンスを出力するクラスである．
//! sizeofによってメモリの使用量が把握できる型およびクラスに対応している．
//! また、特殊なものはtemplate classを特殊化して対応する．
//!  
//! @note 通常であれば、生成された共有メモリはデストラクタで破棄されるべきだと考えるのが自然であるが、
//! 意図せずプログラムが再起動したような場合に共有メモリが破棄されてしまうと、値の更新が読み取れなかったり
//! 以前に送っていた指令が読み取れなくなったりするなどの問題が生じる可能性があるため、あえて破棄していない．
//! 一度確保した共有メモリにサイズの異なるデータを格納しようとするとデータが破損するため、
//! システムを再度立ち上げ直す際には共有メモリを破棄する操作を行うことを推奨する．
// ****************************************************************************
template <class Goal, class Result, class Feedback>
class ActionServer
{
public:
  ActionServer(std::string name, PERM perm = DEFAULT_PERM);
  ~ActionServer();

  void waitNewGoalAvailable();
  Goal acceptNewGoal();
  void rejectNewGoal();

  bool isPreemptRequested();
  void setPreempted();

  void publishResult(const Result& result);
  void publishFeedback(const Feedback& feedback);

private:
  void initializeExclusiveAccess();

  pthread_t thread;

  std::string shm_name;
  PERM shm_perm;
  SharedMemory *shared_memory;

  uint64_t start_timestamp_us;

  uint8_t *memory_ptr;

  pthread_mutex_t *goal_mutex;
  pthread_cond_t *goal_condition;
  uint64_t *goal_timestamp_us;
  Goal *goal_ptr;
  pthread_mutex_t *result_mutex;
  pthread_cond_t *result_condition;
  uint64_t *result_timestamp_us;
  Result *result_ptr;
  Feedback *feedback_ptr;
  ACTION_STATUS *status_ptr;
  uint64_t *cancel_timestamp_us;

  uint64_t current_goal_timestamp_usec;
};

// ****************************************************************************
//! @class ActionClient
//! @brief 共有メモリからトピックを取得する購読者を表現するクラス
//! @details template classとして与えられた型またはクラスをトピックとして読み込むためのクラスである．
//! また、トピックが更新されるまで待機するAPIを持つ．
// ****************************************************************************
template <class Goal, class Result, class Feedback>
class ActionClient
{
public:
  ActionClient(std::string name);
  ~ActionClient();

  void cancelGoal();
  Result getResult();
  Feedback getFeedback();
  ACTION_STATUS getStatus();
  bool isServerConnected();
  bool sendGoal(Goal goal);
  bool waitForResult(unsigned long wait_time_us);
  bool waitForServer(unsigned long wait_time_us);

private:
  std::string shm_name;
  SharedMemory *shared_memory;

  uint8_t *memory_ptr;

  pthread_mutex_t *goal_mutex;
  pthread_cond_t *goal_condition;
  uint64_t *goal_timestamp_us;
  Goal *goal_ptr;
  pthread_mutex_t *result_mutex;
  pthread_cond_t *result_condition;
  uint64_t *result_timestamp_us;
  Result *result_ptr;
  Feedback *feedback_ptr;
  ACTION_STATUS *status_ptr;
  uint64_t *cancel_timestamp_us;

  uint64_t current_result_timestamp_usec;
};

// ****************************************************************************
// 関数定義
// （テンプレートクラス内の関数の定義はコンパイル時に実体化するのでヘッダに書く）
// ****************************************************************************
template <class Goal, class Result, class Feedback>
ActionServer<Goal, Result, Feedback>::ActionServer(std::string name, PERM perm)
: shm_name(name)
, shm_perm(perm)
, shared_memory(nullptr)
, memory_ptr(nullptr)
{
  if (!std::is_standard_layout<Goal>::value 
    || !std::is_standard_layout<Result>::value 
    || !std::is_standard_layout<Feedback>::value)
  {
    throw std::runtime_error("shm::ActionServer: Be setted not POD class!");
  }
  
  shared_memory = new SharedMemoryPosix(shm_name, O_RDWR|O_CREAT, shm_perm);
  shared_memory->connect( (sizeof(pthread_mutex_t)+sizeof(pthread_cond_t)+sizeof(uint64_t)) * 2 + sizeof(Goal) + sizeof(Result) + sizeof(Feedback) + sizeof(ACTION_STATUS) + sizeof(uint64_t));
  if (shared_memory->isDisconnected())
  {
    throw std::runtime_error("shm::ActionServer: Cannot get memory!");
  }

  uint8_t *data_ptr = shared_memory->getPtr();
  memory_ptr = data_ptr;
  goal_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
  data_ptr += sizeof(pthread_mutex_t);
  goal_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
  data_ptr += sizeof(pthread_cond_t);
  goal_timestamp_us = reinterpret_cast<uint64_t *>(data_ptr);
  data_ptr += sizeof(uint64_t);
  goal_ptr = reinterpret_cast<Goal *>(data_ptr);
  data_ptr += sizeof(Goal);
  result_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
  data_ptr += sizeof(pthread_mutex_t);
  result_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
  data_ptr += sizeof(pthread_cond_t);
  result_timestamp_us = reinterpret_cast<uint64_t *>(data_ptr);
  data_ptr += sizeof(uint64_t);
  result_ptr = reinterpret_cast<Result *>(data_ptr);
  data_ptr += sizeof(Result);
  feedback_ptr = reinterpret_cast<Feedback *>(data_ptr);
  data_ptr += sizeof(Feedback);
  status_ptr = reinterpret_cast<ACTION_STATUS *>(data_ptr);
  data_ptr += sizeof(ACTION_STATUS);
  cancel_timestamp_us = reinterpret_cast<uint64_t *>(data_ptr);

  initializeExclusiveAccess();

  *status_ptr = SUCCEEDED;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  *cancel_timestamp_us = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);
  start_timestamp_us = *cancel_timestamp_us;
  *goal_timestamp_us = *cancel_timestamp_us;
  *result_timestamp_us = *cancel_timestamp_us;

  current_goal_timestamp_usec = *goal_timestamp_us;
}

template <class Goal, class Result, class Feedback>
ActionServer<Goal, Result, Feedback>::~ActionServer()
{
  shared_memory->disconnect();
  if (shared_memory != nullptr)
  {
    delete shared_memory;
  }
}

template <class Goal, class Result, class Feedback>
void
ActionServer<Goal, Result, Feedback>::initializeExclusiveAccess()
{
  pthread_condattr_t goal_cond_attr;
  pthread_condattr_init(&goal_cond_attr);
  pthread_condattr_setpshared(&goal_cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(goal_condition, &goal_cond_attr);
  pthread_condattr_destroy(&goal_cond_attr);

  pthread_mutexattr_t goal_m_attr;
  pthread_mutexattr_init(&goal_m_attr);
  pthread_mutexattr_setpshared(&goal_m_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(goal_mutex, &goal_m_attr);
  pthread_mutexattr_destroy(&goal_m_attr);

  pthread_condattr_t result_cond_attr;
  pthread_condattr_init(&result_cond_attr);
  pthread_condattr_setpshared(&result_cond_attr, PTHREAD_PROCESS_SHARED);
  pthread_cond_init(result_condition, &result_cond_attr);
  pthread_condattr_destroy(&result_cond_attr);

  pthread_mutexattr_t result_m_attr;
  pthread_mutexattr_init(&result_m_attr);
  pthread_mutexattr_setpshared(&result_m_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(result_mutex, &result_m_attr);
  pthread_mutexattr_destroy(&result_m_attr);
}

template <class Goal, class Result, class Feedback>
void
ActionServer<Goal, Result, Feedback>::waitNewGoalAvailable()
{
  while (current_goal_timestamp_usec >= *goal_timestamp_us)
  {
    // Wait on the condvar
    pthread_mutex_lock(goal_mutex);
    pthread_cond_wait(goal_condition, goal_mutex);
    pthread_mutex_unlock(goal_mutex);
  }
}

template <class Goal, class Result, class Feedback>
Goal
ActionServer<Goal, Result, Feedback>::acceptNewGoal()
{
  *status_ptr = ACTIVE;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  start_timestamp_us = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);
  current_goal_timestamp_usec = *goal_timestamp_us;

  return *goal_ptr;
}

template <class Goal, class Result, class Feedback>
void
ActionServer<Goal, Result, Feedback>::rejectNewGoal()
{
  *status_ptr = REJECTED;
  current_goal_timestamp_usec = *goal_timestamp_us;
  pthread_cond_broadcast(result_condition);
}

template <class Goal, class Result, class Feedback>
bool
ActionServer<Goal, Result, Feedback>::isPreemptRequested()
{
  if (start_timestamp_us < *cancel_timestamp_us)
  {
    return true;
  }
  return false;
}

template <class Goal, class Result, class Feedback>
void
ActionServer<Goal, Result, Feedback>::setPreempted()
{
  *status_ptr = PREEMPTED;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  *result_timestamp_us = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);

  pthread_cond_broadcast(result_condition);
}

template <class Goal, class Result, class Feedback>
void
ActionServer<Goal, Result, Feedback>::publishResult(const Result& result)
{
  *result_ptr = result;
  *status_ptr = SUCCEEDED;

  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  *result_timestamp_us = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);

  pthread_cond_broadcast(result_condition);
}

template <class Goal, class Result, class Feedback>
void
ActionServer<Goal, Result, Feedback>::publishFeedback(const Feedback& feedback)
{
  *feedback_ptr = feedback;
}


template <class Goal, class Result, class Feedback>
ActionClient<Goal, Result, Feedback>::ActionClient(std::string name)
: shm_name(name)
, shared_memory(nullptr)
{
  if (!std::is_standard_layout<Goal>::value 
    || !std::is_standard_layout<Result>::value 
    || !std::is_standard_layout<Feedback>::value)
  {
    throw std::runtime_error("shm::ActionClient: Be setted not POD class!");
  }
  shared_memory = new SharedMemoryPosix(shm_name, O_RDWR, static_cast<PERM>(0));
}

template <class Goal, class Result, class Feedback>
ActionClient<Goal, Result, Feedback>::~ActionClient()
{
  if (shared_memory != nullptr)
  {
    delete shared_memory;
  }
}

template <class Goal, class Result, class Feedback>
bool
ActionClient<Goal, Result, Feedback>::isServerConnected()
{
    // Check the action shared memory existance.
  if (shared_memory->isDisconnected())
  {
    shared_memory->connect();
    if (shared_memory->isDisconnected())
    {
      return false;
    }
    uint8_t *data_ptr = shared_memory->getPtr();
    memory_ptr = data_ptr;
    goal_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
    data_ptr += sizeof(pthread_mutex_t);
    goal_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
    data_ptr += sizeof(pthread_cond_t);
    goal_timestamp_us = reinterpret_cast<uint64_t *>(data_ptr);
    data_ptr += sizeof(uint64_t);
    goal_ptr = reinterpret_cast<Goal *>(data_ptr);
    data_ptr += sizeof(Goal);
    result_mutex = reinterpret_cast<pthread_mutex_t *>(data_ptr);
    data_ptr += sizeof(pthread_mutex_t);
    result_condition = reinterpret_cast<pthread_cond_t *>(data_ptr);
    data_ptr += sizeof(pthread_cond_t);
    result_timestamp_us = reinterpret_cast<uint64_t *>(data_ptr);
    data_ptr += sizeof(uint64_t);
    result_ptr = reinterpret_cast<Result *>(data_ptr);
    data_ptr += sizeof(Result);
    feedback_ptr = reinterpret_cast<Feedback *>(data_ptr);
    data_ptr += sizeof(Feedback);
    status_ptr = reinterpret_cast<ACTION_STATUS *>(data_ptr);
    data_ptr += sizeof(ACTION_STATUS);
    cancel_timestamp_us = reinterpret_cast<uint64_t *>(data_ptr);
  }

  return true;
}

template <class Goal, class Result, class Feedback>
bool
ActionClient<Goal, Result, Feedback>::sendGoal(Goal goal)
{
    // Check the action shared memory existance.
  if (!(isServerConnected()))
  {
    return false;
  }

  current_result_timestamp_usec = *result_timestamp_us;

  // Set request to shared memory
  *goal_ptr = goal;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  *goal_timestamp_us = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);
  pthread_cond_broadcast(goal_condition);

  return true;
}

template <class Goal, class Result, class Feedback>
Result
ActionClient<Goal, Result, Feedback>::getResult()
{
  return *result_ptr;
}

template <class Goal, class Result, class Feedback>
Feedback
ActionClient<Goal, Result, Feedback>::getFeedback()
{
  return *feedback_ptr;
}

template <class Goal, class Result, class Feedback>
ACTION_STATUS
ActionClient<Goal, Result, Feedback>::getStatus()
{
  return *status_ptr;
}

template <class Goal, class Result, class Feedback>
void
ActionClient<Goal, Result, Feedback>::cancelGoal()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  *cancel_timestamp_us = ((uint64_t) ts.tv_sec * 1000000L) + ((uint64_t) ts.tv_nsec / 1000L);
}

template <class Goal, class Result, class Feedback>
bool
ActionClient<Goal, Result, Feedback>::waitForResult(unsigned long wait_time_us)
{
  struct timespec ts;
  long sec  = static_cast<long>(wait_time_us / 1000000);
  long mod_sec = static_cast<long>(wait_time_us % 1000000);
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += sec;
  ts.tv_nsec+= mod_sec * 1000;
  if (1000000000 <= ts.tv_nsec)
  {
    ts.tv_nsec -= 1000000000;
    ts.tv_sec  += 1;
  }
  
  while (current_result_timestamp_usec >= *result_timestamp_us)
  {
    // Wait on the condvar
    pthread_mutex_lock(result_mutex);
    int ret = pthread_cond_timedwait(result_condition, result_mutex, &ts);
    pthread_mutex_unlock(result_mutex);
    if (ret == ETIMEDOUT)
    {
      return false;
    }
  }
  return true;
}

template <class Goal, class Result, class Feedback>
bool
ActionClient<Goal, Result, Feedback>::waitForServer(uint64_t wait_time_us)
{
  static const uint64_t SLEEP_PERIOD_us = 100000;

  if (isServerConnected())
  {
    return true;
  }

  for (uint64_t time_index = 0; time_index < wait_time_us / SLEEP_PERIOD_us; time_index++)
  {
    if (isServerConnected())
    {
      return true;
    }
    usleep(SLEEP_PERIOD_us);
  }

  return false;
}

}

}

#endif //__SHM_ACTION_LIB_H__