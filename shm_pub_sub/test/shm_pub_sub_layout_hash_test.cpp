//! @file shm_pub_sub_layout_hash_test.cpp
//! @brief ペイロード書式の宣言（R04 / 制約 A）の回帰テスト
//!
//! `element_size`（sizeof）の照合は「同サイズのままメンバを並べ替えた」変更を
//! 通してしまう。再デプロイ後に古いプロセスが生き残っている場合や、古い
//! セグメントが残っている場合に、別物を同じ形式として読み合うことになる。
//! SHM_DECLARE_LAYOUT() はその穴を、人間が版番号を維持せずに閉じる。

#include <gtest/gtest.h>
#include <string>

#include "shm_pub_sub.hpp"

// --- 同じサイズ・同じメンバ、並びだけ違う 2 つの型 ---
struct LayoutA
{
  uint32_t count;
  float    values[8];
};
struct LayoutB
{
  float    values[8];
  uint32_t count;
};
// LayoutA と完全に同一のレイアウト
struct LayoutSame
{
  uint32_t count;
  float    values[8];
};
// 意味だけ変えた（単位を m から mm にした、など）
struct LayoutRevised
{
  uint32_t count;
  float    values[8];
};

SHM_DECLARE_LAYOUT(LayoutA, count, values);
SHM_DECLARE_LAYOUT(LayoutB, values, count);
SHM_DECLARE_LAYOUT(LayoutSame, count, values);
SHM_DECLARE_LAYOUT_REV(LayoutRevised, 2, count, values);

struct SerializedPayload
{
  int x;
};
SHM_DECLARE_SERIALIZED_FORMAT(SerializedPayload, 3);

using namespace irlab::shm;

class SHMLayoutHashTest : public ::testing::Test
{
protected:
  void SetUp() override { cleanup(); }
  void TearDown() override { cleanup(); }
  void cleanup()
  {
    for (const char *t : { "lh_reorder", "lh_ok" })
    {
      try
      {
        disconnectTopic(t);
      }
      catch (const std::exception &)
      {
      }
    }
  }
};

// -----------------------------------------------------------------------------
// 並べ替えは sizeof が同じでもハッシュに出る
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutHashTest, ReorderingMembersChangesTheHashEvenAtTheSameSize)
{
  ASSERT_EQ(sizeof(LayoutA), sizeof(LayoutB)) << "前提: サイズが同じであること";

  EXPECT_NE(shm_schema<LayoutA>::version, shm_schema<LayoutB>::version)
      << "並べ替えを検出できていない（element_size では捕まらない唯一の穴）";
  EXPECT_EQ(shm_schema<LayoutA>::version, shm_schema<LayoutSame>::version)
      << "同一レイアウトなのに別の値になった";
  EXPECT_NE(shm_schema<LayoutA>::version, shm_schema<LayoutRevised>::version)
      << "revision を上げたのに区別されない";
}

// -----------------------------------------------------------------------------
// 宣言の有無が型で分かる
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutHashTest, DeclarationIsVisibleAtCompileTime)
{
  static_assert(shm_schema<LayoutA>::declared, "宣言した型は declared になる");
  static_assert(shm_schema<SerializedPayload>::declared, "シリアライズ型も declared になる");
  static_assert(!shm_schema<int>::declared, "算術型は宣言不要（declared でない）");
  static_assert(shm_schema<int>::version == 0, "未宣言は 0");

  // 宣言した値は必ず 0 以外（0 は「未宣言」の意味に使う）
  EXPECT_NE(shm_schema<LayoutA>::version, 0u);
  EXPECT_EQ(shm_schema<SerializedPayload>::version, 3u);
}

// -----------------------------------------------------------------------------
// 宣言した版はヘッダに記録され、食い違えば payload に触れずに拒まれる
//
// 「再デプロイ後に古いプロセスが生き残っている」状況を、ヘッダの
// schema_version を書き換えて再現する。
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutHashTest, AStaleLayoutIsRejectedWithoutTouchingThePayload)
{
  Publisher<LayoutA> pub("lh_reorder", 3);
  LayoutA            sent{};
  sent.count = 42;
  for (auto &v : sent.values)
  {
    v = 1.0f;
  }
  pub.publish(sent);

  SharedMemoryPosix shm("lh_reorder", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  ShmHeader *header = reinterpret_cast<ShmHeader *>(shm.getPtr());
  ASSERT_EQ(header->schema_version, shm_schema<LayoutA>::version) << "宣言がヘッダに記録されていない";

  // 同じ版なら読める
  {
    Subscriber<LayoutA> sub("lh_reorder");
    bool                ok = false;
    const LayoutA      &v  = sub.subscribe(&ok);
    ASSERT_TRUE(ok);
    EXPECT_EQ(v.count, 42u);
  }

  // 並べ替えた版のプロセスに見せかける。sizeof も schema_id も同じままなので、
  // レイアウトのハッシュだけが唯一の手掛かりになる。
  header->schema_version = shm_schema<LayoutB>::version;

  Subscriber<LayoutA> stale("lh_reorder");
  bool                ok = false;
  stale.subscribe(&ok);
  EXPECT_FALSE(ok) << "レイアウトが違うのに接続できてしまった";
}

// -----------------------------------------------------------------------------
// 宣言していない型は従来どおり動く（移行の途中で壊れない）
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutHashTest, UndeclaredTypesKeepWorking)
{
  Publisher<int>  pub("lh_ok", 3);
  Subscriber<int> sub("lh_ok");
  pub.publish(7);

  bool ok = false;
  EXPECT_EQ(sub.subscribe(&ok), 7);
  EXPECT_TRUE(ok);

  SharedMemoryPosix shm("lh_ok", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  EXPECT_EQ(reinterpret_cast<const ShmHeader *>(shm.getPtr())->schema_version, 0u)
      << "未宣言は 0 のままで、shm_tool doctor から見分けられること";
}
