#ifndef TILEART_H
#define TILEART_H

/* What every drawable thing in the dungeon looks like, in one table.
 *
 * The game resolves a map cell to an ArtId (see resolve_cell in render.c) and
 * each render mode then asks this table a different question about it:
 *
 *   RENDER_TEXT    -- nothing; that mode keeps the original ASCII glyph and
 *                     colour-pair path, which is the one that runs anywhere.
 *   RENDER_BLOCKS  -- ArtDef.glyph + ArtDef.fg/bg, drawn as a true-colour cell.
 *   RENDER_TILES   -- ArtDef.sprite, rasterised to the terminal's real cell
 *                     pixel size and blitted as pixel graphics.
 *
 * Keeping all three keyed off the same ArtId is what stops the modes drifting
 * apart: adding a tile type is one enum entry and one table row, and a missing
 * row is caught by tests/tileart.c rather than by a '?' appearing mid-run.
 */

#include "tui.h"
#include <stdbool.h>
#include <stdint.h>

/* Sprites are authored at 8x16 -- the classic text-cell aspect, so scaling to
   a real terminal cell (commonly 8x17 or 10x21) is close to 1:1 and never
   distorts. */
#define ART_SPRITE_W 8
#define ART_SPRITE_H 16

/* Sprite palette. Each pixel names a role rather than a colour, so one sprite
   serves any colour scheme and re-tinting a tile is a table edit:

     ' '  transparent  -- leave whatever is underneath (terrain, for actors)
     '.'  background   -- ArtDef.bg
     'o'  outline      -- fg darkened hard
     '-'  shade        -- fg darkened
     '#'  base         -- fg
     '+'  highlight    -- fg brightened
     '*'  accent       -- ArtDef.accent
     ':'  accent shade -- accent darkened
*/
#define ART_PALETTE_CHARS " .o-#+*:"

typedef enum {
    /* terrain -- one per TileType */
    ART_WALL = 0,
    ART_FLOOR,
    ART_DECOR,
    ART_WATER,
    ART_BRIDGE,
    ART_LAVA,
    ART_MIASMA,
    ART_STAIRS_DOWN,
    ART_STAIRS_UP,
    ART_PORTAL,
    ART_LOCKED_DOOR,
    ART_SEALED_DOOR,
    ART_LEVER,
    ART_QUEST_BOARD,
    ART_TEMPLE_ENTRANCE,
    ART_SHOP_GENERAL,
    ART_SHOP_ARMORY,
    ART_SHOP_APOTHECARY,
    ART_SHOP_ARCANIST,
    ART_INN,
    ART_TAVERN,

    /* map features -- one per FeatureType */
    ART_SHRINE,
    ART_FOUNTAIN,
    ART_MERCHANT,
    ART_MACHINE,
    ART_RELIC,
    ART_TOWN_GATE,

    /* floor items */
    ART_GOLD,
    ART_KEY,
    ART_POTION,

    /* actors */
    ART_PLAYER,
    ART_MON_VERMIN,
    ART_MON_CLOCKWORK,
    ART_MON_RUINS,
    ART_MON_OUTRIDER,
    ART_MON_ABYSSAL,
    ART_MON_BOSS,

    ART_UNKNOWN,
    ART_THICKET,
    /* Not terrain and not an actor: the mark on a square you can hear but
       cannot see. Drawn as an occupant so the graphical modes show it at all
       -- see the note in render.c's cell_apply_heard(). */
    ART_HEARD,
    ART_COUNT
} ArtId;

typedef struct {
    const char *name;      /* for test failure messages and the art dump */
    uint32_t    glyph;     /* Unicode codepoint for RENDER_BLOCKS; single-width */
    TuiRGB      fg, bg, accent;
    bool        solid;     /* terrain (fills its cell) vs. an actor/item overlay */
    const char *sprite[ART_SPRITE_H];
} ArtDef;

/* Never returns NULL -- an out-of-range id yields ART_UNKNOWN's definition,
   which is deliberately garish so a missing mapping is obvious on screen. */
const ArtDef *art_def(ArtId id);

/* Map the game's own enums onto the table. Kept here rather than in render.c
   so the tests can assert every enumerator is covered without linking the
   renderer. */
ArtId art_for_tile(int tile_type);
ArtId art_for_feature(int feature_type);
ArtId art_for_monster(int color_pair, bool is_boss);

/* Rasterise one sprite into an RGBA buffer at arbitrary size (nearest
   neighbour from 8x16). `dim` is the remembered-but-not-currently-visible
   state the map uses. Pixels the sprite leaves transparent are written only
   if `opaque_bg` is set -- an actor blitted over terrain wants them left
   alone. `stride` is in bytes per output row. */
void art_sprite_rgba(ArtId id, bool dim, bool opaque_bg,
                     unsigned char *rgba, int w, int h, int stride);

/* The same, with the figure's colour replaced by one the caller chose.
   For the party: they share a sprite and are told apart by colour, so the
   colour has to come from the game rather than from the art. NULL is exactly
   art_sprite_rgba(). */
void art_sprite_rgba_tinted(ArtId id, bool dim, bool opaque_bg,
                            const TuiRGB *fg_override,
                            unsigned char *rgba, int w, int h, int stride);

/* fg/bg the block renderer should use for this art, after the dim pass.
   Split out from art_def so the dimming rule lives in exactly one place. */
void art_block_colors(ArtId id, bool dim, TuiRGB *fg, TuiRGB *bg);

/* ---- biome tinting ----------------------------------------------------
 * Every floor used to be cut from the same grey stone: a hundred floors that
 * differ in monster roster and hazard mix, and in nothing you can see at a
 * glance. The art table stays one set of sprites -- authoring five of
 * everything would be five times the table and five times the drift -- and
 * the biome instead shifts the colour of the *terrain*.
 *
 * Terrain only. Actors, items and features keep their own colours, because
 * they are the things you have to pick out of the background, and a monster
 * that goes green in the jungle is a monster you walk into.
 *
 * Set it when a floor is entered; it applies to both graphical modes. Town
 * and any out-of-range value mean "no tint", so nothing has to special-case
 * the surface. */
/* The tint index for the surface. Past the five biomes on purpose: the city is
   not a band of the descent, it is the place you come back to. */
#define ART_TINT_CITY 5

void art_set_biome(int biome);
int  art_get_biome(void);

#endif
