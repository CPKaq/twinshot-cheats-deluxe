// focus.cpp —— 游戏窗口发现、强制前台、恢复游戏焦点
#include "focus.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cwchar>

namespace {

constexpr wchar_t kGameExe[] = L"Twin Shot Deluxe.exe";

DWORD FindGamePid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, kGameExe) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

struct FindCtx {
    DWORD pid = 0;
    HWND best = nullptr;
    LONG bestArea = 0;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<FindCtx*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (GetWindowTextLengthW(hwnd) <= 0) return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return TRUE;
    LONG area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > ctx->bestArea) {
        ctx->bestArea = area;
        ctx->best = hwnd;
    }
    return TRUE;
}

bool AttachAndActivate(HWND target) {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
    DWORD myThread = GetCurrentThreadId();
    if (!fgThread || fgThread == myThread) return false;

    if (!AttachThreadInput(myThread, fgThread, TRUE)) return false;
    BringWindowToTop(target);
    BOOL ok = SetForegroundWindow(target);
    AttachThreadInput(myThread, fgThread, FALSE);
    return ok && GetForegroundWindow() == target;
}

} // namespace

namespace focus {

HWND FindGameWindow() {
    DWORD pid = FindGamePid();
    if (!pid) return nullptr;

    FindCtx ctx{};
    ctx.pid = pid;
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.best;
}

bool ActivateWindow(HWND hwnd, bool topmost) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW);
    if (topmost) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }

    if (SetForegroundWindow(hwnd) && GetForegroundWindow() == hwnd) return true;
    if (AttachAndActivate(hwnd)) return true;

    // 最后再试一次直接的 BringWindowToTop；失败时由调用方提示用户点击。
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    return GetForegroundWindow() == hwnd;
}

bool RestoreGame(HWND gameHwnd) {
    if (!gameHwnd || !IsWindow(gameHwnd)) return false;

    if (IsIconic(gameHwnd)) ShowWindow(gameHwnd, SW_RESTORE);
    if (SetForegroundWindow(gameHwnd) && GetForegroundWindow() == gameHwnd) return true;
    return AttachAndActivate(gameHwnd);
}

} // namespace focus
