"""Draws the app icon (res/app.ico) and the icons of the built-in commands
(lock, shut down, empty recycle bin...) into assets/icons/*.ico.

Requires Python 3.8+ and Pillow (pip install pillow).

Usage:
    python tools/make_icons.py

This PC, Control Panel, Task Manager and the like use Windows' own icons; only the
commands the app runs itself, and a few items without a suitable system icon, are
drawn here. One style for all: a rounded colour tile with a white glyph.

To add an icon, write a function that draws the glyph and add it to ICONS with
its two gradient colours; the file name is the key. To change the app icon, edit
app() and APP, regenerate, and rebuild (CMake recompiles the resources).

Every shape is described on a 256x256 grid. Each size is drawn separately at 4x
and scaled down, so even 16 and 24 px stay crisp. Sizes below 256 are stored as
BMP inside the .ico, 256 as PNG.
"""

import io
import math
import struct
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "icons"
APP_ICON = ROOT / "res" / "app.ico"
SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
SUPER = 4
WHITE = (255, 255, 255, 255)


class Pen:
    """Draws on a 256-unit canvas and converts to real pixels."""

    def __init__(self, size):
        self.px = size * SUPER
        self.k = self.px / 256
        self.img = Image.new("RGBA", (self.px, self.px), (0, 0, 0, 0))
        self.d = ImageDraw.Draw(self.img)

    def s(self, v):
        return v * self.k

    def box(self, x0, y0, x1, y1):
        return [self.s(x0), self.s(y0), self.s(x1), self.s(y1)]

    def tile(self, top, bottom):
        """Rounded colour tile background, lighter at the top."""
        mask = Image.new("L", self.img.size, 0)
        ImageDraw.Draw(mask).rounded_rectangle(self.box(10, 10, 246, 246), radius=self.s(52), fill=255)
        grad = Image.new("RGBA", self.img.size)
        gd = ImageDraw.Draw(grad)
        for y in range(self.px):
            t = y / (self.px - 1)
            gd.line([(0, y), (self.px, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(top, bottom)) + (255,))
        self.img.paste(grad, (0, 0), mask)

    def dot(self, x, y, r, fill=WHITE):
        self.d.ellipse(self.box(x - r, y - r, x + r, y + r), fill=fill)

    def line(self, pts, w, fill=WHITE):
        """Polyline with round caps and joins."""
        sp = [(self.s(x), self.s(y)) for x, y in pts]
        self.d.line(sp, fill=fill, width=round(self.s(w)), joint="curve")
        for x, y in pts:
            self.dot(x, y, w / 2, fill)

    def arc(self, cx, cy, r, start, end, w, fill=WHITE, caps=True):
        """Arc; angles follow PIL: 0 degrees points right, clockwise."""
        self.d.arc(self.box(cx - r, cy - r, cx + r, cy + r), start, end, fill=fill, width=round(self.s(w)))
        if caps:
            for a in (start, end):
                rad = math.radians(a)
                self.dot(cx + (r - w / 2) * math.cos(rad), cy + (r - w / 2) * math.sin(rad), w / 2, fill)

    def rect(self, x0, y0, x1, y1, radius, fill=WHITE):
        self.d.rounded_rectangle(self.box(x0, y0, x1, y1), radius=self.s(radius), fill=fill)

    def polygon(self, pts, fill=WHITE):
        self.d.polygon([(self.s(x), self.s(y)) for x, y in pts], fill=fill)

    def result(self, size):
        return self.img.resize((size, size), Image.LANCZOS)


# ---------------------------------------------------------------------------
# Each icon: background colours + a function that draws the white glyph

def lock(p):
    p.arc(128, 104, 46, 180, 360, 24, caps=False)
    p.line([(82, 104), (82, 122)], 24)
    p.line([(174, 104), (174, 122)], 24)
    p.rect(62, 118, 194, 204, 18)
    p.dot(128, 152, 14, (0, 0, 0, 0))
    p.line([(128, 156), (128, 176)], 12, (0, 0, 0, 0))


def sleep(p):
    # crescent: a big circle minus a circle offset to the top right
    p.dot(118, 138, 70)
    p.dot(152, 108, 60, (0, 0, 0, 0))
    p.line([(160, 62), (196, 62), (160, 98), (196, 98)], 12)


def sign_out(p):
    p.line([(132, 64), (76, 64), (76, 192), (132, 192)], 22)
    p.line([(112, 128), (196, 128)], 22)
    p.line([(166, 96), (198, 128), (166, 160)], 22)


def restart(p):
    p.arc(128, 136, 70, 330, 630, 24)  # gap at the top right, arrow on top pointing right
    p.polygon([(118, 32), (176, 78), (118, 124)])


def shutdown(p):
    p.arc(128, 136, 70, 300, 600, 24)
    p.line([(128, 54), (128, 128)], 24)


def desktop(p):
    p.rect(50, 60, 206, 168, 14)
    p.rect(64, 74, 192, 154, 6, (0, 0, 0, 0))
    p.line([(128, 168), (128, 196)], 18)
    p.line([(92, 200), (164, 200)], 18)
    # a small window minimized on the screen
    p.line([(100, 136), (156, 136)], 14)


def monitor_off(p):
    p.rect(50, 60, 206, 168, 14)
    p.line([(128, 168), (128, 196)], 18)
    p.line([(92, 200), (164, 200)], 18)
    p.rect(64, 74, 192, 154, 6, (0, 0, 0, 0))
    p.arc(128, 116, 26, 300, 600, 10)
    p.line([(128, 84), (128, 112)], 10)


def empty_bin(p):
    p.line([(64, 72), (192, 72)], 18)
    p.line([(106, 72), (110, 54), (146, 54), (150, 72)], 14)
    p.polygon([(76, 88), (180, 88), (168, 206), (88, 206)])
    for x in (106, 128, 150):
        p.line([(x, 108), (x, 186)], 10, (0, 0, 0, 0))


def search(p):
    p.arc(112, 112, 52, 0, 360, 24, caps=False)
    p.line([(152, 152), (196, 196)], 28)


def run(p):
    # a window with an arrow shooting out of it to the top right
    p.line([(140, 72), (60, 72), (60, 196), (184, 196), (184, 116)], 20)
    p.rect(60, 62, 140, 96, 10)
    p.line([(108, 148), (192, 64)], 22)
    p.polygon([(150, 52), (206, 50), (204, 106)])


def env_vars(p):
    # { x }
    p.line([(92, 60), (76, 66), (72, 104), (54, 128), (72, 152), (76, 190), (92, 196)], 16)
    p.line([(164, 60), (180, 66), (184, 104), (202, 128), (184, 152), (180, 190), (164, 196)], 16)
    p.line([(106, 102), (150, 154)], 18)
    p.line([(150, 102), (106, 154)], 18)


ICONS = {
    "lock": ((255, 196, 70), (226, 140, 20), lock),
    "sleep": ((110, 120, 240), (70, 62, 190), sleep),
    "sign-out": ((60, 200, 190), (20, 140, 150), sign_out),
    "restart": ((90, 210, 120), (30, 150, 80), restart),
    "shutdown": ((255, 100, 90), (205, 40, 50), shutdown),
    "desktop": ((80, 170, 255), (30, 110, 220), desktop),
    "monitor-off": ((130, 140, 160), (70, 78, 96), monitor_off),
    "empty-bin": ((255, 150, 80), (225, 90, 40), empty_bin),
    "search": ((70, 150, 255), (40, 90, 220), search),
    "run": ((90, 200, 250), (30, 130, 210), run),
    "env-vars": ((180, 120, 250), (120, 70, 210), env_vars),
}


# App icon: a 2x2 grid with the bottom-right tile in the accent colour
APP = ((70, 90, 120), (34, 44, 64))


def app(p):
    p.rect(52, 52, 122, 122, 18)
    p.rect(134, 52, 204, 122, 18)
    p.rect(52, 134, 122, 204, 18)
    p.rect(134, 134, 204, 204, 18, (255, 196, 61, 255))


def render(top, bottom, draw, size):
    p = Pen(size)
    p.tile(top, bottom)
    glyph = Pen(size)
    draw(glyph)
    # the white glyph sits on its own layer, so the transparent "holes" don't cut through the background
    p.img.alpha_composite(glyph.img)
    return p.result(size)


def bmp_entry(img):
    """A 32-bit BMP icon entry: BITMAPINFOHEADER (double height) + bottom-up BGRA + AND mask."""
    w, h = img.size
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = img.transpose(Image.FLIP_TOP_BOTTOM).tobytes("raw", "BGRA")
    stride = (w + 31) // 32 * 4
    mask = bytearray(stride * h)
    alpha = img.transpose(Image.FLIP_TOP_BOTTOM).getchannel("A").tobytes()
    for y in range(h):
        for x in range(w):
            if alpha[y * w + x] == 0:
                mask[y * stride + x // 8] |= 0x80 >> (x % 8)
    return header + pixels + bytes(mask)


def save_ico(path, top, bottom, draw):
    """Small sizes as BMP (understood by every Windows version and resource compiler), 256 as PNG (smaller)."""
    entries = []
    for size in SIZES:
        img = render(top, bottom, draw, size)
        if size >= 256:
            buf = io.BytesIO()
            img.save(buf, format="PNG", optimize=True)
            entries.append((size, buf.getvalue()))
        else:
            entries.append((size, bmp_entry(img)))
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for size, data in entries:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in entries:
        out += data
    path.write_bytes(bytes(out))
    print(path.name)


def main():
    save_ico(APP_ICON, *APP, app)
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (top, bottom, draw) in ICONS.items():
        save_ico(OUT / f"{name}.ico", top, bottom, draw)


if __name__ == "__main__":
    main()
