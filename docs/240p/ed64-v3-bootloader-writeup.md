# EverDrive-64 V3: ERROR:91 root cause, plus a true-240p conversion of N64ever

Hi! I spent a day debugging N64ever on real EverDrive-64 V3 hardware (2014 cart,
bootloader v4, Sony WEGA CRT) and ended up with two things that may be useful to
this project: the root cause of the intermittent `ERROR:91` boot failures on
V-series carts, and a working true-240p (progressive scan) conversion of the menu.
Patchset attached (`n64ever-240p-patchset.diff`, 14 files); details below.

## Part 1 — ERROR:91 on ED64 V3 is a bootloader load-size problem

**Symptom:** flashing a built `OS64.v64` sometimes produces
`EverDrive-64 bootloader v4 / ERROR:91 / Unexpected error` at power-on. Whether a
given build boots is deterministic per file, but flips unpredictably between
near-identical builds — it looks haunted (we initially blamed a video-mode change;
that was a red herring).

**Root cause:** the 2014 V-series bootloader cannot reliably load a ~46 MB OS file
(the size of an N64ever build with ~3160 baked box-art sprites in the DFS). It was
designed around OS files of ~1–2 MB. With a 46 MB file, depending on where the
bytes land, boots either fail outright with ERROR:91 (before a single instruction
of the menu executes), or occasionally load a silently-corrupted image that
crashes later in bizarre ways (we captured one as a libdragon inspector backtrace
dying inside `dfs_open → lookup_file` on a garbage lookup table).

**Evidence:**
- Instrumented builds wrote a checkpoint log to SD; on ERROR:91 boots the log shows
  the menu never started (first log write is ~250 ms into `menu_init`).
- Two byte-identical copies of the same 46 MB build behaved deterministically
  differently (one booted 2/2, the other failed 15/15) after being rebuilt with
  trivial code changes — content placement, not content meaning.
- Raw integrity probes (DMA + checksum at 8 offsets across the image, verified
  against host-computed sums, at four points during boot) pass 100% on boots that
  succeed — including offsets past 32 MB — so when the load works, it works fully.
- The same menu built at 1.9 MB (art removed from DFS): 100% boot success across
  every power cycle tried, on the same cart, same SD card, same code.

**Fix that works:** keep `OS64.v64` small. I moved the baked art out of the ROM and
load it from the SD card instead — `DFS_BOXART_DIRECTORY` becomes
`sd:/menu/boxart` and the baked `<CODE>/<type>.sprite` files are copied to the card
(`sprite_load()` works on `sd:/` paths unchanged; compression level 3 assets load
fine since `asset_init_compression(3)` is already registered). Menu shrinks to
~1.9 MB, boots reliably, art streams on demand with the existing async machinery.
A cleaner upstream version would probably thread the runtime storage prefix into
the boxart component instead of hardcoding `sd:` (iQue/emulator paths).

This likely explains any V-series ERROR:91 reports you've had: the failure
probability grows with ROM size, and every art-pack addition makes it worse.

## Part 2 — true 240p (progressive) mode for CRTs

480i on a consumer CRT makes the menu text shimmer badly. The N64 can output
240p, and the menu works beautifully in it. The patchset converts the menu to
640×240 `INTERLACE_OFF` + `FILTERS_DISABLED` (crisp, zero flicker on a Trinitron):

- `menu.c`: `display_init(640×240, INTERLACE_OFF, FILTERS_DISABLED)`. Note the
  original code paired `INTERLACE_OFF` with `FILTERS_RESAMPLE` via
  `force_progressive_scan`; `FILTERS_DISABLED` is the right companion at 640 wide
  (the width-≤320 VI bug doesn't apply).
- `constants.h`: all vertical layout budgets halved (a full-screen 240p pixel is
  double-tall): `DISPLAY_HEIGHT 240`, overscan, separator, list row counts, etc.
  BOXART_* size constants stay at native values (they double as source-size
  detectors), only positions/budgets change.
- **Anisotropic art drawing**: every sprite/art blit draws at
  `scale_y = scale_x / 2` so covers/carts keep correct proportions on screen.
  The uniform aspect-fit idiom becomes: fit against `(cell_w, cell_h * 2)`, then
  halve the Y scale. ~13 draw sites across the grid, inspect, screensaver,
  placeholders, boot screens, history thumbnails, splash. Two sites were in RDP
  copy mode (which can't scale) and move to standard mode.
- **Fonts**: the interesting bit. Any font renders double-tall at 240p, so the
  TTFs are pre-stretched 2× *horizontally* (fontTools: `glyf` x-coords ×2,
  `hmtx` advances/LSBs ×2, kern/GPOS dropped) before `mkfont`, and baked at
  `--size 9` instead of 15. Result: normal-proportioned, very readable text on a
  real CRT. Script:

```python
from fontTools.ttLib import TTFont
f = TTFont("Firple-Bold.ttf"); glyf = f["glyf"]
for name in glyf.keys():
    g = glyf[name]
    if g.isComposite():
        for c in g.components: c.x *= 2
    elif g.numberOfContours > 0:
        coords, ends, flags = g.getCoordinates(glyf)
        for i in range(len(coords)): coords[i] = (coords[i][0]*2, coords[i][1])
        g.coordinates = coords
hm = f["hmtx"]
for n in hm.metrics: a,l = hm.metrics[n]; hm.metrics[n] = (a*2, l*2)
for t in ("kern","GPOS"):
    if t in f: del f[t]
f.save("Firple-Bold.ttf")
```

- Overscan calibrated for consumer CRTs (`OVERSCAN_HEIGHT 18`, separator at 190 so
  the actions bar keeps two text lines inside the tube's visible area).

Tested on: ED64 V3 (bootloader v4) + NTSC N64 + Sony KV-27 WEGA. Grid, browser,
file info, ROM launch, screensaver marquee and a 1,150-game library all working.

Happy to answer questions or split this into separate PRs (small-ROM/SD-art vs
240p) if you're interested in either half.

— Mitchell (mitchellv.me), with hardware testing on the ED64 V3 + WEGA setup
described above. Thanks for building N64ever — the grid is exactly what a CRT
corner needed.
