#!/usr/bin/env python3
"""Convert host-rendered PPM frames to PNG previews (480x480, round mask like the real
2.1in round panel) plus a labeled contact sheet. These are REAL renders of display/ui.cpp,
not mockups. Usage: python3 make_previews.py <ppm_dir> <png_out_dir>"""
import sys, os, glob
from PIL import Image, ImageDraw, ImageFont
src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
mask = Image.new("L", (480, 480), 0); ImageDraw.Draw(mask).ellipse((0, 0, 479, 479), fill=255)
files = sorted(glob.glob(os.path.join(src, "*.ppm")))
tiles = []
for f in files:
    im = Image.open(f).convert("RGB")
    out = Image.new("RGB", (480, 480), (40, 40, 40)); out.paste(im, (0, 0), mask)
    name = os.path.splitext(os.path.basename(f))[0]
    out.save(os.path.join(dst, name + ".png")); tiles.append((name, out))
cols = 5; rows = (len(tiles) + cols - 1) // cols; tw = 240
sheet = Image.new("RGB", (cols * (tw + 10) + 10, rows * (tw + 34) + 50), (20, 20, 20))
d = ImageDraw.Draw(sheet)
try: fnt = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 13)
except Exception: fnt = None
d.text((10, 12), "gauge3_v1 display previews v2 (Nov 22 2026 drive values) - rendered on PC from the same display/ui.cpp + gfx.cpp firmware code (480x480 round)", fill=(220, 220, 220), font=fnt)
for i, (n, im) in enumerate(tiles):
    x = 10 + (i % cols) * (tw + 10); y = 40 + (i // cols) * (tw + 34)
    sheet.paste(im.resize((tw, tw), Image.LANCZOS), (x, y)); d.text((x, y + tw + 4), n, fill=(200, 200, 200), font=fnt)
sheet.save(os.path.join(dst, "00_contact_sheet.png")); print(len(tiles), "previews ->", dst)
