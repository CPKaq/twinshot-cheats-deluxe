// trainer_api.h —— trainer 后端的进程内调用接口(B1 进程内 UI / 未来其他前端共用)
//
// 线程契约(硬约束):任何线程都可以调用本接口;
//   - 快照(GetSnapshot)线程安全,内部只做短临界区拷贝;
//   - 命令(Set/Act)仅入队,由 trainer 线程异步执行——因为 mono 调用有
//     线程亲和性,绝不能在 UI/其他线程直接触碰游戏对象。
// 异步语义:命令发出 ≠ 立即生效;以快照/UI 轮询或事件回调为准。
#pragma once

struct TrainerCheatInfo {
    const char* id;      // 稳定标识(ASCII,如 "god"/"lock"/"coins")
    const char* name;    // 显示名(UTF-8)
    const char* type;    // "toggle"(开关型) / "action"(动作型)
    bool on;             // 开关状态(action 型无意义)
    bool ready;          // 定位是否成功(未就绪时命令会被拒绝)
    int  hotkey;         // VK_*(0 = 未设)
};

const int TRAINER_API_MAX_CHEATS = 16;

// 拍一份作弊列表快照(线程安全;UI 线程可调,不触碰 mono)
bool TrainerApi_GetSnapshot(TrainerCheatInfo* out, int max, int* outCount);

// 异步命令:入队后由 trainer 线程执行
void TrainerApi_Set(const char* id, bool on);
void TrainerApi_Act(const char* id, int arg);   // arg = 动作参数(如金币步长);<=0 用默认值
