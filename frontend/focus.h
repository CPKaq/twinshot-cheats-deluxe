// focus.h —— 游戏窗口发现与前台激活
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace focus {

// 按进程名 "Twin Shot Deluxe.exe" 找游戏主窗口；找不到返回 nullptr。
HWND FindGameWindow();

// 显示/恢复窗口并尝试把它带到前台。
// topmost=true 时同时设置 HWND_TOPMOST（给前端面板用）。
bool ActivateWindow(HWND hwnd, bool topmost);

// 隐藏前端后把焦点还给游戏；找不到/已退出时只返回 false。
bool RestoreGame(HWND gameHwnd);

} // namespace focus
