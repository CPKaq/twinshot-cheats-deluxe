# 前端对接架构:Twin Shot Trainer 前后端协议(M1)

> 给前端开发者的对接文档。**前端不需要知道注入 / Mono / 内存 patch 的任何细节**:
> 它只是"连接一根命名管道,收发 JSON 行"的纯客户端。
>
> 当前版本:协议 v1(M1)。后端:`trainer/trainer.cpp`;产物:`build/trainer.dll`。
>
> **注**:正式 UI 采用**外置前端(方案 A)**,设计与清理清单见
> `docs/architecture-frontend-standalone.md`——UI 独立进程,通过本管道控制 `trainer.dll`。
> B1(进程内 UI,见 `docs/architecture-inprocess-ui.md`)为过渡/回退形态,正式 UI 验收后按该文档 §12 清理。

## 1. 总体架构与职责划分

```
┌────────────────────────┐
│ 前端(另一开发者)      │  任意语言:C#/WPF、Python、Electron…
│ 纯管道客户端,无注入职责│
└──────────┬─────────────┘
           │ 命名管道 \\.\pipe\twinshot-trainer(JSON Lines,UTF-8)
           ▼
┌────────────────────────┐     ┌──────────────────────────────┐
│ trainer.dll(后端)     │────▶│ 游戏进程 Twin Shot Deluxe.exe│
│ · 管道服务线程         │     │ (注入由 injector.exe 负责)  │
│   只收命令/发事件      │     └──────────────────────────────┘
│ · trainer 线程         │
│   唯一执行 mono 调用   │
│ · 热键逻辑(与 UI 并存)│
└────────────────────────┘
```

- **后端职责**:注入后的 mono 绑定、作弊定位/执行、热键、管道协议;
- **前端职责**:连接管道、渲染作弊列表与状态、发送 set/act 命令、接收事件;
- **注入职责**:`injector.exe`(后续 M3 将升级为 launcher,可拉起游戏→注入→启动前端)。

### M1 阶段的手动启动顺序(M3 之前的过渡)

1. 启动游戏 → 2. 管理员运行 `build\injector.exe` → 3. 启动前端。
前端启动时若管道还不存在,**应重试连接**(后端就绪即连上),或先 `ping` 探测。

## 2. 传输层约定

| 项 | 值 |
|---|---|
| 管道名 | `\\.\pipe\twinshot-trainer` |
| 类型 | 命名管道,字节流,DUPLEX |
| 消息格式 | **JSON Lines**:每条消息一行 UTF-8 JSON,以 `\n` 结尾 |
| 编码 | UTF-8(作弊名等含中文,前端按 UTF-8 解码) |
| 并发 | **同一时刻 1 个客户端**(M1);后端常驻等待重连,前端可随时断开/重连 |
| 权限 | 默认安全描述符(仅当前用户) |
| 免流探测 | 连接后客户端可发 `{"cmd":"ping"}` 收 `pong` + 协议版本 |

## 3. 命令(前端 → 后端)

| 命令 | 请求 | 说明 |
|---|---|---|
| ping | `{"cmd":"ping"}` | 探活 + 协议版本 |
| list | `{"cmd":"list"}` | 枚举作弊项(含实时状态) |
| set | `{"cmd":"set","id":"god","on":false}` | 开/关一个**开关型**作弊 |
| act | `{"cmd":"act","id":"coins"}` | 触发一个**动作型**作弊(如金币+100) |

## 4. 事件(后端 → 前端)

| 事件 | 示例 | 说明 |
|---|---|---|
| pong | `{"ev":"pong","proto":1}` | ping 应答,proto = 协议版本 |
| list | `{"ev":"list","cheats":[{"id":"god","name":"无伤","type":"toggle","on":true,"ready":true,"hotkey":"F3"},…]}` | list 应答;`type`: `toggle`(开关型)/ `action`(动作型) |
| state | `{"ev":"state","id":"god","on":false,"ok":true}` | 开关结果;`ok:false` 时附 `err`(如 `"not ready"`)。**热键引起的状态变化也推此事件** |
| done | `{"ev":"done","id":"coins","ok":true}` | 动作执行完成(含热键触发的动作) |

### 异步语义(重要)

`set`/`act` 命令**先入队、由后端主线程异步执行**(Mono 运行时有线程亲和性,所有游戏相关调用必须在后端唯一的 trainer 线程执行——前端无感,但需要理解"命令发出 ≠ 立即生效")。前端应以收到的 `state`/`done` 事件为准更新 UI。

### 状态同步(M1 建议)

热键改变状态时会主动推 `state` 事件;动作触发会推 `done`。**M1 阶段事件在下一次管道收发周期到达**(后端管道线程 50ms 轮询),前端建议另加 500ms 一次的 `list` 轮询兜底,以覆盖极端丢包场景。

## 5. 命令执行规则(错误语义)

| 情况 | 事件 |
|---|---|
| set/act 的 `id` 不存在 | `{"ev":"state","id":"…","ok":false,"err":"unknown id"}` |
| set/act 目标未就绪(如锁血未定位成功) | `err:"not ready"` |
| 对动作型发 set / 对开关型发 act | `err:"not a toggle cheat"` / `err:"not an action cheat"` |
| set 幂等(已是目标状态) | 正常返回 `ok:true`(无副作用) |

## 6. Python 最小示例

```python
import json

pipe = open(r"\\.\pipe\twinshot-trainer", "r+b", buffering=0)

def send(obj):
    pipe.write((json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"))

def recv():
    line = b""
    while not line.endswith(b"\n"):
        chunk = pipe.read(1)
        if not chunk:
            raise ConnectionError("backend closed")
        line += chunk
    return json.loads(line.decode("utf-8"))

send({"cmd": "ping"});  print(recv())
send({"cmd": "list"});  print(recv())
send({"cmd": "set", "id": "god", "on": False}); print(recv())
send({"cmd": "act", "id": "coins"});           print(recv())
```

## 7. 现有作弊项 ID 一览(v1)

| id | name | type | 热键 | 行为 |
|---|---|---|---|---|
| `god` | 无伤 | toggle | F3 | 怪物攻击不生效 |
| `lock` | 锁血 | toggle | F4 | 受击反馈保留,血量不减 |
| `coins` | 金币+100 | action | F5 | 金币 +100(真实值,持久化) |

> 前端应**始终以 `list` 返回为准**渲染列表,不要硬编码上表——后端加作弊项后前端无需改代码。

## 8. 路线图(前后端共同)

| 阶段 | 内容 | 状态 |
|---|---|---|
| M1 | 管道 + ping/list/set/act + state/done 事件 | ✅ 本版 |
| M2 | 动作参数化(`{"cmd":"act","id":"coins","args":{"amount":500}}`)+ 查询型命令 `get`(UI 实时显示金币等) | ⬜ |
| M3 | launcher:`injector.exe --launch` 拉起游戏→注入→启动前端 exe,完成"与游戏同时启动" | ⬜ |
| M4 | 字符串参数封送(道具菜单类指令)、多客户端、协议版本握手 | ⬜ 按前端需求 |

## 9. 前端开发注意事项

1. **把 `state`/`done` 事件当唯一事实源**:热键和 UI 并行操作,一切以事件/`list` 为准;
2. `ready:false` 的项(如锁血在某些时点未定位)渲染为禁用态,提示"等待游戏加载";
3. 前端崩溃/断开不影响游戏:后端独立自治,重连即恢复;
4. 协议向后兼容原则:只增字段,不改语义;`proto` 字段用于握手(M1 固定为 1)。
