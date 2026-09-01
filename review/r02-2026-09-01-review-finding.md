# Shared-memory based Handy-communication Manager 再レビュー所見（R02）

- レビュー日: 2026-09-01（Asia/Tokyo）
- 対象コミット: `afc7137 [change] require trivially-copyable payload types by default (R01-F07-b)`
- 比較基準: R01 対象コミット `23d0296` および `r01-2026-08-31-review-finding.md`
- 対象範囲: `shm_base`、`shm_pub_sub`、テスト、CI、タイムマシン API
- 重点: レースコンディション、SIGSEGV/SIGBUS、初期化・世代切替中の異常終了、履歴とメタデータの一貫性

## 結論

R01 時点から大幅に改善している。通常のレイアウト変更を別セグメントの世代切替にしたこと、スロット所有権を robust mutex にしたこと、一意な sequence を導入したこと、共有メモリ形式を自己記述化したこと、Service/Action を撤去したこと、ASan/TSan/UBSan/ARM64 の CI を追加したことは、いずれも妥当な方向である。今回、Release、ASan、UBSan、TSan の既存テストは各 92/92 件成功した。

ただし、現時点でも「致命的異常につながる条件まで最低限保証できた」とは判定できない。追加診断では次を再現した。

- 同じトピックを小さい型で publish し、大きい型で subscribe すると、型・サイズ不一致を検出せず `memcpy` がマッピング外まで進み、`SIGSEGV`（終了コード 139）になった。
- ヘッダの `state` を `INITIALIZING` にしても `validateLayout()` は成功し、Subscriber はデータを `success=true` で読み出した。初期化途中の mutex を利用し得る。
- 次世代名の未初期化セグメントが残ると、Publisher は同じ世代の `O_EXCL` 作成を繰り返し、容量拡張を回復できなかった。
- 1スロットで Publisher と Subscriber を競合させると、`subscribe(..., SampleInfo*)` が成功した 637,253 回中 1,869 回で、返した payload と `SampleInfo::sequence` が別サンプルになった、または info が無効になった。
- `SHM_STRICT_TYPE_CHECK` を有効にしても、`Publisher<std::vector<NonTrivialElement>>` はコンパイル・構築できた。vector 特殊化には厳格型検査が適用されていない。

したがって、タイムマシン機能は現ブランチ上で回帰テスト対象として維持してよいが、安定版の公開 API としてリリースする段階ではない。まず R02-F01～F05 を解消し、その後に API を固定することを推奨する。

## R01 指摘への対応状況

| R01 Finding | 判定 | 再レビュー結果 |
|---|---|---|
| F01 稼働中レイアウト変更 TOCTOU | 一部解消 | 通常の容量変更は別世代セグメントになり、in-place 再配置は回避された。一方、初回作成競合、世代作成者の異常終了、切替中の in-flight publish は未解消。R02-F02/F04/F05。 |
| F02 Service/Action の pthread 再初期化 | 解消 | `shm_service` / `shm_action` を削除。再導入時は新設計が必要。 |
| F03 Action の mutex 外アクセス | 解消 | Action 撤去により対象コードなし。 |
| F04 生存 writer の誤回収 | 概ね解消 | process-shared robust mutex により、停止中 writer と owner death を区別できる。既存の SIGSTOP/SIGKILL テストも成功。ただし mutex 自体の初期化競合は R02-F02 に残る。 |
| F05 timestamp 重複と ABA | 解消 | atomic sequence が発行順とコピー検証の正本になった。同一時刻の tie-break も定義された。 |
| F06 ヘッダ・入力・mapping 検証不足 | 一部解消 | magic/ABI/offset/overflow/truncate/入力上限は検証される。一方、初期化 state、購読型、slot の payload_size、attach 後の変更は十分に検証されない。R02-F01/F02。 |
| F07 型制約・alignment | 一部解消 | scalar は既定で strict、payload alignment も改善。vector 特殊化と実行時の topic schema 照合が残る。R02-F01。 |
| F08 Service 停止・owner death | 解消 | Service/Action 撤去により対象コードなし。 |
| F09 mapping leak | 解消 | `~SharedMemoryPosix()` が冪等な `disconnect()` を呼び、回帰テストも成功。 |
| F10 テスト/CI不足 | 大幅改善 | TSan/UBSan/ARM64、テスト timeout、形式・世代・タイムマシンテストが追加された。ただし今回再現した異常系は未登録。 |

## 重大度

- Critical: 通常の API 誤接続または現実的な初期化競合で、SIGSEGV/SIGBUS、mutex 破壊、範囲外アクセスにつながり得る。
- High: 異常終了や並行世代切替で回復不能、成功扱いの不整合データ、publish 済みデータの消失につながり得る。
- Medium: 長期運用時の資源枯渇、診断情報の破綻、または信頼境界次第で致命的障害を誘発する。

## Findings

### R02-F01 — Critical: Subscriber が topic schema と payload 境界を検証せず、型不一致で SIGSEGV する

該当箇所:

- `shm_base/include/shm_base.hpp:397-418`
- `shm_pub_sub/include/shm_pub_sub.hpp:486-529, 616-661`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:42-55, 125-143, 244-270, 310-325, 442-453`

共有ヘッダには容量と alignment はあるが、scalar/vector の区別、要素型、schema ID、要素サイズなどの topic contract がない。Publisher は `ensureCapacity()` で自分の要求を満たすが、Subscriber は `follow()` するだけで、自分が読む `sizeof(T)` が現在のスロット容量・実 payload 長に収まるかを検査しない。

scalar 版は共有メモリの容量に関係なく `sizeof(T)` をコピーする。追加診断で `Publisher<uint8_t>` と 1 MiB の型を読む `Subscriber<Huge>` を同じ1スロット topic に接続したところ、subscribe 内の `memcpy` で `SIGSEGV`、終了コード 139 になった。サイズが同じ別型の場合は落ちず、誤ったデータを success として返し得る。

vector 版にも次の不足がある。

- `SHM_STRICT_TYPE_CHECK` の static assertion は primary template 内にあり、`Publisher<std::vector<T>>` / `Subscriber<std::vector<T>>` の特殊化には適用されない。非 trivially-copyable だが standard-layout の要素型が既定設定でも受理された。
- `payload_size <= element_capacity` と `payload_size % sizeof(T) == 0` を確認せず `resize` / `memcpy` する。slot メタデータの破損や異種 Publisher により、過大確保や範囲外読み出しが起こり得る。

推奨:

1. ヘッダに安定した明示的 schema ID、payload kind（scalar/vector/serialized）、要素サイズ、必要 alignment を持たせる。`typeid().hash_code()` のようにビルド間で安定しない値は使わない。
2. Subscriber attach 時に期待 schema と照合し、不一致は例外または `NotConnected` として、payload に一切触れない。
3. scalar は少なくとも `payload_size == sizeof(T)`、vector は `payload_size <= capacity` かつ割り切れることを、sequence と同じスナップショット内で確認する。
4. vector 特殊化にも `is_standard_layout<T>` と `is_trivially_copyable<T>` の compile-time 制約を直接適用する。
5. scalar↔scalar、scalar↔vector、vector 要素型違い、同サイズ別 schema、破損 payload_size を回帰テストにする。

### R02-F02 — Critical: root の初期化所有者が一意でなく、INITIALIZING 状態にも attach できる

該当箇所:

- `shm_base/src/shm_topic.cpp:145-231, 236-255`
- `shm_base/src/ring_buffer.cpp:225-315, 322-405, 458-501, 568-589`

世代 1 の作成は `O_CREAT` であり `O_EXCL` ではない。さらに `RingBuffer::initializeOrAttach()` は magic が無い fresh 領域では CAS をせず `state=INITIALIZING` を store して初期化を開始する。このため、同時に fresh と判断した複数 Publisher が同じ `pthread_mutex_t` を並行して `pthread_mutex_init()` し得る。

magic が既にあり state が `INITIALIZING` の場合も、500 ms 待った複数参加者が CAS なしで `INITIALIZING` を store し、全員が再初期化へ進める。初期化途中に creator が停止・死亡した場合を安全に回収する単一所有者プロトコルになっていない。

加えて `validateLayout()` は state を検査せず、root の既存接続経路と世代 1 の attach は初期化完了待ちを行わない。診断では、有効な segment の state を `INITIALIZING` に変更しても `validateLayout=1` となり、Subscriber は `subscribe_ok=1, value=42` を返した。実際のクラッシュ位置次第では、未初期化または半初期化の robust mutex にアクセスすることになる。

初回に異なる要求レイアウトの Publisher が競合すると、片方が root を初期化した後、もう片方が同じ root を破壊的に再初期化する経路も残る。これは通常時の世代分離で解消した F01 を初期作成窓で再発させる。

推奨:

1. root も `O_CREAT|O_EXCL` または kernel が owner death 時に解放するファイルロック等で初期化所有者を一者にする。
2. loser は state が `INITIALIZED` になるまで待ってから再検証する。timeout だけを根拠に複数参加者が pthread object を再初期化してはならない。
3. 安全な自動 takeover を設計できるまでは、半初期化 root を明示エラーにし、`shm_tool remove` を要求する方が安全である。
4. `attachRingBuffer()` は `state == INITIALIZED` を必須にし、state の acquire load 後に全レイアウトを検証する。検証後にも state が変わっていないことを確認する。
5. header 書込み中、各 slot mutex 初期化中、最後の state store 前で creator を SIGKILL する決定的テストと、異なる要求の同時初回作成テストを追加する。

### R02-F03 — High: payload と SampleInfo が一つのスナップショットになっていない

該当箇所:

- `shm_pub_sub/include/shm_pub_sub.hpp:559-568, 649-674`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:354-363, 435-467`
- `shm_base/src/ring_buffer.cpp:704-735`

payload コピーは sequence の前後比較で検証されるが、成功判定後に `getSampleInfo()` を別操作として呼ぶ。その間に同じスロットを Publisher が再確保できるため、返した payload は sequence N、info は 0 または N+1 という組合せになり得る。

1スロットで payload 内に発行番号と同じ値を入れて照合した追加診断では、success 637,253 回中 1,869 回の不一致を再現した。これはタイムマシンと `subscribeAlignedTo()` に直接影響し、別時刻のセンサ値を「整列済み」として成功扱いする可能性がある。

推奨:

- slot 選択、sequence-before、全メタデータ、payload、sequence-after を一つにまとめた `readSample()` を作り、`before == info.sequence == after != 0` の場合だけ成功させる。
- vector の payload_size 境界検証も同じ retry 内で行う。
- info を要求しない最新値読み出しを含め、コピー済み payload と対応 sequence を Subscriber 側に保存し、後から slot を再読しない。
- payload の識別値と `SampleInfo::sequence` が100万回以上常に一致する競合テストを scalar/vector/subscribeAt の各経路へ追加する。

### R02-F04 — High: 未公開の次世代 segment が残ると、容量拡張を自動回復できない

該当箇所:

- `shm_base/src/shm_topic.cpp:286-345, 437-494`

`createNextGeneration()` は `O_CREAT|O_EXCL` に失敗すると、常に「他プロセスが作成中」とみなす。しかし、その segment を開いて初期化完了を待つ、内容を検証して合流する、creator death を判定して回収する、さらに次の世代へ進む、のいずれも行わない。root の `latest_generation` が進まないため、8回とも同じ名前を作ろうとして失敗する。

診断では、現世代の次の名前で1 byteの未初期化 segment を残して vector を拡張すると、毎回次の例外になり、以後の容量拡張を継続できなかった。

```text
shm::Publisher: could not settle on a layout generation after 8 attempts;
publishers on this topic are probably requesting incompatible layouts
```

これは generation creator が `shm_open` 後、初期化・履歴移行・root CAS 前に SIGKILL された場合に現実に起こる。R01 remediation plan に記載された creator SIGKILL 回復テストも現テスト群にはない。

推奨:

- EEXIST 時は対象世代を開き、有限 timeout で state とレイアウトを検証する。
- root がまだ `from_generation` を指し、作成者が死亡したことを単一 recovery owner が確認できた場合だけ orphan を unlink/recreate する。
- 初期化済みで要求を満たすなら root CAS を完了させて合流し、要求不足なら公開後さらに次世代へ進む。
- EEXIST、EACCES、ENOSPC 等を区別して `last_error_` に残す。
- 新世代作成の各段階で SIGKILL し、次 Publisher が有限時間で復旧するテストを追加する。

### R02-F05 — High: 世代切替と in-flight publish が同期せず、成功した publish の消失と sequence 再利用が起こり得る

該当箇所:

- `shm_pub_sub/include/shm_pub_sub.hpp:374-405`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:169-225`
- `shm_base/src/shm_topic.cpp:313-345, 348-412`
- `shm_base/src/ring_buffer.cpp:1022-1100`

Publisher は publish 冒頭で現世代へ追随した後、slot 確保・payload 書込み・commit まで root の世代を再確認しない。別 Publisher がその間に履歴をコピーして root を新世代へ進めると、先行 Publisher は旧世代へ正常 commit して成功を返すが、新規 Subscriber は新世代しか見ない。そのサンプルは移行に含まれず、公開 API 上は成功したのに消失する。

また、新世代の sequence counter は「移行に成功したサンプルの最大 sequence」までしか進まない。コピー競合で最新サンプルを skip した場合や、切替後に旧世代で commit した場合、旧世代で既に使われた sequence を新世代が再利用できる。`SampleInfo` の「トピック内で一意・単調増加」という契約と矛盾する。

推奨:

- sequence allocator を root の全世代共通カウンタにするか、切替時に少なくとも旧 header counter 以上を引き継ぎ、世代を含む一意な sample ID にする。
- publish commit と cutover に二段階プロトコルを導入する。旧世代の commit を drain/freeze してから最終 snapshot を取る方式、または commit 後に cutover を検出した Publisher が新世代へ再発行してから成功を返す方式が考えられる。
- 「世代確認直後で停止した Publisher」「migration 中に commit」「root CAS 直前/直後」を hook/barrier で固定し、成功 publish の消失ゼロ、sequence 重複ゼロを検証する。
- `migrateHistory()` も R02-F03 と同じ一体化した sample snapshot API を使う。

### R02-F06 — Medium: generation メタデータと旧世代の寿命管理が実装意図に一致しない

該当箇所:

- `shm_base/include/shm_base.hpp:411-417, 633-642`
- `shm_base/src/ring_buffer.cpp:534-550, 625-637`
- `shm_base/src/shm_topic.cpp:313-345`

新しい `#N` segment はゼロ初期化領域から作るため、`initializeContents()` が設定する `header.generation` は常に 1 になる。その後 `createNextGeneration()` が呼ぶ `setLatestGeneration(next)` は、この segment 自身の `generation` ではなく、root だけが正本と説明されている `latest_generation` を変更する。結果として `#2` 以降でも `RingBuffer::getGeneration()` は 1 となり、attach 時にも名前の N と header generation を照合できない。

また、世代を進めた後も旧 `#N` の名前を unlink せず、`disconnectTopic()` まで全世代が `/dev/shm` に残る。容量は25%ずつ増えるため世代数は抑制されるが、大容量画像等で段階的に成長すると、旧 segment の合計で tmpfs を枯渇させ得る。R01 remediation plan の「旧 segment は猶予期間後に unlink」は未実装である。

推奨:

- initializer に明示的な own-generation を渡し、`header.generation=N` を初期化前に確定する。attach 時に segment 名と照合する。
- root CAS 後、attach 競合を考慮した猶予または再試行規則を定めて旧世代名を unlink する。既存 mapping は unlink 後も有効なので、取り残された参加者の安全性は維持できる。
- 連続成長時の `/dev/shm` 使用量と、途中参加者・旧世代参加者を含む長時間テストを追加する。

### R02-F07 — Medium（信頼境界依存）: 既定 0666 と attach 後の live header 参照により、ローカル誤操作が致命傷になる

該当箇所:

- `shm_base/include/shm_base.hpp:178-206`
- `shm_base/src/ring_buffer.cpp:592-607, 1202-1219`
- `shm_base/src/shared_memory.cpp:146-208`

既定権限は user/group/other の read/write、すなわち 0666 である。ネットワーク通信ではないため外部ネットワークから直接到達する問題ではないが、同じホストの別ユーザーや、誤った topic 名を使う別プロセスも header/slot/payload を変更できる。

attach 時の検証後も `getElementSize()` 等は共有ヘッダの live 値を読むため、接続後に header を変更または segment を truncate されると、検証済みという前提が崩れ、範囲外アクセスや SIGBUS につながり得る。R02-F01 の再現は悪意がなくても型取り違えだけで起こる。

推奨:

- 既定を 0600 または用途に応じた 0660 とし、0666 は明示 opt-in にする。運用上の user/group 境界を文書化する。
- attach 後の不変レイアウト値は検証済みのローカル snapshot だけを使い、共有ヘッダの live 値からポインタ・長さを再計算しない。
- trusted local processes のみを対象とするなら、脅威モデルとして明記する。それでも accidental type mismatch は R02-F01 で必ず防ぐ。

## 軽微な追加所見

- `SharedMemoryPosix::isDisconnected()` は `fstat()` の失敗を確認せず `stat.st_nlink` を読む（`shm_base/src/shared_memory.cpp:266-280`）。未初期化値を使わず、失敗を disconnected/error として扱うべきである。
- legacy `subscribe(bool*)` は null を許容する記述がなく、そのまま逆参照する。契約として non-null を明記するか、参照引数または null check に変更するとよい。
- `SlotRecord::capture_realtime_us` のコメントは「日時指定の検索用」だが、公開仕様は realtime を検索に使わないとしている。誤実装を防ぐためコメントを揃えるべきである。
- `SharedMemoryPosix::connect()` を同じオブジェクトで複数回呼ぶと、既存 mapping/fd を先に閉じずに上書きする。現在の内部経路では通常発生しないが、public API の再接続契約を整理するとよい。

## 今回の実行結果

### Release

```text
cmake -S . -B /tmp/shm-manager-r02-release \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=OFF
cmake --build /tmp/shm-manager-r02-release -j4
ctest --test-dir /tmp/shm-manager-r02-release --output-on-failure --timeout 60 -j1

Result: 92/92 passed, total 16.80 sec
```

### AddressSanitizer

```text
cmake ... -DSANITIZER=address
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 ctest ...

Result: 92/92 passed, total 18.18 sec
```

LeakSanitizer は実行環境依存の終了時問題を避けるため無効化した。ASan の登録済みテストは clean だが、型不一致診断は登録テストに含まれず、Release の隔離プロセスで SIGSEGV を再現した。

### UndefinedBehaviorSanitizer

```text
cmake ... -DSANITIZER=undefined
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ctest ...

Result: 92/92 passed, total 20.51 sec
```

### ThreadSanitizer

通常起動では既知の `ThreadSanitizer: unexpected memory mapping` になったため、CI と同じく ASLR を無効化して実行した。

```text
cmake ... -DSANITIZER=thread
setarch x86_64 -R ctest --test-dir /tmp/shm-manager-r02-tsan \
  --output-on-failure --timeout 60 -j1

Result: 92/92 passed, total 52.30 sec
```

TSan 成功は同一プロセス thread の検査として有効だが、別プロセス間の protocol 正当性、creator SIGKILL、論理的に別 sample を組み合わせる競合は保証しない。

### 追加の隔離診断

| 診断 | 結果 |
|---|---|
| `Publisher<uint8_t>` → `Subscriber<1MiB struct>`、1 slot | `SIGSEGV`、exit 139 |
| root state を `INITIALIZING` にして validate/subscribe | `validate=1 subscribe_ok=1 value=42` |
| 未初期化の次世代名を先に作成して vector 容量拡張 | 8回試行後に例外。自動復旧せず |
| 1 slot、payload と `SampleInfo::sequence` を100万回照合 | success 637,253、mismatch 1,869 |
| strict ON で非 trivial な vector 要素型を構築 | コンパイル・構築とも成功（本来拒否すべき） |

診断は `r02_*` の一時 topic と `/tmp` の実行ファイルだけを使い、終了後に共有メモリを削除した。ネットワークアクセスは行っていない。

## タイムマシン機能の導入判断

### 推奨判断

実装を取り消す必要はない。検索方針、保持範囲、monotonic/realtime の役割、範囲外 status、sequence tie-break をコードとテストに落としたことは設計検証として有益である。ただし、現時点では experimental 扱いにし、新しい派生機能を増やすより基盤を先に直す。

公開リリースの blocker は次の順で解消することを推奨する。

1. R02-F01: topic schema と全コピー境界を検証し、型不一致を安全に拒否する。
2. R02-F02/F04: root・次世代の単一 creator と、各段階の SIGKILL 回復を成立させる。
3. R02-F03: payload と SampleInfo を一つの検証済みスナップショットとして返す。
4. R02-F05: cutover 中の成功 publish を失わず、sequence を全世代で一意にする。
5. R02-F06/F07: generation identity、旧 segment 回収、権限・信頼境界を運用可能な形にする。

### 公開前の最低合格条件

- 異なる型・同サイズ別 schema・scalar/vector 取り違えを例外/status で拒否し、SIGSEGV/SIGBUS を起こさない。
- root と次世代作成の全段階で creator を SIGKILL しても、次の参加者が有限時間で復旧するか、明示的かつ安全に停止する。
- payload と SampleInfo の対応が競合100万回以上で一度も崩れない。
- 世代切替直前/最中/直後の publish が、成功したなら新世代から必ず読める。
- sequence が同一 topic の全世代で重複しない。
- Release/ASan/UBSan/TSan と ARM64 の同じ回帰セットが成功する。
- 長時間の段階的容量増加で `/dev/shm` 使用量が無制限に増えない。

この条件を満たした後でタイムマシン API の名称・戻り値・互換性を固定するのが安全である。
