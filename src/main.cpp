// Entry point: single-instance check -> load settings and data -> create the hidden main window -> message loop
#include "app.h"

#include <commctrl.h>
#include <ole2.h>

#include "core/handle.h"
#include "core/path.h"
#include "i18n/i18n.h"
#include "launch/builtin.h"
#include "ui/main_wnd.h"

namespace {

// Gives a first run one default page; returns whether it was created
bool EnsureDefaultPage(App& app) {
    if (!app.store.pages.empty()) return false;
    Page page;
    page.name = T(L"page.default", L"Main");
    app.store.pages.push_back(std::move(page));
    app.SaveData();
    return true;
}

// The built-in System page is created only once, as the first page: on first run, or on the first run after
// upgrading.
// Existing pages move down one place and the current page follows them; a new user starts on the System page
void EnsureSystemPage(App& app, bool fresh) {
    if (app.settings.systemPage) return;
    auto& pages = app.store.pages;
    pages.insert(pages.begin(), MakeSystemPage(app.settings.cols));
    app.settings.lastPage = fresh ? 0 : app.settings.lastPage + 1;
    app.settings.systemPage = true;
    app.SaveData();
    app.SaveSettings();
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // already running: show that instance and quit
    Handle mutex(CreateMutexW(nullptr, TRUE, L"QLaunch.SingleInstance"));
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(kMainClass, nullptr)) PostMessageW(other, WM_APP_SHOW, 0, 0);
        return 0;
    }

    // ShellExecuteEx needs COM on the calling thread; OLE drag and drop (RegisterDragDrop) needs OleInitialize
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);

    App app;
    app.settingsPath = AppFile(L"QLaunch.ini");
    app.dataPath = AppFile(L"data.ini");
    bool firstRun = !FileExists(app.settingsPath);
    app.settings.Load(app.settingsPath);
    i18n::Load(app.settings.language);
    app.store.Load(app.dataPath, app.settings.cols);
    if (app.store.legacy) app.SaveData();
    bool fresh = EnsureDefaultPage(app);
    EnsureSystemPage(app, fresh);
    app.iconCache.Open(AppFile(L"icons.dat"));

    MainWnd wnd(app);
    if (!wnd.Create()) return 1;
    if (firstRun) wnd.ShowWelcome();  // a new user doesn't know where the window is, so show it once

    // trim the working set after startup so the resident app uses very little memory
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    OleUninitialize();
    return (int)msg.wParam;
}
