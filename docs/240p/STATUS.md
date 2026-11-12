# Status and handoff

Where the project stands, what's open, and how to pick it up. Last updated 2026-11-12.

## Current build: v28

| Build | Size (bytes) | State |
| --- | --- | --- |
| v25 | 1,982,464 | Long-running build. Rewrote `history.ini` on every launch, which corrupted the SD card on the ED64 V3. |
| v26 | 1,884,160 | **Verified on hardware:** in-place INI saves no longer touch the FAT; game title, 2-line description, SAVE tag. |
| v27 | 1,916,928 | Played on hardware. Fixed menu highlight drift and single-B exit from the file popup. Exposed the blank file-popup rows. |
| v28 | 1,933,312 | **Not yet run on hardware.** File-popup rows visible again, header rule under the text, Sports shelf removed, black-box log. |

Every build must be ≤ 2,048,000 bytes and a different size from the build already flashed
(see the README). `filesystem/` is generated and git-ignored; pad the ROM by putting a file of
spaces at `filesystem/size_pad.txt` (v28 used 40,960 bytes), then `rm build/*.dfs` and rebuild.

## Open issues

1. **C-down acts like B, and B moves the cursor in menus.** The menu never maps C-down to back
   (`src/menu/actions.c`), so the likely cause is the BlueRetro Bluetooth adapter's button map.
   v28's black box logs raw button presses; press each button once in order
   (A, B, Z, L, R, Start, C-up, C-down, C-left, C-right, D-pad up/down/left/right) and read
   the `btn` lines.
2. **Mario Kart 64: Amped Up v2.92 doesn't start.** The menu loads it and hands off (it's in
   `history.ini`), and it has a save from earlier play. v28 logs a `launch:` line with CIC,
   save type and Expansion Pak flags. Suspects: save type from the MK64 game code (`NKTE`),
   Expansion Pak requirement.
3. **v28 is untested.** Check: status line says v28, file popup shows names, the All shelf
   shows all 90 games (the stale art index was deleted so it rebuilds from the pak).

## Black-box log (v28+)

The console logs only if `sd:/menu/blackbox.txt` already exists. Create it on a computer as a
fixed-size file of newlines:

```
python3 -c "open('/Volumes/E64/menu/blackbox.txt','wb').write(b'\n'*65536)"
```

Each boot overwrites it from the start, in place, without growing it, so the FAT is never
touched. It records boot directory probes, art pak/index results, file-popup folder reads,
every launch, and the first 300 button presses. Delete the file to turn logging off.

## Rules learned the hard way (ED64 V3)

- **Never let the console grow or create files during normal use.** FAT allocation from the
  console cross-linked `history.ini` into `/ED64` and wiped the OS file. INI saves now rewrite
  in place at a fixed size (`ini_save` in `src/menu/ini_parser.c`); files are pre-sized to
  32 KB of trailing newlines. A save file is still created the first time each game runs.
- **Text boxes must be at least 15 px tall** with PixelMplus 12 (ascent 11, descent 3).
  libdragon silently skips a line that doesn't fully fit its `.height`.
- **ERROR:91 after flashing** is usually transient: power-cycle a few times.
- **Card writes from macOS must be serial**, and the card must be unmounted with
  `diskutil unmount` (not just `sync`) before it's pulled.
- **Keep `sd:/menu/n64ever/.migrated.v1`** on the card, or the menu wipes favorites on boot.

## Card layout this build expects

```
ED64/OS64.v64                 the menu (this repo's output/OS64.v64)
menu/config.ini               settings (pre-sized 32 KB)
menu/n64ever/favorites.ini    the curated shelf (pre-sized 32 KB)
menu/n64ever/history.ini      recently played (pre-sized 32 KB)
menu/n64ever/.migrated.v1     0-byte marker, required
menu/boxart.pak               indexed cover/screenshot art (tools/240p/build_pak.py)
menu/boxart/<shard>/<CODE>/   per-game art fallback
menu/blackbox.txt             optional diagnostic log (see above)
00_playing/, Extras/          homebrew, hacks, translations (the Homebrew shelf)
Games A-M/, Games N-Z/        retail library
```

## Picking it up

- Toolchain: libdragon's `tools/build-toolchain.sh` (GCC 14.2, needs Homebrew on macOS), then
  build and install the `libdragon` submodule with `patches/libdragon-fat-int-err.patch`
  applied: `make -j && make install && make tools && make tools-install`.
- Build: `make ed64 -j BOXART_PNGS=` → `output/OS64.v64`.
- Shelf edits are card-side data (`favorites.ini`), no rebuild needed; shelf categories are
  keyword lists near `SHELF_*` in `src/menu/views/games_grid.c`.
