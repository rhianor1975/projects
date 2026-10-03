#ifndef ITEMS_H
#define ITEMS_H

#include "common.h"

typedef struct {
    char name[40];
    int price;
    /* Healing is flat + a share of max HP. Flat alone stopped mattering:
       the best draught in the game restored 175% of a fresh character's bar
       and 6% of a floor-99 one, so the whole healing economy quietly
       switched off exactly where the attrition got worst. The flat part
       keeps early potions feeling generous; the percentage is what carries
       them to the bottom. */
    int heal;
    int heal_pct;
    int atk_buff, atk_buff_turns;
    int def_buff, def_buff_turns;
    int perm_maxhp;
    bool is_recall;
} ConsumableTemplate;

typedef struct {
    char name[40];
    int price;
    int bonus;
} GearTemplate;

typedef struct {
    char name[40];
    int price;
    int stat; /* AccessoryStat */
    int bonus;
} AccessoryTemplate;

typedef struct {
    char name[40];
    int type; /* RangedType */
    int price;
    int bonus;      /* base damage */
    int ammo_cost;
    int cooldown;
} RangedTemplate;

extern const ConsumableTemplate GENERAL_STOCK[];
extern const int GENERAL_STOCK_COUNT;

extern const ConsumableTemplate APOTHECARY_STOCK[];
extern const int APOTHECARY_STOCK_COUNT;

extern const GearTemplate WEAPON_STOCK[];
extern const int WEAPON_STOCK_COUNT;

extern const GearTemplate ARMOR_STOCK[];
extern const int ARMOR_STOCK_COUNT;

/* ---- upgrades ---------------------------------------------------------
 * The shops stop at +27, and monster attack does not stop at anything: past
 * floor 30 the player's defence curve goes flat while the dungeon's keeps
 * climbing, which is the wall EVALUATION §22 measured. Upgrades are the way
 * out -- an uncapped track applied to whatever is already equipped, so late
 * gold buys survival instead of piling up.
 *
 * Each step adds UPGRADE_STEP to the piece. The price climbs quadratically:
 * cheap enough that the first few are an obvious buy on the way down, steep
 * enough that a deep run is still spending everything it finds.
 */
/* CHANGE C (EVALUATION §58): 5 -> 15. Same defence for roughly a ninth of
   the gold, because the cumulative price curve is cubic. */
#define UPGRADE_STEP 15

/* The same idea applied to the bars rather than the gear. Health is the
   large one on purpose: hits-to-die is HP x (atk+def) / atk^2, and HP is the
   only term in that expression which nothing but levelling currently grows.
   Aether and ammunition are smaller because they buy uptime, not survival. */
#define UPGRADE_HP_STEP     10
#define UPGRADE_AETHER_STEP  3
#define UPGRADE_AMMO_STEP    2

/* What the next upgrade costs, given how many the piece already carries. */
int upgrade_price(int current_plus);

/* What the black market pays for the level you are standing on. Tied to the
   upgrade ladder so a level is always worth roughly the rung a character at
   that level is shopping for -- which makes the price quadratic in level
   while the experience to re-earn one is linear. That asymmetry is the whole
   loop; see docs/EVALUATION.md 63. Zero at level 1. */
long level_sale_price(int level, int deepest_floor);

/* What the market pays for selling `count` levels in one go, starting from
   `level`. The sum of the individual prices, and it exists so the screen and
   the transaction cannot disagree about the total. Clamped so a count that
   would take you below level 1 sells only what there is. */
long level_sale_batch_price(int level, int count, int deepest_floor);

/* How many points of defence `count` levels off `level` gives back. Defence
   is granted on reaching an even level, so this counts the even levels in
   the range being sold. Shared for the same reason as the price. */
int  level_sale_def_loss(int level, int count);

/* How many levels can actually be sold from here -- level-1, never less
   than zero. */
static inline int level_sale_max(int level) { return level > 1 ? level - 1 : 0; }

/* What the gladiator school charges to raise an attribute that currently
   sits at `value`. Steep and quadratic: the thirty feed every derived stat
   in the game, so a point is worth far more than a point of gear. */
int training_price(int value);

/* One price, whether it is being shown or charged. See items.c. */
int discounted_price(const Player *p, int price);

/* What the bank charges to move the multiplier from `current` to
   `current + 1`. Cubic, because what is being bought is itself a multiplier
   on all future income: a linear price would make every share after the
   first one free in practice. */
long bank_price(int current_mult);

/* Which material an upgrade rung wants alongside the gold, and how much.
 *
   This is the whole point of tiering the materials: the deep rungs take
   platinum and then diamond, and neither exists above floor 40. Farming a
   shallow floor for ever therefore buys nothing past +20, and the only way
   to afford going deeper is to have already been deeper. */
int upgrade_material_tier(int current_plus);
int upgrade_material_cost(int current_plus);
#define BANK_MULT_MAX 100

extern const AccessoryTemplate ACCESSORY_STOCK[];
extern const int ACCESSORY_STOCK_COUNT;

extern const RangedTemplate RANGED_STOCK[];
extern const int RANGED_STOCK_COUNT;

/* Fills the player's ring slot if empty, else the trinket slot if empty,
   else replaces the ring (oldest-first). */
void equip_accessory(Player *p, const char *name, int stat, int bonus);

/* Wandering dungeon merchant: a small fixed selection at a markup over
   town prices -- convenience has a cost. */
extern const ConsumableTemplate MERCHANT_STOCK[];
extern const int MERCHANT_STOCK_COUNT;

/* Depth-aware floor loot: better tiers get more common the deeper you go. */
const ConsumableTemplate *pick_floor_loot(int floor_num);

/* Every consumable row in the game, across all five tables, as one flat
   list. Only tests use this -- it exists so "one name means one item" can be
   asserted rather than maintained by hand. */
int consumable_count(void);
const ConsumableTemplate *consumable_get(int idx);

/* Look one up by name, so code that hands out a starting item uses the real
   entry instead of hand-rolling a copy of it. Copies drift: the starting
   healing draught was still a flat 15 HP after shop draughts moved to
   percentages. NULL if there is no such consumable. */
const ConsumableTemplate *consumable_by_name(const char *name);

/* Adds `qty` of the given consumable to the player's inventory, stacking
   with an existing stack of the same name if one exists. Returns false if
   the inventory is full and a new stack could not be created. */
bool give_consumable(Player *p, const ConsumableTemplate *t, int qty);

/* The kit a run opens with, which is not the same kit on every difficulty.
   See EVALUATION §59: the hard modes were dying on floor 1 with no way to
   convert what they had found into anything, and a charm is the difference
   between a bad floor and a lost run. */
void grant_starting_kit(Player *p);

/* Applies one use of the consumable at inventory index `idx`, decrementing
   or removing the stack. Recall charms are refused here -- use
   consume_recall_charm() instead, from the dedicated 'r' command. */
void use_inventory_item(Player *p, int idx);

/* Finds and consumes one recall charm from the inventory. Returns false if
   the player is not carrying one. */
bool consume_recall_charm(Player *p);
int count_recall_charms(const Player *p);

#endif
