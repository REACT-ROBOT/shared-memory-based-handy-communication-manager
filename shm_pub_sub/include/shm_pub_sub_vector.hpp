//!
//! @file shm_ps_vector.hpp
//! @brief メモリの格納方法を規定するクラスの定義
//! @note 記法はROSに準拠する
//!       http://wiki.ros.org/ja/CppStyleGuide
//!

#ifndef __SHM_PS_VECTOR_LIB_H__
#define __SHM_PS_VECTOR_LIB_H__

#include <iostream>
#include <limits>
#include <string>
#include <regex>
#include <stdexcept>
#include <mutex>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
#include <cstring>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"

namespace irlab
{

  namespace shm
  {

    // ****************************************************************************
    //! @brief 共有メモリにトピックを出力する出版者を表現するクラス
    //! @details template classとして与えられた型またはクラスをトピックとして出力するためのクラスである．
    //! sizeofによってメモリの使用量が把握できる型およびクラスに対応している．
    //! また、特殊なものはtemplate classを特殊化して対応する．
    //!
    //! @note 通常であれば、生成された共有メモリはデストラクタで破棄されるべきだと考えるのが自然であるが、
    //! 意図せずプログラムが再起動したような場合に共有メモリが破棄されてしまうと、値の更新が読み取れなかったり
    //! 以前に送っていた指令が読み取れなくなったりするなどの問題が生じる可能性があるため、あえて破棄していない．
    //! 一度確保した共有メモリにサイズの異なるデータを格納しようとするとデータが破損するため、
    //! システムを再度立ち上げ直す際には共有メモリを破棄する操作を行うことを推奨する．
    // ****************************************************************************
    template <class T>
    class Publisher<std::vector<T>>
    {
    public:
      Publisher(std::string name = "", int buffer_num = 3, PERM perm = DEFAULT_PERM);
      ~Publisher() = default;

      void publish(const std::vector<T> &data);
      void _publish(const std::vector<T> data);

    private:
      std::string shm_name;
      int shm_buf_num;
      PERM shm_perm;
      std::unique_ptr<SharedMemory> shared_memory;
      std::unique_ptr<RingBuffer> ring_buffer;

      size_t vector_size;
    };

    // ****************************************************************************
    //! @brief 共有メモリからトピックを取得する購読者を表現するクラス
    //! @details template classとして与えられた型またはクラスをトピックとして読み込むためのクラスである．
    //! また、トピックが更新されるまで待機するAPIを持つ．
    // ****************************************************************************
    template <typename T>
    class Subscriber<std::vector<T>>
    {
    public:
      Subscriber(std::string name = "");
      ~Subscriber() = default;

      const std::vector<T>& subscribe(bool *is_success);
      bool waitFor(uint64_t timeout_usec);
      void setDataExpiryTime_us(uint64_t time_us);

    private:
      std::string shm_name;
      std::unique_ptr<SharedMemory> shared_memory;
      std::unique_ptr<RingBuffer> ring_buffer;
      int current_reading_buffer;
      uint64_t data_expiry_time_us;

      size_t vector_size;
      std::vector<T> return_buffer_;
    };

    // ****************************************************************************
    // 関数定義
    // （テンプレートクラス内の関数の定義はコンパイル時に実体化するのでヘッダに書く）
    // ****************************************************************************

    //! @brief コンストラクタ
    //! @param [in] name 共有メモリ名
    //! @param [in] buffer_num バッファ数
    //! @param [in] perm 権限情報
    //! @return なし
    //! @details 共有メモリオブジェクトの生成、mutexや条件変数の初期化を行う．
    template <typename T>
    Publisher<std::vector<T>>::Publisher(std::string name, int buffer_num, PERM perm)
        : shm_name(name), shm_buf_num(buffer_num), shm_perm(perm), shared_memory(nullptr), ring_buffer(nullptr), vector_size(0)
    {
      if (!std::is_standard_layout<T>::value)
      {
        throw std::runtime_error("shm::Publisher: Be setted not POD class in vector!");
      }

      shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR | O_CREAT, shm_perm);
      shared_memory->connect(RingBuffer::getSize(vector_size, shm_buf_num));
      if (shared_memory->isDisconnected())
      {
        throw std::runtime_error("shm::Publisher: Cannot get memory!");
      }

      ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr(), vector_size, shm_buf_num);
    }

    //! @brief トピックの書き込み
    //! @param [in] data
    //! @return なし
    //! @details タイムスタンプが最も古いバッファにトピックを書き込み、タイムスタンプを更新する．
    //! また、pthreadの条件変数を介して、待機中のプロセスに再開信号を送る．
    //! @note python対応のために、boostを使用しているので、boostの条件変数でも良いかもしれない。
    template <typename T>
    void
    Publisher<std::vector<T>>::publish(const std::vector<T> &data)
    {
      if (data.size() != vector_size)
      {
        vector_size = data.size();
        ring_buffer.reset();
        shared_memory->disconnectAndUnlink();
        shared_memory->connect(RingBuffer::getSize(sizeof(T) * vector_size, shm_buf_num));

        if (shared_memory->isDisconnected())
        {
          throw std::runtime_error("shm::Publisher: Cannot allocate shared memory!");
        }

        ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr(), sizeof(T) * vector_size, shm_buf_num);
      }

      int oldest_buffer = ring_buffer->getOldestBufferNum();
      for (size_t i = 0; i < 10; i++)
      {
        if (ring_buffer->allocateBuffer(oldest_buffer))
        {
          break;
        }
        usleep(1000); // Wait for 1ms
        oldest_buffer = ring_buffer->getOldestBufferNum();
      }

      // Cross-platform aligned memory access for vectors
      unsigned char* data_ptr = ring_buffer->getDataList();
      size_t buffer_offset = oldest_buffer * vector_size * sizeof(T);
      
      if constexpr (is_arm_platform()) {
        // ARM: Always use memcpy for vector data safety
        std::memcpy(data_ptr + buffer_offset, data.data(), sizeof(T) * vector_size);
      } else {
        // x86/x64: Direct pointer access is safe
        T *first_ptr = reinterpret_cast<T *>(data_ptr + buffer_offset);
        std::memcpy(first_ptr, data.data(), sizeof(T) * vector_size);
      }

      uint64_t current_time_us =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
      ring_buffer->setTimestamp_us(current_time_us, oldest_buffer);

      ring_buffer->signal();
    }

    //! @brief トピックの書き込み（値渡し）
    //! @param [in] data
    //! @return なし
    //! @details boost_python対応時に、参照渡しだと何故かエラーになったために追加した関数．
    template <typename T>
    void
    Publisher<std::vector<T>>::_publish(const std::vector<T> data)
    {
      publish(data);
    }

    //! @brief コンストラクタ
    //! @param [in] 共有メモリ名
    //! @return なし
    //! @details 共有メモリへのアクセスを行う．
    template <typename T>
    Subscriber<std::vector<T>>::Subscriber(std::string name)
        : shm_name(name), shared_memory(nullptr), ring_buffer(nullptr), current_reading_buffer(0), data_expiry_time_us(2000000), return_buffer_(0)
    {
      if (!std::is_standard_layout<T>::value)
      {
        throw std::runtime_error("shm::Subscriber: Be setted not POD class!");
      }
      shared_memory = std::make_unique<SharedMemoryPosix>(shm_name, O_RDWR, static_cast<PERM>(0));
    }

    //! @brief トピックを読み込む
    //! @param なし
    //! @return const T& 読み込んだトピックへのconst参照
    //! @details タイムスタンプが最も新しいトピックを読み込む．
    //! 後々可変長なクラスに拡張できるように、メモリへの直接的な参照を返すので、コピーコンストラクタや代入によってデータを複製することを推奨する．
    template <typename T>
    const std::vector<T>&
    Subscriber<std::vector<T>>::subscribe(bool *is_success)
    {
      if (shared_memory == nullptr || shared_memory->isDisconnected())
      {
        if (ring_buffer != nullptr)
        {
          ring_buffer.reset();
        }

        // Clean up old connection before reconnecting
        shared_memory->disconnect();
        shared_memory->connect();
        if (shared_memory->isDisconnected())
        {
          *is_success = false;
          return return_buffer_;
        }

        try
        {
          if (shared_memory->getPtr() == nullptr)
          {
            *is_success = false;
            return return_buffer_;
          }

          std::cerr << "[Subscriber::subscribe] Waiting for initialization..." << std::endl;
          // Wait for initialization to complete
          if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000)) { // 500ms timeout (increased)
            *is_success = false;
            return return_buffer_;
          }

          ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr());
          size_t element_size = ring_buffer->getElementSize();
          vector_size = element_size / sizeof(T);
          return_buffer_.resize(vector_size);
        }
        catch (const std::bad_alloc &e)
        {
          *is_success = false;
          return return_buffer_;
        }
        ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
      }
      int newest_buffer = ring_buffer->getNewestBufferNum();
      if (newest_buffer < 0)
      {
        *is_success = false;
        return return_buffer_;
      }

      *is_success = true;
      current_reading_buffer = newest_buffer;
      
      // Cross-platform aligned memory access for successful case
      unsigned char* data_ptr = ring_buffer->getDataList();
      size_t buffer_offset = newest_buffer * vector_size * sizeof(T);
      
      if constexpr (is_arm_platform()) {
        // ARM: Use safer memory copy approach for vectors
        std::memcpy(return_buffer_.data(), data_ptr + buffer_offset, sizeof(T) * vector_size);
        return return_buffer_;
      } else {
        // x86/x64: Direct pointer construction is safe
        T *first_ptr = reinterpret_cast<T *>(data_ptr + buffer_offset);
        std::memcpy(return_buffer_.data(), first_ptr, sizeof(T) * vector_size);
        return return_buffer_;
      }
    }

    template <typename T>
    bool
    Subscriber<std::vector<T>>::waitFor(uint64_t timeout_usec)
    {
      if (shared_memory->isDisconnected())
      {
        if (ring_buffer != nullptr)
        {
          ring_buffer.reset();
        }
        // Clean up old connection before reconnecting
        shared_memory->disconnect();
        shared_memory->connect();
        if (shared_memory->isDisconnected())
        {
          return false;
        }
        // Wait for initialization to complete
        if (!RingBuffer::waitForInitialization(shared_memory->getPtr(), 500000)) { // 500ms timeout (increased)
          return false;
        }
        ring_buffer = std::make_unique<RingBuffer>(shared_memory->getPtr());
        size_t element_size = ring_buffer->getElementSize();
        vector_size = element_size / sizeof(T);
        return_buffer_.resize(vector_size);
        ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
      }

      return ring_buffer->waitFor(timeout_usec);
    }

    template <typename T>
    void
    Subscriber<std::vector<T>>::setDataExpiryTime_us(uint64_t time_us)
    {
      data_expiry_time_us = time_us;
      if (ring_buffer != nullptr)
      {
        ring_buffer->setDataExpiryTime_us(data_expiry_time_us);
      }
    }

  }

}

#endif /* __SHM_PS_VECTOR_LIB_H__ */
