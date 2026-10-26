#include "trigger/trigger.h"

#include <commctrl.h>

#include <algorithm>
#include <climits>

namespace trigger {
namespace {

constexpr int kHotkeyId = 1;
constexpr int kCornerSize = 4;  // within this many pixels of a corner counts as the hot corner

HHOOK g_hook = nullptr;
HWND g_notify = nullptr;
HotCorner g_corner = HotCorner::None;
bool g_inCorner = false;
std::vector<RECT> g_monitors;   // cached monitor rectangles; the hook callback makes no system calls

BOOL CALLBACK AddMonitor(HMONITOR, HDC, LPRECT rc, LPARAM) {
    g_monitors.push_back(*rc);
    return TRUE;
}

// The hook gets where the mouse "wants" to go, before the screen edge stops it: push a little further into a
// corner
// and the point is off screen, e.g. x = -6. So pull pt back onto the nearest monitor before testing the corner.
bool InCorner(POINT& pt) {
    const RECT* mon = nullptr;
    long best = LONG_MAX;
    for (const RECT& m : g_monitors) {
        long dx = pt.x < m.left ? m.left - pt.x : pt.x >= m.right ? pt.x - m.right + 1 : 0;
        long dy = pt.y < m.top ? m.top - pt.y : pt.y >= m.bottom ? pt.y - m.bottom + 1 : 0;
        if (dx + dy < best) {
            best = dx + dy;
            mon = &m;
        }
    }
    if (!mon) return false;
    const RECT& m = *mon;
    pt.x = std::min(std::max(pt.x, m.left), m.right - 1);
    pt.y = std::min(std::max(pt.y, m.top), m.bottom - 1);
    bool left = pt.x < m.left + kCornerSize, right = pt.x >= m.right - kCornerSize;
    bool top = pt.y < m.top + kCornerSize, bottom = pt.y >= m.bottom - kCornerSize;
    switch (g_corner) {
        case HotCorner::LeftTop: return left && top;
        case HotCorner::RightTop: return right && top;
        case HotCorner::LeftBottom: return left && bottom;
        case HotCorner::RightBottom: return right && bottom;
        default: return false;
    }
}

LRESULT CALLBACK MouseProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == WM_MOUSEMOVE) {
        POINT pt = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp)->pt;
        bool in = InCorner(pt);
        if (in != g_inCorner) {
            g_inCorner = in;
            PostMessageW(g_notify, in ? WM_APP_CORNER_ENTER : WM_APP_CORNER_LEAVE, 0, MAKELPARAM(pt.x, pt.y));
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

}  // namespace

void RefreshMonitors() {
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, 0);
}

void StartHotCorner(HWND notify, HotCorner corner) {
    StopHotCorner();
    if (corner == HotCorner::None) return;
    g_notify = notify;
    g_corner = corner;
    g_inCorner = false;
    RefreshMonitors();
    g_hook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, GetModuleHandleW(nullptr), 0);
}

void StopHotCorner() {
    if (g_hook) UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
}

bool RegisterHotkey(HWND wnd, WORD value) {
    UnregisterHotkey(wnd);
    UINT vk = LOBYTE(value), f = HIBYTE(value);
    if (!vk) return true;
    UINT mods = MOD_NOREPEAT;
    if (f & HOTKEYF_ALT) mods |= MOD_ALT;
    if (f & HOTKEYF_CONTROL) mods |= MOD_CONTROL;
    if (f & HOTKEYF_SHIFT) mods |= MOD_SHIFT;
    return RegisterHotKey(wnd, kHotkeyId, mods, vk) != FALSE;
}

void UnregisterHotkey(HWND wnd) { UnregisterHotKey(wnd, kHotkeyId); }

bool IsForegroundFullscreen(POINT pt) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == GetDesktopWindow() || fg == GetShellWindow()) return false;
    wchar_t cls[32];
    GetClassNameW(fg, cls, 32);
    if (lstrcmpW(cls, L"WorkerW") == 0 || lstrcmpW(cls, L"Progman") == 0) return false;

    RECT wr;
    MONITORINFO mi{sizeof(mi)};
    if (!GetWindowRect(fg, &wr) ||
        !GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    return wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top &&
           wr.right >= mi.rcMonitor.right && wr.bottom >= mi.rcMonitor.bottom;
}

}  // namespace trigger
