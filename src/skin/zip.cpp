#include "skin/zip.h"

#include "core/path.h"

namespace {

uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

std::string Normalize(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    }
    return out;
}

}  // namespace

bool ZipReader::Open(const std::wstring& path) {
    data_.clear();
    entries_.clear();
    if (!ReadBinaryFile(path, data_, 32 * 1024 * 1024) || data_.size() < 22) return false;

    // search backwards from the end for the "end of central directory" record (up to 64 KB of comment may
    // follow)
    const uint8_t* d = data_.data();
    size_t n = data_.size();
    size_t eocd = 0;
    for (size_t i = n - 22 + 1; i-- > 0 && n - i <= 22 + 65535;) {
        if (U32(d + i) == 0x06054B50) {
            eocd = i;
            break;
        }
    }
    if (U32(d + eocd) != 0x06054B50) return false;

    uint16_t count = U16(d + eocd + 10);
    size_t pos = U32(d + eocd + 16);
    for (uint16_t i = 0; i < count; ++i) {
        if (pos + 46 > n || U32(d + pos) != 0x02014B50) return false;
        uint16_t nameLen = U16(d + pos + 28), extraLen = U16(d + pos + 30), commentLen = U16(d + pos + 32);
        if (pos + 46 + nameLen > n) return false;
        Entry e;
        e.method = U16(d + pos + 10);
        e.packed = U32(d + pos + 20);
        e.size = U32(d + pos + 24);
        e.offset = U32(d + pos + 42);
        e.name = Normalize(std::string(reinterpret_cast<const char*>(d + pos + 46), nameLen));
        entries_.push_back(std::move(e));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

bool ZipReader::Read(const std::string& name, std::vector<uint8_t>& out) const {
    std::string key = Normalize(name);
    for (const Entry& e : entries_) {
        if (e.name != key) continue;
        const uint8_t* d = data_.data();
        if ((size_t)e.offset + 30 > data_.size() || U32(d + e.offset) != 0x04034B50) return false;
        size_t start = e.offset + 30 + U16(d + e.offset + 26) + U16(d + e.offset + 28);
        if (start + e.packed > data_.size() || e.size > 64 * 1024 * 1024) return false;
        out.resize(e.size);
        if (e.method == 0) {
            if (e.packed != e.size) return false;
            memcpy(out.data(), d + start, e.size);
            return true;
        }
        return e.method == 8 && Inflate(d + start, e.packed, out.data(), e.size);
    }
    return false;
}

std::string ZipReader::FindByFileName(const std::string& name) const {
    std::string key = Normalize(name);
    for (const Entry& e : entries_) {
        size_t slash = e.name.find_last_of('/');
        if ((slash == std::string::npos ? e.name : e.name.substr(slash + 1)) == key) return e.name;
    }
    return {};
}
