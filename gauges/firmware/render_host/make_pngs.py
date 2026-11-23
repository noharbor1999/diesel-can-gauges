#!/usr/bin/env python3
"""Turn render_host/out/*.raw (RGB565 framebuffers from the real firmware code) into round-masked PNGs
and one labelled contact sheet."""
import glob, os, sys
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'renders')
os.makedirs(OUT, exist_ok=True)
SCALE = 2

def load(path):
    d = open(path, 'rb').read()
    im = Image.new('RGB', (240, 240))
    px = []
    for i in range(0, len(d), 2):
        v = d[i] | (d[i + 1] << 8)
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        px.append((r * 255 // 31, g * 255 // 63, b * 255 // 31))
    im.putdata(px)
    return im

def masked(im, s):
    big = im.resize((240 * s, 240 * s), Image.NEAREST)
    pad = 8 * s
    W = 240 * s + 2 * pad
    out = Image.new('RGB', (W, W), (255, 255, 255))
    m = Image.new('L', (240 * s, 240 * s), 0)
    ImageDraw.Draw(m).ellipse((0, 0, 240 * s - 1, 240 * s - 1), fill=255)
    ImageDraw.Draw(out).ellipse((pad - 5 * s, pad - 5 * s, pad + 240 * s + 5 * s, pad + 240 * s + 5 * s), fill=(0, 0, 0))
    out.paste(big, (pad, pad), m)
    return out

def font(sz):
    for f in ['/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', '/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf']:
        if os.path.exists(f): return ImageFont.truetype(f, sz)
    return ImageFont.load_default()

files = sorted(glob.glob(os.path.join(HERE, 'out', '*.raw')))
tiles = []
for f in files:
    name = os.path.basename(f)[:-4]
    im = load(f)
    masked(im, SCALE).save(os.path.join(OUT, name + '.png'))
    tiles.append((name, masked(im, 1)))

cols = 6; tw = tiles[0][1].width; lh = 40
rows = (len(tiles) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (tw + 16) + 16, 70 + rows * (tw + lh + 16)), (255, 255, 255))
d = ImageDraw.Draw(sheet)
d.text((16, 18), 'Truck EGT gauge firmware v7 - every screen, rendered from the real drawing code (GFXcanvas16)', fill=(0, 0, 0), font=font(24))
for i, (name, t) in enumerate(tiles):
    x = 16 + (i % cols) * (tw + 16); y = 70 + (i // cols) * (tw + lh + 16)
    sheet.paste(t, (x, y))
    label = name.split('_', 1)[1].replace('_', ' ').upper()
    d.text((x + 4, y + tw + 2), f'{name[:2]}  {label}', fill=(0, 0, 0), font=font(12))
sheet.save(os.path.join(OUT, 'all_screens_v7.png'))
print(len(tiles), 'screens ->', os.path.normpath(OUT))
