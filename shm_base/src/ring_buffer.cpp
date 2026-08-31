#include <shm_base.hpp>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>

namespace irlab
{

namespace shm
{

// ============================================================================
// レイアウトの静的検査
//
// ShmHeader は共有メモリの先頭に生で置くため、サイズとオフセットが
// ビルドごとにずれてはならない。ずれたら即座にコンパイルを止める。
// ============================================================================
static_assert(sizeof(ShmHeader) == 128, "ShmHeader must stay 128 bytes");
static_assert(alignof(ShmHeader) <= 64, "ShmHeader alignment is unexpectedly large");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "atomic<uint32_t> must be lock-free in shared memory");
static_assert(std::atomic<uint64_t>::is_always_lock_free, "atomic<uint64_t> must be lock-free in shared memory");
static_assert(sizeof(SlotRecord) % 64 == 0, "SlotRecord must be a multiple of the cache line");
static_assert(offsetof(ShmHeader, magic) == 0, "magic must be the first field so v1 segments are detected");

uint64_t
getCurrentTimeUSec()
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC_RAW, &t);
  return ((uint64_t)t.tv_sec * 1000000L) + ((uint64_t)t.tv_nsec / 1000L);
}

//! @brief 壁時計の時刻[usec]
//! @details CLOCK_MONOTONIC_RAW は同一 boot 内でしか意味を持たず、日時指定に
//!          使えない。タイムマシン機能の検索用に realtime も併せて記録する。
static uint64_t
getCurrentRealtimeUSec()
{
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return ((uint64_t)t.tv_sec * 1000000L) + ((uint64_t)t.tv_nsec / 1000L);
}

//! @brief この boot を識別する値
//! @details /dev/shm は tmpfs なので通常は再起動で消えるが、消えなかった場合に
//!          「前回起動時の monotonic 時刻」を有効なものとして扱わないようにする。
//!          読めない環境では 0 を返し、その場合は照合しない。
static uint64_t
getBootIdHash()
{
  static const uint64_t cached = [] {
    FILE *fp = fopen("/proc/sys/kernel/random/boot_id", "r");
    if (fp == nullptr)
    {
      return static_cast<uint64_t>(0);
    }
    char buf[64] = { 0 };
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    if (n == 0)
    {
      return static_cast<uint64_t>(0);
    }
    // FNV-1a
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; ++i)
    {
      h ^= static_cast<unsigned char>(buf[i]);
      h *= 1099511628211ULL;
    }
    // 0 は「不明」を表すので避ける
    return h == 0 ? static_cast<uint64_t>(1) : h;
  }();
  return cached;
}

//! @brief 2の冪へ切り上げた境界に整列する
static inline size_t
alignUp(size_t value, size_t alignment)
{
  return (value + alignment - 1) & ~(alignment - 1);
}

static inline bool
isPowerOfTwo(size_t v)
{
  return v != 0 && (v & (v - 1)) == 0;
}

//! @brief レイアウトの各オフセットと総サイズを求める
//! @details 溢れを検査しながら計算し、破綻したら false を返す。
static bool
computeLayout(size_t element_capacity, size_t buf_num, size_t payload_alignment, size_t &slot_offset,
              size_t &data_offset, size_t &total_size)
{
  if (!isPowerOfTwo(payload_alignment))
  {
    return false;
  }

  slot_offset = alignUp(sizeof(ShmHeader), alignof(SlotRecord));

  size_t slots_bytes = 0;
  if (__builtin_mul_overflow(sizeof(SlotRecord), buf_num, &slots_bytes))
  {
    return false;
  }
  size_t slots_end = 0;
  if (__builtin_add_overflow(slot_offset, slots_bytes, &slots_end))
  {
    return false;
  }

  // ペイロードは payload_alignment と 64（キャッシュライン）の大きい方に載せる
  const size_t data_alignment = std::max(payload_alignment, static_cast<size_t>(64));
  data_offset                 = alignUp(slots_end, data_alignment);
  if (data_offset < slots_end)
  {
    return false;
  }

  // 1スロット分のストライドは element_capacity そのものにする。
  // 切り上げてしまうと i 番目のスロットが data_offset + i * element_capacity から
  // ずれ、既存の呼び出し側（i * getElementSize() でオフセットを出す）が壊れる。
  // 代わりに「capacity は payload_alignment の倍数であること」を生成時に要求し、
  // data_offset を境界に載せることで全スロットの整列を保証する。
  if (element_capacity != 0 && (element_capacity % payload_alignment) != 0)
  {
    return false;
  }
  size_t payload_bytes = 0;
  if (__builtin_mul_overflow(element_capacity, buf_num, &payload_bytes))
  {
    return false;
  }
  if (__builtin_add_overflow(data_offset, payload_bytes, &total_size))
  {
    return false;
  }

  total_size = alignUp(total_size, 64);
  return total_size >= data_offset;
}

size_t
RingBuffer::getSize(size_t element_size, int buffer_num, size_t payload_alignment)
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

  const size_t alignment = payload_alignment;
  if (!isPowerOfTwo(alignment) || alignment > MAX_PAYLOAD_ALIGNMENT)
  {
    throw std::invalid_argument("shm::RingBuffer::getSize(): payload_alignment must be a power of two <= " +
                                std::to_string(MAX_PAYLOAD_ALIGNMENT) + ", but got " +
                                std::to_string(payload_alignment));
  }

  size_t slot_offset = 0, data_offset = 0, total_size = 0;
  if (!computeLayout(element_size, static_cast<size_t>(buffer_num), alignment, slot_offset, data_offset, total_size))
  {
    throw std::invalid_argument("shm::RingBuffer::getSize(): element_size " + std::to_string(element_size) +
                                " is not a multiple of payload_alignment " + std::to_string(alignment) +
                                ", or the layout computation overflowed");
  }
  if (total_size > MAX_TOTAL_SIZE)
  {
    throw std::invalid_argument("shm::RingBuffer::getSize(): total size " + std::to_string(total_size) +
                                " exceeds the limit " + std::to_string(MAX_TOTAL_SIZE));
  }
  return total_size;
}

bool
RingBuffer::checkInitialized(unsigned char *first_ptr)
{
  if (first_ptr == nullptr)
  {
    return false;
  }
  const ShmHeader *h = reinterpret_cast<const ShmHeader *>(first_ptr);
  if (h->magic != SHM_MAGIC)
  {
    return false;
  }
  return h->state.load(std::memory_order_acquire) == RingBuffer::INITIALIZED;
}

bool
RingBuffer::waitForInitialization(unsigned char *first_ptr, uint64_t timeout_usec)
{
  auto start_time = getCurrentTimeUSec();

  while (!RingBuffer::checkInitialized(first_ptr))
  {
    auto current_time = getCurrentTimeUSec();
    if (current_time - start_time >= timeout_usec)
    {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }

  return true;
}

//! @brief 既存の共有メモリのヘッダとレイアウトを検証する
//! @details 共有メモリ上の値は「そう書いてあるだけ」なので、そこからポインタを
//!          組み立てる前に、magic・ABI 版・各サイズ・全オフセットが実マッピング長に
//!          収まることを溢れ検査付きで確認する（R01-F06）。
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
  if (reinterpret_cast<uintptr_t>(first_ptr) % alignof(ShmHeader) != 0)
  {
    return fail("first_ptr is not properly aligned for the header");
  }
  if (mapping_size < sizeof(ShmHeader))
  {
    return fail("mapping is smaller than the header (" + std::to_string(mapping_size) + " < " +
                std::to_string(sizeof(ShmHeader)) + ")");
  }

  const ShmHeader *h = reinterpret_cast<const ShmHeader *>(first_ptr);

  if (h->magic != SHM_MAGIC)
  {
    return fail("bad magic 0x" + std::to_string(h->magic) +
                "; the segment was created by another format or by shm v1. "
                "Remove it with 'shm_tool remove <topic>' and restart every process on this topic");
  }
  if (h->abi_major != ABI_MAJOR)
  {
    return fail("ABI major version mismatch (segment " + std::to_string(h->abi_major) + ", this build " +
                std::to_string(ABI_MAJOR) + ")");
  }
  if (h->header_size != sizeof(ShmHeader))
  {
    return fail("header size mismatch (segment " + std::to_string(h->header_size) + ", this build " +
                std::to_string(sizeof(ShmHeader)) + ")");
  }
  if (h->slot_size != sizeof(SlotRecord))
  {
    return fail("slot size mismatch (segment " + std::to_string(h->slot_size) + ", this build " +
                std::to_string(sizeof(SlotRecord)) + ")");
  }

  const uint64_t boot = getBootIdHash();
  if (boot != 0 && h->boot_id_hash != 0 && h->boot_id_hash != boot)
  {
    return fail("the segment was created before the last reboot; its monotonic timestamps are meaningless");
  }

  // element_capacity == 0 は異常ではない。空の vector を publish したトピックや、
  // まだ一度も publish されていない vector トピックが正当にこの状態になる。
  if (h->element_capacity > MAX_ELEMENT_SIZE)
  {
    return fail("element_capacity " + std::to_string(h->element_capacity) + " is out of range");
  }
  if (h->buf_num == 0 || h->buf_num > MAX_BUFFER_NUM)
  {
    return fail("buf_num " + std::to_string(h->buf_num) + " is out of range");
  }
  if (!isPowerOfTwo(h->payload_alignment) || h->payload_alignment > MAX_PAYLOAD_ALIGNMENT)
  {
    return fail("payload_alignment " + std::to_string(h->payload_alignment) + " is not a valid power of two");
  }

  size_t slot_offset = 0, data_offset = 0, total_size = 0;
  if (!computeLayout(h->element_capacity, h->buf_num, h->payload_alignment, slot_offset, data_offset, total_size))
  {
    return fail("layout computation overflowed for the values in the header");
  }
  if (h->slot_offset != slot_offset || h->data_offset != data_offset || h->total_size != total_size)
  {
    return fail("the offsets recorded in the header disagree with the ones computed from its own values");
  }
  if (total_size > MAX_TOTAL_SIZE)
  {
    return fail("total size " + std::to_string(total_size) + " exceeds the limit");
  }
  if (mapping_size < total_size)
  {
    return fail("mapping is smaller than the layout (" + std::to_string(mapping_size) + " < " +
                std::to_string(total_size) + "); the shared memory is truncated");
  }

  return true;
}

//! @brief コンストラクタ
//! @param [in] first_ptr         共有メモリ先頭
//! @param [in] size              要素サイズ。0 かつ buffer_num が 0 なら接続のみ
//! @param [in] buffer_num        バッファ数。0 なら既存レイアウトへの接続のみ
//! @param [in] payload_alignment ペイロードに要求する境界
RingBuffer::RingBuffer(unsigned char *first_ptr, size_t size, int buffer_num, size_t payload_alignment)
  : memory_ptr(first_ptr)
  , header(nullptr)
  , slot_base(nullptr)
  , data_list(nullptr)
  , timestamp_us(0)
  , last_sequence(0)
  , data_expiry_time_us(2000000)
  , expected_element_size(0)
  , expected_buf_num(0)
  , expected_payload_alignment(DEFAULT_PAYLOAD_ALIGNMENT)
  , expected_generation(0)
{
  if (first_ptr == nullptr)
  {
    throw std::invalid_argument("shm::RingBuffer: first_ptr must not be null");
  }
  // 先頭ポインタはヘッダの境界必須。非アラインだと atomic 変数や pthread 構造体への
  // アクセスが ARM では SIGBUS になる（x86 では動いてしまう）ため、
  // プラットフォームによらずここで即時に検出する。
  if (reinterpret_cast<uintptr_t>(first_ptr) % alignof(ShmHeader) != 0)
  {
    throw std::runtime_error("shm::RingBuffer: first_ptr is not properly aligned for the header!");
  }

  header = reinterpret_cast<ShmHeader *>(memory_ptr);

  if (buffer_num != 0)
  {
    // --- 生成経路。API 境界として入力を検証する ---
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
    const size_t alignment = payload_alignment;
    if (!isPowerOfTwo(alignment) || alignment > MAX_PAYLOAD_ALIGNMENT)
    {
      throw std::invalid_argument("shm::RingBuffer: payload_alignment must be a power of two <= " +
                                  std::to_string(MAX_PAYLOAD_ALIGNMENT));
    }
    if (size != 0 && (size % alignment) != 0)
    {
      throw std::invalid_argument("shm::RingBuffer: element_size " + std::to_string(size) +
                                  " must be a multiple of payload_alignment " + std::to_string(alignment));
    }

    initializeOrAttach(size, buffer_num, alignment);
  }
  else
  {
    // --- 接続経路。ヘッダを信じる前に妥当性を確認する ---
    // マッピング長との突き合わせは呼び出し側の validateLayout() が行う
    // （ここでは長さを知り得ない）。attachRingBuffer() を使うこと。
    if (header->magic != SHM_MAGIC)
    {
      throw std::runtime_error("shm::RingBuffer: bad magic; the segment was created by another format or by shm v1");
    }
    if (header->abi_major != ABI_MAJOR)
    {
      throw std::runtime_error("shm::RingBuffer: ABI major version mismatch (segment " +
                               std::to_string(header->abi_major) + ", this build " + std::to_string(ABI_MAJOR) + ")");
    }
    if (header->element_capacity > MAX_ELEMENT_SIZE)
    {
      throw std::runtime_error("shm::RingBuffer: element_capacity " + std::to_string(header->element_capacity) +
                               " in shared memory is out of range");
    }
    if (header->buf_num == 0 || header->buf_num > MAX_BUFFER_NUM)
    {
      throw std::runtime_error("shm::RingBuffer: buf_num " + std::to_string(header->buf_num) +
                               " in shared memory is out of range");
    }

    expected_element_size      = header->element_capacity;
    expected_buf_num           = header->buf_num;
    expected_payload_alignment = header->payload_alignment;
    expected_generation        = header->generation.load(std::memory_order_acquire);
    bindPointers();
  }
}

RingBuffer::~RingBuffer()
{
}

//! @brief ヘッダの値からポインタを組み立てる
void
RingBuffer::bindPointers()
{
  slot_base = memory_ptr + header->slot_offset;
  data_list = memory_ptr + header->data_offset;

  owned_slots = std::make_unique<std::atomic<bool>[]>(expected_buf_num);
  for (size_t i = 0; i < expected_buf_num; ++i)
  {
    owned_slots[i].store(false, std::memory_order_relaxed);
  }
}

SlotRecord *
RingBuffer::slot(int i) const
{
  return reinterpret_cast<SlotRecord *>(slot_base + static_cast<size_t>(i) * sizeof(SlotRecord));
}

bool
RingBuffer::ownsSlot(int i) const
{
  if (owned_slots == nullptr || i < 0 || static_cast<size_t>(i) >= expected_buf_num)
  {
    return false;
  }
  return owned_slots[i].load(std::memory_order_acquire);
}

void
RingBuffer::setSlotOwned(int i, bool owned)
{
  if (owned_slots == nullptr || i < 0 || static_cast<size_t>(i) >= expected_buf_num)
  {
    return;
  }
  owned_slots[i].store(owned, std::memory_order_release);
}

//! @brief 初期化済みなら接続のみ、そうでなければ初期化を行う
//! @details 要求レイアウトが既存の共有メモリと一致する限り再初期化を行わず、
//!          接続のみに留めることで既存の値と発行番号を保存する。
//!          （後発 Publisher がタイムスタンプを消す問題への対応）
void
RingBuffer::initializeOrAttach(size_t element_size_arg, int buffer_num, size_t payload_alignment)
{
  if (hasCompatibleLayout(element_size_arg, buffer_num, payload_alignment))
  {
    expected_element_size      = element_size_arg;
    expected_buf_num           = static_cast<size_t>(buffer_num);
    expected_payload_alignment = payload_alignment;
    expected_generation        = header->generation.load(std::memory_order_acquire);
    bindPointers();
    return;
  }

  // 初期化権を CAS で獲得する。複数の writer がほぼ同時に起動した場合でも
  // 実際に初期化するのは一者だけになる。
  uint32_t expected = NOT_INITIALIZED;
  const bool fresh  = (header->magic != SHM_MAGIC);
  if (fresh)
  {
    // magic が無い＝新規（または別形式）。state は信用できないので直接進める。
    header->state.store(INITIALIZING, std::memory_order_release);
  }
  else if (!header->state.compare_exchange_strong(expected, INITIALIZING, std::memory_order_acq_rel,
                                                  std::memory_order_acquire))
  {
    if (expected == INITIALIZING)
    {
      // 他プロセスが初期化中 → 完了を待ち、レイアウトが合えば接続のみで済ませる
      if (waitForInitialization(memory_ptr, INIT_WAIT_TIMEOUT_US) &&
          hasCompatibleLayout(element_size_arg, buffer_num, payload_alignment))
      {
        expected_element_size      = element_size_arg;
        expected_buf_num           = static_cast<size_t>(buffer_num);
        expected_payload_alignment = payload_alignment;
        expected_generation        = header->generation.load(std::memory_order_acquire);
        bindPointers();
        return;
      }
    }
    // レイアウト不一致、または初期化中に落ちた残骸 → 作り直す。
    // 初期化中であることを購読側に見せるため、一旦 INITIALIZING に落とす。
    header->state.store(INITIALIZING, std::memory_order_release);
  }

  initializeContents(element_size_arg, buffer_num, payload_alignment);
}

//! @brief 共有メモリ上のレイアウトが要求と一致するか確認する
bool
RingBuffer::hasCompatibleLayout(size_t element_size_arg, int buffer_num, size_t payload_alignment) const
{
  if (header->magic != SHM_MAGIC || header->abi_major != ABI_MAJOR)
  {
    return false;
  }
  if (header->state.load(std::memory_order_acquire) != INITIALIZED)
  {
    return false;
  }
  return (header->element_capacity == element_size_arg) && (header->buf_num == static_cast<size_t>(buffer_num)) &&
         (header->payload_alignment == payload_alignment);
}

//! @brief リングバッファの実体を初期化する
//! @details 呼び出し側で state を INITIALIZING にしてから呼ぶこと。
void
RingBuffer::initializeContents(size_t element_size_arg, int buffer_num, size_t payload_alignment)
{
  size_t slot_offset = 0, data_offset = 0, total_size = 0;
  if (!computeLayout(element_size_arg, static_cast<size_t>(buffer_num), payload_alignment, slot_offset, data_offset,
                     total_size))
  {
    throw std::invalid_argument("shm::RingBuffer: layout computation overflowed");
  }

  // 世代は作り直しのたびに進める。旧レイアウトのオフセットを持ったままの
  // インスタンスは isLayoutChanged() でこれに気付いて張り直す。
  const uint64_t next_generation = header->generation.load(std::memory_order_acquire) + 1;

  header->magic             = SHM_MAGIC;
  header->abi_major         = ABI_MAJOR;
  header->abi_minor         = ABI_MINOR;
  header->header_size       = static_cast<uint32_t>(sizeof(ShmHeader));
  header->total_size        = total_size;
  header->element_capacity  = element_size_arg;
  header->buf_num           = static_cast<uint64_t>(buffer_num);
  header->payload_alignment = payload_alignment;
  header->slot_offset       = slot_offset;
  header->slot_size         = sizeof(SlotRecord);
  header->data_offset       = data_offset;
  header->boot_id_hash      = getBootIdHash();
  header->generation.store(next_generation, std::memory_order_relaxed);
  header->sequence.store(0, std::memory_order_relaxed);
  std::memset(header->reserved, 0, sizeof(header->reserved));

  expected_element_size      = element_size_arg;
  expected_buf_num           = static_cast<size_t>(buffer_num);
  expected_payload_alignment = payload_alignment;
  expected_generation        = next_generation;
  bindPointers();

  initializeExclusiveAccess();

  // Ensure all memory operations are complete before marking as initialized
  std::atomic_thread_fence(std::memory_order_release);

  header->state.store(INITIALIZED, std::memory_order_release);
}

//! @brief スロットの robust mutex と発行番号を初期化する
void
RingBuffer::initializeExclusiveAccess()
{
  pthread_mutexattr_t m_attr;
  pthread_mutexattr_init(&m_attr);
  pthread_mutexattr_setpshared(&m_attr, PTHREAD_PROCESS_SHARED);
  // ROBUST が要点。所有者プロセスが死んだことをカーネルが検出し、
  // 次に lock した者へ EOWNERDEAD を返してくれる。
  // これが無いと「死んだのか、単に遅いだけなのか」を時刻で推測するしかなく、
  // 生きている writer からスロットを奪ってデータを壊す（R01-F04）。
  pthread_mutexattr_setrobust(&m_attr, PTHREAD_MUTEX_ROBUST);

  for (size_t i = 0; i < expected_buf_num; ++i)
  {
    SlotRecord *s = slot(static_cast<int>(i));
    pthread_mutex_init(&s->owner, &m_attr);
    s->payload_size         = 0;
    s->capture_monotonic_us = 0;
    s->capture_realtime_us  = 0;
    s->sequence.store(0, std::memory_order_relaxed);
  }

  pthread_mutexattr_destroy(&m_attr);
}

size_t
RingBuffer::getElementSize() const
{
  return header->element_capacity;
}

unsigned char *
RingBuffer::getDataList()
{
  return data_list;
}

uint64_t
RingBuffer::getGeneration() const
{
  return header->generation.load(std::memory_order_acquire);
}

//! @brief 直近で選んだスロットの capture 時刻[usec]
uint64_t
RingBuffer::getTimestamp_us() const
{
  return timestamp_us.load(std::memory_order_relaxed);
}

//! @brief 指定スロットの capture 時刻[usec]
//! @details 有効なデータが無い（発行番号 0）場合は WRITING_FLAG を返す。
//!          isBeingWritten() を使っている既存コードの互換のため。
uint64_t
RingBuffer::getTimestamp_us(int buffer_num) const
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return WRITING_FLAG;
  }
  const SlotRecord *s = slot(buffer_num);
  if (s->sequence.load(std::memory_order_acquire) == 0)
  {
    return WRITING_FLAG;
  }
  return s->capture_monotonic_us;
}

uint64_t
RingBuffer::getSequence(int buffer_num) const
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return 0;
  }
  return slot(buffer_num)->sequence.load(std::memory_order_acquire);
}

uint64_t
RingBuffer::getPayloadSize(int buffer_num) const
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return 0;
  }
  return slot(buffer_num)->payload_size;
}

uint64_t
RingBuffer::getCaptureRealtime_us(int buffer_num) const
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return 0;
  }
  return slot(buffer_num)->capture_realtime_us;
}

bool
RingBuffer::isBeingWritten(uint64_t timestamp)
{
  return (timestamp & WRITING_FLAG) != 0;
}

//! @brief 最新のスロットを選ぶ
//! @details 「最新」は最大の発行番号で決まる。発行番号は単一の atomic から
//!          fetch_add で採番するので一意であり、同一 microsecond に複数の
//!          publish があってもスロット番号で誤選択することがない（R01-F05）。
int
RingBuffer::getNewestBufferNum()
{
  uint64_t newest_seq = 0;
  int      newest     = -1;

  // 走査範囲には構築時のスナップショット expected_buf_num を使う。
  // 共有メモリ上の buf_num は他プロセスの再初期化でいつでも増え得るが、
  // slot_base / data_list のオフセットは構築時のレイアウトのままなので、
  // ヘッダの値を信じて回すとマッピング外を読むことになる（R01-F01）。
  for (size_t i = 0; i < expected_buf_num; i++)
  {
    const uint64_t seq = slot(static_cast<int>(i))->sequence.load(std::memory_order_acquire);
    if (seq > newest_seq)
    {
      newest_seq = seq;
      newest     = static_cast<int>(i);
    }
  }

  if (newest < 0)
  {
    return -1;
  }

  const uint64_t capture = slot(newest)->capture_monotonic_us;
  timestamp_us.store(capture, std::memory_order_relaxed);
  // 「ここまで読んだ」を記録する。v1 では timestamp_us の更新がこの役目を
  // 兼ねており、getNewestBufferNum() の後は isUpdated() が偽になっていた。
  // 外部の特殊化もその挙動に依存しているため、意味を保つ。
  markAsRead(newest_seq);

  const uint64_t expiry_us = data_expiry_time_us.load(std::memory_order_relaxed);
  if (expiry_us == 0)
  {
    return newest;
  }

  const uint64_t current_time_us = getCurrentTimeUSec();
  if (current_time_us - capture < expiry_us)
  {
    return newest;
  }
  return -1;
}

//! @brief 最も古いスロットを選ぶ（書き込み先の候補）
//! @details 発行番号が小さいほど古い。0（未使用）が最優先。
//!          確保できるかどうかは allocateBuffer() が判定する。
int
RingBuffer::getOldestBufferNum()
{
  uint64_t oldest_seq = std::numeric_limits<uint64_t>::max();
  int      oldest     = 0;

  for (size_t i = 0; i < expected_buf_num; i++)
  {
    const uint64_t seq = slot(static_cast<int>(i))->sequence.load(std::memory_order_acquire);
    if (seq < oldest_seq)
    {
      oldest_seq = seq;
      oldest     = static_cast<int>(i);
    }
  }
  return oldest;
}

//! @brief スロットを確保する
//! @details robust mutex の trylock で所有権を取る。
//!          - 成功        → 確保できた
//!          - EBUSY       → 生きている writer が使用中。**絶対に奪わない**。
//!                          以前は「1秒より古ければクラッシュ済み」とみなして
//!                          奪っていたが、SIGSTOP やページフォルトで止まっていた
//!                          だけの writer が再開して新 writer の payload を
//!                          上書きし、壊れた値が有効データとして公開され得た（R01-F04）。
//!          - EOWNERDEAD  → カーネルが所有者の死を確定させた場合のみ回収する。
//!                          中身は壊れている前提なので発行番号を 0 に落とす。
bool
RingBuffer::allocateBuffer(int buffer_num)
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return false;
  }

  SlotRecord *s = slot(buffer_num);
  int         r = pthread_mutex_trylock(&s->owner);

  if (r == EOWNERDEAD)
  {
    // 所有者が死んだ。一貫性を宣言すればロックは自分が保持している。
    pthread_mutex_consistent(&s->owner);
    r = 0;
  }
  else if (r == ENOTRECOVERABLE)
  {
    // 誰かが consistent を宣言せずに手放した。このスロットは以後使えない。
    return false;
  }

  if (r != 0)
  {
    return false;  // EBUSY: 生きている writer が使用中
  }

  // 書き込み中は「有効なデータが無い」状態にしておく。
  // 途中で死んでも 0 のままなので、読み手には最初から見えない。
  // capture 時刻も無効化する。残しておくと commitBuffer() が前回の時刻を
  // そのまま使い回し、最新スロットの時刻が過去へ戻ることがある。
  s->capture_monotonic_us = 0;
  s->capture_realtime_us  = 0;
  s->payload_size         = 0;
  s->sequence.store(0, std::memory_order_release);
  setSlotOwned(buffer_num, true);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  return true;
}

//! @brief 書き込みを確定してスロットを手放す
//! @details 発行番号は「コミット直前」に採番する。確保時ではない。
//!          こうすると「番号が小さいスロット＝先にコミットされたスロット」が
//!          常に成り立ち、遅い Publisher が先に取った番号で後から割り込むことがない。
//!          複数 Publisher を正式にサポートするための要点。
void
RingBuffer::commitBuffer(int buffer_num, size_t payload_size, uint64_t capture_monotonic_us)
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return;
  }
  SlotRecord *s = slot(buffer_num);

  s->payload_size         = payload_size;
  s->capture_realtime_us  = getCurrentRealtimeUSec();
  s->capture_monotonic_us = (capture_monotonic_us != 0) ? capture_monotonic_us : getCurrentTimeUSec();

  const uint64_t seq = header->sequence.fetch_add(1, std::memory_order_acq_rel) + 1;
  s->sequence.store(seq, std::memory_order_release);

  // allocateBuffer() を通さずに呼ばれた場合、この mutex は保持していない。
  // 保持していない mutex の unlock は未定義動作なので、必ず所有を確認する。
  if (ownsSlot(buffer_num))
  {
    setSlotOwned(buffer_num, false);
    pthread_mutex_unlock(&s->owner);
  }
}

void
RingBuffer::releaseBuffer(int buffer_num)
{
  if (!ownsSlot(buffer_num))
  {
    return;
  }
  setSlotOwned(buffer_num, false);
  pthread_mutex_unlock(&slot(buffer_num)->owner);
}

//! @deprecated commitBuffer() を使うこと
void
RingBuffer::setTimestamp_us(uint64_t input_time_us, int buffer_num)
{
  commitBuffer(buffer_num, header->element_capacity, input_time_us);
}

void
RingBuffer::signal()
{
  // NOTE: pthread_cond_broadcast はプロセス間で使用すると永久にブロックする可能性がある。
  // Subscriber プロセスが pthread_cond_timedwait の内部プロトコル実行中に終了すると、
  // condition variable の内部状態（waiter カウンタ）が壊れ、
  // 次の pthread_cond_broadcast が __condvar_quiesce_and_switch_g1 内の
  // futex_wait で永久にブロックする。
  // PTHREAD_MUTEX_ROBUST は mutex の問題をカバーするが、
  // condition variable には同等の仕組み（PTHREAD_COND_ROBUST）が存在しない。
  //
  // データの更新有無は発行番号の atomic 操作で判定できるため、
  // condition variable によるシグナリングは不要。
  // Subscriber 側は waitFor() 内で isUpdated() をポーリングして検知する。
  //
  // この理由から、形式 v2 でも condition variable はレイアウトに置いていない。
}

//! @brief トピックの更新待ち
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
//! @details 直近で読み込んだ発行番号より新しい発行番号が書き込まれたか確認する。
//!          時刻ではなく発行番号で比較するので、同一 microsecond の publish も
//!          取りこぼさない。
bool
RingBuffer::isUpdated() const
{
  const uint64_t seen = last_sequence.load(std::memory_order_relaxed);
  for (size_t i = 0; i < expected_buf_num; i++)
  {
    const uint64_t seq = slot(static_cast<int>(i))->sequence.load(std::memory_order_acquire);
    if (seq > seen)
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
RingBuffer::markAsRead(uint64_t sequence)
{
  uint64_t seen = last_sequence.load(std::memory_order_relaxed);
  while (sequence > seen && !last_sequence.compare_exchange_weak(seen, sequence, std::memory_order_relaxed))
  {
  }
}

void
RingBuffer::markAsInitialized()
{
  header->state.store(INITIALIZED, std::memory_order_release);
}

//! @brief 共有メモリ上のレイアウトが、このインスタンスの前提と食い違っているか
//! @details データ領域の開始位置はバッファ数と要素サイズに依存するため、別の
//!          プロセスが異なるレイアウトで初期化し直すと、このインスタンスが保持する
//!          data_list のオフセットは無効になる。世代番号でも判定するので、
//!          同じレイアウトで作り直された場合（＝発行番号がリセットされた場合）も
//!          検出できる。初期化途中も「使えない」として真を返す。
bool
RingBuffer::isLayoutChanged() const
{
  if (header->magic != SHM_MAGIC || header->abi_major != ABI_MAJOR)
  {
    return true;
  }
  if (header->state.load(std::memory_order_acquire) != INITIALIZED)
  {
    return true;
  }
  if (header->generation.load(std::memory_order_acquire) != expected_generation)
  {
    return true;
  }
  return (header->element_capacity != expected_element_size) || (header->buf_num != expected_buf_num) ||
         (header->payload_alignment != expected_payload_alignment);
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
