// Double-buffered canvas: one 32-bit premultiplied-alpha DIB; all drawing writes pixels directly.
// Text is first drawn with GDI onto a black-and-white mask and then blended by coverage, so text alpha is right
// on layered windows too.
#pragma once

#include "core/base.h"
#include "core/handle.h"
#include "icon/icon.h"
#include "skin/image.h"

struct TextStyle {
    enum Shadow : uint8_t { None, Lower, Upper, Outline };

    COLORREF color = 0;
    Shadow shadow = None;
    COLORREF shadowColor = 0;
    uint8_t shadowAlpha = 255;
    uint8_t shadowWidth = 1;  // pixels: the shadow offset or the outline width
    uint8_t size = 0;         // font size in points; 0 means the size from the settings
    bool bold = false;
};

class Canvas {
public:
    Canvas();
    ~Canvas();
    QL_NO_COPY(Canvas);

    bool Resize(int w, int h);
    void Release();  // frees the memory while the window is hidden

    HDC dc() const { return dc_; }
    int width() const { return w_; }
    int height() const { return h_; }

    void Clear();                                // makes everything transparent
    void Fill(const RECT& rc, COLORREF color);   // opaque fill
    void Frame(const RECT& rc, COLORREF color);  // 1-pixel frame
    void Blend(const IconPixels& icon, int x, int y, int alpha = 255);
    // Draws the src area of bmp into dst. border is the 9-slice border on the source: corners only scale with
    // DPI,
    // edges and the middle stretch (or tile when tile is true).
    void DrawImage(const Bitmap& bmp, const RECT& src, const RECT& dst, const RECT& border, int dpi,
                   bool tile);
    void Text(const RECT& rc, const wchar_t* text, const TextStyle& style, HFONT font, UINT format);
    // Fully transparent pixels of a layered window get no mouse input. Turn the transparent pixels outside
    // inner into
    // an invisible alpha of 1 so the resize border around the window can be grabbed.
    void MakeHittable(const RECT& inner);

private:
    RECT Clip(const RECT& rc) const;
    void DrawPiece(const Bitmap& bmp, int sx, int sw, int sy, int sh, const RECT& dst, int dpi, bool tile);
    bool EnsureMask(int w, int h);
    void BlendMask(int ox, int oy, int mw, int mh, COLORREF color, int alpha, int dx, int dy, int radius);

    HDC dc_ = nullptr;
    GdiObj<HBITMAP> bmp_;
    HGDIOBJ oldBmp_ = nullptr;
    uint32_t* bits_ = nullptr;
    int w_ = 0, h_ = 0;

    // text mask, grown on demand
    HDC maskDc_ = nullptr;
    GdiObj<HBITMAP> maskBmp_;
    HGDIOBJ oldMask_ = nullptr;
    uint32_t* mask_ = nullptr;
    int maskW_ = 0, maskH_ = 0;

    std::vector<int> map_;  // column map for DrawPiece, reused to avoid allocations
};
