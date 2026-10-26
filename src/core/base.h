// Common header: Windows headers, basic types and the app's private messages
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

#define QL_NO_COPY(T)        \
    T(const T&) = delete;     \
    T& operator=(const T&) = delete

// Private window messages
enum : UINT {
    WM_APP_SHOW = WM_APP + 1,   // show the window (also sent when a second instance starts)
    WM_APP_CORNER_ENTER,        // the mouse entered the hot corner
    WM_APP_CORNER_LEAVE,        // the mouse left the hot corner
    WM_APP_ICON_READY,          // the background thread finished extracting an icon
    WM_APP_REDRAW,              // repaint a layered window (several requests are merged)
};

constexpr wchar_t kAppName[] = L"QLaunch";
constexpr wchar_t kMainClass[] = L"QLaunch.Main";
