#!/usr/bin/env python3
"""Rasterize the CalandriApp mark (SVG) to a 1-bit XBM boot logo for InkHUD.

Reads the SVG geometry (polygon outline, mesh lines, nodes, eye) and redraws it
with Pillow at the target size, with stroke widths chosen for a 1-bit e-paper.
Outputs:
  - the USERPREFS_OEM_IMAGE_DATA array (printed, and optionally written into
    userPrefs.jsonc with --userprefs)
  - a 200x200 preview of the whole boot screen, laid out like LogoApplet.cpp,
    with the text rendered from the firmware's own FreeSans 12pt GFX font
  - the same preview zoomed x4

Usage (Windows, from the repo root):
  python variants/esp32s3/calandria_mk1_epaper/tools/calandriapp_logo.py ^
      %USERPROFILE%/Downloads/calandriapp-marca.svg --userprefs userPrefs.jsonc
Requires: pip install pillow
"""

import argparse
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageDraw

REPO_ROOT = Path(__file__).resolve().parents[4]
FONT_DIR = REPO_ROOT / "src/graphics/niche/Fonts"

# Target bitmap (width must be a multiple of 8)
LOGO_W, LOGO_H = 120, 100
SCREEN = 200
TEXT = "CalandriApp"
POWERED_BY = "powered by MESHTASTIC\u00ae"  # must match LogoApplet.cpp

# 1-bit adaptation, in output pixels
SS = 8  # supersampling factor
OUTLINE_W = 3.0  # outer contour
EDGE_W = 2.4  # inner mesh connections
NODE_D = 4.6  # node diameter
EYE_D = 12.0  # eye: black disc diameter
EYE_DOT_D = 3.6  # eye: white center dot diameter
EYE_HALO = 1.2  # eye: white ring between the disc and the incoming lines
MARGIN = 3  # space between the drawing and the bitmap edge
THRESHOLD = 0.45  # coverage needed for a pixel to turn black

# Inner connections dropped if they clog at this size, as (x1, y1, x2, y2) in SVG units
SKIP_EDGES = set()

SVG_NS = "{http://www.w3.org/2000/svg}"


def parse_svg(path):
    root = ET.parse(path).getroot()
    vb = [float(v) for v in root.get("viewBox").split()]
    outline, edges, nodes, eye = [], [], [], None
    for el in root.iter():
        tag = el.tag.replace(SVG_NS, "")
        if tag == "path" and el.get("fill-opacity") is None:
            nums = [float(n) for n in re.findall(r"-?\d+(?:\.\d+)?", el.get("d"))]
            outline = list(zip(nums[0::2], nums[1::2]))
        elif tag == "line":
            e = tuple(float(el.get(k)) for k in ("x1", "y1", "x2", "y2"))
            if e not in SKIP_EDGES and (e[2], e[3], e[0], e[1]) not in SKIP_EDGES:
                edges.append(e)
        elif tag == "circle":
            c = (float(el.get("cx")), float(el.get("cy")), float(el.get("r")))
            if el.get("stroke"):  # the eye is the only stroked circle
                eye = c
            else:
                nodes.append(c)
    if not outline or eye is None:
        sys.exit("SVG does not look like the CalandriApp mark (outline or eye missing)")
    return vb, outline, edges, nodes, eye


def render_logo(svg_path):
    _, outline, edges, nodes, eye = parse_svg(svg_path)
    # Fit the drawing itself (not the SVG's padded viewBox), leaving room for the eye
    xs = [x for x, _ in outline] + [c[0] for c in nodes] + [eye[0]]
    ys = [y for _, y in outline] + [c[1] for c in nodes] + [eye[1]]
    vx, vy, vw, vh = min(xs), min(ys), max(xs) - min(xs), max(ys) - min(ys)
    m = MARGIN
    scale = min((LOGO_W - 2 * m) / vw, (LOGO_H - 2 * m) / vh)
    ox = (LOGO_W - vw * scale) / 2
    oy = (LOGO_H - vh * scale) / 2

    def p(x, y):
        return ((x - vx) * scale + ox) * SS, ((y - vy) * scale + oy) * SS

    big = Image.new("L", (LOGO_W * SS, LOGO_H * SS), 0)
    d = ImageDraw.Draw(big)

    def stroke(a, b, w):
        d.line([a, b], fill=255, width=round(w * SS))
        r = w * SS / 2  # round caps / joins
        for x, y in (a, b):
            d.ellipse([x - r, y - r, x + r, y + r], fill=255)

    def disc(x, y, dia, fill=255):
        r = dia * SS / 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)

    for x1, y1, x2, y2 in edges:
        stroke(p(x1, y1), p(x2, y2), EDGE_W)
    pts = [p(x, y) for x, y in outline]
    for a, b in zip(pts, pts[1:] + pts[:1]):
        stroke(a, b, OUTLINE_W)
    for cx, cy, _ in nodes:
        disc(*p(cx, cy), NODE_D)
    ex, ey = p(eye[0], eye[1])
    disc(ex, ey, EYE_D + 2 * EYE_HALO, fill=0)  # white ring: separates the eye from the mesh
    disc(ex, ey, EYE_D)
    disc(ex, ey, EYE_DOT_D, fill=0)

    small = big.resize((LOGO_W, LOGO_H), Image.Resampling.BOX)
    cut = int(THRESHOLD * 255)
    return small.point(lambda v: 1 if v >= cut else 0, mode="1")  # 1 = black


def to_xbm_bytes(img):
    w, h = img.size
    px = img.load()
    out = []
    for y in range(h):
        for bx in range((w + 7) // 8):
            byte = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < w and px[x, y]:
                    byte |= 1 << bit  # LSB first
            out.append(byte)
    return out


# --- Adafruit GFX font, read from the firmware header -------------------------


def load_gfx_font(path):
    src = Path(path).read_text(encoding="utf-8")
    bm_src = src[src.index("Bitmaps[]") : src.index("Glyphs[]")]
    bm_src = re.sub(r"/\*.*?\*/", "", bm_src, flags=re.S)
    bitmap = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{2}", bm_src)]
    gl_src = src[src.index("Glyphs[]") : src.index("const GFXfont")]
    glyphs = [tuple(int(n) for n in g.split(",")) for g in re.findall(r"\{\s*([-\d,\s]+?)\s*\}", gl_src)]
    first, last = (int(v, 16) for v in re.findall(r"(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*\d+\s*\};", src)[0])
    return bitmap, glyphs, first, last


class GfxText:
    """Mimics InkHUD::AppletFont metrics and Applet::printAt(CENTER, TOP)."""

    def __init__(self, path, pad_top, pad_bottom):  # padding as in AppletFont.h
        self.bitmap, self.glyphs, self.first, self.last = load_gfx_font(path)
        self.ascender = max(max(0, -g[5]) for g in self.glyphs) + pad_top
        self.descender = max(max(0, g[2] + g[5]) for g in self.glyphs) + pad_bottom

    def glyph(self, ch):
        return self.glyphs[ord(ch) - self.first]

    def bounds(self, text):
        cx, minx, maxx = 0, 10**6, -(10**6)
        for ch in text:
            _, w, h, adv, xo, _ = self.glyph(ch)
            if w and h:
                minx, maxx = min(minx, cx + xo), max(maxx, cx + xo + w - 1)
            cx += adv
        return minx, maxx - minx + 1

    def width(self, text):
        return self.bounds(text)[1]

    def draw_centered_top(self, img, x, y, text):
        off, w = self.bounds(text)
        cx, cy = (x - off) - w // 2, y + self.ascender
        px = img.load()
        for ch in text:
            bo, gw, gh, adv, xo, yo = self.glyph(ch)
            bit = 0
            for yy in range(gh):
                for xx in range(gw):
                    if self.bitmap[bo + bit // 8] & (0x80 >> (bit % 8)):
                        X, Y = cx + xo + xx, cy + yo + yy
                        if 0 <= X < img.width and 0 <= Y < img.height:
                            px[X, Y] = 0
                    bit += 1
            cx += adv


def render_screen(logo, font, small):
    """Boot screen as LogoApplet::onRender draws it with USERPREFS_OEM_* set."""
    screen = Image.new("1", (SCREEN, SCREEN), 1)  # 1 = white here
    cx, cy = int(SCREEN * 0.5), int(SCREEN * (0.5 - 0.05))
    cy -= int(SCREEN * 0.06)  # LogoApplet moves the block up when USERPREFS_OEM_POWERED_BY is set
    left, top = cx - LOGO_W // 2, cy - LOGO_H // 2
    lp = logo.load()
    sp = screen.load()
    for y in range(LOGO_H):
        for x in range(LOGO_W):
            if lp[x, y]:
                sp[left + x, top + y] = 0
    if font.width(TEXT) > 0.8 * SCREEN:
        print(f"warning: '{TEXT}' is wider than 80% of the screen; InkHUD will fall back to a smaller font")
    text_top = cy + LOGO_H // 2 + int(SCREEN * 0.1)
    font.draw_centered_top(screen, cx, text_top, TEXT)
    small_top = text_top + font.ascender + font.descender + 2
    small.draw_centered_top(screen, cx, small_top, POWERED_BY)
    if small_top + small.ascender + small.descender > SCREEN:
        print("warning: the 'powered by' line does not fit on screen")
    return screen


def update_userprefs(path, data):
    values = {
        "USERPREFS_OEM_TEXT": TEXT,
        "USERPREFS_OEM_IMAGE_WIDTH": str(LOGO_W),
        "USERPREFS_OEM_IMAGE_HEIGHT": str(LOGO_H),
        "USERPREFS_OEM_IMAGE_DATA": data,
        "USERPREFS_OEM_POWERED_BY": "true",
    }
    lines = Path(path).read_text(encoding="utf-8").splitlines(keepends=True)
    done = set()
    for i, line in enumerate(lines):
        m = re.match(r'(\s*)(?://\s*)?"(USERPREFS_OEM_[A-Z_]+)"\s*:', line)
        if m and m.group(2) in values:
            lines[i] = f'{m.group(1)}"{m.group(2)}": "{values[m.group(2)]}",\n'
            done.add(m.group(2))
    missing = set(values) - done
    if missing:
        sys.exit(f"{path}: keys not found: {', '.join(sorted(missing))}")
    Path(path).write_text("".join(lines), encoding="utf-8", newline="\n")
    print(f"updated {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("svg", help="calandriapp-marca.svg")
    ap.add_argument("--out-dir", default=".", help="where to write the previews (default: current dir)")
    ap.add_argument("--userprefs", help="userPrefs.jsonc to update in place")
    args = ap.parse_args()

    logo = render_logo(args.svg)
    xbm = to_xbm_bytes(logo)
    data = "{ " + ", ".join(f"0x{b:02X}" for b in xbm) + " }"

    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    large = GfxText(FONT_DIR / "FreeSans12pt_Win1252.h", -3, 1)  # fontLarge = FREESANS_12PT
    small = GfxText(FONT_DIR / "FreeSans6pt_Win1252.h", -1, -2)  # fontSmall = FREESANS_6PT
    screen = render_screen(logo, large, small)
    screen.save(out / "calandriapp-arranque-200.png")
    screen.resize((SCREEN * 4, SCREEN * 4), Image.Resampling.NEAREST).save(out / "calandriapp-arranque-200-zoom.png")
    print(f"previews in {out.resolve()}")
    print(f"{LOGO_W}x{LOGO_H}, {len(xbm)} bytes")

    if args.userprefs:
        update_userprefs(args.userprefs, data)
    else:
        print(f'"USERPREFS_OEM_IMAGE_DATA": "{data}",')


if __name__ == "__main__":
    main()
