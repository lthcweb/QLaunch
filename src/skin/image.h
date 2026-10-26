// Skin images: decoded with the system's WIC (PNG/BMP) and converted to premultiplied-alpha BGRA.
#pragma once

#include "core/base.h"

struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint32_t> px;  // premultiplied-alpha BGRA, top-down
    bool Empty() const { return px.empty(); }
};

// COM must be initialized on the calling thread
bool DecodeImage(const std::vector<uint8_t>& data, Bitmap& out);

// Makes opaque pixels of the given colour fully transparent (the skin's clippingColor)
void ApplyClipColor(Bitmap& bmp, COLORREF color);
