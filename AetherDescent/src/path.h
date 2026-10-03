#ifndef PATH_H
#define PATH_H

#include "common.h"

/* Shared grid pathfinding.
 *
 * There were three breadth-first searches in main.c driving auto-explore and
 * one greedy "step in roughly the right direction" in companions.c, and the
 * greedy one was visibly worse: a companion asked to walk around a corner
 * would press into the wall until the target moved. This is the one search
 * both of them use for "get me to a specific square".
 */

/* Both searches refuse to route *through* the up staircase.
 *
 * Landing on a staircase takes it, immediately and without asking. That is
 * right for a keypress and wrong for a delegated walk: auto-explore crossing
 * the square it arrived on sent the run a floor the wrong way. Blocking the
 * tile in the search routes around it instead, which is nearly always
 * possible -- it is one square, and it sits in a room.
 *
 * The start square is exempt (you arrive standing on it), because a search
 * only ever checks the squares it steps *into*.
 */

/* The eight-way step table the searches walk in, exposed so callers can use
   the same ordering (and so nobody writes a ninth copy of it). */
extern const int PATH_DX[8];
extern const int PATH_DY[8];

/* First step of a shortest path from (sx,sy) to (tx,ty), written to
   out_dx/out_dy as a one-square delta. Returns false if the target is
   unreachable or is where you already are -- callers must handle that rather
   than assuming a step exists.
 *
 * `avoid_hazards` routes around lava and miasma, which are passable but cost
 * HP per step. Callers generally try true first and fall back to false, so a
 * hazard can still be crossed when it is the only way through.
 *
 * `require_seen` limits the search to ground the player has actually
 * revealed. Auto-explore needs it -- it must not path through a shortcut the
 * player has no way of knowing about. Companions do not: they are meant to
 * range out on their own, and would otherwise be confined to the player's
 * footprints.
 *
 * Uses file-static scratch arrays (a 140x80 grid is too big for the stack),
 * so it is not reentrant. Every caller is inside the single-threaded turn
 * loop, and none of them nest.
 */
bool path_next_step(const Map *m, int sx, int sy, int tx, int ty,
                    bool avoid_hazards, bool require_seen,
                    int *out_dx, int *out_dy);

/* First step toward the nearest revealed, walkable tile that still borders
   unknown ground -- i.e. "walk to the edge of what you can see", which is
   what a person does with the map in front of them. Returns false when there
   is no reachable frontier left, which is the signal that the floor is
   explored as far as this searcher can get.
 *
 * Never looks past the fog: it will not path through ground nobody has
 * revealed, so it cannot cheat its way to a shortcut.
 *
 * Both auto-explore and hired companions use this, over the same shared
 * `seen` flags -- which is what makes a companion scouting ahead actually
 * useful to the player rather than a private walk. */
/* The frontier tile the search is heading for, rather than the first step
   toward it. Lets a walker commit to one destination across turns instead of
   re-deciding every tick -- re-deciding is what makes a hire orbit a junction
   between two equidistant frontiers forever. */
bool path_frontier_target(const Map *m, int sx, int sy, bool avoid_hazards,
                          int *out_x, int *out_y);

bool path_frontier_step_ex(const Map *m, int sx, int sy, bool avoid_hazards,
                           int *out_dx, int *out_dy, int *out_tx, int *out_ty);

bool path_frontier_step(const Map *m, int sx, int sy, bool avoid_hazards,
                        int *out_dx, int *out_dy);

#endif
