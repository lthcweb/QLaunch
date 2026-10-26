// Colour-only skin: every part is filled with flat colours. Used when no skin file is selected.
#include "core/str.h"
#include "skin/skin.h"

namespace {

struct Palette {
    const wchar_t* name;
    COLORREF window, border, caption, captionText;
    COLORREF tabArea, tab, tabHot, tabSel, tabText, tabTextSel;
    COLORREF grid, cell, cellHot, cellPressed, cellBorder, cellText, dropTarget;
};

const Palette kPalettes[] = {
    {L"Dark",
     RGB(32, 32, 34), RGB(70, 70, 76), RGB(24, 24, 26), RGB(235, 235, 235),
     RGB(214, 214, 214), RGB(214, 214, 214), RGB(236, 236, 236), RGB(34, 34, 36), RGB(30, 30, 30), RGB(255, 255, 255),
     RGB(32, 32, 34), RGB(46, 46, 50), RGB(70, 70, 78), RGB(30, 30, 32), RGB(64, 64, 70), RGB(240, 240, 240), RGB(0, 120, 212)},
    {L"Light",
     RGB(243, 243, 243), RGB(180, 180, 180), RGB(230, 230, 230), RGB(30, 30, 30),
     RGB(225, 225, 225), RGB(225, 225, 225), RGB(240, 240, 240), RGB(255, 255, 255), RGB(60, 60, 60), RGB(0, 0, 0),
     RGB(243, 243, 243), RGB(255, 255, 255), RGB(229, 241, 251), RGB(204, 228, 247), RGB(210, 210, 210), RGB(20, 20, 20), RGB(0, 120, 212)},
};

class ColorSkin : public Skin {
public:
    explicit ColorSkin(const Palette& p) : p_(p) {}

    void SetInput(int dpi, const SkinInput& in) override {
        dpi_ = dpi;
        in_ = in;
    }

    SkinMetrics Metrics() const override {
        const SkinInput& in = in_;
        const int pad = in.pad;
        SkinMetrics m;
        m.frame = RECT{1, 1 + in.caption, 1, 1};
        // no margin on the side of the tabs that touches the button area
        switch (in.side) {
            case TabSide::Left: m.tabMargin = RECT{pad, pad, 0, pad}; break;
            case TabSide::Right: m.tabMargin = RECT{0, pad, pad, pad}; break;
            case TabSide::Top: m.tabMargin = RECT{pad, pad, pad, 0}; break;
            case TabSide::Bottom: m.tabMargin = RECT{pad, 0, pad, pad}; break;
        }
        m.gridMargin = RECT{pad, pad, pad, pad};
        m.tabPitch = m.cellPitch = POINT{in.gap, in.gap};
        int one = Px(1);
        m.shiftDown = POINT{one, one};
        int border = Px(5);
        m.sizeArea = RECT{border, border, border, border};
        return m;
    }

    void PlaceChrome(int w, int, RECT& caption, RECT& close, RECT& pin) const override {
        int btn = in_.caption;
        close = RECT{w - 1 - btn, 1, w - 1, 1 + btn};
        pin = RECT{close.left - btn, 1, close.left, 1 + btn};
        caption = RECT{1 + Px(8), 1, pin.left, 1 + btn};
    }

    void Draw(Canvas& c, Part part, int state, const RECT& rc) override {
        bool sel = state & kSelected, hot = state & kHot, pressed = state & kPressed;
        switch (part) {
            case Part::Window: {
                c.Fill(rc, p_.window);
                c.Frame(rc, p_.border);
                RECT cap{rc.left + 1, rc.top + 1, rc.right - 1, rc.top + 1 + in_.caption};
                c.Fill(cap, p_.caption);
                break;
            }
            case Part::Caption: break;  // the background was drawn with the window
            case Part::CloseButton:
            case Part::PinButton:
                if (hot || pressed) c.Fill(rc, pressed ? p_.cellPressed : p_.cellHot);
                if (part == Part::CloseButton) DrawCross(c, rc);
                else DrawPin(c, rc, sel);
                break;
            case Part::TabArea: c.Fill(rc, p_.tabArea); break;
            case Part::Tab: c.Fill(rc, sel ? p_.tabSel : hot ? p_.tabHot : p_.tab); break;
            case Part::Grid: c.Fill(rc, p_.grid); break;
            case Part::Cell:
                c.Fill(rc, pressed ? p_.cellPressed : hot ? p_.cellHot : p_.cell);
                c.Frame(rc, (state & kDropTarget) ? p_.dropTarget : p_.cellBorder);
                break;
        }
    }

    TextStyle Text(Part part, int state) const override {
        bool sel = state & kSelected;
        TextStyle t;
        switch (part) {
            case Part::Tab: t.color = sel ? p_.tabTextSel : p_.tabText; t.bold = sel; break;
            case Part::Cell: t.color = p_.cellText; break;
            default: t.color = p_.captionText; t.bold = true; break;
        }
        return t;
    }

private:
    int Px(int v) const { return MulDiv(v, dpi_, 96); }

    void Dot(Canvas& c, int x, int y, int t) const { c.Fill(RECT{x, y, x + t, y + t}, p_.captionText); }

    void DrawCross(Canvas& c, const RECT& rc) const {
        int t = Px(1), r = (rc.bottom - rc.top) / 5;
        int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
        for (int i = -r; i <= r; ++i) {
            Dot(c, cx + i, cy + i, t);
            Dot(c, cx - i, cy + i, t);
        }
    }

    // the pin button is a circle, filled while pinned
    void DrawPin(Canvas& c, const RECT& rc, bool filled) const {
        int t = Px(1), r = (rc.bottom - rc.top) / 5;
        int cx = (rc.left + rc.right) / 2, cy = (rc.top + rc.bottom) / 2;
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                int d2 = x * x + y * y;
                if (d2 <= r * r && (filled || d2 >= (r - t) * (r - t))) Dot(c, cx + x, cy + y, 1);
            }
    }

    const Palette& p_;
    int dpi_ = 96;
    SkinInput in_;
};

}  // namespace

std::unique_ptr<Skin> CreateSkin(const std::wstring& name) {
    for (const Palette& p : kPalettes)
        if (EqualsNoCase(name, p.name)) return std::make_unique<ColorSkin>(p);
    if (auto skin = LoadImageSkin(name)) return skin;
    return std::make_unique<ColorSkin>(kPalettes[0]);
}

std::vector<std::wstring> ListSkins() {
    std::vector<std::wstring> out;
    for (const Palette& p : kPalettes) out.push_back(p.name);
    for (auto& name : ListImageSkins()) out.push_back(name);
    return out;
}
