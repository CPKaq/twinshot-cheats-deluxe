// ui.cpp —— 布局计算、命中测试与 GDI 自绘
#include "ui.h"

#include <algorithm>
#include <string>

namespace {

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

int Scale(int dpi) {
    // 只做整数倍缩放；布局/绘制坐标全部是物理像素，避免 GDI 半像素抗锯齿。
    if (dpi >= 240) return 3;
    if (dpi >= 144) return 2;
    return 1;
}

void Fill(HDC dc, const RECT& rc, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rc, brush);
    DeleteObject(brush);
}

void Frame(HDC dc, const RECT& rc, COLORREF color, int thickness = 1) {
    HBRUSH brush = CreateSolidBrush(color);
    for (int i = 0; i < thickness; ++i) {
        RECT r{rc.left + i, rc.top + i, rc.right - i, rc.bottom - i};
        FrameRect(dc, &r, brush);
    }
    DeleteObject(brush);
}

void Text(HDC dc, HFONT font, COLORREF color, const RECT& rc,
          const std::wstring& text, UINT flags) {
    if (!font || text.empty()) return;
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT r = rc;
    DrawTextW(dc, text.c_str(), -1, &r, flags | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

void TextIn(HDC dc, HFONT font, COLORREF color, const RECT& rc,
            const std::wstring& text, UINT flags = DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS) {
    Text(dc, font, color, rc, text, flags);
}

int RowFromHover(int hit) {
    return HitRow(hit);
}

} // namespace

Layout ComputeLayout(int dpi, int cheatCount) {
    const int s = Scale(dpi);
    const int m = 14 * s;
    const int gap = 8 * s;

    Layout l;
    l.scale = s;
    l.width = 380 * s;

    int y = m;
    l.header = {m, y, l.width - m, y + 54 * s};
    y = l.header.bottom;

    l.status = {m, y, l.width - m, y + 26 * s};
    y = l.status.bottom;

    l.toast = {m, y, l.width - m, y + 24 * s};
    y = l.toast.bottom;

    const int rowH = 42 * s;
    const int listTop = y;
    if (cheatCount > 0) {
        l.list = {m, y, l.width - m, y + rowH * cheatCount};
        l.rows.reserve(static_cast<size_t>(cheatCount));
        for (int i = 0; i < cheatCount; ++i) {
            RowLayout row;
            row.row = {m, listTop + i * rowH, l.width - m, listTop + (i + 1) * rowH};
            row.name = {row.row.left + 10 * s, row.row.top,
                        row.row.right - 158 * s, row.row.bottom};
            row.hotkey = {row.row.right - 152 * s, row.row.top + 8 * s,
                          row.row.right - 108 * s, row.row.bottom - 8 * s};
            row.control = {row.row.right - 98 * s, row.row.top + 6 * s,
                           row.row.right - 8 * s, row.row.bottom - 6 * s};
            l.rows.push_back(row);
        }
        y = l.list.bottom;
    } else {
        l.empty = {m, y, l.width - m, y + 124 * s};
        const int btnW = 76 * s;
        const int btnH = 28 * s;
        l.injectButton = {l.empty.left + (l.empty.right - l.empty.left - btnW) / 2,
                          l.empty.bottom - btnH - 12 * s,
                          l.empty.left + (l.empty.right - l.empty.left + btnW) / 2,
                          l.empty.bottom - 12 * s};
        y = l.empty.bottom;
    }

    y += gap;
    l.amountLabel = {m, y, m + 90 * s, y + 46 * s};

    const int btn = 30 * s;
    const int boxW = 96 * s;
    l.plus = {l.width - m - btn, y + 8 * s, l.width - m, y + 8 * s + btn};
    l.minus = {l.plus.left - gap - btn, y + 8 * s, l.plus.left - gap, y + 8 * s + btn};
    l.amountBox = {l.minus.left - gap - boxW, y + 8 * s,
                   l.minus.left - gap, y + 8 * s + btn};
    y += 46 * s;

    l.footer = {m, y, l.width - m, y + 30 * s};
    l.exitButton = {l.footer.right - 60 * s, l.footer.top + 3 * s,
                    l.footer.right, l.footer.bottom - 3 * s};
    l.aboutButton = {l.exitButton.left - gap - 60 * s, l.footer.top + 3 * s,
                     l.exitButton.left - gap, l.footer.bottom - 3 * s};
    y = l.footer.bottom;

    l.height = y + m;
    l.client = {0, 0, l.width, l.height};
    return l;
}

int HitTestUi(const Layout& l, int x, int y) {
    POINT pt{x, y};
    if (PtInRect(&l.header, pt)) return HIT_HEADER;
    if (PtInRect(&l.exitButton, pt)) return HIT_EXIT;
    if (PtInRect(&l.aboutButton, pt)) return HIT_ABOUT;
    if (PtInRect(&l.injectButton, pt)) return HIT_INJECT;
    if (PtInRect(&l.amountBox, pt)) return HIT_AMOUNT;
    if (PtInRect(&l.minus, pt)) return HIT_MINUS;
    if (PtInRect(&l.plus, pt)) return HIT_PLUS;

    for (size_t i = 0; i < l.rows.size(); ++i) {
        if (PtInRect(&l.rows[i].control, pt)) return CtrlHit(static_cast<int>(i));
        if (PtInRect(&l.rows[i].row, pt)) return RowHit(static_cast<int>(i));
    }
    return HIT_NONE;
}

void PaintUi(HDC dc, const AppState& st, const Layout& l,
             HFONT font, HFONT titleFont, HFONT smallFont) {
    const int s = l.scale;
    Fill(dc, l.client, kBg);

    // 顶部标题
    Fill(dc, l.header, kPanel);
    Fill(dc, RECT{l.header.left, l.header.bottom - 2 * s, l.header.right, l.header.bottom}, kBorder);
    TextIn(dc, titleFont, kText,
           RECT{l.header.left + 12 * s, l.header.top + 8 * s,
                l.header.right - 12 * s, l.header.top + 30 * s},
           L"Twin Shot Cheats Deluxe");
    TextIn(dc, smallFont, kMuted,
           RECT{l.header.left + 12 * s, l.header.top + 30 * s,
                l.header.right - 12 * s, l.header.bottom - 4 * s},
           L"重温你无敌的童年");

    // 状态行
    const bool connected = (st.conn == ConnState::Connected);
    const COLORREF dot = st.demo ? kAccent : (connected ? kGreen : kRed);
    RECT dotRc{l.status.left + 2 * s, l.status.top + 10 * s,
               l.status.left + 10 * s, l.status.top + 18 * s};
    Fill(dc, dotRc, dot);
    TextIn(dc, font, connected || st.demo ? kText : kMuted,
           RECT{l.status.left + 18 * s, l.status.top, l.status.right, l.status.bottom},
           st.status, DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    // toast / 提示行（内容为空时保持留白）
    Fill(dc, l.toast, kBg);
    if (!st.toast.empty()) {
        TextIn(dc, smallFont, kAccent, l.toast, st.toast);
    }

    // 作弊列表
    if (st.cheats.empty()) {
        Fill(dc, l.empty, kPanel);
        Frame(dc, l.empty, kBorder, s);

        const bool showInjectButton = !st.demo && !connected &&
            (st.injectPhase == InjectPhase::Idle || st.injectPhase == InjectPhase::Failed);

        std::wstring line1 = L"等待 trainer 列表…";
        std::wstring line2 = L"请先启动游戏，再点击下方“注入”";
        if (st.demo) {
            line1 = L"演示模式";
            line2 = L"下方是示例布局，用于确认视觉稿";
        } else if (connected) {
            line1 = L"已连接 trainer，正在初始化…";
            line2 = L"作弊项定位完成后会自动显示";
        } else {
            switch (st.injectPhase) {
            case InjectPhase::Running:
                line2 = L"正在注入…（injector 最多等待约 60 秒）";
                break;
            case InjectPhase::WaitingPipe:
                line2 = L"注入已执行，正在等待管道连接…";
                break;
            case InjectPhase::Failed:
                line2 = st.injectMessage.empty() ? L"注入失败，请重试" : st.injectMessage;
                break;
            case InjectPhase::Idle:
            default:
                break;
            }
        }

        TextIn(dc, font, st.demo ? kAccent : kText,
               RECT{l.empty.left + 16 * s, l.empty.top + 15 * s,
                    l.empty.right - 16 * s, l.empty.top + 43 * s},
               line1);
        TextIn(dc, smallFont, st.injectPhase == InjectPhase::Failed ? kRed : kMuted,
               RECT{l.empty.left + 16 * s, l.empty.top + 43 * s,
                    l.empty.right - 16 * s, l.empty.top + 78 * s},
               line2,
               DT_LEFT | DT_TOP | DT_END_ELLIPSIS);

        if (showInjectButton) {
            const bool hot = (st.hover == HIT_INJECT);
            Fill(dc, l.injectButton, hot ? kPanel2 : kPanel);
            Frame(dc, l.injectButton, kAccent, s);
            TextIn(dc, font, kAccent, l.injectButton, L"注入",
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    } else {
        const int hoverRow = RowFromHover(st.hover);
        for (size_t i = 0; i < st.cheats.size(); ++i) {
            const CheatView& c = st.cheats[i];
            const RowLayout& r = l.rows[i];
            const int rowIndex = static_cast<int>(i);

            if (hoverRow == rowIndex) Fill(dc, r.row, kPanel2);
            Fill(dc, RECT{r.row.left, r.row.bottom - s, r.row.right, r.row.bottom}, kDim);

            const bool enabled = connected && c.ready && !c.pending;
            const COLORREF nameColor = enabled || st.demo ? kText : kMuted;
            TextIn(dc, font, nameColor,
                   RECT{r.name.left, r.name.top, r.name.right, r.name.bottom},
                   c.name);

            if (!c.hotkey.empty()) {
                Fill(dc, r.hotkey, kDim);
                Frame(dc, r.hotkey, kBorder, s);
                TextIn(dc, smallFont, c.ready || st.demo ? kMuted : kDim,
                       r.hotkey, c.hotkey, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            const bool toggle = (c.type == "toggle");
            if (toggle) {
                const int trackW = 52 * s;
                const int trackH = 24 * s;
                RECT track{r.control.right - trackW,
                           r.control.top + (r.control.bottom - r.control.top - trackH) / 2,
                           r.control.right,
                           r.control.top + (r.control.bottom - r.control.top - trackH) / 2 + trackH};
                const bool on = c.on;
                const COLORREF trackFill = !enabled ? kDim : (on ? RGB(39, 78, 60) : kPanel2);
                const COLORREF trackLine = !enabled ? kBorder : (on ? kGreen : kBorder);
                Fill(dc, track, trackFill);
                Frame(dc, track, trackLine, s);

                const int knob = 18 * s;
                RECT knobRc = on
                    ? RECT{track.right - knob - 2 * s, track.top + 2 * s,
                           track.right - 2 * s, track.bottom - 2 * s}
                    : RECT{track.left + 2 * s, track.top + 2 * s,
                           track.left + 2 * s + knob, track.bottom - 2 * s};
                Fill(dc, knobRc, !enabled ? kMuted : (on ? kGreen : kMuted));

                RECT stateRc{r.control.left, r.control.top,
                             track.left - 6 * s, r.control.bottom};
                TextIn(dc, smallFont, !enabled ? kDim : (on ? kGreen : kMuted),
                       stateRc, c.pending ? L"…" : (on ? L"开" : L"关"),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else {
                RECT btnRc{r.control.left, r.control.top + 1 * s,
                           r.control.right, r.control.bottom - 1 * s};
                const bool hot = (st.hover == CtrlHit(rowIndex));
                Fill(dc, btnRc, !enabled ? kDim : (hot ? RGB(63, 70, 86) : kPanel2));
                Frame(dc, btnRc, !enabled ? kBorder : kAccent, s);
                TextIn(dc, smallFont, !enabled ? kMuted : kAccent,
                       btnRc, c.pending ? L"等待" : (c.ready ? L"执行" : L"未就绪"),
                       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    // 动作数值
    TextIn(dc, font, kMuted, l.amountLabel, L"金币数量",
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Fill(dc, RECT{l.amountBox.left + 1, l.amountBox.top + 1,
                  l.amountBox.right - 1, l.amountBox.bottom - 1}, kPanel2);
    Frame(dc, l.amountBox, st.editingAmount ? kAccent : kBorder, s);

    RECT valueRc{l.amountBox.left + 8 * s, l.amountBox.top,
                 l.amountBox.right - 6 * s, l.amountBox.bottom};
    TextIn(dc, font, st.editingAmount ? kText : kMuted, valueRc, st.amountText,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (st.editingAmount) {
        size_t caret = std::min(st.caret, st.amountText.size());
        SIZE sz{};
        HFONT old = static_cast<HFONT>(SelectObject(dc, font));
        GetTextExtentPoint32W(dc, st.amountText.c_str(), static_cast<int>(caret), &sz);
        SelectObject(dc, old);
        if ((GetTickCount64() / 500) % 2 == 0) {
            int x = valueRc.left + sz.cx;
            Fill(dc, RECT{x, l.amountBox.top + 5 * s, x + s, l.amountBox.bottom - 5 * s}, kAccent);
        }
    }

    auto drawStep = [&](const RECT& rc, const std::wstring& label, int hit) {
        const bool hot = (st.hover == hit);
        Fill(dc, rc, hot ? kPanel2 : kPanel);
        Frame(dc, rc, kBorder, s);
        TextIn(dc, font, kText, rc, label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    drawStep(l.minus, L"-", HIT_MINUS);
    drawStep(l.plus, L"+", HIT_PLUS);

    // 底栏
    Fill(dc, RECT{l.footer.left, l.footer.top, l.footer.right, l.footer.top + s}, kDim);
    TextIn(dc, smallFont, kMuted,
           RECT{l.footer.left, l.footer.top, l.aboutButton.left - 8 * s, l.footer.bottom},
           L"Insert 隐藏 · 热键后台有效",
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    auto drawFooterButton = [&](const RECT& rc, const std::wstring& label, int hit) {
        const bool hot = (st.hover == hit);
        Fill(dc, rc, hot ? kPanel2 : kPanel);
        Frame(dc, rc, kBorder, s);
        TextIn(dc, smallFont, kMuted, rc, label,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    };
    drawFooterButton(l.aboutButton, L"关于", HIT_ABOUT);
    drawFooterButton(l.exitButton, L"退出", HIT_EXIT);

    // 无边框窗口的外框（客户区像素描边）
    Frame(dc, l.client, kBorder, s);
}
