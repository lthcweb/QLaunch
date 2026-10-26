// Minimal XML parser: elements and attributes only; text, comments and declarations are ignored. Enough for
// skin.xml.
#pragma once

#include "core/base.h"

struct XmlNode {
    std::wstring name;
    std::vector<std::pair<std::wstring, std::wstring>> attrs;
    std::vector<XmlNode> children;

    const wchar_t* Attr(const wchar_t* key) const;  // nullptr when missing
    const XmlNode* Child(const wchar_t* key) const;  // first child with that name
    // finds a descendant by path, e.g. L"tabarea/tab/text"
    const XmlNode* Path(const wchar_t* path) const;
};

// Returns false on a parse error. root is the root element.
bool ParseXml(const std::wstring& text, XmlNode& root);
