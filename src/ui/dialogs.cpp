#include "ui/dialogs.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>

#include "core/path.h"
#include "core/str.h"
#include "i18n/i18n.h"
#include "resource.h"
#include "skin/skin.h"

namespace {

std::wstring GetText(HWND dlg, int id) {
    HWND ctl = GetDlgItem(dlg, id);
    int n = GetWindowTextLengthW(ctl);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(ctl, &s[0], n + 1);
    s.resize(n);
    return Trim(s);
}

void AddCombo(HWND dlg, int id, const wchar_t* text) {
    SendDlgItemMessageW(dlg, id, CB_ADDSTRING, 0, (LPARAM)text);
}
int ComboSel(HWND dlg, int id) { return (int)SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0); }
void SetComboSel(HWND dlg, int id, int index) { SendDlgItemMessageW(dlg, id, CB_SETCURSEL, index, 0); }

bool BrowseFile(HWND owner, std::wstring& path, const wchar_t* filter) {
    wchar_t buf[MAX_PATH * 2] = {};
    lstrcpynW(buf, ExpandEnv(path).c_str(), MAX_PATH * 2);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 2;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NODEREFERENCELINKS | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

// ---------------------------------------------------------------------------
// Button properties

const int kShowValues[] = {SW_SHOWNORMAL, SW_SHOWMINNOACTIVE, SW_SHOWMAXIMIZED};

const i18n::DlgText kItemTexts[] = {
    {0, L"item.title"},
    {IDC_NAME_LABEL, L"item.name"},
    {IDC_FILE_LABEL, L"item.file"},
    {IDC_ARGS_LABEL, L"item.args"},
    {IDC_DIR_LABEL, L"item.dir"},
    {IDC_ICON_LABEL, L"item.icon"},
    {IDC_ICON_INDEX_LABEL, L"item.iconIndex"},
    {IDC_SHOW_LABEL, L"item.show"},
    {IDC_TIP_LABEL, L"item.tip"},
    {IDC_ADMIN, L"item.admin"},
    {IDOK, L"common.ok"},
    {IDCANCEL, L"common.cancel"},
};

INT_PTR CALLBACK ItemProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* item = reinterpret_cast<Item*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG: {
            item = reinterpret_cast<Item*>(lp);
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            i18n::ApplyDialog(dlg, kItemTexts, _countof(kItemTexts));
            SetDlgItemTextW(dlg, IDC_NAME, item->name.c_str());
            SetDlgItemTextW(dlg, IDC_FILE, item->file.c_str());
            SetDlgItemTextW(dlg, IDC_ARGS, item->args.c_str());
            SetDlgItemTextW(dlg, IDC_DIR, item->dir.c_str());
            SetDlgItemTextW(dlg, IDC_ICONFILE, item->icon.c_str());
            SetDlgItemInt(dlg, IDC_ICON_INDEX, item->iconIndex, TRUE);
            SetDlgItemTextW(dlg, IDC_TIP, item->tip.c_str());
            AddCombo(dlg, IDC_SHOW, T(L"item.show.normal", L"Normal window"));
            AddCombo(dlg, IDC_SHOW, T(L"item.show.min", L"Minimized"));
            AddCombo(dlg, IDC_SHOW, T(L"item.show.max", L"Maximized"));
            int sel = 0;
            for (int i = 0; i < 3; ++i)
                if (kShowValues[i] == item->show) sel = i;
            SetComboSel(dlg, IDC_SHOW, sel);
            CheckDlgButton(dlg, IDC_ADMIN, item->admin ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_FILE_BROWSE: {
                    std::wstring path = GetText(dlg, IDC_FILE);
                    if (BrowseFile(dlg, path, L"*.*\0*.*\0")) {
                        SetDlgItemTextW(dlg, IDC_FILE, path.c_str());
                        if (GetText(dlg, IDC_NAME).empty())
                            SetDlgItemTextW(dlg, IDC_NAME, FileTitle(path).c_str());
                    }
                    return TRUE;
                }
                case IDC_ICON_BROWSE: {
                    std::wstring path = GetText(dlg, IDC_ICONFILE);
                    if (BrowseFile(dlg, path, L"*.ico;*.exe;*.dll\0*.ico;*.exe;*.dll\0*.*\0*.*\0"))
                        SetDlgItemTextW(dlg, IDC_ICONFILE, path.c_str());
                    return TRUE;
                }
                case IDOK: {
                    if (GetText(dlg, IDC_FILE).empty()) {
                        SetFocus(GetDlgItem(dlg, IDC_FILE));
                        return TRUE;
                    }
                    item->name = GetText(dlg, IDC_NAME);
                    item->file = NormalizeUrl(GetText(dlg, IDC_FILE));
                    if (item->name.empty())
                        item->name = IsUrl(item->file) ? UrlSiteName(item->file) : FileTitle(item->file);
                    item->args = GetText(dlg, IDC_ARGS);
                    item->dir = GetText(dlg, IDC_DIR);
                    item->icon = GetText(dlg, IDC_ICONFILE);
                    item->iconIndex = (int)GetDlgItemInt(dlg, IDC_ICON_INDEX, nullptr, TRUE);
                    item->tip = GetText(dlg, IDC_TIP);
                    int sel = ComboSel(dlg, IDC_SHOW);
                    item->show = kShowValues[sel >= 0 && sel < 3 ? sel : 0];
                    item->admin = IsDlgButtonChecked(dlg, IDC_ADMIN) == BST_CHECKED;
                    EndDialog(dlg, IDOK);
                    return TRUE;
                }
                case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
            }
            break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Enter a name

struct InputArgs {
    const wchar_t* title;
    const wchar_t* label;
    std::wstring* text;
};

INT_PTR CALLBACK InputProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* args = reinterpret_cast<InputArgs*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG:
            args = reinterpret_cast<InputArgs*>(lp);
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            SetWindowTextW(dlg, args->title);
            SetDlgItemTextW(dlg, IDC_INPUT_LABEL, args->label);
            SetDlgItemTextW(dlg, IDC_INPUT, args->text->c_str());
            SetDlgItemTextW(dlg, IDOK, T(L"common.ok", L"OK"));
            SetDlgItemTextW(dlg, IDCANCEL, T(L"common.cancel", L"Cancel"));
            SendDlgItemMessageW(dlg, IDC_INPUT, EM_SETSEL, 0, -1);
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                std::wstring s = GetText(dlg, IDC_INPUT);
                if (s.empty()) return TRUE;
                *args->text = s;
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// Settings

const int kIconSizes[] = {16, 24, 32, 48, 64, 96};

const i18n::DlgText kSettingsTexts[] = {
    {0, L"settings.title"},
    {IDC_GROUP_TRIGGER, L"settings.groupTrigger"},
    {IDC_CORNER_LABEL, L"settings.corner"},
    {IDC_DELAY_LABEL, L"settings.delay"},
    {IDC_HOTKEY_LABEL, L"settings.hotkey"},
    {IDC_SHOWAT_LABEL, L"settings.showAt"},
    {IDC_GROUP_LOOK, L"settings.groupLook"},
    {IDC_LANG_LABEL, L"settings.language"},
    {IDC_SKIN_LABEL, L"settings.skin"},
    {IDC_TABSIDE_LABEL, L"settings.tabSide"},
    {IDC_CELL_LABEL, L"settings.cellSize"},
    {IDC_TAB_LABEL, L"settings.tabSize"},
    {IDC_ICONSIZE_LABEL, L"settings.iconSize"},
    {IDC_HOVER_LABEL, L"settings.hover"},
    {IDC_TABCLICK, L"settings.tabClick"},
    {IDC_AUTOSTART, L"settings.autoStart"},
    {IDOK, L"common.ok"},
    {IDCANCEL, L"common.cancel"},
};

struct SettingsArgs {
    Settings* s;  // the dialog edits a copy; the caller gets it only on OK
    bool* autoStart;
    std::vector<i18n::Language> langs;
    std::vector<std::wstring> skins;
    SettingsPreview preview;
    void* ctx;
};

// The hover delay only applies when tabs switch on hover
void EnableHover(HWND dlg) {
    bool hover = IsDlgButtonChecked(dlg, IDC_TABCLICK) != BST_CHECKED;
    EnableWindow(GetDlgItem(dlg, IDC_HOVER_LABEL), hover);
    EnableWindow(GetDlgItem(dlg, IDC_HOVER), hover);
}

// The font buttons show the current choice, e.g. "Font: Segoe UI 10"
void ShowFont(HWND dlg, int id, const FontSpec& f) {
    std::wstring text = T(L"settings.font", L"Font:");
    if (!text.empty() && text.back() == L':') text += L' ';
    if (f.Empty()) text += T(L"settings.fontTheme", L"From skin");
    else text += f.face + L" " + FromInt(f.size) + (f.bold ? L" B" : L"");
    SetDlgItemTextW(dlg, id, text.c_str());
}

// Font button clicked: pops up "Choose font... / From skin". Returns true when something changed
bool PickFont(HWND dlg, int id, FontSpec& f, const Settings& s) {
    enum { kChoose = 1, kTheme };
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kChoose, T(L"settings.fontChoose", L"Choose font..."));
    AppendMenuW(menu, MF_STRING | (f.Empty() ? MF_CHECKED : 0), kTheme, T(L"settings.fontTheme", L"From skin"));
    RECT rc;
    GetWindowRect(GetDlgItem(dlg, id), &rc);
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD, rc.left, rc.bottom, 0, dlg, nullptr);
    DestroyMenu(menu);
    if (cmd == kTheme) {
        bool changed = !f.Empty();
        f = FontSpec{};
        return changed;
    }
    if (cmd != kChoose) return false;

    HDC dc = GetDC(dlg);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(dlg, dc);
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(f.Empty() ? s.fontSize : f.size, dpi, 72);
    lf.lfWeight = f.bold ? FW_BOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpynW(lf.lfFaceName, (f.Empty() ? s.fontName : f.face).c_str(), LF_FACESIZE);
    CHOOSEFONTW cf{sizeof(cf)};
    cf.hwndOwner = dlg;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;
    if (!ChooseFontW(&cf)) return false;
    f.face = lf.lfFaceName;
    f.size = std::min(40, std::max(6, cf.iPointSize / 10));
    f.bold = lf.lfWeight >= FW_BOLD;
    return true;
}

// Puts the dialog next to the main window so previews stay visible; if neither side fits it stays centred
void PlaceBeside(HWND dlg, HWND owner) {
    RECT o, d;
    if (!IsWindowVisible(owner) || !GetWindowRect(owner, &o) || !GetWindowRect(dlg, &d)) return;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT& work = mi.rcWork;
    int w = d.right - d.left, h = d.bottom - d.top, gap = 8;
    int x;
    if (o.right + gap + w <= work.right) x = o.right + gap;
    else if (o.left - gap - w >= work.left) x = o.left - gap - w;
    else return;
    int y = std::max((int)work.top, std::min((int)o.top, (int)work.bottom - h));
    SetWindowPos(dlg, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void Preview(HWND dlg, SettingsArgs& a) {
    if (!a.preview) return;
    Settings s = *a.s;
    int sel = ComboSel(dlg, IDC_SKIN);
    if (sel >= 0) s.skin = a.skins[sel];
    a.preview(a.ctx, s);
}

void InitSettings(HWND dlg, SettingsArgs& a) {
    const Settings& s = *a.s;
    i18n::ApplyDialog(dlg, kSettingsTexts, _countof(kSettingsTexts));

    const wchar_t* corners[] = {T(L"corner.none", L"None"), T(L"corner.lt", L"Top left"),
                                T(L"corner.rt", L"Top right"), T(L"corner.lb", L"Bottom left"),
                                T(L"corner.rb", L"Bottom right")};
    for (auto c : corners) AddCombo(dlg, IDC_CORNER, c);
    SetComboSel(dlg, IDC_CORNER, (int)s.hotCorner);
    SetDlgItemInt(dlg, IDC_DELAY, s.cornerDelay, FALSE);
    SendDlgItemMessageW(dlg, IDC_HOTKEY, HKM_SETHOTKEY, s.hotkey, 0);
    AddCombo(dlg, IDC_SHOWAT, T(L"showAt.mouse", L"Mouse position"));
    AddCombo(dlg, IDC_SHOWAT, T(L"showAt.last", L"Last position"));
    SetComboSel(dlg, IDC_SHOWAT, (int)s.showAt);

    AddCombo(dlg, IDC_LANG, T(L"lang.auto", L"(System)"));
    SetComboSel(dlg, IDC_LANG, 0);
    for (size_t i = 0; i < a.langs.size(); ++i) {
        AddCombo(dlg, IDC_LANG, a.langs[i].name.c_str());
        if (EqualsNoCase(a.langs[i].code, s.language.c_str())) SetComboSel(dlg, IDC_LANG, (int)i + 1);
    }
    SetComboSel(dlg, IDC_SKIN, 0);
    for (size_t i = 0; i < a.skins.size(); ++i) {
        AddCombo(dlg, IDC_SKIN, a.skins[i].c_str());
        if (EqualsNoCase(a.skins[i], s.skin.c_str())) SetComboSel(dlg, IDC_SKIN, (int)i);
    }

    const wchar_t* sides[] = {T(L"side.left", L"Left"), T(L"side.top", L"Top"),
                              T(L"side.right", L"Right"), T(L"side.bottom", L"Bottom")};
    for (auto c : sides) AddCombo(dlg, IDC_TABSIDE, c);
    SetComboSel(dlg, IDC_TABSIDE, (int)s.tabSide);
    SetDlgItemInt(dlg, IDC_CELLW, s.cellW, FALSE);
    SetDlgItemInt(dlg, IDC_CELLH, s.cellH, FALSE);
    SetDlgItemInt(dlg, IDC_TABW, s.tabW, FALSE);
    SetDlgItemInt(dlg, IDC_TABH, s.tabH, FALSE);
    for (int i = 0; i < (int)_countof(kIconSizes); ++i) {
        AddCombo(dlg, IDC_ICONSIZE, FromInt(kIconSizes[i]).c_str());
        if (kIconSizes[i] == s.iconSize) SetComboSel(dlg, IDC_ICONSIZE, i);
    }
    SetDlgItemInt(dlg, IDC_HOVER, s.tabHoverDelay, FALSE);
    CheckDlgButton(dlg, IDC_TABCLICK, s.tabClick ? BST_CHECKED : BST_UNCHECKED);
    EnableHover(dlg);
    ShowFont(dlg, IDC_CELLFONT, s.buttonFont);
    ShowFont(dlg, IDC_TABFONT, s.tabFont);
    CheckDlgButton(dlg, IDC_AUTOSTART, *a.autoStart ? BST_CHECKED : BST_UNCHECKED);
}

void ReadSettings(HWND dlg, SettingsArgs& a) {
    Settings& s = *a.s;
    int sel = ComboSel(dlg, IDC_CORNER);
    if (sel >= 0) s.hotCorner = (HotCorner)sel;
    s.cornerDelay = (int)GetDlgItemInt(dlg, IDC_DELAY, nullptr, FALSE);
    s.hotkey = (WORD)SendDlgItemMessageW(dlg, IDC_HOTKEY, HKM_GETHOTKEY, 0, 0);
    if ((sel = ComboSel(dlg, IDC_SHOWAT)) >= 0) s.showAt = (ShowAt)sel;

    sel = ComboSel(dlg, IDC_LANG);
    s.language = sel > 0 ? a.langs[sel - 1].code : L"";
    if ((sel = ComboSel(dlg, IDC_SKIN)) >= 0) s.skin = a.skins[sel];
    if ((sel = ComboSel(dlg, IDC_TABSIDE)) >= 0) s.tabSide = (TabSide)sel;

    int w = (int)GetDlgItemInt(dlg, IDC_CELLW, nullptr, FALSE);
    int h = (int)GetDlgItemInt(dlg, IDC_CELLH, nullptr, FALSE);
    if (w >= 24 && w <= 400) s.cellW = w;
    if (h >= 24 && h <= 400) s.cellH = h;
    w = (int)GetDlgItemInt(dlg, IDC_TABW, nullptr, FALSE);
    h = (int)GetDlgItemInt(dlg, IDC_TABH, nullptr, FALSE);
    if (w >= 16 && w <= 400) s.tabW = w;
    if (h >= 12 && h <= 200) s.tabH = h;
    if ((sel = ComboSel(dlg, IDC_ICONSIZE)) >= 0) s.iconSize = kIconSizes[sel];
    int hover = (int)GetDlgItemInt(dlg, IDC_HOVER, nullptr, FALSE);
    if (hover <= 5000) s.tabHoverDelay = hover;
    s.tabClick = IsDlgButtonChecked(dlg, IDC_TABCLICK) == BST_CHECKED;
    *a.autoStart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
}

INT_PTR CALLBACK SettingsProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* a = reinterpret_cast<SettingsArgs*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
        case WM_INITDIALOG:
            SetWindowLongPtrW(dlg, DWLP_USER, lp);
            InitSettings(dlg, *reinterpret_cast<SettingsArgs*>(lp));
            PlaceBeside(dlg, GetParent(dlg));
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDC_SKIN && HIWORD(wp) == CBN_SELCHANGE) {
                Preview(dlg, *a);
                return TRUE;
            }
            if (LOWORD(wp) == IDC_CELLFONT || LOWORD(wp) == IDC_TABFONT) {
                FontSpec& f = LOWORD(wp) == IDC_CELLFONT ? a->s->buttonFont : a->s->tabFont;
                if (PickFont(dlg, LOWORD(wp), f, *a->s)) {
                    ShowFont(dlg, LOWORD(wp), f);
                    Preview(dlg, *a);
                }
                return TRUE;
            }
            if (LOWORD(wp) == IDC_TABCLICK) {
                EnableHover(dlg);
                return TRUE;
            }
            if (LOWORD(wp) == IDOK) {
                ReadSettings(dlg, *a);
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

}  // namespace

bool EditItemDialog(HWND owner, Item& item) {
    Item copy = item;
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ITEM), owner, ItemProc,
                        (LPARAM)&copy) != IDOK)
        return false;
    item = std::move(copy);
    return true;
}

bool InputDialog(HWND owner, const wchar_t* title, const wchar_t* label, std::wstring& text) {
    InputArgs args{title, label, &text};
    return DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_INPUT), owner,
                           InputProc, (LPARAM)&args) == IDOK;
}

bool SettingsDialog(HWND owner, Settings& settings, bool& autoStart, SettingsPreview preview, void* ctx) {
    Settings copy = settings;
    bool autoCopy = autoStart;
    SettingsArgs args{&copy, &autoCopy, i18n::List(), ListSkins(), preview, ctx};
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), owner,
                        SettingsProc, (LPARAM)&args) != IDOK)
        return false;
    settings = copy;
    autoStart = autoCopy;
    return true;
}
