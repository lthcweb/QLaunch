#include "ui/drop_target.h"

#include <shellapi.h>
#include <shlobj.h>

#include "core/path.h"
#include "core/str.h"

namespace {

// IIDs defined here to avoid linking the uuid library
const IID kIID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID kIID_IDropTarget = {0x00000122, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

CLIPFORMAT IdListFormat() {
    static const CLIPFORMAT cf = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_SHELLIDLIST);
    return cf;
}

CLIPFORMAT UrlFormat() {
    static const CLIPFORMAT cf = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_INETURLW);
    return cf;
}

CLIPFORMAT FileGroupFormat() {
    static const CLIPFORMAT cf = (CLIPFORMAT)RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW);
    return cf;
}

bool HasFormat(IDataObject* data, CLIPFORMAT cf) {
    FORMATETC fmt{cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return data->QueryGetData(&fmt) == S_OK;
}

// Reads a UTF-16 text format; only the first line, trimmed
bool ReadText(IDataObject* data, CLIPFORMAT cf, std::wstring& out) {
    FORMATETC fmt{cf, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM med{};
    if (FAILED(data->GetData(&fmt, &med))) return false;
    if (const auto* text = static_cast<const wchar_t*>(GlobalLock(med.hGlobal))) {
        out.assign(text, wcsnlen(text, GlobalSize(med.hGlobal) / sizeof(wchar_t)));
        GlobalUnlock(med.hGlobal);
    }
    ReleaseStgMedium(&med);
    out = Trim(out.substr(0, out.find_first_of(L"\r\n")));
    return !out.empty();
}

// A link dragged from a browser (address bar, link on a page) or selected text that is a web address
bool ReadUrl(IDataObject* data, std::wstring& url) {
    return ReadText(data, UrlFormat(), url) || (ReadText(data, CF_UNICODETEXT, url) && IsUrl(url));
}

// Browsers also offer the link as a virtual "Page title.url" file: use its name for the button
std::wstring LinkTitle(IDataObject* data) {
    std::wstring title;
    FORMATETC fmt{FileGroupFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM med{};
    if (FAILED(data->GetData(&fmt, &med))) return title;
    if (const auto* group = static_cast<const FILEGROUPDESCRIPTORW*>(GlobalLock(med.hGlobal))) {
        std::wstring name(group->cItems ? group->fgd[0].cFileName : L"");
        if (name.size() > 4 && EqualsNoCase(name.substr(name.size() - 4), L".url")) title = FileTitle(name);
        GlobalUnlock(med.hGlobal);
    }
    ReleaseStgMedium(&med);
    return title;
}

bool NameOf(PCIDLIST_ABSOLUTE pidl, SIGDN kind, std::wstring& out) {
    PWSTR name = nullptr;
    if (FAILED(SHGetNameFromIDList(pidl, kind, &name))) return false;
    out = name;
    CoTaskMemFree(name);
    return !out.empty();
}

// Real files use their path; virtual objects use their parsing name (::{20D04FE0-...} is This PC), which opens
// with a shell: prefix
DroppedItem ItemFromIdList(PCIDLIST_ABSOLUTE pidl) {
    DroppedItem it;
    if (NameOf(pidl, SIGDN_FILESYSPATH, it.file)) {
        it.name = FileTitle(it.file);
    } else if (NameOf(pidl, SIGDN_DESKTOPABSOLUTEPARSING, it.file)) {
        it.file = L"shell:" + it.file;
        NameOf(pidl, SIGDN_NORMALDISPLAY, it.name);
    }
    return it;
}

// Shell IDList array (CIDA): item 0 is the parent folder, the rest are relative to it
void ReadIdList(IDataObject* data, std::vector<DroppedItem>& out) {
    FORMATETC fmt{IdListFormat(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM med{};
    if (FAILED(data->GetData(&fmt, &med))) return;
    if (const auto* cida = static_cast<const CIDA*>(GlobalLock(med.hGlobal))) {
        auto at = [cida](UINT i) { return reinterpret_cast<const BYTE*>(cida) + cida->aoffset[i]; };
        auto parent = reinterpret_cast<PCIDLIST_ABSOLUTE>(at(0));
        for (UINT i = 1; i <= cida->cidl; ++i) {
            PIDLIST_ABSOLUTE pidl = ILCombine(parent, reinterpret_cast<PCUIDLIST_RELATIVE>(at(i)));
            if (!pidl) continue;
            DroppedItem it = ItemFromIdList(pidl);
            CoTaskMemFree(pidl);
            if (!it.file.empty()) out.push_back(std::move(it));
        }
        GlobalUnlock(med.hGlobal);
    }
    ReleaseStgMedium(&med);
}

void ReadFiles(IDataObject* data, std::vector<DroppedItem>& out) {
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM med{};
    if (FAILED(data->GetData(&fmt, &med))) return;
    auto drop = static_cast<HDROP>(med.hGlobal);
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        wchar_t path[MAX_PATH * 2];
        if (DragQueryFileW(drop, i, path, MAX_PATH * 2)) out.push_back(DroppedItem{path, FileTitle(path)});
    }
    ReleaseStgMedium(&med);
}

// Only ever "link", so Explorer never thinks we move or copy its files
DWORD ChooseEffect(DWORD allowed) {
    if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
    if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
    return DROPEFFECT_NONE;
}

}  // namespace

HRESULT DropTarget::QueryInterface(REFIID riid, void** out) {
    if (IsEqualIID(riid, kIID_IUnknown) || IsEqualIID(riid, kIID_IDropTarget)) {
        *out = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG DropTarget::Release() {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
}

POINT DropTarget::ToClient(POINTL pt) const {
    POINT p{pt.x, pt.y};
    ScreenToClient(hwnd_, &p);
    return p;
}

HRESULT DropTarget::DragEnter(IDataObject* data, DWORD, POINTL pt, DWORD* effect) {
    std::wstring url;
    accept_ = HasFormat(data, IdListFormat()) || HasFormat(data, CF_HDROP) || ReadUrl(data, url);
    if (!accept_) {
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    *effect = ChooseEffect(*effect);
    sink_.DragOver(ToClient(pt));
    return S_OK;
}

HRESULT DropTarget::DragOver(DWORD, POINTL pt, DWORD* effect) {
    if (!accept_) {
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    *effect = ChooseEffect(*effect);
    sink_.DragOver(ToClient(pt));
    return S_OK;
}

HRESULT DropTarget::DragLeave() {
    if (accept_) sink_.DragEnd();
    accept_ = false;
    return S_OK;
}

HRESULT DropTarget::Drop(IDataObject* data, DWORD, POINTL pt, DWORD* effect) {
    if (!accept_) {
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    accept_ = false;
    std::vector<DroppedItem> items;
    ReadIdList(data, items);  // prefer the IDList: virtual objects only come that way
    if (items.empty()) ReadFiles(data, items);
    std::wstring url;
    if (items.empty() && ReadUrl(data, url)) {
        std::wstring title = LinkTitle(data);
        items.push_back(DroppedItem{NormalizeUrl(url), title.empty() ? UrlSiteName(url) : title});
    }
    *effect = items.empty() ? DROPEFFECT_NONE : ChooseEffect(*effect);
    sink_.DragEnd();
    if (!items.empty()) sink_.DropItems(ToClient(pt), items);
    return S_OK;
}
