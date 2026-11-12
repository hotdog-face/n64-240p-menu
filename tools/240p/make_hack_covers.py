#!/usr/bin/env python3
"""Distinct covers for romhacks that reuse a base game's NUS code. Base game's box
art fills the frame (darkened), with an accent tag + hack name in a bottom banner.
Rendered at 4x then LANCZOS-downsampled; mksprite RGBA16 compress 3."""
from PIL import Image, ImageDraw, ImageFont, ImageEnhance
import os, subprocess, shutil

ASSETS = os.path.expanduser("~/n64/menu/assets/images/boxart")
OUT_STASH = os.path.expanduser("~/n64/boxart-stash")
W, H, S = 160, 120, 4  # landscape, matches base N64 box fronts

def font(sz, bold=True):
    cands = (["/System/Library/Fonts/Supplemental/Arial Bold.ttf"] if bold else
             ["/System/Library/Fonts/Supplemental/Arial.ttf"]) + \
            ["/System/Library/Fonts/Helvetica.ttc"]
    for p in cands:
        if os.path.exists(p):
            try: return ImageFont.truetype(p, sz)
            except: pass
    return ImageFont.load_default()

def wrap(draw, text, fnt, maxw):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if draw.textlength(t, font=fnt) <= maxw: cur = t
        else:
            if cur: lines.append(cur)
            cur = w
    if cur: lines.append(cur)
    return lines[:2]

def make(base_code, art_code, tag, title, accent):
    w, h = W*S, H*S
    src = Image.open(os.path.join(ASSETS, base_code, "front.png")).convert("RGBA")
    # cover-fit base art into frame
    sr, fr = src.width/src.height, w/h
    if sr > fr:
        nh = h; nw = int(h*sr)
    else:
        nw = w; nh = int(w/sr)
    bg = src.resize((nw, nh), Image.LANCZOS).crop(((nw-w)//2,(nh-h)//2,(nw-w)//2+w,(nh-h)//2+h))
    bg = ImageEnhance.Brightness(bg).enhance(0.62)
    im = bg.copy(); d = ImageDraw.Draw(im, "RGBA")
    # bottom gradient banner
    band_h = int(h*0.46)
    grad = Image.new("L", (1, band_h), 0)
    for y in range(band_h):
        grad.putpixel((0,y), int(245*(y/band_h)**0.85))
    grad = grad.resize((w, band_h))
    black = Image.new("RGBA",(w,band_h),(3,5,16,255))
    black.putalpha(grad)
    im.alpha_composite(black,(0,h-band_h))
    d.rectangle([0, h-band_h, w, h-band_h+S], fill=accent+(255,))  # accent rule
    # tag
    f_tag = font(int(h*0.058))
    d.text((int(w*0.045), h-band_h+int(h*0.05)), tag, font=f_tag, fill=accent+(255,))
    # title (1-2 lines), bottom-anchored
    f_ti = font(int(h*0.115))
    lines = wrap(d, title, f_ti, w-int(w*0.09))
    lh = int(h*0.135)
    ty = h - int(h*0.05) - lh*len(lines)
    for ln in lines:
        d.text((int(w*0.045), ty), ln, font=f_ti, fill=(255,255,255,255))
        ty += lh
    im = im.resize((W,H), Image.LANCZOS).convert("RGBA")
    # bake
    srcdir, outdir = f"src_{art_code}", f"out_{art_code}"
    os.makedirs(srcdir, exist_ok=True); os.makedirs(outdir, exist_ok=True)
    im.save(os.path.join(srcdir,"front.png"))
    MKS = os.path.expanduser("~/n64/inst/bin/mksprite")
    r = subprocess.run([MKS,"--format","RGBA16","--compress","3","-o",outdir,os.path.join(srcdir,"front.png")],
                       capture_output=True, text=True)
    assert r.returncode==0, r.stderr[-300:]
    # into stash
    dst = os.path.join(OUT_STASH, art_code); os.makedirs(dst, exist_ok=True)
    shutil.copy(os.path.join(outdir,"front.sprite"), os.path.join(dst,"front.sprite"))
    print(f"  {art_code}: '{title}' <- {base_code}  ({os.path.getsize(os.path.join(dst,'front.sprite'))}B)")

ORANGE=(255,150,40); CYAN=(70,200,255); GREEN=(120,220,120)
make("NKTE","AMPE","ROM HACK","Amped Up", ORANGE)
make("CFZE","FZLC","ROM HACK","F-Zero X Climax", CYAN)
make("CFZE","FZLX","ROM HACK","F-Zero X Expansion Kit", CYAN)
make("NSME","LIMP","ROM HACK","SM64 Last Impact", GREEN)
make("NSME","STRD","ROM HACK","SM64 Star Road", GREEN)
print("all 5 baked")
