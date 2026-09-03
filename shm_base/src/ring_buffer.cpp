#include <shm_base.hpp>
// NOTE: <sched.h> があったが、必要としていたのは sched_yield() だけで、
//       R05 で lockSlotWithin() を trylock + nanosleep へ書き換えた際に
//       その最後の利用者が消えた。<ctime> は nanosleep のために要る。
#include <ctime>
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

#ifdef SHM_ENABLE_TEST_HOOKS
namespace test_hooks
{
//! 定義はここに 1 つだけ置く。既定では何も設定されていない。
std::function<void()> before_commit;
}  // namespace test_hooks
#endif

// ============================================================================
// レイアウトの静的検査
//
// ShmHeader は共有メモリの先頭に生で置くため、サイズとオフセットが
// ビルドごとにずれてはならない。ずれたら即座にコンパイルを止める。
// ============================================================================
static_assert(sizeof(ShmHeader) == 192, "ShmHeader must stay 192 bytes");
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

//! @brief スロットの robust mutex を、単調時計で区切った上限まで待って取る
//! @details reader も payload コピーの間はスロットを排他するため（R03-F04）、
//!          trylock 一発で諦めると次のような取りこぼしが起きる:
//!            - buf_num=1 で reader が全力で回ると writer が確保できず publish が失敗
//!            - 同じスロットを狙う reader 同士が互いに弾き合う
//!          スロットの臨界区間は memcpy 1 回ぶんしかないので、短い上限まで
//!          待てば十分に解消する。
//!
//!          **待ち方に `pthread_mutex_clocklock` を使ってはならない。**
//!          robust mutex の futex ワードに「生きているタスクとして解決できない
//!          TID」が入っていると、glibc が
//!            Assertion `e != ESRCH || !robust' failed. (pthread_mutex_timedlock.c)
//!          で **abort する**。戻り値ではなく assert なので捕捉も回復もできない。
//!          そうなる状態は現実に起こり得る（R05）:
//!            - セグメントの mutex 領域が別プロセスの誤書き込みで壊れた
//!            - 保持者が別の PID namespace に居る（コンテナ間で /dev/shm を共有）
//!            - 保持したまま munmap して終了し、robust list が処理されなかった
//!          いずれも `/dev/shm` に永続するので、Subscriber が起動のたびに
//!          同じスロットを選んで死ぬ恒久的なクラッシュループになる。
//!
//!          `pthread_mutex_trylock` は同じ状態でも EBUSY を返すだけで安全なので、
//!          **trylock と nanosleep の組み合わせ**で待つ。
//!          `sched_yield` のスピンに戻すわけではない（R04-F23 の原因はそれで、
//!          CPU 過負荷のとき待ち手が自分の量子を捨てるだけで保持者に CPU が
//!          回らなかった）。実際に眠れば保持者が走れる。
//! @return 0 なら取得（EOWNERDEAD なら consistent 宣言が必要）、EBUSY なら時間切れ
static int
lockSlotWithin(pthread_mutex_t *mutex, uint64_t timeout_us)
{
  int r = pthread_mutex_trylock(mutex);
  if (r != EBUSY || timeout_us == 0)
  {
    return r;
  }

  // 保持者が走れるよう、実際に眠って待つ。
  // 臨界区間は memcpy 1 回ぶんなので、細かい刻みで十分に拾える。
  constexpr uint64_t SLEEP_STEP_US = 50;
  const uint64_t     deadline      = getCurrentTimeUSec() + timeout_us;
  while (true)
  {
    struct timespec nap;
    nap.tv_sec  = 0;
    nap.tv_nsec = static_cast<long>(SLEEP_STEP_US) * 1000L;
    nanosleep(&nap, nullptr);

    r = pthread_mutex_trylock(mutex);
    if (r != EBUSY)
    {
      return r;
    }
    if (getCurrentTimeUSec() >= deadline)
    {
      return EBUSY;
    }
  }
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
//!          shm_tool doctor が「再起動前の残骸」を見分けるためにも使う。
uint64_t
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

//! @brief トピックの取り決めが一致するか
//! @details 一致しない接続を許すと、長さの解釈が食い違って範囲外アクセスになる。
//!          実測では Publisher<uint8_t> のトピックへ 1 MiB の型で接続すると
//!          SIGSEGV していた（R02-F01）。
bool
RingBuffer::TopicContract::matches(const TopicContract &other, std::string *reason) const
{
  auto fail = [reason](const std::string &msg) {
    if (reason != nullptr)
    {
      *reason = msg;
    }
    return false;
  };

  auto toHex = [](uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", v);
    return std::string(buf);
  };

  auto kind_name = [](PayloadKind k) -> const char * {
    switch (k)
    {
      case PayloadKind::Scalar:     return "scalar";
      case PayloadKind::Vector:     return "vector";
      case PayloadKind::Serialized: return "serialized";
      default:                      return "unknown";
    }
  };

  if (kind != other.kind)
  {
    return fail(std::string("payload kind mismatch (segment is ") + kind_name(other.kind) + ", this process expects " +
                kind_name(kind) + ")");
  }
  if (element_size != other.element_size)
  {
    return fail("element size mismatch (segment " + std::to_string(other.element_size) + " bytes, this process expects " +
                std::to_string(element_size) + " bytes)");
  }
  // 書式の識別子（SHM_DECLARE_LAYOUT / SHM_DECLARE_SERIALIZED_FORMAT）。
  //
  // 「片方が 0 なら照合しない」としてはならない（R04-F09）。
  // 版はセグメント生成時にしか書かれないので、宣言を後から足したトピックは
  // セグメント側が 0 のまま固定される。片側 0 を素通りさせると、
  // **宣言を導入した瞬間・書式を変えた瞬間という、最も検査が要る局面で
  // 検査が消える**。実際 R03 ではその状態になっており、
  // shm_schema<T> は実機で一度も照合されていなかった。
  //
  // 0 は「宣言が無い」であって「何でもよい」ではない。
  // 片側だけ 0 なら、レイアウトが一致しているかを**確かめる手段が無い**ので拒む。
  if (schema_version != other.schema_version)
  {
    if (schema_version != 0 && other.schema_version != 0)
    {
      return fail("payload format mismatch (segment 0x" + toHex(other.schema_version) + ", this process expects 0x" +
                  toHex(schema_version) + "); the member layout or the serialization format changed");
    }
    if (other.schema_version == 0)
    {
      return fail("the segment was created by a build that did not declare this payload's format, "
                  "so its layout cannot be verified against this process (which expects 0x" +
                  toHex(schema_version) + ")");
    }
    return fail("this build does not declare this payload's format, but the segment does (0x" +
                toHex(other.schema_version) +
                "); this process is probably older than the one that created the segment");
  }

  // schema_id は __PRETTY_FUNCTION__ のハッシュなので、**同じツールチェインでしか
  // 一致しない**。異なるコンパイラ／言語バインディング間では別の値になり得る。
  // 書式が宣言されていればそちらが正本なので、ここは宣言が無い場合の保険として使う。
  const bool format_is_declared = (schema_version != 0);
  if (!format_is_declared && schema_id != 0 && other.schema_id != 0 && schema_id != other.schema_id)
  {
    return fail("payload type mismatch: the segment holds a different type of the same size");
  }
  if (alignment != 0 && other.alignment != 0 && (other.alignment % alignment) != 0)
  {
    return fail("payload alignment mismatch (segment " + std::to_string(other.alignment) + ", this process needs " +
                std::to_string(alignment) + ")");
  }
  return true;
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
//! @brief 既存の共有メモリのレイアウトを検証する（真偽版）
//! @details 3 値が要るときは inspectLayout() を使うこと。
bool
RingBuffer::validateLayout(const unsigned char *first_ptr, size_t mapping_size, std::string *reason,
                           const TopicContract *expected, uint64_t expected_generation)
{
  return inspectLayout(first_ptr, mapping_size, reason, expected, expected_generation) == LayoutVerdict::Usable;
}

RingBuffer::LayoutVerdict
RingBuffer::inspectLayout(const unsigned char *first_ptr, size_t mapping_size, std::string *reason,
                          const TopicContract *expected, uint64_t expected_generation, const std::string &topic_name)
{
  const std::string where = topic_name.empty() ? std::string() : (" [topic '" + topic_name + "']");
  // 待っても直らない失敗。呼び出し側は即座に諦めるべきで、
  // 運用者には復旧手順を示す必要がある（R04-F13）。
  auto incompatible = [reason, &where, &topic_name](const std::string &msg) {
    if (reason != nullptr)
    {
      *reason = msg + where + ". Remove the segment with 'shm_tool remove " +
                (topic_name.empty() ? "<topic>" : topic_name) + "' and restart every process on this topic";
    }
    return LayoutVerdict::Incompatible;
  };
  // まだ初期化途中。待てば直る可能性がある。
  auto not_ready = [reason, &where](const std::string &msg) {
    if (reason != nullptr)
    {
      *reason = msg + where;
    }
    return LayoutVerdict::NotReady;
  };
  auto fail = incompatible;

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
    // magic が 0 なのは「作成直後でまだ初期化されていない」状態である。
    // shm_open(O_CREAT) + ftruncate() した直後はゼロ埋めなので、作成者が
    // initializeContents() を終える前に他プロセスが覗くとここに来る。
    // **待てば直る**ので、恒久的な不整合と混同してはならない。
    // v1 のセグメントも先頭 4 バイトが初期化フラグ 0 なら同じ扱いでよい
    //（初期化されていないのだから待つのが正しい）。
    if (h->magic == 0)
    {
      return not_ready("the segment has just been created and is not initialized yet");
    }
    return incompatible("bad magic 0x" + std::to_string(h->magic) +
                        "; the segment was created by another format or by shm v1");
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

  // 初期化が完了していないセグメントに接続してはならない（R02-F02）。
  // 途中の状態では pthread mutex がまだ初期化されていないか、初期化の最中である。
  // 以前は state を見ていなかったため、INITIALIZING のまま success を返していた。
  const uint32_t state = h->state.load(std::memory_order_acquire);
  if (state != INITIALIZED)
  {
    // ここだけは待てば直る可能性がある
    return not_ready(state == INITIALIZING ? "the segment is still being initialized"
                                           : "the segment is not initialized");
  }

  const uint64_t boot = getBootIdHash();
  if (boot != 0 && h->boot_id_hash != 0 && h->boot_id_hash != boot)
  {
    return fail("the segment was created before the last reboot; its monotonic timestamps are meaningless");
  }

  // セグメント名の世代とヘッダの世代が食い違っていないか（R02-F06）
  if (expected_generation != 0 && h->generation != expected_generation)
  {
    return fail("generation mismatch (segment says " + std::to_string(h->generation) + ", expected " +
                std::to_string(expected_generation) + ")");
  }

  // トピックの取り決めの照合（R02-F01）。payload に触れる前に行う。
  if (expected != nullptr)
  {
    TopicContract actual;
    actual.kind         = static_cast<PayloadKind>(h->payload_kind);
    actual.element_size = h->element_size;
    actual.schema_id      = h->schema_id;
    actual.schema_version = h->schema_version;
    actual.alignment      = h->payload_alignment;
    if (!expected->matches(actual, reason))
    {
      return LayoutVerdict::Incompatible;
    }
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

  // 検証している最中に他プロセスが作り直していないことを確認する（R02-F02）
  if (h->state.load(std::memory_order_acquire) != INITIALIZED)
  {
    return fail("the segment was re-initialized while it was being validated");
  }

  return LayoutVerdict::Usable;
}

//! @brief コンストラクタ
//! @param [in] first_ptr         共有メモリ先頭
//! @param [in] size              要素サイズ。0 かつ buffer_num が 0 なら接続のみ
//! @param [in] buffer_num        バッファ数。0 なら既存レイアウトへの接続のみ
//! @param [in] payload_alignment ペイロードに要求する境界
RingBuffer::RingBuffer(unsigned char *first_ptr, size_t size, int buffer_num, size_t payload_alignment,
                       const TopicContract *contract, uint64_t own_generation, uint64_t own_nonce)
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

    initializeOrAttach(size, buffer_num, alignment, contract, own_generation, own_nonce);
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
    // 初期化が完了していないセグメントに接続してはならない（R02-F02）。
    // pthread mutex が未初期化・初期化途中の可能性がある。
    if (header->state.load(std::memory_order_acquire) != INITIALIZED)
    {
      throw std::runtime_error("shm::RingBuffer: the segment is not fully initialized");
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
    expected_generation        = header->generation;
    bindPointers();
  }
}

RingBuffer::~RingBuffer()
{
  // ここで releaseOwnedSlots() を呼んではならない。
  // RingBuffer より先に共有メモリのマッピングが消えている場合があり
  //（破棄順序は利用者側のスコープ次第）、デストラクタから共有メモリへ
  // 書きに行くと SIGSEGV する。解放は「マッピングがまだ生きていると
  // 呼び出し側が知っている場所」で明示的に行うこと。
}

//! @brief このインスタンスが確保したまま手放していないスロットを解放する
//! @details 放置すると 2 つの実害がある（R04）。
//!   1. スロットが永久に「書き込み中」のまま残り、リングが実質的に縮む。
//!      robust mutex はプロセスが死んだときしか回収されないので、
//!      同じプロセス内では二度と使えなくなる。
//!   2. **より深刻**: PTHREAD_MUTEX_ROBUST の mutex はロック中、スレッドの
//!      robust list（カーネルに登録された連結リスト）に繋がれる。そのリストの
//!      ノードは mutex 自身、つまり共有メモリ上にある。解放しないまま共有メモリを
//!      munmap すると、リストが解放済み領域を指したままになり、**後続の無関係な
//!      pthread_mutex_trylock がそこを書きに行って SIGSEGV する**。
//!      「別のトピックの publish が、前に確保しっぱなしにしたロックのせいで落ちる」
//!      という、原因の分からないクラッシュになる。
//!
//! **必ず共有メモリのマッピングが生きているうちに呼ぶこと。**
//! 中身は書き込み途中なので、発行番号を 0 にして読み手から見えなくする。
void
RingBuffer::releaseOwnedSlots()
{
  if (owned_slots == nullptr || slot_base == nullptr)
  {
    return;
  }
  for (size_t i = 0; i < expected_buf_num; ++i)
  {
    const int index = static_cast<int>(i);
    if (!ownsSlot(index))
    {
      continue;
    }
    slot(index)->sequence.store(0, std::memory_order_release);
    releaseBuffer(index);
  }
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
RingBuffer::initializeOrAttach(size_t element_size_arg, int buffer_num, size_t payload_alignment,
                               const TopicContract *contract, uint64_t own_generation, uint64_t own_nonce)
{
  auto adopt = [&]() {
    expected_element_size      = element_size_arg;
    expected_buf_num           = static_cast<size_t>(buffer_num);
    expected_payload_alignment = payload_alignment;
    expected_generation        = header->generation;
    bindPointers();
  };

  if (hasCompatibleLayout(element_size_arg, buffer_num, payload_alignment, contract))
  {
    adopt();
    return;
  }

  // ------------------------------------------------------------------------
  // 初期化権の獲得（R02-F02）
  //
  // 初期化してよいのは「NOT_INITIALIZED からの CAS に成功した一者だけ」に限る。
  // 以前は magic が無い領域では CAS せずに INITIALIZING を store していたため、
  // 同時に新規と判断した複数の Publisher が同じ pthread_mutex_t を並行して
  // pthread_mutex_init() し得た。また待ち時間切れでも各自が INITIALIZING を
  // store して全員が再初期化へ進めた。
  //
  // 新規セグメントは ftruncate でゼロ埋めされるので state は NOT_INITIALIZED に
  // なる。したがって特別扱いは不要で、常に CAS で判定できる。
  // ------------------------------------------------------------------------
  uint32_t expected = NOT_INITIALIZED;
  if (header->state.compare_exchange_strong(expected, INITIALIZING, std::memory_order_acq_rel,
                                            std::memory_order_acquire))
  {
    initializeContents(element_size_arg, buffer_num, payload_alignment, contract, own_generation, own_nonce);
    return;
  }

  if (expected == INITIALIZING)
  {
    // 他プロセスが初期化中。完了を待ってから、あらためて判定する。
    if (waitForInitialization(memory_ptr, INIT_WAIT_TIMEOUT_US))
    {
      if (hasCompatibleLayout(element_size_arg, buffer_num, payload_alignment, contract))
      {
        adopt();
        return;
      }
      throw std::runtime_error("shm::RingBuffer: the segment was initialized with a different layout");
    }
    // 待ちきれなかった。**ここで奪って作り直してはならない。**
    // 初期化中に落ちた残骸なのか、単に遅いだけなのかを時刻から区別できない。
    // 奪えば、生きている相手が初期化中の pthread object を壊すことになる。
    // 安全に自動回収する手段が無いので、明示的に失敗させて人の判断を仰ぐ。
    throw std::runtime_error(
        "shm::RingBuffer: another process has been initializing this segment for too long. "
        "It may have died during initialization. Remove the segment with 'shm_tool remove <topic>' and retry");
  }

  // 既に初期化済みだがレイアウトが合わない。
  // 破壊的に作り直すと、稼働中の参加者のオフセットが無効になる（R01-F01）。
  // レイアウトを変えたいときは新しい世代を作ること（ShmTopic の役目）。
  throw std::runtime_error("shm::RingBuffer: the segment is already initialized with a different layout");
}

//! @brief 共有メモリ上のレイアウトが要求と一致するか確認する
bool
RingBuffer::hasCompatibleLayout(size_t element_size_arg, int buffer_num, size_t payload_alignment,
                                const TopicContract *contract) const
{
  if (header->magic != SHM_MAGIC || header->abi_major != ABI_MAJOR)
  {
    return false;
  }
  if (header->state.load(std::memory_order_acquire) != INITIALIZED)
  {
    return false;
  }
  if (!((header->element_capacity == element_size_arg) && (header->buf_num == static_cast<size_t>(buffer_num)) &&
        (header->payload_alignment == payload_alignment)))
  {
    return false;
  }
  // トピックの取り決めも一致していること（R02-F01）
  if (contract != nullptr)
  {
    TopicContract actual = getContract();
    if (!contract->matches(actual, nullptr))
    {
      return false;
    }
  }
  return true;
}

//! @brief リングバッファの実体を初期化する
//! @details 呼び出し側で state を INITIALIZING にしてから呼ぶこと。
void
RingBuffer::initializeContents(size_t element_size_arg, int buffer_num, size_t payload_alignment,
                               const TopicContract *contract, uint64_t own_generation, uint64_t own_nonce)
{
  size_t slot_offset = 0, data_offset = 0, total_size = 0;
  if (!computeLayout(element_size_arg, static_cast<size_t>(buffer_num), payload_alignment, slot_offset, data_offset,
                     total_size))
  {
    throw std::invalid_argument("shm::RingBuffer: layout computation overflowed");
  }

  // 自分の世代番号は呼び出し側が決める（R02-F06）。
  // 以前は「今の値 + 1」にしていたが、新しいセグメントはゼロ埋めなので
  // #2 でも #3 でも常に 1 になり、セグメント名の N と照合できなかった。
  const uint64_t generation = (own_generation != 0) ? own_generation : 1;

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
  header->generation        = generation;
  header->segment_nonce     = own_nonce;
  header->sequence.store(0, std::memory_order_relaxed);
  std::memset(header->reserved, 0, sizeof(header->reserved));

  // トピックの取り決め（R02-F01）
  header->payload_kind = static_cast<uint32_t>(contract != nullptr ? contract->kind : PayloadKind::Unknown);
  header->element_size = (contract != nullptr) ? contract->element_size : 0;
  header->schema_id      = (contract != nullptr) ? contract->schema_id : 0;
  header->schema_version = (contract != nullptr) ? contract->schema_version : 0;

  expected_element_size      = element_size_arg;
  expected_buf_num           = static_cast<size_t>(buffer_num);
  expected_payload_alignment = payload_alignment;
  expected_generation        = generation;
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
  // NOTE: PTHREAD_PRIO_INHERIT は**設定してはならない**（R05）。
  //       R04-F08 で優先度逆転を避けるために入れたが、robust + PI の mutex に
  //       待ち時間つきロックを掛けると、futex ワードに解決できない TID が
  //       入っている場合に glibc が abort する。詳細は lockSlotWithin() の
  //       コメントを参照。
  //       現在は trylock でしか取らないので優先度継承は働かない。
  //       待っている間は nanosleep で眠るため、保持者に CPU は回る。

  for (size_t i = 0; i < expected_buf_num; ++i)
  {
    SlotRecord *s = slot(static_cast<int>(i));
    pthread_mutex_init(&s->owner, &m_attr);
    s->payload_size.store(0, std::memory_order_relaxed);
    s->capture_monotonic_us.store(0, std::memory_order_relaxed);
    s->capture_realtime_us.store(0, std::memory_order_relaxed);
    s->sequence.store(0, std::memory_order_relaxed);
  }

  pthread_mutexattr_destroy(&m_attr);
}

//! @brief 共有メモリに記録されている topic contract
RingBuffer::TopicContract
RingBuffer::getContract() const
{
  TopicContract c;
  c.kind         = static_cast<PayloadKind>(header->payload_kind);
  c.element_size = header->element_size;
  c.schema_id      = header->schema_id;
  c.schema_version = header->schema_version;
  c.alignment      = header->payload_alignment;
  return c;
}

//! @brief 1 スロットの確保量
//! @details 接続時に検証したスナップショットを返す。共有ヘッダの live 値を
//!          読むと、接続後に他プロセスがヘッダを書き換えた場合に
//!          「検証済み」という前提が崩れる（R02-F07）。
size_t
RingBuffer::getElementSize() const
{
  return expected_element_size;
}

size_t
RingBuffer::getBufferNum() const
{
  return expected_buf_num;
}

size_t
RingBuffer::getPayloadAlignment() const
{
  return expected_payload_alignment;
}

unsigned char *
RingBuffer::getDataList()
{
  return data_list;
}

uint64_t
RingBuffer::getGeneration() const
{
  return expected_generation;
}

//! @brief 現在有効な世代とノンスを詰めたタグ
uint64_t
RingBuffer::getGenerationTag() const
{
  return header->latest_generation.load(std::memory_order_acquire);
}

//! @brief 世代タグを 1 回の CAS で進める
//! @details 世代とノンスを不可分に公開するための唯一の切り替え点。
bool
RingBuffer::tryAdvanceGenerationTag(uint64_t expected_tag, uint64_t desired_tag)
{
  return header->latest_generation.compare_exchange_strong(expected_tag, desired_tag, std::memory_order_acq_rel,
                                                           std::memory_order_acquire);
}

//! @brief このセグメント自身のノンス
uint64_t
RingBuffer::getSegmentNonce() const
{
  return header->segment_nonce;
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
  return s->capture_monotonic_us.load(std::memory_order_relaxed);
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
  return slot(buffer_num)->payload_size.load(std::memory_order_relaxed);
}

uint64_t
RingBuffer::getCaptureRealtime_us(int buffer_num) const
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return 0;
  }
  return slot(buffer_num)->capture_realtime_us.load(std::memory_order_relaxed);
}

//! @brief スロットの素性をまとめて取得する
//! @details 発行番号を先に読み、メタデータを読んでから発行番号を読み直す。
//!          途中で書き換わっていたら sequence を 0 にして「無効」と伝える。
SampleInfo
RingBuffer::getSampleInfo(int buffer_num) const
{
  SampleInfo info;
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return info;
  }
  const SlotRecord *s = slot(buffer_num);

  const uint64_t before = s->sequence.load(std::memory_order_acquire);
  if (before == 0)
  {
    return info;
  }
  info.capture_monotonic_us = s->capture_monotonic_us.load(std::memory_order_relaxed);
  info.capture_realtime_us  = s->capture_realtime_us.load(std::memory_order_relaxed);
  info.payload_size         = s->payload_size.load(std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_acquire);
  if (s->sequence.load(std::memory_order_acquire) != before)
  {
    return SampleInfo{};  // 読んでいる間に書き換わった
  }
  info.sequence = before;
  return info;
}

//! @brief 現在保持している範囲
//! @details 「任意の時刻を引ける」わけではない。リングに残っている分しか
//!          引けないことを呼び出し側が把握できるようにする。
RetentionWindow
RingBuffer::getRetentionWindow() const
{
  RetentionWindow window;
  for (size_t i = 0; i < expected_buf_num; i++)
  {
    const SampleInfo info = getSampleInfo(static_cast<int>(i));
    if (info.sequence == 0)
    {
      continue;
    }
    if (window.count == 0)
    {
      window.oldest_sequence     = info.sequence;
      window.newest_sequence     = info.sequence;
      window.oldest_monotonic_us = info.capture_monotonic_us;
      window.newest_monotonic_us = info.capture_monotonic_us;
      window.oldest_realtime_us  = info.capture_realtime_us;
      window.newest_realtime_us  = info.capture_realtime_us;
    }
    else
    {
      if (info.sequence < window.oldest_sequence)
      {
        window.oldest_sequence = info.sequence;
      }
      if (info.sequence > window.newest_sequence)
      {
        window.newest_sequence = info.sequence;
      }
      window.oldest_monotonic_us = std::min(window.oldest_monotonic_us, info.capture_monotonic_us);
      window.newest_monotonic_us = std::max(window.newest_monotonic_us, info.capture_monotonic_us);
      window.oldest_realtime_us  = std::min(window.oldest_realtime_us, info.capture_realtime_us);
      window.newest_realtime_us  = std::max(window.newest_realtime_us, info.capture_realtime_us);
    }
    ++window.count;
  }
  return window;
}

//! @brief 指定した時刻に対応するスロットを探す
//! @details 選択規則はヘッダの SearchPolicy のコメントに明記したとおり。
//!          等距離のタイブレークは「新しい方（発行番号が大きい方）」で固定する。
//!          発行番号は一意なので、この規則で結果は常に一意に決まる。
int
RingBuffer::findBufferNum(const TimeQuery &query, SearchStatus *status) const
{
  auto set_status = [status](SearchStatus value) {
    if (status != nullptr)
    {
      *status = value;
    }
  };

  int      best      = -1;
  uint64_t best_time = 0;
  uint64_t best_seq  = 0;
  size_t   valid     = 0;

  for (size_t i = 0; i < expected_buf_num; i++)
  {
    const SampleInfo info = getSampleInfo(static_cast<int>(i));
    if (info.sequence == 0)
    {
      continue;
    }
    ++valid;

    // 検索は monotonic のみ。壁時計は NTP で飛ぶため基準にしない。
    const uint64_t t = info.capture_monotonic_us;

    bool candidate = false;
    switch (query.policy)
    {
      case SearchPolicy::AtOrBefore:
        if (t > query.time_us)
        {
          continue;
        }
        // より新しい（目標に近い）ものを選ぶ
        candidate = (best < 0) || (t > best_time) || (t == best_time && info.sequence > best_seq);
        break;

      case SearchPolicy::AtOrAfter:
        if (t < query.time_us)
        {
          continue;
        }
        // より古い（目標に近い）ものを選ぶ
        candidate = (best < 0) || (t < best_time) || (t == best_time && info.sequence < best_seq);
        break;

      case SearchPolicy::Nearest:
      {
        const uint64_t d      = (t > query.time_us) ? (t - query.time_us) : (query.time_us - t);
        const uint64_t best_d = (best_time > query.time_us) ? (best_time - query.time_us)
                                                            : (query.time_us - best_time);
        // 等距離なら新しい方
        candidate = (best < 0) || (d < best_d) || (d == best_d && info.sequence > best_seq);
        break;
      }
    }

    if (candidate)
    {
      best      = static_cast<int>(i);
      best_time = t;
      best_seq  = info.sequence;
    }
  }

  if (valid == 0)
  {
    // 有効なスロットが1つも見えなかった。理由は2つあり、区別しないと
    // 呼び出し側が「データが無い」と「今は読めないだけ」を取り違える。
    //   - 一度も publish されていない            → Empty
    //   - 全スロットがたまたま書き込み中だった   → Contended（再試行の価値あり）
    // 面数が少ない（2 面など）リングを writer が全速で回していると後者は現実に起きる。
    //
    // 判定にはトピック全体の採番カウンタを使う。自セグメントの header->sequence を
    // 見ると、世代 2 以降では常に 0 なので必ず Empty になってしまう（R04-F07）。
    if (currentSequence() == 0)
    {
      set_status(SearchStatus::Empty);
    }
    else
    {
      set_status(SearchStatus::Contended);
    }
    return -1;
  }
  if (best < 0)
  {
    // 有効なサンプルはあるが、方針に合うものが無かった
    set_status(query.policy == SearchPolicy::AtOrBefore ? SearchStatus::TooOld : SearchStatus::TooNew);
    return -1;
  }

  set_status(SearchStatus::Success);
  return best;
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

  // 発行番号を読んでからメタデータを読むまでの間に、writer が同じスロットを
  // 再確保して書き換えることがある。読んだ値がその世代のものだったかを
  // 発行番号の再読みで確認する（seqlock と同じ考え方）。
  if (slot(newest)->sequence.load(std::memory_order_acquire) != newest_seq)
  {
    // 選んだスロットが書き換わった。次の呼び出しで読み直せばよい。
    // ここで -1 を返すと「データ無し」になってしまうので、
    // 他に有効なスロットがあればそれを使う。
    for (size_t i = 0; i < expected_buf_num; i++)
    {
      const uint64_t seq = slot(static_cast<int>(i))->sequence.load(std::memory_order_acquire);
      if (seq > 0 && seq != newest_seq)
      {
        newest     = static_cast<int>(i);
        newest_seq = seq;
        break;
      }
    }
  }

  const uint64_t stable_capture = slot(newest)->capture_monotonic_us.load(std::memory_order_relaxed);
  timestamp_us.store(stable_capture, std::memory_order_relaxed);

  // NOTE: ここで markAsRead(newest_seq) を呼んでいたが、外した（R05-L4）。
  //       この関数は「どのスロットが最新か」を**選ぶ**だけで、呼び出し側が
  //       そこから実際に読めたかどうかは知らない。読めなくても既読にしてしまうと、
  //       `isUpdated()` / `waitFor()` がその 1 更新を取りこぼす。
  //
  //       v1 では timestamp_us の更新がこの役目を兼ねており、外部の特殊化も
  //       その挙動に依存していたので意味を保っていた。しかし 5 つの特殊化を
  //       SubscriberCore に寄せた結果、**この関数を呼ぶ本番経路は
  //       SubscriberCore::readNewest() だけ**になり、その前提は消えた。
  //       既読にするのは「実際に読めた」ことを知っている側の仕事である。

  const uint64_t expiry_us = data_expiry_time_us.load(std::memory_order_relaxed);
  if (expiry_us == 0)
  {
    return newest;
  }

  const uint64_t current_time_us = getCurrentTimeUSec();
  if (current_time_us - stable_capture < expiry_us)
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
  return acquireSlot(buffer_num, SLOT_LOCK_TIMEOUT_US);
}

//! @brief 指定スロットを writer として確保する
//! @param [in] timeout_us ロックを待つ上限。0 なら待たずに諦める
bool
RingBuffer::acquireSlot(int buffer_num, uint64_t timeout_us)
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return false;
  }

  SlotRecord *s = slot(buffer_num);
  int         r = lockSlotWithin(&s->owner, timeout_us);

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
    return false;  // EBUSY: 生きている writer か reader が使用中
  }

  // 書き込み中は「有効なデータが無い」状態にしておく。
  // 途中で死んでも 0 のままなので、読み手には最初から見えない。
  //
  // NOTE: ここで capture 時刻や payload_size をクリアしてはならない。
  //       読み手は「発行番号を読む → メタデータを読む」の順で進むので、
  //       その間にクリアされると capture=0 を読んで期限切れと誤判定する
  //       （publish が続いているのに「データ無し」になる）。
  //       commitBuffer() が必ず全フィールドを書き直すため、クリアは不要。
  s->sequence.store(0, std::memory_order_release);
  setSlotOwned(buffer_num, true);
  std::atomic_thread_fence(std::memory_order_seq_cst);
  return true;
}

//! @brief 書き込めるスロットを 1 つ確保する
//! @details **古い順に全スロットを試す**のが要点（R04-F08）。
//!
//!          以前は getOldestBufferNum() が返す 1 つだけを 10 回試していた。
//!          R03-F04 で reader もスロットを排他するようになったため、
//!          時間検索型の Subscriber（最古スロットを読む）が張り付くと、
//!          他のスロットが空いていても publish が
//!          「Could not allocate a buffer (all buffers are in use)」で
//!          失敗するようになっていた。実測では buf_num=3 で reader が
//!          1 スロット保持するだけで発行が例外になった。
//!          メッセージも二重に誤りで、全バッファは使用中ではないし、
//!          buffer_num を増やしても直らない。
//!
//!          1 巡目は**待たずに** trylock するので、空きがあれば即座に見つかる。
//!          全部塞がっていたときだけ、最も古いものを短時間だけ待つ。
//! @return int 確保できたスロット番号。できなければ -1
int
RingBuffer::acquireWritableSlot()
{
  const size_t n = expected_buf_num;
  if (n == 0)
  {
    return -1;
  }

  // 試行済みの印。MAX_BUFFER_NUM は 1024 なのでスタックに収まる（ヒープ確保をしない）。
  uint64_t   tried[(MAX_BUFFER_NUM + 63) / 64] = { 0 };
  const auto mark     = [&](size_t i) { tried[i >> 6] |= (1ULL << (i & 63)); };
  const auto is_tried = [&](size_t i) { return ((tried[i >> 6] >> (i & 63)) & 1ULL) != 0; };

  for (size_t k = 0; k < n; ++k)
  {
    // 未試行のうち発行番号が最小のもの、すなわち最も古いもの
    size_t   best     = n;
    uint64_t best_seq = 0;
    for (size_t i = 0; i < n; ++i)
    {
      if (is_tried(i))
      {
        continue;
      }
      const uint64_t seq = slot(static_cast<int>(i))->sequence.load(std::memory_order_acquire);
      if (best == n || seq < best_seq)
      {
        best     = i;
        best_seq = seq;
      }
    }
    if (best == n)
    {
      break;
    }
    mark(best);
    if (acquireSlot(static_cast<int>(best), 0))
    {
      return static_cast<int>(best);
    }
  }

  // 全スロットが塞がっていた。最も古いものだけ、短時間待ってみる。
  const int oldest = getOldestBufferNum();
  if (acquireSlot(oldest, SLOT_LOCK_TIMEOUT_US))
  {
    return oldest;
  }
  return -1;
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

  s->payload_size.store(payload_size, std::memory_order_relaxed);
  s->capture_realtime_us.store(getCurrentRealtimeUSec(), std::memory_order_relaxed);
  s->capture_monotonic_us.store((capture_monotonic_us != 0) ? capture_monotonic_us : getCurrentTimeUSec(),
                                std::memory_order_relaxed);

  std::atomic<uint64_t> *counter = (sequence_source != nullptr) ? sequence_source : &header->sequence;
  const uint64_t         seq     = counter->fetch_add(1, std::memory_order_acq_rel) + 1;
  s->sequence.store(seq, std::memory_order_release);

  // allocateBuffer() を通さずに呼ばれた場合、この mutex は保持していない。
  // 保持していない mutex の unlock は未定義動作なので、必ず所有を確認する。
  if (ownsSlot(buffer_num))
  {
    setSlotOwned(buffer_num, false);
    pthread_mutex_unlock(&s->owner);
  }
}

uint64_t
RingBuffer::getSequenceCounter() const
{
  return currentSequence();
}

//! @brief このトピックで今までに採番された発行番号の最大値
//! @details 採番元は root のカウンタなので、**自セグメントの header->sequence を
//!          見てはならない**。世代 2 以降のセグメントの header->sequence は
//!          commit が sequence_source を使う以上、永久に 0 のままである。
//!          これを見誤ると「一度も publish されていない」と誤判定する（R04-F07）。
uint64_t
RingBuffer::currentSequence() const
{
  const std::atomic<uint64_t> *counter = (sequence_source != nullptr) ? sequence_source : &header->sequence;
  return counter->load(std::memory_order_acquire);
}

//! @brief 発行番号の採番元を差し替える
void
RingBuffer::setSequenceSource(std::atomic<uint64_t> *source)
{
  sequence_source = source;
}

//! @brief このリングのヘッダにある発行番号カウンタ
std::atomic<uint64_t> *
RingBuffer::sequenceCounter()
{
  return &header->sequence;
}

//! @brief スロットを排他して、payload と素性を 1 つのスナップショットとして読む
//! @details 宣言側のコメントを参照（R03-F03/F04）。
bool
RingBuffer::readSample(int buffer_num, void *dst, size_t dst_size, SampleInfo *info)
{
  if (buffer_num < 0 || static_cast<size_t>(buffer_num) >= expected_buf_num)
  {
    return false;
  }
  SlotRecord *s = slot(buffer_num);

  // スロットを排他してから読む。writer の memcpy と競合しなくなる。
  int r = lockSlotWithin(&s->owner, SLOT_LOCK_TIMEOUT_US);
  if (r == EOWNERDEAD)
  {
    // 前の所有者が死んだ。一貫性を宣言してロックを引き継ぐ。
    //
    // **ここでデータを捨ててはならない**（R04-F06）。R03-F04 で reader も
    // このロックを取るようになったため、EOWNERDEAD は「writer が書き込み中に
    // 死んだ」を意味しなくなった。単に読んでいた reader が死んだだけかもしれない。
    // 以前はここで sequence を 0 にしていたので、subscriber ノードが落ちた瞬間に
    // publish 済みの最新センサ値が消えていた。
    //
    // writer が書き込み中に死んだ場合は allocateBuffer() が既に sequence を 0 に
    // しているので、下の `sequence == 0` 判定がそのまま拾う。
    pthread_mutex_consistent(&s->owner);
    r = 0;
  }
  if (r != 0)
  {
    return false;  // 書き込み中。呼び出し側が再試行する
  }

  // ロックを保持している間はスロットが書き換わらないので、
  // 発行番号の前後比較は不要になる。
  const uint64_t sequence = s->sequence.load(std::memory_order_acquire);
  if (sequence == 0)
  {
    pthread_mutex_unlock(&s->owner);
    return false;  // 有効なデータが無い
  }

  const size_t payload_size = static_cast<size_t>(s->payload_size.load(std::memory_order_relaxed));
  if (payload_size > expected_element_size || payload_size > dst_size)
  {
    // メタデータの破損か、読み手の受け皿が足りない。
    // このまま memcpy すると範囲外アクセスになる。
    pthread_mutex_unlock(&s->owner);
    return false;
  }

  if (payload_size > 0 && dst != nullptr)
  {
    std::memcpy(dst, data_list + static_cast<size_t>(buffer_num) * expected_element_size, payload_size);
  }

  if (info != nullptr)
  {
    info->sequence             = sequence;
    info->capture_monotonic_us = s->capture_monotonic_us.load(std::memory_order_relaxed);
    info->capture_realtime_us  = s->capture_realtime_us.load(std::memory_order_relaxed);
    info->payload_size         = payload_size;
  }

  pthread_mutex_unlock(&s->owner);
  markAsRead(sequence);
  return true;
}

//! @brief 別のリングから取り出したサンプルを素性ごと取り込む
//! @details 世代を切り替えるときに履歴を引き継ぐために使う。
//!          発行番号を維持するのが要点で、これを新しく採番し直すと
//!          「発行番号は再利用しない」という前提が崩れ、seqlock の検証や
//!          タイムマシンの順序付けが壊れる。
bool
RingBuffer::adoptSample(const SampleInfo &info, const void *payload, size_t bytes)
{
  if (info.sequence == 0 || bytes > expected_element_size)
  {
    return false;
  }

  int target = -1;
  for (size_t i = 0; i < expected_buf_num; ++i)
  {
    if (slot(static_cast<int>(i))->sequence.load(std::memory_order_acquire) != 0)
    {
      continue;
    }
    if (allocateBuffer(static_cast<int>(i)))
    {
      target = static_cast<int>(i);
      break;
    }
  }
  if (target < 0)
  {
    return false;
  }

  SlotRecord *s = slot(target);
  if (bytes > 0 && payload != nullptr)
  {
    std::memcpy(data_list + static_cast<size_t>(target) * expected_element_size, payload, bytes);
  }
  s->payload_size.store(bytes, std::memory_order_relaxed);
  s->capture_monotonic_us.store(info.capture_monotonic_us, std::memory_order_relaxed);
  s->capture_realtime_us.store(info.capture_realtime_us, std::memory_order_relaxed);

  // 採番元は root と共有しているので、引き継いだ番号がそのまま一意である。
  // 念のためカウンタが取り込んだ番号より小さければ進めておく。
  std::atomic<uint64_t> *counter = (sequence_source != nullptr) ? sequence_source : &header->sequence;
  uint64_t               current = counter->load(std::memory_order_acquire);
  while (current < info.sequence &&
         !counter->compare_exchange_weak(current, info.sequence, std::memory_order_acq_rel,
                                         std::memory_order_acquire))
  {
  }

  s->sequence.store(info.sequence, std::memory_order_release);
  setSlotOwned(target, false);
  pthread_mutex_unlock(&s->owner);
  return true;
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
  commitBuffer(buffer_num, expected_element_size, input_time_us);
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
  if (header->generation != expected_generation)
  {
    return true;
  }
  return (header->element_capacity != expected_element_size) || (header->buf_num != expected_buf_num) ||
         (header->payload_alignment != expected_payload_alignment);
}

//! @brief 検証つきで既存の共有メモリへ接続する
//! @details 宣言側のコメントを参照（R01-F06）
std::unique_ptr<RingBuffer>
attachRingBuffer(SharedMemory &memory, std::string *reason, const RingBuffer::TopicContract *expected,
                 uint64_t expected_generation)
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

  if (!RingBuffer::validateLayout(ptr, memory.getSize(), reason, expected, expected_generation))
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
