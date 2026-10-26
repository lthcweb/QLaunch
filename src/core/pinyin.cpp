#include "core/pinyin.h"

#include <algorithm>
#include <cwctype>
#include <iterator>

#include "core/pinyin_table.h"

namespace {

// The character's place in the table (GB2312 level 1), or -1 for a character the table doesn't hold
int TablePlace(wchar_t ch) {
    if (ch < 0x4E00 || ch > 0x9FFF) return -1;
    unsigned char gb[2];
    BOOL usedDefault = FALSE;
    if (WideCharToMultiByte(936, WC_NO_BEST_FIT_CHARS, &ch, 1, reinterpret_cast<char*>(gb), 2, nullptr,
                            &usedDefault) != 2 || usedDefault)
        return -1;
    if (gb[0] < 0xB0 || gb[0] > 0xD7 || gb[1] < 0xA1 || gb[1] > 0xFE) return -1;
    int place = (gb[0] - 0xB0) * 94 + (gb[1] - 0xA1);
    return place < kPinyinChars ? place : -1;
}

// Calls fn(syllable) for every reading of the character at the place
template <class Fn>
void ForEachReading(int place, Fn fn) {
    fn(kSyllables[kFirstReading[place]]);
    auto less = [](const uint16_t (&a)[2], int p) { return a[0] < p; };
    for (auto* r = std::lower_bound(std::begin(kMoreReadings), std::end(kMoreReadings), place, less);
         r != std::end(kMoreReadings) && (*r)[0] == place; ++r)
        fn(kSyllables[(*r)[1]]);
}

}  // namespace

PinyinHit MatchPinyin(const std::wstring& name, const std::wstring& query) {
    size_t m = query.size();
    if (m == 0) return PinyinHit::None;
    for (wchar_t c : query) {
        if (c > 0x7F || !iswalnum(c)) return PinyinHit::None;
    }
    std::vector<int> places(name.size());
    bool chinese = false;
    for (size_t i = 0; i < name.size(); ++i) {
        places[i] = TablePlace(name[i]);
        chinese |= places[i] >= 0;
    }
    if (!chinese) return PinyinHit::None;

    // reach[j]: the first j characters of the query match the name up to the current character
    std::vector<char> reach(m + 1), next(m + 1);
    for (size_t start = 0; start < name.size(); ++start) {
        if (name[start] == L' ') continue;
        std::fill(reach.begin(), reach.end(), 0);
        reach[0] = 1;
        for (size_t i = start; i < name.size(); ++i) {
            std::fill(next.begin(), next.end(), 0);
            bool any = false;
            for (size_t j = 0; j < m; ++j) {
                if (!reach[j]) continue;
                if (name[i] == L' ') {
                    next[j] = 1;
                } else if (places[i] >= 0) {
                    ForEachReading(places[i], [&](const char* syl) {
                        for (size_t k = 0; syl[k] && j + k < m && query[j + k] == (wchar_t)syl[k]; ++k)
                            next[j + k + 1] = 1;
                    });
                } else if ((wchar_t)towlower(name[i]) == query[j]) {
                    next[j + 1] = 1;
                }
            }
            if (next[m]) return start == 0 ? PinyinHit::Start : PinyinHit::Inside;
            for (size_t j = 0; j < m; ++j) any |= next[j] != 0;
            if (!any) break;
            reach.swap(next);
        }
    }
    return PinyinHit::None;
}
