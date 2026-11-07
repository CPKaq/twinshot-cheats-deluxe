
# -*- coding: utf-8 -*-
"""重建 fusion-pixel-12px-subset.ttf 子集。

用法（在仓库任意位置）：
  python frontend\\assets\\subset_fonts.py

流程：
1. 收集 frontend/ 源码中所有字符串字面量（L"..." 与 "..."）的字符；
2. 收集 trainer/trainer.cpp 中 kCheats 的作弊显示名与热键标签字符；
3. 合并安全基本字符集，写出 frontend/assets/fusion-charset.txt（版本化，便于查看）；
4. 用 frontend/assets/fusion-pixel-12px-proportional-zh_hans.ttf（完整
   Fusion Pixel 12px 简体中文版，SIL OFL 1.1，见 LICENSE-OFL.txt）生成
   精简子集 frontend/assets/fusion-pixel-12px-subset.ttf；
5. 校验族名仍是 Fusion Pixel 12px Prop zh-Hans、字符集 100% 覆盖。

新增 UI 文案 / 作弊名 / “关于”正文后重跑一次即可；MyFontHeading.ttf（标题
专用设计字体）与 MyFontIcon.ttf（图标字体）不走本流程，需要新字形时直接在
设计端补字后替换该文件。
"""
import io
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
FRONTEND = os.path.join(ROOT, "frontend")
TRAINER = os.path.join(ROOT, "trainer")
RAW = os.path.join(HERE, "fusion-pixel-12px-proportional-zh_hans.ttf")
OUT = os.path.join(HERE, "fusion-pixel-12px-subset.ttf")
CHARSET_OUT = os.path.join(HERE, "fusion-charset.txt")

chars = set()

# 1) frontend 源码中的字符串字面量（L"..." 与 "..."）
for dirpath, dirs, files in os.walk(FRONTEND):
    dirs[:] = [d for d in dirs if d not in ("assets", "build")]
    for fn in files:
        if not fn.endswith((".cpp", ".h", ".rc")):
            continue
        path = os.path.join(dirpath, fn)
        with io.open(path, "r", encoding="utf-8") as f:
            src = f.read()
        for m in re.finditer(r'L?"((?:[^"\\\n]|\\.)*)"', src):
            lit = m.group(1)
            # 还原常见转义
            lit = lit.replace("\\n", "\n").replace("\\t", " ")
            lit = lit.replace('\\"', '"').replace("\\\\", "\\")
            chars.update(lit)

# 2) trainer.cpp 中 kCheats 的 title 字段（UTF-8 源，管道显示名）
with io.open(os.path.join(TRAINER, "trainer.cpp"), "r", encoding="utf-8") as f:
    tsrc = f.read()
start = tsrc.find("kCheats[]")
table = tsrc[start:tsrc.find("};", start)]
for m in re.finditer(r'\{\s*"[^"]*",\s*"([^"]+)"', table):
    chars.update(m.group(1))
# 3) HotkeyName 产生的热键标签（F1~F12 等）
chars.update("F1234567890")

# 4) 安全基本字符集：ASCII 可打印 + 常用全角标点/特殊符号
safety = (
    "0123456789"
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
    " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"
    "，。：；！？、（）【】《》……—·／＠　"
    "×+-"
)
chars.update(safety)
chars = {c for c in chars if c.isprintable() or c == "　"}

text = "".join(sorted(chars))
with io.open(CHARSET_OUT, "w", encoding="utf-8") as f:
    f.write(text)
print("charset size:", len(text))

# 5) pyftsubset 生成子集
cmd = [
    "python", "-m", "fontTools.subset",
    RAW,
    "--text-file=" + CHARSET_OUT,
    "--output-file=" + OUT,
    "--layout-features=*",
    "--glyph-names",
    "--symbol-cmap",
    "--legacy-cmap",
    "--notdef-glyph",
    "--recommended-glyphs",
]
subprocess.check_call(cmd)
print("wrote", OUT, os.path.getsize(OUT), "bytes")

# 6) 校验：族名 + 覆盖率
from fontTools.ttLib import TTFont
fresh = TTFont(OUT)
family = None
for rec in fresh["name"].names:
    if rec.nameID in (1, 16):
        family = rec.toUnicode()
        break
cmap = fresh.getBestCmap()
covered = sum(1 for c in text if ord(c) in cmap)
missing = [c for c in text if ord(c) not in cmap]
print("family name:", family)
print("coverage: %d/%d" % (covered, len(text)))
if missing:
    print("MISSING:", " ".join("U+%04X" % ord(c) for c in missing))
else:
    print("MISSING: none")
