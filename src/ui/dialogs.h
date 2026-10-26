// Modal dialogs: button properties, enter a name, settings. They return true when the user clicked OK.
#pragma once

#include "core/base.h"
#include "model/model.h"
#include "model/settings.h"

bool EditItemDialog(HWND owner, Item& item);
bool InputDialog(HWND owner, const wchar_t* title, const wchar_t* label, std::wstring& text);
// preview: called when the skin or a font changes in the dialog so the main window shows it at once (ctx is
// passed back).
// On cancel the caller restores the previous settings.
using SettingsPreview = void (*)(void* ctx, const Settings& s);
bool SettingsDialog(HWND owner, Settings& settings, bool& autoStart, SettingsPreview preview, void* ctx);
