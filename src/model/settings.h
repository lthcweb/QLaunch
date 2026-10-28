// Runtime settings, stored in QLaunch.ini next to the exe. Sizes are stored at 96 DPI and scaled per monitor
// when used.
#pragma once

#include "core/base.h"

enum class TabSide { Left, Top, Right, Bottom };
enum class HotCorner { None, LeftTop, RightTop, LeftBottom, RightBottom };
enum class ShowAt { Mouse, LastPos };

// Font chosen for tabs or buttons. An empty face means not set: the general font with the skin's size and
// weight.
// When set it decides the face and size only; bold is the skin's OR this one (the skin may bold the current
// tab, this can't undo that).
struct FontSpec {
    std::wstring face;
    int size = 9;  // points
    bool bold = false;

    bool Empty() const { return face.empty(); }
    std::wstring ToString() const;  // ini form: "Microsoft YaHei UI,10,bold"; an empty string means not set
    static FontSpec Parse(const std::wstring& s);
};

struct Settings {
    // [General]
    std::wstring language;          // empty = follow the system
    std::wstring skin = L"Dark";    // Dark / Light colour scheme, or the name of a skin in the skins folder
    bool systemPage = false;        // the System page has been created (it isn't recreated after the user deletes it)

    // [Trigger]
    HotCorner hotCorner = HotCorner::LeftBottom;
    int cornerDelay = 0;            // how many ms the mouse must stay in the hot corner before the window opens
    WORD hotkey = 0;                // low byte virtual key, high byte HOTKEYF_* (same as the hotkey control)
    ShowAt showAt = ShowAt::Mouse;
    bool tabClick = false;          // switch tabs by clicking them instead of hovering
    int tabHoverDelay = 20;         // ms of hovering over a tab before switching to it (when tabClick is off)

    // [Layout]
    TabSide tabSide = TabSide::Left;
    int cols = 9, rows = 4;         // grid rows and columns, updated when the window border is dragged
    int cellW = 64, cellH = 64;
    int cellGap = 4;
    int iconSize = 32;
    int tabW = 56, tabH = 30;       // tabs on the left/right: each tab's size; on the top/bottom: the width is a minimum
    int captionH = 22;
    int fontSize = 9;               // points
    std::wstring fontName = L"Microsoft YaHei UI";
    FontSpec tabFont, buttonFont;

    // [Window]
    int posX = 100, posY = 100;
    int lastPage = 0;
    bool pinned = false;            // when pinned the window stays open after losing focus

    void Load(const std::wstring& path);
    void Save(const std::wstring& path) const;
};

// Start with Windows (writes HKCU\...\Run)
bool IsAutoStart();
void SetAutoStart(bool on);
