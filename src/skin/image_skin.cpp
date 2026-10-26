// Image skins: reads skin.xml and the images from skins\<name>.zip (or the skins\<name>\ folder).
//
// Key points of skin.xml:
//   sprite   image="a.png" left top width height method="stretch|tile" border="l,t,r,b"
//            coordinates are pixels or "k/n": position = start of the k-th of n parts, size = k parts
//   frame    the four borders; arrangement width/height="ImageSize" means the image's own size
//   buttons  closebutton / lockbutton (pinbutton) / modebutton / searchbutton,
//            states normal/hover/down/downhover, placed by anchors: <left base="TopFrame" side="right"
//            offset="-61"/>
//   tabarea / buttonarea   arrangement offset="l,t,r,b" (relative to their area) margin="l,t,r,b" (padding)
//   tab      one image: columns are designs (or pattern) styles, rows are states (3 or 4 rows, order in Draw)
//   button   the same, 3 rows: normal/hover/down
//   text     old form: <text fontSize color shadow ...><hover .../></text>
//            new form: <text><font .../><decoration><normal/><hover/>...</decoration></text>
// Folder menus (foldermenu/submenu) and the lock mark have no counterpart in this app and are ignored.
// The font face in a skin is ignored (often a Japanese font); the face comes from the settings,
// size/weight/colour/shadow from the skin.
#include <algorithm>
#include <cwctype>

#include "core/path.h"
#include "core/str.h"
#include "skin/image.h"
#include "skin/skin.h"
#include "skin/xml.h"
#include "skin/zip.h"

namespace {

// ---------------------------------------------------------------------------
// Attribute parsing

// "k/n" or pixels
struct Value {
    int num = 0, den = 0;  // den == 0 means pixels

    int Pos(int total) const { return den ? (num - 1) * total / den : num; }
    int Size(int total) const { return den ? num * total / den : num; }
};

Value ParseValue(const wchar_t* s) {
    Value v;
    if (!s) return v;
    wchar_t* end = nullptr;
    v.num = (int)wcstol(s, &end, 10);
    if (end && *end == L'/') v.den = std::max(1, (int)wcstol(end + 1, nullptr, 10));
    return v;
}

// "1, 2, 3, 4" -> list of integers
int ParseInts(const wchar_t* s, int* out, int max) {
    int n = 0;
    while (s && *s && n < max) {
        wchar_t* end = nullptr;
        long v = wcstol(s, &end, 10);
        if (end == s) break;
        out[n++] = (int)v;
        s = end;
        while (*s == L' ' || *s == L',' || *s == L'\t') ++s;
    }
    return n;
}

RECT ParseQuad(const wchar_t* s) {
    int v[4] = {};
    int n = ParseInts(s, v, 4);
    if (n == 1) v[1] = v[2] = v[3] = v[0];  // a single number means the same on all four sides
    return RECT{v[0], v[1], v[2], v[3]};
}

POINT ParsePair(const wchar_t* s) {
    int v[2] = {};
    ParseInts(s, v, 2);
    return POINT{v[0], v[1]};
}

bool ParseColor(const wchar_t* s, COLORREF& out) {
    if (!s || *s != L'#') return false;
    unsigned long v = wcstoul(s + 1, nullptr, 16);
    out = RGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

const wchar_t* AttrOf(const XmlNode* n, const wchar_t* key) { return n ? n->Attr(key) : nullptr; }

// Applies the text attributes of one element to style; both old and new attribute names work
void ApplyTextAttrs(TextStyle& t, const XmlNode& n) {
    COLORREF c;
    if (ParseColor(n.Attr(L"color"), c)) t.color = c;
    if (ParseColor(n.Attr(L"shadowColor"), c)) t.shadowColor = c;
    if (const wchar_t* v = n.Attr(L"shadow")) {
        t.shadow = _wcsicmp(v, L"lower") == 0 ? TextStyle::Lower
                 : _wcsicmp(v, L"upper") == 0 ? TextStyle::Upper
                 : _wcsicmp(v, L"outline") == 0 ? TextStyle::Outline
                 : TextStyle::None;
    }
    if (const wchar_t* v = n.Attr(L"shadowAlpha")) t.shadowAlpha = (uint8_t)std::min(255, std::max(0, ToInt(v)));
    if (const wchar_t* v = n.Attr(L"shadowWidth")) t.shadowWidth = (uint8_t)std::min(8, std::max(1, ToInt(v)));
    const wchar_t* size = n.Attr(L"fontSize");
    if (!size) size = n.Attr(L"size");
    if (size) t.size = (uint8_t)std::min(72, std::max(1, ToInt(size)));
    const wchar_t* style = n.Attr(L"fontStyle");
    if (!style) style = n.Attr(L"style");
    if (style) t.bold = wcsstr(style, L"bold") != nullptr;
}

// text: the <text> element; fontKey: child of <font> in the new form (active/inactive);
// decoKey: child of <decoration> in the new form (normal/hover/active/down);
// hover: in the old form, whether to apply the <hover> under <text>
TextStyle ParseText(const XmlNode* text, const wchar_t* fontKey, const wchar_t* decoKey, bool hover) {
    TextStyle t;
    if (!text) return t;
    ApplyTextAttrs(t, *text);
    if (const XmlNode* font = text->Child(L"font")) {
        ApplyTextAttrs(t, *font);
        if (const XmlNode* f = fontKey ? font->Child(fontKey) : nullptr) ApplyTextAttrs(t, *f);
    }
    if (const XmlNode* deco = text->Child(L"decoration")) {
        ApplyTextAttrs(t, *deco);
        if (const XmlNode* d = decoKey ? deco->Child(decoKey) : nullptr) ApplyTextAttrs(t, *d);
    }
    if (hover)
        if (const XmlNode* h = text->Child(L"hover")) ApplyTextAttrs(t, *h);
    return t;
}

// ---------------------------------------------------------------------------
// Where the skin files come from: a zip or a folder

class SkinFiles {
public:
    bool Open(const std::wstring& name) {
        std::wstring base = AppFile(L"skins\\") + name;
        if (FileExists(base + L".zip")) {
            if (!zip_.Open(base + L".zip")) return false;
            std::string xml = zip_.FindByFileName("skin.xml");  // the zip may have an extra folder level
            if (xml.empty()) return false;
            prefix_ = xml.substr(0, xml.size() - 8);
            isZip_ = true;
            return true;
        }
        dir_ = base + L"\\";
        return FileExists(dir_ + L"skin.xml");
    }

    bool Read(const std::wstring& file, std::vector<uint8_t>& out) const {
        if (isZip_) return zip_.Read(prefix_ + WideToUtf8(file), out);
        return ReadBinaryFile(dir_ + file, out, 32 * 1024 * 1024);
    }

private:
    ZipReader zip_;
    bool isZip_ = false;
    std::string prefix_;
    std::wstring dir_;
};

// ---------------------------------------------------------------------------

struct Sprite {
    int image = -1;  // index into images_; -1 means no image
    Value left, top, width, height;
    RECT border{};
    bool tile = false;

    bool Valid() const { return image >= 0; }
};

// Anchor: <left base="TopFrame" side="right" offset="-61" width="ImageSize"/>
struct Anchor {
    enum Size { Anchored, ImageSize, Fixed };
    Size size = Anchored;
    int fixed = 0;
    int base = 0;   // kBase*
    int side = 0;   // 0 left 1 top 2 right 3 bottom
    int offset = 0;
};

// Elements on the caption bar
enum Elem { kClose, kPin, kMode, kSearch, kCaption, kElemCount };
// What an anchor can refer to: the four borders + the elements above
enum { kTopFrame = 0, kLeftFrame, kRightFrame, kBottomFrame, kElemBase };

struct Element {
    bool present = false;
    Sprite states[4];  // normal, hover, down, downhover
    Anchor anchor[4];  // left, top, right, bottom
};

struct Image {
    std::wstring name;
    int w = 0, h = 0;
    Bitmap bmp;
};

class ImageSkin : public Skin {
public:
    bool Load(const std::wstring& name);

    void SetInput(int dpi, const SkinInput& in) override {
        dpi_ = dpi;
        in_ = in;
    }
    SkinMetrics Metrics() const override;
    void PlaceChrome(int w, int h, RECT& caption, RECT& close, RECT& pin) const override;
    void Draw(Canvas& c, Part part, int state, const RECT& rc) override;
    TextStyle Text(Part part, int state) const override;
    bool Layered() const override { return true; }
    SkinHints Hints() const override { return hints_; }
    void Release() override {
        for (Image& img : images_) img.bmp = Bitmap{};
        decoded_ = false;
    }

private:
    int Px(int v) const { return MulDiv(v, dpi_, 96); }
    int ImageIndex(const wchar_t* file);
    Sprite ParseSprite(const XmlNode* n);
    Sprite SheetCell(const Sprite& sheet, int designs, int rows, int row) const;
    void ParseElement(const XmlNode* n, Element& e);
    void ParseFrame(const XmlNode* frame, const wchar_t* side, Sprite& s, int& thickness, bool vertical);
    bool DecodeImages();
    RECT SpriteRect(const Sprite& s) const;  // area on the source image
    SIZE SpriteSize(const Sprite& s) const;  // size after DPI scaling
    void DrawSprite(Canvas& c, const Sprite& s, const RECT& dst);
    RECT FrameRect(int which, int w, int h) const;
    RECT Resolve(int elem, int w, int h, int depth) const;
    RECT BaseRect(int base, int w, int h, int depth) const;

    std::wstring name_;
    int dpi_ = 96;
    SkinInput in_;
    std::vector<Image> images_;
    bool decoded_ = false;  // whether the image pixels are loaded (freed while the window is hidden)
    COLORREF clipColor_ = RGB(255, 0, 255);
    bool hasClipColor_ = false;

    Sprite background_;
    RECT backgroundOffset_{};
    Sprite frames_[4];  // top, left, right, bottom (same order as kTopFrame...)
    int frameSize_[4] = {};
    Element elems_[kElemCount];
    RECT captionMargin_{};

    Sprite tabArea_, tab_;
    int tabDesigns_ = 1;
    RECT tabOffset_{}, tabMargin_{}, tabTextMargin_{};
    POINT tabPitch_{};
    TextStyle tabText_[4];  // normal, hover, active, active+hover

    Sprite gridArea_, button_;
    int buttonDesigns_ = 1;
    RECT gridOffset_{}, gridMargin_{};
    POINT cellPitch_{}, shiftHover_{}, shiftDown_{};
    TextStyle buttonText_[3];  // normal, hover, down

    TextStyle captionText_;
    RECT sizeArea_{6, 6, 6, 6};
    SkinHints hints_;
};

int ImageSkin::ImageIndex(const wchar_t* file) {
    if (!file || !*file) return -1;
    for (size_t i = 0; i < images_.size(); ++i)
        if (EqualsNoCase(images_[i].name, file)) return (int)i;
    images_.push_back(Image{file});
    return (int)images_.size() - 1;
}

Sprite ImageSkin::ParseSprite(const XmlNode* n) {
    Sprite s;
    if (!n) return s;
    s.image = ImageIndex(n->Attr(L"image"));
    s.left = ParseValue(n->Attr(L"left"));
    s.top = ParseValue(n->Attr(L"top"));
    // no size given: the whole image
    s.width = n->Attr(L"width") ? ParseValue(n->Attr(L"width")) : Value{1, 1};
    s.height = n->Attr(L"height") ? ParseValue(n->Attr(L"height")) : Value{1, 1};
    s.border = ParseQuad(n->Attr(L"border"));
    const wchar_t* method = n->Attr(L"method");
    s.tile = method && _wcsicmp(method, L"tile") == 0;
    return s;
}

// State images of tabs and buttons: columns are designs styles, rows are states; take row `row` of style 0
Sprite ImageSkin::SheetCell(const Sprite& sheet, int designs, int rows, int row) const {
    Sprite s = sheet;
    s.left = Value{1, designs};
    s.width = Value{1, designs};
    s.top = Value{row + 1, rows};
    s.height = Value{1, rows};
    return s;
}

void ImageSkin::ParseElement(const XmlNode* n, Element& e) {
    if (!n) return;
    e.present = true;
    static const wchar_t* kStates[] = {L"normal", L"hover", L"down", L"downhover"};
    for (int i = 0; i < 4; ++i) e.states[i] = ParseSprite(n->Child(kStates[i]));
    if (!e.states[0].Valid() && n->Attr(L"image")) e.states[0] = ParseSprite(n);  // caption background
    for (int i = 1; i < 4; ++i)
        if (!e.states[i].Valid()) e.states[i] = e.states[i == 3 ? 2 : 0];

    static const wchar_t* kSides[] = {L"left", L"top", L"right", L"bottom"};
    static const struct { const wchar_t* name; int base; } kBases[] = {
        {L"TopFrame", kTopFrame},          {L"LeftFrame", kLeftFrame},
        {L"RightFrame", kRightFrame},      {L"BottomFrame", kBottomFrame},
        {L"CloseButton", kElemBase + kClose}, {L"LockButton", kElemBase + kPin},
        {L"PinButton", kElemBase + kPin},  {L"ModeButton", kElemBase + kMode},
        {L"SearchButton", kElemBase + kSearch}, {L"Caption", kElemBase + kCaption},
    };
    const XmlNode* arr = n->Child(L"arrangement");
    for (int i = 0; i < 4; ++i) {
        const XmlNode* a = arr ? arr->Child(kSides[i]) : nullptr;
        if (!a) continue;
        Anchor& an = e.anchor[i];
        const wchar_t* size = a->Attr((i % 2 == 0) ? L"width" : L"height");
        if (size && _wcsicmp(size, L"ImageSize") == 0) an.size = Anchor::ImageSize;
        else if (size && iswdigit(*size)) {
            an.size = Anchor::Fixed;
            an.fixed = ToInt(size);
        }
        if (const wchar_t* base = a->Attr(L"base"))
            for (auto& b : kBases)
                if (_wcsicmp(base, b.name) == 0) an.base = b.base;
        if (const wchar_t* side = a->Attr(L"side"))
            for (int s = 0; s < 4; ++s)
                if (_wcsicmp(side, kSides[s]) == 0) an.side = s;
        an.offset = ToInt(a->Attr(L"offset") ? a->Attr(L"offset") : L"0");
    }
}

void ImageSkin::ParseFrame(const XmlNode* frame, const wchar_t* side, Sprite& s, int& thickness, bool vertical) {
    const XmlNode* n = frame ? frame->Child(side) : nullptr;
    if (!n) return;
    s = ParseSprite(n);
    const wchar_t* size = AttrOf(n->Child(L"arrangement"), vertical ? L"width" : L"height");
    thickness = (size && _wcsicmp(size, L"ImageSize") != 0) ? ToInt(size) : -1;  // -1: computed from the image after decoding
}

bool ImageSkin::Load(const std::wstring& name) {
    SkinFiles files;
    std::vector<uint8_t> raw;
    XmlNode root;
    if (!files.Open(name) || !files.Read(L"skin.xml", raw) ||
        !ParseXml(DecodeText(raw.data(), raw.size()), root) || !EqualsNoCase(root.name, L"skin"))
        return false;
    name_ = name;

    if (const XmlNode* g = root.Child(L"general")) {
        if (const wchar_t* tp = g->Attr(L"tabPosition")) {
            hints_.hasTabSide = true;
            hints_.tabSide = _wcsicmp(tp, L"top") == 0 ? TabSide::Top
                           : _wcsicmp(tp, L"right") == 0 ? TabSide::Right
                           : _wcsicmp(tp, L"bottom") == 0 ? TabSide::Bottom
                           : TabSide::Left;
        }
        hasClipColor_ = ParseColor(g->Attr(L"clippingColor"), clipColor_);
        if (g->Attr(L"sizeArea")) sizeArea_ = ParseQuad(g->Attr(L"sizeArea"));
    }

    if (const XmlNode* bg = root.Child(L"background")) {
        background_ = ParseSprite(bg);
        backgroundOffset_ = ParseQuad(AttrOf(bg->Child(L"arrangement"), L"offset"));
    }
    const XmlNode* frame = root.Child(L"frame");
    ParseFrame(frame, L"top", frames_[kTopFrame], frameSize_[kTopFrame], false);
    ParseFrame(frame, L"left", frames_[kLeftFrame], frameSize_[kLeftFrame], true);
    ParseFrame(frame, L"right", frames_[kRightFrame], frameSize_[kRightFrame], true);
    ParseFrame(frame, L"bottom", frames_[kBottomFrame], frameSize_[kBottomFrame], false);

    ParseElement(root.Child(L"closebutton"), elems_[kClose]);
    ParseElement(root.Child(L"lockbutton"), elems_[kPin]);
    if (!elems_[kPin].present) ParseElement(root.Child(L"pinbutton"), elems_[kPin]);
    ParseElement(root.Child(L"modebutton"), elems_[kMode]);
    ParseElement(root.Child(L"searchbutton"), elems_[kSearch]);
    const XmlNode* caption = root.Child(L"caption");
    ParseElement(caption, elems_[kCaption]);
    captionText_ = ParseText(caption ? caption->Child(L"text") : nullptr, nullptr, nullptr, false);

    if (const XmlNode* ta = root.Child(L"tabarea")) {
        tabArea_ = ParseSprite(ta);
        const XmlNode* arr = ta->Child(L"arrangement");
        tabOffset_ = ParseQuad(AttrOf(arr, L"offset"));
        tabMargin_ = ParseQuad(AttrOf(arr, L"margin"));
        if (const XmlNode* tab = ta->Child(L"tab")) {
            tab_ = ParseSprite(tab);
            tab_.left = tab_.top = Value{};
            const wchar_t* designs = tab->Attr(L"designs") ? tab->Attr(L"designs") : tab->Attr(L"pattern");
            tabDesigns_ = std::max(1, ToInt(designs ? designs : L"1"));
            tabPitch_ = ParsePair(AttrOf(tab->Child(L"arrangement"), L"pitch"));
            // old form: text under <active>/<inactive>; new form: directly under <tab>
            const XmlNode* own = tab->Child(L"text");
            const XmlNode* inactive = tab->Path(L"inactive/text");
            const XmlNode* active = tab->Path(L"active/text");
            if (!inactive) inactive = own;
            if (!active) active = own;
            tabText_[0] = ParseText(inactive, L"inactive", L"normal", false);
            tabText_[1] = ParseText(inactive, L"inactive", L"hover", true);
            tabText_[2] = ParseText(active, L"active", L"active", false);
            tabText_[3] = ParseText(active, L"active", L"active", true);
        }
    }

    if (const XmlNode* ba = root.Child(L"buttonarea")) {
        gridArea_ = ParseSprite(ba);
        const XmlNode* arr = ba->Child(L"arrangement");
        gridOffset_ = ParseQuad(AttrOf(arr, L"offset"));
        gridMargin_ = ParseQuad(AttrOf(arr, L"margin"));
        if (const XmlNode* btn = ba->Child(L"button")) {
            button_ = ParseSprite(btn);
            button_.left = button_.top = Value{};
            const wchar_t* designs = btn->Attr(L"designs") ? btn->Attr(L"designs") : btn->Attr(L"pattern");
            buttonDesigns_ = std::max(1, ToInt(designs ? designs : L"1"));
            const XmlNode* barr = btn->Child(L"arrangement");
            cellPitch_ = ParsePair(AttrOf(barr, L"pitch"));
            shiftHover_ = ParsePair(AttrOf(barr, L"shiftHover"));
            shiftDown_ = ParsePair(AttrOf(barr, L"shiftDown"));
            const XmlNode* text = btn->Child(L"text");
            buttonText_[0] = ParseText(text, nullptr, L"normal", false);
            buttonText_[1] = ParseText(text, nullptr, L"hover", true);
            buttonText_[2] = ParseText(text, nullptr, L"down", false);
        }
    }

    // recommended settings: newer skins have mode1/mode2 per display mode; use mode1
    if (const XmlNode* rec = root.Child(L"recommended")) {
        if (const XmlNode* m1 = rec->Child(L"mode1")) rec = m1;
        if (const XmlNode* t = rec->Child(L"tab")) {
            POINT size = ParsePair(t->Attr(L"size"));
            hints_.tabW = size.x;
            hints_.tabH = size.y;
            tabTextMargin_ = ParseQuad(t->Attr(L"textMargin"));
        }
        if (const XmlNode* b = rec->Child(L"button")) {
            POINT size = ParsePair(b->Attr(L"size"));
            hints_.cellW = size.x;
            hints_.cellH = size.y;
            hints_.iconSize = ToInt(b->Attr(L"iconSize") ? b->Attr(L"iconSize") : L"0");
        }
        if (const XmlNode* c = rec->Child(L"caption")) captionMargin_ = ParseQuad(c->Attr(L"textMargin"));
    }

    // decode once to get the image sizes (the layout needs them); later the pixels are freed when hidden and
    // decoded again when shown
    if (!DecodeImages()) return false;
    for (int i = 0; i < 4; ++i)
        if (frameSize_[i] < 0) {
            RECT r = SpriteRect(frames_[i]);
            frameSize_[i] = (i == kLeftFrame || i == kRightFrame) ? r.right - r.left : r.bottom - r.top;
        }
    return true;
}

bool ImageSkin::DecodeImages() {
    SkinFiles files;
    if (!files.Open(name_)) return false;
    std::vector<uint8_t> raw;
    for (Image& img : images_) {
        if (!img.bmp.Empty()) continue;
        if (!files.Read(img.name, raw) || !DecodeImage(raw, img.bmp)) continue;  // a missing image just leaves that piece undrawn
        if (hasClipColor_) ApplyClipColor(img.bmp, clipColor_);
        img.w = img.bmp.w;
        img.h = img.bmp.h;
    }
    decoded_ = true;
    return true;
}

RECT ImageSkin::SpriteRect(const Sprite& s) const {
    if (!s.Valid()) return RECT{};
    const Image& img = images_[s.image];
    int x = s.left.Pos(img.w), y = s.top.Pos(img.h);
    return RECT{x, y, x + s.width.Size(img.w), y + s.height.Size(img.h)};
}

SIZE ImageSkin::SpriteSize(const Sprite& s) const {
    RECT r = SpriteRect(s);
    return SIZE{Px(r.right - r.left), Px(r.bottom - r.top)};
}

void ImageSkin::DrawSprite(Canvas& c, const Sprite& s, const RECT& dst) {
    if (!s.Valid()) return;
    if (!decoded_) DecodeImages();
    c.DrawImage(images_[s.image].bmp, SpriteRect(s), dst, s.border, dpi_, s.tile);
}

SkinMetrics ImageSkin::Metrics() const {
    auto quad = [this](const RECT& r) { return RECT{Px(r.left), Px(r.top), Px(r.right), Px(r.bottom)}; };
    auto pair = [this](const POINT& p) { return POINT{Px(p.x), Px(p.y)}; };
    SkinMetrics m;
    m.frame = RECT{Px(frameSize_[kLeftFrame]), Px(frameSize_[kTopFrame]), Px(frameSize_[kRightFrame]),
                   Px(frameSize_[kBottomFrame])};
    m.tabOffset = quad(tabOffset_);
    m.tabMargin = quad(tabMargin_);
    m.gridOffset = quad(gridOffset_);
    m.gridMargin = quad(gridMargin_);
    m.tabPitch = pair(tabPitch_);
    m.cellPitch = pair(cellPitch_);
    m.shiftHover = pair(shiftHover_);
    m.shiftDown = pair(shiftDown_);
    m.tabTextMargin = quad(tabTextMargin_);
    m.sizeArea = quad(sizeArea_);
    return m;
}

RECT ImageSkin::FrameRect(int which, int w, int h) const {
    int t = Px(frameSize_[kTopFrame]), b = Px(frameSize_[kBottomFrame]);
    switch (which) {
        case kTopFrame: return RECT{0, 0, w, t};
        case kBottomFrame: return RECT{0, h - b, w, h};
        case kLeftFrame: return RECT{0, t, Px(frameSize_[kLeftFrame]), h - b};
        default: return RECT{w - Px(frameSize_[kRightFrame]), t, w, h - b};
    }
}

RECT ImageSkin::BaseRect(int base, int w, int h, int depth) const {
    if (base < kElemBase) return FrameRect(base, w, h);
    return Resolve(base - kElemBase, w, h, depth + 1);
}

// Computes an element's position from its anchors. A button the skin lacks falls back to the button on its left
// (search -> mode -> pin -> close); if none exist, an empty rectangle at the right end of the top border.
RECT ImageSkin::Resolve(int elem, int w, int h, int depth) const {
    const Element& e = elems_[elem];
    if (depth > 8) return RECT{};
    if (!e.present) {
        if (elem > kClose && elem <= kSearch) return Resolve(elem - 1, w, h, depth + 1);
        RECT top = FrameRect(kTopFrame, w, h);
        if (elem == kCaption)  // no caption position given: inside the top border, left of the buttons
            return RECT{top.left + Px(8), top.top, Resolve(kSearch, w, h, depth + 1).left - Px(2), top.bottom};
        return RECT{top.right, top.top, top.right, top.bottom};
    }

    SIZE img = SpriteSize(e.states[0]);
    int v[4];
    bool known[4] = {};
    for (int i = 0; i < 4; ++i) {
        const Anchor& a = e.anchor[i];
        if (a.size != Anchor::Anchored) continue;
        RECT b = BaseRect(a.base, w, h, depth);
        const LONG sides[4] = {b.left, b.top, b.right, b.bottom};
        v[i] = sides[a.side] + Px(a.offset);
        known[i] = true;
    }
    // derive the other side from the size
    for (int i = 0; i < 4; ++i) {
        if (known[i]) continue;
        const Anchor& a = e.anchor[i];
        int size = a.size == Anchor::Fixed ? Px(a.fixed) : (i % 2 == 0 ? img.cx : img.cy);
        int other = (i + 2) % 4;
        int from = known[other] ? v[other] : 0;
        v[i] = i < 2 ? from - size : from + size;
    }
    return RECT{v[0], v[1], v[2], v[3]};
}

void ImageSkin::PlaceChrome(int w, int h, RECT& caption, RECT& close, RECT& pin) const {
    close = Resolve(kClose, w, h, 0);
    pin = elems_[kPin].present ? Resolve(kPin, w, h, 0) : RECT{};
    if (!elems_[kClose].present) close = RECT{};
    caption = Resolve(kCaption, w, h, 0);
    caption.left += Px(captionMargin_.left);
    caption.top += Px(captionMargin_.top);
    caption.right -= Px(captionMargin_.right);
    caption.bottom -= Px(captionMargin_.bottom);
}

void ImageSkin::Draw(Canvas& c, Part part, int state, const RECT& rc) {
    bool hot = state & (kHot | kDropTarget), pressed = state & kPressed, sel = state & kSelected;
    switch (part) {
        case Part::Window: {
            int w = rc.right - rc.left, h = rc.bottom - rc.top;
            RECT inner{FrameRect(kLeftFrame, w, h).right, FrameRect(kTopFrame, w, h).bottom,
                       FrameRect(kRightFrame, w, h).left, FrameRect(kBottomFrame, w, h).top};
            RECT bg{inner.left + Px(backgroundOffset_.left), inner.top + Px(backgroundOffset_.top),
                    inner.right + Px(backgroundOffset_.right), inner.bottom + Px(backgroundOffset_.bottom)};
            DrawSprite(c, background_, bg);
            for (int i = 0; i < 4; ++i) DrawSprite(c, frames_[i], FrameRect(i, w, h));
            break;
        }
        case Part::Caption: DrawSprite(c, elems_[kCaption].states[0], rc); break;
        case Part::CloseButton:
            DrawSprite(c, elems_[kClose].states[pressed ? 2 : hot ? 1 : 0], rc);
            break;
        case Part::PinButton: {
            // show the "down" state while pinned
            int s = (sel || pressed) ? (hot ? 3 : 2) : (hot ? 1 : 0);
            DrawSprite(c, elems_[kPin].states[s], rc);
            break;
        }
        case Part::TabArea: DrawSprite(c, tabArea_, rc); break;
        case Part::Tab: {
            // 3 rows: normal, hover, active;
            // 4 rows: active, active+hover, normal, normal+hover (order worked out from existing skin images)
            int row = sel ? 2 : hot ? 1 : 0;
            if (tab_.Valid()) {
                int ih = images_[tab_.image].h;
                if (ih % 4 == 0 && ih % 3 != 0) {
                    DrawSprite(c, SheetCell(tab_, tabDesigns_, 4, (sel ? 0 : 2) + (hot ? 1 : 0)), rc);
                    break;
                }
            }
            DrawSprite(c, SheetCell(tab_, tabDesigns_, 3, row), rc);
            break;
        }
        case Part::Grid: DrawSprite(c, gridArea_, rc); break;
        case Part::Cell: DrawSprite(c, SheetCell(button_, buttonDesigns_, 3, pressed ? 2 : hot ? 1 : 0), rc); break;
    }
}

TextStyle ImageSkin::Text(Part part, int state) const {
    bool hot = state & kHot, sel = state & kSelected;
    TextStyle t;
    switch (part) {
        case Part::Tab: t = tabText_[(sel ? 2 : 0) + (hot ? 1 : 0)]; break;
        case Part::Cell: t = buttonText_[(state & kPressed) ? 2 : hot ? 1 : 0]; break;
        default: t = captionText_; break;
    }
    t.shadowWidth = (uint8_t)std::max(1, Px(t.shadowWidth));
    return t;
}

}  // namespace

std::unique_ptr<Skin> LoadImageSkin(const std::wstring& name) {
    auto skin = std::make_unique<ImageSkin>();
    if (!skin->Load(name)) return nullptr;
    return skin;
}

std::vector<std::wstring> ListImageSkins() {
    std::vector<std::wstring> out;
    std::wstring dir = AppFile(L"skins\\");
    WIN32_FIND_DATAW fd;
    HANDLE find = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (find == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (name == L"." || name == L".." || !FileExists(dir + name + L"\\skin.xml")) continue;
        } else {
            size_t dot = name.find_last_of(L'.');
            if (dot == std::wstring::npos || !EqualsNoCase(name.substr(dot), L".zip")) continue;
            name.resize(dot);
        }
        bool dup = false;
        for (auto& n : out) dup = dup || EqualsNoCase(n, name.c_str());
        if (!dup) out.push_back(name);
    } while (FindNextFileW(find, &fd));
    FindClose(find);
    std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.c_str(), b.c_str()) < 0;
    });
    return out;
}
