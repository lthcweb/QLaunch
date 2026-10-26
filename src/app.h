// Process-wide data: settings, pages and the icon cache. There is exactly one instance.
#pragma once

#include "icon/icon.h"
#include "model/model.h"
#include "model/settings.h"

struct App {
    Settings settings;
    Store store;
    IconCache iconCache;
    IconLoader iconLoader{iconCache};

    std::wstring settingsPath;  // QLaunch.ini
    std::wstring dataPath;      // data.ini

    void SaveSettings() const { settings.Save(settingsPath); }
    void SaveData() const { store.Save(dataPath); }
};
