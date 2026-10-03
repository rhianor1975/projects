#include "render.h"
#include "vision.h"
#include "autoplay.h"
#include "items.h"
#include "classes.h"
#include "spells.h"
#include "ranged.h"
#include "abilities.h"
#include "mapgen.h"
#include "gearsets.h"
#include "monsters.h"
#include "save.h"
#include "tileart.h"
#include "companions.h"

static const int MAP_ORIGIN_Y = 2;
static const int MAP_ORIGIN_X = 1;

/* Declared up here rather than beside render_set_headless, because two of the
   sleeps that have to honour it are drawn earlier in the file than the flag
   used to be -- and both of them were quietly ignoring it as a result. See
   anim_pause. */
static bool g_headless;

/* Every wait-for-the-eye pause in this file goes through here.
 *
   Two did not, and both were on paths a self-playing run walks constantly:
   the red border flash spends 70ms every time the player is hit, and the
   lizard race spends 45ms a frame. A --silentrun that takes thousands of
   hits therefore spent nearly all of its wall clock asleep -- measured at 26%
   CPU, four minutes of wall for one minute of work -- and the run looked like
   a slow game rather than a fast game full of naps. There is nobody watching
   a headless run, so there is nothing for it to wait for. */
static void anim_pause(int ms) {
    if (g_headless) return;
    napms(ms);
}

/* How the world is drawn. Only the map area honours this -- every menu,
   shop and log line stays text in all three modes, because that is what they
   are good at and because a tileset for a shop screen would be a worse shop
   screen. */
static RenderMode g_mode = RENDER_TEXT;

/* The smallest viewport the rest of the game tolerates -- town's building
   layout assumes at least this much room. */
#define VIEW_MIN_W 58
#define VIEW_MIN_H 18

/* Tile mode hands the terminal a viewport-sized image on every frame whose
   picture changed, so its cost is set by how many cells are tiled -- not by
   the dungeon, and not by anything the game can optimise away. Past roughly
   this many pixels the transfer dominates and keypresses stop feeling
   immediate, so tile mode draws a smaller map than text mode does on the same
   window. Fewer tiles, but they arrive when you press the key.

   The default is ~360k pixels, about 1.4 MB per changed frame. How much a
   given terminal can actually swallow per keypress is not something the game
   can measure, so the player sets it (display screen) and the numbers are
   shown there. Below VIEW_MIN_* the rest of the game breaks, so that is the
   floor even when the budget asks for less -- at very large cell sizes (a
   big font on a retina display reports physical pixels) the floor can still
   exceed the budget, and correctness wins. */
static long g_tile_frame_pixels = TILE_PIXELS_DEFAULT;

void render_set_tile_pixels(int px) {
    if (px < TILE_PIXELS_MIN) px = TILE_PIXELS_MIN;
    if (px > TILE_PIXELS_MAX) px = TILE_PIXELS_MAX;
    g_tile_frame_pixels = px;
    render_layout_viewport();
}

int render_get_tile_pixels(void) { return (int)g_tile_frame_pixels; }

void render_clamp_tile_viewport(int cw, int ch, int *w, int *h) {
    if (cw <= 0 || ch <= 0 || !w || !h) return;
    if (*w < VIEW_MIN_W) *w = VIEW_MIN_W;
    if (*h < VIEW_MIN_H) *h = VIEW_MIN_H;

    while ((long)(*w) * cw * (long)(*h) * ch > g_tile_frame_pixels) {
        /* Trim whichever axis is currently longer in pixels, so the map stays
           roughly the shape of the window rather than collapsing to a slit. */
        if (*w > VIEW_MIN_W && (long)(*w) * cw >= (long)(*h) * ch) (*w)--;
        else if (*h > VIEW_MIN_H)                                  (*h)--;
        else if (*w > VIEW_MIN_W)                                  (*w)--;
        else break;   /* at the floor -- draw it anyway; correctness first */
    }
}

static void clamp_tile_viewport(int *w, int *h) {
    int cw = 0, ch = 0;
    if (!tui_cell_pixels(&cw, &ch)) return;
    render_clamp_tile_viewport(cw, ch, w, h);
}

/* What the map viewport would be for a given mode and window. Pure, so the
   display screen can quote tile mode's numbers without switching to it. */
static void compute_viewport(RenderMode mode, int rows, int cols, int *w, int *h) {
    /* Reserve room to the right for the sidebar (see sb_x in
       draw_game_screen) and below for the message log + footer. */
    int vw = cols - 45;
    int vh = rows - 9;
    if (vw < VIEW_MIN_W) vw = VIEW_MIN_W;
    if (vh < VIEW_MIN_H) vh = VIEW_MIN_H;
    if (vw > MAP_W) vw = MAP_W;
    if (vh > MAP_H) vh = MAP_H;

    if (mode == RENDER_TILES) clamp_tile_viewport(&vw, &vh);

    *w = vw;
    *h = vh;
}

/* Size the map viewport to the terminal. Split out of main() because the
   answer now depends on the render mode as well as the window, and because
   both can change while the game is running. */
static int g_laid_out_rows, g_laid_out_cols;

void render_layout_viewport(void) {
    /* Callable before initscr() -- the test harness sets the tile budget
       without ever opening a terminal, and there is no layout to compute
       until there is a screen. */
    if (!stdscr) return;

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    g_laid_out_rows = rows;
    g_laid_out_cols = cols;
    compute_viewport(g_mode, rows, cols, &g_view_w, &g_view_h);
}

void render_set_mode(RenderMode m) {
    const char *why = NULL;
    g_mode = render_mode_available(m, &why) ? m : RENDER_TEXT;
    render_layout_viewport();
}

RenderMode render_get_mode(void) { return g_mode; }

void init_colors(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();
    init_pair(CP_PLAYER,        COLOR_CYAN,    -1);
    init_pair(CP_WALL,          COLOR_WHITE,   -1);
    init_pair(CP_FLOOR,         COLOR_WHITE,   -1);
    init_pair(CP_DECOR,         COLOR_GREEN,   -1);
    init_pair(CP_STAIRS,        COLOR_YELLOW,  -1);
    init_pair(CP_SHOP,          COLOR_MAGENTA, -1);
    init_pair(CP_GOLD,          COLOR_YELLOW,  -1);
    init_pair(CP_POTION,        COLOR_CYAN,    -1);
    init_pair(CP_MON_VERMIN,    COLOR_GREEN,   -1);
    init_pair(CP_MON_CLOCKWORK, COLOR_YELLOW,  -1);
    init_pair(CP_MON_RUINS,     COLOR_MAGENTA, -1);
    init_pair(CP_MON_OUTRIDER,  COLOR_BLUE,    -1);
    init_pair(CP_MON_ABYSSAL,   COLOR_RED,     -1);
    init_pair(CP_MON_BOSS,      COLOR_RED,     -1);
    init_pair(CP_UI_HEADER,     COLOR_CYAN,    -1);
    init_pair(CP_UI_WARN,       COLOR_RED,     -1);
    init_pair(CP_WATER,         COLOR_BLUE,    -1);
    init_pair(CP_LAVA,          COLOR_RED,     -1);
    init_pair(CP_MIASMA,        COLOR_GREEN,   -1);
    init_pair(CP_PORTAL,        COLOR_MAGENTA, -1);
    init_pair(CP_SHRINE,        COLOR_CYAN,    -1);
    init_pair(CP_FOUNTAIN,      COLOR_BLUE,    -1);
    init_pair(CP_MERCHANT,      COLOR_YELLOW,  -1);
    init_pair(CP_MACHINE,       COLOR_WHITE,   -1);
    init_pair(CP_RELIC,         COLOR_YELLOW,  -1);
    init_pair(CP_GATE,          COLOR_CYAN,    -1);
    init_pair(CP_LOCKED_DOOR,   COLOR_YELLOW,  -1);
    init_pair(CP_KEY,           COLOR_YELLOW,  -1);
    init_pair(CP_QUEST_BOARD,   COLOR_MAGENTA, -1);
    init_pair(CP_MAGIC,         COLOR_BLUE,    -1);
    init_pair(CP_LEVER,         COLOR_GREEN,   -1);

    init_pair(CP_HIT,                 COLOR_WHITE,   -1);
    init_pair(CP_ABILITY,             COLOR_YELLOW,  -1);
    init_pair(CP_SCHOOL_CONDUIT,      COLOR_YELLOW,  -1);
    init_pair(CP_SCHOOL_RESONANCE,    COLOR_MAGENTA, -1);
    init_pair(CP_SCHOOL_WARDING,      COLOR_CYAN,    -1);
    init_pair(CP_SCHOOL_AETHER_SENSE, COLOR_BLUE,    -1);
    init_pair(CP_SCHOOL_SCRIBING,     COLOR_WHITE,   -1);
    init_pair(CP_SCHOOL_JINX,         COLOR_RED,     -1);
    init_pair(CP_RANGED_BOW,          COLOR_GREEN,   -1);
    init_pair(CP_RANGED_GUN,          COLOR_YELLOW,  -1);
    init_pair(CP_RANGED_LASER,        COLOR_RED,     -1);
    init_pair(CP_RANGED_BLOWGUN,      COLOR_CYAN,    -1);
    init_pair(CP_RANGED_THROWN,       COLOR_WHITE,   -1);
    init_pair(CP_RANGED_GRENADE,      COLOR_MAGENTA, -1);
    init_pair(CP_HP_OK,         COLOR_GREEN,   -1);
    init_pair(CP_HP_MID,        COLOR_YELLOW,  -1);
    init_pair(CP_FRAME,         COLOR_CYAN,    -1);
    init_pair(CP_THICKET,       COLOR_GREEN,   -1);

    /* Bold yellow: the one colour no biome wash and no district ground uses,
       so a heard monster never blends into the floor it is standing on. */
    init_pair(CP_HEARD,            COLOR_YELLOW,  -1);
    init_pair(CP_BIOME_JUNGLE,     COLOR_GREEN,   -1);
    init_pair(CP_BIOME_INDUSTRIAL, COLOR_YELLOW,  -1);
    init_pair(CP_BIOME_RUINS,      COLOR_WHITE,   -1);
    init_pair(CP_BIOME_WASTES,     COLOR_RED,     -1);
    init_pair(CP_BIOME_ABYSS,      COLOR_MAGENTA, -1);

    init_pair(CP_DIST_JUNGLE,   COLOR_GREEN,   -1);
    init_pair(CP_DIST_SEA,      COLOR_BLUE,    -1);
    init_pair(CP_DIST_SWAMP,    COLOR_CYAN,    -1);
    init_pair(CP_DIST_RUINS,    COLOR_WHITE,   -1);
    init_pair(CP_DIST_MYCELIUM, COLOR_MAGENTA, -1);   /* bioluminescent */
    init_pair(CP_DIST_HIVE,     COLOR_YELLOW,  -1);   /* amber comb */
    init_pair(CP_DIST_MIRE,     COLOR_BLUE,    -1);
    init_pair(CP_DIST_CRYSTAL,  COLOR_CYAN,    -1);
    init_pair(CP_DIST_QUIET,    COLOR_WHITE,   -1);
    init_pair(CP_DIST_STORM,    COLOR_YELLOW,  -1);
    init_pair(CP_DIST_ARENA,    COLOR_RED,     -1);   /* the sand is stained */
    init_pair(CP_DIST_AQUEDUCT, COLOR_CYAN,    -1);
    init_pair(CP_DIST_BLOODMARSH, COLOR_RED,   -1);
    init_pair(CP_DIST_PRISM,    COLOR_MAGENTA, -1);
    init_pair(CP_DIST_ASSEMBLY, COLOR_YELLOW,  -1);
    init_pair(CP_DIST_GARDEN,   COLOR_GREEN,   -1);
    init_pair(CP_DIST_WATCH,    COLOR_BLUE,    -1);   /* lamp-lit, and manned */
    init_pair(CP_DIST_GAUNTLET, COLOR_MAGENTA, -1);   /* grave-cut */
    init_pair(CP_DIST_EYE,      COLOR_CYAN,    -1);   /* the quiet quarter, lit */
    init_pair(CP_DIST_MIRROR,   COLOR_WHITE,   -1);   /* flat and bright */
    init_pair(CP_DIST_PETRIFIED,COLOR_YELLOW,  -1);   /* stone bark */
    init_pair(CP_DIST_CHAPEL,   COLOR_WHITE,   -1);   /* cold stone, and quiet */
    init_pair(CP_DIST_GEOTHERMAL, COLOR_RED,   -1);   /* the glow under it */
    init_pair(CP_DIST_SHAFT,    COLOR_WHITE,   -1);   /* cut rock and spoil */
    init_pair(CP_DIST_BONEYARD, COLOR_RED,     -1);   /* rust */
    init_pair(CP_DIST_PROVING,  COLOR_YELLOW,  -1);   /* swept sand, marked out */
    init_pair(CP_DIST_QUICKSAND,COLOR_YELLOW,  -1);
}

static void tile_glyph(TileType t, chtype *ch, int *pair) {
    switch (t) {
        case TILE_WALL:            *ch = '#'; *pair = CP_WALL;  break;
        case TILE_FLOOR:           *ch = '.'; *pair = CP_FLOOR; break;
        case TILE_STAIRS_DOWN:     *ch = '>'; *pair = CP_STAIRS; break;
        case TILE_STAIRS_UP:       *ch = '<'; *pair = CP_STAIRS; break;
        case TILE_SHOP_GENERAL:    *ch = 'G'; *pair = CP_SHOP;  break;
        case TILE_SHOP_ARMORY:     *ch = 'M'; *pair = CP_SHOP;  break;
        case TILE_SHOP_APOTHECARY: *ch = 'P'; *pair = CP_SHOP;  break;
        case TILE_TEMPLE_ENTRANCE: *ch = 'T'; *pair = CP_STAIRS; break;
        case TILE_DECOR:           *ch = ','; *pair = CP_DECOR; break;
        case TILE_THICKET:         *ch = '"'; *pair = CP_THICKET; break;
        case TILE_WATER:           *ch = '~'; *pair = CP_WATER; break;
        case TILE_BRIDGE:          *ch = '='; *pair = CP_DECOR; break;
        case TILE_LAVA:            *ch = '^'; *pair = CP_LAVA;  break;
        case TILE_MIASMA:          *ch = '%'; *pair = CP_MIASMA; break;
        case TILE_PORTAL:          *ch = 'O'; *pair = CP_PORTAL; break;
        case TILE_LOCKED_DOOR:     *ch = '+'; *pair = CP_LOCKED_DOOR; break;
        case TILE_SEALED_DOOR:     *ch = 'D'; *pair = CP_LOCKED_DOOR; break;
        case TILE_LEVER:           *ch = 'L'; *pair = CP_LEVER; break;
        case TILE_QUEST_BOARD:     *ch = 'Q'; *pair = CP_QUEST_BOARD; break;
        case TILE_SHOP_ARCANIST:   *ch = 'A'; *pair = CP_MAGIC; break;
        case TILE_INN:             *ch = 'I'; *pair = CP_HP_OK; break;
        case TILE_TAVERN:          *ch = 'V'; *pair = CP_MERCHANT; break;
        case TILE_JUNKYARD:        *ch = 'J'; *pair = CP_MACHINE; break;
        case TILE_GLADIATOR:       *ch = 'X'; *pair = CP_UI_WARN; break;
        case TILE_BANK:            *ch = 'B'; *pair = CP_GOLD; break;
        case TILE_ORACLE:          *ch = 'O'; *pair = CP_MAGIC; break;
        case TILE_ALTAR:           *ch = 'T'; *pair = CP_UI_WARN; break;
        case TILE_BAZAAR:          *ch = 'Z'; *pair = CP_MERCHANT; break;
        case TILE_KITCHEN:         *ch = 'K'; *pair = CP_MON_OUTRIDER; break;
        case TILE_BLACKMARKET:     *ch = '&'; *pair = CP_MON_ABYSSAL; break;
        case TILE_CRYSTAL:         *ch = '*'; *pair = CP_MAGIC; break;
        case TILE_ROD:             *ch = 'i'; *pair = CP_MACHINE; break;
        /* A mouth in the rock, not a spire in a field. Same system, and the
           glyph is the only place the difference has to show in text. */
        case TILE_VENT:            *ch = 'A'; *pair = CP_DIST_GEOTHERMAL; break;
        case TILE_CURRENT:         *ch = '~'; *pair = CP_DIST_AQUEDUCT; break;
        case TILE_BLOODPOOL:       *ch = '~'; *pair = CP_DIST_BLOODMARSH; break;
        case TILE_PRISM_RED:       *ch = '+'; *pair = CP_UI_WARN;  break;
        case TILE_PRISM_BLUE:      *ch = '+'; *pair = CP_DIST_SEA; break;
        case TILE_PRISM_GREEN:     *ch = '+'; *pair = CP_HP_OK;    break;
        case TILE_BELT:            *ch = '='; *pair = CP_DIST_ASSEMBLY; break;
        case TILE_SNARE:           *ch = '&'; *pair = CP_UI_WARN; break;
        case TILE_PIT:             *ch = '%'; *pair = CP_UI_WARN; break;
        /* Not '$'. Ore is terrain cut into the rock, so it keeps being drawn
           from memory once seen -- and sharing a glyph with dropped gold made
           it read as coins stuck inside a wall you can never reach. It is a
           seam; it gets a seam's mark. */
        case TILE_ORE:             *ch = '\\'; *pair = CP_RELIC;   break;
        case TILE_RACES:           *ch = 'R'; *pair = CP_MON_VERMIN; break;
        default:                   *ch = '?'; *pair = CP_FLOOR; break;
    }
}

static void feature_glyph(FeatureType t, chtype *ch, int *pair) {
    switch (t) {
        case FEATURE_SHRINE:   *ch = '_'; *pair = CP_SHRINE;   break;
        case FEATURE_FOUNTAIN: *ch = '{'; *pair = CP_FOUNTAIN; break;
        case FEATURE_MERCHANT: *ch = 'p'; *pair = CP_MERCHANT; break;
        case FEATURE_MACHINE:  *ch = 'H'; *pair = CP_MACHINE;  break;
        case FEATURE_RELIC:    *ch = '*'; *pair = CP_RELIC;    break;
        case FEATURE_TOWN_GATE:*ch = 'Y'; *pair = CP_GATE;     break;
        case FEATURE_TOLL:     *ch = '$'; *pair = CP_GOLD;     break;
        case FEATURE_CONSOLE:  *ch = 'C'; *pair = CP_MACHINE;  break;
        case FEATURE_STRONGBOX:*ch = '0'; *pair = CP_GOLD;     break;
        default:               *ch = '?'; *pair = CP_FLOOR;    break;
    }
}

/* ---- one cell, resolved once, drawn three ways ----------------------
   Everything the three render modes need to know about a map cell, worked
   out in one place so they can't disagree about what is standing on what.
   Priority runs terrain -> feature -> item -> monster -> player, and only
   terrain survives when the cell is remembered rather than currently seen. */
typedef struct {
    bool     seen, visible;
    ArtId    terrain;
    bool     has_occupant;
    ArtId    occupant;
    uint32_t glyph;       /* block-mode glyph: the occupant's, or the terrain's */
    chtype   text_ch;     /* text-mode glyph and colour pair, unchanged from */
    int      text_pair;   /* the original renderer */
    bool     bold;
    /* Take the foreground from `text_pair` rather than from the art.
     *
       The party are all drawn as '@' and told apart by colour alone -- six
       pairs, deliberately none of them the player's cyan. Text mode has always
       honoured that because it draws straight from `text_pair`; the graphical
       modes took their foreground from the ArtId, and every hero is
       ART_PLAYER, so all five hires came out the same colour. Reported from
       play, against a text-mode screenshot where they are all different.

       Set only for bodies. Monsters keep the art's colours on purpose: their
       sprites are drawn to be told apart by shape, which is the thing tiles
       can do that text cannot. */
    bool     tint_by_pair;
    /* Heard but not seen -- a monster somebody's `hearing_radius` reaches
       through a wall. Drawn as a mark on the square rather than as the
       monster, because the sense reports a position and not an identity.
       See vision.c. */
    bool     heard;
} CellView;

/* The tint a district puts on its own plain ground, and whether it glows.
   Bold is the second axis: eight terminal colours will not separate ten
   districts on their own, and "lit" versus "dim" is exactly the right
   distinction for mycelium-against-mire or crystal-against-swamp. */
/* The wash colour for a floor's own biome. Shared with the minimap and the
   art renderers so one floor is one colour wherever it is drawn. */
int biome_color_pair(Biome biome) {
    switch (biome) {
        case BIOME_JUNGLE:     return CP_BIOME_JUNGLE;
        case BIOME_INDUSTRIAL: return CP_BIOME_INDUSTRIAL;
        case BIOME_RUINS:      return CP_BIOME_RUINS;
        case BIOME_WASTES:     return CP_BIOME_WASTES;
        case BIOME_ABYSS:      return CP_BIOME_ABYSS;
        default:               return -1;
    }
}

int district_color_pair(int kind) {
    int pair; bool bright;
    extern bool district_tint_impl(int kind, int *pair, bool *bright);
    return district_tint_impl(kind, &pair, &bright) ? pair : -1;
}

bool district_tint_impl(int kind, int *pair, bool *bright) {
    switch (kind) {
        case DIST_JUNGLE:   *pair = CP_DIST_JUNGLE;   *bright = false; return true;
        case DIST_SEA:      *pair = CP_DIST_SEA;      *bright = true;  return true;
        case DIST_SWAMP:    *pair = CP_DIST_SWAMP;    *bright = false; return true;
        case DIST_RUINS:    *pair = CP_DIST_RUINS;    *bright = false; return true;
        case DIST_MYCELIUM: *pair = CP_DIST_MYCELIUM; *bright = true;  return true;
        case DIST_HIVE:     *pair = CP_DIST_HIVE;     *bright = false; return true;
        case DIST_MIRE:     *pair = CP_DIST_MIRE;     *bright = false; return true;
        case DIST_CRYSTAL:  *pair = CP_DIST_CRYSTAL;  *bright = true;  return true;
        case DIST_QUIET:    *pair = CP_DIST_QUIET;    *bright = false; return true;
        case DIST_STORM:    *pair = CP_DIST_STORM;    *bright = true;  return true;
        case DIST_ARENA:    *pair = CP_DIST_ARENA;    *bright = false; return true;
        case DIST_AQUEDUCT: *pair = CP_DIST_AQUEDUCT; *bright = true;  return true;
        case DIST_BLOODMARSH: *pair = CP_DIST_BLOODMARSH; *bright = false; return true;
        case DIST_PRISM:    *pair = CP_DIST_PRISM;    *bright = true;  return true;
        case DIST_ASSEMBLY: *pair = CP_DIST_ASSEMBLY; *bright = false; return true;
        case DIST_GARDEN:   *pair = CP_DIST_GARDEN;   *bright = true;  return true;
        case DIST_QUICKSAND:*pair = CP_DIST_QUICKSAND;*bright = false; return true;
        /* The watch reads as built ground gone cold; the barrow is the only
           district drawn in the dimmest colour the palette has, because it is
           the one place on a floor that is about something already over. */
        case DIST_WATCH:    *pair = CP_DIST_WATCH;    *bright = false; return true;
        case DIST_GAUNTLET: *pair = CP_DIST_GAUNTLET; *bright = false; return true;
        case DIST_EYE:      *pair = CP_DIST_EYE;      *bright = true;  return true;
        case DIST_MIRROR:   *pair = CP_DIST_MIRROR;   *bright = true;  return true;
        case DIST_PETRIFIED:*pair = CP_DIST_PETRIFIED;*bright = false; return true;
        case DIST_CHAPEL:   *pair = CP_DIST_CHAPEL;   *bright = false; return true;
        case DIST_GEOTHERMAL:*pair= CP_DIST_GEOTHERMAL;*bright = true;  return true;
        case DIST_SHAFT:    *pair = CP_DIST_SHAFT;    *bright = false; return true;
        case DIST_BONEYARD: *pair = CP_DIST_BONEYARD; *bright = false; return true;
        case DIST_PROVING:  *pair = CP_DIST_PROVING;  *bright = true;  return true;
        default: return false;
    }
}

static bool district_tint(int kind, int *pair, bool *bright) {
    return district_tint_impl(kind, pair, bright);
}

/* Only the neutral ground takes a district's colour. Water stays water-
   coloured, a staircase stays a staircase: those carry information, and
   overpainting them to make a place look pretty would cost more than it
   buys. */
static bool tile_takes_district_tint(TileType t) {
    return t == TILE_FLOOR || t == TILE_WALL || t == TILE_DECOR || t == TILE_THICKET;
}

/* How much stone the growth covers. A fifth of a floor outside a district
   reads as "something is growing here"; two thirds inside one reads as "this
   is a jungle" while still leaving walls and paving visible through it. */
#define BIOME_PATCH_PCT 20
#define DIST_PATCH_PCT  66

/* A stable 0..99 for a tile: same every frame, different every floor, and
   clumped rather than salt-and-pepper.
 *
   The clumping is the point. A per-tile random value speckles evenly and
   looks like television static; growth grows in patches. Mixing a coarse
   value shared by a 3x2 block with a little per-tile jitter gives patches
   with ragged edges for the cost of two multiplies. */
static unsigned tile_hash(unsigned x, unsigned y, unsigned salt) {
    unsigned h = x * 73856093u ^ y * 19349663u ^ salt * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

unsigned tile_speckle(const Map *m, int x, int y) {
    unsigned salt   = (unsigned)m->floor_num * 2654435761u;
    unsigned coarse = tile_hash((unsigned)x / 3u, (unsigned)y / 2u, salt) % 100u;
    unsigned fine   = tile_hash((unsigned)x,      (unsigned)y,      salt) % 100u;
    return (coarse * 3u + fine) / 4u;
}

/* Which layer wins on a tile: 0 bare stone, 1 the floor's biome showing
   through, 2 a wild district. Exported so tests can measure how much of a
   floor is actually growth rather than how much *could* be -- the first
   version of this covered everything and turned a grey floor green, which
   only became obvious by looking at it. */
int terrain_wash_at(const Map *m, int x, int y) {
    if (m->floor_num <= 0) return 0;
    if (!tile_takes_district_tint(m->tiles[y][x].type)) return 0;

    unsigned sp = tile_speckle(m, x, y);
    int pair; bool bright;
    if (district_tint(district_at(m, x, y), &pair, &bright) && sp < DIST_PATCH_PCT)
        return 2;
    if (biome_color_pair(m->biome) > 0 && sp < BIOME_PATCH_PCT)
        return 1;
    return 0;
}

static void cell_terrain(const Map *m, int wx, int wy, CellView *cv) {
    const Tile *t = &m->tiles[wy][wx];
    cv->seen         = t->seen;
    cv->visible      = t->visible;
    cv->terrain      = art_for_tile((int)t->type);
    cv->has_occupant = false;
    cv->occupant     = ART_UNKNOWN;
    cv->bold         = false;
    cv->glyph        = art_def(cv->terrain)->glyph;
    tile_glyph(t->type, &cv->text_ch, &cv->text_pair);

    if (m->floor_num > 0 && tile_takes_district_tint(t->type)) {
        /* Growth in patches, not a wash.
         *
           The first cut of this coloured every neutral tile with the floor's
           biome, which fixed "the floor is grey" by making the floor green
           instead -- one flat colour swapped for another. A jungle grown over
           a city is not green: it is stone with green *through* it, and the
           stone showing between the growth is what makes the growth read as
           growth. Same for a district: a jungle quarter is a quarter with a
           lot of jungle in it, not a green rectangle.

           So both layers are speckled against a stable per-tile value, and
           the difference between "biome" and "district" is how much of the
           stone they cover -- about a fifth against about two thirds. */
        unsigned sp = tile_speckle(m, wx, wy);

        int biome_pair = biome_color_pair(m->biome);
        if (biome_pair > 0 && sp < BIOME_PATCH_PCT) {
            cv->text_pair = biome_pair;
            cv->bold = false;
        }

        int pair; bool bright;
        if (district_tint(district_at(m, wx, wy), &pair, &bright)
            && sp < DIST_PATCH_PCT) {
            cv->text_pair = pair;
            cv->bold = bright;
        }
    }
}

static void cell_put_occupant(CellView *cv, ArtId art, uint32_t glyph,
                              chtype text_ch, int text_pair, bool bold) {
    cv->has_occupant = true;
    cv->occupant     = art;
    cv->glyph        = glyph;
    cv->text_ch      = text_ch;
    cv->text_pair    = text_pair;
    cv->bold         = bold;
    cv->tint_by_pair = false;
}

/* A body: same as above, and the graphical modes take its colour from the
   pair the game chose rather than from the sprite. */
static void cell_put_body(CellView *cv, chtype text_ch, int text_pair) {
    cell_put_occupant(cv, ART_PLAYER, '@', text_ch, text_pair, true);
    cv->tint_by_pair = true;
}

static bool feature_is_drawn(const MapFeature *f) {
    /* A used feature is spent and vanishes -- except the merchant, who is a
       person and stays put after you've traded with him. */
    return !(f->used && f->type != FEATURE_MERCHANT);
}

static void cell_apply_feature(const MapFeature *f, CellView *cv) {
    chtype ch; int pair;
    feature_glyph(f->type, &ch, &pair);
    ArtId a = art_for_feature((int)f->type);
    cell_put_occupant(cv, a, art_def(a)->glyph, ch, pair, true);
}

static void cell_apply_item(const FloorItem *fi, CellView *cv) {
    ArtId a    = fi->is_gold ? ART_GOLD : (fi->is_key ? ART_KEY : ART_POTION);
    int   pair = fi->is_gold ? CP_GOLD  : (fi->is_key ? CP_KEY  : CP_POTION);
    chtype ch  = fi->is_gold ? '$'      : (fi->is_key ? '/'     : '!');
    cell_put_occupant(cv, a, art_def(a)->glyph, ch, pair, false);
}

static void cell_apply_monster(const Monster *mo, CellView *cv) {
    ArtId a = art_for_monster(mo->color_pair, mo->is_boss);
    /* Block mode keeps the monster's own letter rather than a per-faction
       symbol: at one cell per monster the letter is the only thing that
       tells a rat from a rot-hound. */
    cell_put_occupant(cv, a, (uint32_t)(unsigned char)mo->glyph,
                      (chtype)mo->glyph, mo->color_pair,
                      mo->is_boss || mo->is_elite);
}

/* What colour an undriven body draws in. The character has no colour of
   their own -- CP_PLAYER is reserved for whoever is holding the role, so that
   exactly one '@' on screen is ever you -- so they borrow the last companion
   colour when the AI has them. */
static int hero_glyph_pair(const Hero *c) {
    return c->color_pair > 0 ? c->color_pair : CP_MON_RUINS;
}

static void cell_apply_player(CellView *cv) {
    cell_put_body(cv, '@', CP_PLAYER);
}

/* Whole-viewport scratch, filled once per frame. Keeping it out of the
   per-cell path is what stops the renderer going quadratic: the entity
   lists are walked once each, not once per cell. */
static CellView g_cells[MAP_H_MAX][MAP_W_MAX];

/* A square you cannot see but somebody can hear. Drawn on the remembered
   terrain, so the mark reads as "something is there" rather than replacing
   what you know is there -- and never on a visible square, where the monster
   itself is already drawn. */
static void cell_apply_heard(CellView *cv) {
    cv->heard = true;
    /* Drawn as an *occupant*, not by clearing one.
     *
       The first version set `has_occupant = false` and `seen = true`, on the
       reasoning that hearing reports a position rather than an identity and
       that an unseen cell draws nothing. Both halves were wrong, and in
       different modes.

       `seen = true` makes paint_cell and the tile compose loop draw the
       *terrain* -- the real terrain, of a square the player has never seen. So
       the sense that was supposed to say "something is behind that wall" was
       also quietly saying what the floor back there is made of, in block mode
       through the cell's background colour and in tile mode completely.

       And `has_occupant = false` meant tile mode drew no mark at all: it
       composes terrain sprite plus occupant sprite and never looks at `glyph`,
       so the one mode where the leak was total was also the one where nothing
       appeared to explain it.

       So: a real occupant with its own sprite, and `seen` left alone. The
       callers below draw the mark on blank ground when the square is unseen
       and over remembered terrain when it is not, which is the right answer
       both times -- terrain you have already walked past is not a secret. */
    cv->has_occupant = true;
    cv->occupant = ART_HEARD;
    cv->glyph = '?';
    cv->text_ch = '?';
    cv->text_pair = CP_HEARD;
    cv->bold = true;
}

static void build_cells(const Map *m, const Player *p, int camera_x, int camera_y) {
    /* Set from the map every frame rather than once on arrival: it is one
       assignment, and it means no code path -- resume, recall, the display
       screen's live preview -- can leave the art table tinted for a floor
       the player is no longer standing on. Town (floor 0) has no biome and
       takes none. */
    art_set_biome(m->floor_num > 0 ? (int)m->biome : ART_TINT_CITY);

    for (int vy = 0; vy < VIEW_H; vy++) {
        int wy = camera_y + vy;
        if (wy < 0 || wy >= MAP_H) continue;
        for (int vx = 0; vx < VIEW_W; vx++) {
            int wx = camera_x + vx;
            if (wx < 0 || wx >= MAP_W) continue;
            cell_terrain(m, wx, wy, &g_cells[wy][wx]);
        }
    }

    #define IN_VIEW(px, py) ((px) - camera_x >= 0 && (px) - camera_x < VIEW_W && \
                             (py) - camera_y >= 0 && (py) - camera_y < VIEW_H && \
                             (px) >= 0 && (px) < MAP_W && (py) >= 0 && (py) < MAP_H)

    for (int i = 0; i < m->feature_count; i++) {
        const MapFeature *f = &m->features[i];
        if (!IN_VIEW(f->x, f->y) || !m->tiles[f->y][f->x].visible) continue;
        if (!feature_is_drawn(f)) continue;
        cell_apply_feature(f, &g_cells[f->y][f->x]);
    }
    for (int i = 0; i < m->item_count; i++) {
        const FloorItem *fi = &m->items[i];
        if (fi->used || !IN_VIEW(fi->x, fi->y) || !m->tiles[fi->y][fi->x].visible) continue;
        cell_apply_item(fi, &g_cells[fi->y][fi->x]);
    }
    for (int i = 0; i < m->monster_count; i++) {
        const Monster *mo = &m->monsters[i];
        if (!mo->alive || !IN_VIEW(mo->x, mo->y) || !m->tiles[mo->y][mo->x].visible) continue;
        cell_apply_monster(mo, &g_cells[mo->y][mo->x]);
    }
    /* And the ones nobody can see. Same loop shape, inverted visibility test --
       vision_heard_at() has already decided which those are. */
    for (int i = 0; i < m->monster_count; i++) {
        const Monster *mo = &m->monsters[i];
        if (!mo->alive || !IN_VIEW(mo->x, mo->y) || m->tiles[mo->y][mo->x].visible) continue;
        if (vision_heard_at(mo->x, mo->y)) cell_apply_heard(&g_cells[mo->y][mo->x]);
    }
    /* Hired heroes, after monsters so a companion standing on one is what
       you see, and before the player so the player always wins their own
       square. Same '@' as the player -- colour is what tells them apart. */
    if (m->floor_num != 0) {   /* they wait at the temple mouth, not in town */
        for (int i = 0; i < MAX_PARTY; i++) {
            const Hero *c = &p->party[i];
            /* `c == driven`, not `i == p->controlled` -- see the same fix in
               companions_take_turn(). `controlled` can name a body that has
               stopped standing, and hero_driven() falls back; comparing the
               raw index draws the body the human is actually driving as though
               it were a hire. Harmless here only because the player is drawn
               over it afterwards, which is not a reason to leave it. */
            if (c == hero_driven_c(p) || !hero_is_up(c)) continue;
            if (!IN_VIEW(c->x, c->y) || !m->tiles[c->y][c->x].visible) continue;
            /* cell_put_body, not cell_put_occupant: the difference is the
               tint flag, and this is the loop that actually draws the party on
               the map. The first version of the colour fix converted only the
               single-cell path the animation code uses, so the graphical modes
               went on drawing five identical hires and the fix looked like it
               had done nothing. Two call sites, one of them converted, is a
               fix that reports success and changes nothing. */
            cell_put_body(&g_cells[c->y][c->x], '@', hero_glyph_pair(c));
        }
    }

    /* Who is "you" on screen follows the role, not the character.
     *
       The character used to be drawn here, separately from the hires and in
       CP_PLAYER -- the player's own colour. So when somebody else held the
       role there were two white '@'s on the floor and no way to tell which
       one the keys moved, and because the character's AI walks them toward
       the party, the twin turned up *next to you*. They are drawn by the loop
       above now, in their own colour, like every other undriven body. There
       is only one CP_PLAYER on screen and it is always you. */
    {
        int hx = party_x(p), hy = party_y(p);
        if (IN_VIEW(hx, hy)) cell_apply_player(&g_cells[hy][hx]);
    }

    #undef IN_VIEW
}

/* The same resolution for a single cell, used by the animation code to put
   back what a transient glyph was drawn over. One cell against the entity
   lists is cheap; the frame builder above exists precisely so this shape
   isn't used for all of them. */
static void resolve_one_cell(const Map *m, const Player *p, int wx, int wy, CellView *cv) {
    cell_terrain(m, wx, wy, cv);
    if (!cv->visible) {
        if (vision_heard_at(wx, wy)) cell_apply_heard(cv);
        return;
    }

    for (int i = 0; i < m->feature_count; i++) {
        const MapFeature *f = &m->features[i];
        if (f->x == wx && f->y == wy && feature_is_drawn(f)) cell_apply_feature(f, cv);
    }
    for (int i = 0; i < m->item_count; i++) {
        const FloorItem *fi = &m->items[i];
        if (!fi->used && fi->x == wx && fi->y == wy) cell_apply_item(fi, cv);
    }
    for (int i = 0; i < m->monster_count; i++) {
        const Monster *mo = &m->monsters[i];
        if (mo->alive && mo->x == wx && mo->y == wy) cell_apply_monster(mo, cv);
    }
    if (m->floor_num != 0) {
        for (int i = 0; i < MAX_PARTY; i++) {
            const Hero *c = &p->party[i];
            if (c == hero_driven_c(p) || !hero_is_up(c)) continue;
            if (c->x == wx && c->y == wy)
                cell_put_body(cv, '@', hero_glyph_pair(c));
        }
    }
    /* "You" is the body holding the role, not slot 0.
     *
       This is the one that made the '@' disappear. build_cells had already
       been taught to draw the driven body, but this -- the single-cell
       version the animation code uses to put back whatever a bolt or a flash
       was drawn over -- still restored the player glyph at the character's
       square. So every animated frame rubbed out the '@' you were steering
       and painted a second one on the character standing elsewhere. Two
       symptoms, one line: the missing you, and the twin. */
    if (party_x(p) == wx && party_y(p) == wy) cell_apply_player(cv);
}

/* Paint one resolved cell at a screen position. `mode` is passed in rather
   than read from g_mode so the display screen can preview a mode the game
   isn't currently using. Tile mode is absent on purpose: it composes an
   image for the whole viewport rather than touching cells one at a time
   (see draw_map_tiles), and falls back here when it can't. */
/* Blanks one viewport cell. The map owns every square it covers, whether or
   not there is anything there to show. */
static void paint_blank(RenderMode mode, int sy, int sx) {
    if (mode == RENDER_BLOCKS) {
        TuiRGB black = { 0, 0, 0 };
        tui_put_cell(sy, sx, ' ', black, black);
        return;
    }
    attr_t attrs = COLOR_PAIR(CP_FLOOR);
    move(sy, sx);
    attron(attrs);
    addch(' ');
    attroff(attrs);
}

static void paint_cell(RenderMode mode, int sy, int sx, const CellView *cv) {
    /* Unseen used to return without drawing, which was fine while every frame
       began with erase(). It is not fine now: whatever the previous screen put
       there stays. Walking from town into the temple left the town's buildings
       showing through everywhere the new floor was still dark -- "only what is
       inside the fog of war is correct".

       An unseen cell is not "nothing to draw", it is "draw nothing here". */
    if (!cv->seen) {
        /* Heard, but never seen: the mark and nothing under it. */
        if (cv->heard) {
            if (mode == RENDER_BLOCKS) {
                TuiRGB black = { 0, 0, 0 }, fg;
                art_block_colors(ART_HEARD, false, &fg, NULL);
                tui_put_cell(sy, sx, cv->glyph, fg, black);
            } else {
                move(sy, sx);
                attron(COLOR_PAIR(cv->text_pair) | A_BOLD);
                addch(cv->text_ch);
                attroff(COLOR_PAIR(cv->text_pair) | A_BOLD);
            }
            return;
        }
        paint_blank(mode, sy, sx);
        return;
    }

    if (mode == RENDER_BLOCKS) {
        TuiRGB fg, bg;
        bool dim = !cv->visible;
        art_block_colors(cv->terrain, dim, NULL, &bg);
        art_block_colors(cv->has_occupant ? cv->occupant : cv->terrain, dim, &fg, NULL);
        /* A body's colour is the game's, not the sprite's -- see
           CellView.tint_by_pair. Without this the whole party is one colour in
           block mode while text mode gives each of them their own. */
        if (cv->tint_by_pair) {
            TuiRGB own;
            if (tui_pair_rgb(cv->text_pair, &own)) {
                if (dim) { own.r = (unsigned char)(own.r / 2);
                           own.g = (unsigned char)(own.g / 2);
                           own.b = (unsigned char)(own.b / 2); }
                fg = own;
            }
        }
        tui_put_cell(sy, sx, cv->glyph, fg, bg);
        return;
    }

    attr_t attrs = COLOR_PAIR(cv->text_pair);
    if (!cv->visible) attrs |= A_DIM;
    if (cv->bold)     attrs |= A_BOLD;
    move(sy, sx);
    attron(attrs);
    addch(cv->text_ch);
    attroff(attrs);
}

/* `right_edge` is the last column the log may write in. Most screens are the
   whole window and pass 0 for "no limit"; the play screen has a sidebar down
   its right-hand side and passes the divider column.
 *
   That distinction was missing, and the log clipped at `cols` everywhere. On
   the play screen a long line therefore ran straight through the sidebar and
   overwrote it -- reported from play, and legible in the wreckage:
   "...from the Aether-Fletched Warbow.rmour: Void-tempered Plate +20" is the
   log eating the A of "Armour:", and "...that one is yours.ushing Blow" is it
   eating the C of "Crushing Blow". The sidebar was not being clipped; it was
   being written over, one character at a time, by whatever the game last had
   to say. */
static void draw_message_log_within(int start_row, int right_edge) {
    /* Never write on or past the bottom frame line.
     *
     * Callers pass a row derived from however much content they happened to
     * draw, and several of them -- the Pit School, the Tavern, the shops --
     * were passing one that landed on the border. The symptom is a message
     * written over the frame, which reads as corruption rather than as a
     * screen being too full. Clipped here, once, rather than trusted to
     * every caller's arithmetic. */
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int limit = rows - 1;              /* the bottom frame line itself */
    if (start_row >= limit) return;

    int start = g_msg_log_count > MSG_LOG_VISIBLE ? g_msg_log_count - MSG_LOG_VISIBLE : 0;
    for (int i = start; i < g_msg_log_count; i++) {
        int y = start_row + (i - start);
        if (y >= limit) break;

        /* And not past the right edge either, for the same reason. */
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", g_msg_log[i]);
        int edge = (right_edge > 0 && right_edge < cols) ? right_edge : cols - 1;
        int room = edge - MAP_ORIGIN_X;
        if (room < 1) return;
        if ((int)strlen(buf) > room) buf[room] = '\0';
        mvprintw(y, MAP_ORIGIN_X, "%s", buf);
    }
}

static void draw_message_log_at(int start_row) {
    draw_message_log_within(start_row, 0);
}

/* Returns the key, KEY_RESIZE included, so the caller can redraw at the new
   size. Swallowing it here -- which this used to do -- left any screen that
   draws once and then waits frozen at whatever size the terminal was when it
   opened. That is not hypothetical: a browser terminal (`make serve`) starts
   the game before the browser has reported its geometry, so the first draw
   happens at a placeholder size and the real one arrives as a resize. The
   intro screen came up as two frame corners and nothing else. */
static int wait_any_key(void) {
    return getch();
}

/* Below this the frame, sidebar and message log have nowhere to go and the
   screen turns into noise. Say so plainly and wait for the window to grow,
   rather than drawing something unreadable. */
#define MIN_SCREEN_W 80
#define MIN_SCREEN_H 24

static bool screen_is_too_small(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if (rows >= MIN_SCREEN_H && cols >= MIN_SCREEN_W) return false;

    erase();
    mvprintw(0, 0, "Window too small.");
    mvprintw(1, 0, "Need %dx%d, have %dx%d.", MIN_SCREEN_W, MIN_SCREEN_H, cols, rows);
    mvprintw(2, 0, "Make it bigger to play.");
    refresh();
    return true;
}

/* Draws a line-art box around the whole usable terminal, with `title`
   embedded in the top edge. Menu-style screens call this once after
   erase() and then draw their content starting at row 2, col 2, leaving a
   one-cell margin inside the frame. `attrs` are the border/title color --
   pass a plain COLOR_PAIR(CP_FRAME) normally, or something alarming (red,
   reversed) for the one-off damage-flash pass in draw_game_screen. */
static void draw_frame(const char *title, attr_t attrs) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    attron(attrs);
    mvhline(0, 1, '-', cols - 2);
    mvhline(rows - 1, 1, '-', cols - 2);
    mvvline(1, 0, '|', rows - 2);
    mvvline(1, cols - 1, '|', rows - 2);
    mvaddch(0, 0, '+');
    mvaddch(0, cols - 1, '+');
    mvaddch(rows - 1, 0, '+');
    mvaddch(rows - 1, cols - 1, '+');
    attroff(attrs);

    if (title && title[0]) {
        attron(attrs | A_BOLD);
        mvprintw(0, 2, " %s ", title);
        attroff(attrs | A_BOLD);
    }
}

/* A labelled `[====------]` gauge, colored green/yellow/red by fraction
   full. Used for HP and XP everywhere they're shown. */
static void draw_bar(int y, int x, int width, const char *label, int cur, int max) {
    if (max < 1) max = 1;

    /* Shrink to the room that is actually there. `width` is what the layout
       would like; at the minimum 80 columns the sidebar has about twenty, and
       a fixed 18-wide bar plus its label and numbers ran off the right of the
       screen -- losing the closing bracket and the "34/56" that is the only
       part anyone reads. A shorter bar with its numbers intact is strictly
       better than a long one with them missing. */
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)rows;
    char tail[32];
    snprintf(tail, sizeof(tail), "] %d/%d", cur, max);
    int fixed = (int)strlen(label) + 2 + (int)strlen(tail);   /* label, space, '[', tail */
    int room = cols - x - 1 - fixed;
    if (room < width) width = room;
    if (width < 4) width = 4;
    if (cols - x - 1 < fixed + 4) return;    /* no honest way to draw it */

    int pct = cur * 100 / max;
    int color = pct <= 25 ? CP_UI_WARN : (pct <= 60 ? CP_HP_MID : CP_HP_OK);
    int filled = cur * width / max;
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;

    mvprintw(y, x, "%s", label);
    int bx = x + (int)strlen(label) + 1;
    mvaddch(y, bx, '[');
    attron(COLOR_PAIR(color) | A_BOLD);
    for (int i = 0; i < width; i++) {
        mvaddch(y, bx + 1 + i, i < filled ? '=' : '-');
    }
    attroff(COLOR_PAIR(color) | A_BOLD);
    mvprintw(y, bx + 1 + width, "%s", tail);
}

/* " +4" for a piece the smith has been at, empty otherwise. Rotates through
   a few buffers so two can be printed in one call. */
static const char *plus_tag(int plus) {
    static char buf[4][8];
    static int which = 0;
    if (plus <= 0) return "";
    which = (which + 1) % 4;
    snprintf(buf[which], sizeof(buf[which]), " +%d", plus);
    return buf[which];
}

/* ---- long screens ------------------------------------------------------
 * The character sheet, the intro and the key reference are all taller than a
 * small window, and every one of them simply ran off the bottom: content was
 * written at absolute rows with nothing checking how many rows there were.
 * On a 24-row terminal the sheet lost its last eight lines, including the
 * run seed and the "press any key" prompt, and nothing on screen said so.
 *
 * A Page counts rows in *document* space and maps them onto the window,
 * returning -1 for anything currently scrolled out of view. Content that
 * does not fit is reachable rather than lost, and the footer says which way
 * to go.
 */
typedef struct {
    int y;        /* next document row */
    int scroll;   /* first document row currently shown */
    int top;      /* first usable screen row */
    int rows;     /* usable screen rows */
} Page;

static void page_init(Page *pg, int scroll, int top, int rows) {
    pg->y = 0; pg->scroll = scroll; pg->top = top;
    pg->rows = rows > 0 ? rows : 1;
}

/* Claims the next document row and answers where to draw it, or -1 if it is
   off-screen. Callers must claim the row either way, or scrolling would
   change the layout rather than move it. */
static int page_next(Page *pg) {
    int rel = pg->y++ - pg->scroll;
    return (rel < 0 || rel >= pg->rows) ? -1 : pg->top + rel;
}

/* Claims a row and prints into it, truncated to what the window can hold.
   Truncation matters as much as the scrolling: a line one character too long
   wraps onto the next row, which shifts everything below it and writes over
   the frame -- so an over-long string does not just look wrong, it corrupts
   the rest of the screen. Cutting it is the lesser failure, and it is
   visible, which the wrap was not. */
static void page_print(Page *pg, int col, const char *fmt, ...) {
    int y = page_next(pg);
    if (y < 0) return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)rows;
    int room = cols - col - 2;          /* leave the frame's right edge alone */
    if (room < 1) return;
    if ((int)strlen(buf) > room) buf[room] = '\0';

    mvprintw(y, col, "%s", buf);
}

/* Same truncation, without a Page behind it. The sidebar needs it for the
   same reason: at the minimum 80 columns the map takes most of the width and
   a line like "Recall charms: 0   Keys: 0" is wider than what is left, so it
   wrapped back over the map. */
static void mvprintw_clip(int y, int x, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)rows;
    int room = cols - x - 1;
    if (room < 1) return;
    if ((int)strlen(buf) > room) buf[room] = '\0';
    mvprintw(y, x, "%s", buf);
}

#define PLINE(pg, ...)          page_print(&(pg), 2, __VA_ARGS__)
#define PLINE_AT(pg, col, ...)  page_print(&(pg), (col), __VA_ARGS__)

/* How far down this document can usefully be scrolled. */
static int page_max_scroll(const Page *pg) {
    int over = pg->y - pg->rows;
    return over > 0 ? over : 0;
}

/* Draws the scroll footer and takes one key. Returns the new scroll offset,
   or -1 when the player is done with the screen. */
static int page_handle_key(const Page *pg, int scroll) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int max_scroll = page_max_scroll(pg);

    int left = max_scroll - scroll;
    attron(A_DIM);
    if (left > 0) {
        mvprintw_clip(rows - 2, 2, "-- %d more line%s below -- up/down or j/k, any other key to return --",
                 left, left == 1 ? "" : "s");
    } else if (max_scroll > 0) {
        mvprintw_clip(rows - 2, 2, "-- end -- up/down or j/k to go back, any other key to return --");
    } else {
        mvprintw_clip(rows - 2, 2, "Press any key to return...");
    }
    attroff(A_DIM);
    refresh();

    int ch = wait_any_key();
    if (ch == KEY_RESIZE) return scroll;
    if (max_scroll > 0) {
        if (ch == KEY_DOWN || ch == 'j') return scroll < max_scroll ? scroll + 1 : scroll;
        if (ch == KEY_UP   || ch == 'k') return scroll > 0 ? scroll - 1 : scroll;
        if (ch == KEY_NPAGE || ch == ' ') {
            int n = scroll + pg->rows;
            return n > max_scroll ? max_scroll : n;
        }
        if (ch == KEY_PPAGE) {
            int n = scroll - pg->rows;
            return n < 0 ? 0 : n;
        }
    }
    return -1;
}

void screen_intro(void) {
    int scroll = 0;
    for (;;) {
        if (screen_is_too_small()) { getch(); continue; }

        erase();
        draw_frame("A E T H E R   D E S C E N T", COLOR_PAIR(CP_FRAME) | A_BOLD);

        int rows_, cols_;
        getmaxyx(stdscr, rows_, cols_);
        (void)cols_;
        Page pg;
        page_init(&pg, scroll, 2, rows_ - 4);

        PLINE_AT(pg, 4, "The ether-ship's boilers gave out somewhere over the cloud line,");
        PLINE_AT(pg, 4, "and what was left of her came down through the canopy in pieces.");
        PLINE_AT(pg, 4, "You walked out of the wreck alone, and three days later the");
        PLINE_AT(pg, 4, "jungle finally let you go, into a city that is on no Imperial");
        PLINE_AT(pg, 4, "chart: brass domes gone green, ether-lanterns still guttering,");
        PLINE_AT(pg, 4, "vine roots thick as a man's waist through every window. Somebody");
        PLINE_AT(pg, 4, "still lives here, and they still take good coin.");
        (void)page_next(&pg);
        PLINE_AT(pg, 4, "At the heart of the place a temple sinks into the ground rather");
        PLINE_AT(pg, 4, "than rising from it -- a stair spiralling down through flooded");
        PLINE_AT(pg, 4, "ruins, foundry heat, salt wastes and worse, past where torchlight");
        PLINE_AT(pg, 4, "has any business reaching. The locals call the bottom of it the");
        PLINE_AT(pg, 4, "Deep Well, and they say it in the tone people use for a debt.");
        (void)page_next(&pg);
        PLINE_AT(pg, 4, "One hundred floors down, or wherever it actually ends. Nobody");
        PLINE_AT(pg, 4, "has come back up to say which.");
        for (int b_ = 0; b_ < 2; b_++) (void)page_next(&pg);

        attron(A_BOLD);
        PLINE_AT(pg, 4, "Buy what you can afford. Carry what you can lift. Go down.");
        attroff(A_BOLD);
        for (int b_ = 0; b_ < 2; b_++) (void)page_next(&pg);
        /* Kept under 74 characters so they fit inside the frame at the
           minimum 80-column width. They used to run to 82, wrap, and push
           every line below them down over the border. */
        PLINE_AT(pg, 4, "Move: hjkl / arrows / yubn (diagonals)");
        PLINE_AT(pg, 4, "Walk into an enemy to attack it. Into a door to go inside.");
        PLINE_AT(pg, 4, "? : the full key list, any time, in town or below.");
        (void)page_next(&pg);
        PLINE_AT(pg, 4, "x : auto-explore -- hunts, loots, heals, avoids hazards.");
        PLINE_AT(pg, 4, "m : cast a spell    s : recast the last    f : fire ranged");
        PLINE_AT(pg, 4, "a : your class ability, free but on a cooldown");
        PLINE_AT(pg, 4, "i : inventory       r : recall charm");
        PLINE_AT(pg, 4, "M : the whole revealed floor at once, scaled down");
        (void)page_next(&pg);
        PLINE_AT(pg, 4, "The Inn (I) in town rests you, banks your progress, and takes");
        PLINE_AT(pg, 4, "a fifth of your gold. Die and you wake there as you were --");
        PLINE_AT(pg, 4, "except in Hardcore, where it only heals and death is the end.");
        for (int b_ = 0; b_ < 2; b_++) (void)page_next(&pg);
        PLINE_AT(pg, 4, "Press d for display settings (%s backend, %s).",
                  tui_caps()->backend, tui_graphics_name(tui_caps()->best));
        PLINE_AT(pg, 4, "Press any other key to begin...");

        /* Scroll rather than truncate: the intro is the only place the
           controls are spelled out, and on a short window it was the tail --
           the Inn, Hardcore, the display key -- that fell off. */
        int max_scroll = page_max_scroll(&pg);
        attron(A_DIM);
        if (max_scroll > scroll)
            mvprintw_clip(rows_ - 2, 4, "-- more below: up/down or j/k --");
        attroff(A_DIM);
        refresh();

        int ch = getch();
        if (ch == KEY_RESIZE) continue;
        if (max_scroll > 0) {
            if (ch == KEY_DOWN || ch == 'j') { if (scroll < max_scroll) scroll++; continue; }
            if (ch == KEY_UP   || ch == 'k') { if (scroll > 0) scroll--; continue; }
            if (ch == KEY_NPAGE) { scroll += pg.rows; if (scroll > max_scroll) scroll = max_scroll; continue; }
            if (ch == KEY_PPAGE) { scroll -= pg.rows; if (scroll < 0) scroll = 0; continue; }
        }
        if (ch == 'd' || ch == 'D') {
            screen_display_settings();
            continue;   /* come back to the intro once they're done */
        }
        return;
    }
}

/* The thirty, by name. Not static: main.c says which one just changed. */
const char *const ATTR_NAMES[ATTR_COUNT] = {
    "Might", "Brawn", "Agility", "Reflexes", "Precision",
    "Grit", "Fortitude", "Stamina", "Balance", "Vision",
    "Hearing", "Intellect", "Cunning", "Memory", "Resolve",
    "Instinct", "Charm", "Guile", "Presence", "Empathy",
    "Tech-Wit", "Crafting", "Chemistry", "Scribing", "Aether-Sense",
    "Conduit", "Resonance", "Warding", "Fortune", "Jinx",
};

void screen_character_sheet(const Player *p, const Hero *actor) {
    int scroll = 0;
    for (;;) {
        if (screen_is_too_small()) { getch(); continue; }

        char title[80];
        snprintf(title, sizeof(title), "CHARACTER SHEET -- %s, the %s", actor->name, CLASS_TABLE[actor->class_id].name);
        erase();
        draw_frame(title, COLOR_PAIR(CP_FRAME));

        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        (void)cols;

        Page pg;
        page_init(&pg, scroll, 2, rows - 4);   /* frame, title, footer, frame */
        int y;

        PLINE(pg, "\"%s\" -- best with %s: floor %d",
              title_for_floor(load_highscore()), CLASS_TABLE[actor->class_id].name, class_best_floor(actor->class_id));
        PLINE(pg, "Level %d   Floor %d (best %d)   Kills %d   Turns %d",
              actor->level, p->floor, p->deepest_floor, actor->kills, p->turns);
        if ((y = page_next(&pg)) >= 0) draw_bar(y, 2, 18, "HP", actor->hp, actor->maxhp);
        if ((y = page_next(&pg)) >= 0) draw_bar(y, 2, 18, "XP", actor->xp, actor->xp_next);
        PLINE(pg, "Gold %d   Mode: %s", p->gold, difficulty_name(p->difficulty));

        /* Income that did not come out of a monster's purse. Only shown once
           there is some, on the same rule as the materials and the larder. */
        if (p->kitchen_earned > 0 || p->levels_sold > 0) {
            if (p->kitchen_earned > 0)
                PLINE(pg, "Cooked for: %ld gold   (house standing %d -- %s)",
                      p->kitchen_earned, p->kitchen_rep, kitchen_rep_title(p->kitchen_rep));
            if (p->levels_sold > 0)
                PLINE(pg, "Sold to the market: %d level%s for %ld gold",
                      p->levels_sold, p->levels_sold == 1 ? "" : "s", p->levels_sold_gold);
        }

        if ((y = page_next(&pg)) >= 0) { attron(A_BOLD); mvprintw_clip(y, 2, "-- Gear --"); attroff(A_BOLD); }
        PLINE(pg, "Weapon: %s%s (+%d atk)", actor->weapon_name, plus_tag(actor->weapon_plus), actor->weapon_bonus);
        PLINE(pg, "Armour: %s%s (+%d def)", actor->armor_name, plus_tag(actor->armor_plus), actor->armor_bonus);
        PLINE(pg, "Ring:    %s", actor->ring_name[0] ? actor->ring_name : "(none)");
        PLINE(pg, "Trinket: %s", actor->trinket_name[0] ? actor->trinket_name : "(none)");
        if (actor->ranged_type != RANGED_NONE)
            PLINE(pg, "Ranged:  %s (%s)", actor->ranged_name, ranged_type_name(actor->ranged_type));
        PLINE(pg, "Ability: %s -- %s", ability_name(actor->ability_id), ability_desc(actor->ability_id));

        if ((y = page_next(&pg)) >= 0) { attron(A_BOLD); mvprintw_clip(y, 2, "-- Gear Sets --"); attroff(A_BOLD); }
        bool any_set = false;
        for (int b = 0; b < BIOME_COUNT; b++) {
            int count = (actor->weapon_set == b) + (actor->armor_set == b) + (actor->ring_set == b) + (actor->trinket_set == b);
            if (count == 0) continue;
            any_set = true;
            if (count < 2) {
                PLINE(pg, "%-14s %d/4 pieces", set_name(b), count);
            } else if (count < 4) {
                PLINE(pg, "%-14s %d/4 pieces (+8 atk, +8 def)", set_name(b), count);
            } else {
                const char *extra;
                switch (b) {
                    case BIOME_JUNGLE:     extra = "+14% evasion"; break;
                    case BIOME_INDUSTRIAL: extra = "+14 more def"; break;
                    case BIOME_RUINS:      extra = "+14% ward"; break;
                    case BIOME_WASTES:     extra = "+14% hazard resist"; break;
                    default:               extra = "+14% crit"; break;
                }
                PLINE(pg, "%-14s 4/4 pieces (+8 atk, +8 def, %s)", set_name(b), extra);
            }
        }
        if (!any_set) PLINE(pg, "(none worn yet -- found only in the dungeon)");

        if ((y = page_next(&pg)) >= 0) { attron(A_BOLD); mvprintw_clip(y, 2, "-- Combat --"); attroff(A_BOLD); }
        PLINE(pg, "ATK %d   DEF %d", hero_eff_atk(actor), hero_eff_def(actor));
        PLINE(pg, "Crit %d%%   Evasion %d%%   Ward %d%%   Jinx-on-hit %d%%",
              effective_stat(actor, ACC_CRIT), effective_stat(actor, ACC_EVASION),
              effective_stat(actor, ACC_WARD), actor->jinx_pct);

        if ((y = page_next(&pg)) >= 0) { attron(A_BOLD); mvprintw_clip(y, 2, "-- Utility --"); attroff(A_BOLD); }
        PLINE(pg, "FOV radius %d   Hearing radius %d", actor->fov_radius, actor->hearing_radius);
        PLINE(pg, "Hazard resist %d%%   Aggro reduction -%d", actor->hazard_resist_pct, actor->aggro_reduction);
        PLINE(pg, "Gold find +%d%%   XP bonus +%d%%", effective_stat(actor, ACC_GOLD), effective_stat(actor, ACC_XP));
        PLINE(pg, "Shop discount %d%%   Ambush resist %d%%", actor->shop_discount_pct, actor->ambush_resist_pct);

        if ((y = page_next(&pg)) >= 0) { attron(A_BOLD); mvprintw_clip(y, 2, "-- Magic & Luck --"); attroff(A_BOLD); }
        PLINE(pg, "School: %-12s Spell power +%d%%   Ranged damage +%d%%",
              school_name(actor->magic_school), actor->spell_power_pct, actor->ranged_dmg_bonus_pct);
        PLINE(pg, "Potion potency +%d%%   Buff duration +%d%%", actor->potion_potency_pct, actor->buff_duration_pct);
        PLINE(pg, "Recall channel %d turns   Relic quality +%d", actor->recall_turns, actor->relic_quality_bonus);
        PLINE(pg, "Machine luck +%d%%   Fountain/shrine luck +%d%%", actor->machine_luck_pct, actor->fortune_luck_pct);
        PLINE(pg, "Scribing reveal: %s   Aether reveal: %s",
              actor->scribing_reveal ? "yes" : "no", actor->aether_reveal ? "yes" : "no");

        if ((y = page_next(&pg)) >= 0) { attron(A_BOLD); mvprintw_clip(y, 2, "-- The 30 --"); attroff(A_BOLD); }
        /* Six rows of five columns; each row is claimed whether or not it is
           on screen, so scrolling moves the grid instead of reflowing it. */
        for (int row = 0; row < 6; row++) {
            int gy = page_next(&pg);
            if (gy < 0) continue;
            for (int cc = 0; cc < 5; cc++) {
                int i = cc * 6 + row;
                if (i >= ATTR_COUNT) continue;
                mvprintw_clip(gy, 2 + cc * 16, "%-12s %d", ATTR_NAMES[i], actor->attrs[i]);
            }
        }
        (void)page_next(&pg);   /* blank */

        if ((y = page_next(&pg)) >= 0) {
            attron(A_DIM);
            mvprintw_clip(y, 2, "Run seed: %u   (relaunch with --seed %u to replay this dungeon)",
                     p->run_seed, p->run_seed);
            attroff(A_DIM);
        }

        int next = page_handle_key(&pg, scroll);
        if (next < 0) break;
        scroll = next;
    }
}

/* Picks one glyph/color to represent a block of source tiles, by priority:
   notable features first (stairs, shops, lever/door, portal), then plain
   seen floor, then seen wall, then blank for never-seen. Player and
   monster overlays are applied by the caller after this. */
static void minimap_block_glyph(const Map *m, int x0, int y0, int x1, int y1, chtype *ch, int *pair) {
    bool any_seen = false, any_floor = false, any_wall = false;
    int feature_ch = 0, feature_pair = 0;
    int hazard_ch = 0, hazard_pair = 0;

    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const Tile *t = &m->tiles[y][x];
            if (!t->seen) continue;
            any_seen = true;
            switch (t->type) {
                case TILE_STAIRS_DOWN: feature_ch = '>'; feature_pair = CP_STAIRS; break;
                case TILE_STAIRS_UP:   if (!feature_ch) { feature_ch = '<'; feature_pair = CP_STAIRS; } break;
                case TILE_QUEST_BOARD: if (!feature_ch) { feature_ch = 'Q'; feature_pair = CP_QUEST_BOARD; } break;
                case TILE_SHOP_ARCANIST: if (!feature_ch) { feature_ch = 'A'; feature_pair = CP_MAGIC; } break;
                case TILE_INN: if (!feature_ch) { feature_ch = 'I'; feature_pair = CP_HP_OK; } break;
                case TILE_TAVERN: if (!feature_ch) { feature_ch = 'V'; feature_pair = CP_MERCHANT; } break;
                case TILE_JUNKYARD: if (!feature_ch) { feature_ch = 'J'; feature_pair = CP_MACHINE; } break;
                case TILE_GLADIATOR: if (!feature_ch) { feature_ch = 'X'; feature_pair = CP_UI_WARN; } break;
                case TILE_ORACLE: if (!feature_ch) { feature_ch = 'O'; feature_pair = CP_MAGIC; } break;
                case TILE_ALTAR: if (!feature_ch) { feature_ch = 'T'; feature_pair = CP_UI_WARN; } break;
                case TILE_BAZAAR: if (!feature_ch) { feature_ch = 'Z'; feature_pair = CP_MERCHANT; } break;
                case TILE_SHOP_GENERAL:
                case TILE_SHOP_ARMORY:
                case TILE_SHOP_APOTHECARY:
                    if (!feature_ch) { feature_ch = 'S'; feature_pair = CP_SHOP; }
                    break;
                case TILE_LEVER:       if (!feature_ch) { feature_ch = 'L'; feature_pair = CP_LEVER; } break;
                case TILE_SEALED_DOOR:
                case TILE_LOCKED_DOOR:
                    if (!feature_ch) { feature_ch = 'D'; feature_pair = CP_LOCKED_DOOR; }
                    break;
                case TILE_PORTAL:      if (!feature_ch) { feature_ch = 'O'; feature_pair = CP_PORTAL; } break;

                /* Hazards are terrain, not landmarks, so they lose to a
                   staircase or a door sharing the block -- but they have to
                   beat plain floor, or the one part of the map that can kill
                   you on the way past reads as somewhere safe to walk.
                   Lava outranks miasma: it hurts more. */
                case TILE_LAVA:
                    hazard_ch = '~'; hazard_pair = CP_LAVA;
                    break;
                case TILE_MIASMA:
                    if (hazard_ch != '~') { hazard_ch = '%'; hazard_pair = CP_MIASMA; }
                    break;
                case TILE_WATER:
                    if (!hazard_ch) { hazard_ch = '~'; hazard_pair = CP_WATER; }
                    break;

                /* Thicket is a wall you can see over the top of on the
                   minimap: it reads as blocked, because it is. */
                case TILE_THICKET:
                case TILE_CRYSTAL:
                case TILE_ROD:
                case TILE_VENT:
                case TILE_WALL:        any_wall = true; break;
                default:                any_floor = true; break;
            }
        }
    }

    if (feature_ch) {
        *ch = (chtype)feature_ch;
        *pair = feature_pair;
    } else if (hazard_ch) {
        *ch = (chtype)hazard_ch;
        *pair = hazard_pair;
    } else if (any_floor || any_seen) {
        *ch = '.'; *pair = CP_FLOOR;
    } else if (any_wall) {
        *ch = '#'; *pair = CP_WALL;
    } else {
        *ch = ' '; *pair = CP_FLOOR;
        return;                       /* nothing seen here: no colour to give */
    }

    /* Districts colour the map; the floor's own biome deliberately does not.
       One minimap cell is a whole block of tiles, so the speckling the close
       view uses has nothing to speckle here -- and washing every block in the
       biome's colour just makes the map one flat colour, which is the exact
       problem the speckling exists to avoid. What is worth seeing at map
       scale is the *shape* of the places, against neutral rock. Features and
       hazards above keep their own colours: a staircase must not become
       scenery. */
    if (m->floor_num > 0 && (*ch == '.' || *ch == '#')) {
        int dk = district_at(m, (x0 + x1) / 2, (y0 + y1) / 2);
        int dpair; bool dbright;
        extern bool district_tint_impl(int kind, int *pair, bool *bright);
        if (district_tint_impl(dk, &dpair, &dbright)) *pair = dpair;
    }
}

void screen_minimap(const Map *m, const Hero *actor) {
    for (;;) {
        if (screen_is_too_small()) { getch(); continue; }

        erase();

        char title[64];
        snprintf(title, sizeof(title), "FLOOR %d MAP -- %s", m->floor_num, biome_name(m->biome));
        draw_frame(title, COLOR_PAIR(CP_FRAME));

        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        int avail_w = cols - 4;
        int avail_h = rows - 6; /* frame top/bottom + title + two legend rows + prompt */
        if (avail_w < 10) avail_w = 10;
        if (avail_h < 5) avail_h = 5;

        int block_w = (MAP_W + avail_w - 1) / avail_w;
        int block_h = (MAP_H + avail_h - 1) / avail_h;
        if (block_w < 1) block_w = 1;
        if (block_h < 1) block_h = 1;

        int out_w = (MAP_W + block_w - 1) / block_w;
        int out_h = (MAP_H + block_h - 1) / block_h;

        int origin_y = 2, origin_x = 2;

        for (int oy = 0; oy < out_h; oy++) {
            int y0 = oy * block_h, y1 = y0 + block_h; if (y1 > MAP_H) y1 = MAP_H;
            for (int ox = 0; ox < out_w; ox++) {
                int x0 = ox * block_w, x1 = x0 + block_w; if (x1 > MAP_W) x1 = MAP_W;
                chtype ch; int pair;
                minimap_block_glyph(m, x0, y0, x1, y1, &ch, &pair);
                attron(COLOR_PAIR(pair));
                mvaddch(origin_y + oy, origin_x + ox, ch);
                attroff(COLOR_PAIR(pair));
            }
        }

        int py = actor->y / block_h, px = actor->x / block_w;
        attron(COLOR_PAIR(CP_PLAYER) | A_BOLD);
        mvaddch(origin_y + py, origin_x + px, '@');
        attroff(COLOR_PAIR(CP_PLAYER) | A_BOLD);

        /* Two short rows rather than one long one: at 80 columns the single
           line wrapped and ate the row below it. */
        mvprintw_clip(origin_y + out_h + 1, origin_x,
                 "@ you  > down  < up  D door  L lever  O portal  S shop  A guild  I inn  V tavern  J junk  X pit  Q quest");
        int hz = origin_y + out_h + 2;
        attron(COLOR_PAIR(CP_LAVA));   mvaddch(hz, origin_x, '~');      attroff(COLOR_PAIR(CP_LAVA));
        mvprintw_clip(hz, origin_x + 2, "lava");
        attron(COLOR_PAIR(CP_WATER));  mvaddch(hz, origin_x + 8, '~');  attroff(COLOR_PAIR(CP_WATER));
        mvprintw_clip(hz, origin_x + 10, "water");
        attron(COLOR_PAIR(CP_MIASMA)); mvaddch(hz, origin_x + 17, '%'); attroff(COLOR_PAIR(CP_MIASMA));
        mvprintw_clip(hz, origin_x + 19, "miasma -- lava and miasma cost health to cross");
        mvprintw_clip(origin_y + out_h + 3, origin_x, "Press any key to return...");
        refresh();
        if (wait_any_key() != KEY_RESIZE) break;
    }
}

#define CLASS_PAGE_SIZE 14

bool screen_continue_prompt(void) {
    erase();
    draw_frame("A RUN AWAITS", COLOR_PAIR(CP_FRAME));
    mvprintw_clip(3, 4, "You have a suspended run in the depths.");
    mvprintw_clip(5, 4, "1) Continue where you left off");
    mvprintw_clip(6, 4, "2) Abandon it and start a new descent");
    mvprintw_clip(8, 4, "Choose (1-2): ");
    refresh();
    int ch;
    do { ch = getch(); } while (ch != '1' && ch != '2');
    return ch == '1';
}

/* A hand-laid strip with one of everything worth judging: wall against
   floor, water crossed by a bridge, lava, an actor, an item, and a bottom row
   the player is meant to read as remembered-but-out-of-sight. Small enough to
   sit under the mode list on any terminal the game will start on. */
static const char *PREVIEW_SCENE[] = {
    "wwwwwwwwwwwwwwww",
    "w..,..~~=....*.w",
    "w.@.r.~~=..!...w",
    "w..$..~~=....B.w",
    "w..^^..,.....>.w",
    "WWWWWWWWWWWWWWWW",
    NULL
};

static void preview_cell(char c, CellView *cv) {
    bool dim = false;
    if (c == 'W') { c = 'w'; dim = true; }

    int tile = TILE_FLOOR;
    switch (c) {
        case 'w': tile = TILE_WALL;        break;
        case ',': tile = TILE_DECOR;       break;
        case '"': tile = TILE_THICKET;     break;
        case '~': tile = TILE_WATER;       break;
        case '=': tile = TILE_BRIDGE;      break;
        case '^': tile = TILE_LAVA;        break;
        case '>': tile = TILE_STAIRS_DOWN; break;
        default: break;
    }

    cv->seen         = true;
    cv->visible      = !dim;
    cv->terrain      = art_for_tile(tile);
    cv->has_occupant = false;
    cv->occupant     = ART_UNKNOWN;
    cv->bold         = false;
    cv->glyph        = art_def(cv->terrain)->glyph;
    tile_glyph((TileType)tile, &cv->text_ch, &cv->text_pair);
    if (dim) return;

    switch (c) {
        case '@': cell_apply_player(cv); break;
        case 'r': cell_put_occupant(cv, ART_MON_VERMIN, 'r', 'r', CP_MON_VERMIN, false); break;
        case 'B': cell_put_occupant(cv, ART_MON_BOSS,   'B', 'B', CP_MON_BOSS,   true);  break;
        case '$': cell_put_occupant(cv, ART_GOLD,   art_def(ART_GOLD)->glyph,   '$', CP_GOLD,   false); break;
        case '!': cell_put_occupant(cv, ART_POTION, art_def(ART_POTION)->glyph, '!', CP_POTION, false); break;
        case '*': cell_put_occupant(cv, ART_RELIC,  art_def(ART_RELIC)->glyph,  '*', CP_RELIC,  true);  break;
        default: break;
    }
}

/* Draws the sample scene at (oy,ox) in `mode`, so the player can see what a
   mode looks like before committing a run to it -- the alternative being to
   start a descent, dislike it, and come back. Returns the number of rows
   used. */
static int draw_mode_preview(int oy, int ox, RenderMode mode) {
    int rows = 0, cols = 0;
    for (const char **r = PREVIEW_SCENE; *r; r++, rows++) {
        int l = (int)strlen(*r);
        if (l > cols) cols = l;
    }

    static CellView cells[8][32];
    if (rows > 8) rows = 8;
    if (cols > 32) cols = 32;
    for (int y = 0; y < rows; y++) {
        const char *row = PREVIEW_SCENE[y];
        int len = (int)strlen(row);
        for (int x = 0; x < cols; x++)
            preview_cell(x < len ? row[x] : '.', &cells[y][x]);
    }

    if (mode == RENDER_TILES) {
        int cw = 0, ch = 0;
        if (tui_cell_pixels(&cw, &ch)) {
            int pixw = cols * cw, pixh = rows * ch;
            size_t need = (size_t)pixw * (size_t)pixh * 4u;
            unsigned char *buf = calloc(need, 1);
            if (buf) {
                for (int y = 0; y < rows; y++) {
                    for (int x = 0; x < cols; x++) {
                        const CellView *cv = &cells[y][x];
                        unsigned char *corner =
                            buf + ((size_t)(y * ch) * (size_t)pixw + (size_t)(x * cw)) * 4u;
                        art_sprite_rgba(cv->terrain, !cv->visible, true, corner, cw, ch, pixw * 4);
                        if (cv->has_occupant)
                            {   /* A body wears the colour the game gave it. */
                                TuiRGB own; const TuiRGB *tint = NULL;
                                if (cv->tint_by_pair && tui_pair_rgb(cv->text_pair, &own)) tint = &own;
                                art_sprite_rgba_tinted(cv->occupant, false, false, tint,
                                                       corner, cw, ch, pixw * 4);
                            }
                    }
                }
                bool ok = tui_blit_rgba(oy, ox, buf, pixw, pixh);
                free(buf);
                if (ok) return rows;
            }
        }
        /* Fell through: show the text version rather than an empty box, so
           the preview never claims more than the terminal can do. */
        mode = RENDER_TEXT;
    }

    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            paint_cell(mode, oy + y, ox + x, &cells[y][x]);
    return rows;
}

/* One row of the key reference. `town` and `deep` say where the key does
   something, so the same table can render both screens and neither can drift
   from the other. */
typedef struct {
    const char *keys;
    const char *what;
    bool town, deep;
} HelpKey;

static const HelpKey HELP_MOVE[] = {
    { "h j k l",     "left, down, up, right",                       true,  true  },
    { "y u b n",     "the four diagonals",                          true,  true  },
    { "arrow keys",  "the same, if you would rather",               true,  true  },
    { "(into a monster)", "attack it -- there is no separate attack key", false, true },
    { ". or 5",      "wait a turn. In the mycelium, waiting hides you",   false, true },
    { "g",           "set to work on what's here -- rods, wrecks. Takes turns", false, true },
    { "(into a door)",    "enter the shop, inn or tavern",          true,  false },
};

static const HelpKey HELP_FIGHT[] = {
    { "m", "cast a spell -- your school's list, the ones you have learnt", false, true },
    { "s", "cast the last one again, without the menu",                    false, true },
    { "f", "fire the ranged weapon you are carrying",                      false, true },
    { "a", "your class ability -- one per cooldown, and it is free",        false, true },
    { "r", "crack a recall charm and channel back to town",                false, true },
};

static const HelpKey HELP_LOOK[] = {
    { "i", "inventory: drink, read, use",                       true,  true  },
    { "c", "character sheet -- every stat, and your run seed",  true,  true  },
    { "M", "the whole floor at once, scaled down",              false, true  },
    { "R", "records: this run, your bests, and your dead",      false, true  },
    { "D", "display: text, blocks or tiles, and the tile budget", true,  true  },
    { "x", "hunt and explore: walks, fights and loots for you", false, true  },
    { "?", "this",                                              true,  true  },
    { "q", "quit (a run in progress is saved, not lost)",       true,  true  },
};

static void draw_help_block(Page *pg, const char *title, const HelpKey *rows, int n, bool in_town) {
    bool any = false;
    for (int i = 0; i < n; i++) if (in_town ? rows[i].town : rows[i].deep) any = true;
    if (!any) return;

    int y = page_next(pg);
    if (y >= 0) { attron(A_BOLD); mvprintw_clip(y, 4, "%s", title); attroff(A_BOLD); }

    for (int i = 0; i < n; i++) {
        if (in_town ? !rows[i].town : !rows[i].deep) continue;
        y = page_next(pg);
        if (y < 0) continue;
        attron(COLOR_PAIR(CP_UI_HEADER));
        mvprintw_clip(y, 6, "%-20s", rows[i].keys);
        attroff(COLOR_PAIR(CP_UI_HEADER));
        mvprintw_clip(y, 28, "%s", rows[i].what);
    }
    (void)page_next(pg);   /* the gap between sections */
}

void screen_help(bool in_town) {
    int scroll = 0;
    for (;;) {
        if (screen_is_too_small()) { getch(); continue; }

        erase();
        draw_frame(in_town ? "KEYS -- in the city" : "KEYS -- in the temple",
                   COLOR_PAIR(CP_FRAME));

        int rows_, cols_;
        getmaxyx(stdscr, rows_, cols_);
        (void)cols_;
        Page pg;
        page_init(&pg, scroll, 2, rows_ - 4);

        draw_help_block(&pg, "Moving",        HELP_MOVE,  (int)(sizeof(HELP_MOVE)  / sizeof(HELP_MOVE[0])),  in_town);
        draw_help_block(&pg, "Fighting",      HELP_FIGHT, (int)(sizeof(HELP_FIGHT) / sizeof(HELP_FIGHT[0])), in_town);
        draw_help_block(&pg, "Looking about", HELP_LOOK,  (int)(sizeof(HELP_LOOK)  / sizeof(HELP_LOOK[0])),  in_town);

        int y = page_next(&pg);
        if (y >= 0) {
            attron(A_DIM);
            if (in_town) mvprintw_clip(y, 4, "The temple is the T. Everything else in the plaza is a door worth opening.");
            else         mvprintw_clip(y, 4, "Down the > stairs to go deeper. Nothing makes you fight what you can walk around.");
            attroff(A_DIM);
        }

        int next = page_handle_key(&pg, scroll);
        if (next < 0) break;
        scroll = next;
    }
}

void screen_display_settings(void) {
    RenderMode cur = load_render_mode();
    render_set_tile_pixels(load_tile_pixels());
    render_set_mode(cur);

    for (;;) {
        erase();
        draw_frame("DISPLAY", COLOR_PAIR(CP_FRAME));
        const TuiCaps *c = tui_caps();
        int r = 2;

        attron(A_BOLD); mvprintw_clip(r++, 2, "-- What this build and terminal can do --"); attroff(A_BOLD);
        mvprintw_clip(r++, 2, "Backend            : %s", c->backend);
        mvprintw_clip(r++, 2, "TERM               : %s", c->terminal);
        mvprintw_clip(r++, 2, "Pixel protocol     : %s", c->pixel_proto);
        mvprintw_clip(r++, 2, "Best graphics      : %s", tui_graphics_name(c->best));
        mvprintw_clip(r++, 2, "24-bit colour      : %s", c->truecolor ? "yes" : "no");
        mvprintw_clip(r++, 2, "UTF-8              : %s", c->utf8 ? "yes" : "no");
        r++;

        if (strcmp(c->backend, "ncurses") == 0) {
            attron(COLOR_PAIR(CP_UI_WARN));
            mvprintw_clip(r++, 2, "This is the ncurses build -- it draws characters only, whatever");
            mvprintw_clip(r++, 2, "the terminal supports. For graphics, run the notcurses build:");
            attroff(COLOR_PAIR(CP_UI_WARN));
            mvprintw_clip(r++, 4, "make notcurses && ./bin/aether-descent-nc");
            r++;
        }

        attron(A_BOLD); mvprintw_clip(r++, 2, "-- How to draw the world --"); attroff(A_BOLD);
        for (int m = 0; m < RENDER_MODE_COUNT; m++) {
            const char *why = NULL;
            bool ok = render_mode_available((RenderMode)m, &why);
            bool sel = (m == (int)cur);

            if (sel) attron(A_REVERSE);
            mvprintw_clip(r, 2, " %d) %-16s", m + 1, render_mode_name((RenderMode)m));
            if (sel) attroff(A_REVERSE);

            if (ok) {
                mvprintw_clip(r, 26, "%s", sel ? "<- in use" : "available");
                /* Tile mode's cost is worth stating in numbers: it's the one
                   mode where the window size decides whether the game feels
                   responsive, and a smaller window is the fix. */
                if (m == RENDER_TILES) {
                    int cw = 0, chp = 0;
                    if (tui_cell_pixels(&cw, &chp)) {
                        int trows, tcols, vw, vh;
                        getmaxyx(stdscr, trows, tcols);
                        compute_viewport(RENDER_TILES, trows, tcols, &vw, &vh);
                        attron(A_DIM);
                        mvprintw_clip(r, 40, "%dx%d tiles, %dx%d px per changed frame",
                                 vw, vh, vw * cw, vh * chp);
                        attroff(A_DIM);
                    }
                }
            } else {
                attron(A_DIM);
                mvprintw_clip(r, 26, "unavailable -- %s", why ? why : "");
                attroff(A_DIM);
            }
            r++;
        }

        /* Tile mode's speed control. Shown whenever the terminal could do
           tiles at all, not only while they're selected, so the trade-off is
           visible before committing to the mode. */
        if (render_mode_available(RENDER_TILES, NULL)) {
            int cw = 0, chp = 0;
            if (tui_cell_pixels(&cw, &chp)) {
                int trows, tcols, vw, vh;
                getmaxyx(stdscr, trows, tcols);
                compute_viewport(RENDER_TILES, trows, tcols, &vw, &vh);
                r++;
                mvprintw_clip(r++, 2, "Tile detail   : %d px/frame  ->  %dx%d tiles (%dx%d px, cell %dx%d)",
                         render_get_tile_pixels(), vw, vh, vw * cw, vh * chp, cw, chp);
                attron(A_DIM);
                mvprintw_clip(r++, 2, "  -/+ to trade map size for speed. Tiles are the one mode whose cost");
                mvprintw_clip(r++, 2, "  is the picture it ships to the terminal, so fewer tiles = faster.");
                attroff(A_DIM);
            }
        }

        r++;
        attron(A_BOLD); mvprintw_clip(r++, 2, "-- Preview: %s --", render_mode_name(cur)); attroff(A_BOLD);
        r += draw_mode_preview(r, 3, cur) + 1;
        attron(A_DIM);
        mvprintw_clip(r++, 2, "The bottom row is terrain you remember but cannot currently see.");
        attroff(A_DIM);

        r++;
        mvprintw_clip(r++, 2, "Press 1-%d to choose, -/+ for tile detail, Esc/q to go back.",
                 RENDER_MODE_COUNT);
        refresh();

        int ch = getch();
        if (ch == 27 || ch == 'q') return;
        if (ch == '-' || ch == '_' || ch == '+' || ch == '=') {
            /* Halve or double, so a few presses cover the whole useful range
               rather than nudging through it. */
            int px = render_get_tile_pixels();
            px = (ch == '-' || ch == '_') ? px / 2 : px * 2;
            if (px < TILE_PIXELS_MIN) px = TILE_PIXELS_MIN;
            if (px > TILE_PIXELS_MAX) px = TILE_PIXELS_MAX;
            render_set_tile_pixels(px);
            save_tile_pixels(px);
            continue;
        }
        if (ch >= '1' && ch < '1' + RENDER_MODE_COUNT) {
            RenderMode want = (RenderMode)(ch - '1');
            const char *why = NULL;
            if (render_mode_available(want, &why)) {
                cur = want;
                save_render_mode(cur);
                render_set_mode(cur);
            }
        }
    }
}

/* One line on what each of the thirty actually buys, so the school is a
   decision rather than thirty numbers and a price. Terse on purpose. */
static const char *attr_blurb(int a) {
    switch (a) {
        case ATTR_MIGHT:       return "Attack. The most direct point in the game.";
        case ATTR_BRAWN:       return "Attack, and health.";
        case ATTR_GRIT:        return "Defence. The one the deep floors ask for.";
        case ATTR_FORTITUDE:   return "Defence, and hazard resistance.";
        case ATTR_STAMINA:     return "Health.";
        case ATTR_AGILITY:
        case ATTR_REFLEXES:
        case ATTR_BALANCE:     return "Evasion -- the chance a blow simply misses.";
        case ATTR_PRECISION:   return "Critical hits, and what your starting weapon was worth.";
        case ATTR_VISION:      return "How far you see.";
        case ATTR_HEARING:     return "How far you hear what you cannot see.";
        case ATTR_INTELLECT:   return "Experience gained.";
        case ATTR_CUNNING:     return "Gold found.";
        case ATTR_MEMORY:      return "Shop prices.";
        case ATTR_RESOLVE:     return "Hazard resistance.";
        case ATTR_INSTINCT:    return "Ambush resistance.";
        case ATTR_CHARM:       return "Shop prices, hard.";
        case ATTR_GUILE:       return "Aggro -- how readily they notice you.";
        case ATTR_PRESENCE:    return "Aggro, and how the town treats you.";
        case ATTR_EMPATHY:     return "Aggro reduction.";
        case ATTR_TECH_WIT:    return "Machine luck.";
        case ATTR_CRAFTING:    return "Every piece of gear you buy is worth more.";
        case ATTR_CHEMISTRY:   return "Potions heal harder.";
        case ATTR_SCRIBING:    return "Reveals the floor on arrival, at 6+.";
        case ATTR_AETHER_SENSE:return "Reveals what is on the floor, at 6+.";
        case ATTR_CONDUIT:     return "Aether pool.";
        case ATTR_RESONANCE:   return "Spell power.";
        case ATTR_WARDING:     return "Ward -- the chance a spell glances off.";
        case ATTR_FORTUNE:     return "Critical hits, gold, and shrine luck.";
        case ATTR_JINX:        return "A curse on the things you hit.";
        default:               return "";
    }
}

/* Six runners, each drawn with the fixed odds the book is offering. The
   race itself is watched rather than resolved in a line of text: it is the
   only thing in the game you are a spectator at, and that is most of why it
   is here. */
void draw_races(const Player *p, const int *odds, const char **names, int cursor, int stake) {
    erase();
    draw_frame("THE VENUSIAN TRACK -- six runners, and a bookmaker with no illusions",
               COLOR_PAIR(CP_FRAME));

    mvprintw_clip(2, 2, "Gold: %-12d   Stake: %d   Races run this visit: %d",
                  p->gold, stake, p->races_this_visit);
    attron(A_DIM);
    mvprintw_clip(3, 2, "The book is generous early and mean late. Go back down and he forgets.");
    attroff(A_DIM);

    int row = 5;
    for (int i = 0; i < RACE_RUNNERS; i++) {
        bool sel = (i == cursor);
        if (sel) attron(A_REVERSE);
        mvprintw_clip(row++, 4, " %-22s  %2d to 1   returns %d",
                      names[i], odds[i], stake * (odds[i] + 1));
        if (sel) attroff(A_REVERSE);
    }

    row++;
    mvprintw_clip(row++, 2, "Up/Down pick   Left/Right stake +/-50   +/- stake x10   Enter backs it");
    mvprintw_clip(row++, 2, "w backs it and watches them run   Esc/q leaves");
    draw_message_log_at(row + 1);
    refresh();
}

int race_run(const char **names, const int *odds, bool watch) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)rows;

    /* Chance of winning is the inverse of the odds, roughly -- a 2-to-1
       runner wins about a third of the time. Weights, not a simulation of
       lizards: the animation is the show, the weights are the truth. */
    int weight[RACE_RUNNERS], total = 0;
    for (int i = 0; i < RACE_RUNNERS; i++) {
        weight[i] = 120 / (odds[i] + 1);
        if (weight[i] < 1) weight[i] = 1;
        total += weight[i];
    }
    int roll = rand() % total, winner = 0;
    for (int i = 0; i < RACE_RUNNERS; i++) {
        if (roll < weight[i]) { winner = i; break; }
        roll -= weight[i];
    }

    if (!watch) return winner;      /* the result is the result either way */

    /* Now stage a race that happens to end that way. Each runner creeps
       forward at random; the winner is nudged so the finish is honest to the
       roll without being obviously scripted. */
    int pos[RACE_RUNNERS] = {0};
    int track = cols - 34;
    if (track < 10) track = 10;
    if (track > 60) track = 60;

    for (int tick = 0; tick < 400; tick++) {
        erase();
        draw_frame("THEY'RE RUNNING", COLOR_PAIR(CP_FRAME));
        for (int i = 0; i < RACE_RUNNERS; i++) {
            int adv = rand() % 3;
            if (i == winner && (rand() % 100) < 45) adv++;
            pos[i] += adv;
            if (pos[i] > track) pos[i] = track;

            mvprintw_clip(4 + i * 2, 2, "%-20s", names[i]);
            for (int x = 0; x < track; x++) mvaddch(4 + i * 2, 24 + x, '.');
            attron(COLOR_PAIR(CP_MON_VERMIN) | A_BOLD);
            mvaddch(4 + i * 2, 24 + pos[i], 'r');
            attroff(COLOR_PAIR(CP_MON_VERMIN) | A_BOLD);
        }
        refresh();
        anim_pause(45);
        if (pos[winner] >= track) break;
    }

    attron(A_BOLD);
    mvprintw_clip(4 + RACE_RUNNERS * 2 + 1, 2, "%s takes it.", names[winner]);
    attroff(A_BOLD);
    mvprintw_clip(4 + RACE_RUNNERS * 2 + 2, 2, "(any key)");
    refresh();
    getch();
    return winner;
}

void draw_bank(const Player *p, const Hero *actor) {
    erase();
    draw_frame("THE BANK OF THE DEEP WELL -- a share of everything you bring up",
               COLOR_PAIR(CP_FRAME));

    mvprintw_clip(2, 2, "Gold: %-14d   Your share: x%d", p->gold, p->gold_mult);
    attron(A_DIM);
    mvprintw_clip(3, 2, "Every coin you earn from here on is multiplied -- kills, floor gold,");
    mvprintw_clip(4, 2, "bounties, machines, and the scales at the junkyard alike.");
    attroff(A_DIM);

    int row = 6;
    long price = bank_price(p->gold_mult);

    if (price < 0) {
        attron(A_BOLD);
        mvprintw_clip(row++, 2, "You hold a hundred shares. There is nothing left to sell you.");
        attroff(A_BOLD);
    } else {
        long paid = price - price * actor->shop_discount_pct / 100;
        if (paid < 0) paid = 0;

        attron(A_BOLD);
        mvprintw_clip(row++, 2, "  Next share:   x%d  ->  x%d", p->gold_mult, p->gold_mult + 1);
        mvprintw_clip(row++, 2, "  Price:        %ld gold%s", paid,
                      (long)p->gold >= paid ? "" : "   (short)");
        attroff(A_BOLD);
        row++;

        /* The next few, so the shape of the curve is visible before the first
           one is bought. A price that grows with the cube of what you already
           hold looks cheap exactly once. */
        attron(A_DIM);
        mvprintw_clip(row++, 2, "  after that:");
        for (int k = 1; k <= 4; k++) {
            long later = bank_price(p->gold_mult + k);
            if (later < 0) break;
            later -= later * actor->shop_discount_pct / 100;
            mvprintw_clip(row++, 2, "     x%-4d %ld gold", p->gold_mult + k + 1, later);
        }
        attroff(A_DIM);
    }

    row++;
    attron(A_DIM);
    mvprintw_clip(row++, 2, "The bank does not lend and it does not buy back. It sells one thing.");
    attroff(A_DIM);

    row++;
    mvprintw_clip(row++, 2, "Enter buys a share   Esc/q leaves");
    draw_message_log_at(row + 1);
    refresh();
}

/* The pass. One patron at a time, the larder down the side, and the price of
   every cut you could hand over shown before you hand it over -- the decision
   is meant to be arithmetic you can actually do, not a guess. */
void draw_kitchen(const Player *p, const Patron *pat, int served, int total,
                  int cursor, long taken) {
    erase();
    draw_frame("THE ASHFALL KITCHEN -- you hunt it, the house cooks it",
               COLOR_PAIR(CP_FRAME));

    int rep_x = kitchen_rep_permille(p->kitchen_rep);
    mvprintw_clip(2, 2, "Reputation: %-3d (%s)   Payouts x%d.%02d",
                  p->kitchen_rep, kitchen_rep_title(p->kitchen_rep),
                  rep_x / 1000, (rep_x % 1000) / 10);
    mvprintw_clip(3, 2, "Gold: %-12d   Tonight: %ld taken over %d of %d covers",
                  p->gold, taken, served, total);

    int row = 5;

    if (pat) {
        attron(A_BOLD);
        mvprintw_clip(row++, 2, "At the pass: %s", pat->name);
        attroff(A_BOLD);
        mvprintw_clip(row++, 2, "  %s", pat->line);
        attron(A_DIM);
        mvprintw_clip(row++, 2, "  (wants %s -- purse about %d)",
                      meat_name(pat->wants), pat->purse);
        attroff(A_DIM);
        row++;

        for (int g = 0; g < MEAT_GRADE_COUNT; g++) {
            bool have = p->meat[g] > 0;
            bool sel  = (g == cursor);
            if (sel) attron(A_REVERSE);
            else if (!have) attron(A_DIM);

            if (have) {
                int pay = kitchen_payout(p, pat, g);
                int rep = kitchen_rep_delta(pat, g);
                mvprintw_clip(row++, 2, "  %c %-8s x%-3d   pays %-8d  rep %+d",
                              sel ? '>' : ' ', meat_name(g), p->meat[g], pay, rep);
            } else {
                mvprintw_clip(row++, 2, "  %c %-8s --", sel ? '>' : ' ', meat_name(g));
            }

            if (sel) attroff(A_REVERSE);
            else if (!have) attroff(A_DIM);
        }

        row++;
        attron(A_DIM);
        mvprintw_clip(row++, 2, "Serving better than they asked pays a little more and pleases them a lot.");
        mvprintw_clip(row++, 2, "Serving worse pays almost nothing and the house hears about it.");
        attroff(A_DIM);
        row++;
        mvprintw_clip(row++, 2, "Up/Down choose   Enter serve   x turn them away   Esc/q close the pass");
    } else {
        /* Two different reasons to be looking at an empty pass, and they
           want different words -- "nothing to cook" sends you back down for
           meat, "already cooked" sends you back down for a new day. */
        bool bare = (meat_total(p) == 0);

        attron(A_BOLD);
        mvprintw_clip(row++, 2, bare ? "The cook looks at your empty hands."
                                     : "The room is empty and the lamps are out.");
        attroff(A_BOLD);
        row++;
        attron(A_DIM);
        if (bare) {
            mvprintw_clip(row++, 2, "Meat comes off things you killed with a blade. Spells ruin it,");
            mvprintw_clip(row++, 2, "and so do arrows -- there is nothing left on them worth serving.");
        } else {
            mvprintw_clip(row++, 2, "One service a night, and the night is over. Go back down;");
            mvprintw_clip(row++, 2, "come up with a full larder and they will be here again.");
        }
        attroff(A_DIM);
        row++;

        mvprintw_clip(row++, 2, "Larder:");
        for (int g = 0; g < MEAT_GRADE_COUNT; g++)
            mvprintw_clip(row++, 4, "%-8s x%d", meat_name(g), p->meat[g]);

        row++;
        mvprintw_clip(row++, 2, "Esc/q leaves");
    }

    draw_message_log_at(row + 1);
    refresh();
}

/* The black market. Everything it does to you is shown before you agree to
   it -- what you lose, what you are paid, and what it does to the escalation
   the monsters carry. Not as a warning: the whole point of the place is that
   it is a trade you are allowed to make badly. */
void draw_black_market(const Player *p, const Hero *actor, int count) {
    erase();
    draw_frame("THE BLACK MARKET -- it buys experience, and asks nothing",
               COLOR_PAIR(CP_FRAME));

    mvprintw_clip(2, 2, "Level %-4d  HP %d/%d   ATK/DEF %d / %d   Gold %d",
                  actor->level, actor->hp, actor->maxhp,
                  hero_eff_atk(actor), hero_eff_def(actor), p->gold);

    int row = 4;
    int max = level_sale_max(actor->level);

    if (max <= 0) {
        attron(A_BOLD);
        mvprintw_clip(row++, 2, "\"You've nothing left I want.\"");
        attroff(A_BOLD);
        row++;
        attron(A_DIM);
        mvprintw_clip(row++, 2, "There is no level below the first.");
        attroff(A_DIM);
        draw_message_log_at(row + 1);
        refresh();
        return;
    }

    if (count < 1)   count = 1;
    if (count > max) count = max;

    long price   = level_sale_batch_price(actor->level, count, p->deepest_floor);
    int  target  = actor->level - count;
    int  def_off = level_sale_def_loss(actor->level, count);

    /* The target level is the number the player is actually steering by --
       "I am 70 and I want to be 50" -- so it leads, and the count follows. */
    attron(A_BOLD);
    mvprintw_clip(row++, 2, "Sell %d level%s:   %d  ->  %d",
                  count, count == 1 ? "" : "s", actor->level, target);
    mvprintw_clip(row++, 2, "  They pay        %ld gold", price);
    attroff(A_BOLD);
    row++;

    mvprintw_clip(row++, 2, "  You give up     %d max HP   (%d -> %d)",
                  8 * count, actor->maxhp, actor->maxhp - 8 * count > 1 ? actor->maxhp - 8 * count : 1);
    mvprintw_clip(row++, 2, "                  %d attack", count);
    if (def_off > 0)
        mvprintw_clip(row++, 2, "                  %d defence", def_off);
    if (actor->xp > 0)
        mvprintw_clip(row++, 2, "                  %d experience toward level %d",
                      actor->xp, actor->level + 1);
    row++;

    /* The difficulty half of the trade, stated honestly. It is usually small,
       and it is often zero -- escalation is capped, and half of it comes from
       floors entered, which no sale can touch. */
    int now   = monsters_escalation_for(actor->level, p->floor_entries);
    int after = monsters_escalation_for(target,   p->floor_entries);

    mvprintw_clip(row++, 2, "  Monsters carry  +%d%%  ->  +%d%%", now, after);
    attron(A_DIM);
    if (now == after) {
        mvprintw_clip(row++, 2, "     (no change -- you are at the ceiling, or the floors you have");
        mvprintw_clip(row++, 2, "      entered account for all of it)");
    } else {
        mvprintw_clip(row++, 2, "     (takes effect the next time you go down)");
    }
    attroff(A_DIM);
    row++;

    if (p->levels_sold > 0) {
        attron(A_DIM);
        mvprintw_clip(row++, 2, "Sold so far: %d level%s, for %ld gold.",
                      p->levels_sold, p->levels_sold == 1 ? "" : "s",
                      p->levels_sold_gold);
        attroff(A_DIM);
        row++;
    }

    mvprintw_clip(row++, 2, "Left/Right  one level      Up/Down  ten at a time");
    mvprintw_clip(row++, 2, "a  all %d      Enter sells      Esc/q leaves", max);

    draw_message_log_at(row + 1);
    refresh();
}

void draw_gladiator_school(const Player *p, const Hero *actor, int cursor) {
    erase();
    draw_frame("THE PIT SCHOOL -- one virtue at a time, and they charge for it",
               COLOR_PAIR(CP_FRAME));

    mvprintw_clip(2, 2, "Gold: %-10d   Writs: %-3d   A point in one of the thirty.",
                  p->gold, p->training_writs);
    attron(A_DIM);
    mvprintw_clip(3, 2, "The thirty feed every derived stat you have. That is why it costs this much.");
    attroff(A_DIM);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int page = rows - 13;
    if (page < 5) page = 5;
    if (page > ATTR_COUNT) page = ATTR_COUNT;
    int top = (cursor / page) * page;

    int row = 5;
    for (int i = top; i < top + page && i < ATTR_COUNT; i++) {
        int price = training_price(actor->attrs[i]);
        price -= price * actor->shop_discount_pct / 100;
        bool sel = (i == cursor);
        if (sel) attron(A_REVERSE);
        mvprintw_clip(row++, 2, " %-14s %3d -> %-3d   %9d g%s",
                      ATTR_NAMES[i], actor->attrs[i], actor->attrs[i] + 1, price,
                      p->gold >= price ? "" : "   (short)");
        if (sel) attroff(A_REVERSE);
    }

    if (page < ATTR_COUNT) {
        attron(A_DIM);
        mvprintw_clip(row, 8, "-- %d-%d of %d --", top + 1,
                      top + page > ATTR_COUNT ? ATTR_COUNT : top + page, ATTR_COUNT);
        attroff(A_DIM);
    }
    row += 2;

    attron(A_BOLD); mvprintw_clip(row++, 2, "%s", ATTR_NAMES[cursor]); attroff(A_BOLD);
    attron(A_DIM);  mvprintw_clip(row++, 2, "%s", attr_blurb(cursor));  attroff(A_DIM);

    row++;
    mvprintw_clip(row++, 2, "Up/Down move   Enter trains for coin   w spends a writ   Esc/q leaves");
    draw_message_log_at(row + 1);
    refresh();
}

void draw_tavern(const Player *p, int cursor) {
    erase();
    draw_frame("THE BRASS LANTERN -- heroes for hire, coin up front", COLOR_PAIR(CP_FRAME));

    int party = companion_count(p);
    mvprintw_clip(2, 2, "Gold: %-10d   Party: %d of %d hired   Next hire: %ld gold",
             p->gold, party, MAX_COMPANIONS, companion_hire_cost(party));
    attron(A_DIM);
    mvprintw_clip(3, 2, "They fight on their own down there -- you pay them, you don't command them.");
    attroff(A_DIM);

    attron(A_DIM);
    mvprintw_clip(4, 2, "      %-24s %-20s %-8s %-4s %10s", "NAME", "CLASS", "TEMPER", "LVL", "PRICE");
    attroff(A_DIM);

    /* Twenty rows plus a detail pane plus the log does not fit an 80x24
       terminal, so the list pages to whatever height there actually is --
       and the detail pane itself grows by a line per item the selected hero
       has found, so the list has to give those lines back. */
    Hero sel;
    tavern_candidate(p, cursor, &sel);
    const Hero *sel_live = companion_by_roster(p, cursor);
    if (sel_live) sel = *sel_live;
    int gear_rows = sel_live ? sel.gear_count : 0;

    int page = LINES - 21 - gear_rows;
    if (page < 5) page = 5;
    if (page > TAVERN_ROSTER) page = TAVERN_ROSTER;
    int top = (cursor / page) * page;

    int row = 5;
    for (int i = top; i < top + page && i < TAVERN_ROSTER; i++) {
        Hero c;
        tavern_candidate(p, i, &c);
        bool out_with_you = tavern_is_hired(p, i);
        bool fallen = tavern_has_fallen(p, i);

        /* Someone already out with you is shown as they are now, not as the
           Tavern first advertised them -- three levels of their own make a
           difference to whether you keep them. */
        const Hero *live = companion_by_roster(p, i);
        if (live) c = *live;

        char status[40];
        if (out_with_you)  snprintf(status, sizeof(status), "with you");
        else if (fallen)   snprintf(status, sizeof(status), "%ld (was yours)", companion_rehire_cost(party));
        else               snprintf(status, sizeof(status), "%ld", companion_hire_cost(party));

        bool on_cursor = (i == cursor);
        if (on_cursor) attron(A_REVERSE);
        mvprintw_clip(row, 2, " %2d)  %-24.24s %-20.20s %-8.8s L%-3d %10s ",
                 i + 1, c.name, companion_class_name(&c),
                 personality_name(c.personality), c.level, status);
        if (on_cursor) attroff(A_REVERSE);
        row++;
    }

    if (page < TAVERN_ROSTER) {
        attron(A_DIM);
        mvprintw_clip(row, 8, "-- %d-%d of %d --", top + 1,
                 top + page > TAVERN_ROSTER ? TAVERN_ROSTER : top + page, TAVERN_ROSTER);
        attroff(A_DIM);
    }
    row++;
    attron(A_BOLD); mvprintw_clip(row++, 2, "%s", sel.name); attroff(A_BOLD);
    mvprintw_clip(row++, 2, "%s -- %s, carrying a %s",
             companion_class_name(&sel), archetype_name(sel.archetype),
             companion_class_weapon(&sel));

    /* What they actually fight with, which is the thing worth knowing before
       you pay for them. */
    const char *bow = companion_weapon_name(&sel);
    if (bow) {
        mvprintw_clip(row++, 2, "Sidearm: %s", bow);
    } else if (sel.spell_count > 0) {
        /* Column measured from the prefix rather than guessed: "Conduit
           magic, 12 aether:" is four characters longer than "Jinx magic, 9
           aether:", and a fixed column ran the first spell into it. */
        char prefix[48];
        int plen = snprintf(prefix, sizeof(prefix), "%s magic, %d aether:",
                            school_name(sel.magic_school), sel.aether_max);
        mvprintw_clip(row, 2, "%s", prefix);
        int col = 2 + (plen > 0 ? plen : 0) + 2;
        for (int k = 0; k < sel.spell_count; k++) {
            const char *sp = companion_spell_name(&sel, k);
            if (!sp) continue;
            mvprintw_clip(row, col, "%s%s", sp, k + 1 < sel.spell_count ? "," : "");
            col += (int)strlen(sp) + 2;
        }
        row++;
    }
    if (sel_live)
        mvprintw_clip(row++, 2, "Level %d   HP %d/%d   attack %d   defence %d   %d kills",
                 sel.level, sel.hp, sel.maxhp, sel.base_atk, sel.base_def, sel.kills);
    else
        mvprintw_clip(row++, 2, "Level %d   HP %d   attack %d   defence %d",
                 sel.level, sel.maxhp, sel.base_atk, sel.base_def);
    attron(A_DIM); mvprintw_clip(row++, 2, "%s", companion_class_tagline(&sel)); attroff(A_DIM);
    mvprintw_clip(row++, 2, "%s -- %s",
             companion_ability_name(sel.archetype), companion_ability_desc(sel.archetype));
    if (sel_live && sel.gear_count > 0) {
        for (int g = 0; g < sel.gear_count && g < COMPANION_GEAR_SLOTS; g++)
            mvprintw_clip(row++, 4, "* %-26s +%d atk  +%d def  +%d hp",
                     sel.gear[g].name, sel.gear[g].atk, sel.gear[g].def, sel.gear[g].hp);
    }
    switch (sel.personality) {
        case PERSONALITY_BOLD:
            mvprintw_clip(row++, 2, "Bold -- ranges a long way ahead and starts fights you didn't."); break;
        case PERSONALITY_CAUTIOUS:
            mvprintw_clip(row++, 2, "Cautious -- stays close and picks its moments."); break;
        default:
            mvprintw_clip(row++, 2, "Steady -- keeps near the party and fights what comes to it."); break;
    }

    row++;
    if (tavern_can_release(p, cursor)) {
        attron(A_DIM);
        mvprintw_clip(row++, 2, "Yours -- 'r' lets them go for good, and a different hero takes the chair.");
        attroff(A_DIM);
    } else {
        row++;
    }
    mvprintw_clip(row++, 2, "Up/Down move   Enter hires   r releases (yours only)   Esc/q leaves");
    draw_message_log_at(row + 1);
    refresh();
}

int screen_world_size_select(void) {
    erase();
    draw_frame("HOW DEEP DOES IT GO", COLOR_PAIR(CP_FRAME));

    int r = 2;
    attron(A_DIM);
    mvprintw_clip(r++, 4, "The same hundred floors either way. What changes is how much floor.");
    attroff(A_DIM);
    r++;

    for (int i = 0; i < WORLD_SIZE_COUNT; i++) {
        int w, h;
        world_size_dims(i, &w, &h);
        attron(A_BOLD);
        mvprintw_clip(r++, 4, "%d) %-12s %d x %d", i + 1, world_size_name(i), w, h);
        attroff(A_BOLD);
        mvprintw_clip(r++, 7, "%s", world_size_blurb(i));
        r++;
    }

    attron(A_DIM);
    mvprintw_clip(r++, 4, "You cannot change this later -- every floor of the run is built to it.");
    attroff(A_DIM);
    r++;
    mvprintw_clip(r, 4, "Choose (1-%d):", WORLD_SIZE_COUNT);
    refresh();

    for (;;) {
        int ch = getch();
        if (ch >= '1' && ch < '1' + WORLD_SIZE_COUNT) return ch - '1';
    }
}

int screen_mode_select(void) {
    erase();
    draw_frame("CHOOSE YOUR DESCENT", COLOR_PAIR(CP_FRAME));

    int r = 2;
    attron(A_BOLD);
    mvprintw_clip(r++, 4, "1) Normal");
    attroff(A_BOLD);
    mvprintw_clip(r++, 7, "The temple as it's always been -- deliberate, dangerous, fair.");
    r++;
    attron(A_BOLD);
    mvprintw_clip(r++, 4, "2) Hard");
    attroff(A_BOLD);
    mvprintw_clip(r++, 7, "2-3x as many enemies on every floor. Same stats, same fair line-of-sight");
    mvprintw_clip(r++, 7, "aggro as Normal -- just more of them between you and the stairs.");
    r++;
    attron(A_BOLD);
    mvprintw_clip(r++, 4, "3) Swarm");
    attroff(A_BOLD);
    mvprintw_clip(r++, 7, "10-20x as many enemies on every floor. Same stats as Normal, and they");
    mvprintw_clip(r++, 7, "still only aggro once they can actually see you -- it's the numbers");
    mvprintw_clip(r++, 7, "that change, not the danger per monster.");
    r++;
    attron(A_BOLD);
    mvprintw_clip(r++, 4, "4) Hardcore");
    attroff(A_BOLD);
    mvprintw_clip(r++, 7, "Swarm's density and aggression, with no safety net: monsters never");
    mvprintw_clip(r++, 7, "respawn once cleared, but recall charms are disabled outright. For");
    mvprintw_clip(r++, 7, "when Swarm stopped being enough.");
    r += 2;
    mvprintw_clip(r, 4, "Choose (1-4):");
    refresh();

    for (;;) {
        int ch = getch();
        if (ch == '1') return DIFFICULTY_NORMAL;
        if (ch == '2') return DIFFICULTY_HARD;
        if (ch == '3') return DIFFICULTY_SWARM;
        if (ch == '4') return DIFFICULTY_HARDCORE;
    }
}

/* Stage one of character creation: seven roles instead of a hundred names.
   Returns an Archetype, or -1 if the player backed out (nothing to back out
   to at this point, so the caller just re-asks). */
int screen_archetype_select(void) {
    int cursor = 0;

    for (;;) {
        erase();
        draw_frame("WHAT KIND OF PERSON WERE YOU?", COLOR_PAIR(CP_FRAME));

        int row = 2;
        for (int a = 0; a < ARCHETYPE_COUNT; a++) {
            bool sel = (a == cursor);
            if (sel) attron(A_REVERSE);
            mvprintw_clip(row, 2, " %d) %-12s %2d classes ",
                     a + 1, archetype_name(a), archetype_class_count(a));
            if (sel) attroff(A_REVERSE);
            attron(A_DIM);
            mvprintw_clip(row, 34, "%s", archetype_blurb(a));
            attroff(A_DIM);
            row += 2;
        }

        row++;
        /* Name a few of the group's classes, so the role is grounded in
           something concrete before committing to a whole list. */
        int n = archetype_class_count(cursor);
        attron(A_BOLD);
        mvprintw_clip(row++, 2, "%s", archetype_name(cursor));
        attroff(A_BOLD);
        mvprintw_clip(row++, 2, "%s", archetype_blurb(cursor));
        row++;
        mvprintw_clip(row++, 2, "Among them:");
        for (int i = 0; i < 6 && i < n; i++) {
            int idx = archetype_class_index(cursor, i * n / (n < 6 ? n : 6));
            if (idx < 0) continue;
            mvprintw_clip(row + i / 3, 4 + (i % 3) * 26, "%-24s", CLASS_TABLE[idx].name);
        }
        row += 2;

        row++;
        mvprintw_clip(row++, 2, "Up/Down move  1-%d jump  Enter opens that archetype's classes",
                 ARCHETYPE_COUNT);
        refresh();

        int ch = getch();
        if (ch >= '1' && ch < '1' + ARCHETYPE_COUNT) { cursor = ch - '1'; continue; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) return cursor;
        switch (ch) {
            case KEY_UP: case 'k': if (cursor > 0) cursor--; break;
            case KEY_DOWN: case 'j': if (cursor < ARCHETYPE_COUNT - 1) cursor++; break;
            default: break;
        }
    }
}

/* Stage two: the classes inside one archetype. `archetype` < 0 falls back to
   the whole table, which is what the "show me everything" option does.
   Returns a CLASS_TABLE index, or -1 to go back to the archetype list. */
int screen_class_select(int archetype) {
    /* `count` is how many classes this screen offers and `entry_to_class`
       turns a row on it into a CLASS_TABLE index -- so the rest of the
       function never has to care whether it is showing one archetype or the
       whole table. */
    bool all = (archetype < 0 || archetype >= ARCHETYPE_COUNT);
    int count = all ? NUM_CLASSES : archetype_class_count(archetype);
    if (count <= 0) return -1;

    int cursor = 0;
    char numbuf[4] = {0};
    int numlen = 0;

    for (;;) {
        erase();
        char title[80];
        if (all) snprintf(title, sizeof(title), "WHO WERE YOU, BEFORE THE CRASH?  (all %d classes)", count);
        else     snprintf(title, sizeof(title), "%s  --  %d classes",
                          archetype_name(archetype), count);
        draw_frame(title, COLOR_PAIR(CP_FRAME));

        int top = (cursor / CLASS_PAGE_SIZE) * CLASS_PAGE_SIZE;
        for (int i = 0; i < CLASS_PAGE_SIZE && top + i < count; i++) {
            int entry = top + i;
            int idx = all ? entry : archetype_class_index(archetype, entry);
            if (idx < 0) continue;
            bool sel = (entry == cursor);
            int best = class_best_floor(idx);
            if (sel) attron(A_REVERSE);
            if (best > 0) {
                mvprintw_clip(2 + i, 2, "%3d) %-24s (best: floor %d)", entry + 1, CLASS_TABLE[idx].name, best);
            } else {
                mvprintw_clip(2 + i, 2, "%3d) %-24s", entry + 1, CLASS_TABLE[idx].name);
            }
            if (sel) attroff(A_REVERSE);
        }

        int cur_idx = all ? cursor : archetype_class_index(archetype, cursor);
        if (cur_idx < 0) cur_idx = 0;
        const ClassSeed *c = &CLASS_TABLE[cur_idx];
        int info_row = 2 + CLASS_PAGE_SIZE + 1;
        attron(A_BOLD);
        mvprintw_clip(info_row, 2, "%s", c->name);
        attroff(A_BOLD);
        /* Show the role even inside a filtered list -- it is the one piece of
           context the "all classes" view would otherwise lose entirely. */
        attron(A_DIM);
        mvprintw_clip(info_row, 30, "[%s]", archetype_name(c->archetype));
        attroff(A_DIM);
        mvprintw_clip(info_row + 1, 2, "%s", c->tagline);
        mvprintw_clip(info_row + 2, 2, "Weapon: %-24s Armour: %s", c->start_weapon_name, c->start_armor_name);

        mvprintw_clip(info_row + 3, 2, "Signature traits:");
        for (int i = 0; c->overrides[i].attr != ATTR_COUNT && i < 6; i++) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%s %d", ATTR_NAMES[c->overrides[i].attr], c->overrides[i].value);
            int col = i % 3, line = i / 3;
            mvprintw_clip(info_row + 4 + line, 2 + col * 18, "%-16s", buf);
        }

        mvprintw_clip(info_row + 7, 2, "Up/Down move  PgUp/PgDn page  type a number then Enter to jump");
        mvprintw_clip(info_row + 8, 2, "Enter selects the highlighted class   Esc/q goes back to archetypes");
        if (numlen > 0) {
            mvprintw_clip(info_row + 9, 2, "Jump to: %s_", numbuf);
        }
        refresh();

        int ch = getch();
        if (ch >= '0' && ch <= '9') {
            if (numlen < 3) numbuf[numlen++] = (char)ch;
            numbuf[numlen] = '\0';
            continue;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (numlen > 0) numbuf[--numlen] = '\0';
            continue;
        }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            if (numlen > 0) {
                int target = atoi(numbuf) - 1;
                if (target < 0) target = 0;
                if (target >= count) target = count - 1;
                cursor = target;
                numlen = 0;
                numbuf[0] = '\0';
            } else {
                return all ? cursor : archetype_class_index(archetype, cursor);
            }
            continue;
        }
        if (ch == 27 || ch == 'q') {
            if (numlen > 0) { numlen = 0; numbuf[0] = '\0'; continue; }
            return -1;   /* back to the archetype list */
        }
        switch (ch) {
            case KEY_UP: case 'k': if (cursor > 0) cursor--; break;
            case KEY_DOWN: case 'j': if (cursor < count - 1) cursor++; break;
            case KEY_NPAGE: cursor += CLASS_PAGE_SIZE; if (cursor > count - 1) cursor = count - 1; break;
            case KEY_PPAGE: cursor -= CLASS_PAGE_SIZE; if (cursor < 0) cursor = 0; break;
            default: break;
        }
    }
}

void screen_name_entry(char *out, size_t outlen) {
    erase();
    draw_frame("WHAT DO THEY CALL YOU?", COLOR_PAIR(CP_FRAME));
    mvprintw_clip(2, 4, "(leave blank for \"Wanderer\")");
    mvprintw_clip(4, 4, "Name: ");
    refresh();

    char buf[24];
    buf[0] = '\0';
    echo();
    curs_set(1);
    move(4, 10);
    getnstr(buf, (int)sizeof(buf) - 1);
    noecho();
    curs_set(0);

    if (buf[0] == '\0') strncpy(buf, "Wanderer", sizeof(buf) - 1);
    strncpy(out, buf, outlen - 1);
    out[outlen - 1] = '\0';
}

void screen_gameover(const Player *p, const Hero *actor, int best_floor) {
    for (;;) {
        if (screen_is_too_small()) { getch(); continue; }

        erase();
        draw_frame("THE DEEP WELL CLAIMS ANOTHER", COLOR_PAIR(CP_UI_WARN) | A_BOLD);
        attron(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
        mvprintw_clip(2, 4, "%s did not come back up.", actor->name);
        attroff(COLOR_PAIR(CP_UI_WARN) | A_BOLD);

        int r = 4;
        mvprintw_clip(r++, 4, "Something on floor %d of the Deep Well finished what the crash started.", p->floor);
        r++;
        mvprintw_clip(r++, 4, "Class         : %s", CLASS_TABLE[actor->class_id].name);
        mvprintw_clip(r++, 4, "Mode          : %s", difficulty_name(p->difficulty));
        mvprintw_clip(r++, 4, "Reached floor : %d", p->floor);
        mvprintw_clip(r++, 4, "Level         : %d", actor->level);
        mvprintw_clip(r++, 4, "Kills         : %d", actor->kills);
        mvprintw_clip(r++, 4, "Turns taken   : %d", p->turns);
        mvprintw_clip(r++, 4, "Run seed      : %u   (--seed %u replays this dungeon)",
                 p->run_seed, p->run_seed);
        r++;
        mvprintw_clip(r++, 4, "Deepest floor ever reached (this game's memory): %d", best_floor);
        r += 2;
        mvprintw_clip(r, 4, "Press any key to return to the docks...");

        refresh();
        if (wait_any_key() != KEY_RESIZE) break;
    }
}

void screen_win(const Player *p, const Hero *actor, int best_floor) {
    for (;;) {
        if (screen_is_too_small()) { getch(); continue; }

        erase();
        draw_frame("THE WARDEN FALLS", COLOR_PAIR(CP_GOLD) | A_BOLD);
        attron(COLOR_PAIR(CP_UI_HEADER) | A_BOLD);
        mvprintw_clip(2, 4, "The Warden falls, and the Deep Well finally holds still.");
        attroff(COLOR_PAIR(CP_UI_HEADER) | A_BOLD);

        int r = 4;
        mvprintw_clip(r++, 4, "A hundred floors of brass, root and rust, and at the bottom of it only");
        mvprintw_clip(r++, 4, "quiet - the kind that has been waiting a long time for someone to earn it.");
        mvprintw_clip(r++, 4, "Whatever the city above makes of %s climbing back out with the truth of", actor->name);
        mvprintw_clip(r++, 4, "the bottom in their pocket is a story for another night.");
        r += 2;
        mvprintw_clip(r++, 4, "Class         : %s", CLASS_TABLE[actor->class_id].name);
        mvprintw_clip(r++, 4, "Mode          : %s", difficulty_name(p->difficulty));
        mvprintw_clip(r++, 4, "Level         : %d", actor->level);
        mvprintw_clip(r++, 4, "Kills         : %d", actor->kills);
        mvprintw_clip(r++, 4, "Gold carried  : %d", p->gold);
        mvprintw_clip(r++, 4, "Turns taken   : %d", p->turns);
        mvprintw_clip(r++, 4, "Run seed      : %u   (--seed %u replays this dungeon)",
                 p->run_seed, p->run_seed);
        r++;
        mvprintw_clip(r++, 4, "Best floor ever reached (this game's memory): %d", best_floor);
        r += 2;
        mvprintw_clip(r, 4, "Press any key to return to the docks...");

        refresh();
        if (wait_any_key() != KEY_RESIZE) break;
    }
}

int prompt_floor_choice(int deepest_floor) {
    if (deepest_floor <= 1) return 1;

    erase();
    draw_frame("THE TEMPLE ENTRANCE", COLOR_PAIR(CP_FRAME));

    mvprintw_clip(2, 2, "You have gone as deep as floor %d before.", deepest_floor);
    mvprintw_clip(4, 2, "1) Descend fresh, from floor 1");
    mvprintw_clip(5, 2, "2) Continue from floor %d", deepest_floor);
    mvprintw_clip(7, 2, "Choose (1/2), or Esc to cancel:");
    refresh();

    for (;;) {
        int ch = getch();
        if (ch == '1') return 1;
        if (ch == '2') return deepest_floor;
        if (ch == 27 || ch == 'q') return 1;
    }
}

/* One-frame red pulse over whatever's currently drawn, consumed the moment
   the player's HP drops. Deliberately drawn WITHOUT erasing first -- it's
   an overlay on the previous frame, not a new one. */
static void flash_hit_border(void) {
    draw_frame(NULL, COLOR_PAIR(CP_UI_WARN) | A_REVERSE);
    refresh();
    anim_pause(70);
}

/* Same clamped-to-map-edges camera math draw_game_screen uses, shared with
   the animation helpers below so a bolt/burst lines up with the frame
   that's already on screen. Combat only ever animates in the temple, which
   always scrolls, so callers don't need a `scrolling` flag here. */
static void compute_camera(const Player *p, int *camera_x, int *camera_y) {
    /* Centred on the body being driven, not on the main character -- take
       over a hire and the view goes with you. */
    int cx = party_x(p) - VIEW_W / 2;
    int cy = party_y(p) - VIEW_H / 2;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx > MAP_W - VIEW_W) cx = MAP_W - VIEW_W;
    if (cy > MAP_H - VIEW_H) cy = MAP_H - VIEW_H;

    /* In town, stop at the edge of the town rather than at the edge of the
       dungeon grid. The plaza is TOWN_MAP_W x TOWN_MAP_H and everything past it is
       wall that was never meant to be looked at -- without this, a viewport
       narrower than the town scrolls off the end of the plaza and shows a
       screen of nothing. */
    if (p->floor == 0) {
        int max_x = TOWN_MAP_W - VIEW_W, max_y = TOWN_MAP_H - VIEW_H;
        if (max_x < 0) max_x = 0;
        if (max_y < 0) max_y = 0;
        if (cx > max_x) cx = max_x;
        if (cy > max_y) cy = max_y;
    }
    *camera_x = cx;
    *camera_y = cy;
}

/* Redraws exactly one world cell -- tile, then feature/item/monster/player
   overlay, same priority as draw_game_screen's main loops below -- at its
   correct screen position. Used to restore a cell after an animation frame
   has drawn a transient glyph over it. No-op if the cell is off-camera or
   was never seen. */
static void draw_one_cell(const Map *m, const Player *p, int camera_x, int camera_y, int wx, int wy) {
    int vx = wx - camera_x, vy = wy - camera_y;
    if (vx < 0 || vx >= VIEW_W || vy < 0 || vy >= VIEW_H) return;
    if (wx < 0 || wx >= MAP_W || wy < 0 || wy >= MAP_H) return;

    /* In tile mode the world lives on the graphics plane, which the
       animation never touched -- so restoring the cell means clearing the
       transient glyph off the overlay, not redrawing the tile. */
    if (g_mode == RENDER_TILES) {
        tui_overlay_clear_cell(MAP_ORIGIN_Y + vy, MAP_ORIGIN_X + vx);
        return;
    }

    CellView cv;
    resolve_one_cell(m, p, wx, wy, &cv);
    paint_cell(g_mode, MAP_ORIGIN_Y + vy, MAP_ORIGIN_X + vx, &cv);
}

/* Milliseconds each animation frame holds before advancing/restoring.
   Short enough that even a long spell bolt across a wide room reads as
   "fast but visible" rather than making combat feel sluggish. */
#define ANIM_STEP_MS 18

void render_set_headless(bool on) { g_headless = on; }

static void anim_put(int camera_x, int camera_y, int wx, int wy, chtype ch, int pair) {
    int vx = wx - camera_x, vy = wy - camera_y;
    if (vx < 0 || vx >= VIEW_W || vy < 0 || vy >= VIEW_H) return;

    /* Tile mode draws the world into a pixel plane that sits above the text,
       so an animation glyph put on the text surface would be invisible. The
       overlay is a transparent plane above the graphics, which is where these
       transient glyphs belong -- and it also means an animation never costs a
       re-blit of the whole viewport. */
    bool overlaid = (g_mode == RENDER_TILES);
    if (overlaid) tui_overlay_begin(MAP_ORIGIN_Y, MAP_ORIGIN_X, VIEW_H, VIEW_W);

    move(MAP_ORIGIN_Y + vy, MAP_ORIGIN_X + vx);
    attron(COLOR_PAIR(pair) | A_BOLD);
    addch(ch);
    attroff(COLOR_PAIR(pair) | A_BOLD);

    if (overlaid) tui_overlay_end();
    refresh();
}

/* Combat feedback, all synchronous and all drawn as an overlay on whatever
   draw_game_screen last put on screen (like flash_hit_border above) --
   callers don't erase or redraw anything before or after. World
   coordinates in, not screen coordinates. Each call blocks for its
   duration (well under half a second even for a long bolt), so combat/
   spell/ranged/ability code can just call these inline at the moment an
   attack lands, no queuing needed. */
/* ---- batching, so the AI can use the player's own functions -------------
 *
 * The party's animations are collected and played as one volley, because five
 * bodies animating in sequence every turn made the game crawl -- that much has
 * always been true. What was *also* true, and was the problem, is that the
 * batching lived inside companions.c as its own little animation system. So
 * the AI could not call cast_spell_slot() or fire_ranged() -- the real spell
 * and the real weapon -- without animating five times a turn, and it grew a
 * parallel implementation of both instead.
 *
 * With the batch here, anim_bolt/anim_flash_cell/anim_burst append instead of
 * playing while it is open, and the AI can call exactly what the player calls.
 * One spell system, one weapon system, one animation system.
 */
static AnimShot g_batch[ANIM_VOLLEY_MAX];
static int  g_batch_n = 0;
static bool g_batching = false;

void anim_batch_begin(void) { g_batching = true; g_batch_n = 0; }

void anim_batch_end(const Map *m, const Player *p) {
    g_batching = false;
    if (g_batch_n > 0) anim_volley(m, p, g_batch, g_batch_n);
    g_batch_n = 0;
}

/* Silently drops anything past ANIM_VOLLEY_MAX, which is what anim_volley
   would do with it anyway -- sixteen is already more than reads as one
   moment. */
static bool anim_batched(int x0, int y0, int x1, int y1, int radius, chtype ch, int pair) {
    if (!g_batching) return false;
    if (g_batch_n < ANIM_VOLLEY_MAX) {
        AnimShot *a = &g_batch[g_batch_n++];
        a->x0 = x0; a->y0 = y0; a->x1 = x1; a->y1 = y1;
        a->radius = radius; a->ch = ch; a->pair = pair;
    }
    return true;
}

void anim_flash_cell(const Map *m, const Player *p, int x, int y, chtype ch, int pair, int pulses) {
    if (anim_batched(x, y, x, y, 0, ch, pair)) return;
    /* A batched caller has no map or camera to hand -- it is collecting, not
       drawing. Outside a batch there is nothing to draw them against either,
       so this is a no-op rather than a crash. */
    if (!m || !p) return;
    if (g_headless) return;
    int camera_x, camera_y;
    compute_camera(p, &camera_x, &camera_y);
    for (int i = 0; i < pulses; i++) {
        anim_put(camera_x, camera_y, x, y, ch, pair);
        napms(ANIM_STEP_MS + 12);
        draw_one_cell(m, p, camera_x, camera_y, x, y);
        refresh();
        napms(ANIM_STEP_MS);
    }
}

void anim_bolt(const Map *m, const Player *p, int x0, int y0, int x1, int y1, chtype ch, int pair) {
    if (anim_batched(x0, y0, x1, y1, 0, ch, pair)) return;
    /* A batched caller has no map or camera to hand -- it is collecting, not
       drawing. Outside a batch there is nothing to draw them against either,
       so this is a no-op rather than a crash. */
    if (!m || !p) return;
    if (g_headless) return;
    int camera_x, camera_y;
    compute_camera(p, &camera_x, &camera_y);

    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0, y = y0;

    for (;;) {
        if (!(x == x0 && y == y0)) {
            anim_put(camera_x, camera_y, x, y, ch, pair);
            napms(ANIM_STEP_MS);
            draw_one_cell(m, p, camera_x, camera_y, x, y);
        }
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
    refresh();
}

/* ---- volleys ---------------------------------------------------------- */

/* Longest bolt worth drawing. A shot has to cross the viewport at most, and
   capping it keeps one absurd path from setting the length of everyone
   else's frame. */
#define VOLLEY_PATH_MAX 24

typedef struct {
    int px[VOLLEY_PATH_MAX], py[VOLLEY_PATH_MAX];
    int len;                    /* 0 for a burst or an in-place flash */
    int cx, cy, radius;         /* burst */
    chtype ch;
    int pair;
} VolleyShot;

/* Bresenham again, but written into a list rather than drawn as it goes --
   a volley needs every shot's step N available at the same moment, which a
   draw-as-you-walk loop cannot give. */
static void volley_path(VolleyShot *v, int x0, int y0, int x1, int y1) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, x = x0, y = y0;

    v->len = 0;
    for (;;) {
        if (!(x == x0 && y == y0) && v->len < VOLLEY_PATH_MAX) {
            v->px[v->len] = x; v->py[v->len] = y;
            v->len++;
        }
        if ((x == x1 && y == y1) || v->len >= VOLLEY_PATH_MAX) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x += sx; }
        if (e2 <= dx) { err += dx; y += sy; }
    }
}

/* Would the player actually see this happen? Off-camera or unlit and the
   shot is dropped before it costs a single frame of delay. */
static bool volley_worth_drawing(const Map *m, int camera_x, int camera_y,
                                 int x, int y) {
    int vx = x - camera_x, vy = y - camera_y;
    if (vx < 0 || vx >= VIEW_W || vy < 0 || vy >= VIEW_H) return false;
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return false;
    return m->tiles[y][x].visible;
}

void anim_volley(const Map *m, const Player *p, const AnimShot *shots, int n) {
    if (g_headless) return;
    if (!shots || n <= 0) return;

    int camera_x, camera_y;
    compute_camera(p, &camera_x, &camera_y);

    VolleyShot v[ANIM_VOLLEY_MAX];
    int count = 0;
    int frames = 0;

    for (int i = 0; i < n && count < ANIM_VOLLEY_MAX; i++) {
        const AnimShot *a = &shots[i];
        /* The landing point is what matters: a hero firing from the dark
           into a room you can see is worth drawing, and one firing inside a
           room you cannot see is not. */
        if (!volley_worth_drawing(m, camera_x, camera_y, a->x1, a->y1)) continue;

        VolleyShot *w = &v[count];
        memset(w, 0, sizeof(*w));
        w->ch = a->ch; w->pair = a->pair;

        if (a->radius > 0) {
            w->cx = a->x1; w->cy = a->y1; w->radius = a->radius;
            if (a->radius > frames) frames = a->radius;
        } else if (a->x0 == a->x1 && a->y0 == a->y1) {
            /* A melee blow: one cell, one frame. */
            w->px[0] = a->x1; w->py[0] = a->y1; w->len = 1;
            if (frames < 1) frames = 1;
        } else {
            volley_path(w, a->x0, a->y0, a->x1, a->y1);
            if (w->len > frames) frames = w->len;
        }
        count++;
    }

    if (count == 0 || frames == 0) return;      /* nothing visible: no delay */

    for (int f = 0; f < frames; f++) {
        /* Put every shot's frame f down... */
        for (int i = 0; i < count; i++) {
            VolleyShot *w = &v[i];
            if (w->radius > 0) {
                int r = f + 1;
                if (r > w->radius) continue;
                for (int wy = w->cy - r; wy <= w->cy + r; wy++)
                    for (int wx = w->cx - r; wx <= w->cx + r; wx++) {
                        int ddx = wx - w->cx, ddy = wy - w->cy;
                        int d2 = ddx * ddx + ddy * ddy;
                        if (d2 > r * r || d2 <= (r - 1) * (r - 1)) continue;
                        anim_put(camera_x, camera_y, wx, wy, w->ch, w->pair);
                    }
            } else if (f < w->len) {
                anim_put(camera_x, camera_y, w->px[f], w->py[f], w->ch, w->pair);
            }
        }

        napms(ANIM_STEP_MS);

        /* ...then take it all back up, so the next frame starts clean. */
        for (int i = 0; i < count; i++) {
            VolleyShot *w = &v[i];
            if (w->radius > 0) {
                int r = f + 1;
                if (r > w->radius) continue;
                for (int wy = w->cy - r; wy <= w->cy + r; wy++)
                    for (int wx = w->cx - r; wx <= w->cx + r; wx++) {
                        int ddx = wx - w->cx, ddy = wy - w->cy;
                        int d2 = ddx * ddx + ddy * ddy;
                        if (d2 > r * r || d2 <= (r - 1) * (r - 1)) continue;
                        draw_one_cell(m, p, camera_x, camera_y, wx, wy);
                    }
            } else if (f < w->len) {
                draw_one_cell(m, p, camera_x, camera_y, w->px[f], w->py[f]);
            }
        }
    }
    refresh();
}

void anim_burst(const Map *m, const Player *p, int cx, int cy, int radius, chtype ch, int pair) {
    if (anim_batched(cx, cy, cx, cy, radius, ch, pair)) return;
    /* A batched caller has no map or camera to hand -- it is collecting, not
       drawing. Outside a batch there is nothing to draw them against either,
       so this is a no-op rather than a crash. */
    if (!m || !p) return;
    if (g_headless) return;
    int camera_x, camera_y;
    compute_camera(p, &camera_x, &camera_y);

    for (int r = 1; r <= radius; r++) {
        for (int wy = cy - r; wy <= cy + r; wy++) {
            for (int wx = cx - r; wx <= cx + r; wx++) {
                int ddx = wx - cx, ddy = wy - cy;
                int d2 = ddx * ddx + ddy * ddy;
                if (d2 > r * r || d2 <= (r - 1) * (r - 1)) continue;
                anim_put(camera_x, camera_y, wx, wy, ch, pair);
            }
        }
        napms(ANIM_STEP_MS + 12);
        for (int wy = cy - r; wy <= cy + r; wy++) {
            for (int wx = cx - r; wx <= cx + r; wx++) {
                int ddx = wx - cx, ddy = wy - cy;
                int d2 = ddx * ddx + ddy * ddy;
                if (d2 > r * r || d2 <= (r - 1) * (r - 1)) continue;
                draw_one_cell(m, p, camera_x, camera_y, wx, wy);
            }
        }
    }
    refresh();
}

/* Compose the viewport as one image and hand it to the backend in a single
   blit. Per-cell pixel calls would be far slower -- and would also mean the
   terminal never sees the frame as one picture, which is what lets it skip
   the parts that didn't change.

   Returns false if the terminal can't take it; the caller falls back to text
   for that frame rather than showing an empty map. */
static unsigned char *g_tilebuf;
static size_t         g_tilebuf_size;

/* Consecutive failed blits. A terminal that stops accepting images (a resize
   past its bitmap limit, a protocol it withdrew) would otherwise leave the
   player staring at a text map with no explanation of why the tiles went
   away -- or worse, at whatever half-transferred image is still on screen. */
static int g_tile_fail_streak;

static bool draw_map_tiles(int camera_x, int camera_y) {
    int cw = 0, ch = 0;
    if (!tui_cell_pixels(&cw, &ch)) return false;

    int pixw = VIEW_W * cw, pixh = VIEW_H * ch;
    size_t need = (size_t)pixw * (size_t)pixh * 4u;
    if (need == 0) return false;
    if (need > g_tilebuf_size) {
        unsigned char *nb = realloc(g_tilebuf, need);
        if (!nb) return false;
        g_tilebuf = nb;
        g_tilebuf_size = need;
    }
    /* Fully transparent, so cells the player has never seen show the plain
       terminal background instead of a black rectangle. */
    memset(g_tilebuf, 0, need);

    for (int vy = 0; vy < VIEW_H; vy++) {
        int wy = camera_y + vy;
        if (wy < 0 || wy >= MAP_H) continue;
        for (int vx = 0; vx < VIEW_W; vx++) {
            int wx = camera_x + vx;
            if (wx < 0 || wx >= MAP_W) continue;
            const CellView *cv = &g_cells[wy][wx];
            /* An unseen square still draws if something is *heard* on it --
               the mark alone, on the cleared buffer, with no terrain under it.
               Drawing the terrain there would hand over ground the player has
               not earned; see cell_apply_heard(). */
            if (!cv->seen && !cv->heard) continue;

            unsigned char *corner =
                g_tilebuf + ((size_t)(vy * ch) * (size_t)pixw + (size_t)(vx * cw)) * 4u;
            if (cv->seen)
                art_sprite_rgba(cv->terrain, !cv->visible, true, corner, cw, ch, pixw * 4);
            if (cv->has_occupant)
                {   TuiRGB own; const TuiRGB *tint = NULL;
                    if (cv->tint_by_pair && tui_pair_rgb(cv->text_pair, &own)) tint = &own;
                    art_sprite_rgba_tinted(cv->occupant, false, false, tint,
                                           corner, cw, ch, pixw * 4);
                }
        }
    }

    return tui_blit_rgba(MAP_ORIGIN_Y, MAP_ORIGIN_X, g_tilebuf, pixw, pixh);
}

void draw_game_screen(const Map *m, const Player *p, const Hero *actor, const char *floor_label, bool scrolling) {
    /* The one place that is guaranteed to see the live world every frame. */
    autoplay_snapshot(m->floor_num, actor->level, actor->hp, actor->maxhp, (long)p->gold,
                      actor->x, actor->y, p->turns);
    /* The window can be resized at any point, and in tile mode a viewport
       sized for the old one produces an image that no longer fits -- which
       the terminal is free to render as garbage. Re-lay-out before drawing
       anything rather than trusting the size from startup. */
    {
        int rows, cols;
        getmaxyx(stdscr, rows, cols);
        if (rows != g_laid_out_rows || cols != g_laid_out_cols) {
            render_layout_viewport();
            /* The one case that does need the whole screen blanked: the
               viewport just moved, so anything drawn for the old layout is
               debris nothing below will overwrite. Once per resize, not once
               per step. */
            erase();
        }
    }

    if (g_hp_flash) {
        flash_hit_border();
        g_hp_flash = false;
    }

    /* Blank the whole screen. Not a region of it -- all of it.
     *
       This screen used to clear two regions and rely on the map draw covering
       the rest, and the arithmetic was wrong three separate times: the rows
       below a shortened panel ("tGold", "mFloor"), then the two columns
       between the map's right edge and the panel's text, then the panel's half
       of the banner row. Each was found by a player, each was fixed on its
       own, and each looked identical from the chair -- a stripe of the last
       screen you were on standing down the side of the panel, turning "Level"
       into "eLevel" and "Gold" into "gGold".

       Three bugs of one shape means the shape is wrong. A screen that clears
       part of itself needs every part accounted for by somebody; a screen that
       clears all of itself needs nothing accounted for at all.

       ---- on the note that used to be here ----

       This comment previously argued at length that erase() must be avoided
       because it defeats ncurses' frame-to-frame diff and repaints the whole
       terminal every step -- "most of the cost of a step on a big window".
       That was measured, and it does not hold:

         100x30    region-clears 0.135 ms/frame    erase() 0.139 ms/frame
         200x60    region-clears 1.480 ms/frame    erase() 1.518 ms/frame
         300x80    region-clears 2.168 ms/frame    erase() 2.175 ms/frame

       Between 0.3% and 2%, inside the run-to-run spread of the same variant.
       erase() blanks the *virtual* screen; the diff against the physical
       screen still happens afterwards, so what goes over the wire is still
       only what changed. The cost the old note was describing belongs to
       clear()/clearok(), which forces a real repaint, and not to this.

       The project owner called it -- "you are changing screens anyway, how
       much can I lose" -- and the answer is a fortieth of a millisecond. */
    erase();

    int camera_x = 0, camera_y = 0;
    if (scrolling) compute_camera(p, &camera_x, &camera_y);

    char title[80];
    snprintf(title, sizeof(title), "AETHER DESCENT -- %s", floor_label);
    /* An overrun floor stays flagged in the frame. The arrival message
       scrolls out of the log within a few turns, and this is not something a
       player should be able to forget they are standing in. */
    /* A gold rush gets the same treatment for the same reason: it is a
       property of the floor you are standing in that the arrival message
       scrolls away from, and it changes how you should be playing. Overrun
       wins the border when both are true -- being killed matters more than
       being paid. */
    int frame_pair = CP_FRAME;
    const char *frame_tag = NULL;
    char rush_tag[24];
    if (m->overrun) {
        frame_pair = CP_UI_WARN;
        frame_tag  = " OVERRUN ";
    } else if (m->gold_rush) {
        frame_pair = CP_GOLD;
        snprintf(rush_tag, sizeof rush_tag, " GOLD RUSH (x%d) ", GOLD_RUSH_MULT);
        frame_tag = rush_tag;
    }

    draw_frame(title, COLOR_PAIR(frame_pair));

    if (frame_tag) {
        int trows, tcols;
        getmaxyx(stdscr, trows, tcols);
        (void)trows;
        int at = tcols - (int)strlen(frame_tag) - 2;
        if (at > 2) {
            attron(COLOR_PAIR(frame_pair) | A_BOLD | A_REVERSE);
            mvprintw_clip(0, at, "%s", frame_tag);
            attroff(COLOR_PAIR(frame_pair) | A_BOLD | A_REVERSE);
        }
    }

    /* Both flags still show in the sidebar when both are true, since only one
       of them can have the border. */

    /* Divider between the map and the sidebar, running alongside the map
       rows, and a full-width divider under the map separating it from the
       message log. Junction glyphs stitch them into the outer frame. */
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)rows;
    int div_x = MAP_ORIGIN_X + VIEW_W;
    int div_y = MAP_ORIGIN_Y + VIEW_H;

    /* How tall the map would be if the viewport filled the window. In text and
       block modes it equals VIEW_H; in tile mode the pixel budget has shrunk
       VIEW_H, so the map ends early while the sidebar and the log still run to
       the bottom of the window. Same formula as the log's, and it has to stay
       that way -- the two pieces are describing one layout.
     *
       Getting this wrong is visible: the under-map divider used to be drawn
       full width unconditionally, so in tile mode it was struck straight
       through the middle of the sidebar. "Weapon: Piston Fist------------".
       The sidebar was not being clipped, it was being crossed out. */
    int full_vh = rows - 9;
    if (full_vh < VIEW_MIN_H) full_vh = VIEW_MIN_H;
    int panel_bottom = MAP_ORIGIN_Y + (VIEW_H > full_vh ? VIEW_H : full_vh);
    bool short_map = panel_bottom > div_y;

    attron(COLOR_PAIR(CP_FRAME));
    /* The vertical divider runs the full height of the sidebar, not the height
       of the map -- otherwise the sidebar's lower half has nothing separating
       it from the blank space where the map stopped. */
    mvvline(1, div_x, '|', panel_bottom);
    /* Stop the horizontal divider at the sidebar when the sidebar carries on
       past it. It is the map's bottom edge, not the window's. */
    mvhline(div_y, 1, '-', short_map ? div_x - 1 : cols - 2);
    /* No top T-junction glyph here on purpose -- the title text can run
       right up to (and past) this column, and drawing over it would
       corrupt whatever character of the title happened to land there. */
    mvaddch(div_y, 0, '+');
    if (!short_map) mvaddch(div_y, cols - 1, '+');
    mvaddch(div_y, div_x, '+');
    if (short_map) {
        /* And the divider the map's absence left out, one row above the log. */
        mvhline(panel_bottom, 1, '-', cols - 2);
        mvaddch(panel_bottom, 0, '+');
        mvaddch(panel_bottom, cols - 1, '+');
        mvaddch(panel_bottom, div_x, '+');
    }
    attroff(COLOR_PAIR(CP_FRAME));

    /* Said on the banner line, next to the recall channel it generalises --
       both are "you are committed and cannot answer", and both need saying
       loudly rather than in the sidebar. */
    /* Row 1 is the banner line, and it is painted every frame whether or not
       there is anything to say.
     *
       This screen deliberately does not erase() per step -- only on a resize,
       because blanking the whole terminal every turn is what makes a curses
       game flicker. The cost is that every conditionally-drawn cell has to
       clear itself, and these two did not: they drew when busy or channelling
       and simply stopped drawing when finished, leaving the last frame's text
       on screen. Recall ends by moving you to town, so the way it showed up
       was "Channelling recall charm... 1 turn(s) left" sitting over the plaza,
       on a screen reading Floor 0, under a log line saying the charm had
       already flared.

       Composed into one buffer and padded to the viewport width, so the row
       is authoritative rather than additive. A third banner added here later
       gets the clearing for free. */
    {
        char banner[256];
        banner[0] = '\0';
        if (actor->work_turns_left > 0)
            snprintf(banner, sizeof(banner),
                     "Busy: %s. %d turn(s) left. Any key continues, Esc stops.",
                     work_verb(actor->work_kind), actor->work_turns_left);
        /* Channelling wins the row: it is the one you can lose a run by
           missing, and the two cannot both be true anyway. */
        if (actor->recall_channel_left > 0)
            snprintf(banner, sizeof(banner),
                     "Channelling recall charm... %d turn(s) left. Any key continues, Esc cancels.",
                     actor->recall_channel_left);

        int width = VIEW_W;
        if (width > (int)sizeof(banner) - 1) width = (int)sizeof(banner) - 1;
        int len = (int)strlen(banner);
        for (int i = len; i < width; i++) banner[i] = ' ';
        banner[width > 0 ? width : 0] = '\0';

        attron(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
        mvprintw_clip(1, MAP_ORIGIN_X, "%s", banner);
        attroff(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
    }

    build_cells(m, p, camera_x, camera_y);

    /* Tile mode is the only one that can fail at draw time (the terminal can
       withdraw pixel support on a resize, or refuse an oversized image), so it
       degrades to text for that frame instead of leaving a blank map. If it
       keeps failing, stop pretending: drop the mode and say so, rather than
       silently drawing text while the display screen claims tiles. */
    bool drew_tiles = false;
    if (g_mode == RENDER_TILES) {
        drew_tiles = draw_map_tiles(camera_x, camera_y);
        if (drew_tiles) {
            g_tile_fail_streak = 0;
        } else if (++g_tile_fail_streak >= 3) {
            g_mode = RENDER_TEXT;
            g_tile_fail_streak = 0;
            render_layout_viewport();
            log_msg("This terminal stopped accepting tile graphics -- switched to text.");
        }
    }
    if (!drew_tiles) {
        for (int vy = 0; vy < VIEW_H; vy++) {
            int wy = camera_y + vy;
            for (int vx = 0; vx < VIEW_W; vx++) {
                int wx = camera_x + vx;
                /* Off the edge of the world is still inside the viewport, and
                   town is smaller than the viewport -- so these squares have
                   to be blanked too, or the last screen shows through them. */
                if (wy < 0 || wy >= MAP_H || wx < 0 || wx >= MAP_W) {
                    paint_blank(g_mode, MAP_ORIGIN_Y + vy, MAP_ORIGIN_X + vx);
                    continue;
                }
                paint_cell(g_mode, MAP_ORIGIN_Y + vy, MAP_ORIGIN_X + vx, &g_cells[wy][wx]);
            }
        }
    }

    /* The panel sits against the *window*, not against the map.
     *
       These were `MAP_ORIGIN_X + VIEW_W + 2`, which is the same thing in text
       mode -- the viewport there is the window minus the reserves, so the map
       ends exactly where the panel should start. In tile mode it is not: the
       per-frame pixel budget shrinks the viewport (see
       render_clamp_tile_viewport), and on a retina display, where a cell
       reports *physical* pixels, it shrinks it a great deal. Everything
       downstream then followed the map instead of the window, so a large
       terminal in tile mode drew a small picture in the corner, a panel
       hard against it, and several hundred square characters of framed
       nothing. Reported from play with a screenshot; it is the first thing
       you see and it reads as the game being broken.

       Anchored to the window, the map is simply a smaller region inside a
       full-sized screen, which is what a pixel budget should cost you. In
       text mode nothing moves, because there the two anchors agree. */
    int win_cols = cols;                        /* same window, already measured */
    int panel_w = 45 - 3;                       /* what compute_viewport reserves */
    int sb_x = win_cols - panel_w;
    if (sb_x < MAP_ORIGIN_X + VIEW_W + 2) sb_x = MAP_ORIGIN_X + VIEW_W + 2;
    if (sb_x > win_cols - 2) sb_x = win_cols - 2;
    int row = MAP_ORIGIN_Y;

    /* No clear here. The whole inside of the frame is blanked once, above,
       before anything is drawn into it -- so the panel's variable number of
       lines, the gutter beside it and the banner row's far half are all
       covered by the same pass, rather than by three separate ones that each
       had to know where the others stopped. That arithmetic is what produced
       "tGold", and then "eLevel". */

    /* The panel is in two halves, and taking over a hire moves the line
       between them.
     *
       Everything above the gap is *whoever you currently are* -- name, class,
       vitals, the numbers you fight with. Everything below it is what the
       *party* is carrying: gold, keys, scrap, the larder, the floor you are
       all on. That split is not cosmetic. A Hero deliberately is not a
       Player (see its struct), so a hire has no gold, no pack and none of the
       thirty attributes; those belong to the character whoever is driving.
       Drawing them under a hire's name would be claiming they own them. */
    const Hero *body = party_body(p);

    attron(COLOR_PAIR(CP_FRAME) | A_BOLD);
    mvprintw_clip(row++, sb_x, "%s", body ? body->name : actor->name);
    attroff(COLOR_PAIR(CP_FRAME) | A_BOLD);
    if (body) {
        /* Said plainly, every frame. Forgetting which body you are driving is
           how you walk a fragile hire into something the MC would have shrugged
           off, and the name alone does not carry it. */
        attron(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
        mvprintw_clip(row++, sb_x, "%s -- you (Tab)", companion_class_name(body));
        attroff(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
    } else {
        mvprintw_clip(row++, sb_x, "%s", CLASS_TABLE[actor->class_id].name);
    }
    row++;

    if (body) {
        /* The same three gauges the character gets, because a hire holding
           the role is the main character and a panel that quietly drops two
           of their bars is telling them they are a lesser thing. They have
           their own level and experience curve; the aether bar is drawn
           whether or not they cast, matching how the character's is handled,
           so the panel does not change height with who you are. */
        draw_bar(row++, sb_x, 18, "HP", body->hp, body->maxhp);
        draw_bar(row++, sb_x, 18, "XP", body->xp, body->xp_next);
        draw_bar(row++, sb_x, 18, "AE", body->aether, body->aether_max);
        row++;
        mvprintw_clip(row++, sb_x, "Level  %d", body->level);
        mvprintw_clip(row++, sb_x, "ATK/DEF %d / %d",
                      body->base_atk + body->atk_buff, body->base_def + body->def_buff);
    } else {
        draw_bar(row++, sb_x, 18, "HP", actor->hp, actor->maxhp);
        draw_bar(row++, sb_x, 18, "XP", actor->xp, actor->xp_next);
        draw_bar(row++, sb_x, 18, "AE", actor->aether, actor->aether_max);
        /* Ammo only means anything once there is something to fire. Every
           character carries an ammo pool from character creation, so drawing
           this unconditionally showed a permanently-full bar to players who had
           never bought a ranged weapon -- which reads as a broken gauge, not an
           empty slot. The "Ranged:" line below is hidden on the same
           condition. */
        if (actor->ranged_type != RANGED_NONE)
            draw_bar(row++, sb_x, 18, "AM", actor->ranged_ammo, actor->ranged_ammo_max);
        row++;
        mvprintw_clip(row++, sb_x, "Level  %d", actor->level);
        mvprintw_clip(row++, sb_x, "ATK/DEF %d / %d", hero_eff_atk(actor), hero_eff_def(actor));
    }
    mvprintw_clip(row++, sb_x, "Gold   %d", p->gold);
    mvprintw_clip(row++, sb_x, "Floor  %d  (best %d)", p->floor, p->deepest_floor);
    mvprintw_clip(row++, sb_x, "Recall charms: %d   Keys: %d", count_recall_charms(p), p->keys);
    for (int t = 0; t < MAT_COUNT; t++) {
        if (p->mat_count[t] <= 0) continue;
        mvprintw_clip(row++, sb_x, "%-9s %d  (~%ld g)",
                      material_name(t), p->mat_count[t], p->mat_value[t]);
    }
    {   /* The larder, on the same rule as the materials above: shown once
           there is something in it. Without this the only place a cut is
           ever visible is inside the pub, which is the one place you cannot
           be standing when you decide whether to fight something with a
           blade or burn it. */
        int cuts = meat_total(p);
        if (cuts > 0) {
            /* Two lines, always -- a line per grade would have grown this
               panel by five rows and pushed the weapon and armour lines off
               the bottom of a short terminal. mvprintw_clip only clips
               columns, so that failure is silent. */
            char detail[64];
            int n = 0;
            detail[0] = '\0';
            for (int g = MEAT_GRADE_COUNT - 1; g >= 0 && n < (int)sizeof detail - 1; g--) {
                if (p->meat[g] <= 0) continue;
                n += snprintf(detail + n, sizeof detail - (size_t)n, "%s%d %s",
                              n ? "  " : "", p->meat[g], meat_name(g));
                if (n >= (int)sizeof detail) break;
            }

            attron(COLOR_PAIR(CP_MON_OUTRIDER));
            mvprintw_clip(row++, sb_x, "Larder: %d cut%s", cuts, cuts == 1 ? "" : "s");
            attroff(COLOR_PAIR(CP_MON_OUTRIDER));
            attron(A_DIM);
            mvprintw_clip(row++, sb_x, "  %s", detail);
            attroff(A_DIM);
        }
    }

    /* Only once it is worth something -- a permanent "x1" is noise. */
    if (p->gold_mult > 1) {
        attron(COLOR_PAIR(CP_GOLD));
        mvprintw_clip(row++, sb_x, "Bank share: x%d", p->gold_mult);
        attroff(COLOR_PAIR(CP_GOLD));
    }
    {   /* Regeneration only exists if something is worn for it, so only say
           so when there is something to say. */
        int regen = effective_stat(actor, ACC_REGEN);
        if (regen > 0) {
            attron(COLOR_PAIR(CP_HP_OK));
            mvprintw_clip(row++, sb_x, "Regenerating +%d HP/turn", regen);
            attroff(COLOR_PAIR(CP_HP_OK));
        }
    }
    /* The party comes before the gear.
     *
       Five hires are five more actors on the floor, each with their own hit
       points, and losing one costs more than any weapon in the game -- but
       they were listed last, below the armour and the ability, so on a short
       window they were the first thing pushed off the panel. What you own is
       static between shops; who is still standing changes every turn. */
    if (companion_count(p) > 0) {
        /* One line each, hard-capped at the bottom of the map frame: a full
           party of five was running the sidebar off the edge of the panel
           and into the message log. Names are shown over classes here
           because the name is what the player recognises on the map; the
           class is on the Tavern list. */
        int party_limit = MAP_ORIGIN_Y + VIEW_H - 1;
        row++;
        if (row < party_limit) {
            attron(A_BOLD); mvprintw_clip(row++, sb_x, "Party"); attroff(A_BOLD);
        }
        int shown = 0, total = 0;
        for (int i = 0; i < MAX_COMPANIONS; i++) if (p->party[(i) + 1].in_use) total++;

        /* The character belongs in this list whenever they are not the one
           holding the role. The list was written when the MC could never be a
           party member, so driving a hire showed you a roster with yourself
           on it and the character missing entirely -- which is backwards
           twice over. */
        if (p->controlled != 0 && row < party_limit) {
            total++;
            shown++;
            attron(COLOR_PAIR(CP_PLAYER));
            mvprintw_clip(row++, sb_x, "@ %s L%d %d/%d",
                          actor->name, actor->level, actor->hp, actor->maxhp);
            attroff(COLOR_PAIR(CP_PLAYER));
        }

        for (int i = 0; i < MAX_COMPANIONS; i++) {
            const Hero *c = &p->party[(i) + 1];
            if (!c->in_use) continue;
            /* Not the body you are driving: that is you, and you are named at
               the top of the panel. A roster that lists you among the people
               you could switch to is asking you to switch to yourself. */
            if (p->controlled == i + 1) { total--; continue; }
            /* Leave a row for the "+N more" line if there is more to say. */
            if (row >= party_limit || (row == party_limit - 1 && shown < total - 1)) break;
            shown++;
            attron(COLOR_PAIR(c->color_pair));
            if (c->alive) {
                /* Stars for what they have turned up down there -- their kit
                   is the one thing about them that changes mid-run. */
                char finds[COMPANION_GEAR_SLOTS + 1];
                int f = 0;
                for (; f < c->gear_count && f < COMPANION_GEAR_SLOTS; f++) finds[f] = '*';
                finds[f] = '\0';
                mvprintw_clip(row++, sb_x, "@ %-17.17s L%-2d %d/%d%s",
                         c->name, c->level, c->hp, companion_max_hp(p, c), finds);
            } else {
                mvprintw_clip(row++, sb_x, "  %-17.17s fallen", c->name);
            }
            attroff(COLOR_PAIR(c->color_pair));
        }
        if (shown < total && row < party_limit) {
            attron(A_DIM);
            mvprintw_clip(row++, sb_x, "  +%d more (taller window shows all)", total - shown);
            attroff(A_DIM);
        }
    }

    row++;
    mvprintw_clip(row++, sb_x, "Weapon: %s%s", actor->weapon_name, plus_tag(actor->weapon_plus));
    mvprintw_clip(row++, sb_x, "Armour: %s%s", actor->armor_name, plus_tag(actor->armor_plus));
    if (actor->ranged_type != RANGED_NONE) {
        mvprintw_clip(row++, sb_x, "Ranged: %s", actor->ranged_name);
    }
    if (actor->ability_cd > 0) {
        mvprintw_clip(row++, sb_x, "Ability: %s (%d)", ability_name(actor->ability_id), actor->ability_cd);
    } else {
        mvprintw_clip(row++, sb_x, "Ability: %s (ready)", ability_name(actor->ability_id));
    }
    row++;
    if (actor->atk_buff_turns > 0) {
        mvprintw_clip(row++, sb_x, "Ether surge +%d atk (%d)", actor->atk_buff, actor->atk_buff_turns);
    }
    if (actor->def_buff_turns > 0) {
        mvprintw_clip(row++, sb_x, "Defence buff +%d (%d)", actor->def_buff, actor->def_buff_turns);
    }
    if (actor->poison_turns_left > 0 || actor->stun_turns_left > 0 || actor->slow_turns_left > 0) {
        attron(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
        if (actor->poison_turns_left > 0) mvprintw_clip(row++, sb_x, "POISONED (%d)", actor->poison_turns_left);
        if (actor->stun_turns_left > 0) mvprintw_clip(row++, sb_x, "STUNNED (%d)", actor->stun_turns_left);
        if (actor->slow_turns_left > 0) mvprintw_clip(row++, sb_x, "SLOWED (%d)", actor->slow_turns_left);
        attroff(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
    }

    if (m->floor_num > 0 && (m->gold_rush || m->overrun)) {
        row++;
        if (m->overrun) {
            attron(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
            mvprintw_clip(row++, sb_x, "OVERRUN FLOOR");
            attroff(COLOR_PAIR(CP_UI_WARN) | A_BOLD);
        }
        if (m->gold_rush) {
            attron(COLOR_PAIR(CP_GOLD) | A_BOLD);
            mvprintw_clip(row++, sb_x, "GOLD RUSH (x%d)", GOLD_RUSH_MULT);
            attroff(COLOR_PAIR(CP_GOLD) | A_BOLD);
        }
    }

    /* And the log sits above the bottom of the window rather than under the
       map, for the same reason. */
    int log_y = MAP_ORIGIN_Y + VIEW_H + 1;
    {
        /* Where the log would sit if the viewport filled the window -- which
           is the same formula as above with the *window's* height instead of
           the clamped viewport's. In text and block modes the two are equal to
           the row, so nothing moves; in tile mode the budget has shrunk
           VIEW_H and this puts the log back at the bottom where it belongs.

           Deliberately not `win_rows - 2 - MSG_LOG_VISIBLE`, which was the
           first version and sat one row lower than the game has always drawn
           it. Text mode is right today -- confirmed by a screenshot from play
           -- so the fix has to be a no-op there, and "one row tighter" is not
           a no-op. */
        /* full_vh is the one computed for the dividers above -- deliberately
           shared rather than recomputed, because the divider under the map and
           the top of the log are the same edge of the same layout, and two
           copies of the formula is how they drift apart. */
        int want = MAP_ORIGIN_Y + full_vh + 1;
        if (want > log_y) log_y = want;
    }
    /* Stop at the sidebar's divider, not at the window edge. */
    draw_message_log_within(log_y, MAP_ORIGIN_X + VIEW_W);

    /* The footer cannot hold every key and never could -- what it has to do
       is make sure the player knows where the rest of them are.
     *
       Clamped onto the last usable row rather than left where the log
       happens to end. At 24 rows -- the stated minimum, and the size the
       screen audit runs at -- the log's own height pushed this onto the frame
       and mvprintw_clip() dropped it, so the smallest supported window has
       been showing *no key hints at all*. It clips its tail when the window
       is narrow, which is intended; going missing entirely is not, and the
       screenshot tool reads the row back now precisely because a literal that
       vanishes leaves nothing behind to notice.

       Single spaces between the groups, not double. Double reads better and
       is 82 characters against the 78 usable at 24x80, so it lost "? keys" --
       the one entry whose whole job is to say where the other keys are. On the
       smallest supported window, being *there* beats being well spaced. */
    int frows, fcols;
    getmaxyx(stdscr, frows, fcols);
    (void)fcols;
    int footer_y = log_y + MSG_LOG_VISIBLE + 1;
    if (footer_y > frows - 2) footer_y = frows - 2;
    mvprintw_clip(footer_y, MAP_ORIGIN_X, "hjkl/yubn move x explore m magic f fire a ability i inv M map R rec ? keys");

    refresh();
}

static const char *describe_consumable(const ConsumableTemplate *t) {
    static char buf[48];
    if (t->is_recall) {
        snprintf(buf, sizeof(buf), "teleports you to town (takes a moment)");
    } else if (t->heal > 0) {
        snprintf(buf, sizeof(buf), "heals %d HP", t->heal);
    } else if (t->atk_buff > 0) {
        snprintf(buf, sizeof(buf), "+%d atk, %d turns", t->atk_buff, t->atk_buff_turns);
    } else if (t->def_buff > 0) {
        snprintf(buf, sizeof(buf), "+%d def, %d turns", t->def_buff, t->def_buff_turns);
    } else if (t->perm_maxhp > 0) {
        /* "(permanent)" on a shelf label reads as "buying this raises your
           max HP", and it does not -- it is a bottle, and it does nothing at
           all until it is drunk out of the pack. Reported as the elixir "not
           working" after buying a few and then selling levels, which leaves
           exactly the base HP a player would expect it to have raised. */
        snprintf(buf, sizeof(buf), "+%d max HP, permanent -- drink it from the pack", t->perm_maxhp);
    } else {
        buf[0] = '\0';
    }
    return buf;
}

static void draw_shop_header(const char *title, const Player *p, const Hero *actor) {
    erase();
    draw_frame(title, COLOR_PAIR(CP_FRAME));
    /* Padded to the widest purse the game can produce, because the bar that
       follows starts at a fixed column and the two are separate calls.
       Unpadded, an eight-digit purse ran the number straight into the bar --
       "Gold: 57408340HP [====" -- and a ten-digit one (the bank's ceiling is
       two billion) wrote over its opening bracket. */
    mvprintw_clip(2, 2, "Gold: %-11d", p->gold);
    draw_bar(2, 20, 16, "HP", actor->hp, actor->maxhp);
}

/* ---- the three doors added last ---------------------------------------- */

/* Today's price for stall `i`, as a percentage of the list price.
 *
   A hash of the day and the stall, not a stored table: the Bazaar has to
   quote the same number every time you walk back in on the same trip, and
   storing a row of percentages in the save for that would be carrying state
   that is a pure function of one integer. Shared with main.c through
   render.h so the screen and the till cannot disagree, which is the one bug
   a shop with moving prices is actually prone to. */
int bazaar_price_pct(const Player *p, int stall) {
    unsigned h = p->bazaar_day * 2654435761u + (unsigned)stall * 40503u;
    h ^= h >> 13; h *= 1274126177u; h ^= h >> 16;
    return BAZAAR_MIN_PCT + (int)(h % (unsigned)(BAZAAR_MAX_PCT - BAZAAR_MIN_PCT + 1));
}

void draw_oracle(const Player *p, const Hero *actor) {
    draw_shop_header("THE ORACLE'S SANCTUARY -- answers, at a price", p, actor);
    int row = 4;
    mvprintw_clip(row++, 2, "  She does not bargain and she is never wrong.");
    row++;
    mvprintw_clip(row++, 2, "  a) The way down        %6dg   the stairs, marked, on the next floor",
                  discounted_price(p, ORACLE_STAIRS_PRICE));
    mvprintw_clip(row++, 2, "  b) The lie of the land %6dg   the next floor, entire",
                  discounted_price(p, ORACLE_FLOOR_PRICE));
    mvprintw_clip(row++, 2, "  c) A long look         %6dg   the next %d floors, entire",
                  discounted_price(p, ORACLE_LONG_PRICE), ORACLE_LONG_FLOORS);
    row++;
    if (p->oracle_floors > 0) {
        mvprintw_clip(row++, 2, "  Paid for: %s, %d floor%s remaining.",
                      p->oracle_full ? "the whole floor" : "the way down",
                      p->oracle_floors, p->oracle_floors == 1 ? "" : "s");
    } else {
        attron(A_DIM);
        mvprintw_clip(row++, 2, "  You have bought nothing. She waits.");
        attroff(A_DIM);
    }
    row++;
    attron(A_DIM);
    mvprintw_clip(row++, 2, "  A reading is spent when you next set foot on a floor, and a");
    mvprintw_clip(row++, 2, "  second reading replaces the first rather than stacking with it.");
    attroff(A_DIM);

    mvprintw_clip(row + 1, 2, "Press a letter to buy, Esc/q to leave.");
    draw_message_log_at(row + 3);
    refresh();
}

void draw_altar(const Player *p, const Hero *actor, int stage, int give, int take) {
    draw_shop_header("THE ALTAR OF SACRIFICE -- a point for a point", p, actor);
    int row = 4;
    mvprintw_clip(row++, 2, "  %s",
                  stage == 0 ? "What will you give up?" : "And what will you have for it?");
    row++;

    /* Three columns of ten, the way the School lays the same thirty out. */
    int per_col = (ATTR_COUNT + 2) / 3;
    for (int i = 0; i < ATTR_COUNT; i++) {
        int col = i / per_col, line = i % per_col;
        int cur = (stage == 0) ? give : take;
        bool on = (i == cur);
        bool spent = (stage == 1 && i == give);
        if (on) attron(A_REVERSE);
        else if (spent) attron(A_DIM);
        mvprintw_clip(row + line, 2 + col * 18, "%-12s %2d", ATTR_NAMES[i], actor->attrs[i]);
        if (on) attroff(A_REVERSE);
        else if (spent) attroff(A_DIM);
    }
    row += per_col + 1;

    if (stage == 1) {
        int price = discounted_price(p, training_price(actor->attrs[take])
                                        * ALTAR_PRICE_NUM / ALTAR_PRICE_DEN);
        mvprintw_clip(row++, 2, "  %s %d -> %d,  %s %d -> %d,  and %d gold.",
                      ATTR_NAMES[give], actor->attrs[give], actor->attrs[give] - 1,
                      ATTR_NAMES[take], actor->attrs[take], actor->attrs[take] + 1, price);
    } else {
        attron(A_DIM);
        mvprintw_clip(row++, 2, "  Nothing may go below %d. The fee is half the School's,",
                      ALTAR_MIN_ATTR);
        mvprintw_clip(row++, 2, "  on the School's own curve -- you are paying in both coins.");
        attroff(A_DIM);
    }

    mvprintw_clip(row + 1, 2, "Arrows/hjkl to choose, Enter to %s, Esc/q to %s.",
                  stage == 0 ? "offer it" : "seal it",
                  stage == 0 ? "leave" : "go back");
    draw_message_log_at(row + 3);
    refresh();
}

void draw_bazaar(const Player *p, const Hero *actor) {
    draw_shop_header("THE BARTER BAZAAR -- today's price, today only", p, actor);
    int row = 4;
    mvprintw_clip(row++, 2, "  The stalls re-price between trips. Some days you wait.");
    row++;
    for (int i = 0; i < APOTHECARY_STOCK_COUNT; i++) {
        const ConsumableTemplate *t = &APOTHECARY_STOCK[i];
        int pct = bazaar_price_pct(p, i);
        int price = discounted_price(p, t->price * pct / 100);
        /* The arrow is the whole screen: the number alone does not tell you
           whether this is a good day unless you have memorised the list. */
        const char *mark = pct <= 80 ? "  cheap" : (pct >= 120 ? "  dear " : "       ");
        if (pct <= 80) attron(A_BOLD);
        else if (pct >= 120) attron(A_DIM);
        mvprintw_clip(row++, 2, "  %c) %-24s %5dg  (%3d%%)%s", 'a' + i, t->name, price, pct, mark);
        if (pct <= 80) attroff(A_BOLD);
        else if (pct >= 120) attroff(A_DIM);
    }
    row++;
    attron(A_DIM);
    mvprintw_clip(row++, 2, "  The same bottles the Apothecary sells, at a price that moves.");
    attroff(A_DIM);

    mvprintw_clip(row + 1, 2, "Press a letter to buy, Esc/q to leave.");
    draw_message_log_at(row + 3);
    refresh();
}

void draw_shop_general(const Player *p, const Hero *actor) {
    draw_shop_header("GENERAL STORE -- dockside sundries", p, actor);
    int row = 4;
    for (int i = 0; i < GENERAL_STOCK_COUNT; i++) {
        const ConsumableTemplate *t = &GENERAL_STOCK[i];
        mvprintw_clip(row++, 4, "%c) %-26s %4dg  %s", 'a' + i, t->name, t->price, describe_consumable(t));
    }
    mvprintw_clip(row + 1, 2, "Press a letter to buy, Esc/q to leave.");
    draw_message_log_at(row + 3);
    refresh();
}

void draw_shop_apothecary(const Player *p, const Hero *actor) {
    draw_shop_header("APOTHECARY -- ether tonics & draughts", p, actor);
    int row = 4;
    for (int i = 0; i < APOTHECARY_STOCK_COUNT; i++) {
        const ConsumableTemplate *t = &APOTHECARY_STOCK[i];
        mvprintw_clip(row++, 4, "%c) %-26s %4dg  %s", 'a' + i, t->name, t->price, describe_consumable(t));
    }
    row++;
    {
        int pr = upgrade_price(actor->hp_plus);
        pr -= pr * actor->shop_discount_pct / 100;
        mvprintw_clip(row++, 2, "  %c) Constitution Draught  %7dg  +%d max HP, permanent%s",
                      (char)('a' + APOTHECARY_STOCK_COUNT), pr, UPGRADE_HP_STEP,
                      plus_tag(actor->hp_plus));
    }
    attron(A_DIM);
    mvprintw_clip(row++, 2, "  The bar itself, not a bottle. It never runs out and never resets.");
    attroff(A_DIM);

    mvprintw_clip(row + 1, 2, "Press a letter to buy, Esc/q to leave.");
    draw_message_log_at(row + 3);
    refresh();
}

static const char *accessory_stat_name(int stat) {
    switch (stat) {
        case ACC_CRIT:    return "crit";
        case ACC_EVASION: return "evasion";
        case ACC_WARD:    return "ward";
        case ACC_GOLD:    return "gold find";
        case ACC_XP:      return "xp find";
        case ACC_REGEN:   return "HP/turn";
        default:          return "?";
    }
}

void draw_shop_armory(const Player *p, const Hero *actor) {
    draw_shop_header("ARMORY -- Company surplus, no questions asked", p, actor);
    mvprintw_clip(3, 2, "Wielding: %-20s%s (+%d atk)%s", actor->weapon_name,
             actor->weapon_plus ? plus_tag(actor->weapon_plus) : "", actor->weapon_bonus,
              actor->weapon_set >= 0 ? "  [set piece]" : "");
    mvprintw_clip(4, 2, "Wearing:  %-20s%s (+%d def)%s", actor->armor_name,
             actor->armor_plus ? plus_tag(actor->armor_plus) : "", actor->armor_bonus,
              actor->armor_set >= 0 ? "  [set piece]" : "");
    mvprintw_clip(5, 2, "Ring: %-27s Trinket: %s",
              actor->ring_name[0] ? actor->ring_name : "(none)",
              actor->trinket_name[0] ? actor->trinket_name : "(none)");
    mvprintw_clip(6, 2, "Ranged: %s", actor->ranged_type == RANGED_NONE ? "(none) -- press w for ranged weapons" : actor->ranged_name);

    /* ---- laid out to fit a short terminal ------------------------------
     *
     * This listed weapons, then armour, then accessories, then the smith, one
     * per line down the page: 41 rows. On the ~30-row terminal this project is
     * actually played on (large fonts, see ROADMAP 2.1e) everything from the
     * accessories down was simply off the bottom -- including both of the
     * smith's lines. The armour upgrade was reachable the whole time, on 'v',
     * by anybody who knew to press it. It could not be *seen*, so it did not
     * exist.
     *
     * Weapons and armour are the same length and the same shape, so they go
     * side by side; accessories pair up the same way. That is 41 rows down to
     * 26, and the smith -- the one entry here that never runs out -- is above
     * the fold on any terminal the game will start on. */
    int row = 8;
    int colw = COLS / 2 - 2;
    if (colw < 34) colw = 34;
    int c2 = 2 + colw;

    mvprintw_clip(row, 2,  "Weapons:");
    mvprintw_clip(row, c2, "Armour:");
    row++;
    int gear_rows = WEAPON_STOCK_COUNT > ARMOR_STOCK_COUNT ? WEAPON_STOCK_COUNT : ARMOR_STOCK_COUNT;
    for (int i = 0; i < gear_rows; i++) {
        if (i < WEAPON_STOCK_COUNT) {
            const GearTemplate *g = &WEAPON_STOCK[i];
            /* Flag anything that would be a sidegrade or worse, so a 15g
               starter blade can't quietly replace a set weapon three times its
               value just because it was one keypress away. */
            const char *note = g->bonus <= actor->weapon_bonus ? " v" : "";
            mvprintw_clip(row + i, 4, "%c) %-20s %5dg +%d%s",
                          'a' + i, g->name, g->price, g->bonus, note);
        }
        if (i < ARMOR_STOCK_COUNT) {
            const GearTemplate *g = &ARMOR_STOCK[i];
            const char *note = g->bonus <= actor->armor_bonus ? " v" : "";
            mvprintw_clip(row + i, c2 + 2, "%c) %-20s %5dg +%d%s",
                          'a' + WEAPON_STOCK_COUNT + i, g->name, g->price, g->bonus, note);
        }
    }
    row += gear_rows + 1;

    mvprintw_clip(row++, 2, "Accessories (fill ring, then trinket, then replace ring):");
    for (int i = 0; i < ACCESSORY_STOCK_COUNT; i += 2) {
        for (int k = 0; k < 2 && i + k < ACCESSORY_STOCK_COUNT; k++) {
            const AccessoryTemplate *a = &ACCESSORY_STOCK[i + k];
            /* Every accessory but regeneration grants a percentage; the coil
               grants flat HP a turn, and printing "+6% HP/turn" for it would
               be a lie about what the player is buying. */
            const char *unit = (a->stat == ACC_REGEN) ? "" : "%";
            mvprintw_clip(row, k ? c2 + 2 : 4, "%c) %-17s %5dg +%d%s %s",
                          'a' + WEAPON_STOCK_COUNT + ARMOR_STOCK_COUNT + i + k,
                          a->name, a->price, a->bonus, unit, accessory_stat_name(a->stat));
        }
        row++;
    }
    row++;

    attron(A_BOLD);
    mvprintw_clip(row++, 2, "The smith -- works on what you already carry, and never runs out:");
    attroff(A_BOLD);
    {
        /* Mirrors main.c's discounted_price. Duplicated rather than exposed
           because it is two lines and the shop screens are the only readers. */
        int wp = upgrade_price(actor->weapon_plus);
        int ap = upgrade_price(actor->armor_plus);
        wp -= wp * actor->shop_discount_pct / 100;
        ap -= ap * actor->shop_discount_pct / 100;
        char key = (char)('a' + WEAPON_STOCK_COUNT + ARMOR_STOCK_COUNT + ACCESSORY_STOCK_COUNT);

        /* The material cost has to be on the line, not discovered by being
           refused: a price you cannot see is a price you cannot plan for. */
        char wmat[48] = "", amat[48] = "";
        int wt = upgrade_material_tier(actor->weapon_plus), wc = upgrade_material_cost(actor->weapon_plus);
        int at = upgrade_material_tier(actor->armor_plus),  ac = upgrade_material_cost(actor->armor_plus);
        if (wc > 0) snprintf(wmat, sizeof(wmat), " +%d %s(%d)", wc, material_name(wt), p->mat_count[wt]);
        if (ac > 0) snprintf(amat, sizeof(amat), " +%d %s(%d)", ac, material_name(at), p->mat_count[at]);

        mvprintw_clip(row++, 4, "%c) Sharpen weapon   %8dg -> +%-3d (+%d atk)%s",
                      key, wp, actor->weapon_plus + 1, UPGRADE_STEP, wmat);
        mvprintw_clip(row++, 4, "%c) Reinforce armour %8dg -> +%-3d (+%d def)%s",
                      (char)(key + 1), ap, actor->armor_plus + 1, UPGRADE_STEP, amat);
    }
    attron(A_DIM);
    mvprintw_clip(row++, 4, "Upgrades stay with the piece; buying a new one starts it over.");
    attroff(A_DIM);

    mvprintw_clip(row + 1, 2, "Press a letter to buy & equip, w for ranged weapons, Esc/q to leave.");
    draw_message_log_at(row + 3);
    refresh();
}

static const char *ranged_type_desc(int type) {
    switch (type) {
        case RANGED_BOW:     return "steady damage, no special trick";
        case RANGED_GUN:     return "hardest single-target hit, longest reach";
        case RANGED_LASER:   return "pierces into a second target behind the first";
        case RANGED_BLOWGUN: return "lighter hit, weakens the target's attack";
        case RANGED_THROWN:  return "cheap and fast, benefits from crit chance";
        case RANGED_GRENADE: return "area damage to everything nearby";
        default:              return "";
    }
}

void draw_shop_ranged(const Player *p, const Hero *actor) {
    draw_shop_header("ARMORY -- RANGED WEAPONS", p, actor);
    mvprintw_clip(3, 2, "Equipped: %s", actor->ranged_type == RANGED_NONE ? "(none)" : actor->ranged_name);
    mvprintw_clip(4, 2, "Ammo: %d/%d (from Precision)   Damage bonus: +%d%%",
              actor->ranged_ammo, actor->ranged_ammo_max, actor->ranged_dmg_bonus_pct);

    int row = 6;
    int last_type = -1;
    for (int i = 0; i < RANGED_STOCK_COUNT; i++) {
        const RangedTemplate *t = &RANGED_STOCK[i];
        if (t->type != last_type) {
            last_type = t->type;
            mvprintw_clip(row++, 2, "%s -- %s:", ranged_type_name(t->type), ranged_type_desc(t->type));
        }
        mvprintw_clip(row++, 4, "%c) %-24s %5dg  +%d dmg  %d ammo  %d turn cooldown",
                  'a' + i, t->name, t->price, t->bonus, t->ammo_cost, t->cooldown);
    }

    mvprintw_clip(row + 1, 2, "Press a letter to buy & equip, Esc/q to go back.");
    draw_message_log_at(row + 3);
    refresh();
}

void draw_shop_merchant(const Player *p, const Hero *actor) {
    draw_shop_header("A WANDERING TRADER -- prices reflect the risk of finding you", p, actor);
    int row = 4;
    for (int i = 0; i < MERCHANT_STOCK_COUNT; i++) {
        const ConsumableTemplate *t = &MERCHANT_STOCK[i];
        mvprintw_clip(row++, 4, "%c) %-26s %4dg  %s", 'a' + i, t->name, t->price, describe_consumable(t));
    }
    mvprintw_clip(row + 1, 2, "Press a letter to buy, Esc/q to move on.");
    draw_message_log_at(row + 3);
    refresh();
}

/* ---- what this save directory has to show for itself --------------------
 *
 * ROADMAP 2.2, and it was right that the data mostly existed already: kills,
 * turns, gold and depth are on Player, the lifetime best is in the highscore
 * file and the per-class bests are in their own. What it could not have
 * anticipated is the fallen roster, which arrived for the barrow (§103) and
 * turns out to be the interesting half -- a list of records is a scoreboard,
 * and a list of the dead is a history.
 *
 * Laid out for 24x80 because that is the floor, and both lists are capped
 * rather than paged: a hundred classes will not fit and a player who has
 * played eleven of them wants the deepest six, not a paging control.
 */
void draw_records_screen(const Player *p, const Hero *actor) {
    draw_shop_header("RECORDS -- this run, and the ones before it", p, actor);
    int row = 4;

    mvprintw_clip(row++, 2, "This run   %s, the %s",
                  actor->name[0] ? actor->name : "unnamed",
                  CLASS_TABLE[actor->class_id].name);
    mvprintw_clip(row++, 2, "           floor %d (deepest %d)   level %d   %d kills",
                  p->floor, p->deepest_floor, actor->level, actor->kills);
    mvprintw_clip(row++, 2, "           %d gold   %d turns   %s, %s",
                  p->gold, p->turns, difficulty_name(p->difficulty),
                  world_size_name(p->world_size));
    row++;

    int best = load_highscore();
    mvprintw_clip(row++, 2, "Lifetime   best floor %d -- %s", best, title_for_floor(best));
    row++;

    /* Only classes actually taken down a hole. A hundred rows of zero is not
       a record of anything. */
    attron(A_BOLD);
    mvprintw_clip(row++, 2, "Deepest by class");
    attroff(A_BOLD);
    int shown = 0, col = 0;
    int class_row = row;
    for (int c = 0; c < NUM_CLASSES && shown < 6; c++) {
        int cb = class_best_floor(c);
        if (cb <= 0) continue;
        mvprintw_clip(class_row + shown / 3, 4 + col * 25, "%-16s %3d",
                      CLASS_TABLE[c].name, cb);
        shown++;
        col = shown % 3;
    }
    if (shown == 0) mvprintw_clip(class_row, 4, "Nothing yet. Take one down and come back.");
    row = class_row + (shown > 3 ? 2 : 1) + 1;

    attron(A_BOLD);
    mvprintw_clip(row++, 2, "The fallen -- and what the barrows are made of");
    attroff(A_BOLD);

    FallenRecord ring[FALLEN_MAX];
    int n = load_fallen(ring, FALLEN_MAX);
    if (n <= 0) {
        mvprintw_clip(row++, 4, "Nobody yet. The barrows stay shut until somebody is in them.");
    } else {
        int want = n < 5 ? n : 5;
        for (int i = 0; i < want; i++) {
            const FallenRecord *fr = &ring[i];
            mvprintw_clip(row++, 4, "%-22s  L%-3d floor %-3d  %d gold",
                          fr->name[0] ? fr->name : "the nameless",
                          fr->level, fr->floor, fr->gold);
        }
        if (n > want) mvprintw_clip(row++, 4, "...and %d more, further back.", n - want);
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    mvprintw_clip(rows - 2, 2, "Esc/q to return.");
}

void draw_quest_board(const Player *p, const Hero *actor) {
    draw_shop_header("QUEST BOARD -- bounties posted by the desperate", p, actor);
    int row = 4;
    if (p->quest_active) {
        if (p->quest_type == QUEST_FETCH) {
            mvprintw_clip(row++, 2, "Active bounty: recover %s from floor %d.", p->quest_monster, p->quest_floor);
            mvprintw_clip(row++, 2, "Reward: %d gold, %d xp.", p->quest_gold_reward, p->quest_xp_reward);
            row++;
            if (p->quest_item_found) {
                mvprintw_clip(row++, 2, "You have it -- this board pays out the moment you walk up to it.");
            } else {
                mvprintw_clip(row++, 2, "Bring it back here once you've found it.");
            }
        } else if (p->quest_type == QUEST_CLEAR) {
            mvprintw_clip(row++, 2, "Active bounty: clear %d foes from floor %d. (%d/%d so far)",
                     p->quest_kills_needed, p->quest_floor, p->quest_kills_done, p->quest_kills_needed);
            mvprintw_clip(row++, 2, "Reward: %d gold, %d xp.", p->quest_gold_reward, p->quest_xp_reward);
            row++;
            mvprintw_clip(row++, 2, "It completes automatically the moment the count is reached.");
        } else if (p->quest_type == QUEST_TIMED) {
            long left = p->quest_deadline - p->turns;
            mvprintw_clip(row++, 2, "Active bounty: stand on floor %d before the clock runs out.", p->quest_floor);
            mvprintw_clip(row++, 2, "Reward: %d gold, %d xp.", p->quest_gold_reward, p->quest_xp_reward);
            row++;
            if (left > 0) mvprintw_clip(row++, 2, "%ld turns left.", left);
            else          mvprintw_clip(row++, 2, "The clock has run out. Arriving now pays nothing.");
        } else if (p->quest_type == QUEST_NORECALL) {
            mvprintw_clip(row++, 2, "Active bounty: reach floor %d without cracking a recall charm.", p->quest_floor);
            mvprintw_clip(row++, 2, "Reward: %d gold, %d xp.", p->quest_gold_reward, p->quest_xp_reward);
            row++;
            mvprintw_clip(row++, 2, p->quest_broken
                          ? "A charm has been spent. Arriving now pays nothing."
                          : "No charm spent yet.");
        } else if (p->quest_type == QUEST_ESCORT) {
            int s = p->quest_escort_slot;
            bool up = (s >= 1 && s < MAX_PARTY && hero_is_up(&p->party[s]));
            mvprintw_clip(row++, 2, "Active bounty: get %s to floor %d alive.",
                          up ? p->party[s].name : "your client", p->quest_floor);
            mvprintw_clip(row++, 2, "Reward: %d gold, %d xp.", p->quest_gold_reward, p->quest_xp_reward);
            row++;
            if (up) mvprintw_clip(row++, 2, "They walk and fight on their own. They will not wait for you.");
            else    mvprintw_clip(row++, 2, "Your client is dead. Nobody is paying for that.");
        } else {
            mvprintw_clip(row++, 2, "Active bounty: slay a %s on floor %d.", p->quest_monster, p->quest_floor);
            mvprintw_clip(row++, 2, "Reward: %d gold, %d xp.", p->quest_gold_reward, p->quest_xp_reward);
            row++;
            mvprintw_clip(row++, 2, "It completes automatically the moment you land the killing blow.");
        }
        row++;
        mvprintw_clip(row++, 2, "a) Abandon this bounty");
    } else {
        mvprintw_clip(row++, 2, "No bounty posted right now.");
        row++;
        mvprintw_clip(row++, 2, "a) Take a new bounty");
    }
    mvprintw_clip(row + 1, 2, "Press a letter, Esc/q to leave.");
    draw_message_log_at(row + 3);
    refresh();
}

static const char *effect_desc(int effect) {
    switch (effect) {
        case EFFECT_DAMAGE:      return "strikes the nearest visible foe";
        case EFFECT_DAMAGE_NOVA: return "damages every visible foe in reach";
        case EFFECT_SCORCH:      return "sears the ground around you into hazard";
        case EFFECT_BUFF_ATK:    return "grants a temporary attack bonus";
        case EFFECT_HEAL_SELF:   return "heals you instantly";
        case EFFECT_WARD_SHIELD: return "grants a temporary defence bonus";
        case EFFECT_BARRIER:     return "seals a nearby passage shut";
        case EFFECT_PURGE:       return "clears hazard tiles around you";
        case EFFECT_BLINK:       return "teleports you a short distance";
        case EFFECT_REVEAL:      return "reveals the passage around you";
        case EFFECT_BRIDGE:      return "spans nearby water with a bridge";
        case EFFECT_STUN:        return "locks the nearest foe in place";
        case EFFECT_SLOW:        return "weighs down the nearest foe";
        case EFFECT_UNBIND:      return "opens the nearest locked door";
        case EFFECT_LIFE_DRAIN:  return "drains the nearest foe, healing you";
        case EFFECT_CURSE:       return "curses the nearest foe's attack";
        case EFFECT_DOT_BURN:    return "burns and curses the nearest foe";
        default:                 return "";
    }
}

/* Rows a spell list can use, given what the screen around it needs:
   `chrome` counts the frame, header lines, detail pane and footer. */
static int spell_page_rows(int chrome) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int n = rows - chrome;
    if (n < 5) n = 5;
    return n;
}

void draw_shop_arcanist(const Player *p, const Hero *actor, int cursor) {
    draw_shop_header("ARCANIST'S GUILD -- aether theory, sold by the vial", p, actor);
    /* Row 3, not row 2: the header puts "Gold:" and the HP bar on row 2, and
       this line was printed straight over the top of them -- so the one shop
       whose prices run into four figures was also the only one that would
       not tell you what you had to spend. Row 3 was empty. */
    mvprintw_clip(3, 2, "Aether: %d/%d   Power: +%d%%   School: %s (the only one taught)",
                  actor->aether, actor->aether_max, actor->spell_power_pct,
                  school_name(actor->magic_school));

    /* The guild only teaches the caster's own school, so the list is the
       school rather than the whole pool -- `cursor` indexes that list, and
       spell_school_index maps it back to the pool index everything else
       stores and casts by. */
    int school = actor->magic_school;
    int total = spell_school_count(school);
    int page = spell_page_rows(11);      /* header, detail pane, footer, frame */
    int top = (cursor / page) * page;
    int row = 5;

    if (total <= 0) {
        attron(COLOR_PAIR(CP_UI_WARN));
        mvprintw_clip(row, 2, "The guild has nothing to teach a %s caster.", school_name(school));
        attroff(COLOR_PAIR(CP_UI_WARN));
    }

    for (int i = 0; i < page && top + i < total; i++) {
        int idx = spell_school_index(school, top + i);
        const SpellTemplate *t = spell_pool_get(idx);
        if (!t) continue;
        bool known = false;
        for (int k = 0; k < actor->spell_count; k++) {
            if (actor->known_spells[k] == idx) { known = true; break; }
        }

        char status[24];
        if (known) snprintf(status, sizeof(status), "known");
        else snprintf(status, sizeof(status), "%dg", t->learn_price);

        bool sel = (top + i == cursor);
        if (sel) attron(A_REVERSE);
        mvprintw_clip(row + i, 2, "%-28s L%d %-13s %s", t->name, t->level, school_name(t->school), status);
        if (sel) attroff(A_REVERSE);
    }

    const SpellTemplate *cur = spell_pool_get(spell_school_index(school, cursor));
    /* Follow the last entry actually drawn, not the page height -- on a tall
       window a 30-spell school inside a 33-row page left three dead rows
       between the list and the description. */
    int shown_n = total - top;
    if (shown_n > page) shown_n = page;
    if (shown_n < 0) shown_n = 0;
    int detail_row = row + shown_n + 1;
    if (cur) {
        mvprintw_clip(detail_row, 2, "%s -- %s", cur->name, effect_desc(cur->effect));
        mvprintw_clip(detail_row + 1, 2, "%d charge, %d turn cooldown", cur->charge_cost, cur->cooldown);
    }

    {
        int pr = upgrade_price(actor->aether_plus);
        pr -= pr * actor->shop_discount_pct / 100;
        mvprintw_clip(detail_row + 2, 2, "a) Widen your channel  %7dg  +%d aether%s",
                      pr, UPGRADE_AETHER_STEP, plus_tag(actor->aether_plus));
    }
    mvprintw_clip(detail_row + 3, 2, "Up/Down move  PgUp/PgDn page  Enter learn  a widen  Esc/q leave");
    draw_message_log_at(detail_row + 5);
    refresh();
}

static const char *describe_stack(const InvStack *s) {
    static char buf[48];
    if (s->is_recall) {
        snprintf(buf, sizeof(buf), "press 'r' in the dungeon to use");
    } else if (s->heal > 0) {
        snprintf(buf, sizeof(buf), "heals %d HP", s->heal);
    } else if (s->atk_buff > 0) {
        snprintf(buf, sizeof(buf), "+%d atk, %d turns", s->atk_buff, s->atk_buff_turns);
    } else if (s->def_buff > 0) {
        snprintf(buf, sizeof(buf), "+%d def, %d turns", s->def_buff, s->def_buff_turns);
    } else if (s->perm_maxhp > 0) {
        snprintf(buf, sizeof(buf), "+%d max HP (permanent)", s->perm_maxhp);
    } else {
        buf[0] = '\0';
    }
    return buf;
}

void draw_inventory_screen(const Player *p, const Hero *actor, int page) {
    erase();
    draw_frame("INVENTORY", COLOR_PAIR(CP_FRAME));
    mvprintw_clip(2, 2, "Gold %d", p->gold);
    draw_bar(2, 14, 16, "HP", actor->hp, actor->maxhp);

    int pages = (p->inv_count + INV_PAGE - 1) / INV_PAGE;
    if (pages < 1) pages = 1;
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;

    int row = 4;
    if (p->inv_count == 0) {
        mvprintw_clip(row++, 4, "(empty -- the temple's dust has taken everything else)");
    }
    int first = page * INV_PAGE;
    int last  = first + INV_PAGE;
    if (last > p->inv_count) last = p->inv_count;
    for (int i = first; i < last; i++) {
        const InvStack *s = &p->inventory[i];
        mvprintw_clip(row++, 4, "%c) %-26s x%-3d %s",
                 'a' + (i - first), s->name, s->count, describe_stack(s));
    }
    if (pages > 1)
        mvprintw_clip(row + 1, 2, "Page %d/%d -- left/right to turn, letter to use, Esc/q to close.",
                 page + 1, pages);
    else
        mvprintw_clip(row + 1, 2, "Press a letter to use one, SHIFT+letter for %d, Esc/q to close.", INV_BULK_USE);
    refresh();
}

void draw_spell_menu(const Hero *actor, int cursor) {
    erase();
    draw_frame("CAST A SPELL", COLOR_PAIR(CP_MAGIC));
    draw_bar(2, 2, 18, "AE", actor->aether, actor->aether_max);

    if (actor->spell_count == 0) {
        mvprintw_clip(4, 2, "You haven't learned any spells. Visit the Arcanist's Guild in town.");
        mvprintw_clip(6, 2, "Press any key to return.");
        refresh();
        return;
    }

    int page = spell_page_rows(11);
    int top = (cursor / page) * page;
    int row = 4;
    for (int i = 0; i < page && top + i < actor->spell_count; i++) {
        int slot = top + i;
        const SpellTemplate *t = spell_pool_get(actor->known_spells[slot]);
        if (!t) continue;

        char status[24];
        if (actor->spell_cd[slot] > 0) snprintf(status, sizeof(status), "cooling (%d)", actor->spell_cd[slot]);
        else if (actor->aether < t->charge_cost) snprintf(status, sizeof(status), "needs %d charge", t->charge_cost);
        else snprintf(status, sizeof(status), "ready (%d)", t->charge_cost);

        bool sel = (slot == cursor);
        if (sel) attron(A_REVERSE);
        mvprintw_clip(row + i, 2, "%-28s L%d %-13s %s", t->name, t->level, school_name(t->school), status);
        if (sel) attroff(A_REVERSE);
    }

    const SpellTemplate *cur = spell_pool_get(actor->known_spells[cursor < actor->spell_count ? cursor : 0]);
    int shown_n = actor->spell_count - top;
    if (shown_n > page) shown_n = page;
    if (shown_n < 0) shown_n = 0;
    int detail_row = row + shown_n + 1;
    if (cur) {
        mvprintw_clip(detail_row, 2, "%s -- %s", cur->name, effect_desc(cur->effect));
    }

    mvprintw_clip(detail_row + 2, 2, "Up/Down move  PgUp/PgDn page  Enter cast  Esc/q cancel");
    draw_message_log_at(detail_row + 4);
    refresh();
}
