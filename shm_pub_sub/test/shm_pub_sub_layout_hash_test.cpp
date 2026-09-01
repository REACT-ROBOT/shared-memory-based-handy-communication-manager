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
    for (const char *t : { "lh_reorder", "lh_ok", "lh_asym" })
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

// -----------------------------------------------------------------------------
// メンバの書き漏らし・並べ間違いはコンパイルエラーになる
//
// 書き漏らしても、書いた側から見える offsetof / sizeof は変わらないので、
// ハッシュだけでは同じ値になってしまう。layout_covers_type() が
// 「並べたメンバが型を隙間なく覆っているか」を検査することで、
// 次の 4 つはコンパイル時に止まる（実際に落ちることは別途確認済み）。
//
//   - 先頭のメンバを書き漏らした
//   - 途中のメンバを書き漏らした（パディングで説明できない隙間ができる）
//   - 末尾のメンバを書き漏らした
//   - 宣言順と違う順で並べた
//
// ここではその判定関数を直接検証する。マクロ自体は
// 「落ちるべきものが落ちる」ことをコンパイルできない形では書けないため。
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutHashTest, MissingOrMisorderedMembersAreRejected)
{
  using irlab::shm::LayoutField;
  using irlab::shm::layout_covers_type;

  // struct { uint32_t a; double b; }  sizeof 16 / alignof 8
  constexpr LayoutField complete[] = { { 0, 4, 4 }, { 8, 8, 8 } };
  static_assert(layout_covers_type(16, 8, complete), "正しい宣言は通ること");

  // 先頭を書き漏らした（最初のオフセットが 0 でない）
  constexpr LayoutField no_first[] = { { 8, 8, 8 } };
  static_assert(!layout_covers_type(16, 8, no_first), "先頭の書き漏らしを検出できていない");

  // 末尾を書き漏らした: struct { uint32_t a; double b; uint32_t c; } sizeof 24
  constexpr LayoutField no_last[] = { { 0, 4, 4 }, { 8, 8, 8 } };
  static_assert(!layout_covers_type(24, 8, no_last), "末尾の書き漏らしを検出できていない");

  // 途中を書き漏らした: struct { uint32_t a; uint64_t hidden; uint32_t b; }
  constexpr LayoutField no_middle[] = { { 0, 4, 4 }, { 16, 4, 4 } };
  static_assert(!layout_covers_type(24, 8, no_middle), "途中の書き漏らしを検出できていない");

  // 宣言順と違う順で並べた
  constexpr LayoutField wrong_order[] = { { 8, 8, 8 }, { 0, 4, 4 } };
  static_assert(!layout_covers_type(16, 8, wrong_order), "並べ間違いを検出できていない");

  // 配列メンバと単一メンバも通ること
  constexpr LayoutField with_array[] = { { 0, 4, 4 }, { 4, 4324, 4 } };
  static_assert(layout_covers_type(4328, 4, with_array), "配列メンバを含む宣言が通らない");
  constexpr LayoutField single[] = { { 0, 8, 8 } };
  static_assert(layout_covers_type(8, 8, single), "単一メンバの宣言が通らない");

  // 既知の限界: 書き漏らしたメンバが「どのみち必要なパディング」にちょうど
  // 収まる場合は、listed なメンバから見える情報が完全に同一になるため
  // offsetof / sizeof だけでは原理的に区別できない。
  //   before: { uint32_t a;                 double b; }  a@0 b@8 sizeof 16
  //   after : { uint32_t a; uint32_t hidden; double b; }  a@0 b@8 sizeof 16
  constexpr LayoutField hidden_in_padding[] = { { 0, 4, 4 }, { 8, 8, 8 } };
  static_assert(layout_covers_type(16, 8, hidden_in_padding),
                "この形は検出できない（既知の限界）。検出できるようになったらこの記述を消すこと");

  SUCCEED();
}

// -----------------------------------------------------------------------------
// R04-F09: 片方だけ書式を宣言している場合も拒まなければならない
//
// 版はセグメント生成時にしか書かれない。宣言を後から足したトピックは
// セグメント側が 0 のまま固定されるので、「片方が 0 なら照合しない」と
// していると、**宣言を導入した瞬間・書式を変えた瞬間という最も検査が要る
// 局面で検査が消える**。R03 の実装がその状態で、shm_schema<T> は実機で
// 一度も照合されていなかった。
//
// 0 は「宣言が無い」であって「何でもよい」ではない。
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutHashTest, AnUndeclaredSegmentIsRejectedByADeclaredProcess)
{
  Publisher<LayoutA> pub("lh_asym", 3);
  LayoutA            sent{};
  sent.count = 7;
  pub.publish(sent);

  SharedMemoryPosix shm("lh_asym", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  ShmHeader *header = reinterpret_cast<ShmHeader *>(shm.getPtr());
  ASSERT_NE(header->schema_version, 0u);

  // 宣言が無かった頃のビルドが作ったセグメントに見せかける
  header->schema_version = 0;

  Subscriber<LayoutA> sub("lh_asym");
  bool                ok = false;
  sub.subscribe(&ok);
  EXPECT_FALSE(ok) << "書式を確かめる手段が無いのに接続できてしまった";
}

// -----------------------------------------------------------------------------
// 逆向き（自分が未宣言・セグメントが宣言済み）も拒む
//
// 再デプロイ後に古いプロセスが生き残っている場合がこれにあたる。
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutHashTest, ADeclaredSegmentIsRejectedByAnUndeclaredProcess)
{
  // int は宣言不要（版 0）なので、そのトピックのセグメントに版を書き込むと
  // 「セグメントは宣言済み、自分は未宣言」の状態を作れる。
  Publisher<int> pub("lh_asym", 3);
  pub.publish(5);

  SharedMemoryPosix shm("lh_asym", O_RDWR, DEFAULT_PERM);
  ASSERT_TRUE(shm.connect());
  ShmHeader *header = reinterpret_cast<ShmHeader *>(shm.getPtr());
  ASSERT_EQ(header->schema_version, 0u);
  header->schema_version = 0xABCDEF01u;

  Subscriber<int> sub("lh_asym");
  bool            ok = false;
  sub.subscribe(&ok);
  EXPECT_FALSE(ok) << "自分が書式を知らないのに接続できてしまった";
}
