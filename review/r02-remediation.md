# R02 指摘事項 対応記録

- 対応日: 2026-09-01
- 対象: [`r02-2026-09-01-review-finding.md`](r02-2026-09-01-review-finding.md)
- 結果: 指摘 7 件＋軽微 4 件すべて対応。回帰テスト 9 本を追加し、
  Release / ASan / TSan / UBSan の全構成で **101/101 PASS**

レビューが実際に再現した 5 つの診断を、対応後に同じ手順でやり直した結果は次のとおり。

| 診断 | 対応前 | 対応後 |
|---|---|---|
| `Publisher<uint8_t>` → `Subscriber<1MiB struct>` | SIGSEGV (exit 139) | `subscribe ok=0`（拒否） |
| root state を `INITIALIZING` にして validate/subscribe | `validate=1 ok=1 value=42` | `validate=0 ok=0`（拒否） |
| 未初期化の次世代名を残して容量拡張 | 8 回試行後に例外、回復せず | 孤児を回収して拡張成功 |
| 1 slot で payload と `SampleInfo` を照合 | 63 万回中 1,869 回不整合 | 81 万回中 **0 回** |
| strict ON で非 trivial な vector 要素型 | 構築できてしまう | `static assertion failed` |

## R02-F01 — topic schema と payload 境界の検証

ヘッダに **topic contract** を追加した。予約領域に収めたので `ShmHeader` は 128 バイトのまま。

| 項目 | 内容 |
|---|---|
| `payload_kind` | `Scalar` / `Vector` / `Serialized` |
| `element_size` | 要素 1 個のバイト数（Vector なら要素型のサイズ） |
| `schema_id` | 型名を畳み込んだ値。同じサイズの別型を検出する |

- Publisher が記録し、Subscriber は `follow()` の中で照合する。食い違ったら
  **payload に一切触れずに** 失敗する。
- `schema_id` は `typeid().hash_code()` ではなく、コンパイラが埋め込む関数
  シグネチャ（型名を含む）を FNV-1a で畳み込む。同一ツールチェイン内で安定する。
  どちらかが 0 の場合は照合しない。
- scalar は `payload_size == sizeof(T)`、vector は `payload_size <= capacity` かつ
  `payload_size % sizeof(T) == 0` を、sequence と同じスナップショット内で確認する。
- vector 特殊化にも `SHM_ASSERT_SHAREABLE` を直接適用した。汎用テンプレートの
  static_assert は特殊化には適用されないため、これまで素通りしていた。

ABI を 3 に上げた。旧セグメントは magic ではなく ABI 不一致として明確に拒否される。

## R02-F02 — 初期化の単一所有者化

- **`validateLayout()` が `state == INITIALIZED` を必須にした。** 検証の最後にも
  state を読み直し、検証中に作り直されていないことを確認する。
- `RingBuffer` の接続経路も同様に state を確認する。
- **`initializeOrAttach()` から「CAS せずに `INITIALIZING` を store する」経路を削除した。**
  新規セグメントは ftruncate でゼロ埋めされるので state は `NOT_INITIALIZED` になる。
  したがって特別扱いは不要で、常に CAS で判定できる。
- **待ち時間切れで奪わない。** 初期化中に落ちた残骸なのか単に遅いだけなのかを
  時刻から区別できない。奪えば生きている相手が初期化中の pthread object を壊す。
  明示的な例外にして `shm_tool remove` を促す。
- 既に初期化済みでレイアウトが合わない場合も、破壊的に作り直さず例外にする。
  レイアウト変更は世代を分ける仕組み（`ShmTopic`）の役目である。
- **root の作成も `O_CREAT|O_EXCL` にした。** 勝った一者だけが初期化し、
  負けた側は完成を待って接続する。

## R02-F03 — payload と SampleInfo の一体化

`readSlotInto()` を作り、**発行番号 → メタデータ → payload → 発行番号** の順に読んで
前後が一致した場合だけ成功とする。以前は成功判定の後に `getSampleInfo()` を
別操作として呼んでいたため、その間に publisher が同じスロットを再確保でき、
「payload は N、info は N+1」という組合せが返り得た。

`subscribe()` / `subscribe(bool*, SampleInfo*)` / `subscribeAt()` の 3 経路とも
この 1 つの関数を使う。scalar / vector / 3 特殊化すべてに適用した。

## R02-F04 — 孤児セグメントからの回復

`createNextGeneration()` が `EEXIST` を他の失敗と区別し、次を行うようにした。

1. 対象世代を開き、初期化完了を有限時間（既定 1 秒）待つ。
2. 完了すれば孤児ではないので、そのまま合流する。
3. 待っても未初期化で、かつ **root がまだ切り替わっていない** ことを再確認できた
   場合だけ、作成途中で死んだ残骸として unlink して再試行する。
4. `EEXIST` 以外は `strerror` を `last_error_` に残す。

## R02-F05 — 世代切替と in-flight publish

- **発行番号を世代をまたいで一意にした。** 新世代の初期化時に、旧世代の
  カウンタ以上から始める（`adoptSequenceFloor()`）。移行が一部失敗しても、
  切替直後に旧世代で commit が走っても重複しない。
- **コミット後に世代を確認し、切り替わっていたら新世代へ発行し直す。**
  これをしないと、切替の隙間に旧世代へ commit したサンプルが
  「成功したのに誰にも読まれない」ことになる。試行回数には上限を設けた。

## R02-F06 — 世代の識別と旧世代の寿命

- **初期化時に自分の世代番号を明示的に渡す。** 以前は「今の値 + 1」だったため、
  ゼロ埋めの新セグメントでは `#2` でも `#3` でも常に 1 になっていた。
  attach 時にセグメント名の N とヘッダの `generation` を照合する。
- **世代を進めた後、旧世代を unlink する。** 名前を消してもマッピングは
  生き続けるので、旧世代を掴んだままの参加者は安全に読み書きを続けられる。
  世代 1（root 兼ディレクトリ）は決して消さない。
  実測で、世代が 22 まで進んでも `/dev/shm` に残るのは 2 個だけになった。

## R02-F07 — 権限と live header 参照

- **既定権限を 0666 から 0660 にした。** other を落とす。別ユーザー間で共有が
  必要な場合は同じグループにするか、新設の `PERM_ALL` を明示的に渡す。
- **接続後は検証済みのスナップショットだけを使う。** `getElementSize()` /
  `getBufferNum()` / `getPayloadAlignment()` / `getGeneration()` が共有ヘッダの
  live 値を読まないようにした。接続後にヘッダを書き換えられても、
  ポインタや長さの計算が壊れない。

## 軽微な指摘

| 指摘 | 対応 |
|---|---|
| `isDisconnected()` が `fstat()` の失敗を無視 | 失敗時は「切断」として扱う（安全側） |
| `subscribe(bool*)` の null 契約が不明確 | 非 null を明記し、null なら例外 |
| `capture_realtime_us` のコメントが公開仕様と不一致 | 「記録専用、検索には使わない」に修正 |
| `connect()` の再入で既存 mapping/fd を上書き | 先に `disconnect()` する |

## 追加した回帰テスト（`shm_pub_sub_contract_test.cpp`、9 本）

`SubscribingWithABiggerTypeIsRejectedInsteadOfCrashing` /
`SubscribingWithADifferentTypeOfTheSameSizeIsRejected` /
`ScalarAndVectorAreNotInterchangeable` / `MatchingTypeStillWorks` /
`SegmentBeingInitializedIsNotUsable` /
`PayloadAndSampleInfoAlwaysDescribeTheSameSample` /
`OrphanedNextGenerationIsReclaimed` / `SequenceStaysUniqueAcrossGenerations` /
`SupersededGenerationsAreUnlinked`

## 入れ替え時の注意

ヘッダの形式が変わったため **v3 以前のセグメントとは接続できない**。
同じトピックに繋ぐ全プロセスを再ビルドして同時に入れ替え、入れ替え前に
`shm_tool remove` で既存の `/dev/shm/shm_*` を消すこと。

既定権限の変更（0666 → 0660）にも注意する。デーモンが別ユーザーで動いている
構成では、同じグループに揃えるか `PERM_ALL` を明示する必要がある。

## 残っている論点

- 別コンパイラでビルドしたプロセス間では `schema_id` が一致しない。
  共有メモリは同一マシン内の通信なので実用上は問題にならないが、
  ツールチェインを混ぜる運用は想定していない。
- 「creator を各段階で SIGKILL する決定的テスト」は、hook を入れないと
  タイミングを固定できないため未実装。孤児回収と単一所有者化で
  回復できることは確認したが、段階ごとの網羅ではない。
