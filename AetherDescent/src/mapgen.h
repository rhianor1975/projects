#ifndef MAPGEN_H
#define MAPGEN_H

#include "common.h"

/* Generates a fresh procedural floor of the temple. floor_num is 1..MAX_FLOOR.
   Populates rooms, corridors, stairs, monsters and loot, all scaled to depth.
   Returns the (x,y) the player should start at via out_x/out_y (the up-stairs,
   i.e. where you arrive when descending from the floor above). */
void generate_temple_floor(Map *m, int floor_num, int *out_x, int *out_y, const Player *p);

/* The RNG stream a given floor of a given run is generated from. Exposed so
   a test can assert that the same seed really does rebuild the same floor,
   and so nothing else invents a second way of deriving it. */
unsigned int floor_seed(unsigned int run_seed, int floor_num);

/* What a gold-rush floor multiplies its coin by. Shared with the HUD so the
   marker and the maths can never disagree. */
#define GOLD_RUSH_MULT 20

/* What one piece of scrap off `floor_num` is worth at the junkyard. Exposed
   because scrap arrives two ways -- lying on the floor, and accumulating as
   you walk -- and both have to be worth the same thing. */
int junk_value_for_floor(int floor_num);

/* Steps between the scavenging trickle: walking the deep floors turns up
   scrap whether or not you went looking. */
#define JUNK_STEPS_PER_PIECE 100

/* Recomputes the visible/seen flags for tiles within `radius` of (px,py),
   blocked by walls (simple Bresenham line-of-sight per tile). */
/* Is (tx,ty) reachable on foot from (sx,sy) over the map as generated?
 *
   Exposed so the suite can assert the one property a floor must have and
   which nothing checked: that the way down is reachable from the way in. A
   floor that fails it is not hard, it is broken -- and mapgen is where a
   structural change is most likely to produce one. */
bool tile_reachable(const Map *m, int sx, int sy, int tx, int ty);

void compute_fov(Map *m, int px, int py, int radius);

/* Same lighting pass, but without clearing what is already lit -- so a
   second pair of eyes can be added to the frame. compute_fov must run first;
   this only ever adds. Hired companions use it: they are people walking the
   floor, and there is no fog of war around a person. */
void add_fov(Map *m, int px, int py, int radius);

/* Drops the "what was lit last frame" cache that compute_fov uses to avoid
   sweeping the whole map every turn. Call it whenever the map is replaced
   under the renderer -- a new floor, a loaded save -- so the next frame
   clears from scratch instead of unlighting tiles in a map that no longer
   exists. */
void fov_forget(void);

Biome get_biome_for_floor(int floor_num);
const char *biome_name(Biome b);

/* True if a straight line from (x0,y0) to (x1,y1) isn't blocked by a wall
   anywhere in between. Used both for the player's own FOV and for whether
   a monster can actually see the player before it aggroes. */
bool line_of_sight(const Map *m, int x0, int y0, int x1, int y1);

/* One line per wild district kind, for the arrival log. Returns NULL for a
   bit that is not set or not a kind. */
const char *wild_kind_note(int kind);
/* Short name of a district, for the moment the player crosses into it. */
const char *wild_kind_name(int kind);

/* Monotonic count of tiles ever revealed. Cheap stand-in for "has the
   explored area changed" -- see fov_light(). */
unsigned long fov_reveal_ticks(void);

/* Records that one more tile has just been uncovered. For vision.c, which
   composites the per-body maps into the Tile flags and is therefore the thing
   that lifts the fog. */
void fov_note_reveal(void);
#define WILD_KIND_MAX DIST_KIND_COUNT

#endif