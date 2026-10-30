# Twin Shot Deluxe Cheat Trainer

对 Nitrome《Twin Shot Deluxe》(Steam 版,Mono 后端 Unity 游戏)的运行时修改器项目。
不做游戏文件修改(不换 dll),类似 Cheat Engine 的思路,向运行中的进程注入 trainer 实现锁血、无伤等效果。

## 阶段路线(Phase 0~3)

| 阶段 | 内容 | 状态 |
|---|---|---|
| Phase 0 | 侦察:确认位数、Unity/Mono 版本、Managed 目录清单、目标作弊列表 | ✅(x64 / Mono 后端确认) |
| Phase 1 | 静态分析:dnSpy 读 Assembly-CSharp.dll,定位血量字段与受伤方法 | ✅(见 `cheats/01-无伤.md`) |
| Phase 2 | 手动验证:Cheat Engine 复刻无敌效果 → 无伤 patch 验证通过、无副作用 | ✅ |
| Phase 3 | 自动化:injector.exe(C++ 注入)+ trainer.dll(mono API 写字段/patch 方法) | ✅ 三个作弊项落地并实测:**01-无伤(F3)/ 02-锁血(F4)/ 03-金币+100(F5)**;后续按 `cheats/NN-*.md` 迭代 |

## 目录结构

```
docs/     分析指南与产出笔记(static-analysis-guide.md = Phase 1 操作指南)
          前后端对接协议(docs/architecture-frontend.md,外部管道前端)
          外置前端(方案 A)设计(docs/architecture-frontend-standalone.md,正式 UI)
          进程内 UI 设计(docs/architecture-inprocess-ui.md,B1 过渡形态 + TrainerApi 契约)
cheats/   每条作弊的"说明书"(以 TEMPLATE.md 为模板)
injector/ C++ 注入器(Phase 3)
trainer/  C++ trainer DLL 核心(Phase 3;含管道命令服务;进程内 UI 为过渡,B1 清理见外置前端设计文档)
tools/    Python 辅助脚本(pymem 原型 / 地址验证,Phase 2 备用)
```

## 目标作弊(初版清单,Phase 0 里再增删)

- 消除怪物伤害(无伤)— ✅ `cheats/01-无伤.md`(F3,patch 方法)
- 锁定血量/生命数 — ✅ `cheats/02-锁血.md`(F4,写字段快照)
- 金币/免费购买 — ✅ `cheats/03-修改金币.md`(F5 +100,反射调用);免费购买(03b)候选
- 一击必杀
- 免费解锁内置作弊商店 — 候选 `04`(利用 `SetHasPurchasedCheat`,见 03 说明书 §5)

## 约定

1. 一切作弊先过 Phase 1 静态分析 → Phase 2 CE 验证 → Phase 3 才写代码。
2. 说明书模板:`cheats/TEMPLATE.md`;基础结构(玩家类、伤害流)集中在 `cheats/00-玩家与伤害系统.md`;trainer 侧添加作弊的契约见 `docs/trainer-framework.md`。
3. 定位一律用**类名/字段名**(经 mono API),不硬编码内存偏移,以抗游戏小版本更新。
4. 开发机需把本项目目录加入 Defender 排除项(注入器会被启发式误报),详见 `docs/phase3-build.md` 排错表。
