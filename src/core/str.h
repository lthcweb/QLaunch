// String helpers
#pragma once

#include "core/base.h"

std::wstring Utf8ToWide(const char* s, int len = -1);
std::string WideToUtf8(const std::wstring& s);
// Detects UTF-16LE / UTF-8 by BOM; no BOM means UTF-8
std::wstring DecodeText(const void* data, size_t size);

std::wstring Trim(const std::wstring& s);
bool EqualsNoCase(const std::wstring& a, const wchar_t* b);
bool StartsWithNoCase(const std::wstring& s, const wchar_t* prefix);

int ToInt(const std::wstring& s, int def = 0);
std::wstring FromInt(int v);
