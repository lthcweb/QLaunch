#include "skin/xml.h"

#include <cwctype>

#include "core/str.h"

namespace {

class Parser {
public:
    explicit Parser(const std::wstring& s) : s_(s) {}

    bool ParseDocument(XmlNode& root) {
        SkipMisc();
        if (!ParseElement(root, 0)) return false;
        return true;  // anything after the root element (comments etc.) is ignored
    }

private:
    bool At(const wchar_t* t) const { return s_.compare(p_, wcslen(t), t) == 0; }
    bool End() const { return p_ >= s_.size(); }
    void SkipSpace() {
        while (!End() && iswspace(s_[p_])) ++p_;
    }
    bool SkipPast(const wchar_t* t) {
        size_t q = s_.find(t, p_);
        if (q == std::wstring::npos) return false;
        p_ = q + wcslen(t);
        return true;
    }
    // skip whitespace, comments, <?...?> and <!...>
    void SkipMisc() {
        for (;;) {
            SkipSpace();
            if (At(L"<!--")) {
                if (!SkipPast(L"-->")) p_ = s_.size();
            } else if (At(L"<?") || (At(L"<!") && !At(L"<![CDATA["))) {
                if (!SkipPast(L">")) p_ = s_.size();
            } else {
                return;
            }
        }
    }

    std::wstring Name() {
        size_t b = p_;
        while (!End() && !iswspace(s_[p_]) && s_[p_] != L'>' && s_[p_] != L'/' && s_[p_] != L'=') ++p_;
        return s_.substr(b, p_ - b);
    }

    static std::wstring Unescape(const std::wstring& v) {
        if (v.find(L'&') == std::wstring::npos) return v;
        static const struct { const wchar_t* from; wchar_t to; } kEntities[] = {
            {L"&lt;", L'<'}, {L"&gt;", L'>'}, {L"&amp;", L'&'}, {L"&quot;", L'"'}, {L"&apos;", L'\''}};
        std::wstring out;
        for (size_t i = 0; i < v.size(); ++i) {
            bool replaced = false;
            if (v[i] == L'&') {
                for (auto& e : kEntities) {
                    size_t n = wcslen(e.from);
                    if (v.compare(i, n, e.from) == 0) {
                        out += e.to;
                        i += n - 1;
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced) out += v[i];
        }
        return out;
    }

    bool ParseElement(XmlNode& node, int depth) {
        if (depth > 64 || End() || s_[p_] != L'<') return false;
        ++p_;
        node.name = Name();
        if (node.name.empty()) return false;

        // attributes
        for (;;) {
            SkipSpace();
            if (End()) return false;
            if (At(L"/>")) {
                p_ += 2;
                return true;
            }
            if (s_[p_] == L'>') {
                ++p_;
                break;
            }
            std::wstring key = Name();
            SkipSpace();
            if (End() || s_[p_] != L'=') return false;
            ++p_;
            SkipSpace();
            if (End() || (s_[p_] != L'"' && s_[p_] != L'\'')) return false;
            wchar_t quote = s_[p_++];
            size_t close = s_.find(quote, p_);
            if (close == std::wstring::npos) return false;
            node.attrs.emplace_back(key, Unescape(s_.substr(p_, close - p_)));
            p_ = close + 1;
        }

        // child elements; text content is skipped
        for (;;) {
            size_t lt = s_.find(L'<', p_);
            if (lt == std::wstring::npos) return false;
            p_ = lt;
            if (At(L"<!--")) {
                if (!SkipPast(L"-->")) return false;
            } else if (At(L"<![CDATA[")) {
                if (!SkipPast(L"]]>")) return false;
            } else if (At(L"</")) {
                return SkipPast(L">");
            } else if (At(L"<?") || At(L"<!")) {
                if (!SkipPast(L">")) return false;
            } else {
                node.children.emplace_back();
                if (!ParseElement(node.children.back(), depth + 1)) return false;
            }
        }
    }

    const std::wstring& s_;
    size_t p_ = 0;
};

}  // namespace

const wchar_t* XmlNode::Attr(const wchar_t* key) const {
    for (auto& a : attrs)
        if (EqualsNoCase(a.first, key)) return a.second.c_str();
    return nullptr;
}

const XmlNode* XmlNode::Child(const wchar_t* key) const {
    for (auto& c : children)
        if (EqualsNoCase(c.name, key)) return &c;
    return nullptr;
}

const XmlNode* XmlNode::Path(const wchar_t* path) const {
    const XmlNode* node = this;
    std::wstring p = path;
    size_t b = 0;
    while (node && b <= p.size()) {
        size_t e = p.find(L'/', b);
        if (e == std::wstring::npos) e = p.size();
        node = node->Child(p.substr(b, e - b).c_str());
        b = e + 1;
    }
    return node;
}

bool ParseXml(const std::wstring& text, XmlNode& root) {
    root = XmlNode{};
    return Parser(text).ParseDocument(root);
}
