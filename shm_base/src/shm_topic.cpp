//!
//! @file shm_topic.cpp
//! @brief レイアウト世代の切り替えを引き受ける ShmTopic の実装
//!

#include <shm_base.hpp>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <vector>
#include <dirent.h>
#include <thread>

namespace irlab
{

namespace shm
{

//! @brief 世代 N のセグメント名
//! @details 世代 1 はトピック名そのもの。ディレクトリを兼ねるため、
//!          レイアウト変更が起きないトピックでは従来と全く同じ構成になる。
std::string
ShmTopic::generationName(const std::string &name, uint64_t generation)
{
  if (generation <= 1)
  {
    return name;
  }
  return name + "#" + std::to_string(generation);
}

//! @brief トピックに属する全世代のセグメントを削除する
//! @return int 1つでも削除できたら 0、何も消せなければ -1
int
ShmTopic::removeAllGenerations(const std::string &name)
{
  validateShmName(name, "shm::ShmTopic::removeAllGenerations()");

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

  // 世代付きセグメント（prefix + "#<数字>"）を列挙して消す。
  // 名前を知らないと消せないので /dev/shm を走査する。
  DIR *dir = opendir("/dev/shm");
  if (dir == nullptr)
  {
    return result;
  }
  struct dirent *entry = nullptr;
  while ((entry = readdir(dir)) != nullptr)
  {
    const std::string entry_name = entry->d_name;
    if (entry_name.size() <= prefix.size() + 1 || entry_name.compare(0, prefix.size(), prefix) != 0 ||
        entry_name[prefix.size()] != '#')
    {
      continue;
    }
    // '#' の後ろが数字だけであることを確認する（別トピックの誤削除を防ぐ）
    bool all_digits = true;
    for (size_t i = prefix.size() + 1; i < entry_name.size(); ++i)
    {
      if (entry_name[i] < '0' || entry_name[i] > '9')
      {
        all_digits = false;
        break;
      }
    }
    if (!all_digits)
    {
      continue;
    }
    if (shm_unlink(("/" + entry_name).c_str()) == 0)
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
  , current_generation_(0)
{
  validateShmName(name_, "shm::ShmTopic()");
  (void)create;
}

ShmTopic::~ShmTopic() = default;

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
  root_ring_.reset();

  auto attach_existing = [&]() -> bool {
    auto existing = std::make_unique<SharedMemoryPosix>(name_, O_RDWR, static_cast<PERM>(0));
    if (!existing->connect())
    {
      return false;
    }
    std::string reason;
    // 世代 1 なので expected_generation は 1。contract も照合する（R02-F01/F06）。
    if (!RingBuffer::validateLayout(existing->getPtr(), existing->getSize(), &reason, contract, 1))
    {
      last_error_ = "root segment is not usable: " + reason;
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
        initializer.tryAdvanceLatestGeneration(0, 1);
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
ShmTopic::attachGeneration(uint64_t generation, const RingBuffer::TopicContract *expected)
{
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
    current_generation_ = 1;
    return true;
  }

  auto seg = std::make_unique<SharedMemoryPosix>(generationName(name_, generation), O_RDWR, static_cast<PERM>(0));
  if (!seg->connect())
  {
    last_error_ = "generation " + std::to_string(generation) + " does not exist";
    return false;
  }

  // 作成者が初期化を終えるのを待つ（作成と初期化の間に他者が覗く可能性がある）
  if (!RingBuffer::waitForInitialization(seg->getPtr(), 500000))
  {
    last_error_ = "generation " + std::to_string(generation) + " is not initialized";
    return false;
  }

  std::string reason;
  // セグメント名の N とヘッダの generation が一致することも確認する（R02-F06）
  auto rb = attachRingBuffer(*seg, &reason, expected, generation);
  if (rb == nullptr)
  {
    last_error_ = "cannot attach generation " + std::to_string(generation) + ": " + reason;
    return false;
  }

  data_               = std::move(seg);
  ring_               = std::move(rb);
  current_generation_ = generation;
  return true;
}

//! @brief 初期化されないまま残った世代セグメントを回収する
//! @details 世代セグメントを作った直後、初期化を終える前に作成者が死ぬと、
//!          中身が空のセグメントが名前だけ残る。以後 O_EXCL は必ず失敗するので、
//!          放置すると容量拡張が二度とできなくなる（R02-F04）。
//!          誤って生きている作成者のセグメントを消さないよう、
//!          「初期化完了を十分待った」かつ「root がまだ切り替わっていない」
//!          ことを確認してから unlink する。
bool
ShmTopic::reclaimOrphanGeneration(uint64_t generation, uint64_t from_generation)
{
  const std::string seg_name = generationName(name_, generation);

  SharedMemoryPosix seg(seg_name, O_RDWR, static_cast<PERM>(0));
  if (!seg.connect())
  {
    // 既に誰かが片付けた
    return true;
  }

  // 作成者が生きていれば、この間に初期化を終えるはず
  if (RingBuffer::waitForInitialization(seg.getPtr(), ORPHAN_WAIT_TIMEOUT_US))
  {
    return false;  // 孤児ではなかった
  }

  // root がまだ切り替わっていないことを再確認してから消す。
  // 既に切り替わっていれば、このセグメントは現役なので触らない。
  if (root_ring_ == nullptr || root_ring_->getLatestGeneration() != from_generation)
  {
    return false;
  }

  seg.disconnectAndUnlink();
  last_error_ = "reclaimed an orphaned generation " + std::to_string(generation) +
                " (its creator died before finishing initialization)";
  return true;
}

//! @brief 世代を進めた後、不要になった旧世代セグメントを削除する
//! @details 名前を消してもマッピングは生き続けるので、旧世代を掴んだままの
//!          参加者は安全に読み書きを続けられる。放置すると段階的な容量拡張で
//!          /dev/shm を食い潰すため、切り替え時に前の世代を片付ける（R02-F06）。
//!          世代 1 は root（ディレクトリ兼用）なので決して消さない。
void
ShmTopic::unlinkSuperseded(uint64_t generation)
{
  if (generation <= 1)
  {
    return;
  }
  try
  {
    disconnectMemory(generationName(name_, generation));
  }
  catch (const std::exception &)
  {
    // 消せなくても致命的ではない
  }
}

//! @brief 次の世代のセグメントを作って公開する
//! @details O_CREAT | O_EXCL で作成者を一者に絞る。負けた側は勝者のセグメントに
//!          合流し、それでも容量が足りなければ更に次の世代へ挑戦する。
bool
ShmTopic::createNextGeneration(uint64_t from_generation, size_t capacity, int buf_num, size_t payload_alignment,
                               const RingBuffer::TopicContract &contract)
{
  const uint64_t    next      = from_generation + 1;
  const std::string next_name = generationName(name_, next);

  // capacity は payload_alignment の倍数でなければならない。
  if (payload_alignment > 1 && (capacity % payload_alignment) != 0)
  {
    capacity += payload_alignment - (capacity % payload_alignment);
  }
  const size_t size = RingBuffer::getSize(capacity, buf_num, payload_alignment);

  SharedMemoryPosix probe(next_name, O_RDWR | O_CREAT | O_EXCL, perm_);
  if (!probe.connect(size))
  {
    // 作成できなかった。理由を切り分ける（R02-F04）。
    // 以前は一律「他プロセスが作成中」とみなして何もしなかったため、
    // 初期化前に死んだ作成者の残骸があると、以後の容量拡張が
    // 永久に回復できなくなっていた。
    if (errno == EEXIST)
    {
      if (reclaimOrphanGeneration(next, from_generation))
      {
        last_error_ = "generation " + std::to_string(next) + " was an orphan and has been reclaimed; retrying";
      }
      else
      {
        last_error_ = "generation " + std::to_string(next) + " is being created by another process";
      }
    }
    else
    {
      last_error_ = "cannot create generation " + std::to_string(next) + ": " + std::strerror(errno);
    }
    return false;
  }

  try
  {
    // 自分の世代番号を明示して初期化する（R02-F06）
    RingBuffer initializer(probe.getPtr(), capacity, buf_num, payload_alignment, &contract, next);

    // 発行番号は世代をまたいで一意でなければならない（R02-F05）。
    // 旧世代のカウンタ以上から始めれば、旧世代で使った番号を
    // 新世代が再利用することはない。移行が一部失敗しても、
    // 切り替え直後に旧世代で commit が走っても、重複しない。
    if (ring_ != nullptr)
    {
      initializer.adoptSequenceFloor(ring_->getSequenceCounter());
    }

    // 旧世代の履歴を引き継ぐ。
    // 引き継がないと、レイアウトが変わった瞬間に過去のデータが全部消える。
    // まだ公開していないセグメントなので、ここで書いても他プロセスには見えない。
    migrateHistory(initializer);
  }
  catch (const std::exception &e)
  {
    last_error_ = std::string("cannot initialize the new generation: ") + e.what();
    probe.disconnectAndUnlink();
    return false;
  }

  // 完全に初期化してから「現在有効な世代」を進める。
  // ここで初めて他プロセスから見えるようになる。
  if (!root_ring_->tryAdvanceLatestGeneration(from_generation, next))
  {
    // 誰かが先に進めた。自分が作ったセグメントは不要なので消して合流する。
    probe.disconnectAndUnlink();
    last_error_ = "another process advanced the generation first";
    return false;
  }

  probe.disconnect();
  if (!attachGeneration(next, &contract))
  {
    return false;
  }
  // 切り替えが済んだので、もう誰も新規に接続しない旧世代を片付ける（R02-F06）
  unlinkSuperseded(from_generation);
  return true;
}

//! @brief 旧世代の有効なサンプルを新世代へ引き継ぐ
//! @details 発行番号の小さい順に取り込むことで、リング上の新旧関係を保つ。
//!          読み出しは seqlock で検証し、コピー中に上書きされたサンプルは
//!          「もう履歴に残っていない」ものとして黙って飛ばす。
void
ShmTopic::migrateHistory(RingBuffer &destination)
{
  if (ring_ == nullptr)
  {
    return;
  }

  // 発行番号の小さい順に並べる
  std::vector<SampleInfo> samples;
  const size_t            slots = ring_->getBufferNum();
  samples.reserve(slots);
  for (size_t i = 0; i < slots; ++i)
  {
    const SampleInfo info = ring_->getSampleInfo(static_cast<int>(i));
    if (info.sequence != 0)
    {
      samples.push_back(info);
    }
  }
  std::sort(samples.begin(), samples.end(),
            [](const SampleInfo &a, const SampleInfo &b) { return a.sequence < b.sequence; });

  std::vector<unsigned char> scratch;
  const size_t               source_capacity = ring_->getElementSize();
  for (const SampleInfo &info : samples)
  {
    // 元のスロットを探し直す（並べ替えでスロット番号を失っているため）
    int source_slot = -1;
    for (size_t i = 0; i < slots; ++i)
    {
      if (ring_->getSequence(static_cast<int>(i)) == info.sequence)
      {
        source_slot = static_cast<int>(i);
        break;
      }
    }
    if (source_slot < 0)
    {
      continue;  // 既に上書きされた
    }

    const size_t bytes = static_cast<size_t>(info.payload_size);
    if (bytes > source_capacity)
    {
      continue;
    }
    scratch.resize(bytes);
    if (bytes > 0)
    {
      std::memcpy(scratch.data(), ring_->getDataList() + static_cast<size_t>(source_slot) * source_capacity, bytes);
    }
    std::atomic_thread_fence(std::memory_order_acquire);
    if (ring_->getSequence(source_slot) != info.sequence)
    {
      continue;  // コピー中に上書きされた
    }

    destination.adoptSample(info, scratch.data(), bytes);
  }
}

//! @brief 接続世代がまだ有効か
//! @details publish のコミット後に世代が切り替わっていないかを確認するために使う（R02-F05）
bool
ShmTopic::isGeneration(uint64_t generation) const
{
  if (root_ring_ == nullptr)
  {
    return false;
  }
  const uint64_t latest = std::max<uint64_t>(root_ring_->getLatestGeneration(), 1);
  return latest == generation && current_generation_ == generation;
}

bool
ShmTopic::follow(const RingBuffer::TopicContract *expected)
{
  if (!openRoot(false, 0, 0, 1, expected))
  {
    return false;
  }

  const uint64_t latest = root_ring_->getLatestGeneration();
  if (latest == 0)
  {
    last_error_ = "the topic has no published generation yet";
    return false;
  }

  if (ring_ != nullptr && current_generation_ == latest && !ring_->isLayoutChanged())
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
    want += alignment - (want % alignment);
  }

  if (!openRoot(true, want, buf_num, alignment, &contract))
  {
    return false;
  }

  for (int attempt = 0; attempt < MAX_GENERATION_ATTEMPTS; ++attempt)
  {
    const uint64_t latest = std::max<uint64_t>(root_ring_->getLatestGeneration(), 1);

    if (ring_ == nullptr || current_generation_ != latest || ring_->isLayoutChanged())
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
    return true;
  }

  last_error_ = "could not settle on a layout generation after " + std::to_string(MAX_GENERATION_ATTEMPTS) +
                " attempts; publishers on this topic are probably requesting incompatible layouts";
  return false;
}

}  // namespace shm

}  // namespace irlab
