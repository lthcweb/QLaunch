// Icon extraction. Only called on the background thread, so the Shell's COM objects load only there.
#include "icon/icon.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>

#include "core/handle.h"
#include "core/path.h"
#include "core/str.h"
#include "icon/favicon.h"

namespace {

uint64_t Fnv1a(const void* data, size_t len, uint64_t h = 14695981039346656037ull) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// IIDs defined here to avoid linking an extra library
const IID kIID_IImageList = {0x46EB5926, 0x582E, 0x4017, {0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50}};
const IID kIID_IShellItemImageFactory = {0xbcc18b79, 0xba16, 0x442f, {0x80, 0xc4, 0x8a, 0x59, 0xc3, 0x0c, 0x46, 0x3b}};

// Centres an image in a size x size icon, scaled to fit. Small images are enlarged at most 2x: more only blurs.
IconPixels FitIcon(const Bitmap& img, int size) {
    IconPixels out;
    if (img.Empty()) return out;
    int longest = std::max(img.w, img.h);
    int target = std::min(size, longest * 2);
    int w = std::max(1, img.w * target / longest), h = std::max(1, img.h * target / longest);
    Bitmap scaled;
    if (w == img.w && h == img.h) scaled = img;
    else if (!ScaleImage(img, w, h, scaled)) return out;
    out.size = size;
    out.px.assign((size_t)size * size, 0);
    int x0 = (size - w) / 2, y0 = (size - h) / 2;
    for (int y = 0; y < h; ++y)
        std::copy_n(&scaled.px[(size_t)y * w], w, &out.px[(size_t)(y0 + y) * size + x0]);
    return out;
}

// The picture the Shell itself shows for an item, at the wanted size. Used for apps from the Start menu (Store
// apps): the system image lists only have their small, plated logo.
IconPixels ShellItemImage(const std::wstring& path, int size) {
    PIDLIST_ABSOLUTE pidl = ParseShellPath(path);
    if (!pidl) return {};
    ComPtr<IShellItemImageFactory> factory;
    HBITMAP hbmp = nullptr;
    bool ok = SUCCEEDED(SHCreateItemFromIDList(pidl, kIID_IShellItemImageFactory, (void**)factory.put())) &&
              SUCCEEDED(factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY, &hbmp));
    CoTaskMemFree(pidl);
    if (!ok) return {};
    GdiObj<HBITMAP> owner(hbmp);

    BITMAP bm{};
    GetObjectW(hbmp, sizeof(bm), &bm);
    Bitmap img;
    img.w = bm.bmWidth;
    img.h = bm.bmHeight;
    img.px.resize((size_t)img.w * img.h);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = img.w;
    bi.bmiHeader.biHeight = -img.h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    HDC screen = GetDC(nullptr);
    int lines = GetDIBits(screen, hbmp, 0, img.h, img.px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    // much smaller than asked means the Shell has nothing better than the image list: use that instead
    if (lines != img.h || img.Empty() || std::max(img.w, img.h) < size / 2) return {};

    // the alpha may be straight or premultiplied, or missing altogether (all 0): normalise to premultiplied
    bool anyAlpha = false, straight = false;
    for (uint32_t p : img.px) {
        uint32_t a = p >> 24;
        anyAlpha |= a != 0;
        straight |= ((p >> 16) & 0xFF) > a || ((p >> 8) & 0xFF) > a || (p & 0xFF) > a;
    }
    for (uint32_t& p : img.px) {
        uint32_t a = anyAlpha ? p >> 24 : 255;
        if (!anyAlpha || straight) {
            auto mul = [a](uint32_t c) { return (c * a + 127) / 255; };
            p = (a << 24) | (mul((p >> 16) & 0xFF) << 16) | (mul((p >> 8) & 0xFF) << 8) | mul(p & 0xFF);
        }
    }
    return FitIcon(img, size);
}

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
        PIDLIST_ABSOLUTE pidl = ParseShellPath(path);
        if (!pidl) return nullptr;
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

bool IsBlank(const IconPixels& p) {
    for (uint32_t v : p.px)
        if (v >> 24) return false;
    return true;
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

// A web address shows the icon of the program that opens it: the default browser for http(s), the mail
// program for mailto:
HICON UrlIcon(const std::wstring& url, int size) {
    // ASSOCF_IS_PROTOCOL (Windows 8+, not declared for our Windows 7 target) follows the user's choice of
    // default browser; Windows 7 rejects it and gets the plain protocol key instead
    const ASSOCF flags[] = {static_cast<ASSOCF>(0x1000), ASSOCF_NONE};
    std::wstring scheme = url.substr(0, url.find(L':'));
    wchar_t buf[MAX_PATH * 2];
    for (ASSOCF f : flags) {
        DWORD len = _countof(buf);
        if (SUCCEEDED(AssocQueryStringW(f, ASSOCSTR_DEFAULTICON, scheme.c_str(), nullptr, buf, &len))) {
            int index = PathParseIconLocationW(buf);
            if (HICON icon = FileIcon(ExpandEnv(buf), index, size)) return icon;
        }
        len = _countof(buf);
        if (SUCCEEDED(AssocQueryStringW(f, ASSOCSTR_EXECUTABLE, scheme.c_str(), L"open", buf, &len)))
            if (HICON icon = FileIcon(buf, 0, size)) return icon;
    }
    return nullptr;
}

}  // namespace

uint64_t IconKey(const Item& item, int size) {
    bool custom = !item.icon.empty();
    std::wstring path = custom ? ResolvePath(item.icon) : NativePath(ExpandEnv(item.file));
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
        if (IsUrl(item.file)) {
            Bitmap site;  // the site's own icon; failing that (offline, no icon) the browser's
            if (DownloadFavicon(NormalizeUrl(item.file), size, site)) {
                IconPixels px = FitIcon(site, size);
                if (!px.Empty()) return px;
            }
            icon.reset(UrlIcon(NormalizeUrl(item.file), size));
            IconPixels px = icon ? RenderIcon(icon.get(), size) : IconPixels{};
            if (px.Empty() || IsBlank(px)) px = ShellIconPixels(L".html", size);  // generic web page icon
            if (px.Empty() || IsBlank(px)) px = ShellIconPixels(L".url", size);   // Internet shortcut
            return px;
        }
        // Store apps: shell:AppsFolder\<app id>, or shell:<app id> as saved by older versions (ids have a '!')
        if (StartsWithNoCase(item.file, L"shell:AppsFolder\\") ||
            (StartsWithNoCase(item.file, L"shell:") && item.file.find(L'!') != std::wstring::npos)) {
            IconPixels px = ShellItemImage(item.file, size);
            if (!px.Empty() && !IsBlank(px)) return px;
        }
        std::wstring file = NativePath(ExpandEnv(item.file));
        icon.reset(LocatedIcon(file, size));
        if (!icon) return ShellIconPixels(file, size);
    }
    return icon ? RenderIcon(icon.get(), size) : IconPixels{};
}
