// app.h —— 外置前端的状态、窗口与业务逻辑
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>

#include <string>
#include <vector>

#include "pipe_client.h"
#include "ui_about.h"

constexpr wchar_t kWindowClass[] = L"TwinShotTrainerFrontend";
constexpr wchar_t kWindowTitle[] = L"Twin Shot Cheats Deluxe";
constexpr UINT WM_APP_SHOW = WM_APP + 2;

enum class ConnState {
    Disconnected,
    Connected,
};

enum class InjectPhase {
    Idle,
    Running,
    WaitingPipe,
    Failed,
};

struct CheatView {
    std::string id;          // ASCII，协议用
    std::wstring name;       // 显示名（已从 UTF-8 转换）
    std::string type;        // "toggle" / "action"
    std::wstring hotkey;     // "F3" 等

    bool on = false;
    bool ready = false;

    bool pending = false;
    bool pendingOn = false;
    bool beforeOn = false;
    ULONGLONG pendingAt = 0;
};

struct AppState {
    ConnState conn = ConnState::Disconnected;
    bool demo = false;
    InjectPhase injectPhase = InjectPhase::Idle;
    std::wstring injectMessage;

    std::vector<CheatView> cheats;

    int amount = 100;
    std::wstring amountText = L"100";
    bool editingAmount = false;
    size_t caret = 3;

    std::wstring status = L"等待游戏 / 等待注入";
    std::wstring toast;
    ULONGLONG toastUntil = 0;

    HWND gameHwnd = nullptr;
    int dpi = 96;
    bool panelActive = false;
    bool trackingMouse = false;

    int hover = 0;
    int pressed = 0;
};

// 命中测试编码
constexpr int HIT_NONE = 0;
constexpr int HIT_AMOUNT = 1;
constexpr int HIT_MINUS = 2;
constexpr int HIT_PLUS = 3;
constexpr int HIT_EXIT = 4;
constexpr int HIT_HEADER = 5;
constexpr int HIT_ABOUT = 6;
constexpr int HIT_INJECT = 7;
constexpr int HIT_ROW_BASE = 1000;
constexpr int HIT_CTRL_BASE = 2000;

inline int RowHit(int row) { return HIT_ROW_BASE + row; }
inline int CtrlHit(int row) { return HIT_CTRL_BASE + row; }
inline bool IsRowHit(int hit) { return hit >= HIT_ROW_BASE && hit < HIT_CTRL_BASE; }
inline bool IsCtrlHit(int hit) { return hit >= HIT_CTRL_BASE && hit < HIT_CTRL_BASE + 1000; }
inline int HitRow(int hit) {
    if (IsRowHit(hit)) return hit - HIT_ROW_BASE;
    if (IsCtrlHit(hit)) return hit - HIT_CTRL_BASE;
    return -1;
}

struct RowLayout {
    RECT row{};
    RECT name{};
    RECT hotkey{};
    RECT control{};
    RECT icon{};   // 名称前图标字符位（MyFontIcon/PUA；该项无图标时留空矩形）
};

struct Layout {
    int scale = 1;
    int width = 380;
    int height = 300;

    RECT client{};
    RECT header{};
    RECT status{};
    RECT toast{};
    RECT list{};
    RECT empty{};
    RECT injectButton{};

    std::vector<RowLayout> rows;

    RECT amountLabel{};
    RECT amountBox{};
    RECT minus{};
    RECT plus{};

    RECT footer{};
    RECT aboutButton{};
    RECT exitButton{};
};

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Init(HINSTANCE hinst, bool demo);
    int Run();
    void RequestExit();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK AboutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate();
    void OnPaint();
    void OnTimer();
    void OnPipeMessage(PipeMsg* msg);
    void OnMouseMove(int x, int y);
    void OnMouseLeave();
    void OnLButtonDown(int x, int y);
    void OnLButtonUp(int x, int y);
    void OnKeyDown(WPARAM key);
    void OnChar(WPARAM ch);
    void OnDpiChanged(int newDpi, const RECT* suggested);

    void UpdateFonts();
    void LoadFonts();
    void UnloadFonts();
    void RebuildSize();
    void UpdateStatus();
    void UpdateHover(int x, int y);
    void SetToast(std::wstring text, UINT ms = 2600);
    void RegisterInsert();
    void SeedDemo();

    void ShowPanel();
    void HidePanel();
    void TogglePanel();
    void ShowAbout();
    void StartInjection();
    void PollInjection();
    void CloseInjectorHandle();
    void ApplyDarkTitleBar();

    // “关于”窗口（无边框像素风，与主面板共用绘制原语）
    void OnAboutPaint();
    void OnAboutMouseMove(int x, int y);
    void OnAboutMouseLeave();
    void OnAboutLButtonDown(int x, int y);
    void OnAboutLButtonUp(int x, int y);
    void OnAboutKeyDown(WPARAM key);
    void UpdateAboutSize();

    void HandleLine(const std::string& line);
    void ApplyList(const std::string& json);
    void ApplyState(const std::string& json);
    void ApplyDone(const std::string& json);

    void SendList();
    void SendPing();
    void SendSet(const CheatView& cheat, bool on);
    void SendAction(const CheatView& cheat);

    int HitTest(int x, int y) const;
    void ExecuteHit(int hit);
    void CommitAmount();
    int EffectiveAmount() const;
    void AdjustAmount(int delta);

    HWND hwnd_ = nullptr;
    HWND about_ = nullptr;
    HINSTANCE hinst_ = nullptr;
    AppState state_;
    Layout layout_;
    PipeClient pipe_;

    // “关于”窗口状态
    AboutLayout aboutLayout_;
    int aboutHover_ = 0;
    int aboutPressed_ = 0;
    bool aboutTracking_ = false;
    bool aboutDragging_ = false;
    POINT aboutDragCursor_{};
    RECT aboutDragWindow_{};

    HFONT font_ = nullptr;
    HFONT titleFont_ = nullptr;        // 标题（MyFontHeading，原生 16px，主面板与“关于”共用）
    HFONT smallFont_ = nullptr;
    HFONT iconFont_ = nullptr;         // 作弊行图标（MyFontIcon，PUA U+E001-E003）
    HANDLE fontMem_[3] = {nullptr, nullptr, nullptr};
    bool fontBodyLoaded_ = false;
    bool fontTitleLoaded_ = false;
    bool fontIconLoaded_ = false;

    bool dragging_ = false;
    POINT dragCursor_{};
    RECT dragWindow_{};

    UINT_PTR timerId_ = 0;
    bool insertRegistered_ = false;
    bool insertPrev_ = false;
    bool exiting_ = false;

    ULONGLONG lastPing_ = 0;
    ULONGLONG lastList_ = 0;
    ULONGLONG lastGameFind_ = 0;
    ULONGLONG injectWaitUntil_ = 0;
    HANDLE injectorProcess_ = nullptr;
    bool everList_ = false;
};
