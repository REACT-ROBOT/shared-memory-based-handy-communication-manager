#include <shm_base.hpp>

namespace irlab
{

namespace shm
{

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
  if (name[0] == '/')
  {
    name = name.erase(0, 1);
  }
  std::string str_buf = "/shm_" + regex_replace(name, std::regex("/"), "_");
  return shm_unlink(str_buf.c_str());
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
  if (shm_name[0] == '/')
  {
    shm_name = shm_name.erase(0, 1);
  }
}

SharedMemoryPosix::~SharedMemoryPosix()
{
  if (shm_fd >= 0)
  {
    close(shm_fd);
  }
}

bool
SharedMemoryPosix::connect(size_t size)
{
  std::string str_buf = "/shm_" + regex_replace(shm_name, std::regex("/"), "_");

  shm_fd = shm_open(str_buf.c_str(), shm_oflag, static_cast<mode_t>(shm_perm));
  if (shm_fd < 0)
  {
    return false;
  }
  struct stat stat;
  fstat(shm_fd, &stat);
  if (size != 0 && static_cast<size_t>(stat.st_size) < size)
  {
    if (ftruncate(shm_fd, size) < 0)
    {
      throw std::runtime_error("shm::getMemory(): Could not change shared memory size!");
    }
    // To Update stat.st_size
    fstat(shm_fd, &stat);
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
  // Check if other processes/threads are still using this shared memory
  struct stat shm_stat;
  bool        should_unlink = false;

  if (shm_fd >= 0 && fstat(shm_fd, &shm_stat) == 0)
  {
    // st_nlink indicates how many references exist to this shared memory
    // If st_nlink > 1, other processes/threads are still connected
    // Only unlink if we're the last one (st_nlink <= 1)
    if (shm_stat.st_nlink <= 1)
    {
      should_unlink = true;
    }
  }

  // First disconnect (unmap and close fd)
  disconnect();

  // Only unlink if no other processes/threads are using it
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

  fstat(shm_fd, &stat);
  if (stat.st_nlink <= 0)
  {
    return true;
  }
  return false;
}

bool
SharedMemoryPosix::isExists(uint64_t timeout_usec) const
{
  std::string str_buf = "/shm_" + regex_replace(shm_name, std::regex("/"), "_");

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
