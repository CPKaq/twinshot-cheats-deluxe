// trainer.cpp —— Twin Shot Deluxe 内存修改器核心(注入式 DLL)
//
// 架构(多作弊框架 + 双前端通道):
//   1. 等待 mono-2.0-bdwgc.dll 加载,动态绑定其导出函数
//   2. 等 Assembly-CSharp 加载,拿到 image
//   3. 对每个作弊项(Cheat)循环尝试 locate() 直到成功(类/字段可能要等游戏进度)
//   4. 热键循环:每个作弊独立开关;退出时全部还原
//   5. 进程内 UI(B1):独立 Win32 窗口,Insert 显隐,直接调 TrainerApi
//   6. 外部管道前端(A):\\.\pipe\twinshot-trainer,JSON Lines 协议
//   两条前端通道共用 TrainerApi(trainer_api.h)+ 命令队列;
//   mono 调用只发生在 trainer 线程(硬约束)。
//
// 添加新作弊(三步,详见 docs/trainer-framework.md):
//   a. 在 cheats/NN-*.md 完成静态分析(类/字段/方法签名)
//   b. 写 locate / apply / revert 三个函数 + 一个 ctx 结构
//   c. 在下方 kCheats 表加一行
//
// 热键:F3 = 无伤(默认开启)
// 日志:%TEMP%\twinshot_trainer.log(附 OutputDebugStringA)
//
// 本文件不硬编码任何内存偏移:全部经 mono 按名字查找,以抗小版本更新。

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "trainer_api.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cwchar>

// ---------------------------------------------------------------- 日志 ------
static FILE* g_log = nullptr;

static void Log(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char msg[512];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    OutputDebugStringA("[twinshot-trainer] ");
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");

    if (!g_log) {
        char tmp[MAX_PATH] = {0}, path[MAX_PATH] = {0};
        if (!GetEnvironmentVariableA("TEMP", tmp, MAX_PATH)) return;
        snprintf(path, sizeof(path), "%s\\twinshot_trainer.log", tmp);
        g_log = fopen(path, "w");
        if (!g_log) return;
    }
    fprintf(g_log, "%s\n", msg);
    fflush(g_log);
}

// --------------------------------------------------------- Mono 动态绑定 ----
// 只用不透明指针 + 自声明原型,不需要 mono 头文件。
typedef struct MonoDomain          MonoDomain;
typedef struct MonoAssembly        MonoAssembly;
typedef struct MonoImage           MonoImage;
typedef struct MonoClass           MonoClass;
typedef struct MonoMethod          MonoMethod;
typedef struct MonoMethodSignature MonoMethodSignature;
typedef struct MonoType            MonoType;
typedef struct MonoClassField      MonoClassField;
typedef struct MonoVTable          MonoVTable;
typedef struct MonoObject          MonoObject;

typedef MonoDomain*           (*fn_mono_get_root_domain)(void);
typedef MonoDomain*           (*fn_mono_domain_get)(void);
typedef void*                 (*fn_mono_thread_attach)(MonoDomain*);
typedef void                  (*fn_mono_domain_assembly_foreach)(MonoDomain*, void (*)(MonoAssembly*, void*), void*);
typedef MonoImage*            (*fn_mono_assembly_get_image)(MonoAssembly*);
typedef const char*           (*fn_mono_image_get_name)(MonoImage*);
typedef MonoClass*            (*fn_mono_class_from_name)(MonoImage*, const char* ns, const char* name);
typedef MonoMethod*           (*fn_mono_class_get_methods)(MonoClass*, void** iter);
typedef MonoClassField*       (*fn_mono_class_get_field_from_name)(MonoClass*, const char* name);
typedef MonoClassField*       (*fn_mono_class_get_fields)(MonoClass*, void** iter);
typedef const char*           (*fn_mono_method_get_name)(MonoMethod*);
typedef unsigned              (*fn_mono_method_get_token)(MonoMethod*);
typedef MonoMethodSignature*  (*fn_mono_method_get_signature)(MonoMethod*, MonoImage*, unsigned token);
typedef int                   (*fn_mono_signature_get_param_count)(MonoMethodSignature*);
typedef MonoType*             (*fn_mono_signature_get_params)(MonoMethodSignature*, void** iter);
typedef char*                 (*fn_mono_type_get_name)(MonoType*);
typedef void*                 (*fn_mono_compile_method)(MonoMethod*);
// 字段读写(为锁血/金币等数值类作弊预留)
typedef void                  (*fn_mono_field_get_value)(void* obj, MonoClassField*, void* out);
typedef void                  (*fn_mono_field_set_value)(void* obj, MonoClassField*, void* value);
typedef MonoVTable*           (*fn_mono_class_vtable)(MonoDomain*, MonoClass*);
typedef void*                 (*fn_mono_vtable_get_static_field_data)(MonoVTable*);
typedef void                  (*fn_mono_field_static_get_value)(MonoVTable* vt, MonoClassField*, void* out);
typedef MonoObject*           (*fn_mono_runtime_invoke)(MonoMethod*, void* obj, void** params, MonoObject** exc);
typedef const char*           (*fn_mono_field_get_name)(MonoClassField*);
typedef MonoType*             (*fn_mono_field_get_type)(MonoClassField*);
typedef unsigned              (*fn_mono_field_get_flags)(MonoClassField*);

struct MonoApi {
    fn_mono_get_root_domain           mono_get_root_domain         = nullptr;
    fn_mono_domain_get                mono_domain_get              = nullptr;
    fn_mono_thread_attach             mono_thread_attach           = nullptr;
    fn_mono_domain_assembly_foreach   mono_domain_assembly_foreach = nullptr;
    fn_mono_assembly_get_image        mono_assembly_get_image      = nullptr;
    fn_mono_image_get_name            mono_image_get_name          = nullptr;
    fn_mono_class_from_name           mono_class_from_name         = nullptr;
    fn_mono_class_get_methods         mono_class_get_methods       = nullptr;
    fn_mono_class_get_field_from_name mono_class_get_field_from_name = nullptr;
    fn_mono_class_get_fields          mono_class_get_fields        = nullptr;
    fn_mono_method_get_name           mono_method_get_name         = nullptr;
    fn_mono_method_get_token          mono_method_get_token        = nullptr;
    fn_mono_method_get_signature      mono_method_get_signature    = nullptr;
    fn_mono_signature_get_param_count mono_signature_get_param_count = nullptr;
    fn_mono_signature_get_params      mono_signature_get_params    = nullptr;
    fn_mono_type_get_name             mono_type_get_name           = nullptr;
    fn_mono_compile_method            mono_compile_method          = nullptr;
    fn_mono_field_get_value           mono_field_get_value         = nullptr;
    fn_mono_field_set_value           mono_field_set_value         = nullptr;
    fn_mono_class_vtable              mono_class_vtable            = nullptr;
    fn_mono_vtable_get_static_field_data mono_vtable_get_static_field_data = nullptr;
    fn_mono_field_static_get_value    mono_field_static_get_value  = nullptr;
    fn_mono_runtime_invoke            mono_runtime_invoke          = nullptr;
    fn_mono_field_get_name            mono_field_get_name          = nullptr;
    fn_mono_field_get_type            mono_field_get_type          = nullptr;
    fn_mono_field_get_flags           mono_field_get_flags         = nullptr;
};

static MonoApi g_api;

template <typename T>
static T Proc(HMODULE h, const char* name, bool required) {
    void* p = (void*)GetProcAddress(h, name);
    if (!p)
        Log("%s mono export: %s", required ? "MISSING required" : "optional (absent)", name);
    return (T)p;
}

static bool InitMono() {
    HMODULE h = GetModuleHandleW(L"mono-2.0-bdwgc.dll");
    if (!h) return false;                                    // mono 还没加载
    MonoApi& a = g_api;
    // 核心(缺失则整体失败)
    a.mono_get_root_domain           = Proc<fn_mono_get_root_domain>(h, "mono_get_root_domain", true);
    a.mono_domain_get                = Proc<fn_mono_domain_get>(h, "mono_domain_get", true);
    a.mono_thread_attach             = Proc<fn_mono_thread_attach>(h, "mono_thread_attach", true);
    a.mono_domain_assembly_foreach   = Proc<fn_mono_domain_assembly_foreach>(h, "mono_domain_assembly_foreach", true);
    a.mono_assembly_get_image        = Proc<fn_mono_assembly_get_image>(h, "mono_assembly_get_image", true);
    a.mono_image_get_name            = Proc<fn_mono_image_get_name>(h, "mono_image_get_name", true);
    a.mono_class_from_name           = Proc<fn_mono_class_from_name>(h, "mono_class_from_name", true);
    a.mono_class_get_methods         = Proc<fn_mono_class_get_methods>(h, "mono_class_get_methods", true);
    a.mono_class_get_fields          = Proc<fn_mono_class_get_fields>(h, "mono_class_get_fields", false);
    a.mono_method_get_name           = Proc<fn_mono_method_get_name>(h, "mono_method_get_name", true);
    a.mono_method_get_token          = Proc<fn_mono_method_get_token>(h, "mono_method_get_token", true);
    a.mono_method_get_signature      = Proc<fn_mono_method_get_signature>(h, "mono_method_get_signature", true);
    a.mono_signature_get_param_count = Proc<fn_mono_signature_get_param_count>(h, "mono_signature_get_param_count", true);
    a.mono_signature_get_params      = Proc<fn_mono_signature_get_params>(h, "mono_signature_get_params", true);
    a.mono_type_get_name             = Proc<fn_mono_type_get_name>(h, "mono_type_get_name", true);
    a.mono_compile_method            = Proc<fn_mono_compile_method>(h, "mono_compile_method", true);
    if (!a.mono_get_root_domain || !a.mono_domain_get || !a.mono_thread_attach ||
        !a.mono_domain_assembly_foreach || !a.mono_assembly_get_image || !a.mono_image_get_name ||
        !a.mono_class_from_name || !a.mono_class_get_methods || !a.mono_method_get_name ||
        !a.mono_method_get_token || !a.mono_method_get_signature ||
        !a.mono_signature_get_param_count || !a.mono_signature_get_params ||
        !a.mono_type_get_name || !a.mono_compile_method) {
        Log("mono api incomplete, abort");
        return false;
    }
    // 扩展(当前仅数值类作弊需要;缺失只警告,不影响方法 patch 类作弊)
    a.mono_class_get_field_from_name = Proc<fn_mono_class_get_field_from_name>(h, "mono_class_get_field_from_name", false);
    a.mono_field_get_value           = Proc<fn_mono_field_get_value>(h, "mono_field_get_value", false);
    a.mono_field_set_value           = Proc<fn_mono_field_set_value>(h, "mono_field_set_value", false);
    a.mono_class_vtable              = Proc<fn_mono_class_vtable>(h, "mono_class_vtable", false);
    a.mono_vtable_get_static_field_data = Proc<fn_mono_vtable_get_static_field_data>(h, "mono_vtable_get_static_field_data", false);
    a.mono_field_static_get_value    = Proc<fn_mono_field_static_get_value>(h, "mono_field_static_get_value", false);
    a.mono_runtime_invoke            = Proc<fn_mono_runtime_invoke>(h, "mono_runtime_invoke", false);
    a.mono_field_get_name            = Proc<fn_mono_field_get_name>(h, "mono_field_get_name", false);
    a.mono_field_get_type            = Proc<fn_mono_field_get_type>(h, "mono_field_get_type", false);
    a.mono_field_get_flags           = Proc<fn_mono_field_get_flags>(h, "mono_field_get_flags", false);

    // 任何 mono API 都要求当前线程 attach 到 Mono 运行时
    MonoDomain* d = a.mono_get_root_domain();
    if (!d) { Log("root domain not ready"); return false; }
    a.mono_thread_attach(d);
    Log("mono attached, root domain ok");
    return true;
}

// ------------------------------------------------- 定位工具(按名字查找)----
static MonoImage* g_gameImage = nullptr;   // Assembly-CSharp 的 image,全框架共用

struct FindAsmCtx { MonoImage* image; };
static void AsmCallback(MonoAssembly* a, void* user) {
    if (!a) return;
    FindAsmCtx* c = (FindAsmCtx*)user;
    if (c->image) return;
    MonoImage* img = g_api.mono_assembly_get_image(a);
    if (!img) return;
    const char* n = g_api.mono_image_get_name(img);
    if (n && _stricmp(n, "Assembly-CSharp") == 0) c->image = img;
}

static void FindGameImage() {
    FindAsmCtx ctx = {nullptr};
    g_api.mono_domain_assembly_foreach(g_api.mono_domain_get(), AsmCallback, &ctx);
    if (ctx.image) {
        g_gameImage = ctx.image;
        Log("Assembly-CSharp image found");
    }
}

static MonoClass* FindClass(const char* ns, const char* name) {
    if (!g_gameImage) return nullptr;
    MonoClass* cls = g_api.mono_class_from_name(g_gameImage, ns, name);
    if (!cls) Log("class not found (yet?): %s.%s", ns[0] ? ns : "(global)", name);
    return cls;
}

static MonoClassField* GetField(MonoClass* cls, const char* name) {
    if (!g_api.mono_class_get_field_from_name) return nullptr;
    return g_api.mono_class_get_field_from_name(cls, name);
}

static const char* FieldTypeName(MonoClassField* f) {
    if (!g_api.mono_field_get_type || !f) return "?";
    MonoType* t = g_api.mono_field_get_type(f);
    if (!t) return "?";
    char* n = g_api.mono_type_get_name(t);
    return n ? n : "?";                                  // 诊断路径,小泄漏可忽略
}

// FIELD_ATTRIBUTE_STATIC = 0x0010
static void DumpFields(MonoClass* cls, const char* clsName) {
    if (!g_api.mono_field_get_name || !g_api.mono_class_get_fields) {
        Log("field dump unavailable (mono_field_get_name/mono_class_get_fields missing)");
        return;
    }
    Log("---- field dump: %s ----", clsName);
    void* it = nullptr; MonoClassField* f; int i = 0;
    while ((f = g_api.mono_class_get_fields(cls, &it)) != nullptr) {
        unsigned fl = g_api.mono_field_get_flags ? g_api.mono_field_get_flags(f) : 0;
        Log("  [%02d] %-28s : %s%s (flags 0x%04X)", i++,
            g_api.mono_field_get_name(f), FieldTypeName(f),
            (fl & 0x10) ? " [static]" : "", fl);
    }
    Log("---- field dump end ----");
}

// 找"static 且类型名含 typeName"的字段——静态单例是属性(而非字段)时的兜底
static bool TypeNameIs(const char* n, const char* typeName) {
    if (!n || !typeName) return false;
    size_t ln = strlen(n), lt = strlen(typeName);
    if (ln == lt && strcmp(n, typeName) == 0) return true;
    return ln > lt && n[ln - lt - 1] == '.' && strcmp(n + ln - lt, typeName) == 0;
}

// staticOnly=true 找静态字段,false 找实例字段;类型名精确匹配或"命名空间.类名"后缀匹配
static MonoClassField* FindFieldOfType(MonoClass* cls, const char* typeName, bool staticOnly) {
    if (!g_api.mono_field_get_flags || !g_api.mono_field_get_type || !g_api.mono_class_get_fields) return nullptr;
    void* it = nullptr; MonoClassField* f;
    while ((f = g_api.mono_class_get_fields(cls, &it)) != nullptr) {
        unsigned fl = g_api.mono_field_get_flags(f);
        if (staticOnly != ((fl & 0x10) != 0)) continue;
        MonoType* t = g_api.mono_field_get_type(f);
        if (!t) continue;
        char* n = g_api.mono_type_get_name(t);
        if (TypeNameIs(n, typeName)) return f;
    }
    return nullptr;
}

// 按名字+签名找方法:paramCount<0 表示不限;typeContains 非空时要求任一参数类型名含该串
static MonoMethod* FindMethod(const char* ns, const char* klass, const char* name,
                              int paramCount, const char* typeContains) {
    MonoClass* cls = FindClass(ns, klass);
    if (!cls) return nullptr;
    void* it = nullptr; MonoMethod* m;
    while ((m = g_api.mono_class_get_methods(cls, &it)) != nullptr) {
        const char* n = g_api.mono_method_get_name(m);
        if (!n || strcmp(n, name) != 0) continue;
        MonoMethodSignature* sig =
            g_api.mono_method_get_signature(m, g_gameImage, g_api.mono_method_get_token(m));
        if (!sig) continue;
        int pc = g_api.mono_signature_get_param_count(sig);
        if (paramCount >= 0 && pc != paramCount) continue;
        if (typeContains) {
            bool match = false;
            void* pit = nullptr;
            for (int i = 0; i < pc && !match; ++i) {
                MonoType* t = g_api.mono_signature_get_params(sig, &pit);
                if (!t) break;
                char* tn = g_api.mono_type_get_name(t);
                if (tn && strstr(tn, typeContains)) match = true;
            }
            if (!match) continue;
        }
        return m;
    }
    return nullptr;
}

// ------------------------------------------------------------ 字节 patch ----
struct BytePatch {
    void*    entry    = nullptr;   // JIT 入口
    uint8_t  orig[16] = {0};       // 原始字节(还原用)
    uint8_t  code[16] = {0};       // patch 字节
    size_t   len      = 0;
    bool     prepared = false;
    bool     active   = false;
};

static bool PreparePatch(BytePatch* p, MonoMethod* m, const uint8_t* code, size_t len) {
    void* entry = g_api.mono_compile_method(m);      // 已实测:可主动触发 JIT
    if (!entry) { Log("mono_compile_method failed"); return false; }
    DWORD old = 0;
    VirtualProtect(entry, 16, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p->orig, entry, len);
    VirtualProtect(entry, 16, old, &old);
    memcpy(p->code, code, len);
    p->entry = entry; p->len = len; p->prepared = true; p->active = false;
    Log("patch prepared: entry %p, orig %02X %02X %02X, len %zu",
        entry, p->orig[0], p->orig[1], p->orig[2], len);
    return true;
}

static void ApplyPatch(BytePatch* p, bool on) {
    if (!p || !p->prepared || p->active == on) return;
    DWORD old = 0;
    VirtualProtect(p->entry, 16, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p->entry, on ? p->code : p->orig, p->len);
    FlushInstructionCache(GetCurrentProcess(), p->entry, p->len);
    VirtualProtect(p->entry, 16, old, &old);
    p->active = on;
    Log("patch %s (entry %p)", on ? "ON" : "OFF", p->entry);
}

// ------------------------------------------------------------ 作弊项框架 ----
struct Cheat;
typedef bool  (*CheatLocateFn)(Cheat&);   // 定位目标,可反复重试直到成功
typedef void  (*CheatApplyFn)(Cheat&);    // 开启(写补丁/锁字段)
typedef void  (*CheatRevertFn)(Cheat&);   // 关闭(还原)
typedef void  (*CheatTickFn)(Cheat&);     // 周期维护(锁数值类用;可空)
typedef void  (*CheatActionFn)(Cheat&);   // 动作型:热键一次性触发(加金币等;设置后无开/关状态)

struct Cheat {
    const char*   id;                       // 稳定标识(前端管道协议用,ASCII,与 title 解耦)
    const char*   title;                    // 名字(日志/提示用)
    int           hotkey;                   // VK_* 键
    bool          defaultOn;                // 注入成功后默认开启?(动作型无效)
    CheatLocateFn locate;
    CheatApplyFn  apply;
    CheatRevertFn revert;
    CheatTickFn   tick;                     // 开启期间每 ~30ms 调一次;不需要则 nullptr
    void*         ctx;
    CheatActionFn action = nullptr;         // 非空 = 动作型作弊(热键触发 action,忽略 apply/revert/tick/on)
    int           arg   = 100;              // 动作参数(金币步长等);管道/UI 命令可覆盖
    bool          ready = false;
    bool          on    = false;
};

static const char* HotkeyName(int vk) {
    static const char* kF[] = {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
    if (vk >= VK_F1 && vk <= VK_F12) return kF[vk - VK_F1];
    return "?";
}

// ---- 作弊 01:无伤(Player.Hit(Vector2) → 无条件 return false)--------------
struct GodCtx { BytePatch patch; };
static GodCtx g_god;

static bool God_Locate(Cheat&) {
    MonoMethod* m = FindMethod("", "Player", "Hit", 1, "Vector2");
    if (!m) return false;
    Log("resolved Player.Hit(Vector2)");
    static const uint8_t kRetFalse[3] = {0x31, 0xC0, 0xC3};  // xor eax,eax; ret
    return PreparePatch(&g_god.patch, m, kRetFalse, 3);
}
static void God_Apply (Cheat&) { ApplyPatch(&g_god.patch, true);  }
static void God_Revert(Cheat&) { ApplyPatch(&g_god.patch, false); }

// ---- 作弊 02:锁血(Game.instance → Game.level → players[] → 每玩家 hits 快照写回)------
// 数据链(经 dnSpy + field dump 确认,见 cheats/02-锁血.md):
//   Level.Instance 是【属性】而非字段,其实现为:
//     public static Level Instance { get { return Game.instance != null ? Game.instance.level : null; } }
//   所以 trainer 走字段链:Game.instance (静态) → Game.level (实例,当前关卡) → Level.players (Player[]) → Player.hits (int)
// 语义:受击反馈保留(音效/闪烁/击退),但 hits 不减 → Kill() 永不触发。
// 快照:开启瞬间逐玩家记录当前 hits(兼容自定义血量);实例换新(复活/换关)自动重快照。
struct LockCtx {
    MonoClassField* fGameInstance = nullptr;   // Game.instance (static)
    MonoClassField* fLevel        = nullptr;   // Game.level → Level 实例
    MonoClassField* fPlayers      = nullptr;   // Level.players (Player[])
    MonoClassField* fHits         = nullptr;   // Player.hits (int)
    MonoVTable*     gameVt        = nullptr;
    struct Snap { void* obj; int val; } snaps[8];   // obj==nullptr 表示空槽
};
static LockCtx g_lock;

static bool Lock_Locate(Cheat&) {
    if (!g_api.mono_field_static_get_value || !g_api.mono_field_get_value || !g_api.mono_field_set_value ||
        !g_api.mono_class_get_field_from_name || !g_api.mono_class_vtable) {
        Log("lock: field api missing, disabled");
        return false;
    }
    static int fails = 0;                                // 失败计数(节流 + 首次 dump)
    MonoClass* levelCls  = FindClass("", "Level");
    if (!levelCls) return false;
    MonoClass* playerCls = FindClass("", "Player");
    if (!playerCls) return false;
    MonoClass* gameCls   = FindClass("", "Game");
    if (!gameCls) return false;

    // 单例链:Level.Instance 是属性(无字段),实际取 Game.instance(静态)的 level 字段
    if (!g_lock.fGameInstance) {
        g_lock.fGameInstance = GetField(gameCls, "instance");
        if (!g_lock.fGameInstance) g_lock.fGameInstance = FindFieldOfType(gameCls, "Game", true);
    }
    if (!g_lock.fLevel) {
        g_lock.fLevel = GetField(gameCls, "level");
        if (!g_lock.fLevel) g_lock.fLevel = FindFieldOfType(gameCls, "Level", false);
    }
    if (!g_lock.fPlayers) g_lock.fPlayers = GetField(levelCls, "players");
    if (!g_lock.fHits)    g_lock.fHits    = GetField(playerCls, "hits");

    if (g_lock.fGameInstance && g_lock.fLevel && g_lock.fPlayers && g_lock.fHits) {
        g_lock.gameVt = g_api.mono_class_vtable(g_api.mono_domain_get(), gameCls);
        if (!g_lock.gameVt) { Log("lock: mono_class_vtable failed"); return false; }
        memset(g_lock.snaps, 0, sizeof(g_lock.snaps));
        Log("lock: Game.instance -> level -> players / hits resolved");
        return true;
    }

    // 失败:首次 dump 相关类的全部字段供人工比对,之后每 16 次才再报一次
    ++fails;
    if (fails == 1) {
        Log("lock: resolve failed (Game.instance=%s Game.level=%s players=%s hits=%s)",
            g_lock.fGameInstance ? "ok" : "MISS", g_lock.fLevel ? "ok" : "MISS",
            g_lock.fPlayers ? "ok" : "MISS", g_lock.fHits ? "ok" : "MISS");
        if (!g_lock.fGameInstance || !g_lock.fLevel) DumpFields(gameCls, "Game");
        if (!g_lock.fPlayers) DumpFields(levelCls, "Level");
        if (!g_lock.fHits)    DumpFields(playerCls, "Player");
    } else if (fails % 16 == 0) {
        Log("lock: resolve failed (retrying, attempt %d)", fails);
    }
    return false;
}

static int Lock_GetHits(void* player) {
    int v = 0;
    g_api.mono_field_get_value(player, g_lock.fHits, &v);
    return v;
}
static void Lock_SetHits(void* player, int v) {
    g_api.mono_field_set_value(player, g_lock.fHits, &v);
}

// x64 mono 数组布局:对象头 16B + bounds 指针 8B + max_length 8B → 数据区 +0x20
static void Lock_Tick(Cheat&) {
    void* game = nullptr;
    g_api.mono_field_static_get_value(g_lock.gameVt, g_lock.fGameInstance, &game);
    if (!game) return;                                   // 主菜单等:Game.instance 为 null
    void* level = nullptr;
    g_api.mono_field_get_value(game, g_lock.fLevel, &level);
    if (!level) return;                                  // 有 Game 但无关卡实例(过场/菜单)
    void* arr = nullptr;
    g_api.mono_field_get_value(level, g_lock.fPlayers, &arr);
    if (!arr) return;
    long long n = *(long long*)((uint8_t*)arr + 0x18);   // max_length(64 位下为 8 字节)
    if (n < 0 || n > 8) { Log("lock: players count %lld unexpected, skip", n); return; }
    void** elems = (void**)((uint8_t*)arr + 0x20);
    for (long long i = 0; i < n; ++i) {
        void* p = elems[i];
        if (!p) continue;                                // 该槽位无玩家
        // 快照:按实例指针匹配;指针不在表里 = 新实例(复活/换关),以当前 hits 重新快照
        LockCtx::Snap* s = nullptr;
        for (auto& sn : g_lock.snaps) if (sn.obj == p) { s = &sn; break; }
        if (!s) {
            for (auto& sn : g_lock.snaps) if (!sn.obj) { s = &sn; break; }
            if (!s) {                                    // 表满(理论上不可能:>8 个玩家)
                memset(g_lock.snaps, 0, sizeof(g_lock.snaps));
                s = &g_lock.snaps[0];
            }
            s->obj = p;
            s->val = Lock_GetHits(p);
            Log("lock: snapshot player #%lld hits=%d (%p)", i, s->val, p);
        }
        Lock_SetHits(p, s->val);
    }
}
static void Lock_Apply(Cheat&) {
    memset(g_lock.snaps, 0, sizeof(g_lock.snaps));       // 开启瞬间重新快照
}
static void Lock_Revert(Cheat&) {
    memset(g_lock.snaps, 0, sizeof(g_lock.snaps));       // 关闭即恢复自然扣血
}

// ---- 作弊 03:金币+100(反射调用 SaveData.GetCoins / SetCoins)------------
// 数据链(经 dnSpy 确认,见 cheats/03-修改金币.md):
//   金币无专属字段,是 SaveData.Backing 键值存储的 "coins" 条目;
//   UI 读 GetCoins(显示层缓存,面板 Show 时刷新),消费走 SetCoins(自带 Save() 持久化)。
//   → 走"调用游戏自己的方法":mono_runtime_invoke 反射调用,改真实值。
// 形态:动作型(action 槽)——热键 F5 一次 +100,无开/关状态。
struct CoinsCtx {
    MonoMethod* getCoins = nullptr;   // static int GetCoins()
    MonoMethod* setCoins = nullptr;   // static void SetCoins(int)
};
static CoinsCtx g_coins;

static bool Coins_Locate(Cheat&) {
    if (!g_api.mono_runtime_invoke) { Log("coins: mono_runtime_invoke missing, disabled"); return false; }
    g_coins.getCoins = FindMethod("", "SaveData", "GetCoins", 0, nullptr);
    if (!g_coins.getCoins) return false;
    g_coins.setCoins = FindMethod("", "SaveData", "SetCoins", 1, "Int32");
    if (!g_coins.setCoins) return false;
    Log("coins: SaveData.GetCoins / SetCoins resolved");
    return true;
}

// 反射调用静态方法;retOut 仅在有返回值时传(装箱对象,值在 +0x10)
static bool InvokeStatic(MonoMethod* m, void** params, MonoObject** retOut) {
    MonoObject* exc = nullptr;
    MonoObject* ret = g_api.mono_runtime_invoke(m, nullptr, params, &exc);
    if (exc) {
        Log("runtime_invoke exception in %s", g_api.mono_method_get_name(m));
        return false;
    }
    if (retOut) *retOut = ret;
    return true;
}

static void Coins_Action(Cheat& c) {
    MonoObject* boxed = nullptr;
    int cur = 0;
    if (!InvokeStatic(g_coins.getCoins, nullptr, &boxed) || !boxed) return;
    cur = *(int*)((uint8_t*)boxed + 0x10);               // 装箱 int:对象头 16B 后是值
    int step = (c.arg > 0) ? c.arg : 100;
    int next = cur + step;
    void* args[1] = { &next };
    if (InvokeStatic(g_coins.setCoins, args, nullptr))
        Log("[coins] %d -> %d (+%d)", cur, next, step);
}

// ---- 作弊注册表:新作弊先写 locate/apply/revert(/tick),再在这里加一行 ----------
// id 是前端管道协议的稳定标识(docs/architecture-frontend.md),ASCII 小写。
static Cheat kCheats[] = {
    { "lock",  "锁血",     VK_F4, true,  Lock_Locate, Lock_Apply, Lock_Revert, Lock_Tick, &g_lock },
    { "god",   "无伤",     VK_F3, true,  God_Locate, God_Apply, God_Revert, nullptr, &g_god },
    { "coins", "金币+100", VK_F5, false, Coins_Locate, nullptr, nullptr, nullptr, &g_coins, Coins_Action },
};
static const size_t kCheatsCount = sizeof(kCheats) / sizeof(kCheats[0]);

static Cheat* FindCheatById(const char* id) {
    if (!id) return nullptr;
    for (size_t i = 0; i < kCheatsCount; ++i)
        if (strcmp(kCheats[i].id, id) == 0) return &kCheats[i];
    return nullptr;
}

// ------------------------------------------------- 前端管道服务(M1)--------
// 协议见 docs/architecture-frontend.md:JSON Lines / UTF-8 / 单客户端 / 协议 v1。
// 线程模型(硬约束):管道线程只"收命令→入队、发事件",**绝不直接调 mono**;
// 游戏相关命令由 Worker 每轮消费(ProcessIpcCommands),mono 调用只发生在 trainer 线程。
#define TRAINER_PIPE_NAME L"\\\\.\\pipe\\twinshot-trainer"
#define IPC_PROTO 1

static CRITICAL_SECTION g_ipcCs;
static volatile bool g_run = false;              // 全局运行标志(Worker/管道线程共用)

struct PipeCmd {
    enum Type { LIST, SET, ACT } type;
    char id[32];
    bool on;                                     // SET 用
    int  arg;                                    // ACT 用(动作参数)
};
static PipeCmd g_cmdQ[16];  static int g_cmdQCount = 0;
static char     g_evQ[16][512]; static int g_evQCount = 0;

static void IpcPushEvent(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_ipcCs);
    if (g_evQCount < 16) snprintf(g_evQ[g_evQCount++], sizeof(g_evQ[0]), "%s", buf);
    LeaveCriticalSection(&g_ipcCs);
}
static bool IpcPopEvent(char* out, size_t outLen) {
    EnterCriticalSection(&g_ipcCs);
    bool ok = (g_evQCount > 0);
    if (ok) {
        snprintf(out, outLen, "%s", g_evQ[0]);
        for (int i = 1; i < g_evQCount; ++i)
            snprintf(g_evQ[i-1], sizeof(g_evQ[0]), "%s", g_evQ[i]);
        g_evQCount--;
    }
    LeaveCriticalSection(&g_ipcCs);
    return ok;
}
static void IpcEnqueueCmd(PipeCmd::Type type, const char* id, bool on, int arg = 0) {
    EnterCriticalSection(&g_ipcCs);
    if (g_cmdQCount < 16) {
        PipeCmd& c = g_cmdQ[g_cmdQCount++];
        c.type = type;
        snprintf(c.id, sizeof(c.id), "%s", id ? id : "");
        c.on = on;
        c.arg = arg;
    }
    LeaveCriticalSection(&g_ipcCs);
}

// 极简 JSON 取值(只覆盖自家协议):找 "key" 后跟冒号的字符串/布尔
static bool JsonGetString(const char* json, const char* key, char* out, size_t outLen) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '"') return false;
    ++p;
    size_t n = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) { ++p; if (n + 1 < outLen) out[n++] = *p++; }
        else if (n + 1 < outLen) out[n++] = *p++;
        else break;
    }
    out[n] = 0;
    return true;
}
static bool JsonGetBool(const char* json, const char* key, bool def) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != ':') return def;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncmp(p, "true", 4) == 0) return true;
    if (strncmp(p, "false", 5) == 0) return false;
    return def;
}
static int JsonGetInt(const char* json, const char* key, int def) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(json, pat);
    if (!p) return def;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != ':') return def;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    return atoi(p);                                  // 解析失败返回 0,由调用方兜底
}

static void IpcPushState(const Cheat& c, bool ok, const char* err) {
    if (ok)
        IpcPushEvent("{\"ev\":\"state\",\"id\":\"%s\",\"on\":%s,\"ok\":true}",
                     c.id, c.on ? "true" : "false");
    else
        IpcPushEvent("{\"ev\":\"state\",\"id\":\"%s\",\"ok\":false,\"err\":\"%s\"}",
                     c.id, err ? err : "error");
}

// ------------------------------------------------- TrainerApi(正式解耦层)--
// 进程内 UI(B1)与外部管道前端共用的语义层:入队异步执行;快照线程安全。
// 见 trainer_api.h 与 docs/architecture-inprocess-ui.md。
bool TrainerApi_GetSnapshot(TrainerCheatInfo* out, int max, int* outCount) {
    if (!out || !outCount || max <= 0) return false;
    EnterCriticalSection(&g_ipcCs);
    int n = 0;
    for (size_t i = 0; i < kCheatsCount && n < max; ++i) {
        const Cheat& c = kCheats[i];
        out[n].id     = c.id;
        out[n].name   = c.title;
        out[n].type   = c.action ? "action" : "toggle";
        out[n].on     = c.on;
        out[n].ready  = c.ready;
        out[n].hotkey = c.hotkey;
        ++n;
    }
    LeaveCriticalSection(&g_ipcCs);
    *outCount = n;
    return true;
}
void TrainerApi_Set(const char* id, bool on) {
    IpcEnqueueCmd(PipeCmd::SET, id, on, 0);
}
void TrainerApi_Act(const char* id, int arg) {
    IpcEnqueueCmd(PipeCmd::ACT, id, false, arg);
}

// Worker 每轮消费命令队列:此处在 trainer 线程,可安全调 mono
static void ProcessIpcCommands() {
    for (;;) {
        PipeCmd cmd;
        {
            EnterCriticalSection(&g_ipcCs);
            if (g_cmdQCount == 0) { LeaveCriticalSection(&g_ipcCs); return; }
            cmd = g_cmdQ[0];
            for (int i = 1; i < g_cmdQCount; ++i) g_cmdQ[i-1] = g_cmdQ[i];
            g_cmdQCount--;
            LeaveCriticalSection(&g_ipcCs);
        }
        if (cmd.type == PipeCmd::LIST) {
            char buf[1024] = {0};
            int pos = snprintf(buf, sizeof(buf), "{\"ev\":\"list\",\"cheats\":[");
            for (size_t i = 0; i < kCheatsCount && pos > 0 && pos < (int)sizeof(buf) - 96; ++i) {
                const Cheat& c = kCheats[i];
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                    "%s{\"id\":\"%s\",\"name\":\"%s\",\"type\":\"%s\",\"on\":%s,\"ready\":%s,\"hotkey\":\"%s\"}",
                    i ? "," : "", c.id, c.title, c.action ? "action" : "toggle",
                    c.on ? "true" : "false", c.ready ? "true" : "false", HotkeyName(c.hotkey));
            }
            snprintf(buf + pos, sizeof(buf) - (size_t)pos, "]}");
            IpcPushEvent("%s", buf);
            continue;
        }
        Cheat* c = FindCheatById(cmd.id);
        if (!c) {
            IpcPushEvent("{\"ev\":\"state\",\"id\":\"%s\",\"ok\":false,\"err\":\"unknown id\"}", cmd.id);
            continue;
        }
        if (cmd.type == PipeCmd::SET) {
            if (!c->ready) { IpcPushState(*c, false, "not ready"); continue; }
            if (c->action) { IpcPushState(*c, false, "not a toggle cheat"); continue; }
            if (c->on != cmd.on) {
                c->on = cmd.on;
                if (c->on) c->apply(*c); else c->revert(*c);
                Log("[cheat] %s %s", c->title, c->on ? "ON" : "OFF");
            }
            IpcPushState(*c, true, nullptr);            // 幂等:同状态也回执
        } else { // ACT
            if (!c->ready) { IpcPushEvent("{\"ev\":\"done\",\"id\":\"%s\",\"ok\":false,\"err\":\"not ready\"}", c->id); continue; }
            if (!c->action) { IpcPushEvent("{\"ev\":\"done\",\"id\":\"%s\",\"ok\":false,\"err\":\"not an action cheat\"}", c->id); continue; }
            if (cmd.arg > 0) c->arg = cmd.arg;          // 覆盖动作参数(<=0 保留当前值)
            c->action(*c);
            IpcPushEvent("{\"ev\":\"done\",\"id\":\"%s\",\"ok\":true}", c->id);
        }
    }
}

// ---- 管道线程:收发循环,不碰任何 mono 数据 --------------------------------
static void HandleClientLine(const char* line) {
    char cmd[16];
    if (!JsonGetString(line, "cmd", cmd, sizeof(cmd))) return;
    if (strcmp(cmd, "ping") == 0) { IpcPushEvent("{\"ev\":\"pong\",\"proto\":%d}", IPC_PROTO); return; }
    if (strcmp(cmd, "list") == 0) { IpcEnqueueCmd(PipeCmd::LIST, "", false); return; }
    char id[32];
    if (!JsonGetString(line, "id", id, sizeof(id))) return;
    if (strcmp(cmd, "set") == 0)
        IpcEnqueueCmd(PipeCmd::SET, id, JsonGetBool(line, "on", false), 0);
    else if (strcmp(cmd, "act") == 0)
        IpcEnqueueCmd(PipeCmd::ACT, id, false, JsonGetInt(line, "amount", 0));   // M2:参数已可用
}

static DWORD WINAPI PipeThreadEntry(LPVOID) {
    while (g_run) {
        HANDLE p = CreateNamedPipeW(TRAINER_PIPE_NAME,
            PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);                  // 单实例(M1)
        if (p == INVALID_HANDLE_VALUE) {
            Log("pipe create failed, err=%lu", GetLastError());
            Sleep(1000);
            continue;
        }
        Log("pipe server listening");
        BOOL connected = ConnectNamedPipe(p, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) { CloseHandle(p); continue; }
        Log("frontend connected");
        IpcEnqueueCmd(PipeCmd::LIST, "", false);         // 连接后先发一份列表快照

        char rbuf[1024];
        size_t rlen = 0;
        while (g_run) {
            char ev[512];                                // 1) 推送待发事件
            while (IpcPopEvent(ev, sizeof(ev))) {
                DWORD w = 0;
                if (!WriteFile(p, ev, (DWORD)strlen(ev), &w, nullptr) ||
                    !WriteFile(p, "\n", 1, &w, nullptr)) goto disconnected;
            }
            DWORD avail = 0;                             // 2) 非阻塞查收入站
            if (!PeekNamedPipe(p, nullptr, 0, nullptr, &avail, nullptr)) goto disconnected;
            if (avail == 0) { Sleep(50); continue; }
            DWORD r = 0;
            if (!ReadFile(p, rbuf + rlen, (DWORD)(sizeof(rbuf) - 1 - rlen), &r, nullptr)) goto disconnected;
            rlen += r; rbuf[rlen] = 0;
            char* start = rbuf;                          // 3) 按行分发
            char* nl;
            while ((nl = strchr(start, '\n')) != nullptr) {
                *nl = 0;
                HandleClientLine(start);
                start = nl + 1;
            }
            if (start != rbuf) {                         // 4) 保留未完整的一行
                size_t rem = rlen - (size_t)(start - rbuf);
                memmove(rbuf, start, rem);
                rlen = rem; rbuf[rlen] = 0;
            } else rlen = 0;
        }
    disconnected:
        Log("frontend disconnected");
        FlushFileBuffers(p);
        DisconnectNamedPipe(p);
        CloseHandle(p);
    }
    return 0;
}

// ------------------------------------------------------------ 主工作线程 ----
static void Worker() {
    Log("trainer thread started, waiting for mono-2.0-bdwgc.dll ...");
    for (int i = 0; i < 600 && g_run; ++i) {                 // 最长等 60 秒
        if (GetModuleHandleW(L"mono-2.0-bdwgc.dll")) break;
        Sleep(100);
    }

    bool monoOk = false;
    for (int i = 0; i < 240 && g_run && !monoOk; ++i) {
        monoOk = InitMono();
        if (!monoOk) Sleep(250);
    }
    if (!monoOk) {
        Log("FATAL: mono init failed (see log)");
        MessageBoxA(nullptr, "mono 初始化失败,请查看 %TEMP%\\twinshot_trainer.log",
                    "twinshot-trainer", MB_ICONERROR);
        return;
    }

    // 定位阶段:游戏 image + 各作弊,可多轮重试(类/字段可能要等游戏加载)
    // 注意:某作弊 ready 后立即 default 开启,不被其他作弊的重试拖住(修正:此前要等循环整体结束)
    const int kMaxTries = 240;                               // 60s @ 250ms
    for (int t = 0; t < kMaxTries && g_run; ++t) {
        if (!g_gameImage) FindGameImage();
        int pending = 0;
        for (size_t i = 0; i < kCheatsCount; ++i) {
            Cheat& c = kCheats[i];
            if (c.ready) continue;
            if (g_gameImage && c.locate(c)) {
                c.ready = true;
                Log("[cheat] %s ready (hotkey %s)", c.title, HotkeyName(c.hotkey));
                if (c.defaultOn && !c.action) {                  // 动作型无开关状态
                    c.apply(c);
                    c.on = true;
                    Log("[cheat] %s ON (default)", c.title);
                }
            } else {
                pending++;
            }
        }
        if (g_gameImage && pending == 0) break;
        Sleep(250);
    }
    for (size_t i = 0; i < kCheatsCount; ++i) {
        Cheat& c = kCheats[i];
        if (!c.ready)
            Log("[cheat] %s NOT ready after timeout (功能不可用,热键无效)", c.title);
        else if (c.defaultOn && !c.on) {                     // 保险:超时恰好当轮就绪的情况
            c.apply(c);
            c.on = true;
            Log("[cheat] %s ON (default)", c.title);
        }
    }

    // 热键循环(开启期间的 tick 也在同一线程执行,保证 mono 调用线程一致性)
    bool prev[kCheatsCount] = {false};
    while (g_run) {
        for (size_t i = 0; i < kCheatsCount; ++i) {
            Cheat& c = kCheats[i];
            bool down = (GetAsyncKeyState(c.hotkey) & 0x8000) != 0;
            if (down && !prev[i]) {
                if (!c.ready) {
                    Log("[cheat] %s not ready, hotkey ignored", c.title);
                } else if (c.action) {
                    c.arg = 100;                                 // 热键固定默认步长(UI/管道可自定义)
                    c.action(c);                                 // 动作型:一次性触发
                    IpcPushEvent("{\"ev\":\"done\",\"id\":\"%s\",\"ok\":true}", c.id);
                } else {
                    c.on = !c.on;
                    if (c.on) c.apply(c); else c.revert(c);
                    Log("[cheat] %s %s", c.title, c.on ? "ON" : "OFF");
                    IpcPushState(c, true, nullptr);              // 热键变化同步给前端
                }
            }
            prev[i] = down;
            if (c.on && c.ready && c.tick) c.tick(c);
        }
        ProcessIpcCommands();                                    // 消费前端命令(本线程执行)
        Sleep(30);
    }

    // 退出前全部还原
    for (size_t i = 0; i < kCheatsCount; ++i) {
        Cheat& c = kCheats[i];
        if (c.on) { c.revert(c); c.on = false; }
    }
    Log("trainer thread exit");
}

static DWORD WINAPI WorkerEntry(LPVOID) { Worker(); return 0; }

// ------------------------------------------------- 进程内 UI(B1)----------
// 独立 Win32 顶层窗口(不 hook 游戏渲染),Insert 显隐,关闭按钮 = 隐藏。
// UI 线程只允许调 TrainerApi(快照/入队),绝不直接碰 mono ——
// 与管道前端遵守同一线程契约(命令由 trainer 线程异步执行)。
// 风险提示:UI 崩溃即游戏进程崩溃,故 UI 侧代码保持极简、零外部依赖。
static HMODULE g_hSelf = nullptr;                      // DllMain 保存(UI 线程用)

static HWND g_uiCtrl[TRAINER_API_MAX_CHEATS];          // 每作弊一行:复选框/按钮
static TrainerCheatInfo g_uiMap[TRAINER_API_MAX_CHEATS];
static int g_uiCount = 0;

#define UI_EDIT_ID   99                                // 数值输入框(动作参数)
#define UI_CTRL_BASE 1000                              // 控件 ID = UI_CTRL_BASE + i
#define UI_TIMER_ID  1

static void UiUtf8ToWide(const char* s, wchar_t* out, size_t outChars) {
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)outChars) <= 0 && outChars)
        out[0] = 0;
}

static BOOL CALLBACK UiSetFont(HWND child, LPARAM font) {
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

static void UiBuildControls(HWND h) {
    // 客户区:每作弊一行 34px,底部一行动作数值输入;宽 250
    RECT rc = {0, 0, 250, 10 + g_uiCount * 34 + 48};
    AdjustWindowRect(&rc, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    SetWindowPos(h, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);

    wchar_t wlabel[160];
    for (int i = 0; i < g_uiCount; ++i) {
        const TrainerCheatInfo& ci = g_uiMap[i];
        int y = 10 + i * 34;
        char label[128];
        snprintf(label, sizeof(label), "%s [%s]", ci.name, HotkeyName(ci.hotkey));
        UiUtf8ToWide(label, wlabel, 160);
        CreateWindowW(L"STATIC", wlabel, WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                      10, y + 5, 105, 20, h, nullptr, g_hSelf, nullptr);
        bool toggle = (strcmp(ci.type, "toggle") == 0);
        g_uiCtrl[i] = CreateWindowW(L"BUTTON", toggle ? L"启用" : L"执行",
            WS_CHILD | WS_VISIBLE | (toggle ? BS_AUTOCHECKBOX : BS_PUSHBUTTON),
            120, y, 110, 24, h, (HMENU)(INT_PTR)(UI_CTRL_BASE + i), g_hSelf, nullptr);
    }
    int editY = 10 + g_uiCount * 34 + 4;
    CreateWindowW(L"STATIC", L"动作数值:", WS_CHILD | WS_VISIBLE,
                  10, editY + 5, 60, 20, h, nullptr, g_hSelf, nullptr);
    CreateWindowW(L"EDIT", L"100",
                  WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
                  75, editY, 60, 24, h, (HMENU)(INT_PTR)UI_EDIT_ID, g_hSelf, nullptr);
    CreateWindowW(L"STATIC", L"Insert 显/隐", WS_CHILD | WS_VISIBLE,
                  150, editY + 5, 90, 20, h, nullptr, g_hSelf, nullptr);
    EnumChildWindows(h, UiSetFont, (LPARAM)GetStockObject(DEFAULT_GUI_FONT));
}

static LRESULT CALLBACK UiWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        // 快照建控件表(name/type 是静态串,随时可读;ready/on 由定时器刷新)
        TrainerCheatInfo snap[TRAINER_API_MAX_CHEATS];
        int n = 0;
        if (TrainerApi_GetSnapshot(snap, TRAINER_API_MAX_CHEATS, &n) && n > 0) {
            g_uiCount = n;
            for (int i = 0; i < n; ++i) g_uiMap[i] = snap[i];
        }
        UiBuildControls(h);
        SetTimer(h, UI_TIMER_ID, 100, nullptr);
        return 0;
    }
    case WM_COMMAND: {
        int id = (int)LOWORD(wp);
        if (HIWORD(wp) != BN_CLICKED ||
            id < UI_CTRL_BASE || id >= UI_CTRL_BASE + g_uiCount)
            return 0;
        int i = id - UI_CTRL_BASE;
        const TrainerCheatInfo& ci = g_uiMap[i];
        if (strcmp(ci.type, "toggle") == 0) {
            bool on = SendMessageW(g_uiCtrl[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
            TrainerApi_Set(ci.id, on);
        } else {
            wchar_t buf[32] = {0};
            GetDlgItemTextW(h, UI_EDIT_ID, buf, 32);
            int amount = (int)wcstol(buf, nullptr, 10);
            if (amount <= 0) amount = 100;               // 与热键默认步长一致
            TrainerApi_Act(ci.id, amount);
        }
        return 0;
    }
    case WM_TIMER: {
        static bool prevIns = false;                     // Insert 边沿检测
        bool ins = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (ins && !prevIns) {
            bool vis = IsWindowVisible(h);
            ShowWindow(h, vis ? SW_HIDE : SW_SHOW);
            if (!vis) SetForegroundWindow(h);
        }
        prevIns = ins;
        TrainerCheatInfo snap[TRAINER_API_MAX_CHEATS];   // 快照同步:热键/管道改动回显
        int n = 0;
        if (TrainerApi_GetSnapshot(snap, TRAINER_API_MAX_CHEATS, &n)) {
            for (int i = 0; i < n && i < g_uiCount; ++i) {
                if (strcmp(g_uiMap[i].id, snap[i].id) != 0) continue;
                EnableWindow(g_uiCtrl[i], snap[i].ready);
                if (strcmp(snap[i].type, "toggle") == 0)
                    SendMessageW(g_uiCtrl[i], BM_SETCHECK,
                                 snap[i].on ? BST_CHECKED : BST_UNCHECKED, 0);
            }
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);                          // 关闭 = 隐藏,游戏继续
        return 0;
    case WM_DESTROY:
        KillTimer(h, UI_TIMER_ID);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static DWORD WINAPI UiThreadEntry(LPVOID) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = UiWndProc;
    wc.hInstance     = g_hSelf;
    wc.lpszClassName = L"TwinshotTrainerUI";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return 0;

    HWND h = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Twin Shot Trainer",
                             WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, 250, 120,
                             nullptr, nullptr, g_hSelf, nullptr);
    if (!h) return 0;
    ShowWindow(h, SW_SHOW);                              // 注入即弹窗
    while (g_run) {
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(10);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_ipcCs);
        g_hSelf = hModule;
        g_run = true;
        CreateThread(nullptr, 0, WorkerEntry, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, PipeThreadEntry, nullptr, 0, nullptr);
        CreateThread(nullptr, 0, UiThreadEntry, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        g_run = false;   // 工作线程检测后退出并还原;强制卸载时补丁保留,属预期
        break;
    }
    return TRUE;
}

// 无导出符号时部分注入器会拒绝加载;放一个空导出保底
extern "C" __declspec(dllexport) void TwinshotTrainerDummy() {}
