// Path helpers. The app is portable: every data file lives next to the exe.
#pragma once

#include "core/base.h"

const std::wstring& AppDir();                       // ends with '\'
std::wstring AppFile(const wchar_t* relative);      // AppDir() + relative

std::wstring ExpandEnv(const std::wstring& path);   // expands %windir% and other variables
std::wstring ResolvePath(const std::wstring& path); // expands variables; relative paths start at the exe folder
std::wstring DirOf(const std::wstring& path);       // no trailing '\'
std::wstring FileTitle(const std::wstring& path);   // no folder, no extension
bool FileExists(const std::wstring& path);
bool DirExists(const std::wstring& path);
uint64_t LastWriteTime(const std::wstring& path);   // 0 when the file doesn't exist
// Reads a whole file; anything larger than maxSize bytes counts as a failure
bool ReadBinaryFile(const std::wstring& path, std::vector<uint8_t>& out, size_t maxSize);
