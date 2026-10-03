/* Offline balance model.
 *
 * Links against the real combat.c and gearsets.c, so it exercises the
 * shipping damage formula rather than a copy of it. Simulates a plausible
 * run -- the real XP curve, the real per-level stat gains, the real gear
 * tables -- and reports what an ordinary monster hit actually does at each
 * biome, in both directions.
 *
 * The check that matters: neither side may ever reach a point where its
 * attacks do nothing. `atk - def` used to fail this badly (the player took
 * literally zero from every non-crit hit past floor ~15), which is exactly
 * the kind of thing live playtesting is bad at noticing and a table like
 * this makes obvious.
 *
 * Run with `make test`. Exits non-zero if any tier regresses.
 */
#include "../src/common.h"
#include "../src/combat.h"
#include "../src/gearsets.h"
#include "../src/monsters.h"
#include <stdio.h>

/* Averaged base stats of each tier's monster roster (see monsters.c). */
typedef struct {
    int floor;
    const char *biome_name;
    int atk_lo, atk_hi;   /* base_atk range before the floor_num term */
    int def_avg;          /* base_def average */
    int hp_avg;           /* base_hp average */
    int xp_avg;           /* base_xp average */
} TierProfile;

static const TierProfile TIERS[BIOME_COUNT] = {
    {15, "Jungle",      4, 10,  1,  14,  6},
    {35, "Industrial",  7, 11,  3,  24, 12},
    {60, "Ruins",      12, 22,  8,  52, 26},
    {85, "Wastes",     18, 27,  9,  72, 40},
    {99, "Abyss",      26, 38, 15, 127, 70},
};

/* Depths to report on. The early rows matter as much as the deep ones:
   the danger when raising monster attack is over-correcting and making
   floor 1 lethal to a starting character. `set_tier < 0` means the player
   is too shallow to have found that biome's set gear yet and is still on
   shop equipment. */
typedef struct {
    int floor;
    int tier;        /* index into TIERS for the monster roster */
    int set_tier;    /* index into the gear sets, or -1 for shop gear */
    int shop_armour; /* flat armour bonus assumed when set_tier < 0 */
    int shop_weapon;
} Sample;

static const Sample SAMPLES[] = {
    { 1,  0, -1,  0,  0},   /* fresh character, starting kit */
    { 5,  0, -1,  5,  5},   /* early shop gear */
    {15,  0,  0,  0,  0},   /* Jungle set */
    {35,  1,  1,  0,  0},   /* Industrial set */
    {60,  2,  2,  0,  0},   /* Ruins set */
    {85,  3,  3,  0,  0},   /* Wastes set */
    {99,  4,  4,  0,  0},   /* Abyss set */
};
#define SAMPLE_COUNT (int)(sizeof(SAMPLES) / sizeof(SAMPLES[0]))

/* How many kills a player racks up clearing one floor. Deliberately on the
   generous side: over-levelling is the pessimistic case for this check. */
#define KILLS_PER_FLOOR 25

typedef struct {
    int level, maxhp, base_atk, base_def;
    int xp, xp_next;
} SimPlayer;

/* Mirrors grant_xp()'s level-up block in combat.c. */
static void sim_grant_xp(SimPlayer *s, int xp) {
    s->xp += xp;
    while (s->xp >= s->xp_next) {
        s->xp -= s->xp_next;
        s->level++;
        s->maxhp += 8;
        s->base_atk += 1;
        if (s->level % 2 == 0) s->base_def += 1;
        s->xp_next = 20 + (s->level - 1) * 15;
    }
}

/* Advance the simulated character from `from_floor` to `to_floor`. */
static void sim_descend(SimPlayer *s, int from_floor, int to_floor) {
    for (int f = from_floor; f <= to_floor; f++) {
        int tier = 0;
        for (int t = 0; t < BIOME_COUNT; t++) {
            if (f <= TIERS[t].floor) { tier = t; break; }
            tier = t;
        }
        int per_kill = TIERS[tier].xp_avg + f / 2;
        for (int k = 0; k < KILLS_PER_FLOOR; k++) sim_grant_xp(s, per_kill);
    }
}

/* Monsters now scale with the player's level, which is the standard way to
   stop a character outgrowing the game -- and the standard way to ruin it.
   If they scale as fast as the player does, every level-up is worthless and
   the player is running to stand still. So: at a fixed depth, gaining levels
   must still make you harder to kill. */
static int check_levelling_still_pays(void) {
    int failures = 0;
    const int floor = 60;
    const TierProfile *T = &TIERS[2];              /* Ruins roster */
    int gear_def = set_gear_piece(2, RELIC_ARMOR)->bonus + 8;

    printf("\nlevelling still pays (floor %d, same gear, rising level)\n", floor);
    printf("  level  maxhp  monster hit  ordinary hits you survive\n");

    int prev_survived = 0;
    for (int lv = 20; lv <= 140; lv += 20) {
        int maxhp = 40 + 8 * (lv - 1);
        int def   = 2 + lv / 2 + gear_def;
        int esc   = monsters_escalation_for(lv, floor);
        int atk   = (T->atk_hi + floor) * (100 + esc) / 100;
        int taken = damage_after_defence(atk, def, 0);
        int survived = maxhp / (taken > 0 ? taken : 1);

        printf("  %-6d %-6d %-12d %d\n", lv, maxhp, taken, survived);

        if (survived < prev_survived) {
            printf("  FAIL: level %d survives fewer hits than the level below it\n", lv);
            failures++;
        }
        prev_survived = survived;
    }
    return failures;
}


/* The purse a new character starts with must be the shipping number.
 *
   It sat at 600,000,000 through most of the six-actor rebuild -- deliberately,
   so the Tavern and the smith could be exercised from turn one -- and the
   comment beside it said "put it back to 60 before measuring anything". That
   comment was the only thing holding the line, and a comment is not a guard:
   every winnability figure this project quotes is a fair-start figure, and a
   testing purse that outlives the testing is how a game ships unbalanced with
   every number in its own docs saying otherwise.

   Reads main.c rather than calling the setup, because the setup is static and
   this file does not link main.o. That is the weaker kind of test and it is
   said out loud here: it checks the *source line*, so it catches both ways
   this goes wrong -- the constant being retuned, and the assignment being
   swapped back to a literal -- and would miss a second assignment somewhere
   else entirely. It is still better than the comment that was there before. */
static int check_the_starting_purse(void) {
    printf("the starting purse\n");
    int bad = 0;

    if (NEW_GAME_GOLD != 60) {
        printf("  FAIL: NEW_GAME_GOLD is %d, and the shipping purse is 60\n", NEW_GAME_GOLD);
        bad++;
    }

    const char *paths[] = { "src/main.c", "../src/main.c" };
    FILE *f = NULL;
    for (size_t i = 0; i < sizeof paths / sizeof paths[0] && !f; i++)
        f = fopen(paths[i], "r");
    if (!f) {
        printf("  FAIL: cannot open src/main.c (run from the repo root)\n");
        return bad + 1;
    }

    char line[512];
    bool found = false, literal = false;
    while (fgets(line, sizeof line, f)) {
        if (!strstr(line, "p->gold =")) continue;
        if (strstr(line, "NEW_GAME_GOLD")) { found = true; continue; }
        /* Any other assignment of a starting purse is the thing this guards. */
        for (const char *c = line; *c; c++)
            if (*c >= '0' && *c <= '9') { literal = true; break; }
    }
    fclose(f);

    if (!found) {
        printf("  FAIL: new-game setup no longer assigns NEW_GAME_GOLD\n");
        bad++;
    }
    if (literal) {
        printf("  FAIL: something assigns p->gold a bare number at new-game time\n");
        bad++;
    }
    if (!bad) printf("  a new character starts with %d gold, from the constant\n", NEW_GAME_GOLD);
    return bad;
}

int main(void) {
    int failures = 0;

    failures += check_the_starting_purse();
    printf("\n");

    SimPlayer s = { .level = 1, .maxhp = 40, .base_atk = 8, .base_def = 4,
                    .xp = 0, .xp_next = 20 };
    int prev_floor = 1;

    printf("Aether Descent -- balance model (%d kills/floor)\n", KILLS_PER_FLOOR);
    printf("formula: damage_after_defence() from src/combat.c\n\n");
    printf("%-11s %-5s %-5s  | monster -> player        | player -> monster\n",
           "biome", "floor", "lvl");
    printf("%-11s %-5s %-5s  | %-6s %-6s %-9s | %-6s %-6s %s\n",
           "", "", "", "dmg", "%maxhp", "hits2kill", "dmg", "mon hp", "hits2kill");
    printf("--------------------------------------------------------------------------------\n");

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        const Sample *S = &SAMPLES[i];
        const TierProfile *T = &TIERS[S->tier];
        sim_descend(&s, prev_floor, S->floor);
        prev_floor = S->floor + 1;

        /* Player gear at this depth: this biome's set armour + weapon plus
           the 2-piece set bonus, or plain shop gear if too shallow. */
        int player_def, player_atk;
        if (S->set_tier >= 0) {
            player_def = s.base_def + set_gear_piece(S->set_tier, RELIC_ARMOR)->bonus + 8;
            player_atk = s.base_atk + set_gear_piece(S->set_tier, RELIC_WEAPON)->bonus + 8;
        } else {
            player_def = s.base_def + S->shop_armour;
            player_atk = s.base_atk + S->shop_weapon;
        }

        /* Monster stats at this depth (mirrors instantiate() in monsters.c). */
        /* Mirrors instantiate() in monsters.c: linear in depth, then scaled
           by the run's escalation, which keys off the player rather than the
           floor. Keep this in step with monsters.c or the table lies. */
        int esc = monsters_escalation_for(s.level, S->floor);
        int m_atk_lo = (T->atk_lo + S->floor) * (100 + esc) / 100;
        int m_atk_hi = (T->atk_hi + S->floor) * (100 + esc) / 100;
        int m_def    = T->def_avg + S->floor / 5;
        int m_hp     = T->hp_avg + S->floor * 3;

        int in_lo  = damage_after_defence(m_atk_lo, player_def, 0);
        int in_hi  = damage_after_defence(m_atk_hi, player_def, 0);
        int out    = damage_after_defence(player_atk, m_def, 0);

        int pct       = in_hi * 100 / s.maxhp;
        int hits_on_p = s.maxhp / (in_hi > 0 ? in_hi : 1);
        int hits_on_m = (m_hp + out - 1) / (out > 0 ? out : 1);

        printf("%-11s %-5d %-5d  | %2d-%-3d %3d%%   %-9d | %-6d %-6d %d\n",
               S->set_tier >= 0 ? T->biome_name : "(shop gear)", S->floor, s.level,
               in_lo, in_hi, pct, hits_on_p,
               out, m_hp, hits_on_m);

        /* --- assertions ------------------------------------------------
           Bounds are deliberately loose enough to survive ordinary tuning
           and tight enough to catch the failure modes that actually
           happened: armour nullifying damage entirely, and the reverse
           over-correction where a starting character gets one-shot. */
        if (in_lo < 1 || in_hi < 1) {
            printf("   FAIL: monsters deal no damage here\n");
            failures++;
        }
        if (out < 1) {
            printf("   FAIL: player deals no damage here\n");
            failures++;
        }
        if (hits_on_p > 45) {
            printf("   FAIL: %d ordinary hits to kill the player -- monsters are irrelevant\n",
                   hits_on_p);
            failures++;
        }
        if (hits_on_p < 5) {
            printf("   FAIL: only %d ordinary hits to kill the player -- too swingy\n",
                   hits_on_p);
            failures++;
        }
        if (hits_on_m > 25) {
            printf("   FAIL: %d hits to kill one monster -- fights are a slog\n", hits_on_m);
            failures++;
        }
    }

    /* Armour must always help but never nullify. */
    failures += check_levelling_still_pays();

    printf("\ndefence curve at attack=40: ");
    for (int d = 0; d <= 400; d += 80) printf("def%d->%d  ", d, damage_after_defence(40, d, 0));
    printf("\n");
    for (int d = 0; d <= 4000; d += 250) {
        if (damage_after_defence(40, d, 0) < 1) {
            printf("FAIL: defence %d zeroes out a 40-attack hit\n", d);
            failures++;
            break;
        }
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
