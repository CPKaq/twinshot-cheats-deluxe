# 外置前端(方案 A)设计与实施计划

> 状态:**设计定稿;前端骨架已实现(占位皮肤,待像素素材)**。
> 目标产物:`build\frontend.exe`(x64,纯 Win32/GDI,零第三方依赖)。
> 适用场景:作为正式控制窗口,通过命名管道控制已注入的 `trainer.dll`,替代当前进程内 B1 UI。
>
> 相关文档:
> - 管道协议:`docs/architecture-frontend.md`(协议 v1,本文不改变其语义)
> - 过渡方案 B1:`docs/architecture-inprocess-ui.md`（已清理，仅历史记录）
> - 作弊框架:`docs/trainer-framework.md`
> - 构建排错:`docs/phase3-build.md`

---

## 1. 目标与非目标

### 1.1 目标

1. **独立进程**:`frontend.exe` 不注入、不读游戏内存、不调用 Mono,只连接命名管道。
2. **纯 Win32 + GDI**:使用标准窗口/控件打底,自绘像素皮肤;不引入 Qt/ImGui/Electron/WebView2。
3. **像素风格**:自定义像素字体、图标、九宫格边框;整数缩放,避免 ClearType/半透明缩放导致发虚。
4. **Insert 显隐**:显示时激活前端窗口,游戏因失焦暂停;隐藏时把焦点还给游戏,游戏恢复。
5. **热键与 UI 并存**:F3/F4/F5 仍由 DLL 的 Worker 线程处理,面板是否激活、是否隐藏都不影响热键。
6. **状态驱动**:列表、开关、ready、动作完成/错误全部以管道事件和 `list` 为准;UI 不硬编码作弊项。
7. **可恢复**:前端可先于游戏启动;游戏重启、前端崩溃、管道断开后能重连并恢复显示。

### 1.2 非目标(本版不做)

- 不做独占全屏上的游戏内 overlay;游戏建议使用窗口化/无边框窗口化。
- 不做 `WS_EX_NOACTIVATE` / 点击穿透模式;本方案采用"面板激活 = 游戏暂停"的模式 1。
- 不在前端内做注入;注入仍由 `injector.exe` 完成,后续 M3 launcher 再考虑一键启动。
- 不做多客户端、热键重绑定、复杂参数编辑器、配置持久化(可作为后续增量)。
- 正式 UI 验收后,不再维护 DLL 内的 B1 UI。

---

## 2. 已确认的交互决策

| 项 | 决策 |
|---|---|
| 窗口形态 | 无边框(`WS_POPUP`)置顶窗口，`WS_EX_TOPMOST | WS_EX_TOOLWINDOW`；顶部标题区域可拖动 |
| 标题 | `Twin Shot Cheats Deluxe` |
| 显示 | `ShowWindow(SW_SHOW)` + 置顶 + 激活前端窗口;游戏暂停 |
| 隐藏 | `ShowWindow(SW_HIDE)` + 激活游戏窗口;游戏恢复 |
| Insert | 前端负责;`RegisterHotKey(MOD_NOREPEAT)` 优先,失败则定时轮询 |
| F3/F4/F5 | DLL 负责;前端不重复注册 |
| 退出 | 无系统标题栏/关闭按钮；面板底部“退出”按钮关闭程序；退出前隐藏并恢复游戏 |
| 面板可见但游戏被点回前台 | v1 不自动隐藏；Insert 严格切换显示/隐藏 |
| 激活失败 | 面板仍置顶显示,状态栏提示"点击面板以暂停游戏";不阻塞游戏 |
| B1 | 已清理；Insert 只由前端响应 |

> 注:游戏"失焦暂停、重新激活恢复"是当前实测行为,方案依赖该行为提供"操作面板时游戏暂停"的体验;仍需在窗口化/无边框/独占全屏下复测。

---

## 3. 进程、线程与职责

```text
frontend.exe                              Twin Shot Deluxe.exe
┌──────────────────────────┐              ┌──────────────────────────────────┐
│ UI 线程                  │              │ Unity / Mono + Assembly-CSharp   │
│  · 窗口/控件/像素绘制    │              │                                  │
│  · 状态镜像/输入/托盘    │              │  trainer.dll                     │
│  · Insert 显隐/焦点切换  │              │   ├─ Worker 线程(唯一碰 mono)   │
│                          │              │   │    · 定位/apply/tick/热键    │
│ 管道线程                 │  命名管道    │   │    · 消费命令队列            │
│  · 连接/重连             │◄────────────►│   └─ 管道线程                    │
│  · JSON Lines 收发       │ JSON/UTF-8   │        · list/set/act 入队       │
│  · PostMessage 给 UI     │              │        · state/done 事件回发      │
└──────────────────────────┘              └──────────────────────────────────┘
          ▲
          │ injector.exe 一次性注入 trainer.dll,然后退出
          └──────────────────────────────────────────────────────────────────
```

### 3.1 线程契约

| 线程 | 允许做 | 禁止做 |
|---|---|---|
| 前端 UI 线程 | 创建/操作窗口、绘制、读取本地状态、发命令到出站队列 | 阻塞式管道 I/O、等待 `list`、直接调用任何 DLL 内部函数 |
| 前端管道线程 | 连接/重连、读写管道、拆行/解析 JSON、`PostMessage` 通知 UI | 直接操作 HWND/控件、直接改 UI 状态 |
| DLL Worker 线程 | Mono API、patch、字段读写、热键、消费命令 | 被 UI 线程阻塞 |
| DLL 管道线程 | 收命令入队、发事件 | 直接调用 Mono |

**规则:前端 UI 线程永远不阻塞。** 管道连接失败、`list` 延迟、游戏未启动,都通过状态栏/定时器表现,不卡消息循环。

---

## 4. 窗口与视觉

### 4.1 窗口

- 无边框顶层窗口，标题 `Twin Shot Cheats Deluxe`；
- `WS_POPUP` + `WS_EX_TOPMOST | WS_EX_TOOLWINDOW`（不占任务栏/Alt+Tab）；
- 顶部标题区域自绘，支持鼠标拖动移动窗口；没有系统标题栏/最小化/关闭按钮；
- 主窗口默认位于主显示器左上角 (200,100)（客户区）；“关于”窗口在其基础上偏移 (100,100)，避免与主面板重叠；
- 面板底部“退出”按钮 = 隐藏、恢复游戏、退出前端；
- Insert 只负责隐藏/显示；Esc 仅取消数值编辑，不再隐藏（“关于”窗口内 Esc 关闭该窗口）；
- 最小尺寸由设计稿决定，保证最长中文名 + 热键 + 控件不挤压；
- 记住上次位置（可选），恢复时做屏幕边界校正，避免出现在屏幕外；
- 置顶在显示时设置；不在游戏运行时高频抢 z-order。

### 4.2 布局

建议结构(具体像素尺寸以现有视觉稿为准):

```text
┌─────────────────────────────────────┐
│  Twin Shot Cheats Deluxe            │  ← 自绘标题，可拖动
│  重温你无敌的童年                    │  ← slogan
├─────────────────────────────────────┤
│  ● 已连接 / 正在初始化…             │  ← 状态栏:连接、ready、暂停提示
│                                     │
│  无伤        [F3]        [ 开关 ]   │  ← 开关型
│  锁血        [F4]        [ 开关 ]   │
│  增加金币    [F5]   [100] [ 执行 ]  │  ← 动作型 + 数值
│                                     │
│  ┌───────────────────────────────┐  │
│  │ 提示/错误/动作结果 toast       │  │
│  └───────────────────────────────┘  │
│  游戏已暂停，按 Insert 返回游戏      │  ← 激活面板时显示
│               [ 关于 ] [ 退出 ]     │
└─────────────────────────────────────┘
```

- 作弊行由 `list` 动态生成,不硬编码 `god/lock/coins`;
- `type=="toggle"`:复选框/开关 + 热键标签;
- `type=="action"`:执行按钮 + 共享“金币数量”输入框;
- `ready==false`:控件禁用/降透明度,状态提示"等待游戏加载/未定位";
- 行数超过可视区域时使用滚动(可后置);当前 3 项可先不做;
- “金币数量”用自绘输入框;输入 `<=0` 按 100 兜底;
- 底部左侧显示 `Insert 隐藏 · 热键后台有效`;右侧为“关于”“退出”按钮;
- 未连接时列表区域显示“等待 trainer 列表…”+“请先启动游戏，再点击下方‘注入’”+`[注入]`按钮;
- 注入中按钮消失,文字显示“正在注入…（injector 最多等待约 60 秒）”;失败/取消 UAC 后按钮重新出现,文字说明原因;按钮文字始终为“注入”;
- “关于”窗口为主面板同款的无边框像素窗口:头部为单行标题区（可拖动）,标题用主面板同款的 `MyFontHeading`（标题字体组件已合一）,右上角手绘像素 × 或 Esc 关闭;内容为数据块(`AboutBlock`),含开发者列表与字体致谢;
- 连接状态、初始化中、游戏退出、管道断开、动作成败都要有明确文字,不只靠颜色。

### 4.3 像素化技术约束

- **字体**:三份字体以 `RCDATA` 嵌入 exe,`AddFontMemResourceEx` 私有加载,失败回退系统字体;`fusion-pixel-12px-subset.ttf`（字体族 `Fusion Pixel 12px Prop zh-Hans`，12px 正文/小字，字符子集约 41KB，SIL OFL 1.1 见 `frontend/assets/LICENSE-OFL.txt`）、`MyFontHeading.ttf`（字体族 `MyFontHeading`，原生 16px，主面板与“关于”标题共用，自制）、`MyFontIcon.ttf`（字体族 `MyFontIcon`，按 12px 渲染的图标字符 `U+E001-E003`，仅在加载成功时绘制，自制）;完整原始 Fusion Pixel 保留在 `frontend/assets/fusion-pixel-12px-proportional-zh_hans.ttf` 供重新子集化（一键脚本 `python frontend/assets/subset_fonts.py`，字符集版本化于 `fusion-charset.txt`）;
- **字号**:只用字体原生像素尺寸或整数倍;`CreateFontW` 使用负高度、`NONANTIALIASED_QUALITY`,关闭 ClearType;当前 1x/2x/3x 对应 12/24/36px 与 16/32/48px;
- **精简**:`fusion-pixel-12px-subset.ttf` 为按 UI 文案子集化后的精简版（约 41KB），新增文案后用 `python frontend/assets/subset_fonts.py` 重建，详见 `frontend/assets/README.md`;
- **DPI**:manifest 声明 Per-Monitor V2;处理 `WM_DPICHANGED`;按设计稿定义的 1x/2x 级别缩放,不做小数倍拉伸;
- **绘制**:GDI 双缓冲(`CreateCompatibleDC` + `CreateCompatibleBitmap` + `BitBlt`),`WM_ERASEBKGND` 返回 1 防闪烁;
- **图标**:优先 16×16 / 24×24 的 32 位 BMP/ICO + `AlphaBlend`;PNG 需要 GDI+/WIC,非必要不引入;
- **控件**:按钮/复选框用 `BS_OWNERDRAW` + `WM_DRAWITEM`;标签用自绘 `STATIC` 或 `WM_CTLCOLORSTATIC`;编辑框保留标准 `EDIT` 以免费获得输入/光标/选择;
- **窗口背景/边框**:九宫格贴图或纯色+像素描边;不使用模糊阴影、圆角抗锯齿;
- **调色板**:以现有像素设计稿为准;建议从游戏截图取色,保持深底、米白字、橙黄强调。

### 4.4 像素清晰度与素材格式

- 客户区绘制全部使用整数物理像素；`FillRect` / `FrameRect` / `BitBlt` 不做插值，1x 下 1px 线就是 1px；
- 边框宽度随整数倍 `s`（1x/2x/3x）缩放：1x 为 1px，2x 为 2px；如果某条线希望永远 1px，就固定传 `1`，不要用小数；
- 不做半像素坐标、`StretchBlt` 小数拉伸、GDI+ 变换或分层半透明缩放，否则会模糊；
- 当前为无边框窗口，客户区自绘标题/外框；DWM 仍可能提供窗口阴影或圆角（取决于系统），不属于像素客户区；
- 文字使用像素字体 + `NONANTIALIASED_QUALITY`；字体加载失败回退系统字体时才使用 ClearType；
- 素材格式：
  - 分割线/开关/简单图标：优先直接用 GDI 整数图元绘制，不需要图片；
  - 位图图标：16×16 / 24×24 的 32 位 BMP（`AlphaBlend`）或 PNG（需 GDI+/WIC），硬边缘、不要抗锯齿；
  - 九宫格：BMP/PNG，按整数倍绘制九块，不拉伸角块；
  - 字体：TTF/OTF，`RCDATA` 嵌入 + `AddFontMemResourceEx`；
  - 窗口图标：ICO。

---

## 5. 前端状态模型

UI 线程持有唯一状态,管道线程只投递事件。

```text
FrontendState
├─ connection: Disconnected | Connecting | Connected | Initializing | Error
├─ cheats: vector<CheatView>
│   ├─ id / name / type / on / ready / hotkey
│   ├─ pending: 是否有未确认的命令
│   └─ pendingSince / pendingDesiredOn
├─ toast: 最近一条动作/错误提示 + 过期时间
├─ panelVisible / panelActive
├─ gameHwnd / gameRunning
└─ lastListAt / lastPongAt
```

### 5.1 事实源

- **`list` 是最终基线**:连接后、每 500ms、重连后都以 `list` 校准;
- **`state`/`done` 是即时事件**:收到后立即更新对应项/toast;
- **不要乐观永久改状态**:点击开关时只标记 `pending`,收到 `state` 后落定;
- `state ok:false` 时清除 pending,显示 `err` 对应提示;
- `done` 用于动作成功/失败提示;金币动作可显示"已执行 +N";
- 热键引起的状态变化也会推 `state`;前端不需要区分来源。

### 5.2 pending 与超时

- 点击开关/动作后,控件进入 pending(可显示"执行中"或禁用重复点击);
- 正常应在 1~2 个管道周期内收到事件;超过 2s 未收到:
  - 清除 pending;
  - 发送一次 `list` 校准;
  - 若仍无响应,标记连接异常/提示重连。
- 当前后端命令/事件队列各 16 深度,满了会静默丢弃;前端必须做防抖,不要连续高速发命令。

### 5.3 初始化与列表延迟

当前后端实现中,`list` 由 Worker 线程消费;Worker 在启动阶段要先完成作弊定位(最长约 60s),因此前端连上后可能只收到 `pong`,暂时收不到 `list`。

处理方式:

- 连接成功但未收到首个 `list` 时,状态显示"正在初始化/定位作弊项…";
- `ping` 可用来确认管道仍活着,但不能单独证明后端可用;
- 首个 `list` 超过合理时间(如 65s)仍未来,显示"trainer 未就绪,请查看日志";
- 可选优化:让 DLL 管道线程读取一份只读作弊快照直接生成 `list` 应答,绕过 Worker 定位阶段;若实施,需把状态字段读写纳入同一临界区。

### 5.4 重连

- 管道断开:不清空最后一份列表,但全部置为不可操作,状态"连接断开";
- 重连成功:发送 `ping`/`list`,清空 pending,丢弃旧事件影响;
- 游戏重启:旧 DLL 随进程消失;前端保持运行,等待新 DLL 的管道;
- 重连退避:500ms~1s,避免空转;
- 单实例:命名互斥体防止多个前端;第二个实例启动时激活已有窗口或提示。

---

## 6. 管道客户端设计

### 6.1 连接

- 路径:`\\.\pipe\twinshot-trainer`;
- `CreateFileW(... OPEN_EXISTING ...)`;失败时:
  - `ERROR_PIPE_BUSY` → `WaitNamedPipeW` + 重试;
  - `ERROR_FILE_NOT_FOUND` → 游戏未启动/未注入,继续等待;
  - `ERROR_ACCESS_DENIED` → 权限/完整性级别问题,提示"请以相同权限运行前端/游戏";
- 连接成功发送 `{"cmd":"ping"}` 和 `{"cmd":"list"}`;
- 单客户端:第二个前端连接会失败,需单实例保护。

### 6.2 I/O 线程

推荐一个专用管道线程,循环执行:

1. 刷新出站命令队列(小 JSON 行,`WriteFile`);
2. `PeekNamedPipe` 查询可读字节;
3. 有数据则 `ReadFile`,追加到行缓冲;
4. 按 `\n` 拆行,解析 JSON,`PostMessage` 给 UI;
5. `Sleep(20~50ms)`,兼顾延迟与 CPU;
6. 出错/断开 → 关闭句柄,通知 UI,退避重连。

要点:

- **不要用阻塞 `ReadFile` 占住线程导致无法写命令**;用 `PeekNamedPipe` 轮询(与后端管道线程同思路)或 overlapped I/O;
- 出站队列用临界区/互斥保护;UI 线程只入队,不直接 `WriteFile`;
- 行缓冲设上限(如 8KB),超限丢弃并记录日志,防止异常对端撑爆内存;
- UTF-8 解码用 `MultiByteToWideChar(CP_UTF8, ...)`;中文名必须正确显示;
- `PostMessage` 携带堆事件指针时,窗口销毁/消息失败要负责释放;
- UI 线程只处理消息和重绘,不做网络/管道等待。

### 6.3 协议使用

命令/事件语义以 `docs/architecture-frontend.md` 为准,前端只使用:

| 方向 | 消息 | 用途 |
|---|---|---|
| → | `ping` | 探活 |
| → | `list` | 拉取基线 |
| → | `set {id,on}` | 开关型 |
| → | `act {id,amount}` | 动作型 |
| ← | `pong {proto}` | 探活应答/版本 |
| ← | `list {cheats[]}` | 完整基线 |
| ← | `state {id,on,ok,err?}` | 开关结果/热键变化 |
| ← | `done {id,ok,err?}` | 动作完成 |

- 不硬编码作弊列表;新增作弊项由后端 `list` 自动出现;
- `amount` 已由当前后端支持;`<=0` 时后端保留当前值,前端按 100 兜底;
- 协议只增字段、不改语义;前端遇到未知字段/事件应忽略而不是崩溃。

### 6.4 轮询策略

- `ping`:连接期间每 1s 一次,用于区分"管道活着但 Worker 未就绪";
- `list`:连接后立即一次,之后每 500ms 一次;有 pending 时可提前拉一次;
- 不做高频轮询(避免占满 16 深度队列);
- 收到 `state/done` 后不需要立即再 `list`,除非 pending 超时或状态不一致。

### 6.5 手动注入

前端不自动判断游戏是否启动，也不自动注入。未连接时在空列表区域显示“注入”按钮：

- 点击后用 `ShellExecuteExW` + `runas` 启动同目录 `injector.exe`；
- 注入期间按钮消失，状态文字显示“正在注入…”；
- 注入器退出后等待管道最多 5 秒；
- 成功：按钮保持隐藏，列表出现；
- 失败/UAC 取消/超时：按钮重新出现，文字显示原因；
- 按钮文字始终为“注入”，不改变文字；
- 同一时刻只允许一个 injector 进程；
- 普通权限运行会触发 UAC；以管理员运行前端可避免额外提示。

> 当前 `injector.exe` 自身最多等待游戏约 60 秒，因此 UI 会提示“最多等待约 60 秒”。

---

## 7. 显隐、焦点与游戏窗口

### 7.1 状态机

v1 **不自动隐藏**；Insert 严格切换显示/隐藏：

```text
[GAME] 隐藏，游戏前台
   │ Insert
   ▼
[TRAINER] 可见且前台，游戏暂停
   │ Insert
   ▼
[GAME] 隐藏 + 恢复游戏焦点
```

- `GAME → TRAINER`：显示面板、置顶、强制激活；游戏失焦暂停；
- `TRAINER → GAME`：隐藏面板，把焦点还给游戏；游戏恢复；
- 面板可见但非前台（用户点了游戏）时，按 Insert 仍然直接隐藏，不要求先激活；
- 不在 v1 做“游戏获得焦点时自动隐藏”（原因见 §7.6）；
- 无边框窗口通过拖动顶部标题区域移动；没有系统 X；面板底部“退出”按钮关闭程序并恢复游戏；
- Esc 仅取消数值编辑，不改变窗口显隐。

### 7.2 显示并激活前端

```text
ShowWindow(panel, SW_SHOW)
SetWindowPos(panel, HWND_TOPMOST, ..., SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW)
SetForegroundWindow(panel)
  ├─ 成功 → 状态 TRAINER
  └─ 失败 → AttachThreadInput 兜底再试
        ├─ 成功 → 状态 TRAINER
        └─ 失败 → 面板保持置顶,状态提示"点击面板以暂停游戏"
```

注意:

- 外部进程调用 `SetForegroundWindow` 可能被 Windows 前台锁拒绝;`AttachThreadInput` 是必要兜底,但不要在成功后继续 attach;
- 激活后检查 `GetForegroundWindow()==panel`,不要假设成功;
- 失败时不要隐藏面板,也不要假装游戏已暂停;让用户点击面板完成激活;
- 若游戏以管理员运行而前端不是,前台激活/管道连接都可能失败;此时建议同权限运行,或在文档中提示。

### 7.3 隐藏并恢复游戏

```text
ShowWindow(panel, SW_HIDE)
FindGameWindow()  // 缓存/重新枚举
if (!gameHwnd) return
if (IsIconic(gameHwnd)) ShowWindow(gameHwnd, SW_RESTORE)
SetForegroundWindow(gameHwnd)
```

- **隐藏不等于恢复游戏焦点**;必须显式激活游戏,否则游戏可能一直暂停;
- 游戏窗口句柄在启动、重连、游戏重启后重新获取;
- 找不到游戏窗口时只隐藏面板,不激活其他窗口;
- 游戏已退出时,隐藏面板即可,状态显示"等待游戏"。

### 7.4 游戏窗口发现

- 用 Toolhelp 按进程名 `Twin Shot Deluxe.exe` 找 PID;
- `EnumWindows` + `GetWindowThreadProcessId` 匹配 PID;
- 选择可见、标题非空的顶层窗口作为主窗口;
- 校验 PID,避免激活同名/其他进程窗口;
- `IsWindow` 失效、管道断开或游戏重启时重新查找;
- 可选:由 DLL 在协议里上报游戏 HWND,减少前端枚举;当前不强制。

### 7.5 Insert 热键

- 优先 `RegisterHotKey(hwnd, ID_INSERT, MOD_NOREPEAT, VK_INSERT)`;
- 隐藏窗口仍能收到 `WM_HOTKEY`,适合显隐;
- 注册失败(例如游戏占用 Insert)→ 回退 UI 线程 100ms 轮询 `GetAsyncKeyState`,做边沿检测;
- 前端只注册 Insert;F3/F4/F5 继续由 DLL 处理,避免双重触发;
- B1 已从 DLL 移除；Insert 仅由前端处理，不再有双响应。

### 7.6 自动隐藏（v1 不做，后续可选）

- v1 **不自动隐藏**：面板可见但非前台时，按 Insert 仍然直接隐藏；
- 原因：外部进程激活可能失败；若游戏仍然是前台，自动隐藏会让刚显示的面板立刻消失，且难以判断是“用户点了游戏”还是“激活失败”；
- Insert 本身就是严格的显示/隐藏切换，逻辑最直接；
- 后续若实测需要，可用 `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` 或 100ms 定时器监听前台变化，并加状态守卫：**仅当面板曾经成功激活、且前台切到游戏窗口时才隐藏**，绝不能在“显示并激活失败”的路径上触发。

---

## 8. 生命周期与异常

### 8.1 启动顺序

支持两种顺序:

1. 游戏 → injector → frontend:前端连接后立即 `list`;
2. frontend → 游戏 → injector:前端显示"等待游戏/等待注入",管道出现后自动连接。

M3 可选 launcher:前端/独立 launcher 检测游戏、提权运行 injector、再启动前端;本版不做。

### 8.2 游戏退出/重启

- 管道断开 → 状态"游戏已退出/连接断开",控件禁用,保留最后列表;
- 前端继续运行,后台重试连接;
- 游戏重启并重新注入后,自动连接、`list` 刷新;
- 若游戏窗口句柄变化,重新查找。

### 8.3 DLL/后端异常

- 未注入:无管道,显示等待;
- Mono 初始化失败:当前实现 Worker 退出但管道可能仍在,`ping` 有回应、`list` 无响应;前端必须用 `list` 超时判断,不能只看 `ping`;
- 定位失败/未 ready:`list` 中 `ready:false`,控件禁用,显示"未就绪/等待游戏加载";
- 命令失败:`state/done` 的 `err` 显示为中文提示,原始错误可放 tooltip/日志。

### 8.4 前端退出

- 无系统标题栏，因此没有 X/最小化按钮；面板底部“退出” = 隐藏、恢复游戏、结束前端进程；
- Alt+F4 仍会走 `WM_CLOSE`，当前按“退出”处理；
- 退出后 DLL 的热键和已开启作弊仍然有效；Insert 不再可用，重新打开前端即可；
- 退出前尽量等待出站队列清空（短超时），避免最后一条命令丢失。

### 8.5 日志

- 前端日志建议写 `%TEMP%\twinshot_frontend.log`(或 exe 同目录,视权限);
- 记录:管道连接/断开、游戏 HWND、激活成功/失败、命令/事件、异常;
- 调试构建可打印详细事件;发布构建保留连接/焦点/错误级别。

---

## 9. 文件、构建与部署

### 9.1 当前实现目录

```text
frontend/
  main.cpp              // WinMain、单实例、消息循环
  app.h / app.cpp       // 应用状态、命令分发、定时器、焦点
  ui.h / ui.cpp         // 布局计算、命中测试、GDI 自绘（主面板，占位皮肤）
  ui_about.h / .cpp     // “关于”窗口：布局、命中测试、像素风绘制（内容占位）
  ui_common.h           // 两窗口共用的像素配色与 GDI 原语（Fill/Frame/Text）
  pipe_client.h / .cpp  // 管道线程、JSON Lines 收发
  focus.h / focus.cpp   // 游戏窗口发现、ForceForeground/RestoreGame
  frontend.rc / frontend.manifest
  build.bat             // MinGW-w64 构建脚本
  README.md
```

后续像素素材落地时可拆出 `skin.cpp` / `widgets.cpp` / `assets/`，不影响现有模块边界。

### 9.2 构建

当前 MinGW-w64（MSYS2 UCRT64）可直接运行：

```bat
frontend\build.bat
```

产物：`build\frontend.exe`。等价命令见 `frontend/README.md`。

- x64;与游戏/injector 位数一致;
- 纯 Win32/GDI;静态链接 CRT/MinGW 运行时,避免额外 dll;
- 入口用标准 `WinMain`，避免 MinGW 的 `wWinMain/-municode` 坑;
- 资源用 `rc`(MSVC)或 `windres`(MinGW)编译;
- 链接库当前为 `user32`、`gdi32`、`shell32`;不引入第三方库;
- 支持 `frontend.exe --demo`，不连接管道，用内置三条示例数据显示布局。

### 9.3 部署

最终三件套:

```text
build\
  injector.exe
  trainer.dll
  frontend.exe
```

- 注入仍需要管理员;前端通常不需要;
- 若游戏以管理员运行,前端可能需要同权限,或调整 DLL 管道安全描述符;
- 前端不修改游戏文件;不写入游戏目录(日志除外)。

---

## 10. 实施顺序

1. **骨架**:`frontend.exe` 窗口 + 像素字体/皮肤 + 静态假数据;不接管道也能看布局;
2. **管道**:连接/重连、`list/set/act`、`state/done`、状态栏、toast;
3. **焦点**:游戏窗口发现、显示激活、隐藏恢复、Insert 显隐、无边框标题拖动;
4. **完善**:未就绪/错误/初始化提示、单实例、托盘、窗口位置、日志;
5. **验收**:按 §11 测试;通过后按 §12 清理 B1,更新构建/文档。

每一步都可独立验收,避免 UI 和管道/焦点问题混在一起排查。

### 10.1 当前落地情况

- [x] 骨架：Win32 窗口、静态布局、占位 GDI 绘制、`--demo`；
- [x] 像素字体资源：Fusion Pixel 12px（OFL，正文字符子集）+ 自制 MyFontHeading（16px 标题）以 RCDATA 嵌入并私有加载；无边框窗口；
- [x] 管道：连接/重连、`list/set/act`、`state/done`、状态栏、toast、pending 超时；
- [x] 焦点：游戏窗口发现、显示激活、隐藏恢复、Insert 显隐、无边框标题拖动；Esc 仅取消数值编辑；
- [x] 基础交互：开关、动作、金币数量、无边框拖动、关于/退出按钮、单实例；
- [x] 手动注入：未连接时“注入”按钮启动同目录 injector.exe（UAC），注入中隐藏按钮，失败/超时恢复按钮；
- [x] “关于”窗口：主面板同款无边框像素窗口（占位内容已改为正式文案），×/Esc 关闭，主窗口隐藏时一起隐藏；主窗口默认位于主显示器左上角 (200,100)，“关于”偏移 (100,100)；
- [x] 作弊行图标：名称前 16px 图标位，MyFontIcon PUA 字符（`U+E001/E002/E003` ↔ `god/lock/coins`）；`kCheats` 按 F3-F5 顺序排列；
- [ ] 图标/九宫格/最终配色；
- [ ] 字体子集字符覆盖维护（三份字体均为按需嵌入，exe 约 856KB；新增文案后重跑 `python frontend/assets/subset_fonts.py` 重建 `fusion-pixel-12px-subset.ttf`，`MyFontHeading`/`MyFontIcon` 需设计端补字形；Fusion Pixel 分发须保留 `LICENSE-OFL.txt`）；
- [ ] 托盘、滚动、完整无障碍；
- [ ] 与真实游戏联调 §11 中的焦点/暂停/金币项。

当前皮肤是**占位实现**（纯色块 + 像素描边），像素字体已接入；视觉稿确定后主要改 `frontend/ui.cpp`、`frontend/ui_about.cpp` 和资源文件。

已用本地 mock 命名管道验证：`list` 解析、开关 `set/state`、动作 `act/done`、断线重连、`--demo` 布局和基本点击；真实游戏联调项仍见 §11。

---

## 11. 待测试项 / 观察项

> 以下项在实现阶段逐条验证;结果回填本文或 `docs/phase3-build.md`。

### 11.1 焦点与窗口

- [ ] 外部进程 `SetForegroundWindow` 在游戏前台时是否成功;
- [ ] `AttachThreadInput` 兜底是否成功,是否造成输入卡顿/副作用;
- [ ] 游戏以管理员运行时,非管理员前端能否连接管道/激活窗口;
- [ ] 显示面板后游戏是否稳定暂停,隐藏后是否稳定恢复;
- [ ] 隐藏时游戏窗口最小化/全屏切换是否正常,有无闪屏;
- [ ] 窗口化 / 无边框 / 独占全屏三种模式下的置顶与激活表现;
- [ ] 面板可见时点击游戏（面板变为非前台），按 Insert 是否直接隐藏并恢复游戏;
- [ ] 激活失败时面板是否保持可见，且不会被误自动隐藏;
- [ ] Esc 是否只取消数值编辑、不隐藏窗口;
- [ ] 无边框窗口顶部拖动的边界、双击、多显示器表现;
- [ ] “关于”按钮能打开像素风关于窗口（占位内容），主窗口隐藏时一起隐藏；× 或 Esc 均可关闭;
- [ ] 多显示器、125%/150% DPI 下窗口位置与像素字体清晰度;
- [ ] Insert 长按/连按是否只切换一次(`MOD_NOREPEAT`/边沿检测);
- [ ] Insert 与游戏按键冲突时的回退/提示。

### 11.2 热键与作弊

- [ ] 面板激活、游戏暂停时 F3/F4/F5 是否仍然有效;
- [ ] 暂停状态下“增加金币”动作的 `mono_runtime_invoke` 是否卡死/异常,恢复后数值是否正确;
- [ ] 暂停状态下锁血 tick 是否持续写回,恢复后是否正常;
- [ ] UI 点击与热键同时操作同一作弊时,状态是否最终一致;
- [ ] `ready:false` 时控件禁用,热键被忽略的提示是否清楚。

### 11.3 管道与状态

- [ ] 前端先启动、后注入时能否自动连接;
- [ ] 游戏重启/前端重启后能否重连并恢复列表;
- [ ] 首次 `list` 被定位阶段延迟时,初始化提示是否正确;
- [ ] `set/act` 失败(`not ready`/`unknown id`/类型错误)能否显示错误;
- [ ] 动作成功 `done` 是否有 toast,金币数值提示是否正确;
- [ ] 快速连点/队列满时是否丢命令,防抖是否有效;
- [ ] 断线期间残留事件在重连后是否会造成错误状态,`list` 是否能纠正;
- [ ] 第二个前端实例是否被单实例保护拦截。

### 11.4 手动注入

- [ ] 未连接时“注入”按钮出现，连接/初始化后消失;
- [ ] 点击注入触发 UAC，拒绝后按钮恢复并显示取消原因;
- [ ] 游戏未启动时 injector 等待 60 秒期间 UI 不卡死，按钮隐藏;
- [ ] 游戏稍后启动时 injector 能完成注入，前端自动连接;
- [ ] injector 不存在/退出码非 0 时按钮恢复并显示原因;
- [ ] 注入成功但管道 5 秒内未连接时显示失败并允许重试;
- [ ] 注入中再次点击不会启动第二个 injector。

### 11.5 B1 清理回归

> B1 已于 2026-10-31 清理；以下保留为历史回归项。

- [ ] 移除 B1 UI 线程后 DLL 仍能正常注入/定位/patch;
- [ ] 无 B1 时 Insert 只由前端响应,不出现双触发;
- [ ] 移除 `gdi32` 后构建通过,DLL 不缺少依赖;
- [ ] 外部前端完成所有原 B1 功能:开关、动作、数值、ready、热键状态回显。

---

## 12. B1 清理记录（已执行）

> `trainer/trainer.cpp` 的 B1 UI、`TrainerApi` 与 `trainer/trainer_api.h` 已于 2026-10-31 删除；`trainer.dll` 构建不再链接 `gdi32`。本节由原始的分符号清理清单精简而来，详细的符号级对照见该日期的 git 历史；回归项见 §11.5。

- 独立 UI 相关符号（`UiWndProc`、`UiBuildControls`、`UiThreadEntry`、`g_uiCtrl/g_uiMap/g_uiCount`、`UI_*` 常量）随 B1 窗口一并删除；`DllMain` 不再创建 UI 线程或保存 `g_hSelf`；
- `TrainerApi_GetSnapshot` / `TrainerApi_Set` / `TrainerApi_Act` 与 `trainer_api.h` 已删除；外置前端只通过管道 `list/set/act`；
- 确认保留：`HotkeyName`（管道 `list` 与日志使用）、`g_ipcCs` 与命令/事件队列、`Worker`/`ProcessIpcCommands`（作弊执行核心）、`MessageBoxA`（Mono 初始化失败提示）；
- 文档同步：`docs/phase3-build.md` 去掉 `gdi32`；根 `README.md` 加入 `frontend/` 说明；`docs/architecture-inprocess-ui.md` 标注为历史方案。

---

## 13. 开放问题

1. ~~自动隐藏是否默认开启，还是保留“面板可见但游戏运行”作为 HUD 模式？~~ **已决策：v1 不自动隐藏，Insert 严格切换显示/隐藏；自动隐藏作为后续可选增量。**
2. ~~关闭按钮 X = 隐藏，退出放托盘；若不做托盘，是否在面板内加“退出”按钮？~~ **已决策：无边框窗口，无系统 X；面板底部“退出”按钮关闭程序。**
3. 像素视觉稿的最终尺寸、字体、图标、配色资源清单;
4. 是否需要在面板上显示日志尾部/最近错误,还是只显示 toast?
5. 是否在 M3 把 injector/前端合并为 launcher;
6. 游戏以管理员运行时的权限策略(前端同权限 vs 修改管道安全描述符);
7. 是否将 `list` 改为管道线程直接快照应答,以消除启动期列表延迟。
