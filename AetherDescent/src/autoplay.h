/* Silent self-play: run the real game with nobody at the keyboard.
 *
 *   ./bin/aether --silentrun [--log PATH] [--keys N] [--seed N]
 *
 * The simulator in tools/ answers "is this balanced". It cannot answer "does
 * this break", because it drives combat.c and mapgen.c directly and never
 * opens a shop, a spell list, an inventory or a save prompt. Every menu in
 * the game is invisible to it.
 *
 * This runs the actual main.c loop instead. Input is supplied by a policy
 * rather than a person, output goes to /dev/null, and everything that
 * happens is appended to a log for reading afterwards. What it is good at is
 * finding the crash that happens when you open the black market at level 1
 * with an empty purse -- the kind of thing that only exists in the UI, and
 * that a balance harness structurally cannot reach.
 *
 * The policy was a pure biased fuzzer, on the argument that a policy which
 * understood which screen it was on would only ever visit the screens I
 * remembered to teach it about -- exactly the set I would have tested by
 * hand anyway -- while random keys with an escape bias go everywhere,
 * including the places I would not have thought to look.
 *
 * That argument was right about menus and wrong about the map. Sixty
 * thousand keys reached deepest floor **1**, every snapshot reading
 * `floor 0, turn 0`: entering the dungeon means walking onto one specific
 * tile in a 140x80 town, and a random walk essentially never lands on it.
 * The fuzzer was not choosing not to go down. It could not get there. So the
 * whole of the game below the plaza -- mapgen, monsters, combat, descent,
 * death, the floor-choice prompt, every deep screen -- was unreachable, and
 * the log said so only because the snapshots were there to say it.
 *
 * So the policy is now two policies, split along the line the evidence drew.
 * *Navigation* is deliberate: it knows whether it is standing in the town or
 * in the temple, and it paths -- with the same `path_next_step` the simulator
 * and the hired companions use -- to a goal it has chosen. *Everything else*
 * is still the fuzzer, unchanged. The split is enforced structurally rather
 * than by discipline: main.c arms a scene immediately before its own getch(),
 * and the first key served consumes it, so every nested screen -- shops,
 * inventory, spell lists, prompts, anything anyone adds later -- falls back
 * to random keys without having to be told to.
 *
 * Navigation also does not take every key. Roughly one in five is still
 * drawn from the pool even on the map, so walking to the stairs does not
 * stop the run from firing spells, opening the pack or wandering into a
 * shrine on the way.
 */
#ifndef AETHER_AUTOPLAY_H
#define AETHER_AUTOPLAY_H

#include <stdbool.h>
/* For Player and Map, which the navigation half of the policy reads. Both are
   anonymous typedefs, so there is no forward declaration to use instead.
   common.h does not include this header, so there is no cycle. */
#include "common.h"

/* Start silent self-play. Returns false if the log could not be opened, in
   which case nothing is enabled and the caller should bail out rather than
   quietly run a normal game. */
bool autoplay_begin(const char *log_path, unsigned int seed, long max_keys);

bool autoplay_active(void);

/* Which map the game is about to ask for a key on.
 *
 * Armed by main.c immediately before its own getch(), and consumed by the
 * first key served afterwards -- so a screen opened *by* that key (a shop, a
 * prompt, the inventory) is back to AP_SCENE_MENU and gets the fuzzer. That
 * is the whole enforcement mechanism: navigation cannot leak into a screen it
 * knows nothing about, and a screen added tomorrow is covered without anybody
 * remembering to cover it.
 *
 * AP_SCENE_DESCEND is the one prompt navigation does answer, because it is
 * not really a menu: it is the question "which floor", and leaving it to the
 * fuzzer means escaping out of it and restarting at floor 1 forever.
 */
typedef enum {
    AP_SCENE_MENU = 0,   /* anything else: shops, lists, confirmations */
    AP_SCENE_TOWN,
    AP_SCENE_TEMPLE,
    AP_SCENE_DESCEND,    /* the temple entrance's floor-choice prompt */
} AutoScene;

void autoplay_scene(AutoScene scene, const Player *p, const Map *m);

/* The key the policy would press next. Called by aether_getch(). */
int autoplay_key(void);

/* The answer to a *non-blocking* poll -- "has the player reached for the
   keyboard?" -- which is a different question from "give me a key", and the
   honest answer from a policy that is not a person is nearly always no.
 *
 * Auto-explore asks it once per step, to let a watching player call the walk
 * off. Routed through autoplay_key() it got a keypress every single time, so
 * auto-explore stopped on its first tick of every invocation it has ever had
 * under this mode -- and auto-explore is the one thing in the game that can
 * cross a floor and take the stairs on its own. It was the most heavily
 * weighted key in the pool and it had never once run.
 *
 * Returns ERR almost always, and occasionally a key, because "the player
 * interrupts a delegated walk" is itself a code path worth walking into.
 */
int autoplay_peek(void);

/* Append a line to the run log. Used by log_msg so that everything the game
   tells the player is in the record too, not just the keystrokes. */
void autoplay_note(const char *fmt, ...);

/* Periodic state snapshot. A log of keystrokes alone cannot answer the first
   question anyone asks of a run -- how far did it get -- which is why the
   first long run had to have its depth inferred from which shop refusals
   appeared in it. Called from the draw path, so it always has real state. */
void autoplay_snapshot(int floor, int level, int hp, int maxhp, long gold,
                       int x, int y, int turns);

/* Close the log and write the summary footer. Safe to call when inactive. */
void autoplay_end(const char *why);

/* Total keys served so far -- main.c uses it to stop a run that will not
   end on its own. */
long autoplay_keys_served(void);

/* Is this tile type on the town tour -- the list of doors self-play walks into
   so the fuzzer gets inside every shop screen? Exposed for the test that
   asserts the list against the town map, because a door added to the plaza and
   not to the list is a screen that silently stops being exercised. */
bool autoplay_tours_tile(int t);

#endif
