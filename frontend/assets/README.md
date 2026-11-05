# 像素字体资源

当前使用的四份字体（均按需嵌入，完整原始 Zpix 见 `zpix_raw.ttf`）：

| 文件 | 字体族 | 用途 | 当前大小 |
|---|---|---|---|
| `zpix.ttf` | `Zpix`（官方 ZPix 子集） | 12px 正文、小字 | 约 37 KB |
| `FFF Neostandard Bold Regular.ttf` | `FFF Neostandard Bold` | 8px 设计，按 16px 用于主面板标题/大字 | 约 23 KB |
| `MyFontHeading.ttf` | `MyFontHeading` | 原生 16px 中文，仅用于“关于”页大标题（FFF 无中文字形；目前专含“关于”二字） | 约 6 KB |
| `MyFontIcon.ttf` | `MyFontIcon` | 作弊行名称前的图标字符，PUA `U+E001`(无伤)/`U+E002`(锁血)/`U+E003`(增加金币)，按 22px 渲染与 Zpix 正文等高 | 约 6 KB |
| `zpix_raw.ttf` | `Zpix`（完整字符集） | 原始完整字体，仅供重新子集化参考，不参与构建 | 约 4.7 MB |

`frontend/frontend.rc` 以 `RCDATA` 嵌入前四份到 `frontend.exe`（当前 exe 约 895 KB），运行时由 `AddFontMemResourceEx` 私有加载，不安装到系统。

## 子集覆盖维护

子集只包含收集到的字符，因此**新增 UI 文案、新增作弊名、修改“关于”标题文字时都需要重新子集化**，否则会缺字。UI 实际用到的字符来自：

- frontend 源码中的所有字符串字面量（自动收集）；
- 后端 `list` 返回的作弊名称（由 `trainer.dll` 的 `kCheats` 决定，自动收集）；
- 固定的安全基本字符集（ASCII 可打印 + 常用全角标点）；
- “关于”页标题（当前为“关于”二字，在 `MyFontHeading.ttf` 中；该字体为专用设计字体，扩展标题文字需在设计端补充字形）。

## 子集化方法

首选一键脚本（自动收集源码字符 → 写出 `zpix-charset.txt` → 生成 `zpix.ttf` → 校验族名与覆盖）：

```bat
pip install fonttools
python frontend\assets\subset_zpix.py
```

生成用的字符集版本化在 `frontend/assets/zpix-charset.txt`，等价的手动命令：

```bat
pyftsubset frontend\assets\zpix_raw.ttf ^
  --text-file=frontend\assets\zpix-charset.txt ^
  --output-file=frontend\assets\zpix.ttf ^
  --layout-features=* ^
  --glyph-names ^
  --symbol-cmap ^
  --legacy-cmap ^
  --notdef-glyph ^
  --recommended-glyphs
```

（`MyFontHeading.ttf` 为专用设计字体，不在此流程内：标题文字需要新字形时直接在设计端补字后替换该文件，族名保持 `MyFontHeading`。）

`FFF Neostandard Bold` 只有 23 KB，除非要极致压缩，否则不必子集化。

脚本结束后先运行 `frontend.exe --demo` 确认字体族名和缺字情况；`frontend.rc` 中的资源路径无需改动（`zpix.ttf` 与 `MyFontHeading.ttf` 就是嵌入的文件）。

## 注意事项

- 只嵌入 **TTF/OTF**；不要用 WOFF/WOFF2，`AddFontMemResourceEx` 不认。
- 子集化后确认字体族名仍是 `Zpix` / `MyFontHeading`，否则 `CreateFontW` 找不到。
- `Zpix` 按 12px 使用；主面板标题 `FFF Neostandard Bold` 按 16px 使用（8px 设计的 2x）；“关于”页标题 `MyFontHeading` 为原生 16px。
- 使用 `NONANTIALIASED_QUALITY`、负像素高度和整数倍字号，才能保持像素边缘；不要用 ClearType/半透明缩放。
- 如果最终字体有许可证，分发时保留对应 license 文件。
- 替换字体后先运行 `frontend.exe --demo` 确认字体族名和原生像素尺寸。
