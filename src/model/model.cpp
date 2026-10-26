#include "model/model.h"

#include <cwchar>

#include "core/ini.h"
#include "core/str.h"

#include <algorithm>

int Page::Find(Pos pos) const {
    for (int i = 0; i < (int)cells.size(); ++i)
        if (cells[i].pos == pos) return i;
    return -1;
}

Item* Page::At(Pos pos) {
    int i = Find(pos);
    return i >= 0 ? &cells[i].item : nullptr;
}

void Page::Put(Pos pos, Item item) {
    if (pos.row < 0 || pos.col < 0) return;
    int i = Find(pos);
    if (item.IsEmpty()) {
        if (i >= 0) cells.erase(cells.begin() + i);
    } else if (i >= 0) {
        cells[i].item = std::move(item);
    } else {
        auto at = std::lower_bound(cells.begin(), cells.end(), pos,
                                   [](const Cell& c, const Pos& p) { return c.pos < p; });
        cells.insert(at, Cell{pos, std::move(item)});
    }
}

Pos Page::FirstFree(int cols) const {
    Pos p{0, 0};
    for (const Cell& c : cells) {  // cells are sorted row-major, so the first gap is the answer
        if (c.pos.col >= cols) continue;
        if (!(c.pos == p)) break;
        if (++p.col == cols) p = Pos{p.row + 1, 0};
    }
    return p;
}

int Page::RowCount() const {
    return cells.empty() ? 0 : cells.back().pos.row + 1;
}

// ---------------------------------------------------------------------------
// data.ini format (section names are unique, so any ini reader can read it):
//   [P03]             page 3
//   [P03.R01C05]      the button of page 3 at row 1, column 5 (all counted from 0)
//   [P03.S01]         sub-page 1 of page 3
//   [P03.S01.R01C05]  the button of that sub-page at row 1, column 5
// Older versions saved buttons by index, e.g. [P03.B007]; these are converted to rows and columns using the
// column count and saved in the new format.
// ---------------------------------------------------------------------------

namespace {

void ReadItem(const Ini::Section& s, Item& it) {
    it.name = s.Get(L"Name");
    it.file = s.Get(L"File");
    it.args = s.Get(L"Args");
    it.dir = s.Get(L"Dir");
    it.icon = s.Get(L"Icon");
    it.iconIndex = s.GetInt(L"IconIndex", 0);
    it.tip = s.Get(L"Tip");
    it.show = s.GetInt(L"Show", SW_SHOWNORMAL);
    it.admin = s.GetInt(L"Admin", 0) != 0;
}

void WriteItem(Ini::Section& s, const Item& it) {
    s.Set(L"Name", it.name);
    s.Set(L"File", it.file);
    if (!it.args.empty()) s.Set(L"Args", it.args);
    if (!it.dir.empty()) s.Set(L"Dir", it.dir);
    if (!it.icon.empty()) {
        s.Set(L"Icon", it.icon);
        s.SetInt(L"IconIndex", it.iconIndex);
    }
    if (!it.tip.empty()) s.Set(L"Tip", it.tip);
    if (it.show != SW_SHOWNORMAL) s.SetInt(L"Show", it.show);
    if (it.admin) s.SetInt(L"Admin", 1);
}

// keeps a damaged file with huge numbers from causing a huge allocation
constexpr int kMaxPages = 100;
constexpr int kMaxRows = 1000;
constexpr int kMaxCols = 50;
constexpr int kMaxCells = kMaxRows * kMaxCols;
bool InRange(int v, int max) { return v >= 0 && v < max; }

Page& EnsurePage(std::vector<Page>& v, int index) {
    if (index >= (int)v.size()) v.resize(index + 1);
    return v[index];
}

void WriteCells(Ini& ini, const std::wstring& prefix, const Page& page) {
    wchar_t name[64];
    for (const Cell& c : page.cells) {
        swprintf(name, 64, L"%ls.R%02dC%02d", prefix.c_str(), c.pos.row, c.pos.col);
        WriteItem(ini.Add(name), c.item);
    }
}

// Parses the cell position after prefix in a section name: new format R01C05, old format B007
bool ParseCell(const wchar_t* s, int legacyCols, Pos& pos, bool& legacy) {
    int r = -1, c = -1, b = -1, n = 0;
    if (swscanf(s, L"R%dC%d%n", &r, &c, &n) == 2 && s[n] == 0) {
        if (!InRange(r, kMaxRows) || !InRange(c, kMaxCols)) return false;
        pos = Pos{r, c};
        return true;
    }
    if (swscanf(s, L"B%d%n", &b, &n) == 1 && s[n] == 0) {
        if (!InRange(b, kMaxCells)) return false;
        pos = Pos{b / legacyCols, b % legacyCols};
        legacy = true;
        return true;
    }
    return false;
}

}  // namespace

bool Store::Load(const std::wstring& path, int legacyCols) {
    pages.clear();
    legacy = false;
    Ini ini;
    if (!ini.Load(path)) return false;
    legacyCols = std::max(1, legacyCols);

    for (auto& sec : ini.Sections()) {
        int p = -1, s = -1, n = 0;
        Pos pos;
        const wchar_t* name = sec.name.c_str();
        // try the formats from longest to shortest; %n confirms that the whole name matched
        if (swscanf(name, L"P%d.S%d.%n", &p, &s, &n) == 2 && n > 0 &&
            ParseCell(name + n, legacyCols, pos, legacy)) {
            if (!InRange(p, kMaxPages) || !InRange(s, kMaxPages)) continue;
            Item it;
            ReadItem(sec, it);
            EnsurePage(EnsurePage(pages, p).subs, s).Put(pos, std::move(it));
        } else if (swscanf(name, L"P%d.S%d%n", &p, &s, &n) == 2 && name[n] == 0) {
            if (!InRange(p, kMaxPages) || !InRange(s, kMaxPages)) continue;
            EnsurePage(EnsurePage(pages, p).subs, s).name = sec.Get(L"Name");
        } else if (swscanf(name, L"P%d.%n", &p, &n) == 1 && n > 0 &&
                   ParseCell(name + n, legacyCols, pos, legacy)) {
            if (!InRange(p, kMaxPages)) continue;
            Item it;
            ReadItem(sec, it);
            EnsurePage(pages, p).Put(pos, std::move(it));
        } else if (swscanf(name, L"P%d%n", &p, &n) == 1 && name[n] == 0) {
            if (!InRange(p, kMaxPages)) continue;
            EnsurePage(pages, p).name = sec.Get(L"Name");
        }
    }
    return true;
}

bool Store::Save(const std::wstring& path) const {
    Ini ini;
    wchar_t name[64];
    for (int p = 0; p < (int)pages.size(); ++p) {
        const Page& page = pages[p];
        swprintf(name, 64, L"P%02d", p);
        std::wstring prefix = name;
        Ini::Section& sec = ini.Add(prefix);
        sec.Set(L"Name", page.name);
        WriteCells(ini, prefix, page);

        for (int s = 0; s < (int)page.subs.size(); ++s) {
            swprintf(name, 64, L"%ls.S%02d", prefix.c_str(), s);
            ini.Add(name).Set(L"Name", page.subs[s].name);
            WriteCells(ini, name, page.subs[s]);
        }
    }
    return ini.Save(path);
}
