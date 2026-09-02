//! @file shm_pub_sub_double_include_test.cpp
//! @brief 書式の宣言が二重インクルードで壊れないこと
//!
//! `SHM_DECLARE_LAYOUT` / `SHM_DECLARE_SERIALIZED_FORMAT` は
//! `shm_schema<T>` の特殊化を定義する。**インクルードガードの外に置くと**、
//! 同じヘッダを推移的に 2 回取り込んだ翻訳単位でマクロが再展開され、
//! 二重定義でコンパイルが落ちる。
//!
//! これは x86 側で各 .cpp を単独で構文チェックしていたときには出ず、
//! Raspberry Pi 4 でワークスペース全体をビルドして初めて露見した。
//! 単独の構文チェックでは「1 つの翻訳単位が 1 回だけ取り込む」状況しか
//! 作れないためである。

#include <gtest/gtest.h>

// わざと 2 回取り込む
#include "shm_pub_sub.hpp"
#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"
#include "shm_pub_sub_vector.hpp"

namespace
{
struct DoubleIncluded
{
  uint32_t a;
  float    b;
};
}  // namespace

SHM_DECLARE_LAYOUT(DoubleIncluded, a, b);

TEST(SHMDoubleIncludeTest, DeclaringAFormatSurvivesADoubleInclude)
{
  // ここまでコンパイルできていることが検証内容そのものだが、
  // 宣言が実際に効いていることも確かめておく。
  EXPECT_TRUE(irlab::shm::shm_schema<DoubleIncluded>::declared);
  EXPECT_NE(irlab::shm::shm_schema<DoubleIncluded>::version, 0u);
}
