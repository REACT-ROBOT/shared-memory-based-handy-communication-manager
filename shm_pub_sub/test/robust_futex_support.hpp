//! @file robust_futex_support.hpp
//! @brief robust futex（所有者が死んだ mutex の EOWNERDEAD 通知）が使える環境かを調べる
//!
//! QEMU のユーザモードエミュレーション（CI の arm64 ジョブ）は set_robust_list /
//! get_robust_list を実装しておらず ENOSYS を返す。ゲストのリストをホストカーネルに
//! 渡せないためで、そこではロックを持ったまま死んだプロセスがいても EOWNERDEAD が
//! 通知されない。「死んだ所有者のロックを回収できる」ことを確かめるテストは、
//! そうした環境では原理的に通らないので SKIP する。実機の Linux カーネル
//! （x86 / Raspberry Pi）では常に使える。
#ifndef SHM_TEST_ROBUST_FUTEX_SUPPORT_HPP
#define SHM_TEST_ROBUST_FUTEX_SUPPORT_HPP

#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

namespace shm_test
{

inline bool
robustFutexSupported()
{
  void  *head = nullptr;
  size_t len  = 0;
  // glibc はスレッド開始時に robust list を登録するので、使える環境なら必ず成功する
  return syscall(SYS_get_robust_list, 0, &head, &len) == 0 || errno != ENOSYS;
}

}  // namespace shm_test

//! robust futex が使えない環境ではテストを SKIP する
#define SHM_SKIP_WITHOUT_ROBUST_FUTEX()                                                               \
  do                                                                                                   \
  {                                                                                                    \
    if (!shm_test::robustFutexSupported())                                                             \
    {                                                                                                  \
      GTEST_SKIP() << "robust futex (set_robust_list) is unavailable here (e.g. QEMU user-mode "       \
                      "emulation); a lock held by a killed process cannot be recovered in this "       \
                      "environment";                                                                   \
    }                                                                                                  \
  } while (0)

#endif  // SHM_TEST_ROBUST_FUTEX_SUPPORT_HPP
