// main.cpp —— frontend.exe 入口（Win32 GUI）
#include "app.h"

#include <shellapi.h>

#include <cwchar>

namespace {

void EnablePerMonitorDpi() {
    // manifest 已声明 PerMonitorV2；这里作为没有 manifest 时的兜底。
    using Fn = BOOL(WINAPI*)(HANDLE);
    static Fn fn = reinterpret_cast<Fn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (fn) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
        fn(reinterpret_cast<HANDLE>(-4));
    } else {
        SetProcessDPIAware();
    }
}

} // namespace

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int) {
    EnablePerMonitorDpi();

    bool demo = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--demo") == 0) {
                demo = true;
                break;
            }
        }
        LocalFree(argv);
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"TwinShotTrainerFrontend.Instance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            PostMessageW(existing, WM_APP_SHOW, 0, 0);
        }
        CloseHandle(mutex);
        return 0;
    }

    App app;
    int rc = 1;
    if (app.Init(hinst, demo)) rc = app.Run();

    if (mutex) CloseHandle(mutex);
    return rc;
}
