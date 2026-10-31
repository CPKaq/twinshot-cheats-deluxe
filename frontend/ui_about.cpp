// ui_about.cpp —— “关于”窗口：布局计算、命中测试与像素风 GDI 自绘
#include "ui_about.h"

#include "ui_common.h"

#include <string>
#include <vector>

namespace {

// 占位内容：正式文案（开发者列表 / 外部资源说明）确定后只改这里
std::vector<AboutBlock> AboutContent() {
    std::vector<AboutBlock> out;
    auto add = [&out](int kind, const wchar_t* text) {
        AboutBlock b;
        b.kind = kind;
        b.text = text;
        out.push_back(std::move(b));
    };

    add(AboutHeading, L"开　　发");
    add(AboutText, L"CPK @ Nitrome 吧攻略组");
    add(AboutHeading, L"共同开发");
    add(AboutText, L"GLM  ·  DeepSeek");
    add(AboutHeading, L"字体资源");
    add(AboutMuted, L"Zpix - SolidZORO");
    add(AboutMuted, L"FFF Neostandard Bold - Fonts For Flash");
    return out;
}

} // namespace

AboutLayout ComputeAboutLayout(int dpi) {
    const int s = Scale(dpi);
    const int m = 14 * s;
    const int gap = 8 * s;

    AboutLayout l;
    l.scale = s;
    l.width = 380 * s;

    l.header = {m, m, l.width - m, m + 46 * s};
    l.title = {l.header.left + 12 * s, l.header.top,
               l.header.right - 44 * s, l.header.bottom};
    l.close = {l.header.right - 32 * s, l.header.top + 8 * s,
               l.header.right, l.header.bottom - 8 * s};

    int y = l.header.bottom + gap;
    const int iconW = 16 * s;
    const int iconGap = 6 * s;
    for (AboutBlock def : AboutContent()) {
        switch (def.kind) {
        case AboutHeading: {
            const int rowH = 26 * s;
            y += 4 * s;
            def.area = {m, y, l.width - m, y + rowH};
            def.icon = {};
            y += rowH;
            break;
        }
        case AboutText: {
            const int rowH = 24 * s;
            def.icon = {m, y + (rowH - iconW) / 2, m + iconW, y + (rowH + iconW) / 2};
            def.area = {def.icon.right + iconGap, y, l.width - m, y + rowH};
            y += rowH;
            break;
        }
        case AboutMuted:
        default: {
            const int rowH = 20 * s;
            def.icon = {m + 2 * s, y + (rowH - iconW) / 2,
                        m + 2 * s + iconW, y + (rowH + iconW) / 2};
            def.area = {def.icon.right + iconGap, y, l.width - m, y + rowH};
            y += rowH;
            break;
        }
        }
        l.blocks.push_back(std::move(def));
    }

    l.height = y + m;
    l.client = {0, 0, l.width, l.height};
    return l;
}

int HitTestAbout(const AboutLayout& l, int x, int y) {
    POINT pt{x, y};
    if (PtInRect(&l.close, pt)) return HIT_ABOUT_CLOSE;
    if (PtInRect(&l.header, pt)) return HIT_ABOUT_HEADER;
    return 0;
}

void PaintAbout(HDC dc, const AboutLayout& l, HFONT font, HFONT titleFont,
                HFONT smallFont, int hover, int pressed) {
    const int s = l.scale;
    if (!dc || s <= 0) return;

    Fill(dc, l.client, kBg);

    // 头部标题区（可拖动）：单行主标题与右上角 × 同高度
    Fill(dc, l.header, kPanel);
    Fill(dc, RECT{l.header.left, l.header.bottom - 2 * s, l.header.right, l.header.bottom},
         kBorder);
    Text(dc, titleFont, kText, l.title, L"关于", DT_LEFT | DT_VCENTER);

    // 右上角关闭键：手绘像素 ×，悬停/按下高亮
    const bool closeHot = (hover == HIT_ABOUT_CLOSE);
    Fill(dc, l.close, closeHot ? kPanel2 : kPanel);
    Frame(dc, l.close,
          (closeHot || pressed == HIT_ABOUT_CLOSE) ? kAccent : kBorder, s);
    {
        const COLORREF crossColor = closeHot ? kAccent : kMuted;
        const int cx = (l.close.left + l.close.right) / 2;
        const int cy = (l.close.top + l.close.bottom) / 2;
        const int arm = 4;
        for (int d = -arm; d <= arm; d += 1) {
            Fill(dc, RECT{cx + d * s, cy + d * s, cx + d * s + s, cy + d * s + s}, crossColor);
            Fill(dc, RECT{cx + d * s, cy - (d + 1) * s, cx + (d + 1) * s, cy - d * s}, crossColor);
        }
    }

    // 正文块：小节标题 / 正文 / 弱化说明；图标槽位当前留空
    for (const AboutBlock& b : l.blocks) {
        switch (b.kind) {
        case AboutHeading:
            Text(dc, smallFont, kAccent, b.area, b.text, DT_LEFT | DT_VCENTER);
            break;
        case AboutMuted:
            Text(dc, smallFont, kMuted, b.area, b.text, DT_LEFT | DT_VCENTER);
            break;
        case AboutText:
        default:
            Text(dc, font, kText, b.area, b.text, DT_LEFT | DT_VCENTER);
            break;
        }
    }

    // 无边框窗口的外框（客户区像素描边）
    Frame(dc, l.client, kBorder, s);
}
