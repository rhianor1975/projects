/* Renders the tile art to image files so it can actually be looked at.
 *
 * The two graphical render modes only exist on the notcurses backend, which
 * refuses to start under a pipe or a detached terminal -- so the automated
 * playtest harness cannot screenshot them. This tool takes the same code path
 * the tile renderer uses (art_sprite_rgba) and writes the result to disk
 * instead of to a terminal, which makes the art reviewable without a human in
 * front of a real terminal.
 *
 *   make art     -> build/art-atlas.ppm, build/art-scene.ppm
 *
 * Not part of the game build; it links only tileart.o.
 */

#include "../src/tileart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int w, h;
    unsigned char *px;   /* RGBA */
} Image;

static Image img_new(int w, int h, unsigned char r, unsigned char g, unsigned char b) {
    Image im;
    im.w = w; im.h = h;
    im.px = calloc((size_t)w * (size_t)h, 4);
    if (!im.px) { fprintf(stderr, "artdump: out of memory\n"); exit(1); }
    for (int i = 0; i < w * h; i++) {
        im.px[i * 4 + 0] = r;
        im.px[i * 4 + 1] = g;
        im.px[i * 4 + 2] = b;
        im.px[i * 4 + 3] = 255;
    }
    return im;
}

static void img_write_ppm(const Image *im, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "artdump: cannot write %s\n", path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", im->w, im->h);
    for (int i = 0; i < im->w * im->h; i++)
        fwrite(&im->px[i * 4], 1, 3, f);
    fclose(f);
    printf("wrote %s (%dx%d)\n", path, im->w, im->h);
}

/* Draw one sprite at cell size (cw x ch) with its top-left at (x,y). */
static void draw_art(Image *im, ArtId id, int x, int y, int cw, int ch, bool dim, bool opaque) {
    if (x < 0 || y < 0 || x + cw > im->w || y + ch > im->h) return;
    unsigned char *corner = im->px + ((size_t)y * (size_t)im->w + (size_t)x) * 4;
    art_sprite_rgba(id, dim, opaque, corner, cw, ch, im->w * 4);
}

/* ---- 1. the atlas: every art id, lit and dimmed ---------------------- */

static void dump_atlas(const char *path) {
    const int CW = 32, CH = 64;      /* 4x the authored 8x16 */
    const int PAD = 6, COLS = 9;
    int rows = (ART_COUNT + COLS - 1) / COLS;

    /* Two blocks: normal on top, the dimmed (remembered) variant below, so
       the out-of-sight tint can be judged against the lit version. */
    int w = COLS * (CW + PAD) + PAD;
    int h = 2 * (rows * (CH + PAD) + PAD) + PAD;
    Image im = img_new(w, h, 16, 16, 20);

    int block_h = rows * (CH + PAD) + PAD;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < ART_COUNT; i++) {
            int c = i % COLS, r = i / COLS;
            int x = PAD + c * (CW + PAD);
            int y = pass * block_h + PAD + r * (CH + PAD);
            draw_art(&im, (ArtId)i, x, y, CW, CH, pass == 1, true);
        }
    }

    img_write_ppm(&im, path);
    free(im.px);
}

/* ---- 2. a scene: terrain with actors composited over it -------------- */

/* Hand-laid so every visual case appears at least once: walls, floor, water
   crossed by a bridge, a lava pool, miasma, doors, shops, and actors standing
   on several different terrains -- which is the case the transparent-sprite
   composite path exists for. The same layout is drawn twice, lit and then
   dimmed, so the remembered-but-not-visible tint can be judged side by side. */
static const char *SCENE[] = {
    "wwwwwwwwwwwwwwwwwwwwwwwwww",
    "w....,...w~~~~~w....^^...w",
    "w..@..r..w~~=~~w...^^^..~w",
    "w.....,..d~~=~~w....^^...w",
    "w..$..c..w~~=~~w....,....w",
    "wwwwdwwwwwww=wwwwwwwwwwwww",
    "w....,......=.......%%...w",
    "w..!..S..F..=..M..a.%%%..w",
    "w..T..w..w..=..o.....%...w",
    "w..R..P..G..=..A..b..>...w",
    "wwwwwwwwwwwwwwwwwwwwwwwwww",
    NULL
};

static void scene_art(char c, ArtId *terrain, ArtId *actor, bool *has_actor) {
    *actor = ART_UNKNOWN;
    *has_actor = false;
    switch (c) {
        case 'w': *terrain = ART_WALL; return;
        case '.': *terrain = ART_FLOOR; return;
        case ',': *terrain = ART_DECOR; return;
        case '~': *terrain = ART_WATER; return;
        case '=': *terrain = ART_BRIDGE; return;
        case '^': *terrain = ART_LAVA; return;
        case '%': *terrain = ART_MIASMA; return;
        case 'd': *terrain = ART_LOCKED_DOOR; return;
        case '>': *terrain = ART_STAIRS_DOWN; return;
        case 'G': *terrain = ART_SHOP_GENERAL; return;
        case 'M': *terrain = ART_SHOP_ARMORY; return;
        case 'P': *terrain = ART_SHOP_APOTHECARY; return;
        case 'A': *terrain = ART_SHOP_ARCANIST; return;
        case 'T': *terrain = ART_TEMPLE_ENTRANCE; return;
        default: break;
    }
    /* everything else stands on plain floor */
    *terrain = ART_FLOOR;
    *has_actor = true;
    switch (c) {
        case '@': *actor = ART_PLAYER;         break;
        case 'r': *actor = ART_MON_VERMIN;     break;
        case 'c': *actor = ART_MON_CLOCKWORK;  break;
        case 'o': *actor = ART_MON_OUTRIDER;   break;
        case 'a': *actor = ART_MON_ABYSSAL;    break;
        case 'b': *actor = ART_MON_BOSS;       break;
        case 'R': *actor = ART_MON_RUINS;      break;
        case '$': *actor = ART_GOLD;           break;
        case '!': *actor = ART_POTION;         break;
        case 'S': *actor = ART_SHRINE;         break;
        case 'F': *actor = ART_FOUNTAIN;       break;
        default:  *actor = ART_UNKNOWN;        break;
    }
}

static void dump_scene(const char *path) {
    /* A plausible real terminal cell. Not 8x16 on purpose -- this is also the
       check that the scaler doesn't distort at a size it wasn't authored
       for. */
    const int CW = 10, CH = 21;

    int rows = 0, cols = 0;
    for (const char **r = SCENE; *r; r++, rows++) {
        int l = (int)strlen(*r);
        if (l > cols) cols = l;
    }

    Image im = img_new(cols * CW, rows * CH * 2, 0, 0, 0);

    /* pass 0 = currently visible, pass 1 = remembered but out of sight. */
    for (int pass = 0; pass < 2; pass++) {
        bool dim = (pass == 1);
        for (int ry = 0; ry < rows; ry++) {
            const char *row = SCENE[ry];
            int len = (int)strlen(row);
            for (int rx = 0; rx < cols; rx++) {
                char c = rx < len ? row[rx] : ' ';

                ArtId terrain, actor;
                bool has_actor;
                scene_art(c, &terrain, &actor, &has_actor);

                int x = rx * CW, y = (pass * rows + ry) * CH;
                draw_art(&im, terrain, x, y, CW, CH, dim, true);
                /* Out of sight the game shows terrain only -- actors are not
                   remembered -- so the dim pass draws the terrain alone. */
                if (has_actor && !dim) draw_art(&im, actor, x, y, CW, CH, false, false);
            }
        }
    }

    img_write_ppm(&im, path);
    free(im.px);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "build";
    char path[512];

    art_set_biome(-1);
    snprintf(path, sizeof(path), "%s/art-atlas.ppm", dir);
    dump_atlas(path);

    snprintf(path, sizeof(path), "%s/art-scene.ppm", dir);
    dump_scene(path);

    /* One scene per biome. The tint is the only thing that makes a hundred
       floors look like more than one place, and it is the one part of the
       art nobody can judge from a table of RGB triples -- so it gets
       rendered here rather than requiring a descent to floor 80. */
    /* The city is in this list even though it is not a biome: it is a tint,
       it is the place a run begins and ends, and it went untinted for as long
       as it did precisely because nothing rendered it for anybody to look at. */
    static const char *const BIOME_FILE[] = {
        "jungle", "industrial", "ruins", "wastes", "abyss", "city"
    };
    for (int b = 0; b < (int)(sizeof(BIOME_FILE) / sizeof(BIOME_FILE[0])); b++) {
        art_set_biome(b);
        snprintf(path, sizeof(path), "%s/art-scene-%s.ppm", dir, BIOME_FILE[b]);
        dump_scene(path);
    }
    art_set_biome(-1);

    return 0;
}
