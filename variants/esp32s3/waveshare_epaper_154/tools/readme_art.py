"""Banner and section titles for the README, in the CalandriApp look ("Topografia" style, Barlow).

Same mark, palette and title style as github.com/proto-hommer/calandriapp, so both repos read as one
project. Titles are transparent PNGs (readable in GitHub's light and dark themes); the README uses
the title text as alt text.

Barlow is not in this repo (OFL, from Google Fonts or CalandriApp's res/font):
    python readme_art.py --barlow path/to/barlow_regular.ttf
Writes ../img/readme/banner.png and ../img/readme/titulos/*.png
"""

import argparse
import math
import os
import re
import unicodedata

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "img", "readme"))
MONO = r"C:\Windows\Fonts\consola.ttf"

# CalandriApp mark (marca.svg): nodes, outline and mesh edges; node 13 is the eye
P = [168, 62, 138, 47, 124, 53, 98, 74, 72, 106, 28, 156, 36, 168, 84, 124, 120, 120, 146, 98, 152, 70, 108, 96, 128, 92, 136, 66]
PTS = [(P[i], P[i + 1]) for i in range(0, len(P), 2)]
EYE = 13
OUTLINE = list(range(11))
E = [0, 13, 1, 13, 2, 13, 10, 13, 3, 13, 3, 11, 13, 12, 10, 12, 9, 12, 8, 12, 11, 12, 11, 4, 11, 7, 11, 8, 4, 7, 7, 5, 11, 13]
EDGES = [(E[i], E[i + 1]) for i in range(0, len(E), 2)]
BOX = (24.8, 43.8, 146.4, 127.4)

COPPER = (201, 130, 69)
COPPER_DIM = (120, 78, 44)
EYE_C = (60, 200, 120)
TEAL = (46, 143, 163)
NAME = (200, 214, 222)
SUB = (90, 120, 135)

H2 = [
    "Qué es esto",
    "Estado del proyecto",
    "Hardware",
    "Cómo se hizo",
    "Cableado",
    "Funciones",
    "Flashear",
    "Compilar desde el código",
    "Lo que viene",
    "Problemas conocidos",
    "Créditos, marcas y licencias",
]
H3 = [
    "Con CalandriApp",
    "Lo que anda",
    "Lo que falta probar",
    "Necesario",
    "Opcional",
    "La radio",
    "El encoder",
    "GPIO19 y GPIO20: no tocar",
    "Nodes",
    "Mensajes",
    "Teclado en pantalla",
    "Waypoints",
    "Radar",
    "Estado",
    "Controles",
    "Primer arranque",
    "A batería",
    "Actualizar",
    "GPS",
    "Batería 18650",
    "Carcasa 3D",
]


def spaced(draw, text, font, x, y, fill, spacing, anchor="lm"):
    for c in text:
        draw.text((x, y), c, font=font, fill=fill, anchor=anchor)
        x += font.getlength(c) + spacing
    return x


def spaced_width(text, font, spacing):
    return sum(font.getlength(c) for c in text) + spacing * (len(text) - 1)


def draw_mark(d, cx, cy, s):
    bx, by, bw, bh = BOX
    pts = [(cx + (x - (bx + bw / 2)) * s, cy + (y - (by + bh / 2)) * s) for x, y in PTS]
    lw = max(2, int(s * 0.7))
    for a, b in EDGES:
        d.line([pts[a], pts[b]], fill=COPPER_DIM, width=max(2, lw - 2))
    ol = [pts[i] for i in OUTLINE]
    for i in range(len(ol) - 1):
        d.line([ol[i], ol[i + 1]], fill=COPPER, width=lw)
    for i, p in enumerate(pts):
        if i == EYE:
            continue
        r = s * 1.7
        d.ellipse((p[0] - r, p[1] - r, p[0] + r, p[1] + r), fill=COPPER)
    e = pts[EYE]
    R = s * 3.6
    d.ellipse((e[0] - R, e[1] - R, e[0] + R, e[1] + R), outline=EYE_C, width=max(2, int(s * 0.9)))
    r = R * 0.45
    d.ellipse((e[0] - r, e[1] - r, e[0] + r, e[1] + r), fill=EYE_C)


def banner(font):
    W, H = 1760, 560
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(6 + 3 * t), int(10 + 4 * t), int(16 + 6 * t)))
    # terrain contours (different hills than CalandriApp's banner: same style, another place)
    topo = Image.new("RGB", (W, H))
    td = ImageDraw.Draw(topo)
    for px, py, n, ph in [(330, 430, 9, 1.1), (1420, 80, 8, 2.3), (900, 640, 6, 0.2)]:
        for k in range(1, n + 1):
            r = 62 * k
            pts = []
            for a_ in range(0, 361, 3):
                a = math.radians(a_)
                rr = r * (1 + 0.10 * math.sin(3 * a + ph + k * 0.3) + 0.05 * math.sin(5 * a + ph * 2))
                pts.append((px + rr * math.cos(a), py + rr * math.sin(a) * 1.15))
            td.line(pts, fill=(30, 52, 64), width=2)
    im = ImageChops.add(im, topo)
    mark = Image.new("RGB", (W, H))
    draw_mark(ImageDraw.Draw(mark), 330, 280, 2.3)
    im = ImageChops.add(im, ImageChops.add(mark, mark.filter(ImageFilter.GaussianBlur(9)).point(lambda v: int(v * 0.35))))
    d = ImageDraw.Draw(im)

    x0 = 600
    f = ImageFont.truetype(font, 100)
    spaced(d, "CALANDRIA MK1", f, x0, 215, NAME, 18)
    fs = ImageFont.truetype(MONO, 30)
    spaced(d, "E-PAPER · LORA · MESHTASTIC", fs, x0 + 4, 305, SUB, 9)

    # "alpha" tag: copper outline, like a node label on the map
    ft = ImageFont.truetype(MONO, 26)
    tag = "ALPHA · EN DESARROLLO"
    tw = spaced_width(tag, ft, 6)
    tx, ty = x0 + 4, 365
    d.rounded_rectangle((tx, ty, tx + tw + 36, ty + 50), radius=8, outline=COPPER, width=3)
    spaced(d, tag, ft, tx + 18, ty + 25, COPPER, 6)

    fp = ImageFont.truetype(font, 28)
    d.text((W - 60, H - 40), "complemento de CalandriApp · powered by Meshtastic®", font=fp, fill=(110, 130, 140), anchor="rm")
    for x, y, sx, sy in [(28, 28, 1, 1), (W - 28, 28, -1, 1), (28, H - 28, 1, -1), (W - 28, H - 28, -1, -1)]:
        d.line([(x, y), (x + sx * 44, y)], fill=(40, 110, 125), width=3)
        d.line([(x, y), (x, y + sy * 44)], fill=(40, 110, 125), width=3)
    im.save(os.path.join(OUT, "banner.png"), optimize=True)


def slug(t):
    t = unicodedata.normalize("NFKD", t).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]+", "-", t.lower()).strip("-")


def h2(font, text):
    W, H = 1760, 130
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    f = ImageFont.truetype(font, 74)
    d.ellipse((6, 52, 30, 76), fill=COPPER + (255,))
    d.line([(30, 64), (70, 64)], fill=COPPER + (255,), width=4)
    end = spaced(d, text.upper(), f, 90, 66, COPPER + (255,), 10)
    d.line([(end + 26, 66), (W - 30, 66)], fill=TEAL + (150,), width=3)
    d.ellipse((W - 30, 57, W - 12, 75), outline=TEAL + (220,), width=3)
    im.save(os.path.join(OUT, "titulos", slug(text) + ".png"), optimize=True)


def h3(font, text):
    f = ImageFont.truetype(font, 54)
    w = int(spaced_width(text.upper(), f, 6)) + 70
    im = Image.new("RGBA", (w, 84), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((4, 34, 20, 50), outline=TEAL + (255,), width=4)
    spaced(d, text.upper(), f, 42, 44, TEAL + (255,), 6)
    im.save(os.path.join(OUT, "titulos", "s-" + slug(text) + ".png"), optimize=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--barlow", required=True, help="Barlow Regular .ttf")
    a = ap.parse_args()
    os.makedirs(os.path.join(OUT, "titulos"), exist_ok=True)
    banner(a.barlow)
    for t in H2:
        h2(a.barlow, t)
    for t in H3:
        h3(a.barlow, t)
    print(OUT)


if __name__ == "__main__":
    main()
