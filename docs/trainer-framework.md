# Trainer 框架:如何添加一个新作弊项

> 代码位置:`trainer/trainer.cpp`。作弊项框架把"定位 → 开启 → 关闭 → 周期维护"四件事抽象出来,
> 添加新作弊 = 写 3~4 个函数 + 注册表里加一行,不碰框架主体。

## 框架流程(注入后自动执行)

```
等 mono-2.0-bdwgc.dll → 绑定导出 → attach 线程
        │
        ▼
找 Assembly-CSharp image(重试)
        │
        ▼
对 kCheats 里每个作弊循环调用 locate(),直到全部 ready(60s 预算)
        │
        ▼
defaultOn 的作弊自动 apply + ON
        │
        ▼
热键循环(每 30ms):每个作弊独立开关;开启期间调用 tick()(如有)
        │
        ▼
线程退出:所有开启中的作弊 revert()
```

## `Cheat` 结构契约

```cpp
struct Cheat {
    const char*   title;      // 日志用名字
    int           hotkey;     // VK_*,如 VK_F4
    bool          defaultOn;  // 注入成功后默认开启?
    CheatLocateFn locate;     // bool(Cheat&):定位目标;**可被反复调用**直到返回 true
    CheatApplyFn  apply;      // 开启
    CheatRevertFn revert;     // 关闭(必须能安全还原)
    CheatTickFn   tick;       // 每约 30ms 调一次(开启期间);不需要则 nullptr
    void*         ctx;        // 该作弊私有状态(通常一个全局静态 struct)
    bool ready = false;       // 框架维护:locate 成功后置位
    bool on    = false;       // 框架维护:当前开关状态
};
```

三条硬性契约:

1. **locate 幂等且不炸**:游戏还没加载到相应内容时返回 `false` 即可,框架会重试;locate 内部只做"查找 + 准备",**不要有副作用**(写内存放在 apply)。
2. **apply/revert 快速、无阻塞、幂等**:只做内存写/字段写;框架负责跟踪 `on`,apply/revert 内部不要自己维护开关状态(BytePatch 的 `active` 防重入是现成范例)。
3. **所有 mono 调用都发生在 trainer 线程**(attach 过的),**不要**在新线程里直接调 mono API;tick 也在该线程执行,所以锁字段天然安全。

第四种形态:**动作型**(`action` 槽非空)。热键一次性触发 `action`,没有开/关状态(`on`/`tick`/`defaultOn` 均忽略),适合"加金币 +100"这类一次性操作。方法调用型作弊(`mono_runtime_invoke`)通常配这个形态。

## 现成工具函数

| 函数 | 用途 |
|---|---|
| `FindClass(ns, name)` | 按"命名空间.类名"找类(ns 为 `""` 表示全局命名空间) |
| `FindMethod(ns, klass, name, paramCount, typeContains)` | 按名字找方法;`paramCount<0` 不限参数数;`typeContains` 要求任一参数类型名含该串(区分重载,如 `"Vector2"` vs `"Arrow"`) |
| `PreparePatch(&patch, method, code, len)` | 对方法主动 JIT,记录原字节,准备 patch 内容(如 `31 C0 C3`) |
| `ApplyPatch(&patch, on)` | 写入/还原 patch(带防重入) |
| `g_api.mono_class_get_field_from_name` | 找实例/静态字段(数值类作弊用) |
| `g_api.mono_field_get_value / mono_field_set_value` | 读写实例字段 |
| `g_api.mono_class_vtable + mono_vtable_get_static_field_data` | 读写**静态**字段(如 `Level.Instance` 这类) |

## 示例:现成的"无伤"(方法 patch 型)

```cpp
struct GodCtx { BytePatch patch; };
static GodCtx g_god;

static bool God_Locate(Cheat&) {
    MonoMethod* m = FindMethod("", "Player", "Hit", 1, "Vector2");
    if (!m) return false;                                   // 游戏还没加载,等下一轮重试
    static const uint8_t kRetFalse[3] = {0x31, 0xC0, 0xC3};
    return PreparePatch(&g_god.patch, m, kRetFalse, 3);
}
static void God_Apply (Cheat&) { ApplyPatch(&g_god.patch, true);  }
static void God_Revert(Cheat&) { ApplyPatch(&g_god.patch, false); }

// 注册表加一行:
// { "无伤", VK_F3, true, God_Locate, God_Apply, God_Revert, nullptr, &g_god },
```

## 数值类(写字段)作弊的典型写法(如"锁血")

```cpp
struct LockCtx { MonoClass* cls; MonoClassField* hp; ... };
static LockCtx g_lock;

static bool Lock_Locate(Cheat&) {        // 只查"类 + 字段",不碰内存
    g_lock.cls = FindClass("", "Player");
    if (!g_lock.cls) return false;
    g_lock.hp = g_api.mono_class_get_field_from_name(g_lock.cls, "<血量字段名>");
    return g_lock.hp != nullptr;
}
static void Lock_Tick(Cheat&) {          // 开启期间持续把血写回上限
    // 遍历玩家实例(实例发现方式见 02-锁血.md 的结论),对每个:
    // g_api.mono_field_set_value(obj, g_lock.hp, &maxHp);
}
static void Lock_Apply (Cheat&) { /* 置位内部标志,tick 接管 */ }
static void Lock_Revert(Cheat&) { /* 清标志,恢复自然扣血 */ }
```

> "活对象实例怎么拿"是数值类作弊的关键难点(mono 没有遍历实例的现成 API,常用 hook 构造函数记录 / GC 堆扫描),结论以 `cheats/02-锁血.md` 的静态分析为准,实现时再定。

## 测试/发布习惯

- 编译产物统一放 `build/`;**替换 trainer.dll 前先关闭游戏**(运行中的 dll 文件被锁,编译会报 Permission denied);
- 改完代码 → 关游戏 → 重新编译 → 重开游戏 → 注入,看日志里每个作弊的 `ready / ON / OFF`。
