#include "core/path.h"

#include <shlwapi.h>

#include "core/handle.h"

const std::wstring& AppDir() {
    static std::wstring dir = [] {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        std::wstring s(buf, n);
        return s.substr(0, s.find_last_of(L'\\') + 1);
    }();
    return dir;
}

std::wstring AppFile(const wchar_t* relative) { return AppDir() + relative; }

std::wstring ExpandEnv(const std::wstring& path) {
    if (path.find(L'%') == std::wstring::npos) return path;
    wchar_t buf[MAX_PATH * 2];
    DWORD n = ExpandEnvironmentStringsW(path.c_str(), buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return (n == 0 || n > sizeof(buf) / sizeof(buf[0])) ? path : std::wstring(buf);
}

std::wstring ResolvePath(const std::wstring& path) {
    std::wstring out = ExpandEnv(path);
    if (!out.empty() && PathIsRelativeW(out.c_str())) out = AppDir() + out;
    return out;
}

std::wstring DirOf(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? std::wstring() : path.substr(0, p);
}

std::wstring FileTitle(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    return (dot == std::wstring::npos || dot == 0) ? name : name.substr(0, dot);
}

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t LastWriteTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
}

bool ReadBinaryFile(const std::wstring& path, std::vector<uint8_t>& out, size_t maxSize) {
    out.clear();
    Handle f(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    LARGE_INTEGER size{};
    if (!f || !GetFileSizeEx(f.get(), &size) || (uint64_t)size.QuadPart > maxSize) return false;
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    if (!out.empty() && (!ReadFile(f.get(), out.data(), (DWORD)out.size(), &read, nullptr) || read != out.size())) {
        out.clear();
        return false;
    }
    return true;
}
