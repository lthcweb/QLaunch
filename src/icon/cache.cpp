#include "icon/icon.h"

#include "core/handle.h"

// icons.dat layout:
//   header { uint32 magic, uint32 version }
//   entry  { uint64 key, uint32 size, uint32 pixels[size*size] }, repeated
// When a key appears more than once the last entry wins (append-only: simple and robust).

namespace {

constexpr uint32_t kMagic = 0x43494C51;  // "QLIC"
constexpr uint32_t kVersion = 4;  // bump when the extraction changes; old caches are discarded and rebuilt
constexpr uint32_t kMaxSize = 256;

struct FileHeader {
    uint32_t magic, version;
};
#pragma pack(push, 4)
struct EntryHeader {
    uint64_t key;
    uint32_t size;
};
#pragma pack(pop)

class Lock {
public:
    explicit Lock(CRITICAL_SECTION& cs) : cs_(cs) { EnterCriticalSection(&cs_); }
    ~Lock() { LeaveCriticalSection(&cs_); }
    QL_NO_COPY(Lock);

private:
    CRITICAL_SECTION& cs_;
};

bool ReadAt(HANDLE f, uint32_t offset, void* buf, DWORD len) {
    LARGE_INTEGER pos;
    pos.QuadPart = offset;
    DWORD got = 0;
    return SetFilePointerEx(f, pos, nullptr, FILE_BEGIN) && ReadFile(f, buf, len, &got, nullptr) &&
           got == len;
}

}  // namespace

IconCache::IconCache() { InitializeCriticalSection(&lock_); }
IconCache::~IconCache() { DeleteCriticalSection(&lock_); }

void IconCache::Open(const std::wstring& path) {
    Lock lock(lock_);
    path_ = path;
    index_.clear();
    indexed_ = false;
}

void IconCache::EnsureIndex() {
    if (indexed_) return;
    indexed_ = true;
    Handle f(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!f) return;

    FileHeader fh{};
    if (!ReadAt(f.get(), 0, &fh, sizeof(fh)) || fh.magic != kMagic || fh.version != kVersion)
        return;  // wrong format: treat it as no cache; the next write rebuilds it
    uint32_t offset = sizeof(fh);
    EntryHeader eh;
    while (ReadAt(f.get(), offset, &eh, sizeof(eh)) && eh.size > 0 && eh.size <= kMaxSize) {
        uint32_t bytes = eh.size * eh.size * 4;
        Entry e{eh.key, offset + (uint32_t)sizeof(eh), eh.size};
        bool replaced = false;
        for (auto& old : index_) {
            if (old.key == e.key) {
                old = e;
                replaced = true;
                break;
            }
        }
        if (!replaced) index_.push_back(e);
        offset += sizeof(eh) + bytes;
    }
}

bool IconCache::Get(uint64_t key, IconPixels& out) {
    Lock lock(lock_);
    EnsureIndex();
    for (auto& e : index_) {
        if (e.key != key) continue;
        Handle f(CreateFileW(path_.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                             0, nullptr));
        if (!f) return false;
        out.size = (int)e.size;
        out.px.resize((size_t)e.size * e.size);
        if (ReadAt(f.get(), e.offset, out.px.data(), (DWORD)(out.px.size() * 4))) return true;
        out = IconPixels{};
        return false;
    }
    return false;
}

void IconCache::Put(uint64_t key, const IconPixels& icon) {
    if (icon.Empty() || icon.size > (int)kMaxSize) return;
    Lock lock(lock_);
    EnsureIndex();

    Handle f(CreateFileW(path_.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                         OPEN_ALWAYS, 0, nullptr));
    if (!f) return;
    LARGE_INTEGER size{};
    GetFileSizeEx(f.get(), &size);
    DWORD written = 0;
    if (size.QuadPart == 0 || index_.empty()) {  // new file or old format: rewrite the header
        FileHeader fh{kMagic, kVersion};
        SetFilePointer(f.get(), 0, nullptr, FILE_BEGIN);
        WriteFile(f.get(), &fh, sizeof(fh), &written, nullptr);
        SetEndOfFile(f.get());
        size.QuadPart = sizeof(fh);
    }
    SetFilePointerEx(f.get(), size, nullptr, FILE_BEGIN);
    EntryHeader eh{key, (uint32_t)icon.size};
    WriteFile(f.get(), &eh, sizeof(eh), &written, nullptr);
    WriteFile(f.get(), icon.px.data(), (DWORD)(icon.px.size() * 4), &written, nullptr);

    Entry e{key, (uint32_t)size.QuadPart + (uint32_t)sizeof(eh), (uint32_t)icon.size};
    for (auto& old : index_) {
        if (old.key == key) {
            old = e;
            return;
        }
    }
    index_.push_back(e);
}

void IconCache::Clear() {
    Lock lock(lock_);
    DeleteFileW(path_.c_str());
    index_.clear();
    indexed_ = true;
}
