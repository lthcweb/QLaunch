#include "model/settings.h"

#include <algorithm>

#include "core/ini.h"
#include "core/str.h"

namespace {

int Clamp(int v, int lo, int hi) { return std::min(std::max(v, lo), hi); }

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

}  // namespace

std::wstring FontSpec::ToString() const {
    if (face.empty()) return {};
    return face + L"," + FromInt(size) + (bold ? L",bold" : L"");
}

FontSpec FontSpec::Parse(const std::wstring& s) {
    FontSpec f;
    size_t c1 = s.find(L',');
    f.face = Trim(s.substr(0, c1));
    if (f.face.empty() || c1 == std::wstring::npos) return f;
    size_t c2 = s.find(L',', c1 + 1);
    f.size = Clamp(ToInt(s.substr(c1 + 1, c2 - c1 - 1), 9), 6, 40);
    f.bold = c2 != std::wstring::npos && Trim(s.substr(c2 + 1)) == L"bold";
    return f;
}

void Settings::Load(const std::wstring& path) {
    Ini ini;
    if (!ini.Load(path)) return;  // first run: keep the defaults

    language = ini.GetStr(L"General", L"Language");
    skin = ini.GetStr(L"General", L"Skin", skin.c_str());
    systemPage = ini.GetInt(L"General", L"SystemPage", 0) != 0;

    hotCorner = (HotCorner)Clamp(ini.GetInt(L"Trigger", L"HotCorner", (int)hotCorner), 0, 4);
    cornerDelay = Clamp(ini.GetInt(L"Trigger", L"CornerDelay", cornerDelay), 0, 5000);
    hotkey = (WORD)ini.GetInt(L"Trigger", L"HotKey", hotkey);
    showAt = (ShowAt)Clamp(ini.GetInt(L"Trigger", L"ShowAt", (int)showAt), 0, 1);
    tabHoverDelay = Clamp(ini.GetInt(L"Trigger", L"TabHoverDelay", tabHoverDelay), -1, 5000);

    tabSide = (TabSide)Clamp(ini.GetInt(L"Layout", L"TabSide", (int)tabSide), 0, 3);
    cols = Clamp(ini.GetInt(L"Layout", L"Cols", cols), 1, 50);
    rows = Clamp(ini.GetInt(L"Layout", L"Rows", rows), 1, 50);
    cellW = Clamp(ini.GetInt(L"Layout", L"CellWidth", cellW), 24, 400);
    cellH = Clamp(ini.GetInt(L"Layout", L"CellHeight", cellH), 24, 400);
    cellGap = Clamp(ini.GetInt(L"Layout", L"CellGap", cellGap), 0, 40);
    iconSize = Clamp(ini.GetInt(L"Layout", L"IconSize", iconSize), 16, 256);
    tabW = Clamp(ini.GetInt(L"Layout", L"TabWidth", tabW), 16, 400);
    tabH = Clamp(ini.GetInt(L"Layout", L"TabHeight", tabH), 12, 200);
    captionH = Clamp(ini.GetInt(L"Layout", L"CaptionHeight", captionH), 0, 100);
    fontSize = Clamp(ini.GetInt(L"Layout", L"FontSize", fontSize), 6, 40);
    fontName = ini.GetStr(L"Layout", L"FontName", fontName.c_str());
    tabFont = FontSpec::Parse(ini.GetStr(L"Layout", L"TabFont"));
    buttonFont = FontSpec::Parse(ini.GetStr(L"Layout", L"ButtonFont"));

    posX = ini.GetInt(L"Window", L"X", posX);
    posY = ini.GetInt(L"Window", L"Y", posY);
    lastPage = std::max(0, ini.GetInt(L"Window", L"LastPage", lastPage));
    pinned = ini.GetInt(L"Window", L"Pinned", 0) != 0;
}

void Settings::Save(const std::wstring& path) const {
    Ini ini;
    Ini::Section& g = ini.Get(L"General");
    g.Set(L"Language", language);
    g.Set(L"Skin", skin);
    g.SetInt(L"SystemPage", systemPage ? 1 : 0);

    Ini::Section& t = ini.Get(L"Trigger");
    t.SetInt(L"HotCorner", (int)hotCorner);
    t.SetInt(L"CornerDelay", cornerDelay);
    t.SetInt(L"HotKey", hotkey);
    t.SetInt(L"ShowAt", (int)showAt);
    t.SetInt(L"TabHoverDelay", tabHoverDelay);

    Ini::Section& l = ini.Get(L"Layout");
    l.SetInt(L"TabSide", (int)tabSide);
    l.SetInt(L"Cols", cols);
    l.SetInt(L"Rows", rows);
    l.SetInt(L"CellWidth", cellW);
    l.SetInt(L"CellHeight", cellH);
    l.SetInt(L"CellGap", cellGap);
    l.SetInt(L"IconSize", iconSize);
    l.SetInt(L"TabWidth", tabW);
    l.SetInt(L"TabHeight", tabH);
    l.SetInt(L"CaptionHeight", captionH);
    l.SetInt(L"FontSize", fontSize);
    l.Set(L"FontName", fontName);
    l.Set(L"TabFont", tabFont.ToString());
    l.Set(L"ButtonFont", buttonFont.ToString());

    Ini::Section& w = ini.Get(L"Window");
    w.SetInt(L"X", posX);
    w.SetInt(L"Y", posY);
    w.SetInt(L"LastPage", lastPage);
    w.SetInt(L"Pinned", pinned ? 1 : 0);

    ini.Save(path);
}

bool IsAutoStart() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, kAppName, RRF_RT_REG_SZ, nullptr, nullptr,
                        nullptr) == ERROR_SUCCESS;
}

void SetAutoStart(bool on) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return;
    if (on) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(exe) + L"\"";
        RegSetValueExW(key, kAppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kAppName);
    }
    RegCloseKey(key);
}
