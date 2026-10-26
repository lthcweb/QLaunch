// Read-only zip: reads the central directory and extracts files by name (stored or deflate). Skin zips are a
// few hundred KB, read whole into memory.
#pragma once

#include "core/base.h"

class ZipReader {
public:
    bool Open(const std::wstring& path);

    // names match the path in the zip, case-insensitively, with '\' and '/' treated the same
    bool Read(const std::string& name, std::vector<uint8_t>& out) const;
    // finds the first entry whose file name (without folders) is name and returns its full path
    std::string FindByFileName(const std::string& name) const;

private:
    struct Entry {
        std::string name;
        uint32_t offset;     // offset of the local file header
        uint32_t packed, size;
        uint16_t method;
    };
    std::vector<uint8_t> data_;
    std::vector<Entry> entries_;
};

// Raw deflate (RFC 1951) decoder. out must already be resized to the decompressed size.
bool Inflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen);
