# R01 指摘事項 対策方針書

- 作成日: 2026-08-31
- 対象: [`r01-2026-08-31-review-finding.md`](r01-2026-08-31-review-finding.md) の未対応 Finding
  （F01、F04、F05、F06、F07、F09、F10）
- 前提: F02、F03、F08 は shm_service / shm_action の削除（v2.0.0）で解消済み

## 0. 結論

1. 残る Finding のうち F01・F04・F05・F06・F07 は、いずれも **「共有メモリに
   自己記述的なヘッダが無い」** という一点に帰着する。個別に潰すと共有メモリ形式が
   3回変わり、そのたびに全プロセスの同時入れ替えが必要になる。**形式変更は一度にまとめる。**
2. F01 の根は「稼働中のセグメントを破壊的に再レイアウトする」設計そのものにある。
   検査を増やしても TOCTOU は消えないので、**レイアウト世代ごとにセグメントを分ける**。
3. **複数 Publisher を正式サポートする。** 発行番号を1本の atomic に集約すれば
   順序は一意に定まる。保証と制約を明文化し、回帰テストを置く。
4. **trivially-copyable の必須化（F07-b）は独立トラック**とし、Raspberry Pi 4 での
   検証が済んでから本流へマージする。形式変更とは依存関係が無いので並走できる。
5. F09（munmap 漏れ）と F10（テスト/CI）は形式変更と独立に先行着手できる。

決定の経緯は §8 を参照。

## 1. 実測で確認した再現（対策の根拠）

対策を机上で決めないため、指摘のうち再現可能なものを実際に走らせて確認した。

| # | 入力 | 現在の挙動 | 該当 |
|---|------|-----------|------|
| 1 | `Publisher<int>("t", -1)` | **SIGSEGV** | F06 |
| 2 | `Publisher<int>("t", 1<<28)`（約1GB） | 例外なく成功。上限が無い | F06 |
| 3 | ヘッダだけ残して `ftruncate` した共有メモリに `Subscriber` が接続 | **SIGSEGV** | F06 |
| 4 | `alignas(32)` の型を publish | 例外なく成功。`data_offset` は 8 境界のみ | F07 |
| 5 | standard-layout だがコピーコンストラクタが非 trivial な型 | 例外なく成功し、**代入演算子が走って値が化ける** | F07 |
| 6 | 生ポインタを含む型 | 例外なく成功（他プロセスで無意味なアドレスになる） | F07 |

1 と 3 は「公開 API を普通に呼ぶだけで落ちる」ため、入力検証は最優先で入れる。

### 併せて判明した事実

- **リングバッファの `pthread_mutex_t` と `pthread_cond_t` は一度もロック／待機されていない。**
  `initializeExclusiveAccess()` で初期化されるだけで、`signal()` のポーリング化以降は
  完全な死荷重になっている。レイアウト上 96 バイト前後を占め、再レイアウトのたびに
  無意味に再初期化されている。**新形式ではこの領域をスロット単位の robust mutex に
  置き換えるので、レイアウトの総量はほぼ増えない。**
- `RingBuffer::initializeAlignedPointers()` と `waitForPthreadInitialization()` は
  **宣言だけで定義が無い**（未使用）。`pthread_init_flag` も代入されるだけで参照されない。
- レイアウト変更（再接続）のロジックは **本体・vector 特殊化・`cv::Mat` 特殊化
  （react_cv）・`Lidar2dScanData` 特殊化（sensor_daemons）の4箇所に複製されている**。
  しかも後ろ2つは別リポジトリにある。形式を変えるならこの4つを同時に直す必要があり、
  取りこぼすと「レイアウトの解釈だけが食い違う」という最悪の壊れ方をする。
  → 対策として **レイアウト判断を `RingBuffer` 側に集約し、特殊化から
  connect/disconnect/再生成の手順を書けなくする**（後述 F01-3）。

## 2. 中核対策 A — 自己記述的な共有メモリヘッダ（形式 v2）

現在の先頭領域は `initialization_flag` 以外に何の識別情報も持たず、`element_size` と
`buf_num` を無検証で信じてポインタを組み立てている。ここに固定長ヘッダを置く。

```
Header（固定長・element_size / buf_num に依存しない）
  +0   atomic<uint32_t> state          NOT_INITIALIZED / INITIALIZING / INITIALIZED
  +4   uint32_t  magic                 'SHM2' 定数
  +8   uint16_t  abi_major             非互換変更で増やす
  +10  uint16_t  abi_minor             後方互換な追加で増やす
  +12  uint32_t  header_size           前方互換のため実長を持つ
  +16  uint64_t  total_size            レイアウト全体のバイト数
  +24  uint64_t  element_size
  +32  uint64_t  buf_num
  +40  uint64_t  payload_alignment     alignof(T)
  +48  uint64_t  slot_offset           SlotRecord[] 先頭
  +56  uint64_t  data_offset           ペイロード先頭
  +64  atomic<uint64_t> generation     レイアウト世代
  +72  atomic<uint64_t> sequence       単調増加の発行番号（トピック全体で共通）
  +80  uint64_t  boot_id_hash          再起動をまたいだ残骸を弾く
  +88  予約
  = 128 バイト（64 バイト境界）

SlotRecord[buf_num]（1スロット = 64 バイト＝キャッシュライン、false sharing 回避）
  +0   atomic<uint64_t> sequence               0 = 無効／確定時に発行番号
  +8   uint64_t         capture_monotonic_us
  +16  uint64_t         capture_realtime_us
  +24  pthread_mutex_t  owner                  PROCESS_SHARED | ROBUST
  （x86-64 glibc では sizeof(pthread_mutex_t)=40 なのでちょうど 64）

Payload
  data_offset は max(payload_alignment, 64) 境界
  ストライドは round_up(element_size, payload_alignment)
```

この1つのヘッダが F05（sequence）、F06（magic/version/total_size/検証）、
F07（payload_alignment）、F04（スロット単位 robust mutex）をまとめて解く。

**condition variable は新設しない。** プロセス間 condvar が waiter の異常終了で
壊れる問題（`signal()` のコメント参照）は robust 化できないため、更新検知は
現行どおり atomic のポーリングを維持する。robust *mutex* はカーネルが所有者の死を
検出するので使ってよい（既存テストで `EOWNERDEAD` → `pthread_mutex_consistent()`
の動作は確認済み）。

## 3. 中核対策 B — レイアウト世代ごとにセグメントを分ける【採用】

F01 の TOCTOU は「稼働中の同じセグメントを ftruncate して作り直す」ことに由来する。
検査を増やしても、検査と実アクセスの間の窓は残る。窓を消すには
**一度公開したセグメントのレイアウトを二度と変えない** しかない。

```
/shm_<topic>              ディレクトリセグメント（1ページ固定・伸縮しない）
    magic / abi / atomic<uint64_t> generation
    現世代の element_capacity / buf_num / payload_alignment

/shm_<topic>#<generation> データセグメント
    形式 v2 の本体。generation を公開する前に完全に初期化し、以後レイアウト不変
```

- レイアウト変更 = 「gen+1 のセグメントを作って初期化 → ディレクトリの
  `generation` を release store」。既存セグメントには一切触れない。
- 古い世代を掴んだままのプロセスは、**有効なマッピングの中に書き続ける**だけで
  範囲外アクセスにはならない。次の publish/subscribe で世代の変化に気付いて張り直す。
  つまり最悪でも「一時的に stale」で済み、「破損」にはならない。
- 旧セグメントは猶予期間後に unlink する（`shm_tool` の list/remove も対応させる）。

**この変更が必要な根拠**: レイアウト変更は死んだ機能ではない。`cv::Mat` 特殊化
（画像サイズ変更）、`Lidar2dScanData` 特殊化（点数変更）、`PointCloud2DScanData`
特殊化が実際に実行時再確保を行っている。したがって「レイアウトは生成時に固定」
という単純な解決は採れない。

### 3.1 固定長ではなく「容量」で持つ

世代の作り直しは高価なので、**要素サイズを固定値ではなく容量として扱う**。

- ヘッダは `element_capacity`（スロット1つ分の確保量）を持つ。
- スロットは実長 `payload_size` を持つ（`<= element_capacity`）。
- 再レイアウトは `payload_size > element_capacity` のときだけ。しかも容量は
  **増やすだけ**で減らさない（縮小したいときは明示的に作り直す）。
- 増やすときはヒステリシスを付ける（次の2冪、または +25% など）。

これで LiDAR の点数や画像サイズが毎フレーム揺れても世代が回らない。
スカラの `Publisher<T>` は `element_capacity == sizeof(T)` で固定なので、
そもそも一度も再レイアウトしない。

### 3.2 世代を上げる権利の調停（複数 Publisher 前提）

複数の Publisher が同時に「容量が足りない」と判断し得るので、作成者を1者に絞る。

1. `shm_open("/shm_<topic>#<gen+1>", O_CREAT | O_EXCL)` を試す。
   **成功した1者だけが作成者**になり、初期化まで責任を持つ。
2. `EEXIST` で負けた側は、勝者が作ったセグメントに接続して
   **自分の要求容量を満たすか確認**する。満たせば単に採用する。
3. 満たさない場合（勝者より大きい容量が要る）は gen+2 で再挑戦する。
   ただし **試行回数に上限を設け、超えたら例外**にする。容量が単調増加なので
   通常は数回で収束するが、無限に往復させない。
4. 作成者は初期化完了後に、ディレクトリの `generation` を
   `compare_exchange(gen, gen+1)` で公開する。CAS に負けたら作ったセグメントを
   unlink して、勝者の世代に合流する。

## 3A. 複数 Publisher の正式サポート【採用】

これまで「できてしまう」だけで仕様が無かった状態を、明文化してテストする。

### 保証する性質

1. **全順序**: 発行番号はヘッダの `atomic<uint64_t> sequence` を
   `fetch_add` して得る。Publisher が何本あっても順序は一意に定まる。
2. **番号はコミット直前に取る**。確保時ではなく、ペイロードを書き終えて
   `slot.sequence` を release store する直前に `fetch_add` する。
   こうすると「番号が小さいスロット＝先にコミットされたスロット」が常に成り立ち、
   遅い Publisher が先に取った番号で後から割り込むことがなくなる。
3. **最新値は最大 sequence**。同値は発生しないので、タイブレークの規則が要らない。
4. **スロットの排他はスロット単位の robust mutex**。異なる Publisher が同じ
   スロットへ同時に書くことはない（F04 の対策がそのまま多重化に効く）。

### 明文化する制約

- **`buf_num` は同時に動く Publisher の数より大きくすること。** 等しいか
  少ないと、全スロットが `EBUSY` になって publish が失敗し得る。
  既定の3面のままなら Publisher は2本までが安全。
  → 生成時に上限を検査できないので、**publish 失敗時の例外メッセージに
  「Publisher 数に対して buf_num が不足している可能性」を明記**する。
- **best-effort である**。Publisher の合計レートがリングの回転を上回れば、
  読み手は途中の値を取りこぼす。これは仕様であり、欠落検出は
  `sequence` の飛びで利用者側が判定できる。
- **全 Publisher が同じ型／同じ payload_alignment であること。** 異なる型を
  同じトピックに publish するのは設定誤りとして例外で弾く（ヘッダの
  `element_size` と `payload_alignment` の照合で検出できる）。

## 4. Finding 別の対策

### F01 — 稼働中のレイアウト変更と publish/subscribe の TOCTOU

| | 内容 |
|---|---|
| 原因 | `isLayoutChanged()` の確認後、走査は共有メモリ上の `*buf_num`、書き込み位置は構築時オフセット、という混在。さらに ftruncate が生きたマッピングの下で起きる |
| 規模 | 大 |

1. **世代別セグメント（中核対策 B・採用決定）** を導入し、in-place の破壊的再レイアウトを廃止する。
2. `RingBuffer` は **共有メモリ上の `*buf_num` を走査に使わない**。構築時に確定した
   `expected_buf_num` のみを使う。`getNewestBufferNum()` / `getOldestBufferNum()` /
   `isUpdated()` / `allocateBuffer()` の全ループが対象。
   （世代別セグメントを入れれば理屈上は不要になるが、多重防御として入れる）
3. **レイアウト判断を `RingBuffer` に集約する。** `RingBuffer::reshape(element_size)`
   のような API を用意し、Publisher 特殊化は「必要な要素サイズを渡す」だけにする。
   現在4箇所に複製されている connect/disconnect/再生成の手順を特殊化から消す。
4. publish は **コミット直後にも世代を再確認**し、変わっていたらそのスロットを
   無効化（`slot.sequence = 0`）してから再接続する。

### F04 — 1秒経過した writer を「クラッシュ済み」とみなす

| | 内容 |
|---|---|
| 原因 | 時刻だけを根拠にスロットを奪う。かつ `setTimestamp_us()` が無条件 store なので、奪われた側も「有効」として公開してしまう |
| 規模 | 中 |

**時刻ベースの奪取は完全に廃止する。** `STALE_WRITE_TIMEOUT_US` は削除。

スロットの所有権を **スロット単位の robust mutex** で表す。

- `pthread_mutex_trylock()` が
  - `0` → 獲得
  - `EBUSY` → **生きている writer が使用中。絶対に奪わない**（現在の欠陥はここ）
  - `EOWNERDEAD` → カーネルが所有者の死を確定させた場合のみ。
    `pthread_mutex_consistent()` して `slot.sequence = 0`（内容は壊れている前提）で再利用
- 全スロットが `EBUSY` なら publish は **失敗して例外**（現行の10回リトライを維持）。
  SIGSTOP で止まった writer がいる場合、そのスロットは復帰まで使えないが、
  これは「静かにデータが壊れる」より「うるさく失敗する」ほうが正しい。
- 書き込み中は `slot.sequence = 0` にしておき、完了時に発行番号を release store する。
  途中で死んでも 0 のままなので、読み手には最初から見えない。

これで「停止していた writer が再開して他人のスロットを上書きする」経路が
構造的に消える。ownership token の CAS 検証はこの設計では不要になる。

### F05 — タイムスタンプ重複による「最新」誤選択と seqlock の ABA

| | 内容 |
|---|---|
| 原因 | 発行順の正本が microsecond 時刻そのもので、一意性も単調性も無い |
| 規模 | 中 |

1. ヘッダの `atomic<uint64_t> sequence` を **発行順の正本**にする。
   publish は `fetch_add(1)` で一意な番号を取り、スロットに release store する。
2. 「最新」は **最大 sequence**。同値は起こらないのでスロット番号によるタイブレークは廃止。
3. subscribe の整合性検証は timestamp ではなく **sequence の前後比較**にする。
   sequence は再利用されないので ABA が原理的に起きない。
4. 時刻は検索用の属性として分離し、**monotonic と realtime の両方**を持つ。
   `CLOCK_MONOTONIC_RAW` は同一 boot 内でしか意味を持たないため、タイムマシン機能の
   日時指定には realtime が要る。`isUpdated()` の比較も sequence ベースに変える。
5. 期限判定（`data_expiry_time_us`）は monotonic 側で行い、boot をまたいだ残骸は
   ヘッダの `boot_id_hash` 不一致で弾く。

### F06 — RingBuffer が共有ヘッダを信頼し、mmap 範囲と整数演算を検証しない

| | 内容 |
|---|---|
| 原因 | attach 用コンストラクタがマッピング長を受け取らない。API 境界に上下限が無い |
| 規模 | 中（ただし再現1・3の SIGSEGV を直すので最優先） |

1. **`RingBuffer` の attach コンストラクタでマッピング長を必須引数にする。**
   `SharedMemory::getSize()` は既に実マッピング長を返すので配線するだけで済む。
2. attach 時に順に検証し、いずれか外れたら **例外**（落とさない）:
   `magic` / `abi_major` / `header_size >= sizeof(Header)` /
   `mapping_size >= header_size` / `element_size > 0` /
   `0 < buf_num <= MAX_BUF_NUM` / `payload_alignment` が2の冪 /
   `data_offset + stride*buf_num <= min(total_size, mapping_size)`。
   加減乗算はすべて `__builtin_mul_overflow` / `__builtin_add_overflow` で検査する。
3. **API 境界の入力検証**（再現1・2への直接対策）:
   `buffer_num` は `int` をやめるか、`buffer_num > 0` と現実的上限
   （例: 1024）を必須にする。`element_size` と総サイズにも上限を置く。
   `SharedMemoryPosix` と `disconnectMemory()` は **空文字列を明示的に拒否**する
   （現在は `name[0]` を無条件に触っている）。トピック名の文字種と長さも検証する。
4. `connect()` で `fstat()` の戻り値を確認する（現在は2箇所とも無視）。
   要求サイズに満たないファイルへの接続は失敗として扱う。

### F07 — 型制約と payload alignment が不十分

| | 内容 |
|---|---|
| 原因 | trivially-copyable の強制が ARM のみ。`data_offset` が 8 境界固定 |
| 規模 | 小〜中 |

**F07 は2つに分ける。** アライメントは形式変更に含め、型制約は独立トラックにする。

#### F07-a: payload alignment（形式 v2 に含める）

1. `payload_alignment` をヘッダに持たせ、`data_offset` を `alignof(T)` に揃える。
   attach 側は自分の `alignof(T)` を満たしているか検証する。
2. **代入ではなく `memcpy` に統一する。** 現在は x86 で
   `*reinterpret_cast<T*>(ptr) = data`（未構築領域への代入 = UB）を行っている。
   `memcpy` はアライメント要求が無く、trivially-copyable なら意味も正しい。
   結果として `is_arm_platform()` による分岐が publish / subscribe から消える。
3. `alignof(T)` に上限（ページサイズ）を設け、超える型は拒否する。

#### F07-b: trivially-copyable の全プラットフォーム必須化【独立トラック】

**別ブランチで進め、Raspberry Pi 4 での動作検証が済んでから本流へマージする。**
形式 v2（P2/P3）とは独立に開発でき、順序の依存も無い。

段階的に入れる。

| 段階 | 内容 |
|---|---|
| b-1 | `SHM_STRICT_TYPE_CHECK`（既定 OFF）を追加し、ON のとき `static_assert(std::is_trivially_copyable_v<T>)` を `Publisher`/`Subscriber` に入れる |
| b-2 | 全パッケージを `-DSHM_STRICT_TYPE_CHECK=ON` でビルドし、**コンパイルエラーになる型を洗い出す**（実行不要・網羅的） |
| b-3 | 該当する型を修正する（後述の直し方） |
| b-4 | **Pi 4 実機で全テスト＋実運用シナリオを流し、挙動が変わらないことを確認** |
| b-5 | 既定を ON に切り替え、`std::is_standard_layout` と併せて必須にする |

**現時点の見込み（静的調査の結果）**

- workspace 内で publish/subscribe される型は 78 種。うち generic な
  `Publisher<T>` を通るものについて、定義を追えた範囲では
  **コピーコンストラクタ／コピー代入／デストラクタ／virtual を持つ型は見つからなかった**。
- `SensorStatus` のように「自前の既定コンストラクタを持ち、文字列は
  `char[128]` で保持する」という書き方が徹底されており、これは
  trivially-copyable を壊さない（後述）。
- したがって b-3 の修正はほとんど発生しない見込みだが、**b-2 で機械的に
  確認してから断定する**。ヘッダを追えなかった型と別リポジトリの型が残っている。

**壊れる型の直し方**（該当した場合）

| 症状 | 直し方 |
|---|---|
| `std::string` / `std::vector` メンバ | 固定長配列（`char[N]`）にする。可変長が本質なら特殊化を書く |
| 自前のコピーコンストラクタ／コピー代入 | `= default` にできないか検討する。できなければ特殊化へ |
| 自前のデストラクタ | 解放すべき資源があるなら共有メモリに置けない型。特殊化へ |
| `virtual` 関数 | 共有メモリに置けない（vtable ポインタは他プロセスで無意味）。設計を見直す |

### F09 — SharedMemoryPosix のデストラクタが munmap しない

| | 内容 |
|---|---|
| 原因 | デストラクタが `close(fd)` のみ |
| 規模 | 小（形式変更と独立に着手可能） |

`~SharedMemoryPosix()` から `disconnect()` を呼ぶ。`disconnect()` は既に
`shm_ptr`/`shm_fd` を見て冪等に書かれているので、そのまま使える。

### F10 — テスト/CI が「レースが無いこと」を示す構成になっていない

| | 内容 |
|---|---|
| 規模 | 中（形式変更と独立に着手可能。むしろ先行させる） |

1. **CI に ThreadSanitizer ジョブと UndefinedBehaviorSanitizer ジョブを追加**する。
   UBSan は `-fsanitize=undefined,alignment` を指定する（アライメント違反の検出が要件）。
   *注意*: レビュー時に TSan が `unexpected memory mapping` で動かなかったのは
   実行基盤（WSL2）の ASLR 設定が原因なので、CI では動く見込みが高い。
   ローカル用に `setarch $(uname -m) -R ctest` の手順を README に書く。
2. **ARM64 ジョブを追加**する（QEMU エミュレーション）。ARM のアライメント問題は
   x86 CI では原理的に検出できず、これまで Pi4 実機でしか見つかっていない。
3. **ハングを FAIL にする。** CMake の `set_tests_properties(... TIMEOUT n)` を全テストに
   付け、ctest の呼び出し方に依存しないようにする。`SUCCEED()` で通している
   `CondVarCorruptionTest.SingleProcessKillDuringTimedwait` は明示的な失敗に変える。
4. **ブロックしたスレッドを detach しない。** `tryBroadcast()` は fork した子プロセスに
   置き換える（現在はフィクスチャ破棄と競合し得る）。
5. **決定的なプロセス間注入**を可能にする。`BUILD_TESTS` のときだけ有効な
   フック（`RingBuffer::setTestHook()`）を「レイアウト確認直後」「ペイロード書き込み中」
   「コミット直前」に置き、テストから別プロセスを正確にその位置へ差し込む。
   これが無いと F01・F04 の回帰テストは確率的にしか再現しない。

## 5. 追加で直すもの（レビュー未記載）

| 項目 | 内容 | 段階 |
|---|---|---|
| 死んだ mutex / condvar | 一度もロックされない `pthread_mutex_t` / `pthread_cond_t` / `pthread_init_flag` をレイアウトから削除する。**これはレイアウトが変わるので P0 ではできない**（新旧バイナリが混在できなくなる）。形式 v2 と同時に行う | P2 |
| 未定義の宣言 | `initializeAlignedPointers()` / `waitForPthreadInitialization()` の宣言を削除する | P0 |
| `shm_tool` の未初期化変数 | `main()` の `MODE mode` は `list` でも `remove` でも無い引数のとき未初期化のまま `switch` に入る。既定値と引数チェックを入れる | P0 |
| テストが古い `.so` を掴む | `LD_LIBRARY_PATH` にインストール済みの shm があると、テストがビルドツリーではなくそちらを解決する。`undefined symbol` で落ちるか、もっと悪いと「古い実装で緑になる」。`--disable-new-dtags` で `DT_RPATH` を出す | P0 |

## 5A. 実施状況（2026-08-31）

### 完了: P0 + P1 + T-a b-1

| 項目 | 内容 |
|---|---|
| F06 | `buffer_num` の下限・上限、要素サイズ・総サイズの上限、空／不正なトピック名、`fstat` の戻り値を API 境界で検証。`RingBuffer::validateLayout()` と `attachRingBuffer()` を追加し、既存共有メモリへの接続は必ずマッピング長との突き合わせを通す |
| F01（部分） | `getNewestBufferNum()` / `getOldestBufferNum()` / `isUpdated()` / `allocateBuffer()` の走査範囲を、共有メモリ上の `*buf_num` から構築時スナップショット `expected_buf_num` に変更。TOCTOU 本体は P3 で解消する |
| F09 | `~SharedMemoryPosix()` から `disconnect()` を呼ぶ |
| F10 | `-DSANITIZER=address\|thread\|undefined` を追加。CI に **TSan / UBSan / ARM64(QEMU)** ジョブを追加。全テストに `TIMEOUT` プロパティ。`DT_RPATH` で古い `.so` を掴まないようにする |
| F10 | `tryBroadcast()` の detach したスレッドを fork した子プロセスに置換（detach したスレッドが共有メモリを掴んだまま fixture の munmap と競合していた） |
| T-a b-1 | `-DSHM_STRICT_TYPE_CHECK=ON`（既定 OFF）で `static_assert` による型検査 |
| 雑則 | `shm_tool` の未初期化 `mode`、コマンド名の前方一致、未定義の宣言 2 件 |

### サニタイザ導入で新たに見つかり、修正した実在の不具合

いずれもレビュー R01 には記載が無かったもの。

| # | 内容 | 検出 |
|---|---|---|
| 1 | `SharedMemoryPosix::connect()` が呼ばれるたびに `std::regex` を構築していた。libstdc++ の regex はロケール（ctype）の遅延初期化キャッシュを同期なしで触るため、**複数スレッドからの同時 connect がデータ競合**になる。正規表現は '/' を '_' に置換するだけだったので、単純なループに置換（副次的に再接続経路が速くなる） | TSan |
| 2 | `RingBuffer::timestamp_us` / `data_expiry_time_us` への非同期の読み書き。1つの Publisher / Subscriber を複数スレッドから使うと競合する。共有メモリ上ではなくインスタンス側の変数なので `std::atomic` 化で解決（レイアウト不変） | TSan |
| 3 | 空の `std::vector` を publish / subscribe すると `memcpy` に **ヌルポインタ**を渡していた（長さ 0 でも未定義動作） | UBSan |
| 4 | テストの `recoverMutex()` が `pthread_mutex_lock` の戻り値を見ずに必ず `unlock` していた。`ENOTRECOVERABLE` 等では保持していない mutex を unlock することになる | TSan |
| 5 | `RingBuffer(ptr, element_size=0, buffer_num=N)` が attach 経路に落ち、`buf_num=0` でレイアウトを計算するため **`data_list` が `timestamp_list` と重なる位置**を指していた（要素長 0 の vector Publisher。実害が出る前に作り直されるため潜在） | コード検査 |

### 検証結果

5 構成すべてで **65/65 PASS**。

```text
Release                 65/65
AddressSanitizer        65/65
UndefinedBehaviorSan    65/65
ThreadSanitizer         65/65   ← レビュー時は起動すらできなかった
SHM_STRICT_TYPE_CHECK   65/65
```

TSan の `unexpected memory mapping` は ASLR のエントロピーが原因で、
`setarch $(uname -m) -R ctest ...` で回避できる（CI もこの形にした）。

対策前に実測していた SIGSEGV 2 件は、いずれも例外／失敗返却になった。

```text
buffer_num = -1      → throw: buffer_num must be in [1, 1024], but got -1
buffer_num = 1<<28   → throw: buffer_num must be in [1, 1024], but got 268435456
disconnectMemory("") → throw: shared memory name must not be empty
切り詰めた共有メモリ → subscribe が is_success=false を返す（落ちない）
```

### 見送った項目

- **テストフック**（決定的なプロセス間注入）は P2 へ。挿入したい箇所のコードが
  形式 v2 でそのまま書き換わるため、先に入れても捨てることになる。
- **死んだ mutex / condvar のレイアウトからの削除** は形式変更なので P2。

### T-a b-2 の状況

`-DSHM_STRICT_TYPE_CHECK=ON` で本ライブラリをビルドしたところ、
違反したのは **`BadClass` のみ**だった。これは「不正な型が弾かれること」を
確認するための意図的に不正なテスト用の型で、厳格モードではコンパイル時に
弾かれる（＝検査が実行時からコンパイル時に移った）ため、対応する実行時テストを
`#ifndef SHM_STRICT_TYPE_CHECK` で囲んだ。

**残りの b-2**: `shm_ws` の全パッケージをコンテナ内で
`-DSHM_STRICT_TYPE_CHECK=ON` を付けてビルドし、違反型を洗い出す。
静的調査では違反は見つかっていないので、大きな修正は発生しない見込み。

### 完了: P2（共有メモリ形式 v2）

| 項目 | 内容 |
|---|---|
| F04 | 時刻ベースのスロット奪取を**全廃**。所有権をスロット単位の robust mutex で表し、`EBUSY`（生きている writer）では絶対に奪わず、`EOWNERDEAD`（カーネルが所有者の死を確定）でのみ回収する |
| F05 | ヘッダの単一 atomic から `fetch_add` で採番する **`sequence` を発行順の正本**にした。番号は**コミット直前**に採るので「番号が小さい＝先にコミットされた」が常に成立する。整合性検証も時刻から発行番号に変更し、ABA が原理的に起きなくなった |
| F05 | 時刻を順序から分離し、`capture_monotonic_us`（期限判定）と `capture_realtime_us`（日時指定の検索用）を両方持たせた |
| F06 | magic / ABI 版 / header_size / slot_size / total_size / boot_id を持たせ、attach 時に全オフセットを実マッピング長に対して検証する。ヘッダが自分自身と矛盾している場合も弾く |
| F07-a | `payload_alignment` をヘッダに持たせ、ペイロード先頭を `max(alignof(T), 64)` に載せる。書き込み・読み出しを **memcpy に統一**し、未構築領域への代入（UB）と ARM/x86 の分岐を削除した |
| §3A | 複数 Publisher を正式サポート。回帰テストで全順序・torn read ゼロ・スロット枯渇時の明示的失敗を検証 |
| §5 | 一度もロックされていなかった `pthread_mutex_t` / `pthread_cond_t` / `pthread_init_flag` をレイアウトから削除（スロット単位 robust mutex で置換） |

**新しいレイアウト**

```
ShmHeader (128 バイト固定)
  magic / state / abi_major / abi_minor / header_size / total_size
  element_capacity / buf_num / payload_alignment
  slot_offset / slot_size / data_offset
  generation / sequence / boot_id_hash / reserved[4]

SlotRecord[buf_num]（1スロット = 1キャッシュライン。false sharing 回避）
  sequence (0 = 無効) / payload_size
  capture_monotonic_us / capture_realtime_us
  owner: pthread_mutex_t (PROCESS_SHARED | ROBUST)

Payload  data_offset は max(payload_alignment, 64) 境界
         ストライドは element_capacity（切り上げない）
```

ストライドを切り上げないのは、既存の呼び出し側が `i * getElementSize()` で
オフセットを出しているため。代わりに「capacity は payload_alignment の倍数」を
生成時に要求し、`data_offset` を境界に載せることで全スロットの整列を保証する。

**API の互換性**: 外部の3特殊化（`cv::Mat` / `Lidar2dScanData` /
`PointCloud2DScanData`）が使う API はすべて維持した。
`getOldestBufferNum()` → `allocateBuffer()` → 書き込み → `setTimestamp_us()`
という手順もそのまま動く（`setTimestamp_us()` は `commitBuffer()` の別名として残した）。
新しく `getSequence()` / `getPayloadSize()` / `getCaptureRealtime_us()` /
`commitBuffer()` / `releaseBuffer()` / `getGeneration()` を追加した。
整合性検証は発行番号で行うべきなので、3特殊化は `getSequence()` への移行を推奨する。

**追加した回帰テスト（9本）**

| テスト | 検証内容 |
|---|---|
| `HeaderIsSelfDescribing` | ヘッダの各フィールドが正しく書かれている |
| `ForeignMagicIsRejected` | v1 の領域・ABI 不一致・自己矛盾したヘッダを弾く |
| `SequenceIsUniqueAndMonotonicWithinOneMicrosecond` | 同一 µs の連続 publish でも最後の値が最新として返る |
| `SequenceNeverRepeatsAcrossSlots` | 発行番号が重複しない |
| `OverAlignedPayloadIsPlacedOnItsBoundary` | `alignas(32)` の型が全スロットで境界に載る |
| **`StoppedWriterSlotIsNotStolen`** | SIGSTOP で 1.5 秒止めた**生きた** writer からスロットを奪わない（F04 の核心） |
| `KilledWriterSlotIsReclaimed` | SIGKILL された writer のスロットは回収され、中身は無効になる |
| `MultiplePublishersProduceAConsistentTotalOrder` | 3 Publisher 同時発行で torn read ゼロ |
| `ExhaustedSlotsFailLoudlyInsteadOfCorrupting` | 全スロット占有時は沈黙せず例外 |

既存の `CrashedWriterSlotsMustBeReclaimed` は「タイムスタンプに UINT64_MAX を
書けばマーカーを偽装できる」という v1 前提だったため、実際に fork した子を
SIGKILL する形に書き換えた。

**P2 の実装中に TSan が見つけた自分の設計不備**: `setTimestamp_us()` が
`allocateBuffer()` を経ずに呼ばれると、保持していない mutex を unlock していた。
インスタンスごとにスロットの所有権を追跡し、保持している場合だけ unlock するよう修正。

**検証**: Release / ASan / TSan / UBSan の全構成で **74/74 PASS**。

**バージョン**: 3.0.0。共有メモリのレイアウトが変わるため、同じトピックに
接続する全プロセスを再ビルドして同時に入れ替え、入れ替え前に `shm_tool remove` で
既存の `/dev/shm/shm_*` を消す必要がある。参照側3パッケージの
`SHM_REQUIRED_VERSION` も 3.0.0 に更新した。

### 完了: P3（世代別セグメント）

| 項目 | 内容 |
|---|---|
| F01 | **稼働中セグメントの破壊的な再レイアウトを廃止**。レイアウトを変えるときは既存セグメントに一切触れず、新しい世代を別セグメントとして作り、完全に初期化してから「現在有効な世代」を CAS で進める |
| §3.1 | 要素サイズを固定値ではなく**容量**として扱う。スロットは実長 `payload_size` を持ち、容量は 25% の余裕を付けて**増やすだけ**。LiDAR の点数や画像サイズが揺れても世代が回らない |
| §3.2 | `O_CREAT \| O_EXCL` で作成者を1者に絞る。負けた側は勝者に合流し、それでも足りなければ次の世代へ挑戦（試行回数に上限） |
| §5 | Publisher / Subscriber から connect / disconnect / RingBuffer の作り直しを**全て排除**し、`ShmTopic` に集約した |

**セグメント名**

```
/shm_<topic>        世代 1。データ本体であると同時にディレクトリを兼ねる。
                    ヘッダの latest_generation がトピック全体の正本。
/shm_<topic>#<N>    世代 N (N >= 2)
```

世代 1 をディレクトリと兼用するのは、余分なマッピングを増やさないためと、
レイアウト変更が起きないトピック（スカラ型はこれに当たる）で従来と全く同じ
構成のままにするため。実際、スカラ型では世代 2 以降が作られないことを
テストで確認している。

**要求の食い違いは「増やすだけ」で収束させる**

容量・スロット数・アライメントのいずれも、一致ではなく**包含**で判定する。
一致を求めると、`buf_num` の違う Publisher が同じトピックに繋いだときに
互いに相手のレイアウトを作り直し合って世代が往復し、収束しない
（実際に P3 の実装中にこれで無限ループになった）。スロットが多い分には
誰も困らない（履歴が長くなるだけで、「buf_num は同時 Publisher 数より
大きいこと」という制約にも有利）ので、必ず最大値へ収束させる。

**後片付け**

`disconnectMemory()` は世代 1 しか消さないため、全世代を消す
`disconnectTopic()` を追加した。`shm_tool remove` もこちらを使う。

**追加した回帰テスト（6本）**

| テスト | 検証内容 |
|---|---|
| `ScalarTopicNeverCreatesASecondGeneration` | 固定長トピックでは世代が増えない |
| **`GrowingVectorCreatesANewGenerationWithoutTouchingTheOldOne`** | レイアウト変更時、既存セグメントのヘッダが `latest_generation` 以外 1 バイトも書き換わらない（F01 の核心） |
| `CapacityOnlyGrowsAndShrinkingDoesNotChurnGenerations` | 長さが揺れても世代が回らない。返るのは容量ではなく実際の長さ |
| `StaleParticipantsFollowTheNewGenerationSafely` | 取り残された Publisher が落ちず、現世代へ追随して正しく書ける |
| `PublishersWithDifferentRequirementsConverge` | 要求が食い違う Publisher が有限回で収束する |
| `DisconnectTopicRemovesEveryGeneration` | 全世代のセグメントが消える |

**P3 の実装中に見つけた自分のバグ**

1. `openRoot()` が要求レイアウトで root セグメントを**その場で再初期化**していた。
   P3 で無くしたはずの破壊的再レイアウトそのもの。既に有効な世代 1 があれば
   決して初期化し直さないよう修正。
2. `allocateBuffer()` が `capture_monotonic_us` をクリアしていたため、読み手が
   「発行番号を読む → capture を読む」の間に割り込まれると 0 を読んで
   期限切れと誤判定し、publish が続いているのに「データ無し」を返していた。
   ASan ビルドで 15 回中 4 回再現。`commitBuffer()` が必ず全フィールドを
   書き直すのでクリアは不要と判断して削除し、併せて選択後に発行番号を
   再確認する経路を追加。修正後 40 回連続で再現せず。

**検証**: Release / ASan / TSan / UBSan の全構成を 2 回ずつ、**80/80 PASS**。

## 6. 実施順序

形式変更を1回に束ねることを最優先にする。F07-b だけは独立トラックで並走させる。

| 段階 | 内容 | 形式変更 | 依存 |
|---|---|---|---|
| **P0** | F09（munmap）、F10（TSan/UBSan/ARM64/TIMEOUT/テストフック）、§5 の雑則 | 無し | 無し。単独で着手可 |
| **P1** | F06 の API 境界検証を先出し（`buffer_num<=0`、空名、上限、`fstat` 確認） | 無し | 無し。再現1の SIGSEGV が消える |
| **P2** | **形式 v2 の導入**（中核対策 A）= F04 + F05 + F06 の残り + F07-a | **有り** | P1 |
| **P3** | **世代別セグメント + 複数 Publisher 正式化**（中核対策 B / §3A）= F01 | **有り** | P2 |
| **P4** | タイムマシン機能 | — | P3 |
| **T-a** | **F07-b（trivially-copyable 必須化）** — 別ブランチ | 無し | **並走可。Pi 4 検証後にマージ** |

P2 と P3 はどちらも共有メモリ形式を変えるので、**まとめて1回のリリース（v3.0.0）に
する**ことを推奨する。分けると「同じトピックに繋ぐ全プロセスを再ビルドして同時に
入れ替える」作業が2回発生する。

### 影響範囲（P2・P3 を入れる場合に必ず同時に直すもの）

**レイアウトを直接扱うコードは4箇所しか無い**（調査で確認済み）。
残り約16の `Publisher<X>` は generic 版の明示的インスタンス化にすぎず、
形式変更の影響を受けない。

| 箇所 | リポジトリ／パッケージ |
|---|---|
| `Publisher/Subscriber<std::vector<T>>` | 本体 `shm_pub_sub` |
| `Publisher/Subscriber<cv::Mat>` | `react_cv/shm_pub_sub_cv` |
| `Publisher/Subscriber<Lidar2dScanData>` | `sensor_daemons/lidar_2D_data` |
| `Publisher/Subscriber<PointCloud2DScanData>` | `sensor_daemons/point_cloud_2D_data` |

そのほか:

- 本体: `shm_base`、`tools/shm_tool`（世代付きセグメント名の list/remove）、Python バインディング
- 参照側の `SHM_REQUIRED_VERSION`（3パッケージ）を 3.0.0 に更新
- 既存の `/dev/shm/shm_*` は形式が変わるため、入れ替え時に `shm_tool remove` が必須

**F01-3（レイアウト判断の `RingBuffer` への集約）を P2 で先に済ませておく**と、
上記4箇所が「必要な容量を渡すだけ」になり、P3 の世代管理を4回書かずに済む。

## 7. 検証計画（追加する回帰テスト）

| Finding | テスト |
|---|---|
| F01 | フックでレイアウト確認直後に停止 → 別プロセスが世代を上げる → 再開しても範囲外に書かない／stale を success で返さない |
| F04 | writer を payload 書き込み中に `SIGSTOP` → 1秒超待つ → 別 writer が publish → `SIGCONT` で再開。破損値が有効として公開されないこと |
| F04 | writer を payload 書き込み中に `SIGKILL` → `EOWNERDEAD` 経由でスロットが回収され、途中の値が読めないこと |
| F05 | 同一 microsecond 内の連続 publish で「最後に publish した値」が必ず選ばれること。単一スロット構成でも torn read を success にしないこと |
| F06 | truncated / 巨大値 / 負バッファ数 / 0バッファ / 空名 / magic 不一致 / 版不一致 を例外で拒否し、落ちないこと（再現1・3 の回帰） |
| F07 | `alignas(16/32/64)` の型が正しい境界に置かれること。非 trivial な standard-layout 型とポインタを含む型が拒否されること（再現4・5・6 の回帰） |
| F09 | 大きな vector で Publisher/Subscriber の生成破棄を繰り返し、`/proc/self/maps` の行数と VmSize が増えないこと |
| 複数 Publisher | barrier で N 本の Publisher を同時発行し、(a) sequence に重複が無い (b) 欠番はあっても逆転が無い (c) 読み手が返す値が必ずどれか1本の完全な1メッセージであること |
| 複数 Publisher | `buf_num` と同数の Publisher を張って全スロットを占有させ、明確な例外で失敗すること（沈黙して壊れないこと） |
| 複数 Publisher | 2本の Publisher が異なる容量を同時に要求し、世代が有限回で収束すること。上限超過で例外になること |
| 世代 | 世代を上げている最中に作成者を SIGKILL し、次の Publisher が引き継いで正常化すること。旧世代を掴んだ Publisher が範囲外に書かないこと |
| F07-b | `-DSHM_STRICT_TYPE_CHECK=ON` で全パッケージがビルドできること（コンパイル時検査そのものが回帰テスト） |
| F07-b | Pi 4 実機で shm_base / shm_pub_sub の全テストと実運用シナリオが従来どおり通ること |

## 8. 決定事項（2026-08-31）

| 論点 | 決定 |
|---|---|
| 世代別セグメント（P3）を採るか | **採用する。** F01 を Critical のまま残さない |
| 複数 Publisher を正式サポートするか | **正式サポートする。** §3A に保証と制約を明文化し、回帰テストを置く |
| trivially-copyable の全プラットフォーム必須化 | **独立トラック（T-a）で進める。** `SHM_STRICT_TYPE_CHECK` を既定 OFF で先に入れ、全パッケージのビルドで違反型を洗い出し、**Raspberry Pi 4 実機での検証が済んでから**本流へマージして既定 ON にする |

### 補足: trivially-copyable とは何か

`std::is_trivially_copyable<T>` は「**そのオブジェクトはバイト列を `memcpy` する
だけで正しく複製できる**」という性質である。共有メモリは生のバイト列でしかないので、
これを満たさない型を置くと、コピーコンストラクタや代入演算子が本来やるはずの処理
（深いコピー、参照カウント、確保）が丸ごと飛ばされる。

`std::is_standard_layout` は「メンバの**配置**が C と同じで予測可能」という別の性質で、
コピーの正しさは何も保証しない。現在の実装は x86 でこちらしか見ていないため、
コピーの意味が壊れる型が素通りしている（§1 の再現5）。

**壊すのは次の4つだけ**である。実測値:

| 型の例 | standard_layout | trivially_copyable |
|---|---|---|
| `struct { int a; double b; float c[4]; };` | 1 | **1** |
| 自前の**既定**コンストラクタを持つ型 | 1 | **1**（壊れない） |
| 自前の**コピー**コンストラクタを持つ型 | 1 | **0** |
| 自前の**デストラクタ**を持つ型 | 1 | **0** |
| **`virtual`** 関数を持つ型 | 0 | **0** |

つまり「自分でコンストラクタを書いたら駄目」ではない。
**コピーコンストラクタ / コピー代入 / デストラクタ / virtual** の4つだけが問題になる。
`std::string` や `std::vector` をメンバに持つ型は、内部にポインタを抱えるため
（そのポインタは他プロセスでは無意味なアドレスになる）当然これに該当する。

本 workspace の型は `SensorStatus` のように
「既定コンストラクタは自前、文字列は `char[128]` で保持」という書き方で統一されており、
この形は trivially-copyable を壊さない。静的調査の範囲では違反型は見つかっていないが、
T-a の b-2 で機械的に確認する。
