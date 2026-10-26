// Layout and hit testing. Pure computation that never touches windows or GDI, easy to read and change on its
// own.
//
// Window structure (tabs on the left):
//   ┌ top frame (caption · search) pin  x ┐
//   │ tabs │ grid of cells               │
//   └ bottom frame ──────────────────────┘
// Frame widths and the offsets and margins of each area come from the skin (see SkinMetrics in skin.h).
// Sub-tabs share the tab bar with the top-level tabs: opening a page that has sub-pages turns the bar into
// "‹ parent page" + its sub-tabs, and going back restores the top-level tabs. The window and the button area
// never change size.
#pragma once

#include "core/base.h"
#include "model/model.h"
#include "model/settings.h"
#include "skin/skin.h"

// Sizes from the settings, in pixels scaled for DPI
struct Metrics {
    int caption, tabW, tabH, cellW, cellH, gap, pad, iconSize, textPad, searchW;

    static Metrics From(const Settings& s, int dpi);
};

// Everything needed to work out the window size
struct Sizing {
    Metrics m{};
    SkinMetrics sm{};
    TabSide side = TabSide::Left;
};

struct Layout {
    RECT client{}, dragBar{}, caption{}, closeBtn{}, pinBtn{};
    RECT title{}, search{};   // inside the caption: the title text, and the search box at its right end
    RECT tabPanel{}, tabArea{};    // background panel of the tab bar, area the tabs are laid out in
    RECT gridPanel{}, grid{};
    std::vector<RECT> tabs;   // may extend past tabArea; clipped when drawn
    bool drilled = false;     // the bar shows sub-tabs: tabs[0] is "‹ parent page", the sub-tabs follow
    int cols = 1, rows = 1;
    int cellW = 0, cellH = 0;
    POINT pitch{};            // gap between cells

    RECT Cell(int slot) const;  // slot: index of a visible cell in the grid, row-major
};

struct LayoutInput {
    Sizing s;
    std::vector<int> tabTextW;  // text width of each tab in the bar (decides tab widths when tabs are on the top/bottom)
    bool drilled = false;       // see Layout::drilled
};

void ComputeLayout(Layout& out, int width, int height, const LayoutInput& in, const Skin& skin);

// Window size <-> grid rows and columns
SIZE WindowSizeFor(const Sizing& s, int cols, int rows);
void GridFromWindowSize(const Sizing& s, int w, int h, int& cols, int& rows);

// Tab: top-level tab; Parent: the "‹ parent page" at the top of the sub-tab bar; SubTab: a sub-tab
enum class HitKind { None, Caption, Close, Pin, Search, Tab, Parent, SubTab, Cell };
struct Hit {
    HitKind kind = HitKind::None;
    int index = -1;  // tab index or cell slot
    bool operator==(const Hit& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const Hit& o) const { return !(*this == o); }
};

Hit HitTest(const Layout& l, POINT pt);
