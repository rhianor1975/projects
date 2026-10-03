#ifndef AUTOEXPLORE_H
#define AUTOEXPLORE_H

#include "common.h"

/* ---- what the delegated walk decides -----------------------------------
 *
 * Auto-explore is two things bolted together: a set of *decisions* -- where to
 * step, what to spend on a target, whether anything is happening at all -- and
 * a *loop* that carries them out, draws a frame and reads the keyboard.
 *
 * Only the decisions are here. The loop stays in main.c, because it needs the
 * turn resolution, the renderer and the input queue, and dragging those into a
 * module to satisfy a header would be moving the tangle rather than undoing
 * it.
 *
 * The split is for one reason: the test suite links every module except
 * main.o, so anything living in main.c cannot be tested at all. This session
 * spent most of its time on defects that only playing could find, and two of
 * them were auto-explore's -- a stuck-detector watching the wrong body, and a
 * step refused for standing next to your own party. Both were decisions. Both
 * are now assertable.
 */

/* Can a delegated walk step here? `avoid_hazards` is the first pass: the
   searches run twice, preferring a route that does not cross lava or miasma
   and accepting one that does only when there is no other way. */
bool autoexplore_passable(const Map *m, int x, int y, bool avoid_hazards);

/* A signature of "is anything happening". The walk stops when this stops
   changing, so what it includes is what counts as progress: the driven body's
   position, health and kills, the floor, every living monster's health, and
   the number of tiles uncovered.
 *
   It reads the *driven* body. Reading slot 0 while the walk moved someone else
   is what made auto-explore report itself stuck the moment the character's AI
   settled down -- see EVALUATION §92. */
unsigned long autoexplore_progress(const Player *p, const Map *m);

/* How far the hunter will go out of its way for a fight. Bounded on purpose:
   "hunt what's near you" and "clear the floor" are different requests. Also
   the radius the caller searches for a target with, so the two cannot
   disagree about what counts as near. */
#define AUTOEXPLORE_HUNT_RADIUS 10

/* First step of a route to a known square, over ground the player has actually
   revealed, preferring a hazard-free way and accepting a hazardous one only
   when there is none. Exposed because closing on a target is the one decision
   the caller has to make with coordinates it copied out itself. */
bool autoexplore_step_toward(const Map *m, int sx, int sy, int tx, int ty,
                             int *out_dx, int *out_dy);

/* What the hunter should do about a target, decided without doing any of it. */
typedef enum {
    AX_NOTHING,   /* nothing worth acting on */
    AX_MELEE,     /* adjacent: step into it */
    AX_ABILITY,   /* spend the class ability */
    AX_SPELL,     /* cast `slot` */
    AX_SHOOT,     /* fire the ranged weapon */
    AX_CLOSE      /* out of reach: walk at it */
} AxKind;

typedef struct {
    AxKind kind;
    int slot;     /* AX_SPELL only */
} AxFight;

/* The order is the decision, and it is deliberate: spend the thing that is
   hardest to replace first, while the target is still worth it.
 *
   The ability goes above the spell and the shot because it returns on a
   cooldown and a boss does not return at all -- and it is held entirely for
   elites and bosses, because an ability spent on a rat is worse than an
   ability unspent (ROADMAP 2.1b). */
AxFight autoexplore_decide_fight(const Player *p, const Map *m,
                                 const Monster *target, int dist);

/* Standing on ground that carries you -- an aqueduct channel or an assembly
   belt -- which way is off it? Writes a one-square delta and returns true;
   false when the tile underfoot does not move you, or when nothing adjacent
   is any better.

   This is the first thing a delegated walk asks, ahead of loot, the stairs
   and the frontier, because while you are being carried none of those
   decisions survive contact with the floor. is_walkable_delegated() keeps a
   walk from *routing onto* conveyed ground; this is what gets it off ground
   it is already on, which is the other half and was missing. */
bool autoexplore_escape_conveyance(const Map *m, int x, int y,
                                   int *out_dx, int *out_dy);

/* Where to step when there is nothing to fight: loot first, then the stairs if
   they are known, then the nearest frontier. Returns false when none of them
   has an answer, which is the walk's one honest way to finish.
 *
   `out_stairs_known` reports which of the two "no route" messages the caller
   should print, since that is the only thing distinguishing them. */
bool autoexplore_route(const Player *p, const Map *m,
                       int *out_dx, int *out_dy, bool *out_stairs_known);

#endif
