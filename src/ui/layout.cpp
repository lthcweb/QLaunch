#include "ui/layout.h"

#include <algorithm>

namespace {

bool Vertical(TabSide s) { return s == TabSide::Left || s == TabSide::Right; }

RECT Offset(const RECT& r, const RECT& o) {
    return RECT{r.left + o.left, r.top + o.top, r.right + o.right, r.bottom + o.bottom};
}
RECT Shrink(const RECT& r, const RECT& m) {
    return RECT{r.left + m.left, r.top + m.top, r.right - m.right, r.bottom - m.bottom};
}

// Width (tabs left/right) or height (tabs top/bottom) of the tab area, so that area + offset - margin fits the
// tabs exactly
int TabRegionSize(const Sizing& s) {
    const SkinMetrics& k = s.sm;
    if (Vertical(s.side))
        return s.m.tabW + k.tabMargin.left + k.tabMargin.right - k.tabOffset.right + k.tabOffset.left;
    return s.m.tabH + k.tabMargin.top + k.tabMargin.bottom - k.tabOffset.bottom + k.tabOffset.top;
}

// How far the tab panel reaches into the button area because of the skin offset. The button area starts outside
// the panel
// so they don't overlap; e.g. with offset="-9, 1, 9, 24" the panel reaches 9 px right and the button area moves
// 9 px too.
int TabOverhang(const Sizing& s) {
    const RECT& o = s.sm.tabOffset;
    switch (s.side) {
        case TabSide::Left:   return std::max(0, (int)o.right);
        case TabSide::Right:  return std::max(0, (int)-o.left);
        case TabSide::Top:    return std::max(0, (int)o.bottom);
        case TabSide::Bottom: return std::max(0, (int)-o.top);
    }
    return 0;
}

// Fixed cost outside the grid: frame, tab bar, and the offsets and margins of the button area
SIZE Overhead(const Sizing& s) {
    const SkinMetrics& k = s.sm;
    SIZE o{k.frame.left + k.frame.right + k.gridMargin.left + k.gridMargin.right - k.gridOffset.right +
               k.gridOffset.left,
           k.frame.top + k.frame.bottom + k.gridMargin.top + k.gridMargin.bottom - k.gridOffset.bottom +
               k.gridOffset.top};
    if (Vertical(s.side)) o.cx += TabRegionSize(s) + TabOverhang(s);
    else o.cy += TabRegionSize(s) + TabOverhang(s);
    return o;
}

void LayoutTabs(Layout& l, const LayoutInput& in) {
    const Metrics& m = in.s.m;
    const POINT pitch = in.s.sm.tabPitch;
    const RECT& a = l.tabArea;
    l.tabs.resize(in.tabTextW.size());
    int pos = Vertical(in.s.side) ? a.top : a.left;
    for (size_t i = 0; i < l.tabs.size(); ++i) {
        if (Vertical(in.s.side)) {
            l.tabs[i] = RECT{a.left, pos, a.right, pos + m.tabH};
            pos += m.tabH + pitch.y;
        } else {
            int w = std::max(m.tabW, in.tabTextW[i] + 2 * m.textPad);
            l.tabs[i] = RECT{pos, a.top, pos + w, a.bottom};
            pos += w + pitch.x;
        }
    }
}

}  // namespace

Metrics Metrics::From(const Settings& s, int dpi) {
    auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };
    Metrics m;
    m.caption = px(s.captionH);
    m.tabW = px(s.tabW);
    m.tabH = px(s.tabH);
    m.cellW = px(s.cellW);
    m.cellH = px(s.cellH);
    m.gap = px(s.cellGap);
    m.pad = px(6);
    m.iconSize = px(s.iconSize);
    m.textPad = px(8);
    m.searchW = px(150);
    return m;
}

RECT Layout::Cell(int slot) const {
    int c = slot % cols, r = slot / cols;
    int x = grid.left + c * (cellW + pitch.x), y = grid.top + r * (cellH + pitch.y);
    return RECT{x, y, x + cellW, y + cellH};
}

void ComputeLayout(Layout& l, int width, int height, const LayoutInput& in, const Skin& skin) {
    const Metrics& m = in.s.m;
    const SkinMetrics& k = in.s.sm;
    l = Layout{};
    l.client = RECT{0, 0, width, height};
    l.cellW = m.cellW;
    l.cellH = m.cellH;
    l.pitch = k.cellPitch;

    l.dragBar = RECT{0, 0, width, k.frame.top};
    skin.PlaceChrome(width, height, l.caption, l.closeBtn, l.pinBtn);
    // the search box takes the right end of the caption, left of the pin and close buttons when they sit on the
    // caption, and at most half of it
    int right = l.caption.right;
    for (const RECT* b : {&l.pinBtn, &l.closeBtn})
        if (!IsRectEmpty(b) && b->left > l.caption.left && b->left < right && b->top < l.caption.bottom &&
            b->bottom > l.caption.top)
            right = b->left;
    right -= m.pad;
    int left = std::max(right - m.searchW, (int)(l.caption.left + right) / 2);
    l.search = RECT{left, l.caption.top, right, l.caption.bottom};
    l.title = RECT{l.caption.left, l.caption.top, left, l.caption.bottom};

    // inside = window - frame, then split into the tab area and the button area
    RECT inner{k.frame.left, k.frame.top, width - k.frame.right, height - k.frame.bottom};
    RECT region = inner, rest = inner;
    int size = TabRegionSize(in.s), gap = TabOverhang(in.s);
    switch (in.s.side) {
        case TabSide::Left:   region.right = inner.left + size; rest.left = region.right + gap; break;
        case TabSide::Right:  region.left = inner.right - size; rest.right = region.left - gap; break;
        case TabSide::Top:    region.bottom = inner.top + size; rest.top = region.bottom + gap; break;
        case TabSide::Bottom: region.top = inner.bottom - size; rest.bottom = region.top - gap; break;
    }
    l.tabPanel = Offset(region, k.tabOffset);
    l.tabArea = Shrink(l.tabPanel, k.tabMargin);
    l.drilled = in.drilled;
    LayoutTabs(l, in);

    l.gridPanel = Offset(rest, k.gridOffset);
    RECT content = Shrink(l.gridPanel, k.gridMargin);
    l.grid = content;
    l.cols = std::max(1, (int)((content.right - content.left + l.pitch.x) / (m.cellW + l.pitch.x)));
    l.rows = std::max(1, (int)((content.bottom - content.top + l.pitch.y) / (m.cellH + l.pitch.y)));
}

SIZE WindowSizeFor(const Sizing& s, int cols, int rows) {
    SIZE o = Overhead(s);
    const POINT p = s.sm.cellPitch;
    return SIZE{o.cx + cols * s.m.cellW + (cols - 1) * p.x, o.cy + rows * s.m.cellH + (rows - 1) * p.y};
}

void GridFromWindowSize(const Sizing& s, int w, int h, int& cols, int& rows) {
    SIZE o = Overhead(s);
    const POINT p = s.sm.cellPitch;
    int stepX = s.m.cellW + p.x, stepY = s.m.cellH + p.y;
    // round to the nearest whole cell; feels better when dragging the border
    cols = std::max(1, (int)((w - o.cx + p.x + stepX / 2) / stepX));
    rows = std::max(1, (int)((h - o.cy + p.y + stepY / 2) / stepY));
}

Hit HitTest(const Layout& l, POINT pt) {
    if (PtInRect(&l.closeBtn, pt)) return {HitKind::Close, 0};
    if (PtInRect(&l.pinBtn, pt)) return {HitKind::Pin, 0};
    if (PtInRect(&l.search, pt)) return {HitKind::Search, 0};
    for (int i = 0; i < (int)l.tabs.size(); ++i) {
        RECT r;
        if (!IntersectRect(&r, &l.tabs[i], &l.tabArea) || !PtInRect(&r, pt)) continue;
        if (!l.drilled) return {HitKind::Tab, i};
        return i == 0 ? Hit{HitKind::Parent, 0} : Hit{HitKind::SubTab, i - 1};
    }
    if (PtInRect(&l.grid, pt)) {
        int c = (pt.x - l.grid.left) / (l.cellW + l.pitch.x);
        int r = (pt.y - l.grid.top) / (l.cellH + l.pitch.y);
        if (c < l.cols && r < l.rows) {
            int slot = r * l.cols + c;
            RECT rc = l.Cell(slot);
            if (PtInRect(&rc, pt)) return {HitKind::Cell, slot};
        }
    }
    if (PtInRect(&l.dragBar, pt)) return {HitKind::Caption, 0};
    return {};
}
