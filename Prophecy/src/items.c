#include "prophecy.h"

/* Strength IS Health and Willpower IS Magic: only the cubes on the right
 * side of the character card count in a battle, so a wounded hero is a
 * weaker hero.  Items and Abilities add on top of what is left. */
static int carried(const Player *p, int want_will)
{
    int i, v = 0;

    for (i = 0; i < p->nitems; i++) {
        if (p->dmg[i]) continue;      /* face-down, and may not be used */
        v += want_will ? item_proto[p->items[i]].d_will : item_proto[p->items[i]].d_str;
    }
    for (i = 0; i < p->nabils; i++)
        v += want_will ? abil_proto[p->abils[i]].d_will : abil_proto[p->abils[i]].d_str;
    return v;
}

/* The Crown is worn, not carried: its +1 is a standing Strength bonus, so it
 * belongs here with the Items rather than at any one battle. */
int eff_str(const Player *p)
{
    return p->str_now + carried(p, 0) + (has_art(p, ART_CROWN) ? 1 : 0);
}
int eff_will(const Player *p)
{
    return p->will_now + carried(p, 1) + (has_art(p, ART_CAPE) ? 1 : 0);
}

/* A Spell is simply an Ability with a Magic cost printed on it. */
int is_spell(const AbilityCard *a) { return a->magic > 0 || a->kind != SP_PASSIVE; }
