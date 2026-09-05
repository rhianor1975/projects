#include <string.h>
#include "talisman.h"

/* Objects and Followers are held as deck_proto indices, so their bonuses
 * are recomputed from what a character is carrying right now.  Taking an
 * item off somebody therefore moves its Strength and Craft with it, which
 * is what the rulebook's "the winner may take one Object" requires. */

static int carried_sum(const Player *p, int off)
{
    int i, v = 0;

    for (i = 0; i < p->nitems; i++) {
        const Card *c = &deck_proto[p->carried[i]];
        switch (off) {
        case 0: v += c->d_str;   break;
        case 1: v += c->d_craft; break;
        case 2: v += c->d_life;  break;
        default: v += c->d_fate; break;
        }
    }
    return v;
}

/* Rulebook: only one Weapon may be used in an attack -- the Warrior is
 * the exception and swings two.  Magic items that are not Weapons still
 * stack normally. */
/* The Character's own abilities, plus the services a Henchman performs on
 * their behalf.  His restrictions and his personal resistances stay his. */
int player_abil(const Player *p)
{
    unsigned a = p->abil;

    if (henchmen_on && p->hench.ct >= 0 && p->hench.lives > 0)
        a |= char_tbl[p->hench.ct].abil & HENCH_PASSES;
    return (int)a;
}

unsigned align_bit(Alignment a)
{
    return a == AL_GOOD ? NEEDS_GOOD : a == AL_EVIL ? NEEDS_EVIL : NEEDS_NEUTRAL;
}

/* "which can only be used by Good or Neutral Characters" -- a card with no
 * requirement is open to anyone.  The Druid, who "may change Alignment at
 * will", can pick up anything, because she can simply be whatever the thing
 * demands. */
int align_allows(const Player *p, int ci)
{
    unsigned need = deck_proto[ci].needs;

    if (!need || need == NEEDS_ANY) return 1;
    if (has_ab(p, AB_ANY_ALIGN))    return 1;
    return (need & align_bit(p->align)) != 0;
}

int eff_str(const Player *p)
{
    int i, v = p->base_str, best = 0, second = 0;

    /* 19:3 -- "Toads have a Strength of 1 and a Craft of 1; but the original
     * Character retains all Strength and Craft Counters."  It carries
     * nothing while it is a Toad, so nothing adds to that 1 either. */
    if (p->toad > 0) return 1;

    for (i = 0; i < p->nitems; i++) {
        const Card *c = &deck_proto[p->carried[i]];
        /* Cards: the Priest "may not use any Sword or Axe", the Monk no
         * "Sword, Helmet, Shield or Armour".  They may carry them -- and
         * the Priest may still build a Raft with the Axe -- but the steel
         * lends them nothing. */
        if (has_ab(p, AB_NO_SWORD) &&
            (!strcmp(c->name, "Sword") || !strcmp(c->name, "Axe"))) continue;
        if (!(c->kw & KW_WEAPON)) { v += c->d_str; continue; }
        if (c->d_str > best)        { second = best; best = c->d_str; }
        else if (c->d_str > second) { second = c->d_str; }
    }
    v += best;
    if (has_ab(p, AB_TWO_WEAPONS)) v += second;
    /* "While wearing the belt, you have a Strength of 12" -- a flat 12,
     * not a bonus.  For a Troll at Strength 19 the Belt is a downgrade,
     * which is exactly why the card is survivable. */
    if (p->belt) return 12;
    return v < 0 ? 0 : v;
}

/* One Armour may be used per attack, so all that matters is having one. */
int has_armour(const Player *p)
{
    int i;

    for (i = 0; i < p->nitems; i++)
        if (deck_proto[p->carried[i]].kw & KW_ARMOUR) return 1;
    return 0;
}
int eff_craft(const Player *p)
{
    int v;
    if (p->toad > 0) return 1;        /* 19:3 -- a Toad is Craft 1 */
    v = p->base_craft + carried_sum(p, 1);
    return v < 0 ? 0 : v;
}
int eff_maxlives(const Player *p) { int v = p->base_maxlives + carried_sum(p, 2); return v < 1 ? 1 : v; }
int eff_maxfate(const Player *p)  { int v = p->base_maxfate  + carried_sum(p, 3); return v < 0 ? 0 : v; }

/* Losing an item can lower a ceiling below what is currently in the pool. */
void clamp_pools(Player *p)
{
    int m = eff_maxlives(p);
    if (p->lives > m) p->lives = m;
    m = eff_maxfate(p);
    if (p->fate > m) p->fate = m;
}

int item_give(Player *p, int ci)
{
    /* "He cannot use it because he is of Evil Alignment.  He must leave it
     * face up in the Space where he Encountered it."  One gate here covers
     * every way an Object can reach a Character -- picked up, looted off a
     * body, bought, or handed over. */
    if (!align_allows(p, ci)) return 0;
    if (p->toad > 0) return 0;          /* 19:2 -- a Toad may hold nothing */
    if (p->nitems >= MAX_ITEMS) return 0;
    p->carried[p->nitems++] = ci;
    clamp_pools(p);
    return 1;
}

void item_drop(Player *p, int slot)
{
    int i;

    if (slot < 0 || slot >= p->nitems) return;
    for (i = slot; i < p->nitems - 1; i++)
        p->carried[i] = p->carried[i + 1];
    p->nitems--;
    clamp_pools(p);
}

/* Followers ride along free; only Objects count against the limit. */
int item_objects(const Player *p)
{
    int i, n = 0;

    for (i = 0; i < p->nitems; i++)
        if (deck_proto[p->carried[i]].type == C_OBJECT) n++;
    return n;
}

int item_limit(const Player *p)
{
    int i, n = BASE_CARRY, mule = 0;

    for (i = 0; i < p->nitems; i++) {
        const Card *c = &deck_proto[p->carried[i]];
        if (!strcmp(c->name, "Mule")) mule = 1;
        n += c->carry;
    }
    /* Rule 5:3 caps a Character at four Objects "unless they have a Mule",
     * and the worked example has a Wizard carrying ten on one -- so the
     * Mule is unlimited, not a +2.  Talisman the Adventure reins that in:
     * "It is suggested that Mules be restricted to carrying eight objects.
     * This introduces a balance to the new cards." */
    if (mule && n < MULE_CARRY) n = MULE_CARRY;
    return n;
}

/* Score an Object the way item_worst() does, so "better" means the same
 * thing on both sides of a trade. */
static int obj_value(const Card *c)
{
    int v = c->d_str * 3 + c->d_craft * 3 + c->d_life * 2 + c->d_fate + c->carry * 10;
    if (c->kw & KW_ARMOUR) v += 5;
    if (c->kw & KW_WEAPON) v += 2;
    /* Some Objects carry no stat at all and are looked up by name where they
     * matter -- the Water Bottle in the Desert, the Raft at the river, the
     * Axe that builds it.  Scored on stats alone they came out at nothing,
     * so the Water Bottle was traded for a Prayer Book and the Desert
     * collected the Life it was bought to save. */
    v += c->util;
    return v;
}

/* Which carried Object, if any, is worth giving up for card `ci`?  Returns
 * its slot, or -1 to leave the thing on the ground.  The margin stops two
 * Objects of equal worth being traded back and forth forever. */
int item_best_swap(const Player *p, int ci)
{
    int slot = item_worst(p);

    if (slot < 0) return -1;
    return obj_value(&deck_proto[ci]) > obj_value(&deck_proto[p->carried[slot]]) + 2
           ? slot : -1;
}

/* The one to leave behind: cheapest, and never the beast doing the carrying
 * unless there is nothing else to give up. */
int item_worst(const Player *p)
{
    int i, worst = -1, wv = 1 << 20;

    for (i = 0; i < p->nitems; i++) {
        const Card *c = &deck_proto[p->carried[i]];
        int v;
        if (c->type != C_OBJECT) continue;             /* Followers are free */
        v = obj_value(c);
        /* Armour, Helmet and Shield carry no stat bonus at all -- what they
         * are worth is the roll that turns a blow aside, one time in three.
         * Scored on stats alone they came out at zero, so they were always
         * the first thing dropped for a trinket. */
        if (c->kw & KW_ARMOUR) v += 5;
        if (c->kw & KW_WEAPON) v += 2;
        if (v < wv) { wv = v; worst = i; }
    }
    return worst;
}

/* Whichever item helps the thief most: weight the stat they fight with. */
/* The best of a Character's Objects, by the same measure item_worst() uses
 * to decide what to throw away.  This carried its own third formula until
 * now -- no armour, no weapons, no named effects, and a different weight on
 * carrying capacity -- so a thief would take a Sword off someone and leave
 * the Medi-kit sitting next to it. */
int item_best(const Player *p)
{
    return item_best_of(p, C_OBJECT);
}

/* Which of `p`'s cards of this type is worth the most.  Returns a slot. */
int item_best_of(const Player *p, int type)
{
    int i, best = -1, bv = -1;

    for (i = 0; i < p->nitems; i++) {
        const Card *c = &deck_proto[p->carried[i]];
        int v;
        if ((int)c->type != type) continue;
        v = obj_value(c);
        if (v > bv) { bv = v; best = i; }
    }
    return best;
}

/* Rulebook table -- Craft 1-2: none, 3: one, 4-5: two, 6+: three.
 * It reads effective Craft, so a Magic Object can buy you a Spell slot. */
int spell_limit(const Player *p)
{
    int c = eff_craft(p);

    int n;

    if (c <= 2)      n = 0;
    else if (c == 3) n = 1;
    else if (c <= 5) n = 2;
    else             n = 3;
    /* The Wizard and the Prophetess "always have at least one Spell", so
     * their limit can never fall below one however low their Craft goes. */
    if (has_ab(p, AB_ALWAYS_SPELL) && n < 1) n = 1;

    /* Rule 2:6: "This limit may only be exceeded by a Character possessing
     * the Wand."  The Wand keeps her one Spell above what she started with,
     * so the limit has to make room for it or spell_enforce() would take it
     * straight back off her. */
    {
        int i;
        for (i = 0; i < p->nitems; i++)
            if (!strcmp(deck_proto[p->carried[i]].name, "Wand")) {
                if (n < p->start_spells + 1) n = p->start_spells + 1;
                break;
            }
    }
    if (n > MAX_SPELLS) n = MAX_SPELLS;
    return n;
}
