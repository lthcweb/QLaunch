#include "launch/builtin.h"

#include <shellapi.h>
#include <shlobj.h>

#include "core/str.h"
#include "i18n/i18n.h"
#include "ui/msgbox.h"

namespace {

constexpr wchar_t kPrefix[] = L"ql:";

// Everything except signing out needs the shutdown privilege for ExitWindowsEx
void EnableShutdownPrivilege() {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid))
        AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr);
    CloseHandle(token);
}

bool Confirm(HWND owner, const wchar_t* key, const wchar_t* fallback) {
    return MessageBoxAtCursor(owner, T(key, fallback), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) == IDOK;
}

bool EndSession(HWND owner, UINT how, const wchar_t* key, const wchar_t* fallback) {
    if (!Confirm(owner, key, fallback)) return false;
    if (how != EWX_LOGOFF) EnableShutdownPrivilege();
    return ExitWindowsEx(how, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED) != FALSE;
}

// Sleep: SetSuspendState from powrprof.dll, loaded on demand to avoid linking another library
bool SleepComputer(HWND owner) {
    if (!Confirm(owner, L"msg.sleep", L"Put the computer to sleep now?")) return false;
    using Fn = BOOLEAN(WINAPI*)(BOOLEAN hibernate, BOOLEAN force, BOOLEAN wakeupEventsDisabled);
    HMODULE mod = LoadLibraryW(L"powrprof.dll");
    auto fn = mod ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(mod, "SetSuspendState"))) : nullptr;
    return fn && fn(FALSE, FALSE, FALSE);
}

// Turn off the display: wait a moment so the hand moving off the button doesn't wake it again
bool MonitorOff(HWND owner) {
    ::Sleep(500);
    SendMessageW(owner, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
    return true;
}

// Presses Win+key. Search (Win+S), Run (Win+R) and Show desktop (Win+D) are left to the system shortcuts:
// there is no file that opens them (shell:::{CLSID} reports "no associated app" on recent Windows)
bool PressWinKey(WORD key) {
    INPUT in[4] = {};
    const WORD keys[4] = {VK_LWIN, key, key, VK_LWIN};
    for (int i = 0; i < 4; ++i) {
        in[i].type = INPUT_KEYBOARD;
        in[i].ki.wVk = keys[i];
        in[i].ki.dwFlags = i >= 2 ? KEYEVENTF_KEYUP : 0;
    }
    return SendInput(4, in, sizeof(INPUT)) == 4;
}

// ---------------------------------------------------------------------------
// System page

struct Entry {
    int group;               // each group starts a new row and wraps when a row is full
    const wchar_t* file;
    const wchar_t* args;
    const wchar_t* nameKey;  // name in the language file
    const wchar_t* name;     // English name used when the language file lacks it
    const wchar_t* icon;     // relative paths start at the exe folder
};

const Entry kSystemPage[] = {
    {0, L"shell:::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", nullptr, L"sys.computer", L"This PC", nullptr},
    {0, L"shell:::{26EE0668-A00A-44D7-9371-BEB064C98683}", nullptr, L"sys.controlPanel", L"Control Panel", nullptr},
    {0, L"shell:::{645FF040-5081-101B-9F08-00AA002F954E}", nullptr, L"sys.recycleBin", L"Recycle Bin", nullptr},
    {0, L"shell:Downloads", nullptr, L"sys.downloads", L"Downloads", nullptr},
    {0, L"shell:Personal", nullptr, L"sys.documents", L"Documents", nullptr},
    {0, L"shell:::{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}", nullptr, L"sys.network", L"Network", nullptr},
    {0, L"ql:EmptyRecycleBin", nullptr, L"sys.emptyBin", L"Empty Recycle Bin", L"icons\\empty-bin.ico"},
    {0, L"ql:Search", nullptr, L"sys.search", L"Search", L"icons\\search.ico"},

    {1, L"ql:Lock", nullptr, L"sys.lock", L"Lock", L"icons\\lock.ico"},
    {1, L"ql:Sleep", nullptr, L"sys.sleep", L"Sleep", L"icons\\sleep.ico"},
    {1, L"ql:SignOut", nullptr, L"sys.signOut", L"Sign out", L"icons\\sign-out.ico"},
    {1, L"ql:Restart", nullptr, L"sys.restart", L"Restart", L"icons\\restart.ico"},
    {1, L"ql:Shutdown", nullptr, L"sys.shutdown", L"Shut down", L"icons\\shutdown.ico"},
    {1, L"ql:ShowDesktop", nullptr, L"sys.desktop", L"Show desktop", L"icons\\desktop.ico"},
    {1, L"ql:MonitorOff", nullptr, L"sys.monitorOff", L"Turn off display", L"icons\\monitor-off.ico"},

    {2, L"%SystemRoot%\\System32\\Taskmgr.exe", nullptr, L"sys.taskmgr", L"Task Manager", nullptr},
    {2, L"ql:Run", nullptr, L"sys.run", L"Run", L"icons\\run.ico"},
    {2, L"shell:AppsFolder\\windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel",
     nullptr, L"sys.settings", L"Settings", nullptr},
    {2, L"%SystemRoot%\\System32\\cmd.exe", nullptr, L"sys.cmd", L"Command Prompt", nullptr},
    {2, L"%SystemRoot%\\System32\\notepad.exe", nullptr, L"sys.notepad", L"Notepad", nullptr},
    {2, L"%SystemRoot%\\System32\\calc.exe", nullptr, L"sys.calc", L"Calculator", nullptr},
    {2, L"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe", nullptr, L"sys.powershell",
     L"PowerShell", nullptr},

    {3, L"shell:::{74246bfc-4c96-11d0-abef-0020af6b0b7a}", nullptr, L"sys.devmgr", L"Device Manager", nullptr},
    {3, L"%SystemRoot%\\System32\\services.msc", nullptr, L"sys.services", L"Services", nullptr},
    {3, L"%SystemRoot%\\regedit.exe", nullptr, L"sys.regedit", L"Registry Editor", nullptr},
    {3, L"%SystemRoot%\\System32\\rundll32.exe", L"sysdm.cpl,EditEnvironmentVariables", L"sys.envVars",
     L"Environment Variables", L"icons\\env-vars.ico"},
    {3, L"shell:::{7b81be6a-ce2b-4676-a29e-eb907a5126c5}", nullptr, L"sys.programs", L"Programs and Features", nullptr},
    {3, L"shell:::{BB06C0E4-D293-4F75-8A90-CB05B6477EEE}", nullptr, L"sys.system", L"System", nullptr},
};

const wchar_t* ItemName(const Entry& e) { return T(e.nameKey, e.name); }

const Entry* FindEntry(const Item& it) {
    for (const Entry& e : kSystemPage)
        if (EqualsNoCase(it.file, e.file) && EqualsNoCase(it.args, e.args ? e.args : L"")) return &e;
    return nullptr;
}

}  // namespace

bool IsBuiltinCommand(const std::wstring& file) { return StartsWithNoCase(file, kPrefix); }

bool RunBuiltinCommand(HWND owner, const std::wstring& file) {
    std::wstring cmd = file.substr(wcslen(kPrefix));
    auto is = [&cmd](const wchar_t* name) { return EqualsNoCase(cmd, name); };
    if (is(L"Lock")) return LockWorkStation() != FALSE;
    if (is(L"Sleep")) return SleepComputer(owner);
    if (is(L"SignOut")) return EndSession(owner, EWX_LOGOFF, L"msg.signOut", L"Sign out now?");
    if (is(L"Restart")) return EndSession(owner, EWX_REBOOT, L"msg.restart", L"Restart the computer now?");
    if (is(L"Shutdown")) return EndSession(owner, EWX_POWEROFF, L"msg.shutdown", L"Shut down the computer now?");
    if (is(L"MonitorOff")) return MonitorOff(owner);
    if (is(L"EmptyRecycleBin")) return SUCCEEDED(SHEmptyRecycleBinW(owner, nullptr, 0));  // the system asks for confirmation itself
    if (is(L"Search")) return PressWinKey('S');
    if (is(L"Run")) return PressWinKey('R');
    if (is(L"ShowDesktop")) return PressWinKey('D');
    return false;
}

Page MakeSystemPage(int cols) {
    Page page;
    page.name = T(L"page.system", L"System");
    Pos pos{-1, 0};
    int group = -1;
    for (const Entry& e : kSystemPage) {
        if (e.group != group || pos.col >= cols) {
            group = e.group;
            pos = Pos{pos.row + 1, 0};
        }
        Item it;
        it.file = e.file;
        if (e.args) it.args = e.args;
        if (e.icon) it.icon = e.icon;
        it.name = ItemName(e);
        page.Put(pos, std::move(it));
        ++pos.col;
    }
    return page;
}

BuiltinNames CurrentBuiltinNames() {
    BuiltinNames names;
    names.defaultPage = T(L"page.default", L"Main");
    names.systemPage = T(L"page.system", L"System");
    for (const Entry& e : kSystemPage) names.items.push_back(ItemName(e));
    return names;
}

bool RenameBuiltins(std::vector<Page>& pages, const BuiltinNames& old) {
    BuiltinNames now = CurrentBuiltinNames();
    bool changed = false;
    auto rename = [&changed](std::wstring& name, const std::wstring& from, const std::wstring& to) {
        if (name == from && from != to) {
            name = to;
            changed = true;
        }
    };
    auto renamePage = [&](Page& page) {
        rename(page.name, old.defaultPage, now.defaultPage);
        rename(page.name, old.systemPage, now.systemPage);
        for (Cell& c : page.cells)
            if (const Entry* e = FindEntry(c.item)) {
                size_t i = e - kSystemPage;
                rename(c.item.name, old.items[i], now.items[i]);
            }
    };
    for (Page& page : pages) {
        renamePage(page);
        for (Page& sub : page.subs) renamePage(sub);
    }
    return changed;
}
