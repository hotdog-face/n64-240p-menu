#!/usr/bin/env python3
"""Build a CURATED cartridge grid: only fun-today, playable-by-modern-standards games.

Writes sd:/menu/n64ever/favorites.ini from the curated list below, matched against
the ROMs actually on the card. Run regenerate_grid.py instead to restore the
full-library grid. Curation is by normalized-title matching; edit CURATED to taste.
"""
import os, re, struct, shutil

ROOT = '/Volumes/E64'
SKIP_DIRS = {'.Spotlight-V100', '.Trashes', '.fseventsd', 'ED64', 'ED64-IO', 'menu', 'NES', '64DD'}
exts = ('.z64', '.n64', '.v64')

# Curated canon + hidden gems + the best of the owned hacks/translations.
# Each entry is a normalized title (lowercase alnum). Comments give the real name.
CURATED = """
supermario64            | Super Mario 64
banjokazooie            | Banjo-Kazooie
banjotooie              | Banjo-Tooie
donkeykong64            | Donkey Kong 64
conkersbadfurday        | Conker's Bad Fur Day
kirby64thecrystalshards | Kirby 64
rocketrobotonwheels     | Rocket: Robot on Wheels
spacestationsiliconvalley | Space Station Silicon Valley
mischiefmakers          | Mischief Makers
rayman2thegreatescape   | Rayman 2
goemonsgreatadventure   | Goemon's Great Adventure
mysticalninjastarringgoemon | Mystical Ninja Starring Goemon
yoshisstory             | Yoshi's Story
bomberman64             | Bomberman 64
bomberman64thesecondattack | Bomberman 64: The Second Attack
bombermanhero           | Bomberman Hero
glover                  | Glover
legendofzeldatheocarinaoftime | Zelda: Ocarina of Time
legendofzeldathemajorasmask | Zelda: Majora's Mask
papermario              | Paper Mario
shadowman               | Shadow Man
bodyharvest             | Body Harvest
ogrebattle64personoflordlycaliber | Ogre Battle 64
harvestmoon64           | Harvest Moon 64
mariokart64             | Mario Kart 64
diddykongracing         | Diddy Kong Racing
fzerox                  | F-Zero X
waverace64kawasakijetski | Wave Race 64
waverace64              | Wave Race 64 (alt name)
beetleadventureracing   | Beetle Adventure Racing
excitebike64            | Excitebike 64
hydrothunder            | Hydro Thunder
sanfranciscorush2049    | Rush 2049
sanfranciscorushextremeracing | SF Rush
rush2extremeracingusa   | Rush 2
rr64ridgeracer64        | Ridge Racer 64
starwarsepisodeiracer   | Episode I Racer
wipeout64               | Wipeout 64
topgearrally            | Top Gear Rally
topgearoverdrive        | Top Gear Overdrive
worlddriverchampionship | World Driver Championship
stuntracer64            | Stunt Racer 64
snowboardkids           | Snowboard Kids
snowboardkids2          | Snowboard Kids 2
1080teneightysnowboarding | 1080 Snowboarding
1080snowboarding        | 1080 (alt)
cruisnworld             | Cruis'n World
mickeysspeedwayusa      | Mickey's Speedway USA
micromachines64turbo    | Micro Machines 64
goldeneye007            | GoldenEye 007
perfectdark             | Perfect Dark
turokdinosaurhunter     | Turok
turok2seedsofevil       | Turok 2
doom64                  | Doom 64
quakeii                 | Quake II
starwarsroguesquadron   | Rogue Squadron
starwarsepisodeibattlefornaboo | Battle for Naboo
starwarsshadowsoftheempire | Shadows of the Empire
starfox64               | Star Fox 64
jetforcegemini          | Jet Force Gemini
forsaken64              | Forsaken 64
winback covertoperations | WinBack
winbackcovertoperations | WinBack (alt)
supersmashbros          | Super Smash Bros.
killerinstinctgold      | Killer Instinct Gold
wwfnomercy              | WWF No Mercy
wwfwrestlemania2000     | WrestleMania 2000
mortalkombattrilogy     | MK Trilogy
rakugakids              | Rakuga Kids
fightersdestiny         | Fighters Destiny
nflblitz                | NFL Blitz
nbahangtime             | NBA Hangtime
mariotennis             | Mario Tennis
mariogolf               | Mario Golf
kengriffeyjrsslugfest   | Ken Griffey Slugfest
internationalsuperstarsoccer64 | ISS 64
internationalsuperstarsoccer98 | ISS 98
waynegretzkys3dhockey98 | Gretzky Hockey 98
waynegretzkys3dhockey   | Gretzky Hockey
tonyhawksproskater      | THPS
tonyhawksproskater2     | THPS 2
tonyhawksproskater3     | THPS 3
tetrisphere             | Tetrisphere
newtetristhe            | The New Tetris
drmario64               | Dr. Mario 64
pokemonpuzzleleague     | Pokemon Puzzle League
wetrix                  | Wetrix
bustamove2arcadeedition | Bust-A-Move 2
bustamove99             | Bust-A-Move 99
marioparty2             | Mario Party 2
marioparty3             | Mario Party 3
magicaltetrischallenge  | Magical Tetris Challenge
pokemonsnap             | Pokemon Snap
pokemonstadium          | Pokemon Stadium
pokemonstadium2         | Pokemon Stadium 2
pilotwings64            | Pilotwings 64
blastcorps              | Blast Corps
starcraft64             | StarCraft 64
infishermanbasshunter64 | In-Fisherman Bass Hunter (novelty: fishing)
"""

# Distinctive substring matches for hacks / translations / homebrew (normalized).
CURATED_SUBSTR = """
animalforestdoubutsunomori | Animal Forest (English) — cozy novelty
sinandpunishmentjenglish   | (unused; see below)
sinandpunishment           | Sin and Punishment (English)
denshadego64               | Densha de Go! 64 (English) — train sim novelty
customroboteng             | Custom Robo (English)
fushiginodungeon           | Shiren the Wanderer 2 (English)
puyopuyosun64              | Puyo Puyo Sun 64 (English)
viewpoint2064              | Viewpoint 2064 (English)
chameleontwistteng         | Chameleon Twist JP (English)
virtualprowrestling2       | Virtual Pro Wrestling 2 (English)
goldeneyexv                | GoldenEye X
fzeroxclimax               | F-Zero X Climax (hack)
fzeroxexpansionpack        | F-Zero X Expansion Pack (hack)
smashremix                 | Smash Remix
ampedup                    | Mario Kart 64 Amped Up
pyoro64                    | Pyoro 64 (homebrew)
flappybirdv                | Flappy Bird 64 (homebrew)
64doomdoomport             | 64Doom (homebrew)
sm64lastimpact             | SM64: Last Impact (Kaze)
supermariostarroadenhanced | Star Road Enhanced Console Port (Kaze)
n64brewgamejam2024         | N64brew Jam 2024 (21 minigames)
"""

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

def norm(t):
    t = re.sub(r'^\d{4}-\d{2}-\d{2}\s+', '', t)
    t = re.sub(r'\([^)]*\)', '', t)
    t = re.sub(r'\[[^\]]*\]', '', t)
    return re.sub(r'[^a-z0-9]+', '', t.lower())

exact = {}
for line in CURATED.strip().splitlines():
    key = line.split('|')[0].strip()
    if key:
        exact[key] = None
subs = []
for line in CURATED_SUBSTR.strip().splitlines():
    key = line.split('|')[0].strip()
    if key and 'unused' not in line:
        subs.append([key, None])

# US-preferred file matching (flat layout: (USA) files outrank other regions by name tag)
def prio_of(rel):
    parts = rel.split('/')
    top = parts[0]
    base = 0 if top.startswith('Games ') else (2 if top == '00_playing' else 4)
    fname = parts[-1]
    return base + (0 if '(USA)' in fname or '(Japan, USA)' in fname or '(World)' in fname else 1)

for dirpath, dirnames, filenames in os.walk(ROOT):
    rel = os.path.relpath(dirpath, ROOT)
    if rel.split(os.sep)[0] in SKIP_DIRS:
        dirnames[:] = []
        continue
    for fn in filenames:
        if fn.startswith('.') or not fn.lower().endswith(exts):
            continue
        full = os.path.join(dirpath, fn)
        n = norm(os.path.splitext(fn)[0])
        relp = os.path.relpath(full, ROOT).replace(os.sep, '/')
        p = prio_of(relp)
        if n in exact and (exact[n] is None or p < exact[n][0]):
            exact[n] = (p, full, relp)
        for e in subs:
            if e[0] in n and (e[1] is None or p < e[1][0]):
                e[1] = (p, full, relp)

picked = {}
missed = []
for key, hit in exact.items():
    if hit:
        picked[hit[2]] = hit[1]
    else:
        missed.append(key)
for key, hit in [(e[0], e[1]) for e in subs]:
    if hit:
        picked[hit[2]] = hit[1]
    else:
        missed.append(key + ' (substr)')

entries = []
for relp, full in picked.items():
    code = game_code(full) or ''   # homebrew may have a blank/invalid code; entry still valid
    title = norm(os.path.basename(relp))
    entries.append((title, 'sd:/' + relp, code))
entries.sort()

# Full-grid mode: only include games that have cover art (any region variant).
STASH = os.path.expanduser('~/n64/boxart-stash')
def has_art(code):
    if not code:
        return False
    if os.path.isdir(os.path.join(STASH, code)):
        return True
    for r in 'EPJUAXFDIS':
        if r != code[3] and os.path.isdir(os.path.join(STASH, code[:3] + r)):
            return True
    return False

dropped = [t for (t, p, c) in entries if not has_art(c)]
entries = [(t, p, c) for (t, p, c) in entries if has_art(c)]
if dropped:
    print(f'{len(dropped)} artless games hidden from grid (file browser only):')
    for t in dropped:
        print('  -', t)

def fmt_value(v):
    if not v:
        return ''
    if v[0] in ' \t' or v[-1] in ' \t' or any(c in v for c in ';#"\''):
        return '"' + v.replace('\\', '\\\\').replace('"', '\\"') + '"'
    return v

lines = ['[favorite]']
for i, (title, sdpath, code) in enumerate(entries):
    lines.append(f'{i}_primary_path = {fmt_value(sdpath)}')
    lines.append(f'{i}_secondary_path = ')
    lines.append(f'{i}_type = 1')
    if code:
        lines.append(f'{i}_game_code = {code}')

fav = ROOT + '/menu/n64ever/favorites.ini'
if os.path.exists(fav):
    shutil.copy(fav, fav + '.bak')
with open(fav, 'w') as f:
    f.write('\n'.join(lines) + '\n')
print(f'CURATED grid written: {len(entries)} games')
if missed:
    print(f'{len(missed)} curated titles not found on card:')
    for m in missed:
        print('  -', m)
