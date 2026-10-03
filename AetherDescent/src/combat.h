#ifndef COMBAT_H
#define COMBAT_H

#include "common.h"

/* The damage model, kept pure (no RNG, no globals, no side effects) so it
   can be exercised directly by tests/ instead of only through live play.
   `variance` is the caller's own random jitter -- combat passes
   rand() % 4 - 1; a test passes 0 for the deterministic mid-point.

   Defence gives diminishing returns rather than flat subtraction:
       dmg = atk^2 / (atk + def)
   Straight `atk - def` looks reasonable early and then collapses, because
   armour outscales monster attack with depth -- past mid-game every hit
   floored at zero and the only damage left in the game was the 5%
   unavoidable crit. With this curve armour is always worth stacking (def
   == atk halves the hit, def == 3*atk quarters it) but can never zero a
   hit out, so no amount of gear turns a fight into a formality. */
int damage_after_defence(int attack, int defence, int variance);

/* Player strikes an adjacent monster. Handles death, XP and gold reward,
   and a brief hit-flash on both combatants. Sets *out_boss_killed to true
   if the killed monster was the floor-100 boss. */
/* One swing, for any of the six actors. Takes the body doing the hitting;
   nothing in it asks which one that is except the log line. */
/* One damage roll for any actor: weapon, set bonuses, stance, buffs, crit and
   the ring/trinket terms that ride in through effective_stat. Both the swing
   below and the AI's own strike go through it, so an accessory is worth the
   same on any of the six. */
/* `m` is only read for the square the attacker is standing on -- the
   proving ground's unarmed skin. Pass NULL where there is no floor. */
int hero_damage_roll(Player *p, const Hero *h, const Map *m, const Monster *mo, bool *out_crit);

/* The roll plus whatever a completed gear set did about it. This is what an
   attack should call: the proc then fires for a spell, a shot and a swing
   alike, and for all six actors, because the roll underneath is shared. */
int hero_damage_with_procs(Player *p, Hero *h, const Map *m, Monster *mo, bool *out_crit);

/* One swing, for any of the six actors. Takes the body doing the hitting;
   nothing in it asks which one that is except the log line. */
void hero_attack_monster(Player *p, Hero *h, Map *m, Monster *mo, bool *out_boss_killed);

/* Give a level back: its hit points, its attack, its share of defence, and
   any progress toward the next one. Refuses at level 1 and only there.
   The exact inverse of the level-up inside grant_xp(). */
bool player_sell_level(Player *p);

/* Swarm and Hardcore bank 1/Nth of the experience Normal and Hard do for the
   identical kill; N is 10 and it is the only place difficulty touches
   experience at all (see grant_xp). Exposed so it can be *swept* rather than
   argued about: it is the constant the measurements keep arriving at, and a
   single number that decides whether two of the four modes are finishable
   should be answered with a curve. 1 removes the penalty entirely. */
void xp_penalty_set_divisor(int n);
int  xp_penalty_divisor(void);

/* Applies dmg to mo and handles death (kills/gold/xp/quest/boss-flag) the
   same way a melee hit does. Shared by melee attacks and spell damage;
   callers are responsible for their own hit/miss and damage-amount logic
   and any attack-flavored log message before calling this. Killing an
   elite or boss also rolls a small extra chance at a dungeon gear-set
   piece, on top of whatever the floor itself might drop -- needs the Map
   for the current biome. */
/* `killer` is the actor whose blow it is: the kill tally and the experience go
   to them, while the gold, the bounty progress, the larder and the boss flag
   go to the party whoever swung. NULL for damage with no author. */
void monster_take_damage(Player *p, Hero *killer, Map *m, Monster *mo, int dmg, bool *out_boss_killed);

/* Nearest living monster within `radius` tiles that the player has direct
   line of sight to, or NULL if none. Shared targeting logic for any
   non-adjacent attack -- spells and ranged weapons alike. */
Monster *find_nearest_target(Map *m, const Player *p, int radius);

/* Same as find_nearest_target, but ignores `exclude` -- used for
   pierce-style attacks (e.g. a laser) to find the next target beyond the
   first one hit. */
Monster *find_nearest_target_excluding(Map *m, const Player *p, int radius, const Monster *exclude);

/* Damages every visible, line-of-sight monster within `radius` of the
   player. Returns how many were hit. Shared by nova-style spells and
   grenade-style ranged weapons. */
int damage_all_in_reach(Map *m, Player *p, int radius, int dmg, bool *out_boss_killed);

/* A single monster's turn: attack if adjacent to the player, otherwise
   chase or wander. */
void monster_take_turn(Map *m, Monster *mo, Player *p);

/* Runs monster_take_turn for every living monster on the map. If the player
   dies mid-turn, *out_player_died is set true and processing stops. */
void process_monster_turns(Map *m, Player *p, bool *out_player_died);

/* Decrements buff timers, clearing bonuses that have expired. Call once per
   player turn. */
void hero_tick_buffs(Player *p, Hero *h);

/* Heals whatever a worn regeneration accessory provides. No-op without one --
   the game has no innate regeneration, which is exactly why the accessory is
   worth what it costs. Suppressed while poisoned, so a slow drip can't
   silently no-op a damage-over-time effect. Call once per player turn. */
void hero_tick_regen(Player *p, Hero *h);

/* Applies poison damage-over-time and counts it down. Call once per player
   turn, and check p->hp afterward -- poison can kill. */
void hero_tick_status(Player *p, Hero *h);

/* True (and logs a message, and consumes one turn of the stun) if the
   player is currently stunned and their move/attack should be suppressed. */
bool consume_stun_if_active(Player *p);

/* Experience to the body that earned it; NULL means whoever is being played. */
void grant_xp(Player *p, Hero *h, int xp);

/* Every coin the player earns goes through here. Applies gold-find, the
   altar's boon and the bank's multiplier in that order, clamps the purse
   against overflow, and returns what was actually credited -- callers log
   the return value, never the amount they asked for.
 *
   It was four inconsistent copies before: the altar's doubling reached
   floor gold and scrap but not a single kill, and the companions' pickups
   applied nothing at all. */
int player_gain_gold(Player *p, int amount);

/* Credits coin with no multipliers at all -- for returns on a stake the
   player already owned, where multiplying the gross return would multiply
   money that was never income. See the comment on the definition. */
int player_credit_gold(Player *p, long amount);

/* Crystal district: a cast or a shot has a chance to come off a crystal and
   find the caster instead. Returns true if it did -- the caller has already
   spent the aether or the ammunition by then, which is the price. Melee is
   deliberately exempt: this is the one place where the sword is the right
   tool and the spellbook is not. */
bool crystal_ricochet(Map *m, Player *p, int magnitude, const char *what);

/* Every turn, wherever the player is: the storm-cage picks a rod, warns, and
   strikes it. Safe to call on floors with no storm district. */
void districts_tick(Map *m, Player *p);

/* What the proving ground's rule is called, for the arrival note. */
const char *proving_rule_name(int state);

/* Move something without asking it: a current, and later a conveyor, a flood
   surge, an ice slide. Walks up to `dist` tiles along (dx,dy) and stops at
   the first tile it must not enter. Returns how far it actually went.
 *
   The guards are the whole value of having this in one place -- every future
   consumer inherits them instead of rediscovering them.
 *
   Takes a mutable Map only because monster_at() does; nothing here writes to
   it. Casting the const away instead was the tidier-looking lie. */
/* `avoid` is the party whose bodies must not be landed on, and `self` is the
   one body exempt from that (the one being moved, when it is a body). Pass
   NULL for both when moving a monster with no party in scope.

   It took every *body* into account only for the one the human was driving,
   which meant a belt could shove a monster onto a hire and put two actors on
   one square -- the single invariant the combat system assumes throughout. */
int displace_actor(Map *m, int *x, int *y, int dx, int dy, int dist,
                   const Monster *ignore, const Player *avoid, const Hero *self);

#endif
