#include "skin/image.h"

#include <wincodec.h>

#include "core/handle.h"

namespace {

// GUIDs defined here to avoid depending on windowscodecs.lib / uuid.lib
const GUID kCLSID_WICImagingFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
const GUID kIID_IWICImagingFactory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
const GUID kFormat32bppPBGRA = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};

bool CreateFactory(ComPtr<IWICImagingFactory>& factory) {
    return SUCCEEDED(CoCreateInstance(kCLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      kIID_IWICImagingFactory, (void**)factory.put()));
}

bool CopyOut(IWICBitmapSource* src, UINT w, UINT h, Bitmap& out) {
    out.px.resize((size_t)w * h);
    if (FAILED(src->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(out.px.data())))) {
        out = Bitmap{};
        return false;
    }
    out.w = (int)w;
    out.h = (int)h;
    return true;
}

}  // namespace

bool DecodeImage(const std::vector<uint8_t>& data, Bitmap& out) {
    out = Bitmap{};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT frames = 0, w = 0, h = 0;
    if (!CreateFactory(factory) || FAILED(factory->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), (DWORD)data.size())) ||
        FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                                decoder.put())) ||
        FAILED(decoder->GetFrameCount(&frames)) || frames == 0)
        return false;

    // an .ico holds the same picture at several sizes: take the largest
    UINT best = 0, bestW = 0;
    for (UINT i = 0; i < frames && frames > 1; ++i) {
        ComPtr<IWICBitmapFrameDecode> f;
        UINT fw = 0, fh = 0;
        if (SUCCEEDED(decoder->GetFrame(i, f.put())) && SUCCEEDED(f->GetSize(&fw, &fh)) && fw > bestW) {
            best = i;
            bestW = fw;
        }
    }
    if (FAILED(decoder->GetFrame(best, frame.put())) ||
        FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(frame.get(), kFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                     0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192)
        return false;
    return CopyOut(converter.get(), w, h, out);
}

bool ScaleImage(const Bitmap& in, int w, int h, Bitmap& out) {
    out = Bitmap{};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> src;
    ComPtr<IWICBitmapScaler> scaler;
    if (in.Empty() || w <= 0 || h <= 0 || !CreateFactory(factory) ||
        FAILED(factory->CreateBitmapFromMemory(in.w, in.h, kFormat32bppPBGRA, in.w * 4, in.w * in.h * 4,
                                               reinterpret_cast<BYTE*>(const_cast<uint32_t*>(in.px.data())),
                                               src.put())) ||
        FAILED(factory->CreateBitmapScaler(scaler.put())) ||
        FAILED(scaler->Initialize(src.get(), w, h, WICBitmapInterpolationModeFant)))
        return false;
    return CopyOut(scaler.get(), w, h, out);
}

void ApplyClipColor(Bitmap& bmp, COLORREF color) {
    uint32_t key = 0xFF000000u | (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
    for (uint32_t& p : bmp.px)
        if (p == key) p = 0;
}
