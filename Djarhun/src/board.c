#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "djarhun.h"

int space_card[MAX_SPACES][MAX_ON_SPACE];
int space_ncard[MAX_SPACES];
int space_left[MAX_SPACES][MAX_ON_SPACE];

static const LandDef *land_of(int land)
{
    if (land < 0 || land >= LAND_COUNT) land = LAND_DURACH;
    return &land_tbl[land];
}

int land_len(int land) { return land_of(land)->n; }

int space_id(int land, int idx)
{
    const LandDef *L = land_of(land);
    return L->base + ((idx % L->n) + L->n) % L->n;
}

const Space *space_at(int land, int idx)
{
    const LandDef *L = land_of(land);
    return &L->spaces[((idx % L->n) + L->n) % L->n];
}

/* ---------------------------------------------------------------- dice --
 * "Although not included, you will need a d4, d6, d8, d10, d12 & d20." */
int die(int sides) { return sides < 1 ? 1 : (rand() % sides) + 1; }

/* ------------------------------------------------- the three key tables --
 * Reference sheet, front.  These are what make a statistic worth raising:
 * Strength is how much you can carry, Speed is how far you move, Sorcery
 * is how much magic you can hold. */
int items_allowed(int strength)
{
    if (strength <= 2)  return 3;
    if (strength <= 5)  return 4;
    if (strength <= 9)  return 5;
    if (strength <= 13) return 6;
    if (strength <= 19) return 7;
    return 8;
}

int move_die(int speed)
{
    if (speed <= 3)  return 4;
    if (speed <= 9)  return 6;
    if (speed <= 15) return 8;
    if (speed <= 21) return 10;
    return 12;
}

int spells_allowed(int sorcery)
{
    if (sorcery <= 2)  return 0;
    if (sorcery <= 5)  return 1;
    if (sorcery <= 9)  return 2;
    if (sorcery <= 16) return 3;
    return 4;
}

/* "To achieve Level 1, you need 7 XP.  To reach Level 2, you need an
 * additional 14 points (7 + 14 = 21)."  Each step is 7 more than the one
 * before, so the total for level n is 7 * n(n+1)/2 -- which reproduces the
 * printed table exactly: 7, 21, 42, 70, 105, 147, 196, 252, 315, 385. */
int xp_for_level(int level)
{
    if (level < 1) return 0;
    if (level > MAX_LEVEL) level = MAX_LEVEL;
    return 7 * level * (level + 1) / 2;
}

/* "Each Hero is allowed a maximum of 4 Health... If their Level is higher
 * than 4, then they are allowed to fortify their Health equal to their
 * Level." */
int max_health(const Hero *h)
{
    return h->level > 4 ? h->level : 4;
}

/* The land table owns the space-id range; if it ever outgrows the card
 * piles, say so here rather than corrupt them quietly. */
void board_check(void)
{
    int land, total = 0;
    for (land = 0; land < LAND_COUNT; land++) {
        const LandDef *L = &land_tbl[land];
        if (L->base + L->n > total) total = L->base + L->n;
    }
    if (total > MAX_SPACES) {
        fprintf(stderr, "board: %d spaces declared but MAX_SPACES is %d\n",
                total, MAX_SPACES);
        exit(1);
    }
}
