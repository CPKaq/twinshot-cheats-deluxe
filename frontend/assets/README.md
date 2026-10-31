# 像素字体资源

当前使用的两份字体：

| 文件 | 字体族 | 用途 | 当前大小 |
|---|---|---|---|
| `zpix.ttf` | `Zpix`（官方 ZPix） | 12px 正文、小字 | 约 4.7 MB |
| `FFF Neostandard Bold Regular.ttf` | `FFF Neostandard Bold` | 8px 设计，按 16px 用于标题/大字 | 约 23 KB |

`frontend/frontend.rc` 以 `RCDATA` 嵌入 `frontend.exe`，运行时由 `AddFontMemResourceEx` 私有加载，不安装到系统。

旧的 `Zfull-GB.ttf`（多字重点阵合集）和 `IPix.ttf` 已不再被引用，可后续删除。

## 是否需要精简

**当前不需要；正式发布建议精简 `zpix.ttf`。**

- 当前 `frontend.exe` 约 5.5 MB，已经比之前两份完整字体（约 22 MB）小很多。
- `FFF Neostandard Bold` 只有 23 KB，基本没有精简价值。
- `Zpix` 的完整字符集约 4.7 MB；UI 实际只用到：
  - ASCII 数字/字母/标点；
  - 固定 UI 文案（“无伤”“锁血”“增加金币”“金币数量”“关于”“退出”等）；
  - 后端 `list` 返回的作弊名称；
  - 未来可能新增的作弊名称。
- 如果只嵌入当前 UI 和当前作弊名，`zpix.ttf` 子集可以缩到几百 KB 甚至更小。
- 但作弊名由 `trainer.dll` 的 `kCheats` 决定，未来加新作弊时可能出现缺字；建议构建时从 `trainer.cpp` / `cheats/*.md` / UI 源文件收集字符，或保留完整字体作为回退。

## 子集化方法（可选）

使用 `fonttools` 的 `pyftsubset`：

```bat
pip install fonttools

pyftsubset frontend\assets\zpix.ttf ^
  --text-file=build\ui-charset.txt ^
  --output-file=frontend\assets\zpix-subset.ttf ^
  --layout-features=* ^
  --glyph-names ^
  --symbol-cmap ^
  --legacy-cmap ^
  --notdef-glyph ^
  --recommended-glyphs
```

`FFF Neostandard Bold` 只有 23 KB，除非要极致压缩，否则不必子集化。

`build\ui-charset.txt` 至少包含：

```text
0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz
+-:：/·。，、！？（）[]【】…—
无伤锁血增加金币金币数量关于退出已连接正在初始化等待游戏注入
Insert Esc F3 F4 F5 Twin Shot Cheats Deluxe 重温你无敌的童年
```

实际字符集建议从源码/说明书自动收集，而不是手写。

然后把 `frontend.rc` 中的资源路径替换为子集文件：

```rc
IDR_FONT_ZPIX RCDATA "assets/zpix-subset.ttf"
```

## 注意事项

- 只嵌入 **TTF/OTF**；不要用 WOFF/WOFF2，`AddFontMemResourceEx` 不认。
- 子集化后确认字体族名仍是 `Zpix`，否则 `CreateFontW` 找不到。
- `Zpix` 按 12px 使用；`FFF Neostandard Bold` 按 16px 使用（8px 设计的 2x）。
- 使用 `NONANTIALIASED_QUALITY`、负像素高度和整数倍字号，才能保持像素边缘；不要用 ClearType/半透明缩放。
- 如果最终字体有许可证，分发时保留对应 license 文件。
- 替换字体后先运行 `frontend.exe --demo` 确认字体族名和原生像素尺寸。
