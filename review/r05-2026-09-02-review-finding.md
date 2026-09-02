# R05 レビュー所見

- 実施日: 2026-09-02
- 対象コミット: `4b00efc`（R04 対応完了・Raspberry Pi 4 でビルドとテスト 138/138 PASS を確認した時点）
- 実施体制: 観点を分けた **3 名の独立レビュアー**による並行レビュー
  1. 致命的欠陥の有無（実機投入の可否を判断する）
  2. R04 で新しく入ったコードの堅牢性（破壊注入）
  3. ドキュメントと対応記録の正確さ、利用者視点
- レビュアーはリポジトリを変更していない。再現コードは scratchpad のみ

## 判定

| レビュアー | 判定 |
|---|---|
| 致命的欠陥 | **あり（Critical 1 件）** |
| 堅牢性 | なし |
| ドキュメント | なし（ただし Critical 相当の機能未達が 1 件） |

**このレビューの意義は、R04 までの 4 回では出なかった種類の問題を 2 つ捕まえたことにある。**

1. **別々に正しい 2 つの修正が、重なったときだけ壊れる**（Critical-1）。
   R04-F08（`PTHREAD_PRIO_INHERIT`）と R04-F23（`pthread_mutex_clocklock`）は
   単独ではどちらも安全で、両方入れたときだけプロセスが `SIGABRT` で落ちる。
   しかも **TSan ビルドではスピン待ちにフォールバックするため原理的に検出できず**、
   138/138 が緑のまま潜んでいた。
2. **回帰テストが「たまたま通っていた」**（R05-F01）。
   `SHM_DECLARE_LAYOUT` が塞ぐと謳った当のケースを検出できていなかったが、
   テストに使った 2 型のサイズが違ったため偶然オフセットがずれて検出でき、
   緑になっていた。R03 で指摘された「テストが機能を守れていない」の再発である。

---

## Critical-1 — `PTHREAD_PRIO_INHERIT` + 待ち時間つきロックがプロセスを `SIGABRT` で落とす

**該当**: `shm_base/src/ring_buffer.cpp` の `lockSlotWithin()`（R04-F23 / `f3f2c89`）と
`initializeExclusiveAccess()`（R04-F08 / `a068df1`）

robust + PI の mutex に待ち時間つきロックを掛けたとき、futex ワードに
「生きているタスクとして解決できない TID」が入っていると glibc が

```
Assertion `e != ESRCH || !robust' failed.  (nptl/pthread_mutex_timedlock.c:375)
```

で **abort** する。戻り値ではなく assert なので捕捉も回復もできない。

```
protocol=none          trylock -> 16(EBUSY)  clocklock -> 110(ETIMEDOUT)
protocol=PRIO_INHERIT  trylock -> 16(EBUSY)  clocklock -> Assertion failed / abort
```

**成立条件（3 つとも実証済み）**

| 引き金 | 実証方法 |
|---|---|
| セグメントの mutex 領域が別プロセスの誤書き込み・ビット反転で壊れた | ランダム破壊 fuzz が自力で発見（400 回中 1 回）、決定的に再現 |
| 保持者が**別の PID namespace** に居る（コンテナ間で `/dev/shm` を共有） | `unshare -r --pid --fork` で再現 |
| 保持したまま `munmap` して終了し、robust list が処理されなかった | 単体で再現。**R04 が自分で見つけた不具合そのものの残骸** |

いずれも `/dev/shm` に**永続**する（`trylock` は永久に `EBUSY`、`EOWNERDEAD` にならない）。
`readSample()` は subscribe のたびに必ず通るので、汚染スロットの `sequence != 0` なら
**Subscriber が起動のたびに同じスロットを選んで死ぬ恒久的なクラッシュループ**になる。
`sequence == 0` なら `getOldestBufferNum()` が必ずそれを返すので Publisher が落ちる。
例外ではなく `SIGABRT` なので、上位の try/catch も安全停止も効かない。

**関連**: `docker/launch_robot_container.sh` は `--ipc=host` を使っていないので現状 2 番目は
成立しないが、`docker/launch_simulator_container.sh` は使っている。

---

## R05-F01 — レイアウトハッシュが「塞ぐ」と謳ったケースを検出しない

**該当**: `shm_base/include/shm_base.hpp` の `SHM_LAYOUT_FIELD` / `layout_hash()`

畳み込んでいたのは `offsetof` / `sizeof` / `alignof` の 3 数値だけで、**メンバ名も型名も
入っていなかった**。`layout_covers_type()` が「宣言順＝オフセット順」を強制するため、
構造体を並べ替えた利用者はマクロ列も並べ替えざるを得ず、結果としてハッシュが変わらない。

```
PoseA{double x,y,theta} vs PoseB{double theta,x,y}   aaa522b5 vs aaa522b5  未検出
{uint32_t n; float v}   vs {uint32_t n; int32_t v}   541e605c vs 541e605c  未検出
Inner{float x,y}        vs Inner{float y,x}（入れ子） c84b2232 vs c84b2232  未検出
```

`r04-remediation.md` / `spec_jp.md` / `spec_en.md` / ヘッダのコメントが揃って
「並べ替え・型入れ替えがすべてハッシュに出る」「唯一の限界はパディングにちょうど
収まる場合だけ」と書いており、**いずれも不正確**だった。

---

## R05-F02 / F03 — 正しい宣言を弾く

| 型 | 結果 |
|---|---|
| `struct { char a; alignas(16) int b; };` | `static assertion failed`（**正しいのに弾かれる**） |
| `struct { uint32_t a; union { uint32_t s; uint64_t l; }; };` | 代表に選ぶメンバによって通ったり弾かれたり |

隙間の許容量に `alignof(decltype(T::m))` を使っていたのが原因。`decltype` は素の型を
返すので**メンバ宣言に付けた `alignas` を拾わない**。90 種類以上のペイロード型を
移行する計画なので、移行の途中で確実に踏む。

---

## R05-F04 — 世代タグ未公開のトピックが恒久的に詰み、`doctor` は OK と言う

**該当**: `shm_base/src/shm_topic.cpp` の `openRoot()` 作成分岐

root は 1 文目で `INITIALIZED` になるが、`latest_generation` の公開は 2 文目である。
この間に他プロセスが接続すると、publish 後の `isGeneration()` が root のタグ（まだ 0）と
食い違って false を返し、4 回再試行して例外で失敗する。
**タグを公開できるのは作成者だけ**なので、作成者がこの 2 文の間で死ぬと誰も直せない。

```
試行1〜3: 例外: shm::Publisher: the layout generation kept changing while publishing
--- この状態で shm_tool doctor ---
r05tagwin2  4  1 scalar  4 B  3  未宣言  640 B  0 ms  OK（下の注記を参照）
1 個のセグメント: 対処が必要 0 件、注記 1 件
```

一過性（作成者が生きている場合）でも、24 プロセス × 1200 ラウンドを CPU 負荷下で
回して 1 回発生した。

---

## R05-F05 — `shm_tool doctor` が壊れたヘッダで落ちる／誤診する

**該当**: `tools/shm_tool/src/main.cpp` の `inspectSegment()`

`RingBuffer::inspectLayout()` を通さず自前の緩い検査しかしていなかった。

| 症状 | 詳細 |
|---|---|
| **SEGV** | `buf_num * slot_size` と `slot_offset + …` が `uint64` で折り返り、ガードを通過してマッピング外を読む |
| **非アラインな atomic 読み** | `slot_offset` が 64 バイト境界に載らない場合。x86 では動くが **aarch64 では SIGBUS** |
| **誤診** | `buf_num` 範囲外・`element_capacity` / `total_size` 過大・`slot_offset` 改竄を、すべて「OK」と報告（ライブラリは接続を拒否する） |

ランダム破壊 4500 回では出ず、狙った値でのみ再現。診断ツールなので影響は限定的だが、
**doctor はまさに「壊れたセグメントを調べる」ために使うもの**で、一番使いたい場面で落ちる。

---

## Low（今後の改善項目）

| ID | 内容 |
|---|---|
| L1 | `createNextGeneration()` の `attachGeneration()` 失敗経路が `releaseOwnedSlots()` を飛ばす。Critical-1 のケース 3 を自力で作り得る唯一の残存経路 |
| L2 | `migrateHistory` のスナップショット後・CAS 前に旧世代へ commit した publisher は、`isGeneration()` 検査が CAS より前に走るため再発行しない（R04-F11 で削除したドレインが拾っていたケース）。容量拡張時のみなので Low |
| L3 | `RingBuffer::getSize()` の `std::invalid_argument` が `openRoot()` / `createNextGeneration()` の try の外で投げられ、bool を返す契約の `ensureCapacity()` から漏れる |
| L4 | `getNewestBufferNum()` の `markAsRead()` が、その後 `readSample()` が全リトライ失敗しても実行済み。`waitFor()` が 1 更新分取りこぼし得る |
| L5 | `pthread_mutex_clocklock` は kernel < 5.14 + PI mutex で `ENOTSUP` を返す可能性（未実証）。Critical-1 の対応で消える |
| L6 | 型側 `alignas` を付けた型では、末尾の書き漏らしを見逃す窓が `alignof(T)-1` バイトまで広がる（`offsetof`/`sizeof` だけでは原理的に判別できない） |
| L7 | ビットフィールドを含む型は `offsetof` の生の診断が出る。静的表明で明示するほうが親切 |

---

## ドキュメントの食い違い（21 件）

R04-F17 で `spec_*.md` は更新されたが、**それ以外は v1.x 時代のまま**だった。

| 深刻度 | 件数 | 代表例 |
|---|---|---|
| High | 6 | `subscribeAlignedTo` の引数順が誤り（主用途の唯一のサンプル、2 箇所）／`-lshm_pub_sub` は存在しない（実際は `shm_pub_sub.so`）／インクルードパスが 2 つ要るのに 1 つ／「ヘッダオンリー」と書いてあるが違う／`find_package` の記述が皆無 |
| Medium | 11 | `ipcs -m` での診断（System V 用で無関係）／「ゼロコピー」（実際は 1 回コピー）／存在しない 6 ファイルへのリンク／`BUILD_TESTING` と `BUILD_TESTS` の取り違え／クラス図が v1 のまま／`subscribeAt()` がどこにも書かれていない |
| Low | 4 | README のライセンスバッジ矛盾／`SearchStatus::InvalidReference` が表に無い／出典が実在しない可能性 |

**動かないコード例が 7 件**あり、いずれも実際のコンパイル・実行エラーで確認された。

## 利用者が踏みそうだが文書で塞がれていない穴（10 件）

| ID | 深刻度 | 内容 |
|---|---|---|
| H01 | High | `subscribe()` の**既定期限 2 秒**がどこにも書かれていない。低レートのトピックで「受信できない」の最有力原因なのに、troubleshooting の原因表にも無い |
| H02 | High | `subscribe()` の返り値は **2 回目**の呼び出しで黙って化ける。1 回は生き残るので「試したら大丈夫だった」で通してしまう |
| H03 | High | 独自型を載せる手順（`SHM_DECLARE_LAYOUT` と**置き場所の 2 制約**）が利用者向け文書に無い。spec の「新しいデータ型の追加」は v1 のまま |
| H04 | High | トピック名の制約が無い。`'/'` は `'_'` に置換されるので **`"a/b"` と `"a_b"` が無警告で衝突する**。ROS 風に `/odom` と書く利用者は必ず居る |
| H05 | Medium | 既定権限 0660 は umask で実際には 0640 になり、グループは読み取り専用 |
| H06 | Medium | `buffer_num` の決め方が spec にしかなく、quickstart / チュートリアルは既定 3 のまま注意なし |
| H07 | Medium | Python に時刻合わせ API が無いことが書かれていない。`import` の通し方も未記載 |
| H08 | Medium | `shm_tool` の入手方法（どこにビルドされ、PATH にどう通すか）がゼロ行 |
| H09 | Low | `shm_base.so` は無印なので、移行時に古い `.so` を掴んで謎の undefined symbol になる |
| H10 | Low | `max_skew_us` に 0 を渡すと R04-F14 以前の挙動に戻ることが spec に書かれていない |

---

## 壊そうとして壊れなかった領域

指摘と同じくらい重要なので明記する。

- **`acquireWritableSlot()` の境界**: `buf_num` = 1/2/3/63/64/65/127/128/129/1023/1024 で
  全スロット確保・重複・満杯後の `-1` を ASan と UBSan(`alignment` 付き) で検証。すべて期待どおり
- **ライブラリ本体の破壊耐性**: ヘッダ + スロット全域をランダム破壊する fuzz 計 1050 反復
  （scalar/vector × pub/sub を子プロセスで実行）で**範囲外アクセス 0 件**。すべて理由つきで拒否
- **`openRoot()` の破棄順序（R04-F01）**: `shm_tool remove` を 0.25 秒間隔で連打しながら
  publisher/subscriber を回す ASan ストレス 25 秒で、クラッシュ 0 / ASan 報告 0
- **通常運用の並行性**: vector publisher×2（世代を churn）+ scalar×2 + subscriber×4 + SIGKILL ループ、
  25 秒で読み出し成功 39 万件、ペイロード破損 0、デッドロック 0
- **robust mutex の EOWNERDEAD / ENOTRECOVERABLE**: SIGKILL ループ下でもデータ破損 0
- **`shm_tool` の引数**: `''` / `'#'` / `--help` / 10 万文字 / `../../etc` / 未知コマンド、すべて安全。
  パストラバーサルも無し
- **`releaseOwnedSlots()` の呼び出し経路**: L1 の 1 経路を除き、すべて munmap より前に呼ばれる
- **`inspectLayout()` の「誤って NotReady」側**: 12 パターンで該当なし
