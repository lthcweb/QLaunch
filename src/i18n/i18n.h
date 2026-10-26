// Localization: lang/<code>.ini holds key=text lines in its [Strings] section.
// Code calls T(L"menu.open", L"Open"); the second argument is the English default, shown when the file lacks
// the key.
#pragma once

#include "core/base.h"

namespace i18n {

struct Language {
    std::wstring code;  // file name, e.g. zh-CN
    std::wstring name;  // display name, e.g. 简体中文
};

// An empty code picks the language from the system UI language
void Load(const std::wstring& code);
const std::wstring& Current();
std::vector<Language> List();

const wchar_t* T(const wchar_t* key, const wchar_t* fallback);

// Dialog control text: keys look like "<prefix>.<suffix>"; missing keys keep the English text from the
// resources
struct DlgText {
    int id;               // control ID; 0 means the dialog title
    const wchar_t* key;
};
void ApplyDialog(HWND dlg, const DlgText* items, size_t count);

}  // namespace i18n

using i18n::T;
