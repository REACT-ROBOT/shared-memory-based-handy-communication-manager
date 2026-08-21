// =============================================================================
// レイアウト変更・共有メモリ作り直しに関する回帰テスト
//
// このファイルのテストは「既知の未修正バグ」を再現するもので、
// 現行実装では FAIL することを確認済み（テストファースト）。
//
// 対象バグ:
//   1. Publisher<std::vector<T>>::publish() はベクタ長が変わるたびに
//      disconnectAndUnlink() で共有メモリを破棄して作り直す。同じトピックに
//      別の Publisher がいると、その Publisher は破棄済みの共有メモリを掴んだまま
//      publish を続け、誰にも読まれない領域へ書き込み続ける。例外も出ない。
//      disconnectAndUnlink() は st_nlink <= 1 のときだけ unlink する実装だが、
//      POSIX 共有メモリの st_nlink は「名前があるか」を表すだけで、他プロセスが
//      開いているかどうかとは無関係のため、この条件は常に成立する。
//   2. Subscriber はリングバッファのレイアウトが変わっても ring_buffer を
//      作り直さない。作り直すのは共有メモリが切断された場合だけ。
//      バッファ数の異なる Publisher が同じトピックを初期化するとデータ位置が
//      ずれ、subscribe() が success=true のまま無関係な領域を読む。
// =============================================================================

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "shm_base.hpp"
#include "shm_pub_sub.hpp"
#include "shm_pub_sub_vector.hpp"

using namespace irlab::shm;

namespace {

struct Msg {
  uint32_t value;
  uint32_t padding[7];
};

Msg makeMsg(uint32_t value) {
  Msg m{};
  m.value = value;
  return m;
}

}  // namespace

class SHMLayoutTest : public ::testing::Test {
protected:
  void SetUp() override { cleanup(); }
  void TearDown() override { cleanup(); }

  void cleanup() {
    disconnectMemory("layout_vector_resize");
    disconnectMemory("layout_bufnum_mismatch");
    disconnectMemory("layout_stale_publisher");
  }
};

// -----------------------------------------------------------------------------
// 仕様: ベクタ長の変更で共有メモリが作り直されても、同じトピックの別の
//       Publisher が取り残されてはならない。書き込みが届かなくなるなら
//       例外なりで気付けなければならない。
//
// 現行実装: 取り残された Publisher の publish() は成功したように見えるが、
//           破棄済みの共有メモリへ書いているため誰にも届かない。FAIL する。
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutTest, VectorResizeMustNotStrandAnotherPublisher) {
  const std::string topic      = "layout_vector_resize";
  const int         buffer_num = 3;

  Publisher<std::vector<int>> pub_a(topic, buffer_num);
  Publisher<std::vector<int>> pub_b(topic, buffer_num);

  pub_a.publish(std::vector<int>{ 1, 1, 1 });

  Subscriber<std::vector<int>> sub(topic);
  bool                         ok = false;
  ASSERT_EQ(sub.subscribe(&ok), (std::vector<int>{ 1, 1, 1 }));
  ASSERT_TRUE(ok) << "前提: pub_a の書き込みが読めているはず";

  // pub_b がベクタ長を変えて publish → 共有メモリを unlink して作り直す
  pub_b.publish(std::vector<int>{ 2, 2, 2, 2 });

  bool ok_b = false;
  auto from_b = sub.subscribe(&ok_b);
  ASSERT_TRUE(ok_b) << "前提: pub_b の書き込みは読めているはず";
  ASSERT_EQ(from_b, (std::vector<int>{ 2, 2, 2, 2 }));

  // 取り残された pub_a が publish する。例外は出ない。
  const std::vector<int> from_a_value{ 7, 7, 7 };
  EXPECT_NO_THROW(pub_a.publish(from_a_value));

  bool ok_a = false;
  auto after = sub.subscribe(&ok_a);
  EXPECT_TRUE(ok_a);
  EXPECT_EQ(after, from_a_value)
      << "ベクタ長の変更で共有メモリが作り直され、pub_a が破棄済みの領域へ"
         "書き込み続けている（例外も出ないため気付けない）";
}

// -----------------------------------------------------------------------------
// 仕様: リングバッファのレイアウトが変わったあと、subscribe() は正しい値を
//       返すか失敗するかのどちらかでなければならない。success=true で
//       無関係な値を返してはならない。
//
// 現行実装: Subscriber は ring_buffer を作り直さないため、バッファ数が変わって
//           データ位置がずれても古いオフセットで読み続け、success=true のまま
//           別の領域の値を返す。FAIL する。
//
// 注: バッファ数や型サイズを変えた場合に既存データが失われること自体は
//     クラスのドキュメントに既知の制約として記載されている。ここで問題に
//     しているのは「失われること」ではなく「壊れた値を成功として返すこと」。
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutTest, SubscribeMustNotReturnGarbageAfterLayoutChange) {
  const std::string topic = "layout_bufnum_mismatch";

  Publisher<Msg>  pub_3buf(topic, 3);
  Subscriber<Msg> sub(topic);

  pub_3buf.publish(makeMsg(100));
  bool ok = false;
  ASSERT_EQ(sub.subscribe(&ok).value, 100u);
  ASSERT_TRUE(ok) << "前提: buffer_num=3 の書き込みが読めているはず";

  // バッファ数の違う Publisher がリングバッファを作り直す（レイアウト変更）
  Publisher<Msg> pub_8buf(topic, 8);

  pub_8buf.publish(makeMsg(200));

  bool       ok_after = false;
  const Msg& after    = sub.subscribe(&ok_after);
  std::cout << "  レイアウト変更後の subscribe: success=" << ok_after << " value=" << after.value << std::endl;

  if (ok_after) {
    EXPECT_EQ(after.value, 200u)
        << "レイアウト変更後に Subscriber が古いオフセットで読み続け、"
           "無関係な領域の値を success=true で返した";
  }

  // 新しく作った Subscriber は正しいレイアウトで読めることの対比
  Subscriber<Msg> fresh_sub(topic);
  bool            ok_fresh = false;
  const Msg&      fresh    = fresh_sub.subscribe(&ok_fresh);
  EXPECT_TRUE(ok_fresh);
  EXPECT_EQ(fresh.value, 200u) << "新規 Subscriber なら正しく読めるはず（既存 Subscriber との差が問題）";
}

// -----------------------------------------------------------------------------
// 仕様: 自分が構築したときと違うレイアウトに作り直されたあとの publish() は、
//       古いオフセットのまま書いてはならない。
//
// publish() は走査するバッファ数を共有メモリ上の値 (*buf_num) から読む一方、
// 書き込み位置は構築時に計算したオフセットを使う。バッファ数が増えた状態で
// 気付かずに書くと、確保していない位置——バッファ数によってはマッピングの外——
// へ書き込むことになる。
//
// 現行実装: 取り残された Publisher の書き込みは、正しいレイアウトで読む
//           Subscriber からは無関係な値に見える。FAIL する。
// -----------------------------------------------------------------------------
TEST_F(SHMLayoutTest, PublishMustNotUseStaleLayout) {
  const std::string topic = "layout_stale_publisher";

  Publisher<Msg> pub_3buf(topic, 3);
  pub_3buf.publish(makeMsg(100));

  // バッファ数の違う Publisher がリングバッファを作り直す
  Publisher<Msg> pub_8buf(topic, 8);
  pub_8buf.publish(makeMsg(200));

  // 取り残された側が publish する
  EXPECT_NO_THROW(pub_3buf.publish(makeMsg(300)));

  // 現在のレイアウトで正しく読む Subscriber から見えるか
  Subscriber<Msg> sub(topic);
  bool            ok = false;
  const Msg&      m  = sub.subscribe(&ok);
  std::cout << "  取り残された Publisher の publish 後: success=" << ok << " value=" << m.value << std::endl;

  EXPECT_TRUE(ok);
  EXPECT_EQ(m.value, 300u) << "レイアウト変更に気付かない Publisher が古いオフセットへ書き込んだ";
}
