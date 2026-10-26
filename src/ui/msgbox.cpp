#include "ui/msgbox.h"

#include <algorithm>

namespace {

HHOOK g_hook = nullptr;

// Before the box activates, centre it on the mouse and keep it fully on screen
LRESULT CALLBACK CbtProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_ACTIVATE) {
        auto box = reinterpret_cast<HWND>(wp);
        RECT rc;
        GetWindowRect(box, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        POINT pt;
        GetCursorPos(&pt);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& work = mi.rcWork;
        int x = std::max<int>(work.left, std::min<int>(pt.x - w / 2, work.right - w));
        int y = std::max<int>(work.top, std::min<int>(pt.y - h / 2, work.bottom - h));
        SetWindowPos(box, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
        return 0;
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

}  // namespace

int MessageBoxAtCursor(HWND owner, const wchar_t* text, UINT type) {
    g_hook = SetWindowsHookExW(WH_CBT, CbtProc, nullptr, GetCurrentThreadId());
    int ret = MessageBoxW(owner, text, kAppName, type);
    if (g_hook) {  // never activated (e.g. creation failed)
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    return ret;
}
