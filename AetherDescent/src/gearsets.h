#ifndef GEARSETS_H
#define GEARSETS_H

#include "common.h"

typedef struct {
    char name[48];
    int bonus;           /* weapon/armor: flat atk/def bonus. ring/trinket: accessory bonus */
    int accessory_stat;  /* AccessoryStat, only meaningful for RELIC_RING/RELIC_TRINKET */
} SetGearTemplate;

/* Short display name for a biome's gear set, e.g. "Root-Drowned" -- used
   for UI headers like "Root-Drowned set: 3/4 pieces". */
const char *set_name(int biome);

/* The one curated item for (biome, slot). Sets are hand-authored, not
   procedural -- always the same 4 pieces per biome -- so this is a direct
   table lookup, never a roll. Returns NULL for an out-of-range biome/slot. */
const SetGearTemplate *set_gear_piece(int biome, int slot);

/* Recomputes h->set_bonus_* from scratch based on which biome (if any) is
   currently equipped in each of the 4 gear slots. Call after any change to
   weapon/armor/ring/trinket: a shop purchase, a relic pickup, or a kill
   drop -- never increment/decrement these fields by hand. */
void recompute_set_bonus(Hero *h);

/* Equips set piece (biome, slot) onto that body -- same overwrite-in-
   place behaviour as buying gear in town, plus tagging the slot with its
   set and recomputing the set bonus. Logs its own pickup message. Used by
   both trigger_relic() (a relic found on the floor) and a kill-drop. */
void apply_set_gear(Hero *h, int biome, int slot);

/* Rolls a random slot from `biome`'s set and applies it. Used for the
   small extra chance an elite or boss kill drops something on top of the
   normal floor-generation relic. */
void grant_random_set_piece(Hero *h, int biome);

/* ---- what a full set actually does -------------------------------------
 *
 * Two pieces pay attack and defence; four pieces pay a flavoured stat on top.
 * Both are numbers on a sheet, and neither is a reason to hunt the last piece
 * of a set rather than wear the best four things you happen to have found.
 *
 * The proc is that reason. One per set, each doing the thing the biome does:
 *
 *   Root-Drowned   the jungle's venom      poisons what you hit
 *   Company        machinery               a blow that staggers
 *   Ward-Touched   the old wards           turns a blow aside outright
 *   Salt-Bitten    corrosion               eats a monster's armour
 *   Abyssal        the killing dark        a heavier strike out of nothing
 *
 * Deferred twice, correctly, while the damage model was in doubt -- flat
 * bonuses can be reasoned about on a sheet, a proc cannot. The model is
 * measured and regression-guarded now (EVALUATION §14), which is what makes
 * these safe to balance rather than guess at.
 *
 * SET_PROC_PCT is the one knob, for the same reason BAND_MAX_HELP is: five
 * chances chosen per set is five numbers somebody picked. Zero disables every
 * proc, which is the control.
 */
#define SET_PROC_PCT 15

/* Fires when `h` lands a blow on `mo`. Returns extra damage to add, and may
   leave a condition on the monster. Silent when no set is complete. */
int set_proc_on_hit(Player *p, Hero *h, Monster *mo, int dmg);

/* Fires when `h` is about to be hit. Returns the damage after whatever the
   set did about it -- which for Ward-Touched can be none of it. */
int set_proc_on_hurt(Player *p, Hero *h, int dmg);

#endif
