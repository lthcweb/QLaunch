// Message box at the mouse position instead of the centre of the screen.
// The launcher opens near the mouse, so confirmations follow the mouse and the user needn't look or move to the
// screen centre.
#pragma once

#include "core/base.h"

// Like MessageBoxW; the title is always the app name
int MessageBoxAtCursor(HWND owner, const wchar_t* text, UINT type);
