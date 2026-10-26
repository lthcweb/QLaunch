#include "core/ini.h"

#include "core/handle.h"
#include "core/path.h"
#include "core/str.h"

bool ReadTextFile(const std::wstring& path, std::wstring& out) {
    std::vector<uint8_t> raw;
    if (!ReadBinaryFile(path, raw, 64 * 1024 * 1024)) return false;
    out = DecodeText(raw.data(), raw.size());
    return true;
}

const std::wstring* Ini::Section::Find(const wchar_t* key) const {
    for (auto& kv : values)
        if (EqualsNoCase(kv.first, key)) return &kv.second;
    return nullptr;
}

std::wstring Ini::Section::Get(const wchar_t* key, const wchar_t* def) const {
    const std::wstring* v = Find(key);
    return v ? *v : def;
}

int Ini::Section::GetInt(const wchar_t* key, int def) const {
    const std::wstring* v = Find(key);
    return v ? ToInt(*v, def) : def;
}

void Ini::Section::Set(const wchar_t* key, const std::wstring& value) {
    for (auto& kv : values) {
        if (EqualsNoCase(kv.first, key)) {
            kv.second = value;
            return;
        }
    }
    values.emplace_back(key, value);
}

void Ini::Section::SetInt(const wchar_t* key, int value) { Set(key, FromInt(value)); }

bool Ini::Load(const std::wstring& path) {
    sections_.clear();
    std::wstring text;
    if (!ReadTextFile(path, text)) return false;

    Section* cur = nullptr;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = Trim(text.substr(pos, end - pos));
        pos = end + 1;

        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            size_t close = line.find(L']');
            cur = &Add(Trim(line.substr(1, close == std::wstring::npos ? std::wstring::npos : close - 1)));
            continue;
        }
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || !cur) continue;
        cur->values.emplace_back(Trim(line.substr(0, eq)), Trim(line.substr(eq + 1)));
    }
    return true;
}

bool Ini::Save(const std::wstring& path) const {
    std::wstring text;
    for (auto& sec : sections_) {
        text += L'[' + sec.name + L"]\r\n";
        for (auto& kv : sec.values) text += kv.first + L'=' + kv.second + L"\r\n";
        text += L"\r\n";
    }
    std::string data = "\xEF\xBB\xBF" + WideToUtf8(text);

    std::wstring tmp = path + L".tmp";
    {
        Handle file(CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) return false;
        DWORD written = 0;
        if (!WriteFile(file.get(), data.data(), (DWORD)data.size(), &written, nullptr) ||
            written != data.size())
            return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
}

const Ini::Section* Ini::Find(const wchar_t* name) const {
    for (auto& sec : sections_)
        if (EqualsNoCase(sec.name, name)) return &sec;
    return nullptr;
}

Ini::Section& Ini::Get(const wchar_t* name) {
    for (auto& sec : sections_)
        if (EqualsNoCase(sec.name, name)) return sec;
    return Add(name);
}

Ini::Section& Ini::Add(const std::wstring& name) {
    sections_.push_back(Section{name, {}});
    return sections_.back();
}

std::wstring Ini::GetStr(const wchar_t* sec, const wchar_t* key, const wchar_t* def) const {
    const Section* s = Find(sec);
    return s ? s->Get(key, def) : def;
}

int Ini::GetInt(const wchar_t* sec, const wchar_t* key, int def) const {
    const Section* s = Find(sec);
    return s ? s->GetInt(key, def) : def;
}
