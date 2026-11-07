# frontend —— Twin Shot Cheats Deluxe 外置前端（方案 A）

纯 C++ / Win32 / GDI 的独立控制窗口。它不注入游戏、不读取游戏内存、不调用 Mono，只通过命名管道 `\\.\pipe\twinshot-trainer` 与 `trainer.dll` 通信。

当前是**架构与布局骨架 + 占位皮肤**：像素字体已接入（Fusion Pixel 12px + 自制的 MyFontHeading / MyFontIcon），图标和九宫格素材仍待替换。视觉稿确定后，主要改 `ui.cpp` / `ui_about.cpp` 的绘制和 `frontend.rc` 资源。

窗口为**无边框置顶窗口**：没有系统标题栏，拖动顶部标题区域可移动；Insert 隐藏/显示，面板底部“退出”按钮关闭程序。主窗口默认位于**主显示器左上角 (200,100)**，“关于”窗口在此基础**偏移 (100,100)**。“关于”窗口是**主面板同款的无边框像素窗口**：单行标题区可拖动，右上角手绘像素 ×（Esc 也可关闭），主窗口隐藏时一起隐藏；当前内容为占位文本（正式的开发者列表/外部资源说明及图标待补充）。

## 文件

| 文件 | 作用 |
|---|---|
| `main.cpp` | `WinMain`、DPI、单实例、消息循环 |
| `app.h/.cpp` | 应用状态、管道事件、焦点、窗口、命令 |
| `ui.h/.cpp` | 布局计算、命中测试、GDI 自绘（主面板） |
| `ui_about.h/.cpp` | “关于”窗口：布局/命中/像素风绘制（`ui_about.cpp` 内容占位） |
| `ui_common.h` | 两窗口共用的像素配色与 GDI 原语（`Fill`/`Frame`/`Text`） |
| `pipe_client.h/.cpp` | 命名管道客户端线程 |
| `focus.h/.cpp` | 找游戏窗口、激活前端、恢复游戏焦点 |
| `frontend.rc` / `frontend.manifest` | 字体、DPI manifest 等资源 |
| `resource.h` | 字体资源 ID |
| `build.bat` | MinGW-w64（MSYS2 UCRT64）构建脚本 |

## 构建

在仓库根目录运行：

```bat
frontend\build.bat
```

产物：`build\frontend.exe`。

手动命令（MinGW-w64）：

```bat
windres -I frontend frontend\frontend.rc -O coff -o build\frontend.res
g++ -std=c++17 -O2 -Wall -Wextra -Wno-cast-function-type -mwindows -DUNICODE -D_UNICODE ^
    frontend\main.cpp frontend\app.cpp frontend\ui.cpp frontend\ui_about.cpp ^
    frontend\pipe_client.cpp frontend\focus.cpp build\frontend.res ^
    -o build\frontend.exe -luser32 -lgdi32 -lshell32 ^
    -static -static-libgcc -static-libstdc++
```

> 前端与游戏/注入器都必须是 x64。

## 字体资源

`frontend/assets` 中的三份字体已通过 `frontend.rc` 嵌入 exe（构建时使用的子集文件；开源分发相关许可见下）：

```rc
#include "resource.h"

IDR_FONT_BODY RCDATA "assets/fusion-pixel-12px-subset.ttf"
IDR_FONT_TITLE RCDATA "assets/MyFontHeading.ttf"
IDR_FONT_ICON RCDATA "assets/MyFontIcon.ttf"

1 24 "frontend.manifest"
```

`app.cpp` 启动时用 `AddFontMemResourceEx` 私有加载：

- `fusion-pixel-12px-subset.ttf` → 字体族 `Fusion Pixel 12px Prop zh-Hans`（TakWolf 的 Fusion Pixel 字体，SIL OFL 1.1，许可见 `assets/LICENSE-OFL.txt`），用于 12px 正文/小字（字符子集约 41KB，字符集见 `assets/fusion-charset.txt`）；
- `MyFontHeading.ttf` → 字体族 `MyFontHeading`，原生 16px，主面板标题（"Twin Shot Cheats Deluxe"）与“关于”页大标题共用（标题组件合一，不再分两套）；
- `MyFontIcon.ttf` → 字体族 `MyFontIcon`，作弊行图标字符 `U+E001`(god/无伤)、`U+E002`(lock/锁血)、`U+E003`(coins/增加金币)，未注册的 id 不绘制图标；
- `CreateFontW` 使用负像素高度、`NONANTIALIASED_QUALITY`，只按 1x/2x/3x 整数缩放；
- 任一字体加载失败时回退到 `Microsoft YaHei UI`，并用 ClearType 保证可读性（图标字体失败则不绘制图标）。

> 字体是按字符子集化的：新增 UI 文案或修改标题文字后重跑 `python frontend\assets\subset_fonts.py`（自动从源码 UI 字符串与作弊名收集字符并重建 `fusion-pixel-12px-subset.ttf`；MyFontHeading/MyFontIcon 需在设计端补字形）即可，当前 exe 约 856KB。

## 运行

正常模式：

```bat
build\frontend.exe
```

它会自动等待并连接命名管道；游戏未启动或未注入时显示“等待游戏 / 等待注入”。

演示模式（不连接管道，用内置示例数据检查布局）：

```bat
build\frontend.exe --demo
```

## 注入流程

前端启动后默认不自动注入：

1. 显示“等待 trainer 列表…”和“注入”按钮；
2. 玩家先启动游戏（或先点注入，injector 会自己等待游戏最多约 60 秒）；
3. 点击“注入”后通过 `ShellExecuteExW(..., "runas")` 启动同目录 `injector.exe`；
4. 注入期间按钮消失，状态文字显示“正在注入…”；
5. 成功后管道连接，列表出现；
6. 失败或取消 UAC 后按钮重新出现，状态文字说明原因，可再次点击“注入”。

按钮文字始终是“注入”，不改成“注入中/重试”；状态完全由按钮是否显示和文字表示。

> 普通权限运行时点击注入会弹 UAC；如果希望完全无提示，需要以管理员权限运行前端。

## 当前交互

| 操作 | 行为 |
|---|---|
| Insert | 隐藏/显示；显示时激活并暂停游戏 |
| 拖动顶部标题区域 | 移动无边框窗口 |
| 未连接时点击“注入” | 启动同目录 `injector.exe`（会触发管理员 UAC）；注入中按钮消失，状态文字提示进度 |
| 点击开关/执行 | 通过管道发送 `set` / `act` |
| 点击金币数量 | 编辑“金币数量”；Enter 提交；`-` / `+` 每次调整 100；Esc 取消编辑 |
| 点击“关于” | 打开同款像素风“关于”窗口（占位内容：开发者/外部资源）；右上角 × 或 Esc 关闭 |
| 面板“退出” | 隐藏窗口、恢复游戏焦点并退出前端；DLL 的 F3/F4/F5 仍有效 |

## 当前未完成 / 后续

- 图标、九宫格、最终配色（等素材）；
- 字体子集化（当前 exe 约 5.5MB；如需进一步减小可精简）；
- 字体在 2x/3x 下的精确像素双倍放大验证；
- 作弊项超过一屏时的滚动；
- 更完整的键盘焦点/无障碍；
- 与真实游戏联调后的焦点恢复、暂停状态、金币执行验证。

协议与设计细节见 `docs/architecture-frontend-standalone.md`，B1 清理清单见该文档 §12。
