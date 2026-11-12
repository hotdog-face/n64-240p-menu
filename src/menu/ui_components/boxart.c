/**
 * @file boxart.c
 * @brief Implementation of the boxart UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>
#include <string.h>

#include "../ui_components.h"
#include "../path.h"
#include "../png_decoder.h"
#include "../settings.h"
#include "../game_special.h"
#include "constants.h"
#include "utils/fs.h"

#define OLD_BOXART_DIRECTORY       "menu/boxart"
#define METADATA_BASE_DIRECTORY    "menu/metadata"
#define HOMEBREW_ID_SUBDIRECTORY   "homebrew"

/* Path and source-PNG size for the cache write pending after a decode completes */
static char     pending_cache_path[256];
static uint32_t pending_png_size;

/* Cache file header written/read before the raw pixel data.
   png_size is the byte length of the source PNG; if it changes the cache is stale. */
typedef struct {
    uint32_t png_size;
    uint16_t width;
    uint16_t height;
    uint32_t stride;
} boxart_cache_header_t;

/* Derive cache path by replacing trailing ".png" with ".cache" */
static void make_cache_path(char *out, size_t out_size, const char *png_path) {
    strncpy(out, png_path, out_size - 1);
    out[out_size - 1] = '\0';
    size_t len = strlen(out);
    if (len > 4 && memcmp(out + len - 4, ".png", 4) == 0) {
        if (len - 4 + 6 < out_size) {
            memcpy(out + len - 4, ".cache", 7); /* includes '\0' */
        }
    }
}

/* Get file size in bytes (returns 0 on error) */
static uint32_t get_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fclose(f);
    return (s > 0) ? (uint32_t)s : 0;
}

/* Load a previously cached raw surface.
   png_size is the current PNG byte count — cache is rejected if it differs. */
static surface_t *load_cache(const char *cache_path, uint32_t png_size) {
    FILE *f = fopen(cache_path, "rb");
    if (!f) return NULL;

    boxart_cache_header_t hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return NULL; }

    /* Reject stale cache (PNG was replaced) */
    if (hdr.png_size != png_size) { fclose(f); return NULL; }

    surface_t *img = calloc(1, sizeof(surface_t));
    if (!img) { fclose(f); return NULL; }

    *img = surface_alloc(FMT_RGBA16, hdr.width, hdr.height);
    if (!img->buffer) { free(img); fclose(f); return NULL; }

    if (img->stride != hdr.stride) {
        surface_free(img); free(img); fclose(f); return NULL;
    }

    size_t data_size = (size_t)hdr.stride * hdr.height;
    if (fread(img->buffer, 1, data_size, f) != data_size) {
        surface_free(img); free(img); fclose(f); return NULL;
    }

    fclose(f);
    return img;
}

/* Write decoded surface to cache file alongside source PNG size for future validation */
static void write_cache(const char *cache_path, const surface_t *img, uint32_t png_size) {
    FILE *f = fopen(cache_path, "wb");
    if (!f) return;

    boxart_cache_header_t hdr = {
        .png_size = png_size,
        .width    = img->width,
        .height   = img->height,
        .stride   = img->stride,
    };
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(img->buffer, 1, (size_t)img->stride * img->height, f);
    fclose(f);
}

/**
 * @brief PNG decoder callback for boxart image loading.
 */
static void png_decoder_callback(png_err_t err, surface_t *decoded_image, void *callback_data) {
    component_boxart_t *b = (component_boxart_t *)(callback_data);
    b->loading = false;
    b->image = decoded_image;

    if (err == PNG_OK && decoded_image && pending_cache_path[0]) {
        write_cache(pending_cache_path, decoded_image, pending_png_size);
        pending_cache_path[0] = '\0';
        pending_png_size = 0;
    }
}

/* Baked cover sprites now live on the SD card (sd:/menu/boxart/<CODE>/<type>.sprite)
   instead of inside the ROM's DFS: the EverDrive-64 V3 bootloader (2014) cannot
   reliably load an OS file bigger than the ~2MB it was designed around, so the 44MB
   art pack had to leave the ROM. sprite_load() works identically on sd:/ paths.
   NOTE: hardcodes the sd: prefix (fine for ED64/SC64; iQue+emulator would need the
   runtime storage prefix threaded through here). */
#define DFS_BOXART_DIRECTORY    "sd:/menu/boxart"
/* Art library is SHARDED by the code's 2nd character (boxart/<c1>/<CODE>/...):
   FAT name lookups are linear directory scans over the EverDrive's slow SD PIO,
   so one 767-entry directory made EVERY cover-open pay a multi-hundred-sector
   scan tax (and enumeration effectively quadratic -> the minutes-long freeze).
   With shards every directory on the path is small. */
#define ART_SHARD_FMT           DFS_BOXART_DIRECTORY "/%c/%c%c%c%c/%s.sprite"

/* Map a grid image-view setting (settings.h grid_image_view_t) to a loader type. */
file_image_type_t ui_components_boxart_view_to_type(int view) {
    switch (view) {
        case GRID_IMAGE_BOX_BACK:   return IMAGE_BOXART_BACK;
        case GRID_IMAGE_BOX_3D:     return IMAGE_BOXART_3D;
        case GRID_IMAGE_CART_FRONT: return IMAGE_GAMEPAK_FRONT;
        case GRID_IMAGE_CART_3D:    return IMAGE_GAMEPAK_3D;
        case GRID_IMAGE_LOGO:       return IMAGE_LOGO;
        case GRID_IMAGE_SCREENSHOT: return IMAGE_SCREENSHOT;
        default:                    return IMAGE_BOXART_FRONT;
    }
}

/* Logical media type -> ROM DFS sprite basename (NULL = no baked art for type). */
static const char *dfs_type_name(file_image_type_t t) {
    switch (t) {
        case IMAGE_BOXART_FRONT:  return "front";
        case IMAGE_BOXART_BACK:   return "back";
        case IMAGE_BOXART_3D:     return "box3d";
        case IMAGE_GAMEPAK_FRONT: return "cart";
        case IMAGE_GAMEPAK_3D:    return "cart3d";
        case IMAGE_LOGO:          return "logo";
        case IMAGE_SCREENSHOT:    return "snap";
        default:                  return NULL;
    }
}

/* Custom-folder PNG filename suffix per type (see /menu/n64ever/gameconfigs/). */
static const char *custom_type_suffix(file_image_type_t t) {
    switch (t) {
        case IMAGE_BOXART_FRONT:  return "-front";
        case IMAGE_BOXART_BACK:   return "-back";
        case IMAGE_BOXART_3D:     return "-3dbox";
        case IMAGE_GAMEPAK_FRONT: return "-cart";
        case IMAGE_GAMEPAK_3D:    return "-3dcart";
        case IMAGE_LOGO:          return "-logo";
        case IMAGE_SCREENSHOT:    return "-snap";
        default:                  return NULL;
    }
}

/* SD-card metadata-folder PNG filename per type (NULL = no SD equivalent). */
static const char *metadata_type_filename(file_image_type_t t) {
    switch (t) {
        case IMAGE_GAMEPAK_FRONT: return "gamepak_front.png";
        case IMAGE_GAMEPAK_BACK:  return "gamepak_back.png";
        case IMAGE_BOXART_BACK:   return "boxart_back.png";
        case IMAGE_BOXART_LEFT:   return "boxart_left.png";
        case IMAGE_BOXART_RIGHT:  return "boxart_right.png";
        case IMAGE_BOXART_BOTTOM: return "boxart_bottom.png";
        case IMAGE_BOXART_TOP:    return "boxart_top.png";
        case IMAGE_BOXART_FRONT:  return "boxart_front.png";
        default:                  return NULL; /* 3d box, 3d cart, logo: DFS/custom only */
    }
}

/* Fallback cascade per requested type. Product spec:
   box types prefer front > 3D box > back, then cart > 3D cart, then logo;
   cart types prefer cart > 3D cart > logo, then box; logo prefers
   logo > front > 3D box > back > cart > 3D cart. */
static const file_image_type_t *fallback_chain(file_image_type_t t, int *n) {
    /* Cartridge art fully retired per user preference: chains are box/logo/snap only. */
    static const file_image_type_t FRONT[]  = { IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK, IMAGE_LOGO };
    static const file_image_type_t BACK[]   = { IMAGE_BOXART_BACK, IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_LOGO };
    static const file_image_type_t BOX3D[]  = { IMAGE_BOXART_3D, IMAGE_BOXART_FRONT, IMAGE_BOXART_BACK, IMAGE_LOGO };
    static const file_image_type_t CART[]   = { IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK, IMAGE_LOGO };
    static const file_image_type_t CART3D[] = { IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK, IMAGE_LOGO };
    static const file_image_type_t LOGO[]   = { IMAGE_LOGO, IMAGE_BOXART_FRONT, IMAGE_BOXART_3D, IMAGE_BOXART_BACK };
    static const file_image_type_t SNAP[]   = { IMAGE_SCREENSHOT };   /* screenshot or nothing:
        falling back to covers made the carousel's bottom panel redundant with the covers above */
    *n = 4;
    switch (t) {
        case IMAGE_BOXART_BACK:   return BACK;
        case IMAGE_BOXART_3D:     return BOX3D;
        case IMAGE_GAMEPAK_FRONT: return CART;
        case IMAGE_GAMEPAK_3D:    return CART3D;
        case IMAGE_LOGO:          return LOGO;
        case IMAGE_SCREENSHOT:    *n = 1; return SNAP;
        default:                  return FRONT;
    }
}

/* Attempt to load a PNG (cache-first, then decode). Returns true if the component
   was satisfied (image cached, or async decode started). */
static bool try_png(component_boxart_t *b, char *png_path, int max_w, int max_h) {
    uint32_t png_sz = get_file_size(png_path);
    if (png_sz == 0) return false;
    char cache_path[400];
    make_cache_path(cache_path, sizeof(cache_path), png_path);
    surface_t *cached = load_cache(cache_path, png_sz);
    if (cached) { b->loading = false; b->image = cached; return true; }
    if (png_decoder_start(png_path, max_w, max_h, png_decoder_callback, b) == PNG_OK) {
        strncpy(pending_cache_path, cache_path, sizeof(pending_cache_path) - 1);
        pending_cache_path[sizeof(pending_cache_path) - 1] = '\0';
        pending_png_size = png_sz;
        return true;
    }
    return false;
}

/* In-RAM index of which <CODE> art directories exist on the SD card, built once on
   first use. Failed art lookups previously cost a full FAT directory scan of the
   ~770-entry sd:/menu/boxart folder over the EverDrive's slow SD interface — dozens
   of scans per artless game (region candidates x fallback chain) locked the grid for
   minutes. With the index, a miss costs zero I/O. */
#define ART_INDEX_MAX 2048
static uint32_t art_index[ART_INDEX_MAX];
static int art_index_n = -1;   /* -1 = not built yet */

static uint32_t art_code_key (char a, char b, char c, char d) {
    return ((uint32_t)(uint8_t)a << 24) | ((uint32_t)(uint8_t)b << 16) |
           ((uint32_t)(uint8_t)c << 8)  |  (uint32_t)(uint8_t)d;
}

static int art_key_cmp (const void *x, const void *y) {
    uint32_t a = *(const uint32_t *)x, b = *(const uint32_t *)y;
    return (a > b) - (a < b);
}

/* -- boxart.pak: single indexed archive of the whole art library --------------
   One fopen at boot replaces ~2,900 per-file FAT lookups over slow ED64 SD PIO.
   Big-endian, sorted by (code key, type) for bsearch; sprite streams follow the
   record table and are loaded via fseek + asset_loadf (which reads from the
   FILE's current position -- traced in libdragon asset.c: asset_loadf ->
   asset_loadfd reads at the fd's current offset with *sz as the stream size).
   If the pak is absent or malformed, every path below falls back to the
   sharded per-file directories -- the pak is an accelerator, not a dependency. */
#define ART_PAK_PATH  "sd:/menu/boxart.pak"
#define ART_PAK_MAGIC 0x4E415031   /* 'NAP1' */
#define ART_PAK_MAX_ENTRIES 16384  /* sanity bound on header count */

typedef struct __attribute__((packed)) {
    uint32_t key;      /* game code, art_code_key() packing */
    uint8_t  type;     /* 0 front, 1 back, 2 box3d, 3 logo, 4 snap */
    uint8_t  pad[3];
    uint32_t offset;   /* absolute file offset of sprite stream */
    uint32_t size;     /* stream size in bytes */
} pak_rec_t;

static FILE *pak_file = NULL;
static pak_rec_t *pak_recs = NULL;
static int pak_n = -1;   /* -1 = not tried, 0 = absent/failed, >0 = entry count */

static int pak_type_id (file_image_type_t t) {
    switch (t) {
        case IMAGE_BOXART_FRONT: return 0;
        case IMAGE_BOXART_BACK:  return 1;
        case IMAGE_BOXART_3D:    return 2;
        case IMAGE_LOGO:         return 3;
        case IMAGE_SCREENSHOT:   return 4;
        default:                 return -1;   /* cart/cart3d: retired, not packed */
    }
}

static void pak_open (void) {
    if (pak_n >= 0) return;
    pak_n = 0;
    FILE *f = fopen(ART_PAK_PATH, "rb");
    if (!f) return;
    uint32_t hdr[2];
    if (fread(hdr, sizeof(uint32_t), 2, f) != 2 ||
        hdr[0] != ART_PAK_MAGIC || hdr[1] == 0 || hdr[1] > ART_PAK_MAX_ENTRIES) {
        fclose(f);
        return;
    }
    int n = (int)hdr[1];
    pak_recs = malloc((size_t)n * sizeof(pak_rec_t));
    if (!pak_recs) { fclose(f); return; }
    if (fread(pak_recs, sizeof(pak_rec_t), n, f) != (size_t)n) {
        free(pak_recs);
        pak_recs = NULL;
        fclose(f);
        return;
    }
    /* Validate before trusting: asset_loadfd asserts on odd offsets, and a
       malformed table must degrade to the per-file path, never crash. */
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    uint32_t prev_key = 0;
    int prev_type = -1;
    for (int i = 0; i < n; i++) {
        const pak_rec_t *r = &pak_recs[i];
        if ((r->offset & 1) || r->type > 7 ||
            (long)r->offset + (long)r->size > fsz ||
            r->key < prev_key || (r->key == prev_key && (int)r->type <= prev_type)) {
            debugf("[BOXART] pak rejected: bad record %d\n", i);
            free(pak_recs);
            pak_recs = NULL;
            fclose(f);
            return;
        }
        prev_key = r->key;
        prev_type = r->type;
    }
    pak_file = f;   /* kept open for the whole session */
    pak_n = n;
    debugf("[BOXART] pak loaded: %d entries\n", pak_n);
}

/* bsearch over records sorted by (key, type). */
static const pak_rec_t *pak_find (uint32_t key, int type) {
    int lo = 0, hi = pak_n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const pak_rec_t *r = &pak_recs[mid];
        if (r->key < key || (r->key == key && r->type < type)) lo = mid + 1;
        else if (r->key > key || r->type > type)               hi = mid - 1;
        else return r;
    }
    return NULL;
}

#define ART_INDEX_CACHE       "sd:/menu/cache/artindex.bin"
#define ART_INDEX_CACHE_MAGIC 0x41494458   /* 'AIDX' */

static void art_index_build (void) {
    art_index_n = 0;

    /* Fastest path: the pak IS the index. Unique keys, already sorted. */
    pak_open();
    if (pak_n > 0) {
        for (int i = 0; i < pak_n && art_index_n < ART_INDEX_MAX; i++) {
            if (art_index_n == 0 || art_index[art_index_n - 1] != pak_recs[i].key) {
                art_index[art_index_n++] = pak_recs[i].key;
            }
        }
        debugf("[BOXART] index from pak: %d codes\n", art_index_n);
        return;
    }

    /* Fast path: cached index (two small reads instead of scanning a ~770-entry
       FAT directory over slow SD every boot). Regenerated whenever absent. */
    FILE *cf = fopen(ART_INDEX_CACHE, "rb");
    if (cf) {
        uint32_t hdr[2];
        if (fread(hdr, sizeof(uint32_t), 2, cf) == 2 && hdr[0] == ART_INDEX_CACHE_MAGIC &&
            hdr[1] > 0 && hdr[1] <= ART_INDEX_MAX &&
            fread(art_index, sizeof(uint32_t), hdr[1], cf) == hdr[1]) {
            art_index_n = (int)hdr[1];
        }
        fclose(cf);
        if (art_index_n > 0) {
            debugf("[BOXART] art index cache hit: %d entries\n", art_index_n);
            return;
        }
    }

    /* Sharded layout: enumerate boxart/<shard>/<CODE>. Every directory is small,
       so this is quick even on cold cache; progress shown regardless. */
    dir_t sh;
    int scanned = 0;
    int rs = dir_findfirst(DFS_BOXART_DIRECTORY, &sh);
    while (rs == 0) {
        if (sh.d_type == DT_DIR && sh.d_name[0] && !sh.d_name[1]) {
            char shard_path[48];
            snprintf(shard_path, sizeof(shard_path), DFS_BOXART_DIRECTORY "/%c", sh.d_name[0]);
            dir_t info;
            int r = dir_findfirst(shard_path, &info);
            while (r == 0) {
                if (info.d_type == DT_DIR && info.d_name[0] && info.d_name[1] && info.d_name[2] &&
                    info.d_name[3] && !info.d_name[4] && art_index_n < ART_INDEX_MAX) {
                    art_index[art_index_n++] = art_code_key(info.d_name[0], info.d_name[1],
                                                            info.d_name[2], info.d_name[3]);
                }
                if ((++scanned & 31) == 0 && display_get_width() > 0) {
                    surface_t *d = display_try_get();
                    if (d) {
                        graphics_fill_screen(d, graphics_make_color(0x08, 0x09, 0x14, 0xFF));
                        graphics_set_color(graphics_make_color(0xFF, 0xFF, 0xFF, 0xFF), 0);
                        char msg[64];
                        snprintf(msg, sizeof(msg), "Indexing cover art library... %d", art_index_n);
                        graphics_draw_text(d, 48, 110, msg);
                        graphics_draw_text(d, 48, 126, "(one-time scan; cached for future boots)");
                        display_show(d);
                    }
                }
                r = dir_findnext(shard_path, &info);
            }
        }
        rs = dir_findnext(DFS_BOXART_DIRECTORY, &sh);
    }
    qsort(art_index, art_index_n, sizeof(uint32_t), art_key_cmp);
    debugf("[BOXART] indexed %d art directories\n", art_index_n);

    if (art_index_n > 0) {
        cf = fopen(ART_INDEX_CACHE, "wb");
        if (cf) {
            uint32_t hdr[2] = { ART_INDEX_CACHE_MAGIC, (uint32_t)art_index_n };
            fwrite(hdr, sizeof(uint32_t), 2, cf);
            fwrite(art_index, sizeof(uint32_t), art_index_n, cf);
            fclose(cf);
        }
    }
}

static bool art_index_has (char a, char b, char c, char d) {
    if (art_index_n < 0) {
        art_index_build();
    }
    /* Retry a few times if the first build found nothing (e.g. queried before the
       SD was fully ready). Bounded so a genuinely artless card doesn't rescan forever. */
    static int art_index_retries = 0;
    if (art_index_n == 0 && art_index_retries < 8) {
        art_index_retries++;
        art_index_build();
    }
    uint32_t key = art_code_key(a, b, c, d);
    return bsearch(&key, art_index, art_index_n, sizeof(uint32_t), art_key_cmp) != NULL;
}

/* True if ANY art directory exists for this game code (exact region or a region
   variant). Zero-I/O after the first call (RAM index). Used by the grid to hide
   artless games entirely ("full grid" mode). */
int ui_components_boxart_index_count (void) {
    if (art_index_n < 0) {
        art_index_build();
    }
    return art_index_n;
}

bool ui_components_boxart_exists (const char *game_code) {
    if (!game_code || !game_code[0]) return false;
    /* FAIL OPEN: if the art index is empty (no art installed / index failed), treat
       every game as having art — an empty grid is far worse than placeholder tiles. */
    if (art_index_n == 0) return true;
    static const char regions[] = { 'E', 'P', 'J', 'U', 'A', 'X', 'F', 'D', 'I', 'S' };
    if (art_index_has(game_code[0], game_code[1], game_code[2], game_code[3])) return true;
    for (size_t i = 0; i < sizeof(regions); i++) {
        if (regions[i] != game_code[3] &&
            art_index_has(game_code[0], game_code[1], game_code[2], regions[i])) return true;
    }
    return false;
}

/* Attempt to load a built-in ROM DFS sprite for the given code+type.
   The baked art library is keyed mostly by US (E) game codes, but users often run
   PAL/JP dumps (same game, different 4th region byte). So if the ROM's exact code
   has no baked art, fall back to other region variants of the same title (the first
   three code chars identify the game). A region-mismatched cover beats no cover. */
/* Turn a loaded asset buffer into the component's sprite+surface. Owns abuf. */
static bool sprite_into_boxart (component_boxart_t *b, void *abuf, int sz) {
    if (!abuf || sz <= 0) { free(abuf); return false; }
    sprite_t *spr = sprite_load_buf(abuf, sz);
    if (!spr) { free(abuf); return false; }
    spr->flags |= SPRITE_FLAGS_OWNEDBUFFER;   /* mirror sprite_load() ownership */
    surface_t *s = calloc(1, sizeof(surface_t));
    if (!s) { sprite_free(spr); return false; }
    *s = sprite_get_pixels(spr);
    b->loading = false;
    b->sprite = spr;
    b->image = s; /* surface struct only; pixel buffer is owned by the sprite */
    return true;
}

static bool try_dfs_sprite(component_boxart_t *b, const char *game_code, file_image_type_t t) {
    const char *tn = dfs_type_name(t);
    if (!tn) return false;

    /* Attempt order: the ROM's own region first, then the common region bytes. */
    static const char region_candidates[] = { 'E', 'P', 'J', 'U', 'A', 'X', 'F', 'D', 'I', 'S' };
    char order[1 + sizeof(region_candidates)];
    int no = 0;
    order[no++] = game_code[3];
    for (size_t i = 0; i < sizeof(region_candidates); i++) {
        if (region_candidates[i] != game_code[3]) order[no++] = region_candidates[i];
    }

    char path[64];
    for (int i = 0; i < no; i++) {
        /* Index check first: a miss costs zero SD I/O. If the index is EMPTY (scan
           failed or art not installed), skip the gate entirely and probe the SD the
           old way — art must never be lost to an index failure. */
        if (art_index_n > 0 && !art_index_has(game_code[0], game_code[1], game_code[2], order[i])) continue;
        /* Pak fast path: one seek + one read on the always-open archive. */
        if (pak_n > 0) {
            int tid = pak_type_id(t);
            if (tid < 0) return false;
            const pak_rec_t *r = pak_find(
                art_code_key(game_code[0], game_code[1], game_code[2], order[i]), tid);
            if (!r) continue;   /* this region has no such art; try the next */
            if (fseek(pak_file, (long)r->offset, SEEK_SET) == 0) {
                int sz = (int)r->size;
                void *abuf = asset_loadf(pak_file, &sz);
                if (abuf) return sprite_into_boxart(b, abuf, sz);
            }
            /* Pak I/O failed (seen on hardware: FatFS FR_INT_ERR on seek).
               Retire the pak for this session and fall through to the proven
               per-file path -- art gets slower, the menu keeps running. The
               art index stays valid: pak and shard dirs hold the same set. */
            debugf("[BOXART] pak I/O failed, falling back to per-file art\n");
            fclose(pak_file);
            pak_file = NULL;
            pak_n = 0;
        }
        snprintf(path, sizeof(path), ART_SHARD_FMT,
                 game_code[1], game_code[0], game_code[1], game_code[2], order[i], tn);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        /* Crash-proof load: sprite_load() hard-asserts if its internal open fails
           (seen once on hardware even after a successful precheck). Load through the
           already-open FILE instead — no second open, no assert path. */
        /* asset_loadf takes the file's total size as INPUT (validates the
           compressed stream against it) — passing 0 asserts on hardware. */
        fseek(f, 0, SEEK_END);
        int sz = (int)ftell(f);
        rewind(f);
        void *abuf = asset_loadf(f, &sz);
        fclose(f);
        return sprite_into_boxart(b, abuf, sz);
    }
    return false;
}

/* Attempt to load an SD-card metadata-folder PNG for the given code+type. */
static bool try_metadata_png(component_boxart_t *b, const char *storage_prefix,
                             const char *game_code, const char *rom_title, file_image_type_t t) {
    const char *filename = metadata_type_filename(t);
    if (!filename) return false;

    /* One-time gate: if there is no metadata root at all, skip every per-game probe.
       Each probe costs several directory scans on slow SD (incl. false hits into the
       big sd:/menu/boxart sprite tree via the legacy fallback). */
    static int metadata_root_state = -1;   /* -1 unknown, 0 absent, 1 present */
    if (metadata_root_state < 0) {
        path_t *root = path_init((char *)storage_prefix, METADATA_BASE_DIRECTORY);
        metadata_root_state = directory_exists(path_get(root)) ? 1 : 0;
        path_free(root);
    }
    if (metadata_root_state == 0) return false;

    char boxart_path[32];
    path_t *path = path_init(storage_prefix, METADATA_BASE_DIRECTORY);

    if (game_code[1] == 'E' && game_code[2] == 'D' && rom_title) {
        char safe_title[21];
        memcpy(safe_title, rom_title, 20);
        safe_title[20] = '\0';
        snprintf(boxart_path, sizeof(boxart_path), HOMEBREW_ID_SUBDIRECTORY"/%s", safe_title);
        path_push(path, boxart_path);
    } else {
        snprintf(boxart_path, sizeof(boxart_path), "%c/%c/%c/%c", game_code[0], game_code[1], game_code[2], game_code[3]);
        path_push(path, boxart_path);
        if (!directory_exists(path_get(path))) path_pop(path);
        if (!directory_exists(path_get(path))) {
            path_free(path);
            path = path_init(storage_prefix, OLD_BOXART_DIRECTORY);
            path_push(path, boxart_path);
            if (!directory_exists(path_get(path))) path_pop(path);
        }
    }

    bool ok = false;
    if (directory_exists(path_get(path))) {
        path_push(path, (char *)filename);
        ok = try_png(b, path_get(path), BOXART_WIDTH_MAX, BOXART_HEIGHT_MAX);
    }
    path_free(path);
    return ok;
}

/**
 * @brief Initialize and load the boxart component for a game.
 *
 * For the requested view, walks the fallback chain; for each candidate type it
 * tries, in priority order: a custom-folder PNG override, the built-in ROM DFS
 * sprite, then an SD-card metadata PNG. Returns the first that loads, or NULL if
 * no media exists for the game at all (caller shows the title text instead).
 */
int ui_components_boxart_alpha_threshold(const component_boxart_t *b) {
    /* N64 3D-cart AND logo art have soft anti-aliased edges on a transparent field; a 1-bit cutout
       draws that faint fringe opaque (the "eaten/haloed" look). A higher threshold crops the fringe
       for a clean edge. Other types -- and all 64DD disk/expansion art -- stay at 1. */
    return (b && (b->type == IMAGE_GAMEPAK_3D || b->type == IMAGE_LOGO) && !b->is_dd) ? 128 : 1;
}

component_boxart_t *ui_components_boxart_init(const char *storage_prefix, const char *game_code, const char *rom_title, const char *rom_path_hint, file_image_type_t current_image_view, bool use_custom) {
    component_boxart_t *b;

    if ((b = calloc(1, sizeof(component_boxart_t))) == NULL) {
        return NULL;
    }
    b->loading = true;
    b->is_dd = (game_code && (game_code[0] == 'D' || game_code[0] == 'E'));  /* 64DD disk/expansion */

    /* Derive the SD prefix and ROM filename stem for custom-folder lookups. */
    char sd_prefix[16] = "sd:";
    char stem[256] = "";
    if (rom_path_hint && rom_path_hint[0]) {
        const char *colon = strchr(rom_path_hint, ':');
        if (colon) {
            size_t plen = (size_t)(colon - rom_path_hint + 1);
            if (plen < sizeof(sd_prefix)) { memcpy(sd_prefix, rom_path_hint, plen); sd_prefix[plen] = '\0'; }
        }
        const char *slash = strrchr(rom_path_hint, '/');
        const char *fname = slash ? slash + 1 : rom_path_hint;
        strncpy(stem, fname, sizeof(stem) - 1); stem[sizeof(stem) - 1] = '\0';
        char *dot = strrchr(stem, '.'); if (dot) *dot = '\0';
    }

    /* Special-edition art redirect: a ROM that reuses another game's NUS code (e.g. OoT
       Master Quest, which ships under the regular OoT codes CZLE/NZLP) gets its own baked
       art under a distinct DFS key, matched on the filename rather than the colliding code. */
    const char *dfs_code = game_code;
    if (stem[0]) {
        int si = game_special_match(stem);
        if (si >= 0) {
            const game_special_t *sp = game_special_get(si);
            if (sp && sp->art_code) dfs_code = sp->art_code;
        }
    }

    int chain_n;
    const file_image_type_t *chain = fallback_chain(current_image_view, &chain_n);

    for (int i = 0; i < chain_n; i++) {
        file_image_type_t t = chain[i];
        b->type = t;   /* record the type actually loaded (a success returns from this iteration) */

        /* 1. Custom-folder PNG override (beats built-in art). Gated by the
           "Use custom files" setting — skipping these SD probes when off is a big
           speed win, since they almost always miss. */
        if (use_custom && stem[0]) {
            char cpath[400];
            const char *suf = custom_type_suffix(t);
            if (suf) {
                snprintf(cpath, sizeof(cpath), "%s/menu/n64ever/gameconfigs/%s%s.png", sd_prefix, stem, suf);
                if (try_png(b, cpath, 1024, 1024)) return b;
            }
            /* (Legacy /menu/custom PNG probes dropped: the boot migration relocates all custom
               art to /menu/n64ever, so they were wasted SD opens on every cover load.) */
        }

        /* 2. Built-in ROM DFS sprite (special-edition redirect applied). */
        if (try_dfs_sprite(b, dfs_code, t)) return b;

        /* 3. SD-card metadata-folder PNG. */
        if (try_metadata_png(b, storage_prefix, game_code, rom_title, t)) return b;
    }

    free(b);
    return NULL;
}

/**
 * @brief Free the boxart component and its resources.
 */
void ui_components_boxart_free(component_boxart_t *b) {
    if (b) {
        if (b->loading) {
            png_decoder_abort();
        }
        if (b->sprite) {
            /* Sprite-backed: the surface struct references the sprite's buffer,
               so free only the struct here and release the sprite separately. */
            if (b->image) free(b->image);
            sprite_free(b->sprite);
        } else if (b->image) {
            surface_free(b->image);
            free(b->image);
        }
        free(b);
    }
}

/**
 * @brief Draw the boxart image or a loading placeholder.
 */
void ui_components_boxart_draw(component_boxart_t *b) {
    if (b && b->image) {
        int box_x = BOXART_X;
        int box_y = BOXART_Y;
        rdpq_mode_push();
        rdpq_set_mode_standard();
        rdpq_mode_filter(FILTER_BILINEAR);
        if (b->image->width <= BOXART_WIDTH_MAX && b->image->height <= BOXART_HEIGHT_MAX) {
            /* Pre-scaled database images: snap to their designated display positions.
               240p: draw at half vertical scale (double-tall pixels). */
            if (b->image->height == BOXART_HEIGHT_MAX) {
                box_x = BOXART_X_JP;
                box_y = BOXART_Y_JP;
            } else if (b->image->width == BOXART_WIDTH_DD && b->image->height == BOXART_HEIGHT_DD) {
                box_x = BOXART_X_DD;
                box_y = BOXART_Y_DD;
            }
            rdpq_tex_blit(b->image, box_x, box_y,
                &(rdpq_blitparms_t){ .scale_x = 1.0f, .scale_y = 0.5f });
        } else {
            /* Large sidecar image: fit the boxart area in square-pixel space
               (240p: height budget counts double), draw at half vertical scale. */
            float sx = (float)BOXART_WIDTH / b->image->width;
            float sy = (float)BOXART_HEIGHT / b->image->height;   /* source-space budget */
            float scale = sx < sy ? sx : sy;
            int draw_w = (int)(b->image->width  * scale);
            int draw_h = (int)(b->image->height * scale / 2);
            int off_x  = (BOXART_WIDTH  - draw_w) / 2;
            int off_y  = ((BOXART_HEIGHT / 2) - draw_h) / 2;
            rdpq_tex_blit(b->image,
                BOXART_X + off_x, BOXART_Y + off_y,
                &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale / 2 });
        }
        rdpq_mode_pop();
    } else {
        ui_components_box_draw(
            BOXART_X,
            BOXART_Y,
            BOXART_X + BOXART_WIDTH,
            BOXART_Y + BOXART_HEIGHT,
            BOXART_LOADING_COLOR
        );
    }
}
