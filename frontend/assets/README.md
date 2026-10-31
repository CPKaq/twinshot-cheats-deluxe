# 像素字体资源

当前使用的三份字体（均为按需字符子集）：

| 文件 | 字体族 | 用途 | 当前大小 |
|---|---|---|---|
| `zpix.ttf` | `Zpix`（官方 ZPix 子集） | 12px 正文、小字 | 约 12 KB |
| `FFF Neostandard Bold Regular.ttf` | `FFF Neostandard Bold` | 8px 设计，按 16px 用于主面板标题/大字 | 约 23 KB |
| `ipix.ttf` | `IPix` | 原生 16px 中文，仅用于“关于”页大标题（FFF 无中文字形） | 约 1.3 KB |
| `zpix_raw.ttf` | `Zpix`（完整字符集） | 原始完整字体，仅供重新子集化参考，不参与构建 | 约 4.7 MB |

`frontend/frontend.rc` 以 `RCDATA` 嵌入前三份到 `frontend.exe`（当前 exe 约 840 KB），运行时由 `AddFontMemResourceEx` 私有加载，不安装到系统。

## 子集覆盖维护

子集只包含收集到的字符，因此**新增 UI 文案、新增作弊名、修改“关于”标题文字时都需要重新子集化**，否则会缺字。UI 实际用到的字符来自：

- ASCII 数字/字母/标点；
- 固定 UI 文案（“无伤”“锁血”“增加金币”“金币数量”“关于”“退出”等）；
- 后端 `list` 返回的作弊名称（由 `trainer.dll` 的 `kCheats` 决定）；
- “关于”页标题（当前为“关于”二字，在 `ipix.ttf` 中）。

## 子集化方法（可选）

使用 `fonttools` 的 `pyftsubset`：

```bat
pip install fonttools

pyftsubset frontend\assets\zpix_raw.ttf ^
  --text-file=build\ui-charset.txt ^
  --output-file=frontend\assets\zpix.ttf ^
  --layout-features=* ^
  --glyph-names ^
  --symbol-cmap ^
  --legacy-cmap ^
  --notdef-glyph ^
  --recommended-glyphs
```

（`ipix.ttf` 同理：从完整版 IPix 生成子集并输出为 `frontend\assets\ipix.ttf`；族名保持 `IPix`。）

`FFF Neostandard Bold` 只有 23 KB，除非要极致压缩，否则不必子集化。

`build\ui-charset.txt` 至少包含：

```text
0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz
+-:：/·。，、！？（）[]【】…—
无伤锁血增加金币金币数量关于退出已连接正在初始化等待游戏注入
Insert Esc F3 F4 F5 Twin Shot Cheats Deluxe 重温你无敌的童年
```

实际字符集建议从源码/说明书自动收集，而不是手写。

生成后先运行 `frontend.exe --demo` 确认字体族名和缺字情况；`frontend.rc` 中的资源路径无需改动（`zpix.ttf` / `ipix.ttf` 就是嵌入的子集文件）。

## 注意事项

- 只嵌入 **TTF/OTF**；不要用 WOFF/WOFF2，`AddFontMemResourceEx` 不认。
- 子集化后确认字体族名仍是 `Zpix` / `IPix`，否则 `CreateFontW` 找不到。
- `Zpix` 按 12px 使用；主面板标题 `FFF Neostandard Bold` 按 16px 使用（8px 设计的 2x）；“关于”页标题 `IPix` 为原生 16px。
- 使用 `NONANTIALIASED_QUALITY`、负像素高度和整数倍字号，才能保持像素边缘；不要用 ClearType/半透明缩放。
- 如果最终字体有许可证，分发时保留对应 license 文件。
- 替换字体后先运行 `frontend.exe --demo` 确认字体族名和原生像素尺寸。
