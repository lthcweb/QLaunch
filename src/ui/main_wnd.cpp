// Main window: message dispatch, show/hide, DPI, window size
#include "ui/main_wnd.h"

#include <windowsx.h>
#include <commctrl.h>
#include <imm.h>
#include <shellapi.h>

#include <algorithm>

#include "resource.h"
#include "i18n/i18n.h"
#include "trigger/trigger.h"
#include "ui/msgbox.h"

namespace {

template <class Fn>
Fn LoadProc(const wchar_t* dll, const char* name) {
    HMODULE mod = GetModuleHandleW(dll);
    if (!mod) mod = LoadLibraryW(dll);
    return mod ? reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(mod, name))) : nullptr;
}

int SystemDpi() {
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    return dpi;
}

// per-monitor DPI on Windows 8.1 and later, otherwise the system DPI
int MonitorDpi(POINT pt) {
    using Fn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    static Fn fn = LoadProc<Fn>(L"shcore.dll", "GetDpiForMonitor");
    UINT x = 96, y = 96;
    if (fn && SUCCEEDED(fn(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), 0, &x, &y))) return (int)x;
    return SystemDpi();
}

// The window may cover the taskbar (it pops up briefly and hides again), so it is kept within the whole monitor
RECT MonitorAt(POINT pt) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    return mi.rcMonitor;
}

// Moves the window onto the monitor; when it doesn't fit, the top-left corner stays visible
POINT ClampToMonitor(POINT pos, SIZE size, const RECT& mon) {
    pos.x = std::max(std::min(pos.x, mon.right - size.cx), mon.left);
    pos.y = std::max(std::min(pos.y, mon.bottom - size.cy), mon.top);
    return pos;
}

// When opened from the hot corner the app isn't in the foreground and SetForegroundWindow is refused;
// borrowing the foreground thread's input state gets around that
void ForceForeground(HWND wnd) {
    HWND fg = GetForegroundWindow();
    DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    DWORD me = GetCurrentThreadId();
    bool attached = fgThread && fgThread != me && AttachThreadInput(fgThread, me, TRUE);
    SetForegroundWindow(wnd);
    SetActiveWindow(wnd);
    SetFocus(wnd);
    if (attached) AttachThreadInput(fgThread, me, FALSE);
}

}  // namespace

bool MainWnd::Create() {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kMainClass;
    if (!RegisterClassExW(&wc)) return false;

    // WS_THICKFRAME is only there for resizing by the border; WM_NCCALCSIZE removes the non-client area
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kMainClass,
                            kAppName, WS_POPUP | WS_THICKFRAME, app_.settings.posX,
                            app_.settings.posY, 100, 100, nullptr, nullptr, inst, this);
    if (!hwnd_) return false;
    oleDrop_ = new DropTarget(hwnd_, *this);
    RegisterDragDrop(hwnd_, oleDrop_);

    page_ = std::max(0, std::min(app_.settings.lastPage, (int)app_.store.pages.size() - 1));
    ApplySettings();
    return true;
}

LRESULT CALLBACK MainWnd::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MainWnd* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWnd*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
    } else {
        self = reinterpret_cast<MainWnd*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWnd::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    switch (msg) {
        case WM_NCCALCSIZE:
            if (wp) return 0;  // the whole window is client area
            break;
        case WM_NCHITTEST: return NcHitTest(pt);
        case WM_SIZING: OnSizing(wp, reinterpret_cast<RECT*>(lp)); return TRUE;
        case WM_SIZE:
            Relayout();
            Redraw();
            return 0;
        case WM_GETMINMAXINFO: {
            if (!skin_) break;  // no skin yet while the window is being created
            SIZE s = WindowSizeFor(sizing_, 1, 1);
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = POINT{s.cx, s.cy};
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            if (!skin_) return 0;
            UpdateSizing();
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SIZE s = WindowSizeFor(sizing_, app_.settings.cols, app_.settings.rows);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, s.cx, s.cy, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_APP_REDRAW:
            redrawPending_ = false;
            if (layered_ && visible_) PaintLayered();
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd_, &ps);
            Paint(dc);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE && visible_ && !app_.settings.pinned && modal_ == 0) Hide();
            return 0;
        case WM_MOUSEMOVE: OnMouseMove(pt, (wp & MK_LBUTTON) != 0); return 0;
        case WM_MOUSELEAVE:
            tracking_ = false;
            if (!dragging_) {
                hover_ = Hit{};
                Redraw();
            }
            return 0;
        case WM_LBUTTONDOWN: OnLButtonDown(pt); return 0;
        case WM_LBUTTONUP: OnLButtonUp(pt); return 0;
        case WM_RBUTTONUP: OnContextMenu(pt); return 0;
        // The caption is non-client area (so it can drag the window); right-click here opens the main menu.
        // The button-down must be swallowed too, or the system starts its own caption menu handling and the
        // button-up never arrives.
        case WM_NCRBUTTONDOWN:
            if (wp == HTCAPTION) return 0;
            break;
        case WM_NCRBUTTONUP:
            if (wp == HTCAPTION) {
                MainMenu(pt);
                return 0;
            }
            break;
        case WM_CONTEXTMENU:  // menu key or Shift+F10 (lp is -1), and non-client right-clicks forwarded by the system
            if (lp == -1) GetCursorPos(&pt);
            MainMenu(pt);
            return 0;
        case WM_XBUTTONUP:  // mouse "back" side button
            if (GET_XBUTTON_WPARAM(wp) == XBUTTON1) CloseSubs();
            return TRUE;
        case WM_MOUSEWHEEL:
            ScreenToClient(hwnd_, &pt);
            OnWheel(pt, GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        case WM_CHAR: OnChar((wchar_t)wp); return 0;
        case WM_IME_STARTCOMPOSITION:
            // the input method's composition window goes to the search box
            if (HIMC imc = ImmGetContext(hwnd_)) {
                COMPOSITIONFORM cf{CFS_POINT, POINT{layout_.search.left, layout_.search.top}, {}};
                ImmSetCompositionWindow(imc, &cf);
                ImmReleaseContext(hwnd_, imc);
            }
            break;
        case WM_KEYDOWN:
            if (OnSearchKey((UINT)wp)) return 0;
            if (wp == VK_ESCAPE) {
                if (drilled_) CloseSubs();
                else Hide();
            } else if (wp == VK_BACK) {
                CloseSubs();
            }
            return 0;
        case WM_TIMER: OnTimer(wp); return 0;
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm->hwndFrom == tooltip_ && nm->code == TTN_GETDISPINFOW) {
                tipText_ = TooltipText();
                reinterpret_cast<NMTTDISPINFOW*>(lp)->lpszText = &tipText_[0];
            }
            return 0;
        }

        case WM_HOTKEY: Toggle(); return 0;
        case WM_APP_SHOW: {
            POINT cur;
            GetCursorPos(&cur);
            Show(cur, app_.settings.showAt == ShowAt::Mouse);
            return 0;
        }
        case WM_APP_CORNER_ENTER:
            if (visible_ || trigger::IsForegroundFullscreen(pt)) return 0;
            if (app_.settings.cornerDelay <= 0) {
                Show(pt, app_.settings.showAt == ShowAt::Mouse);
            } else {
                cornerPt_ = pt;
                SetTimer(hwnd_, kTimerCorner, app_.settings.cornerDelay, nullptr);
            }
            return 0;
        case WM_APP_CORNER_LEAVE: KillTimer(hwnd_, kTimerCorner); return 0;
        case WM_APP_ICON_READY:
            if (visible_) LoadIcons();
            return 0;
        case WM_DISPLAYCHANGE: trigger::RefreshMonitors(); return 0;

        case WM_CLOSE: Hide(); return 0;  // Alt+F4 only hides; quitting is in the right-click menu
        case WM_ENDSESSION:
            if (wp) {
                app_.SaveSettings();
                app_.SaveData();
            }
            return 0;
        case WM_DESTROY:
            RevokeDragDrop(hwnd_);
            oleDrop_->Release();
            oleDrop_ = nullptr;
            trigger::StopHotCorner();
            trigger::UnregisterHotkey(hwnd_);
            app_.SaveSettings();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void MainWnd::OnTimer(UINT_PTR id) {
    if (id == kTimerLeave) {
        CheckMouseLeft();
        return;
    }
    KillTimer(hwnd_, id);
    if (id == kTimerCorner) {
        POINT cur;
        GetCursorPos(&cur);
        Show(cur, app_.settings.showAt == ShowAt::Mouse);
    } else if (id == kTimerHover) {
        // hovering a tab switches to it; while dragging a button, hovering "‹ parent page" goes back to the
        // top-level tabs so it can go to another page
        if (hover_.kind == HitKind::Tab) SwitchPage(hover_.index);
        else if (hover_.kind == HitKind::SubTab) SwitchSub(hover_.index);
        else if (hover_.kind == HitKind::Parent && (dragging_ || fileDrag_)) CloseSubs();
    }
}

void MainWnd::Show(POINT anchor, bool atMouse) {
    if (app_.store.pages.empty()) return;
    dpi_ = MonitorDpi(anchor);
    UpdateSizing();
    if (!tooltip_) {
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                   WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, 0, 0, 0, 0, hwnd_,
                                   nullptr, GetModuleHandleW(nullptr), nullptr);
        TOOLINFOW ti{sizeof(ti)};
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = hwnd_;
        ti.uId = (UINT_PTR)hwnd_;
        ti.lpszText = LPSTR_TEXTCALLBACKW;
        SendMessageW(tooltip_, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, 600);
    }

    const Settings& s = app_.settings;
    SIZE size = WindowSizeFor(sizing_, s.cols, s.rows);
    POINT pos = atMouse ? POINT{anchor.x - size.cx / 2, anchor.y - size.cy / 2} : POINT{s.posX, s.posY};
    pos = ClampToMonitor(pos, size, MonitorAt(atMouse ? anchor : pos));

    // position and draw everything before showing; a layered window shown before drawing is blank
    visible_ = true;
    LoadIcons();
    SetWindowPos(hwnd_, HWND_TOPMOST, pos.x, pos.y, size.cx, size.cy, SWP_NOACTIVATE);
    Relayout();
    if (layered_) PaintLayered();
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ForceForeground(hwnd_);
    Redraw();

    // opened by hotkey or from elsewhere the mouse isn't over the window: wait until it has been inside once
    // before hiding when it leaves
    entered_ = CursorInWindow();
    SetTimer(hwnd_, kTimerLeave, 50, nullptr);
}

bool MainWnd::CursorInWindow() const {
    POINT cur;
    RECT wr;
    return GetCursorPos(&cur) && GetWindowRect(hwnd_, &wr) && PtInRect(&wr, cur);
}

void MainWnd::CheckMouseLeft() {
    if (!visible_) {
        KillTimer(hwnd_, kTimerLeave);
        return;
    }
    if (CursorInWindow()) {
        entered_ = true;
        return;
    }
    // don't hide while pinned, while a menu or dialog is open, or while dragging a button or the border (the
    // window then holds the mouse capture)
    if (entered_ && !app_.settings.pinned && modal_ == 0 && !dragging_ && !fileDrag_ &&
        GetCapture() == nullptr)
        Hide();
}

void MainWnd::ShowWelcome() {
    const Settings& s = app_.settings;
    Show(POINT{s.posX, s.posY}, false);

    std::wstring text = T(L"msg.welcome", L"QLaunch is running in the background.");
    if (s.hotCorner != HotCorner::None) {
        const wchar_t* corners[] = {L"", T(L"corner.lt", L"Top left"), T(L"corner.rt", L"Top right"),
                                    T(L"corner.lb", L"Bottom left"), T(L"corner.rb", L"Bottom right")};
        std::wstring line = T(L"msg.welcomeCorner", L"To open it, move the mouse to the screen corner: %s.");
        size_t at = line.find(L"%s");
        if (at != std::wstring::npos) line.replace(at, 2, corners[(int)s.hotCorner]);
        text += L"\n\n" + line;
    }
    text += L"\n";
    text += T(L"msg.welcomeMenu", L"Right-click the title bar for settings.");
    {
        ModalGuard guard(*this);
        MessageBoxAtCursor(hwnd_, text.c_str(), MB_OK | MB_ICONINFORMATION);
    }
    if (!CursorInWindow()) Hide();
}

void MainWnd::Hide() {
    if (!visible_) return;
    visible_ = false;
    KillTimer(hwnd_, kTimerLeave);
    dragging_ = false;
    hover_ = pressed_ = dropTarget_ = Hit{};
    if (GetCapture() == hwnd_) ReleaseCapture();
    ShowWindow(hwnd_, SW_HIDE);
    StopSearch();

    RECT wr;
    GetWindowRect(hwnd_, &wr);
    app_.settings.posX = wr.left;
    app_.settings.posY = wr.top;
    app_.settings.lastPage = page_;
    app_.SaveSettings();

    // keep as little memory as possible while hidden: free icons, canvas, skin images and fonts, then trim the
    // working set
    app_.iconLoader.CancelAll();
    ReleaseIcons();
    canvas_.Release();
    fonts_.clear();
    if (skin_) skin_->Release();
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

void MainWnd::Toggle() {
    if (visible_) {
        Hide();
        return;
    }
    POINT cur;
    GetCursorPos(&cur);
    Show(cur, app_.settings.showAt == ShowAt::Mouse);
}

void MainWnd::ApplySettings() {
    trigger::StartHotCorner(hwnd_, app_.settings.hotCorner);
    trigger::RegisterHotkey(hwnd_, app_.settings.hotkey);
    ApplyLook();
}

void MainWnd::ApplyLook() {
    skin_.reset();  // loaded again with the new settings when next needed
    if (visible_) {
        UpdateSizing();
        UpdateWindowSize();
        ReleaseIcons();  // the icon size may have changed
        LoadIcons();
        Relayout();
        if (layered_) PaintLayered();
        if (!IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_SHOWNA);  // don't steal the focus while the settings dialog previews
        Redraw();
    }
}

Skin& MainWnd::EnsureSkin() {
    if (skin_) return *skin_;
    skin_ = CreateSkin(app_.settings.skin);

    // image skins need a per-pixel-alpha layered window; a layered window gets no system shadow (it would be
    // rectangular)
    bool layered = skin_->Layered();
    if (layered != layered_) {
        // when switching window type while shown, hide first and show again once the new content is drawn, so
        // the old picture never flashes
        if (IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE);
        layered_ = layered;
        LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, layered ? ex | WS_EX_LAYERED : ex & ~WS_EX_LAYERED);
        ULONG_PTR cs = GetClassLongPtrW(hwnd_, GCL_STYLE);
        SetClassLongPtrW(hwnd_, GCL_STYLE, layered ? cs & ~CS_DROPSHADOW : cs | CS_DROPSHADOW);
    }
    return *skin_;
}

void MainWnd::UpdateSizing() {
    Skin& skin = EnsureSkin();
    const Settings& s = app_.settings;
    sizing_.m = Metrics::From(s, dpi_);
    sizing_.side = s.tabSide;
    skin.SetInput(dpi_, SkinInput{s.tabSide, sizing_.m.caption, sizing_.m.pad, sizing_.m.gap});
    sizing_.sm = skin.Metrics();

    fonts_.clear();
    SelectObj sel(canvas_.dc(), FontFor(skin.Text(Part::Cell, kNormal), Part::Cell));
    TEXTMETRICW tm;
    GetTextMetricsW(canvas_.dc(), &tm);
    textH_ = tm.tmHeight;
}

void MainWnd::UpdateWindowSize() {
    const Settings& s = app_.settings;
    if (!skin_) return;
    SIZE size = WindowSizeFor(sizing_, s.cols, s.rows);
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    if (wr.right - wr.left == size.cx && wr.bottom - wr.top == size.cy) return;
    POINT pos = ClampToMonitor(POINT{wr.left, wr.top}, size, MonitorAt(POINT{wr.left, wr.top}));
    SetWindowPos(hwnd_, nullptr, pos.x, pos.y, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
}

void MainWnd::Relayout() {
    if (app_.store.pages.empty() || !skin_) return;
    const Page& page = app_.store.pages[page_];
    LayoutInput in;
    in.s = sizing_;
    in.drilled = drilled_;
    HFONT tabFont = FontFor(skin_->Text(Part::Tab, kSelected), Part::Tab);  // the current tab's font is usually the widest
    if (drilled_) {
        in.tabTextW.push_back(TextWidth(tabFont, L"‹ " + page.name));
        for (auto& sp : page.subs) in.tabTextW.push_back(TextWidth(tabFont, sp.name));
    } else {
        for (auto& p : app_.store.pages)
            in.tabTextW.push_back(TextWidth(tabFont, p.subs.empty() ? p.name : p.name + L" ›"));
    }

    RECT rc;
    GetClientRect(hwnd_, &rc);
    ComputeLayout(layout_, rc.right, rc.bottom, in, *skin_);
    if (Searching()) PlaceResults();

    // the scroll position can't go past the content
    int needRows = CurrentView().RowCount();
    scrollRow_ = std::max(0, std::min(scrollRow_, needRows - layout_.rows));
}

LRESULT MainWnd::NcHitTest(POINT pt) {
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    const RECT& b = sizing_.sm.sizeArea;
    bool left = pt.x < wr.left + b.left, right = pt.x >= wr.right - b.right;
    bool top = pt.y < wr.top + b.top, bottom = pt.y >= wr.bottom - b.bottom;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    ScreenToClient(hwnd_, &pt);
    return HitTest(layout_, pt).kind == HitKind::Caption ? HTCAPTION : HTCLIENT;
}

// Snaps to whole cells while the border is dragged
void MainWnd::OnSizing(WPARAM edge, RECT* rc) {
    if (!skin_) return;
    Settings& s = app_.settings;
    GridFromWindowSize(sizing_, rc->right - rc->left, rc->bottom - rc->top, s.cols, s.rows);
    SIZE size = WindowSizeFor(sizing_, s.cols, s.rows);

    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
        rc->left = rc->right - size.cx;
    else
        rc->right = rc->left + size.cx;
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
        rc->top = rc->bottom - size.cy;
    else
        rc->bottom = rc->top + size.cy;
}
