#!/usr/bin/env python3
"""ペイロード書式の宣言の置き場所を検査する（R04 / Pi4 のビルドで露見）

`SHM_DECLARE_LAYOUT` / `SHM_DECLARE_SERIALIZED_FORMAT` は `shm_schema<T>` の
特殊化を定義する。置き場所に 2 つの制約がある。

  1. **インクルードガードの内側**。外に出すと、同じヘッダを推移的に 2 回
     取り込んだ翻訳単位でマクロが再展開され、二重定義になる。

  2. **その型を使う Publisher / Subscriber の特殊化より前**。それらの
     contractOf() は schema_version_of<T>() を呼ぶので、後ろに置くと
     「暗黙実体化の後に特殊化を宣言した」ことになり ill-formed である。

2 番目が厄介なのは、**コンパイラの版によって通ってしまう**ことである。
実体化の時点（point of instantiation）の扱いに幅があるため、x86 の gcc 11 では
通り、Raspberry Pi 4 のビルドで初めて落ちた。コンパイラに頼らず、
ここで静的に検査する。
"""
import os
import re
import sys

# 検査対象。ワークスペースに無ければ黙って飛ばす（単体リポジトリでも動くように）
CANDIDATES = [
    "shm_pub_sub/include/shm_pub_sub.hpp",
    "shm_pub_sub/include/shm_pub_sub_vector.hpp",
    "../react_cv/shm_pub_sub_cv/include/shm_pub_sub_cv.hpp",
    "../sensor_daemons/lidar_2D_related/lidar_2D_data/include/shm_pub_sub_lidar_2D_data.hpp",
    "../sensor_daemons/point_cloud_2D_related/point_cloud_2D_data/include/shm_pub_sub_point_cloud_2D_data.hpp",
]

DECLARE_RE = re.compile(r"^\s*SHM_DECLARE_(?:LAYOUT|LAYOUT_REV|SERIALIZED_FORMAT)\s*\(", re.M)
USE_RE = re.compile(r"schema_version_of\s*<")
ENDIF_RE = re.compile(r"^\s*#endif", re.M)


def strip_comments(text):
    """コメントを同じ長さの空白に置き換える（行番号とオフセットを保つ）

    コメント内に schema_version_of<> と書いてあるだけで誤検出しないため。
    文字列リテラルまでは扱わない（このマクロの置き場所検査には不要）。
    """
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j):
                out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n":
                    out[k] = " "
            i = j
        else:
            i += 1
    return "".join(out)


def check(path):
    with open(path, encoding="utf-8") as f:
        raw = f.read()
    text = strip_comments(raw)

    problems = []
    declarations = [m.start() for m in DECLARE_RE.finditer(text)]
    if not declarations:
        return problems  # 宣言が無いヘッダは対象外

    def line_of(offset):
        return text.count("\n", 0, offset) + 1

    # 1. インクルードガードの内側にあること
    endifs = [m.start() for m in ENDIF_RE.finditer(text)]
    if endifs:
        last_endif = endifs[-1]
        for d in declarations:
            if d > last_endif:
                problems.append(
                    "%s:%d: 宣言がインクルードガード (#endif は %d 行) の外にある。"
                    "二重インクルードで shm_schema<T> が二重定義になる"
                    % (path, line_of(d), line_of(last_endif)))

    # 2. schema_version_of<T>() の最初の使用より前にあること
    first_use = USE_RE.search(text)
    if first_use is not None:
        first_declaration = min(declarations)
        if first_declaration > first_use.start():
            problems.append(
                "%s:%d: 宣言が schema_version_of<> の使用 (%d 行) より後ろにある。"
                "暗黙実体化の後に特殊化を宣言することになり ill-formed である"
                "（コンパイラの版によっては通ってしまうので、ここで弾く）"
                % (path, line_of(first_declaration), line_of(first_use.start())))

    return problems


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    checked = 0
    problems = []
    for relative in CANDIDATES:
        path = os.path.normpath(os.path.join(root, relative))
        if not os.path.exists(path):
            continue
        checked += 1
        problems.extend(check(path))

    if checked == 0:
        print("検査対象のヘッダが見つからない", file=sys.stderr)
        return 1
    for p in problems:
        print(p, file=sys.stderr)
    print("%d 個のヘッダを検査、%d 件の問題" % (checked, len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
