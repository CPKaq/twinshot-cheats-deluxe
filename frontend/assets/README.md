# 像素字体资源

当前使用的三份字体（均按需嵌入 exe）：

| 文件 | 字体族 | 用途 | 当前大小 | 许可证 |
|---|---|---|---|---|
| `fusion-pixel-12px-subset.ttf` | `Fusion Pixel 12px Prop zh-Hans`（[TakWolf/fusion-pixel-font](https://github.com/TakWolf/fusion-pixel-font) 的字符子集） | 12px 正文、小字 | 约 41 KB | SIL OFL 1.1（见 `LICENSE-OFL.txt`） |
| `MyFontHeading.ttf` | `MyFontHeading`（自制） | 原生 16px，主面板标题与“关于”页标题共用（含拉丁字母 + “关于”二字） | 约 7.6 KB | 未定（以仓库主许可证为准） |
| `MyFontIcon.ttf` | `MyFontIcon`（自制） | 作弊行名称前的图标字符，PUA `U+E001`(无伤)/`U+E002`(锁血)/`U+E003`(增加金币)，按 12px 渲染与正文等高 | 约 6 KB | 未定（以仓库主许可证为准） |

未参与构建、仅存档的文件：

| 文件 | 说明 |
|---|---|
| `fusion-pixel-12px-proportional-zh_hans.ttf` | 完整 Fusion Pixel 12px 简体中文版（约 7 MB），仅供重新子集化参考，不参与构建（该目录下被 `*.ttf` 忽略规则放行的只有上表中的三份待分发字体） |

`frontend/frontend.rc` 以 `RCDATA` 嵌入前三份到 `frontend.exe`（当前 exe 约 856 KB），运行时由 `AddFontMemResourceEx` 私有加载，不安装到系统。

> Fusion Pixel 字体采用 SIL OFL 1.1：随分发的字体（含子集）必须保留其版权声明与许可文本（`LICENSE-OFL.txt`），“Fusion Pixel” 为保留字（Reserved Font Name），衍生字重命名时不得占用该名称。字符子集属于衍生版本，分发仓库中时以上约定随文件一并满足。

## 子集覆盖维护

Fusion Pixel 子集只包含收集到的字符，因此**新增 UI 文案、新增作弊名、修改“关于”正文时都需要重新子集化**，否则会缺字。UI 实际用到的字符来自：

- frontend 源码中的所有字符串字面量（自动收集）；
- 后端 `list` 返回的作弊名称（由 `trainer.dll` 的 `kCheats` 决定，自动收集）；
- 固定的安全基本字符集（ASCII 可打印 + 常用全角标点）；
- 主面板 / “关于”页标题（`MyFontHeading.ttf` 为专用设计字体，不走子集化流程；扩展标题文字需在设计端补充字形——当前已含 A-Z / a-z / 空格 / “关于”）。

## 子集化方法

首选一键脚本（自动收集源码字符 → 写出 `fusion-charset.txt` → 生成 `fusion-pixel-12px-subset.ttf` → 校验族名与覆盖）：

```bat
pip install fonttools
python frontend\assets\subset_fonts.py
```

生成用的字符集版本化在 `frontend/assets/fusion-charset.txt`，等价的手动命令：

```bat
pyftsubset frontend\assets\fusion-pixel-12px-proportional-zh_hans.ttf ^
  --text-file=frontend\assets\fusion-charset.txt ^
  --output-file=frontend\assets\fusion-pixel-12px-subset.ttf ^
  --layout-features=* ^
  --glyph-names ^
  --symbol-cmap ^
  --legacy-cmap ^
  --notdef-glyph ^
  --recommended-glyphs
```

（`MyFontHeading.ttf` / `MyFontIcon.ttf` 为自制设计字体，不在此流程内：需要新字形时直接在设计端补字后替换对应文件，族名保持不变。）

脚本结束后先运行 `frontend.exe --demo` 确认字体族名和缺字情况；`frontend.rc` 中的资源路径无需改动（`fusion-pixel-12px-subset.ttf` 与 `MyFontHeading.ttf` 就是嵌入的文件）。

## 注意事项

- 只嵌入 **TTF/OTF**；不要用 WOFF/WOFF2，`AddFontMemResourceEx` 不认。
- 子集化后确认字体族名仍是 `Fusion Pixel 12px Prop zh-Hans` / `MyFontHeading` / `MyFontIcon`，否则 `CreateFontW` 找不到。
- `Fusion Pixel 12px` 按 12px 使用；标题 `MyFontHeading` 按 16px 使用（原生 16px 设计）；图标 `MyFontIcon` 按 12px 使用（字面网格与正文字一致）。
- 使用 `NONANTIALIASED_QUALITY`、负像素高度和整数倍字号，才能保持像素边缘；不要用 ClearType/半透明缩放。
- 分发时保留各字体的 license 文件（Fusion Pixel 为 `LICENSE-OFL.txt`）；上架 GNU GPLv3 后在仓库主 license 中注明字体文件的例外归属。
- 替换字体后先运行 `frontend.exe --demo` 确认字体族名和原生像素尺寸。
