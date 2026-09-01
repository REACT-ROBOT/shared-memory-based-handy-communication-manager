# Shared-memory based Handy-communication Manager 再レビュー所見（R03）

- レビュー日: 2026-09-01（Asia/Tokyo）
- 対象コミット: `1be9c84 [fix] address the second review (R02)`
- 比較基準: `afc7137` および `r02-2026-09-01-review-finding.md`
- 対象範囲: `shm_base`、`shm_pub_sub`、R02 対応テスト、タイムマシン API
- 重点: レースコンディション、SIGSEGV/SIGBUS、初期化・世代切替、payload とメタデータの一貫性

## 結論

R02-F01～F07 への対応により、型 contract、root 初期化所有権、初期化 state 検証、世代番号、旧世代 unlink、既定権限、検証済みレイアウトのローカル保持は大きく改善した。今回実行済みの Release テストは 101/101 件成功した。

ただし、「致命的異常につながる競合条件まで最低限保証できた」とはまだ判定できない。特に次の問題が残る。

- 容量拡張の主体である vector Publisher に、commit 後の世代確認と再発行が実装されていない。旧世代へ成功 commit したサンプルが新世代へ現れない経路がそのまま残る。
- scalar Publisher の事後確認にも、確認直後に cutover される TOCTOU がある。さらに sequence counter は世代ごとなので、旧世代の in-flight commit と新世代の最初の commit が同じ番号を使い得る。
- vector の最新値 `subscribe(bool*, SampleInfo*)` は payload の成功判定後に `SampleInfo` を再取得しており、R02-F03 の不整合窓が残る。同じ経路では `payload_size <= capacity` も検査していない。
- 次世代の孤児回収は「1秒以内に初期化されなかった」ことだけで生存中 creator の segment を unlink し得る。停止・高負荷・major fault と creator death を区別できない。
- sequence 前後確認をしていても、通常メモリの payload を reader と writer が同時に `memcpy` すること自体は C++ の data race である。atomic fence は非 atomic payload の競合を合法化しない。

したがって、タイムマシン API は引き続き実験ブランチ上で回帰テストに利用してよいが、安定版として API を固定・公開する前に R03-F01～F04 を解消することを推奨する。新機能をさらに広げるより、まず generation cutover と sample snapshot の同期方式を確定する段階である。

## R02 指摘への対応状況

| R02 Finding | 判定 | R03 再レビュー結果 |
|---|---|---|
| F01 topic schema / payload 境界 | 一部解消 | contract と scalar、vector `subscribeAt()` の境界検証は改善。vector の通常 `subscribe()` は共通 helper を使わず、過大 `payload_size` の検査がない。R03-F02。 |
| F02 root 初期化所有権 / state | 解消 | root は `O_CREAT|O_EXCL`、loser は完了待ち、attach は `INITIALIZED` 必須になった。timeout 後の root takeover も行わない。 |
| F03 payload / SampleInfo 一体性 | 一部解消 | scalar と vector `subscribeAt()` は一体取得へ変更。vector の最新値 overload は成功後に slot を再読するため未解消。R03-F02。 |
| F04 orphan generation | 危険な形で一部解消 | 残骸からは回復するが、時間だけで生存 creator も回収できてしまう。R03-F03。 |
| F05 cutover / sequence | 未解消 | scalar の事後再発行は改善だが TOCTOU が残り、vector には未適用。counter floor 取得後の旧世代 commit により重複可能。R03-F01。 |
| F06 generation metadata / cleanup | 解消 | own generation の記録・照合と旧世代 unlink が追加された。既存 mapping は unlink 後も維持される。 |
| F07 権限 / live header | 概ね解消 | 既定 0660、レイアウト値のローカル snapshot 化を確認。topic schema の安定性は R03-F05。 |

R02 の軽微所見では、`fstat()` 失敗処理と realtime コメントは修正された。null の `bool*` は scalar では例外化されたが vector では未対応である。

## 重大度

- Critical: 通常の API 誤接続または現実的な競合で SIGSEGV/SIGBUS、mapping 外アクセス、共有同期オブジェクト破壊につながり得る。
- High: 成功した publish の消失、sample ID 重複、異なる sample の payload/metadata 混在、世代切替不能につながり得る。
- Medium: ビルド・運用条件次第で誤接続や可用性低下を招き、上位機能の契約を破る。

## Findings

### R03-F01 — High: generation cutover と in-flight publish がまだ同期されず、成功 sample の消失と sequence 重複が残る

該当箇所:

- `shm_pub_sub/include/shm_pub_sub.hpp:386-459`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:212-261`
- `shm_base/src/shm_topic.cpp:397-440, 509-520`
- `shm_base/src/ring_buffer.cpp:1172-1215`

scalar Publisher は commit 後に `isGeneration(generation_before)` を一度確認し、切替済みなら再発行するようになった。しかし、確認結果が true になった直後から `publish()` が return するまでの間に別 Publisher が root を次世代へ進められる。新世代側の履歴 snapshot がその commit より前なら、旧世代への commit は API 上成功しても新規 Subscriber から見えない。事後確認を増やすだけでは cutover との相互排他が無いため、この窓は閉じない。

さらに、実際に容量拡張を起こす `Publisher<std::vector<T>>::publish()` には、scalar 版の `publishOnce()` 相当、generation の保存、commit 後の確認、再発行のいずれも入っていない。`ensureCapacity()` 後に得た旧 `RingBuffer*` へ commit してそのまま成功を返すため、R02-F05 の競合経路が直接残っている。

sequence も完全には一意になっていない。新世代は作成途中の一時点で旧世代 counter を floor として採用するが、その取得後も旧世代 writer は commit できる。たとえば floor が K の後、旧世代と新世代がそれぞれ次の commit を行うと、両方が K+1 を採番し得る。新旧 mapping に同じ sequence で異なる payload が存在し、`SampleInfo::sequence` の「トピック内で一意・単調」という前提、履歴順序、seqlock の ABA 回避根拠が崩れる。

推奨:

1. sequence allocator を世代 segment ではなく root に置き、全世代で一つの atomic counter を共有する。
2. cutover に writer admission protocol を導入する。旧世代への新規 writer を閉じ、既存 writer 数が 0 になるまで drain し、最終 snapshot 後に root を進める方式が明確である。
3. 代替方式を採る場合も、commit と generation 判定の間に cutover が入れない同期点を設ける。事後の一回確認だけを完了条件にしない。
4. scalar/vector 共通の publish 実装へ寄せ、両方に同じ世代契約を適用する。
5. `allocate 後停止 -> 別 writer が migrate/cutover -> 旧 writer commit` を barrier で固定し、成功 sample の可視性と sequence 一意性を決定的に検査する。

### R03-F02 — High: vector の通常 subscribe に payload 境界検証と SampleInfo の一体取得が適用されていない

該当箇所:

- `shm_pub_sub/include/shm_pub_sub_vector.hpp:321-364`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:369-455`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:489-533`

`readSlotInto()` には `payload_size <= capacity` と `payload_size % sizeof(T) == 0`、payload コピー後の sequence 再確認、同じ snapshot の `SampleInfo` 返却が実装された。`subscribeAt()` はこの helper を利用している。

一方、通常の最新値 `subscribe(bool*)` は旧実装を残しており、`payload_size` を直接読んで `vector::resize()` / `memcpy()` する。過大な値を capacity と比較しないため、slot metadata の破損、低レベル API の誤用、同一権限グループ内の誤書込みがあると mapping 外読出しや過大 allocation につながり得る。

加えて `subscribe(bool*, SampleInfo*)` はまず `subscribe(bool*)` で payload を確定し、その関数から戻った後に `getSampleInfo(current_reading_buffer)` を呼ぶ。この間に 1 スロット ring が再確保されると、payload は N、info は 0 または N+1 になり得る。R02-F03 で再現した構造と同じであり、タイムスタンプに基づく上位処理へ誤った組合せを渡す。

推奨:

- vector の最新値経路も scalar と同様に `subscribe(bool*, SampleInfo*)` を本体とし、全 overload を `readSlotInto()` へ集約する。
- `state/is_success == nullptr` を scalar と同じ `std::invalid_argument` にする。
- scalar/vector、latest/subscribeAt の全4経路について、過大・非整除 payload_size と payload/info 一体性を回帰テスト化する。

### R03-F03 — High: 1秒 timeout だけで「孤児」と判定し、生存中 generation creator を unlink できる

該当箇所:

- `shm_base/include/shm_base.hpp:750-757`
- `shm_base/src/shm_topic.cpp:295-330`
- `shm_base/src/shm_topic.cpp:355-440`

`reclaimOrphanGeneration()` は次世代 segment を開き、初期化が1秒で終わらず、root がまだ旧世代なら unlink する。しかし、どちらの条件も creator の死亡を証明しない。creator は `SIGSTOP`、CPU starvation、大きな segment の page fault、デバッガ停止、履歴移行などで1秒を超えて生存し得る。

次の interleaving が可能である。

1. A が `#N` を `O_EXCL` で作り、同じ inode を mmap したまま初期化に時間を要する。
2. B が1秒後に `#N` の名前を unlink する。A の mapping は生きているため、A は処理を継続できる。
3. A が旧 root から N への CAS を成功させる一方、B は同じ `#N` 名で別 inode を作り直し得る。
4. root が示す N と名前から接続できる N が別実体、未初期化、または一時的に不存在となり、参加者が現世代へ attach できなくなる。

これは R01 で廃止した「一定時間を超えた writer は死んだとみなす」方式と同じ誤判定である。既存回帰テストは意図的に残した1 byte segment を回収できることだけを検査し、生存 creator を盗まないことは検査していない。

推奨:

- 時間だけで自動 unlink しない。creator PID に加えて boot ID と process start identity を記録し、`pidfd` 等で死亡を確認するか、owner death をカーネルが管理する process-shared robust mutex / file lock を recovery ownership に使う。
- 固定名 `#N` の再利用を避け、nonce を含む一意 segment 名を完成後に root へ原子的に公開する方式も検討する。
- 安全な owner-death 判定ができない場合は、自動回収せず明示エラーと `shm_tool remove` を要求する方が安全である。
- creator を1秒以上 `SIGSTOP` しても segment が回収されないこと、各初期化段階で `SIGKILL` した場合だけ回収できることを決定的にテストする。

### R03-F04 — High: sequence を使った再確認だけでは、非 atomic payload の C++ data race は解消しない

該当箇所:

- `shm_pub_sub/include/shm_pub_sub.hpp:413-455, 538-580`
- `shm_pub_sub/include/shm_pub_sub_vector.hpp:246-260, 321-364, 400-427`
- `shm_base/src/shm_topic.cpp:443-506`

reader は `sequence -> metadata -> memcpy(payload) -> fence -> sequence` の順で読み、番号が変化したら結果を捨てる。実機上で torn sample を検出する仕組みとしては有用だが、writer の `memcpy` と reader の `memcpy` が同じ通常メモリへ同時アクセスする可能性は残る。少なくとも一方が write で、atomic でも mutex で同期されてもいないため、C++ memory model 上は data race であり undefined behavior である。

`std::atomic_thread_fence` と前後の atomic sequence は、payload 自体への競合アクセスを atomic に変えない。共有ファイルを別アドレスへ mmap した alias は sanitizer が同一物理領域として追跡できない場合もあり、TSan 成功だけでこの条件を否定できない。`migrateHistory()` の payload コピーにも同じ問題がある。

推奨:

- 最も単純には reader も slot の robust mutex を取得し、lock 後に sequence/metadata を再検証してから payload をコピーする。writer と payload アクセスが相互排他され、C++/pthread の happens-before が成立する。
- reader を待たせられない要件なら、reader count/epoch により writer が参照中 slot を再利用しない方式、または immutable sample block を原子的に切り替える方式を設計する。
- 別 mmap alias を使わず同じ address を共有する単体診断も用意し、TSan が payload 競合を観測できる構成で確認する。

### R03-F05 — Medium: 自動 schema ID は型名 hash であり、メッセージ schema の互換性を表せない

該当箇所:

- `shm_base/include/shm_base.hpp:423-453`
- `shm_base/src/ring_buffer.cpp:149-196`

`type_schema_id<T>()` は `__PRETTY_FUNCTION__` に含まれる型名の hash である。異なる型名の同サイズ型を拒否する改善にはなるが、同じ型名の構造体が別プロセスのビルドで異なる member 意味・packing・feature macro を持つ場合や、同じサイズのまま schema を変更した場合を識別できない。ツールチェインが同じでも protocol version が同じとは限らない。

推奨:

- public API で利用者指定の安定 schema ID / version を必須または優先できるようにする。
- 自動 hash は補助的な既定値と位置付け、異なる binary/configuration 間の互換性保証には使わないことを README に明記する。
- schema ID、payload kind、element size、alignment、ABI major のどれを変えたら新 topic 名または cleanup が必要かを運用契約として定義する。

## 軽微な追加所見

- vector の `subscribe(bool*)` は null をそのまま逆参照する（`shm_pub_sub/include/shm_pub_sub_vector.hpp:369-375, 423-439`）。scalar と同じ例外契約に揃えるべきである。
- `RingBuffer::INIT_WAIT_TIMEOUT_US` 周辺のコメントには timeout 後に再初期化すると残っているが、現実装は takeover せず失敗する。安全な現実装にコメントを合わせると、将来誤って takeover を復活させる危険を減らせる。
- R02 対応テスト `SequenceStaysUniqueAcrossGenerations` は単一 Publisher が順番に grow/publish/subscribe するだけで、counter floor 取得後の旧世代 commit を発生させない。競合保証のテストにはならない。
- `PayloadAndSampleInfoAlwaysDescribeTheSameSample` は scalar だけを対象とするため、実装が分岐している vector の回帰を検出できない。

## タイムマシン機能を導入するタイミング

タイムマシン機能は ring 内の履歴だけでなく、次の基盤契約に依存する。

1. sample ID が世代をまたいで一意である。
2. payload、capture time、sequence が一つの snapshot として読める。
3. generation cutover で成功 publish が欠落しない。
4. reader/writer の payload アクセスが C++ memory model 上も競合しない。

現在はこのうち1～4に残課題がある。したがって、API 利用例と回帰テストを育てる作業は継続してよいが、安定 API 化、互換性の約束、実運用投入は基盤修正後にするのが妥当である。優先順位は `R03-F03（誤回収） -> R03-F01（cutover/sequence） -> R03-F04（payload 同期） -> R03-F02（vector 読出し統合） -> タイムマシン API 固定` を推奨する。

## 今回の実行結果と制約

### Release

```text
cmake -S . -B /tmp/shm-manager-r03-release \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON -DBUILD_EXAMPLES=OFF
cmake --build /tmp/shm-manager-r03-release -j4
ctest --test-dir /tmp/shm-manager-r03-release --output-on-failure --timeout 60 -j1

Result: 101/101 passed, total 18.13 sec
```

Release の既存テスト成功は通常経路の回帰がないことを示すが、上記 Findings の固定 interleaving はテストに含まれないため、競合が無いことの証明にはならない。

### 未実施

OpenAI 側の追加安全確認が繰り返し表示されたため、ユーザー指示に従って追加の fault injection、独自競合 probe、ASan/UBSan/TSan の R03 実行は中止した。本書の R03-F01～F05 はコード上で成立する interleaving と C++/POSIX の同期契約に基づく静的所見であり、今回の追加動的再現結果ではない。

## リリース判定

現時点の判定は **基盤の安定版リリース不可、タイムマシン機能は experimental 継続可** とする。

最低限、次を release gate とすることを推奨する。

- vector latest 読出しを共通 snapshot helper へ統合し、payload 境界を全経路で検査する。
- timeout だけの orphan unlink を廃止する。
- topic-wide sequence と cutover/write admission の同期方式を実装する。
- payload の reader/writer 同期を C++ data-race-free にする。
- 上記 interleaving の決定的回帰テストと Release/ASan/UBSan/TSan を通す。
