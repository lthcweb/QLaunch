#include "i18n/i18n.h"

#include <unordered_map>

#include "core/ini.h"
#include "core/path.h"
#include "core/str.h"

namespace i18n {
namespace {

std::wstring g_code;
std::unordered_map<std::wstring, std::wstring> g_strings;

std::wstring LangFile(const std::wstring& code) { return AppDir() + L"lang\\" + code + L".ini"; }

// System UI language -> language file code such as zh-CN. Returns en when there is no matching file.
std::wstring DetectCode() {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name,
                         LOCALE_NAME_MAX_LENGTH, 0) > 0) {
        std::wstring code = name;
        if (FileExists(LangFile(code))) return code;
        code = code.substr(0, code.find(L'-'));  // zh-SG → zh
        if (FileExists(LangFile(code))) return code;
    }
    return L"en";
}

}  // namespace

void Load(const std::wstring& code) {
    g_code = code.empty() ? DetectCode() : code;
    g_strings.clear();

    Ini ini;
    if (!ini.Load(LangFile(g_code))) return;
    if (const Ini::Section* sec = ini.Find(L"Strings")) {
        for (auto& kv : sec->values) {
            std::wstring text = kv.second;
            // language files write line breaks and tabs as
            //  and \t (\t separates menu shortcuts)
            for (size_t p = 0; (p = text.find(L'\\', p)) != std::wstring::npos; ++p) {
                if (p + 1 >= text.size()) break;
                wchar_t c = text[p + 1];
                if (c == L'n' || c == L't') text.replace(p, 2, 1, c == L'n' ? L'\n' : L'\t');
            }
            g_strings[kv.first] = text;
        }
    }
}

const std::wstring& Current() { return g_code; }

std::vector<Language> List() {
    std::vector<Language> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((AppDir() + L"lang\\*.ini").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        Language lang;
        lang.code = FileTitle(fd.cFileName);
        Ini ini;
        ini.Load(LangFile(lang.code));
        lang.name = ini.GetStr(L"Info", L"Name", lang.code.c_str());
        out.push_back(lang);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

const wchar_t* T(const wchar_t* key, const wchar_t* fallback) {
    auto it = g_strings.find(key);
    return it == g_strings.end() ? fallback : it->second.c_str();
}

void ApplyDialog(HWND dlg, const DlgText* items, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        auto it = g_strings.find(items[i].key);
        if (it == g_strings.end()) continue;
        HWND target = items[i].id == 0 ? dlg : GetDlgItem(dlg, items[i].id);
        if (target) SetWindowTextW(target, it->second.c_str());
    }
}

}  // namespace i18n
