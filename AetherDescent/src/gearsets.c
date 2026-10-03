#include "gearsets.h"

/* Every dungeon-only relic belongs to exactly one of these five sets, one
   per biome, each with exactly one hand-authored piece per gear slot --
   no procedural generation, since 5 x 4 = 20 items is small enough to name
   properly and a set's identity depends on those names meaning something.
   Base bonus climbs steeply by biome (a Salt-Cured weapon comfortably
   outclasses the best shop weapon) since these exist specifically to give
   depth a reason to matter once gold stops being the bottleneck. Names
   echo the biome's own boss (Root-Drowned Matriarch, Foreman's Engine,
   Flooded Custodian, Salt-Cured Colossus, Warden of the Deep Well). */
static const SetGearTemplate SET_GEAR[BIOME_COUNT][RELIC_KIND_COUNT] = {
    [BIOME_JUNGLE] = {
        [RELIC_WEAPON]  = {"Root-Drowned Thornblade",   14, ACC_NONE},
        [RELIC_ARMOR]   = {"Root-Drowned Bark Plating", 15, ACC_NONE},
        [RELIC_RING]    = {"Root-Drowned Vine-Ring",     9, ACC_EVASION},
        [RELIC_TRINKET] = {"Root-Drowned Seed-Pouch",    9, ACC_XP},
    },
    [BIOME_INDUSTRIAL] = {
        [RELIC_WEAPON]  = {"Foreman's Piston Maul",     23, ACC_NONE},
        [RELIC_ARMOR]   = {"Foreman's Riveted Plating", 25, ACC_NONE},
        [RELIC_RING]    = {"Foreman's Signet",          15, ACC_WARD},
        [RELIC_TRINKET] = {"Foreman's Ledger",          15, ACC_GOLD},
    },
    [BIOME_RUINS] = {
        [RELIC_WEAPON]  = {"Custodian's Crystal Halberd", 35, ACC_NONE},
        [RELIC_ARMOR]   = {"Custodian's Drowned Mail",    37, ACC_NONE},
        [RELIC_RING]    = {"Custodian's Ward-Band",       23, ACC_WARD},
        [RELIC_TRINKET] = {"Custodian's Reliquary",       23, ACC_CRIT},
    },
    [BIOME_WASTES] = {
        [RELIC_WEAPON]  = {"Salt-Cured Cleaver",             49, ACC_NONE},
        [RELIC_ARMOR]   = {"Salt-Cured Hide Plating",        51, ACC_NONE},
        [RELIC_RING]    = {"Salt-Cured Wanderer's Band",     33, ACC_EVASION},
        [RELIC_TRINKET] = {"Salt-Cured Waterskin",           33, ACC_GOLD},
    },
    [BIOME_ABYSS] = {
        [RELIC_WEAPON]  = {"Warden's Reaping Scythe", 67, ACC_NONE},
        [RELIC_ARMOR]   = {"Warden's Black Shroud",   70, ACC_NONE},
        [RELIC_RING]    = {"Warden's Signet",         45, ACC_CRIT},
        [RELIC_TRINKET] = {"Warden's Locket",         45, ACC_WARD},
    },
};

static const char *SET_NAMES[BIOME_COUNT] = {
    "Root-Drowned", "Foreman's", "Custodian's", "Salt-Cured", "Warden's"
};

const char *set_name(int biome) {
    if (biome < 0 || biome >= BIOME_COUNT) return "?";
    return SET_NAMES[biome];
}

const SetGearTemplate *set_gear_piece(int biome, int slot) {
    if (biome < 0 || biome >= BIOME_COUNT || slot < 0 || slot >= RELIC_KIND_COUNT) return NULL;
    return &SET_GEAR[biome][slot];
}

/* 2 pieces of any one set: a flat combat-stat bump. A full 4: an additional
   biome-flavored bonus on top, matching that biome's signature danger
   (Jungle poison -> evasion, Industrial machinery -> more armor, Ruins'
   old wards -> ward, the corrosive Wastes -> hazard resist, the Abyss's
   killing precision -> crit). Only one biome's worth of pieces can ever
   reach 4 at once -- there are only 4 gear slots total. */
#define SET_2PC_ATK 8
#define SET_2PC_DEF 8
#define SET_4PC_BONUS 14

void recompute_set_bonus(Hero *h) {
    h->set_complete = -1;
    h->set_bonus_atk = 0;
    h->set_bonus_def = 0;
    h->set_bonus_evasion = 0;
    h->set_bonus_ward = 0;
    h->set_bonus_hazard_resist = 0;
    h->set_bonus_crit = 0;

    for (int b = 0; b < BIOME_COUNT; b++) {
        int count = (h->weapon_set == b) + (h->armor_set == b) + (h->ring_set == b) + (h->trinket_set == b);
        if (count < 2) continue;

        h->set_bonus_atk += SET_2PC_ATK;
        h->set_bonus_def += SET_2PC_DEF;
        if (count < 4) continue;
        h->set_complete = b;      /* four pieces: the proc is live -- see gearsets.h */

        switch (b) {
            case BIOME_JUNGLE:     h->set_bonus_evasion += SET_4PC_BONUS; break;
            case BIOME_INDUSTRIAL: h->set_bonus_def += SET_4PC_BONUS; break;
            case BIOME_RUINS:      h->set_bonus_ward += SET_4PC_BONUS; break;
            case BIOME_WASTES:     h->set_bonus_hazard_resist += SET_4PC_BONUS; break;
            case BIOME_ABYSS:      h->set_bonus_crit += SET_4PC_BONUS; break;
            default: break;
        }
    }
}

void apply_set_gear(Hero *h, int biome, int slot) {
    const SetGearTemplate *g = set_gear_piece(biome, slot);
    if (!g) return;

    int bonus = g->bonus + h->relic_quality_bonus;

    switch (slot) {
        case RELIC_WEAPON:
            h->weapon_bonus = bonus;
            strncpy(h->weapon_name, g->name, sizeof(h->weapon_name) - 1);
            h->weapon_name[sizeof(h->weapon_name) - 1] = '\0';
            h->weapon_set = biome;
            log_msg("You cannot resist taking up the %s.", g->name);
            break;
        case RELIC_ARMOR:
            h->armor_bonus = bonus;
            strncpy(h->armor_name, g->name, sizeof(h->armor_name) - 1);
            h->armor_name[sizeof(h->armor_name) - 1] = '\0';
            h->armor_set = biome;
            log_msg("You cannot resist putting on the %s.", g->name);
            break;
        case RELIC_RING:
            strncpy(h->ring_name, g->name, sizeof(h->ring_name) - 1);
            h->ring_name[sizeof(h->ring_name) - 1] = '\0';
            h->ring_stat = g->accessory_stat;
            h->ring_bonus = bonus;
            h->ring_set = biome;
            log_msg("You cannot resist fitting the %s.", g->name);
            break;
        case RELIC_TRINKET:
            strncpy(h->trinket_name, g->name, sizeof(h->trinket_name) - 1);
            h->trinket_name[sizeof(h->trinket_name) - 1] = '\0';
            h->trinket_stat = g->accessory_stat;
            h->trinket_bonus = bonus;
            h->trinket_set = biome;
            log_msg("You cannot resist fitting the %s.", g->name);
            break;
        default:
            return;
    }

    recompute_set_bonus(h);
}

void grant_random_set_piece(Hero *h, int biome) {
    apply_set_gear(h, biome, rand() % RELIC_KIND_COUNT);
}

/* ---- the procs ---------------------------------------------------------
   See gearsets.h for why these exist and why they waited for the damage model.

   Every one is announced, unlike the band gifts: a set proc is something the
   player earned by finishing a set and should be able to see working, where a
   band gift is something the game did quietly for somebody who was losing.
   Same machinery, opposite decision, and the difference is who chose it. */

int set_proc_on_hit(Player *p, Hero *h, Monster *mo, int dmg) {
    if (!h || !mo || h->set_complete < 0 || SET_PROC_PCT <= 0) return 0;
    if (rand() % 100 >= SET_PROC_PCT) return 0;

    bool mine = (h == hero_driven(p));
    const char *who = mine ? "Your" : h->name;

    switch (h->set_complete) {
        case BIOME_JUNGLE:
            /* The venom the jungle is named for. Stacks its duration rather
               than its damage, so it is a reason to keep hitting the same
               thing rather than a burst. */
            mo->jinx_atk_penalty += 2;
            if (mo->jinx_turns_left < 6) mo->jinx_turns_left = 6;
            log_msg("%s root-drowned gear weeps venom into the %s.", who, mo->name);
            return 0;

        case BIOME_INDUSTRIAL:
            /* Machinery: a blow that staggers. The Works' own signature
               condition, turned around and pointed outward. */
            mo->stun_turns_left += 1;
            log_msg("%s Company plate drives the %s off its footing.", who, mo->name);
            return 0;

        case BIOME_RUINS:
            /* The wards cut both ways: a pulse that ignores armour. */
            log_msg("%s ward-touched set discharges into the %s.", who, mo->name);
            return dmg / 3 + 1;

        case BIOME_WASTES:
            /* Corrosion eats what it lands on -- permanently, for this
               monster. The only proc that makes a fight easier the longer it
               lasts, which is what the Wastes are about. */
            if (mo->def > 0) mo->def -= 1;
            log_msg("%s salt-bitten edge eats into the %s's guard.", who, mo->name);
            return 0;

        case BIOME_ABYSS:
            /* The killing dark: a heavier strike out of nothing. */
            log_msg("%s abyssal set finds something soft in the %s.", who, mo->name);
            return dmg / 2 + 2;

        default: break;
    }
    return 0;
}

int set_proc_on_hurt(Player *p, Hero *h, int dmg) {
    if (!h || h->set_complete != BIOME_RUINS || SET_PROC_PCT <= 0) return dmg;
    if (rand() % 100 >= SET_PROC_PCT) return dmg;

    /* Ward-Touched is the only set whose proc is defensive, and it is the one
       whose flat bonus is already ward -- the set does one thing, harder, at
       four pieces. */
    if (h == hero_driven(p)) log_msg("The old wards take the blow instead of you.");
    else                     log_msg("The old wards take the blow instead of %s.", h->name);
    return 0;
}
