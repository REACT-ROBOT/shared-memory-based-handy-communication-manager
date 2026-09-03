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

## 共通化 — SubscriberCore

適合性スイートを安全網にして、コピーそのものを解消した。

型に依存するのは **スロットから自分の型へ読み出す処理だけ** である。
そこを `SlotReader`（関数ポインタ + `void*`）として呼び戻す形にすれば、
残りは全部 `shm_base` の `SubscriberCore` に置ける。

`SubscriberCore` が持つもの:

- `ShmTopic` の生成と所有、名前の検証
- 世代への追随（`follow()`）と型の取り決めの照合
- `subscribe()` / `subscribeAt()` の再試行ループ
- `subscribeAlignedTo()` の `InvalidReference` ガードとずれ判定
- `waitFor()` / `getRetentionWindow()` / `setDataExpiryTime_us()`
- 期限の既定値（2 秒）と競合カウンタ

各 `Subscriber<T>` に残るのは `readSlotInto()` と返り値のダブルバッファだけ。

| 特殊化 | 変更 |
|---|---|
| scalar | 216 行 → 37 行 |
| vector | 204 行 → 45 行 |
| cv::Mat / Lidar2dScanData / PointCloud2DScanData | 同様に委譲のみ |

`std::function` ではなく関数ポインタ + `void*` にしてある。ヒープ確保も
`<functional>` への依存も増やさないためで、読み出し経路はセンサのレートで回る。

### 共通化が効くことの確認

`SubscriberCore` の `InvalidReference` ガードを **1 行**壊すと、
scalar と vector の適合性テストに加えて既存の R04 回帰テストまで
**3 件が同時に落ちる**。以前は片方だけ直しても気付けなかった。

### 副次的に閉じたずれ

`existsPublisherMemory()` と競合カウンタは core にあるので、core を使う特殊化には
自動的に付く（外部 3 つには無かった）。期限の既定値も 1 箇所になった。

書かれるだけで読まれていなかったメンバも 5 箇所から落とした
（`current_reading_buffer` ×4、`vector_size` ×1）。

### この作業が掘り当てた既存の不具合

react_cv の回帰スイートを**通しで**実行すると SIGSEGV していた。
共通化とは無関係で、変更前の HEAD でも同一に落ちることを確認済みである。

`PublishMustNotWriteToUnallocatedBuffer` が `allocateBuffer()` でスロットの
robust mutex を確保したまま、`releaseOwnedSlots()` を呼ばずに munmap していた。
`~RingBuffer()` は意図的に解放しない（マッピングが先に消えている場合があるため）
ので、確保した側が明示的に呼ぶ契約である。呼ばないと、スレッドの robust list が
解放済み領域を指したまま残り、後続の**無関係な** `pthread_mutex_trylock` が
SIGSEGV する（R04 で記録された挙動）。

実際、次に走る `RingBufferElementSizeMustBeEightByteAligned` が
`Publisher::publish()` の中で落ちていた。**単体では再現せず、スイートを通しで
実行したときだけ起きる**ため見過ごされていた。shm 本体の同名テストは
`releaseOwnedSlots()` を呼んでいる。

## 共通化 — PublisherCore

Publisher 側にも同じ並びが 5 箇所にあった。「容量を確保 → 世代を控える →
スロットを取る → 書く → コミット → 世代が変わっていないか確認」と、
その外側の再試行ループである。

型に依存するのは **必要な容量の計算** と **スロットへ書く処理** だけなので、
後者を `SlotWriter` として呼び戻す。

`PublisherCore` が持つもの:

- `ShmTopic` の生成と所有、名前と `buffer_num` の検証
- `ensureCapacity()` による世代の用意
- 世代タグの控えと、コミット後の世代確認
- `acquireWritableSlot()` の再試行（全スロット走査 + 1ms 待ち × 3）
- `SHM_FIRE_TEST_HOOK_BEFORE_COMMIT()` → `commitBuffer()` → `signal()`
- **capture 時刻を publish の入口で一度だけ採ること（R04-F12）**
- 書き込み失敗時に `releaseBuffer()` すること

| 特殊化 | 削減 |
|---|---|
| scalar | 175 行 |
| vector | 144 行 |
| cv::Mat / Lidar2dScanData / PointCloud2DScanData | 同様に委譲のみ |

### 失敗時の方針は意図的に core へ持ち込まない

scalar / vector / cv::Mat は例外を投げ、Lidar2dScanData / PointCloud2DScanData は
`std::cerr` に出して続行する、という違いが既にある。後者を例外へ変えると
稼働中の daemon の挙動が変わるので、core は `Result` を返すだけにした。

```
Ok / GenerationChanged / CapacityUnavailable / NoWritableSlot / PayloadRejected
```

`describe()` が例外にも `std::cerr` にもそのまま使える文言を作る。
**方針の違いは残したまま、機構だけを共有した。**

### 効くことの確認

`PublisherCore` の capture 時刻の受け渡しを 1 行壊すと、本体（vector の適合性 +
既存の cutover 回帰）と外部（lidar の適合性）が**同時に**落ちる。
以前は 5 箇所を個別に直す必要があった。

### 副次的に良くなったこと

- lidar / pc の `publish_mutex_` が再試行ループ全体を覆うようになった（以前は 1 回ぶん）
- `serialize()` が失敗したときにスロットを解放するようになった（放置するとリングが縮む）
- 「再試行させないために true を返す」という分かりにくい書き方が無くなった
- cv のスロット内訳（metadata + padding + 画像）に名前が付き、`publishOnce` と
  `readSlotInto` が同じ式を別々に書いている状態が解消された

---

## 未着手

| 項目 | 内容 |
|---|---|
| lidar / pc の publish 失敗 | 戻り値 void・例外なし・`std::cerr` のみで、呼び出し側から検知できない。cv は同条件で例外を投げるので型によって挙動が違う。**共通化で機構は揃ったので、方針を揃えるかは別途の判断**（`bool publish()` を足すのが移行コスト最小） |
| R05 の Low 5 件 | `r05-remediation.md` に判断理由つきで記録済み |

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
