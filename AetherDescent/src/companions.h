#ifndef COMPANIONS_H
#define COMPANIONS_H

#include "common.h"

/* Heroes hired at the Tavern.
 *
 * They are deliberately not a pet system. The player never equips them,
 * never orders them, never picks their spells. They walk the floor on their
 * own, fight what they run into, and level off their own kills -- closer to
 * other players sharing the dungeon than to a summon.
 *
 * What the player does control is who to pay for, and each one costs ten
 * times the last (see companion_hire_cost), so a full party of five is a
 * whole run's fortune rather than a shopping trip.
 */

/* What the n-th hire costs, n counted from 0: 1k, 10k, 100k, 1M, 10M. */
long companion_hire_cost(int already_hired);

/* A dead hero can be taken on again, cheaper -- the Tavern knows what
   happened and prices accordingly. */
long companion_rehire_cost(int tier);

/* ---- the Tavern roster ----------------------------------------------
   Twenty candidates, regenerated from p->tavern_seed every time rather than
   stored, so the roster is stable across visits and saves without carrying
   twenty structs around. Fills `out` with candidate `idx`. */
void tavern_candidate(const Player *p, int idx, Hero *out);

bool tavern_is_hired(const Player *p, int idx);
bool tavern_has_fallen(const Player *p, int idx);

/* Give up the slot of anyone who fell and mark their roster seat for rehire.
   Runs on every dungeon turn and again on arrival in town -- see the note in
   companions.c for what happens when only the first of those is true. */
void companions_reap_fallen(Player *p);

/* Number of party slots currently filled. */
int companion_count(const Player *p);

/* The party will not let you die with a charm in your pack.
 *
 * Called wherever the player's death is detected, after the guardian angel has
 * declined. If a hire is still standing and there is a recall charm in the
 * pack, the nearest one crosses to the body, cracks it over you, and the
 * temple lets go of you both -- you come round on one hit point, on your way
 * home, a charm poorer.
 *
 * Returns true if it happened, in which case the caller must NOT treat the
 * player as dead. It is not free and it is not repeatable on demand: it costs
 * the charm, it needs somebody left alive to spend it, and it puts you in town
 * rather than back on your feet where you fell -- a rescue, not a second wind.
 */
bool companions_attempt_rescue(Player *p, Map *m);

/* Movement is not here any more. There is one movement function for all six
   actors -- actor_step() in main.c -- because three implementations of "an
   actor moves one square" is three sets of physics that cannot agree, and
   every bug this feature produced from play was them disagreeing. */

/* There is no separate turn for the main character. companions_take_turn()
   runs every actor the human is not attached to, slot 0 included. */

/* Takes candidate `idx` on, charging the appropriate price. Returns false
   (and charges nothing) if the party is full, the candidate is already out
   with you, or the purse is short. Logs the reason either way. */
bool companion_hire(Player *p, int idx);

/* A companion that was built rather than hired: no roster seat, no fee, and
   gone when you leave the floor. Returns false if the party is full.
 *
   Constructed by generating a well-formed roster candidate and overriding it,
   rather than filling a thirty-field struct by hand -- a zero-initialised
   Hero has shipped as a caster holding a bow once already. */
bool companion_grant_golem(Player *p, int floor_num);

/* Called when the floor changes: anything temporary does not come with you. */
void companions_dismiss_temporary(Player *p);

/* Lets candidate `idx` go. Only works on your own -- someone out with you,
   or someone buried in your service; you cannot dismiss a stranger, or the
   roster would be a free reroll button until it handed you a hero you liked.

   No refund: the Tavern's sign says coin up front and means it. What you do
   get is a clean line -- that slot is filled by a different hero, and the
   party place (if they were holding one) is free. Irreversible: the one you
   let go is gone, not shelved. */
bool companion_release(Player *p, int idx);

/* True if `idx` is someone you could let go -- i.e. yours, living or dead. */
bool tavern_can_release(const Player *p, int idx);

/* The live party member for roster slot `idx`, or NULL if that candidate
   isn't out with you. The Tavern list shows this rather than the template
   for anyone already hired -- a hero who has gained three levels down there
   should not still be advertised at the level you found them. */
const Hero *companion_by_roster(const Player *p, int idx);

/* ---- in the dungeon --------------------------------------------------
   Called when a floor is entered: drops the living party in around the
   player's arrival point. */
void companions_place(Player *p, Map *m);

/* How far a hired hero sees for themselves. Smaller than the player's: they
   are scouts, not a satellite. */
#define COMPANION_FOV 5

/* Lights the floor around every living companion, on top of the player's own
   field of view. Call straight after compute_fov, every time -- a companion
   is a person standing in a dungeon, and there is no fog of war around a
   person. Also the reason hiring is worth something beyond damage: whatever
   they can see, you can see. */
void companions_reveal(Player *p, Map *m);

/* One turn for every living companion: fight what is adjacent, close on what
   is near, otherwise range out and look for trouble. Runs after the player's
   turn and before the monsters'. */
/* One actor per tile: if (nx,ny) holds a party member, swap places with them
   and return true. An ally is somebody to squeeze past, not a wall -- a step
   refused for standing next to your own party spends no turn, which reads to
   auto-explore's stuck-detector as a walk that has stopped working. */
bool party_displace_into(Player *p, Hero *h, int nx, int ny);

/* Time passing for every actor, wherever they are -- town included. Runs
   hero_tick() once per slot. Separate from the AI pass below because
   everybody ticks and only five decide. */
void party_tick(Player *p, Map *m);

/* One turn's decision for every actor the human is not attached to, slot 0
   included. Underground only. */
/* Which spell slot this hire should cast right now, or -1 for none.
 *
   Pure -- it decides and does nothing -- so what a hire *chooses* can be
   asserted without building a floor and running a fight. That split exists
   because the choosing is what broke: a usefulness test with a catch-all
   `else` made STUN, SLOW and CURSE permanently "useful", and a weight driven
   by spell level made a big curse outrank a small bolt, so a hire would stand
   in front of something cursing it for as long as the player watched. Neither
   half of that is visible from outside a turn loop, and neither was.

   `can_reach` is the caller's answer to whether `target` is actually in range
   and in sight; companion_cast() works it out and passes it in. */
int companion_pick_spell(const Player *p, const Hero *c,
                         const Monster *target, bool can_reach);

void companions_take_turn(Player *p, Map *m);

/* True once if any of the six felled the Warden since this was last asked.
   Reading it clears it. The turn loop asks after the world has moved, because
   an actor acting under AI has no GameState to end the run with. */
bool companions_boss_felled(void);

/* Time passing for one actor: cooldowns, ammo, aether, buffs, status, regen.
   Every actor gets this and only this -- there is no second version for the
   character. */
void hero_tick(Player *p, Hero *h, Map *m);

/* A companion standing at (x,y), or NULL. Monsters use this to decide who to
   swing at -- a hired hero that monsters ignore is not a party member, it is
   scenery. */
Hero *companion_at(Player *p, int x, int y);

/* Damage landing on a body, after whatever computed it decided how much: the
   Vanguard's guard halves it, it lands, and a death hands the role on.
   Returns true if this killed them. The defence maths is not here -- it
   belongs where the number is worked out, so that every source uses the same
   model. */
bool hero_take_damage(Player *p, Hero *c, int dmg);

/* The smith's work, shared out to the party -- the best upgrade counter any
   body carries, since the smith went to that weapon whoever was holding it.
   Public because combat.c folds the defensive half into a body's defence. */
int party_atk_bonus(const Player *p);
int party_def_bonus(const Player *p);

/* There is no separate levelling for hires. Every actor goes through
   grant_xp() in combat.c, credited to whoever landed the blow. */

const char *personality_name(int personality);

/* The signature move each role brings. Companions are not commanded, so
   these fire on their own when they would actually help -- the player sees
   them in the log, not in a menu. Exposed for the Tavern list, so you know
   what you are buying before you buy it. */
/* The class a hire is, for display.
   Their stats still come from the archetype tilt on top of the player's
   yardstick, because a hire is scaled to the party rather than to their own
   sheet -- but the sheet is real now. hero_create() writes the thirty
   attributes and every derived stat onto a hire exactly as it does onto the
   character, so the class is no longer only a label on the list. */
const char *companion_class_name(const Hero *c);
const char *companion_class_tagline(const Hero *c);
const char *companion_class_weapon(const Hero *c);

/* What this hire is carrying, for the Tavern list. Returns NULL when they
   carry nothing of that kind -- most roles fight with their hands. */
/* How much this class shifts a hire's health, attack and defence, in percent
   either way, on top of the archetype tilt. Exposed so the "two Vanguards
   are not the same Vanguard" property can be asserted without a roster. */
void companion_class_variation(int class_id, int *hp_pct, int *atk_pct, int *def_pct);

/* A hire's effective ceilings: what they were taken on with, plus whatever
   the smith, apothecary and guild have done for the party since. The stored
   maxhp/aether_max stay as the base, so these track the player rather than
   being baked in at hire time. */
int companion_max_hp(const Player *p, const Hero *c);
int companion_max_aether(const Player *p, const Hero *c);

const char *companion_weapon_name(const Hero *c);
const char *companion_spell_name(const Hero *c, int slot);

const char *companion_ability_name(int archetype);
const char *companion_ability_desc(int archetype);

#endif
