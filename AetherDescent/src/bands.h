#ifndef BANDS_H
#define BANDS_H

#include "common.h"

/* ---- what the place does for you ---------------------------------------
 *
 * Five bands, five gifts. The ROADMAP's proposal, in the project owner's
 * words: each band gets an environment that helps the player in its own way,
 * and the first band's job is to *present the world and make the player want
 * to continue* -- not to filter them.
 *
 * The distinction that makes these worth building is that a band already has
 * terrain which does things **to** you. What no band had was anything the
 * environment does **for** you. These are that.
 *
 * ---- three decisions, made once and written here ----
 *
 * **Invisible.** Decided by the project owner. Nothing is announced, nothing
 * appears on any screen, no number moves that the player can read. The place
 * simply behaves a little better when a run is going badly. dda.h argues the
 * case and it is the right one -- "DDA that swings hard reads as the game
 * cheating, and players are right to resent it" -- so the shape is small,
 * forgiving and asymmetric.
 *
 * **Asymmetric.** A gift appears when pressure is *negative* and never becomes
 * a penalty when it is positive. A player who is coasting gets the game as
 * written; a player who is drowning gets a floor that is slightly kinder. It
 * can only ever add.
 *
 * **One knob.** BAND_MAX_HELP is the only constant, and each band expresses it
 * in its own units. That is deliberate and it is EVALUATION 73's rule: a bonus
 * chosen per band, per difficulty and per stat is five numbers somebody picked
 * and a finding they authored. One number can be swept and reported as a
 * curve. Sweep it before trusting any value of it -- it has not been.
 *
 * ---- what each band gives ----
 *
 *   Roots      growth that feeds you        a little regeneration
 *   Works      machinery still under power  aether and ammunition return faster
 *   Ruins      wards that still hold        blows turned aside more often
 *   Wastes     salt that preserves          supplies sometimes not spent
 *   Abyss      dark that hides as it blinds monsters notice you later
 *
 * Every one is a property of the *place*, readable as flavour rather than as
 * charity, which is what lets them stay invisible without feeling arbitrary.
 */

/* The one knob. How far a band will go for a player who is drowning, in
   "steps" -- each band scales its own effect off this. Zero disables the whole
   system, which is how it should be measured against. */
#define BAND_MAX_HELP 4

/* How much help the floor underfoot is willing to give, 0..BAND_MAX_HELP.
   Zero unless pressure is negative; grows as it falls toward DDA_MIN.
 *
   Takes no Map. mapgen sets m->biome from get_biome_for_floor(floor_num) and
   nothing else ever writes it, so the band is a function of p->floor -- which
   means these can be asked from anywhere, including the places that spend a
   draught and never had a Map to hand. */
int band_help(const Player *p);

/* Sunken Jungle Roots: extra hit points a turn, on top of whatever gear gives.
   The band the run is decided in (EVALUATION 86: 46 of 100 Normal deaths fall
   on floors 1-4), and the one whose job is explicitly not to filter. */
int band_regen_bonus(const Player *p);

/* Company Works: extra aether, and extra ammunition, per regeneration tick. */
int band_recharge_bonus(const Player *p);

/* Flooded Ruins: percentage points of ward, added to the body's own. */
int band_ward_bonus(const Player *p);

/* Salt Wastes: true if this use of a consumable should not spend it. */
bool band_supply_saved(const Player *p);

/* The Abyss: tiles taken off a monster's aggro radius. */
int band_aggro_reduction(const Player *p);

/* ---- what the place always does ----------------------------------------
 *
 * The gifts above are conditional and invisible. These are the other half, and
 * they are neither: a rule each band always has, the same for everybody, which
 * the player is meant to learn and play around.
 *
 * ROADMAP Phase 3 asked for exactly this -- "biomes still differ only in
 * monster roster and hazard mix mechanically; give each a rule" -- and named
 * three of the five. They are the cheapest way to make a hundred floors feel
 * less uniform, because a rule is a sentence and a district is a system.
 *
 * Each is the same idea as its band's gift, pointed the other way, which is
 * what keeps a band coherent rather than a bag of effects:
 *
 *   Roots    growth hides    -- you and the things hunting you, both
 *   Works    machinery gates -- more sealed doors, more levers to find
 *   Ruins    wards persist   -- some monsters carry one
 *   Wastes   salt corrodes   -- the ground hurts more than it looks
 *   Abyss    the dark blinds -- sight is shorter, and Aether-Sense matters
 */

/* Roots: tiles off *everyone's* notice, yours and theirs. The growth does not
   care who is hiding in it, which is what makes it a rule rather than a gift. */
int biome_growth_cover(const Player *p);

/* Ruins: percentage of a monster's blows that its own ward turns aside. The
   band where raw damage stops being the whole answer. */
int biome_monster_ward_pct(const Player *p);

/* Wastes: extra damage the ground does, on top of the hazard's own. */
int biome_hazard_extra(const Player *p);

/* Abyss: tiles taken off the driven body's sight radius. Never below
   BIOME_ABYSS_SIGHT_FLOOR -- a rule that makes the map unreadable at 1400x800
   is a bug, not a difficulty (ROADMAP 3.1 rule 2). Aether-Sense halves it,
   which is the point of the rule: it gives the attribute something to do. */
#define BIOME_ABYSS_SIGHT_FLOOR 4
int biome_sight_penalty(const Player *p);

#endif
