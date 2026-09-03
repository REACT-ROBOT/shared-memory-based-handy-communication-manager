#!/usr/bin/env python3
"""マークダウンの資料を、印刷して読める PDF に整形する。

マークダウンが正で、PDF はそこから生成する派生物である。
本文を直すときは .md を直し、このスクリプトを流し直すこと。

    python3 review/render_pdf.py review/merge-brief.md

Google Chrome のヘッドレス印刷を使う（pandoc / wkhtmltopdf は環境に無い）。
日本語は Noto Sans CJK JP に載せる。
"""

import html
import re
import subprocess
import sys
from pathlib import Path

import markdown

# ---------------------------------------------------------------------------
# 印刷用のスタイル
#
# 画面ではなく紙で読まれる前提なので、次を優先している。
#   - 見出しとその直後の内容が別ページに分かれない（break-after: avoid）
#   - 表が行の途中で切れない
#   - 引用ブロックは「必ず読ませたい注意」として枠で囲む
#   - 数字が並ぶ列は tabular-nums で桁を揃える
# ---------------------------------------------------------------------------
CSS = """
@page {
  size: A4;
  margin: 18mm 16mm 20mm;
}

:root {
  --ink:        #14181a;
  --ink-mid:    #47555a;
  --ink-soft:   #6b7b80;
  --line:       #c7d2d1;
  --line-soft:  #dee5e4;
  --surface:    #f2f5f5;
  --accent:     #2c6b66;
  --break:      #9c3b2e;
  --break-soft: #f7e6e2;
  --caution:    #8a6420;
  --sans: "Noto Sans CJK JP", "Noto Sans JP", sans-serif;
  --mono: "Noto Sans Mono CJK JP", "DejaVu Sans Mono", monospace;
}

* { box-sizing: border-box; }

body {
  font-family: var(--sans);
  font-size: 9.6pt;
  line-height: 1.75;
  color: var(--ink);
  margin: 0;
  -webkit-print-color-adjust: exact;
  print-color-adjust: exact;
}

/* ---- 表紙まわり ---- */
h1 {
  font-size: 20pt;
  font-weight: 700;
  line-height: 1.35;
  letter-spacing: -.01em;
  margin: 0 0 14px;
  padding-bottom: 14px;
  border-bottom: 2px solid var(--accent);
}

/* 冒頭のメタ情報（**対象**: ... の連なり） */
h1 + p {
  font-family: var(--mono);
  font-size: 8.4pt;
  line-height: 2;
  color: var(--ink-mid);
  background: var(--surface);
  border-left: 3px solid var(--line);
  padding: 10px 14px;
  margin: 0 0 16px;
}

h2 {
  font-size: 13.5pt;
  font-weight: 700;
  letter-spacing: -.005em;
  margin: 0 0 10px;
  padding-top: 4px;
  break-after: avoid;
  break-before: page;
}
/* 最初の h2（判断）は改ページしない。1 枚目に結論を置くため */
hr + h2 { break-before: auto; }
h2:first-of-type { break-before: auto; }

h3 {
  font-size: 10.8pt;
  font-weight: 700;
  color: var(--accent);
  margin: 20px 0 7px;
  break-after: avoid;
}

p { margin: 0 0 9px; orphans: 3; widows: 3; }
ul, ol { margin: 0 0 11px; padding-left: 1.5em; }
li { margin-bottom: 3px; }
strong { font-weight: 700; }

code {
  font-family: var(--mono);
  font-size: .88em;
  background: var(--surface);
  padding: 1px 3px;
  border-radius: 2px;
}

pre {
  font-family: var(--mono);
  font-size: 8.2pt;
  line-height: 1.6;
  background: var(--surface);
  border: 1px solid var(--line-soft);
  border-left: 3px solid var(--line);
  padding: 9px 12px;
  margin: 0 0 12px;
  white-space: pre-wrap;
  word-break: break-word;
  break-inside: avoid;
}
pre code { background: none; padding: 0; font-size: inherit; }

/* ---- 引用は「必ず読ませたい注意」---- */
blockquote {
  margin: 0 0 13px;
  padding: 11px 15px;
  background: var(--break-soft);
  border-left: 3px solid var(--break);
  break-inside: avoid;
}
blockquote p { margin: 0 0 5px; }
blockquote p:last-child { margin-bottom: 0; }
blockquote strong { color: var(--break); }

/* ---- 表 ---- */
table {
  border-collapse: collapse;
  width: 100%;
  font-size: 8.8pt;
  line-height: 1.6;
  margin: 0 0 14px;
  break-inside: avoid;
}
th, td {
  text-align: left;
  vertical-align: top;
  padding: 6px 9px;
  border-bottom: 1px solid var(--line-soft);
}
thead th {
  font-size: 7.6pt;
  font-weight: 700;
  letter-spacing: .07em;
  color: var(--ink-soft);
  background: var(--surface);
  border-bottom: 1px solid var(--line);
  white-space: nowrap;
}
tbody tr:last-child td { border-bottom: 1px solid var(--line); }
/* 右寄せ指定（--- ---: ）の列は数字が並ぶので桁を揃える */
th[align="right"], td[align="right"] {
  text-align: right;
  font-family: var(--mono);
  font-variant-numeric: tabular-nums;
  white-space: nowrap;
}
table code { font-size: .92em; }

/* 「影響」のような短い区分語が 1 文字ずつ折り返さないようにする。
   2 列目が短語の表（影響範囲、publish の走査結果）で効く。 */
td strong { white-space: nowrap; }
/* 立場・分類の列は語の途中で切らない */
tbody td:first-child { word-break: keep-all; }

hr {
  border: 0;
  border-top: 1px solid var(--line);
  margin: 22px 0 20px;
}

/* 最初の水平線の直後（＝判断）だけは目立たせる */
a { color: var(--accent); }
"""

FOOT = """
<div class="foot">
  <span>{title}</span>
  <span>{src}</span>
</div>
"""

PAGE_CSS = """
.foot {
  margin-top: 26px;
  padding-top: 10px;
  border-top: 1px solid var(--line);
  display: flex;
  justify-content: space-between;
  font-family: var(--mono);
  font-size: 7.6pt;
  color: var(--ink-soft);
}
"""


def build_html(md_path: Path) -> str:
    text = md_path.read_text(encoding="utf-8")

    # 最初の見出しを文書タイトルにする
    m = re.search(r"^#\s+(.+)$", text, re.M)
    title = m.group(1).strip() if m else md_path.stem

    body = markdown.markdown(
        text,
        extensions=["tables", "fenced_code", "sane_lists", "attr_list"],
    )

    foot = FOOT.format(title=html.escape(title),
                       src=html.escape(f"{md_path.name} が正 — 本文の修正はこちらへ"))

    return (
        "<!doctype html>\n"
        '<html lang="ja"><head><meta charset="utf-8">'
        f"<title>{html.escape(title)}</title>"
        f"<style>{CSS}{PAGE_CSS}</style>"
        f"</head><body>{body}{foot}</body></html>"
    )


def main() -> int:
    if len(sys.argv) < 2:
        print(f"使い方: {sys.argv[0]} <markdown ファイル> [出力 PDF]", file=sys.stderr)
        return 2

    md_path = Path(sys.argv[1]).resolve()
    if not md_path.exists():
        print(f"{md_path} がありません", file=sys.stderr)
        return 1

    pdf_path = Path(sys.argv[2]).resolve() if len(sys.argv) > 2 else md_path.with_suffix(".pdf")
    html_path = pdf_path.with_suffix(".html")

    html_path.write_text(build_html(md_path), encoding="utf-8")

    # Chrome のヘッドレス印刷。--no-sandbox はコンテナ内でも動かすため。
    cmd = [
        "google-chrome", "--headless", "--disable-gpu", "--no-sandbox",
        "--no-pdf-header-footer",
        f"--print-to-pdf={pdf_path}",
        html_path.as_uri(),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if not pdf_path.exists():
        print(result.stderr or result.stdout, file=sys.stderr)
        print("PDF を生成できませんでした", file=sys.stderr)
        return 1

    html_path.unlink(missing_ok=True)
    size_kb = pdf_path.stat().st_size / 1024
    print(f"{pdf_path}  ({size_kb:.0f} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
