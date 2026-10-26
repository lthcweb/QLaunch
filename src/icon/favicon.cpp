#include "icon/favicon.h"

#include <wininet.h>

#include <algorithm>
#include <cstdlib>

#include "core/handle.h"
#include "core/str.h"

namespace {

constexpr DWORD kTimeoutMs = 5000;
constexpr size_t kMaxPage = 512 * 1024;  // the <link> tags are in <head>; the rest of the page isn't needed
constexpr size_t kMaxImage = 1024 * 1024;
constexpr int kMaxTries = 3;              // icon addresses tried before /favicon.ico

struct NetTraits {
    static HINTERNET Invalid() { return nullptr; }
    static bool Valid(HINTERNET h) { return h != nullptr; }
    static void Close(HINTERNET h) { InternetCloseHandle(h); }
};
using NetHandle = UniqueHandle<HINTERNET, NetTraits>;

// GETs url (following redirects); finalUrl, when given, receives the address it ended up at
bool Fetch(HINTERNET session, const std::wstring& url, size_t maxSize, std::vector<uint8_t>& out,
           std::wstring* finalUrl = nullptr) {
    out.clear();
    NetHandle req(InternetOpenUrlW(session, url.c_str(), nullptr, 0,
                                   INTERNET_FLAG_NO_UI | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_AUTH, 0));
    if (!req) return false;
    DWORD status = 0, len = sizeof(status);
    if (!HttpQueryInfoW(req.get(), HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr) ||
        status != 200)
        return false;
    BYTE buf[16 * 1024];
    DWORD got = 0;
    while (out.size() < maxSize && InternetReadFile(req.get(), buf, sizeof(buf), &got) && got > 0)
        out.insert(out.end(), buf, buf + got);
    if (finalUrl) {
        wchar_t where[INTERNET_MAX_URL_LENGTH];
        len = sizeof(where);
        *finalUrl = InternetQueryOptionW(req.get(), INTERNET_OPTION_URL, where, &len) ? where : url;
    }
    return !out.empty();
}

// Value of attribute `name` in a tag (searched in the lower-case copy, returned from the original)
std::string Attr(const std::string& tag, const std::string& lower, const char* name) {
    std::string key = std::string(name) + "=";
    for (size_t at = lower.find(key); at != std::string::npos; at = lower.find(key, at + 1)) {
        if (at == 0 || !isspace((unsigned char)lower[at - 1])) continue;  // "data-rel=" is not "rel="
        size_t start = at + key.size();
        if (start >= tag.size()) break;
        char quote = tag[start];
        if (quote == '"' || quote == '\'') {
            size_t end = tag.find(quote, start + 1);
            return tag.substr(start + 1, end == std::string::npos ? std::string::npos : end - start - 1);
        }
        return tag.substr(start, tag.find_first_of(" \t\r\n>", start) - start);
    }
    return {};
}

struct Candidate {
    std::string href;
    int size;  // from sizes="32x32"; 0 when not given
};

// <link rel="icon" | "shortcut icon" | "apple-touch-icon" href=... sizes=...> in the page's <head>.
// SVG icons are skipped: WIC can't draw them.
std::vector<Candidate> FindIcons(const std::vector<uint8_t>& page) {
    std::string html(page.begin(), page.end());
    std::string lower = html;
    for (char& c : lower) c = (char)tolower((unsigned char)c);
    size_t headEnd = lower.find("</head>");
    std::vector<Candidate> out;
    for (size_t at = lower.find("<link"); at < headEnd; at = lower.find("<link", at + 5)) {
        size_t end = lower.find('>', at);
        if (end == std::string::npos) break;
        std::string tag = html.substr(at, end - at), low = lower.substr(at, end - at);
        std::string rel = Attr(low, low, "rel"), href = Attr(tag, low, "href");
        if (rel.find("icon") == std::string::npos || rel.find("mask-icon") != std::string::npos || href.empty())
            continue;
        std::string hrefLow = Attr(low, low, "href");
        if (hrefLow.find(".svg") != std::string::npos || hrefLow.compare(0, 5, "data:") == 0 ||
            Attr(low, low, "type").find("svg") != std::string::npos)
            continue;
        for (size_t amp; (amp = href.find("&amp;")) != std::string::npos;) href.replace(amp, 5, "&");
        int size = atoi(Attr(low, low, "sizes").c_str());
        if (size == 0 && rel.find("apple-touch-icon") != std::string::npos) size = 180;  // its usual size
        out.push_back(Candidate{href, size});
    }
    return out;
}

// Best first: the smallest one at least as big as wanted, then the bigger the better. Unknown sizes are
// usually 16 or 32.
void Rank(std::vector<Candidate>& list, int wanted) {
    auto score = [wanted](const Candidate& c) {
        int s = c.size ? c.size : 32;
        return s >= wanted ? 100000 - s : s;
    };
    std::stable_sort(list.begin(), list.end(),
                     [&](const Candidate& a, const Candidate& b) { return score(a) > score(b); });
}

std::wstring Combine(const std::wstring& base, const std::wstring& relative) {
    wchar_t buf[INTERNET_MAX_URL_LENGTH];
    DWORD len = _countof(buf);
    return InternetCombineUrlW(base.c_str(), relative.c_str(), buf, &len, ICU_NO_ENCODE) ? buf : L"";
}

bool FetchImage(HINTERNET session, const std::wstring& url, Bitmap& out) {
    std::vector<uint8_t> data;
    return !url.empty() && Fetch(session, url, kMaxImage, data) && DecodeImage(data, out) && out.w >= 16;
}

}  // namespace

bool DownloadFavicon(const std::wstring& url, int size, Bitmap& out) {
    out = Bitmap{};
    if (!StartsWithNoCase(url, L"http://") && !StartsWithNoCase(url, L"https://")) return false;
    // a browser's user agent: some sites refuse unknown clients
    NetHandle session(InternetOpenW(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                                    L"(KHTML, like Gecko) Chrome/120.0 Safari/537.36",
                                    INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0));
    if (!session) return false;
    DWORD timeout = kTimeoutMs;
    for (DWORD opt : {INTERNET_OPTION_CONNECT_TIMEOUT, INTERNET_OPTION_SEND_TIMEOUT, INTERNET_OPTION_RECEIVE_TIMEOUT})
        InternetSetOptionW(session.get(), opt, &timeout, sizeof(timeout));

    std::vector<uint8_t> page;
    std::wstring base = url;
    if (Fetch(session.get(), url, kMaxPage, page, &base)) {
        std::vector<Candidate> icons = FindIcons(page);
        Rank(icons, size);
        for (int i = 0; i < (int)icons.size() && i < kMaxTries; ++i)
            if (FetchImage(session.get(), Combine(base, Utf8ToWide(icons[i].href.c_str())), out)) return true;
    }
    return FetchImage(session.get(), Combine(base, L"/favicon.ico"), out);
}
