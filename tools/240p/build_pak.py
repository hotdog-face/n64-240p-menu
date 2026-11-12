#!/usr/bin/env python3
"""Build boxart.pak: one indexed archive of all art the 240p menu uses.

Types: front=0 back=1 box3d=2 logo=3 snap=4 (cart/cart3d retired from the UI -> excluded).
Layout: 'NAP1' + u32 count, then count*16B records {>I key, B type, 3x pad, >I off, >I size},
sorted by (key, type), followed by the raw .sprite streams.  All big-endian (N64).

Every stream is 16-byte aligned: libdragon's asset_read_header asserts (cur & 1) == 0,
and packing sprites back-to-back would push later streams onto odd offsets.  The menu
(src/menu/ui_components/boxart.c) memory-maps this: pak_open() reads the header once at
boot, the art index is derived from the record keys (no directory scan), and each cover
loads via fseek(offset) + asset_loadf.

Input:  ~/n64/boxart-stash/<CODE>/<type>.sprite   (CODE = 4-char DFS art key)
Output: ~/n64/boxart-stash/boxart.pak
Card:   copy to sd:/menu/boxart.pak, then delete sd:/menu/cache/artindex.bin to force a
        rebuild of the index from the new pak.
"""
import os, struct

STASH = os.path.expanduser("~/n64/boxart-stash")
OUT = os.path.join(STASH, "boxart.pak")
TYPES = {"front": 0, "back": 1, "box3d": 2, "logo": 3, "snap": 4}
ALIGN = 16

entries = []
for code in sorted(os.listdir(STASH)):
    d = os.path.join(STASH, code)
    if len(code) != 4 or not os.path.isdir(d) or code.startswith("._"):
        continue
    key = (ord(code[0]) << 24) | (ord(code[1]) << 16) | (ord(code[2]) << 8) | ord(code[3])
    for name, tid in TYPES.items():
        p = os.path.join(d, name + ".sprite")
        if os.path.isfile(p):
            entries.append((key, tid, p, os.path.getsize(p)))

entries.sort(key=lambda e: (e[0], e[1]))

# Header size, aligned; every stream offset kept on a 16-byte boundary.
hdr_size = 8 + 16 * len(entries)
hdr_size = (hdr_size + ALIGN - 1) // ALIGN * ALIGN
off = hdr_size
recs, blobs = [], []
for key, tid, p, sz in entries:
    recs.append(struct.pack(">IBBBBII", key, tid, 0, 0, 0, off, sz))
    blobs.append((p, sz))
    off += (sz + ALIGN - 1) // ALIGN * ALIGN

with open(OUT, "wb") as f:
    f.write(b"NAP1" + struct.pack(">I", len(entries)))
    f.write(b"".join(recs))
    f.write(b"\0" * (hdr_size - 8 - 16 * len(entries)))
    for p, sz in blobs:
        f.write(open(p, "rb").read())
        pad = (ALIGN - sz % ALIGN) % ALIGN
        f.write(b"\0" * pad)

codes = len({e[0] for e in entries})
print(f"boxart.pak: {len(entries)} sprites / {codes} game codes / {os.path.getsize(OUT):,} bytes")
