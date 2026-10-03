#ifndef KITCHEN_H
#define KITCHEN_H

#include "common.h"

const char *meat_name(int grade);

/* What a body is worth to a cook. Depth sets the floor, the monster's own
   weight raises it, and a boss is always the best cut in the house. */
int meat_grade_for_kill(int floor_num, int monster_max_hp, bool boss);

/* Called from the one melee path. Silently does nothing if the kill was not
   clean, if the roll fails, or if the larder for that grade is full. Returns
   the grade taken, or -1. */
int meat_on_clean_kill(Player *p, int floor_num, int monster_max_hp, bool boss);

int  meat_total(const Player *p);

/* How much of one grade this character can carry. Grows with level, capped
   so a deep character is not hauling an abattoir. */
int  meat_larder_cap(const Player *p);

/* A patron of the house. Generated per service night. */
typedef struct {
    int  wants;        /* MeatGrade they came in for */
    int  purse;        /* what they will pay at a perfect match, before rep */
    const char *name;
    const char *line;
} Patron;

void kitchen_roll_patrons(const Player *p, Patron *out, int count);

/* What serving `grade` to `pat` pays, and what it does to the house.
   Both are pure so the tests can pin the economy without a terminal. */
int  kitchen_payout(const Player *p, const Patron *pat, int grade);
int  kitchen_rep_delta(const Patron *pat, int grade);

/* Reputation multiplies payouts; stated once so the UI and the payout agree. */
int  kitchen_rep_permille(int rep);

const char *kitchen_rep_title(int rep);

#endif
