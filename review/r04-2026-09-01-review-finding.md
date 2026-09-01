# R04 レビュー所見

- 実施日: 2026-09-01
- 対象コミット: `067806a [fix] address the third review (R03)` / ブランチ `add-race-condition-regression-tests`
- 実施体制: 観点を分けた **3 名の独立レビュアー**による並行レビュー
  1. 並行性・同期・クラッシュ耐性
  2. API 設計・型安全・入力検証・メモリ安全・後方互換
  3. テストの実効性と対応記録の主張の検証
- 環境: x86_64 22core / gcc 11。Raspberry Pi 4 (aarch64) での確認は未実施
- レビュアーはリポジトリを変更していない（読み取り専用）。再現コードは scratchpad のみ

## 総括

R03 で据えた 3 つの不変条件（ノンス付き世代名 / root 一元採番 / reader もスロット mutex）のうち、
**ノンス付き世代名は完全に機能している**（誤削除・衝突・消し残しの経路は見つからず）。
**payload の data race も解消している**（TSan 106 テストで警告 0 件）。

一方で次の 4 つが判明した。

1. **R01〜R03 が 3 回とも見落としていた Critical が 2 件ある**（use-after-munmap、容量不足のまま成功を返す）。
   前者の引き金は `shm_tool remove` と Publisher の再起動で、**R03 の移行手順そのもの**である。
2. **R03 の対応が持ち込んだ回帰が 4 件ある**。うち「reader もロックを取る」方式は、
   data race を消すという目的は達成したが、**実測で購読スループット 3.6 分の 1・publish 遅延 12〜18ms** という
   代償が大きく、Pi4 の 100Hz 制御ループには通らない。方式そのものの再検討が要る。
3. **R03 の対応が「本体の読み出し経路」に閉じており、外部特殊化・Python バインディング・
   運用面・schema 契約に届いていない**。容量拡張が日常的に起きるのはまさに外部特殊化である。
4. **R03 の対応記録に書いた「Release / ASan / TSan / UBSan の全構成で 106/106 PASS」は再現しない。**
   Release `-j1` を 17 回回して 2 回 SEGFAULT、`ctest -j4` は 5 回中 5 回失敗する（R04-F01 が原因）。
   さらに**バグ再注入により、R03 の中心的な修正（F04 の reader ロック）と cutover 三点セットには
   回帰テストが 1 本も無い**ことが実証された。R03-F04 を丸ごと取り消しても 106/106 PASS し、
   TSan の警告も 0 件のままである。

**リリース判定**: 本体・外部特殊化とも実機投入不可。

> **記録の訂正**: `review/r03-remediation.md` の「検証結果」節に書いた 106/106 PASS は、
> 単発の `-j1` 実行 1 回ずつの結果であり、**反復実行とフレーキー確認を怠っていた**。
> 本書の R04-F23 を参照。

---

## 指摘一覧

| ID | 深刻度 | 概要 | 由来 | 実証 |
|---|---|---|---|---|
| [R04-F01](#r04-f01) | Critical | root 張り直しで古いマッピングを munmap し `ring_` がぶら下がる | R03 以前から | ✅ 決定的 |
| [R04-F02](#r04-f02) | Critical | `ensureCapacity()` が容量不足のまま true を返し範囲外書き込み | R03 以前から | ✅ 決定的 |
| [R04-F03](#r04-f03) | Critical | `PointCloud2DScanData::deserialize()` に長さ検査が無い | R03 以前から | ✅ ASan |
| [R04-F04](#r04-f04) | High | cv Python が ndarray の次元数を検査せず範囲外読み出し | R03 以前から | ✅ ASan |
| [R04-F05](#r04-f05) | High | `Lidar2dScanData::deserialize()` の境界検査が桁溢れ・例外漏れ | R03 以前から | ✅ 決定的 |
| [R04-F06](#r04-f06) | High | reader が死ぬだけで publish 済みサンプルが破棄される | **R03 の回帰** | ✅ 決定的 |
| [R04-F07](#r04-f07) | High | 世代 2 以降で `Contended` が `Empty` に化ける | **R03 の回帰** | ✅ 30.5% 誤答 |
| [R04-F08](#r04-f08) | High | reader ロックにより writer が飢餓・publish 遅延が 12〜18ms | **R03 の回帰（設計）** | ✅ 実測比較 |
| [R04-F09](#r04-f09) | High | `shm_schema<T>` が既存セグメントでは一切照合されない | **R03 の対策が無効** | ✅ 決定的 |
| [R04-F10](#r04-f10) | High | 外部特殊化 3 つに commit 後の世代確認が入っていない | **R03 の対応漏れ** | ✅ grep 0 件 |
| [R04-F11](#r04-f11) | Medium | 切替後のドレインが定常状態で 1 件も取り込めない | **R03 の機構が無効** | ✅ 論証 |
| [R04-F12](#r04-f12) | Medium | 世代切替で同じ測定値が 2 つの発行番号で二重に現れる | **R03 の回帰** | ✅ 594 万中 1 件 |
| [R04-F13](#r04-f13) | Medium | 恒久的な不整合でも `openRoot()` が 1 秒待つ | R03 以前から | ✅ 決定的 |
| [R04-F14](#r04-f14) | Medium | `subscribeAlignedTo` の既定が安全側でない | R03 以前から | ✅ 決定的 |
| [R04-F15](#r04-f15) | Medium | null `state` の契約が 5 実装で 3 通りに分裂 | **R03 の対応漏れ** | 静的 |
| [R04-F16](#r04-f16) | Medium | Python バインディングが型を区別できない | R03 以前から | ✅ 決定的 |
| [R04-F17](#r04-f17) | Medium | 仕様書が形式 v1 のまま。`SHM_VERSION` も据え置き | R03 以前から | 静的 |
| [R04-F18](#r04-f18) | Medium | `shm_tool list` が世代・ABI・contract を表示しない | R03 以前から | 静的 |
| [R04-F19](#r04-f19) | Low | トピック名に `#` を許すため世代名と衝突し別トピックを消す | **R03 の新規** | ✅ 決定的 |
| [R04-F20](#r04-f20) | Low | scalar / vector / 外部特殊化で公開 API が揃っていない | R03 以前から | 静的 |
| [R04-F21](#r04-f21) | Low | 作成中に死んだ世代セグメントが次の切替まで残る | R03 の設計上の割り切り | ✅ 実測 |
| [R04-F22](#r04-f22) | Low | `sequence_source` が別オブジェクトのマッピングへの生ポインタ | **R03 の新規** | 静的 |
| [R04-F23](#r04-f23) | High | 「全構成 106/106 PASS」が再現しない。`-j1` で 2/17 SEGFAULT、`-j4` は全滅 | **R03 の記録が誤り** | ✅ 反復実行 |
| [R04-F24](#r04-f24) | High | R03-F04 と cutover 三点セットに回帰テストが 1 本も無い | **R03 の対応漏れ** | ✅ 再注入 |
| [R04-F25](#r04-f25) | High | `SupersededGenerationsAreUnlinked` の前提 ASSERT が完全に空振り | **R03 の回帰** | ✅ 決定的 |
| [R04-F26](#r04-f26) | Medium | `PayloadAndSampleInfo…` は「info が別サンプル」を検出できない | R03 以前から | ✅ 再注入 |
| [R04-F27](#r04-f27) | Medium | `ContentionCounters…` の期待値変更はカバレッジの回帰 | **R03 の回帰** | ✅ 再注入 |
| [R04-F28](#r04-f28) | Medium | `ctest -j` で走らない。ワーカースレッドの例外が `std::terminate` | R03 以前から | ✅ 5/5 失敗 |
| [R04-F29](#r04-f29) | Low | `DEFAULT_PERM = 0660` が umask で 0640 になり、グループ共有が成立しない | R03 以前から | ✅ 実測 |

---

# 1. 並行性・同期・クラッシュ耐性

> 初回 `ctest` で 4 テストが SEGFAULT した。原因は同時刻に別作業が `/dev/shm/shm_*` を掃除していたことだが、
> **それが SIGSEGV になること自体が R04-F01 である**。単独実行なら Release/TSan とも 106/106 PASS する。

## R04-F01

### Critical: root 張り直しで古いマッピングを munmap し、`ring_` がぶら下がる（use-after-munmap）

**該当**: `shm_base/src/shm_topic.cpp:206`(`root_ring_.reset()`), `:230`(`root_ = std::move(existing)`),
参照側 `:604`(`follow`), `:635`(`ensureCapacity`)

`openRoot()` の早期 return 条件は `root_ != nullptr && root_ring_ != nullptr && !root_->isDisconnected()`。
`isDisconnected()` は `st_nlink <= 0`、つまり **トピック名が unlink されたら真**になる。
その場合 `attach_existing()` が新しい inode を mmap し、`root_ = std::move(existing)` で
**古い `SharedMemoryPosix` のデストラクタが munmap する**。しかし世代 1 の `ring_`（と `root_ring_`、
および `ring_->sequence_source`）は古いマッピングを指したままで、`follow()` / `ensureCapacity()` は
次の行でそれを逆参照する。

さらに `attach_existing()` が一度でも失敗すると（`validateLayout` が "still being initialized" を返す等）
`root_ring_` が null のまま `root_` だけ残るため、**以後必ずこの経路に入る**。

成立する入力（いずれも運用上ふつうに起きる）:

- `shm_tool remove <topic>` / `disconnectTopic()` / `rm -f /dev/shm/shm_*`
  （**R03 の remediation 自身が「更新後は一度 remove せよ」と指示している**）
- Publisher プロセスの再起動（unlink → 同名で再作成）

**実証（決定的・100% 再現）**

```text
publisher 側: step1 subscribe ok=1 v=11 / unlinking / publisher recreated
  → SIGSEGV at ring_buffer.cpp:1501 (isLayoutChanged)
     #1 ShmTopic::ensureCapacity  shm_topic.cpp:635
subscriber 側:
  → SIGSEGV  #1 ShmTopic::follow  shm_topic.cpp:604
```

`header` の値は 0x7ffff7bcf000（munmap 済み領域）、`current_tag_ = 0x1000000000000`（世代 1）。
pre-R03 (`1be9c84`) でも再現するので R03 由来ではないが、**R01〜R03 で一度も指摘されていない**。

**推奨**: root を差し替える前に `ring_` / `data_` / `root_ring_` / `current_tag_` を必ず破棄・無効化する
（`ring_.reset(); data_.reset(); root_ring_.reset(); root_ = std::move(existing); current_tag_ = 0;` の順）。
`follow` / `ensureCapacity` は root を張り直した事実を検出して必ず `attachGeneration` からやり直す。
`sequence_source` の再バインドも同時に必要。

## R04-F02

### Critical: `ensureCapacity()` が容量不足のまま true を返し、vector publish が範囲外書き込みする

**該当**: `shm_topic.cpp:189`(`std::min(target, MAX_ELEMENT_SIZE)`),
`shm_topic.cpp:663-669`（`createNextGeneration` 成功で即 `return true`、要求充足を再確認しない）,
`shm_pub_sub_vector.hpp:282-285`（memcpy は `sizeof(T)*vector_size` を無条件に書く）

**実証**

```text
ensureCapacity(1200000000) -> 1
  実際の element_size = 1073741824  (要求 1200000000, 不足 126258176 bytes)
```

end-to-end では `free(): invalid size` でアボート — **126MB のヒープ外書き込み**が実際に起きる。

**推奨**: `ensureCapacity` は成功を返す前に必ず `ring_->getElementSize() >= want` を確認する。
`publishOnce` 側でも memcpy 直前に容量検査を入れる。

## R04-F06

### High: reader が死ぬだけで publish 済みサンプルが破棄される

**該当**: `shm_base/src/ring_buffer.cpp:1291-1298`

R03-F04 で **reader もスロットの robust mutex を取る**ようになった結果、`EOWNERDEAD` は
「writer が書き込み中に死んだ」を意味しなくなった。しかし `readSample()` は無条件に
`s->sequence.store(0)` して有効データを捨てる。

```cpp
if (r == EOWNERDEAD) {
  pthread_mutex_consistent(&s->owner);
  s->sequence.store(0, std::memory_order_release);   // ← reader が死んだ場合も消す
  ...
```

**実証**: publish(4242) 後、reader がロック保持中に死亡させると

```text
正常時        : ok=1 v=4242   保持サンプル数 = 1
reader 死亡後 : ok=0          保持サンプル数 = 0   ← publish 済みデータが消えた
```

以後 publish されるまで全 Subscriber がデータを読めない。
Pi4 で subscriber ノードが OOM killer や再起動で落ちた瞬間に最新センサ値が消える。

**推奨**: writer が書き込み中に死んだ場合は `allocateBuffer()`（`ring_buffer.cpp:1220`）が既に
`sequence=0` にしているので、`readSample` 側で消す必要はない。EOWNERDEAD では
`pthread_mutex_consistent()` だけ行い、そのまま既存の `sequence == 0` 判定に任せる。

## R04-F07

### High: 世代 2 以降で `header->sequence` が常に 0 → Empty と Contended を取り違える

> API 観点のレビュアーも独立に同じ結論に到達した（そちらの記述は後段）。

**該当**: `ring_buffer.cpp:1245-1246`（commit は `sequence_source`=root を使う）と
`ring_buffer.cpp:1056`（`findBufferNum` は自セグメントの `header->sequence` を見る）の不整合。
公開 API `getSequenceCounter()`（`ring_buffer.cpp:1258-1262`）も同じ。

R03-F01 の採番一元化により、世代 2 以降のセグメントの `header->sequence` は**永久に 0**。
`findBufferNum()` はこの値で「一度も publish されていない (Empty)」と
「たまたま全スロットが読めなかった (Contended)」を区別しているため、
世代 2 以降では競合が必ず `Empty` と報告される。
`subscribeAt()` は `Empty` を終端扱いにして再試行しない（`shm_pub_sub.hpp:707-717`）。

**実証**

```text
接続世代 = 3 / header->sequence = 0 / スロット発行番号 = 2
subscribeAt: Success=13894  Empty=6106  Contended=0
```

publish 継続中にもかかわらず **20,000 回中 6,106 回 (30.5%) が「データが 1 件も無い」と誤答**した。
タイムマシン API の契約が壊れている。

**影響範囲**: `Publisher<std::vector<T>>` は容量 0 で世代 1 を作り、最初の publish で必ず世代 2 へ進む。
cv::Mat / lidar / point_cloud も遅延確保なので同様。**可変長トピックは例外なく世代 2 以降**である。

**回帰テストの穴**: `shm_pub_sub_timemachine_test.cpp:226 ContentionIsDistinguishedFromMissingData` は
**スカラ `Publisher<Msg>`（世代 1）** で書かれているためこの経路を通らない。

**推奨**: `RingBuffer` に `currentSequence()`（= `sequence_source ? sequence_source->load() : header->sequence.load()`）
を用意し、`findBufferNum()` と `getSequenceCounter()` の両方をそれに向ける。
回帰テストを vector トピック（世代 ≥ 2）でも回す。

## R04-F08

### High: reader がロックを取るようになり、writer が単一 reader に止められて publish() が例外になる

**該当**: `shm_pub_sub.hpp:424-441`, `shm_pub_sub_vector.hpp:258-275`
（10 回リトライがいずれも `getOldestBufferNum()` の返す**同一スロット**）,
`ring_buffer.cpp:1156-1172`（`getOldestBufferNum` はロック状況を見ず決定的）,
`ring_buffer.cpp:47-64`(`lockSlotWithin`, 2ms)

**実証 1（決定的）**: buf_num=3 で reader が 1 スロットを保持しただけで、残り 2 スロットが空いているのに

```text
publish 例外 (32 ms 後): Could not allocate a buffer (all buffers are in use).
                          buffer_num must be greater than the number of concurrent publishers
```

エラーメッセージは二重に誤り（全バッファは使用中ではないし、buffer_num を増やしても直らない）。
時間検索型の Subscriber（`subscribeAt(AtOrBefore, ...)`）は最古スロットを読むので、
writer が狙うスロットと**恒常的に**衝突する。

**実証 2（R03 前後の比較, 3 秒）**

| 構成 | subscribe 成功 (旧→新) | publish 成功 (旧→新) | publish 最大遅延 (旧→新) |
|---|---|---|---|
| buf=3, reader 8 | 6,880,507 → **1,927,104** | 578,880 → 427,702 | 1.38ms → 1.04ms |
| buf=1, reader 4 | 10,978 → 924,050 | 645,132 → **150,435** | 1.08ms → **12.7ms** |
| buf=1, reader 32 | — → 691,530 | — → **10,665** | — → **18.7ms** (subscribe 最大 44.8ms) |

既定構成でも購読スループットが 3.6 分の 1、buf_num=1 では発行が 4〜80 分の 1 に落ちる。
Pi4 の 100Hz 制御ループに対して 10ms 超のブロックは許容できない。
加えて mutex に `PTHREAD_PRIO_INHERIT` が無く（`ring_buffer.cpp:746-753`）、
`lockSlotWithin` は `sched_yield` スピンなので **SCHED_FIFO 下では最大 2ms の優先度逆転**が起きる。

**推奨**:
(a) 確保リトライは全スロットを走査する、
(b) `PTHREAD_PRIO_INHERIT` を設定する、
(c) reader を待たせない方式（reader epoch / immutable block 切替）を再検討する、
(d) 少なくとも `subscribe` の最悪遅延 5×`SLOT_LOCK_TIMEOUT_US` を API 契約として明記する。

> **注**: R03-F04 のレビュー推奨は「最も単純には reader も slot の robust mutex を取得し…」だったが、
> 同時に「reader を待たせられない要件なら、reader count/epoch により writer が参照中 slot を
> 再利用しない方式、または immutable sample block を原子的に切り替える方式を設計する」とも書かれていた。
> 実測の結果、**このライブラリは後者の要件に該当する**ことが判明した。

## R04-F11

### Medium: `migrateHistory` のドレインが `adoptSample` の失敗を無視する

**該当**: `shm_topic.cpp:570-571`（返り値を捨てて `highest` を無条件に進める）,
`ring_buffer.cpp:1353-1369`（`adoptSample` は `sequence==0` の空きスロットしか使わない）

初回移行で新世代の全スロットが埋まると（`new_buf_num = max(old, requested)` なので定常運転では必ずそうなる）、
`createNextGeneration:512-523` の 4 巡ドレインは**原理的に 1 件も取り込めない**。
それでも `migrated_up_to` は進み `drained <= migrated_up_to` で break するため、取りこぼしが静かに確定する。
**R03-F01(b) の機構は定常状態で実質無効**である。

**推奨**: `adoptSample` の失敗を伝播し、失敗したサンプルを `highest` に含めない。
あるいは新世代側で最古スロットを追い出して取り込む。

## R04-F12

### Medium: 世代切替で同じ測定値が 2 つの発行番号・2 つの時刻で二重に現れる

R03-F01 の (b) ドレインが旧世代の commit を元の sequence/capture 時刻のまま取り込み、
(c) `publishOnce` が同じ `data` を**新しい** sequence/時刻で再発行する
（`shm_pub_sub.hpp:459-461`, `shm_pub_sub_vector.hpp:293-295`）ため、
両方が成立すると同一測定が 2 サンプルになる。

**実証**: 4 秒 / 5,946,430 読み取り中 1 件

```text
payload=12094 -> sequences: 12095 12096
```

発行番号の一意性は保たれるが、履歴・タイムマシンから見ると
**同じ測定が別時刻に 2 回起きたように見える**。

**推奨**: 再発行時に元の sequence/capture 時刻を引き継ぐか、
ドレインと再発行のどちらか一方に責務を寄せる。

## 見て問題なかった領域（並行性）

- **ENOTRECOVERABLE / robust mutex の遷移**: 「所有者死亡 → EOWNERDEAD → consistent せずに再度死亡」を
  作ったが、glibc は所有者死亡では ENOTRECOVERABLE に遷移せず EOWNERDEAD を返し続け、
  publish/subscribe は回復した。`consistent` を呼ばずに `unlock` する経路もコード上に無く、
  スロットが恒久的に死ぬ経路は見つからなかった。
- **`unlinkStaleGenerations()`**: 削除条件（`shm_topic.cpp:414-415`）は自分で CAS した直後の
  `live_tag` に対して判定するため、現役セグメントを消す経路は無い。作成中の競合相手のセグメントを
  先に消すことはあるが、その相手は CAS に必ず負けて自分で unlink するため実害なし。
- **ノンス衝突 / 世代番号 16bit**: `O_EXCL` + ヘッダ `segment_nonce` と名前の二重照合
  （`shm_topic.cpp:355-360`）で塞がっている。`MAX_GENERATION` は容量が幾何級数的に増える設計上到達せず、
  到達しても例外で止まり破損しない。
- **スロット mutex 間のデッドロック**: 2 つのロックを同時に持つ経路は無い。ロック順序の問題は成立しない。
- **payload の data race**: TSan 106 テストで警告 **0 件**。R03-F04 の対応は目的を達成している。
- **メモリ順序**: `commitBuffer` の relaxed メタデータ store → release の sequence store と、
  reader 側の acquire load → relaxed メタデータ read の組は正しい。ロック外の
  `getSampleInfo`/`getNewestBufferNum`/`findBufferNum` も順序が成立している。
  ヘッダの非 atomic フィールドは INITIALIZED 到達後に書き換わらないため競合しない。

## 軽微（並行性）

- `readSample` は payload_size 不一致で呼び出し側が捨てるサンプルにも `markAsRead()` を呼ぶ
  （`ring_buffer.cpp:1336`）。`isUpdated()`/`waitFor()` が「読んだ」と誤認する。
- `subscribe`/`publish` は毎回 `openRoot` → `isDisconnected()` → `fstat(2)` を呼ぶ。
  100Hz × N トピックで無視できないうえ、**R04-F01 の引き金**でもある。
- `openRoot` 内の `RingBuffer::getSize()` は `std::invalid_argument` を投げるが、
  `Publisher` のコンストラクタは `catch (const std::runtime_error&)` しか捕まえない（`shm_pub_sub.hpp:371`）。
- ヘッダを直接 `#include` して CMake ターゲット経由でリンクしない使い方だと
  `SHM_STRICT_TYPE_CHECK` が伝播せず、x86 では `Publisher<std::vector<T>>` の特殊化 include 忘れが
  素通りして `std::vector` オブジェクトごと memcpy される（**本レビュー中に実際に踏み、ASan が double-free を検出**）。
  R01-F07-b の既知事項だが、ヘッダ側で `#error` にするか特殊化を `shm_pub_sub.hpp` から
  自動 include する方が安全。

---

# 2. API 設計・型安全・入力検証・メモリ安全・後方互換

> ビルド: `Debug + ASan` **106/106 PASS**、`Debug + UBSan` **106/106 PASS**（回帰なし）

R03 で入った 3 つの不変条件は、**本体（shm_base + shm_pub_sub）については意図どおり効いている**。
ヘッダとスロットメタデータに対する破壊注入 2,000 回（接続前・接続後の両方）で ASan 違反はゼロで、
`readSample()` の境界検査と `computeLayout()` / `validateLayout()` の溢れ検査に穴は見つからなかった。

一方で **R03 の対応が「本体の読み出し経路」に閉じており、外部特殊化・Python バインディング・
運用面・schema 契約には届いていない**。

## R04-F03

### Critical: `PointCloud2DScanData::deserialize()` に長さ検査が無く、購読経路から任意長のヒープ範囲外読み出しになる

**該当**: `sensor_daemons/point_cloud_2D_related/point_cloud_2D_data/lib/point_cloud_2D_data.cpp:387-436`
（特に **424 行の `memcpy`**）。呼び出し元: `.../lib/shm_pub_sub_point_cloud_2D_data.cpp:159-178`

`buffer_size` は先頭 24 バイトの最小長チェックにしか使われていない。点数はペイロード中の
`capacity_` を**無検証で信じて** `points_.resize(capacity_)` →
`memcpy(points_.data(), ptr, capacity_ * sizeof(Point2D))` を実行する。
lidar 版にある `if (ptr + ... > end_ptr) return -1;` に相当する検査が 1 つも無い。

R03 remediation の表は「`point_cloud_2D_data` は生バイト列を `staging_` へ退避 → ロック外で deserialize」で
対応済みと記載しているが、**退避先の長さを deserialize が守っていない**ため staging_ の外へ出る。

**再現（E2E、実証済み）**: publish → 同じ権限グループの別プロセスがスロットのペイロード先頭 +8 の
`capacity_` を 16 → 200000 に書き換え → `Subscriber<PointCloud2DScanData>::subscribe()`

```text
==271709==ERROR: AddressSanitizer: heap-buffer-overflow
READ of size 2400000 at 0x511000000128
    #1 PointCloud2DScanData::deserialize(...) point_cloud_2D_data.cpp:424
    #3 Subscriber<PointCloud2DScanData>::readSlotInto(...) shm_pub_sub_point_cloud_2D_data.cpp:178
    #4 Subscriber<PointCloud2DScanData>::subscribe(bool*, SampleInfo*) ...:229
0x511000000128 is located 0 bytes to the right of 232-byte region
```

**なぜ「壊れたペイロード」が現実的か**: 既定権限は 0660 で同一グループの誤書込みを排除できない
（R02-F07 で確認済みの信頼境界）。加えて **R04-F09 により、書式を変えて `schema_version` を上げても
既存セグメントでは照合されない**ため、「書式を変えた新バイナリが旧書式のペイロードを新パーサで読む」が
普通に起きる。その場合 `capacity_` はオフセットのずれた任意の 8 バイトになる。

**推奨**: `deserialize()` 冒頭で `capacity_ > (size_t)(end_ptr - ptr) / sizeof(Point2D)` を検査して
`-1` を返す。lidar と同じ「各フィールドの前で残り長を確認する」形に揃える。
`resize()` は検査の**後**に行う。

## R04-F04

### High: cv Python バインディングが ndarray の次元数を検査せず、2 次元配列の publish で範囲外読み出しになる

**該当**: `react_cv/shm_pub_sub_cv/src/shm_pub_sub_cv_python.cpp:35-61`（**58 行の `memcpy`**）

```cpp
const Py_intptr_t* shape = ndarr.get_shape();   // 要素数は get_nd() 個しかない
int rows = shape[0]; int cols = shape[1]; int channel = shape[2];   // 2次元なら範囲外
...
memcpy(mat.data, ndarr.get_data(), sizeof(uchar) * rows * cols * channel);
```

`get_nd()` を確認していないため、**グレースケール画像（2 次元 uint8 配列）**という
最もありふれた入力で `shape[2]` が `strides[0]` を読み、`channel` が画像幅などの巨大値になる。
その値で `memcpy` の長さを決めるので、numpy バッファの外を読み、
その内容がそのまま共有メモリへ publish される（**情報漏洩＋データ破損**）。

**再現（実証済み）**

```python
p = shm_pub_sub_cv.PublisherMat("r04_pycv", 3)
p.publish(np.zeros((8, 8), dtype=np.uint8))
```
```text
==272519==ERROR: AddressSanitizer: heap-buffer-overflow
READ of size 512 at 0x506000021ae0   （64 バイトの numpy バッファに対して）
    #2 ConvertNDArrayToMat(...) shm_pub_sub_cv_python.cpp:58
```

**同ファイルの併発問題**（Low〜Medium）

- `ConvertMatToNDArray`（18-33 行）: 深度を uint8 と決め打ちし、
  `mat.rows*mat.cols*mat.channels()` バイトしかコピーしない。
  CV_16U / CV_32F のトピックを購読すると**黙って壊れた画像**を返す。
- 同関数は subscribe 失敗時の空 `cv::Mat`（`data == nullptr`）に対して `memcpy(dst, nullptr, 0)` を実行する（UB）。
- `rows * cols * channel` は `int` 演算。4K 画像 × 多チャネルで溢れる。
- `dtype` と連続性（`get_flags()` の C_CONTIGUOUS）を検査していない。

**推奨**: `get_nd()` が 2 か 3 かで分岐（2 なら channel=1）、`dtype`・連続性を検査、
長さ計算を `size_t` にする。範囲外なら例外を投げて Python 側へ返す。

## R04-F05

### High: `Lidar2dScanData::deserialize()` の境界検査が桁溢れし、例外が `subscribe()` から漏れる

**該当**: `sensor_daemons/lidar_2D_related/lidar_2D_data/lib/lidar_2D_data.cpp:412-419`

```cpp
if (ptr + capacity_ * sizeof(Point) > end_ptr) return -1;
points_.resize(capacity_);
```

`capacity_` はペイロード由来の無検証値。`sizeof(Point) == 12` なので `capacity_ = 2^62` のとき
`capacity_ * 12` は `3 * 2^64 ≡ 0` に巻き戻り、検査を素通りする。
加えて `ptr + n` というポインタ演算自体がオブジェクト外を指すため UB。

**再現（E2E、実証済み）**

```text
[通常の過大値]         capacity_=200000              -> subscribe ok=0（拒否されている＝正常）
[乗算が桁溢れする値]   capacity_=4611686018427387904 -> !! subscribe() から例外が漏れた: vector::_M_default_append
```

`std::length_error` が `deserialize` → `readSlotInto` → `subscribe()` を貫通する。
センサループの `subscribe()` が投げることは契約にないので、呼び出し側は捕まえない。

**推奨**: 全ての検査を `残り長 / 要素サイズ` 比較へ書き換える
（`capacity_ > (size_t)(end_ptr - ptr) / sizeof(Point)`）。`resize()` の前に上限も課す。

**同ファイルの併発問題（Low）**: `setPoints(const float*, const float*, const int32_t*, size_t)`
（`lidar_2D_data.cpp:183-198`）は `points_.clear()` した直後に `addPoint()` を呼ぶので、
`point_count_(0) >= points_.size()(0)` が必ず成立し **常に `std::out_of_range` を投げる**。
かつこの時点で `points_.size()==0` / `capacity_` は元のまま、という不整合が残るため、
例外を握り潰した呼び出し側が `publish()` すると `serialize()` の
`memcpy(ptr, points_.data(), capacity_ * sizeof(Point))`（346 行）が空 vector の外を読む。

## R04-F09

### High: `shm_schema<T>` は実機では一切照合されない（R03-F05 の対策が事実上無効）

**該当**: `shm_base/src/ring_buffer.cpp:205-221`（`TopicContract::matches()` の版判定）,
`ring_buffer.cpp:717-720`（`initializeContents()`。版はセグメント生成時にしか書かれない）,
`shm_base/include/shm_base.hpp:465-500`

**(a) 片側が 0 なら照合しない**

```cpp
if (schema_version != 0 && other.schema_version != 0 && schema_version != other.schema_version)
```

版を「導入した瞬間」「上げた瞬間」は必ず片側が 0 の混在状態になる。
**この機能が最も必要な局面で検査が消える**設計になっている。

**(b) 既存セグメントには版が書き込まれない**

`initializeContents()` は新規セグメントでしか走らない。共有メモリは意図的にプロセス終了で
破棄しない設計なので、**トピックが一度でも作られていれば `schema_version` は 0 のまま固定**され、
以後どのプロセスが版を宣言しても header は 0 のままである。

**再現（実証済み）**

```text
--- 1) 版なし(0) のビルドがトピックを作る ---   pub(version=0): OK
--- 2) 版 7 の新ビルドが publish（既存セグメントへ attach）--- pub(version=7): OK
--- 3) 互換性の無い版 8 が subscribe ---        sub(version=8): ok=1 count=7
--- ヘッダの schema_version ---
element_size @104 = 12   schema_id @112 = 1770503117471965953
payload_kind @120 = 1,   schema_version @124 = 0     <= 永久に 0
```

つまり **`shm_tool remove` を全トピックに手動で実行しない限り、`shm_schema<T>` は何も守らない**。
cv::Mat / Lidar2dScanData / PointCloud2DScanData に付けた `schema_version = 1` も、
既存セグメント上では無意味である。これが R04-F03 の現実的な発火経路になる。

**回帰テストの穴**: `shm_pub_sub_r03_test.cpp:356 ExplicitSchemaVersionIsRecordedAndEnforced` は
**両側が非 0 の場合しかテストしていない**（7 vs 8）。
実運用で起きる「セグメント 0 vs プロセス 7」を検査していないので緑のままである。

**推奨（いずれか）**

1. 版を `schema_id` に畳み込む（`schema_id = combine(type_schema_id<T>(), version)`）。
   既存の「payload type mismatch → `shm_tool remove <topic>`」メッセージがそのまま発火するので、
   新しい規則も新しいメッセージも不要。
2. 「セグメントが 0、自プロセスが非 0」を**非互換として明示的に拒否**し、`shm_tool remove <topic>` を促す。
   attach 時に header を書き換えて版を「昇格」させるのは、旧バイナリが同時に走っていると危険なので採らないこと。
3. どちらにせよ、非対称ケースの回帰テストを追加すること。

## R04-F10

### High: 外部特殊化 3 つに R03-F01(c)（commit 後の世代確認と再発行）が入っていない

**該当**: `react_cv/shm_pub_sub_cv/src/shm_pub_sub_cv.cpp:70-160`,
`.../lidar_2D_data/lib/shm_pub_sub_lidar_2D_data.cpp:52-119`,
`.../point_cloud_2D_data/lib/shm_pub_sub_point_cloud_2D_data.cpp:51-124`

`grep -rn 'isGeneration|generationTag|publishOnce'` を 3 パッケージに掛けた結果は **0 件**。
本体は `publishOnce()` + `topic->isGeneration(generation_before)` + 最大 4 回の再試行を持つが、
3 つの特殊化はいずれも `ensureCapacity()` → `commitBuffer()` → そのまま return である。
R03-F01 が閉じたはずの「切替の隙間に旧世代へ commit したサンプルが成功扱いのまま誰にも読まれない」経路が
そのまま残っている。

**皮肉な点**: スカラ型はそもそも容量が固定で世代切替がほぼ起きない。
**世代切替が日常的に起きるのは画像サイズと点数が変わるこの 3 つだけ**であり、
対策が入っていないのがまさにそこである。

**推奨**: 3 つとも `publishOnce()` へ切り出し、`generationTag()` の保存と `isGeneration()` による
再発行ループを入れる。同じコードが 4 箇所に複製されているので、`ShmTopic` 側に
「commit まで面倒を見る」ヘルパ（`publishBytes(const void*, size_t)`）を置いて特殊化から共有するのが本筋
（**今回のレビューで見つかった不整合の大半は「4 箇所の複製」が原因**）。

## R04-F13

### Medium: 恒久的な不整合でも `openRoot()` が 1 秒待つ。ABI 4 移行時の実機挙動が悪い

**該当**: `shm_base/src/shm_topic.cpp:278-296`（1000ms の再試行ループ）,
`ring_buffer.cpp:343-350`（ABI 不一致メッセージ）

`openRoot()` の 1 秒ループは「O_EXCL の競争に負けたので勝者の初期化完了を待つ」ためのものだが、
**待っても絶対に解決しない失敗（ABI 不一致、contract 不一致、magic 不一致）でも同じだけ待つ**。

**再現（実証済み）** — ABI 3 のセグメントを残した状態:

```text
=== Subscriber ===
Subscriber: ok=0  elapsed=0 ms        （購読側は即座に失敗する。正しい）
=== Publisher ===
Publisher threw after 1001 ms:
  shm::Publisher: root segment is not usable: ABI major version mismatch (segment 3, this build 4)
ensureCapacity #0 -> 0  (1001 ms)
```

`Publisher<cv::Mat>` / lidar / point_cloud は **publish() のたびに `ensureCapacity()` を呼ぶ**ので、
ABI 3 のセグメントが 1 つ残っているだけで 40 Hz のセンサノードが **1 Hz のエラーログ生成器**になる
（しかも lidar/pcd は例外ではなく `std::cerr` に出して return するので、
ノードは止まらず delay だけが残り、原因が非常に追いにくい）。

**メッセージ品質**: `bad magic` のメッセージは復旧手順を書いているが、
**ABI 不一致・header size 不一致・slot size 不一致のメッセージには書かれていない**。
R03 → R04 の移行で運用者が実機で最初に見るのはまさに ABI 不一致のメッセージである。
トピック名も含まれていない。

**推奨**: `openRoot()` の待ちは「未初期化」の場合だけに限定し、
`validateLayout()` が恒久的な不整合を返したら即座に失敗させる
（`validateLayout()` に「待てば直る／直らない」を区別する戻り値を持たせるのが素直）。
ABI / header_size / slot_size 不一致のメッセージにトピック名と `shm_tool remove <topic>` を追記する。

## R04-F14

### Medium: `subscribeAlignedTo` の既定値が安全側でなく、主用途で誤った整列を Success として返す

**該当**: `shm_pub_sub.hpp:637-670`（および vector 版 `shm_pub_sub_vector.hpp:461-491`、
cv 版、lidar 版、pcd 版の同名関数）, `ring_buffer.cpp:1064-1074`

**(a) 参照 SampleInfo の妥当性を検査していない**。`subscribe(&ok, &info)` は失敗時に
`*info = SampleInfo{}`（全 0）を書く。その `info` をそのまま `subscribeAlignedTo()` に渡すと、
**目標時刻 0 に対する `Nearest` 検索**になる。

**(b) 既定 `max_skew_us = 0` は「無制限」**。時刻合わせ API の既定値としては最も危険な選択。

**再現（実証済み）** — ライブラリの主用途（オドメトリ更新に最も近いスキャンを取る）そのもの:

```text
odom subscribe ok=0  info.sequence=0 monotonic=0
subscribeAlignedTo(既定 max_skew_us=0) -> status=0(Success)  scan_time=8657082916  now=8657182771
  => 目標時刻 0（数十年前）に対して 'Success' を返し、リング内で最も古いスキャンを整列済みとして返した
```

**併発するドキュメントの嘘**: `subscribeAt()` の doc コメントは `TooOld` / `TooNew` を返り得る状態として
列挙しているが、`findBufferNum()` の `Nearest` は有効サンプルが 1 件でもあれば必ず `best` を決めるので
（`ring_buffer.cpp:1029-1038`）、**既定ポリシーでは `TooOld` / `TooNew` は原理的に発生しない**。

**推奨**: `reference.sequence == 0` を明示的に弾く。`max_skew_us` を必須引数にするか有限の既定値にする。
doc コメントに「`Nearest` では `TooOld`/`TooNew` は返らない」と書く。

## R04-F15

### Medium: null `state` に対する契約が 3 通りに分裂している

| 実装 | 該当箇所 | null を渡すと |
|---|---|---|
| `Subscriber<T>` | `shm_pub_sub.hpp:585-590` | `std::invalid_argument` を投げる |
| `Subscriber<std::vector<T>>` | `shm_pub_sub_vector.hpp:406-412` | `std::invalid_argument` を投げる |
| `Subscriber<cv::Mat>` | `shm_pub_sub_cv.cpp:299` | **null を逆参照して SIGSEGV** |
| `Subscriber<Lidar2dScanData>` | `shm_pub_sub_lidar_2D_data.cpp:197` | 黙って無視して続行 |
| `Subscriber<PointCloud2DScanData>` | `shm_pub_sub_point_cloud_2D_data.cpp:202` | 黙って無視して続行 |

R03 の軽微所見「vector の `subscribe(bool*)` が null を逆参照」を本体だけで直し、外部特殊化に展開していない。
**推奨**: 全経路を `std::invalid_argument` に統一する。

## R04-F16

### Medium: Python バインディングが型を区別できず、チュートリアル記載の使い方が実際には別の型になる

**該当**: `shm_pub_sub/src/shm_pub_sub_python.cpp:101-127`

3 つの C++ クラスを **同じ Python 名 "Publisher"（および "Subscriber"）** で登録しているため、
最後に登録された float 版しかモジュールから見えない。

**再現（実証済み）** — `manual/tutorials_shm_pub_sub_python_jp.md:54-61` に書かれている通りの使い方:

```python
pb = shm_pub_sub.Publisher("r04_py_bool", False, 3)   # bool のつもり
pb.publish(True)                                       # -> 実際には float トピックに 1.0 を書く
si = shm_pub_sub.Subscriber("r04_py_int", 0)
si.subscribe()                                         # -> (42.0, True)  int ではなく float
```

結果として **C++ の `Publisher<bool>` / `Publisher<int>` のトピックは Python から一切購読できない**
（contract 不一致で `is_success=False` が返るだけで、理由は伝わらない）。
R03 の schema 設計コメントは「異なる言語バインディングをまたぐ通信で可搬な版だけを根拠にできる」と謳っているが、
その言語バインディングが型を表現できていない。

**同ファイルのコピペ不具合**: 104 行で `PublisherBool` に `&PublisherInt::_publish` を bind している
（現状は到達しないが、名前衝突を直した瞬間に顕在化する）。

**併発**: Python 側に `SampleInfo` / `subscribeAt` / `subscribeAlignedTo` / `RetentionWindow` が
一切露出していない。タイムマシン機能は C++ 専用である。

## R04-F17

### Medium: 仕様書が形式 v1 のままで、実装と 3 メジャー版ずれている

**該当**: `manual/spec_jp.md:222-267`（共有メモリレイアウト）, `434-462`（同期機構）, `551-585`（データ整合性）

仕様書の「共有メモリレイアウト」は依然として **`pthread_mutex_t` + `pthread_cond_t` + `element_size` +
`buffer_num` + timestamp 配列** という v1 の構成を図示しており、「同期機構」は
`pthread_cond_signal` / `pthread_cond_timedwait` によるシグナリングを正としている。
実装では `RingBuffer::signal()` が空関数で、**condition variable はレイアウトに存在しない**
（`ring_buffer.cpp:1412-1428`）。

仕様書は同時に「その他のクラスについては都度特殊化した Publisher/Subscriber を定義することで対応できる」
（18 行）と読者に特殊化の作成を勧めている。
**この仕様書を読んで特殊化を書いた結果が、今回見つかった F03/F05/F10/F15 の不整合そのものである。**

`shm_schema` / `subscribeAt` / `subscribeAlignedTo` / `SearchPolicy` / `RetentionWindow` /
世代セグメント名 / ABI 4 は `manual/` と `README.md` のどこにも登場しない。

**併発（バージョニング）**: `CMakeLists.txt:3` の `project(shm VERSION 4.0.0)` は据え置きで、
42-47 行のコメントは「4.0.0 = ABI 3」と書いている。R03 で ABI が 4 になり `ShmHeader` が
128→192 バイトになったのに **`SHM_VERSION` は変わっていない**。
`find_package(shm_base 4.0.0)` で ABI 3 と ABI 4 を区別できない。

**推奨**: `SHM_VERSION` を 5.0.0 に上げ、実レイアウト・世代セグメント名・`shm_schema<T>` の使い方・
ABI 4 への移行手順・タイムマシン API の意味論を仕様書に反映する。
**特殊化を書くときに守るべき契約（世代再確認、staging の長さ、境界検査、null 契約）をチェックリスト化する。**

## R04-F18

### Medium: `shm_tool list` が世代・ABI・contract を表示しないため、ABI 4 移行の判断材料がない

**該当**: `tools/shm_tool/src/main.cpp:70-107`

`remove` は `disconnectTopic()` → `removeAllGenerations()` を通るので
**新しいノンス付き世代名に正しく対応している**（実証済み）。一方 `list` は `ls -l /dev/shm` の出力を
空白で切って並べ直すだけでヘッダを一切読まない。ABI 4 への移行で運用者が知りたい
「どのセグメントが旧 ABI か」が分からない。世代セグメントは `shm_foo#7-c3e9915d96d3` として並ぶだけで、
どのトピックに属するかも名前から推測するしかない。

**併発（Low）**: `fgets()` の戻り値未検査（75 行）、`popen()` が NULL を返したら `feof(NULL)` で落ちる、
`disconnectTopic()` の戻り値を見ないので存在しないトピックの `remove` も成功扱い（exit 0）。

## R04-F19

### Low: トピック名に `#` を許しているため、世代名と衝突して別トピックの root を消せる

**該当**: `shm_base/src/shared_memory.cpp:39-62`（`validateShmName()` は `..` と NUL しか弾かない）,
`shm_topic.cpp:69-97`（`parseGenerationSuffix()`）, `380-423`（`unlinkStaleGenerations()`）

R03 で `#` が世代名の予約文字になったが、名前検証は更新されていない。

**再現（実証済み）**: トピック `r04_topic#2-0000deadbeef` と、無関係なトピック `r04_topic` を同時に使う

```text
victim root segment exists            : 1
victim root segment exists after grow : 0   <= 別トピックのセグメントを消した
```

**推奨**: `validateShmName()` で `#` を拒否する。
`parseGenerationSuffix()` の `std::stoull(..., 16)` は末尾のゴミを黙って無視するので、
16 進として厳密に全消費することを確認するとよい。

## R04-F20

### Low: scalar / vector / 外部特殊化で公開 API が揃っていない

| 項目 | `Subscriber<T>` / `Publisher<T>` | `<std::vector<T>>` | cv / lidar / pcd |
|---|---|---|---|
| move 構築 | 可 | **不可**（`~X() = default` で暗黙 move が消える） | lidar/pcd は可、cv は不可 |
| `existsPublisherMemory()` | あり | **なし** | なし |
| contention カウンタ | あり | あり | **なし** |
| `publish()` 失敗時 | 例外 | 例外 | cv は例外、lidar/pcd は **`std::cerr` + return** |
| ARM の trivially-copyable 実行時検査 | あり（`shm_pub_sub.hpp:353-359`） | **なし** | なし |

`Publisher<std::vector<T>>` が move 不可なので `std::vector<Publisher<...>>` に入れられない一方、
`Publisher<T>` は入れられる、という差は利用者にとって説明しにくい。
`SHM_STRICT_TYPE_CHECK=OFF` でビルドした ARM 実機では、vector 版だけが
trivially-copyable でない要素型を通す。

## R04-F21

### Low: 世代作成中に死んだプロセスのセグメントが、次の世代切替まで回収されない

`unlinkStaleGenerations()` は「現世代より新しい世代番号のセグメントには絶対に触らない」
（`shm_topic.cpp:413-419`）ため、CAS 前に死んだ作成者のセグメントは**次に世代が進むまで残る**。
これは R03-F03 への正しい対応（時間で生死を判定しない）であり設計として妥当だが、
画像トピックなら数百 MB のセグメントが残り得る。R04-F18 と併せて可視化する手段が必要。
実測では 8 段階の容量拡張後に残ったのは root + 現世代のみで、消し残しは無かった。

## R04-F22

### Low: `sequence_source` が別オブジェクトのマッピング内への生ポインタで、所有関係がコード上に表現されていない

`shm_topic.cpp:310, 466` で `rb.setSequenceSource(root_ring_->sequenceCounter())` としており、
`RingBuffer::sequence_source` は `root_` が保持する mmap 領域内を指す。
`openRoot()` が `root_` を張り替える経路では古い `root_` のデストラクタが munmap する
（**これが R04-F01 の一部**）。「`root_` より先に `ring_` を無効化しなければならない」という
不変条件がコード上に書かれておらず、`ShmTopic` に手を入れたときに壊れやすい。

**推奨**: `sequence_source` を `shared_ptr` 経由にするか、`openRoot()` の張り替え時に明示的に `ring_.reset()` する。

## 問題の無かった領域（API・メモリ安全、実証込み）

指摘事項と同じくらい重要なので明記する。

- **`RingBuffer::readSample()` の境界検査は正しい**（`ring_buffer.cpp:1288-1310`）。
  `payload_size > expected_element_size || payload_size > dst_size` の二重検査と、
  検証済みスナップショット由来のアドレス計算で穴は無い。
- **vector 版 `readSlotInto()` の `hint` 方式に穴は無い**（`shm_pub_sub_vector.hpp:356-395`）。
  **実測**: サイズを毎回変える publisher と並走させて 3 秒間 616,655 回 subscribe →
  **成功 616,655 / 失敗 0 / 内部 retry 55 / 値の破損 0**。
  ただし cv/lidar/pcd が「スロット容量ぶん確保 → payload_size で切る」方式なのに対して
  1 つだけ方式が違うので、統一を検討する価値はある（安全性の問題ではない）。
- **`computeLayout()` / `validateLayout()` の溢れ検査は網羅的**（`ring_buffer.cpp:120-170, 310-433`）。
  `__builtin_mul_overflow` / `__builtin_add_overflow`、`alignUp()` の巻き戻り検出、
  ヘッダ記載オフセットと自前計算の照合、`mapping_size >= total_size` の確認、
  検証中の再初期化の再確認まで入っている。**攻撃可能な組み合わせは見つからなかった。**
- **壊れたヘッダ／スロットメタデータに対する本体の耐性**。境界値をランダム注入し
  `subscribe` / `subscribeAt` / `getRetentionWindow` / `waitFor` を呼ぶ試験を
  **接続前 400 回・接続後 1,600 回**実施。**ASan 違反ゼロ、例外の漏れゼロ**。
- **cv::Mat 特殊化のメタデータ検証は堅牢**（`shm_pub_sub_cv.cpp:239-262`）。
  境界値注入 300 回で ASan 違反ゼロ、例外漏れゼロ。
  `capacity / elem_bytes` / `capacity / row_bytes` という除算による検査は正しい形
  （**F03/F05 はこれを真似るべき**）。
- **世代セグメントの後始末に消し残しは無い**。ノンス付き名の孤児を置いても `shm_tool remove` で全て消えた。
  `parseGenerationSuffix()` は他トピックのセグメントを誤って消さない（F19 の `#` 入りトピック名を除く）。
- **reader がスロット mutex を取るようになったことで publish が失敗するようにはなっていない**
  （`lockSlotWithin()` は機能している）。reader 4 / `buf_num=1` → publish 850,791 回・throw 0、
  reader 32 / `buf_num=3` → 240,270 回・throw 0。
  ※ ただし R04-F08 の条件（reader が最古スロットを保持し続ける）では失敗する。
- **`generationName` / `generationTag` / `generation()` の使い分けは一貫している**。
  `ShmTopic` の doc コメント（`shm_base.hpp:880-885`）だけが古い `#<N>` 形式のままなので更新すること。
- Release / ASan / UBSan の 3 構成で **106/106 PASS**。

---

# 3. テストの実効性と対応記録の検証

> このレビュアーは「対応記録に書かれた主張を疑ってかかる」役割を担った。
> バグ再注入は scratchpad の複製ツリーで行い、元リポジトリは無変更である。

## R04-F23

### High: 「Release / ASan / TSan / UBSan の全構成で 106/106 PASS」は再現しない

| 構成 | 実行 | 結果 |
|---|---|---|
| Release `-j1` | 17 回 | **15 回 PASS / 2 回 FAIL**。run#1 = #41, #85 が SEGFAULT、run#2 = #41 が SEGFAULT |
| Release `-j4` | 5 回 | **0 回 PASS**。#37/#38/#41/#42/#45/#71/#87〜#94/#97 などが SEGFAULT |
| ASan `-j1` | 1 回 | 106/106 PASS |
| ASan `-j4` | 1 回 | 8 件 FAIL（SEGV） |
| UBSan `-j1` | 1 回 | 106/106 PASS |
| TSan `-j1`（`setarch -R`） | 1 回 | 106/106 PASS |
| 単体 `ContentionCounters…` 反復 | 30 回 | 8 回 SEGFAULT（別セッションでは 0/30・0/20 と再現率が揺れる） |

`-j4` の SEGV は**全て同一シグネチャ**であり、**R04-F01 そのもの**である。

```text
#0 RingBuffer::isLayoutChanged()   shm_base/src/ring_buffer.cpp:1501
#1 ShmTopic::ensureCapacity(...)   shm_base/src/shm_topic.cpp:635      ← publish 経路
   / ShmTopic::follow(...)         shm_base/src/shm_topic.cpp:604      ← subscribe 経路
#2 Publisher<T>::publishOnce       shm_pub_sub/include/shm_pub_sub.hpp:415
```

**R04-F01 の最小再現（25 行、100% 再現）**

```cpp
Publisher<int> pub("r04_repro", 3);
pub.publish(1);                  // publish#1 ok
disconnectMemory("r04_repro");   // 別プロセスの shm_tool remove 相当
pub.publish(2);                  // → SIGSEGV (100%)
```

Subscriber 版（unlink 後に別プロセスがトピックを作り直し → `sub.subscribe()`）も 100% SEGV。
`git archive 1be9c84`（R02 時点）でも `openRoot()` の構造は同一なので **R03 の回帰ではない**。

**記録の訂正**: `review/r03-remediation.md` の「検証結果」に書いた数値は、
各構成 1 回ずつの `-j1` 実行結果である。**反復実行によるフレーキー確認をしていなかった。**

**推奨**: R04-F01 を修正したうえで、CI に「Release `-j1` × 10 回」と「`-j4`」を入れる。

## R04-F24

### High: R03-F04 と cutover 三点セットには回帰テストが 1 本も無い

修正を丸ごと取り消しても全テストが緑のままだったもの:

| 注入 | 取り消した修正 | 結果 |
|---|---|---|
| **H** | **R03-F04 そのもの** — `readSample()` からスロット robust mutex を外し R02 の seqlock 読みへ戻す | **Release 106/106 PASS、TSan 106/106 PASS、TSan 警告 0 件** |
| **F** | **R03-F01(c)** — vector `publishOnce()` の publish 後世代確認と再発行を削除 | **106/106 PASS** |
| **G1** | **R02-F05** — scalar `publishOnce()` の publish 後世代確認を削除 | **106/106 PASS** |
| **G2** | **R03-F01(b)** — cutover 直後の `MAX_DRAIN_ROUNDS` 回収ループを無効化 | **106/106 PASS** |
| **C** | 競合カウンタを常に 0 に固定 | **106/106 PASS** |

**R03 レビューが求めた「`allocate 後停止 → 別 writer が migrate/cutover → 旧 writer commit` を
barrier で固定し、成功 sample の可視性と sequence 一意性を決定的に検査する」のうち、
実装されたのは sequence 一意性だけで、可視性は未実装である。**

`PublishingWhileTheGenerationMovesKeepsTheLatestValueVisible` が記録上その回帰テストとされているが、
実際に見ているのは「例外が飛ばない」「読めた値の各要素が 2 つのマジック値のどちらか」だけで、
**publish したサンプルが購読側から見えたかを一切確認していない**。
さらに A5 と 5A が 1 つのベクタ内に混在しても `break` で抜けるだけで不整合と数えないので、
2 writer 間の torn read も検出しない。

注入 H が TSan でも捕まらない点は、R03 レビューの
「別 mmap alias は sanitizer が同一物理領域として追跡できない場合がある。
TSan 成功だけでこの条件を否定できない」という指摘を**両方向から実証**した形である
（修正しても壊しても TSan は同じ結果）。

**推奨**: 注入 F / G1 / G2 / H が必ず FAIL するテストを追加する。
R03-F04 については、外部から 2ms 超スロットを保持して retry/failure が上がることを確認する形が使える。

## R04-F25

### High: `SupersededGenerationsAreUnlinked` の前提 ASSERT が完全に空振りしている

R03 で `latest_generation` が「世代 16bit + ノンス 48bit」のパック値に変わったのに、
`shm_pub_sub_contract_test.cpp` の `latestGeneration()` は生のタグを返すままである。

```text
gen1: raw=744880375650569  unpacked=2   → EXPECT_GT(raw, 3u) は必ず PASS
```

世代が 1 のまま（＝一度も世代が進まなくても）タグは `1<<48 = 281474976710656 > 3` なので必ず通る。
しかも本体の `EXPECT_LE(countSegments, 2)` も「世代が 1 つも作られず root だけ」なら 1 で通るため、
**この 2 つが同時に空振りすると「何も起きていないのに緑」になる**。
失敗時のメッセージも `世代は 5919803217356884 まで進んだ` という無意味な数字を出す。

同じ `latestGeneration()` を使う `SequenceStaysUniqueAcrossGenerations` の前提は、
世代が上位ビットにあるため**偶然**歯が残っているが、意図した比較にはなっていない。

> `shm_pub_sub_generation_test.cpp` 側の `latestGeneration()` は R03 で `unpackGeneration()` 経由に
> 直したが、**`shm_pub_sub_contract_test.cpp` 側の同名ヘルパを直し忘れていた**。

**推奨**: 全テストファイルを横断して `latest_generation` の生読みを `unpackGeneration()` 経由にする。

## R04-F26

### Medium: `PayloadAndSampleInfoAlwaysDescribeTheSameSample` は「info が別サンプル」を検出できない

| 注入 | 内容 | 結果 |
|---|---|---|
| **B** | vector `subscribe` を「payload 確定後に `getSampleInfo()` 再読み」に戻す（R03-F02 の取り消し） | `VectorPayloadAndSampleInfo…` が 5/5 FAIL ✅ |
| **B2** | vector `readSlotInto` が返す `info` を「有効だが別サンプル」に（`sequence+1`, `capture+1ms`） | **全 25 件中素通り**。捕まえたのは無関係な `OldGenerationCommit…` のみ ❌ |
| **B3** | scalar 側で同じ細工 | `PayloadAndSampleInfo…` は**素通り** ❌（`SHMTimeMachineTest` が偶発的に検出） |

**検出できるのは「info が空（sequence==0）」だけで、「info が別サンプルのもの」は検出できない。**
R02/R03 の所見が挙げていた実害（「時刻合わせで別時刻のセンサ値を整列済みとして返す」）そのものの形である。
scalar は時間旅行テストが偶発的に拾うが、**vector には拾うものが何もない**。

**推奨**: payload に自分の sequence を書き込み、`info.sequence` と突き合わせる形に強化する。

## R04-F27

### Medium: `ContentionCountersDetectWriterOutpacingReader` の期待値変更はカバレッジの回帰

R03 対応で `EXPECT_GT(retry, 0)` → `EXPECT_EQ(failure, 0)` に変更した件。

**技術的な言い分（「reader がロックを取るようになったので retry は 0 が正常」）自体は正しい。**
しかし結果として:

1. 実測すると**過負荷条件も正常レート条件も完全に同じ数値**になる:
   ```text
   overload:  reads=390032  retry=0  failure=0     (buf_num=1, 全力 publish)
   sane rate: reads=629367  retry=0  failure=0     (buf_num=3, 5ms 間隔)
   ```
   2 つのブロックの assertion が実質同一（`reads>0` と `failure==0`）になり、
   テスト名が主張する「writer が reader を追い越しているのを検出できる」を**一切検証しなくなった**。
2. 注入 C（カウンタを 0 固定）で **PASS したまま**。カウンタ機構が壊れても誰も気づかない。
3. 代替の陽性テストが用意されていない。`SLOT_LOCK_TIMEOUT_US`(2ms) を超えてスロットを保持すれば
   retry は決定的に上げられる（テスト側でスロットの mutex を握るだけ）ので、置き換えは可能だった。

**判定: フレーキーな `retry>0` を外した判断は妥当だが、陽性検証を残さず削っただけなので、
正味はカバレッジの回帰であり隠蔽に当たる。**
カウンタは記録上「rplidar_daemon → lidar_2D_to_point_cloud_2D のレート設計を定量チェックする運用機能」
とされており、その機能が無検証になった影響は小さくない。

なお `EXPECT_EQ(failure, 0)` 自体も、`readSample` が 2ms 待ちを 5 回連続で失敗すれば上がるので
負荷次第でフレーキーになり得る（今回の実行では未観測）。

**推奨**: 陽性の検証を戻す。無理ならテスト名と記録から「検出できる」という主張を取り下げる。

## R04-F28

### Medium: `ctest -j` で走らない。ワーカースレッドの例外が `std::terminate` になる

- **並列実行不可**: 各 fixture の `SetUp()`/`TearDown()` が自分の担当トピックを**全部**
  `disconnectTopic()` するため、並列実行すると兄弟テストが使用中のセグメントを unlink し、
  R04-F01 を踏んで SEGV する。CMake に `RUN_SERIAL` も `RESOURCE_LOCK` も無いので、
  `ctest -j4` と書いた人は原因不明の大量 SEGFAULT を見ることになる。CI は `-j` なしなので現状は露呈していない。
- **例外を捕まえないワーカースレッド**: `shm_pub_sub_race_test.cpp` はスレッド 5 本に対し `catch` 1 個だけ。
  `runTornReadStress` と `FailedSubscribeMustNotCorruptPreviousValue` の publisher スレッドは無防備で、
  `publish()` が投げると（`Could not allocate a buffer` / `the layout generation kept changing`）
  **FAIL ではなく `std::terminate`** になる。

**その他のフレーキー要因**

- 時間ベースの判定: `OrphanedGenerationDoesNotBlockGrowthAndIsNotWaitedFor` の `< 200ms`
  （160KB セグメント作成 + 履歴移行を含む。サニタイザ下や高負荷では危うい）。
- 負荷依存の前提: `FailedSubscribeMustNotCorruptPreviousValue` の `ASSERT_GT(failures, 0u)`。
- 条件付きで消える検証: `OrphanedGeneration…` の末尾の残骸片付け確認が
  `if (世代が期待どおり進んでいたら)` で囲まれており、条件が外れると黙って何も検証しない。
- CPU 数: 22 コア環境で確認。少コア環境で `PublishingWhileTheGenerationMoves…` が
  `MAX_PUBLISH_ATTEMPTS=4` を使い切って例外を投げる余地がある（`EXPECT_EQ(throws, 0)`）。

## R04-F29

### Low: `DEFAULT_PERM = 0660` が umask で 0640 になり、グループ共有が成立しない

`shm_open` は umask を通すので、実ファイルは umask 022 環境で **0640** になる。
R02-F07 の「同じグループにすれば共有できる」という意図がグループ書き込み権の欠落で成立しない。
**権限のテストは 1 本も無い。**

## 対応記録の主張の検証結果

| 主張 | 判定 | 根拠 |
|---|---|---|
| 「Release / ASan / TSan / UBSan の全構成で 106/106 PASS」 | **不成立** | R04-F23 |
| 「発行番号の採番元は root の 1 個だけ」 | **副作用が未処理** | commit 側は正しいが `findBufferNum()` / `getSequenceCounter()` が自セグメントの `header->sequence` を見たまま（R04-F07） |
| 「全読み出し経路が `readSample()` を通る」 | **概ね成立、ただし穴あり** | ライブラリ内の payload 読み出し 3 経路は全て `readSample()` 経由で正しい。`getSampleInfo()` の残存は `getRetentionWindow()` / `findBufferNum()` のメタデータのみ（atomic 化済み）。**ただし `getDataList()` は公開 API のまま**で、本リポジトリ自身のテスト（`shm_base_test.cpp:329/369/379/419/535/536/595`, `shm_pub_sub_race_test.cpp:182`）がロック外で payload を読んでいる |
| 「時間で他プロセスの生死を判定する処理を削除した」 | **成立** | `reclaimOrphanGeneration` / `ORPHAN_WAIT_TIMEOUT_US` / `adoptSequenceFloor` は grep で全滅。`waitForInitialization` はタイムアウトで false を返すのみ。`initializeOrAttach` は takeover せず例外。`allocateBuffer` は EOWNERDEAD のみ回収。`unlinkStaleGenerations` の「同世代・別ノンス」判定は、そのノンスの作成者が CAS に必ず負けることから論理的に安全（時間非依存） |
| 「vector publisher にも publish 後の世代確認を追加。scalar と等価」 | **コードは等価だが検証されていない** | 構造は同一。ただし機構を丸ごと外しても 106/106 PASS（R04-F24） |
| ABI / ヘッダサイズ / static_assert | **成立。aarch64 も検証済み** | 下記 |

### aarch64 の確認（R03 の「未実施」項目が解消）

クロスコンパイル + qemu 実行で、x86-64 と aarch64 の双方で
`sizeof(ShmHeader)=192`, `sizeof(SlotRecord)=128`, `alignof(SlotRecord)=64` を実測一致。
`pthread_mutex_t` は 40→48 バイトと違うが `alignas(64)` に吸収される。
**`review/r03-remediation.md` の「未実施: ShmHeader を 192 バイトへ拡張したので static_assert の再確認が要る」は、
静的レイアウトについては問題なしと報告できる**（実機での動作確認は依然として未実施）。

### 記録と実装のズレ（軽微）

- `shm_base.hpp:405` のコメントが依然 **「固定長ヘッダ（128 バイト）」**（実際は 192）。
- `ShmTopic` クラスのドキュメント（`shm_base.hpp:855-860`）が世代名を **`/shm_<topic>#<N>`** のままで
  ノンスが無く、`generationName()` 側のコメントと食い違う。
- ルート `CMakeLists.txt` の版履歴が「4.0.0: … ABI を 3 に上げた」で止まっており、
  `project(shm VERSION 4.0.0)` も据え置き（R04-F17 と同じ指摘）。
  `react_cv/shm_pub_sub_cv/CMakeLists.txt` は `SHM_VERSION` で互換判定しているので、
  **混在ビルドを configure 時に検出できない**。

## テストが 1 本も無い重要経路

1. **root セグメントの unlink → 再作成**（R04-F01 のパス）。`shm_tool remove` の運用そのもの。
2. **`shm_tool` 全体**（`tools/shm_tool`）。対応記録が利用者に実行を指示しているコマンドである。
3. **cutover を跨いだ「成功 publish の可視性」**（R04-F24）。
4. **世代切替の多プロセス競合**: `createNextGeneration` の CAS 敗者経路、
   `unlinkStaleGenerations` の「同世代・別ノンス」枝。世代関係のテストは全て単一プロセス・逐次。
5. **生きた作成者を SIGSTOP しても盗まれないこと**（R03 レビューの明示的な推奨）。
   現状の `SegmentOfAFutureGenerationIsNeverUnlinked` は「空ファイルを置くだけ」で生きたプロセスが登場しない。
6. **世代 ≥2 での `Contended` / `Empty` 判別**（R04-F07）。
7. **上限に達したときの経路**: `MAX_GENERATION` / `MAX_PUBLISH_ATTEMPTS` / `MAX_GENERATION_ATTEMPTS` の枯渇、
   `adoptSample` が空きスロット不足で false を返す（＝履歴が黙って落ちる）ケース。
8. **`subscribe(nullptr)` が `std::invalid_argument` を投げること**（R02/R03 の明示的な修正）。
   実装は scalar/vector とも正しく動くことを実測確認したが、テストが無い。
9. **既定権限 0660 / `PERM_ALL`**（R02-F07）。R04-F29 の umask 問題も含め未検証。
10. **`getDataList()` を使う外部特殊化 3 種**。本スイートの対象外。
11. **Python バインディング**（`shm_pub_sub_python.cpp`）。
12. **vector の `subscribeAlignedTo`**（scalar のみカバー）。
13. **世代切替を跨いだ `waitFor()` / `signal()`**。

---

## 回帰テストの穴（3 レビュアー統合）

**まず前提**: 現状のスイートは、**R03 の中心的な修正を丸ごと取り消しても緑のまま**である（R04-F24）。
「106/106 PASS」は品質の根拠にならない。

### 必須（release gate）

1. **`shm_tool remove` / Publisher 再起動後の `subscribe`/`publish` が SIGSEGV しないこと**（R04-F01/F23）。
   25 行の最小再現がそのまま使える。
2. **注入 H が FAIL すること** — R03-F04 の排他を外したら落ちるテスト。
   外部から `SLOT_LOCK_TIMEOUT_US` を超えてスロットを保持し、retry/failure が上がることを確認する形（R04-F24/F27）。
3. **注入 F / G1 / G2 が FAIL すること** — cutover を跨いだ**成功 publish の可視性**を
   barrier で固定して決定的に検査する（R04-F24）。
4. **`ensureCapacity` が成功を返したら要求容量を満たしていること**（R04-F02）。
5. **reader がロック保持中に死んでも publish 済みサンプルが残ること**（R04-F06）。
6. `ContentionIsDistinguishedFromMissingData` を **vector トピック（世代 ≥ 2）** でも回す（R04-F07）。
7. 外部特殊化 3 つに、**壊れたペイロードで OOB / 例外漏れが起きないこと**の回帰テスト（R04-F03/F05）。
   現状これらのパッケージには shm 経路の破壊注入テストが存在しない。

### 既存テストの修正

8. `SupersededGenerationsAreUnlinked` の `latestGeneration()` を `unpackGeneration()` 経由にする。
   **全テストファイルを横断して確認すること**（R04-F25）。
9. `PayloadAndSampleInfo…`（scalar / vector とも）を
   「payload に埋めた sequence と `info.sequence` の一致」で検証する形に強化（R04-F26）。
10. `ContentionCountersDetect…` に陽性の検証を戻す。
    無理ならテスト名と記録から「検出できる」という主張を取り下げる（R04-F27）。
11. CMake に `RUN_SERIAL`（または共有 `RESOURCE_LOCK`）を設定し、
    ワーカースレッドの例外を握って `EXPECT` に落とす（R04-F28）。
12. `ExplicitSchemaVersionIsRecordedAndEnforced` に
    **「セグメント 0 / 自プロセス非 0」の非対称ケース**を追加（R04-F09）。
13. `subscribeAlignedTo` に **無効な参照 SampleInfo（`sequence == 0`）** を渡すケース（R04-F14）。
14. ABI / contract 不一致時の **`Publisher` 構築が 100ms 以内に失敗すること**（R04-F13）。

### CI

15. **Release `-j1` × 10 回**と **`-j4`** を CI に入れる。単発実行はフレーキーを隠す（R04-F23）。

## 修正の優先順（3 レビュアーの提案を統合）

| 順 | ID | 理由 |
|---|---|---|
| 1 | R04-F01 (+F23) | SIGSEGV。**R03 の移行手順そのものが引き金**で、テストスイートも不安定にしている |
| 2 | R04-F02 | 126MB のヒープ外書き込み |
| 3 | R04-F03 | 購読するだけで OOB。修正は数行 |
| 4 | R04-F04 | ありふれた入力（グレースケール画像）で OOB。修正は数行 |
| 5 | R04-F05 | 桁溢れ + 例外漏れ |
| 6 | R04-F06 | reader 死亡でセンサ値消失 |
| 7 | R04-F07 | タイムマシン API の契約が壊れている |
| 8 | R04-F25 | 空振りテストの修正。**他にも同種が無いか横断確認**が要る |
| 9 | R04-F24 (+F26/F27) | **テストが機能を守れていない**。以降の修正の妥当性を確認する土台になるので早い方がよい |
| 10 | R04-F08 | リアルタイム性。**方式の再検討が要るので時間がかかる** |
| 11 | R04-F09 | R03-F05 が無効。F03 の発火経路でもある |
| 12 | R04-F10 + R04-F15 | まとめて「特殊化を `ShmTopic` の共通ヘルパに寄せる」で片付けるのが本筋 |
| 13 | R04-F11 / F12 | 世代切替の履歴品質 |
| 14 | R04-F13 / F17 / F18 / F28 | ABI 4 移行の運用性と CI。実機投入の直前に必要 |
| 15 | 残り（F14 / F16 / F19〜F22 / F29） | |

> **設計上の総括**: 今回見つかった不整合の大半（F03 / F04 / F05 / F10 / F15 / F20）は、
> **同じ責務のコードが 5 箇所（scalar / vector / cv / lidar / pcd）に複製されている**ことに起因する。
> 個別に潰すのではなく、`ShmTopic` 側に publish/subscribe の骨格を寄せ、
> 特殊化は「シリアライズ／デシリアライズ」だけを担う形にリファクタリングするのが本筋である。
