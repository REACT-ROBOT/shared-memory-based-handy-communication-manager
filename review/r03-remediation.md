# R03 指摘事項 対応記録

- 対応日: 2026-09-01
- 対象: [`r03-2026-09-01-review-finding.md`](r03-2026-09-01-review-finding.md)
- 結果: 指摘 5 件＋軽微 4 件すべて対応。回帰テスト 6 本を追加し、
  Release / ASan / TSan / UBSan の全構成で **106/106 PASS**

R03 は動的再現ではなく「コード上で成立する interleaving」に基づく静的所見だった。
そのため対応も、個々の症状を潰すのではなく **その interleaving が構成できない構造へ
作り替える** ことを方針とした。具体的には次の 3 つを不変条件として据えた。

| 不変条件 | 効果 |
|---|---|
| 世代セグメント名は毎回一意（ノンス付き） | 名前の取り合いが原理的に起きない → 時間で他プロセスの生死を判定する処理が不要になる |
| 発行番号の採番元は root の 1 個だけ | 世代をまたいだ番号の重複が原理的に起きない |
| payload はスロットの robust mutex 下でのみ読み書きする | writer/reader の同時 memcpy が起きない（data race の消滅）|

ABI は **major 3 → 4**、`ShmHeader` は 128 → 192 バイトに拡張した。
既存セグメントは magic/ABI 検証で弾かれるので、更新後は一度
`shm_tool remove <topic>` で消すこと。

---

## R03-F03 — 時間による孤児判定の廃止（最優先）

**問題**: 世代セグメント名が固定の `#N` だったため、作成途中で死んだプロセスの
残骸が名前を占有すると `O_EXCL` が必ず失敗し、容量拡張が回復しなかった。
R02 ではこれを「1 秒待って未初期化なら unlink」で回避したが、
時間の経過は相手の死の証明にならない。SIGSTOP で止められている、
負荷で遅れている、といった **生きている作成者のセグメントを消し得た**。

**対応**: 名前にノンスを含める。

```
世代 1      : /shm_<topic>                     （root、ディレクトリ兼用）
世代 N >= 2 : /shm_<topic>#<N>-<ノンス48bit16進>
```

- ノンスは作成のたびに引き直すので、残骸が名前を塞ぐことがない。
  → `reclaimOrphanGeneration()` と `ORPHAN_WAIT_TIMEOUT_US` を **削除** した。
- 世代番号とノンスは `latest_generation` 1 語（上位 16bit / 下位 48bit）に詰め、
  **1 回の CAS で不可分に公開**する。別フィールドだと
  「新しい世代番号 × 古いノンス」の組を読み得る。
- 接続時はヘッダの `segment_nonce` と名前のノンスの一致も確認する。
- 後片付けは `unlinkStaleGenerations()` に置き換えた。消すのは
  **現役でないと確定できる 2 種類だけ**で、時刻は一切見ない。

  | 条件 | 判定 |
  |---|---|
  | 世代番号 < 現世代 | 既に root が先へ進んでいる → 消す |
  | 世代番号 == 現世代 かつ ノンス不一致 | 切り替え競争に負けた残骸 → 消す |
  | 世代番号 > 現世代 | **今まさに作成中かもしれない → 絶対に触らない** |

**回帰テスト**:
`OrphanedGenerationDoesNotBlockGrowthAndIsNotWaitedFor`（孤児があっても
200ms 以内に拡張が完了する = 待っていない）、
`SegmentOfAFutureGenerationIsNeverUnlinked`（先の世代のセグメントは消されない）。

---

## R03-F01 — cutover と発行番号

**(a) 採番元の一元化**

発行番号は「トピック内で一意」が契約だが、カウンタが世代ごとにあったため、
切替直後に旧世代へ滑り込んだ commit と新世代の最初の commit が
同じ番号を採り得た。R02 の `adoptSequenceFloor()` は floor を取った**後**の
旧世代 commit を防げない。

`RingBuffer::setSequenceSource()` を追加し、`ShmTopic` が全世代のリングに
**root のカウンタ**を束ねるようにした。どの世代へ commit しても同じ
`fetch_add` を通るので、重複が原理的に起きない。`adoptSequenceFloor()` は削除。

**(b) 切替直後の取りこぼしの回収**

履歴移行の開始から CAS 成立までの間に旧世代へ commit されたサンプルは、
移行対象の一覧に載っていない。CAS 後に旧世代のマッピングを保持したまま
`migrateHistory(old, new, after_sequence)` を最大 `MAX_DRAIN_ROUNDS` 回まわし、
移行済みより新しい番号のサンプルを拾い直す。

**(c) publish 後の世代確認を vector にも**

スカラ版には R02-F05 で入れた「コミット後に世代が変わっていたら発行し直す」を
vector 版に入れ忘れていた。`Publisher<std::vector<T>>::publishOnce()` を切り出し、
`publish()` が最大 4 回まで再試行するようにした。
(b) が CAS より前の commit を、(c) が CAS より後の commit を拾うので、
切替を跨いだサンプルの取りこぼしが無くなる。

**回帰テスト**:
`OldGenerationCommitCannotReuseANewGenerationSequence`（旧世代のリングを直接掴んだまま
世代を進め、そこへ commit しても番号が重複しないことを決定的に検証）、
`PublishingWhileTheGenerationMovesKeepsTheLatestValueVisible`（2 スレッドで
世代を動かし続けても publish が失敗せず、壊れた値も読めない）。

---

## R03-F04 — payload の data race

**問題**: `sequence -> memcpy -> fence -> sequence` は torn sample の**検出**には
なるが、writer と reader が同じ通常メモリへ同時に memcpy する可能性は残る。
少なくとも一方が write で、atomic でも mutex でも同期されていないので、
C++ のメモリモデル上は data race であり未定義動作である。

**対応**: レビューの推奨どおり **reader もスロットの robust mutex を取る**。
`RingBuffer::readSample(slot, dst, dst_size, info)` を新設し、
ロック下で「発行番号 → メタデータ → payload」を 1 つのスナップショットとして読む。
ロックを保持している間はスロットが書き換わらないので、
**発行番号の前後比較そのものが不要になり、読み出し経路が単純になった**。

副次的に R02-F03 の窓（payload と `SampleInfo` が別サンプルになる）も
構造的に閉じる。全読み出し経路がこの 1 本を通る:

| 経路 | 対応 |
|---|---|
| `Subscriber<T>` / `Subscriber<std::vector<T>>` | `readSlotInto()` → `readSample()` |
| `ShmTopic::migrateHistory()` | 直接 `readSample()` |
| `shm_pub_sub_cv` / `lidar_2D_data` / `point_cloud_2D_data` | 生バイト列を `staging_` へ退避 → ロック外で deserialize |

**writer を待たせない工夫**: reader がロックを持つようになると、
`buf_num=1` で reader が全力で回ると writer が確保できなくなる。
`lockSlotWithin()`（単調時計で区切った trylock + `sched_yield`、上限
`SLOT_LOCK_TIMEOUT_US = 2ms`）を writer/reader 双方で使い、
「奪わないが短時間なら待つ」に統一した。
`pthread_mutex_timedlock` は `CLOCK_REALTIME` 基準で NTP 補正の影響を受けるため使わない。

**メタデータも atomic 化**: `payload_size` / `capture_monotonic_us` /
`capture_realtime_us` は、読むスロットの選択や時刻検索でロック外から読む経路がある。
plain な整数のままでは同じ理由で data race なので `std::atomic<uint64_t>` にした
（`SlotRecord` のサイズは 128 バイトのまま）。

---

## R03-F02 — vector の最新値読み出しの統合

`Subscriber<std::vector<T>>::subscribe(bool*)` に seqlock が直書きのまま残っており、
`subscribe(bool*, SampleInfo*)` は **subscribe した後で** `getSampleInfo()` を
呼び直していた。R02-F03 で塞いだはずの窓がそのまま残っていた。

- `subscribe(bool*, SampleInfo*)` を本体にし、`subscribe(bool*)` は委譲だけにした。
- 読み出しは `readSlotInto()` → `readSample()` に一本化。payload 長の検証も
  ロック下の `payload_size` で行う。
- `is_success == nullptr` で `std::invalid_argument`（スカラと同じ契約）。

**回帰テスト**: `VectorPayloadAndSampleInfoAlwaysDescribeTheSameSample`。

---

## R03-F05 — 可搬な schema 版

`type_schema_id<T>()` は `__PRETTY_FUNCTION__` の hash なので
**同じツールチェインでしか一致しない**うえ、「型名は同じだがメンバを足した」という
互換性を壊す変更を検出できない。

`shm_schema<T>` トレイトを追加した（既定 `version = 0` = 未指定）。

```cpp
struct ScanHeader { uint32_t count; float angle_min; };
namespace irlab { namespace shm {
template <> struct shm_schema<ScanHeader> { static constexpr uint32_t version = 3; };
}}
```

- 版はヘッダの `schema_version` に記録され、**両者が 0 以外を指定したときだけ**照合する。
- 版が明示されている場合は `schema_id`（ツールチェイン依存）の照合を行わない。
  異なるコンパイラや言語バインディングをまたぐ通信で、可搬な版だけを根拠にできる。
- 手書きのシリアライズ書式を持つ 3 つの特殊化（cv::Mat / Lidar2dScanData /
  PointCloud2DScanData）に `schema_version = 1` を明示した。

**回帰テスト**: `ExplicitSchemaVersionIsRecordedAndEnforced`。

---

## 軽微な追加所見

| 所見 | 対応 |
|---|---|
| vector の `subscribe(bool*)` が null を逆参照 | F02 の統合で `std::invalid_argument` に統一 |
| `INIT_WAIT_TIMEOUT_US` のコメントが「timeout 後に再初期化する」のまま | 実装（takeover せず失敗）に合わせ、復活させない理由を明記 |
| `SequenceStaysUniqueAcrossGenerations` が競合を起こしていない | 決定的に競合させる `OldGenerationCommitCannotReuseANewGenerationSequence` を追加し、旧テストに相互参照を記載 |
| `PayloadAndSampleInfoAlwaysDescribeTheSameSample` が scalar のみ | vector 版を追加 |

---

## 検証結果

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
ctest --test-dir build -j1          -> 106/106 PASS (20.0s)

-DSANITIZER=address                 -> 106/106 PASS
-DSANITIZER=undefined               -> 106/106 PASS
setarch $(uname -m) -R … -DSANITIZER=thread -> 106/106 PASS
```

外部特殊化（`shm_pub_sub_cv` / `lidar_2D_data` / `point_cloud_2D_data`）は
`-fsyntax-only` で追随を確認済み。

## 未実施

- **Raspberry Pi 4（aarch64）での実行**。R02/R03 の変更後は未検証。
  `ShmHeader` を 192 バイトへ拡張したので `static_assert` の再確認が要る。
- 実センサでの通し確認（形式が変わったので `shm_tool remove` が先）。
- ドキュメント（形式仕様・世代セグメント名・`shm_schema<T>`）の更新。
