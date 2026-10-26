#include "icon/icon.h"

#include <objbase.h>

IconLoader::~IconLoader() {
    CancelAll();
    if (thread_) {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
    }
    DeleteCriticalSection(&lock_);
}

void IconLoader::Request(HWND notify, const Item& item, int size, uint64_t key) {
    EnterCriticalSection(&lock_);
    bool queued = false;
    for (auto& j : jobs_) queued |= (j.key == key);
    if (!queued) jobs_.push_back(Job{notify, item, size, key});
    bool start = !running_;
    running_ = true;
    LeaveCriticalSection(&lock_);

    if (start) {
        if (thread_) CloseHandle(thread_);  // the previous thread has already exited
        thread_ = CreateThread(nullptr, 0, ThreadProc, this, 0, nullptr);
    }
}

void IconLoader::CancelAll() {
    EnterCriticalSection(&lock_);
    jobs_.clear();
    LeaveCriticalSection(&lock_);
}

DWORD WINAPI IconLoader::ThreadProc(void* self) {
    // the Shell icon interfaces need a single-threaded apartment
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    static_cast<IconLoader*>(self)->Run();
    CoUninitialize();
    return 0;
}

void IconLoader::Run() {
    for (;;) {
        EnterCriticalSection(&lock_);
        if (jobs_.empty()) {
            running_ = false;  // set inside the lock; Request sees it and starts a new thread
            LeaveCriticalSection(&lock_);
            return;
        }
        Job job = std::move(jobs_.front());
        jobs_.erase(jobs_.begin());
        LeaveCriticalSection(&lock_);

        IconPixels px = ExtractIconPixels(job.item, job.size);
        // store a 1x1 transparent placeholder on failure so it isn't extracted again on every show
        if (px.Empty()) px = IconPixels{1, std::vector<uint32_t>(1, 0)};
        cache_.Put(job.key, px);
        PostMessageW(job.notify, WM_APP_ICON_READY, 0, 0);
    }
}
