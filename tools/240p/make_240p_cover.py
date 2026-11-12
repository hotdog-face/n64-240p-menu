#!/usr/bin/env python3
"""Cover for the 240p Test Suite: SMPTE-style color bars + PLUGE + title.
Rendered at 3x then downsampled (LANCZOS) for crisp edges, then mksprite RGBA16."""
from PIL import Image, ImageDraw, ImageFont
import os, subprocess

W, H = 113, 158
S = 4
im = Image.new("RGBA", (W*S, H*S), (0, 0, 0, 255))
d = ImageDraw.Draw(im)
w, h = W*S, H*S

# --- 75% SMPTE color bars: top ~52% ---
bars = [(192,192,192),(192,192,0),(0,192,192),(0,192,0),
        (192,0,192),(192,0,0),(0,0,192)]
bar_top, bar_bot = 0, int(h*0.52)
bw = w / len(bars)
for i, c in enumerate(bars):
    d.rectangle([int(i*bw), bar_top, int((i+1)*bw), bar_bot], fill=c+(255,))

# --- reverse/castellation strip ---
cast_bot = int(h*0.60)
rev = [(0,0,192),(16,16,16),(192,0,192),(16,16,16),(0,192,192),(16,16,16),(192,192,192)]
for i, c in enumerate(rev):
    d.rectangle([int(i*bw), bar_bot, int((i+1)*bw), cast_bot], fill=c+(255,))

# --- PLUGE: black with -/0/+ near-black steps ---
pluge_bot = int(h*0.70)
d.rectangle([0, cast_bot, w, pluge_bot], fill=(16,16,16,255))
steps = [(6,6,6),(16,16,16),(28,28,28)]
sx = int(w*0.62)
for i, c in enumerate(steps):
    d.rectangle([sx+int(i*(w*0.12)), cast_bot, sx+int((i+1)*(w*0.12)), pluge_bot], fill=c+(255,))

# --- title block: deep blue with crisp text ---
d.rectangle([0, pluge_bot, w, h], fill=(4, 8, 40, 255))
d.rectangle([0, pluge_bot, w, pluge_bot+S], fill=(255,255,255,255))  # top rule

def load_font(sz):
    for p in ["/System/Library/Fonts/Supplemental/Arial Bold.ttf",
              "/System/Library/Fonts/Helvetica.ttc",
              "/System/Library/Fonts/SFNSDisplay.ttf"]:
        if os.path.exists(p):
            try: return ImageFont.truetype(p, sz)
            except: pass
    return ImageFont.load_default()

def centered(text, font, cy, fill):
    bb = d.textbbox((0,0), text, font=font)
    tw, th = bb[2]-bb[0], bb[3]-bb[1]
    d.text(((w-tw)/2 - bb[0], cy - th/2 - bb[1]), text, font=font, fill=fill)

f_big = load_font(int(h*0.16))
f_sm  = load_font(int(h*0.055))
band_mid = (pluge_bot + h) / 2
centered("240p", f_big, band_mid - h*0.045, (255,255,255,255))
centered("TEST SUITE", f_sm, band_mid + h*0.075, (120,200,255,255))

im = im.resize((W, H), Image.LANCZOS)
png = "240p_front.png"
im.convert("RGBA").save(png)
print("wrote", png, im.size)

MKS = os.path.expanduser("~/n64/inst/bin/mksprite")
out = "240p_front.sprite"
r = subprocess.run([MKS, "--format", "RGBA16", "--compress", "3", "-o", out, png],
                   capture_output=True, text=True)
if r.returncode != 0:
    print("MKSPRITE FAIL:", r.stderr[-400:]); raise SystemExit(1)
print("baked", out, os.path.getsize(out), "bytes")
