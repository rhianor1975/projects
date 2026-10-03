#ifndef MONSTERS_H
#define MONSTERS_H

#include "common.h"

/* Pick a random monster template for the given floor and instantiate it,
   with stats scaled to that depth. */
Monster make_monster_for_floor(int floor_num, int x, int y);

/* The swarm modes make monsters *weaker*, not just more numerous.
 *
 * This was the intent all along and it had never been built. Swarm and
 * Hardcore generate at 10-20x density and pay a tenth of the experience --
 * but `make_monster_for_floor` does not take a difficulty and never has, so
 * every monster in every mode had identical hit points, attack and defence.
 * The player was fighting the Normal bestiary twenty times over and being
 * levelled as though they were killing trash: both halves of the punishment
 * and neither half of the compensation. It is why nothing moved Hardcore's
 * median through six versions of the guardian angel, five town-stop
 * intervals, every playstyle and the whole experience-divisor range
 * (EVALUATION §87, §88).
 *
 * A swarm monster should die in a hit or two and hurt a little; the danger is
 * being surrounded and worn down, not any one of them. Set once per floor
 * alongside monsters_set_escalation(); the two are the same idiom for the
 * same reason -- a global here beats threading difficulty through ten call
 * sites that do not otherwise care.
 */
void monsters_set_difficulty(int difficulty);

/* Extra monster attack, as a percentage, applied to everything made from now
   on. Set whenever a floor is entered, from the player's level and how many
   floors this run has entered.

   Depth alone was not enough to keep a run tense: the player's power grows
   with levels and gear faster than floor number grows monsters, and nothing
   stopped a player walking back up to a cleared floor and finding it easy.
   This makes the run itself the clock. Capped, or the bottom floors become
   arithmetic rather than a fight. */
void monsters_set_escalation(int pct);
int  monsters_escalation_for(int level, int floor_entries);

/* An extra-tough named monster, spawned on every 10th floor below 100. */
Monster make_elite_for_floor(int floor_num, int x, int y);

/* The fixed floor-100 encounter -- the only monster that ends the game
   when killed (is_boss=true). */
Monster make_boss(int x, int y);

/* True if floor_num is one of the 4 biome-capping boss floors (15/35/60/85). */
bool is_biome_boss_floor(int floor_num);

/* A named, biome-flavored capstone encounter for floor_num (must be one of
   15/35/60/85 -- see is_biome_boss_floor). Notably tougher than a regular
   elite, but is_boss stays false: killing it doesn't end the game, only
   the floor-100 Warden does that. */
Monster make_biome_boss(int floor_num, int x, int y);

/* Drops corpses out of m->monsters, keeping only the living. Without this
   the array only ever grows: every scan (monster turns, targeting, AoE,
   rendering) walks every monster ever spawned on the floor, and once the
   count reaches MAX_MONSTERS respawns silently stop and the floor stays
   clear for good.

   Call this ONLY at a point where no Monster* is being held across it --
   compaction moves elements, so any live pointer would dangle. In practice
   that means once per turn, after all monster turns have resolved; never
   from inside a combat routine. */
void compact_dead_monsters(Map *m);

/* Called once per player turn. Every so often, spawns a fresh monster
   somewhere far from the player so a floor never stays permanently clear. */
void maybe_respawn_monsters(Map *m, const Player *p);

/* How many turns of standing still it takes to double the floor's population.
   Scales with the map -- a bigger floor takes proportionally longer to cross,
   so a flat interval would mean four times the doublings for the same walk. */
int monster_doubling_interval(void);

/* Turns between respawn waves on a floor that is being camped. Scales with
   the map for the same reason the doubling interval does. */
int monster_respawn_interval(void);

/* Name of a random monster template valid for the given floor's tier, for
   quest-giving purposes. Points at static storage -- copy it out. */
const char *pick_quest_monster_name(int floor_num);

#endif
