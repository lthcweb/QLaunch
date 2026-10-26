// Icons: extraction (calls the Shell; slow and memory hungry) -> icons.dat cache -> the UI only reads the
// cache.
// Pixels are always square, size x size, premultiplied-alpha BGRA.
#pragma once

#include "core/base.h"
#include "model/model.h"

struct IconPixels {
    int size = 0;
    std::vector<uint32_t> px;
    bool Empty() const { return px.empty(); }
};

// Same file, icon index, size and file time -> same key
uint64_t IconKey(const Item& item, int size);

// Extracts an icon on the calling thread (COM must be initialized). Returns empty on failure.
IconPixels ExtractIconPixels(const Item& item, int size);

// icons.dat: a header + {key, size, pixels} entries. New icons are appended. Thread safe.
class IconCache {
public:
    IconCache();
    ~IconCache();
    QL_NO_COPY(IconCache);

    void Open(const std::wstring& path);
    bool Get(uint64_t key, IconPixels& out);
    void Put(uint64_t key, const IconPixels& icon);
    void Clear();  // deletes the cache file; icons are extracted again next time

private:
    void EnsureIndex();

    struct Entry {
        uint64_t key;
        uint32_t offset;
        uint32_t size;
    };
    CRITICAL_SECTION lock_;
    std::wstring path_;
    std::vector<Entry> index_;  // read on first use, the file isn't touched at startup
    bool indexed_ = false;
};

// Background extraction thread: it exits when the queue is empty, so the Shell DLLs load only when needed.
// Each extracted icon is written to the cache and WM_APP_ICON_READY is posted to notify.
class IconLoader {
public:
    explicit IconLoader(IconCache& cache) : cache_(cache) { InitializeCriticalSection(&lock_); }
    ~IconLoader();
    QL_NO_COPY(IconLoader);

    void Request(HWND notify, const Item& item, int size, uint64_t key);
    void CancelAll();

private:
    static DWORD WINAPI ThreadProc(void* self);
    void Run();

    struct Job {
        HWND notify;
        Item item;
        int size;
        uint64_t key;
    };
    IconCache& cache_;
    CRITICAL_SECTION lock_;
    std::vector<Job> jobs_;
    HANDLE thread_ = nullptr;
    bool running_ = false;
};
