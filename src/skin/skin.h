// Look interface. The UI only tells the skin "draw this part in this state into this rectangle"; colours or
// images don't matter to it.
// Two implementations:
//   color_skin.cpp  flat colours (Dark / Light), built in
//   image_skin.cpp     image skins (skins\<name>.zip or skins\<name>\skin.xml)
#pragma once

#include <memory>

#include "core/base.h"
#include "model/settings.h"
#include "skin/canvas.h"

enum class Part { Window, Caption, CloseButton, PinButton, TabArea, Tab, Grid, Cell };

// State bits, can be combined, e.g. kSelected | kHot
enum : int { kNormal = 0, kHot = 1, kPressed = 2, kSelected = 4, kDropTarget = 8 };

// Layout sizes decided by the skin, in pixels already scaled for the current DPI.
// Window = frame around it (the top frame holds the caption) + inside. The inside holds the tab bar and the
// button area:
//   panel   = area + offset (offset left/top move the top-left corner, right/bottom the bottom-right; may reach
//   into the frame)
//   content = panel - margin
struct SkinMetrics {
    RECT frame{};
    RECT tabOffset{}, tabMargin{};
    RECT gridOffset{}, gridMargin{};
    POINT tabPitch{}, cellPitch{};    // gap between tabs, between cells
    POINT shiftHover{}, shiftDown{};  // shift of a button's content when hovered, pressed
    RECT tabTextMargin{};             // distance from the tab text to the tab edge
    RECT sizeArea{};                  // width of the resize border on each side
};

// Settings the skin needs to compute sizes (already DPI scaled)
struct SkinInput {
    TabSide side = TabSide::Left;
    int caption = 0, pad = 0, gap = 0;
};

// Settings a skin recommends, applied when switching to it. 0 means no recommendation.
struct SkinHints {
    bool hasTabSide = false;
    TabSide tabSide = TabSide::Left;
    int tabW = 0, tabH = 0, cellW = 0, cellH = 0, iconSize = 0;
};

class Skin {
public:
    virtual ~Skin() = default;

    // called when the DPI or settings change; later sizes are computed from it
    virtual void SetInput(int dpi, const SkinInput& in) = 0;
    virtual SkinMetrics Metrics() const = 0;
    // positions of the caption text, close button and pin button for a w x h window
    virtual void PlaceChrome(int w, int h, RECT& caption, RECT& close, RECT& pin) const = 0;
    virtual void Draw(Canvas& c, Part part, int state, const RECT& rc) = 0;
    virtual TextStyle Text(Part part, int state) const = 0;
    virtual bool Layered() const { return false; }  // needs a per-pixel-alpha (layered) window
    virtual SkinHints Hints() const { return {}; }
    virtual void Release() {}  // frees images etc. while the window is hidden; they load again on the next draw
};

std::unique_ptr<Skin> CreateSkin(const std::wstring& name);
std::vector<std::wstring> ListSkins();

// image_skin.cpp: returns null when loading fails
std::unique_ptr<Skin> LoadImageSkin(const std::wstring& name);
std::vector<std::wstring> ListImageSkins();
