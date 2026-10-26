// Built-in features:
//   1. Commands the app runs itself; the button's File is ql:<name>, e.g. ql:Lock (lock the PC)
//   2. The "System" page created on first run (This PC, Control Panel, Shut down, Task Manager...)
#pragma once

#include "core/base.h"
#include "model/model.h"

bool IsBuiltinCommand(const std::wstring& file);
// Runs a ql: command. Sleep, sign out, restart and shut down ask first
bool RunBuiltinCommand(HWND owner, const std::wstring& file);

// Names come from the current language file. cols is the current number of cells per row, used for the layout
Page MakeSystemPage(int cols);

// Renames the built-in pages and buttons when the language changes: remember the old names before switching,
// then rename whatever still has an old name. Anything the user renamed is left alone
struct BuiltinNames {
    std::wstring defaultPage, systemPage;
    std::vector<std::wstring> items;  // one per button of the System page, in order
};
BuiltinNames CurrentBuiltinNames();
bool RenameBuiltins(std::vector<Page>& pages, const BuiltinNames& old);
