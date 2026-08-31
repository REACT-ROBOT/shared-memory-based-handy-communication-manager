# Shared-memory based Handy-communication Manager レビュー所見（R01）

- レビュー日: 2026-08-31
- 対象コミット: `23d0296 [fix] align the shared memory layout of shm_service and shm_action`
- 対象範囲: `shm_base`、`shm_pub_sub`、`shm_service`、`shm_action`、テスト、CI 設定
- 重点: レースコンディション、範囲外アクセス、SIGSEGV/SIGBUS、デッドロック、プロセス異常終了からの回復

## 結論

現時点では「通常条件の回帰テストは充実してきている」が、「致命的異常につながる条件まで最低限保証できている」とは判定できない。

Release ビルドの既存テストは 61/61 件成功し、ASan ビルドでも主要スイートは成功した。一方、共有メモリのレイアウトを稼働中に変更する際の TOCTOU、Action の mutex 外アクセス、初期化途中のプロセス死、長時間停止した writer の誤回収、壊れた共有メモリヘッダ、過大/負のバッファ数など、SIGSEGV、SIGBUS、データ破壊、永久待ちにつながる経路が未検証のまま残っている。

したがって、タイムマシン機能の公開実装は重大所見を解消してから行うことを推奨する。ただし、時刻・順序・保持期間・検索結果の仕様策定は今すぐ始めるべきである。特に現行の microsecond タイムスタンプは重複するため、現在の形式のまま検索 API を追加すると後から共有メモリ形式を再変更することになる。

## 重大度

- Critical: 現実的な並行実行や再起動で、範囲外アクセス、データ破壊、永久待ちに直結し得る。
- High: 特定の入力、停止時間、型、破損状態で致命的障害または誤データ成功を起こし得る。
- Medium: 長時間運用やテスト方法の不備により、障害の見逃しまたは資源枯渇につながる。

## Findings

### R01-F01 — Critical: 稼働中のレイアウト変更と publish/subscribe の間に TOCTOU がある

該当箇所:

- `shm_pub_sub/include/shm_pub_sub.hpp:262-324`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:167-230`
- `shm_base/src/ring_buffer.cpp:535-552`

`publish()` は冒頭で `isLayoutChanged()` を一度確認した後、共有ヘッダからバッファ数を読み、インスタンス生成時に計算した `timestamp_list` / `data_list` を使って書き込む。その確認直後に別 Publisher が vector 長、型サイズ、バッファ数の異なるレイアウトへ再初期化すると、先行 Publisher は古いオフセットと新しいヘッダ値を混在させる。

起こり得る結果:

- 別領域または mmap 範囲外への書き込み
- タイムスタンプ領域とデータ領域の相互破壊
- Subscriber が壊れた値を `is_success=true` で返す
- SIGSEGV/SIGBUS

既存の `SHMLayoutTest` はレイアウト変更後に古い Publisher/Subscriber を順番に再利用しており、「変更チェックと実データアクセスの間」に変更を差し込むテストではない。

推奨:

- 稼働中の破壊的なレイアウト再初期化を禁止するか、世代番号と排他的なレイアウト変更プロトコルを導入する。
- publish/subscribe 全体で同じ世代を使ったことをコミット時にも検証する。
- vector 長変更をサポートするなら、同じ共有メモリを in-place で再配置しない設計も検討する。
- 変更チェック直後に別プロセスを停止・再開させる決定的なプロセス間テストを追加する。

### R01-F02 — Critical: Service/Action が既存の pthread オブジェクトを無条件に再初期化する

該当箇所:

- `shm_service/include/shm_service.hpp:171-197, 220-251`
- `shm_action/include/shm_action.hpp:196-228, 241-271`

Server の生成時に、共有メモリが既に存在するか、他プロセスが mutex/condition variable を使用中かを確認せず、`pthread_mutex_init()` と `pthread_cond_init()` を同じ領域へ再実行している。初期化フラグ、magic、ABI version、型サイズ、総サイズの照合もない。

POSIX pthread オブジェクトを使用中または初期化済みのまま再初期化する動作は安全ではない。Server の再起動、二重起動、旧バイナリとの混在、型の異なる Client 接続で、永久待ち、内部状態破壊、範囲外アクセスが起こり得る。Client は自分の `ServiceLayout` / `ActionLayout` が実際の mmap サイズ内かも検証していない。

推奨:

- 共通ヘッダに magic、ABI version、total size、各型サイズ/アラインメント、初期化状態、世代番号を持たせる。
- `NOT_INITIALIZED -> INITIALIZING -> INITIALIZED` を単一所有者だけが進めるプロトコルにする。
- 既存レイアウトが一致する場合は pthread オブジェクトを再初期化しない。
- 不一致時は利用者がいないことを保証して新しい名前/世代へ切り替える。
- Server 二重起動、初期化中 SIGKILL、Server 再起動中の Client 待機、型/版不一致をプロセス間テストにする。

### R01-F03 — Critical: Action の述語とデータが mutex 外で読み書きされ、通知取りこぼしも残る

該当箇所:

- `shm_action/include/shm_action.hpp:274-355`
- `shm_action/include/shm_action.hpp:413-492`

具体例:

- `sendGoal()` は `goal_ptr` と `goal_timestamp_us` を `goal_mutex` なしで更新する。
- `waitForResult()` は `result_timestamp_us` を mutex 外で評価してから mutex を取得して待つ。
- `rejectNewGoal()`、`setPreempted()`、`getResult()`、`getStatus()`、`cancelGoal()` に保護されない共有アクセスがある。
- result/feedback/status/cancel が一つの一貫したスナップショットになっていない。

これにより lost wakeup、torn read、別 Goal の結果の取り違え、データ競合が起こり得る。`MultipleClientsTest` は Client を 300 ms ずつずらし、3件中2件成功で合格するため、同時競合の正しさを保証していない。

推奨:

- 条件変数の述語は、更新側・待機側の双方が同じ mutex を保持して読み書きする。
- Goal ID/sequence を要求と結果に持たせ、Client が自分の結果だけを受理できるようにする。
- status、result、feedback、cancel の所有 mutex と一貫性規則を明文化する。
- barrier で同時発行する複数 Client、通知直前/直後の停止、結果取り違えゼロを検証する。

### R01-F04 — High: 1秒経過した writer を「クラッシュ済み」とみなすため、生存 writer 同士が同じスロットへ書ける

該当箇所:

- `shm_base/include/shm_base.hpp:349-356`
- `shm_base/src/ring_buffer.cpp:396-460`

書き込み中マーカーが1秒より古いと無条件に再取得可能になる。大きなデータ、ページフォルト、高負荷、デバッガ停止、`SIGSTOP`、リアルタイム優先度差などで writer が1秒以上停止しても、プロセスは生存しており後で処理を再開する。

再開した旧 writer は、新 writer が再取得した同じ payload へ並行書き込みし、最後に通常タイムスタンプを store する。現在は「自分のマーカーをまだ所有しているか」を commit 時に CAS 確認していない。このため壊れた payload が有効データとして公開され得る。

推奨:

- 時刻だけを根拠に生存 writer のスロットを奪わない。
- process-shared robust mutex など、owner death を実際に検出できる方式を優先する。
- 少なくとも ownership token を commit 時に CAS 検証する。ただし、再取得後に旧 writer が payload 書き込みを再開する問題もあるため、token 確認だけでは完全ではない。
- writer を `SIGSTOP` で1秒超停止し、別 writer が発行後に再開するプロセス間テストを追加する。

### R01-F05 — High: タイムスタンプ重複により「最新」の誤選択と seqlock の ABA が起こり得る

該当箇所:

- `shm_pub_sub/include/shm_pub_sub.hpp:319-324, 463-525`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:229-230, 350-389`
- `shm_base/src/ring_buffer.cpp:351-393`
- `shm_pub_sub/test/shm_pub_sub_timestamp_test.cpp:77-115`

公開時刻は microsecond 分解能の `getCurrentTimeUSec()` そのもので、直前値より必ず大きくする処理がない。既存テスト自身も同一 microsecond の重複を既知の限界として認識し、意図的にクロックが進むまで待っている。

影響:

- 同値時はスロット番号で「最新」が決まり、最後に publish した値とは限らない。
- 単一バッファで `timestamp_before == timestamp_after` となる ABA が起きると、コピー中に書き換えられても整合性確認を通過し得る。
- タイムマシン検索で同時刻データの順序を定義できない。

推奨:

- 共有ヘッダに単調増加する `sequence` を追加し、順序と seqlock 世代の正本にする。
- capture time は検索用属性として sequence と分離する。
- 同一時刻の連続 publish、単一スロット、ラップ直前を決定的にテストする。

### R01-F06 — High: RingBuffer が共有ヘッダを信頼し、mmap 範囲と整数演算を検証しない

該当箇所:

- `shm_base/src/shared_memory.cpp:70-110`
- `shm_base/src/ring_buffer.cpp:21-107, 114-170`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:319-323, 335-342`

既存 RingBuffer へ attach するコンストラクタは、共有メモリ上の `element_size` と `buf_num` を読み、その値からポインタを構成するが、マッピングサイズを受け取らず、`data_offset + element_size * buf_num` が範囲内かを確認できない。破損/途中作成/異なる版の共有メモリでは任意の範囲外ポインタが生成される。

また、Publisher の `buffer_num` に正数制約がなく、負値は `size_t` へ変換され巨大なループ/オフセットになる。サイズ加算・乗算の overflow 検査もない。空文字列を `SharedMemoryPosix` や `disconnectMemory()` に直接渡すと `name[0]` を範囲外参照する。

推奨:

- RingBuffer attach 時に mapping size を必須引数にし、全オフセット、個数、乗算/加算を checked arithmetic で検証する。
- `buffer_num > 0` と現実的な上限、`element_size > 0`、総サイズ上限を API 境界で検証する。
- magic/version/header checksum または少なくとも header length を追加する。
- truncated file、巨大値、負バッファ数、0バッファ、空名、壊れた初期化フラグを death test/例外テストにする。

### R01-F07 — High: 型制約と payload alignment が不十分で、正規のテンプレート引数でも UB/SIGSEGV になり得る

該当箇所:

- `shm_base/src/ring_buffer.cpp:90-99`
- `shm_pub_sub/include/shm_pub_sub.hpp:176-195, 294-317, 347-365, 493-511`
- `shm_service/include/shm_service.hpp:166-189`
- `shm_action/include/shm_action.hpp:183-217`

`std::is_trivially_copyable` の強制は ARM のみで、x86/x64 では standard-layout だけを条件に、未構築の mmap 領域へ代入演算子を実行する。standard-layout でも非 trivial なコピー、デストラクタ、内部ポインタを持つ型は作れるため、共有メモリへそのまま配置できる保証にはならない。

また Pub/Sub の `data_list` は最大8バイト境界にしか揃えていないが、x86 側は `alignas(16)` 以上の `T` も直接 `T*` に cast して代入する。コンパイラが aligned 命令を使えば SIGSEGV になり得る。

推奨:

- 全プラットフォームで trivially-copyable を最低条件とする。
- payload alignment をレイアウトメタデータに含め、`alignof(T)` を満たす。
- 非 trivial 型は明示的な serializer/deserializer 経路へ分離する。
- `alignas(16/32/64)`、非 trivial standard-layout、ポインタを含む型の拒否テストを追加する。

### R01-F08 — High: Service の停止処理とプロセス死回復が安全でない

該当箇所:

- `shm_service/include/shm_service.hpp:97-100, 200-217, 254-308`
- `shm_service/include/shm_service.hpp:234-250`
- `shm_action/include/shm_action.hpp:253-270`

`shutdown_requested` は `volatile bool` だが、volatile はスレッド同期にならず、デストラクタと worker thread の間にデータ競合がある。さらにデストラクタは broadcast 後すぐ `pthread_cancel()` を行うため、worker が共有 mutex を保持した cancellation point で終了すると、非 robust mutex が永久ロックされ得る。

Service/Action の mutex は `PTHREAD_MUTEX_ROBUST` ではなく、Client/Server が保持中に SIGKILL されると以後の利用者が永久待ちになる。戻り値 `EOWNERDEAD` 等を処理するコードもない。

推奨:

- 停止フラグを `std::atomic<bool>` または同じ mutex で保護する。
- 通常は通知して join し、強制 cancellation に依存しない終了プロトコルにする。
- process-shared mutex を robust 化し、全 lock 箇所で戻り値と `EOWNERDEAD` を処理する。
- request/result mutex 保持中の Client/Server SIGKILL と、その後の復旧をテストする。

### R01-F09 — Medium: SharedMemoryPosix のデストラクタが munmap せず、長時間プロセスで mapping が蓄積する

該当箇所:

- `shm_base/src/shared_memory.cpp:62-68, 113-139`

デストラクタは fd を close するだけで `munmap()` しない。Publisher/Subscriber は既定デストラクタで `SharedMemoryPosix` を破棄するため、明示的に `disconnect()` しない通常の寿命終了でも mapping がプロセス終了まで残る。

短命テストでは見えにくいが、長時間プロセスで Publisher/Subscriber/Client を再生成すると仮想アドレス空間と VMA を消費し、最終的に mmap 失敗や性能劣化につながる。

推奨:

- `~SharedMemoryPosix()` から冪等な `disconnect()` を呼ぶ。
- 大きな vector を使った生成/破棄ループで `/proc/self/maps` または VmSize が増えないことを検証する。

### R01-F10 — Medium: 現行テスト/CIは「レースがない」ことを証明する構成になっていない

確認事項:

- CI の sanitizer は AddressSanitizer のみで、ThreadSanitizer/UBSan はない。
- `CondVarCorruptionTest.SingleProcessKillDuringTimedwait` は broadcast が永久ブロックしても `SUCCEED()` する。
- `tryBroadcast()` はブロックした thread を detach するため、その後の unmap/fixture 破棄と競合し得る。
- Service の複数 Client テストは50 msずつずらしている。
- Action の複数 Client テストは300 msずつずらし、3件中2件成功で合格する。
- timestamp テストは重複時刻を避けており、既知の ABA 条件を検査していない。
- ASan は範囲外アクセスには有効だが、通知取りこぼしやデータ競合の保証にはならない。

推奨:

- 決定的な barrier/hook を設け、問題となる命令間へ別プロセスを差し込む。
- 同一プロセス thread テストだけでなく、`fork/exec` した別プロセスで検証する。
- TSan が使えるジョブ、UBSan、32/64-bit、ARM64 実機またはエミュレーションを追加する。
- ハングする可能性のある全テストに外側 watchdog を置き、ハングを明確な FAIL にする。
- 「N件中一部成功」を正しさの合格条件にしない。

## 良い点・既に検証されている範囲

以下は今回確認できた改善点であり、維持すべき回帰テストである。

- timestamp を atomic にし、書き込み中マーカーを設けている。
- Subscriber がコピー前後の timestamp を比較し、失敗時に直前の成功値を壊さないダブルバッファを持つ。
- buffer 確保失敗時に未確保領域へ書かない。
- 後発 Publisher が同一レイアウトの既存 timestamp を消さない。
- レイアウト変更を通常の逐次実行で検知して再接続する。
- ARM の `shm_service` / `shm_action` 配置で各フィールドの alignment を計算している。
- Pub/Sub の condition variable 破損問題を避けるため、更新待ちを atomic timestamp の polling に変更している。
- Release の登録済みテスト 61 件は全件成功した。

ただし、これらは Findings に記載した割り込み位置、異常終了、入力破損、型境界まで保証するものではない。

## 今回の実行結果

### Release

```text
cmake -S <source> -B /tmp/shm-manager-r01-release \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=OFF
cmake --build /tmp/shm-manager-r01-release -j4
ctest --test-dir /tmp/shm-manager-r01-release --output-on-failure --timeout 20 -j1

Result: 61/61 passed, total 24.56 sec
```

### AddressSanitizer

`-DDEBUG=ON` で ASan ビルドに成功。LeakSanitizer はこの実行基盤の ptrace 制約で終了時に fatal となるため、`ASAN_OPTIONS=detect_leaks=0` で主要スイートを個別実行した。

```text
RingBufferTest.WaitForTimeout: 1/1 passed
SHMPubSubRaceTest:            6/6 passed
SHMServiceTest:              12/12 passed
SHMActionTest:               10/10 passed
```

この結果は実行された経路で ASan の異常を検出しなかったことを示すが、未実行の割り込み位置やデータ競合を否定しない。

### ThreadSanitizer

TSan ビルドには成功したが、この実行基盤では起動時に `FATAL: ThreadSanitizer: unexpected memory mapping` となり有効な診断結果を取得できなかった。したがって TSan clean とは判定していない。

## タイムマシン機能の導入判断

### 推奨判断

公開 API の実装は、少なくとも R01-F01、F03、F04、F05、F06、F07 を解消し、対応する回帰テストが通った後に行う。Service/Action を同じリリース品質として扱うなら F02、F08 もリリースゲートに含める。

一方、次の設計作業は先に行う。

1. 「時刻」の定義を決める。
   - 現在の `CLOCK_MONOTONIC_RAW` は同一 boot 内だけで意味を持ち、日時指定には使えない。
   - wall-clock 指定が必要なら realtime/capture time も保持する。
2. 順序と時刻を分離する。
   - `sequence`: 一意な発行順、整合性検証、同時刻 tie-break 用。
   - `capture_time`: 検索用。必要なら monotonic と realtime の両方。
3. 検索意味を決める。
   - nearest、at-or-before、at-or-after のどれか。
   - 同距離時の tie-break。
   - oldest より前/newest より後の場合の戻り値。
   - 読み出し競合時に retry、NotFound、Contended のどれを返すか。
4. 保持範囲を明示する。
   - 「任意時刻」ではなく「リングに残っている保持期間内の時刻」である。
   - デフォルト3面では履歴用途として短すぎる可能性が高い。
5. 共有メモリ形式を version 化する。
   - magic、ABI version、header/payload size、alignment、slot count、generation、sequence、capture time を含める。

### 実装前の最低合格条件

- 同時刻 publish を含めても一意な順序で最新値を選べる。
- wrap 中に nearest 読み出しを連続しても torn data を成功扱いしない。
- writer を payload 書き込み中に SIGKILL/SIGSTOP しても破損値を返さず、規定通り復旧する。
- vector resize/レイアウト世代変更と検索を同時実行しても範囲外アクセスしない。
- 壊れた/truncated header を例外または失敗で拒否し、落ちない。
- target が oldest 前、newest 後、同距離、同一 timestamp 複数件の結果が仕様通りである。
- 複数 Publisher を正式サポートするか、単一 Publisher 制約にするかが明文化され、その条件のプロセス間テストがある。

## 修正優先順

1. 共有メモリ header/version/size 検証と入力上限（F06、F07）
2. レイアウト変更プロトコルの固定（F01）
3. slot ownership と sequence の再設計（F04、F05）
4. RAII cleanup と sanitizer/故障注入テストの整備（F09、F10）
5. 上記の新形式を基盤としてタイムマシン機能を実装

（F02、F03、F08 は shm_service / shm_action の削除で解消。下記「対応状況」を参照）

## 対応状況

### 2026-08-31 — shm_service / shm_action を削除（v2.0.0）

F02、F03、F08 は shm_service / shm_action だけに存在する所見であり、いずれも
「共有メモリ上の pthread オブジェクトを安全に扱えていない」という設計の根に
由来する。修正するには共有ヘッダの世代管理、robust mutex、Goal ID を持つ
一貫したスナップショット、cancellation に依存しない終了プロトコルを入れる、
実質的な作り直しが必要になる。

一方、利用状況を調べた結果、この 2 つには利用者が居なかった。

- REACT-simulator ワークスペース全体、および `~/git_ws` 配下の全リポジトリを
  対象に `shm_service.hpp` / `shm_action.hpp` の include、`ServiceServer` /
  `ServiceClient` / `ActionServer` / `ActionClient` の使用を検索したが、
  本リポジトリ自身のサンプル・テスト・マニュアル以外の該当は無かった。
- 同一リポジトリ内でも shm_base / shm_pub_sub / tools は shm_service /
  shm_action を一切参照していない。
- 外部リポジトリからの参照は react_cv / sensor_daemons の CMakeLists にある
  「必要バージョンの根拠」を説明したコメントのみで、コードの依存は無かった。

したがって作り直しではなく撤去を選び、次を削除した。

- `shm_service/`、`shm_action/` 一式（実装、サンプル、テスト）
- 対応するマニュアル（`tutorials_shm_service_*.md`、`tutorials_shm_action_*.md`）
  と、他マニュアル／README 中の記述・リンク
- ルート `CMakeLists.txt` の `add_subdirectory()`、Doxygen の EXCLUDE 指定

公開 API の削除なのでバージョンを 2.0.0 に上げ、参照側 3 パッケージ
（`shm_pub_sub_cv`、`lidar_2D_data`、`point_cloud_2D_data`）の
`SHM_REQUIRED_VERSION` も 2.0.0 に更新した。削除後のテストは 57/57 成功。

再導入する場合は、本レビューの F02、F03、F08 を解消した設計で新規に作ること。

残る所見 F01、F04、F05、F06、F07、F09、F10 は shm_base / shm_pub_sub 側の
問題であり、未対応のまま残っている。
