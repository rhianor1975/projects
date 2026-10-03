#ifndef RANGED_H
#define RANGED_H

#include "common.h"

const char *ranged_type_name(int type);

/* Signature color pair and travel glyph for a ranged weapon type, used by
   fire_ranged's animation calls -- a bow arrow reads differently on screen
   than a gunshot or a laser beam. */
int ranged_color_pair(int type);
chtype ranged_glyph(int type);

/* Ammo regenerates one point every this many turns.
   It used to be one *per* turn, which made the AM bar decorative: every
   weapon in the shop has a cooldown of 1-5 turns and costs 1-3 ammo, so the
   pool refilled at least as fast as the cooldown let you spend it and the bar
   could never fall. A player with a bow could fire for an entire run and
   never see it move. At one per four turns, sustained fire drains -- and a
   walk between fights fills it back up.

   tests/invariants.c pins the rule this has to satisfy: for every weapon
   sold, firing as fast as its cooldown allows must be net-negative. */
#define RANGED_AMMO_REGEN_TURNS 4

/* Decrements the equipped weapon's cooldown and regenerates ammo. Call
   once per player turn, alongside tick_spells. */
void tick_ranged(Player *p, Hero *h);

/* Equips a ranged weapon, replacing whatever was equipped before. */
void equip_ranged(Hero *h, const char *name, int type, int bonus, int ammo_cost, int cooldown);

/* Fires the equipped ranged weapon at whatever's in range. Logs the reason
   and returns false (no turn consumed) if it can't fire right now. */
bool fire_ranged(Player *p, Hero *h, Map *m, bool *out_boss_killed);

/* How far the equipped weapon can actually hit, in tiles (0 with none
   equipped). A grenade's blast radius is smaller than the range it searches,
   so this reports what it will really reach. */
int ranged_reach(const Hero *h);

/* True if the equipped weapon could fire this turn -- something equipped,
   off cooldown, enough ammo. Says nothing about whether anything is in
   range. Exists so a caller deciding between attacks can ask first rather
   than calling fire_ranged and reading the refusal out of the message log,
   which is what auto-explore would otherwise fill the log with. */
bool ranged_ready(const Hero *h);

#endif
