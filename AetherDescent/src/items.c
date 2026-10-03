#include "items.h"
#include "bands.h"
#include "quests.h"
#include "dda.h"
#include "gearsets.h"
#include <string.h>

/* name, price, heal, heal%, atk buff, turns, def buff, turns, +maxhp, recall */
const ConsumableTemplate GENERAL_STOCK[] = {
    {"Ration Pack",              6,  8,  3,  0, 0,  0, 0,  0, false},
    {"Traveler's Bandages",      9,  12, 5,  0, 0,  0, 0,  0, false},
    {"Minor Healing Draught",   14,  14, 8,  0, 0,  0, 0,  0, false},
    {"Ether Tonic (Lesser)",    15,  0,  0,  3, 15, 0, 0,  0, false},
    {"Guard Tonic (Lesser)",    13,  0,  0,  0, 0,  3, 15, 0, false},
    {"Field Rations, Preserved",20,  18, 10, 0, 0,  0, 0,  0, false},
};
const int GENERAL_STOCK_COUNT = (int)(sizeof(GENERAL_STOCK) / sizeof(GENERAL_STOCK[0]));

const ConsumableTemplate APOTHECARY_STOCK[] = {
    {"Greater Healing Draught",  32, 25, 18, 0,  0,  0, 0,  0, false},
    {"Superior Healing Draught", 55, 35, 30, 0,  0,  0, 0,  0, false},
    {"Ether Tonic (Greater)",    35, 0,  0,  6,  20, 0, 0,  0, false},
    {"Ether Tonic (Volatile)",   55, 0,  0,  10, 15, 0, 0,  0, false},
    {"Guard Draught",            25, 0,  0,  0,  0,  5, 20, 0, false},
    {"Guard Draught, Reinforced",45, 0,  0,  0,  0,  9, 20, 0, false},
    /* 5,000 gold for +5 permanent max HP. Deliberately poor value per coin:
       it is a trickle you can always buy rather than a stat you can rush, and
       at fifty gold a go it was the cheapest stat in the game by two orders
       of magnitude. */
    {"Elixir of Vigor",        5000, 0,  0,  0,  0,  0, 0,  5, false},
    {"Recall Charm",             45, 0,  0,  0,  0,  0, 0,  0, true},
};
const int APOTHECARY_STOCK_COUNT = (int)(sizeof(APOTHECARY_STOCK) / sizeof(APOTHECARY_STOCK[0]));

const GearTemplate WEAPON_STOCK[] = {
    {"Rusty Cutlass",        15,  2},
    {"Brass Rapier",         55,  5},
    {"Riveted Cleaver",     110,  8},
    {"Steam-Forged Sabre",  200, 12},
    {"Clockwork Estoc",     350, 16},
    {"Star-iron Edge",      600, 21},
    {"Ether-Etched Blade", 1000, 27},
};
const int WEAPON_STOCK_COUNT = (int)(sizeof(WEAPON_STOCK) / sizeof(WEAPON_STOCK[0]));

const GearTemplate ARMOR_STOCK[] = {
    {"Worn Leathers",           15,  2},
    {"Riveted Jacket",          55,  5},
    {"Padded Brigandine",      110,  8},
    {"Brass Plate",            200, 12},
    {"Sealed Pressure Suit",   350, 16},
    {"Star-iron Mail",         600, 21},
    {"Void-tempered Plate",   1000, 27},
};
const int ARMOR_STOCK_COUNT = (int)(sizeof(ARMOR_STOCK) / sizeof(ARMOR_STOCK[0]));

/* name, type, price, base damage, ammo cost, cooldown */
const RangedTemplate RANGED_STOCK[] = {
    {"Recurve Longbow",           RANGED_BOW,      60,  12, 1, 2},
    {"Reinforced Compound Bow",   RANGED_BOW,     200,  20, 1, 2},
    {"Aether-Fletched Warbow",    RANGED_BOW,     500,  32, 1, 2},

    {"Brass Revolver",            RANGED_GUN,      80,  16, 1, 3},
    {"Steam-Cycled Rifle",        RANGED_GUN,     260,  26, 1, 3},
    {"Company Autoloader",        RANGED_GUN,     650,  40, 2, 3},

    {"Prototype Beam Emitter",    RANGED_LASER,   150,  10, 2, 4},
    {"Refined Aether Cannon",     RANGED_LASER,   400,  16, 2, 4},
    {"Sunfire Projector",         RANGED_LASER,   900,  24, 3, 4},

    {"Bone Blowgun",              RANGED_BLOWGUN,  50,   6, 1, 2},
    {"Venom-Coil Pipe",           RANGED_BLOWGUN, 180,  10, 1, 2},
    {"Widowmaker's Reed",         RANGED_BLOWGUN, 450,  16, 1, 2},

    {"Rusted Throwing Stars",     RANGED_THROWN,   45,   8, 1, 1},
    {"Balanced Razor-Discs",      RANGED_THROWN,  160,  14, 1, 1},
    {"Company Star-Set",          RANGED_THROWN,  420,  22, 1, 1},

    {"Tin Grenade",               RANGED_GRENADE, 100,  14, 2, 5},
    {"Fragmentation Charge",      RANGED_GRENADE, 320,  22, 3, 5},
    {"Company Ordnance",          RANGED_GRENADE, 750,  34, 3, 5},
};
const int RANGED_STOCK_COUNT = (int)(sizeof(RANGED_STOCK) / sizeof(RANGED_STOCK[0]));

const ConsumableTemplate MERCHANT_STOCK[] = {
    {"Minor Healing Draught",   20, 14, 8, 0, 0, 0,  0, 0, false},
    {"Greater Healing Draught", 45, 25, 18, 0, 0, 0,  0, 0, false},
    {"Ether Tonic (Greater)",   48, 0, 0,  6, 20, 0, 0, 0, false},
    {"Guard Draught",           35, 0, 0,  0, 0,  5, 20, 0, false},
    {"Recall Charm",            60, 0, 0,  0, 0,  0, 0,  0, true},
};
const int MERCHANT_STOCK_COUNT = (int)(sizeof(MERCHANT_STOCK) / sizeof(MERCHANT_STOCK[0]));

static const ConsumableTemplate LOOT_LOW[] = {
    {"Ration Pack",           0, 8, 3,  0, 0,  0, 0,  0, false},
    {"Minor Healing Draught", 0, 14, 8, 0, 0,  0, 0,  0, false},
    {"Ether Tonic (Lesser)",  0, 0, 0,  3, 15, 0, 0,  0, false},
    {"Guard Tonic (Lesser)",  0, 0, 0,  0, 0,  3, 15, 0, false},
};
static const ConsumableTemplate LOOT_MID[] = {
    {"Greater Healing Draught", 0, 25, 18, 0, 0,  0, 0,  0, false},
    {"Ether Tonic (Greater)",   0, 0, 0,  6, 20, 0, 0,  0, false},
    {"Guard Draught",           0, 0, 0,  0, 0,  5, 20, 0, false},
    {"Elixir of Vigor",         0, 0, 0,  0, 0,  0, 0,  5, false},
};
static const ConsumableTemplate LOOT_HIGH[] = {
    {"Superior Healing Draught",  0, 35, 30, 0,  0,  0, 0, 0, false},
    {"Ether Tonic (Volatile)",    0, 0, 0,  10, 15, 0, 0, 0, false},
    {"Guard Draught, Reinforced", 0, 0, 0,  0,  0,  9, 20, 0, false},
    {"Recall Charm",              0, 0, 0,  0,  0,  0, 0, 0, true},
};

/* Consumables are declared in five separate tables (two shops, the wandering
   merchant, and three loot tiers), and the same drink appears in several of
   them. Nothing structural stops those copies drifting apart -- adding the
   heal-percentage field, they immediately did, leaving a looted Superior
   Draught healing twice what a bought one did. This lets tests/invariants.c
   walk every row and insist that one name means one item. */
static const ConsumableTemplate *const ALL_TABLES[] = {
    GENERAL_STOCK, APOTHECARY_STOCK, MERCHANT_STOCK, LOOT_LOW, LOOT_MID, LOOT_HIGH,
};
static const int ALL_TABLE_COUNTS[] = {
    (int)(sizeof(GENERAL_STOCK) / sizeof(GENERAL_STOCK[0])),
    (int)(sizeof(APOTHECARY_STOCK) / sizeof(APOTHECARY_STOCK[0])),
    (int)(sizeof(MERCHANT_STOCK) / sizeof(MERCHANT_STOCK[0])),
    (int)(sizeof(LOOT_LOW) / sizeof(LOOT_LOW[0])),
    (int)(sizeof(LOOT_MID) / sizeof(LOOT_MID[0])),
    (int)(sizeof(LOOT_HIGH) / sizeof(LOOT_HIGH[0])),
};
#define ALL_TABLE_N ((int)(sizeof(ALL_TABLES) / sizeof(ALL_TABLES[0])))

int consumable_count(void) {
    int n = 0;
    for (int t = 0; t < ALL_TABLE_N; t++) n += ALL_TABLE_COUNTS[t];
    return n;
}

const ConsumableTemplate *consumable_get(int idx) {
    if (idx < 0) return NULL;
    for (int t = 0; t < ALL_TABLE_N; t++) {
        if (idx < ALL_TABLE_COUNTS[t]) return &ALL_TABLES[t][idx];
        idx -= ALL_TABLE_COUNTS[t];
    }
    return NULL;
}

const ConsumableTemplate *consumable_by_name(const char *name) {
    if (!name) return NULL;
    int n = consumable_count();
    for (int i = 0; i < n; i++) {
        const ConsumableTemplate *t = consumable_get(i);
        if (t && strcmp(t->name, name) == 0) return t;
    }
    return NULL;
}

const ConsumableTemplate *pick_floor_loot(int floor_num) {
    if (floor_num <= 30) {
        return &LOOT_LOW[rand() % 4];
    } else if (floor_num <= 70) {
        return (rand() % 100 < 60) ? &LOOT_MID[rand() % 4] : &LOOT_LOW[rand() % 4];
    } else {
        return (rand() % 100 < 55) ? &LOOT_HIGH[rand() % 4] : &LOOT_MID[rand() % 4];
    }
}

const AccessoryTemplate ACCESSORY_STOCK[] = {
    {"Ring of Precision",  200, ACC_CRIT,    8},
    {"Ring of Evasion",    200, ACC_EVASION, 8},
    {"Charm of Warding",   200, ACC_WARD,    6},
    {"Lucky Coin-Charm",   180, ACC_GOLD,   10},
    {"Scholar's Locket",   180, ACC_XP,     10},
    /* Deliberately the most expensive thing in the game -- above the 2600g
       level-5 spells and well above any weapon. It removes the attrition
       ceiling, so it should cost a run's worth of gold to reach. */
    {"Ouroboros Coil",    5000, ACC_REGEN,   6},
};
const int ACCESSORY_STOCK_COUNT = (int)(sizeof(ACCESSORY_STOCK) / sizeof(ACCESSORY_STOCK[0]));

void equip_accessory(Player *p, const char *name, int stat, int bonus) {
    char *target_name;
    int *target_stat, *target_bonus, *target_set;

    if (hero_driven(p)->ring_stat == ACC_NONE) {
        target_name = hero_driven(p)->ring_name;
        target_stat = &hero_driven(p)->ring_stat;
        target_bonus = &hero_driven(p)->ring_bonus;
        target_set = &hero_driven(p)->ring_set;
    } else if (hero_driven(p)->trinket_stat == ACC_NONE) {
        target_name = hero_driven(p)->trinket_name;
        target_stat = &hero_driven(p)->trinket_stat;
        target_bonus = &hero_driven(p)->trinket_bonus;
        target_set = &hero_driven(p)->trinket_set;
    } else {
        target_name = hero_driven(p)->ring_name;
        target_stat = &hero_driven(p)->ring_stat;
        target_bonus = &hero_driven(p)->ring_bonus;
        target_set = &hero_driven(p)->ring_set;
    }

    strncpy(target_name, name, 39);
    target_name[39] = '\0';
    *target_stat = stat;
    *target_bonus = bonus;
    /* Shop accessories aren't part of a set -- clear whatever set tag this
       slot might have had from a previously-worn relic. */
    *target_set = -1;
    recompute_set_bonus(hero_driven(p));
}

void grant_starting_kit(Player *p) {
    /* Two draughts for everyone -- the long-standing floor-1 concession. */
    give_consumable(p, &GENERAL_STOCK[0], 2);

    /* And a way out, for the modes that need one.
     *
       Hard and Swarm open against a crowd a level-1 character cannot fight,
       and until now had no way to turn a first floor into anything: no gold,
       no gear, and no exit but the stairs. A charm converts a bad floor into
       a trip to the smith. Hardcore is left without deliberately -- recall is
       disabled there by design, and that *is* the mode. */
    if (p->difficulty == DIFFICULTY_HARD || p->difficulty == DIFFICULTY_SWARM) {
        for (int i = 0; i < GENERAL_STOCK_COUNT; i++) {
            if (!GENERAL_STOCK[i].is_recall) continue;
            give_consumable(p, &GENERAL_STOCK[i], 2);
            break;
        }
    }
}

bool give_consumable(Player *p, const ConsumableTemplate *t, int qty) {
    for (int i = 0; i < p->inv_count; i++) {
        if (strcmp(p->inventory[i].name, t->name) == 0) {
            p->inventory[i].count += qty;
            return true;
        }
    }
    if (p->inv_count >= INV_CAP) return false;

    InvStack *s = &p->inventory[p->inv_count];
    strncpy(s->name, t->name, sizeof(s->name) - 1);
    s->heal = t->heal;
    s->heal_pct = t->heal_pct;
    s->atk_buff = t->atk_buff;
    s->atk_buff_turns = t->atk_buff_turns;
    s->def_buff = t->def_buff;
    s->def_buff_turns = t->def_buff_turns;
    s->perm_maxhp = t->perm_maxhp;
    s->is_recall = t->is_recall;
    s->count = qty;
    p->inv_count++;
    return true;
}

void use_inventory_item(Player *p, int idx) {
    if (idx < 0 || idx >= p->inv_count) return;
    InvStack *s = &p->inventory[idx];

    if (s->is_recall) {
        /* A no-recall bounty is broken here and nowhere else -- the one place
           a charm is actually spent. */
        quest_note_recall(p);
        log_msg("Press 'r' to channel a recall charm -- it needs a steady moment.");
        return;
    }

    double potency = 1.0 + hero_driven(p)->potion_potency_pct / 100.0;
    double duration = 1.0 + hero_driven(p)->buff_duration_pct / 100.0;

    if (s->heal > 0 || s->heal_pct > 0) {
        /* A floor that needed potions was a hard floor -- one of the four
           signals DDA reads. Noted here so every producer is one grep away. */
        dda_note_heal(p);
        int amount = (int)((s->heal + hero_driven(p)->maxhp * s->heal_pct / 100) * potency);
        hero_driven(p)->hp += amount;
        if (hero_driven(p)->hp > hero_driven(p)->maxhp) hero_driven(p)->hp = hero_driven(p)->maxhp;
        log_msg("You drink the %s, healing %d HP.", s->name, amount);
        if (hero_driven(p)->poison_turns_left > 0) {
            hero_driven(p)->poison_turns_left = 0;
            log_msg("The draught cleanses the poison from your blood.");
        }
    }
    if (s->atk_buff > 0) {
        hero_driven(p)->atk_buff = (int)(s->atk_buff * potency);
        hero_driven(p)->atk_buff_turns = (int)(s->atk_buff_turns * duration);
        log_msg("Ether courses through you: +%d attack for %d turns.", hero_driven(p)->atk_buff, hero_driven(p)->atk_buff_turns);
    }
    if (s->def_buff > 0) {
        hero_driven(p)->def_buff = (int)(s->def_buff * potency);
        hero_driven(p)->def_buff_turns = (int)(s->def_buff_turns * duration);
        log_msg("Your guard steadies: +%d defence for %d turns.", hero_driven(p)->def_buff, hero_driven(p)->def_buff_turns);
    }
    if (s->perm_maxhp > 0) {
        int amount = (int)(s->perm_maxhp * potency);
        hero_driven(p)->maxhp += amount;
        hero_driven(p)->hp += amount;
        log_msg("You feel permanently sturdier: +%d max HP.", amount);
    }

    /* Salt preserves. On the Wastes, for a run that is going badly, a draught
       is sometimes not spent -- nothing is said about it, the count simply does
       not move. See bands.h. */
    if (!band_supply_saved(p)) s->count--;
    if (s->count <= 0) {
        for (int i = idx; i < p->inv_count - 1; i++) {
            p->inventory[i] = p->inventory[i + 1];
        }
        p->inv_count--;
    }
}

bool consume_recall_charm(Player *p) {
    for (int i = 0; i < p->inv_count; i++) {
        if (p->inventory[i].is_recall && p->inventory[i].count > 0) {
            p->inventory[i].count--;
            if (p->inventory[i].count <= 0) {
                for (int j = i; j < p->inv_count - 1; j++) {
                    p->inventory[j] = p->inventory[j + 1];
                }
                p->inv_count--;
            }
            return true;
        }
    }
    return false;
}

int count_recall_charms(const Player *p) {
    int total = 0;
    for (int i = 0; i < p->inv_count; i++) {
        if (p->inventory[i].is_recall) total += p->inventory[i].count;
    }
    return total;
}

int upgrade_price(int current_plus) {
    if (current_plus < 0) current_plus = 0;
    int n = current_plus + 1;
    /* 140, 400, 780, 1280, 1900, ... -- the first is pocket change by floor
       5 and the twentieth is a run's savings. */
    return 20 * n * n + 100 * n + 20;
}

long level_sale_price(int level, int deepest_floor) {
    (void)deepest_floor;
    if (level < 2) return 0;

    /* A gold a point, for exactly the experience that level cost.
     *
       This was keyed to deepest_floor, to stop somebody reaching level 40 on
       the first floor and cashing out. It stopped that and opened something
       worse: the price then had nothing to do with where you were standing,
       so the play became "touch floor 81 once, retreat to floor 3, farm 93
       harmless monsters forever and sell at 18,870 a level". Same lap, same
       minutes, thirty-three times the pay -- a high-water mark being
       laundered rather than a reward for risk.

       Depth does not need help from the price. A lap of floor 94 yields
       30,791 experience against floor 3's 655 for the same walking, so deep
       farming already earns levels forty-seven times faster and therefore
       earns gold forty-seven times faster. Paying a flat rate per point of
       experience lets that curve do the work, and means the market can never
       pay for a risk you are not currently taking.

       The rate is calibrated against what a lap pays in coin: at a gold a
       point, selling what you earned roughly doubles a lap's income at every
       depth. A real choice, at the price of your own hit points, which is
       what an unguarded market should be. */
    return MARKET_GOLD_PER_XP * (20 + (long)(level - 1) * 15);
}

long level_sale_batch_price(int level, int count, int deepest_floor) {
    int max = level_sale_max(level);
    if (count > max) count = max;
    if (count < 1) return 0;

    long total = 0;
    for (int i = 0; i < count; i++) total += level_sale_price(level - i, deepest_floor);
    return total;
}

int level_sale_def_loss(int level, int count) {
    int max = level_sale_max(level);
    if (count > max) count = max;
    if (count < 1) return 0;

    /* Even levels in [level-count+1, level] -- defence is granted on
       reaching an even level, so it is taken back on giving one up. */
    return level / 2 - (level - count) / 2;
}

/* What a price becomes after the driven body's haggling.
 *
   This was `static int discounted_price()` in main.c, with the same
   arithmetic written out inline at seven places in render.c -- the tills used
   the function and the screens used the copies. Nothing had drifted yet, but a
   shop whose displayed price is computed separately from its charged price is
   one edit away from lying to the player, and the Bazaar makes that worse by
   moving the base price around. One function, both sides. */
int discounted_price(const Player *p, int price) {
    int off = price - price * hero_driven_c(p)->shop_discount_pct / 100;
    return off < 0 ? 0 : off;
}

int training_price(int value) {
    if (value < 1) value = 1;
    return 200 * value * value + 300;
}

int upgrade_material_tier(int current_plus) {
    if (current_plus >= UPGRADE_DIAMOND_FROM)  return MAT_DIAMOND;
    if (current_plus >= UPGRADE_PLATINUM_FROM) return MAT_PLATINUM;
    return MAT_SCRAP;
}

int upgrade_material_cost(int current_plus) {
    /* Scrap rungs cost none: the early game should not be gated on a
       currency the player has not been taught about yet. */
    if (current_plus < UPGRADE_PLATINUM_FROM) return 0;
    int over = current_plus - UPGRADE_PLATINUM_FROM;
    if (current_plus >= UPGRADE_DIAMOND_FROM) over = current_plus - UPGRADE_DIAMOND_FROM;
    return 2 + over / 4;
}

long bank_price(int current_mult) {
    if (current_mult < 1) current_mult = 1;
    if (current_mult >= BANK_MULT_MAX) return -1;      /* nothing left to sell */
    long n = current_mult;                            /* the share you already hold */
    /* x2 costs 1,500 -- a first floor's takings.
       x5 is 96,000, about a full descent's income at the current curve.
       x10 is 1.1M, x20 is 10M, x50 is 176M, x100 is 1.47 billion.
       Each share is worth buying only if you intend to keep descending,
       which is the point: it is a wager on your own run. */
    return 1500L * n * n * n;
}
