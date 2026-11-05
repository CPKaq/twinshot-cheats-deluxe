// ui.h —— 布局与 GDI 绘制（无第三方依赖）
#pragma once

#include "app.h"

Layout ComputeLayout(int dpi, int cheatCount);
int HitTestUi(const Layout& layout, int x, int y);
void PaintUi(HDC dc, const AppState& state, const Layout& layout,
             HFONT font, HFONT titleFont, HFONT smallFont, HFONT iconFont);
