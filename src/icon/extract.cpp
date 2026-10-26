// Icon extraction. Only called on the background thread, so the Shell's COM objects load only there.
#include "icon/icon.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "core/handle.h"
#include "core/path.h"
#include "core/str.h"

namespace {

uint64_t Fnv1a(const void* data, size_t len, uint64_t h = 14695981039346656037ull) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// IID of IImageList, defined here to avoid linking an extra library
const IID kIID_IImageList = {0x46EB5926, 0x582E, 0x4017, {0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50}};

// Renders an HICON as size x size premultiplied BGRA.
// It is drawn once on black and once on white: the black result is the premultiplied colour and the
// difference gives alpha. This works the same for 32-bit alpha icons, old masked icons and monochrome ones.
IconPixels RenderIcon(HICON icon, int size) {
    IconPixels out;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    void* bits[2] = {};
    GdiObj<HBITMAP> bmp[2] = {
        GdiObj<HBITMAP>(CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits[0], nullptr, 0)),
        GdiObj<HBITMAP>(CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits[1], nullptr, 0))};
    if (bmp[0] && bmp[1]) {
        const size_t count = (size_t)size * size;
        for (int i = 0; i < 2; ++i) {
            SelectObj sel(dc, bmp[i].get());
            memset(bits[i], i == 0 ? 0x00 : 0xFF, count * 4);
            DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
        }
        GdiFlush();
        const auto* black = static_cast<const uint32_t*>(bits[0]);
        const auto* white = static_cast<const uint32_t*>(bits[1]);
        out.size = size;
        out.px.resize(count);
        for (size_t i = 0; i < count; ++i) {
            int g0 = (black[i] >> 8) & 0xFF, g1 = (white[i] >> 8) & 0xFF;
            uint32_t a = 255 - (g1 - g0);
            out.px[i] = (black[i] & 0x00FFFFFF) | (a << 24);
        }
    }
    DeleteDC(dc);
    return out;
}

// The smallest system image list size that is >= size
int ShellImageListFor(int size) {
    if (size <= 16) return SHIL_SMALL;
    if (size <= 32) return SHIL_LARGE;
    if (size <= 48) return SHIL_EXTRALARGE;
    return SHIL_JUMBO;
}

// Gets the icon from the system image list `list` (SHIL_*)
HICON ShellIcon(const std::wstring& path, int list) {
    SHFILEINFOW sfi{};
    if (StartsWithNoCase(path, L"shell:")) {  // virtual object such as This PC
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) return nullptr;
        SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl), 0, &sfi, sizeof(sfi),
                       SHGFI_PIDL | SHGFI_SYSICONINDEX);
        CoTaskMemFree(pidl);
    } else {
        UINT flags = SHGFI_SYSICONINDEX;
        DWORD attr = 0;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            flags |= SHGFI_USEFILEATTRIBUTES;  // file doesn't exist: use the generic icon for its extension
            attr = FILE_ATTRIBUTE_NORMAL;
        }
        if (!SHGetFileInfoW(path.c_str(), attr, &sfi, sizeof(sfi), flags)) return nullptr;
    }

    IUnknown* images = nullptr;
    if (FAILED(SHGetImageList(list, kIID_IImageList, (void**)&images))) return nullptr;
    // documented: an IImageList* can be used directly as an HIMAGELIST
    HICON icon = ImageList_GetIcon(reinterpret_cast<HIMAGELIST>(images), sfi.iIcon, ILD_TRANSPARENT);
    images->Release();
    return icon;
}

// The icon was drawn only in the top-left quarter
bool OnlyInTopLeft(const IconPixels& p) {
    int half = p.size / 2;
    for (int y = 0; y < p.size; ++y)
        for (int x = 0; x < p.size; ++x)
            if ((x >= half || y >= half) && (p.px[(size_t)y * p.size + x] >> 24)) return false;
    return true;
}

// Uses the system image list. When a file has no 256 px icon, the 256 list draws it at its own size in the
// top-left corner, so it ends up tiny after scaling; in that case use the 48 list and scale up.
IconPixels ShellIconPixels(const std::wstring& path, int size) {
    int list = ShellImageListFor(size);
    IconHandle icon(ShellIcon(path, list));
    if (!icon) return {};
    IconPixels px = RenderIcon(icon.get(), size);
    if (list == SHIL_JUMBO && OnlyInTopLeft(px)) {
        icon.reset(ShellIcon(path, SHIL_EXTRALARGE));
        if (icon) px = RenderIcon(icon.get(), size);
    }
    return px;
}

// Takes the icon straight from a file at the wanted size: picks the closest image and scales it to size x size.
// A negative index is a resource ID.
HICON FileIcon(const std::wstring& path, int index, int size) {
    HICON icon = nullptr;
    if (PrivateExtractIconsW(path.c_str(), index, size, size, &icon, nullptr, 1, 0) != 1) return nullptr;
    return icon;
}

// File types that carry their own icon
bool HasOwnIcon(const std::wstring& path) {
    const wchar_t* ext = PathFindExtensionW(path.c_str());
    for (const wchar_t* e : {L".exe", L".dll", L".ico", L".cpl", L".scr"})
        if (_wcsicmp(ext, e) == 0) return true;
    return false;
}

// Gets the icon at the wanted size from the file that holds it: exe, ico etc. hold their own; for documents
// ask the Shell where the icon is (usually an icon inside the associated program).
// The system image lists only come in 16/32/48/256, so avoid them when the file can be read directly.
HICON LocatedIcon(const std::wstring& path, int size) {
    if (StartsWithNoCase(path, L"shell:") || GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        return nullptr;
    if (HasOwnIcon(path)) return FileIcon(path, 0, size);
    SHFILEINFOW sfi{};
    if (!SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICONLOCATION) || !sfi.szDisplayName[0])
        return nullptr;  // the icon comes from a Shell extension at run time, there is no fixed location
    return FileIcon(ExpandEnv(sfi.szDisplayName), sfi.iIcon, size);
}

}  // namespace

uint64_t IconKey(const Item& item, int size) {
    bool custom = !item.icon.empty();
    std::wstring path = custom ? ResolvePath(item.icon) : ExpandEnv(item.file);
    CharLowerBuffW(&path[0], (DWORD)path.size());
    int index = custom ? item.iconIndex : 0;
    uint64_t mtime = LastWriteTime(path);

    uint64_t h = Fnv1a(path.data(), path.size() * sizeof(wchar_t));
    h = Fnv1a(&index, sizeof(index), h);
    h = Fnv1a(&size, sizeof(size), h);
    return Fnv1a(&mtime, sizeof(mtime), h);
}

IconPixels ExtractIconPixels(const Item& item, int size) {
    IconHandle icon;
    if (!item.icon.empty()) icon.reset(FileIcon(ResolvePath(item.icon), item.iconIndex, size));
    if (!icon && !item.file.empty()) {
        std::wstring file = ExpandEnv(item.file);
        icon.reset(LocatedIcon(file, size));
        if (!icon) return ShellIconPixels(file, size);
    }
    return icon ? RenderIcon(icon.get(), size) : IconPixels{};
}
