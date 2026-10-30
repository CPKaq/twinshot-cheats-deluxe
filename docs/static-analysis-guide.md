# Twin Shot Deluxe 静态分析指南(Phase 1)

> 目标:用 dnSpy(ILSpy)读懂游戏的 C# 代码,找出实现"锁血 / 消除怪物伤害"所需的**类、字段、方法**,
> 并记录到 `cheats/` 目录的说明书中,作为 Phase 2(Cheat Engine 验证)和 Phase 3(C++ trainer)的规格依据。
>
> 游戏是 **Mono 后端** Unity 游戏,所有 C# 代码都在 Managed 目录的 dll 里,可以完整反编译阅读。

---

## 0. 定位文件

在游戏目录(Steam 默认 `steamapps/common/Twin Shot Deluxe/`)下:

```
Twin Shot Deluxe.exe                    ← 主程序(进程名,Che 搜索时用;带空格)
MonoBleedingEdge/                       ← Mono 运行时(MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll 记下版本号)
Twin Shot Deluxe_Data/
  ├─ Managed/
  │    ├─ Assembly-CSharp.dll           ← 游戏自己的代码:95% 的内容在这里
  │    ├─ Assembly-CSharp-firstpass.dll ← 第三方插件/早期编译代码:找不到时来这里搜
  │    ├─ UnityEngine.*.dll
  │    └─ mono-2.0-bdwgc.dll ←(如有)
  └─ globalgamemanagers                 ← 里面藏着 Unity 引擎版本号(记下来,写进说明书)
```

**必查**:`Managed/` 下除了 `Assembly-CSharp.dll` 还有哪些可疑 dll(如 `Assembly-CSharp-firstpass.dll`),一个一个用 dnSpy 打开翻命名空间。

---

## 1. dnSpy 基本操作

- 下载 **dnSpy**(便携版 zip,解压即用,不需要 .NET 环境)。
- 把 `Assembly-CSharp.dll` 拖进 dnSpy 左侧窗口。
- 左侧 Assembly Explorer:点开程序集 → 命名空间 → 类,双击方法即可看到反编译后的 **C# 源码**(通常接近原文)。
- **右键类/方法 → 分析(Analyze)**:可以列出"谁引用了它 / 它引用了谁",这是追调用链的核心功能。
- 顶部搜索框:输入类/方法/字段名,回车会在所有已打开程序集中搜索。
- `Ctrl+F` 在当前反编译窗口内搜文本;在类中搜字段名很常用。

> 小技巧:遇到可疑类不要慌——Nitrome 的代码通常不做混淆,类名/字段名基本都是可读的。

---

## 2. 搜索策略:按线索找"血量"和"伤害"

游戏由 Flash 原版(AS3)移植,类名很可能沿用原版词汇。按下面顺序搜索,每找到一处,
就把它的"引用树"(存取它的地方)顺着读一遍,直到确定受伤入口。

### 线索清单(逐个在搜索框/`Ctrl+F` 里试)

| 主题 | 搜索关键词(不含引号) |
|---|---|
| 血量 | `health` `hp` `lives` `heart` `soul` `healthBar` |
| 受伤 | `damage` `takeDamage` `hurt` `hit` `injure` `dead` `die` `kill` |
| 无敌 | `invincible` `invulnerable` `god` `iframe` `invincibility` |
| 购买/金币(以后可用) | `coin` `money` `gold` `shop` `buy` |
| 敌人 | `enemy` `monster` `creature` `monsterSpawner`(这类名字能顺藤摸瓜找到怪物类) |
| 玩家 | `player` `PlayerCreature` `PlayerController` `Hero` `Cat` |
| 调试残留 | `cheat` `debug` `godMode`(很多 Flash 移植会保留作弊标记,运气好直接白送锁血字段) |

### 逐个类去读代码,重点确认三件事

1. **玩家血量字段** → 字段名 + 所在类 + 类型(int/float/bool?是单值还是数组?合作模式是 List<Player> 吗?)
2. **受伤方法** → 玩家(或基类)里形如 `Damage(int amount)` / `TakeDamage(...)` / `Hurt(...)` 的方法。
3. **调用来源** → 用 dnSpy"分析"窗口看 `Damage()` 被谁调用:
   - 若所有怪物都调用玩家的同一个方法 → **patch 这一个方法 = 全清**,最理想。
   - 若伤害在别处(碰撞处理器、掉落物)等统一定值 → 那里下手更好。

### 常见"看不懂"时的追法

- 血量字段是 `int health` 但从未被直接改?→ 用**分析**看 `health` 被谁赋值,顺着赋值处往上读调用链。
- 代码里大量用接口/事件?→ 搜 `OnDamage` / `OnDeath` / `HitListener` 这类订阅者。
- 数值不大但就是找不到?→ 也可能是属性/封装,看反编译时编译器生成的字段名(如 `<health>k__BackingField`)。

---

## 3. 记录规范(边找边写,不要等)

每确认一个发现,立即在 `cheats/` 下开一份说明书(复制模板 `TEMPLATE.md`),内容必须可复现:

- **类、字段、方法的完整签名**(从 dnSpy 右键"复制"或手写,含命名空间和参数类型);
- **所在程序集文件名**(Assembly-CSharp 还是 firstpass);
- 这段代码**做了什么**(用 2~3 句话描述,附上关键代码片段摘录,行号也要);
- 该发现对哪条作弊有效(锁血 / 无伤 / 秒杀 / 金币…)、打算在 Phase 3 里用写字段还是 patch 方法实现;
- 待验证问题(Phase 2 用 CE 验证时回答)。

> 血量这类一问一答的"基础设施"建议单独开一份 `cheats/00-玩家与伤害系统.md`,
> 把玩家类继承结构、伤害流注释下来,之后所有作弊说明书都引用它。

---

## 4. 与 Cheat Engine 交叉验证(Phase 2 预告)

静态分析得到的结论要能落到内存地址才能用。流程:

1. 启动游戏 → CE 附加进程(Twin Shot Deluxe.exe)。
2. CE 菜单 **Mono → Activate Mono Features**,激活成功即可。
3. 再进 **Mono → Mono Dissect**,在左树里搜索刚才在 dnSpy 记下的类名。
4. 确认 dnSpy 里看到的字段/方法 == CE 里看到的字段/方法 → 在 CE 里找到血量实例字段地址 → Freeze。
5. 若行为符合预期 → 该作弊项在说明书上标 "CE 验证通过",并抄下"地址是怎么定位的"(类名/字段名即定位方式)。

---

## 5. 本阶段的产出与完成标准

- [ ] Managed 目录清单、Unity 版本号、mono 版本号已记录
- [ ] `cheats/00-玩家与伤害系统.md`:玩家类继承树 + 血量字段 + `Damage` 方法 + 调用链,完整
- [ ] 每条目标作弊(锁血、无伤、秒杀、金币…)各有一份说明书,或至少标注"未开始"
- [ ] 全部字段/方法签名已抄录,Phase 3 写代码时**不需要再打开 dnSpy 查**

完成后我们进入 Phase 2(CE 手动验证)或 Phase 3(C++ trainer 骨架)。
