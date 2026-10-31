// ui_about.h —— “关于”窗口：布局、命中测试与像素风 GDI 绘制（无第三方依赖）
#pragma once

#include <windows.h>

#include <string>
#include <vector>

// 命中测试编码：独立区间（3000+），不与主面板的 HIT_*（0~8 / 2000+）冲突
constexpr int HIT_ABOUT_CLOSE  = 3000;  // 右上角 ×（关闭窗口）
constexpr int HIT_ABOUT_HEADER = 3001;  // 头部标题区（可拖动）

enum AboutBlockKind {
    AboutHeading = 0,   // 小节标题（强调色）
    AboutText    = 1,   // 正文
    AboutMuted   = 2,   // 弱化说明
};

// 正文内容块：以数据组织页面，将来插入图标只需填 icon 区域并在绘制处补图元
struct AboutBlock {
    int kind = AboutHeading;
    std::wstring text;
    RECT area{};   // 文本绘制区
    RECT icon{};   // 预留图标槽位（当前留空）
};

struct AboutLayout {
    int scale = 1;
    int width = 380;
    int height = 320;

    RECT client{};
    RECT header{};    // 标题区（可拖动）
    RECT title{};     // 单行主标题，与右上角 × 同高度
    RECT close{};     // 右上角 ×（关闭窗口）

    std::vector<AboutBlock> blocks;
};

AboutLayout ComputeAboutLayout(int dpi);
int HitTestAbout(const AboutLayout& layout, int x, int y);
// titleFont 为“关于”页大标题字体（IPix / 加载失败时回退到主面板标题字体）
void PaintAbout(HDC dc, const AboutLayout& layout,
                HFONT font, HFONT titleFont, HFONT smallFont,
                int hover, int pressed);
