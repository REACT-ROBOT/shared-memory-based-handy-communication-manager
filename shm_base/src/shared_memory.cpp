#include <shm_base.hpp>

namespace irlab
{

namespace shm
{

//! @brief 共有メモリ名を /dev/shm 上の名前へ変換する
//! @param [in] name 共有メモリ名（先頭の '/' は取り除かれている前提）
//! @return std::string shm_open に渡す名前
//! @details 以前は regex_replace と std::regex("/") で置換していたが、
//!          これには2つの問題があった。
//!          1. connect() / isExists() / disconnectMemory() が呼ばれるたびに
//!             正規表現をコンパイルしていた（購読の再接続経路は頻繁に通る）。
//!          2. libstdc++ の regex はコンパイル時にロケール（ctype）の
//!             遅延初期化キャッシュを同期なしで触るため、複数スレッドから
//!             同時に connect() するとデータ競合になる。
//!             ThreadSanitizer で実際に検出された（R01-F10）。
//!          やりたいのは '/' を '_' に置換するだけなので、正規表現を使わない。
static std::string
toShmPath(const std::string &name)
{
  std::string path = "/shm_";
  path.reserve(path.size() + name.size());
  for (char c : name)
  {
    path.push_back(c == '/' ? '_' : c);
  }
  return path;
}

//! @brief 共有メモリ名として使える文字列かを検証する
//! @param [in] name    検証する名前
//! @param [in] context エラーメッセージに載せる呼び出し元
//! @details 名前はそのまま /dev/shm 配下のファイル名になる。空文字列は
//!          `name[0]` の範囲外参照になり、`..` やヌル文字は意図しない
//!          パスを指し得るため、境界で弾く（R01-F06）。
void
validateShmName(const std::string &name, const char *context)
{
  if (name.empty())
  {
    throw std::invalid_argument(std::string(context) + ": shared memory name must not be empty");
  }
  // "/shm_" の接頭辞と、先頭の '/' を1つ剥がす分を見込んだ上限
  constexpr size_t MAX_SHM_NAME_LENGTH = 200;
  if (name.size() > MAX_SHM_NAME_LENGTH)
  {
    throw std::invalid_argument(std::string(context) + ": shared memory name is too long (" +
                                std::to_string(name.size()) + " > " + std::to_string(MAX_SHM_NAME_LENGTH) + ")");
  }
  if (name.find('\0') != std::string::npos || name.find("..") != std::string::npos)
  {
    throw std::invalid_argument(std::string(context) + ": shared memory name must not contain '..' or NUL");
  }
  // 先頭の '/' を剥がした後が空になる名前（"/" だけ）も拒否する
  if (name == "/")
  {
    throw std::invalid_argument(std::string(context) + ": shared memory name must not be \"/\"");
  }
}

//! @brief トピック名として使えるかを検証する
//! @details 共有メモリ名としての検証に加えて、**トピック名だけに課す制約**を見る。
//!          世代セグメント名（/shm_<topic>#<世代>-<ノンス>）はライブラリが
//!          内部で作るので、そちらは validateShmName() だけを通す。
void
validateTopicName(const std::string &name, const char *context)
{
  validateShmName(name, context);

  // '#' は世代セグメント名の予約文字である。トピック名に含められると、
  // そのトピックが別トピックの世代セグメントに見えてしまい、世代の後始末で
  // **無関係なセグメントを消せる**（R04-F19）。実際に
  // "topic#2-0000deadbeef" というトピックを作ると、別トピック "topic" が
  // 世代 3 へ進んだときに古い世代の残骸とみなされて unlink された。
  if (name.find('#') != std::string::npos)
  {
    throw std::invalid_argument(std::string(context) +
                                ": topic name must not contain '#'; it is reserved for layout generation "
                                "segments (/shm_<topic>#<generation>-<nonce>)");
  }
}

//! @brief 共有メモリを破棄する(POSIX版)
//! @param [in] name 共有メモリ名
//! @return なし
//! @details この関数では、引数で与えられた共有メモリ名の共有メモリを破棄する.
//! 破棄すると言ってもファイルの削除と同様に、すでにプログラムで展開されている場合は、
//! 破棄された共有メモリにアクセスし続けることができる．
//! 破棄後に同名の共有メモリを作成した場合、新しいアドレスが生成されるため、
//! 再度サブスクライバを生成しないと新しい共有メモリにアクセスできない．
//! ファイルディスクリプタを保持し、inodeのカウントを監視することで、
//! 共有メモリの破棄を検知することができるが、むやみに共有メモリを破棄することがないことを前提として、
//! 余計な処理を加えないためにあえて共有メモリの監視はしていない。
int
disconnectMemory(std::string name)
{
  // 空文字列のまま name[0] を触っていた（R01-F06）。名前は共有メモリのパスに
  // なるため、空・過長・パス区切りの悪用を API 境界で弾く。
  validateShmName(name, "shm::disconnectMemory()");

  if (name[0] == '/')
  {
    name = name.erase(0, 1);
  }
  const std::string str_buf = toShmPath(name);
  return shm_unlink(str_buf.c_str());
}

//! @brief トピックの共有メモリを世代ごと破棄する(POSIX版)
//! @details 形式 v3 ではレイアウトを変えるたびに /shm_<topic>#<N> という
//!          別セグメントが増える。disconnectMemory() は世代 1 しか消さないので、
//!          トピックを完全に片付けるときはこちらを使うこと。
int
disconnectTopic(const std::string &name)
{
  return ShmTopic::removeAllGenerations(name);
}

SharedMemory::SharedMemory(int oflag, PERM perm)
  : shm_fd(-1)
  , shm_oflag(oflag)
  , shm_perm(perm)
  , shm_size(0)
  , shm_ptr(nullptr)
{
}

size_t
SharedMemory::getSize() const
{
  return shm_size;
}

unsigned char *
SharedMemory::getPtr()
{
  return shm_ptr;
}

SharedMemoryPosix::SharedMemoryPosix(std::string name, int oflag, PERM perm)
  : SharedMemory(oflag, perm)
  , shm_name(name)
{
  // 空文字列のまま shm_name[0] を触っていた（R01-F06）
  validateShmName(shm_name, "shm::SharedMemoryPosix()");

  if (shm_name[0] == '/')
  {
    shm_name = shm_name.erase(0, 1);
  }
}

//! @brief デストラクタ
//! @details 以前は fd を閉じるだけで munmap していなかったため、明示的に
//!          disconnect() しない通常の寿命終了でマッピングがプロセス終了まで
//!          残っていた。長時間動くプロセスで Publisher/Subscriber を作り直すと
//!          アドレス空間と VMA を食い潰し、最終的に mmap 失敗に至る（R01-F09）。
//!          disconnect() は shm_ptr / shm_fd を見て冪等に書かれているので、
//!          既に切断済みでも安全に呼べる。
SharedMemoryPosix::~SharedMemoryPosix()
{
  disconnect();
}

bool
SharedMemoryPosix::connect(size_t size)
{
  // 同じオブジェクトに対して connect() を続けて呼ぶと、以前は既存の
  // マッピングと fd を閉じずに上書きしていた（アドレス空間と fd が漏れる）。
  // 張り直しとして扱い、先に片付ける。
  disconnect();

  const std::string str_buf = toShmPath(shm_name);

  shm_fd = shm_open(str_buf.c_str(), shm_oflag, static_cast<mode_t>(shm_perm));
  if (shm_fd < 0)
  {
    return false;
  }
  struct stat stat;
  // fstat の失敗を無視すると未初期化の st_size を使ってしまう（R01-F06）
  if (fstat(shm_fd, &stat) != 0)
  {
    close(shm_fd);
    shm_fd = -1;
    return false;
  }
  if (size != 0 && static_cast<size_t>(stat.st_size) < size)
  {
    if (ftruncate(shm_fd, size) < 0)
    {
      close(shm_fd);
      shm_fd = -1;
      throw std::runtime_error("shm::SharedMemoryPosix::connect(): Could not change shared memory size!");
    }
    // To Update stat.st_size
    if (fstat(shm_fd, &stat) != 0)
    {
      close(shm_fd);
      shm_fd = -1;
      return false;
    }
  }

  // 長さ 0 の共有メモリは mmap できない。作成直後に他プロセスが
  // ftruncate する前を掴んだ場合はここに来るので、失敗として扱う。
  if (stat.st_size <= 0)
  {
    close(shm_fd);
    shm_fd = -1;
    return false;
  }

  // mmap はファイル全長で行うため、shm_size にも実際にマッピングした長さを
  // 記録する。ここに引数の要求サイズを記録すると、既存ファイルの方が大きい場合に
  // shm_size < マッピング長 となり、disconnect() の munmap(shm_ptr, shm_size) が
  // マッピングの末尾を解放し損ねてアドレス空間が漏れる。
  // 要求サイズより大きい共有メモリに接続するのは異常ではない: Publisher は
  // 自分の型とバッファ数から計算したサイズで接続するため、より大きなレイアウトで
  // 作られた既存の共有メモリに繋ぐと必ずこの状況になる。
  shm_size = static_cast<size_t>(stat.st_size);

  shm_ptr = reinterpret_cast<unsigned char *>(mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));

  if (shm_ptr == MAP_FAILED)
  {
    close(shm_fd);
    shm_fd = -1;
    return false;
  }

  return true;
}

int
SharedMemoryPosix::disconnect()
{
  int result = 0;

  // Unmap memory if it was mapped
  if (shm_ptr != nullptr && shm_ptr != MAP_FAILED && shm_size > 0)
  {
    result  = munmap(shm_ptr, shm_size);
    shm_ptr = nullptr;
  }

  // Close file descriptor
  if (shm_fd >= 0)
  {
    close(shm_fd);
    shm_fd = -1;
  }

  // Reset size
  shm_size = 0;

  // NOTE: We do NOT call disconnectMemory(shm_unlink) here.
  // This allows reconnection to the same shared memory.
  // To completely remove shared memory, call disconnectAndUnlink() instead.
  return result;
}

int
SharedMemoryPosix::disconnectAndUnlink()
{
  // NOTE: 以前ここには「st_nlink > 1 なら他プロセスが接続中なので unlink しない」
  // という意図のガードがあったが、POSIX 共有メモリの st_nlink は名前が存在するか
  // どうかを表すだけで、他プロセスが開いているかどうかとは無関係である
  // (実測でも subscriber が開いた状態で st_nlink は 1 のままだった)。
  // したがってこのガードは常に素通りしており、他の利用者を検出できていなかった。
  // POSIX 共有メモリには接続数を知る移植性のある手段が無いため、
  // 「他の利用者がいないことを呼び出し側が保証する」ことを前提とする API とする。
  // 名前を消しても既存のマッピングは生き続けるので、破棄した後もそれを掴んだ
  // プロセスは黙って読み書きを続けられる点に注意すること。
  // ガードを外した後も fstat() の呼び出しと struct stat が残っていたが、
  // 結果は一度も読まれていなかった。判定は「自分がこのセグメントを開けていたか」
  // だけである。
  const bool should_unlink = (shm_fd >= 0);

  // First disconnect (unmap and close fd)
  disconnect();

  // 開けていたなら名前を消す。他の利用者がいないことは呼び出し側の保証である
  // （上の NOTE のとおり、それを知る移植性のある手段は無い）。
  if (should_unlink)
  {
    return disconnectMemory(shm_name);
  }

  return 0;
}

bool
SharedMemoryPosix::isDisconnected() const
{
  struct stat stat;
  if (shm_fd < 0)
  {
    return true;
  }

  // fstat の失敗を無視すると未初期化の st_nlink を読むことになる。
  // 判定できない場合は「切断されている」として扱う（安全側）。
  if (fstat(shm_fd, &stat) != 0)
  {
    return true;
  }
  if (stat.st_nlink <= 0)
  {
    return true;
  }
  return false;
}

bool
SharedMemoryPosix::isExists(uint64_t timeout_usec) const
{
  const std::string str_buf = toShmPath(shm_name);

  // Try to open the shared memory (read-only, no create)
  int fd = shm_open(str_buf.c_str(), O_RDONLY, 0);
  if (fd < 0)
  {
    // Shared memory file does not exist
    return false;
  }

  bool result = false;

  struct stat st;
  if (fstat(fd, &st) == 0 && st.st_size > 0)
  {
    // Map the memory (read-only) to check initialization
    size_t         map_size = std::min((size_t)st.st_size, (size_t)4096);  // Map at least first 4KB
    unsigned char *ptr      = reinterpret_cast<unsigned char *>(mmap(NULL, map_size, PROT_READ, MAP_SHARED, fd, 0));

    if (ptr != MAP_FAILED)
    {
      // Check if already initialized
      if (RingBuffer::checkInitialized(ptr))
      {
        result = true;
      }
      else
      {
        // Not initialized yet, wait for initialization with timeout
        if (RingBuffer::waitForInitialization(ptr, timeout_usec))
        {
          result = true;
        }
      }

      munmap(ptr, map_size);
    }
  }

  close(fd);
  return result;
}

}  // namespace shm

}  // namespace irlab
