// RAII wrappers for GDI objects, kernel handles and icons. A resident app must not leak handles.
#pragma once

#include "core/base.h"

template <class T, class Traits>
class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(T h) : h_(h) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(UniqueHandle&& o) noexcept : h_(o.release()) {}
    UniqueHandle& operator=(UniqueHandle&& o) noexcept {
        if (this != &o) reset(o.release());
        return *this;
    }
    QL_NO_COPY(UniqueHandle);

    T get() const { return h_; }
    explicit operator bool() const { return Traits::Valid(h_); }
    T release() {
        T h = h_;
        h_ = Traits::Invalid();
        return h;
    }
    void reset(T h = Traits::Invalid()) {
        if (Traits::Valid(h_)) Traits::Close(h_);
        h_ = h;
    }

private:
    T h_ = Traits::Invalid();
};

struct GdiTraits {
    static HGDIOBJ Invalid() { return nullptr; }
    static bool Valid(HGDIOBJ h) { return h != nullptr; }
    static void Close(HGDIOBJ h) { DeleteObject(h); }
};
struct KernelTraits {
    static HANDLE Invalid() { return nullptr; }
    static bool Valid(HANDLE h) { return h != nullptr && h != INVALID_HANDLE_VALUE; }
    static void Close(HANDLE h) { CloseHandle(h); }
};
struct IconTraits {
    static HICON Invalid() { return nullptr; }
    static bool Valid(HICON h) { return h != nullptr; }
    static void Close(HICON h) { DestroyIcon(h); }
};

// GDI objects are all stored as HGDIOBJ and cast back when used
template <class T>
class GdiObj : public UniqueHandle<HGDIOBJ, GdiTraits> {
public:
    GdiObj() = default;
    explicit GdiObj(T h) : UniqueHandle(h) {}
    T get() const { return static_cast<T>(UniqueHandle::get()); }
};

using Handle = UniqueHandle<HANDLE, KernelTraits>;
using IconHandle = UniqueHandle<HICON, IconTraits>;

// Selects an object into a DC and restores the old one at the end of the scope
class SelectObj {
public:
    SelectObj(HDC dc, HGDIOBJ obj) : dc_(dc), old_(SelectObject(dc, obj)) {}
    ~SelectObj() { SelectObject(dc_, old_); }
    QL_NO_COPY(SelectObj);

private:
    HDC dc_;
    HGDIOBJ old_;
};

// COM interface pointer, released in the destructor
template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() {
        if (p_) p_->Release();
    }
    QL_NO_COPY(ComPtr);

    T* operator->() const { return p_; }
    T* get() const { return p_; }
    T** put() {  // for receiving an out parameter
        if (p_) p_->Release();
        p_ = nullptr;
        return &p_;
    }
    explicit operator bool() const { return p_ != nullptr; }

private:
    T* p_ = nullptr;
};
