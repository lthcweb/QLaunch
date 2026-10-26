#include "skin/canvas.h"

#include <algorithm>
#include <cstring>

namespace {

uint32_t Mul(uint32_t c, uint32_t a) { return (c * a + 127) / 255; }

// COLORREF (0x00BBGGRR) -> premultiplied pixel (0xAARRGGBB) with alpha a
uint32_t ToPixel(COLORREF c, uint32_t a = 255) {
    return (a << 24) | (Mul(GetRValue(c), a) << 16) | (Mul(GetGValue(c), a) << 8) | Mul(GetBValue(c), a);
}

// Premultiplied "source over" blend; the formula is the same for all four channels
void Over(uint32_t& d, uint32_t s) {
    uint32_t inv = 255 - (s >> 24);
    if (inv == 255) return;
    if (inv == 0) {
        d = s;
        return;
    }
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        uint32_t v = ((s >> shift) & 0xFF) + Mul((d >> shift) & 0xFF, inv);
        out |= std::min(v, 255u) << shift;
    }
    d = out;
}

// Multiplies the whole pixel by opacity a
uint32_t Scale(uint32_t p, uint32_t a) {
    if (a >= 255) return p;
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) out |= Mul((p >> shift) & 0xFF, a) << shift;
    return out;
}

HBITMAP CreateDib(HDC dc, int w, int h, uint32_t** bits) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down, rows in screen order
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, reinterpret_cast<void**>(bits), nullptr, 0);
}

HDC CreateMemDc() {
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    SetBkMode(dc, TRANSPARENT);
    return dc;
}

}  // namespace

Canvas::Canvas() : dc_(CreateMemDc()), maskDc_(CreateMemDc()) {}

Canvas::~Canvas() {
    Release();
    DeleteDC(dc_);
    DeleteDC(maskDc_);
}

bool Canvas::Resize(int w, int h) {
    if (w == w_ && h == h_ && bits_) return true;
    if (oldBmp_) SelectObject(dc_, oldBmp_);
    oldBmp_ = nullptr;
    bmp_.reset();
    bits_ = nullptr;
    w_ = h_ = 0;
    if (w <= 0 || h <= 0) return false;

    bmp_.reset(CreateDib(dc_, w, h, &bits_));
    if (!bmp_) return false;
    oldBmp_ = SelectObject(dc_, bmp_.get());
    w_ = w;
    h_ = h;
    return true;
}

void Canvas::Release() {
    Resize(0, 0);
    if (oldMask_) SelectObject(maskDc_, oldMask_);
    oldMask_ = nullptr;
    maskBmp_.reset();
    mask_ = nullptr;
    maskW_ = maskH_ = 0;
    map_.clear();
    map_.shrink_to_fit();
}

RECT Canvas::Clip(const RECT& rc) const {
    return RECT{std::max<LONG>(rc.left, 0), std::max<LONG>(rc.top, 0),
                std::min<LONG>(rc.right, w_), std::min<LONG>(rc.bottom, h_)};
}

void Canvas::Clear() {
    if (bits_) memset(bits_, 0, (size_t)w_ * h_ * 4);
}

void Canvas::Fill(const RECT& rc, COLORREF color) {
    RECT r = Clip(rc);
    uint32_t px = ToPixel(color);
    for (LONG y = r.top; y < r.bottom; ++y) std::fill(bits_ + y * w_ + r.left, bits_ + y * w_ + r.right, px);
}

void Canvas::Frame(const RECT& rc, COLORREF color) {
    Fill(RECT{rc.left, rc.top, rc.right, rc.top + 1}, color);
    Fill(RECT{rc.left, rc.bottom - 1, rc.right, rc.bottom}, color);
    Fill(RECT{rc.left, rc.top, rc.left + 1, rc.bottom}, color);
    Fill(RECT{rc.right - 1, rc.top, rc.right, rc.bottom}, color);
}

void Canvas::Blend(const IconPixels& icon, int x, int y, int alpha) {
    if (icon.size <= 1 || !bits_) return;  // size 1 is the "extraction failed" placeholder
    const int n = icon.size;
    RECT r = Clip(RECT{x, y, x + n, y + n});
    for (LONG py = r.top; py < r.bottom; ++py) {
        const uint32_t* src = icon.px.data() + (py - y) * n - x;
        uint32_t* dst = bits_ + py * w_;
        for (LONG px = r.left; px < r.right; ++px) Over(dst[px], Scale(src[px], alpha));
    }
}

// Draws one piece of a 9-slice image. stretch: the source stretches to dst; tile: the source is scaled for DPI
// and repeated over dst.
// Nearest-neighbour sampling keeps the edges crisp.
void Canvas::DrawPiece(const Bitmap& bmp, int sx, int sw, int sy, int sh, const RECT& dst, int dpi,
                       bool tile) {
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
    RECT r = Clip(dst);
    if (r.left >= r.right || r.top >= r.bottom) return;

    // which source column each target column maps to
    int unitW = tile ? std::max(1, MulDiv(sw, dpi, 96)) : dw;
    int unitH = tile ? std::max(1, MulDiv(sh, dpi, 96)) : dh;
    auto srcX = [&](int dx) { return sx + (int)((int64_t)((dx % unitW) * 2 + 1) * sw / (2 * unitW)); };
    auto srcY = [&](int dy) { return sy + (int)((int64_t)((dy % unitH) * 2 + 1) * sh / (2 * unitH)); };

    map_.resize(r.right - r.left);
    for (LONG x = r.left; x < r.right; ++x) map_[x - r.left] = srcX(x - dst.left);
    for (LONG y = r.top; y < r.bottom; ++y) {
        const uint32_t* src = bmp.px.data() + (size_t)srcY(y - dst.top) * bmp.w;
        uint32_t* out = bits_ + y * w_;
        for (LONG x = r.left; x < r.right; ++x) Over(out[x], src[map_[x - r.left]]);
    }
}

void Canvas::DrawImage(const Bitmap& bmp, const RECT& srcIn, const RECT& dst, const RECT& border,
                       int dpi, bool tile) {
    if (bmp.Empty() || !bits_) return;
    RECT src{std::max<LONG>(srcIn.left, 0), std::max<LONG>(srcIn.top, 0),
             std::min<LONG>(srcIn.right, bmp.w), std::min<LONG>(srcIn.bottom, bmp.h)};
    int sw = src.right - src.left, sh = src.bottom - src.top;
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    // source borders can't exceed the source area; target borders scale with DPI and shrink proportionally when
    // they don't fit
    int sl = std::min<int>(border.left, sw), sr = std::min<int>(border.right, sw - sl);
    int st = std::min<int>(border.top, sh), sb = std::min<int>(border.bottom, sh - st);
    int dl = MulDiv(sl, dpi, 96), dr = MulDiv(sr, dpi, 96);
    int dt = MulDiv(st, dpi, 96), db = MulDiv(sb, dpi, 96);
    if (dl + dr > dw) {
        dl = MulDiv(dw, dl, dl + dr);
        dr = dw - dl;
    }
    if (dt + db > dh) {
        dt = MulDiv(dh, dt, dt + db);
        db = dh - dt;
    }

    const int xs[4] = {(int)src.left, (int)src.left + sl, (int)src.right - sr, (int)src.right};
    const int ys[4] = {(int)src.top, (int)src.top + st, (int)src.bottom - sb, (int)src.bottom};
    const int xd[4] = {(int)dst.left, (int)dst.left + dl, (int)dst.right - dr, (int)dst.right};
    const int yd[4] = {(int)dst.top, (int)dst.top + dt, (int)dst.bottom - db, (int)dst.bottom};
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            DrawPiece(bmp, xs[i], xs[i + 1] - xs[i], ys[j], ys[j + 1] - ys[j],
                      RECT{xd[i], yd[j], xd[i + 1], yd[j + 1]}, dpi, tile);
}

bool Canvas::EnsureMask(int w, int h) {
    if (w <= maskW_ && h <= maskH_ && mask_) return true;
    w = std::max(w, maskW_);
    h = std::max(h, maskH_);
    if (oldMask_) SelectObject(maskDc_, oldMask_);
    oldMask_ = nullptr;
    maskBmp_.reset(CreateDib(maskDc_, w, h, &mask_));
    if (!maskBmp_) {
        mask_ = nullptr;
        maskW_ = maskH_ = 0;
        return false;
    }
    oldMask_ = SelectObject(maskDc_, maskBmp_.get());
    SetTextColor(maskDc_, RGB(255, 255, 255));
    maskW_ = w;
    maskH_ = h;
    return true;
}

// Blends color into the canvas by coverage from the mask (mw x mh, top-left at canvas ox,oy).
// dx,dy offset the whole mask (drop shadow); radius > 0 takes the maximum coverage within radius (outline).
void Canvas::BlendMask(int ox, int oy, int mw, int mh, COLORREF color, int alpha, int dx, int dy,
                       int radius) {
    RECT r = Clip(RECT{ox, oy, ox + mw, oy + mh});
    for (LONG y = r.top; y < r.bottom; ++y) {
        uint32_t* out = bits_ + y * w_;
        for (LONG x = r.left; x < r.right; ++x) {
            int mx = x - ox - dx, my = y - oy - dy;
            uint32_t cover = 0;
            for (int j = std::max(0, my - radius); j <= std::min(mh - 1, my + radius); ++j)
                for (int i = std::max(0, mx - radius); i <= std::min(mw - 1, mx + radius); ++i)
                    cover = std::max(cover, (mask_[j * maskW_ + i] >> 8) & 0xFF);
            if (cover) Over(out[x], ToPixel(color, Mul(cover, alpha)));
        }
    }
}

void Canvas::Text(const RECT& rc, const wchar_t* text, const TextStyle& style, HFONT font, UINT format) {
    if (!bits_ || !*text) return;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    int pad = style.shadow == TextStyle::None ? 0 : style.shadowWidth;
    int mw = w + 2 * pad, mh = h + 2 * pad;
    if (w <= 0 || h <= 0 || !EnsureMask(mw, mh)) return;

    // white text on black: the green channel is the coverage
    for (int y = 0; y < mh; ++y) memset(mask_ + y * maskW_, 0, mw * 4);
    {
        SelectObj sel(maskDc_, font);
        RECT tr{pad, pad, pad + w, pad + h};
        DrawTextW(maskDc_, text, -1, &tr, format | DT_NOPREFIX);
        GdiFlush();
    }

    int ox = rc.left - pad, oy = rc.top - pad;
    int sw = style.shadowWidth;
    switch (style.shadow) {
        case TextStyle::Lower: BlendMask(ox, oy, mw, mh, style.shadowColor, style.shadowAlpha, sw, sw, 0); break;
        case TextStyle::Upper: BlendMask(ox, oy, mw, mh, style.shadowColor, style.shadowAlpha, -sw, -sw, 0); break;
        case TextStyle::Outline:
            BlendMask(ox, oy, mw, mh, style.shadowColor, style.shadowAlpha, 0, 0, (sw + 1) / 2);
            break;
        case TextStyle::None: break;
    }
    BlendMask(ox, oy, mw, mh, style.color, 255, 0, 0, 0);
}

void Canvas::MakeHittable(const RECT& inner) {
    for (int y = 0; y < h_; ++y) {
        bool rowInside = y >= inner.top && y < inner.bottom;
        uint32_t* row = bits_ + y * w_;
        for (int x = 0; x < w_; ++x) {
            if (rowInside && x >= inner.left && x < inner.right) {
                x = inner.right - 1;  // skip the middle
                continue;
            }
            if ((row[x] >> 24) == 0) row[x] = 0x01000000u;
        }
    }
}
