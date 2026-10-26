// Main window: drawing and icon loading
#include "ui/main_wnd.h"

#include <commctrl.h>

#include <algorithm>

namespace {

RECT Intersect(const RECT& a, const RECT& b) {
    RECT r;
    if (!IntersectRect(&r, &a, &b)) SetRectEmpty(&r);
    return r;
}

RECT Shrink(const RECT& r, const RECT& m) {
    return RECT{r.left + m.left, r.top + m.top, r.right - m.right, r.bottom - m.bottom};
}

}  // namespace

void MainWnd::Redraw() {
    if (!layered_) {
        InvalidateRect(hwnd_, nullptr, FALSE);
    } else if (!redrawPending_ && visible_) {
        // several repaint requests within one message are merged into one
        redrawPending_ = true;
        PostMessageW(hwnd_, WM_APP_REDRAW, 0, 0);
    }
}

bool MainWnd::Render() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (!skin_ || app_.store.pages.empty() || !canvas_.Resize(rc.right, rc.bottom)) return false;

    canvas_.Clear();
    // the button area panel goes first: the tab panel may cover its edge because of the skin offset
    skin_->Draw(canvas_, Part::Window, kNormal, layout_.client);
    skin_->Draw(canvas_, Part::Grid, kNormal, layout_.gridPanel);
    PaintCaption();
    PaintTabs();
    PaintCells();

    // the dragged button follows the mouse
    if (dragging_) {
        bool here = dragFrom_.page == page_ && dragFrom_.sub == sub_;
        int idx = here ? CurrentView().Find(dragFrom_.pos) : -1;
        int icon = sizing_.m.iconSize;
        if (idx >= 0 && idx < (int)icons_.size())
            canvas_.Blend(icons_[idx], dragPt_.x - icon / 2, dragPt_.y - icon / 2, 160);
    }
    return true;
}

void MainWnd::Paint(HDC target) {
    if (layered_ || !Render()) return;
    BitBlt(target, 0, 0, canvas_.width(), canvas_.height(), canvas_.dc(), 0, 0, SRCCOPY);
}

void MainWnd::PaintLayered() {
    if (!Render()) return;
    const RECT& b = sizing_.sm.sizeArea;
    canvas_.MakeHittable(RECT{b.left, b.top, canvas_.width() - b.right, canvas_.height() - b.bottom});
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    POINT pos{wr.left, wr.top}, src{0, 0};
    SIZE size{canvas_.width(), canvas_.height()};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, nullptr, &pos, &size, canvas_.dc(), &src, 0, &blend, ULW_ALPHA);
}

void MainWnd::PaintCaption() {
    const RECT& cap = layout_.caption;
    skin_->Draw(canvas_, Part::Caption, kNormal, cap);

    // caption: the page name, or "page › sub-page" on a sub-page
    const Page& page = app_.store.pages[page_];
    std::wstring title = page.name;
    if (sub_ >= 0) title += L"  ›  " + page.subs[sub_].name;
    TextStyle style = skin_->Text(Part::Caption, kNormal);
    canvas_.Text(cap, title.c_str(), style, FontFor(style),
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    auto btnState = [&](HitKind k) {
        int s = kNormal;
        if (hover_.kind == k) s |= kHot;
        if (pressed_.kind == k && hover_.kind == k) s |= kPressed;
        return s;
    };
    int pin = btnState(HitKind::Pin) | (app_.settings.pinned ? kSelected : 0);
    skin_->Draw(canvas_, Part::PinButton, pin, layout_.pinBtn);
    skin_->Draw(canvas_, Part::CloseButton, btnState(HitKind::Close), layout_.closeBtn);
}

void MainWnd::PaintTabs() {
    auto state = [&](HitKind k, int i, bool selected) {
        int s = selected ? kSelected : kNormal;
        Hit h{k, i};
        if (hover_ == h) s |= kHot;
        if ((dragging_ || fileDrag_) && dropTarget_ == h) s |= kDropTarget;
        return s;
    };
    const UINT format = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS;
    const RECT& textMargin = sizing_.sm.tabTextMargin;

    // Draws one tab; mark is a small arrow at one end: "›" on the right of a page with sub-pages, "‹" on the
    // left of "‹ parent page"
    auto draw = [&](int i, int s, const std::wstring& name, const wchar_t* mark, UINT markAlign) {
        RECT r = Intersect(layout_.tabs[i], layout_.tabArea);
        if (IsRectEmpty(&r)) return;
        skin_->Draw(canvas_, Part::Tab, s, r);
        TextStyle style = skin_->Text(Part::Tab, s);
        HFONT font = FontFor(style, Part::Tab);
        RECT tr = Shrink(r, textMargin);
        const int pad = MulDiv(3, dpi_, 96);  // when the skin gives no text margin, keep the arrow off the edge anyway
        tr.left = std::max(tr.left, r.left + pad);
        tr.right = std::min(tr.right, r.right - pad);
        if (mark) {
            canvas_.Text(tr, mark, style, font, markAlign | DT_VCENTER | DT_SINGLELINE);
            int w = TextWidth(font, mark);
            if (markAlign == DT_LEFT) tr.left += w;
            else tr.right -= w;
        }
        canvas_.Text(tr, name.c_str(), style, font, format);
    };

    skin_->Draw(canvas_, Part::TabArea, kNormal, layout_.tabPanel);
    const auto& pages = app_.store.pages;
    if (!layout_.drilled) {
        for (int i = 0; i < (int)layout_.tabs.size(); ++i)
            draw(i, state(HitKind::Tab, i, i == page_), pages[i].name,
                 pages[i].subs.empty() ? nullptr : L"›", DT_RIGHT);
        return;
    }
    // sub-tab bar: the parent page at the top is always drawn selected, showing where we are
    const Page& page = pages[page_];
    draw(0, state(HitKind::Parent, 0, true), page.name, L"‹", DT_LEFT);
    for (int i = 1; i < (int)layout_.tabs.size(); ++i)
        draw(i, state(HitKind::SubTab, i - 1, i - 1 == sub_), page.subs[i - 1].name, nullptr, 0);
}

void MainWnd::PaintCells() {
    const Metrics& m = sizing_.m;
    const Page& view = CurrentView();
    const int slots = layout_.cols * layout_.rows;
    const int textGap = MulDiv(2, dpi_, 96);

    for (int slot = 0; slot < slots; ++slot) {
        Pos pos = RefForSlot(slot).pos;
        RECT rc = layout_.Cell(slot);
        Hit h{HitKind::Cell, slot};
        int s = kNormal;
        if (hover_ == h) s |= kHot;
        if (pressed_ == h && hover_ == h && !dragging_) s |= kPressed;
        if ((dragging_ || fileDrag_) && dropTarget_ == h) s |= kDropTarget;
        skin_->Draw(canvas_, Part::Cell, s, rc);

        int idx = view.Find(pos);
        if (idx < 0) continue;
        const Item& it = view.cells[idx].item;
        bool isDragSource = dragging_ && dragFrom_.page == page_ && dragFrom_.sub == sub_ &&
                            dragFrom_.pos == pos;
        // hovered/pressed content shifts as the skin says
        POINT shift = (s & kPressed) ? sizing_.sm.shiftDown : (s & kHot) ? sizing_.sm.shiftHover : POINT{};

        int contentH = m.iconSize + textGap + textH_;
        int x = rc.left + (m.cellW - m.iconSize) / 2 + shift.x;
        int y = rc.top + std::max(0, (m.cellH - contentH) / 2) + shift.y;
        if (idx < (int)icons_.size()) canvas_.Blend(icons_[idx], x, y, isDragSource ? 80 : 255);

        RECT tr{rc.left + textGap + shift.x, y + m.iconSize + textGap, rc.right - textGap + shift.x,
                y + m.iconSize + textGap + textH_};
        TextStyle style = skin_->Text(Part::Cell, s);
        canvas_.Text(tr, it.name.c_str(), style, FontFor(style, Part::Cell), DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

void MainWnd::LoadIcons() {
    const Page& view = CurrentView();
    icons_.resize(view.cells.size());
    bool loaded = false;
    for (size_t i = 0; i < view.cells.size(); ++i) {
        const Item& it = view.cells[i].item;
        if (!icons_[i].Empty()) continue;
        uint64_t key = IconKey(it, sizing_.m.iconSize);
        if (app_.iconCache.Get(key, icons_[i])) loaded = true;
        else app_.iconLoader.Request(hwnd_, it, sizing_.m.iconSize, key);
    }
    if (loaded) Redraw();
}

void MainWnd::ReleaseIcons() {
    icons_.clear();
    icons_.shrink_to_fit();
}

HFONT MainWnd::FontFor(const TextStyle& style, Part part) {
    const Settings& st = app_.settings;
    const FontSpec* own = part == Part::Tab ? &st.tabFont : part == Part::Cell ? &st.buttonFont : nullptr;
    if (own && own->Empty()) own = nullptr;
    const std::wstring& face = own ? own->face : st.fontName;
    int size = own ? own->size : style.size ? style.size : st.fontSize;
    bool bold = style.bold || (own && own->bold);
    for (const Font& f : fonts_)
        if (f.size == size && f.bold == bold && f.face == face) return f.font.get();
    // greyscale antialiasing: text is drawn to a mask and blended, and ClearType's coloured fringes go wrong on
    // translucent backgrounds
    HFONT font = CreateFontW(-MulDiv(size, dpi_, 72), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE,
                             FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, DEFAULT_PITCH, face.c_str());
    fonts_.push_back(Font{face, size, bold, GdiObj<HFONT>(font)});
    return font;
}

int MainWnd::TextWidth(HFONT font, const std::wstring& s) const {
    SelectObj sel(canvas_.dc(), font);
    SIZE size{};
    GetTextExtentPoint32W(canvas_.dc(), s.c_str(), (int)s.size(), &size);
    return size.cx;
}
