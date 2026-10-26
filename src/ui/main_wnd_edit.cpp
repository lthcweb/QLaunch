// Main window: mouse input, drag and drop, context menus, editing pages and buttons
#include "ui/main_wnd.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdlib>

#include "core/path.h"
#include "core/str.h"
#include "i18n/i18n.h"
#include "launch/builtin.h"
#include "launch/launch.h"
#include "trigger/trigger.h"
#include "ui/dialogs.h"
#include "ui/msgbox.h"

namespace {

enum Cmd : UINT {
    kCmdOpen = 1,
    kCmdRunAdmin,
    kCmdOpenFolder,
    kCmdEdit,
    kCmdDelete,
    kCmdAdd,
    kCmdRenamePage,
    kCmdAddPage,
    kCmdAddSub,
    kCmdDeletePage,
    kCmdMoveLeft,
    kCmdMoveRight,
    kCmdPin,
    kCmdRefreshIcons,
    kCmdSettings,
    kCmdExit,
};

void AddMenu(HMENU m, UINT id, const wchar_t* text, bool checked = false) {
    AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0), id, text);
}

// Where a dragged thing can be dropped: cells and all kinds of tabs
bool Droppable(const Hit& h) {
    return h.kind == HitKind::Cell || h.kind == HitKind::Tab || h.kind == HitKind::Parent ||
           h.kind == HitKind::SubTab;
}

// Shows the menu, destroys it and returns the chosen command (0 for none)
UINT RunMenu(HMENU menu, HWND owner, POINT screenPt) {
    UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, owner, nullptr);
    DestroyMenu(menu);
    return cmd;
}

}  // namespace

// ---------------------------------------------------------------------------
// Current view

Page& MainWnd::CurrentView() {
    return const_cast<Page&>(static_cast<const MainWnd*>(this)->CurrentView());
}

const Page& MainWnd::CurrentView() const {
    if (Searching()) return results_;
    const Page& p = app_.store.pages[page_];
    return (sub_ >= 0 && sub_ < (int)p.subs.size()) ? p.subs[sub_] : p;
}

Pos MainWnd::ViewPos(int slot) const {
    return Pos{slot / layout_.cols + scrollRow_, slot % layout_.cols};
}

MainWnd::CellRef MainWnd::RefForSlot(int slot) const {
    if (!Searching()) return CellRef{page_, sub_, ViewPos(slot)};
    int idx = results_.Find(ViewPos(slot));
    return idx < 0 ? CellRef{} : found_[idx];
}

Item* MainWnd::ItemAt(const CellRef& ref) {
    if (ref.page < 0) return nullptr;
    Page* p = &app_.store.pages[ref.page];
    if (ref.sub >= 0) p = &p->subs[ref.sub];
    return p->At(ref.pos);
}

void MainWnd::RefreshView() {
    scrollRow_ = 0;
    ReleaseIcons();
    Relayout();
    LoadIcons();
    Redraw();
}

void MainWnd::SwitchPage(int page) {
    if (page < 0 || page >= (int)app_.store.pages.size()) return;
    bool searched = StopSearch();  // choosing a tab ends the search
    if (page == page_ && sub_ < 0 && !drilled_) {
        if (searched) RefreshView();
        return;
    }
    page_ = page;
    sub_ = -1;
    drilled_ = false;
    scrollRow_ = 0;
    ReleaseIcons();
    Relayout();
    LoadIcons();
    Redraw();
}

void MainWnd::OpenPage(int page) {
    SwitchPage(page);
    if (app_.store.pages[page_].subs.empty()) return;
    drilled_ = true;
    Relayout();
    ResetHover();
    Redraw();
}

void MainWnd::CloseSubs() {
    if (!drilled_) {
        if (StopSearch()) RefreshView();
        return;
    }
    drilled_ = false;
    if (sub_ >= 0) {
        SwitchSub(-1);
    } else {
        Relayout();
        Redraw();
    }
    ResetHover();
}

// E.g. after clicking "‹ parent page" a different top-level tab is now under the mouse. Left alone, hover
// switching
// would jump to it at once, so record it as the hovered tab and use the normal rules once the mouse moves to
// another tab.
void MainWnd::ResetHover() {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd_, &pt);
    hover_ = HitTest(layout_, pt);
    KillTimer(hwnd_, kTimerHover);
}

void MainWnd::SwitchSub(int sub) {
    if (StopSearch() && sub == sub_) {
        RefreshView();
        return;
    }
    if (sub == sub_) return;
    sub_ = sub;
    scrollRow_ = 0;
    ReleaseIcons();
    Relayout();
    LoadIcons();
    Redraw();
}

void MainWnd::DataChanged() {
    app_.SaveData();
    auto& pages = app_.store.pages;
    page_ = std::max(0, std::min(page_, (int)pages.size() - 1));
    if (sub_ >= (int)pages[page_].subs.size()) sub_ = -1;
    if (pages[page_].subs.empty()) drilled_ = false;
    if (Searching()) RunSearch();  // a result was edited or deleted: search again
    ReleaseIcons();
    UpdateWindowSize();
    Relayout();
    LoadIcons();
    Redraw();
}

// ---------------------------------------------------------------------------
// Mouse

void MainWnd::OnMouseMove(POINT pt, bool leftDown) {
    if (!tracking_) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
        tracking_ = TrackMouseEvent(&tme) != FALSE;
    }

    // holding a cell and moving past the system threshold starts a drag
    if (leftDown && !dragging_ && !Searching() && pressed_.kind == HitKind::Cell &&
        (abs(pt.x - downPt_.x) > GetSystemMetrics(SM_CXDRAG) ||
         abs(pt.y - downPt_.y) > GetSystemMetrics(SM_CYDRAG))) {
        CellRef ref = RefForSlot(pressed_.index);
        if (ItemAt(ref)) {
            dragging_ = true;
            dragFrom_ = ref;
        }
    }

    Hit h = HitTest(layout_, pt);
    if (dragging_) {
        dragPt_ = pt;
        dropTarget_ = Droppable(h) ? h : Hit{};
        Redraw();
    }
    SetHover(h);
}

// Hovering a top-level tab or a sub-tab switches to it; while dragging, hovering "‹ parent page" goes back to
// the top-level tabs
void MainWnd::SetHover(const Hit& h) {
    if (h == hover_) return;
    hover_ = h;
    KillTimer(hwnd_, kTimerHover);
    bool carrying = dragging_ || fileDrag_;
    int delay = app_.settings.tabHoverDelay;
    bool newTab = (h.kind == HitKind::Tab && h.index != page_) || (h.kind == HitKind::SubTab && h.index != sub_);
    if (newTab && (delay >= 0 || carrying))
        SetTimer(hwnd_, kTimerHover, std::max(delay, 1), nullptr);
    else if (carrying && h.kind == HitKind::Parent)
        SetTimer(hwnd_, kTimerHover, 500, nullptr);
    UpdateTooltip();
    Redraw();
}

void MainWnd::OnLButtonDown(POINT pt) {
    pressed_ = HitTest(layout_, pt);
    downPt_ = pt;
    SetCapture(hwnd_);
    Redraw();
}

void MainWnd::OnLButtonUp(POINT pt) {
    if (GetCapture() == hwnd_) ReleaseCapture();
    Hit h = HitTest(layout_, pt);
    Hit pressed = pressed_;
    pressed_ = Hit{};
    if (dragging_) {
        dragging_ = false;
        Hit target = dropTarget_;
        dropTarget_ = Hit{};
        Drop(target);
    } else if (h == pressed) {
        Click(h);
    }
    Redraw();
}

void MainWnd::Click(const Hit& hit) {
    switch (hit.kind) {
        case HitKind::Close: Hide(); break;
        case HitKind::Pin:
            app_.settings.pinned = !app_.settings.pinned;
            app_.SaveSettings();
            break;
        case HitKind::Search:
            if (searchOpen_) EndSearch();
            else OpenSearch();
            break;
        case HitKind::Tab: OpenPage(hit.index); break;
        case HitKind::Parent: CloseSubs(); break;
        case HitKind::SubTab: SwitchSub(hit.index); break;
        case HitKind::Cell:
            if (Item* it = ItemAt(RefForSlot(hit.index))) Launch(*it, false);
            break;
        default: break;
    }
}

void MainWnd::OnWheel(POINT pt, int delta) {
    int step = delta > 0 ? -1 : 1;
    if (PtInRect(&layout_.tabPanel, pt)) {
        if (drilled_) {
            int n = (int)app_.store.pages[page_].subs.size();
            SwitchSub(std::max(-1, std::min(sub_ + step, n - 1)));
        } else {
            SwitchPage(std::max(0, std::min(page_ + step, (int)app_.store.pages.size() - 1)));
        }
    } else if (PtInRect(&layout_.grid, pt)) {
        scrollRow_ = std::max(0, scrollRow_ + step);
        Relayout();  // keeps the scroll position within the content
        Redraw();
    }
}

// ---------------------------------------------------------------------------
// Drag and drop

void MainWnd::MoveItem(const CellRef& from, const CellRef& to) {
    if (from.page == to.page && from.sub == to.sub && from.pos == to.pos) return;
    auto pageOf = [this](const CellRef& r) -> Page& {
        Page& p = app_.store.pages[r.page];
        return r.sub >= 0 ? p.subs[r.sub] : p;
    };
    Item* src = ItemAt(from);
    if (!src) return;
    Item moving = std::move(*src);

    // if the target cell holds a button, swap them
    Page& dst = pageOf(to);
    Item displaced;
    if (Item* d = dst.At(to.pos)) displaced = std::move(*d);
    dst.Put(to.pos, std::move(moving));
    pageOf(from).Put(from.pos, std::move(displaced));
    DataChanged();
}

void MainWnd::Drop(const Hit& target) {
    auto& pages = app_.store.pages;
    const int cols = layout_.cols;
    switch (target.kind) {
        case HitKind::Cell: MoveItem(dragFrom_, RefForSlot(target.index)); break;
        case HitKind::Tab:
        case HitKind::Parent: {
            int page = target.kind == HitKind::Tab ? target.index : page_;
            if (page != dragFrom_.page || dragFrom_.sub != -1)
                MoveItem(dragFrom_, CellRef{page, -1, pages[page].FirstFree(cols)});
            break;
        }
        case HitKind::SubTab:
            if (target.index != dragFrom_.sub || dragFrom_.page != page_)
                MoveItem(dragFrom_, CellRef{page_, target.index,
                                            pages[page_].subs[target.index].FirstFree(cols)});
            break;
        default: break;
    }
}

// Files dragged in from Explorer: placed in the target cell, or in a free cell of the page whose tab they were
// dropped on
// ---------------------------------------------------------------------------
// Dragged in from Explorer: files, folders, shortcuts, and virtual objects such as Control Panel and Recycle
// Bin

void MainWnd::DragOver(POINT pt) {
    fileDrag_ = true;
    Hit h = HitTest(layout_, pt);
    Hit target = Droppable(h) ? h : Hit{};
    if (!(target == dropTarget_)) {
        dropTarget_ = target;
        Redraw();
    }
    SetHover(h);
}

void MainWnd::DragEnd() {
    fileDrag_ = false;
    dropTarget_ = Hit{};
    SetHover(Hit{});
    Redraw();
}

void MainWnd::DropItems(POINT pt, std::vector<DroppedItem>& items) {
    if (StopSearch()) RefreshView();  // dropped files go to the page, not into the results
    Hit h = HitTest(layout_, pt);
    Page* page = &CurrentView();
    Pos cell;  // target cell; row = -1 means not dropped on a cell
    if (h.kind == HitKind::Tab) page = &app_.store.pages[h.index];
    else if (h.kind == HitKind::Parent) page = &app_.store.pages[page_];
    else if (h.kind == HitKind::SubTab) page = &app_.store.pages[page_].subs[h.index];
    else if (h.kind == HitKind::Cell) cell = RefForSlot(h.index).pos;

    for (DroppedItem& d : items) {
        Item it;
        it.file = std::move(d.file);
        it.name = std::move(d.name);
        Pos target = (cell.row >= 0 && !page->At(cell)) ? cell : page->FirstFree(layout_.cols);
        page->Put(target, std::move(it));
        cell = Pos{};  // only the first one goes to the target cell, the rest find free cells
    }
    DataChanged();
    SetForegroundWindow(hwnd_);  // once activated, clicking elsewhere triggers auto-hide
}

void MainWnd::Launch(const Item& item, bool admin) {
    Item copy = item;  // don't touch the data after Hide
    bool hide = !app_.settings.pinned;
    // Start programs while the window is still in front: Windows only lets a new program, and the
    // administrator prompt, come to the front when whoever starts it is in front. Built-in commands press
    // keys (Win+R, Win+D) or ask first, so those hide before.
    bool builtin = IsBuiltinCommand(copy.file);
    if (hide && builtin) Hide();
    LaunchItem(hwnd_, copy, admin);
    if (hide && !builtin) Hide();
}

// ---------------------------------------------------------------------------
// Context menus

// Three context menus: buttons and tabs each handle their own things; global settings are in the main menu
void MainWnd::OnContextMenu(POINT pt) {
    Hit h = HitTest(layout_, pt);
    // while sub-tabs are shown, right-click on "‹ parent page" or on the blank bar goes back to the top-level
    // tabs
    if (drilled_ && (h.kind == HitKind::Parent ||
                     (h.kind == HitKind::None && PtInRect(&layout_.tabPanel, pt)))) {
        CloseSubs();
        return;
    }
    POINT sp = pt;
    ClientToScreen(hwnd_, &sp);
    if (h.kind == HitKind::Cell) CellMenu(h, sp);
    else if (h.kind == HitKind::Tab || h.kind == HitKind::SubTab) TabMenu(h, sp);
    else MainMenu(sp);
}

void MainWnd::CellMenu(const Hit& h, POINT sp) {
    ModalGuard guard(*this);
    CellRef ref = RefForSlot(h.index);
    if (Searching() && !ItemAt(ref)) {  // an empty cell among the results belongs to no page
        MainMenu(sp);
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (const Item* it = ItemAt(ref)) {
        AddMenu(menu, kCmdOpen, T(L"menu.open", L"Open"));
        if (!IsUrl(it->file)) {  // a web address has no administrator mode and no file location
            AddMenu(menu, kCmdRunAdmin, T(L"menu.runAdmin", L"Run as administrator"));
            AddMenu(menu, kCmdOpenFolder, T(L"menu.openFolder", L"Open file location"));
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AddMenu(menu, kCmdEdit, T(L"menu.edit", L"Properties..."));
        AddMenu(menu, kCmdDelete, T(L"menu.delete", L"Delete"));
    } else {
        AddMenu(menu, kCmdAdd, T(L"menu.add", L"Add button..."));
    }

    switch (RunMenu(menu, hwnd_, sp)) {
        case kCmdOpen: Click(h); break;
        case kCmdRunAdmin:
            if (Item* it = ItemAt(ref)) Launch(*it, true);
            break;
        case kCmdOpenFolder:
            if (Item* it = ItemAt(ref)) OpenItemFolder(*it);
            break;
        case kCmdEdit:
        case kCmdAdd: EditItem(ref); break;
        case kCmdDelete:
            if (MessageBoxAtCursor(hwnd_, T(L"msg.deleteItem", L"Delete this button?"),
                                   MB_YESNO | MB_ICONQUESTION) == IDYES) {
                Page& p = app_.store.pages[ref.page];
                (ref.sub >= 0 ? p.subs[ref.sub] : p).Put(ref.pos, Item{});
                DataChanged();
            }
            break;
    }
    Redraw();
}

void MainWnd::TabMenu(const Hit& h, POINT sp) {
    ModalGuard guard(*this);
    int page = h.kind == HitKind::Tab ? h.index : page_;
    int sub = h.kind == HitKind::SubTab ? h.index : -1;
    HMENU menu = CreatePopupMenu();
    AddMenu(menu, kCmdRenamePage, T(L"menu.rename", L"Rename..."));
    if (sub < 0) AddMenu(menu, kCmdAddPage, T(L"menu.addPage", L"New page..."));
    AddMenu(menu, kCmdAddSub, T(L"menu.addSub", L"New sub-page..."));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddMenu(menu, kCmdMoveLeft, T(L"menu.moveBack", L"Move back"));
    AddMenu(menu, kCmdMoveRight, T(L"menu.moveForward", L"Move forward"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddMenu(menu, kCmdDeletePage, T(L"menu.deletePage", L"Delete"));

    switch (RunMenu(menu, hwnd_, sp)) {
        case kCmdRenamePage: RenamePage(page, sub); break;
        case kCmdAddPage: AddPage(page + 1); break;
        case kCmdAddSub: AddSub(page); break;
        case kCmdDeletePage: DeletePage(page, sub); break;
        case kCmdMoveLeft: MovePage(page, sub, -1); break;
        case kCmdMoveRight: MovePage(page, sub, +1); break;
    }
    Redraw();
}

void MainWnd::MainMenu(POINT sp) {
    ModalGuard guard(*this);
    HMENU menu = CreatePopupMenu();
    AddMenu(menu, kCmdAddPage, T(L"menu.addPage", L"New page..."));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddMenu(menu, kCmdPin, T(L"menu.pin", L"Keep open"), app_.settings.pinned);
    AddMenu(menu, kCmdRefreshIcons, T(L"menu.refreshIcons", L"Refresh icons"));
    AddMenu(menu, kCmdSettings, T(L"menu.settings", L"Settings..."));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AddMenu(menu, kCmdExit, T(L"menu.exit", L"Exit"));

    switch (RunMenu(menu, hwnd_, sp)) {
        case kCmdAddPage: AddPage((int)app_.store.pages.size()); break;
        case kCmdPin: Click(Hit{HitKind::Pin, 0}); break;
        case kCmdRefreshIcons:
            app_.iconCache.Clear();
            ReleaseIcons();
            LoadIcons();
            break;
        case kCmdSettings: ShowSettings(); break;
        case kCmdExit: DestroyWindow(hwnd_); return;
    }
    Redraw();
}

// ---------------------------------------------------------------------------
// Editing

void MainWnd::EditItem(const CellRef& ref) {
    ModalGuard guard(*this);
    Page& p = app_.store.pages[ref.page];
    Page& view = ref.sub >= 0 ? p.subs[ref.sub] : p;
    Item it;
    if (Item* old = view.At(ref.pos)) it = *old;
    if (!EditItemDialog(hwnd_, it)) return;
    view.Put(ref.pos, std::move(it));
    DataChanged();
}

void MainWnd::AddPage(int at) {
    ModalGuard guard(*this);
    std::wstring name = T(L"page.new", L"New page");
    if (!InputDialog(hwnd_, T(L"menu.addPage", L"New page..."), T(L"page.name", L"Name:"), name))
        return;
    auto& pages = app_.store.pages;
    at = std::max(0, std::min(at, (int)pages.size()));
    Page page;
    page.name = name;
    pages.insert(pages.begin() + at, std::move(page));
    page_ = at;
    sub_ = -1;
    DataChanged();
}

void MainWnd::AddSub(int page) {
    ModalGuard guard(*this);
    std::wstring name = T(L"page.newSub", L"New sub-page");
    if (!InputDialog(hwnd_, T(L"menu.addSub", L"New sub-page..."), T(L"page.name", L"Name:"), name))
        return;
    Page sub;
    sub.name = name;
    auto& subs = app_.store.pages[page].subs;
    subs.push_back(std::move(sub));
    page_ = page;
    sub_ = (int)subs.size() - 1;
    drilled_ = true;
    DataChanged();
}

void MainWnd::RenamePage(int page, int sub) {
    ModalGuard guard(*this);
    Page& p = app_.store.pages[page];
    Page& target = sub >= 0 ? p.subs[sub] : p;
    std::wstring name = target.name;
    if (!InputDialog(hwnd_, T(L"menu.rename", L"Rename..."), T(L"page.name", L"Name:"), name)) return;
    target.name = name;
    DataChanged();
}

void MainWnd::DeletePage(int page, int sub) {
    ModalGuard guard(*this);
    auto& pages = app_.store.pages;
    if (sub < 0 && pages.size() <= 1) {
        MessageBoxAtCursor(hwnd_, T(L"msg.lastPage", L"The last page can't be deleted."),
                           MB_OK | MB_ICONINFORMATION);
        return;
    }
    const wchar_t* question = sub < 0
        ? T(L"msg.deletePage", L"Delete this page, its sub-pages and all their buttons?")
        : T(L"msg.deleteSub", L"Delete this sub-page and all its buttons?");
    if (MessageBoxAtCursor(hwnd_, question, MB_YESNO | MB_ICONWARNING) != IDYES) return;

    if (sub < 0) {
        pages.erase(pages.begin() + page);
        if (page_ >= page && page_ > 0) --page_;
        sub_ = -1;
    } else {
        auto& subs = pages[page].subs;
        subs.erase(subs.begin() + sub);
        sub_ = -1;
    }
    DataChanged();
}

void MainWnd::MovePage(int page, int sub, int delta) {
    auto& pages = app_.store.pages;
    if (sub < 0) {
        int to = page + delta;
        if (to < 0 || to >= (int)pages.size()) return;
        std::swap(pages[page], pages[to]);
        if (page_ == page) page_ = to;
        else if (page_ == to) page_ = page;
    } else {
        auto& subs = pages[page].subs;
        int to = sub + delta;
        if (to < 0 || to >= (int)subs.size()) return;
        std::swap(subs[sub], subs[to]);
        if (sub_ == sub) sub_ = to;
        else if (sub_ == to) sub_ = sub;
    }
    DataChanged();
}

namespace {

// After switching skins, apply the tab position and sizes the skin recommends
void ApplySkinHints(Settings& s) {
    SkinHints h = CreateSkin(s.skin)->Hints();
    if (h.hasTabSide) s.tabSide = h.tabSide;
    if (h.tabW > 0 && h.tabH > 0) {
        s.tabW = h.tabW;
        s.tabH = h.tabH;
    }
    if (h.cellW > 0 && h.cellH > 0) {
        s.cellW = h.cellW;
        s.cellH = h.cellH;
    }
    if (h.iconSize > 0) s.iconSize = h.iconSize;
}

// Settings as they were when the dialog opened; previews build on them and cancel restores them
const Settings* g_beforePreview = nullptr;
bool g_previewed = false;

}  // namespace

void MainWnd::PreviewSettings(void* self, const Settings& s) {
    auto* w = static_cast<MainWnd*>(self);
    Settings p = *g_beforePreview;
    p.skin = s.skin;
    p.tabFont = s.tabFont;
    p.buttonFont = s.buttonFont;
    if (!EqualsNoCase(p.skin, g_beforePreview->skin.c_str())) ApplySkinHints(p);
    w->app_.settings = p;
    w->ApplyLook();
    g_previewed = true;
}

void MainWnd::ShowSettings() {
    ModalGuard guard(*this);
    const Settings before = app_.settings;
    Settings s = before;
    bool autoStart = IsAutoStart();
    g_beforePreview = &before;
    g_previewed = false;
    bool ok = SettingsDialog(hwnd_, s, autoStart, &MainWnd::PreviewSettings, this);
    g_beforePreview = nullptr;
    // a preview may have changed the current settings: restore first, then apply the dialog's result
    app_.settings = before;
    if (!ok) {
        if (g_previewed) ApplyLook();
        return;
    }

    if (!EqualsNoCase(s.skin, before.skin.c_str())) ApplySkinHints(s);
    bool langChanged = s.language != before.language;
    app_.settings = s;
    if (autoStart != IsAutoStart()) SetAutoStart(autoStart);
    if (langChanged) {
        BuiltinNames old = CurrentBuiltinNames();
        i18n::Load(s.language);
        if (RenameBuiltins(app_.store.pages, old)) DataChanged();
    }
    app_.SaveSettings();
    ApplySettings();
    if (s.hotkey && !trigger::RegisterHotkey(hwnd_, s.hotkey))
        MessageBoxAtCursor(hwnd_, T(L"msg.hotkeyFailed", L"The hotkey is already used by another program."),
                           MB_OK | MB_ICONWARNING);
}

// ---------------------------------------------------------------------------
// Search

namespace {

std::wstring Lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size());
    return s;
}

}  // namespace

void MainWnd::OpenSearch() {
    searchOpen_ = true;
    Redraw();
}

void MainWnd::OnChar(wchar_t ch) {
    // control characters (Esc, Backspace, Enter, Ctrl+letter) are handled as keys; a search doesn't start with
    // a space
    if (ch < L' ' || ch == 0x7F || (ch == L' ' && query_.empty())) return;
    searchOpen_ = true;
    query_ += ch;
    RunSearch();
}

bool MainWnd::OnSearchKey(UINT vk) {
    if (!searchOpen_) return false;
    int n = (int)results_.cells.size(), cols = std::max(1, resultCols_);
    switch (vk) {
        case VK_ESCAPE: EndSearch(); return true;
        case VK_BACK:  // deletes the last character; on an empty box, closes it
            if (query_.empty()) {
                EndSearch();
            } else {
                query_.pop_back();
                RunSearch();
            }
            return true;
        case VK_RETURN:
            if (Searching() && selected_ < n) {
                if (Item* it = ItemAt(found_[selected_])) Launch(*it, GetKeyState(VK_CONTROL) < 0);
            }
            return true;
        case VK_LEFT: selected_ = std::max(0, selected_ - 1); break;
        case VK_RIGHT: selected_ = std::min(n - 1, selected_ + 1); break;
        case VK_UP: if (selected_ >= cols) selected_ -= cols; break;
        case VK_DOWN: if (selected_ + cols < n) selected_ += cols; break;
        default: return false;
    }
    // keep the selected result in view
    int row = n ? selected_ / cols : 0;
    if (row < scrollRow_) scrollRow_ = row;
    else if (row >= scrollRow_ + layout_.rows) scrollRow_ = row - layout_.rows + 1;
    Relayout();
    Redraw();
    return true;
}

// A button matches when its name or its file name contains the text, ignoring case. Names that start with the
// text come first, then names that contain it, then file names; otherwise in page order.
void MainWnd::RunSearch() {
    std::wstring q = Lower(query_);
    std::vector<std::pair<int, CellRef>> hits;
    if (q.empty()) {  // everything deleted: the box stays open over the page
        results_.cells.clear();
        found_.clear();
        RefreshView();
        return;
    }
    auto scan = [&](const Page& p, int page, int sub) {
        for (const Cell& c : p.cells) {
            std::wstring name = Lower(c.item.name);
            size_t at = name.find(q);
            int rank = at == 0 ? 0 : at != std::wstring::npos ? 1 : -1;
            if (rank < 0 && !IsUrl(c.item.file) && Lower(FileTitle(c.item.file)).find(q) != std::wstring::npos)
                rank = 2;
            if (rank >= 0) hits.push_back({rank, CellRef{page, sub, c.pos}});
        }
    };
    const auto& pages = app_.store.pages;
    for (int i = 0; i < (int)pages.size(); ++i) {
        scan(pages[i], i, -1);
        for (int j = 0; j < (int)pages[i].subs.size(); ++j) scan(pages[i].subs[j], i, j);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    found_.clear();
    results_.cells.clear();
    for (auto& h : hits) {
        found_.push_back(h.second);
        results_.cells.push_back(Cell{Pos{}, *ItemAt(h.second)});
    }
    resultCols_ = 0;
    selected_ = 0;
    RefreshView();  // Relayout places the results for the current column count
}

void MainWnd::PlaceResults() {
    if (resultCols_ == layout_.cols) return;
    resultCols_ = layout_.cols;
    for (int i = 0; i < (int)results_.cells.size(); ++i)
        results_.cells[i].pos = Pos{i / resultCols_, i % resultCols_};
}

bool MainWnd::StopSearch() {
    if (!searchOpen_) return false;
    searchOpen_ = false;
    query_.clear();
    results_.cells.clear();
    found_.clear();
    return true;
}

void MainWnd::EndSearch() {
    if (StopSearch()) RefreshView();
}

// ---------------------------------------------------------------------------
// Tooltips

void MainWnd::UpdateTooltip() {
    if (tooltip_) SendMessageW(tooltip_, TTM_POP, 0, 0);
}

std::wstring MainWnd::TooltipText() const {
    if (hover_.kind == HitKind::Search && !searchOpen_)
        return T(L"search.tip", L"Type on the window to search all buttons");
    if (hover_.kind != HitKind::Cell || dragging_) return {};
    const Page& view = CurrentView();
    int idx = view.Find(ViewPos(hover_.index));
    if (idx < 0) return {};
    const Item& it = view.cells[idx].item;
    return it.tip.empty() ? it.name + L"\n" + it.file : it.tip;
}
