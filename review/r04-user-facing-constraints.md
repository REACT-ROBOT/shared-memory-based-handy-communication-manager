# 利用者側コードへの制約による保守負担の削減 — 検討案

- 作成日: 2026-09-01
- 背景: [`r04-2026-09-01-review-finding.md`](r04-2026-09-01-review-finding.md) の指摘 29 件のうち、
  相当数が「利用者が守るべき約束を、ライブラリ側が防御的コードで肩代わりしている」ことに起因する。
  防御コードを足すほどライブラリは複雑になり、次のレビューで新しい穴が見つかる、という循環になっている。
- 方針: **約束をコンパイル時か起動時に強制し、破ったら止まる**ようにして、
  ライブラリ側の防御コードを増やさずに済ませる。
  利用者の書き方が多少変わるのは許容する。

---

## 0. 前提: 「バッファを増やせば reader のロックを外せる」は成り立たない

reader がスロットの robust mutex を取るのは、**payload の data race を消すため**である（R03-F04）。
data race は確率ではなく C++ メモリモデル上の性質なので、バッファを増やしても
「writer と reader が同じスロットに同時に触る」可能性がゼロにはならない。
ゼロだと言うには

> reader は必ず X µs 以内に読み終わり、writer が一周するのに X µs 以上かかる

という**時間的な仮定**が要る。これは R03-F03 で「時間で他プロセスの生死を判定してはいけない」として
排除したのと同じ種類の不健全な推論であり、SIGSTOP・優先度逆転・ページフォルト・スワップで簡単に破れる。

reader のロックを外して writer を止めない方法は、**reader が「このスロットを読んでいる」印（refcount）を
atomic で置き、writer がその印のあるスロットを避ける**方式である。reader 同士も writer も待たなくなるが、

- robust mutex による「reader が死んだら自動回収」が失われる
- 死んだ reader の印が残るとそのスロットが永久に使えないので、回収機構を別途書く必要がある

**コードは確実に増える**ため、保守負担を減らすという目的とは逆方向である。
→ **reader のロックは現状維持**とする（2026-09-01 決定）。

一方、R04-F08 の修正で writer は **「空きスロットがあれば必ず即座に確保できる」** ようになった。
つまり「十分なバッファがあれば writer は止まらない」は既に成立している。
残る問題は **「十分かどうかを誰が保証するか」** であり、これは利用者側の宣言で解ける。

---

## 制約 A — `shm_schema<T>` の必須化（コンパイルエラー）

### 何を強制するか

独自に定義した型を publish / subscribe するとき、**書式の版の宣言を必須にする**。

### 利用者コード

```cpp
struct LidarScan {
  uint32_t count;
  float    ranges[1081];
};

// これが無いとコンパイルエラー
namespace irlab { namespace shm {
template <> struct shm_schema<LidarScan> { static constexpr uint32_t version = 3; };
}}
```

違反時のメッセージ:

```
error: static assertion failed: shm::Publisher : this payload type has no shm_schema<T>
       specialization. Declare the wire-format version so that incompatible builds are
       rejected instead of reading each other's bytes:
           template <> struct shm_schema<YourType> { static constexpr uint32_t version = 1; };
       Bump the version whenever you add, remove, reorder or retype a member.
```

`int` / `float` / `double` などの算術型と、ライブラリが用意した特殊化（cv::Mat など）は対象外。

### 何が防げるか

- **R04-F09**: 現状 `schema_version` は「両者が 0 以外を指定したときだけ」照合される。
  版を導入した瞬間・上げた瞬間は必ず片側が 0 になるので、**最も必要な局面で検査が消える**。
  必須化すれば 0 が存在しなくなり、この抜け道が構造的に消える。
- **R04-F03 の発火経路**: 「書式を変えた新バイナリが、旧書式のペイロードを新パーサで読む」を止められる。
  現状これが point_cloud の範囲外読み出しを現実的にする主因になっている。

### 何が防げないか

- 版を上げ忘れた場合。これは検出できない（人間の規律に依存する）。
  ただし「書式を変えたら版を上げる」は 1 行なので、レビューで見落としにくい。

### 移行コスト

- 独自型ごとに 3 行。既存の型に一括で付ける作業が一度だけ発生する。
- `SHM_STRICT_TYPE_CHECK` と同じく、既定 ON / オプトアウト可（`SHM_REQUIRE_SCHEMA=OFF`）にできる。

---

## 制約 B — トピック定義の一元化とバッファ十分性の `static_assert`

### 何を強制するか

**トピック名・ペイロード型・書式版・バッファ数・想定参加者数を 1 箇所で宣言させる**。
Publisher / Subscriber はその定義だけを受け取る。

### 利用者コード

```cpp
// topics/lidar_scan_topic.hpp — このヘッダを publisher も subscriber も include する
#include <shm_topic_def.hpp>

struct LidarScan { uint32_t count; float ranges[1081]; };

SHM_DEFINE_TOPIC(LidarScanTopic,
  /* topic name  */ "lidar_scan",
  /* payload     */ LidarScan,
  /* schema ver  */ 3,
  /* buffers     */ 8,
  /* max readers */ 4,
  /* max writers */ 1);
```

展開されるもの:

```cpp
struct LidarScanTopic {
  using payload = LidarScan;
  static constexpr const char *name           = "lidar_scan";
  static constexpr uint32_t    schema_version = 3;
  static constexpr int         buffers        = 8;
  static constexpr int         max_readers    = 4;
  static constexpr int         max_writers    = 1;

  // ここが要点。writer が必ず空きスロットを見つけられることを保証する。
  //   - reader は 1 人あたり最大 1 スロットを保持し得る
  //   - writer は 1 人あたり最大 1 スロットを保持し得る
  //   - 誰にも保持されていない「読める最新値」が最低 1 つ残る必要がある
  static_assert(buffers >= max_readers + max_writers + 1,
                "buffers is too small: a writer could fail to find a free slot. "
                "Use at least max_readers + max_writers + 1.");
  static_assert(max_writers >= 1, "a topic needs at least one writer");
};
// shm_schema<LidarScan> も同時に定義される（制約 A を自動で満たす）
```

使う側:

```cpp
irlab::shm::Publisher<LidarScanTopic>  pub;   // 名前も buf_num も渡さない
irlab::shm::Subscriber<LidarScanTopic> sub;

pub.publish(scan);
bool ok = false;
const LidarScan &s = sub.subscribe(&ok);
```

### 何が防げるか

- **バッファ不足による飢餓をコンパイル時に排除**。これが利用者の求めていた
  「十分なバッファがあれば writer が止まらない」をビルドで担保する形になる。
- **publisher と subscriber で buf_num が食い違う事故**。現状は
  `Publisher<T>("topic", 3)` と `Publisher<T>("topic", 8)` を別ファイルに書けてしまい、
  世代が回って収束するまで無駄が出る（R01 で containment 判定にして収束させたが、そもそも起きない方がよい）。
- **トピック名の打ち間違い**。文字列リテラルが 1 箇所になる。
- **型の取り違え**。同じトピック名に別の型で繋ぐコードが書けなくなる。

### 何が防げないか

- 宣言した `max_readers` より実際に多くの subscriber が繋いだ場合。
  これはコンパイル時には分からない → 制約 C で拾う。

### 移行コスト

- トピックごとに定義ヘッダ 1 つ。既存の `Publisher<T>(name, buf_num)` はそのまま残すので、
  **段階的に移行できる**（新規コードから新しい書き方に寄せる）。
- ただし「両方の書き方が残る」こと自体が保守負担なので、最終的には旧 API を deprecated にしたい。

---

## 制約 C — 起動時と実行時の突き合わせ

### 何を強制するか

コンパイル時に分からない「実際の参加者数」「既存セグメントの形」を、**黙って劣化させずに止める**。

1. **接続時**: 既存セグメントの `buf_num` が宣言より小さければ、そのまま使わず世代を進める。
   進められなければ例外で止める（現状は「増やすだけ」で収束を待つので、
   宣言と食い違ったまま動き続ける余地がある）。
2. **ABI / contract 不一致は即座に失敗**。現状は待っても解決しない不整合でも
   `openRoot()` が 1 秒待つため、40Hz のセンサノードが 1Hz のエラーログ生成器になる（R04-F13）。
3. **実行時**: `acquireWritableSlot()` が「空きが無く待ちに入った」回数を計上する。
   これは**宣言した参加者数を超えている証拠**なので、
   - 既定: カウンタに積んで `TopicHealth` で読めるようにする
   - strict モード: 例外で止める

   参加者を数えるレジストリは作らない。作ると「死んだ参加者の登録をどう消すか」という
   R03-F03 と同じ問題が再発する。**症状（空きが無かった）を数える方が安全で単純**。

### 利用者コード

```cpp
// 任意。困ったときだけ見る
const auto health = pub.health();
std::printf("空き待ちに入った回数 = %lu / publish %lu 回\n",
            health.slot_wait_count, health.publish_count);
```

### 何が防げるか

- **R04-F13**: ABI 4 移行時に、旧セグメントが 1 つ残っているだけでノードが
  静かに遅くなる（原因が追いにくい）状態を、明示的な失敗に変える。
- 宣言した `max_readers` を超えた運用を、**実害が出る前に**気づける。

---

## 制約 D — `shm_tool doctor`（事前評価）

### 何をするか

`/dev/shm` を走査してヘッダを読み、実機投入前に判断材料を出す。

```
$ shm_tool doctor
topic            abi  gen  kind        elem      buf  schema  size     状態
lidar_scan         4    3  serialized     1      8   v3      1.2 MiB  OK
camera_image       3    1  serialized     1      3   v1      6.0 MiB  ★ABI 3（このビルドは 4）→ shm_tool remove が必要
odom               4    1  scalar        64      3   v2      12 KiB   OK
old_topic#2-a1b2   4    2  serialized     1      8   v3      900 KiB  ★現世代でない残骸
```

### 何が防げるか

- **R04-F18**: 現状の `list` は `ls -l /dev/shm` を整形するだけでヘッダを読まないため、
  「どのセグメントが旧 ABI か」が分からない。ABI 4 への移行で最初に必要になる情報がこれ。
- **R04-F21**: 世代作成中に死んだプロセスのセグメント（次の世代切替まで残る）を可視化できる。

### 移行コスト

- なし（新しいサブコマンド）。

---

## 制約 E — 返り値の寿命（任意・要検討）

### 現状の落とし穴

`subscribe()` は内部の二重バッファへの `const T&` を返す。
**次の `subscribe()` を呼ぶと、前に返した参照の中身が入れ替わる**。

```cpp
const LidarScan &a = sub.subscribe(&ok);
const LidarScan &b = sub.subscribe(&ok);   // ここで a の中身が変わる
use(a, b);                                  // ← 誤り。a は b と同じか、壊れている
```

これは現在ドキュメントでしか伝えていない。コンパイラは何も言わない。

### 案

`subscribe()` の返り値を値（`Sample<T>`）にする、あるいは
`[[nodiscard]]` を付けた RAII ハンドルにして、次の subscribe までしか生きないことを型で表す。

- 値返しは安全だが、大きなペイロードで毎回コピーが発生する（cv::Mat / 点群では無視できない）
- RAII ハンドルは refcount と同じ問題（死んだ reader の印）を持ち込む

**→ 今回は見送りを推奨。** ドキュメントと、既存の「返り値を必ず即座にコピーする」規約で運用し、
制約 A〜D を先に入れる。

---

## 推奨

| 順 | 制約 | 理由 |
|---|---|---|
| 1 | **A（schema 版の必須化）** | 単独で入れられる。R04-F09 を構造的に消し、F03 の発火経路を塞ぐ。利用者の負担は型ごとに 3 行 |
| 2 | **D（`shm_tool doctor`）** | 既存 API を一切変えない。ABI 4 の実機投入前に必要 |
| 3 | **B（トピック定義の一元化）** | 本命。ただし新旧 API が併存するので、移行方針を決めてから入れたい |
| 4 | **C（起動時・実行時の突き合わせ）** | B とセットで効く。単独でも F13 の部分は価値がある |
| — | E（返り値の寿命） | 見送り。コピーコストか refcount のどちらかを払うことになる |

A と D は**既存コードを一切壊さずに**入れられるので先に進めたい。
B は書き方が変わるため、既存トピックをどこまで移行するかを決めてから着手するのが良い。
