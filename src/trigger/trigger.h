// Ways to open the window: the hot corner (low-level mouse hook, no DLL needed) and a global hotkey.
#pragma once

#include "core/base.h"
#include "model/settings.h"

namespace trigger {

// Posts WM_APP_CORNER_ENTER / WM_APP_CORNER_LEAVE to notify when the mouse enters/leaves the hot corner.
// The hook callback does as little as possible; delays and showing are handled by the window.
void StartHotCorner(HWND notify, HotCorner corner);
void StopHotCorner();
void RefreshMonitors();  // call when the monitor layout changes

// value is the same as the hotkey control's: low byte virtual key, high byte HOTKEYF_*
bool RegisterHotkey(HWND wnd, WORD value);
void UnregisterHotkey(HWND wnd);

// Don't open while a full-screen program (game, video) is in front
bool IsForegroundFullscreen(POINT pt);

}  // namespace trigger
