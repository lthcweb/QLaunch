"""Generates the bundled skins (skin.xml + PNG images) into dist/skins/.

Requires Python 3.8+ and Pillow (pip install pillow).

Usage:
    python tools/make_skin.py
        Regenerate every skin listed in THEMES below.

    python tools/make_skin.py NAME COLORS
        Generate one new skin into dist/skins/NAME/.

COLORS is either a Realtime Colors link (https://www.realtimecolors.com) or just
the part after "colors=" in such a link: five hex colours in the order
text-background-primary-secondary-accent, for example

    python tools/make_skin.py Lilac "ecf0e8-060705-c1cbb1-464365-a37ca8"
    python tools/make_skin.py Lilac "https://www.realtimecolors.com/?colors=ecf0e8-060705-c1cbb1-464365-a37ca8"

To keep a new skin, add it to THEMES so that a full regeneration includes it.
To ship it as a single file, zip the contents of its folder as NAME.zip and put
that in the skins folder next to the exe; the app reads folders and zips alike.

All skins share one shape: a translucent rounded window, gradient buttons and an
inner button frame with notched corners. Images are drawn at 96 DPI and the app
scales them per monitor; the 9-slice borders are written into skin.xml.
"""
import re
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw

SKINS = Path(__file__).resolve().parent.parent / "dist" / "skins"
SS = 4  # draw at 4x and scale down for antialiased edges

RGB = tuple  # (r, g, b)


@dataclass
class Palette:
    bg_top: RGB          # window background gradient
    bg_bottom: RGB
    edge: RGB            # window outline
    highlight: RGB       # highlight line along the top of the window
    btn_top: RGB         # button gradient
    btn_bottom: RGB
    btn_edge: RGB        # button outline
    btn_inner: RGB       # inner button frame (with notched corners)
    hot_top: RGB         # hovered
    hot_bottom: RGB
    hot_edge: RGB
    hot_inner: RGB
    down_top: RGB        # pressed
    down_bottom: RGB
    down_inner: RGB
    tab_tint: RGB        # faint fill of unselected tabs
    tab_hot_edge: RGB    # outline of a hovered tab
    act_top: RGB         # selected tab
    act_bottom: RGB
    marker: RGB          # bar left of the selected tab, pin when pinned
    glyph: RGB           # caption button glyphs
    glyph_hot: RGB
    text: RGB            # button text, selected tab text, caption
    text_dim: RGB        # unselected tab text
    text_hot: RGB        # hovered text
    shadow: RGB          # text shadow


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


# Teal blue, taken from a game menu screenshot
VOYAGE = Palette(
    bg_top=(66, 108, 132), bg_bottom=(50, 92, 112),
    edge=(120, 175, 186), highlight=(170, 215, 222),
    btn_top=(69, 126, 139), btn_bottom=(52, 100, 115),
    btn_edge=(96, 152, 162), btn_inner=(84, 142, 152),
    hot_top=(84, 146, 158), hot_bottom=(62, 118, 133),
    hot_edge=(150, 205, 210), hot_inner=(110, 170, 178),
    down_top=(48, 94, 108), down_bottom=(56, 106, 120), down_inner=(70, 124, 136),
    tab_tint=(255, 255, 255), tab_hot_edge=(120, 180, 188),
    act_top=(92, 156, 168), act_bottom=(68, 128, 142),
    marker=(236, 214, 150),
    glyph=(205, 232, 232), glyph_hot=(255, 255, 255),
    text=(225, 245, 243), text_dim=(196, 226, 226), text_hot=(245, 230, 168),
    shadow=(10, 42, 53),
)


def parse_colors(spec):
    """A Realtime Colors link or "text-background-primary-secondary-accent" -> 5 RGB tuples"""
    m = re.search(r"colors=([0-9a-fA-F-]+)", spec)
    hexes = (m.group(1) if m else spec).strip().lstrip("#").split("-")
    if len(hexes) < 5 or not all(re.fullmatch(r"[0-9a-fA-F]{6}", h) for h in hexes[:5]):
        raise SystemExit(f"Cannot read colours: {spec} (need 5 six-digit hex colours separated by -)")
    return [tuple(int(h[i:i + 2], 16) for i in (0, 2, 4)) for h in hexes[:5]]


def luminance(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def from_colors(spec):
    """Expands the 5 site colours into a full palette: buttons use the secondary colour,
    hover moves towards the primary, the selected tab and markers use the accent."""
    text, bg, primary, secondary, accent = parse_colors(spec)
    if luminance(bg) > 128:
        # light background: buttons light at the top, darker at the bottom, darker still when pressed
        btn = (lerp(secondary, bg, 0.55), secondary)
        down = (lerp(secondary, text, 0.12), lerp(secondary, text, 0.05))
        window = (bg, lerp(bg, secondary, 0.6))
    else:
        btn = (lerp(secondary, text, 0.08), lerp(secondary, bg, 0.2))
        down = (lerp(secondary, bg, 0.3), lerp(secondary, bg, 0.15))
        window = (lerp(bg, secondary, 0.18), lerp(bg, secondary, 0.06))
    return Palette(
        bg_top=window[0], bg_bottom=window[1],
        edge=lerp(bg, primary, 0.35), highlight=lerp(bg, text, 0.3),
        btn_top=btn[0], btn_bottom=btn[1],
        btn_edge=lerp(secondary, primary, 0.4), btn_inner=lerp(secondary, primary, 0.22),
        hot_top=lerp(btn[0], primary, 0.2), hot_bottom=lerp(btn[1], primary, 0.07),
        hot_edge=primary, hot_inner=lerp(secondary, primary, 0.45),
        down_top=down[0], down_bottom=down[1],
        down_inner=lerp(secondary, primary, 0.15),
        tab_tint=text, tab_hot_edge=lerp(secondary, primary, 0.5),
        act_top=lerp(secondary, accent, 0.45), act_bottom=lerp(secondary, accent, 0.28),
        marker=lerp(accent, text, 0.45),
        glyph=lerp(text, bg, 0.15), glyph_hot=text,
        text=text, text_dim=lerp(text, bg, 0.2), text_hot=lerp(accent, text, 0.6),
        shadow=bg,
    )


# Colours in Realtime Colors order: text-background-primary-secondary-accent
THEMES = {
    "Voyage": VOYAGE,                                         # teal blue, after a game menu
    "Lilac": from_colors("ecf0e8-060705-c1cbb1-464365-a37ca8"),     # lilac on black
    "Graphite": from_colors("ecf0e8-10110e-c1cbb1-1b1b1f-a37ca8"),  # graphite grey
    "Stone": from_colors("ecebe7-262624-b5b0a6-3a3936-8fa3a0"),     # neutral: warm grey
    "Celadon": from_colors("2f3a36-eef2ee-7fa396-dce6e0-4e7a6c"),   # understated: celadon, light
    "Latte": from_colors("4a3424-f5ece1-c8946a-e9d9c6-d9733b"),     # warm: latte, light
    "Sakura": from_colors("5a3a48-fff0f4-f4a7c0-fbdde6-e8638e"),    # sweet: cherry blossom pink, light
    "Glacier": from_colors("e3eef5-0e1620-7fb8d9-1c2a38-4fd1ff"),   # cool: glacier blue
    "Blaze": from_colors("fff1ec-2a0e0e-ff7a59-5a1c1c-ffc53d"),     # fiery: flame red
    "Forest": from_colors("e9f0e4-16211a-9cc59a-26382c-d8c26a"),    # natural: forest green
    "Nebula": from_colors("ece9ff-14122b-a99bff-262248-ff8ad8"),    # dreamy: nebula purple
}


# ---------------------------------------------------------------------------
# Drawing helpers (coordinates are already multiplied by SS)

def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def shrink(img):
    return img.resize((img.width // SS, img.height // SS), Image.LANCZOS)


def gradient(w, h, top, bottom, alpha=255):
    """Vertical gradient"""
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for y in range(h):
        c = lerp(top, bottom, y / max(1, h - 1)) + (alpha,)
        for x in range(w):
            px[x, y] = c
    return img


def rounded_fill(img, box, radius, fill_img):
    """Pastes fill_img onto img through the rounded rectangle box"""
    mask = Image.new("L", img.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius, fill=255)
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    layer.paste(fill_img, (box[0], box[1]))
    img.alpha_composite(Image.composite(layer, Image.new("RGBA", img.size, (0, 0, 0, 0)), mask))


def notched_frame(d, x0, y0, x1, y1, n, color, width):
    """Thin frame with notched (concave) corners"""
    d.line([(x0 + n, y0), (x1 - n, y0)], fill=color, width=width)
    d.line([(x0 + n, y1), (x1 - n, y1)], fill=color, width=width)
    d.line([(x0, y0 + n), (x0, y1 - n)], fill=color, width=width)
    d.line([(x1, y0 + n), (x1, y1 - n)], fill=color, width=width)
    # notched corner: a quarter circle centred on the button corner
    d.arc([x0 - n, y0 - n, x0 + n, y0 + n], 0, 90, fill=color, width=width)
    d.arc([x1 - n, y0 - n, x1 + n, y0 + n], 90, 180, fill=color, width=width)
    d.arc([x1 - n, y1 - n, x1 + n, y1 + n], 180, 270, fill=color, width=width)
    d.arc([x0 - n, y1 - n, x0 + n, y1 + n], 270, 360, fill=color, width=width)


def face(img, x0, y0, w, h, top, bottom, edge, inner=None):
    """Button or tab face: rounded gradient + outline, optionally with a notched inner frame"""
    s = SS
    box = (x0 + s // 2, y0 + s // 2, x0 + w * s - s, y0 + h * s - s)
    rounded_fill(img, box, 3 * s, gradient(box[2] - box[0] + 1, box[3] - box[1] + 1, top, bottom))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(box, 3 * s, outline=edge + (255,), width=s)
    if inner:
        i = 3 * s
        notched_frame(d, box[0] + i, box[1] + i, box[2] - i, box[3] - i, 4 * s, inner + (255,), s)


# ---------------------------------------------------------------------------
# Window: rounded and translucent; the top border is 28 px high for the caption and buttons, the other sides 5

WIN_W, WIN_H = 120, 120
TOP, SIDE = 28, 5


def window(p):
    s = SS
    img = canvas(WIN_W, WIN_H)
    box = (0, 0, WIN_W * s - 1, WIN_H * s - 1)
    rounded_fill(img, box, 7 * s, gradient(WIN_W * s, WIN_H * s, p.bg_top, p.bg_bottom, 242))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(box, 7 * s, outline=p.edge + (200,), width=s)
    d.line([(7 * s, s), (WIN_W * s - 7 * s, s)], fill=p.highlight + (110,), width=s)
    return shrink(img)


# ---------------------------------------------------------------------------
# Caption buttons: 2 columns (close, pin) x 4 rows (normal, hover, down, downhover), 18x18 each

SYS = 18


def sysbutton(p):
    s = SS
    img = canvas(SYS * 2, SYS * 4)
    d = ImageDraw.Draw(img)
    for row in range(4):
        hot, down = row in (1, 3), row >= 2
        for col in range(2):
            x0, y0 = col * SYS * s, row * SYS * s
            box = (x0 + s, y0 + s, x0 + (SYS - 1) * s, y0 + (SYS - 1) * s)
            if hot:
                d.rounded_rectangle(box, 4 * s, fill=p.glyph_hot + (45,))
            elif down and col == 0:
                d.rounded_rectangle(box, 4 * s, fill=p.shadow + (60,))
            c = (p.glyph_hot if hot else p.glyph) + (255,)
            cx, cy = x0 + SYS * s // 2, y0 + SYS * s // 2
            if col == 0:  # ×
                r = 4 * s
                d.line([(cx - r, cy - r), (cx + r, cy + r)], fill=c, width=2 * s)
                d.line([(cx - r, cy + r), (cx + r, cy - r)], fill=c, width=2 * s)
            else:  # pin: solid marker colour when pinned (down)
                head = (cx - 3 * s, cy - 6 * s, cx + 3 * s, cy + 1 * s)
                if down:
                    c = p.marker + (255,)
                    d.ellipse(head, fill=c)
                else:
                    d.ellipse(head, outline=c, width=2 * s)
                d.line([(cx, cy + 1 * s), (cx, cy + 6 * s)], fill=c, width=2 * s)
    return shrink(img)


# ---------------------------------------------------------------------------
# Buttons: 3 rows (normal, hover, down), 48x48 each, 9-slice border 8

CELL = 48


def button(p):
    s = SS
    img = canvas(CELL, CELL * 3)
    face(img, 0, 0, CELL, CELL, p.btn_top, p.btn_bottom, p.btn_edge, p.btn_inner)
    face(img, 0, CELL * s, CELL, CELL, p.hot_top, p.hot_bottom, p.hot_edge, p.hot_inner)
    face(img, 0, 2 * CELL * s, CELL, CELL, p.down_top, p.down_bottom, p.hot_edge, p.down_inner)
    return shrink(img)


# ---------------------------------------------------------------------------
# Tabs: 3 rows (normal, hover, active), 40x24 each, 9-slice border 6

TAB_W, TAB_H = 40, 24


def tab(p):
    s = SS
    img = canvas(TAB_W, TAB_H * 3)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((s, s, TAB_W * s - s, TAB_H * s - s), 3 * s, fill=p.tab_tint + (14,))
    face(img, 0, TAB_H * s, TAB_W, TAB_H, p.hot_top, p.hot_bottom, p.tab_hot_edge)
    y = 2 * TAB_H * s
    face(img, 0, y, TAB_W, TAB_H, p.act_top, p.act_bottom, p.hot_edge)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((3 * s // 2, y + 5 * s, 3 * s, y + (TAB_H - 5) * s), s // 2, fill=p.marker + (255,))
    return shrink(img)


# ---------------------------------------------------------------------------

def hexc(c):
    return "#%02X%02X%02X" % c


def skin_xml(name, p):
    return f"""\
<?xml version="1.0" encoding="UTF-8"?>
<!-- Generated by tools/make_skin.py. To change the images, edit the script and regenerate. -->
<skin title="{name}" author="QLaunch">
	<general tabPosition="left" alphaChannel="true" sizeArea="6, 6, 6, 6" />

	<background image="window.png" left="{SIDE}" top="{TOP}" width="{WIN_W - 2 * SIDE}" height="{WIN_H - TOP - SIDE}" method="stretch" border="0, 0, 0, 0">
		<arrangement offset="0, 0, 0, 0" />
	</background>

	<frame>
		<top image="window.png" left="0" top="0" width="1/1" height="{TOP}" method="stretch" border="10, 0, 10, 0">
			<arrangement height="ImageSize" />
		</top>
		<left image="window.png" left="0" top="{TOP}" width="{SIDE}" height="{WIN_H - TOP - SIDE}" method="stretch" border="0, 0, 0, 0">
			<arrangement width="ImageSize" />
		</left>
		<right image="window.png" left="{WIN_W - SIDE}" top="{TOP}" width="{SIDE}" height="{WIN_H - TOP - SIDE}" method="stretch" border="0, 0, 0, 0">
			<arrangement width="ImageSize" />
		</right>
		<bottom image="window.png" left="0" top="{WIN_H - SIDE}" width="1/1" height="{SIDE}" method="stretch" border="10, 0, 10, 0">
			<arrangement height="ImageSize" />
		</bottom>
	</frame>

	<closebutton>
		<normal image="sysbutton.png" left="1/2" top="1/4" width="1/2" height="1/4" />
		<hover image="sysbutton.png" left="1/2" top="2/4" width="1/2" height="1/4" />
		<down image="sysbutton.png" left="1/2" top="3/4" width="1/2" height="1/4" />
		<arrangement>
			<left base="TopFrame" side="right" offset="-{SYS + 6}" />
			<top base="TopFrame" side="top" offset="5" />
			<right width="ImageSize" />
			<bottom height="ImageSize" />
		</arrangement>
	</closebutton>

	<lockbutton>
		<normal image="sysbutton.png" left="2/2" top="1/4" width="1/2" height="1/4" />
		<hover image="sysbutton.png" left="2/2" top="2/4" width="1/2" height="1/4" />
		<down image="sysbutton.png" left="2/2" top="3/4" width="1/2" height="1/4" />
		<downhover image="sysbutton.png" left="2/2" top="4/4" width="1/2" height="1/4" />
		<arrangement>
			<left base="CloseButton" side="left" offset="-{SYS + 2}" />
			<top base="TopFrame" side="top" offset="5" />
			<right width="ImageSize" />
			<bottom height="ImageSize" />
		</arrangement>
	</lockbutton>

	<caption>
		<arrangement>
			<left base="TopFrame" side="left" offset="12" />
			<top base="TopFrame" side="top" offset="0" />
			<right base="LockButton" side="left" offset="-4" />
			<bottom base="TopFrame" side="bottom" offset="0" />
		</arrangement>
		<text fontSize="10" fontStyle="bold" shadow="lower" color="{hexc(p.text)}" shadowColor="{hexc(p.shadow)}" shadowAlpha="140" />
	</caption>

	<!-- No backing panel for the tab bar or the button area; equal margins all round line up with tabs on the left or on top -->
	<tabarea>
		<arrangement offset="0, 0, 0, 0" margin="4, 4, 4, 4" />
		<tab image="tab.png" method="stretch" border="6, 6, 6, 6" pattern="1">
			<arrangement pitch="0, 3" />
			<inactive>
				<text fontSize="9" fontStyle="normal" shadow="lower" color="{hexc(p.text_dim)}" shadowColor="{hexc(p.shadow)}" shadowAlpha="140">
					<hover color="{hexc(p.text)}" />
				</text>
			</inactive>
			<active>
				<text fontSize="9" fontStyle="bold" shadow="lower" color="{hexc(p.text)}" shadowColor="{hexc(p.shadow)}" shadowAlpha="160">
					<hover color="{hexc(p.text_hot)}" />
				</text>
			</active>
		</tab>
	</tabarea>

	<buttonarea>
		<arrangement offset="0, 0, 0, 0" margin="4, 4, 4, 4" />
		<button image="button.png" method="stretch" border="8, 8, 8, 8" pattern="1">
			<arrangement pitch="5, 5" shiftHover="0, 0" shiftDown="0, 1" />
			<text fontSize="9" fontStyle="normal" shadow="lower" color="{hexc(p.text)}" shadowColor="{hexc(p.shadow)}" shadowAlpha="150">
				<hover color="{hexc(p.text_hot)}" />
			</text>
		</button>
	</buttonarea>

	<recommended>
		<tab size="48, 26" textMargin="7, 0, 5, 0" />
	</recommended>
</skin>
"""


def write_skin(name, p):
    out = SKINS / name
    out.mkdir(parents=True, exist_ok=True)
    window(p).save(out / "window.png", optimize=True)
    sysbutton(p).save(out / "sysbutton.png", optimize=True)
    button(p).save(out / "button.png", optimize=True)
    tab(p).save(out / "tab.png", optimize=True)
    (out / "skin.xml").write_text(skin_xml(name, p), encoding="utf-8-sig", newline="\n")
    print("written to", out)


def main():
    if len(sys.argv) == 3:
        write_skin(sys.argv[1], from_colors(sys.argv[2]))
    elif len(sys.argv) == 1:
        for name, p in THEMES.items():
            write_skin(name, p)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
