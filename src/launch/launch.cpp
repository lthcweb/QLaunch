#include "launch/launch.h"

#include <shellapi.h>
#include <shlobj.h>

#include "core/path.h"
#include "launch/builtin.h"
#include "core/str.h"

bool LaunchItem(HWND owner, const Item& item, bool forceAdmin) {
    if (item.file.empty()) return false;
    if (IsBuiltinCommand(item.file)) return RunBuiltinCommand(owner, item.file);
    if (IsUrl(item.file)) {  // opens in the default browser (or mail program for mailto:)
        std::wstring url = NormalizeUrl(item.file);
        return (INT_PTR)ShellExecuteW(owner, nullptr, url.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
    }
    std::wstring file = NativePath(ExpandEnv(item.file));
    std::wstring dir = NativePath(ExpandEnv(item.dir));
    // with no working folder, use the program's own folder; many old programs rely on that
    if (dir.empty() && !StartsWithNoCase(file, L"shell:")) dir = DirOf(file);

    SHELLEXECUTEINFOW sei{sizeof(sei)};
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_LOG_USAGE;
    sei.hwnd = owner;
    sei.lpVerb = (item.admin || forceAdmin) ? L"runas" : nullptr;
    sei.lpFile = file.c_str();
    sei.lpParameters = item.args.empty() ? nullptr : item.args.c_str();
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = item.show;

    // virtual objects (shell:::{CLSID}, maybe with a sub path, Store apps) are parsed to an IDList first,
    // which is more reliable
    PIDLIST_ABSOLUTE pidl = StartsWithNoCase(file, L"shell:") ? ParseShellPath(file) : nullptr;
    if (pidl) {
        sei.fMask |= SEE_MASK_IDLIST;
        sei.lpIDList = pidl;
        sei.lpFile = nullptr;
    }
    bool ok = ShellExecuteExW(&sei) != FALSE;
    CoTaskMemFree(pidl);
    return ok;
}

void OpenItemFolder(const Item& item) {
    std::wstring file = ExpandEnv(item.file);
    if (file.empty() || StartsWithNoCase(file, L"shell:") || IsBuiltinCommand(file) || IsUrl(file)) return;
    std::wstring args = L"/select,\"" + file + L"\"";
    ShellExecuteW(nullptr, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}
