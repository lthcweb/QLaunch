#include "core/str.h"

#include <cwchar>

std::wstring Utf8ToWide(const char* s, int len) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, len, &out[0], n);
    if (len < 0) out.pop_back();  // drop the trailing '\0'
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring DecodeText(const void* data, size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    if (size >= 2 && p[0] == 0xFF && p[1] == 0xFE)
        return std::wstring(reinterpret_cast<const wchar_t*>(p + 2), (size - 2) / 2);
    if (size >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        return Utf8ToWide(reinterpret_cast<const char*>(p + 3), (int)size - 3);
    return Utf8ToWide(reinterpret_cast<const char*>(p), (int)size);
}

std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return {};
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool EqualsNoCase(const std::wstring& a, const wchar_t* b) {
    return _wcsicmp(a.c_str(), b) == 0;
}

bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix) {
    size_t n = wcslen(prefix);
    return s.size() >= n && _wcsnicmp(s.c_str(), prefix, n) == 0;
}

int ToInt(const std::wstring& s, int def) {
    if (s.empty()) return def;
    wchar_t* end = nullptr;
    long v = wcstol(s.c_str(), &end, 0);  // accepts a 0x prefix
    return end == s.c_str() ? def : (int)v;
}

std::wstring FromInt(int v) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", v);
    return buf;
}
