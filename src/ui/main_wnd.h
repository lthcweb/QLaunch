// Main window. Normally hidden; opened by the hot corner or hotkey; hides again on losing focus or when the
// mouse leaves (unless pinned).
// The implementation is split over three files:
//   main_wnd.cpp        window messages, show/hide, sizes
//   main_wnd_paint.cpp  drawing
//   main_wnd_edit.cpp   clicks, drag and drop, context menus, editing, search
#pragma once

#include "app.h"

#include <shellapi.h>

#include <memory>

#include "core/handle.h"
#include "skin/canvas.h"
#include "skin/skin.h"
#include "ui/drop_target.h"
#include "ui/layout.h"

class MainWnd : private DropSink {
public:
    explicit MainWnd(App& app) : app_(app) {}
    QL_NO_COPY(MainWnd);

    bool Create();
    HWND hwnd() const { return hwnd_; }
    // First run: show the window at the default position and tell the user in a message box how to open it
    void ShowWelcome();

private:
    // While a dialog or menu is open the window doesn't hide on losing focus or when the mouse leaves.
    // Afterwards the mouse is usually outside (where the dialog or menu was), so wait until it has been inside
    // again
    class ModalGuard {
    public:
        explicit ModalGuard(MainWnd& w) : w_(w) { ++w_.modal_; }
        ~ModalGuard() {
            if (--w_.modal_ == 0) w_.entered_ = w_.CursorInWindow();
        }
        QL_NO_COPY(ModalGuard);

    private:
        MainWnd& w_;
    };

    // Full position of a cell: which page, which sub-page (-1 = the page itself), which row and column
    struct CellRef {
        int page = -1, sub = -1;
        Pos pos;
    };

    static constexpr UINT_PTR kTimerCorner = 1;  // hot corner delay
    static constexpr UINT_PTR kTimerHover = 2;   // switch tabs on hover
    static constexpr UINT_PTR kTimerLeave = 3;   // while shown, check whether the mouse has left the window

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    // main_wnd.cpp
    void Show(POINT anchor, bool atMouse);
    void Hide();
    void Toggle();
    bool CursorInWindow() const;
    void CheckMouseLeft();        // hide if the mouse was inside and has left
    void ApplySettings();         // rebuild the triggers after a settings change, then ApplyLook
    void ApplyLook();             // rebuild skin, fonts and window size after a look setting changes
    Skin& EnsureSkin();           // the skin loads on first show; no skin file is read at startup
    void UpdateSizing();          // recompute sizes and fonts after the DPI, settings or skin change
    void UpdateWindowSize();      // resize the window to the rows and columns in the settings
    void Relayout();
    LRESULT NcHitTest(POINT screenPt);
    void OnSizing(WPARAM edge, RECT* rc);

    // main_wnd_paint.cpp
    void Redraw();                // request a repaint: WM_PAINT for normal windows, UpdateLayeredWindow for layered ones
    bool Render();                // draw the whole window into canvas_
    void Paint(HDC target);
    void PaintLayered();
    void PaintCaption();
    void PaintSearch();
    void PaintTabs();
    void PaintCells();
    void LoadIcons();             // read the current page's icons from the cache; missing ones go to the background thread
    void ReleaseIcons();
    // part picks the font setting: tabs and buttons can each have their own font, the rest use the general font
    HFONT FontFor(const TextStyle& style, Part part = Part::Caption);
    int TextWidth(HFONT font, const std::wstring& s) const;

    // main_wnd_edit.cpp
    Page& CurrentView();          // the page or sub-page shown, or the search results
    const Page& CurrentView() const;
    Pos ViewPos(int slot) const;  // position in the current view of a visible cell
    CellRef RefForSlot(int slot) const;  // where the button in a visible cell lives (page = -1 for none)
    Item* ItemAt(const CellRef& ref);
    void RefreshView();           // the view's content changed: scroll to the top, lay out, reload icons
    void SwitchPage(int page);    // switch to a page; the bar shows the top-level tabs
    void OpenPage(int page);      // top-level tab clicked: switch to it and, if it has sub-pages, show its sub-tabs in the bar
    void CloseSubs();             // the bar goes back to the top-level tabs
    void ResetHover();            // after the bar changes, find the tab under the mouse again without starting a hover switch
    void SwitchSub(int sub);
    void OnMouseMove(POINT pt, bool leftDown);
    void SetHover(const Hit& h);  // what's under the mouse changed: restart the hover timer
    void OnLButtonDown(POINT pt);
    void OnLButtonUp(POINT pt);
    void OnContextMenu(POINT pt);
    void CellMenu(const Hit& h, POINT screenPt);
    void TabMenu(const Hit& h, POINT screenPt);
    void MainMenu(POINT screenPt);   // global settings; right-click on the caption or on blank parts of the tab bar and button area
    void OnWheel(POINT pt, int delta);
    void OnTimer(UINT_PTR id);
    void Click(const Hit& hit);
    void Launch(const Item& item, bool admin);  // starts a button's target and hides the window
    void Drop(const Hit& target);
    void MoveItem(const CellRef& from, const CellRef& to);
    void EditItem(const CellRef& ref);
    void AddPage(int at);
    void AddSub(int page);
    void RenamePage(int page, int sub);
    void DeletePage(int page, int sub);
    void MovePage(int page, int sub, int delta);
    // dragged in from Explorer (DropSink)
    void DragOver(POINT pt) override;
    void DragEnd() override;
    void DropItems(POINT pt, std::vector<DroppedItem>& items) override;
    // Search: clicking the magnifying glass or just typing opens the search box. Typing searches the buttons
    // of every page and sub-page by name and file name; the results replace the buttons shown until the search
    // is closed (Esc, the magnifying glass) or another tab is chosen
    bool Searching() const { return !query_.empty(); }  // results are shown
    void OpenSearch();
    void OnChar(wchar_t ch);
    bool OnSearchKey(UINT vk);    // Esc, Backspace, Enter and arrows while the box is open; true when handled
    void RunSearch();             // the query changed: show the matches, or the page again when it is empty
    void PlaceResults();          // lay the results out row by row for the current column count
    bool StopSearch();            // closes the box without redrawing; true if it was open
    void EndSearch();             // closes the box and shows the page again
    void ShowSettings();
    static void PreviewSettings(void* self, const Settings& s);  // the skin changed in the settings dialog
    void DataChanged();           // data changed: save, lay out again, reload icons
    void UpdateTooltip();
    std::wstring TooltipText() const;

    App& app_;
    HWND hwnd_ = nullptr;
    HWND tooltip_ = nullptr;
    int dpi_ = 96;
    Sizing sizing_;
    Layout layout_;
    Canvas canvas_;
    std::unique_ptr<Skin> skin_;
    bool layered_ = false;        // whether this is currently a layered window (image skin)
    bool redrawPending_ = false;

    struct Font {
        std::wstring face;
        int size;
        bool bold;
        GdiObj<HFONT> font;
    };
    std::vector<Font> fonts_;     // cached by face, size and weight
    int textH_ = 0;               // line height of button text

    int page_ = 0, sub_ = -1;     // current page and sub-page (-1 = the page's own buttons)
    bool drilled_ = false;        // the bar is showing the sub-tabs of page_
    int scrollRow_ = 0;           // scrolled rows when there are more cells than fit
    std::vector<IconPixels> icons_;  // icons of the buttons in the current view, same index as Page::cells

    bool searchOpen_ = false;     // the search box is shown, with a caret
    std::wstring query_;          // search text; empty when not searching
    Page results_;                // matching buttons, laid out row by row
    std::vector<CellRef> found_;  // where each result lives, same index as results_.cells
    int resultCols_ = 0;          // column count results_ was laid out for
    int selected_ = 0;            // result started by Enter, moved with the arrow keys

    bool visible_ = false;
    bool entered_ = false;        // the mouse has been inside since this show (only then does leaving hide the window)
    POINT cornerPt_{};            // mouse position when it entered the hot corner (used with a delay)
    std::wstring tipText_;
    int modal_ = 0;               // > 0 while a dialog or menu is open; losing focus then doesn't hide
    Hit hover_, pressed_;
    bool tracking_ = false;       // TrackMouseEvent has been called
    POINT downPt_{};
    bool dragging_ = false;
    bool fileDrag_ = false;       // something is being dragged in from outside
    DropTarget* oleDrop_ = nullptr;
    CellRef dragFrom_;
    Hit dropTarget_;
    POINT dragPt_{};
};
