/* Tile-art invariants.
 *
 * The two graphical render modes only run on the notcurses backend, which
 * refuses to start under a pipe or a detached terminal -- so no automated
 * test can look at what they draw. What CAN be checked automatically is
 * everything upstream of the terminal: that the table is complete, that every
 * sprite is the shape the rasteriser assumes, and that the rasteriser fills
 * the pixels it promises to fill. Those are also the failures that would be
 * invisible in review -- a missing enum row draws a magenta '?' somewhere on
 * floor 60, and a short sprite string is a typo nobody sees.
 *
 * Links against the real src/tileart.c, never a copy.
 */

#include "../src/tileart.h"
#include "../src/common.h"
#include "../src/render.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); \
        failures++; \
    } \
} while (0)

/* ---- 1. every art id has a real definition -------------------------- */

static void test_table_complete(void) {
    printf("art table\n");
    for (int i = 0; i < ART_COUNT; i++) {
        const ArtDef *d = art_def((ArtId)i);
        CHECK(d != NULL, "art %d: NULL definition", i);
        if (!d) continue;
        /* art_def falls back to ART_UNKNOWN for holes, so a genuine gap shows
           up as a non-UNKNOWN index answering with UNKNOWN's name. */
        if (i != ART_UNKNOWN)
            CHECK(strcmp(d->name, "unknown") != 0,
                  "art %d has no table row (fell back to ART_UNKNOWN)", i);
        CHECK(d->name && d->name[0], "art %d: empty name", i);
        CHECK(d->glyph != 0, "%s: no block-mode glyph", d->name);
    }
    printf("  %d art definitions\n", ART_COUNT);
}

/* ---- 2. sprites are exactly the shape the rasteriser assumes -------- */

static void test_sprite_shapes(void) {
    printf("sprite shapes\n");
    for (int i = 0; i < ART_COUNT; i++) {
        const ArtDef *d = art_def((ArtId)i);
        for (int r = 0; r < ART_SPRITE_H; r++) {
            const char *row = d->sprite[r];
            if (!row) { CHECK(false, "%s row %d: missing", d->name, r); continue; }
            CHECK((int)strlen(row) == ART_SPRITE_W,
                  "%s row %d: %d columns, want %d", d->name, r, (int)strlen(row), ART_SPRITE_W);
            for (const char *c = row; *c; c++)
                CHECK(strchr(ART_PALETTE_CHARS, *c) != NULL,
                      "%s row %d: '%c' is not a palette character", d->name, r, *c);
        }
    }
    printf("  %d sprites x %dx%d, palette-clean\n", ART_COUNT, ART_SPRITE_W, ART_SPRITE_H);
}

/* ---- 3. every game enum maps to a real art ------------------------- */

static void test_enum_coverage(void) {
    printf("enum coverage\n");

    /* If a TileType is added without an art row this loop is what catches it,
       rather than a '?' appearing on a floor nobody has generated yet. */
    for (int t = TILE_WALL; t <= TILE_LEVER; t++) {
        ArtId a = art_for_tile(t);
        CHECK(a != ART_UNKNOWN, "TileType %d has no art", t);
    }
    for (int f = FEATURE_SHRINE; f <= FEATURE_TOWN_GATE; f++) {
        ArtId a = art_for_feature(f);
        CHECK(a != ART_UNKNOWN, "FeatureType %d has no art", f);
    }

    int mon_pairs[] = { CP_MON_VERMIN, CP_MON_CLOCKWORK, CP_MON_RUINS,
                        CP_MON_OUTRIDER, CP_MON_ABYSSAL, CP_MON_BOSS };
    for (size_t i = 0; i < sizeof(mon_pairs) / sizeof(mon_pairs[0]); i++) {
        ArtId a = art_for_monster(mon_pairs[i], false);
        CHECK(a != ART_UNKNOWN, "monster colour pair %d has no art", mon_pairs[i]);
    }
    CHECK(art_for_monster(CP_MON_VERMIN, true) == ART_MON_BOSS,
          "a boss must use the boss sprite whatever its faction colour");

    /* Out-of-range must not read past the table. */
    CHECK(art_for_tile(9999) == ART_UNKNOWN, "unknown tile type must fall back");
    CHECK(art_def((ArtId)-1) != NULL, "negative art id must not crash");
    CHECK(art_def((ArtId)9999) != NULL, "out-of-range art id must not crash");

    printf("  tiles, features and monster factions all mapped\n");
}

/* ---- 4. the rasteriser --------------------------------------------- */

#define PW 13   /* deliberately not 8x16 -- the real cell never is */
#define PH 27

static void test_rasteriser(void) {
    printf("rasteriser\n");

    static unsigned char buf[PH][PW][4];

    /* Terrain is opaque: every pixel of the cell must be written, or the map
       shows through in holes. */
    memset(buf, 0, sizeof(buf));
    art_sprite_rgba(ART_WALL, false, true, &buf[0][0][0], PW, PH, PW * 4);
    int unwritten = 0;
    for (int y = 0; y < PH; y++)
        for (int x = 0; x < PW; x++)
            if (buf[y][x][3] != 255) unwritten++;
    CHECK(unwritten == 0, "opaque wall left %d of %d pixels unwritten", unwritten, PW * PH);

    /* An actor drawn over terrain must leave the transparent parts of its
       sprite alone -- that is the whole reason for the flag. */
    memset(buf, 0, sizeof(buf));
    for (int y = 0; y < PH; y++)
        for (int x = 0; x < PW; x++) { buf[y][x][0] = 9; buf[y][x][3] = 255; }
    art_sprite_rgba(ART_PLAYER, false, false, &buf[0][0][0], PW, PH, PW * 4);
    int kept = 0, painted = 0;
    for (int y = 0; y < PH; y++)
        for (int x = 0; x < PW; x++)
            { if (buf[y][x][0] == 9) kept++; else painted++; }
    CHECK(kept > 0,    "transparent player sprite covered the whole cell");
    CHECK(painted > 0, "player sprite painted nothing at all");

    /* Dim must darken and must never brighten. */
    static unsigned char lit[PH][PW][4], dim[PH][PW][4];
    art_sprite_rgba(ART_FLOOR, false, true, &lit[0][0][0], PW, PH, PW * 4);
    art_sprite_rgba(ART_FLOOR, true,  true, &dim[0][0][0], PW, PH, PW * 4);
    int brighter = 0, darker = 0;
    for (int y = 0; y < PH; y++)
        for (int x = 0; x < PW; x++)
            for (int ch = 0; ch < 3; ch++) {
                if (dim[y][x][ch] > lit[y][x][ch]) brighter++;
                if (dim[y][x][ch] < lit[y][x][ch]) darker++;
            }
    CHECK(brighter == 0, "%d channels got brighter when dimmed", brighter);
    CHECK(darker > 0,    "dimming a lit floor changed nothing");

    /* A zero-sized or NULL target must be a no-op, not a crash: the caller
       gets its cell geometry from the terminal, which can report oddities. */
    art_sprite_rgba(ART_WALL, false, true, NULL, PW, PH, PW * 4);
    art_sprite_rgba(ART_WALL, false, true, &buf[0][0][0], 0, 0, PW * 4);

    /* Block colours must respect the same dim rule. */
    TuiRGB flit, fdim;
    art_block_colors(ART_WALL, false, &flit, NULL);
    art_block_colors(ART_WALL, true,  &fdim, NULL);
    CHECK(fdim.r <= flit.r && fdim.g <= flit.g && fdim.b <= flit.b,
          "dimmed block colour is brighter than the lit one");

    printf("  opacity, transparency, dimming and degenerate sizes all hold\n");
}

/* ---- 5. the tile-mode viewport budget ------------------------------- */

/* Tile mode's whole cost is the image it ships to the terminal each frame,
   and that is set here. A regression in this function is a game that feels
   broken rather than one that looks wrong, so it gets its own checks -- and
   they run under the ncurses build, which has no pixel graphics at all. */
static void test_viewport_budget(void) {
    printf("tile viewport budget\n");

    /* Cell sizes spanning tiny bitmap fonts to a large retina cell. */
    const int cells[][2] = { {6,12}, {8,16}, {9,19}, {10,21}, {14,30}, {20,44} };
    const long budget = TILE_PIXELS_DEFAULT;

    for (size_t i = 0; i < sizeof(cells) / sizeof(cells[0]); i++) {
        int cw = cells[i][0], ch = cells[i][1];
        int w = MAP_W, h = MAP_H;                 /* start from the largest */
        render_clamp_tile_viewport(cw, ch, &w, &h);

        CHECK(w >= 58 && h >= 18,
              "cell %dx%d: clamped to %dx%d, below the minimum viewport", cw, ch, w, h);
        CHECK(w <= MAP_W && h <= MAP_H,
              "cell %dx%d: clamped to %dx%d, larger than the map", cw, ch, w, h);

        long px = (long)w * cw * (long)h * ch;
        bool at_floor = (w == 58 && h == 18);
        CHECK(px <= budget || at_floor,
              "cell %dx%d: %dx%d tiles = %ld px, over the %ld budget",
              cw, ch, w, h, px, budget);
        printf("  cell %2dx%-2d -> %3dx%-2d tiles (%ld px%s)\n",
               cw, ch, w, h, px, at_floor && px > budget ? ", at the floor" : "");
    }

    /* A viewport already under budget must come back untouched -- the clamp
       is a ceiling, not a resize. */
    int w = 60, h = 20;
    render_clamp_tile_viewport(8, 16, &w, &h);
    CHECK(w == 60 && h == 20, "an under-budget viewport was shrunk to %dx%d", w, h);

    /* Degenerate inputs must not spin or crash. */
    w = MAP_W; h = MAP_H;
    render_clamp_tile_viewport(0, 0, &w, &h);
    CHECK(w == MAP_W && h == MAP_H, "a zero cell size must be a no-op");
    render_clamp_tile_viewport(8, 16, NULL, NULL);

    /* An absurd cell size can't be satisfied; it must stop at the floor
       rather than loop forever or go negative. */
    w = MAP_W; h = MAP_H;
    render_clamp_tile_viewport(400, 400, &w, &h);
    CHECK(w == 58 && h == 18, "unsatisfiable budget settled at %dx%d, want the floor", w, h);

    /* The budget is the player's lever, so moving it has to actually move
       the viewport -- and has to be clamped to its stated range. */
    int wide_w = MAP_W, wide_h = MAP_H;
    render_set_tile_pixels(TILE_PIXELS_MAX);
    CHECK(render_get_tile_pixels() == TILE_PIXELS_MAX, "max budget did not stick");
    render_clamp_tile_viewport(10, 21, &wide_w, &wide_h);

    int tight_w = MAP_W, tight_h = MAP_H;
    render_set_tile_pixels(TILE_PIXELS_MIN);
    CHECK(render_get_tile_pixels() == TILE_PIXELS_MIN, "min budget did not stick");
    render_clamp_tile_viewport(10, 21, &tight_w, &tight_h);

    CHECK((long)wide_w * wide_h > (long)tight_w * tight_h,
          "a bigger budget must tile more: %dx%d vs %dx%d",
          wide_w, wide_h, tight_w, tight_h);

    render_set_tile_pixels(1);
    CHECK(render_get_tile_pixels() == TILE_PIXELS_MIN, "budget below the range must clamp up");
    render_set_tile_pixels(999999999);
    CHECK(render_get_tile_pixels() == TILE_PIXELS_MAX, "budget above the range must clamp down");
    render_set_tile_pixels(TILE_PIXELS_DEFAULT);

    printf("  budget honoured across 6 cell sizes; the lever moves it, clamped\n");
}

/* ---- biome tinting ----------------------------------------------------
   A hundred floors used to be cut from the same grey stone. The tint has to
   actually change terrain, actually differ between biomes, and pointedly not
   touch anything standing on the terrain. */
static void test_biome_tint(void) {
    printf("biome tinting\n");

    TuiRGB base_fg, base_bg;
    art_set_biome(-1);
    art_block_colors(ART_FLOOR, false, &base_fg, &base_bg);

    /* Terrain shifts, and each biome shifts it somewhere different. */
    TuiRGB seen[BIOME_COUNT];
    for (int b = 0; b < BIOME_COUNT; b++) {
        art_set_biome(b);
        CHECK(art_get_biome() == b, "the biome that was set is the biome in force");
        art_block_colors(ART_FLOOR, false, &seen[b], NULL);
        CHECK(seen[b].r != base_fg.r || seen[b].g != base_fg.g || seen[b].b != base_fg.b,
              "every biome moves the colour of the ground off the default");
    }
    for (int a = 0; a < BIOME_COUNT; a++)
        for (int b = a + 1; b < BIOME_COUNT; b++)
            CHECK(seen[a].r != seen[b].r || seen[a].g != seen[b].g || seen[a].b != seen[b].b,
                  "no two biomes look the same");

    /* Actors do not. A monster that goes green in the jungle is a monster you
       walk into, so the tint is terrain-only. */
    for (int b = 0; b < BIOME_COUNT; b++) {
        art_set_biome(-1);
        TuiRGB plain;
        art_block_colors(ART_PLAYER, false, &plain, NULL);
        art_set_biome(b);
        TuiRGB tinted;
        art_block_colors(ART_PLAYER, false, &tinted, NULL);
        CHECK(plain.r == tinted.r && plain.g == tinted.g && plain.b == tinted.b,
              "actors keep their own colour whatever floor they are on");
    }

    /* Out of range means the surface, not a crash or a stale tint. */
    art_set_biome(BIOME_COUNT + 5);
    CHECK(art_get_biome() == -1, "an unknown biome is treated as no biome");
    TuiRGB back;
    art_block_colors(ART_FLOOR, false, &back, NULL);
    CHECK(back.r == base_fg.r && back.g == base_fg.g && back.b == base_fg.b,
          "and leaves the art table exactly as it found it");

    /* Dimming still applies on top -- remembered ground has to stay dimmer
       than lit ground in every biome, or the fog stops reading as fog. */
    for (int b = 0; b < BIOME_COUNT; b++) {
        art_set_biome(b);
        TuiRGB lit, dim;
        art_block_colors(ART_FLOOR, false, &lit, NULL);
        art_block_colors(ART_FLOOR, true,  &dim, NULL);
        CHECK(dim.r + dim.g + dim.b < lit.r + lit.g + lit.b,
              "remembered ground stays darker than lit ground in every biome");
    }

    art_set_biome(-1);
    printf("  five biomes, terrain only, dimming survives\n");
}

/* ---- a body's colour comes from the game, not from the sprite ------------
   The party are all one sprite -- ART_PLAYER -- and told apart by colour
   alone: six pairs, deliberately none of them the player's cyan. Text mode has
   always honoured that, because it draws straight from the colour pair. The
   graphical modes took their foreground from the ArtId, so every hire came out
   identical; reported from play against a text-mode screenshot where they are
   all different.

   Asserted on the rasteriser rather than on a screen, because neither
   graphical mode can be opened here -- notcurses will not start under a pipe.
   What is checkable is the thing that was wrong: whether the override reaches
   the pixels. */
static int test_bodies_wear_their_own_colour(void) {
    enum { W = 8, H = 16 };
    static unsigned char plain[W * H * 4], tinted[W * H * 4];
    int bad = 0;

    memset(plain, 0, sizeof plain);
    memset(tinted, 0, sizeof tinted);

    art_sprite_rgba(ART_PLAYER, false, true, plain, W, H, W * 4);

    TuiRGB own = { 200, 40, 40 };            /* nothing like the sprite's own */
    art_sprite_rgba_tinted(ART_PLAYER, false, true, &own, tinted, W, H, W * 4);

    if (memcmp(plain, tinted, sizeof plain) == 0) {
        printf("  FAIL: tinting a body changed nothing -- the override never reaches the pixels\n");
        bad++;
    }

    /* And the override has to be what actually landed, not merely *a* change. */
    bool found = false;
    for (int i = 0; i < W * H; i++) {
        if (tinted[i*4+0] == own.r && tinted[i*4+1] == own.g && tinted[i*4+2] == own.b) { found = true; break; }
    }
    if (!found) {
        printf("  FAIL: the tinted sprite contains none of the colour it was given\n");
        bad++;
    }

    /* NULL is exactly the untinted call -- otherwise every other caller in the
       game has quietly changed. */
    static unsigned char via_null[W * H * 4];
    memset(via_null, 0, sizeof via_null);
    art_sprite_rgba_tinted(ART_PLAYER, false, true, NULL, via_null, W, H, W * 4);
    if (memcmp(plain, via_null, sizeof plain) != 0) {
        printf("  FAIL: passing no tint is not the same as the untinted call\n");
        bad++;
    }

    if (!bad) printf("  a body's colour reaches the pixels, and no tint changes nothing\n");
    return bad;
}

/* Every biome has to be visibly a different place. This is the check that the
   original tint table would have failed: it rendered, it was "gentle", and
   measured across a whole scene wastes and city landed 3.8 apart out of 255.
   Reported from play as "on the tileset all biomes look the same".
 *
   So assert the thing that actually matters -- separation -- rather than that
   tinting happened at all. The terrain colours below are the real dark end of
   the palette (floor, wall) plus a mid tone, because dark is where the old
   purely-multiplicative shift collapsed and where any future regression will
   collapse again. */
static int test_biomes_are_told_apart(void) {
    static const char *NAME[] = { "jungle", "industrial", "ruins",
                                  "wastes", "abyss", "city" };
    const int N = (int)(sizeof NAME / sizeof NAME[0]);
    /* Below this, two floors read as the same floor. Chosen under the measured
       spread: the closest pair sits at 14.8, so 10 leaves room to retune the
       rows without the test becoming a copy of the table. */
    const double FLOOR_APART = 10.0;

    double mr[8], mg[8], mb[8];
    int bad = 0, prev = art_get_biome();

    for (int b = 0; b < N; b++) {
        art_set_biome(b);
        double r = 0, g = 0, bl = 0;
        int n = 0;
        static const ArtId TERRAIN[] = { ART_FLOOR, ART_WALL, ART_STAIRS_DOWN };
        for (size_t t = 0; t < sizeof TERRAIN / sizeof TERRAIN[0]; t++) {
            TuiRGB fg, bg;
            art_block_colors(TERRAIN[t], false, &fg, &bg);
            r += fg.r + bg.r; g += fg.g + bg.g; bl += fg.b + bg.b; n += 2;
        }
        mr[b] = r / n; mg[b] = g / n; mb[b] = bl / n;
    }
    art_set_biome(prev);

    double closest = 1e9;
    const char *ca = "", *cb = "";
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++) {
            double dr = mr[i] - mr[j], dg = mg[i] - mg[j], db = mb[i] - mb[j];
            double d = sqrt(dr * dr + dg * dg + db * db);
            if (d < closest) { closest = d; ca = NAME[i]; cb = NAME[j]; }
        }

    printf("  closest two biomes: %s vs %s, %.1f apart\n", ca, cb, closest);
    if (closest < FLOOR_APART) {
        printf("  FAIL: %s and %s render as the same place\n", ca, cb);
        bad++;
    } else {
        printf("  every biome is a visibly different place\n");
    }
    return bad;
}


/* Hearing must not hand over the map.
 *
   The mark for "something is there, and you cannot see it" is drawn as an
   occupant sprite over whatever the square already showed. The first version
   forced the cell's `seen` flag instead, which made every renderer draw the
   *terrain* of a square the player had never been shown -- so a sense sold as
   "a position, not an identity" was also reporting what the floor was made of.
   In tile mode it was worse than a leak: the compose loop draws terrain plus
   occupant and never reads `glyph`, so with the occupant cleared there was no
   mark at all, and the only mode where the leak was total was the one with
   nothing on screen to explain it.

   The property here is about the sprite rather than the renderer, because the
   renderer's half is asserted by the fact that the mark has to be an occupant
   to appear at all: ART_HEARD must be a *thing you can draw*, must not be
   tinted by the biome (it is not part of the floor), and must not resemble any
   monster in the table -- the moment it does, it stops being a mark. */
static int test_the_heard_mark_gives_nothing_away(void) {
    int bad = 0;
    const ArtDef *d = art_def(ART_HEARD);

    if (d->solid) {
        printf("  FAIL: the heard mark is flagged as terrain, so the biome tints it\n");
        bad++;
    }

    /* Tinting is what would make it read as part of the floor it is standing
       on. Rendered under two different biomes it must come out identical. */
    static unsigned char a[16 * 16 * 4], b[16 * 16 * 4];
    int prev = art_get_biome();
    art_set_biome(0);
    memset(a, 0, sizeof a);
    art_sprite_rgba(ART_HEARD, false, false, a, 16, 16, 16 * 4);
    art_set_biome(4);
    memset(b, 0, sizeof b);
    art_sprite_rgba(ART_HEARD, false, false, b, 16, 16, 16 * 4);
    art_set_biome(prev);
    if (memcmp(a, b, sizeof a) != 0) {
        printf("  FAIL: the heard mark changes colour with the biome\n");
        bad++;
    }

    /* It has to actually mark something -- a sprite of nothing but transparent
       would satisfy everything above and show the player no mark at all, which
       is exactly the tile-mode failure this test exists because of. */
    int opaque = 0;
    for (size_t i = 3; i < sizeof a; i += 4) if (a[i]) opaque++;
    if (opaque < 16) {
        printf("  FAIL: the heard mark is (almost) entirely transparent -- %d pixels\n", opaque);
        bad++;
    }

    if (!bad) printf("  the heard mark draws, and says nothing about the floor (%d px)\n", opaque);
    return bad;
}

int main(void) {
    printf("=== tile art ===\n\n");
    test_table_complete();
    test_sprite_shapes();
    test_enum_coverage();
    test_rasteriser();
    test_biome_tint();
    test_viewport_budget();
    failures += test_bodies_wear_their_own_colour();
    failures += test_biomes_are_told_apart();
    failures += test_the_heard_mark_gives_nothing_away();

    printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
