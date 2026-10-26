// Accepts things dragged in from Explorer, and links dragged from a browser (OLE drag and drop).
// WM_DROPFILES only receives real files; virtual objects such as Control Panel, Recycle Bin and This PC
// are only offered as Shell IDLists, so IDropTarget is used instead.
#pragma once

#include "core/base.h"

#include <ole2.h>

struct DroppedItem {
    std::wstring file;  // file path, a virtual object such as shell:::{CLSID}, or a web address
    std::wstring name;  // name shown on the button
};

// Implemented by the window; all points are in client coordinates
class DropSink {
public:
    virtual void DragOver(POINT pt) = 0;
    virtual void DragEnd() = 0;  // dragged away or dropped
    virtual void DropItems(POINT pt, std::vector<DroppedItem>& items) = 0;

protected:
    ~DropSink() = default;
};

class DropTarget : public IDropTarget {
public:
    DropTarget(HWND hwnd, DropSink& sink) : hwnd_(hwnd), sink_(sink) {}
    QL_NO_COPY(DropTarget);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override;

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;

private:
    virtual ~DropTarget() = default;
    POINT ToClient(POINTL pt) const;

    HWND hwnd_;
    DropSink& sink_;
    ULONG refs_ = 1;
    bool accept_ = false;  // the dragged data holds something we can use
};
