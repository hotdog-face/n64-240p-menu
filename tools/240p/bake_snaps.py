#!/usr/bin/env python3
"""Bake gameplay screenshots ("snaps") into the sharded art tree the 240p menu reads.

NOTE: This is a faithful reconstruction of the working script (the original lived in a
session scratchpad that has since been cleared).  It documents exactly what the shipped
snaps were built from and how; adjust the paths/sizes at the top if re-running.

Source : ~/n64/thumbs/Named_Snaps  (libretro "Named_Snaps" thumbnail set)
Match  : by No-Intro ROM name.  libretro mangles filesystem-hostile characters in its
         thumbnail filenames -- &*/:`<>?\\| all become '_' -- so the ROM name is passed
         through the same mangling before lookup.
Resize : center-crop to a mild 16:10 (target width = height * 1.6) then resize to 240x150.
Encode : mksprite --format RGBA16 --compress 3
Output : <CARD_ART>/<code[1]>/<CODE>/snap.sprite   (2-level DFS shard, shard = code[1])

The baked snaps also feed build_pak.py (type "snap" = 4), which is the archive the menu
actually loads at runtime; the sharded tree is the per-file fallback.
"""
import os, re, subprocess, tempfile
from PIL import Image

SNAP_SRC = os.path.expanduser("~/n64/thumbs/Named_Snaps")
CARD_ART = os.path.expanduser("~/n64/boxart-stash")   # stash = source of truth; reshard to card
MKSPRITE = os.path.expanduser("~/n64/inst/bin/mksprite")
OUT_W, OUT_H = 240, 150

# libretro filename mangling: these characters -> '_'
_MANGLE = str.maketrans({c: "_" for c in '&*/:`<>?\\|'})

def libretro_name(rom_name_no_ext):
    return rom_name_no_ext.translate(_MANGLE)

def crop_1610(im):
    """Center-crop to width = height*1.6 (a mild horizontal crop), then resize."""
    w, h = im.size
    target = int(h * 1.6)
    if target < w:                       # crop width
        x0 = (w - target) // 2
        im = im.crop((x0, 0, x0 + target, h))
    else:                                # crop height
        target_h = int(w / 1.6)
        y0 = (h - target_h) // 2
        im = im.crop((0, y0, w, y0 + target_h))
    return im.resize((OUT_W, OUT_H), Image.LANCZOS)

def bake_one(code, rom_name_no_ext):
    src = os.path.join(SNAP_SRC, libretro_name(rom_name_no_ext) + ".png")
    if not os.path.isfile(src):
        return False
    im = crop_1610(Image.open(src).convert("RGBA"))
    with tempfile.TemporaryDirectory() as td:
        png = os.path.join(td, "snap.png")
        im.save(png)
        r = subprocess.run([MKSPRITE, "--format", "RGBA16", "--compress", "3",
                            "-o", td, png], capture_output=True, text=True)
        if r.returncode != 0:
            print("mksprite fail:", rom_name_no_ext, r.stderr[-200:])
            return False
        dst = os.path.join(CARD_ART, code[1], code)
        os.makedirs(dst, exist_ok=True)
        os.replace(os.path.join(td, "snap.sprite"), os.path.join(dst, "snap.sprite"))
    return True

if __name__ == "__main__":
    # Feed (game_code, rom_name_without_extension) pairs from your favorites/library.
    # The shipped run baked ~563 snaps into the stash.
    raise SystemExit("Edit main() to supply (code, rom_name) pairs, then run.")
