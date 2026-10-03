#ifndef RECORD_H
#define RECORD_H

#include "common.h"

/* ---- the tape -----------------------------------------------------------
 *
 * A per-tick dump of everything a bug report needs and a screenshot cannot
 * carry: where all six bodies were, what the map underneath them said, which
 * squares the game believed were seen and which were lit, and what it wrote in
 * the log -- for every single turn.
 *
 * Asked for in exactly those terms: "so when i play it exports the game...
 * player coordenates, tileset, log... per tick... it will be massive but if i
 * play a game and found problem i can point to this log".
 *
 * That is the right shape for the two open reports, and neither is reachable
 * any other way. "Artefacts on undiscovered map" is a disagreement between the
 * terrain a cell draws and the `seen` flag that should have stopped it, which
 * lives for one frame and cannot be caught by hand. "@ starts to move crazy"
 * is a sequence of positions against a sequence of keys, which is a thing you
 * read afterwards, not something you can watch.
 *
 * ---- format ----
 *
 * JSON Lines: one self-contained object per line, so the file is greppable,
 * tailable, and survives being cut off mid-run when the game is killed. A
 * single JSON array would be none of those things.
 *
 *   {"t":"floor",...}   once on arrival, carrying the whole map
 *   {"t":"tick",...}    once per key, carrying the viewport and the deltas
 *
 * ---- size ----
 *
 * About 3 KB a tick at a normal viewport, so a long session is hundreds of
 * megabytes. That was accepted when it was asked for, and it is the reason the
 * map itself is written once per floor rather than once per tick: the full
 * grid is 1400x800, and writing it every turn would be a megabyte a keypress.
 * What changes every turn is the viewport, and that is what a tick carries.
 */

/* Start recording to `path`. Anything already there is truncated. A NULL or
   empty path leaves recording off, which is the normal case. */
void record_open(const char *path);
bool record_active(void);
void record_close(void);

/* The whole map, once, on arrival. */
void record_floor(const Map *m, const Player *p);

/* One turn: party, viewport, monsters in view, and whatever was logged since
   the last call. `key` is the keystroke that produced the turn. */
void record_tick(const Map *m, const Player *p, int key);

#endif
