// Data model: page -> buttons + sub-pages (second-level tabs) -> buttons. Only two levels.
#pragma once

#include "core/base.h"

// One program button. Empty name and file means an empty cell.
struct Item {
    std::wstring name;
    std::wstring file;      // program, document, folder, or a virtual object such as shell:::{CLSID}
    std::wstring args;
    std::wstring dir;       // working folder; empty means the program's own folder
    std::wstring icon;      // custom icon file; empty means the icon of file
    int iconIndex = 0;
    std::wstring tip;
    int show = SW_SHOWNORMAL;
    bool admin = false;     // run as administrator

    bool IsEmpty() const { return name.empty() && file.empty(); }
};

// A cell position. Buttons keep their row and column when the window is resized.
struct Pos {
    int row = -1, col = -1;
    bool operator==(const Pos& o) const { return row == o.row && col == o.col; }
    bool operator<(const Pos& o) const { return row != o.row ? row < o.row : col < o.col; }
};

// A cell that holds a button
struct Cell {
    Pos pos;
    Item item;
};

struct Page {
    std::wstring name;
    std::vector<Cell> cells;        // only cells that hold a button, sorted row-major
    std::vector<Page> subs;         // sub-pages (second-level tabs); a sub-page has no subs of its own

    int Find(Pos pos) const;           // index into cells; -1 for an empty cell
    Item* At(Pos pos);                 // nullptr for an empty cell
    void Put(Pos pos, Item item);      // an empty item clears the cell
    Pos FirstFree(int cols) const;     // first empty cell in row-major order
    int RowCount() const;              // number of rows up to the last button
};

class Store {
public:
    // legacyCols: the old format saved positions by index; convert them with the column count of that time
    bool Load(const std::wstring& path, int legacyCols);
    bool Save(const std::wstring& path) const;

    std::vector<Page> pages;
    bool legacy = false;  // an old format was read; save in the new format right away so a later column change can't break the conversion
};
