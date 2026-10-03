#ifndef SPELLS_H
#define SPELLS_H

#include "common.h"

typedef enum {
    EFFECT_DAMAGE,       /* direct damage to the nearest visible foe */
    EFFECT_DAMAGE_NOVA,  /* damage to every visible foe within reach */
    EFFECT_SCORCH,       /* conjures a ring of miasma around the caster */
    EFFECT_BUFF_ATK,     /* temporary attack bonus */
    EFFECT_HEAL_SELF,    /* instant self-heal */
    EFFECT_WARD_SHIELD,  /* temporary defence bonus */
    EFFECT_BARRIER,      /* seals a nearby tile into a permanent wall */
    EFFECT_PURGE,        /* clears lava/miasma tiles around the caster */
    EFFECT_BLINK,        /* short random teleport */
    EFFECT_REVEAL,       /* fog-of-war reveal around the caster */
    EFFECT_BRIDGE,       /* conjures bridges over nearby water */
    EFFECT_STUN,         /* target can't act for a few turns */
    EFFECT_SLOW,         /* target has a chance to skip its turn for a while */
    EFFECT_UNBIND,       /* opens the nearest locked door without a key */
    EFFECT_LIFE_DRAIN,   /* damages the target and heals the caster */
    EFFECT_CURSE,        /* saps the target's attack for a while */
    EFFECT_DOT_BURN,     /* heavy necrotic damage plus a lingering curse */
    /* ---- the variety pass (ROADMAP Phase 3) ----
       Curation fixed each school's *identity* and left its mechanics alone, so
       a school drew on three effects and its thirty spells differed by numbers
       and archetype rather than by what they did -- Resonance drew on two.
       These are one new *shape* each, not one new number:

         CHAIN       hits more than one thing without being a radius
         PARTY_HEAL  acts on the whole party, which nothing did before
         HASTE       acts on time rather than on a body
         REFLECT     punishes being attacked instead of preventing it
         SENSE_LIFE  buys information, which only the Oracle was going to
         MASS_SLOW   control over an area rather than over a target

       Appended, never inserted: a Hero's known_spells holds pool indices and
       the pool is built from these in order. */
    EFFECT_CHAIN,
    EFFECT_PARTY_HEAL,
    EFFECT_HASTE,
    EFFECT_REFLECT,
    EFFECT_SENSE_LIFE,
    EFFECT_MASS_SLOW,
    /* Appended, never inserted -- a spell's effect is an index into this and
       the pool is authored against it. Exists so an effect can be bounds
       checked at all: without a count, nothing could say whether a table entry
       held a real effect or a typo, which is exactly the class of defect the
       pool-integrity suite is for. */
    EFFECT_COUNT
} SpellEffect;

typedef struct {
    char name[40];
    int school;   /* SchoolId */
    int level;    /* 1-5 */
    int effect;   /* SpellEffect */
    int magnitude;
    int duration;
    int charge_cost;
    int cooldown;
    int learn_price;
} SpellTemplate;

const char *school_name(int school);

/* Signature color pair and cast/impact glyph for a school, used by
   cast_spell_slot's animation calls so a Warding shield reads differently
   on screen than a Jinx curse or a Conduit bolt. */
int school_color_pair(int school);
chtype school_glyph(int school);

/* Total number of spells across the whole generated pool. */
int spell_pool_count(void);
const SpellTemplate *spell_pool_get(int idx);

/* True if pool index `idx` is learnable by p -- which now means, and only
   means, that it belongs to p's own magic_school. */
bool player_can_learn(const Hero *h, int idx);

/* The pool viewed one school at a time, which is how the guild browses it:
   a caster is never shown a spell they could not learn. spell_school_index
   maps the n-th spell of `school` to its pool index (-1 out of range), so
   callers still store and cast by pool index and nothing downstream changes.

   Already-known spells are deliberately not re-checked anywhere: a character
   who learned outside their school under the old rule keeps what they paid
   for. */
int spell_school_count(int school);
int spell_school_index(int school, int n);

/* Decrements cooldowns (by known-spell slot) and regenerates aether
   charge. Call once per player turn, alongside tick_buffs/tick_status. */
void tick_spells(Hero *h);

/* Casts a known, off-cooldown, affordable spell by slot index into
   p->known_spells. Logs the reason and returns false (no turn consumed)
   if it isn't castable right now. */
bool cast_spell_slot(Player *p, Hero *h, Map *m, int slot, bool *out_boss_killed);

/* Picks a known spell that would hurt something `target_dist` tiles away and
   is castable this turn -- off cooldown, charge affordable, offensive, and
   with the reach to get there. Returns the slot, or -1 if nothing qualifies.
   Prefers the largest magnitude that fits, so a hoarded level-5 spell gets
   used on something worth it rather than the first slot in the list.

   Pure: it decides nothing and casts nothing, it only answers "could I?".
   Auto-explore needs that separation, because cast_spell_slot reports its
   refusals to the message log and a hunter that guessed would bury the log
   in them. */
int spell_pick_attack_slot(const Hero *h, int target_dist);

#endif
