# R01〜R05 の積み上げに対するリファクタリング

- 実施日: 2026-09-02
- 動機: 5 回のレビューで 45 コミットを積み上げた結果、**後の修正で前の修正が
  意味を失ったり、重複したりしていないか**を 点検する
- 方法: 視点を分けた 4 体の調査エージェントを並行で走らせ、**報告は必ず自分で
  裏を取ってから**適用した

## 総括 — 一番の収穫は「エージェントの主張を検証したこと」

4 体の報告のうち **2 件は誤りだった**。裏取りをせずに適用していたら、
どちらも状況を悪くしていた。

| 主張 | 検証結果 |
|---|---|
| TSan スピンフォールバックが死んでいる（私の事前の疑い） | **外れ**。R05 がブロックごと削除済み |
| `clocklock` / `PRIO_INHERIT` のコメントが古い（私の事前の疑い） | **外れ**。正しく追従済み |
| `layout_covers_type` の順序検査にテストが無い | **半分外れ**。テストの穴ではなく分岐が論理的に冗長 |
| 外部 3 特殊化に R04-F12 が未適用 | **正しい**。実測で 379µs → 1µs |
| 返り値ダブルバッファが無防備 | **正しい**。注入で実証 |

とくに 3 番目は、「テストを足す」で終わらせずに実験したことで
**分岐が冗長である**という別の結論に達した。原因を取り違えたまま
テストだけ足していたら、冗長性は残り、しかも「守られている」と誤解する
コメントが増えていた。

---

## 見つかった中で最も重い 2 件

### 1. R04-F12 が、最も効く場所にだけ適用されていなかった

`publish()` が `commitBuffer()` に capture 時刻を渡しておらず、commit 時点で
打たれていた。実害は 2 つある。

- serialize / memcpy にかかった時間だけ、刻まれる時刻が系統的に遅れる
- 世代切替で再試行するたびに時刻を採り直すため、同じ測定が別時刻になる

本体（scalar / vector）には R04-F12 で入っていたが、**cv::Mat / Lidar2dScanData /
PointCloud2DScanData の 3 つには届いていなかった**。

皮肉なのは、その `publish()` 自身のコメントがこう書いていたことである。

> **世代切替が日常的に起きるのは画像サイズや点数が変わるこちらの方**なので、
> 影響はむしろ大きい（R04-F10）

影響が大きいと認識して書かれた場所に、対になる修正が届いていなかった。

60,000 点のスキャンで実測した結果:

```
修正前: publish() 入口と刻まれた capture 時刻の差  最大 379 us
修正後: 同                                          最大   1 us
```

`subscribeAlignedTo()` はこの値でずれを測るので、差はそのまま融合精度に効く。
Raspberry Pi 4 では更に開く。

### 2. 回帰テストが守っているつもりのものを守れていなかった（3 例目）

`FailedSubscribeMustNotCorruptPreviousValue` は、外からスロットの mutex を
保持して失敗を作る。その状態では `readSample()` が冒頭の `lockSlotWithin()` で
返るので、**dst への memcpy に一度も到達しない**。返り値バッファは書かれよう
がないため、ダブルバッファを丸ごと取り消しても 145 件すべてが緑のままだった。

R03・R05 に続く 3 例目である。「修正を巻き戻すと落ちる」注入実験は各修正で
行ってきたが、それは**バグを入れた側**の検証であって、**テストが対象を
正しく突いているか**は別に確かめる必要がある。

コピー後に失敗する経路（`readSlotInto()` の `payload_size` 検査）を決定的に
作る形へ書き直し、ダブルバッファ無効化で落ちることを確認した。

---

## 適用した内容

| 分類 | 内容 |
|---|---|
| 実害のある修正 | R04-F12 を外部 3 特殊化へ。cv の書かれるだけのメンバ 3 つ（デッドストア兼データ競合）を削除 |
| 不変条件の穴 | `setGenerationTag()` / `markAsInitialized()` を削除 |
| 死んだコード | `getLatestGeneration` / `schema_is_declared` / align ヘルパ 4 つ / `<sched.h>` / 未使用 `fstat` / `doctor` の恒真ガード |
| テスト | 無防備だったダブルバッファを回帰テストで固定。assertion ゼロの 3 件（229 行）を削除。実証済みの重複 2 組を統合。死んだコード 3 箇所 |
| コメント | 条件変数・タイムスタンプ選択・ハッシュの内容など、実装と矛盾していたものを訂正 |

`isBeingWritten()` は呼び出し元ゼロだが**残した**。`getTimestamp_us(int)` が
今も `WRITING_FLAG` を番兵として返す契約なので、素のビット演算を呼び出し側に
書かせないための唯一の手段である。

`LayoutField::align` も冗長だが**消してはならない**。消すとハッシュ値が変わり、
稼働中の全セグメントの `schema_version` が不一致になる。

---

## 構造的な根本原因への第一歩 — 適合性スイート

型に一切依存しない約 150 行が 5 箇所にバイト等価でコピーされている件について、
**共通化の前に「5 つが守るべき契約」を 1 箇所に書いた適合性スイートを用意した**。
実装は 5 箇所のままだが、次に誰かが片方だけ直したら CI が落ちる。

`shm_pub_sub/test/shm_pub_sub_conformance.hpp` は型パラメータ化テストなので、
外部リポジトリは Traits を 20 行書いて実体化するだけでよい。
CMake からは INTERFACE ターゲット `shm_pub_sub_conformance` を link する。

固定した契約は 11 件。

| テスト | 由来 |
|---|---|
| `RoundTripsThePayload` | — |
| `CaptureTimeIsStampedAtPublishEntry` | R04-F12 |
| `PayloadAndSampleInfoDescribeTheSameSample` | R02-F03 |
| `SequenceIsMonotonic` | R01-F05 |
| `TheReturnedReferenceSurvivesOneMoreSubscribe` | ダブルバッファ |
| `ExpiryIsHonouredAndZeroDisablesIt` | — |
| `WaitForTimesOutWithoutAPublish` | — |
| `RetentionWindowCoversWhatWasPublished` | — |
| `SubscribeAtFindsAKnownSample` | — |
| `SubscribeAlignedToHonoursMaxSkew` | R04-F14 |
| `SubscribeAlignedToRejectsAnInvalidReference` | R04-F14 |

### 実際に効くことの確認

capture 時刻の検査は「入口で打ったなら開始側に、commit で打ったなら終了側に
寄る」を**所要時間で正規化して**見るので、機械の速さに依存しない。
判定できるほど publish に時間がかからない型では SKIP して理由を報告する。

R04-F12 修正前の実装（`4de827d~1`）を取り出して回すと、決定的に落ちる。

```
Lidar2D: capture 時刻が publish() の終了側に寄っている。
publish 所要 1462us に対し、入口からの遅れ 1461us
```

**45 コミット・5 回のレビューを生き延びた欠陥を、このスイートは捕まえる。**

### 適用状況

| 特殊化 | 結果 |
|---|---|
| scalar | 11/11（capture 時刻は判定不能で SKIP。固定長なので正常） |
| vector | 11/11 |
| cv::Mat | 11/11 |
| Lidar2dScanData | 11/11 |
| PointCloud2DScanData | 11/11 |

### このスイートが早速見つけたもの

**私が同日の `[doc]` コミットで spec に書いた `SearchPolicy` の表が逆だった。**
`TooOld` / `TooNew` は「指定した時刻が保持範囲に対してどうか」を表すのであって、
サンプル側から見た向きではない。

- `AtOrBefore` で該当なし = その時刻は既に上書きされた → `TooOld`
- `AtOrAfter` で該当なし = その時刻はまだ publish されていない → `TooNew`

実装（`findBufferNum`）と enum の定義コメントが正しく、`spec_jp` / `spec_en` を
直した。契約をテストとして書き下ろす作業が、文書の誤りを 1 件炙り出したことになる。

---

## 未着手 — 共通化そのもの

コピーそのものは残っている。上の適合性スイートは**漏れを検出できる**ように
しただけで、**漏れが起きなくなった**わけではない。

次の段階は、`shm_base` に型に依存しないヘルパ（例: `findAlignedSlot()`）を置き、
5 箇所からそれを呼ぶ形へ寄せることである。3 リポジトリを同時に書き換え、
`shm_base` のヘッダ互換にも触れるので、適合性スイートが緑であることを
安全網にしながら進めるのがよい。

そのほか、特殊化間に残っているずれ:

| 項目 | 本体 | cv | lidar / pc |
|---|---|---|---|
| `existsPublisherMemory()` | あり | 無し | 無し |
| 競合カウンタ | あり | 無し | 無し |
| publish 失敗の通知 | 例外 | 例外 | **`std::cerr` で戻り値 void** |
| ムーブ可能性 | 可 | 不可 | `= default` と書いてあるが実際は不可 |

3 行目は実害がある。lidar / pc の `publish()` は**失敗しても呼び出し側から
検知できない**。全スロットが reader に押さえられて 1 スキャンも書けなくても、
daemon はログを吐きながら「publish 成功」として回り続ける。
