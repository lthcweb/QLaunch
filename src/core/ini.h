// Minimal ini reader/writer: the whole file is parsed in memory, keeping sections and keys in order.
// Reads UTF-8 (BOM optional) and UTF-16LE (with BOM). Writes UTF-8 with BOM via a temp file + replace.
#pragma once

#include "core/base.h"

class Ini {
public:
    struct Section {
        std::wstring name;
        std::vector<std::pair<std::wstring, std::wstring>> values;

        const std::wstring* Find(const wchar_t* key) const;
        std::wstring Get(const wchar_t* key, const wchar_t* def = L"") const;
        int GetInt(const wchar_t* key, int def = 0) const;
        void Set(const wchar_t* key, const std::wstring& value);
        void SetInt(const wchar_t* key, int value);
    };

    bool Load(const std::wstring& path);
    bool Save(const std::wstring& path) const;

    const std::vector<Section>& Sections() const { return sections_; }
    const Section* Find(const wchar_t* name) const;
    Section& Get(const wchar_t* name);  // appended when missing
    Section& Add(const std::wstring& name);

    std::wstring GetStr(const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"") const;
    int GetInt(const wchar_t* sec, const wchar_t* key, int def = 0) const;

private:
    std::vector<Section> sections_;
};

// Reads a whole text file and converts it to UTF-16, detecting the encoding
bool ReadTextFile(const std::wstring& path, std::wstring& out);
