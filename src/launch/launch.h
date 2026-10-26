// Starting programs and opening their folder
#pragma once

#include "core/base.h"
#include "model/model.h"

bool LaunchItem(HWND owner, const Item& item, bool forceAdmin = false);
void OpenItemFolder(const Item& item);
