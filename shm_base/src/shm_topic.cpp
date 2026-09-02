//!
//! @file shm_topic.cpp
//! @brief レイアウト世代の切り替えを引き受ける ShmTopic の実装
//!

#include <shm_base.hpp>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <vector>
#include <dirent.h>
#include <thread>
#include <unistd.h>

namespace irlab
{

namespace shm
{

//! @brief 世代タグに対応するセグメント名
//! @details 世代 1 はトピック名そのもの。ディレクトリを兼ねるため、
//!          レイアウト変更が起きないトピックでは従来と全く同じ構成になる。
//!
//!          世代 2 以降には **ノンスを名前に含める**（R03-F03）。
//!          固定名 "#N" だと、作成途中で死んだプロセスの残骸が名前を占有し、
//!          以後 O_EXCL が必ず失敗して容量拡張ができなくなる。
//!          かといって「一定時間待って未初期化なら消す」方式は、単に遅い／
//!          停止させられているだけの生きた作成者を消してしまう危険がある。
//!          名前にノンスを入れれば名前の取り合い自体が起きないので、
//!          時間で生死を判定する必要がなくなる。
std::string
ShmTopic::generationName(const std::string &name, uint64_t tag)
{
  const uint64_t generation = unpackGeneration(tag);
  if (generation <= 1)
  {
    return name;
  }
  std::ostringstream oss;
  oss << name << "#" << generation << "-" << std::hex << std::setw(12) << std::setfill('0') << unpackNonce(tag);
  return oss.str();
}

namespace
{
//! @brief 世代セグメント名に使う 48bit のノンスを作る
//! @details 衝突しても O_EXCL が弾いて作り直すだけなので、暗号強度は要らない。
uint64_t
makeSegmentNonce()
{
  static std::atomic<uint64_t> counter{0};
  std::random_device           rd;
  uint64_t                     v = (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd());
  v ^= static_cast<uint64_t>(::getpid()) << 17;
  v ^= static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  v ^= counter.fetch_add(0x9E3779B97F4A7C15ULL, std::memory_order_relaxed);
  v &= 0x0000FFFFFFFFFFFFULL;
  return (v == 0) ? 1 : v;  // 0 は「ノンス無し（世代 1）」の意味に使う
}

//! @brief セグメント名から世代タグを取り出す
//! @details "<base>#<gen>-<nonce16進>" のときだけ真を返す。
bool
parseGenerationSuffix(const std::string &entry, const std::string &prefix, uint64_t *tag)
{
  if (entry.size() <= prefix.size() + 1 || entry.compare(0, prefix.size(), prefix) != 0 || entry[prefix.size()] != '#')
  {
    return false;
  }
  const std::string suffix = entry.substr(prefix.size() + 1);
  const size_t      dash   = suffix.find('-');
  if (dash == std::string::npos || dash == 0 || dash + 1 >= suffix.size())
  {
    return false;
  }
  const std::string generation_text = suffix.substr(0, dash);
  const std::string nonce_text       = suffix.substr(dash + 1);

  // std::stoull は末尾のゴミを黙って無視するので、全部使い切ったことを確かめる。
  // これをしないと "3-abcXYZ" のような名前を正当な世代名とみなしてしまう（R04-F19）。
  auto all_digits = [](const std::string &text, int base) {
    if (text.empty())
    {
      return false;
    }
    for (char c : text)
    {
      const bool decimal = (c >= '0' && c <= '9');
      const bool hex     = decimal || (c >= 'a' && c <= 'f');
      if (!(base == 16 ? hex : decimal))
      {
        return false;
      }
    }
    return true;
  };
  if (!all_digits(generation_text, 10) || !all_digits(nonce_text, 16))
  {
    return false;
  }

  try
  {
    const uint64_t generation = std::stoull(generation_text, nullptr, 10);
    const uint64_t nonce      = std::stoull(nonce_text, nullptr, 16);
    if (generation < 2 || generation > MAX_GENERATION || nonce > 0x0000FFFFFFFFFFFFULL)
    {
      return false;
    }
    *tag = packGeneration(generation, nonce);
  }
  catch (const std::exception &)
  {
    return false;
  }
  return true;
}
}  // namespace

//! @brief トピックに属する全世代のセグメントを削除する
//! @return int 1つでも削除できたら 0、何も消せなければ -1
int
ShmTopic::removeAllGenerations(const std::string &name)
{
  validateTopicName(name, "shm::ShmTopic::removeAllGenerations()");

  std::string base = name;
  if (!base.empty() && base[0] == '/')
  {
    base.erase(0, 1);
  }
  // トピック名の '/' は共有メモリ名では '_' になる（toShmPath と同じ規則）
  for (char &c : base)
  {
    if (c == '/')
    {
      c = '_';
    }
  }
  const std::string prefix = "shm_" + base;

  int result = disconnectMemory(name);

  // 世代付きセグメント（prefix + "#<世代>-<ノンス>"）を列挙して消す。
  // 名前を知らないと消せないので /dev/shm を走査する。
  DIR *dir = opendir("/dev/shm");
  if (dir == nullptr)
  {
    return result;
  }
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr)
  {
    uint64_t tag = 0;
    // 書式に合致するものだけを消す（別トピックの誤削除を防ぐ）
    if (!parseGenerationSuffix(entry->d_name, prefix, &tag))
    {
      continue;
    }
    if (shm_unlink((std::string("/") + entry->d_name).c_str()) == 0)
    {
      result = 0;
    }
  }
  closedir(dir);
  return result;
}

ShmTopic::ShmTopic(std::string name, PERM perm, bool create)
  : name_(std::move(name))
  , perm_(perm)
  , root_(nullptr)
  , root_ring_(nullptr)
  , data_(nullptr)
  , ring_(nullptr)
  , current_tag_(0)
{
  validateTopicName(name_, "shm::ShmTopic()");
  (void)create;
}

ShmTopic::~ShmTopic()
{
  // マッピングを手放す前にスロットの robust mutex を解放する。
  // 握ったまま munmap すると、スレッドの robust list が解放済み領域を指し、
  // **後続の無関係な pthread_mutex_trylock が SIGSEGV する**（R04）。
  if (ring_ != nullptr)
  {
    ring_->releaseOwnedSlots();
  }
}

//! @brief 容量を増やす際の刻み
//! @details 要求のたびにぴったり作り直すと、長さが 1 要素ずつ揺れるだけで
//!          世代が回り続ける。増やすときは余裕を持たせ、減っても縮めない。
size_t
ShmTopic::growCapacity(size_t current, size_t required, size_t alignment)
{
  size_t target = std::max(current, required);
  if (target > current)
  {
    // 25% の余裕を足す（次に少し伸びても作り直さずに済む）
    const size_t headroom = target / 4;
    if (target <= RingBuffer::MAX_ELEMENT_SIZE - headroom)
    {
      target += headroom;
    }
  }
  // capacity は payload_alignment の倍数でなければならない
  if (alignment > 1)
  {
    const size_t rem = target % alignment;
    if (rem != 0)
    {
      target += alignment - rem;
    }
  }
  return std::min(target, RingBuffer::MAX_ELEMENT_SIZE);
}

//! @brief 世代 1（ディレクトリ兼データ）を開く
//! @details **既に有効な世代 1 があれば決して初期化し直さない。**
//!          ここで要求レイアウトに合わせて作り直すと、稼働中のセグメントを
//!          破壊的に再レイアウトすることになり、P3 で無くしたはずの
//!          TOCTOU がそのまま復活する。レイアウトを変えたいときは
//!          必ず新しい世代を作ること。
bool
ShmTopic::openRoot(bool create, size_t initial_capacity, int buf_num, size_t payload_alignment,
                   const RingBuffer::TopicContract *contract)
{
  if (root_ != nullptr && root_ring_ != nullptr && !root_->isDisconnected())
  {
    return true;
  }

  // ここから root を張り直す。**その前に root のマッピングに依存するものを
  // 全て捨てなければならない**（R04-F01）。
  //
  //   - 世代 1 の ring_ は root と同じマッピングを指している
  //   - 全世代の ring_ の sequence_source は root のヘッダ内を指している
  //   - root_ring_ も同様
  //
  // 下の attach_existing() が成功すると `root_ = std::move(existing)` で
  // 古い SharedMemoryPosix が munmap される。ここで捨てておかないと、
  // 直後に follow()/ensureCapacity() が ring_->isLayoutChanged() を呼んで
  // 解放済み領域を読み、SIGSEGV する。
  //
  // 引き金は「root セグメントが unlink され作り直される」ことで、
  // 稼働中の Publisher/Subscriber の傍らで `shm_tool remove <topic>` を
  // 実行する、あるいは Publisher プロセスが再起動する、という運用で普通に起きる。
  //
  // current_tag_ を 0 にしておけば、follow()/ensureCapacity() は必ず
  // attachGeneration() からやり直すので、sequence_source も張り直される。
  // robust mutex を握ったまま munmap すると後続の trylock が落ちるので、
  // マッピングを手放す前に必ず解放する（R04）。
  if (ring_ != nullptr)
  {
    ring_->releaseOwnedSlots();
  }
  ring_.reset();
  data_.reset();
  root_ring_.reset();
  current_tag_ = 0;

  // 待っても直らない失敗を掴んだかどうか。掴んだら再試行ループを即座に打ち切る。
  bool incompatible = false;

  auto attach_existing = [&]() -> bool {
    auto existing = std::make_unique<SharedMemoryPosix>(name_, O_RDWR, static_cast<PERM>(0));
    if (!existing->connect())
    {
      return false;
    }
    std::string reason;
    // 世代 1 なので expected_generation は 1。contract も照合する（R02-F01/F06）。
    const auto verdict =
        RingBuffer::inspectLayout(existing->getPtr(), existing->getSize(), &reason, contract, 1, name_);
    if (verdict != RingBuffer::LayoutVerdict::Usable)
    {
      last_error_ = "root segment is not usable: " + reason;
      // ABI や型が違うなら、待っても絶対に解決しない。
      // ここで区別しないと publish のたびに秒単位で待たされる（R04-F13）。
      incompatible = (verdict == RingBuffer::LayoutVerdict::Incompatible);
      return false;
    }
    try
    {
      root_ring_ = std::make_unique<RingBuffer>(existing->getPtr());
    }
    catch (const std::exception &e)
    {
      last_error_ = std::string("cannot attach the root segment: ") + e.what();
      return false;
    }
    root_ = std::move(existing);
    return true;
  };

  if (attach_existing())
  {
    return true;
  }
  if (incompatible)
  {
    // 作り直しても O_EXCL で弾かれるだけなので、ここで諦める
    return false;
  }
  if (!create)
  {
    if (last_error_.empty())
    {
      last_error_ = "the topic does not exist yet";
    }
    return false;
  }

  // --- 世代 1 の新規作成 ---
  size_t capacity = initial_capacity;
  if (payload_alignment > 1 && (capacity % payload_alignment) != 0)
  {
    capacity += payload_alignment - (capacity % payload_alignment);
  }
  const int    slots = (buf_num > 0) ? buf_num : 1;
  const size_t size  = RingBuffer::getSize(capacity, slots, payload_alignment);

  // 作成者を一者に絞る（R02-F02）。
  // O_CREAT だけだと、同時に「無い」と判断した複数の Publisher がそれぞれ
  // 初期化へ進み、同じ pthread_mutex_t を並行して初期化し得た。
  // O_EXCL に成功した一者だけが初期化し、負けた側は完成を待って接続する。
  {
    SharedMemoryPosix creator(name_, O_RDWR | O_CREAT | O_EXCL, perm_);
    if (creator.connect(size))
    {
      try
      {
        RingBuffer initializer(creator.getPtr(), capacity, slots, payload_alignment, contract, 1);
        initializer.tryAdvanceGenerationTag(0, packGeneration(1, 0));
      }
      catch (const std::exception &e)
      {
        last_error_ = std::string("cannot initialize the root segment: ") + e.what();
        creator.disconnectAndUnlink();
        return false;
      }
    }
  }

  // 自分が作った場合も、負けた場合も、ここで改めて接続する。
  // 負けた場合は相手の初期化完了を待つ。
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
  while (true)
  {
    if (attach_existing())
    {
      return true;
    }
    if (incompatible)
    {
      // 待っても直らない。すぐ返す（R04-F13）
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline)
    {
      if (last_error_.empty())
      {
        last_error_ = "cannot open the root segment";
      }
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

//! @brief 指定世代のセグメントへ接続する
bool
ShmTopic::attachGeneration(uint64_t tag, const RingBuffer::TopicContract *expected)
{
  const uint64_t generation = unpackGeneration(tag);

  // 発行番号は root のカウンタで一元採番する（R03-F01）。
  // 世代ごとのカウンタだと、切り替えの前後で同じ番号が二度出得る。
  auto bind_sequence_source = [&](RingBuffer &rb) {
    if (root_ring_ != nullptr)
    {
      rb.setSequenceSource(root_ring_->sequenceCounter());
    }
  };

  // 差し替える前に、今の ring_ が握っているスロットを解放する（R04）
  if (ring_ != nullptr)
  {
    ring_->releaseOwnedSlots();
  }

  if (generation <= 1)
  {
    // 世代 1 は root_ そのもの
    data_.reset();
    std::string reason;
    ring_ = attachRingBuffer(*root_, &reason, expected, 1);
    if (ring_ == nullptr)
    {
      last_error_ = "cannot attach generation 1: " + reason;
      return false;
    }
    bind_sequence_source(*ring_);
    current_tag_ = packGeneration(1, 0);
    return true;
  }

  auto seg = std::make_unique<SharedMemoryPosix>(generationName(name_, tag), O_RDWR, static_cast<PERM>(0));
  if (!seg->connect())
  {
    last_error_ = "generation " + std::to_string(generation) + " does not exist";
    return false;
  }

  // 作成者が初期化を終えるのを待つ（作成と初期化の間に他者が覗く可能性がある）。
  // ここで待ち切れなくても、そのセグメントを消したりはしない。
  // root の世代タグは初期化が完全に終わってから公開されるので、
  // タグに現れているセグメントは必ず初期化済みか、まさに公開直後である。
  if (!RingBuffer::waitForInitialization(seg->getPtr(), INIT_WAIT_TIMEOUT_US))
  {
    last_error_ = "generation " + std::to_string(generation) + " is not initialized";
    return false;
  }

  std::string reason;
  // セグメント名の世代番号とヘッダの generation が一致することも確認する（R02-F06）
  auto rb = attachRingBuffer(*seg, &reason, expected, generation);
  if (rb == nullptr)
  {
    last_error_ = "cannot attach generation " + std::to_string(generation) + ": " + reason;
    return false;
  }
  // ノンスも照合する。名前だけの一致で信用しない（R03-F03）。
  if (rb->getSegmentNonce() != unpackNonce(tag))
  {
    last_error_ = "generation " + std::to_string(generation) + " has a mismatched nonce";
    return false;
  }

  bind_sequence_source(*rb);
  data_        = std::move(seg);
  ring_        = std::move(rb);
  current_tag_ = tag;
  return true;
}

//! @brief 現役でないと確実に言える世代セグメントを削除する
//! @details 名前を消してもマッピングは生き続けるので、旧世代を掴んだままの
//!          参加者は安全に読み書きを続けられる。放置すると段階的な容量拡張で
//!          /dev/shm を食い潰すため、切り替え時に片付ける（R02-F06）。
//!
//!          消してよいのは次の 2 種類だけで、どちらも**時間を見ずに**判定できる
//!          （R03-F03）:
//!            - 現世代より古い世代 … 既に root のタグが先へ進んでいる
//!            - 現世代と同じ番号でノンスが違うもの … 切り替え競争に負けた残骸
//!          これより新しい世代番号のセグメントは「今まさに作成中」かもしれない
//!          ので絶対に触らない。世代 1 は root（ディレクトリ兼用）なので消さない。
void
ShmTopic::unlinkStaleGenerations(uint64_t live_tag)
{
  const uint64_t live_generation = unpackGeneration(live_tag);
  const uint64_t live_nonce      = unpackNonce(live_tag);

  std::string base = name_;
  if (!base.empty() && base[0] == '/')
  {
    base.erase(0, 1);
  }
  for (char &c : base)
  {
    if (c == '/')
    {
      c = '_';
    }
  }
  const std::string prefix = "shm_" + base;

  DIR *dir = opendir("/dev/shm");
  if (dir == nullptr)
  {
    return;
  }
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr)
  {
    uint64_t tag = 0;
    if (!parseGenerationSuffix(entry->d_name, prefix, &tag))
    {
      continue;
    }
    const uint64_t generation = unpackGeneration(tag);
    const bool     is_stale =
        (generation < live_generation) || (generation == live_generation && unpackNonce(tag) != live_nonce);
    if (!is_stale)
    {
      continue;
    }
    shm_unlink((std::string("/") + entry->d_name).c_str());
  }
  closedir(dir);
}

//! @brief 次の世代のセグメントを作って公開する
//! @details セグメント名にノンスを含めるので名前の取り合いは起きない。
//!          完全に初期化してから root の世代タグを 1 回の CAS で進める。
//!          CAS に負けた側は自分の作ったセグメントを消して勝者へ合流する。
bool
ShmTopic::createNextGeneration(uint64_t from_tag, size_t capacity, int buf_num, size_t payload_alignment,
                               const RingBuffer::TopicContract &contract)
{
  const uint64_t next_generation = unpackGeneration(from_tag) + 1;
  if (next_generation > MAX_GENERATION)
  {
    last_error_ = "the topic has exhausted its layout generations";
    return false;
  }
  const uint64_t    next_tag  = packGeneration(next_generation, makeSegmentNonce());
  const std::string next_name = generationName(name_, next_tag);

  // capacity は payload_alignment の倍数でなければならない。
  if (payload_alignment > 1 && (capacity % payload_alignment) != 0)
  {
    capacity += payload_alignment - (capacity % payload_alignment);
  }
  const size_t size = RingBuffer::getSize(capacity, buf_num, payload_alignment);

  SharedMemoryPosix probe(next_name, O_RDWR | O_CREAT | O_EXCL, perm_);
  if (!probe.connect(size))
  {
    // ノンス付きの名前なので EEXIST はまず起きない（起きてもノンスを取り直せばよい）。
    last_error_ = "cannot create generation " + std::to_string(next_generation) + ": " + std::strerror(errno);
    return false;
  }

  try
  {
    // 自分の世代番号とノンスを刻んで初期化する（R02-F06 / R03-F03）
    RingBuffer initializer(probe.getPtr(), capacity, buf_num, payload_alignment, &contract, next_generation,
                           unpackNonce(next_tag));
    // 発行番号は root のカウンタで一元採番する（R03-F01）
    if (root_ring_ != nullptr)
    {
      initializer.setSequenceSource(root_ring_->sequenceCounter());
    }

    // 旧世代の履歴を引き継ぐ。
    // 引き継がないと、レイアウトが変わった瞬間に過去のデータが全部消える。
    // まだ公開していないセグメントなので、ここで書いても他プロセスには見えない。
    if (ring_ != nullptr)
    {
      migrateHistory(*ring_, initializer);
    }
    // probe を切る前に、initializer が握ったままのスロットを解放する（R04）
    initializer.releaseOwnedSlots();
  }
  catch (const std::exception &e)
  {
    last_error_ = std::string("cannot initialize the new generation: ") + e.what();
    probe.disconnectAndUnlink();
    return false;
  }

  // 完全に初期化してから「現在有効な世代」を進める。
  // 世代番号とノンスは 1 語なので、この 1 回の CAS で不可分に公開される。
  if (!root_ring_->tryAdvanceGenerationTag(from_tag, next_tag))
  {
    // 誰かが先に進めた。自分が作ったセグメントは不要なので消して合流する。
    probe.disconnectAndUnlink();
    last_error_ = "another process advanced the generation first";
    return false;
  }

  probe.disconnect();

  // 旧世代のマッピングは、スロットのロックを解放してから手放す。
  auto old_segment = std::move(data_);
  auto old_ring    = std::move(ring_);

  if (!attachGeneration(next_tag, &contract))
  {
    return false;
  }

  // NOTE: ここで旧世代を「もう一度さらって取りこぼしを拾う」処理を持っていたが、
  //       R04-F11/F12 により削除した。理由は 2 つある。
  //
  //       1. **定常状態では原理的に 1 件も拾えない。**
  //          adoptSample() は発行番号 0 の空きスロットにしか書けないが、
  //          新世代のスロット数は max(旧, 要求) なので、初回移行が済んだ時点で
  //          埋まっている。それでも「拾った最大の発行番号」だけは進めていたので、
  //          取りこぼしが静かに確定していた。
  //
  //       2. **そもそも重複している。** 拾うはずだったのは「移行のスナップショットと
  //          CAS の間に旧世代へ commit された分」だが、その writer 自身が
  //          publish 後の世代確認で気づいて新世代へ発行し直す。CAS はその確認より
  //          前に完了しているためである。
  //
  //       むしろ両方が成立すると、同じ測定値が 2 つの発行番号・2 つの時刻で
  //       二重に現れる（R04-F12）。履歴とタイムマシンから見ると「同じ測定が
  //       別時刻に 2 回起きた」ように見えるので、有害ですらあった。
  if (old_ring != nullptr)
  {
    old_ring->releaseOwnedSlots();
  }
  old_ring.reset();
  old_segment.reset();

  // 切り替えが済んだので、もう誰も新規に接続しない世代を片付ける（R02-F06 / R03-F03）
  unlinkStaleGenerations(next_tag);
  return true;
}

//! @brief 旧世代の有効なサンプルを新世代へ引き継ぐ
//! @details 発行番号の小さい順に取り込むことで、リング上の新旧関係を保つ。
//!          読み出しはスロットを排他して行うので、コピー中に上書きされることはない。
//!          引き継がないと、レイアウトが変わった瞬間に過去のデータが全部消える。
void
ShmTopic::migrateHistory(RingBuffer &source, RingBuffer &destination)
{
  const size_t slots         = source.getBufferNum();
  const size_t slot_capacity = source.getElementSize();

  // 発行番号の小さい順に並べる
  std::vector<std::pair<uint64_t, int>> ordered;
  ordered.reserve(slots);
  for (size_t i = 0; i < slots; ++i)
  {
    const uint64_t sequence = source.getSequence(static_cast<int>(i));
    if (sequence != 0)
    {
      ordered.emplace_back(sequence, static_cast<int>(i));
    }
  }
  std::sort(ordered.begin(), ordered.end());

  std::vector<unsigned char> scratch(slot_capacity);
  for (const auto &candidate : ordered)
  {
    SampleInfo info;
    // スロットを排他して payload と素性を 1 回で読む（R03-F04）
    if (!source.readSample(candidate.second, scratch.data(), scratch.size(), &info))
    {
      continue;  // 書き込み中か、既に上書きされた
    }
    // 発行番号と capture 時刻をそのまま引き継ぐ。新しく採り直すと、
    // 同じ測定が別時刻に起きたように見えてしまう。
    destination.adoptSample(info, scratch.data(), info.payload_size);
  }
}

//! @brief 接続世代がまだ有効か
//! @details publish のコミット後に世代が切り替わっていないかを確認するために使う（R02-F05）
bool
ShmTopic::isGeneration(uint64_t tag) const
{
  if (root_ring_ == nullptr)
  {
    return false;
  }
  const uint64_t latest = root_ring_->getGenerationTag();
  return latest == tag && current_tag_ == tag;
}

bool
ShmTopic::follow(const RingBuffer::TopicContract *expected)
{
  if (!openRoot(false, 0, 0, 1, expected))
  {
    return false;
  }

  const uint64_t latest = root_ring_->getGenerationTag();
  if (unpackGeneration(latest) == 0)
  {
    last_error_ = "the topic has no published generation yet";
    return false;
  }

  if (ring_ != nullptr && current_tag_ == latest && !ring_->isLayoutChanged())
  {
    return true;
  }
  return attachGeneration(latest, expected);
}

bool
ShmTopic::ensureCapacity(size_t required_capacity, int buf_num, size_t payload_alignment,
                         const RingBuffer::TopicContract &contract)
{
  const size_t alignment = payload_alignment;
  size_t       want      = required_capacity;
  if (alignment > 1 && (want % alignment) != 0)
  {
    if (want > RingBuffer::MAX_ELEMENT_SIZE - alignment)
    {
      last_error_ = "the requested payload size overflows when aligned";
      return false;
    }
    want += alignment - (want % alignment);
  }

  // 1 スロットの上限を超える要求は、どれだけ世代を進めても満たせない。
  // ここで断らないと growCapacity() が上限で clamp した容量のまま
  // 成功を返し、呼び出し側が要求どおりの長さを memcpy してスロットの外へ
  // 書き込む（R04-F02）。
  if (want > RingBuffer::MAX_ELEMENT_SIZE)
  {
    last_error_ = "the requested payload size (" + std::to_string(want) + " bytes) exceeds the per-slot maximum (" +
                  std::to_string(RingBuffer::MAX_ELEMENT_SIZE) + " bytes)";
    return false;
  }

  if (!openRoot(true, want, buf_num, alignment, &contract))
  {
    return false;
  }

  for (int attempt = 0; attempt < MAX_GENERATION_ATTEMPTS; ++attempt)
  {
    uint64_t latest = root_ring_->getGenerationTag();
    if (unpackGeneration(latest) == 0)
    {
      latest = packGeneration(1, 0);
    }

    if (ring_ == nullptr || current_tag_ != latest || ring_->isLayoutChanged())
    {
      if (!attachGeneration(latest, &contract))
      {
        // 作成中でまだ読めない場合がある。少し待って見直す。
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
    }

    // 現世代が要求を「満たしている」かを見る。一致ではなく包含で判定するのが要点。
    //
    // 一致を求めると、buf_num の違う Publisher が同じトピックに繋いだときに
    // 互いに相手のレイアウトを作り直し合って世代が往復し、いつまでも収束しない。
    // スロットが多い分には誰も困らない（履歴が長くなるだけで、むしろ
    // 「buf_num は同時 Publisher 数より大きいこと」という制約にも有利）ので、
    // 容量・スロット数・アライメントのいずれも「増やすだけ」で運用する。
    // これにより、要求が食い違っても必ず最大値へ収束する。
    if (ring_->getElementSize() >= want && ring_->getBufferNum() >= static_cast<size_t>(buf_num) &&
        (ring_->getPayloadAlignment() % alignment) == 0)
    {
      return true;
    }

    // 足りない → 次の世代を作る。いずれの値も減らさない。
    const size_t new_capacity  = growCapacity(ring_->getElementSize(), want, alignment);
    const int    new_buf_num   = static_cast<int>(std::max(ring_->getBufferNum(), static_cast<size_t>(buf_num)));
    const size_t new_alignment = std::max(ring_->getPayloadAlignment(), alignment);
    if (!createNextGeneration(latest, new_capacity, new_buf_num, new_alignment, contract))
    {
      // 競合に負けた／作成中だった。勝者の世代を見に行く。
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }

    // 世代は進んだが、それで要求を満たせたとは限らない。
    // 満たしていなければループ先頭の判定へ戻して確かめ直す。
    // **決して「世代を作れた」だけで成功を返さないこと**（R04-F02）。
  }

  last_error_ = "could not settle on a layout generation after " + std::to_string(MAX_GENERATION_ATTEMPTS) +
                " attempts; publishers on this topic are probably requesting incompatible layouts";
  return false;
}

}  // namespace shm

}  // namespace irlab
