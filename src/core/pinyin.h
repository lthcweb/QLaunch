// Pinyin matching for search: "jsb", "jishiben" and "jishib" all find 记事本
#pragma once

#include "core/base.h"

// Where a query was found in a name by its pinyin
enum class PinyinHit { None, Start, Inside };

// Matches a query of lowercase ASCII letters and digits against a name. Each Chinese character in the name
// takes the start (or the whole) of one of its readings, any reading of a polyphone; other characters must
// match the query as they are, ignoring case, and spaces in the name are skipped. The query has to match
// characters that follow each other, starting at any character of the name.
PinyinHit MatchPinyin(const std::wstring& name, const std::wstring& query);
