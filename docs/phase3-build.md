# Phase 3 构建/使用说明:injector.exe + trainer.dll

## 全流程使用顺序

1. 启动 `Twin Shot Deluxe.exe`(或先运行 injector,它会等进程出现,最长 60 秒);
2. 管理员运行 `build\injector.exe`;
3. injector 自动把同目录的 `trainer.dll` 注入(两个文件都在 `build/`,保持放一起最省事);
4. trainer 初始化:等待 mono → 定位各作弊项(见 `docs/trainer-framework.md`)→ 应用;
5. 热键(每个作弊独立):**F3 = 无伤**(注入后默认开启);退出游戏/关闭补丁自动还原;
6. 进程内 UI:注入即弹窗(**Insert 显/隐**),勾选/按钮操作与热键等效;动作数值输入框可自定义金币步长;
7. 日志:`%TEMP%\twinshot_trainer.log`,或用 DebugView 看 `OutputDebugString`。

## 编译

> 先关闭游戏:运行中的 trainer.dll 被锁定,重新编译会报 `Permission denied`。

### MSVC(推荐, x64 开发者命令行)

```bat
cl /O2 /EHsc injector\injector.cpp /Fe:build\injector.exe shell32.lib
cl /LD /O2 /EHsc trainer\trainer.cpp /Fe:build\trainer.dll user32.lib
```

### MinGW-w64

```bat
g++ -O2 -std=c++17 -Wall injector\injector.cpp -o build\injector.exe -lshell32 -static -static-libstdc++ -static-libgcc
g++ -O2 -std=c++17 -Wall -shared trainer\trainer.cpp -o build\trainer.dll -luser32 -static -static-libgcc
```

> `-static` 让产物不依赖 MinGW 运行时 dll(libstdc++-6.dll / libgcc_s_seh-1.dll),
> 否则注入后游戏进程找不到这些 dll 会导致 `LoadLibraryW failed`。
> 两者任选其一;都不要用 x86 目标,游戏进程是 64 位,注入器位数必须一致(x64 编译)。
> 不需要任何第三方库,只用 WinAPI(shell32 的 CommandLineToArgvW 用于取宽字符参数)。
> 仓库根目录旧的 injector.exe / trainer.dll 是历史构建产物,可删除;以后一律用 `build/`。

## 排错速查

| 现象 | 原因/处理 |
|---|---|
| `undefined reference to 'WinMain'`(g++) | 代码入口用了 `wmain`(MSVC 扩展,g++ 不认)。当前版本已是标准 `main() + CommandLineToArgvW`;若自己改回 `wmain`,需加 `-municode` |
| injector 控制台输出中文乱码 | `wprintf` 里对宽字符串必须用 `%ls`(MinGW 按 C 标准 `%s` = 窄字符串),当前代码已全部使用 `%ls` |
| dll 被占用,编译报 `Permission denied` | 游戏(或上次注入)还开着,trainer.dll 在进程内被锁 → 先关闭游戏再编译 |
| **Defender 误报病毒/自动删除产物** | 注入 API(`CreateRemoteThread` 等)+ 无签名触发的启发式误报(检测名常带 `!ml`)。处理:把**本项目文件夹**加入 Defender 排除项(GUI 或管理员 PowerShell:`Add-MpPreference -ExclusionPath '<项目目录>'`),再从"保护历史记录"还原被隔离文件;可向微软提交误报申诉 https://www.microsoft.com/wdsi/filesubmission 。**不要**用 UPX 加壳(加重误报),排除范围也别扩大到整个磁盘 |
| `OpenProcess failed` | 权限不足 → 管理员运行 injector;或游戏是安全模式?正常单机无 |
| `LoadLibraryW failed (exit 0)` | dll 缺依赖或位数不符:`dumpbin /dependents trainer.dll` 检查;确认 x64;MinGW 产物要加 `-static`,否则缺 libstdc++-6.dll / libgcc_s_seh-1.dll |
| 日志出现 `missing mono export: xxx` | Unity 定制 Mono 精简了部分导出符号 → 记下函数名,改用短模式匹配 AOB 扫描方案(后续里程碑;把该导出名发我) |
| 日志出现 `not found Player` | Assembly-CSharp 尚未(或不应加载?)→ 重试窗口 60 秒,进一次主菜单后再试 |
| 锁血 `resolve failed` + field dump | dnSpy 里看到的单例(`X.Instance`)很可能是**属性**而非字段 → 看 dump 里 static 字段/数据来源类,修正数据链(参考 `cheats/02-锁血.md` 的 Game.instance→level 案例) |
| 补丁无效果 / 挨打仍掉血 | F3 确认状态;看日志里 `entry` 与 `orig bytes`,和 Phase 2 看 CE 里的原始字节比对 |
| 水流/击退等机制变化 | 理论上不会(Phase 2 已验证整体 return false 无副作用);若出现立即关闭补丁并回填说明书 |

## 与 Phase 2 手动流程的对应关系

| CE 手动(Phase 2) | trainer 自动(Phase 3) |
|---|---|
| Mono Dissect 按类名找方法 | `mono_class_from_name` + 遍历 `mono_class_get_methods` 按名字/签名匹配 |
| 让游戏先调用一次,拿 JIT 地址 | `mono_compile_method` 主动编译(已实测可行) |
| Auto Assemble 写 `31 C0 C3` | `VirtualProtect` + `memcpy` + `FlushInstructionCache` |
| 记录原字节 / [DISABLE] 还原 | 内存中缓存 `g_orig[3]`,F3 或退出时写回 |

## 后续里程碑(按优先级)

1. **扩展作弊项(当前进行中)**:锁血、金币等,直接按 `docs/trainer-framework.md` 契约往 `kCheats` 表加条目;数值类作弊需先解决"活对象实例发现"(以 `cheats/02-锁血.md` 的静态分析结论为准);
2. **更安全的定位与容错**:仅当 Unity 精简了某导出符号时才需要(目前 15 个核心导出全部可用),做 AOB 短模式扫描兜底;
3. **配置化**:作弊项默认开关/热键抽到配置文件(JSON),非必需,先跑通功能再说;
4. **UI 反馈**:作弊开关时给游戏窗口提示(可选,例如 overlay 文本);
5. 反射调用静态方法类作弊(为"一击必杀"等需要调用游戏函数的效果做准备)。
