#include <shm_base.hpp>
#include <condition_variable>
#include <limits>
#include <chrono>
#include <thread>

namespace irlab
{

namespace shm
{

uint64_t
getCurrentTimeUSec()
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC_RAW, &t);
  return ((uint64_t)t.tv_sec * 1000000L) + ((uint64_t)t.tv_nsec / 1000L);
} 

size_t
RingBuffer::getSize(size_t element_size, int buffer_num)
{
  // 負値や過大な値をそのまま計算に流すと size_t への変換で巨大なオフセットになり、
  // 呼び出し側が範囲外ポインタを作る。API 境界で弾く（R01-F06）。
  if (buffer_num <= 0 || static_cast<size_t>(buffer_num) > MAX_BUFFER_NUM)
  {
    throw std::invalid_argument("shm::RingBuffer::getSize(): buffer_num must be in [1, " +
                                std::to_string(MAX_BUFFER_NUM) + "], but got " + std::to_string(buffer_num));
  }
  if (element_size > MAX_ELEMENT_SIZE)
  {
    throw std::invalid_argument("shm::RingBuffer::getSize(): element_size " + std::to_string(element_size) +
                                " exceeds the limit " + std::to_string(MAX_ELEMENT_SIZE));
  }

  // Use aligned layout calculation for accurate size
  size_t mutex_offset, cond_offset, element_size_offset, buf_num_offset, timestamp_offset, data_offset;
  const size_t total = calculateAlignedLayout(element_size, buffer_num, mutex_offset, cond_offset,
                                              element_size_offset, buf_num_offset, timestamp_offset, data_offset);
  if (total > MAX_TOTAL_SIZE)
  {
    throw std::invalid_argument("shm::RingBuffer::getSize(): total size " + std::to_string(total) +
                                " exceeds the limit " + std::to_string(MAX_TOTAL_SIZE));
  }
  return total;
}

//! @brief 既存の共有メモリのレイアウトが実マッピング長に収まっているか検証する
//! @details 共有メモリ上の element_size / buf_num は「そう書いてあるだけ」で、
//!          切り詰められた領域や別形式の領域を読むと任意の値が出てくる。
//!          その値からポインタを組み立てる前に、加減乗算を溢れ検査付きで行い、
//!          全域が mapping_size に収まることを確認する（R01-F06）。
bool
RingBuffer::validateLayout(const unsigned char *first_ptr, size_t mapping_size, std::string *reason)
{
  auto fail = [reason](const std::string &msg) {
    if (reason != nullptr)
    {
      *reason = msg;
    }
    return false;
  };

  if (first_ptr == nullptr)
  {
    return fail("first_ptr is null");
  }
  if (reinterpret_cast<uintptr_t>(first_ptr) % 8 != 0)
  {
    return fail("first_ptr is not 8-byte aligned");
  }

  // element_size / buf_num を読むために、まずその位置までマッピングされているか確認する。
  // ダミー値で求めた配置でも両者のオフセットは buffer_num に依存しない。
  size_t d_mutex, d_cond, d_elem_off, d_bufnum_off, d_ts, d_data;
  calculateAlignedLayout(0, 1, d_mutex, d_cond, d_elem_off, d_bufnum_off, d_ts, d_data);

  const size_t header_end = std::max(d_elem_off, d_bufnum_off) + sizeof(size_t);
  if (mapping_size < header_end)
  {
    return fail("mapping is smaller than the header (" + std::to_string(mapping_size) + " < " +
                std::to_string(header_end) + ")");
  }

  const size_t element_size = *reinterpret_cast<const size_t *>(first_ptr + d_elem_off);
  const size_t buf_num      = *reinterpret_cast<const size_t *>(first_ptr + d_bufnum_off);

  // element_size == 0 は異常ではない。空の vector を publish したトピックや、
  // まだ一度も publish されていない vector トピックが正当にこの状態になる。
  // 危険なのは過大な値のほうなので、上限だけを見る。
  if (element_size > MAX_ELEMENT_SIZE)
  {
    return fail("element_size " + std::to_string(element_size) + " is out of range");
  }
  if (buf_num == 0 || buf_num > MAX_BUFFER_NUM)
  {
    return fail("buf_num " + std::to_string(buf_num) + " is out of range");
  }

  // element_size * buf_num を溢れ検査付きで行う。上の上限で実際には溢れないが、
  // 上限を緩めたときに静かに壊れないよう明示的に検査する。
  size_t payload_bytes = 0;
  if (__builtin_mul_overflow(element_size, buf_num, &payload_bytes))
  {
    return fail("element_size * buf_num overflows");
  }

  size_t mutex_offset, cond_offset, element_size_offset, buf_num_offset, timestamp_offset, data_offset;
  const size_t required =
      calculateAlignedLayout(element_size, static_cast<int>(buf_num), mutex_offset, cond_offset, element_size_offset,
                             buf_num_offset, timestamp_offset, data_offset);

  size_t payload_end = 0;
  if (__builtin_add_overflow(data_offset, payload_bytes, &payload_end))
  {
    return fail("data_offset + payload overflows");
  }
  if (required < payload_end || required > MAX_TOTAL_SIZE)
  {
    return fail("computed layout size " + std::to_string(required) + " is inconsistent");
  }
  if (mapping_size < required)
  {
    return fail("mapping is smaller than the layout (" + std::to_string(mapping_size) + " < " +
                std::to_string(required) + "); the shared memory is truncated or of another format");
  }

  return true;
}

bool
RingBuffer::checkInitialized(unsigned char *first_ptr)
{
  if (first_ptr == nullptr)
  {
    return false;
  }

  std::atomic<uint32_t> *initialization_flag = reinterpret_cast<std::atomic<uint32_t> *>(first_ptr);
  return (initialization_flag->load(std::memory_order_relaxed) == RingBuffer::INITIALIZED);
}

bool
RingBuffer::waitForInitialization(unsigned char *first_ptr, uint64_t timeout_usec)
{
  auto start_time       = getCurrentTimeUSec();
  auto timeout_duration = timeout_usec;

  while (!RingBuffer::checkInitialized(first_ptr))
  {
    auto current_time = getCurrentTimeUSec();
    if (current_time - start_time >= timeout_duration)
    {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));  // Reduced wait interval for faster response
  }

  return true;
}

size_t
RingBuffer::calculateAlignedLayout(size_t element_size, int buffer_num, size_t &mutex_offset, size_t &cond_offset,
                                   size_t &element_size_offset, size_t &buf_num_offset, size_t &timestamp_offset,
                                   size_t &data_offset)
{
  size_t current_offset = 0;

  // 1. initialization_flag (std::atomic<uint32_t>) - starts at beginning
  current_offset = 0;

  // 2. pthread_init_flag (std::atomic<uint32_t>) - aligned to 8 bytes for ARM
  current_offset += get_aligned_size<std::atomic<uint32_t>>(1);

  // 3. mutex (pthread_mutex_t) - aligned to 8 bytes for ARM
  mutex_offset   = (current_offset + get_alignment<pthread_mutex_t>() - 1) & ~(get_alignment<pthread_mutex_t>() - 1);
  current_offset = mutex_offset + sizeof(pthread_mutex_t);

  // 4. condition (pthread_cond_t) - aligned to 8 bytes for ARM
  cond_offset    = (current_offset + get_alignment<pthread_cond_t>() - 1) & ~(get_alignment<pthread_cond_t>() - 1);
  current_offset = cond_offset + sizeof(pthread_cond_t);

  // 5. element_size (size_t) - aligned to 8 bytes for ARM
  element_size_offset = (current_offset + get_alignment<size_t>() - 1) & ~(get_alignment<size_t>() - 1);
  current_offset      = element_size_offset + sizeof(size_t);

  // 6. buf_num (size_t) - aligned to 8 bytes for ARM
  buf_num_offset = (current_offset + get_alignment<size_t>() - 1) & ~(get_alignment<size_t>() - 1);
  current_offset = buf_num_offset + sizeof(size_t);

  // 7. timestamp_list (std::atomic<uint64_t> * buffer_num) - aligned to 8 bytes for ARM
  timestamp_offset =
      (current_offset + get_alignment<std::atomic<uint64_t>>() - 1) & ~(get_alignment<std::atomic<uint64_t>>() - 1);
  current_offset = timestamp_offset + sizeof(std::atomic<uint64_t>) * buffer_num;

  // 8. data_list (aligned for element type) - use maximum alignment for safety
  const size_t data_alignment =
      std::max(get_alignment<uint64_t>(), static_cast<size_t>(8));  // At least 8-byte aligned on ARM
  data_offset    = (current_offset + data_alignment - 1) & ~(data_alignment - 1);
  current_offset = data_offset + element_size * buffer_num;

  // 合計サイズは 8 バイト境界に切り上げる。getSize() を使って複数の
  // リングバッファを同一共有メモリ上に連結配置したとき、次のリングの
  // atomic 変数や pthread 構造体が非アライン配置になって ARM で SIGBUS
  // するのを防ぐため（返り値のパディングのみで、各オフセットは不変）。
  current_offset = (current_offset + data_alignment - 1) & ~(data_alignment - 1);

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
  , expected_element_size(0)
  , expected_buf_num(0)
{
  // 先頭ポインタは 8 バイト境界必須。非アラインだと atomic 変数や
  // pthread 構造体へのアクセスが ARM では SIGBUS になる（x86 では動いて
  // しまう）ため、プラットフォームによらずここで即時に検出する。
  if (reinterpret_cast<uintptr_t>(first_ptr) % 8 != 0)
  {
    throw std::runtime_error("RingBuffer: first_ptr must be 8-byte aligned!");
  }

  // Use aligned layout calculation for ARM compatibility
  size_t mutex_offset, cond_offset, element_size_offset, buf_num_offset, timestamp_offset, data_offset;

  // 生成経路（buffer_num 指定あり）は API 境界として入力を検証する。
  // 0 を渡すと下の attach 経路に落ちて未初期化のヘッダを読むため、0 も弾く。
  if (buffer_num != 0)
  {
    if (buffer_num < 0 || static_cast<size_t>(buffer_num) > MAX_BUFFER_NUM)
    {
      throw std::invalid_argument("shm::RingBuffer: buffer_num must be in [1, " + std::to_string(MAX_BUFFER_NUM) +
                                  "], but got " + std::to_string(buffer_num));
    }
    if (size > MAX_ELEMENT_SIZE)
    {
      throw std::invalid_argument("shm::RingBuffer: element_size " + std::to_string(size) + " exceeds the limit " +
                                  std::to_string(MAX_ELEMENT_SIZE));
    }
  }

  // size == 0（要素長 0 の vector Publisher など）でも buffer_num が指定されて
  // いれば生成経路として扱う。以前は attach 経路に落ちて共有メモリ上の
  // buf_num（新規なら 0）でレイアウトを計算するため、data_list が
  // timestamp_list と重なる位置を指していた。
  if (buffer_num != 0)
  {
    // Calculate aligned layout for new buffer creation
    calculateAlignedLayout(size, buffer_num, mutex_offset, cond_offset, element_size_offset, buf_num_offset,
                           timestamp_offset, data_offset);
    expected_element_size = size;
    expected_buf_num      = static_cast<size_t>(buffer_num);
  }
  else
  {
    // Reading existing buffer - need to extract parameters first
    // IMPORTANT: Must use aligned offsets, not sizeof() sum, to match the writer's layout
    size_t temp_mutex_offset, temp_cond_offset, temp_element_size_offset, temp_buf_num_offset, temp_timestamp_offset, temp_data_offset;

    // First pass: calculate offsets with dummy values to find element_size and buf_num locations
    calculateAlignedLayout(0, 1, temp_mutex_offset, temp_cond_offset, temp_element_size_offset, temp_buf_num_offset,
                           temp_timestamp_offset, temp_data_offset);

    // Now read element_size and buf_num using aligned offsets
    element_size = reinterpret_cast<size_t *>(memory_ptr + temp_element_size_offset);
    buf_num = reinterpret_cast<size_t *>(memory_ptr + temp_buf_num_offset);

    // 共有メモリ上の値は「そう書いてあるだけ」なので、ポインタを組み立てる前に
    // 現実的な範囲に収まっているか確認する。マッピング長との突き合わせは
    // 呼び出し側の validateLayout() が行う（ここでは長さを知り得ない）。
    // element_size == 0 は空 vector トピックで正当に発生する（上の validateLayout
    // のコメント参照）。過大な値だけを弾く。
    if (*element_size > MAX_ELEMENT_SIZE)
    {
      throw std::runtime_error("shm::RingBuffer: element_size " + std::to_string(*element_size) +
                               " in shared memory is out of range; the segment is corrupted or of another format");
    }
    if (*buf_num == 0 || *buf_num > MAX_BUFFER_NUM)
    {
      throw std::runtime_error("shm::RingBuffer: buf_num " + std::to_string(*buf_num) +
                               " in shared memory is out of range; the segment is corrupted or of another format");
    }

    // Second pass: calculate aligned layout based on actual parameters from shared memory
    calculateAlignedLayout(*element_size, static_cast<int>(*buf_num), mutex_offset, cond_offset, element_size_offset,
                           buf_num_offset, timestamp_offset, data_offset);
    expected_element_size = *element_size;
    expected_buf_num      = *buf_num;
  }

  // Initialize pointers using calculated aligned offsets
  initialization_flag = reinterpret_cast<std::atomic<uint32_t> *>(memory_ptr);
  pthread_init_flag =
      reinterpret_cast<std::atomic<uint32_t> *>(memory_ptr + get_aligned_size<std::atomic<uint32_t>>(1));
  mutex          = reinterpret_cast<pthread_mutex_t *>(memory_ptr + mutex_offset);
  condition      = reinterpret_cast<pthread_cond_t *>(memory_ptr + cond_offset);
  element_size   = reinterpret_cast<size_t *>(memory_ptr + element_size_offset);
  buf_num        = reinterpret_cast<size_t *>(memory_ptr + buf_num_offset);
  timestamp_list = reinterpret_cast<std::atomic<uint64_t> *>(memory_ptr + timestamp_offset);
  data_list      = memory_ptr + data_offset;

  // element_size / buf_num の書き込みは initializeOrAttach() 経由で行う。
  // 初期化済みの共有メモリに接続するだけの場合は書き換えてはならない。

  if (buffer_num != 0)
  {
    initializeOrAttach(size, buffer_num);
  }
  else
  {
    // For subscriber accessing existing memory, just set up pointers
    // Initialization check will be done via checkInitialized()
  }
}

//! @brief 初期化済みなら接続のみ、そうでなければ初期化を行う
//! @param [in] element_size 要求する要素サイズ
//! @param [in] buffer_num   要求するバッファ数
//! @return なし
//! @details 以前はこの経路が無条件に初期化を行っていたため、既に publish 済みの
//!          共有メモリに対して後から同名トピックの writer が生成されるだけで
//!          （publish しなくても、別プロセスからでも）全タイムスタンプが 0 に
//!          クリアされ、購読側が「データ無し」と判定するようになっていた。
//!          データ本体は残るため「値は生きているのにトピックだけ消える」症状に
//!          なり、期限を無効化して値を保持する使い方（パラメータ保持）も
//!          成立しなかった。
//!          ここでは要求レイアウトが既存の共有メモリと一致する限り再初期化を
//!          行わず、接続のみに留めることで既存の値とタイムスタンプを保存する。
//! @note    レイアウトが一致しない場合（型のサイズやバッファ数を変えた場合）は
//!          従来通り作り直すため、既存データは失われる。これはクラスの
//!          ドキュメントに既知の制約として記載されている挙動である。
void
RingBuffer::initializeOrAttach(size_t element_size_arg, int buffer_num)
{
  // 既に初期化済みでレイアウトも一致 → 何もせず接続のみ
  if (hasCompatibleLayout(element_size_arg, buffer_num))
  {
    return;
  }

  // 初期化権を CAS で獲得する。複数の writer がほぼ同時に起動した場合でも
  // 実際に初期化するのは一者だけになる。
  uint32_t expected = NOT_INITIALIZED;
  if (!initialization_flag->compare_exchange_strong(expected, INITIALIZING, std::memory_order_acq_rel,
                                                    std::memory_order_acquire))
  {
    if (expected == INITIALIZING)
    {
      // 他プロセスが初期化中 → 完了を待ち、レイアウトが合えば接続のみで済ませる
      if (waitForInitialization(memory_ptr, INIT_WAIT_TIMEOUT_US) && hasCompatibleLayout(element_size_arg, buffer_num))
      {
        return;
      }
      // 待ちきれなかった（初期化中に落ちた残骸）か、レイアウトが違う → 作り直す
    }
    // expected == INITIALIZED でレイアウト不一致、または上記の作り直し。
    // 初期化中であることを購読側に見せるため、一旦 INITIALIZING に落とす。
    initialization_flag->store(INITIALIZING, std::memory_order_release);
  }

  initializeContents(element_size_arg, buffer_num);
}

//! @brief 共有メモリ上のレイアウトが要求と一致するか確認する
//! @param [in] element_size 要求する要素サイズ
//! @param [in] buffer_num   要求するバッファ数
//! @return bool 初期化済みかつレイアウトが一致していれば真
//! @details 初期化途中の共有メモリから element_size / buf_num を読むと
//!          不定値を掴むため、必ず初期化フラグの確認を先に行う。
bool
RingBuffer::hasCompatibleLayout(size_t element_size_arg, int buffer_num) const
{
  if (initialization_flag->load(std::memory_order_acquire) != INITIALIZED)
  {
    return false;
  }
  return (*element_size == element_size_arg) && (*buf_num == static_cast<size_t>(buffer_num));
}

//! @brief リングバッファの実体を初期化する
//! @param [in] element_size 要素サイズ
//! @param [in] buffer_num   バッファ数
//! @return なし
//! @details 呼び出し側で初期化フラグを INITIALIZING にしてから呼ぶこと。
void
RingBuffer::initializeContents(size_t element_size_arg, int buffer_num)
{
  *element_size = element_size_arg;
  *buf_num      = buffer_num;

  // 走査範囲のスナップショットも作り直したレイアウトに合わせる
  expected_element_size = element_size_arg;
  expected_buf_num      = static_cast<size_t>(buffer_num);

  initializeExclusiveAccess();

  // Initialize all timestamp buffers to 0
  for (size_t i = 0; i < static_cast<size_t>(buffer_num); ++i)
  {
    timestamp_list[i].store(0, std::memory_order_relaxed);
  }

  // Ensure all memory operations are complete before marking as initialized
  std::atomic_thread_fence(std::memory_order_release);

  // Mark as initialized after all setup is complete
  initialization_flag->store(INITIALIZED, std::memory_order_release);
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
  pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
  pthread_cond_init(condition, &cond_attr);
  pthread_condattr_destroy(&cond_attr);

  pthread_mutexattr_t m_attr;
  pthread_mutexattr_init(&m_attr);
  pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED);
  pthread_mutexattr_setrobust(&m_attr, PTHREAD_MUTEX_ROBUST);
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
uint64_t
RingBuffer::getTimestamp_us() const
{
  return timestamp_us.load(std::memory_order_relaxed);
}

//! @brief 指定バッファのタイムスタンプ取得
//! @param [in] buffer_num バッファ番号
//! @return uint64_t タイムスタンプ
//! @details 指定したバッファの現在のタイムスタンプを読み込む．
//!          読み出し側の整合性検証（コピー前後の比較）に使用する．
uint64_t
RingBuffer::getTimestamp_us(int buffer_num) const
{
  return timestamp_list[buffer_num].load(std::memory_order_acquire);
}

//! @brief タイムスタンプ値が「書き込み途中」マーカーかどうかを判定する
//! @param [in] timestamp 判定するタイムスタンプ値
//! @return bool 書き込み途中なら真
bool
RingBuffer::isBeingWritten(uint64_t timestamp)
{
  return (timestamp & WRITING_FLAG) != 0;
}

//! @brief タイムスタンプ取得
//! @param なし
//! @return なし
//! @details 直近で読み込んだトピックのタイムスタンプを返す．
void
RingBuffer::setTimestamp_us(uint64_t input_time_us, int buffer_num)
{
  // release: データ本体の書き込みがタイムスタンプより先に可視になることを保証する
  timestamp_list[buffer_num].store(input_time_us, std::memory_order_release);
}

int
RingBuffer::getNewestBufferNum()
{
  uint64_t timestamp_buf         = 0;
  size_t   newest_buffer         = -1;
  bool     found_valid_timestamp = false;

  // 走査範囲には構築時のスナップショット expected_buf_num を使う。
  // 共有メモリ上の *buf_num は他プロセスの再初期化でいつでも増え得るが、
  // timestamp_list / data_list のオフセットは構築時のレイアウトのままなので、
  // *buf_num を信じて回すとマッピング外を読むことになる（R01-F01）。
  for (size_t i = 0; i < expected_buf_num; i++)
  {
    uint64_t ts = timestamp_list[i].load();

    if (!isBeingWritten(ts) && ts > 0 && ts >= timestamp_buf)
    {
      timestamp_buf         = ts;
      newest_buffer         = i;
      found_valid_timestamp = true;
    }
  }

  // If no valid timestamp found, return -1
  if (!found_valid_timestamp)
  {
    return -1;
  }

  timestamp_us.store(timestamp_buf, std::memory_order_relaxed);

  // If data_expiry_time_us is 0, disable expiry check
  const uint64_t expiry_us = data_expiry_time_us.load(std::memory_order_relaxed);
  if (expiry_us == 0)
  {
    return newest_buffer;
  }

  uint64_t current_time_us = getCurrentTimeUSec();

  if (current_time_us - timestamp_buf < expiry_us)
  {
    return newest_buffer;
  }
  // std::cerr << "Data is expiry By time. (duration: " << current_time_us - timestamp_us
  //           << ", expiry time: " << data_expiry_time_us << ")" << std::endl;

  return -1;
}

int
RingBuffer::getOldestBufferNum()
{
  uint64_t now_us        = getCurrentTimeUSec();
  uint64_t oldest_value  = std::numeric_limits<uint64_t>::max();
  int      oldest_buffer = 0;
  bool     found         = false;
  // 走査範囲は構築時のスナップショット（理由は getNewestBufferNum() のコメント参照）
  for (size_t i = 0; i < expected_buf_num; i++)
  {
    uint64_t ts = timestamp_list[i].load(std::memory_order_acquire);
    if (isBeingWritten(ts))
    {
      uint64_t alloc_time_us = ts & ~WRITING_FLAG;
      if (now_us - alloc_time_us < STALE_WRITE_TIMEOUT_US)
      {
        // 生きている writer が書き込み中 → 候補から除外
        continue;
      }
      // クラッシュした writer の残骸 → 最優先で再利用する
      ts = 0;
    }
    if (!found || ts < oldest_value)
    {
      oldest_value  = ts;
      oldest_buffer = static_cast<int>(i);
      found         = true;
    }
  }

  if (found)
  {
    timestamp_us.store(oldest_value, std::memory_order_relaxed);
  }
  return oldest_buffer;
}

bool
RingBuffer::allocateBuffer(int buffer_num)
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return false;
  }
  uint64_t temp_buffer_timestamp = timestamp_list[buffer_num].load(std::memory_order_acquire);
  if (isBeingWritten(temp_buffer_timestamp))
  {
    // マーカーに埋め込まれた確保時刻が新しければ、生きている writer が
    // 書き込み中なので確保失敗。古ければクラッシュした writer の残骸
    // なので奪って再利用する（CAS で競合しても片方だけが成功する）。
    uint64_t alloc_time_us = temp_buffer_timestamp & ~WRITING_FLAG;
    if (getCurrentTimeUSec() - alloc_time_us < STALE_WRITE_TIMEOUT_US)
    {
      return false;
    }
  }
  uint64_t writing_marker = getCurrentTimeUSec() | WRITING_FLAG;
  if (!timestamp_list[buffer_num].compare_exchange_strong(temp_buffer_timestamp, writing_marker,
                                                          std::memory_order_acq_rel))
  {
    return false;
  }
  // 書き込み途中マーカーが、後続のデータ書き込みより先に他プロセスから
  // 見えることを保証する（逆順に見えると読み手の整合性検証が破れる）
  std::atomic_thread_fence(std::memory_order_seq_cst);
  return true;
}

void
RingBuffer::signal()
{
  // NOTE: pthread_cond_broadcast は プロセス間で使用すると永久にブロックする可能性がある。
  // Subscriberプロセスが pthread_cond_timedwait の内部プロトコル実行中に終了すると、
  // condition variable の内部状態（waiterカウンタ）が壊れ、
  // 次の pthread_cond_broadcast が __condvar_quiesce_and_switch_g1 内の
  // futex_wait で永久にブロックする。
  // PTHREAD_MUTEX_ROBUST は mutex の問題をカバーするが、
  // condition variable には同等の仕組み（PTHREAD_COND_ROBUST）が存在しない。
  //
  // データの更新有無は timestamp_list の atomic 操作で判定可能なため、
  // condition variable によるシグナリングは不要。
  // Subscriber 側は waitFor() 内で isUpdated() をポーリングして検知する。
}

//! @brief トピックの更新待ち
//! @param timeout_usec 待ち時間[usec]
//! @return bool トピックが更新されたかどうか
//! @details 待ち時間の間、トピックの更新をポーリングで待ち続ける．
//!          更新された場合または待ち時間が経過した場合、関数を終了する．
//! @note pthread_cond_timedwait を使用しない理由は signal() のコメントを参照
bool
RingBuffer::waitFor(uint64_t timeout_usec)
{
  uint64_t start_time = getCurrentTimeUSec();

  while (!isUpdated())
  {
    uint64_t elapsed = getCurrentTimeUSec() - start_time;
    if (elapsed >= timeout_usec)
    {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
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
  // 走査範囲は構築時のスナップショット（理由は getNewestBufferNum() のコメント参照）
  for (size_t i = 0; i < expected_buf_num; i++)
  {
    uint64_t ts = timestamp_list[i].load();
    // Skip buffers being written and invalid timestamps (0)
    if (!isBeingWritten(ts) && ts > 0 && timestamp_us.load(std::memory_order_relaxed) < ts)
    {
      return true;
    }
  }
  return false;
}

void
RingBuffer::setDataExpiryTime_us(uint64_t time_us)
{
  data_expiry_time_us.store(time_us, std::memory_order_relaxed);
}

void
RingBuffer::markAsInitialized()
{
  initialization_flag->store(INITIALIZED, std::memory_order_release);
}

//! @brief 共有メモリ上のレイアウトが、このインスタンスの前提と食い違っているか
//! @param なし
//! @return bool 食い違っていれば真（このインスタンスは作り直しが必要）
//! @details データ領域の開始位置はバッファ数に依存するため、別のプロセスが
//!          異なるバッファ数や要素サイズで初期化し直すと、このインスタンスが
//!          保持する data_list のオフセットは無効になる。気付かずに使い続けると
//!          読み手は無関係な領域を success として返し、書き手はバッファ数だけ
//!          共有メモリから読む（*buf_num）ため、割り当てたつもりのない位置へ
//!          書き込んでマッピング外に出る危険がある。
//!          初期化途中も「使えない」として真を返す。
bool
RingBuffer::isLayoutChanged() const
{
  if (initialization_flag->load(std::memory_order_acquire) != INITIALIZED)
  {
    return true;
  }
  return (*element_size != expected_element_size) || (*buf_num != expected_buf_num);
}

//! @brief 検証つきで既存の共有メモリへ接続する
//! @details 宣言側のコメントを参照（R01-F06）
std::unique_ptr<RingBuffer>
attachRingBuffer(SharedMemory &memory, std::string *reason)
{
  unsigned char *ptr = memory.getPtr();
  if (ptr == nullptr)
  {
    if (reason != nullptr)
    {
      *reason = "shared memory is not mapped";
    }
    return nullptr;
  }

  if (!RingBuffer::validateLayout(ptr, memory.getSize(), reason))
  {
    return nullptr;
  }

  try
  {
    return std::make_unique<RingBuffer>(ptr);
  }
  catch (const std::exception &e)
  {
    if (reason != nullptr)
    {
      *reason = e.what();
    }
    return nullptr;
  }
}

}  // namespace shm

}  // namespace irlab
