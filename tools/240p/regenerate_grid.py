#!/usr/bin/env python3
"""Regenerate the N64ever cartridge grid (favorites.ini) from the ROMs on the SD card.

Run this after adding/removing games:  python3 regenerate_grid.py
The card must be mounted at /Volumes/E64. Writes sd:/menu/n64ever/favorites.ini
(backing up the previous one to favorites.ini.bak).

Scans all folders except system ones, reads each ROM header for its game code
(which keys the cover art at sd:/menu/boxart/<CODE>/), dedupes the same game
appearing in multiple folders (All Roms copy preferred), sorts alphabetically.
"""
import os, re, struct, shutil

ROOT = '/Volumes/E64'
SKIP_DIRS = {'.Spotlight-V100', '.Trashes', '.fseventsd', 'ED64', 'ED64-IO', 'menu', 'NES', '64DD'}
CANON = {'Games A-M', 'Games N-Z', '00_playing'}
exts = ('.z64', '.n64', '.v64')

def game_code(path):
    try:
        with open(path, 'rb') as f:
            hdr = f.read(0x40)
        if len(hdr) < 0x40:
            return None
        magic = struct.unpack('>I', hdr[:4])[0]
        if magic == 0x80371240:
            raw = hdr
        elif magic == 0x37804012:
            raw = bytes(hdr[i ^ 1] for i in range(0x40))
        elif magic == 0x40123780:
            raw = b''.join(hdr[i:i+4][::-1] for i in range(0, 0x40, 4))
        else:
            return None
        code = raw[0x3B:0x3F].decode('ascii', 'replace')
        return code if all(32 < ord(c) < 127 for c in code) else None
    except OSError:
        return None

def strip_prefixes(t, in_list_folder):
    t = re.sub(r'^\d{4}-\d{2}-\d{2}\s+', '', t)      # release-date prefixes (Collections)
    if in_list_folder:
        t = re.sub(r'^\d{1,3}[\s._-]+', '', t)        # "001 " list numbering
    return t

def norm_title(fname, in_list_folder):
    t = strip_prefixes(os.path.splitext(fname)[0], in_list_folder)
    t = re.sub(r'\([^)]*\)', '', t)
    t = re.sub(r'\[[^\]]*\]', '', t)
    return re.sub(r'[^a-z0-9]+', '', t.lower())

best = {}
n_files = 0
for dirpath, dirnames, filenames in os.walk(ROOT):
    rel = os.path.relpath(dirpath, ROOT)
    top = rel.split(os.sep)[0]
    if top in SKIP_DIRS:
        dirnames[:] = []
        continue
    for fn in filenames:
        if fn.startswith('.') or not fn.lower().endswith(exts):
            continue
        n_files += 1
        full = os.path.join(dirpath, fn)
        code = game_code(full)
        if not code:
            continue
        in_list = top not in CANON and top != '.'
        key = (code, norm_title(fn, in_list))
        if top.startswith('Games '):
            prio = 0        # flattened canonical library
        elif top == '00_playing':
            prio = 1
        else:
            prio = 2        # Extras (hacks/homebrew/translations/protos/tools)
        title = strip_prefixes(os.path.splitext(fn)[0], True)
        sdpath = 'sd:/' + os.path.relpath(full, ROOT).replace(os.sep, '/')
        if key not in best or prio < best[key][0]:
            best[key] = (prio, title, sdpath, code)

entries = sorted(best.values(), key=lambda e: e[1].lower())
print(n_files, 'ROM files ->', len(entries), 'unique games')
assert len(entries) <= 2048, "menu favorites capacity is 2048"

def fmt_value(v):
    if not v:
        return ''
    if v[0] in ' \t' or v[-1] in ' \t' or any(c in v for c in ';#"\''):
        return '"' + v.replace('\\', '\\\\').replace('"', '\\"') + '"'
    return v

lines = ['[favorite]']
for i, (prio, title, sdpath, code) in enumerate(entries):
    lines.append(f'{i}_primary_path = {fmt_value(sdpath)}')
    lines.append(f'{i}_secondary_path = ')
    lines.append(f'{i}_type = 1')
    lines.append(f'{i}_game_code = {code}')

fav = ROOT + '/menu/n64ever/favorites.ini'
if os.path.exists(fav):
    shutil.copy(fav, fav + '.bak')
# Pre-size to a 32 KB multiple with trailing newlines: the menu (v26+) rewrites
# this file IN PLACE and never grows it, which keeps the ED64 V3 from touching the FAT.
data = ('\n'.join(lines) + '\n').encode('utf-8')
size = max(32768, (len(data) // 32768 + 1) * 32768)
with open(fav, 'wb') as f:
    f.write(data + b'\n' * (size - len(data)))
os.sync() if hasattr(os, 'sync') else None
print('favorites.ini written:', len(entries), 'entries')
