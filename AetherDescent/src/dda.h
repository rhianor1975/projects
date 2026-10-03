#ifndef DDA_H
#define DDA_H

#include "common.h"

/* Dynamic difficulty adjustment -- the measurement half.
 *
 * Escalation (monsters.c) is not DDA: it is a static formula on player level
 * and floors entered, so it responds to *what you are* rather than *how you
 * are doing*. That is why it fights grinding -- a level gained raises
 * difficulty permanently whether you are steamrolling or barely alive.
 *
 * This reads performance instead. A floor cleared comfortably pushes pressure
 * up; one that mauled you or drove you out pushes it down, harder and faster
 * than cruising pushes it up. Quick to forgive, slow to punish: DDA that
 * swings hard reads as the game cheating, and players are right to resent it.
 *
 * NOTHING CONSUMES THIS YET, deliberately. Phase one measures what the curve
 * would have been across real simulated runs, so the tuning is done against
 * numbers rather than guesses -- the same way winnability was settled.
 */

#define DDA_MAX          40   /* pressure is bounded; a runaway dial is a bug */
#define DDA_MIN         (-40)

#define DDA_CRUISED       4   /* barely scratched */
#define DDA_STEADY        1   /* took some, gave more */
#define DDA_HURT        (-8)  /* needed the potions */
#define DDA_ROUTED     (-14)  /* nearly died, or left the way you came */

/* Snapshot the state a floor is entered in. Called once per floor arrival. */
void dda_floor_begin(Player *p);

/* Read how that floor went and move the pressure. Called when the floor is
   left, before the next one is entered. */
void dda_floor_end(Player *p, const Map *m);

/* Current pressure, DDA_MIN..DDA_MAX. Positive means "this run is coasting".
   The settled history of finished floors *plus* how the floor underfoot is
   going -- see dda_floor_so_far. */
int dda_pressure(const Player *p);

/* How the floor being stood on is going, without waiting for it to be left.
 *
   dda_floor_end() only runs when a floor is left, so a run that dies on floor
   1 -- the median run in all three hard modes, EVALUATION 86 -- moved pressure
   not at all, and anything riding on pressure would have helped only the
   players already doing fine. This is the intra-floor trigger that fixes it.

   Only ever negative: a floor going badly says so at once, a floor going well
   waits to be finished. Quick to forgive, slow to punish. */
int dda_floor_so_far(const Player *p);

/* The settled half alone: floors actually finished, with the one in progress
   left out. For reporting. */
int dda_settled(const Player *p);

/* One word for a log line or a report column. */
const char *dda_label(const Player *p);

/* Signals the player's own actions feed in. Kept as calls rather than raw
   field pokes so every producer is greppable. */
void dda_note_heal(Player *p);
void dda_note_retreat(Player *p);

/* ---- the guardian angel: a cheat code ----------------------------------
 *
 * A thumb on the scale: points of crit, points of evasion, and a monster
 * critical that lands less often. Nothing is added to the character, nothing
 * is shown on any screen, and nothing is granted -- the dice simply lean.
 *
 * **It is a cheat, not a difficulty.** Off unless the player asks for it
 * (`--guardian`), and orthogonal to the four modes: Hardcore with the angel
 * is still Hardcore, with its permadeath, its 10x experience penalty and its
 * refusal to let you recall. A difficulty setting is a claim about how the
 * run is meant to go; a cheat is something the player turned on, and folding
 * one into the other would make every difficulty number in EVALUATION.md a
 * statement about an assist nobody chose.
 *
 * Being off by default is also what keeps §83, §85 and §86 reproducible --
 * the harness has to be told to enable it, so no published number moves
 * underneath itself.
 *
 * Read from *current* hit points rather than from `dda_pressure`, which is
 * the important design decision here and not an obvious one. Pressure only
 * moves in dda_floor_end(), which runs when a floor is *left*; a run that
 * dies on floor 1 never leaves it, so pressure never updates and an angel
 * riding on it would be blind to exactly the runs that need it -- which,
 * measured, is the median run in all three hard modes (EVALUATION §86).
 * Hit points are known every turn, on every floor, including the first.
 *
 * It has two halves, and dda.c explains why the first version had only the
 * second and measured as doing nothing at all:
 *
 *   THE ESCORT  large, unconditional, strongest on floor 1, gone by the
 *               middle of the descent. The opening is meant to be a cakewalk
 *               on purpose -- it is where the world gets shown to you.
 *   THE NET     small, only when nearly dead, active at every depth. Past the
 *               middle it is all there is.
 *
 * What it will not do, at any strength: add hit points, grant an item, or
 * make the monster's unavoidable critical avoidable. It moves dice.
 */

/* Extra percentage points of crit and evasion. 0 when the player is
   comfortable. */
int guardian_grace(const Player *p);

/* The monster's unavoidable critical, softened. Separate from guardian_grace
   because it must never become *avoidable*: that term exists to keep the
   temple able to hurt however overbuilt a character gets, and grace at full
   strength is several times the base chance, so subtracting it directly would
   have deleted the mechanic outright for anyone under a third health rather
   than making it rarer. Halves it at most, and never returns 0 for a
   non-zero base. */
int guardian_softened_crit(int base_pct, const Player *p);

/* The dungeon master's screen. Call wherever the player's death is detected:
   if this returns true it has converted the killing blow into one hit point
   and the caller must NOT treat the player as dead.

   Once per floor, only above the fade line, never in town. It converts a
   killing blow; it does not resurrect, and it does not heal. See dda.c for
   why this replaced most of the flat bonus -- a bonus big enough to stop the
   deaths also stopped the fights being close, and the near miss is the entire
   point. */
bool guardian_catch(Player *p);

/* The follow-through, called by whoever handled a successful guardian_catch
   and has a Map to work with. The catch alone buys one hit point, which buys
   exactly one blow -- measured, it saved *nothing* on Hardcore, 60 deaths in
   floors 1-9 out of 60 runs with a free save on every floor, because the
   player came round at 1 HP in the same room with the same monsters adjacent
   and on their turn.

   A dungeon master does not say "you are on one hit point" and move on. They
   say the thing that gives the player a turn to use. This stuns what is
   standing over you, so the moment buys space instead of a blow. */
void guardian_follow_through(Player *p, Map *m);

/* Rearms the once-per-floor save. Called by dda_floor_begin. */
void guardian_floor_reset(void);

/* ---- the avenging angel: the same dungeon master, facing the other way ---
 *
 * A master who only ever saves you is not running a game, they are running a
 * cutscene. The other half of the job is noticing that the table is bored and
 * putting something worse in the corridor.
 *
 * Extra escalation percentage points, on top of monsters_escalation_for(),
 * when the run has been coasting. Zero when the player is even or struggling,
 * so it can never pile onto somebody already losing.
 *
 * **This half reads dda_pressure, and the guardian half deliberately does
 * not.** That asymmetry is the whole design, not an inconsistency:
 *
 *   - Dying is an *instant*. It happens inside a floor, most often the first,
 *     and pressure only updates when a floor is *left* -- so a guardian
 *     riding on pressure would be blind to the deaths it exists to prevent
 *     (EVALUATION §86). It reads hit points, which are true every turn.
 *   - Coasting is a *history*. One easy room proves nothing; four floors
 *     cleared at full health is a fact about the run. Pressure is exactly
 *     that measurement, and a player who is coasting is by definition
 *     finishing floors, so the update always arrives.
 *
 * Same switch as the guardian (`--guardian`), because they are one system:
 * turning on the dungeon master gets you both the screen and the thing in
 * the corridor. Turning on only the half that helps you is not a dungeon
 * master, it is a difficulty setting with extra steps.
 */
int avenger_escalation(const Player *p);

/* 0 is off and the default; 100 is full strength. Set by `--guardian [N]` in
   the game and by argv[11] in the simulator, so the effect is always a
   difference between two runs rather than a number asserted about one. */
void guardian_set_strength(int pct);
int  guardian_strength(void);

#endif
