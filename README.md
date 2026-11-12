# N64 240p Menu

A game-picking front end for the Nintendo 64, built to run in **true 240p progressive scan**
on a CRT. It replaces the EverDrive's stock file list with a carousel of cover art, gameplay
screenshots, descriptions and mood shelves, all readable from the couch.

Built for an **EverDrive-64 V3** (2014 bootloader v4) on a Sony Trinitron. It's a fork of
[N64ever](https://github.com/bjerreman/N64FlashcartMenu-N64ever), the cover-art front end for
[N64FlashcartMenu](https://github.com/Polprzewodnikowy/N64FlashcartMenu).

Case study: [mitchellv.me/work/n64-240p-menu](https://www.mitchellv.me/work/n64-240p-menu)

## What's different from N64ever

- **240p output.** 640×240, non-interlaced, 16-bit, no filtering. Every vertical layout budget
  is halved, art is drawn at half height so the CRT's tall pixels make it look right, and the
  fonts are pre-stretched to double width so text keeps its shape.
- **Carousel home screen.** Covers scroll along the top half; the selected game's screenshot
  and description fill the bottom half.
- **Mood shelves.** All, Homebrew, Party, Racing, Action, Adventure, Sports and Chill.
- **Small ROM, art on the SD card.** Cover art and screenshots load from one indexed file
  (`sd:/menu/boxart.pak`), so the menu itself stays under the V3 bootloader's size limit.
- **Starts on the last game played**, with a pinned 240p test suite tile and save badges.

## Controls

| Button | Action |
| --- | --- |
| A | Play |
| B | File browser |
| L | Next shelf |
| Z | Random game |
| C-up / C-down | Cycle shelves |
| R | Menu |

## Building

You need the libdragon toolchain (`N64_INST` set) and this repo's submodules:

```
git submodule update --init --recursive
make ed64 BOXART_PNGS=
```

The output is `output/OS64.v64`. Copy it to `ED64/OS64.v64` on the SD card.

### EverDrive-64 V3 rules

The V3's bootloader copies the menu into the cart's internal flash on boot. From testing:

1. **Keep the ROM at or under 2,048,000 bytes.** Larger files fail with ERROR:91.
2. **Every new build must differ in size from the one already flashed.** The bootloader skips
   same-size files. The ROM pads in 16 KB steps, so pad `filesystem/version.txt` to cross a
   boundary.
3. **ERROR:91 right after copying a new build is often transient.** Power-cycle a few times
   before changing anything.

The full investigation is in [docs/240p/ed64-v3-bootloader-writeup.md](docs/240p/ed64-v3-bootloader-writeup.md).

### libdragon patch

[patches/libdragon-fat-int-err.patch](patches/libdragon-fat-int-err.patch) makes FatFS's
`FR_INT_ERR` an `EIO` error instead of an assertion, so a bad read fails one file instead of
crashing the menu.

## Art and games

**No ROMs, box art or screenshots are included.** The scripts in [tools/240p](tools/240p)
build the SD-side art pak, bake screenshots, make the custom romhack covers and generate the
curated shelf from your own card.

## Credits and licence

N64FlashcartMenu by NetworkFusion and Polprzewodnikowy, N64ever by bjerreman, built on
[libdragon](https://github.com/DragonMinded/libdragon). Licensed under the GNU AGPL v3, like
upstream; see [LICENSE.md](LICENSE.md), [README-upstream.md](README-upstream.md) and
[README-n64ever.md](README-n64ever.md) for third-party licences.
