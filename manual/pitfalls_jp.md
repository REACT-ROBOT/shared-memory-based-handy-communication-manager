# ⚠️ よく踏む落とし穴
[[English](../md_manual_pitfalls_en.html) | 日本語]

コンパイルは通り、単体では動き、しばらく経ってから困る種類のものを集めた。
すべて実際に手元で再現して確かめてある。

- [1. `subscribe()` は 2 秒で期限切れになる](#1-subscribe-は-2-秒で期限切れになる)
- [2. `subscribe()` の返り値は 2 回目で化ける](#2-subscribe-の返り値は-2-回目で化ける)
- [3. トピック名の `/` は `_` に置換される](#3-トピック名の--は-_-に置換される)
- [4. 権限 0660 は umask に引かれて 0640 になる](#4-権限-0660-は-umask-に引かれて-0640-になる)
- [5. `buffer_num` は履歴の長さを決める](#5-buffer_num-は履歴の長さを決める)
- [6. 独自型を載せるときの置き場所](#6-独自型を載せるときの置き場所)
- [7. `max_skew_us` に 0 を渡すと上限が無くなる](#7-max_skew_us-に-0-を渡すと上限が無くなる)
- [8. Python には時刻合わせ API が無い](#8-python-には時刻合わせ-api-が無い)
- [9. `shm_tool` の入手と PATH](#9-shm_tool-の入手と-path)
- [10. 古い `shm_base.so` を掴む](#10-古い-shm_baseso-を掴む)
- [11. `publish()` は失敗すると例外を投げる](#11-publish-は失敗すると例外を投げる)
- [12. 1 トピックに publisher が複数あると、容量拡張で 1 件落ち得る](#12-1-トピックに-publisher-が複数あると容量拡張で-1-件落ち得る)

---

## 1. `subscribe()` は 2 秒で期限切れになる

**「publish しているのに受信できない」の最有力原因である。**

`subscribe()` は最新のサンプルを返すが、その `capture_monotonic_us` が現在時刻から
**既定 2 秒**より古いと `state = false` にする。1Hz や 0.2Hz のトピック、あるいは
デバッガで止めた直後に「なぜか届かない」となる。

```cpp
Publisher<int> pub("slow"); Subscriber<int> sub("slow");
pub.publish(7);
bool ok = false;
sub.subscribe(&ok);          // ok = true
sleep(3);
sub.subscribe(&ok);          // ok = false  ← データは残っているのに失敗する
```

期限を変えるか、無効にする。

```cpp
sub.setDataExpiryTime_us(10 * 1000 * 1000);  // 10 秒
sub.setDataExpiryTime_us(0);                 // 0 = 期限なし
```

`subscribe()` は**キューではない**。次が来るまで同じ値を返し続けるので、
発行より速く回すとデータが失われるのではなく、同じ値が繰り返し返る。
取りこぼしを知りたいなら `SampleInfo::sequence` の連番を見ること。

---

## 2. `subscribe()` の返り値は 2 回目で化ける

返り値は内部のダブルバッファへの**参照**である。1 回後までは生き残るが、
**2 回目の `subscribe()` で書き換わる**。1 回では壊れないので、
「試したら大丈夫だった」で通してしまいやすい。

```cpp
const int &r = sub.subscribe(&ok);   // r == 11
sub.subscribe(&ok);                  // r == 11  ← まだ生きている
sub.subscribe(&ok);                  // r == 33  ← 化けた
```

後で使うなら**値でコピーする**こと。

```cpp
const int value = sub.subscribe(&ok);   // 参照ではなく値で受ける
```

ダブルバッファにしてあるのは、失敗した `subscribe()` が直前に返した値を
壊さないためである（読み出しは常に「今返していない方」へ行う）。
そのぶん寿命が 1 回ぶん延びているだけで、保持されることは保証していない。

---

## 3. トピック名の `/` は `_` に置換される

ROS の習慣で `/odom` と書きたくなるが、`/` は `_` に置換される。したがって
**`"a/b"` と `"a_b"` は無警告で同じトピックになる。**

```cpp
Publisher<int>  pub("a/b");   // 実体は /dev/shm/shm_a_b
Subscriber<int> sub("a_b");   // 同じものに繋がる（成功してしまう）
```

次は例外で弾かれる。

| 名前 | 結果 |
|---|---|
| `'#'` を含む | 例外。`/shm_<topic>#<世代>-<ノンス>` の区切りに使うため予約されている |
| 200 文字超 | 例外（`shared memory name is too long`） |

`/` は弾かれない。名前空間を分けたいなら `_` か `.` を自分で使い、
`/` を混ぜないと決めておくのが安全である。

---

## 4. 権限 0660 は umask に引かれて 0640 になる

`DEFAULT_PERM` は `0660` だが、`shm_open()` は**作成時の `umask` を引く**。
`umask 0022`（多くのディストリビューションの既定）では実際には `0640` になり、
同一グループの別ユーザは**読めるが書けない**。

```
umask 0022 で作ると: -rw-r----- 1 user group ... shm_topic
```

複数ユーザ／複数コンテナで書き合うなら、publisher を起こす**前に**
`umask 0002` にしておくこと。既に作られたセグメントの権限は変わらないので、
`shm_tool remove` してから作り直す。

---

## 5. `buffer_num` は履歴の長さを決める

`buffer_num` はスロット数であり、**保持できる履歴の長さ**でもある。

```
履歴の長さ ≒ buffer_num ÷ 発行レート
```

10Hz のセンサを既定の `buffer_num = 3` で作ると、履歴は **300ms** しか無い。
時刻合わせ（`subscribeAlignedTo()` / `subscribeAt()`）で 500ms 前を引こうとしても
`TooOld` になる。実際に保持している時間幅は確認できる。

```cpp
const RetentionWindow w = sub.getRetentionWindow();
```

```bash
shm_tool doctor    # 「履歴」欄に実測値が出る
```

同時に書き込む writer が多い場合も `buffer_num` を増やす。writer は空いている
スロットを `trylock` で探し、全部埋まっていると publish に失敗する。

---

## 6. 独自型を載せるときの置き場所

`SHM_DECLARE_LAYOUT()` を付けると、メンバの位置・大きさ・名前・型から
レイアウトのハッシュが自動で作られ、版番号を手で維持せずに
「同サイズのまま並べ替えた」変更まで検出できるようになる。

```cpp
struct Pose { double x, y, theta; };
SHM_DECLARE_LAYOUT(Pose, x, y, theta);   // 名前空間の外、グローバルスコープで
```

メンバを 1 つでも書き漏らすと**コンパイルエラーになる**（`layout_covers_type()` が
隙間を検出する）。宣言順に全部並べること。

ただし検出には 2 つの原理的な限界がある。どちらも `offsetof` と `sizeof` からは
判別できないので、検査では塞げない。

**限界 1: 書き漏らしが「どのみち必要なパディング」に収まる場合**

```cpp
struct A { uint32_t a;                  double b; };  // a@0 b@8 sizeof 16
struct B { uint32_t a; uint32_t hidden; double b; };  // a@0 b@8 sizeof 16
```

listed なメンバから見て完全に同一なので区別できない。通常の型では隙間は
最大 `alignof(T)-1` バイトなので、入るのは小さなメンバに限られる。

**限界 2: 型に `alignas` を付けると、末尾の窓が広がる**

```cpp
struct alignas(32) Over { uint64_t a; uint64_t hidden; };  // sizeof 32
SHM_DECLARE_LAYOUT(Over, a);   // hidden を書き漏らしても通ってしまう
```

`sizeof` が 32 の倍数へ切り上がるため、**末尾に 31 バイトまでの書き漏らしを
見逃す**。`alignas` を付けた型では、宣言に全メンバが並んでいることを目で
確かめること。

**ビットフィールドを含む型は宣言できない**

```cpp
struct WithBitfield { uint32_t a : 4; uint32_t b : 28; };
SHM_DECLARE_LAYOUT(WithBitfield, a, b);
// error: attempt to take address of bit-field structure member 'WithBitfield::a'
```

`offsetof` も `sizeof` もビットフィールドには使えないため、この生の診断が出る。
そもそもビットフィールドの配置は実装定義でプロセス間の合意にできないので、
共有メモリに載せる型では使わないこと（固定幅整数とマスクで表す）。

ワイヤ形式が `serialize()` の実装で決まる型（`cv::Mat` や独自のスキャン型）は
レイアウトから導出できないので、版を手で持つ形を使う。

```cpp
SHM_DECLARE_SERIALIZED_FORMAT(MyScan, 1);   // serialize() を変えたら 1 → 2
```

**置き場所に 2 つの制約がある。** どちらも x86 の gcc 11 では通ってしまい、
Raspberry Pi 4（gcc 12）のビルドで初めて落ちる。

1. **インクルードガードの内側に置く。** 外に出すと、同じヘッダを推移的に
   2 回取り込んだ翻訳単位でマクロが再展開され、`shm_schema<T>` が二重定義になる。
2. **`Publisher` / `Subscriber` の特殊化より前に置く。** それらの `contractOf()` が
   `schema_version_of<T>()` を呼ぶので、後ろに置くと「暗黙実体化の後に特殊化を
   宣言した」ことになり ill-formed である。実体化の時点はコンパイラの版で変わる。

入れ子の型も同じ理由で、**外側より前に**宣言する。

```cpp
struct Vec2 { double x, y; };
struct Path { Vec2 a; Vec2 b; };
SHM_DECLARE_LAYOUT(Vec2, x, y);      // 先
SHM_DECLARE_LAYOUT(Path, a, b);      // 後（Vec2 の版を畳み込むため）
```

移行中のパッケージを 1 つずつ潰すには `-DSHM_REQUIRE_LAYOUT=ON` でビルドする。
未宣言の型を publish / subscribe した時点でコンパイルエラーになる。
どのトピックが未宣言かは `shm_tool doctor` が「未宣言」と表示する。

---

## 7. `max_skew_us` に 0 を渡すと上限が無くなる

`subscribeAlignedTo()` の `max_skew_us` は必須引数だが、**`0` を渡すとずれの判定を
丸ごと飛ばす**。`Nearest` は有効なサンプルが 1 つでもあれば必ず「最も近いもの」を
返すので、何時間ずれていても `Success` になる。

```cpp
scan_sub.subscribeAlignedTo(odom_info, &st, 0);       // 上限なし。危険
scan_sub.subscribeAlignedTo(odom_info, &st, 50000);   // 50ms を超えたら弾く
```

融合してよい上限は用途が決めるものなので、必ず具体的な値を書くこと。

なお、失敗した `subscribe()` の `SampleInfo` は全ゼロである。それを基準に渡すと
時刻 0 に対する検索になるが、これは `InvalidReference` で弾かれる。

---

## 8. Python には時刻合わせ API が無い

Python バインディングが提供するのは `bool` / `int` / `float` の
`Publisher` / `Subscriber` と `publish()` / `subscribe()` だけである。
次は**すべて C++ 専用**である。

- `subscribeAt()` / `subscribeAlignedTo()` / `SampleInfo` / `RetentionWindow`
- `setDataExpiryTime_us()` / `waitFor()`
- `std::vector<T>` と独自型

時刻合わせが要るノードは C++ で書くこと。

`import` を通すには、Python モジュールが置かれる `<build>/python` を
`PYTHONPATH` に入れる。site-packages には**入らない**（`PYTHON_INSTALL_DIR` の
既定が `${CMAKE_BINARY_DIR}/python` である）。

```bash
export PYTHONPATH=$PWD/build/python:$PYTHONPATH
export LD_LIBRARY_PATH=$HOME/.local/lib:$LD_LIBRARY_PATH   # shm_base.so の解決に要る

python3 -c "
import shm_pub_sub
p = shm_pub_sub.Publisher('py_demo', 0, 3)   # (topic, 型の見本, buffer_num)
s = shm_pub_sub.Subscriber('py_demo', 0)
p.publish(42)
print(s.subscribe())        # -> (42, True)   値と成否のタプル
"
```

`subscribe()` は C++ と違って**値と成否のタプル**を返す。
`Publisher` / `Subscriber` は第 2 引数の見本の型で `Bool` / `Int` / `Float` の
どれを作るかを決める。

---

## 9. `shm_tool` の入手と PATH

`shm_tool` はこのリポジトリのビルドで作られる。パッケージには入っていない。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$HOME/.local
cmake --build build -j$(nproc)
cmake --install build          # $HOME/.local/bin/shm_tool

export PATH=$HOME/.local/bin:$PATH
export LD_LIBRARY_PATH=$HOME/.local/lib:$LD_LIBRARY_PATH
```

インストールせずに使うなら `build/tools/shm_tool/shm_tool` にある。
どちらの場合も `LD_LIBRARY_PATH` が要る（次項）。

| コマンド | 用途 |
|---|---|
| `shm_tool list` | `/dev/shm` にあるセグメントを並べる |
| `shm_tool doctor [topic]` | ヘッダを読んで、ABI・書式・履歴・異常を報告する |
| `shm_tool remove <topic>` | トピックを世代セグメントごと消す |

`doctor` は対処が必要なものがあると終了コードを変えるので、
起動スクリプトの前段に置いて使える。

---

## 10. 古い `shm_base.so` を掴む

共有ライブラリに `lib` の接頭辞が付かないため、`shm_pub_sub.so` は
`shm_base.so` という**素の名前**を `DT_NEEDED` に持ち、`RUNPATH` を持たない。
したがって解決先は `LD_LIBRARY_PATH` の**先頭にあるものが勝つ**。

古いワークスペースの `build/lib` がパスに残っていると、こうなる。

```
shm_tool: symbol lookup error: shm_tool: undefined symbol:
  _ZN5irlab3shm15disconnectTopicERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE
```

ライブラリを更新したら、必ずどれを掴んでいるか確認する。

```bash
ldd $(which shm_tool) | grep shm
ldd ./your_program    | grep shm
```

同じ理由で、`.so` を差し替えただけでは**動いているプロセスは古い実装のまま**である。
ABI を上げたときは、そのトピックを使う全プロセスを起動し直すこと。
古い ABI のセグメントが残っているかどうかは `shm_tool doctor` が指摘する。

---

## 11. `publish()` は失敗すると例外を投げる

`publish()` は失敗し得る。**5 つの特殊化すべてで `std::runtime_error` を投げる。**

```cpp
try {
  scan_pub.publish(scan);
} catch (const std::runtime_error &e) {
  // 取りこぼした。e.what() に理由が入っている
}
```

失敗するのは次のいずれかで、どれも例外的な事象である。

| 理由 | よくある原因 |
|---|---|
| 全スロットが reader に押さえられたまま数 ms 空かない | サンプルを長く抱えている subscriber、`buffer_num` 不足 |
| 要求容量を満たすセグメントを用意できない | `/dev/shm` の枯渇、`MAX_ELEMENT_SIZE` 超過 |
| シリアライズに失敗した | 呼び出し側のデータが壊れている |
| 世代が 4 回続けて切り替わった | 複数の publisher が長さを激しく変えている |

**捕まえないと `std::terminate` でプロセスが落ちる。** 黙って取りこぼすより
落ちて気付いた方がよい種類の失敗なので、それを既定にしてある。センサ daemon の
発行ループでは、取りこぼしを数えて動き続けるか、意図して落とすかを決めて書くこと。

```cpp
while (running) {
  try {
    scan_pub.publish(scan);
  } catch (const std::runtime_error &e) {
    ++dropped;                       // 数えて続行する場合
    std::cerr << e.what() << std::endl;
  }
}
```

> `Lidar2dScanData` / `PointCloud2DScanData` は以前 `std::cerr` に出して続行し、
> 呼び出し側からは失敗が見えなかった。その後 `bool` を返す形を経て、
> 現在は他の型と同じく例外に統一されている。**古い呼び出し側は
> try/catch が無いと落ちるようになる**ので、移行時に確認すること。

失敗が続くようなら `buffer_num` を増やすか、`shm_tool doctor` で
そのトピックの履歴と競合を確認する。

---

## 12. 1 トピックに publisher が複数あると、容量拡張で 1 件落ち得る

可変長のトピック（`std::vector<T>` / スキャン / 点群 / 画像）は、必要な容量が
増えると**新しい世代のセグメント**を作って切り替える。このとき旧世代の履歴を
新世代へ引き継ぐが、次の順序が重なると 1 件だけ引き継がれない。

```
切替側: 履歴のスナップショットを取る
別の publisher: 旧世代へ書き込む
別の publisher: 世代を確認する → まだ切り替わっていないので発行し直さない
切替側: 世代を切り替える
```

このサンプルはスナップショットに入らず、発行し直されもしない。
`SampleInfo::sequence` に欠番として現れる。

**条件は「同じトピックに publisher が 2 つ以上あり、かつ容量拡張が起きる」ことである。**
publisher が 1 つなら起きない。拡張を起こすのも書き込むのも同じ呼び出しなので、
同時には走らないためである。

避けるには次のいずれかを取る。

- **1 トピック 1 publisher にする**（最も確実で、設計としても分かりやすい）
- 起動時に最大サイズを一度 publish して容量を先に確保しておく
  （容量は増やすだけで運用されるので、以後の拡張が起きない）

塞げないのは、引き継ぎの窓を消す手段がどれも別の問題を生むためである。
切替後にもう一度さらうと同じ測定が二重に現れ、スナップショットを取り直しても
新世代のスロットは既に埋まっている。

