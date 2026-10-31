// ui_common.h —— 主面板与“关于”窗口共用的像素配色与 GDI 原语（无第三方依赖）
#pragma once

#include <windows.h>

#include <string>

// 调色板：深底、米白字、橙黄强调（与像素设计稿一致）
constexpr COLORREF kBg      = RGB(20, 22, 28);
constexpr COLORREF kPanel   = RGB(31, 35, 44);
constexpr COLORREF kPanel2  = RGB(40, 45, 57);
constexpr COLORREF kBorder  = RGB(65, 73, 90);
constexpr COLORREF kText    = RGB(232, 230, 222);
constexpr COLORREF kMuted   = RGB(140, 149, 166);
constexpr COLORREF kAccent  = RGB(242, 184, 75);
constexpr COLORREF kGreen   = RGB(111, 207, 151);
constexpr COLORREF kRed     = RGB(235, 87, 87);
constexpr COLORREF kDim     = RGB(52, 57, 69);

// 只做整数倍缩放；布局/绘制坐标全部是物理像素，避免 GDI 半像素抗锯齿。
inline int Scale(int dpi) {
    if (dpi >= 240) return 3;
    if (dpi >= 144) return 2;
    return 1;
}

inline void Fill(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

inline void Frame(HDC dc, const RECT& rc, COLORREF color, int thickness = 1) {
    HBRUSH brush = CreateSolidBrush(color);
    for (int i = 0; i < thickness; ++i) {
        RECT r{rc.left + i, rc.top + i, rc.right - i, rc.bottom - i};
        FrameRect(dc, &r, brush);
    }
    DeleteObject(brush);
}

inline void Text(HDC dc, HFONT font, COLORREF color, const RECT& rc,
                 const std::wstring& text, UINT flags) {
    if (!font || text.empty()) return;
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT r = rc;
    DrawTextW(dc, text.c_str(), -1, &r, flags | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

inline void TextIn(HDC dc, HFONT font, COLORREF color, const RECT& rc,
                   const std::wstring& text,
                   UINT flags = DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS) {
    Text(dc, font, color, rc, text, flags);
}
