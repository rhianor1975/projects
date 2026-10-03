#ifndef VISION_H
#define VISION_H

#include "common.h"

/* ---- one map per body --------------------------------------------------
 *
 * Every hero keeps their own picture of the floor: what they can see from
 * where they are standing, and what they have personally been shown.
 *
 * Why this exists rather than one shared pair of flags on the Tile. The
 * renderer, auto-explore and the path searches all read `Tile.visible` and
 * `Tile.seen`, and those two fields were written by whichever body the code
 * happened to be thinking about at the time. That is the same premise that
 * gave `Player` two meanings, one layer down: a single set of eyes, belonging
 * to nobody in particular, that everything assumed was the character's. It
 * produced a '@' that vanished when you switched bodies and an auto-explore
 * that reported itself stuck while walking perfectly well.
 *
 * So: six maps, one per party slot, and the Tile flags become the *composite*
 * that the screen is drawn from. Nothing outside this module has to change
 * how it reads a tile -- but there is now an answer to "what does this
 * particular hero know", and it is a real one rather than a shared global
 * that the last caller happened to write.
 *
 * ---- on cost ----
 *
 * 1400x800 per layer, two layers, six slots: 13.4 MB, held once for the whole
 * program rather than per floor. That is deliberate and it was asked for --
 * the alternative is a bitset at 1/8th the size and eight times the fiddle,
 * and this is not the part of the game that is short of memory.
 *
 * Time is the part that would have been short. Clearing six 1.1-million-tile
 * layers every turn is 6.7 million writes for the sake of undoing a few
 * hundred, which is the exact mistake compute_fov's lit-list was written to
 * avoid at 1400x800 (EVALUATION 57). So each slot keeps its own lit-list and
 * clears only what it lit. The composite is rebuilt the same way.
 *
 * ---- on threads ----
 *
 * Six bodies recomputing six independent maps is the shape that invites a
 * thread each, and it was considered. It is not worth it and it is not close:
 * the work is a few hundred line-of-sight walks per turn, the six maps are
 * written once and read once in the same turn, and the join cost would exceed
 * the work. The reason the game ever felt slow here was a map-sized sweep per
 * turn, not a shortage of cores -- and the fix for that is the lit-list, which
 * is already in. If this ever does become the bottleneck the slots are already
 * independent, which is the property that would make threading safe.
 */

/* What one body knows. Indexed [y][x] like the Map itself. */
typedef struct {
    unsigned char vis[MAP_H_MAX][MAP_W_MAX];   /* lit for this body, this turn */
    unsigned char seen[MAP_H_MAX][MAP_W_MAX];  /* this body has been shown it */
} HeroVision;

/* The map belonging to party slot `slot` (0..MAX_PARTY-1), or NULL if the
   slot is out of range. */
const HeroVision *hero_vision(int slot);

/* True if that body has ever been shown (x,y). The question the per-body
   maps exist to be able to answer. */
bool hero_has_seen(int slot, int x, int y);

/* Forget everything, for every body. Call wherever the floor is replaced --
   the same places that call fov_forget(). */
void hero_vision_forget(void);

/* Seeds every in-use body's memory from the map's own `seen` flags.
 *
   A saved run carries the party's shared memory of a floor and not six
   private copies of it, because the save is the Map struct and always has
   been. Rather than grow the save file by six times a million tiles for a
   distinction nobody can see, a loaded floor hands every body the party's
   memory and they diverge again from there. */
void hero_vision_seed_from_map(const Map *m, const Player *p);

/* Recomputes every in-use body's map and composites the result back into
   m->tiles[].visible / .seen, which is what the screen and the searches read.
 *
   The driven body looks with its own eyes -- its own Vision-derived radius,
   and its own answer to "am I standing in the mire". The rest see COMPANION_FOV
   around themselves, which is the scouting radius hiring has always bought and
   is left alone here on purpose: this change is about *whose* map is whose, and
   handing every hire the character's sight radius would be a balance change
   wearing a refactor's clothes. */
void hero_vision_update(Map *m, const Player *p, int driven_radius);

/* True if a living monster stands there that nobody can see but somebody can
   hear -- within their `hearing_radius`, through walls. Rebuilt by
   hero_vision_update() after the composite, so it answers against the picture
   actually on screen. The renderer marks the square without naming what is on
   it. See the long note in vision.c. */
bool vision_heard_at(int x, int y);

#endif
