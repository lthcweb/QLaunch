#include "skin/image.h"

#include <wincodec.h>

#include "core/handle.h"

namespace {

// GUIDs defined here to avoid depending on windowscodecs.lib / uuid.lib
const GUID kCLSID_WICImagingFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
const GUID kIID_IWICImagingFactory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
const GUID kFormat32bppPBGRA = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};

}  // namespace

bool DecodeImage(const std::vector<uint8_t>& data, Bitmap& out) {
    out = Bitmap{};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    UINT w = 0, h = 0;
    if (FAILED(CoCreateInstance(kCLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                kIID_IWICImagingFactory, (void**)factory.put())) ||
        FAILED(factory->CreateStream(stream.put())) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data.data()), (DWORD)data.size())) ||
        FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                                decoder.put())) ||
        FAILED(decoder->GetFrame(0, frame.put())) ||
        FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(frame.get(), kFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr,
                                     0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192)
        return false;

    out.px.resize((size_t)w * h);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, w * h * 4, reinterpret_cast<BYTE*>(out.px.data())))) {
        out = Bitmap{};
        return false;
    }
    out.w = (int)w;
    out.h = (int)h;
    return true;
}

void ApplyClipColor(Bitmap& bmp, COLORREF color) {
    uint32_t key = 0xFF000000u | (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color);
    for (uint32_t& p : bmp.px)
        if (p == key) p = 0;
}
