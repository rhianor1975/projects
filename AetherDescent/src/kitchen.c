/* The Ashfall Kitchen -- the pub on the north side of town.
 *
   The rule the whole thing hangs on is that meat comes off a blade and
   nothing else. Burn a thing to death and there is nothing on it; put an
   arrow through it and the cut is ruined; let a companion tear it apart and
   you have a mess. That single restriction is what makes the kitchen a
   build decision rather than a passive drop -- a pure caster walks past it
   entirely, and a fighter who has been ignoring the pub is leaving a second
   income on the floor of every corridor they cleared. */

#include "kitchen.h"
#include <string.h>
#include <stdlib.h>

static const char *MEAT_NAMES[MEAT_GRADE_COUNT] = {
    "stringy", "fair", "prime", "mythic"
};

const char *meat_name(int grade) {
    if (grade < 0 || grade >= MEAT_GRADE_COUNT) return "spoiled";
    return MEAT_NAMES[grade];
}

/* Base worth of a cut before the patron, the match and the house's standing
   are applied. The gap between grades is wide on purpose: the interesting
   decision at the pass is whether to spend a prime cut on a patron who only
   asked for fair, and that is only a decision if prime is worth much more. */
static const int MEAT_BASE[MEAT_GRADE_COUNT] = { 40, 140, 460, 1600 };

int meat_grade_for_kill(int floor_num, int monster_max_hp, bool boss) {
    if (boss) return MEAT_MYTHIC;

    /* Depth sets the floor of the grade. */
    int grade = MEAT_STRINGY;
    if (floor_num >= 25) grade = MEAT_FAIR;
    if (floor_num >= 60) grade = MEAT_PRIME;

    /* A heavy thing for its depth is a better cut than a rat at the same
       depth. Compared against a rough expectation rather than an exact one
       -- this only needs to separate the notable from the ordinary. */
    int typical = 12 + floor_num * 3;
    if (monster_max_hp > typical * 2 && grade < MEAT_PRIME) grade++;

    return grade;
}

int meat_on_clean_kill(Player *p, int floor_num, int monster_max_hp, bool boss) {
    /* A boss is always worth butchering; ordinary bodies mostly are not. */
    if (!boss && rand() % 100 >= MEAT_DROP_PCT) return -1;

    int grade = meat_grade_for_kill(floor_num, monster_max_hp, boss);
    if (p->meat[grade] >= meat_larder_cap(p)) return -1;

    p->meat[grade]++;
    return grade;
}

int meat_larder_cap(const Player *p) {
    int lvl = hero_driven_c(p)->level > 0 ? hero_driven_c(p)->level : 1;
    int cap = MEAT_LARDER_BASE + (lvl - 1) * MEAT_LARDER_PER_LEVEL;
    if (cap > MEAT_LARDER_CEIL) cap = MEAT_LARDER_CEIL;
    return cap;
}

int meat_total(const Player *p) {
    int n = 0;
    for (int g = 0; g < MEAT_GRADE_COUNT; g++) n += p->meat[g];
    return n;
}

int kitchen_rep_permille(int rep) {
    if (rep < 0) rep = 0;
    if (rep > KITCHEN_REP_MAX) rep = KITCHEN_REP_MAX;
    /* An unknown kitchen pays half. A famous one pays triple. */
    return 500 + rep * 25;
}

const char *kitchen_rep_title(int rep) {
    if (rep >= 90) return "the best table in the city";
    if (rep >= 70) return "a name people say";
    if (rep >= 45) return "well spoken of";
    if (rep >= 25) return "getting known";
    if (rep >= 10) return "a room above a bar";
    return "nobody's been in";
}

/* Patrons. The purse scales with the grade they came in for, so a room that
   wants mythic is a room worth having mythic for. */
static const char *PATRON_NAMES[] = {
    "a canal pilot",        "the assayer's widow",  "two off-shift miners",
    "a tally clerk",        "the harbourmaster",    "a lamplighter",
    "someone in good boots","a votary of the Rill", "the night foreman",
    "a lizard-track tout",  "an off-duty gatekeep", "a woman with a ledger"
};

static const char *PATRON_LINES[MEAT_GRADE_COUNT] = {
    "\"Something hot. Don't care what.\"",
    "\"Whatever's honest, and a lot of it.\"",
    "\"I've had a week. Bring the good cut.\"",
    "\"I heard what you brought up. That.\""
};

void kitchen_roll_patrons(const Player *p, Patron *out, int count) {
    /* Names are dealt without replacement. Drawing each independently put
       the same customer at the pass twice in a row on the very first live
       night -- seven draws from twelve names makes a collision far more
       likely than not, and it reads as a bug even though the payouts were
       right. Partial Fisher-Yates over an index deck. */
    enum { NAME_COUNT = (int)(sizeof PATRON_NAMES / sizeof *PATRON_NAMES) };
    int deck[NAME_COUNT];
    for (int i = 0; i < NAME_COUNT; i++) deck[i] = i;
    for (int i = 0; i < NAME_COUNT - 1; i++) {
        int j = i + rand() % (NAME_COUNT - i);
        int t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }

    /* Deeper players draw a better room -- word travels. Reputation pulls
       the same way, which is what makes rep worth having beyond the
       multiplier: a famous kitchen gets asked for the expensive things. */
    int reach = p->deepest_floor / 20 + p->kitchen_rep / 30;

    for (int i = 0; i < count; i++) {
        int want = rand() % (2 + (reach > 2 ? 2 : reach));
        if (want >= MEAT_GRADE_COUNT) want = MEAT_GRADE_COUNT - 1;

        /* The purse scales with how deep you have been, not just with what
           they ordered. Without this the room stops improving around floor
           forty -- demand saturates once all four grades are on the table --
           and a night at floor 100 pays less than a night at floor 40, which
           is the sort of thing that reads as a balance opinion and is really
           an arithmetic mistake.

           Deliberately linear, so the kitchen stays front-loaded: several
           upgrade rungs a night when you are poor and it matters, and about
           half a rung a night at the bottom, where it should be a supplement
           and not the reason to come up. */
        long purse = (long)MEAT_BASE[want] * (10 + p->deepest_floor) / 10;
        purse += rand() % (purse / 2 + 1);

        out[i].wants = want;
        out[i].purse = (int)(purse > 1000000 ? 1000000 : purse);
        /* More covers than names would wrap and repeat; the deck is sized so
           that cannot happen, and this keeps it true if either number moves. */
        out[i].name  = PATRON_NAMES[deck[i % NAME_COUNT]];
        out[i].line  = PATRON_LINES[want];
    }
}

int kitchen_payout(const Player *p, const Patron *pat, int grade) {
    if (grade < 0 || grade >= MEAT_GRADE_COUNT) return 0;

    int gap = grade - pat->wants;
    long pay;

    if (gap == 0) {
        pay = pat->purse;                       /* exactly what they asked for */
    } else if (gap > 0) {
        /* Better than they asked for. They are pleased, and they pay what
           they came in with -- not what the cut was worth. Overserving is
           the mistake the game wants you to be able to make. */
        pay = pat->purse + (long)pat->purse * 15 / 100;
    } else {
        /* Worse. They eat it, and they pay accordingly -- steeply, so that
           fobbing a prime-wanting patron off with stringy is not a strategy. */
        pay = pat->purse;
        for (int i = gap; i < 0; i++) pay = pay * 30 / 100;
    }

    pay = pay * kitchen_rep_permille(p->kitchen_rep) / 1000;
    return (int)(pay > 2000000 ? 2000000 : pay);
}

int kitchen_rep_delta(const Patron *pat, int grade) {
    if (grade < 0 || grade >= MEAT_GRADE_COUNT) return -3;   /* turned away */

    int gap = grade - pat->wants;
    if (gap == 0) return 3;
    if (gap  > 0) return 4;      /* delighted, and they tell people */
    if (gap == -1) return -2;
    return -5;
}
