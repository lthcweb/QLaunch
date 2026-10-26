#include "core/path.h"

#include <shlobj.h>
#include <shlwapi.h>

#include "core/handle.h"
#include "core/str.h"

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
    return NativePath(out);
}

std::wstring NativePath(const std::wstring& path) {
#ifdef _WIN64
    return path;
#else
    static const bool wow64 = [] {
        BOOL on = FALSE;
        return IsWow64Process(GetCurrentProcess(), &on) && on;
    }();
    if (!wow64) return path;
    wchar_t win[MAX_PATH];
    UINT n = GetWindowsDirectoryW(win, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return path;
    std::wstring windir(win, n);
    std::wstring system32 = windir + L"\\System32\\";
    if (path.size() <= system32.size() || _wcsnicmp(path.c_str(), system32.c_str(), system32.size()) != 0)
        return path;
    return windir + L"\\Sysnative\\" + path.substr(system32.size());
#endif
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

PIDLIST_ABSOLUTE ParseShellPath(const std::wstring& path) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    if (SUCCEEDED(SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr))) return pidl;
    if (!StartsWithNoCase(path, L"shell:") || path.find(L'\\') != std::wstring::npos) return nullptr;
    std::wstring app = L"shell:AppsFolder\\" + path.substr(6);
    return SUCCEEDED(SHParseDisplayName(app.c_str(), nullptr, &pidl, 0, nullptr)) ? pidl : nullptr;
}

bool IsUrl(const std::wstring& s) {
    if (StartsWithNoCase(s, L"www.") || StartsWithNoCase(s, L"mailto:")) return true;
    // a scheme is a letter followed by letters, digits, '+', '-' or '.'; requiring two characters keeps
    // drive letters (C:) out
    size_t colon = s.find(L"://");
    if (colon == std::wstring::npos || colon < 2 || !iswalpha(s[0])) return false;
    for (size_t i = 1; i < colon; ++i)
        if (!iswalnum(s[i]) && s[i] != L'+' && s[i] != L'-' && s[i] != L'.') return false;
    return true;
}

std::wstring NormalizeUrl(const std::wstring& s) {
    return StartsWithNoCase(s, L"www.") ? L"https://" + s : s;
}

std::wstring UrlSiteName(const std::wstring& url) {
    std::wstring host = NormalizeUrl(url);
    size_t start = host.find(L"://");
    host = start == std::wstring::npos ? host.substr(host.find(L':') + 1) : host.substr(start + 3);
    host = host.substr(0, host.find_first_of(L"/?#"));
    host = host.substr(host.find(L'@') + 1);  // user@host; npos + 1 == 0 keeps the whole string
    host = host.substr(0, host.find(L':'));   // port
    if (StartsWithNoCase(host, L"www.")) host.erase(0, 4);
    return host.empty() ? url : host;
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
