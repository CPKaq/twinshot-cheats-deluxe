// app.cpp —— 外置前端主逻辑：窗口、状态、管道事件、焦点与布局
#include "app.h"

#include "focus.h"
#include "ui.h"
#include "ui_common.h"
#include "resource.h"

#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

namespace {

constexpr int ID_TIMER_UI = 1;
constexpr int ID_HOTKEY_INSERT = 1;
constexpr wchar_t kAboutClass[] = L"TwinShotTrainerAbout";

void Log(const char* fmt, ...) {
    char temp[MAX_PATH]{};
    if (!GetEnvironmentVariableA("TEMP", temp, MAX_PATH)) return;
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s\\twinshot_frontend.log", temp);
    FILE* f = std::fopen(path, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

UINT WindowDpi(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn) {
        UINT dpi = fn(hwnd);
        if (dpi) return dpi;
    }
    return 96;
}

int PixelScale(int dpi) {
    if (dpi >= 240) return 3;
    if (dpi >= 144) return 2;
    return 1;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

void SkipWs(const std::string& s, size_t& p) {
    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
}

bool FindValue(const std::string& s, const char* key, size_t& p) {
    std::string pat = "\"";
    pat += key;
    pat += "\"";
    size_t q = s.find(pat);
    if (q == std::string::npos) return false;
    q += pat.size();
    SkipWs(s, q);
    if (q >= s.size() || s[q] != ':') return false;
    ++q;
    SkipWs(s, q);
    p = q;
    return p < s.size();
}

bool JsonString(const std::string& s, const char* key, std::string& out) {
    size_t p = 0;
    if (!FindValue(s, key, p) || s[p] != '"') return false;
    ++p;
    out.clear();
    while (p < s.size()) {
        char c = s[p++];
        if (c == '"') return true;
        if (c == '\\' && p < s.size()) {
            char e = s[p++];
            switch (e) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            default: out.push_back(e); break; // 协议内不依赖 \uXXXX
            }
        } else {
            out.push_back(c);
        }
    }
    return false;
}

bool JsonBool(const std::string& s, const char* key, bool def) {
    size_t p = 0;
    if (!FindValue(s, key, p)) return def;
    if (s.compare(p, 4, "true") == 0) return true;
    if (s.compare(p, 5, "false") == 0) return false;
    return def;
}

std::vector<CheatView> ParseCheats(const std::string& json) {
    std::vector<CheatView> out;
    size_t key = json.find("\"cheats\"");
    if (key == std::string::npos) return out;
    size_t arr = json.find('[', key);
    if (arr == std::string::npos) return out;

    size_t i = arr + 1;
    while (i < json.size()) {
        while (i < json.size()) {
            char c = json[i];
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
                ++i;
            } else {
                break;
            }
        }
        if (i >= json.size() || json[i] == ']') break;
        if (json[i] != '{') {
            ++i;
            continue;
        }

        size_t start = i;
        bool inString = false;
        int depth = 0;
        size_t j = i;
        for (; j < json.size(); ++j) {
            char c = json[j];
            if (inString) {
                if (c == '\\') {
                    ++j;
                } else if (c == '"') {
                    inString = false;
                }
            } else {
                if (c == '"') {
                    inString = true;
                } else if (c == '{') {
                    ++depth;
                } else if (c == '}') {
                    --depth;
                    if (depth == 0) {
                        ++j;
                        break;
                    }
                }
            }
        }
        if (j > json.size()) break;

        std::string obj = json.substr(start, j - start);
        CheatView c;
        JsonString(obj, "id", c.id);
        std::string name, type, hotkey;
        JsonString(obj, "name", name);
        JsonString(obj, "type", type);
        JsonString(obj, "hotkey", hotkey);
        c.name = Utf8ToWide(name);
        c.type = type;
        c.hotkey = Utf8ToWide(hotkey);
        c.on = JsonBool(obj, "on", false);
        c.ready = JsonBool(obj, "ready", false);
        if (!c.id.empty()) out.push_back(std::move(c));
        i = j;
    }
    return out;
}

CheatView* FindCheat(std::vector<CheatView>& cheats, const std::string& id) {
    for (auto& c : cheats) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

bool IsClickableHit(int hit) {
    return hit == HIT_AMOUNT || hit == HIT_MINUS || hit == HIT_PLUS ||
           hit == HIT_EXIT || hit == HIT_ABOUT || hit == HIT_INJECT || IsCtrlHit(hit);
}

} // namespace

App::~App() {
    pipe_.Stop();
    if (insertRegistered_ && hwnd_) UnregisterHotKey(hwnd_, ID_HOTKEY_INSERT);
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);
    if (smallFont_) DeleteObject(smallFont_);
    if (aboutTitleFont_) DeleteObject(aboutTitleFont_);
    if (iconFont_) DeleteObject(iconFont_);
    UnloadFonts();
}

bool App::Init(HINSTANCE hinst, bool demo) {
    hinst_ = hinst;
    state_.demo = demo;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kWindowClass;
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    Layout initial = ComputeLayout(96, demo ? 3 : 0);
    RECT wr{0, 0, initial.width, initial.height};
    const DWORD style = WS_POPUP;
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    AdjustWindowRectEx(&wr, style, FALSE, exStyle);

    // 默认位置：主显示器工作区左上角 (200,100)（“关于”窗口在此基础上偏移 (100,100)）
    hwnd_ = CreateWindowExW(
        exStyle, kWindowClass, kWindowTitle, style,
        200 - wr.left, 100 - wr.top, wr.right - wr.left, wr.bottom - wr.top,
        nullptr, nullptr, hinst, this);
    if (!hwnd_) return false;

    ShowPanel();
    UpdateWindow(hwnd_);
    return true;
}

int App::Run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::RequestExit() {
    exiting_ = true;
    CloseInjectorHandle();
    if (about_ && IsWindow(about_)) {
        DestroyWindow(about_);
        about_ = nullptr;
    }
    if (hwnd_ && IsWindow(hwnd_)) {
        if (IsWindowVisible(hwnd_)) HidePanel();
        DestroyWindow(hwnd_);
    }
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) {
        self->hwnd_ = hwnd;
        return self->HandleMessage(msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK App::AboutWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_ERASEBKGND:
        // 背景在 WM_PAINT 的缓冲 DC 中绘制，返回 1 防闪烁
        return 1;
    case WM_PAINT:
        if (self) {
            self->OnAboutPaint();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (self) {
            self->OnAboutMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        }
        break;
    case WM_MOUSELEAVE:
        if (self) {
            self->OnAboutMouseLeave();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (self) {
            self->aboutDragging_ = false;
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
        if (self) {
            self->OnAboutLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (self) {
            self->OnAboutLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (self) {
            self->OnAboutKeyDown(wp);
            return 0;
        }
        break;
    case WM_ACTIVATE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        if (self) {
            const int dpi = HIWORD(wp);
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            if (dpi && dpi != self->state_.dpi) {
                self->state_.dpi = dpi;
                self->UpdateFonts();
            }
            if (suggested) {
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            self->UpdateAboutSize();
            if (self->about_) InvalidateRect(self->about_, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && self) {
            const auto pos = GetMessagePos();
            POINT pt{GET_X_LPARAM(pos), GET_Y_LPARAM(pos)};
            ScreenToClient(hwnd, &pt);
            const int hit = HitTestAbout(self->aboutLayout_, pt.x, pt.y);
            SetCursor(LoadCursorW(nullptr,
                hit == HIT_ABOUT_HEADER ? IDC_SIZEALL
                : hit == HIT_ABOUT_CLOSE ? IDC_HAND
                : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_NCDESTROY:
        if (self) self->about_ = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;
    case WM_PAINT:
        OnPaint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (wp == ID_TIMER_UI) OnTimer();
        return 0;
    case WM_HOTKEY:
        if (wp == ID_HOTKEY_INSERT) TogglePanel();
        return 0;
    case WM_APP_PIPE:
        OnPipeMessage(reinterpret_cast<PipeMsg*>(lp));
        return 0;
    case WM_APP_SHOW:
        ShowPanel();
        return 0;
    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSELEAVE:
        OnMouseLeave();
        return 0;
    case WM_CAPTURECHANGED:
        dragging_ = false;
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_KEYDOWN:
        OnKeyDown(wp);
        return 0;
    case WM_CHAR:
        OnChar(wp);
        return 0;
    case WM_ACTIVATE:
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED: {
        int dpi = HIWORD(wp);
        OnDpiChanged(dpi ? dpi : 96, reinterpret_cast<const RECT*>(lp));
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, state_.hover == HIT_HEADER
                ? IDC_SIZEALL
                : (IsClickableHit(state_.hover) ? IDC_HAND : IDC_ARROW)));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        RequestExit(); // 无系统标题栏；Alt+F4 等同“退出”
        return 0;
    case WM_DESTROY:
        if (timerId_) {
            KillTimer(hwnd_, timerId_);
            timerId_ = 0;
        }
        if (about_ && IsWindow(about_)) {
            DestroyWindow(about_);
            about_ = nullptr;
        }
        CloseInjectorHandle();
        if (insertRegistered_) {
            UnregisterHotKey(hwnd_, ID_HOTKEY_INSERT);
            insertRegistered_ = false;
        }
        pipe_.Stop();
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void App::OnCreate() {
    state_.dpi = WindowDpi(hwnd_);
    LoadFonts();
    UpdateFonts();
    ApplyDarkTitleBar();
    if (state_.demo) SeedDemo();
    RebuildSize();
    RegisterInsert();
    timerId_ = SetTimer(hwnd_, ID_TIMER_UI, 100, nullptr);
    if (!state_.demo) pipe_.Start(hwnd_, WM_APP_PIPE);
    UpdateStatus();
}

void App::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int w = std::max<int>(1, rc.right - rc.left);
    int h = std::max<int>(1, rc.bottom - rc.top);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

    HBRUSH bg = CreateSolidBrush(RGB(20, 22, 28));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    layout_ = ComputeLayout(state_.dpi, static_cast<int>(state_.cheats.size()));
    PaintUi(mem, state_, layout_, font_, titleFont_, smallFont_, iconFont_);
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

void App::OnTimer() {
    const ULONGLONG now = GetTickCount64();

    if (!state_.gameHwnd || !IsWindow(state_.gameHwnd)) {
        if (now - lastGameFind_ >= 1000) {
            state_.gameHwnd = focus::FindGameWindow();
            lastGameFind_ = now;
        }
    }

    PollInjection();

    if (!state_.demo && state_.conn == ConnState::Connected) {
        if (lastPing_ == 0 || now - lastPing_ >= 1000) SendPing();
        if (lastList_ == 0 || now - lastList_ >= 500) SendList();
    }

    for (auto& c : state_.cheats) {
        if (!c.pending) continue;
        if (now - c.pendingAt > 1800) {
            c.pending = false;
            if (c.type == "toggle") c.on = c.beforeOn;
            SetToast(L"命令超时，已重新同步状态");
            SendList();
        }
    }

    if (!state_.toast.empty() && state_.toastUntil && now >= state_.toastUntil) {
        state_.toast.clear();
        state_.toastUntil = 0;
    }

    if (!insertRegistered_) {
        bool down = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        if (down && !insertPrev_) TogglePanel();
        insertPrev_ = down;
    }

    UpdateStatus();
    if (IsWindowVisible(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnPipeMessage(PipeMsg* msgPtr) {
    std::unique_ptr<PipeMsg> msg(msgPtr);
    if (!msg) return;
    if (msg->type != PipeMsg::Line) {
        Log("pipe msg type=%d", static_cast<int>(msg->type));
    }

    switch (msg->type) {
    case PipeMsg::Connected:
        state_.conn = ConnState::Connected;
        everList_ = false;
        lastPing_ = 0;
        lastList_ = 0;
        CloseInjectorHandle();
        state_.injectPhase = InjectPhase::Idle;
        state_.injectMessage.clear();
        for (auto& c : state_.cheats) c.pending = false;
        SetToast(L"已连接 trainer，正在初始化…");
        SendPing();
        SendList();
        break;
    case PipeMsg::Disconnected:
        state_.conn = ConnState::Disconnected;
        everList_ = false;
        CloseInjectorHandle();
        state_.injectPhase = InjectPhase::Idle;
        state_.injectMessage.clear();
        for (auto& c : state_.cheats) c.pending = false;
        SetToast(L"连接断开，等待重新注入 / 启动游戏");
        break;
    case PipeMsg::Line:
        HandleLine(msg->text);
        break;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::HandleLine(const std::string& line) {
    std::string ev;
    if (!JsonString(line, "ev", ev)) return;

    if (ev == "list") {
        ApplyList(line);
    } else if (ev == "state") {
        ApplyState(line);
    } else if (ev == "done") {
        ApplyDone(line);
    }
    // pong：只作为探活，不需要额外处理。
}

void App::ApplyList(const std::string& json) {
    auto fresh = ParseCheats(json);
    const bool countChanged = fresh.size() != state_.cheats.size();

    if (!countChanged) {
        for (size_t i = 0; i < fresh.size(); ++i) {
            if (fresh[i].id != state_.cheats[i].id) {
                // 顺序/集合改变也按重建处理。
                state_.cheats = std::move(fresh);
                RebuildSize();
                everList_ = true;
                return;
            }
        }
    }

    // 保留 pending 项的本地乐观状态，等 state 事件落定。
    for (auto& n : fresh) {
        for (const auto& old : state_.cheats) {
            if (old.id != n.id) continue;
            if (old.pending) {
                n.pending = true;
                n.pendingOn = old.pendingOn;
                n.beforeOn = old.beforeOn;
                n.pendingAt = old.pendingAt;
                n.on = old.on;
            }
            break;
        }
    }

    state_.cheats = std::move(fresh);
    state_.conn = ConnState::Connected;
    everList_ = true;
    if (countChanged) RebuildSize();
    UpdateStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ApplyState(const std::string& json) {
    std::string id;
    if (!JsonString(json, "id", id)) return;
    CheatView* c = FindCheat(state_.cheats, id);
    if (!c) return;

    const bool ok = JsonBool(json, "ok", false);
    c->pending = false;
    if (ok) {
        c->on = JsonBool(json, "on", c->on);
    } else {
        c->on = c->beforeOn;
        std::string err;
        JsonString(json, "err", err);
        SetToast(L"操作失败：" + Utf8ToWide(err.empty() ? "unknown error" : err));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::ApplyDone(const std::string& json) {
    std::string id;
    if (!JsonString(json, "id", id)) return;
    CheatView* c = FindCheat(state_.cheats, id);
    if (!c) return;

    c->pending = false;
    const bool ok = JsonBool(json, "ok", false);
    if (ok) {
        SetToast(L"动作已执行（+ " + std::to_wstring(EffectiveAmount()) + L"）");
    } else {
        std::string err;
        JsonString(json, "err", err);
        SetToast(L"动作失败：" + Utf8ToWide(err.empty() ? "unknown error" : err));
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::SendList() {
    if (pipe_.Send("{\"cmd\":\"list\"}\n")) lastList_ = GetTickCount64();
}

void App::SendPing() {
    if (pipe_.Send("{\"cmd\":\"ping\"}\n")) lastPing_ = GetTickCount64();
}

void App::SendSet(const CheatView& cheat, bool on) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "{\"cmd\":\"set\",\"id\":\"%s\",\"on\":%s}\n",
                  cheat.id.c_str(), on ? "true" : "false");
    pipe_.Send(buf);
}

void App::SendAction(const CheatView& cheat) {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "{\"cmd\":\"act\",\"id\":\"%s\",\"amount\":%d}\n",
                  cheat.id.c_str(), EffectiveAmount());
    pipe_.Send(buf);
}

void App::ShowPanel() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;

    state_.gameHwnd = focus::FindGameWindow();
    ShowWindow(hwnd_, SW_SHOW);
    const bool ok = focus::ActivateWindow(hwnd_, true);
    Log("ShowPanel: game=%p activated=%d", static_cast<void*>(state_.gameHwnd), ok ? 1 : 0);
    if (!ok) {
        SetToast(L"窗口已显示，但未能自动激活；点击面板以暂停游戏", 4200);
    }
    UpdateStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::HidePanel() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;

    state_.editingAmount = false;
    ShowWindow(hwnd_, SW_HIDE);
    if (about_ && IsWindow(about_)) ShowWindow(about_, SW_HIDE);

    if (!state_.gameHwnd || !IsWindow(state_.gameHwnd)) {
        state_.gameHwnd = focus::FindGameWindow();
    }
    if (state_.gameHwnd) {
        if (!focus::RestoreGame(state_.gameHwnd)) {
            SetToast(L"已隐藏，但未能自动恢复游戏焦点；请点击游戏窗口", 4200);
        }
    }
    Log("HidePanel: game=%p", static_cast<void*>(state_.gameHwnd));
    UpdateStatus();
}

void App::TogglePanel() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;

    if (IsWindowVisible(hwnd_)) {
        HidePanel();
    } else {
        ShowPanel();
    }
}

void App::ShowAbout() {
    if (about_ && IsWindow(about_)) {
        ShowWindow(about_, SW_SHOW);
        SetForegroundWindow(about_);
        return;
    }

    if (!hinst_) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::AboutWndProc;
    wc.hInstance = hinst_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kAboutClass;
    wc.hbrBackground = nullptr;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;

    // 与主面板同款的无边框置顶窗口；客户区尺寸由关于页布局决定
    aboutLayout_ = ComputeAboutLayout(state_.dpi);
    const DWORD style = WS_POPUP;
    const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    RECT rc{0, 0, aboutLayout_.width, aboutLayout_.height};
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    RECT mainRc{};
    GetWindowRect(hwnd_, &mainRc);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    // “关于”在主窗口基础上偏移 (100,100)，避免与主面板重叠
    // （主窗口默认位于主显示器左上角 (200,100)，“关于”通常落在 (300,200)）
    int x = mainRc.left + 100;
    int y = mainRc.top + 100;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    if (x + w > work.right) x = work.right - w;
    if (y + h > work.bottom) y = work.bottom - h;

    about_ = CreateWindowExW(exStyle, kAboutClass, L"关于", style,
                             x, y, w, h, hwnd_, nullptr, hinst_, this);
    if (!about_) return;
    ShowWindow(about_, SW_SHOW);
    UpdateWindow(about_);
    SetForegroundWindow(about_);
}

void App::OnAboutPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(about_, &ps);
    RECT rc{};
    GetClientRect(about_, &rc);
    int w = std::max<int>(1, rc.right - rc.left);
    int h = std::max<int>(1, rc.bottom - rc.top);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(mem, bmp));

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    PaintAbout(mem, aboutLayout_, font_, aboutTitleFont_, smallFont_,
               aboutHover_, aboutPressed_);
    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(about_, &ps);
}

void App::UpdateAboutSize() {
    if (!about_ || !IsWindow(about_)) return;

    aboutLayout_ = ComputeAboutLayout(state_.dpi);
    RECT rc{0, 0, aboutLayout_.width, aboutLayout_.height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(about_, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(about_, GWL_EXSTYLE));
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    RECT wr{};
    GetWindowRect(about_, &wr);
    SetWindowPos(about_, nullptr, wr.left, wr.top,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    InvalidateRect(about_, nullptr, FALSE);
}

void App::OnAboutMouseMove(int x, int y) {
    if (aboutDragging_) {
        POINT pt{};
        GetCursorPos(&pt);
        const int dx = pt.x - aboutDragCursor_.x;
        const int dy = pt.y - aboutDragCursor_.y;
        SetWindowPos(about_, nullptr, aboutDragWindow_.left + dx, aboutDragWindow_.top + dy,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    const int hit = HitTestAbout(aboutLayout_, x, y);
    if (hit != aboutHover_) {
        aboutHover_ = hit;
        InvalidateRect(about_, nullptr, FALSE);
    }

    if (!aboutTracking_) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = about_;
        if (TrackMouseEvent(&tme)) aboutTracking_ = true;
    }
}

void App::OnAboutMouseLeave() {
    if (aboutDragging_) return;
    aboutTracking_ = false;
    if (aboutHover_) {
        aboutHover_ = HIT_NONE;
        InvalidateRect(about_, nullptr, FALSE);
    }
}

void App::OnAboutLButtonDown(int x, int y) {
    SetFocus(about_);
    POINT pt{x, y};
    const int hit = HitTestAbout(aboutLayout_, x, y);
    if (PtInRect(&aboutLayout_.header, pt) && hit != HIT_ABOUT_CLOSE) {
        // 无边框窗口：标题区域拖动（× 按钮优先于拖动）
        aboutDragging_ = true;
        GetCursorPos(&aboutDragCursor_);
        GetWindowRect(about_, &aboutDragWindow_);
        SetCapture(about_);
        return;
    }
    aboutPressed_ = hit;
    InvalidateRect(about_, nullptr, FALSE);
}

void App::OnAboutLButtonUp(int x, int y) {
    if (aboutDragging_) {
        aboutDragging_ = false;
        if (GetCapture() == about_) ReleaseCapture();
        return;
    }
    const int hit = HitTestAbout(aboutLayout_, x, y);
    const int pressed = aboutPressed_;
    aboutPressed_ = HIT_NONE;
    if (pressed != HIT_NONE && pressed == hit && hit == HIT_ABOUT_CLOSE) {
        DestroyWindow(about_);
        return;
    }
    InvalidateRect(about_, nullptr, FALSE);
}

void App::OnAboutKeyDown(WPARAM key) {
    if (key == VK_ESCAPE) {
        DestroyWindow(about_);
    }
}

void App::CloseInjectorHandle() {
    if (injectorProcess_) {
        CloseHandle(injectorProcess_);
        injectorProcess_ = nullptr;
    }
}

void App::StartInjection() {
    if (pipe_.connected()) return;
    if (state_.injectPhase == InjectPhase::Running ||
        state_.injectPhase == InjectPhase::WaitingPipe) {
        return;
    }

    wchar_t self[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) {
        state_.injectPhase = InjectPhase::Failed;
        state_.injectMessage = L"无法定位 frontend.exe 路径";
        return;
    }
    std::wstring dir = self;
    size_t slash = dir.find_last_of(L'\\');
    if (slash == std::wstring::npos) {
        state_.injectPhase = InjectPhase::Failed;
        state_.injectMessage = L"无法定位 injector.exe";
        return;
    }
    dir.resize(slash);
    const std::wstring injector = dir + L"\\injector.exe";
    if (GetFileAttributesW(injector.c_str()) == INVALID_FILE_ATTRIBUTES) {
        state_.injectPhase = InjectPhase::Failed;
        state_.injectMessage = L"未找到 injector.exe（应与 frontend.exe 同目录）";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.hwnd = hwnd_;
    sei.lpVerb = L"runas";
    sei.lpFile = injector.c_str();
    sei.lpDirectory = dir.c_str();
    sei.nShow = SW_HIDE;

    CloseInjectorHandle();
    state_.injectMessage.clear();
    if (!ShellExecuteExW(&sei)) {
        const DWORD err = GetLastError();
        state_.injectPhase = InjectPhase::Failed;
        state_.injectMessage = (err == ERROR_CANCELLED)
            ? L"注入失败：已取消管理员授权"
            : L"注入失败：无法启动 injector.exe";
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }

    injectorProcess_ = sei.hProcess;
    state_.injectPhase = InjectPhase::Running;
    state_.injectMessage = L"正在注入…（injector 最多等待约 60 秒）";
    if (!injectorProcess_) {
        state_.injectPhase = InjectPhase::WaitingPipe;
        injectWaitUntil_ = GetTickCount64() + 5000;
        state_.injectMessage = L"注入已执行，正在等待管道连接…";
    }
    UpdateStatus();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::PollInjection() {
    if (state_.conn == ConnState::Connected) {
        CloseInjectorHandle();
        if (state_.injectPhase != InjectPhase::Idle) {
            state_.injectPhase = InjectPhase::Idle;
            state_.injectMessage.clear();
        }
        return;
    }

    const ULONGLONG now = GetTickCount64();
    if (state_.injectPhase == InjectPhase::Running) {
        if (!injectorProcess_) {
            state_.injectPhase = InjectPhase::WaitingPipe;
            injectWaitUntil_ = now + 5000;
            state_.injectMessage = L"注入已执行，正在等待管道连接…";
            return;
        }

        DWORD wait = WaitForSingleObject(injectorProcess_, 0);
        if (wait == WAIT_OBJECT_0) {
            DWORD code = 1;
            GetExitCodeProcess(injectorProcess_, &code);
            CloseInjectorHandle();
            if (code == 0) {
                state_.injectPhase = InjectPhase::WaitingPipe;
                injectWaitUntil_ = now + 5000;
                state_.injectMessage = L"注入完成，正在等待 trainer 连接…";
            } else {
                state_.injectPhase = InjectPhase::Failed;
                state_.injectMessage = L"注入失败：injector 退出码 " + std::to_wstring(code);
            }
        }
    } else if (state_.injectPhase == InjectPhase::WaitingPipe) {
        if (now >= injectWaitUntil_) {
            state_.injectPhase = InjectPhase::Failed;
            state_.injectMessage = L"注入完成，但未能连接到 trainer";
        }
    }
}

void App::UpdateStatus() {
    std::wstring s;
    if (state_.demo) {
        s = L"演示模式 · 未连接游戏";
    } else if (state_.conn == ConnState::Disconnected) {
        switch (state_.injectPhase) {
        case InjectPhase::Running:
            s = L"正在注入…";
            break;
        case InjectPhase::WaitingPipe:
            s = L"正在等待 trainer 连接…";
            break;
        case InjectPhase::Failed:
            s = L"注入失败";
            break;
        case InjectPhase::Idle:
        default:
            s = L"等待游戏 / 等待注入";
            break;
        }
    } else {
        s = everList_ ? L"已连接" : L"已连接 · 正在初始化…";
        if (state_.gameHwnd && IsWindow(state_.gameHwnd)) {
            if (GetForegroundWindow() == hwnd_) {
                s += L" · 游戏已暂停";
            } else if (IsWindowVisible(hwnd_)) {
                s += L" · 点击面板以暂停游戏";
            }
        } else {
            s += L" · 未找到游戏窗口";
        }
    }
    state_.status = std::move(s);
    state_.panelActive = (GetForegroundWindow() == hwnd_);
}

void App::OnMouseMove(int x, int y) {
    if (dragging_) {
        POINT pt{};
        GetCursorPos(&pt);
        const int dx = pt.x - dragCursor_.x;
        const int dy = pt.y - dragCursor_.y;
        SetWindowPos(hwnd_, nullptr, dragWindow_.left + dx, dragWindow_.top + dy,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    UpdateHover(x, y);
}

void App::OnMouseLeave() {
    if (dragging_) return;
    state_.trackingMouse = false;
    if (state_.hover != HIT_NONE) {
        state_.hover = HIT_NONE;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::UpdateHover(int x, int y) {
    int hit = HitTest(x, y);
    if (hit == HIT_INJECT) {
        const bool canInject = !state_.demo && state_.conn != ConnState::Connected &&
            (state_.injectPhase == InjectPhase::Idle || state_.injectPhase == InjectPhase::Failed);
        if (!canInject) hit = HIT_NONE;
    }
    if (hit != state_.hover) {
        state_.hover = hit;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (!state_.trackingMouse) {
        TRACKMOUSEEVENT tme{};
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd_;
        if (TrackMouseEvent(&tme)) state_.trackingMouse = true;
    }

    SetCursor(LoadCursorW(nullptr, state_.hover == HIT_HEADER
        ? IDC_SIZEALL
        : (IsClickableHit(state_.hover) ? IDC_HAND : IDC_ARROW)));
}

void App::OnLButtonDown(int x, int y) {
    SetFocus(hwnd_);
    POINT pt{x, y};
    if (PtInRect(&layout_.header, pt)) {
        // 无边框窗口：标题区域拖动
        if (state_.editingAmount) {
            state_.editingAmount = false;
            CommitAmount();
        }
        dragging_ = true;
        GetCursorPos(&dragCursor_);
        GetWindowRect(hwnd_, &dragWindow_);
        SetCapture(hwnd_);
        return;
    }

    const int hit = HitTest(x, y);
    state_.pressed = hit;

    if (hit == HIT_AMOUNT) {
        state_.editingAmount = true;
        state_.caret = state_.amountText.size();
    } else if (state_.editingAmount) {
        state_.editingAmount = false;
        CommitAmount();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnLButtonUp(int x, int y) {
    if (dragging_) {
        dragging_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        return;
    }
    const int hit = HitTest(x, y);
    const int pressed = state_.pressed;
    state_.pressed = HIT_NONE;
    if (pressed != HIT_NONE && pressed == hit) ExecuteHit(hit);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnKeyDown(WPARAM key) {
    if (key == VK_ESCAPE) {
        // 仅取消数值编辑；隐藏仍由 Insert 负责。
        if (state_.editingAmount) {
            state_.editingAmount = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    } else if (key == VK_RETURN) {
        if (state_.editingAmount) {
            CommitAmount();
            state_.editingAmount = false;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
    }
}

void App::OnChar(WPARAM ch) {
    if (!state_.editingAmount) return;

    if (ch == L'\b') {
        if (state_.caret > 0 && state_.caret <= state_.amountText.size()) {
            state_.amountText.erase(state_.caret - 1, 1);
            --state_.caret;
        }
    } else if (ch == L'\r' || ch == L'\n') {
        CommitAmount();
        state_.editingAmount = false;
    } else if (ch >= L'0' && ch <= L'9') {
        if (state_.amountText.size() < 6) {
            if (state_.caret > state_.amountText.size()) state_.caret = state_.amountText.size();
            state_.amountText.insert(state_.caret, 1, static_cast<wchar_t>(ch));
            ++state_.caret;
        }
    } else {
        return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::OnDpiChanged(int newDpi, const RECT* suggested) {
    state_.dpi = newDpi > 0 ? newDpi : 96;
    UpdateFonts();
    if (suggested) {
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    RebuildSize();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

int App::HitTest(int x, int y) const {
    return HitTestUi(layout_, x, y);
}

void App::ExecuteHit(int hit) {
    if (hit == HIT_EXIT) {
        RequestExit();
        return;
    }
    if (hit == HIT_ABOUT) {
        ShowAbout();
        return;
    }
    if (hit == HIT_INJECT) {
        if (!state_.demo && state_.conn != ConnState::Connected &&
            (state_.injectPhase == InjectPhase::Idle || state_.injectPhase == InjectPhase::Failed)) {
            StartInjection();
        }
        return;
    }
    if (hit == HIT_AMOUNT) {
        state_.editingAmount = true;
        state_.caret = state_.amountText.size();
        return;
    }
    if (hit == HIT_MINUS) {
        AdjustAmount(-100);
        return;
    }
    if (hit == HIT_PLUS) {
        AdjustAmount(100);
        return;
    }
    if (!IsCtrlHit(hit)) return;

    const int index = HitRow(hit);
    if (index < 0 || index >= static_cast<int>(state_.cheats.size())) return;
    CheatView& c = state_.cheats[static_cast<size_t>(index)];

    if (!c.ready) {
        SetToast(L"该作弊项尚未就绪，等待游戏加载");
        return;
    }
    if (!state_.demo && state_.conn != ConnState::Connected) {
        SetToast(L"尚未连接 trainer");
        return;
    }
    if (c.pending) return;

    if (c.type == "toggle") {
        const bool next = !c.on;
        if (state_.demo) {
            c.on = next;
            SetToast(next ? L"演示：已开启 " + c.name : L"演示：已关闭 " + c.name);
            return;
        }
        c.beforeOn = c.on;
        c.pendingOn = next;
        c.on = next;
        c.pending = true;
        c.pendingAt = GetTickCount64();
        SendSet(c, next);
        SetToast(next ? L"正在开启 " + c.name + L"…" : L"正在关闭 " + c.name + L"…");
    } else {
        CommitAmount();
        if (state_.demo) {
            SetToast(L"演示：执行 " + c.name + L" + " + std::to_wstring(EffectiveAmount()));
            return;
        }
        c.pending = true;
        c.pendingAt = GetTickCount64();
        SendAction(c);
        SetToast(L"正在执行 " + c.name + L"…");
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::CommitAmount() {
    if (state_.amountText.empty()) {
        state_.amount = 100;
    } else {
        long v = std::wcstol(state_.amountText.c_str(), nullptr, 10);
        if (v < 1) v = 100;
        if (v > 999999) v = 999999;
        state_.amount = static_cast<int>(v);
    }
    state_.amountText = std::to_wstring(state_.amount);
    if (state_.caret > state_.amountText.size()) state_.caret = state_.amountText.size();
}

int App::EffectiveAmount() const {
    if (state_.amountText.empty()) return 100;
    long v = std::wcstol(state_.amountText.c_str(), nullptr, 10);
    if (v <= 0) return 100;
    if (v > 999999) return 999999;
    return static_cast<int>(v);
}

void App::AdjustAmount(int delta) {
    CommitAmount();
    long v = static_cast<long>(state_.amount) + delta;
    if (v < 1) v = 1;
    if (v > 999999) v = 999999;
    state_.amount = static_cast<int>(v);
    state_.amountText = std::to_wstring(state_.amount);
    state_.editingAmount = false;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::UpdateFonts() {
    if (font_) DeleteObject(font_);
    if (titleFont_) DeleteObject(titleFont_);
    if (smallFont_) DeleteObject(smallFont_);
    if (aboutTitleFont_) DeleteObject(aboutTitleFont_);
    if (iconFont_) DeleteObject(iconFont_);
    font_ = nullptr;
    titleFont_ = nullptr;
    smallFont_ = nullptr;
    aboutTitleFont_ = nullptr;
    iconFont_ = nullptr;

    const int scale = PixelScale(state_.dpi);
    const int bodyPx = 12 * scale;   // Zpix 原生 12px
    const int titlePx = 16 * scale;  // FFF 8px 设计（主面板）/ MyFontHeading 原生 16px（“关于”标题）
    const int iconPx = 12 * scale;   // 图标与 Zpix 正文等高

    const wchar_t* bodyFace = fontBodyLoaded_ ? L"Zpix" : L"Microsoft YaHei UI";
    const wchar_t* titleFace = fontTitleLoaded_ ? L"FFF Neostandard Bold" : L"Microsoft YaHei UI";
    const wchar_t* aboutFace = fontAboutTitleLoaded_ ? L"MyFontHeading" : titleFace;
    const UINT bodyQuality = fontBodyLoaded_ ? NONANTIALIASED_QUALITY : CLEARTYPE_QUALITY;
    const UINT titleQuality = fontTitleLoaded_ ? NONANTIALIASED_QUALITY : CLEARTYPE_QUALITY;
    const UINT aboutQuality = fontAboutTitleLoaded_ ? NONANTIALIASED_QUALITY : titleQuality;

    auto make = [&](int px, int weight, const wchar_t* face, UINT quality) {
        return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           quality, DEFAULT_PITCH | FF_DONTCARE, face);
    };
    font_ = make(bodyPx, FW_NORMAL, bodyFace, bodyQuality);
    smallFont_ = make(bodyPx, FW_NORMAL, bodyFace, bodyQuality);
    titleFont_ = make(titlePx, FW_NORMAL, titleFace, titleQuality);
    aboutTitleFont_ = make(titlePx, FW_NORMAL, aboutFace, aboutQuality);
    // 图标字体加载失败时保持 nullptr，绘制端跳过图标（不留 .notdef 方框）
    if (fontIconLoaded_) {
        iconFont_ = make(iconPx, FW_NORMAL, L"MyFontIcon", NONANTIALIASED_QUALITY);
    }
}

void App::LoadFonts() {
    if (!hinst_) return;

    struct Item {
        int id;
        const wchar_t* name;
        HANDLE* handle;
        bool* loaded;
    };
    Item items[4] = {
        {IDR_FONT_ZPIX, L"Zpix", &fontMem_[0], &fontBodyLoaded_},
        {IDR_FONT_FFF, L"FFF Neostandard Bold", &fontMem_[1], &fontTitleLoaded_},
        {IDR_FONT_HEADING, L"MyFontHeading", &fontMem_[2], &fontAboutTitleLoaded_},
        {IDR_FONT_ICON, L"MyFontIcon", &fontMem_[3], &fontIconLoaded_},
    };

    for (auto& item : items) {
        if (*item.handle) continue;
        HRSRC res = FindResourceW(hinst_, MAKEINTRESOURCEW(item.id), RT_RCDATA);
        if (!res) {
            Log("font resource missing: %ls", item.name);
            continue;
        }
        DWORD size = SizeofResource(hinst_, res);
        HGLOBAL global = LoadResource(hinst_, res);
        void* data = global ? LockResource(global) : nullptr;
        if (!size || !data) {
            Log("font resource load failed: %ls", item.name);
            continue;
        }

        DWORD count = 0;
        HANDLE mem = AddFontMemResourceEx(data, size, nullptr, &count);
        if (!mem || count == 0) {
            Log("AddFontMemResourceEx failed: %ls (err=%lu)", item.name, GetLastError());
            continue;
        }
        *item.handle = mem;
        *item.loaded = true;
        Log("font loaded: %ls (%lu bytes)", item.name, size);
    }
}

void App::UnloadFonts() {
    for (HANDLE& handle : fontMem_) {
        if (handle) {
            RemoveFontMemResourceEx(handle);
            handle = nullptr;
        }
    }
    fontBodyLoaded_ = false;
    fontTitleLoaded_ = false;
    fontAboutTitleLoaded_ = false;
    fontIconLoaded_ = false;
}

void App::RebuildSize() {
    if (!hwnd_ || !IsWindow(hwnd_)) return;

    layout_ = ComputeLayout(state_.dpi, static_cast<int>(state_.cheats.size()));
    RECT rc{0, 0, layout_.width, layout_.height};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
    const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    SetWindowPos(hwnd_, nullptr, wr.left, wr.top,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::SetToast(std::wstring text, UINT ms) {
    state_.toast = std::move(text);
    state_.toastUntil = GetTickCount64() + ms;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::RegisterInsert() {
    insertRegistered_ = RegisterHotKey(hwnd_, ID_HOTKEY_INSERT, MOD_NOREPEAT, VK_INSERT) != FALSE;
    if (!insertRegistered_) {
        SetToast(L"Insert 热键注册失败，已改用轮询", 4200);
    }
}

void App::SeedDemo() {
    state_.conn = ConnState::Connected;
    state_.cheats.clear();
    state_.cheats.push_back({ "god", L"无伤", "toggle", L"F3", true, true, false, false, false, 0 });
    state_.cheats.push_back({ "lock", L"锁血", "toggle", L"F4", true, true, false, false, false, 0 });
    state_.cheats.push_back({ "coins", L"增加金币", "action", L"F5", false, true, false, false, false, 0 });
    state_.amount = 100;
    state_.amountText = L"100";
    UpdateStatus();
}

void App::ApplyDarkTitleBar() {
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    auto fn = reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmSetWindowAttribute"));
    if (fn) {
        BOOL dark = TRUE;
        if (FAILED(fn(hwnd_, 20, &dark, sizeof(dark)))) {
            fn(hwnd_, 19, &dark, sizeof(dark)); // 旧版 Windows 的 DWMWA_USE_IMMERSIVE_DARK_MODE
        }
    }
    FreeLibrary(dwm);
}
