/* Invariant checks for things live playtesting can't reliably reach.
 *
 * Everything here links against the real game modules. No side effects --
 * in particular nothing touches the save file on disk, so `make test` is
 * safe to run with a suspended run in progress.
 *
 * Run with `make test`. Exits non-zero on any failure.
 */
#include "../src/common.h"
#include "../src/combat.h"
#include "../src/monsters.h"
#include "../src/gearsets.h"
#include "../src/render.h"
#include "../src/items.h"
#include "../src/features.h"
#include "../src/path.h"
#include "../src/classes.h"
#include <unistd.h>
#include <stdlib.h>
#include "../src/mapgen.h"
#include "../src/spells.h"
#include "../src/items.h"
#include "../src/ranged.h"
#include "../src/save.h"
#include "../src/companions.h"
#include "../src/vision.h"
#include "../src/autoplay.h"
#include "../src/town.h"
#include "../src/dda.h"
#include "../src/bands.h"
#include "../src/quests.h"
#include "../src/autoexplore.h"
#include "../src/ranged.h"
#include "../src/save.h"
#include "../src/companions.h"
#include "../src/spells.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- where the suite is allowed to touch the disk -----------------------
 *
 * `build/test-state`, and nowhere else. Two tests below need a real state
 * directory to write into, and both used to hand it back with unsetenv() --
 * which does not restore the previous value, it removes it, so from the first
 * such test onward state_dir() fell back to $HOME and every later test read
 * and wrote the player's own save files.
 *
 * Nothing was corrupted by that, because no later test happened to write.
 * What it did break was quieter: the district sweeps read the *player's*
 * fallen roster instead of the seeded one, so "every district kind still
 * generates" failed for the barrow on a machine whose owner had never lost a
 * run -- and would have passed, for the wrong reason, on one whose owner had.
 * A test that depends on the developer's home directory is not a test.
 */
#define SCRATCH_STATE_DIR "build/test-state"

static void restore_scratch_state_dir(void) {
    setenv("AETHER_STATE_DIR", SCRATCH_STATE_DIR, 1);
}

/* ---- one scratch world for the tests below ------------------------------
   sizeof(Map) is 65 MB. A `static Map` per test function is the obvious way to
   write these and there are thirty of them, which is two gigabytes of BSS --
   close enough to the linker's 2 GB range limit that adding two more test
   functions broke the build with a RIP-relative fixup error rather than
   anything to do with the tests. The ones added this session share a pair. */
/* ---- one scratch world, shared ----------------------------------------
   sizeof(Map) is 65 MB, and this file used to declare a `static Map m;` inside
   nineteen different test functions. Function-scope statics are still distinct
   objects, so that was 1.2 GB of BSS for nineteen maps that are never alive at
   the same time -- and it put the binary close enough to the linker's 2 GB
   range limit that adding two test functions broke the *link* with a
   RIP-relative fixup error, which looks nothing like its cause.

   One object now, reset at each of the old declaration sites so every test
   still starts from a zeroed map exactly as it did before. The handful of
   tests that genuinely need two maps at once keep their own. */
static Map m;

static Player g_tp;
static Map    g_tm;


static int failures;

static void check(bool cond, const char *what) {
    if (!cond) { printf("  FAIL: %s\n", what); failures++; }
}

/* ---- auto-explore hazard avoidance (the S2-1 mechanism) --------------
   The fix works by choosing between two walkability predicates. Lava and
   miasma are walkable-but-damaging, so a hazard-avoiding path must reject
   them while the permissive fallback still accepts them -- otherwise a
   floor whose only route crosses lava would become unexplorable. */
static void test_hazard_predicates(void) {
    printf("hazard predicates (auto-explore pathing)\n");
    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) m.tiles[y][x].type = TILE_FLOOR;

    m.tiles[1][1].type = TILE_LAVA;
    m.tiles[1][2].type = TILE_MIASMA;
    m.tiles[1][3].type = TILE_WALL;
    m.tiles[1][4].type = TILE_FLOOR;
    m.tiles[1][5].type = TILE_SEALED_DOOR;

    check(is_walkable_player(&m, 1, 1),  "player may step into lava (it's a choice, not a wall)");
    check(is_walkable_player(&m, 2, 1),  "player may step into miasma");
    check(!is_walkable_monster(&m, 1, 1), "hazard-avoiding path rejects lava");
    check(!is_walkable_monster(&m, 2, 1), "hazard-avoiding path rejects miasma");

    check(!is_walkable_player(&m, 3, 1),  "walls block both");
    check(!is_walkable_monster(&m, 3, 1), "walls block both");
    check(is_walkable_player(&m, 4, 1),   "plain floor passable");
    check(is_walkable_monster(&m, 4, 1),  "plain floor passable to hazard-avoiding path too");
    check(!is_walkable_player(&m, 5, 1),  "sealed door blocks until its lever is pulled");
}

/* ---- monster array compaction (S2-2) --------------------------------- */
static void test_compaction(void) {
    printf("monster array compaction\n");
    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));

    for (int i = 0; i < 10; i++) {
        m.monsters[i].alive = (i % 2 == 0);   /* 5 alive, 5 dead */
        m.monsters[i].hp = i + 1;             /* tag to identify survivors */
        m.monsters[i].x = i;
    }
    m.monster_count = 10;

    compact_dead_monsters(&m);
    check(m.monster_count == 5, "corpses are dropped from the array");

    bool all_alive = true, order_kept = true;
    for (int i = 0; i < m.monster_count; i++) {
        if (!m.monsters[i].alive) all_alive = false;
        if (m.monsters[i].x != i * 2) order_kept = false;
    }
    check(all_alive, "only living monsters survive compaction");
    check(order_kept, "relative order of survivors is preserved");

    /* Idempotent: compacting an already-clean array changes nothing. */
    int before = m.monster_count;
    compact_dead_monsters(&m);
    check(m.monster_count == before, "compaction is idempotent");

    /* Degenerate cases must not underflow or crash. */
    m.monster_count = 0;
    compact_dead_monsters(&m);
    check(m.monster_count == 0, "empty array stays empty");

    for (int i = 0; i < 4; i++) { m.monsters[i].alive = false; }
    m.monster_count = 4;
    compact_dead_monsters(&m);
    check(m.monster_count == 0, "an all-dead floor compacts to zero");
}

/* ---- damage model (S1-2) --------------------------------------------- */
static void test_damage_model(void) {
    printf("damage model\n");

    check(damage_after_defence(50, 0, 0) > damage_after_defence(50, 50, 0),
          "armour reduces damage");
    check(damage_after_defence(50, 50, 0) > damage_after_defence(50, 500, 0),
          "more armour reduces it further");

    /* The bug that started all this: armour must never zero a hit out. */
    bool never_zero = true;
    for (int atk = 1; atk <= 200; atk += 7)
        for (int def = 0; def <= 5000; def += 137)
            if (damage_after_defence(atk, def, 0) < 1) never_zero = false;
    check(never_zero, "no attack/armour combination ever deals zero damage");

    /* Monotonic in attack, so a stronger monster is never weaker. */
    bool monotonic = true;
    for (int atk = 1; atk < 200; atk++)
        if (damage_after_defence(atk + 1, 60, 0) < damage_after_defence(atk, 60, 0))
            monotonic = false;
    check(monotonic, "damage rises monotonically with attack");

    /* Degenerate inputs stay sane rather than dividing by zero or going
       negative -- a fully jinxed monster can reach attack 0. */
    check(damage_after_defence(0, 0, 0) >= 1, "zero-attack attacker still grazes");
    check(damage_after_defence(10, -5, 0) >= 1, "negative armour is clamped, not trusted");
    check(damage_after_defence(10, 10, -99) >= 1, "variance can't push damage below 1");
}

/* ---- content sanity --------------------------------------------------- */
static void test_gear_sets(void) {
    printf("gear sets\n");
    for (int b = 0; b < BIOME_COUNT; b++) {
        check(set_name(b) != NULL && set_name(b)[0] != '\0', "every biome set is named");
        int prev = -1;
        for (int slot = 0; slot < RELIC_KIND_COUNT; slot++) {
            const SetGearTemplate *g = set_gear_piece(b, slot);
            if (!g) { check(false, "every (biome, slot) has a piece"); continue; }
            check(g->name[0] != '\0', "every set piece is named");
            check(strlen(g->name) < 40, "set piece names fit the Player name buffers");
            check(g->bonus > 0, "every set piece gives a bonus");
            (void)prev;
        }
        /* Weapon/armour must out-class the best shop gear, or the whole
           point of diving for them is gone. */
        check(set_gear_piece(b, RELIC_WEAPON)->bonus > 0, "set weapon has a bonus");
    }
    /* Depth must actually pay: each biome's armour beats the last. */
    for (int b = 1; b < BIOME_COUNT; b++) {
        check(set_gear_piece(b, RELIC_ARMOR)->bonus > set_gear_piece(b - 1, RELIC_ARMOR)->bonus,
              "deeper biome sets are stronger than shallower ones");
    }
    /* Out-of-range lookups return NULL rather than reading past the table. */
    check(set_gear_piece(-1, 0) == NULL, "negative biome rejected");
    check(set_gear_piece(BIOME_COUNT, 0) == NULL, "out-of-range biome rejected");
    check(set_gear_piece(0, RELIC_KIND_COUNT) == NULL, "out-of-range slot rejected");
}

/* ---- auto-explore's combat decisions -------------------------------
   The hunter picks between melee, a spell and a ranged shot every step, and
   it has to make that choice *without* calling the real cast/fire functions
   -- those report their refusals to the message log, and a hunter that
   guessed wrong would bury the log in "not enough aether" every turn. These
   two predicates are that check, so they are worth pinning. */

static void test_ranged_readiness(void) {
    printf("ranged readiness (auto-explore target choice)\n");

    Player p;
    memset(&p, 0, sizeof(p));

    p.party[0].ranged_type = RANGED_NONE;
    check(!ranged_ready(&p.party[0]), "an empty weapon slot is never ready");
    check(ranged_reach(&p.party[0]) == 0, "an empty weapon slot has no reach");

    p.party[0].ranged_type = RANGED_BOW;
    p.party[0].ranged_ammo = 10; p.party[0].ranged_ammo_cost = 1; p.party[0].ranged_cooldown = 0;
    check(ranged_ready(&p.party[0]), "a loaded, cooled-down bow is ready");
    check(ranged_reach(&p.party[0]) > 1, "a bow outreaches melee");

    p.party[0].ranged_cooldown = 2;
    check(!ranged_ready(&p.party[0]), "a weapon on cooldown is not ready");
    p.party[0].ranged_cooldown = 0;

    p.party[0].ranged_ammo = 0;
    check(!ranged_ready(&p.party[0]), "a weapon without ammo is not ready");
    p.party[0].ranged_ammo = 10;

    /* A grenade searches further than it damages; reporting the search range
       would have the hunter throw at things the blast cannot touch. */
    p.party[0].ranged_type = RANGED_GRENADE;
    check(ranged_reach(&p.party[0]) == 2, "grenade reach is the blast radius, not the search range");

    check(!ranged_ready(NULL), "NULL player is handled by ranged_ready");
    check(ranged_reach(NULL) == 0, "NULL player is handled by ranged_reach");

    printf("  equipped/cooldown/ammo gates and per-type reach hold\n");
}

static void test_spell_pick(void) {
    printf("offensive spell choice (auto-explore)\n");

    /* A level-1 direct-damage spell to choose, and a self-heal to sit beside
       it in the known list as something that must not be chosen. */
    int atk_idx = -1, util_idx = -1;
    for (int i = 0; i < spell_pool_count() && (atk_idx < 0 || util_idx < 0); i++) {
        const SpellTemplate *t = spell_pool_get(i);
        if (!t) continue;
        if (atk_idx < 0 && t->effect == EFFECT_DAMAGE && t->level == 1) atk_idx = i;
        if (util_idx < 0 && t->effect == EFFECT_HEAL_SELF) util_idx = i;
    }
    check(atk_idx >= 0, "the pool contains a level-1 damage spell");
    check(util_idx >= 0, "the pool contains a self-heal");
    if (atk_idx < 0 || util_idx < 0) return;

    const SpellTemplate *atk = spell_pool_get(atk_idx);

    Player p;
    memset(&p, 0, sizeof(p));
    p.party[0].known_spells[0] = util_idx;
    p.party[0].known_spells[1] = atk_idx;
    p.party[0].spell_count = 2;
    p.party[0].aether = 999;

    check(spell_pick_attack_slot(&p.party[0], 1) == 1, "picks the damage spell over the heal");
    check(spell_pick_attack_slot(&p.party[0], 99) == -1, "does not pick at a target beyond reach");

    p.party[0].spell_cd[1] = 3;
    check(spell_pick_attack_slot(&p.party[0], 1) == -1, "does not pick a spell on cooldown");
    p.party[0].spell_cd[1] = 0;

    p.party[0].aether = atk->charge_cost - 1;
    check(spell_pick_attack_slot(&p.party[0], 1) == -1, "does not pick a spell it cannot afford");
    p.party[0].aether = 999;

    /* A purely utility loadout must fall through to the hunter's other
       options rather than casting a shield at a rat. */
    p.party[0].spell_count = 1;
    p.party[0].known_spells[0] = util_idx;
    check(spell_pick_attack_slot(&p.party[0], 1) == -1, "a utility-only loadout yields no attack");

    p.party[0].spell_count = 0;
    check(spell_pick_attack_slot(&p.party[0], 1) == -1, "knowing no spells yields no attack");
    check(spell_pick_attack_slot(NULL, 1) == -1, "NULL player is handled");

    printf("  offensive-only, in-reach, affordable, off-cooldown\n");
}

/* ---- one caster, one school ----------------------------------------
   The guild used to open levels 1-4 to everybody, which meant every
   character browsed the same ~200 spells and the school a class was built
   around barely mattered. Now a caster only ever sees their own. These
   checks are what keep the *list* and the *rule* from drifting apart: the
   guild browses by school, but player_can_learn is still the authority, and
   a mismatch between the two would be a screen offering something the buy
   path refuses. */
static void test_school_locked_spells(void) {
    printf("school-locked spell access\n");

    int total_via_schools = 0;
    for (int school = 0; school < SCHOOL_COUNT; school++) {
        int n = spell_school_count(school);
        check(n > 0, "every school has spells to teach");
        total_via_schools += n;

        /* Every level must be reachable within the school, or a caster hits
           a ceiling their class was supposed to be able to pass. */
        bool level_seen[6] = { false, false, false, false, false, false };
        for (int i = 0; i < n; i++) {
            int idx = spell_school_index(school, i);
            const SpellTemplate *t = spell_pool_get(idx);
            if (!t) { check(false, "school index maps to a real spell"); continue; }
            check(t->school == school, "a school's list contains only that school");
            if (t->level >= 1 && t->level <= 5) level_seen[t->level] = true;
        }
        for (int lv = 1; lv <= 5; lv++)
            check(level_seen[lv], "every school covers every spell level");
    }

    /* The per-school lists must partition the pool -- no spell unreachable,
       none reachable twice. */
    check(total_via_schools == spell_pool_count(),
          "the school lists together cover the whole pool exactly once");

    /* And the rule agrees with the list, in both directions. */
    Player p;
    memset(&p, 0, sizeof(p));
    for (int school = 0; school < SCHOOL_COUNT; school++) {
        p.party[0].magic_school = school;
        int learnable = 0;
        for (int idx = 0; idx < spell_pool_count(); idx++) {
            const SpellTemplate *t = spell_pool_get(idx);
            if (!t) continue;
            bool can = player_can_learn(&p.party[0], idx);
            check(can == (t->school == school),
                  "learnable exactly when the spell is in the caster's school");
            if (can) learnable++;
        }
        check(learnable == spell_school_count(school),
              "the browsable list is exactly the learnable set");
    }

    /* Out-of-range must not read past the tables. */
    check(spell_school_count(-1) == 0, "a negative school teaches nothing");
    check(spell_school_count(SCHOOL_COUNT) == 0, "an out-of-range school teaches nothing");
    check(spell_school_index(0, -1) == -1, "a negative entry has no pool index");
    check(spell_school_index(0, 999999) == -1, "an out-of-range entry has no pool index");
    check(spell_school_index(-1, 0) == -1, "a negative school has no pool index");

    printf("  %d spells, partitioned across %d schools, list == rule\n",
           spell_pool_count(), SCHOOL_COUNT);
}

/* ---- the curated pool ----------------------------------------------
   Every spell is hand-written now, which trades a generator's guarantees
   for a table someone has to keep honest. These are those guarantees,
   restated as tests: no duplicate names anywhere in the game, every school
   covering every level with the same number of choices, names that fit the
   guild's column, and a price curve that actually rises. */
static void test_curated_pool(void) {
    printf("curated spell pool\n");

    int total = spell_pool_count();

    /* Names are the whole point of curating -- a duplicate means two
       different spells are indistinguishable in every list in the game. */
    for (int a = 0; a < total; a++) {
        const SpellTemplate *sa = spell_pool_get(a);
        if (!sa) { check(false, "every pool index resolves"); continue; }

        check(sa->name[0] != '\0', "no spell has an empty name");
        /* The guild lays names out in a 28-column field; longer ones shove
           the level and price columns out of alignment. */
        check(strlen(sa->name) <= 28, "spell names fit the guild's name column");

        for (int b = a + 1; b < total; b++) {
            const SpellTemplate *sb = spell_pool_get(b);
            if (sb && strcmp(sa->name, sb->name) == 0) {
                printf("  FAIL: duplicate spell name \"%s\"\n", sa->name);
                failures++;
            }
        }
    }

    /* Same shape in every school, so no school is a worse pick than another
       purely on how much it offers. */
    int per_school = spell_school_count(0);
    for (int school = 0; school < SCHOOL_COUNT; school++) {
        check(spell_school_count(school) == per_school,
              "every school offers the same number of spells");

        int per_level[6] = { 0, 0, 0, 0, 0, 0 };
        int last_level = 0;
        for (int i = 0; i < spell_school_count(school); i++) {
            const SpellTemplate *t = spell_pool_get(spell_school_index(school, i));
            if (!t) continue;
            check(t->level >= 1 && t->level <= 5, "spell levels are within 1-5");
            if (t->level >= 1 && t->level <= 5) per_level[t->level]++;
            /* The guild browses in table order, so table order *is* the
               display order -- it has to already be sorted by level. */
            check(t->level >= last_level, "a school's spells are listed in level order");
            last_level = t->level;

            check(t->magnitude >= 1 && t->charge_cost >= 1 && t->cooldown >= 1,
                  "no spell has a degenerate stat");
            check(t->learn_price > 0, "every spell has a price");
        }
        for (int lv = 1; lv <= 5; lv++)
            check(per_level[lv] == per_level[1], "each level offers the same count");
    }

    /* Price has to rise with level, or the browse list stops meaning
       anything -- the cheapest level-N spell must beat the dearest N-1. */
    for (int school = 0; school < SCHOOL_COUNT; school++) {
        int cheapest[6] = {0}, dearest[6] = {0};
        for (int i = 0; i < spell_school_count(school); i++) {
            const SpellTemplate *t = spell_pool_get(spell_school_index(school, i));
            if (!t || t->level < 1 || t->level > 5) continue;
            if (cheapest[t->level] == 0 || t->learn_price < cheapest[t->level])
                cheapest[t->level] = t->learn_price;
            if (t->learn_price > dearest[t->level]) dearest[t->level] = t->learn_price;
        }
        for (int lv = 2; lv <= 5; lv++)
            check(cheapest[lv] > dearest[lv - 1],
                  "every spell of a level costs more than any of the level below");
    }

    /* And the top of the curve should read as a real investment against the
       best thing gold can otherwise buy (a 1000g weapon in the armoury). */
    int dearest_l5 = 0;
    for (int i = 0; i < spell_school_count(0); i++) {
        const SpellTemplate *t = spell_pool_get(spell_school_index(0, i));
        if (t && t->level == 5 && t->learn_price > dearest_l5) dearest_l5 = t->learn_price;
    }
    check(dearest_l5 > 1000, "a level-5 spell outprices the best weapon in the armoury");

    printf("  %d spells, %d per school, unique names, level-ordered, rising prices\n",
           total, per_school);
}

/* ---- one name, one item --------------------------------------------
   Consumables are declared in five tables (two shops, the merchant, three
   loot tiers) and the same drink appears in several of them, with nothing
   structural keeping the copies in step. Adding the heal-percentage field
   they drifted immediately: a looted Superior Draught healed twice what a
   bought one did, same name, same screen. */
static void test_consumables_consistent(void) {
    printf("consumable tables\n");

    int n = consumable_count();
    check(n > 0, "the consumable tables are reachable");

    for (int a = 0; a < n; a++) {
        const ConsumableTemplate *x = consumable_get(a);
        if (!x) { check(false, "every consumable index resolves"); continue; }

        /* Healing must scale, or it stops mattering exactly where attrition
           gets worst -- that was the whole point of adding heal_pct. */
        if (x->heal > 0)
            check(x->heal_pct > 0, "a healing item restores a share of max HP, not just a flat amount");

        for (int b = a + 1; b < n; b++) {
            const ConsumableTemplate *y = consumable_get(b);
            if (!y || strcmp(x->name, y->name) != 0) continue;
            bool same = x->heal == y->heal && x->heal_pct == y->heal_pct
                     && x->atk_buff == y->atk_buff && x->atk_buff_turns == y->atk_buff_turns
                     && x->def_buff == y->def_buff && x->def_buff_turns == y->def_buff_turns
                     && x->perm_maxhp == y->perm_maxhp && x->is_recall == y->is_recall;
            if (!same) {
                printf("  FAIL: \"%s\" differs between tables (%d+%d%% vs %d+%d%%)\n",
                       x->name, x->heal, x->heal_pct, y->heal, y->heal_pct);
                failures++;
            }
        }
    }
    printf("  %d rows, duplicates agree, healing scales with max HP\n", n);
}

/* ---- regeneration ----------------------------------------------------
   The game has no innate healing over time, which is what made attrition the
   real difficulty. The Ouroboros Coil is the only source, so the rule that
   matters is that it does nothing until worn -- and that it can't quietly
   cancel a poison. */
static void test_regen(void) {
    printf("regeneration accessory\n");

    Player p;
    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 200; p.party[0].hp = 100;
    p.party[0].ring_stat = ACC_NONE; p.party[0].trinket_stat = ACC_NONE;

    hero_tick_regen(&p, hero_driven(&p));
    check(p.party[0].hp == 100, "no regeneration without an accessory that grants it");

    p.party[0].ring_stat = ACC_REGEN; p.party[0].ring_bonus = 6;
    hero_tick_regen(&p, hero_driven(&p));
    check(p.party[0].hp == 106, "a worn coil regenerates its rated amount");

    /* Must not overheal. */
    p.party[0].hp = p.party[0].maxhp - 2;
    hero_tick_regen(&p, hero_driven(&p));
    check(p.party[0].hp == p.party[0].maxhp, "regeneration stops at full, it does not overshoot");

    /* Must not fight a damage-over-time effect. */
    p.party[0].hp = 100; p.party[0].poison_turns_left = 3;
    hero_tick_regen(&p, hero_driven(&p));
    check(p.party[0].hp == 100, "regeneration is suppressed while poisoned");
    p.party[0].poison_turns_left = 0;

    /* Must not resurrect. */
    p.party[0].hp = 0;
    hero_tick_regen(&p, hero_driven(&p));
    check(p.party[0].hp == 0, "regeneration never revives a dead character");

    /* And it has to be reachable: something in the shop must sell it. */
    bool sold = false;
    int price = 0;
    for (int i = 0; i < ACCESSORY_STOCK_COUNT; i++)
        if (ACCESSORY_STOCK[i].stat == ACC_REGEN) { sold = true; price = ACCESSORY_STOCK[i].price; }
    check(sold, "the armoury stocks a regeneration accessory");
    check(price >= 5000, "it is priced as a late-game capstone, not a purchase");

    printf("  off by default, capped, poison-suppressed, stocked at %dg\n", price);
}

/* ---- archetypes ------------------------------------------------------
   Character creation browses classes one archetype at a time, so the groups
   have to partition the table: a class in none of them is unpickable, and a
   class in two would be offered twice. Neither is visible in review -- the
   archetype is one field on a 100-row data table. */
static void test_archetypes(void) {
    printf("class archetypes\n");

    int counted = 0;
    for (int a = 0; a < ARCHETYPE_COUNT; a++) {
        int n = archetype_class_count(a);
        check(n > 0, "every archetype offers at least one class");
        counted += n;

        check(archetype_name(a)[0] != '\0', "every archetype is named");
        check(archetype_blurb(a)[0] != '\0', "every archetype explains itself");

        for (int i = 0; i < n; i++) {
            int idx = archetype_class_index(a, i);
            check(idx >= 0 && idx < NUM_CLASSES, "an archetype entry maps to a real class");
            if (idx < 0 || idx >= NUM_CLASSES) continue;
            check(CLASS_TABLE[idx].archetype == a,
                  "an archetype's list contains only classes of that archetype");
        }
        check(archetype_class_index(a, n) == -1, "one past the end has no class");
        check(archetype_class_index(a, -1) == -1, "a negative entry has no class");
    }

    check(counted == NUM_CLASSES,
          "the archetypes together cover every class exactly once");

    /* Belt and braces: each class must be reachable through exactly one
       archetype list. */
    for (int c = 0; c < NUM_CLASSES; c++) {
        int seen = 0;
        for (int a = 0; a < ARCHETYPE_COUNT; a++)
            for (int i = 0; i < archetype_class_count(a); i++)
                if (archetype_class_index(a, i) == c) seen++;
        if (seen != 1) {
            printf("  FAIL: %s is reachable from %d archetype lists, want 1\n",
                   CLASS_TABLE[c].name, seen);
            failures++;
        }
    }

    check(archetype_class_count(-1) == 0, "a negative archetype offers nothing");
    check(archetype_class_count(ARCHETYPE_COUNT) == 0, "an out-of-range archetype offers nothing");

    printf("  %d classes partitioned across %d archetypes\n", NUM_CLASSES, ARCHETYPE_COUNT);
}

/* ---- ammo has to be spendable ---------------------------------------
   The AM gauge was decorative for the whole life of the feature: regen was
   one point per turn, every weapon costs 1-3 and cools down in 1-5, so the
   pool refilled at least as fast as the cooldown allowed it to be spent and
   the bar could not fall. A resource that cannot be depleted is not a
   resource, and it makes the Precision-scaled ammo pool do nothing.

   The rule: firing as fast as a weapon's cooldown permits must lose ammo.
   Over one cycle you wait `cooldown` turns, regaining cooldown/REGEN_TURNS,
   and spend `ammo_cost` -- so the cost must exceed that, which without
   floating point is cost * REGEN_TURNS > cooldown. */
static void test_ammo_is_spendable(void) {
    printf("ranged ammo economy\n");

    int worst_shots = 1 << 30;
    const char *worst = "";

    for (int i = 0; i < RANGED_STOCK_COUNT; i++) {
        const RangedTemplate *t = &RANGED_STOCK[i];

        check(t->ammo_cost >= 1, "every weapon costs at least one ammo to fire");
        check(t->cooldown >= 1, "every weapon has a cooldown");

        bool drains = t->ammo_cost * RANGED_AMMO_REGEN_TURNS > t->cooldown;
        if (!drains) {
            printf("  FAIL: %s regenerates as fast as it fires (cost %d, cooldown %d)\n",
                   t->name, t->ammo_cost, t->cooldown);
            failures++;
            continue;
        }

        /* Sustained shots from a mid-range pool, as a sanity check that the
           drain isn't so steep the weapon is unusable. */
        int net_x4 = t->ammo_cost * RANGED_AMMO_REGEN_TURNS - t->cooldown;
        int shots = 12 * RANGED_AMMO_REGEN_TURNS / net_x4;
        if (shots < worst_shots) { worst_shots = shots; worst = t->name; }
        check(shots >= 4, "a weapon lasts at least a few shots on a modest pool");
    }

    printf("  %d weapons, all drain under sustained fire (tightest: %s, ~%d shots)\n",
           RANGED_STOCK_COUNT, worst, worst_shots);
}

/* ---- lingering on a floor -------------------------------------------
   Standing on one floor doubles its population every 1000 turns. The two
   things that can go wrong are both invisible until they bite: a cleared
   floor doubling zero and staying empty forever, and a doubling walking off
   the end of the fixed monster array. */
static void test_floor_doubling(void) {
    printf("floor population doubling\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    memset(&p, 0, sizeof(p));
    p.difficulty = DIFFICULTY_NORMAL;
    p.party[0].x = 1; p.party[0].y = 1;

    /* A bare open floor -- no generator, so the numbers are the mechanic's
       and not map generation's. */
    memset(&m, 0, sizeof(m));
    m.floor_num = 10;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            m.tiles[y][x].type = TILE_FLOOR;

    int live = 0;
    for (int i = 0; i < 40; i++) {
        m.monsters[m.monster_count++] = make_monster_for_floor(10, 60 + (i % 30), 40);
        live++;
    }
    (void)live;

    int before = m.monster_count;
    int span = monster_doubling_interval();
    for (int t = 0; t < span; t++) maybe_respawn_monsters(&m, &p);
    int after = m.monster_count;
    check(after >= before * 2,
          "a doubling interval of standing still at least doubles what is on it");

    /* A cleared floor must still repopulate -- otherwise clearing a floor and
       camping it is the safest thing in the game, which is backwards. */
    memset(&m, 0, sizeof(m));
    m.floor_num = 10;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            m.tiles[y][x].type = TILE_FLOOR;
    /* Counted in respawn *intervals*, not raw turns: the interval scales with
       the map, so a literal turn count silently stops meaning anything the
       moment the floor size changes. */
    int wave = monster_respawn_interval();
    for (int t = 0; t < wave * 40; t++) maybe_respawn_monsters(&m, &p);
    check(m.monster_count > 0, "a cleared floor still repopulates when camped on");

    /* And it must never run past the array. */
    for (int t = 0; t < wave * 800; t++) maybe_respawn_monsters(&m, &p);
    check(m.monster_count <= MAX_MONSTERS, "doubling never exceeds MAX_MONSTERS");
    check(m.monster_count > 100, "a long camp really does fill the floor up");

    printf("  doubles, refills a cleared floor, capped at %d\n", MAX_MONSTERS);
}

/* ---- the Inn snapshot ------------------------------------------------
   Dying restores the character banked at the Inn. If that restore is wrong
   the player does not get an error, they get a subtly different character --
   the wrong gold, the wrong spells, someone else entirely. Round-tripping it
   is the only way to know. */
static void test_inn_snapshot(void) {
    printf("inn snapshot\n");

    /* Keep the test out of the real save directory. */
    setenv("AETHER_STATE_DIR", "/tmp/aether-inn-test", 1);
    delete_inn_snapshot();

    check(!has_inn_snapshot(), "no snapshot before anyone rests");

    Player banked;
    memset(&banked, 0, sizeof(banked));
    banked.party[0].level = 27; banked.party[0].maxhp = 240; banked.party[0].hp = 240;
    banked.gold = 1234; banked.party[0].class_id = 5; banked.difficulty = DIFFICULTY_HARD;
    banked.party[0].base_atk = 31; banked.party[0].base_def = 14;
    banked.deepest_floor = 42; banked.floor_entries = 55;
    banked.party[0].spell_count = 3;
    banked.party[0].known_spells[0] = 7; banked.party[0].known_spells[1] = 19; banked.party[0].known_spells[2] = 88;
    banked.party[0].ring_stat = ACC_REGEN; banked.party[0].ring_bonus = 6;
    banked.inv_count = 1; banked.inventory[0].count = 4;
    strncpy(banked.party[0].name, "Testwright", sizeof(banked.party[0].name) - 1);
    strncpy(banked.party[0].weapon_name, "Star-iron Edge", sizeof(banked.party[0].weapon_name) - 1);

    save_inn_snapshot(&banked);
    check(has_inn_snapshot(), "resting leaves a snapshot behind");

    /* Die: the live character is wrecked, then restored from the bank. */
    Player dead;
    memset(&dead, 0, sizeof(dead));
    dead.party[0].level = 1; dead.gold = 0;
    check(load_inn_snapshot(&dead), "a banked character loads back");

    check(dead.party[0].level == banked.party[0].level, "level survives the round trip");
    check(dead.gold == banked.gold, "gold survives the round trip");
    check(dead.party[0].maxhp == banked.party[0].maxhp, "max HP survives the round trip");
    check(dead.party[0].base_atk == banked.party[0].base_atk && dead.party[0].base_def == banked.party[0].base_def,
          "attack and defence survive the round trip");
    check(dead.party[0].class_id == banked.party[0].class_id, "the class survives the round trip");
    check(dead.difficulty == banked.difficulty, "the difficulty survives the round trip");
    check(dead.deepest_floor == banked.deepest_floor, "the depth record survives");
    check(dead.floor_entries == banked.floor_entries, "run escalation survives");
    check(dead.party[0].spell_count == banked.party[0].spell_count &&
          dead.party[0].known_spells[2] == banked.party[0].known_spells[2], "the spellbook survives");
    check(dead.party[0].ring_stat == banked.party[0].ring_stat && dead.party[0].ring_bonus == banked.party[0].ring_bonus,
          "worn accessories survive");
    check(dead.inv_count == banked.inv_count && dead.inventory[0].count == 4,
          "the pack survives");
    check(strcmp(dead.party[0].name, banked.party[0].name) == 0, "the name survives");
    check(strcmp(dead.party[0].weapon_name, banked.party[0].weapon_name) == 0, "the weapon survives");
    check(memcmp(&dead, &banked, sizeof(Player)) == 0,
          "the restored character is byte-identical to the banked one");

    /* Surviving a death must not consume the bank -- the Inn is a checkpoint,
       not a one-shot revive. */
    check(has_inn_snapshot(), "the snapshot stands after being restored from");

    /* A new character must not be able to die into the previous one. */
    delete_inn_snapshot();
    check(!has_inn_snapshot(), "retiring a character clears the bank");
    Player nobody;
    memset(&nobody, 0, sizeof(nobody));
    check(!load_inn_snapshot(&nobody), "nothing loads once the bank is cleared");

    /* Garbage must be refused rather than half-loaded. */
    FILE *f = fopen("/tmp/aether-inn-test/.aether_descent_inn", "wb");
    if (f) { fputs("not a snapshot", f); fclose(f); }
    check(!has_inn_snapshot(), "a corrupt snapshot is rejected");
    check(!load_inn_snapshot(&nobody), "a corrupt snapshot never loads");

    delete_inn_snapshot();
    restore_scratch_state_dir();
    printf("  round-trips a character exactly, survives reuse, rejects garbage\n");
}

/* ---- archetypes arrive equipped for their role ----------------------
   A Marksman with no bow is just a worse Skirmisher, and an Arcanist who
   cannot cast until they can afford the guild is not an Arcanist yet. These
   are promises the archetype screen makes to the player before they commit
   a run, so they hold for every class in the group, not just the first. */
static void test_archetype_starting_kit(void) {
    printf("archetype starting kit\n");

    long fight_gear = 0, fight_n = 0, caster_gear = 0, caster_n = 0;

    for (int a = 0; a < ARCHETYPE_COUNT; a++) {
        for (int i = 0; i < archetype_class_count(a); i++) {
            int cls = archetype_class_index(a, i);
            if (cls < 0) continue;

            Player p;
            memset(&p, 0, sizeof(p));
            apply_class_to_player(&p, cls);

            if (a == ARCH_MARKSMAN)
                check(p.party[0].ranged_type != RANGED_NONE,
                      "every Marksman starts with something to shoot");

            if (a == ARCH_ARCANIST) {
                check(p.party[0].spell_count > 0, "every Arcanist starts knowing a spell");
                if (p.party[0].spell_count > 0) {
                    const SpellTemplate *t = spell_pool_get(p.party[0].known_spells[0]);
                    check(t != NULL, "the starting spell is a real spell");
                    if (t) {
                        check(t->school == p.party[0].magic_school,
                              "the starting spell is from the caster's own school");
                        check(t->level == 1, "the starting spell is a level-1 spell");
                    }
                }
            }

            /* Nobody starts unarmed or unarmoured, whatever their attributes. */
            check(p.party[0].weapon_bonus > 0 && p.party[0].armor_bonus > 0,
                  "every character starts with real gear");

            if (a == ARCH_VANGUARD || a == ARCH_SKIRMISHER) {
                fight_gear += p.party[0].weapon_bonus + p.party[0].armor_bonus; fight_n++;
            } else if (a == ARCH_ARCANIST) {
                caster_gear += p.party[0].weapon_bonus + p.party[0].armor_bonus; caster_n++;
            }
        }
    }

    /* The trade the design rests on: front-liners are better equipped than
       casters, who are paying for a spell instead. */
    check(fight_n > 0 && caster_n > 0, "both groups have classes to compare");
    if (fight_n > 0 && caster_n > 0) {
        check(fight_gear / fight_n > caster_gear / caster_n,
              "fighting archetypes start better equipped than casters");
        printf("  fighters avg gear %ld, casters %ld\n",
               fight_gear / fight_n, caster_gear / caster_n);
    }
}

/* ---- hired companions -----------------------------------------------
   The party is the most expensive thing in the game -- ten million for the
   fifth -- so the economy has to be exact, and a hero who dies has to end up
   back on the Tavern's books rather than vanishing with the money. */
static void test_companions(void) {
    printf("hired companions\n");

    /* Prices are ten times the last, five deep. */
    long expect[MAX_COMPANIONS] = { 1000, 10000, 100000, 1000000, 10000000 };
    for (int i = 0; i < MAX_COMPANIONS; i++)
        check(companion_hire_cost(i) == expect[i], "each hire costs ten times the last");
    for (int i = 0; i < MAX_COMPANIONS; i++)
        check(companion_rehire_cost(i) < companion_hire_cost(i),
              "a fallen hero comes back cheaper than they cost");

    Player p;
    memset(&p, 0, sizeof(p));
    p.party[0].level = 5; p.tavern_seed = 12345u; p.gold = 100000000;
    p.party[0].maxhp = 70; p.party[0].base_atk = 12; p.party[0].weapon_bonus = 6;
    p.party[0].base_def = 8; p.party[0].armor_bonus = 4;

    /* The roster is regenerated from the seed, not stored -- so it has to be
       stable, or the person you were looking at changes while you read. */
    Hero a, b;
    tavern_candidate(&p, 7, &a);
    tavern_candidate(&p, 7, &b);
    check(strcmp(a.name, b.name) == 0 && a.base_atk == b.base_atk,
          "the same roster slot yields the same hero every time");
    check(a.level == p.party[0].level, "hires start at the player's level");
    check(a.name[0] != '\0', "every candidate is named");
    check(a.personality >= 0 && a.personality < PERSONALITY_COUNT,
          "every candidate has a personality");
    check(a.archetype >= 0 && a.archetype < ARCHETYPE_COUNT,
          "every candidate has an archetype");
    check(a.class_id >= 0 && a.class_id < NUM_CLASSES,
          "every candidate is one of the hundred classes");
    check(a.archetype == CLASS_TABLE[a.class_id].archetype,
          "and their role always agrees with their class");
    check(companion_class_name(&a)[0] != '\0', "the class has a name to show");

    /* Archetype has to mean something, or it is a word on a list. Sample
       across seeds: the ones who stand in front should out-live the ones who
       don't, and the casters should hit hardest. */
    long tough_hp = 0, frail_hp = 0, tough_n = 0, frail_n = 0;
    long caster_atk = 0, vanguard_atk = 0, caster_n = 0, vanguard_n = 0;
    int seen_arch = 0;
    for (unsigned int seed = 1; seed <= 60; seed++) {
        Player q; memset(&q, 0, sizeof(q));
        q.party[0].level = 5; q.tavern_seed = seed;
        q.party[0].maxhp = 70; q.party[0].base_atk = 12; q.party[0].weapon_bonus = 6;
        q.party[0].base_def = 8; q.party[0].armor_bonus = 4;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero c;
            tavern_candidate(&q, i, &c);
            seen_arch |= 1 << c.archetype;
            if (c.archetype == ARCH_VANGUARD || c.archetype == ARCH_SURVIVOR) {
                tough_hp += c.maxhp; tough_n++;
                if (c.archetype == ARCH_VANGUARD) { vanguard_atk += c.base_atk; vanguard_n++; }
            }
            if (c.archetype == ARCH_ARCANIST) {
                frail_hp += c.maxhp; frail_n++;
                caster_atk += c.base_atk; caster_n++;
            }
        }
    }
    check(seen_arch == (1 << ARCHETYPE_COUNT) - 1, "all seven archetypes show up on the roster");

    /* Traits used to be bit-slices of one hash, which correlated them: a
       roster came out eight Artificers deep with four heroes sharing a name.
       Both properties are now pinned. */
    int arch_count[ARCHETYPE_COUNT] = {0};
    int worst_dupe_names = 0;
    for (unsigned int seed = 1; seed <= 40; seed++) {
        Player q; memset(&q, 0, sizeof(q));
        q.party[0].level = 3; q.tavern_seed = seed;
        q.party[0].maxhp = 50; q.party[0].base_atk = 9; q.party[0].weapon_bonus = 3;
        q.party[0].base_def = 6; q.party[0].armor_bonus = 2;
        char seen_names[TAVERN_ROSTER][32];
        int dupes = 0;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero c;
            tavern_candidate(&q, i, &c);
            arch_count[c.archetype]++;
            snprintf(seen_names[i], sizeof(seen_names[i]), "%s", c.name);
            for (int j = 0; j < i; j++) if (strcmp(seen_names[i], seen_names[j]) == 0) dupes++;
        }
        if (dupes > worst_dupe_names) worst_dupe_names = dupes;
    }
    check(worst_dupe_names == 0, "no two heroes on a roster share a name");
    /* Roles are drawn first and evenly, then a class from inside the role --
       so a tavern is a bit of everything rather than a third Skirmishers,
       which is what drawing straight from the hundred classes would give. */
    int lo = arch_count[0], hi = arch_count[0];
    for (int i = 1; i < ARCHETYPE_COUNT; i++) {
        if (arch_count[i] < lo) lo = arch_count[i];
        if (arch_count[i] > hi) hi = arch_count[i];
    }
    check(lo > 0 && hi < lo * 2,
          "roles are spread evenly across the roster, not weighted by class count");

    /* And every class the roster hands out really is one of that role's. */
    for (unsigned int seed = 200; seed < 210; seed++) {
        Player q; memset(&q, 0, sizeof(q));
        q.party[0].level = 3; q.tavern_seed = seed; q.party[0].maxhp = 50; q.party[0].base_atk = 9;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero c;
            tavern_candidate(&q, i, &c);
            bool in_role = false;
            for (int k = 0; k < archetype_class_count(c.archetype); k++)
                if (archetype_class_index(c.archetype, k) == c.class_id) in_role = true;
            check(in_role, "a hire's class belongs to the role they were drawn as");
        }
    }

    int temper_count[PERSONALITY_COUNT] = {0};
    for (unsigned int seed = 100; seed < 140; seed++) {
        Player q; memset(&q, 0, sizeof(q));
        q.party[0].level = 3; q.tavern_seed = seed;
        q.party[0].maxhp = 50; q.party[0].base_atk = 9; q.party[0].weapon_bonus = 3;
        q.party[0].base_def = 6; q.party[0].armor_bonus = 2;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero c;
            tavern_candidate(&q, i, &c);
            temper_count[c.personality]++;
        }
    }
    int tlo = temper_count[0], thi = temper_count[0];
    for (int i = 1; i < PERSONALITY_COUNT; i++) {
        if (temper_count[i] < tlo) tlo = temper_count[i];
        if (temper_count[i] > thi) thi = temper_count[i];
    }
    check(tlo > 0 && thi < tlo * 2, "temperaments are spread evenly too");
    check(tough_n > 0 && frail_n > 0 && caster_n > 0 && vanguard_n > 0, "enough of each to compare");
    check(tough_hp / tough_n > frail_hp / frail_n,
          "front-line archetypes carry more health than casters");
    check(caster_atk / caster_n > vanguard_atk / vanguard_n,
          "casters hit harder than the ones who stand in front");
    check(a.hp == a.maxhp, "a fresh candidate is at full health after the tilt");

    /* A hire has to match what the player actually brings, not their level.
       Two characters at the same level, one kitted out and one not, should
       not be offered the same heroes. */
    Player rich, poor;
    memset(&rich, 0, sizeof(rich));
    rich.party[0].level = 50; rich.tavern_seed = 909u;
    rich.party[0].maxhp = 400; rich.party[0].base_atk = 40; rich.party[0].weapon_bonus = 55;
    rich.party[0].set_bonus_atk = 12; rich.party[0].base_def = 30; rich.party[0].armor_bonus = 40;
    rich.party[0].spell_count = 15; rich.party[0].spell_power_pct = 60;
    poor = rich;
    poor.party[0].maxhp = 120; poor.party[0].weapon_bonus = 2; poor.party[0].set_bonus_atk = 0;
    poor.party[0].armor_bonus = 1; poor.party[0].spell_count = 0; poor.party[0].spell_power_pct = 0;

    long rich_atk = 0, poor_atk = 0, rich_hp = 0, poor_hp = 0;
    for (int i = 0; i < TAVERN_ROSTER; i++) {
        Hero r, o;
        tavern_candidate(&rich, i, &r);
        tavern_candidate(&poor, i, &o);
        rich_atk += r.base_atk; poor_atk += o.base_atk;
        rich_hp  += r.maxhp;    poor_hp  += o.maxhp;
        check(r.level == rich.party[0].level, "a hire still starts at the player's level");
    }
    check(rich_atk > poor_atk * 2,
          "a well-equipped player is offered heroes to match, not level-alikes");
    check(rich_hp > poor_hp, "and tougher ones as well");

    /* A fighter who hires a caster should get a caster of their own standing
       -- proficiency in their own discipline, not the fighter's stats in a
       robe. The player here has no magic at all; the Arcanists on the list
       must still out-hit the front-liners. */
    Player fighter;
    memset(&fighter, 0, sizeof(fighter));
    fighter.party[0].level = 30; fighter.tavern_seed = 4242u;
    fighter.party[0].maxhp = 300; fighter.party[0].base_atk = 35; fighter.party[0].weapon_bonus = 45;
    fighter.party[0].base_def = 28; fighter.party[0].armor_bonus = 30;
    long mage_atk = 0, tank_atk = 0; int mage_n = 0, tank_n = 0;
    for (unsigned int seed = 1; seed <= 40; seed++) {
        fighter.tavern_seed = seed;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero c;
            tavern_candidate(&fighter, i, &c);
            if (c.archetype == ARCH_ARCANIST) { mage_atk += c.base_atk; mage_n++; }
            if (c.archetype == ARCH_VANGUARD) { tank_atk += c.base_atk; tank_n++; }
        }
    }
    check(mage_n > 0 && tank_n > 0, "both roles turn up on a fighter's roster");
    check(mage_atk / mage_n > tank_atk / tank_n,
          "a fighter's hired casters are proficient casters, not spare fighters");

    /* Fill the party, and check it stops at five. */
    int hired = 0;
    for (int i = 0; i < TAVERN_ROSTER; i++) if (companion_hire(&p, i)) hired++;
    check(hired == MAX_COMPANIONS, "the party fills to exactly five");
    check(companion_count(&p) == MAX_COMPANIONS, "and reports itself full");
    check(!companion_hire(&p, TAVERN_ROSTER - 1), "a sixth hire is refused");

    /* The purse actually paid for them. */
    long spent = 100000000 - p.gold;
    long total = 0;
    for (int i = 0; i < MAX_COMPANIONS; i++) total += companion_hire_cost(i);
    check(spent == total, "the gold taken matches the price list exactly");

    /* A pauper hires nobody. */
    Player broke;
    memset(&broke, 0, sizeof(broke));
    broke.party[0].level = 1; broke.tavern_seed = 5u; broke.gold = 10;
    check(!companion_hire(&broke, 0), "no coin, no hero");
    check(companion_count(&broke) == 0, "and no slot is taken");

    /* Death returns them to the Tavern, at a discount, with the slot free. */
    Hero *victim = &p.party[(0) + 1];
    int roster = victim->roster_idx;
    check(tavern_is_hired(&p, roster), "a hired hero is marked as out with you");
    hero_take_damage(&p, victim, victim->hp + 100);
    check(!victim->alive, "enough damage kills a companion");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    m.floor_num = 3;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) m.tiles[y][x].type = TILE_FLOOR;
    companions_take_turn(&p, &m);          /* reaps the fallen */

    check(!tavern_is_hired(&p, roster), "a fallen hero is no longer out with you");
    check(tavern_has_fallen(&p, roster), "and is back on the Tavern's books");
    check(companion_count(&p) == MAX_COMPANIONS - 1, "their party slot frees up");

    /* Letting one go: the slot frees, the money does not come back, and a
       different person is sitting in that chair afterwards. */
    Hero keep;
    int live_idx = -1;
    for (int i = 0; i < TAVERN_ROSTER; i++) if (tavern_is_hired(&p, i)) { live_idx = i; break; }
    check(live_idx >= 0, "someone is still out with you to release");
    tavern_candidate(&p, live_idx, &keep);
    int before_count = companion_count(&p);
    int before_gold  = p.gold;

    check(tavern_can_release(&p, live_idx), "a hero of yours can be let go");
    check(companion_release(&p, live_idx), "and releasing them succeeds");
    check(companion_count(&p) == before_count - 1, "releasing frees the party slot");
    /* Half the fee back. This used to assert that releasing refunded
       nothing, which meant changing your mind about a hire cost the entire
       price -- 1,000, then 10,000, then 100,000 gold a slot. A hero who is
       still walking can be paid off; one who died down there cannot, and
       that case is checked separately below. */
    check(p.gold > before_gold, "releasing a living hire hands half the fee back");
    {
        long expect = companion_hire_cost(before_count - 1) / 2;
        check(p.gold - before_gold == (int)expect, "and it is exactly half");
    }
    check(!tavern_is_hired(&p, live_idx), "they are no longer out with you");
    check(!tavern_has_fallen(&p, live_idx), "and they are not on the fallen list either");

    Hero replacement;
    tavern_candidate(&p, live_idx, &replacement);
    check(strcmp(replacement.name, keep.name) != 0,
          "a different hero takes the released one's place on the roster");
    check(replacement.name[0] != '\0', "and the replacement is a real, named hero");

    /* Everyone above them on the list is untouched -- only that line rerolls.
       (Names dedupe against earlier entries, so a *later* line may re-roll if
       the new arrival happens to take its name; earlier ones never move.) */
    Player mirror = p;
    mirror.tavern_reroll[live_idx]--;                 /* rewind just that line */
    for (int i = 0; i < live_idx; i++) {
        Hero before, after;
        tavern_candidate(&mirror, i, &before);
        tavern_candidate(&p, i, &after);
        check(strcmp(before.name, after.name) == 0 && before.archetype == after.archetype,
              "releasing one hero does not disturb the roster above them");
    }

    /* The fallen can be struck off too -- you are not stuck advertising a
       grave you don't intend to buy back. */
    check(tavern_can_release(&p, roster), "a fallen hero of yours can be struck off");
    Hero dead_before;
    tavern_candidate(&p, roster, &dead_before);
    check(companion_release(&p, roster), "and striking them off succeeds");
    check(!tavern_has_fallen(&p, roster), "they leave the fallen list");
    Hero dead_after;
    tavern_candidate(&p, roster, &dead_after);
    check(strcmp(dead_before.name, dead_after.name) != 0,
          "and a different hero takes their chair as well");

    /* A stranger is not yours to dismiss -- otherwise the roster is a free
       reroll button until it hands you five heroes you like. */
    int stranger = -1;
    for (int i = 0; i < TAVERN_ROSTER; i++)
        if (!tavern_is_hired(&p, i) && !tavern_has_fallen(&p, i)) { stranger = i; break; }
    check(stranger >= 0, "there is a stranger on the list");
    Hero s_before;
    tavern_candidate(&p, stranger, &s_before);
    check(!tavern_can_release(&p, stranger), "a stranger cannot be released");
    check(!companion_release(&p, stranger), "and trying does nothing");
    Hero s_after;
    tavern_candidate(&p, stranger, &s_after);
    check(strcmp(s_before.name, s_after.name) == 0, "so a stranger's line never rerolls");

    /* Levelling is theirs, off their own kills -- and it is the same levelling
       everybody else gets. There were two curves once, one for the character
       and a proportional one for hires; there is one now, credited to whoever
       landed the blow. */
    Hero solo;
    tavern_candidate(&p, 3, &solo);
    int lv0 = solo.level, hp0 = solo.maxhp, atk0 = solo.base_atk;
    grant_xp(&p, &solo, 100000);
    check(solo.maxhp > hp0 && solo.base_atk > atk0,
          "levelling raises a companion's health and attack");
    check(solo.level > lv0, "a companion levels off its own experience");

    /* And the character, given the same experience from the same call, grows
       by the same rule -- which is the property the two curves denied. */
    Hero twin = solo;
    twin.level = lv0; twin.maxhp = hp0; twin.base_atk = atk0; twin.xp = 0;
    twin.xp_next = 20 + (lv0 - 1) * 15;
    grant_xp(&p, &twin, 100000);
    check(twin.level == solo.level && twin.maxhp == solo.maxhp,
          "and a hire and the character grow by the same rule");

    printf("  prices exact, party caps at %d, fallen return cheaper, released reroll\n", MAX_COMPANIONS);
}

/* ---- what a hired hero actually does down there ---------------------- */

/* An open floor with one monster placed where the caller wants it, so a
   companion's decision can be watched in isolation. */
static const int RING_DX[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
static const int RING_DY[8] = { 0, 0,-1, 1, -1,-1,  1, 1};

static void open_floor(Map *m) {
    memset(m, 0, sizeof(*m));
    m->floor_num = 5;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) m->tiles[y][x].type = TILE_FLOOR;
}

static Monster *drop_monster(Map *m, int x, int y, int hp) {
    Monster *mo = &m->monsters[m->monster_count++];
    memset(mo, 0, sizeof(*mo));
    mo->x = x; mo->y = y; mo->hp = hp; mo->maxhp = hp;
    mo->atk = 1; mo->def = 0; mo->alive = true;
    mo->xp_reward = 1;
    snprintf(mo->name, sizeof(mo->name), "test-thing");
    return mo;
}

/* Puts one hero of a chosen role and temperament in the party at (x,y),
   bypassing the Tavern so the test controls the variables. */
static Hero *plant(Player *p, int archetype, int personality, int x, int y) {
    memset(p, 0, sizeof(*p));
    p->party[0].x = 70; p->party[0].y = 40; p->party[0].level = 5; p->tavern_seed = 1u;
    p->party[0].maxhp = 80; p->party[0].base_atk = 30; p->party[0].weapon_bonus = 10;
    p->party[0].base_def = 10; p->party[0].armor_bonus = 5;

    Hero *c = &p->party[(0) + 1];

    /* Built through the real roster, not hand-assembled: a hire's kit --
       school, spells, weapon -- is decided when the candidate is generated,
       and a hand-built Hero would be a Marksman with nothing to shoot,
       which is a different thing from a Marksman. Walk the seeds until one
       of the wanted role turns up. */
    bool found = false;
    for (unsigned int seed = 1; seed < 400 && !found; seed++) {
        p->tavern_seed = seed;
        for (int i = 0; i < TAVERN_ROSTER && !found; i++) {
            Hero cand;
            tavern_candidate(p, i, &cand);
            if (cand.archetype != archetype) continue;
            *c = cand;
            found = true;
        }
    }
    if (!found) { printf("  FAIL: no %s on any roster\n", archetype_name(archetype)); failures++; }

    /* Now pin everything the behaviour tests need to be deterministic. */
    c->in_use = true; c->alive = true;
    c->roster_idx = 0; c->personality = personality;
    c->x = x; c->y = y;
    c->maxhp = 100; c->hp = 100;
    c->base_atk = 40; c->base_def = 5;
    c->level = 5; c->xp = 0; c->xp_next = 1000000;  /* levelling out of scope */
    c->gear_count = 0; c->potion_count = 0;
    /* Signature moves are tested on their own below; parked here so the
       movement and reach cases measure one thing at a time. */
    c->ability_cd = 999;
    c->shot_cd = 0; c->ranged_cooldown = 0;
    for (int k = 0; k < c->spell_count; k++) c->spell_cd[k] = 0;
    c->aether = c->aether_max;
    snprintf(c->name, sizeof(c->name), "Test Hero");
    p->tavern_hired_mask |= 1u;
    return c;
}

static void test_companion_behaviour(void) {
    printf("companions in the dungeon\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;

    /* Reach: a Marksman shoots across a room. A Vanguard has to walk. */
    Hero *shooter = plant(&p, ARCH_MARKSMAN, PERSONALITY_STEADY, 70, 44);
    open_floor(&m);
    Monster *mo = drop_monster(&m, 70, 40, 500);
    int before_hp = mo->hp, sx = shooter->x, sy = shooter->y;
    companions_take_turn(&p, &m);
    check(mo->hp < before_hp, "a Marksman opens fire from four squares away");
    check(shooter->x == sx && shooter->y == sy, "and holds position to do it");

    Hero *brawler = plant(&p, ARCH_VANGUARD, PERSONALITY_STEADY, 70, 44);
    open_floor(&m);
    mo = drop_monster(&m, 70, 40, 500);
    before_hp = mo->hp;
    companions_take_turn(&p, &m);
    check(mo->hp == before_hp, "a Vanguard cannot reach the same target");
    check(brawler->y < 44, "so it closes instead");

    /* Nobody shoots through a wall. */
    Hero *blocked = plant(&p, ARCH_MARKSMAN, PERSONALITY_STEADY, 70, 44);
    open_floor(&m);
    for (int x = 60; x < 80; x++) m.tiles[42][x].type = TILE_WALL;
    mo = drop_monster(&m, 70, 40, 500);
    before_hp = mo->hp;
    companions_take_turn(&p, &m);
    check(mo->hp == before_hp, "line of sight is required to shoot");
    (void)blocked;

    /* Shooting is rationed by the weapon's own cooldown out of RANGED_STOCK,
       not by a flat rule -- so the assertion is that firing puts the weapon
       out of action for exactly as long as that weapon says. */
    Hero *cd = plant(&p, ARCH_MARKSMAN, PERSONALITY_STEADY, 70, 44);
    check(cd->ranged_slot > 0, "a hired Marksman actually carries a weapon");
    int weapon_cd = RANGED_STOCK[cd->ranged_slot - 1].cooldown;
    open_floor(&m);
    mo = drop_monster(&m, 70, 40, 50000);
    /* The game's sequence: the five decide, then time passes for all six.
       Ticking used to be the first half of the AI loop; it is its own pass
       now (party_tick), so a test that only calls the AI never advances a
       cooldown -- which is the harness disagreeing with the game, not the
       game being wrong. */
    party_tick(&p, &m); companions_take_turn(&p, &m);
    check(cd->ranged_cooldown == weapon_cd, "firing puts the weapon on its own cooldown");
    int after_first = mo->hp;
    for (int t = 0; t < weapon_cd - 1; t++) {
        party_tick(&p, &m); companions_take_turn(&p, &m);
        check(mo->hp == after_first, "and it cannot fire again while reloading");
    }
    party_tick(&p, &m); companions_take_turn(&p, &m);
    check(mo->hp < after_first, "but comes off cooldown");

    /* Temperament: Cautious backs out when it is going badly, Bold never
       does. This is the part of "personality" the player actually sees. */
    Hero *shy = plant(&p, ARCH_VANGUARD, PERSONALITY_CAUTIOUS, 70, 42);
    shy->hp = 20;                                   /* 20% -- below the line */
    open_floor(&m);
    drop_monster(&m, 70, 41, 500);
    int d_before = (shy->y - 41) * (shy->y - 41);
    companions_take_turn(&p, &m);
    int d_after = (shy->y - 41) * (shy->y - 41);
    check(d_after > d_before, "a Cautious hero disengages when badly hurt");

    Hero *bold = plant(&p, ARCH_VANGUARD, PERSONALITY_BOLD, 70, 42);
    bold->hp = 5;                                   /* nearly dead */
    bold->ability_cd = 999;                         /* no Bulwark to hide behind */
    open_floor(&m);
    mo = drop_monster(&m, 70, 41, 500);
    before_hp = mo->hp;
    companions_take_turn(&p, &m);
    check(mo->hp < before_hp, "a Bold hero keeps swinging at 5% health");

    /* Hurt but not routed: won't start a fight across the room, will still
       defend itself from what is already on top of it. */
    Hero *wary = plant(&p, ARCH_VANGUARD, PERSONALITY_CAUTIOUS, 70, 42);
    wary->hp = 50;                                  /* under engage, over retreat */
    open_floor(&m);
    drop_monster(&m, 70, 38, 500);                  /* four squares off */
    int wy = wary->y;
    companions_take_turn(&p, &m);
    check(wary->y >= wy, "a hurt Cautious hero doesn't go looking for it");

    /* Pathing: a wall with one gap. Greedy stepping pressed into the wall
       forever; a real path goes around. */
    Hero *walker = plant(&p, ARCH_VANGUARD, PERSONALITY_BOLD, 70, 46);
    open_floor(&m);
    for (int x = 60; x < 80; x++) m.tiles[44][x].type = TILE_WALL;
    m.tiles[44][64].type = TILE_FLOOR;              /* the one way through */
    drop_monster(&m, 70, 42, 500);
    bool got_through = false;
    for (int t = 0; t < 40 && !got_through; t++) {
        companions_take_turn(&p, &m);
        if (walker->y < 44) got_through = true;
    }
    check(got_through, "a companion paths around a wall instead of pressing into it");

    /* Loot: they earn their own off their own kills. Guaranteed here by
       killing a boss enough times that the roll has to land. */
    Hero *looter = plant(&p, ARCH_SURVIVOR, PERSONALITY_BOLD, 70, 41);
    bool found = false;
    for (int t = 0; t < 400 && !found; t++) {
        open_floor(&m);
        m.floor_num = 90;
        Monster *boss = drop_monster(&m, 70, 40, 1);
        boss->is_boss = true;
        looter->x = 70; looter->y = 41;
        companions_take_turn(&p, &m);
        found = looter->gear_count > 0;
    }
    check(found, "a companion turns up its own gear off its own kills");
    check(looter->gear[0].name[0] != '\0', "and the find is named");
    check(looter->base_atk > 40, "and it is folded straight into their stats");
    check(looter->gear_count <= COMPANION_GEAR_SLOTS, "they never carry more than the slots allow");

    /* Loot they walk over. They are a second player down there, so they pick
       things up -- but where it ends up is not symmetric, because the player
       is the one paying. */
    Hero *scav = plant(&p, ARCH_SURVIVOR, PERSONALITY_BOLD, 20, 20);
    open_floor(&m);
    /* Ring the companion, so whichever way it steps it lands on something --
       an open floor with no monsters gives no control over the direction. */
    for (int d = 0; d < 8; d++) {
        m.items[m.item_count].x = 20 + RING_DX[d];
        m.items[m.item_count].y = 20 + RING_DY[d];
        m.items[m.item_count].is_gold = true;
        m.items[m.item_count].gold_amount = 250;
        m.item_count++;
    }
    p.gold = 0;
    companions_take_turn(&p, &m);
    check(p.gold >= 250, "gold a companion picks up goes to the player");
    (void)scav;

    /* A healing draught they keep, because they are the ones who will need
       it, and they drink it when it is actually bad. */
    Hero *drinker = plant(&p, ARCH_SURVIVOR, PERSONALITY_BOLD, 20, 20);
    drinker->ability_cd = 999;                       /* no Second Wind to confuse it */
    open_floor(&m);
    for (int d = 0; d < 8; d++) {
        m.items[m.item_count].x = 20 + RING_DX[d];
        m.items[m.item_count].y = 20 + RING_DY[d];
        m.items[m.item_count].heal = 40;
        snprintf(m.items[m.item_count].name, sizeof(m.items[m.item_count].name), "draught");
        m.item_count++;
    }
    companions_take_turn(&p, &m);
    check(drinker->potion_count > 0, "a healing draught stays with the companion");
    drinker->hp = 20;
    companions_take_turn(&p, &m);
    check(drinker->hp > 20, "and they drink it when it gets bad");
    check(drinker->potion_count == 0, "spending it");

    /* Relics go to the player -- the set bonus is built around the player's
       four gear slots, and there is nowhere else for it to go. */
    Player relic_p;
    Hero *finder = plant(&relic_p, ARCH_SURVIVOR, PERSONALITY_BOLD, 20, 20);
    open_floor(&m);
    for (int d = 0; d < 8; d++) {
        m.features[m.feature_count].type = FEATURE_RELIC;
        m.features[m.feature_count].x = 20 + RING_DX[d];
        m.features[m.feature_count].y = 20 + RING_DY[d];
        m.features[m.feature_count].relic_kind = RELIC_WEAPON;
        m.features[m.feature_count].relic_set_id = BIOME_JUNGLE;
        m.features[m.feature_count].used = false;
        m.feature_count++;
    }
    companions_take_turn(&relic_p, &m);
    bool claimed = false;
    for (int i = 0; i < m.feature_count; i++) if (m.features[i].used) claimed = true;
    check(claimed, "a companion picks a relic up rather than walking past it");
    check(relic_p.party[0].weapon_bonus > 0, "and the relic lands on the player, not on them");
    (void)finder;

    /* The stairs are the player's decision. A companion drifting onto them
       -- let alone down them -- is the party leaving without you. */
    Hero *drifter = plant(&p, ARCH_SURVIVOR, PERSONALITY_BOLD, 71, 40);
    open_floor(&m);
    m.tiles[40][70].type = TILE_STAIRS_DOWN;
    m.stairs_down_x = 70; m.stairs_down_y = 40;
    bool stood_on_stairs = false;
    for (int t = 0; t < 200 && !stood_on_stairs; t++) {
        companions_take_turn(&p, &m);
        if (drifter->x == 70 && drifter->y == 40) stood_on_stairs = true;
    }
    check(!stood_on_stairs, "companions never step onto the stairs");

    /* ---- signature moves ---- */

    /* Survivor heals itself instead of dying, and does it without being told
       -- including from inside the retreat branch, which is where a hurt
       companion would otherwise always end up. */
    Hero *medic = plant(&p, ARCH_SURVIVOR, PERSONALITY_CAUTIOUS, 70, 42);
    medic->ability_cd = 0;
    medic->hp = 20;
    open_floor(&m);
    drop_monster(&m, 70, 41, 500);
    companions_take_turn(&p, &m);
    check(medic->hp > 20, "a Survivor uses Second Wind rather than just running");
    check(medic->ability_cd > 0, "and it goes on cooldown");

    /* Vanguard's Bulwark halves what lands on it. */
    Hero *wall = plant(&p, ARCH_VANGUARD, PERSONALITY_BOLD, 70, 42);
    wall->ability_cd = 0;
    open_floor(&m);
    drop_monster(&m, 70, 41, 500);
    companions_take_turn(&p, &m);
    check(wall->guard_turns > 0, "a Vanguard digs in when something is on it");
    int hp_before = wall->hp;
    hero_take_damage(&p, wall, 40);
    check(wall->hp == hp_before - 20, "and Bulwark halves the damage while it lasts");

    /* Arcanist's Detonation catches more than the one it aimed at. */
    Hero *mage = plant(&p, ARCH_ARCANIST, PERSONALITY_BOLD, 70, 44);
    mage->ability_cd = 0;
    open_floor(&m);
    Monster *a1 = drop_monster(&m, 70, 41, 5000);
    Monster *a2 = drop_monster(&m, 71, 41, 5000);
    companions_take_turn(&p, &m);
    check(a1->hp < 5000 && a2->hp < 5000, "Detonation catches everything around the target");

    /* Envoy's Rally picks the party up -- the reason to bring one at all. */
    Player party;
    Hero *envoy = plant(&party, ARCH_ENVOY, PERSONALITY_STEADY, 70, 42);
    envoy->ability_cd = 0;
    party.party[0].maxhp = 100; party.party[0].hp = 40;
    Hero *mate = &party.party[(1) + 1];
    memset(mate, 0, sizeof(*mate));
    mate->in_use = true; mate->alive = true; mate->roster_idx = 1;
    mate->x = 71; mate->y = 42; mate->maxhp = 100; mate->hp = 30;
    mate->base_atk = 10; mate->level = 5; mate->xp_next = 1000000;
    mate->ability_cd = 999;
    snprintf(mate->name, sizeof(mate->name), "Mate");
    party.tavern_hired_mask |= 3u;
    open_floor(&m);
    companions_take_turn(&party, &m);
    check(mate->hp > 30, "Rally patches up the rest of the party");
    check(party.party[0].atk_buff > 0, "and sharpens the player's next swings");

    /* Marksman's Called Shot goes straight through armour. */
    Hero *sniper = plant(&p, ARCH_MARKSMAN, PERSONALITY_STEADY, 70, 44);
    sniper->ability_cd = 0;
    open_floor(&m);
    Monster *armoured = drop_monster(&m, 70, 41, 5000);
    armoured->def = 10000;                  /* nothing ordinary could scratch it */
    companions_take_turn(&p, &m);
    check(armoured->hp < 5000, "Called Shot ignores armour entirely");

    /* ---- never rooted to the spot ----
       The turn used to `continue` on intent rather than on outcome, so a
       hero that decided to do something and then couldn't spent the turn
       standing still. Measured at 41 consecutive turns in one case. */

    /* In reach, but behind a wall: cannot shoot, so it must move. */
    Hero *pinned = plant(&p, ARCH_MARKSMAN, PERSONALITY_BOLD, 70, 44);
    pinned->ability_cd = 999;
    open_floor(&m);
    for (int x = 60; x < 80; x++) m.tiles[42][x].type = TILE_WALL;
    drop_monster(&m, 70, 40, 5000);
    int sx0 = pinned->x, sy0 = pinned->y;
    companions_take_turn(&p, &m);
    check(pinned->x != sx0 || pinned->y != sy0,
          "a hero that cannot shoot its target moves instead of freezing");

    /* In reach, clear line, but reloading: close rather than hold. Holding
       looked like a bug from the player's seat. */
    Hero *reloading = plant(&p, ARCH_MARKSMAN, PERSONALITY_BOLD, 70, 44);
    reloading->ability_cd = 999;
    reloading->shot_cd = 2;
    reloading->ranged_cooldown = 4;          /* weapon out of action too */
    open_floor(&m);
    drop_monster(&m, 70, 40, 5000);
    sy0 = reloading->y;
    companions_take_turn(&p, &m);
    check(reloading->y < sy0, "a reloading hero closes instead of standing there");

    /* Anything adjacent is a plain melee swing, no cooldown, whatever the
       role carries -- this holds for every class, not just the shooters. */
    int ranged_roles[3] = { ARCH_MARKSMAN, ARCH_ARCANIST, ARCH_ARTIFICER };
    for (int r = 0; r < 3; r++) {
        Hero *up_close = plant(&p, ranged_roles[r], PERSONALITY_BOLD, 70, 41);
        up_close->ability_cd = 999;
        up_close->shot_cd = 99;                    /* gun very much empty */
        up_close->ranged_cooldown = 99;
        for (int k = 0; k < up_close->spell_count; k++) up_close->spell_cd[k] = 99;
        open_floor(&m);
        Monster *inface = drop_monster(&m, 70, 40, 5000);
        int hp0 = inface->hp;
        companions_take_turn(&p, &m);
        check(inface->hp < hp0,
              "a ranged hero with something in its face hits it rather than reloading");
    }

    /* ---- loot is fetched, not tripped over ----
       They used to collect only what they happened to walk across. */
    Hero *fetcher = plant(&p, ARCH_SURVIVOR, PERSONALITY_BOLD, 40, 40);
    fetcher->ability_cd = 999;
    open_floor(&m);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) m.tiles[y][x].seen = true;   /* nothing to explore */
    m.items[m.item_count].x = 46; m.items[m.item_count].y = 40;
    m.items[m.item_count].is_gold = true;
    m.items[m.item_count].gold_amount = 500;
    m.item_count++;
    p.gold = 0;
    for (int t = 0; t < 30 && p.gold == 0; t++) companions_take_turn(&p, &m);
    check(p.gold >= 500, "a companion walks six squares to loot rather than ignoring it");

    /* ---- their own kit ----
       A caster carries real spells out of the pool; a shooter carries a real
       weapon out of RANGED_STOCK; and the ones who fight with their hands
       carry neither. */
    for (unsigned int seed = 1; seed <= 60; seed++) {
        Player q; memset(&q, 0, sizeof(q));
        q.party[0].level = 20; q.tavern_seed = seed; q.party[0].maxhp = 200; q.party[0].base_atk = 30;
        q.party[0].weapon_bonus = 20; q.party[0].base_def = 15;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero cand;
            tavern_candidate(&q, i, &cand);

            if (cand.archetype == ARCH_ARCANIST) {
                check(cand.spell_count > 0 || cand.ranged_slot > 0,
                      "an Arcanist is never hired with an empty spellbook");
                if (cand.spell_count > 0) {
                    check(cand.magic_school >= 0 && cand.magic_school < SCHOOL_COUNT,
                          "a caster's school is a real school");
                    check(cand.aether_max > 0, "and they have charge to spend");
                    for (int k = 0; k < cand.spell_count; k++) {
                        const SpellTemplate *t = spell_pool_get(cand.known_spells[k]);
                        check(t != NULL, "every spell slot names a real spell");
                        if (!t) continue;
                        check(t->school == cand.magic_school,
                              "and one from their own school");
                        /* The rule that matters most: nothing that reshapes
                           the floor. A hire that walls off your corridor or
                           burns the ground you are standing on is a hazard
                           you paid for, not a companion. */
                        check(t->effect != EFFECT_BARRIER && t->effect != EFFECT_SCORCH
                              && t->effect != EFFECT_PURGE && t->effect != EFFECT_BRIDGE
                              && t->effect != EFFECT_BLINK && t->effect != EFFECT_UNBIND
                              && t->effect != EFFECT_REVEAL,
                              "no companion ever learns terrain magic");
                    }
                    check(cand.aether_max >= spell_pool_get(cand.known_spells[0])->charge_cost,
                          "they can afford the heaviest thing they know");
                }
            } else if (cand.archetype == ARCH_MARKSMAN || cand.archetype == ARCH_ARTIFICER) {
                check(cand.ranged_slot > 0 && cand.ranged_slot <= RANGED_STOCK_COUNT,
                      "a Marksman or Artificer is hired carrying a weapon");
            } else {
                check(cand.spell_count == 0 && cand.ranged_slot == 0,
                      "the roles that fight with their hands carry neither");
            }
        }
    }

    /* A zeroed Hero must read as unarmed, not as a Conduit caster with a
       bow -- which is what a -1 sentinel in a memset struct would give. */
    Hero blank;
    memset(&blank, 0, sizeof(blank));
    check(blank.ranged_slot == 0 && blank.spell_count == 0,
          "an empty party slot carries nothing by construction");
    check(companion_weapon_name(&blank) == NULL, "and names no weapon");

    /* A hire taken on deep arrives with deeper magic than one taken on at
       the start -- their kit scales with them, like their stats. */
    int low_lvl = 0, high_lvl = 0, low_n = 0, high_n = 0;
    for (unsigned int seed = 1; seed <= 80; seed++) {
        Player lo, hi;
        memset(&lo, 0, sizeof(lo)); memset(&hi, 0, sizeof(hi));
        lo.party[0].level = 1;  lo.tavern_seed = seed; lo.party[0].maxhp = 50;  lo.party[0].base_atk = 10;
        hi.party[0].level = 45; hi.tavern_seed = seed; hi.party[0].maxhp = 400; hi.party[0].base_atk = 60;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero a, b;
            tavern_candidate(&lo, i, &a);
            tavern_candidate(&hi, i, &b);
            for (int k = 0; k < a.spell_count; k++) { low_lvl  += spell_pool_get(a.known_spells[k])->level; low_n++; }
            for (int k = 0; k < b.spell_count; k++) { high_lvl += spell_pool_get(b.known_spells[k])->level; high_n++; }
        }
    }
    check(low_n > 0 && high_n > 0, "casters turn up at both levels");
    check(high_lvl / high_n > low_lvl / low_n,
          "a hire taken on deep knows heavier magic than one taken on at the start");

    /* A caster with a spell ready uses it rather than punching. */
    Hero *wizard = plant(&p, ARCH_ARCANIST, PERSONALITY_BOLD, 70, 43);
    wizard->ability_cd = 999;
    if (wizard->spell_count > 0) {
        open_floor(&m);
        Monster *victim = drop_monster(&m, 70, 40, 100000);
        int before = victim->hp, aether_before = wizard->aether;
        companions_take_turn(&p, &m);
        check(victim->hp < before || wizard->aether != aether_before,
              "a caster with a spell ready casts it rather than closing to punch");
    }

    /* And a shooter shoots. */
    Hero *shooter2 = plant(&p, ARCH_MARKSMAN, PERSONALITY_BOLD, 70, 44);
    shooter2->ability_cd = 999;
    open_floor(&m);
    Monster *quarry = drop_monster(&m, 70, 40, 100000);
    int q0 = quarry->hp;
    companions_take_turn(&p, &m);
    check(quarry->hp < q0, "a shooter uses the weapon it is carrying");
    check(shooter2->ranged_cooldown > 0, "and the weapon goes on cooldown when it does");

    /* Two classes of the same role are no longer the same hero: the class's
       own attribute spread moves their stats a few percent either way. */
    {
        Player q; memset(&q, 0, sizeof(q));
        q.party[0].level = 20; q.party[0].maxhp = 200; q.party[0].base_atk = 40; q.party[0].base_def = 20;
        int distinct = 0, compared = 0;
        for (int role = 0; role < ARCHETYPE_COUNT; role++) {
            int n = archetype_class_count(role);
            for (int a = 0; a + 1 < n; a++) {
                Hero ca, cb;
                memset(&ca, 0, sizeof(ca)); memset(&cb, 0, sizeof(cb));
                ca.class_id = archetype_class_index(role, a);
                cb.class_id = archetype_class_index(role, a + 1);
                int ha, aa, da, hb, ab, db;
                companion_class_variation(ca.class_id, &ha, &aa, &da);
                companion_class_variation(cb.class_id, &hb, &ab, &db);
                compared++;
                if (ha != hb || aa != ab || da != db) distinct++;
                /* Clamped, so no class turns its role inside out. */
                check(ha >= -15 && ha <= 15 && aa >= -15 && aa <= 15 && da >= -15 && da <= 15,
                      "a class nudges its hero's stats without overturning the role");
            }
        }
        check(compared > 0, "there are same-role class pairs to compare");
        check(distinct * 2 > compared,
              "most same-role classes differ from each other mechanically");
    }

    printf("  reach, cooldowns, nerve, paths, loot, moves, real spellbooks and weapons\n");
}

/* ---- seeded runs ------------------------------------------------------
   The whole point is that "floor 12 of seed 44815" names one specific floor,
   so a bug can be handed over instead of described. */

static unsigned long map_fingerprint(const Map *m) {
    unsigned long h = 1469598103934665603UL;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            h ^= (unsigned long)m->tiles[y][x].type;
            h *= 1099511628211UL;
        }
    for (int i = 0; i < m->monster_count; i++) {
        h ^= (unsigned long)(m->monsters[i].x * 977 + m->monsters[i].y);
        h *= 1099511628211UL;
    }
    for (int i = 0; i < m->item_count; i++) {
        h ^= (unsigned long)(m->items[i].x * 977 + m->items[i].y);
        h *= 1099511628211UL;
    }
    return h;
}

static void seeded_player(Player *p, unsigned int seed) {
    memset(p, 0, sizeof(*p));
    p->run_seed = seed; p->tavern_seed = seed ^ 0x5bf03635u;
    p->party[0].level = 5; p->party[0].maxhp = 80; p->party[0].hp = 80;
    p->party[0].base_atk = 14; p->party[0].base_def = 8; p->party[0].fov_radius = 7;
    p->difficulty = DIFFICULTY_NORMAL;
}

static void test_seeded_runs(void) {
    printf("seeded runs\n");

    static Map a, b;
    Player pa, pb;
    int ax, ay, bx, by;

    /* Same seed, same floor -- twice, from different points in the global
       RNG stream. This is the property that matters: what you rolled in
       combat on the way down must not change the floor you arrive at. */
    seeded_player(&pa, 44815u);
    srand(1);
    generate_temple_floor(&a, 12, &ax, &ay, &pa);

    seeded_player(&pb, 44815u);
    srand(999999);
    for (int i = 0; i < 5000; i++) (void)rand();      /* a long, messy run */
    generate_temple_floor(&b, 12, &bx, &by, &pb);

    check(map_fingerprint(&a) == map_fingerprint(&b),
          "the same seed rebuilds the same floor, whatever happened before it");
    check(ax == bx && ay == by, "and drops you in the same place on it");

    /* Different seeds must actually differ, or the above is vacuous. */
    Player pc; seeded_player(&pc, 44816u);
    static Map c;
    int cx, cy;
    generate_temple_floor(&c, 12, &cx, &cy, &pc);
    check(map_fingerprint(&a) != map_fingerprint(&c),
          "a different seed is a different floor");

    /* Different depths of one run differ too -- a seed is a dungeon, not a
       floor stamped a hundred times. */
    static Map d;
    int dx, dy;
    seeded_player(&pa, 44815u);
    generate_temple_floor(&d, 13, &dx, &dy, &pa);
    check(map_fingerprint(&a) != map_fingerprint(&d),
          "floors within a run differ from each other");

    /* The derivation itself: distinct, never zero (a zero seed would send
       srand somewhere shared by every run that had one). */
    check(floor_seed(44815u, 12) != floor_seed(44815u, 13), "each depth gets its own stream");
    check(floor_seed(44815u, 12) != floor_seed(44816u, 12), "so does each run");
    check(floor_seed(0u, 0) != 0u, "and a zero seed still yields a usable stream");

    /* The Tavern roster is part of the run, so it has to come back too. */
    Hero ra, rb;
    seeded_player(&pa, 7777u);
    seeded_player(&pb, 7777u);
    tavern_candidate(&pa, 4, &ra);
    tavern_candidate(&pb, 4, &rb);
    check(strcmp(ra.name, rb.name) == 0 && ra.class_id == rb.class_id,
          "the same seed offers the same heroes for hire");

    printf("  a seed names a dungeon: floors, drops and roster all reproduce\n");
}

/* ---- gear upgrades ---------------------------------------------------
   The uncapped track: the shops stop at +27 and the dungeon does not stop,
   so this is what a late run spends gold on. */
static void test_upgrades(void) {
    printf("gear upgrades\n");

    /* Monotonic and never free -- a flat or falling price would make the
       twentieth upgrade as cheap as the first. */
    int last = 0;
    for (int plus = 0; plus < 40; plus++) {
        int price = upgrade_price(plus);
        check(price > last, "each upgrade costs more than the one before");
        last = price;
    }
    check(upgrade_price(0) < 500, "the first one is affordable early");
    check(upgrade_price(20) > upgrade_price(0) * 20, "and the deep ones are a real cost");

    /* The party inherits: your +N is their +N, which is the only way your
       gear ever reaches a hire. */
    Player p;
    memset(&p, 0, sizeof(p));
    p.party[0].level = 10; p.tavern_seed = 99u; p.gold = 100000000;
    p.party[0].maxhp = 150; p.party[0].base_atk = 25; p.party[0].base_def = 12;
    check(companion_hire(&p, 0), "a hero to test the inheritance on");

    Hero *c = &p.party[(0) + 1];
    c->ability_cd = 999;
    c->maxhp = 200; c->hp = 200;

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    m.floor_num = 4;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) m.tiles[y][x].type = TILE_FLOOR;

    /* Same hero, same monster, with and without the smith's work. */
    p.party[0].weapon_plus = 0;
    c->x = 70; c->y = 41;
    Monster *mo = &m.monsters[m.monster_count++];
    memset(mo, 0, sizeof(*mo));
    mo->x = 70; mo->y = 40; mo->hp = mo->maxhp = 1000000; mo->alive = true;
    mo->def = 0; mo->atk = 1;
    snprintf(mo->name, sizeof(mo->name), "dummy");

    int before = mo->hp;
    companions_take_turn(&p, &m);
    int plain = before - mo->hp;

    p.party[0].weapon_plus = 10;
    c->x = 70; c->y = 41;
    c->shot_cd = 0; c->ranged_cooldown = 0;
    for (int k = 0; k < c->spell_count; k++) c->spell_cd[k] = 0;
    c->aether = c->aether_max;
    before = mo->hp;
    companions_take_turn(&p, &m);
    int upgraded = before - mo->hp;

    check(upgraded > plain,
          "a hired hero hits harder when the player's weapon is upgraded");

    /* And the armour half: their damage taken drops.
     *
       Driven through the monster's actual attack rather than through the
       damage applier, because the party's plate is folded into the *defence*
       now rather than subtracted from the number afterwards -- and the whole
       point of moving it there was that every source of damage should use one
       defence model. A test that calls the applier directly cannot see that,
       which is how it was passing while an undriven body defended in its
       underwear. */
    Monster *puncher = drop_monster(&m, c->x + 1, c->y, 500);
    puncher->atk = 60; puncher->def = 0; puncher->jinx_turns_left = 0;
    c->evasion_pct = c->set_bonus_evasion = c->evasion_buff = 0;
    c->ward_pct = c->set_bonus_ward = 0;      /* measure armour, not luck */

    int took_plain = 0, took_upgraded = 0;
    for (int trial = 0; trial < 200; trial++) {
        p.party[0].armor_plus = 0;
        c->hp = c->maxhp;
        monster_take_turn(&m, puncher, &p);
        took_plain += c->maxhp - c->hp;

        p.party[0].armor_plus = 8;
        c->hp = c->maxhp;
        monster_take_turn(&m, puncher, &p);
        took_upgraded += c->maxhp - c->hp;
    }
    check(took_upgraded < took_plain,
          "and takes less when the player's armour is upgraded");
    check(took_upgraded >= 1, "but a hit always lands for something");

    printf("  prices climb, and the party carries whatever the smith did for you\n");
}

/* ---- gold rush floors -------------------------------------------------
   The economy is what a hundred-floor run is gated on, and this is the
   supply side: one floor in eight worth going out of your way for. */
static void test_gold_rush(void) {
    printf("gold rush floors\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy, rush = 0, plain = 0;
    long rush_gold = 0, plain_gold = 0;

    for (unsigned int seed = 1; seed <= 300; seed++) {
        memset(&p, 0, sizeof(p));
        p.run_seed = seed; p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
        p.difficulty = DIFFICULTY_NORMAL;
        generate_temple_floor(&m, 20, &sx, &sy, &p);

        long gold = 0;
        for (int i = 0; i < m.item_count; i++)
            if (m.items[i].is_gold) gold += m.items[i].gold_amount;

        if (m.gold_rush) { rush++; rush_gold += gold; }
        else             { plain++; plain_gold += gold; }
    }

    check(rush > 0, "gold rush floors do occur");
    check(plain > 0, "and ordinary floors still outnumber them");
    /* One in eight, give or take -- loose bounds, because this is a die roll
       and a test that pins it exactly would fail on a different seed. */
    check(rush * 100 / (rush + plain) > 4 && rush * 100 / (rush + plain) < 25,
          "roughly one floor in eight is a gold rush");

    check(rush_gold / rush > (plain_gold / plain) * 10,
          "a gold rush floor is worth an order of magnitude more");

    /* The danger is unchanged: this is a reward, not a difficulty setting.
       If it also spawned fewer monsters it would be a free floor. */
    int rush_mon = 0, plain_mon = 0, rn = 0, pn = 0;
    for (unsigned int seed = 1; seed <= 200; seed++) {
        memset(&p, 0, sizeof(p));
        p.run_seed = seed; p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
        p.difficulty = DIFFICULTY_NORMAL;
        generate_temple_floor(&m, 20, &sx, &sy, &p);
        if (m.gold_rush) { rush_mon += m.monster_count; rn++; }
        else             { plain_mon += m.monster_count; pn++; }
    }
    if (rn > 0 && pn > 0) {
        int r = rush_mon / rn, q = plain_mon / pn;
        check(r > q / 2 && r < q * 2,
              "and is exactly as dangerous as any other floor");
    }

    printf("  one floor in eight, %dx the coin, none of the mercy\n", GOLD_RUSH_MULT);
}

/* ---- scrap ------------------------------------------------------------
   A separate kind of dungeon item: worth nothing until it is hauled back up
   and weighed. Its value is multiplied per biome, which is the only income
   curve in the game that is not linear in depth. */
static void test_junk(void) {
    printf("scrap and the junkyard\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy;

    /* Scrap is no longer strewn on the floor -- it accrues by walking. The
       floors must be clean of it, or the two sources would double up. */
    for (unsigned int seed = 1; seed <= 40; seed++) {
        memset(&p, 0, sizeof(p));
        p.run_seed = seed; p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
        p.difficulty = DIFFICULTY_NORMAL;
        generate_temple_floor(&m, 40, &sx, &sy, &p);
        for (int i = 0; i < m.item_count; i++)
            check(!m.items[i].is_junk, "no scrap is left lying on the floor");
    }

    /* The value curve is what both the walk and the junkyard price from, and
       it is the only income in the game that is not linear in depth. */
    check(junk_value_for_floor(1) > 0, "even the first floor pays something");
    check(junk_value_for_floor(90) > junk_value_for_floor(5) * 8,
          "and a step taken deep is worth many times one taken shallow");
    check(JUNK_STEPS_PER_PIECE > 0, "the trickle has an interval");

    /* Monotonic across the biome steps: no depth is ever worth less than a
       shallower one. */
    int prev = 0;
    for (int f = 1; f <= 100; f++) {
        int v = junk_value_for_floor(f);
        check(v >= prev, "scrap value never falls as you go deeper");
        prev = v;
    }

    printf("  no litter on the floors; value doubles per biome\n");
}

/* ---- the altar --------------------------------------------------------
   Half your health now, for doubled gold and scrap over the next ten floors.
   Priced in the one currency no shop sells back. */
static void test_altar(void) {
    printf("blood altars\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy, with = 0, without = 0;

    /* Every tenth floor has one; other floors do not. */
    for (unsigned int seed = 1; seed <= 40; seed++) {
        memset(&p, 0, sizeof(p));
        p.run_seed = seed; p.party[0].level = 10; p.party[0].maxhp = 200; p.party[0].fov_radius = 7;

        generate_temple_floor(&m, 30, &sx, &sy, &p);
        for (int i = 0; i < m.feature_count; i++)
            if (m.features[i].type == FEATURE_ALTAR) { with++; break; }

        generate_temple_floor(&m, 33, &sx, &sy, &p);
        for (int i = 0; i < m.feature_count; i++)
            if (m.features[i].type == FEATURE_ALTAR) { without++; break; }
    }
    check(with > 30, "floors divisible by ten carry an altar");
    check(without == 0, "and no other floor does");

    /* The bargain: half the current bar, and ten floors of doubling. */
    memset(&p, 0, sizeof(p));
    p.run_seed = 5u; p.party[0].level = 10; p.party[0].maxhp = 200; p.party[0].hp = 160; p.floor = 30;
    p.party[0].fov_radius = 7;
    generate_temple_floor(&m, 30, &sx, &sy, &p);

    MapFeature *altar = NULL;
    for (int i = 0; i < m.feature_count; i++)
        if (m.features[i].type == FEATURE_ALTAR) { altar = &m.features[i]; break; }
    check(altar != NULL, "there is an altar to step on");
    if (!altar) return;

    trigger_feature(&m, altar, &p);
    check(p.party[0].hp == 80, "it takes exactly half of what you are carrying");
    check(p.gold_boon_until_floor == 40, "and pays out for the next ten floors");
    check(altar->used, "and is spent once used");

    /* It can never be the thing that kills you -- a run ending on a menu-less
       square you walked onto would be indefensible. */
    memset(&p, 0, sizeof(p));
    p.run_seed = 5u; p.party[0].level = 10; p.party[0].maxhp = 200; p.party[0].hp = 1; p.floor = 30;
    p.party[0].fov_radius = 7;
    generate_temple_floor(&m, 30, &sx, &sy, &p);
    for (int i = 0; i < m.feature_count; i++)
        if (m.features[i].type == FEATURE_ALTAR) { altar = &m.features[i]; break; }
    trigger_feature(&m, altar, &p);
    check(p.party[0].hp >= 1, "and never takes the last of it");

    printf("  every tenth floor, half your blood, ten floors of doubled coin\n");
}

/* ---- the gladiator school --------------------------------------------
   The only way an attribute moves after character creation, which makes
   train_attribute the only place that re-derives stats mid-run. Everything
   here is about what it must NOT quietly undo. */
static void test_training(void) {
    printf("gladiator school\n");

    int last = 0;
    for (int v = 1; v < 20; v++) {
        int price = training_price(v);
        check(price > last, "training the same attribute again always costs more");
        last = price;
    }
    check(training_price(3) > upgrade_price(3) * 2,
          "a point in the thirty costs far more than a point of gear");

    Player p;
    memset(&p, 0, sizeof(p));
    apply_class_to_player(&p, 0);
    p.party[0].level = 1;

    /* A run's worth of purchases that training must not throw away. */
    p.party[0].hp_plus = 6;   p.party[0].maxhp += 6 * UPGRADE_HP_STEP;
    p.party[0].aether_plus = 3; p.party[0].aether_max += 3 * UPGRADE_AETHER_STEP;
    p.party[0].weapon_plus = 4; p.party[0].weapon_bonus += 4 * UPGRADE_STEP;
    p.party[0].armor_plus = 5;  p.party[0].armor_bonus += 5 * UPGRADE_STEP;
    p.party[0].hp = p.party[0].maxhp / 2;

    int maxhp_before = p.party[0].maxhp, hp_before = p.party[0].hp;
    int weapon_before = p.party[0].weapon_bonus, armor_before = p.party[0].armor_bonus;
    int aether_before = p.party[0].aether_max;
    int might_before = p.party[0].attrs[ATTR_MIGHT], atk_before = p.party[0].base_atk;

    train_attribute(&p.party[0], ATTR_MIGHT);

    check(p.party[0].attrs[ATTR_MIGHT] == might_before + 1, "the point lands");
    check(p.party[0].base_atk > atk_before, "and the derived stat moves with it");

    /* The three things a naive re-derive would destroy. */
    check(p.party[0].maxhp >= maxhp_before, "the bought max HP survives training");
    check(p.party[0].hp == hp_before, "and training is not a free full heal");
    check(p.party[0].weapon_bonus == weapon_before && p.party[0].armor_bonus == armor_before,
          "and the equipped gear is not replaced with the class's starting kit");
    check(p.party[0].aether_max == aether_before, "and the bought aether survives");

    /* Health training raises the ceiling without silently topping you up. */
    hp_before = p.party[0].hp;
    int cap_before = p.party[0].maxhp;
    train_attribute(&p.party[0], ATTR_STAMINA);
    check(p.party[0].maxhp > cap_before, "Stamina raises the ceiling");
    check(p.party[0].hp == hp_before, "without filling the bar");

    /* Everything above was checked on a level-1 character, which is why it
       passed for as long as it did. The stats levels buy live in the same
       three fields the attribute table derives -- base_atk, base_def, maxhp --
       and a re-derive assigns them rather than adding to them, so it silently
       un-levelled anyone who had played. Measured before the fix: one point
       of Might at level 20 took attack 28 -> 10, defence 16 -> 6, max HP
       238 -> 86, and threw the bow away. Anything that reaches this file
       through the attribute table has to be tested on a character who has
       been somewhere. */
    Player q;
    memset(&q, 0, sizeof(q));
    apply_class_to_player(&q, 0);
    q.party[0].level = 1;

    for (int L = 2; L <= 20; L++) {          /* exactly combat.c's level-up */
        q.party[0].level++;
        q.party[0].maxhp    += 8;
        q.party[0].hp        = q.party[0].maxhp;
        q.party[0].base_atk += 1;
        if (q.party[0].level % 2 == 0) q.party[0].base_def += 1;
    }
    equip_ranged(&q.party[0], "shortbow", RANGED_BOW, 6, 1, 2);
    q.party[0].base_atk += 2;                          /* a shrine, which is not in the table either */

    int lvl_atk = q.party[0].base_atk, lvl_def = q.party[0].base_def, lvl_hp = q.party[0].maxhp;
    int ranged_before = q.party[0].ranged_bonus;

    train_attribute(&q.party[0], ATTR_MIGHT);
    check(q.party[0].base_atk > lvl_atk, "a levelled character's attack goes up, not back to class base");
    check(q.party[0].base_def == lvl_def, "twenty levels of defence survive a point of Might");
    check(q.party[0].maxhp == lvl_hp, "and so does the max HP those levels bought");
    check(q.party[0].ranged_type != RANGED_NONE && q.party[0].ranged_bonus == ranged_before,
          "and the School does not confiscate the bow");

    /* The general form: buying a point moves exactly that attribute's share
       of a stat, whoever else contributes to it. */
    int atk_from_attrs_before = 4 + q.party[0].attrs[ATTR_MIGHT] / 2 + q.party[0].attrs[ATTR_BRAWN] / 4;
    int atk_before_second     = q.party[0].base_atk;
    train_attribute(&q.party[0], ATTR_MIGHT);
    int atk_from_attrs_after  = 4 + q.party[0].attrs[ATTR_MIGHT] / 2 + q.party[0].attrs[ATTR_BRAWN] / 4;
    check(q.party[0].base_atk - atk_before_second == atk_from_attrs_after - atk_from_attrs_before,
          "the stat moves by the attribute's share and nothing else");

    printf("  a point lands, and nothing the run paid for -- or earned -- is undone\n");
}

/* ---- the way back up is not a route --------------------------------
   Landing on a staircase takes it, with no prompt. That is right for a
   keypress and wrong for a delegated walk, so no search may ever hand back
   a step onto the up stairs. */
static void test_never_routes_up(void) {
    printf("pathing never climbs\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) { m.tiles[y][x].type = TILE_FLOOR; m.tiles[y][x].seen = true; }

    /* Up stairs directly between the walker and where it wants to go. */
    m.tiles[40][71].type = TILE_STAIRS_UP;
    m.stairs_down_x = 75; m.stairs_down_y = 40;

    int dx = 0, dy = 0;
    bool ok = path_next_step(&m, 70, 40, 75, 40, true, false, &dx, &dy);
    check(ok, "a route to the far side still exists");
    check(!(dx == 1 && dy == 0), "and it does not step onto the up stairs");

    /* The same with the frontier search: unexplored ground beyond it. */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 72; x < MAP_W; x++) m.tiles[y][x].seen = false;
    ok = path_frontier_step(&m, 70, 40, true, &dx, &dy);
    if (ok) {
        int nx = 70 + dx, ny = 40 + dy;
        check(m.tiles[ny][nx].type != TILE_STAIRS_UP,
              "the frontier search does not step onto the up stairs either");
    }

    /* A corridor whose only route is through the stairs: the search must
       refuse rather than climb. Refusing is recoverable; climbing is not. */
    static Map c;
    memset(&c, 0, sizeof(c));
    c.floor_num = 5;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) { c.tiles[y][x].type = TILE_WALL; c.tiles[y][x].seen = true; }
    for (int x = 68; x <= 74; x++) c.tiles[40][x].type = TILE_FLOOR;
    c.tiles[40][71].type = TILE_STAIRS_UP;
    c.stairs_down_x = 74; c.stairs_down_y = 40;

    ok = path_next_step(&c, 68, 40, 74, 40, true, false, &dx, &dy);
    check(!ok, "a corridor plugged by the up stairs is reported as no route");

    printf("  no search returns a step onto the way back up\n");
}

/* ---- every floor can be finished --------------------------------------
   A floor whose stairs are walled off is a run that ends with nothing the
   player did wrong. Rare (2 in 720 before the fix) and fatal when it lands. */
/* How many floors a map-generation suite should sample.
 *
   These suites were written when a floor cost a fraction of a millisecond to
   generate. At 1400x800 one costs about 0.7 s, and the same 2,100-floor sweep
   would take twenty-five minutes -- which does not make the suite thorough,
   it makes it a suite nobody runs. So the sample count comes down as the map
   goes up, and every threshold below is expressed as a fraction of whatever
   was actually sampled rather than as a raw count. */
static int sample_seeds(int at_baseline_size) {
    int divisor = (int)(((long)MAP_W * MAP_H) / 11200);
    if (divisor < 1) divisor = 1;
    if (divisor > 24) divisor = 24;          /* never sample fewer than a few */
    int n = at_baseline_size / divisor;
    return n < 4 ? 4 : n;
}

static bool walk_reaches(const Map *m, int sx, int sy) {
    if (m->stairs_down_x < 0) return true;
    static bool seen[MAP_H_MAX][MAP_W_MAX];
    static int qx[MAP_W_MAX * MAP_H_MAX], qy[MAP_W_MAX * MAP_H_MAX];
    memset(seen, 0, sizeof(seen));
    int h = 0, t = 0;
    qx[t] = sx; qy[t] = sy; t++; seen[sy][sx] = true;
    while (h < t) {
        int cx = qx[h], cy = qy[h]; h++;
        if (cx == m->stairs_down_x && cy == m->stairs_down_y) return true;
        for (int d = 0; d < 8; d++) {
            int nx = cx + RING_DX[d], ny = cy + RING_DY[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H || seen[ny][nx]) continue;
            if (!is_walkable_player(m, nx, ny)) continue;
            seen[ny][nx] = true; qx[t] = nx; qy[t] = ny; t++;
        }
    }
    return false;
}

static void test_stairs_always_reachable(void) {
    printf("every floor can be finished\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy, checked = 0;

    /* Across world sizes as well as depths. This ran at whatever size was
       already set, which in practice meant one -- and a floor's structure is
       scaled by area, so the sizes are exactly where a structural change to
       mapgen would break connectivity without the small map ever noticing.
       The larger two are sampled less: a Deeps floor is 25 times the Shaft's
       area and the flood fill is proportional. */
    const struct { int size; int seeds; int step; } SWEEP[] = {
        { WORLD_SHAFT, 60, 11 },
        { WORLD_HALLS,  8, 23 },
        { WORLD_DEEPS,  2, 47 },
    };

    for (unsigned w = 0; w < sizeof SWEEP / sizeof SWEEP[0]; w++) {
        world_size_apply(SWEEP[w].size);
        int seeds = sample_seeds(SWEEP[w].seeds);
        for (int seed = 1; seed <= seeds; seed++) {
            for (int f = 1; f <= 99; f += SWEEP[w].step) {
                memset(&p, 0, sizeof(p));
                p.run_seed = (unsigned)(seed + w * 7919);
                p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
                p.difficulty = DIFFICULTY_NORMAL;
                generate_temple_floor(&m, f, &sx, &sy, &p);
                checked++;
                check(walk_reaches(&m, sx, sy),
                      "the down stairs can be walked to from where you arrive");
            }
        }
    }
    world_size_apply(WORLD_SHAFT);
    check(checked >= 30, "enough floors were generated to mean something");

    printf("  %d floors across three world sizes, every one of them crossable\n", checked);
}

/* ---- rooms come in more than one shape ---------------------------------
   Every room used to be 5-11 on both axes: sixty draws from one shape, which
   makes a floor read as sixty of the same place. */
static void test_room_variety(void) {
    printf("room shapes\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy;

    long fill_sum = 0; int floors = 0;
    int biggest_seen = 0, smallest_run = 1 << 30;

    int seeds = sample_seeds(40);
    for (int seed = 1; seed <= seeds; seed++) {
        for (int f = 1; f <= 60; f += 19) {
            memset(&p, 0, sizeof(p));
            p.run_seed = (unsigned)seed; p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
            p.difficulty = DIFFICULTY_NORMAL;
            generate_temple_floor(&m, f, &sx, &sy, &p);

            long walk = 0;
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++)
                    if (is_walkable_player(&m, x, y)) walk++;
            fill_sum += walk; floors++;

            /* The widest unbroken run of open floor on any row: a plaza or a
               hall shows up here and a grid of 5x5 chambers cannot. */
            int widest = 0;
            for (int y = 0; y < MAP_H; y++) {
                int run = 0;
                for (int x = 0; x < MAP_W; x++) {
                    if (is_walkable_player(&m, x, y)) { run++; if (run > widest) widest = run; }
                    else run = 0;
                }
            }
            if (widest > biggest_seen) biggest_seen = widest;
            if (widest < smallest_run) smallest_run = widest;
        }
    }

    check(floors >= 16, "enough floors to say anything");
    check(fill_sum / floors > (long)(MAP_W * MAP_H) * 45 / 100,
          "the floor is mostly open ground, not mostly rock");
    check(fill_sum / floors < (long)(MAP_W * MAP_H) * 70 / 100,
          "but it is a dungeon, not a plain");
    check(biggest_seen >= 16, "some floor has a room wide enough to be a plaza");
    check(smallest_run >= 11, "and every floor has something wider than a corridor");

    printf("  fill %ld%%, widest open run %d tiles\n",
           (fill_sum / floors) * 100 / (MAP_W * MAP_H), biggest_seen);
}

/* ---- the floor is a cave that people built in ---------------------------
   Room-and-corridor everywhere makes every floor the same kind of place.
   Some of the level was never built on: growth, standing water, bog, an
   older quarter. Not every floor -- an ordinary built floor has to stay the
   baseline or the exception stops reading as one. */
static bool reaches_tile(const Map *m, int sx, int sy, int tx, int ty) {
    static bool seen[MAP_H_MAX][MAP_W_MAX];
    static int qx[MAP_W_MAX * MAP_H_MAX], qy[MAP_W_MAX * MAP_H_MAX];
    memset(seen, 0, sizeof(seen));
    int h = 0, t = 0;
    qx[t] = sx; qy[t] = sy; t++; seen[sy][sx] = true;
    while (h < t) {
        int cx = qx[h], cy = qy[h]; h++;
        if (cx == tx && cy == ty) return true;
        for (int d = 0; d < 8; d++) {
            int nx = cx + RING_DX[d], ny = cy + RING_DY[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H || seen[ny][nx]) continue;
            if (!is_walkable_player(m, nx, ny)) continue;
            seen[ny][nx] = true; qx[t] = nx; qy[t] = ny; t++;
        }
    }
    return false;
}

static void test_wild_districts(void) {
    printf("wild districts\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy;

    int kinds[WILD_KIND_MAX] = {0};
    int floors = 0, bare = 0, thicket_floors = 0;

    int seeds = sample_seeds(90);
    for (int seed = 1; seed <= seeds; seed++) {
        for (int f = 1; f <= 95; f += 13) {
            memset(&p, 0, sizeof(p));
            p.run_seed = (unsigned)seed; p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
            p.difficulty = DIFFICULTY_NORMAL;
            generate_temple_floor(&m, f, &sx, &sy, &p);
            floors++;

            int here = 0;
            for (int k = 0; k < WILD_KIND_MAX; k++)
                if (m.wild_kinds & (1u << (unsigned)k)) { kinds[k]++; here++; }
            if (!here) bare++;

            long thick = 0;
            for (int y = 0; y < MAP_H; y++)
                for (int x = 0; x < MAP_W; x++)
                    if (m.tiles[y][x].type == TILE_THICKET) thick++;
            if (thick > 0) thicket_floors++;

            /* Undergrowth is not a floor you can walk over. If it ever
               becomes one, the jungle stops breaking sightlines and the
               whole point of the district goes with it. */
            if (thick > 0) {
                bool walkable_thicket = false;
                for (int y = 0; y < MAP_H && !walkable_thicket; y++)
                    for (int x = 0; x < MAP_W; x++)
                        if (m.tiles[y][x].type == TILE_THICKET &&
                            is_walkable_player(&m, x, y)) { walkable_thicket = true; break; }
                check(!walkable_thicket, "thicket blocks the way");
            }
        }
    }

    for (int k = 0; k < WILD_KIND_MAX; k++) {
        char msg[96];
        snprintf(msg, sizeof msg, "every kind of wild turns up somewhere (%s)",
                 wild_kind_name(k) ? wild_kind_name(k) : "unnamed kind");
        check(kinds[k] > 0, msg);
    }
    /* This used to require that at least a quarter of floors were bare, on
       the theory that the ordinary built floor should stay the baseline. It
       was measured afterwards: that rule produced 40% of floors with no
       district at all and 5.2% of an average floor inside one, so what the
       player actually walked through was grey corridor with the biome's name
       written on the title bar and nowhere else. The assertion was pinning
       the wrong end of the design.

       Every floor is somewhere now. The contrast comes from how much of a
       floor is wild rather than from whether any of it is. */
    check(bare == 0, "no floor is bare rock with a biome's name over it");
    check(thicket_floors > 0, "growth actually gets generated");

    printf("  %d floors: jungle %d  sea %d  bog %d  ruins %d;  %d%% plain\n",
           floors, kinds[0], kinds[1], kinds[2], kinds[3], bare * 100 / floors);
}

/* ---- the camp ----------------------------------------------------------
   One floor in six has somewhere you can stop: walled, stocked, and empty of
   anything that wants to kill you. It is only worth having if all three hold
   at once -- a camp you cannot reach, or one with a monster standing in it,
   is worse than no camp. */
static void test_haven(void) {
    printf("the camp\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy;

    int floors = 0, camps = 0, with_water = 0, with_trader = 0;

    int seeds = sample_seeds(90);
    for (int seed = 1; seed <= seeds; seed++) {
        for (int f = 1; f <= 95; f += 13) {
            memset(&p, 0, sizeof(p));
            p.run_seed = (unsigned)seed; p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
            p.difficulty = DIFFICULTY_NORMAL;
            generate_temple_floor(&m, f, &sx, &sy, &p);
            floors++;
            if (m.haven_w <= 0) continue;
            camps++;

            for (int i = 0; i < m.monster_count; i++)
                check(!in_haven(&m, m.monsters[i].x, m.monsters[i].y),
                      "nothing is generated inside the palisade");

            int fountain = -1, trader = -1, inside = 0;
            for (int i = 0; i < m.feature_count; i++) {
                if (!in_haven(&m, m.features[i].x, m.features[i].y)) continue;
                inside++;
                if (m.features[i].type == FEATURE_FOUNTAIN) fountain = i;
                if (m.features[i].type == FEATURE_MERCHANT) trader = i;
            }
            check(inside > 0, "the camp has something in it worth walking to");
            if (fountain >= 0) {
                with_water++;
                check(reaches_tile(&m, sx, sy, m.features[fountain].x, m.features[fountain].y),
                      "and you can walk to it from where you arrive");
            }
            if (trader >= 0) with_trader++;
        }
    }

    check(camps * 100 / floors >= 8, "camps turn up often enough to test");
    check(camps * 100 / floors <= 30, "but not on most floors");
    check(with_water == camps, "every camp has water");
    check(with_trader * 100 / camps >= 50, "and most have somebody selling");

    printf("  %d camps over %d floors (%d%%), %d with a trader\n",
           camps, floors, camps * 100 / floors, with_trader);
}

/* ---- the bank ----------------------------------------------------------
   A standing multiplier on income is the strongest thing that can be sold,
   so the two properties that matter are that it reaches *every* source and
   that its price outruns what it earns you per share. */
static void test_bank(void) {
    printf("the bank\n");

    Player p;
    memset(&p, 0, sizeof(p));
    p.gold_mult = 1;
    p.floor = 3;

    /* Baseline: no gold-find, no boon, no share. */
    check(player_gain_gold(&p, 100) == 100, "a coin is a coin at x1");
    check(p.gold == 100, "and it lands in the purse");

    p.gold = 0; p.gold_mult = 7;
    check(player_gain_gold(&p, 100) == 700, "the share multiplies plain income");

    /* Gold-find and the altar stack with it rather than replacing it. */
    p.gold = 0; p.gold_mult = 3; p.party[0].gold_bonus_pct = 50;
    check(player_gain_gold(&p, 100) == 450, "gold-find applies before the share");

    p.gold = 0; p.party[0].gold_bonus_pct = 0; p.gold_boon_until_floor = 10;
    check(player_gain_gold(&p, 100) == 600, "and the altar's doubling too");

    /* In town the altar is not running, which is what stops the scrap sale
       double-dipping: junk value is already doubled as it is picked up. */
    p.gold = 0; p.floor = 0;
    check(player_gain_gold(&p, 100) == 300, "the boon is a dungeon thing, not a town one");

    /* The purse is an int; a big share on a deep haul must clamp, not wrap. */
    p.gold = 1900000000; p.gold_mult = 100; p.gold_boon_until_floor = 0;
    int got = player_gain_gold(&p, 1000000);
    check(p.gold <= 2000000000 && p.gold >= 1900000000, "the purse clamps instead of wrapping");
    check(got >= 0, "and reports what it actually credited");

    check(player_gain_gold(&p, 0) == 0, "nothing gained from nothing");
    check(player_gain_gold(&p, -50) == 0, "and a negative is not a refund");

    /* Price: each share must cost more than the one before, and the whole
       ladder has to stay inside a long. */
    long prev = 0;
    for (int mult = 1; mult < BANK_MULT_MAX; mult++) {
        long price = bank_price(mult);
        check(price > prev, "each share costs more than the last");
        check(price > 0, "and never overflows into nonsense");
        prev = price;
    }
    check(bank_price(BANK_MULT_MAX) < 0, "the ladder ends");

    /* The real question: does a share pay for itself faster than the next
       one costs? If not, the curve is decorative. */
    long price_x2 = bank_price(1);
    check(price_x2 >= 1000 && price_x2 <= 5000, "the first share is a floor or two of takings");

    printf("  x2 %ld   x5 %ld   x10 %ld   x50 %ld   x100 %ld\n",
           bank_price(1), bank_price(4), bank_price(9), bank_price(49), bank_price(99));
}

/* ---- the three senses districts ----------------------------------------
   Mycelium, hive and mire are not shapes, they are rules that apply while
   you stand inside a rectangle. Each is asserted against its own opposite:
   the rule fires inside and does not fire outside. */
static void test_sense_districts(void) {
    printf("senses: mycelium, hive, mire\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    Player p;
    int sx, sy;

    int found[DIST_KIND_COUNT] = {0};
    int seeds = sample_seeds(60);

    for (int seed = 1; seed <= seeds; seed++) {
        for (int f = 1; f <= 95; f += 13) {
            memset(&p, 0, sizeof(p));
            p.run_seed = (unsigned)seed; p.party[0].level = 10; p.party[0].maxhp = 150; p.party[0].fov_radius = 7;
            p.difficulty = DIFFICULTY_NORMAL;
            generate_temple_floor(&m, f, &sx, &sy, &p);

            for (int i = 0; i < m.district_count; i++) {
                const MapDistrict *d = &m.districts[i];
                check(d->kind >= 0 && d->kind < DIST_KIND_COUNT, "a district has a real kind");
                found[d->kind]++;

                /* The record and the bitmask must never disagree -- the
                   arrival note reads one and the rules read the other. */
                check((m.wild_kinds & (1u << (unsigned)d->kind)) != 0,
                      "every recorded district is in the floor's kind mask");

                /* Its own tiles resolve to it, and a tile well outside does not. */
                check(district_at(&m, d->x, d->y) >= 0, "a district covers its own corner");
                check(district_at(&m, d->x + d->w - 1, d->y + d->h - 1) >= 0,
                      "and its far corner");
            }
        }
    }

    for (int k = 0; k < DIST_KIND_COUNT; k++)
        check(found[k] > 0, "every district kind, old and new, still generates");

    /* ---- the mire caps sight, and only inside itself ---- */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.district_count = 1;
    m.districts[0] = (MapDistrict){ 10, 10, 20, 20, DIST_MIRE, 0, 0, 0 };
    check(district_at(&m, 15, 15) == DIST_MIRE, "the mire covers its middle");
    check(district_at(&m,  5,  5) == -1, "and not the ground outside it");

    /* ---- the hive shares a wound ---- */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.district_count = 1;
    m.districts[0] = (MapDistrict){ 0, 0, 40, 40, DIST_HIVE, 0, 0, 0 };
    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 500; p.party[0].hp = 500; p.party[0].level = 5; p.party[0].x = 100; p.party[0].y = 100;
    p.gold_mult = 1;

    m.monsters[0] = make_monster_for_floor(5, 10, 10);
    m.monsters[1] = make_monster_for_floor(5, 12, 12);   /* inside, in earshot */
    m.monsters[2] = make_monster_for_floor(5, 200, 200); /* outside the district */
    m.monster_count = 3;
    for (int i = 0; i < 3; i++) { m.monsters[i].alive = true; m.monsters[i].aggro = false; m.monsters[i].hp = 40; }

    monster_take_damage(&p, hero_driven(&p), &m, &m.monsters[0], 1, NULL);
    check(m.monsters[1].aggro, "hurting one of the hive wakes its neighbour");
    check(m.monsters[1].hp < 40, "and the neighbour feels it too");
    check(!m.monsters[2].aggro, "but nothing outside the comb hears a thing");

    /* ---- the quiet quarter wakes all at once, and stays awake ---- */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.district_count = 1;
    m.districts[0] = (MapDistrict){ 0, 0, 60, 60, DIST_QUIET, 0, 0, 0 };
    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 500; p.party[0].hp = 500; p.party[0].level = 5; p.party[0].x = 300; p.party[0].y = 300; p.gold_mult = 1;

    m.monsters[0] = make_monster_for_floor(5, 5, 5);
    m.monsters[1] = make_monster_for_floor(5, 55, 55);   /* far side of the quarter */
    m.monsters[2] = make_monster_for_floor(5, 200, 200); /* outside it */
    m.monster_count = 3;
    for (int i = 0; i < 3; i++) { m.monsters[i].alive = true; m.monsters[i].aggro = false; m.monsters[i].hp = 40; }

    monster_take_damage(&p, hero_driven(&p), &m, &m.monsters[0], 1, NULL);
    check(m.monsters[1].aggro, "breaking the quiet wakes the whole quarter, not just the neighbours");
    check(!m.monsters[2].aggro, "and nothing outside it");

    /* ---- the storm strikes, and earths through what is touching the rod ---- */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.district_count = 1;
    m.districts[0] = (MapDistrict){ 10, 10, 20, 20, DIST_STORM, 0, 0, 0 };
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) m.tiles[y][x].type = TILE_FLOOR;
    m.tiles[15][15].type = TILE_ROD;
    m.storm_x = 15; m.storm_y = 15; m.storm_countdown = 1;

    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 500; p.party[0].hp = 500; p.party[0].level = 5; p.party[0].x = 100; p.party[0].y = 100; p.gold_mult = 1;
    m.monsters[0] = make_monster_for_floor(5, 16, 15);   /* touching the rod */
    m.monsters[1] = make_monster_for_floor(5, 40, 40);   /* nowhere near it */
    m.monster_count = 2;
    for (int i = 0; i < 2; i++) { m.monsters[i].alive = true; m.monsters[i].hp = 40; }

    int hp_before = p.party[0].hp;
    districts_tick(&m, &p);
    check(!m.monsters[0].alive, "a bolt earths through whatever is touching the rod");
    check(m.monsters[1].alive, "and leaves the rest of the floor alone");
    /* Not `== hp_before`: the kill grants XP, and a level-up raises HP. */
    check(p.party[0].hp >= hp_before, "standing well clear costs nothing");

    /* Standing next to it does not. */
    m.tiles[15][15].type = TILE_ROD;
    m.storm_x = 15; m.storm_y = 15; m.storm_countdown = 1;
    p.party[0].x = 16; p.party[0].y = 16;
    districts_tick(&m, &p);
    check(p.party[0].hp < 500, "standing beside a struck rod does");

    /* A floor with no storm district must never arm one. */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.storm_x = m.storm_y = -1;
    districts_tick(&m, &p);
    check(m.storm_x < 0, "no storm district, no bolt");

    /* ---- two of a kind on one floor are two separate places ----
       A floor can carry more than one district of the same kind (a quarter of
       floors on the larger worlds do). The rules must scope to the district
       you are standing in, not to every district that happens to share its
       name. */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.district_count = 2;
    m.districts[0] = (MapDistrict){ 0, 0, 30, 30, DIST_QUIET, 0, 0, 0 };
    m.districts[1] = (MapDistrict){ 100, 100, 30, 30, DIST_QUIET, 0, 0, 0 };   /* a second, far off */
    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 500; p.party[0].hp = 500; p.party[0].level = 5; p.party[0].x = 300; p.party[0].y = 300; p.gold_mult = 1;

    m.monsters[0] = make_monster_for_floor(5, 5, 5);       /* quarter one */
    m.monsters[1] = make_monster_for_floor(5, 25, 25);     /* quarter one, far side */
    m.monsters[2] = make_monster_for_floor(5, 105, 105);   /* quarter TWO */
    m.monster_count = 3;
    for (int i = 0; i < 3; i++) { m.monsters[i].alive = true; m.monsters[i].aggro = false; m.monsters[i].hp = 40; }

    monster_take_damage(&p, hero_driven(&p), &m, &m.monsters[0], 1, NULL);
    check(m.monsters[1].aggro, "breaking the quiet wakes the quarter you broke it in");
    check(!m.monsters[2].aggro, "and leaves the other quarter asleep");

    check(district_index_at(&m, 5, 5) == 0, "the first quarter is district 0");
    check(district_index_at(&m, 105, 105) == 1, "the second is district 1");
    check(district_at(&m, 5, 5) == district_at(&m, 105, 105),
          "same kind, different place -- which is exactly the trap");

    /* ---- the arena runs its waves and pays once ---- */
    memset(&m, 0, sizeof(m));
    m.floor_num = 12;
    m.biome = BIOME_JUNGLE;
    m.district_count = 1;
    m.districts[0] = (MapDistrict){ 20, 20, 30, 20, DIST_ARENA, 0, 0, 0 };
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) m.tiles[y][x].type = TILE_FLOOR;
    m.arena_district = -1; m.arena_wave = 0; m.arena_paid = false;
    m.stairs_down_x = 5; m.stairs_down_y = 5;

    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 100000; p.party[0].hp = 100000; p.party[0].level = 40; p.gold_mult = 1;
    p.party[0].x = 5; p.party[0].y = 5;                       /* not on the sand yet */

    districts_tick(&m, &p);
    check(m.arena_wave == 0, "the arena sleeps until you step onto the sand");

    p.party[0].x = 35; p.party[0].y = 30;                     /* onto the sand */
    districts_tick(&m, &p);
    check(m.arena_wave == 1, "stepping on starts it");
    check(m.monster_count > 0, "and lets the first wave in");

    /* Fight all the way through: clear the sand, tick, repeat. */
    int guard = 0;
    while (m.arena_wave <= ARENA_WAVES && guard++ < 200) {
        for (int i = 0; i < m.monster_count; i++) m.monsters[i].alive = false;
        m.monster_count = 0;
        districts_tick(&m, &p);
    }
    check(m.arena_wave == ARENA_WAVES + 1, "ten waves and it is over");
    check(m.arena_paid, "and the chest comes up");

    int gold_after = p.gold;
    districts_tick(&m, &p);
    districts_tick(&m, &p);
    check(p.gold == gold_after, "standing in a finished arena pays nothing more");

    /* ---- the toll takes everything and moves you ---- */
    memset(&m, 0, sizeof(m));
    m.floor_num = 12;
    m.stairs_down_x = 90; m.stairs_down_y = 40;
    for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) m.tiles[y][x].type = TILE_FLOOR;
    m.feature_count = 1;
    m.features[0].type = FEATURE_TOLL;
    m.features[0].x = 10; m.features[0].y = 10; m.features[0].used = false;

    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 100; p.party[0].hp = 30; p.party[0].level = 5; p.gold = 5000; p.gold_mult = 1;
    p.party[0].x = 10; p.party[0].y = 10;
    trigger_feature(&m, &m.features[0], &p);
    check(p.gold == 0, "the toll is everything you are carrying");
    check(p.party[0].x == 90 && p.party[0].y == 40, "and it puts you at the stairs");

    /* Broke: no transport, and nothing taken. */
    m.features[0].used = false;
    p.gold = 0; p.party[0].x = 10; p.party[0].y = 10;
    trigger_feature(&m, &m.features[0], &p);
    check(p.party[0].x == 10 && p.party[0].y == 10, "an empty purse buys no passage");

    /* ---- the ground you fight on ---- */
    {
        static Map gm;
        Player gp;
        memset(&gm, 0, sizeof(gm));
        gm.floor_num = 8;
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) gm.tiles[y][x].type = TILE_FLOOR;

        memset(&gp, 0, sizeof(gp));
        /* A body has to be *standing* for the world to act on it. This set the
           stats and the position and left in_use/alive at zero, which worked
           only because the prism rule used to read hero_driven() -- and that
           falls back to slot 0 whatever its state. The rule is per body now,
           so the body has to be a real one. */
        gp.party[0].in_use = true; gp.party[0].alive = true;
        gp.party[0].base_atk = 100; gp.party[0].base_def = 100; gp.party[0].maxhp = 200; gp.party[0].hp = 100;
        gp.party[0].x = 50; gp.party[0].y = 50; gp.gold_mult = 1;

        int flat_atk = hero_eff_atk(hero_driven_c(&gp)), flat_def = hero_eff_def(hero_driven_c(&gp));

        gm.tiles[50][50].type = TILE_PRISM_RED;
        districts_tick(&gm, &gp);
        check(hero_eff_atk(hero_driven_c(&gp)) > flat_atk, "red light hits harder");
        check(hero_eff_def(hero_driven_c(&gp)) < flat_def, "and folds faster");

        gm.tiles[50][50].type = TILE_PRISM_BLUE;
        districts_tick(&gm, &gp);
        check(hero_eff_atk(hero_driven_c(&gp)) < flat_atk, "blue light holds");
        check(hero_eff_def(hero_driven_c(&gp)) > flat_def, "but does not hurt");

        /* Step off and it is gone -- that is the whole mechanic. */
        gm.tiles[50][50].type = TILE_FLOOR;
        districts_tick(&gm, &gp);
        check(hero_eff_atk(hero_driven_c(&gp)) == flat_atk, "plain ground is plain again");
        check(hero_eff_def(hero_driven_c(&gp)) == flat_def, "in both directions");

        gm.tiles[50][50].type = TILE_PRISM_GREEN;
        int hp_before = gp.party[0].hp;
        districts_tick(&gm, &gp);
        check(gp.party[0].hp > hp_before, "green light mends");
        gp.party[0].hp = gp.party[0].maxhp;
        districts_tick(&gm, &gp);
        check(gp.party[0].hp == gp.party[0].maxhp, "and never past full");

        /* The marsh mends them, and only them. */
        gm.tiles[50][50].type = TILE_FLOOR;
        gm.tiles[60][60].type = TILE_BLOODPOOL;
        gm.monsters[0] = make_monster_for_floor(8, 60, 60);
        gm.monsters[1] = make_monster_for_floor(8, 70, 70);
        gm.monster_count = 2;
        for (int i = 0; i < 2; i++) { gm.monsters[i].alive = true; gm.monsters[i].hp = 5; }
        gp.party[0].hp = 50;
        districts_tick(&gm, &gp);
        check(gm.monsters[0].hp > 5, "what wades in the marsh knits back together");
        check(gm.monsters[1].hp == 5, "and what stands on dry ground does not");
        check(gp.party[0].hp == 50, "the marsh is not for you");

        gm.monsters[0].hp = gm.monsters[0].maxhp;
        districts_tick(&gm, &gp);
        check(gm.monsters[0].hp == gm.monsters[0].maxhp, "nor does it overfill them");
    }

    /* ---- snares: ground you can take and they cannot ----
       The asymmetry is the mechanic, so it is what gets asserted. If a snare
       ever becomes monster-passable it stops being a trade and becomes a
       tax. */
    {
        static Map sm;
        memset(&sm, 0, sizeof(sm));
        sm.floor_num = 7;
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) sm.tiles[y][x].type = TILE_FLOOR;
        sm.tiles[30][30].type = TILE_SNARE;

        check(is_walkable_player(&sm, 30, 30), "you can walk into a snare");
        check(!is_walkable_monster(&sm, 30, 30), "and nothing else will follow you in");

        /* Auto-explore's hazard-avoiding pass uses the monster rule, so it
           routes around snares for free -- and its permissive fallback can
           still cross one when that is the only way. */
        check(!is_walkable_monster(&sm, 30, 30), "a careful path avoids them");
        check(is_walkable_player(&sm, 30, 30), "a desperate one does not have to");

        /* The two skins differ, and differ in the way the design says: the
           garden bites harder, the sand holds longer. */
        check(SNARE_GARDEN_DAMAGE > SNARE_SAND_DAMAGE, "the garden bites harder");
        check(SNARE_SAND_HOLD > SNARE_GARDEN_HOLD, "and the sand holds longer");
    }

    /* ---- every district is visibly a different place ---- */
    int pairs[DIST_KIND_COUNT];
    for (int k = 0; k < DIST_KIND_COUNT; k++) {
        pairs[k] = district_color_pair(k);
        check(pairs[k] > 0, "every district kind has a colour of its own");
    }
    for (int a = 0; a < DIST_KIND_COUNT; a++)
        for (int b = a + 1; b < DIST_KIND_COUNT; b++)
            check(pairs[a] != pairs[b], "and no two share a colour pair");
    check(district_color_pair(-1) < 0, "ordinary ground takes no tint");
    check(district_color_pair(DIST_KIND_COUNT) < 0, "and neither does nonsense");

    printf("  all %d kinds generate, each its own colour; hive and quiet wake, storm earths\n",
           DIST_KIND_COUNT);
}

/* ---- work that takes turns -----------------------------------------------
   The game had one multi-turn action, hard-coded. This is the general form,
   and the two properties that matter are that the right tiles offer work and
   that the wrong ones do not -- the resolver trusts this table completely. */
static void test_multi_turn_work(void) {
    printf("work that takes turns\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    m.floor_num = 10;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            m.tiles[y][x].type = TILE_FLOOR;

    check(work_available_at(&m, 10, 10) == WORK_NONE, "plain floor is not a job");

    m.tiles[10][10].type = TILE_ROD;
    check(work_available_at(&m, 10, 10) == WORK_HARVEST_ROD, "a rod can be cut up");

    /* Rubble is only salvage inside an old quarter. Everywhere else it is
       scenery, and treating it as a job would carpet every floor in them. */
    m.tiles[12][12].type = TILE_DECOR;
    check(work_available_at(&m, 12, 12) == WORK_NONE, "rubble outside the ruins is just rubble");

    m.district_count = 1;
    m.districts[0] = (MapDistrict){ 5, 5, 30, 30, DIST_RUINS, 0, 0, 0 };
    check(work_available_at(&m, 12, 12) == WORK_SALVAGE, "and inside them it is a wreck");

    /* Town is not a worksite, and neither is off-map. */
    m.floor_num = 0;
    check(work_available_at(&m, 10, 10) == WORK_NONE, "no prospecting in the plaza");
    m.floor_num = 10;
    check(work_available_at(&m, -1, 10) == WORK_NONE, "and none off the edge of the world");
    check(work_available_at(&m, MAP_W, 10) == WORK_NONE, "either edge");

    check(work_turns_for(WORK_HARVEST_ROD) > 0, "a rod costs turns");
    check(work_turns_for(WORK_SALVAGE) > work_turns_for(WORK_HARVEST_ROD),
          "and a wreck costs more than a rod");
    check(work_turns_for(WORK_NONE) == 0, "and idling costs none");

    /* ---- writs, and the rule they enforce ----
       A writ moves an attribute without gold, but only at the School and only
       once each. The property that matters is that finishing the arena grants
       exactly one -- not a point, and not an endless supply. */
    {
        static Map am;
        Player ap;
        memset(&am, 0, sizeof(am));
        am.floor_num = 12;
        am.biome = BIOME_JUNGLE;
        am.district_count = 1;
        am.districts[0] = (MapDistrict){ 20, 20, 30, 20, DIST_ARENA, 0, 0, 0 };
        for (int y = 0; y < MAP_H; y++) for (int x = 0; x < MAP_W; x++) am.tiles[y][x].type = TILE_FLOOR;
        am.arena_district = -1; am.arena_wave = 0; am.arena_paid = false;
        am.stairs_down_x = 5; am.stairs_down_y = 5;

        memset(&ap, 0, sizeof(ap));
        ap.party[0].maxhp = 100000; ap.party[0].hp = 100000; ap.party[0].level = 40; ap.gold_mult = 1;
        ap.party[0].x = 35; ap.party[0].y = 30;
        check(ap.training_writs == 0, "you start with no writs");

        int guard = 0;
        while (am.arena_wave <= ARENA_WAVES && guard++ < 200) {
            for (int i = 0; i < am.monster_count; i++) am.monsters[i].alive = false;
            am.monster_count = 0;
            districts_tick(&am, &ap);
        }
        check(ap.training_writs == 1, "finishing the arena is worth exactly one writ");

        districts_tick(&am, &ap);
        districts_tick(&am, &ap);
        check(ap.training_writs == 1, "and standing in a finished arena mints no more");
    }

    printf("  rod %d turns, wreck %d turns; only the right tiles offer work\n",
           work_turns_for(WORK_HARVEST_ROD), work_turns_for(WORK_SALVAGE));
}

/* ---- being moved without asking ------------------------------------------
   Displacement is one helper because its *guards* are the valuable part:
   conveyors, floods, ice and currents all want the same four refusals, and
   rediscovering them per consumer is how a floor sweeps you down a staircase
   you never chose. Each refusal is asserted here once. */
static void test_displacement(void) {
    printf("displacement\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            m.tiles[y][x].type = TILE_FLOOR;

    /* Open ground: it goes the whole way. */
    int x = 50, y = 50;
    check(displace_actor(&m, &x, &y, 1, 0, 3, NULL, NULL, NULL) == 3, "open water carries you the full distance");
    check(x == 53 && y == 50, "and leaves you where it should");

    /* A wall stops it short rather than through. */
    m.tiles[50][56].type = TILE_WALL;
    x = 53; y = 50;
    check(displace_actor(&m, &x, &y, 1, 0, 5, NULL, NULL, NULL) == 2, "a wall stops the drift");
    check(x == 55, "one tile short of it");

    /* Never onto a staircase -- being swept to the next floor by a current
       you did not choose to enter is the failure this guard exists for. */
    memset(&m.tiles[60][0], 0, 0);
    for (int i = 0; i < MAP_W; i++) m.tiles[60][i].type = TILE_FLOOR;
    m.tiles[60][43].type = TILE_STAIRS_DOWN;
    x = 40; y = 60;
    int went = displace_actor(&m, &x, &y, 1, 0, 6, NULL, NULL, NULL);
    check(went == 2, "the current stops before a staircase");
    check(x == 42, "and never stands you on it");

    m.tiles[60][43].type = TILE_STAIRS_UP;
    x = 40; y = 60;
    displace_actor(&m, &x, &y, 1, 0, 6, NULL, NULL, NULL);
    check(x == 42, "the way back up is no different");

    /* Never into an occupant: two actors on one tile is the invariant every
       combat routine assumes. */
    m.tiles[60][43].type = TILE_FLOOR;
    m.monsters[0] = make_monster_for_floor(5, 43, 60);
    m.monsters[0].alive = true;
    m.monster_count = 1;
    x = 40; y = 60;
    check(displace_actor(&m, &x, &y, 1, 0, 6, NULL, NULL, NULL) == 2, "and stops short of somebody standing there");

    /* Nor onto anybody -- and "anybody" means any body, not just the one the
       human is attached to. A belt used to carry a monster straight through a
       hire and put two actors on one square. */
    Player p;
    memset(&p, 0, sizeof(p));
    p.party[0].in_use = true; p.party[0].alive = true; p.party[0].hp = 10;
    p.party[0].x = 44; p.party[0].y = 61;
    for (int i = 0; i < MAP_W; i++) m.tiles[61][i].type = TILE_FLOOR;
    m.monster_count = 0;
    x = 41; y = 61;
    /* 41 -> 42 -> 43, then 44 is you: two steps, not three. */
    check(displace_actor(&m, &x, &y, 1, 0, 6, NULL, &p, NULL) == 2, "a monster in the water stops short of you");
    check(x == 43, "one tile behind you");

    /* The same for a hire standing further along the same channel. */
    p.party[1].in_use = true; p.party[1].alive = true; p.party[1].hp = 10;
    p.party[1].x = 48; p.party[1].y = 61;
    p.party[0].x = -1; p.party[0].y = -1;      /* the character is elsewhere */
    x = 45; y = 61;
    check(displace_actor(&m, &x, &y, 1, 0, 6, NULL, &p, NULL) == 2,
          "and stops short of a hire in the water too");
    check(x == 47, "one tile behind them");

    /* And the body being carried is exempt from its own check, or nothing
       could ever be moved at all. */
    p.party[1].x = 45; p.party[1].y = 61;
    int hx = 45, hy = 61;
    check(displace_actor(&m, &hx, &hy, 1, 0, 3, NULL, &p, &p.party[1]) == 3,
          "a body is not blocked by where it is standing");

    /* The edge of the world is not a place to be carried to. */
    x = MAP_W - 2; y = 50;
    displace_actor(&m, &x, &y, 1, 0, 5, NULL, NULL, NULL);
    check(x < MAP_W, "nothing is carried off the map");

    /* Zero distance and a zero direction are both no-ops rather than loops. */
    x = 50; y = 50;
    check(displace_actor(&m, &x, &y, 1, 0, 0, NULL, NULL, NULL) == 0, "no distance, no movement");
    check(displace_actor(&m, &x, &y, 0, 0, 3, NULL, NULL, NULL) == 0, "no direction, no movement");
    check(x == 50 && y == 50, "and nothing moved");

    /* ---- belts: lanes that alternate ----
       The direction is derived from the lane index rather than stored, so the
       property worth asserting is that adjacent lanes genuinely disagree --
       a bug there turns a navigation puzzle into a one-way conveyor. */
    {
        static Map bm;
        memset(&bm, 0, sizeof(bm));
        bm.floor_num = 6;
        for (int yy = 0; yy < MAP_H; yy++)
            for (int xx = 0; xx < MAP_W; xx++)
                bm.tiles[yy][xx].type = TILE_FLOOR;
        bm.district_count = 1;
        bm.districts[0] = (MapDistrict){ 10, 10, 40, 30, DIST_ASSEMBLY, 0, 0, 0 };

        int adx, ady;
        belt_flow_at(&bm, 20, 20, &adx, &ady);
        check(adx == 0 && ady == 0, "plain floor is not a belt");

        bm.tiles[12][20].type = TILE_BELT;   /* lane 0 */
        bm.tiles[15][20].type = TILE_BELT;   /* lane 1 */
        bm.tiles[18][20].type = TILE_BELT;   /* lane 2 */

        int d0, d1, d2, dy0;
        belt_flow_at(&bm, 20, 12, &d0, &dy0);
        belt_flow_at(&bm, 20, 15, &d1, &dy0);
        belt_flow_at(&bm, 20, 18, &d2, &dy0);
        check(d0 != 0, "a belt runs somewhere");
        check(d0 == -d1, "and the next lane runs the other way");
        check(d0 == d2, "and the one after that comes back round");

        /* A belt tile outside any assembly district carries nothing. */
        bm.tiles[80][80].type = TILE_BELT;
        belt_flow_at(&bm, 80, 80, &adx, &ady);
        check(adx == 0 && ady == 0, "a belt outside the works is scrap");
    }

    printf("  stops at walls, stairs, occupants and the map edge; belts alternate\n");
}

/* ---- a companion that was built, not hired -------------------------------
   The golem is the first Hero with no roster seat, and `roster_idx` is
   used as a shift amount by the reaper -- `1u << -1` is undefined behaviour,
   not merely wrong. That is what these assert. */
static void test_granted_companion(void) {
    printf("built companions\n");

    Player p;
    memset(&p, 0, sizeof(p));
    p.run_seed = 11; p.tavern_seed = 99; p.party[0].level = 10;
    p.party[0].maxhp = 200; p.party[0].hp = 200; p.party[0].x = 40; p.party[0].y = 40; p.gold_mult = 1;

    check(companion_count(&p) == 0, "you start alone");
    check(companion_grant_golem(&p, 12), "the line builds one");
    check(companion_count(&p) == 1, "and it joins the party");

    /* Find it and check the things that would break the roster. */
    const Hero *g = NULL;
    for (int i = 0; i < MAX_COMPANIONS; i++)
        if (p.party[(i) + 1].in_use) g = &p.party[(i) + 1];
    check(g != NULL, "it occupies a slot");
    check(g->roster_idx < 0, "and no roster seat");
    check(g->temporary, "and it is temporary");
    check(g->alive && g->hp > 0, "and it is standing");
    /* "Does not cast" is an empty spellbook, not a missing school. It used to
       be asserted as magic_school < 0, which was the sentinel rather than the
       behaviour -- and that sentinel is what locked every non-Arcanist hire
       out of the Guild ("nothing to teach a ? caster") the moment a hire could
       be the body you are playing. */
    check(g->spell_count == 0, "a built thing does not cast");
    check(g->magic_school >= 0 && g->magic_school < SCHOOL_COUNT,
          "but it has a school like everybody else, so the Guild will teach it");
    check(g->ranged_slot == 0, "and carries nothing to shoot");

    /* It must not be findable as a roster hire, or the Tavern would offer to
       release a thing it never hired. */
    for (int i = 0; i < TAVERN_ROSTER; i++)
        check(companion_by_roster(&p, i) != g, "and never answers to a roster index");

    /* Killing it must not shift by a negative index. */
    for (int i = 0; i < MAX_COMPANIONS; i++)
        if (p.party[(i) + 1].in_use) { p.party[(i) + 1].alive = false; p.party[(i) + 1].hp = 0; }
    unsigned before_hired = p.tavern_hired_mask, before_fallen = p.tavern_fallen_mask;
    {
        /* A real floor: the reaper runs inside the party's turn, and that
           turn walks the map. */
        static Map rm;
        memset(&rm, 0, sizeof(rm));
        rm.floor_num = 12;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                rm.tiles[y][x].type = TILE_FLOOR;
        p.party[0].x = 40; p.party[0].y = 40;
        companions_take_turn(&p, &rm);
    }
    check(p.tavern_hired_mask == before_hired, "its death frees no roster seat");
    check(p.tavern_fallen_mask == before_fallen, "and mourns nobody");

    /* Leaving the floor dismisses it. */
    memset(&p, 0, sizeof(p));
    p.run_seed = 11; p.tavern_seed = 99; p.party[0].level = 10; p.party[0].maxhp = 200; p.party[0].hp = 200;
    companion_grant_golem(&p, 5);
    check(companion_count(&p) == 1, "built again");
    companions_dismiss_temporary(&p);
    check(companion_count(&p) == 0, "and left behind when the floor changes");

    printf("  no roster seat, no shift by -1, gone with the floor\n");
}

/* ---- dynamic difficulty: the measurement half --------------------------
   Nothing consumes pressure yet, so what matters is that the *reading* is
   right: the four bands fire on the conditions they claim, the response is
   asymmetric, and the value cannot run away. */
static void test_dda_reading(void) {
    printf("dda pressure\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    m.floor_num = 10;

    Player p;
    memset(&p, 0, sizeof(p));
    p.party[0].maxhp = 100;

    /* Untouched: coasting. */
    dda_floor_begin(&p); p.party[0].hp = 95;
    dda_floor_end(&p, &m);
    check(dda_pressure(&p) == DDA_CRUISED, "an untouched floor reads as cruising");

    /* Same health, but it cost potions -- that was not a free floor. */
    p.dda_pressure = 0;
    dda_floor_begin(&p); p.party[0].hp = 95;
    for (int i = 0; i < 3; i++) dda_note_heal(&p);
    dda_floor_end(&p, &m);
    check(dda_pressure(&p) == DDA_HURT, "potions spent means it was not free");

    /* Nearly dead. */
    p.dda_pressure = 0;
    dda_floor_begin(&p); p.party[0].hp = 10;
    dda_floor_end(&p, &m);
    check(dda_pressure(&p) == DDA_ROUTED, "walking out at a tenth health is a rout");

    /* Left the way you came. */
    p.dda_pressure = 0;
    dda_floor_begin(&p); p.party[0].hp = 90;
    dda_note_retreat(&p);
    dda_floor_end(&p, &m);
    check(dda_pressure(&p) == DDA_ROUTED, "retreating counts however healthy you left");

    /* Middling. */
    p.dda_pressure = 0;
    dda_floor_begin(&p); p.party[0].hp = 60;
    dda_floor_end(&p, &m);
    check(dda_pressure(&p) == DDA_STEADY, "a fought-through floor is steady");

    /* Asymmetric on purpose: quick to forgive, slow to punish. */
    check(-DDA_ROUTED > DDA_CRUISED * 2, "a rout undoes more than a cruise builds");
    check(-DDA_HURT > DDA_CRUISED, "and so does a mauling");

    /* Bounded in both directions -- a runaway dial is a bug, not a difficulty. */
    p.dda_pressure = 0;
    for (int i = 0; i < 200; i++) { dda_floor_begin(&p); p.party[0].hp = 100; dda_floor_end(&p, &m); }
    check(dda_pressure(&p) == DDA_MAX, "cruising for ever tops out");
    for (int i = 0; i < 200; i++) { dda_floor_begin(&p); p.party[0].hp = 1; dda_floor_end(&p, &m); }
    check(dda_pressure(&p) == DDA_MIN, "and drowning bottoms out");

    /* Town is not a performance. */
    p.dda_pressure = 0;
    m.floor_num = 0;
    dda_floor_begin(&p); p.party[0].hp = 100;
    dda_floor_end(&p, &m);
    check(dda_pressure(&p) == 0, "the plaza is not scored");

    /* And a label for every band. */
    m.floor_num = 5;
    int probes[] = { DDA_MAX, 15, 0, -15, DDA_MIN };
    for (int i = 0; i < 5; i++) {
        p.dda_pressure = probes[i];
        check(dda_label(&p) != NULL && dda_label(&p)[0] != '\0', "every band has a name");
    }

    printf("  four bands, asymmetric, bounded %d..%d\n", DDA_MIN, DDA_MAX);
}

/* ---- the pathfinder still finds shortest paths ---------------------------
   path_next_step() moved from breadth-first to A*. Both return *a* shortest
   route, so the assertion that matters is that the route is still shortest --
   an inadmissible heuristic or a bungled relaxation would still return
   something, just longer, and nothing else in the game would notice. */
static void test_path_is_shortest(void) {
    printf("pathfinding\n");

    memset(&m, 0, sizeof(m));   /* the shared scratch world, fresh */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.stairs_down_x = -1; m.stairs_down_y = -1;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            m.tiles[y][x].type = TILE_FLOOR;

    /* Open ground: the step count must equal Chebyshev distance exactly. */
    for (int trial = 0; trial < 6; trial++) {
        int sx = 20 + trial * 3, sy = 20;
        int tx = sx + 17 + trial, ty = sy + 9;
        int steps = 0, cx = sx, cy = sy;
        while (!(cx == tx && cy == ty) && steps < 500) {
            int dx, dy;
            if (!path_next_step(&m, cx, cy, tx, ty, false, false, &dx, &dy)) break;
            cx += dx; cy += dy; steps++;
        }
        int adx = tx - sx, ady = ty - sy;
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        int ideal = adx > ady ? adx : ady;
        check(cx == tx && cy == ty, "A* arrives");
        check(steps == ideal, "in exactly the shortest number of steps");
    }

    /* Around a wall: still reaches, still no longer than breadth-first would. */
    for (int y = 10; y < 40; y++) m.tiles[y][50].type = TILE_WALL;
    int cx = 45, cy = 25, steps = 0;
    while (!(cx == 60 && cy == 25) && steps < 500) {
        int dx, dy;
        if (!path_next_step(&m, cx, cy, 60, 25, false, false, &dx, &dy)) break;
        cx += dx; cy += dy; steps++;
    }
    check(cx == 60 && cy == 25, "it routes around a wall");
    check(steps <= 40, "without wandering");

    /* Genuinely unreachable: it must refuse rather than loop or guess. */
    memset(&m, 0, sizeof(m));
    m.floor_num = 5;
    m.stairs_down_x = -1; m.stairs_down_y = -1;
    for (int y = 20; y < 30; y++) for (int x = 20; x < 30; x++) m.tiles[y][x].type = TILE_FLOOR;
    for (int y = 60; y < 70; y++) for (int x = 60; x < 70; x++) m.tiles[y][x].type = TILE_FLOOR;
    int dx, dy;
    check(!path_next_step(&m, 25, 25, 65, 65, false, false, &dx, &dy),
          "a sealed room is refused, not guessed at");
    check(!path_next_step(&m, 25, 25, 25, 25, false, false, &dx, &dy),
          "and standing on the target is not a step");

    printf("  shortest on open ground, routes walls, refuses the impossible\n");
}

/* ---- tiered materials --------------------------------------------------
   Three materials, split along the biome seams, and the deep upgrade rungs
   will not take anything else. The property worth asserting is the one the
   whole idea rests on: you cannot buy a deep rung with shallow material. */
static void test_material_tiers(void) {
    printf("materials\n");

    check(material_tier_for_floor(1)   == MAT_SCRAP,    "the shallows give scrap");
    check(material_tier_for_floor(35)  == MAT_SCRAP,    "and keep giving it through the Works");
    check(material_tier_for_floor(36)  == MAT_PLATINUM, "the Ruins give platinum");
    check(material_tier_for_floor(85)  == MAT_PLATINUM, "through to the end of the Wastes");
    check(material_tier_for_floor(86)  == MAT_DIAMOND,  "and the Abyss gives diamond");
    check(material_tier_for_floor(100) == MAT_DIAMOND,  "all the way down");

    /* The seams are the *biome* seams, asserted against the biome function
       rather than against numbers written twice. Each material is named for
       the places it comes from, and a table that drifts from the bands it
       claims to follow is how platinum ended up starting five floors after
       the Ruins did. */
    for (int f = 1; f <= MAX_FLOOR; f++) {
        Biome b = get_biome_for_floor(f);
        int want = (b == BIOME_ABYSS) ? MAT_DIAMOND
                 : (b == BIOME_RUINS || b == BIOME_WASTES) ? MAT_PLATINUM
                 : MAT_SCRAP;
        if (material_tier_for_floor(f) != want) {
            check(false, "every floor's material matches its biome");
            break;
        }
        if (f == MAX_FLOOR) check(true, "every floor's material matches its biome");
    }

    /* And the invariant that was missing, which is the one that mattered: a
       material the smith demands has to be obtainable before the rung that
       demands it. Platinum was wanted from +20 and did not drop until floor
       41, so the only way past +20 was to reach floor 41 on the upgrades the
       smith would not sell you. */
    for (int tier = 0; tier < MAT_COUNT; tier++) {
        int first_floor = -1;
        for (int f = 1; f <= MAX_FLOOR && first_floor < 0; f++)
            if (material_tier_for_floor(f) == tier) first_floor = f;
        int first_rung = -1;
        for (int n = 0; n < 200 && first_rung < 0; n++)
            if (upgrade_material_cost(n) > 0 && upgrade_material_tier(n) == tier) first_rung = n;
        if (first_rung < 0) continue;      /* nothing ever demands this one */
        check(first_floor > 0, "a demanded material drops somewhere");
    }

    for (int t = 0; t < MAT_COUNT; t++)
        check(material_name(t) != NULL && material_name(t)[0] != '\0', "each has a name");

    /* The early rungs must not be gated -- a currency the player has not been
       taught about yet is a wall, not a decision. */
    for (int n = 0; n < UPGRADE_PLATINUM_FROM; n++)
        check(upgrade_material_cost(n) == 0, "the first rungs cost coin only");

    /* And the deep ones must be. */
    check(upgrade_material_tier(UPGRADE_PLATINUM_FROM) == MAT_PLATINUM,
          "past twenty the smith wants platinum");
    check(upgrade_material_cost(UPGRADE_PLATINUM_FROM) > 0, "and a real amount of it");
    check(upgrade_material_tier(UPGRADE_DIAMOND_FROM) == MAT_DIAMOND,
          "past fifty, diamond");
    check(upgrade_material_cost(UPGRADE_DIAMOND_FROM) > 0, "and a real amount of that");

    /* This is the anti-farming property, stated directly: the material a deep
       rung needs is not obtainable at the depth a farmer would be sitting at. */
    int deep_tier = upgrade_material_tier(UPGRADE_DIAMOND_FROM);
    for (int floor = 1; floor <= 40; floor++)
        check(material_tier_for_floor(floor) != deep_tier,
              "no shallow floor yields what a deep rung costs");

    /* Cost rises with the rung, so the ladder keeps asking for more. */
    check(upgrade_material_cost(UPGRADE_PLATINUM_FROM + 20) >
          upgrade_material_cost(UPGRADE_PLATINUM_FROM),
          "and it wants more of it as you climb");

    printf("  scrap/platinum/diamond, gated from +%d and +%d\n",
           UPGRADE_PLATINUM_FROM, UPGRADE_DIAMOND_FROM);
}

/* The track exists to pay for gear without feeding the level cycle, so the
   one thing it must never do is quietly take. The first ladder shipped here
   returned 0.82 per gold staked in the window advertised as generous -- it
   looked right, read right, and was a sink. This pins the sign. */
static void test_race_book(void) {
    printf("the lizard track\n");

    int generous[RACE_RUNNERS], sour[RACE_RUNNERS];
    race_odds(0, generous);
    race_odds(RACE_GENEROUS_RACES, sour);

    int g = race_return_permille(generous);
    int s = race_return_permille(sour);

    check(g > 1000, "the generous window pays the player, not the house");
    check(s < 1000, "and the book takes it back afterwards");
    check(g < 1400, "but not so hard the track replaces the dungeon");

    /* Every runner is the same bet -- there is no trap pick, and no runner
       the player is supposed to know to avoid. Longer odds, longer price. */
    for (int i = 1; i < RACE_RUNNERS; i++)
        check(generous[i] > generous[i - 1], "odds run favourite to outsider");

    int weight[RACE_RUNNERS], total = 0;
    for (int i = 0; i < RACE_RUNNERS; i++) {
        weight[i] = 120 / (generous[i] + 1);
        if (weight[i] < 1) weight[i] = 1;
        total += weight[i];
    }
    for (int i = 0; i < RACE_RUNNERS; i++) {
        int ev = weight[i] * (generous[i] + 1) * 1000 / total;
        check(ev > 1000 && ev < 1400, "no runner is a trap pick");
    }

    /* Nobody is at the pass twice in one night. The first live night put the
       same customer up two covers running -- the payouts were right and it
       still read as broken. */
    {
        for (int trial = 0; trial < 200; trial++) {
            Player q; memset(&q, 0, sizeof q);
            q.deepest_floor = 40;
            Patron room[KITCHEN_PATRONS];
            kitchen_roll_patrons(&q, room, KITCHEN_PATRONS);
            for (int i = 0; i < KITCHEN_PATRONS; i++)
                for (int j = i + 1; j < KITCHEN_PATRONS; j++)
                    check(room[i].name != room[j].name,
                          "no customer is served twice in one night");
        }
    }

    /* The souring has to bite within a visit, or farming is unbounded. */
    check(RACE_GENEROUS_RACES > 0 && RACE_GENEROUS_RACES < 10,
          "the good prices are a handful, not an afternoon");

    /* No multiplier may touch a gambling return.
     *
       The track pays back stake plus winnings. Running that through the
       income funnel applied the bank share to the *stake as well*, so a book
       paying 0.82 per gold staked paid 1.64 at two shares and printed without
       bound -- found in play at 2,805 races and a million gold. Standing at
       the track must lose money at every share. */
    {
        for (int mult = 1; mult <= 8; mult *= 2) {
            Player q; memset(&q, 0, sizeof q);
            q.gold = 100000; q.gold_mult = mult;

            int stake = 50, book[RACE_RUNNERS];
            for (int r = 0; r < 3000 && q.gold >= stake; r++) {
                race_odds(r, book);
                int w[RACE_RUNNERS], total = 0;
                for (int i = 0; i < RACE_RUNNERS; i++) {
                    w[i] = 120 / (book[i] + 1);
                    if (w[i] < 1) w[i] = 1;
                    total += w[i];
                }
                int roll = rand() % total, win = RACE_RUNNERS - 1;
                for (int i = 0; i < RACE_RUNNERS; i++) {
                    if (roll < w[i]) { win = i; break; }
                    roll -= w[i];
                }
                q.gold -= stake;
                if (win == 0) player_credit_gold(&q, (long)stake * (book[0] + 1));
            }
            check(q.gold < 100000, "standing at the track loses money at every bank share");
        }
    }

    printf("  generous %d.%03d, sour %d.%03d per gold staked, %d races deep\n",
           g / 1000, g % 1000, s / 1000, s % 1000, RACE_GENEROUS_RACES);
}

/* The kitchen. Two things matter here and they are both easy to break by
   accident: meat must come off a blade and nothing else, and a service night
   must pay enough to be worth walking up for without paying so much that
   nobody ever goes down again. */
static void test_kitchen(void) {
    printf("the kitchen\n");

    /* Grades track depth, and a boss is always the best thing in the room. */
    check(meat_grade_for_kill(1, 10, false)   == MEAT_STRINGY, "the shallows give stringy meat");
    check(meat_grade_for_kill(30, 40, false)  == MEAT_FAIR,    "the middle gives fair");
    check(meat_grade_for_kill(70, 100, false) == MEAT_PRIME,   "the deeps give prime");
    check(meat_grade_for_kill(1, 10, true)    == MEAT_MYTHIC,  "and a boss is mythic at any depth");

    /* A notably heavy thing beats an ordinary one at the same depth, but
       cannot reach the grade reserved for bosses. */
    check(meat_grade_for_kill(30, 5000, false) > meat_grade_for_kill(30, 40, false),
          "a heavy kill is a better cut than a rat beside it");
    for (int f = 1; f <= 100; f += 7)
        check(meat_grade_for_kill(f, 100000, false) < MEAT_MYTHIC,
              "no ordinary monster is ever a mythic cut");

    /* The larder is finite, so a Swarm corridor cannot bank a thousand nights.
       Ask far more times than the cap and confirm it stops. */
    Player p; memset(&p, 0, sizeof p);
    p.party[0].level = 1;
    for (int i = 0; i < MEAT_LARDER_CEIL * 40; i++)
        meat_on_clean_kill(&p, 1, 10, true);   /* boss: no drop roll, always tries */
    check(p.meat[MEAT_MYTHIC] == meat_larder_cap(&p), "the larder fills and then stops");

    /* And the pack grows with the character rather than being a flat wall. */
    {
        Player lo; memset(&lo, 0, sizeof lo); lo.party[0].level = 1;
        Player hi; memset(&hi, 0, sizeof hi); hi.party[0].level = 40;
        check(meat_larder_cap(&hi) > meat_larder_cap(&lo), "a bigger character carries more");
        Player top; memset(&top, 0, sizeof top); top.party[0].level = 999;
        check(meat_larder_cap(&top) == MEAT_LARDER_CEIL, "but the pack still has a size");
    }

    /* Reputation is the compounding half, and it has to be bounded at both
       ends -- an unknown house pays less, a famous one more, neither runaway. */
    check(kitchen_rep_permille(0) < 1000, "an unknown kitchen pays under the odds");
    check(kitchen_rep_permille(KITCHEN_REP_MAX) > 1000, "a famous one pays over");
    check(kitchen_rep_permille(KITCHEN_REP_MAX) <= 3500, "but never runs away with it");
    for (int r = 1; r <= KITCHEN_REP_MAX; r++)
        check(kitchen_rep_permille(r) >= kitchen_rep_permille(r - 1),
              "and it never pays less for being better known");

    /* The decision at the pass has to be real: giving a patron exactly what
       they asked for beats fobbing them off, and overserving is a genuine
       trade rather than a strictly-better move. */
    memset(&p, 0, sizeof p);
    p.kitchen_rep = 50;
    Patron pat = { MEAT_PRIME, 500, "a test", "\"...\"" };

    check(kitchen_payout(&p, &pat, MEAT_PRIME) > kitchen_payout(&p, &pat, MEAT_FAIR),
          "the right cut beats a worse one");
    check(kitchen_payout(&p, &pat, MEAT_FAIR) > kitchen_payout(&p, &pat, MEAT_STRINGY),
          "and a near miss beats a bad one");
    check(kitchen_rep_delta(&pat, MEAT_MYTHIC) > kitchen_rep_delta(&pat, MEAT_PRIME),
          "overserving pleases them more");
    {
        /* ...but the extra gold is small next to what the better cut would
           have fetched from someone who actually wanted it. That gap is the
           whole reason not to just always serve your best. */
        Patron rich = { MEAT_MYTHIC, 1600, "a test", "\"...\"" };
        int wasted = kitchen_payout(&p, &pat,  MEAT_MYTHIC);
        int spent  = kitchen_payout(&p, &rich, MEAT_MYTHIC);
        check(wasted < spent, "a mythic cut is worth more to someone who asked for it");
    }
    check(kitchen_rep_delta(&pat, -1) < 0, "turning someone away costs the house");

    /* Serving badly must be worse than the payout alone suggests, or the
       reputation track is decoration. */
    check(kitchen_rep_delta(&pat, MEAT_STRINGY) < kitchen_rep_delta(&pat, MEAT_FAIR),
          "the worse the miss, the worse the word");

    /* A deeper player's room must be worth more than a shallow one's. The
       first cut of this scaled the purse by the grade ordered and nothing
       else, and since demand saturates once all four grades are on the table
       (around floor forty), a night at the bottom of the temple paid less
       than a night halfway down. Averaged over many rooms, because seven
       random patrons is far too noisy to compare on one sample. */
    {
        long prev = 0;
        int depths[] = { 1, 20, 40, 60, 80, 100 };
        for (unsigned d = 0; d < sizeof depths / sizeof *depths; d++) {
            long total = 0;
            for (int trial = 0; trial < 400; trial++) {
                Player q; memset(&q, 0, sizeof q);
                q.deepest_floor = depths[d];
                q.kitchen_rep   = KITCHEN_REP_START;
                Patron room[KITCHEN_PATRONS];
                kitchen_roll_patrons(&q, room, KITCHEN_PATRONS);
                for (int i = 0; i < KITCHEN_PATRONS; i++)
                    total += kitchen_payout(&q, &room[i], room[i].wants);
            }
            check(total > prev, "a deeper player draws a richer room");
            prev = total;
        }
    }

    printf("  %d grades, %d covers a night, rep pays x%d.%02d to x%d.%02d\n",
           MEAT_GRADE_COUNT, KITCHEN_PATRONS,
           kitchen_rep_permille(0) / 1000, (kitchen_rep_permille(0) % 1000) / 10,
           kitchen_rep_permille(KITCHEN_REP_MAX) / 1000,
           (kitchen_rep_permille(KITCHEN_REP_MAX) % 1000) / 10);
}

/* The rule the user named, stated as an executable fact: a kill by anything
   other than a blade leaves nothing behind. This is enforced by a latch in
   combat.c that is set in exactly one place, so what this really checks is
   that no new damage source has started setting it. */
static void test_meat_only_from_blades(void) {
    printf("spells ruin the meat\n");

    Map *m = calloc(1, sizeof *m);
    Player p; memset(&p, 0, sizeof p);
    p.floor = 30;
    p.party[0].hp = p.party[0].maxhp = 9999;
    p.party[0].base_atk = 4000;             /* enough to fell anything in one swing */

    int gx, gy;
    generate_temple_floor(m, 30, &gx, &gy, &p);

    int by_spell = 0, by_blade = 0;

    /* Kill a long run of monsters with a non-melee source. */
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        memset(p.meat, 0, sizeof p.meat);
        monster_take_damage(&p, hero_driven(&p), m, mo, mo->hp + 1000, NULL);
        by_spell += meat_total(&p);
    }
    check(by_spell == 0, "nothing killed by a spell leaves a cut");

    /* And the same monsters, killed properly, do leave something. The drop
       is a percentage, so this counts across a population rather than
       asserting on one kill. */
    generate_temple_floor(m, 30, &gx, &gy, &p);
    p.floor = 30;
    memset(p.meat, 0, sizeof p.meat);
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        for (int swing = 0; swing < 200 && mo->alive; swing++)
            hero_attack_monster(&p, hero_mc(&p), m, mo, NULL);
    }
    by_blade = meat_total(&p);
    check(by_blade > 0, "a blade does");

    printf("  %d monsters: %d cuts by blade, %d by spell\n",
           m->monster_count, by_blade, by_spell);
    free(m);
}

/* Every field added this session has to survive a trip through the save
   file. The header check guards against reading at the wrong offset, but it
   cannot catch a field that was simply never written -- so round-trip the
   ones that are new and compare. */
static void test_save_round_trip(void) {
    printf("save round trip\n");

    char dir[] = "/tmp/aether-rt-XXXXXX";
    check(mkdtemp(dir) != NULL, "a scratch state dir");
    setenv("AETHER_STATE_DIR", dir, 1);

    Player p; memset(&p, 0, sizeof p);
    Map *m = calloc(1, sizeof *m);
    check(m != NULL, "a map to go with it");

    p.party[0].level = 7; p.party[0].hp = p.party[0].maxhp = 90; p.gold = 12345;
    p.floor = 4; p.deepest_floor = 9;
    /* This session's new body and party state, set to something distinctive
       so a round trip that dropped it would be visible rather than a pair of
       zeroes agreeing with each other. */
    p.party[0].reflect_pct = 37; p.party[0].reflect_turns = 4;
    p.party[0].stance_atk_pct = 50;
    p.training_writs = 6;
    p.meat[MEAT_STRINGY] = 3;
    p.meat[MEAT_FAIR]    = 11;
    p.meat[MEAT_PRIME]   = 2;
    p.meat[MEAT_MYTHIC]  = 1;
    p.kitchen_rep    = 63;
    p.kitchen_nights = 1;
    p.kitchen_earned = 987654;
    p.races_this_visit = 2;
    p.mat_count[MAT_PLATINUM] = 8;
    /* The three doors added last, and the vent field's flag on the Map. A
       reading bought in town and not yet spent is exactly the state a save is
       most likely to be taken in -- you buy it, you walk to the temple, and
       the game saves on the way. */
    p.oracle_floors = ORACLE_LONG_FLOORS;
    p.oracle_full   = true;
    p.bazaar_day    = 41;

    int gx, gy;
    generate_temple_floor(m, 4, &gx, &gy, &p);
    p.party[0].x = gx; p.party[0].y = gy;

    m->storm_is_vent = true;
    save_run(&p, m);

    Player q; memset(&q, 0, sizeof q);
    Map *m2 = calloc(1, sizeof *m2);
    check(load_run(&q, m2), "the run loads back");
    check(m2->storm_is_vent == m->storm_is_vent, "and the floor remembers which skin is armed");

    for (int g = 0; g < MEAT_GRADE_COUNT; g++)
        check(q.meat[g] == p.meat[g], "every grade of the larder survives");
    check(q.kitchen_rep    == p.kitchen_rep,    "the house's standing survives");
    check(q.kitchen_nights == p.kitchen_nights, "and how many nights it has had");
    check(q.kitchen_earned == p.kitchen_earned, "and what it has earned you");
    check(q.races_this_visit == p.races_this_visit, "the bookmaker remembers too");
    check(q.mat_count[MAT_PLATINUM] == p.mat_count[MAT_PLATINUM], "and the materials");
    check(q.gold == p.gold && q.party[0].level == p.party[0].level, "along with the basics");
    check(q.oracle_floors == p.oracle_floors, "an unspent Oracle reading survives");
    check(q.oracle_full   == p.oracle_full,   "and which reading it was");
    check(q.bazaar_day    == p.bazaar_day,    "and what day it is at the Bazaar");

    /* The state this session added, specifically. It all rides in the raw
       struct write, so this is not really testing the serialiser -- it is
       testing that nobody has put a pointer, a cache or a file handle into
       Player or Map, which is the one change that would quietly break a
       format that has been "write the struct" since the beginning. Six
       SAVE_VERSION bumps went in today and each was a chance to do it. */
    check(q.party[0].reflect_pct == p.party[0].reflect_pct
       && q.party[0].reflect_turns == p.party[0].reflect_turns,
          "the ward's reflect survives a save");
    check(q.party[0].stance_atk_pct == p.party[0].stance_atk_pct,
          "and the ground you were standing on");
    check(q.training_writs == p.training_writs, "and the writs you were owed");

    printf("  %d larder grades, rep, nights and lifetime take all round-trip\n",
           MEAT_GRADE_COUNT);

    free(m); free(m2);
    restore_scratch_state_dir();
}

/* The black market. Deliberately unguarded, so what is pinned here is not
   "you cannot abuse it" -- you can, by design -- but that the sale is the
   exact inverse of the level-up, and that it stops at level 1 rather than
   running the stat maths off the bottom. */
static void test_black_market(void) {
    printf("the black market\n");

    /* Level up N times, sell N times, and land back exactly where you were.
       If grant_xp and player_sell_level ever drift apart, this catches it --
       the defence-on-even-levels rule is the easy half to get wrong. */
    for (int target = 2; target <= 40; target++) {
        Player a; memset(&a, 0, sizeof a);
        a.party[0].level = 1; a.party[0].maxhp = a.party[0].hp = 56;
        a.party[0].base_atk = 15; a.party[0].base_def = 14;
        a.party[0].xp_next = 20;

        Player before = a;

        /* Climb by handing over exactly the experience each level wants. */
        while (a.party[0].level < target) grant_xp(&a, &a.party[0], a.party[0].xp_next);

        Player peak = a;
        while (a.party[0].level > 1) check(player_sell_level(&a), "a level above the first always sells");

        check(a.party[0].level     == before.party[0].level,     "selling all the way back restores the level");
        check(a.party[0].maxhp     == before.party[0].maxhp,     "and the hit points");
        check(a.party[0].base_atk  == before.party[0].base_atk,  "and the attack");
        check(a.party[0].base_def  == before.party[0].base_def,  "and the defence");
        check(a.party[0].xp_next   == before.party[0].xp_next,   "and what the next level costs");
        check(peak.party[0].level  == target,           "the climb went where it was told");
    }

    /* Level 1 is the floor, and it is the only refusal. */
    {
        Player a; memset(&a, 0, sizeof a);
        a.party[0].level = 1; a.party[0].maxhp = a.party[0].hp = 56; a.party[0].base_atk = 15; a.party[0].base_def = 14;
        check(!player_sell_level(&a), "level 1 is the bottom");
        check(a.party[0].level == 1 && a.party[0].maxhp == 56, "and a refused sale changes nothing");
    }

    /* Selling forfeits progress toward the next level. */
    {
        Player a; memset(&a, 0, sizeof a);
        a.party[0].level = 5; a.party[0].maxhp = a.party[0].hp = 90; a.party[0].base_atk = 20; a.party[0].base_def = 16;
        a.party[0].xp = 47; a.party[0].xp_next = 80;
        check(player_sell_level(&a), "it sells");
        check(a.party[0].xp == 0, "progress toward the next level is forfeit");
        check(a.party[0].xp_next == 20 + (a.party[0].level - 1) * 15, "and the bar is re-cut for the new level");
    }

    /* The price is quadratic in level while re-earning one is linear -- that
       asymmetry IS the farming loop, and it is intended. Stated as a test so
       nobody later "fixes" it without meaning to. */
    check(level_sale_price(1, 40) == 0, "level 1 is worth nothing");
    for (int L = 3; L <= 60; L++)
        check(level_sale_price(L, 40) >= level_sale_price(L - 1, 40),
              "a deeper level is never worth less");
    /* The price must not know where you have been.
     *
       Keying it to deepest_floor was meant to stop level-1 farming and did
       -- while making it strictly better to touch a deep floor once and then
       farm a safe shallow one at thirty-three times the rate. A flat rate per
       point of experience lets the experience curve price depth instead, and
       that curve already pays a floor-94 lap forty-seven times a floor-3 lap
       for the same walking. */
    for (int d = 1; d <= 100; d += 9)
        check(level_sale_price(40, d) == level_sale_price(40, 1),
              "how deep you once went prices nothing");

    /* And a level is worth exactly the experience it cost, so there is no
       rung of the ladder that is better to grind than any other. */
    for (int L = 2; L <= 80; L++) {
        long xp = 20 + (long)(L - 1) * 15;
        check(level_sale_price(L, 40) == MARKET_GOLD_PER_XP * xp,
              "a level sells for what it cost to earn");
    }

    /* No cap of any kind: sell, re-earn, sell again, as often as you like. */
    {
        Player a; memset(&a, 0, sizeof a);
        a.party[0].level = 1; a.party[0].maxhp = a.party[0].hp = 56; a.party[0].base_atk = 15; a.party[0].base_def = 14;
        a.party[0].xp_next = 20;
        long total = 0;
        for (int cycle = 0; cycle < 50; cycle++) {
            while (a.party[0].level < 6) grant_xp(&a, &a.party[0], a.party[0].xp_next);
            while (a.party[0].level > 1) { total += level_sale_price(a.party[0].level, 40); player_sell_level(&a); }
        }
        check(total > 0, "the loop pays every time round, by design");
    }

    /* Selling twenty levels in one go must be identical -- in gold and in
       every stat -- to selling one twenty times. The screen quotes the batch
       price before the transaction runs level by level, so if those two ever
       disagree the player is quoted a number they do not get paid. */
    for (int L = 5; L <= 70; L += 5) {
        for (int n = 1; n <= L - 1 && n <= 20; n += 3) {
            Player a; memset(&a, 0, sizeof a);
            a.party[0].level = L; a.party[0].maxhp = a.party[0].hp = 56 + 8 * (L - 1);
            a.party[0].base_atk = 15 + (L - 1); a.party[0].base_def = 14 + (L - 1) / 2;

            long quoted = level_sale_batch_price(L, n, 40);
            int  def_before = a.party[0].base_def;

            long walked = 0;
            int  sold = 0;
            while (sold < n && a.party[0].level > 1) {
                walked += level_sale_price(a.party[0].level, 40);
                player_sell_level(&a);
                sold++;
            }

            check(walked == quoted, "the quoted batch price is what you get paid");
            check(a.party[0].level == L - sold, "and you land on the level the screen said");
            check(def_before - a.party[0].base_def == level_sale_def_loss(L, n),
                  "and lose the defence the screen said");
        }
    }

    /* Selling everything lands on level 1 exactly, never past it. */
    for (int L = 2; L <= 60; L++) {
        Player a; memset(&a, 0, sizeof a);
        a.party[0].level = L; a.party[0].maxhp = a.party[0].hp = 56 + 8 * (L - 1);
        a.party[0].base_atk = 15 + (L - 1); a.party[0].base_def = 14 + (L - 1) / 2;
        int all = level_sale_max(L);
        for (int i = 0; i < all; i++) player_sell_level(&a);
        check(a.party[0].level == 1, "selling the lot stops at the first level");
        check(!player_sell_level(&a), "and will not go further");
    }

    printf("  sale is the exact inverse of the level-up; batch matches one-at-a-time\n");
    printf("  a level pays %ld gold per xp point it cost, wherever you earned it\n",
           (long)MARKET_GOLD_PER_XP);
}

/* Stone first.
 *
   The biome wash shipped covering every neutral tile, which fixed "the floor
   is grey" by making the floor green -- one flat colour for another. A jungle
   grown over a city is stone with green through it, and the stone showing
   between the growth is what makes the growth read as growth. This pins the
   proportion on every biome, because the failure was invisible to every test
   that existed and obvious the moment somebody looked at it. */
static void test_growth_is_patchy(void) {
    printf("stone with growth through it\n");

    Map *m = calloc(1, sizeof *m);
    Player p; memset(&p, 0, sizeof p);
    p.run_seed = 4242;

    const int FLOORS[] = { 1, 25, 50, 75, 95 };   /* one per biome band */

    for (unsigned i = 0; i < sizeof FLOORS / sizeof *FLOORS; i++) {
        int gx, gy;
        generate_temple_floor(m, FLOORS[i], &gx, &gy, &p);

        long n = 0, bare = 0, biome = 0, dist = 0;
        for (int y = 0; y < MAP_H; y++) {
            for (int x = 0; x < MAP_W; x++) {
                TileType t = m->tiles[y][x].type;
                if (t != TILE_FLOOR && t != TILE_WALL &&
                    t != TILE_DECOR && t != TILE_THICKET) continue;
                switch (terrain_wash_at(m, x, y)) {
                    case 2: dist++;  break;
                    case 1: biome++; break;
                    default: bare++; break;
                }
                n++;
            }
        }
        check(n > 0, "the floor has neutral terrain on it");

        /* Stone has to stay the material the place is built from. */
        check(bare * 100 / n >= 55, "most of a floor is still stone");
        check(bare * 100 / n <= 90, "but not so much that nothing grows");

        /* And something has to be growing, on every biome -- this is the
           assertion that would have caught the floor being bare rock with a
           name over it, and its opposite. */
        check((biome + dist) * 100 / n >= 10, "growth is visible on every biome");
    }

    /* The speckle must be stable: the same tile takes the same colour every
       frame, or the floor shimmers as you walk. */
    {
        int gx, gy;
        generate_temple_floor(m, 12, &gx, &gy, &p);
        for (int y = 5; y < 40; y++)
            for (int x = 5; x < 60; x++)
                check(terrain_wash_at(m, x, y) == terrain_wash_at(m, x, y),
                      "a tile's colour does not change between reads");
    }

    printf("  most of every floor stays stone; growth shows through on all five biomes\n");
    free(m);
}


/* ---- the guardian angel (dda.h phase two) ---------------------------
   Three properties it has to have, and one it must not: it must be silent
   above the "hurt" line, it must grow as death approaches, it must be
   switchable off for a control, and it must never exceed its own cap. The
   last matters because it is subtracted from MONSTER_CRIT_PCT -- a grace
   larger than that constant would make the unavoidable critical impossible
   rather than rarer. */
static void test_guardian_angel(void) {
    printf("guardian angel\n");

    Player p;
    memset(&p, 0, sizeof(p));
    apply_class_to_player(&p, 0);
    p.party[0].maxhp = 100;

    /* Off unless asked for. It is a cheat, not a difficulty, and a cheat that
       is on by default is a difficulty -- it would also have silently moved
       every winnability number in EVALUATION.md. */
    p.floor = 1; p.party[0].hp = 100;
    check(guardian_strength() == 0, "the angel is off by default");
    check(guardian_grace(&p) == 0,  "and grants nothing until switched on");

    guardian_set_strength(100);

    /* The escort: large at the top, gone by the middle, and monotone in
       between. This is the half that makes the opening a cakewalk, so it has
       to be checked at full health -- the first version of the angel only
       existed below 45% and measured as doing nothing at all. */
    p.party[0].hp = 100;
    p.floor = 1;   int top    = guardian_grace(&p);
    p.floor = 10;  int early  = guardian_grace(&p);
    p.floor = 25;  int middle = guardian_grace(&p);
    p.floor = 50;  int gone   = guardian_grace(&p);
    p.floor = 80;  int deep   = guardian_grace(&p);

    check(top > 0,           "a healthy character IS helped on floor 1");
    check(top > early,       "and less so by floor 10");
    check(early > middle,    "and less again by floor 25");
    check(gone == 0,         "and not at all by the middle of the descent");
    check(deep == 0,         "nor anywhere below it");
    /* Deliberately a nudge and not a wall. The version that was large enough
       to be a cakewalk on its own measured as making fights *less* close --
       health between floors went up, not down -- and stopped being the kind
       of game anybody stays up for. The escort tilts; the screen saves. */
    check(top > 5 && top < 30, "the escort tilts the dice without ending the fight");

    p.floor = 0;
    check(guardian_grace(&p) == 0, "town is not helped");

    /* The net: small, conditional, and still there at the bottom. */
    p.floor = 80;
    p.party[0].hp = 100; check(guardian_grace(&p) == 0, "deep and healthy is on its own");
    p.party[0].hp = 44;  check(guardian_grace(&p) == 0, "and the hurt line itself is still nothing");
    /* Not 44: the ramp starts at zero on the 45% line by design -- no step
       for a player to notice or to game -- so a point under it rounds to
       nothing. That is the intended shape, not a dead zone. */
    p.party[0].hp = 35;  int hurt   = guardian_grace(&p);
    p.party[0].hp = 20;  int routed = guardian_grace(&p);
    p.party[0].hp = 1;   int dying  = guardian_grace(&p);
    check(hurt > 0,        "deep and hurt still gets the net");
    check(routed > hurt,   "and more of it when routed");
    check(dying > routed,  "and most at death's door");

    /* Both halves at once, at the top of the dungeon. */
    p.floor = 1; p.party[0].hp = 1;
    check(guardian_grace(&p) > top, "escort and net stack");

    /* The unavoidable critical must stay possible. It is the one attack that
       ignores every defence a character has built, deliberately, so an angel
       that switched it off would make someone at 10% health immune to the
       only thing that was going to reach them. */
    check(guardian_softened_crit(5, &p) >= 1, "the monster critical is never erased");
    check(guardian_softened_crit(5, &p) < 5,  "but it is rarer when you are dying");
    check(guardian_softened_crit(5, &p) >= 5 / 2,
          "and halved at most, however hard the mode's angel works");
    p.floor = 80; p.party[0].hp = 100;
    check(guardian_softened_crit(5, &p) == 5, "and untouched when you are well");
    check(guardian_softened_crit(0, &p) == 0, "a zero base stays zero");

    /* The control has to actually be a control. */
    p.floor = 1; p.party[0].hp = 100;
    guardian_set_strength(0);
    check(guardian_grace(&p) == 0, "strength 0 disables it completely");
    guardian_set_strength(50);
    int half = guardian_grace(&p);
    guardian_set_strength(100);
    check(half > 0 && half < guardian_grace(&p), "and strength scales it between");

    /* The screen: the DM's actual move. Converts a killing blow into one hit
       point, once per floor, only in the opening. */
    /* Per mode. The same cheat, working as hard as the mode requires: a
       player who switches the master on is asking to see the game, and on
       Hardcore that costs far more than on Normal. Hardcore is still
       Hardcore -- permadeath, no recall, a tenth of the experience. */
    guardian_set_strength(100);
    p.floor = 1; p.party[0].hp = 100;
    p.difficulty = DIFFICULTY_NORMAL;   int g_normal = guardian_grace(&p);
    p.difficulty = DIFFICULTY_HARD;     int g_hard   = guardian_grace(&p);
    p.difficulty = DIFFICULTY_SWARM;    int g_swarm  = guardian_grace(&p);
    p.difficulty = DIFFICULTY_HARDCORE; int g_hc     = guardian_grace(&p);
    check(g_hard > g_normal, "Hard gets a harder-working angel than Normal");
    check(g_swarm > g_hard,  "Swarm harder still");
    check(g_hc > g_swarm,    "and Hardcore most of all -- it has no recall to run home with");
    p.difficulty = DIFFICULTY_NORMAL;

    guardian_floor_reset();

    p.floor = 3; p.party[0].maxhp = 100; p.party[0].hp = 0;
    check(guardian_catch(&p), "a killing blow in the opening is caught");
    check(p.party[0].hp == 1, "and leaves exactly one hit point, not a heal");

    p.party[0].hp = 0;
    check(!guardian_catch(&p), "but only once on a floor -- the second is yours");

    guardian_floor_reset();
    p.party[0].hp = 0;
    check(guardian_catch(&p), "and it rearms on the next floor");

    /* Not a resurrection, and not for the second half of the game. */
    guardian_floor_reset();
    p.party[0].hp = 40;
    check(!guardian_catch(&p), "a living character is not 'caught'");

    guardian_floor_reset();
    p.floor = 80; p.party[0].hp = 0;
    check(!guardian_catch(&p), "past the middle the screen comes down");

    guardian_floor_reset();
    p.floor = 0; p.party[0].hp = 0;
    check(!guardian_catch(&p), "and town never needed it");

    guardian_floor_reset();
    guardian_set_strength(0);
    p.floor = 3; p.party[0].hp = 0;
    check(!guardian_catch(&p), "and the cheat being off means off");
    guardian_set_strength(100);

    /* The avenging half. A master who only ever saves you is running a
       cutscene; the other half of the job is noticing the table is bored. */
    guardian_set_strength(100);
    Player q;
    memset(&q, 0, sizeof(q));
    apply_class_to_player(&q, 0);
    q.party[0].maxhp = 100; q.party[0].hp = 100; q.floor = 10;

    q.dda_pressure = 0;    check(avenger_escalation(&q) == 0, "an even run is left alone");
    q.dda_pressure = 10;   check(avenger_escalation(&q) == 0, "and so is a merely comfortable one");
    q.dda_pressure = -30;  check(avenger_escalation(&q) == 0,
                                 "and a drowning run is NEVER pushed further down");

    q.dda_pressure = 20;   int leaning  = avenger_escalation(&q);
    q.dda_pressure = DDA_MAX; int bored = avenger_escalation(&q);
    check(leaning > 0,       "a coasting run gets something in the corridor");
    check(bored > leaning,   "and a thoroughly bored one gets more");
    check(bored <= 25,       "but the master leans, they do not rewrite the encounter");

    /* The crossfade: the two faces trade places over the descent. The escort
       is loudest at the top and gone by the middle; the avenger is quietest
       at the top and at full weight from the middle down. Both are present
       throughout -- a second playthrough should not get to coast the opening
       untouched, and the deep game should not be unsurvivable. */
    q.dda_pressure = DDA_MAX;
    q.floor = 1;   int av_top    = avenger_escalation(&q);
    q.floor = 25;  int av_mid    = avenger_escalation(&q);
    q.floor = 50;  int av_middle = avenger_escalation(&q);
    q.floor = 95;  int av_deep   = avenger_escalation(&q);

    check(av_top > 0,          "a steamrolling opening is still noticed");
    check(av_mid > av_top,     "and the master leans harder as you descend");
    check(av_middle > av_mid,  "and harder again by the middle");
    check(av_deep == av_middle,"and stays at full weight below it, not beyond");

    q.party[0].hp = 100; q.floor = 1;
    guardian_set_strength(100);
    check(guardian_grace(&q) > 0 && av_top < av_middle,
          "so the top of the dungeon is escort-heavy and the bottom avenger-heavy");

    guardian_set_strength(0);
    q.dda_pressure = DDA_MAX;
    check(avenger_escalation(&q) == 0, "and both faces are off together");

    printf("  a nudge, a screen, a net -- and something in the corridor when you coast\n");
}


/* ---- take control: the MC is a role, not a character ----------------
   "As soon as we change to the new hero, he de facto becomes the MC." Every
   property below is that one sentence, checked. */
static void test_take_control(void) {
    printf("take control\n");

    static Player p;
    memset(&p, 0, sizeof(p));
    apply_class_to_player(&p, 0);
    p.party[0].hp = 50; p.party[0].maxhp = 50; p.party[0].x = 10; p.party[0].y = 10;

    check(p.controlled == 0, "a new character drives themselves");
    check(party_x(&p) == 10 && party_y(&p) == 10, "and the party's eyes are theirs");
    check(party_body(&p) == NULL, "with no hire holding the role");
    check(party_anyone_alive(&p), "and somebody is standing");

    /* Nobody to hand it to yet. */
    check(!party_switch_next(&p), "with no hires, there is nobody to switch to");
    check(p.controlled == 0, "and the role does not move");

    /* Two hires, standing somewhere else. */
    p.party[(0) + 1].in_use = true; p.party[(0) + 1].alive = true;
    p.party[(0) + 1].x = 20; p.party[(0) + 1].y = 21; p.party[(0) + 1].hp = 30;
    snprintf(p.party[(0) + 1].name, sizeof(p.party[(0) + 1].name), "Kesh");
    p.party[(2) + 1].in_use = true; p.party[(2) + 1].alive = true;
    p.party[(2) + 1].x = 30; p.party[(2) + 1].y = 31; p.party[(2) + 1].hp = 30;
    snprintf(p.party[(2) + 1].name, sizeof(p.party[(2) + 1].name), "Bel");

    check(party_switch_next(&p), "with hires, the role can move");
    check(p.controlled == 1, "to the first one standing");
    check(party_body(&p) != NULL, "which is now the body being driven");
    check(party_x(&p) == 20 && party_y(&p) == 21,
          "and the camera and the eyes go with it, not with the character");

    /* Skips the empty slot, wraps back through the character. */
    check(party_switch_next(&p) && p.controlled == 3, "empty slots are skipped");
    check(party_switch_next(&p) && p.controlled == 0, "and it wraps to the character");

    /* The dead are not drivable, and a dead body never holds the role. */
    p.controlled = 1;
    p.party[(0) + 1].alive = false;
    check(party_body(&p) == NULL, "a body that has died cannot be driven");
    check(party_x(&p) == 10, "and the eyes fall back to the character");

    /* The torch passes rather than the run ending. */
    p.controlled = 0;
    p.party[0].hp = 0;                                   /* the character falls */
    check(party_anyone_alive(&p), "a fallen character is not the end -- Bel is up");
    check(party_pass_the_torch(&p), "so the role passes");
    check(p.controlled == 3, "to the one still standing");
    check(party_x(&p) == 30 && party_y(&p) == 31, "who is now the main character");

    /* And the real game over. */
    p.party[(2) + 1].alive = false;
    check(!party_anyone_alive(&p), "nobody left standing is the game over");
    check(!party_pass_the_torch(&p), "and there is no torch to pass");

    printf("  the role moves, the eyes follow it, and it outlives any one body\n");
}

/* ---- everybody can be taught -------------------------------------------
   Reported from play: a hired hero taken over showed "School: ?" at the Guild
   and "The guild has nothing to teach a ? caster". companion_equip() blanked
   magic_school to -1 and only restored it for Arcanists, so every other hire
   was locked out of an entire system by a sentinel written back when a hire
   could never walk into a shop.

   Every class has a school -- compute_magic_school() spreads even the classes
   with no magic attribute at all, precisely so nobody is excluded -- and a
   hire is a class like any other. */
/* ---- an ally is not a wall ---------------------------------------------
   Reported from play: "when two or more @ are nearby the autoexplore is
   stopping." The one-actor-per-tile rule was enforced by refusing the step,
   and the path searches only know about terrain -- so a delegated walk would
   route onto a party member's square, be refused, spend no turn, and the
   stuck detector would fire. Allies swap places instead. */
/* ---- a blow is a blow, whoever lands it ---------------------------------
   Five separate AI damage paths -- strike, spell, shot, blast, ability -- each
   did `mo->hp -= dmg` and its own bookkeeping, never touching
   monster_take_damage(). So a monster killed by a hire produced no gold, no
   bounty progress, no larder, and no boss kill: a hire landing the last blow
   on the Warden left it dead with the run still going, because STATE_WIN is
   only ever reached through the flag that function sets. */
static void test_ai_kills_count(void) {
    printf("a blow is a blow\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tp, 0, sizeof(g_tp)); memset(&g_tm, 0, sizeof(g_tm));
    apply_class_to_player(&g_tp, 0);
    g_tp.gold = 1000000; g_tp.tavern_seed = 77u; g_tp.party[0].level = 10; g_tp.floor = 100;

    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            g_tm.tiles[y][x].type = (x == 0 || y == 0 || x == MAP_W-1 || y == MAP_H-1)
                                 ? TILE_WALL : TILE_FLOOR;
            g_tm.tiles[y][x].visible = g_tm.tiles[y][x].seen = true;
        }
    g_tm.monster_count = 0; g_tm.item_count = 0; g_tm.feature_count = 0; g_tm.floor_num = 100;
    g_tp.party[0].x = 20; g_tp.party[0].y = 20;
    check(companion_hire(&g_tp, 0), "a hire is taken on");
    g_tp.party[1].x = 30; g_tp.party[1].y = 20;
    g_tp.party[1].base_atk = 9999;                 /* one blow, whatever it swings */

    Monster *boss = &g_tm.monsters[g_tm.monster_count++];
    memset(boss, 0, sizeof(*boss));
    snprintf(boss->name, sizeof(boss->name), "The Warden");
    boss->x = 31; boss->y = 20; boss->hp = boss->maxhp = 40;
    boss->alive = true; boss->aggro = true; boss->glyph = 'W';
    boss->is_boss = true; boss->xp_reward = 50; boss->gold_reward = 500;

    g_tp.quest_active = true; g_tp.quest_type = QUEST_CLEAR;
    g_tp.quest_floor = 100; g_tp.quest_kills_needed = 5; g_tp.quest_kills_done = 0;
    int gold_before = g_tp.gold;

    for (int t = 0; t < 40 && boss->alive; t++) companions_take_turn(&g_tp, &g_tm);

    check(!boss->alive, "a hire can kill the Warden");
    check(companions_boss_felled(), "and the run registers it -- the win is not silently voided");
    check(g_tp.gold > gold_before, "the party is paid for a kill a hire landed");
    check(g_tp.quest_kills_done > 0, "and the bounty counts it");
    check(g_tp.party[1].kills > 0, "the kill is credited to the body that landed it");
}

/* ---- nobody is out of reach ---------------------------------------------
   companion_at() answered "is one of my hires here", which was the same
   question as "is an undriven body here" only while the character could never
   be undriven. Monsters find someone to hit through it, so while it scanned
   slots 1..5 the character was invulnerable whenever you played somebody
   else. companions_place() had the mirror image: it never repositioned slot 0,
   so descending as a hire left the character on the previous floor's
   coordinates -- measured inside solid terrain. */
static void test_no_body_is_special(void) {
    printf("nobody is out of reach\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tp, 0, sizeof(g_tp)); memset(&g_tm, 0, sizeof(g_tm));
    apply_class_to_player(&g_tp, 0);
    g_tp.gold = 100000000; g_tp.tavern_seed = 12u; g_tp.party[0].level = 8; g_tp.floor = 3;
    generate_temple_floor(&g_tm, 3, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
    check(companion_hire(&g_tp, 0), "a hire is taken on");
    companions_place(&g_tp, &g_tm);

    g_tp.controlled = 1;                       /* play the hire, leave the character */
    check(companion_at(&g_tp, g_tp.party[0].x, g_tp.party[0].y) == &g_tp.party[0],
          "an undriven character is a body a monster can find");
    check(companion_at(&g_tp, g_tp.party[1].x, g_tp.party[1].y) == NULL,
          "and the body being driven is not, since it is handled on its own");

    int ox = g_tp.party[0].x, oy = g_tp.party[0].y;
    int ux, uy;
    generate_temple_floor(&g_tm, 4, &ux, &uy, &g_tp);
    g_tp.party[1].x = ux; g_tp.party[1].y = uy;   /* the driven body takes the stairs */
    companions_place(&g_tp, &g_tm);
    check(!(g_tp.party[0].x == ox && g_tp.party[0].y == oy),
          "descending as a hire still moves the character to the new floor");
    check(is_walkable_player(&g_tm, g_tp.party[0].x, g_tp.party[0].y),
          "and puts them somewhere they can actually stand");
}

/* ---- DDA reads a floor you never leave -------------------------------
   dda_floor_end() is the only thing that files a verdict and it runs when a
   floor is *left*. A run that dies on floor 1 never leaves it, so pressure
   read a flat zero -- and that is the median run in all three hard modes
   (EVALUATION 86). Any help-the-struggling-player system riding on pressure
   would have helped exactly the players who were already fine. */
/* ---- what the place does for you --------------------------------------
   The band gifts (bands.h). Three properties, which are the three decisions
   the project owner made, stated as things that must hold:

     asymmetric -- a gift appears when a run is going badly and never becomes a
                   penalty when it is going well;
     bounded    -- one knob, and nothing exceeds it;
     silent     -- nothing here writes to the message log, which is what
                   "invisible" means in code.

   Plus the one that is not a decision but a defect the first cut had: on the
   band where a run actually dies, a run that is actually dying has to get
   something. Floored division gave it nothing. */
/* ---- Phase 3 content -------------------------------------------------
   Three ROADMAP items: a rule per biome, a proc per completed gear set, and
   three more bounty kinds. Each is asserted as the property that makes it
   worth having rather than as the numbers it currently uses. */
/* ---- the data tables ---------------------------------------------------
   ROADMAP 2.6 asks for these by name: spell-pool integrity, class-table
   completeness, no name overflows in the procedural generators.

   They are worth having for a specific reason this session made concrete. A
   table entry with a wrong field is a *correct program doing the wrong thing*
   -- it compiles, it runs, it never crashes, and it is invisible until a
   player walks into the one screen that reads it. That is precisely how a
   sentinel of -1 in magic_school became "the guild has nothing to teach a ?
   caster" and stayed there. Nothing else in the harness looks at data. */
/* ---- the delegated walk ------------------------------------------------
   Auto-explore's decisions, which could not be tested at all until they came
   out of main.c -- the suite links every module except main.o.

   The two defects this session found in it were both decisions, and both were
   found by playing: a stuck-detector watching the wrong body (§92), and an
   ability that would have been spent on a rat had it been added carelessly
   (2.1b). Both are assertions now. */
static void test_autoexplore_decisions(void) {
    printf("the delegated walk\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tp, 0, sizeof(g_tp)); memset(&g_tm, 0, sizeof(g_tm));
    apply_class_to_player(&g_tp, 0);
    g_tp.party[0].maxhp = 300; g_tp.party[0].hp = 300; g_tp.party[0].level = 10;
    g_tp.floor = 5;
    g_tp.gold = 100000000; g_tp.tavern_seed = 31u;   /* enough to take somebody on */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            g_tm.tiles[y][x].type = (x == 0 || y == 0 || x == MAP_W-1 || y == MAP_H-1)
                                    ? TILE_WALL : TILE_FLOOR;
            g_tm.tiles[y][x].visible = g_tm.tiles[y][x].seen = true;
        }
    g_tm.floor_num = 5; g_tm.monster_count = 0;
    g_tp.party[0].x = 20; g_tp.party[0].y = 20;

    Monster rat;  memset(&rat, 0, sizeof rat);
    snprintf(rat.name, sizeof rat.name, "rat");
    rat.alive = true; rat.hp = rat.maxhp = 50; rat.x = 24; rat.y = 20;

    Monster boss = rat; snprintf(boss.name, sizeof boss.name, "Warden");
    boss.is_boss = true;
    Monster elite = rat; snprintf(elite.name, sizeof elite.name, "elite");
    elite.is_elite = true;

    /* 2.1b: the ability is for the rare thing. This is the whole decision, and
       it is the one the roadmap left as a judgement call. */
    g_tp.party[0].ability_cd = 0;
    check(autoexplore_decide_fight(&g_tp, &g_tm, &rat, 4).kind != AX_ABILITY,
          "the class ability is not spent on a rat");
    check(autoexplore_decide_fight(&g_tp, &g_tm, &elite, 4).kind == AX_ABILITY,
          "but it is spent on an elite");
    check(autoexplore_decide_fight(&g_tp, &g_tm, &boss, 4).kind == AX_ABILITY,
          "and on a boss");

    /* An ability on cooldown is not an option, and must not stall the hunt. */
    g_tp.party[0].ability_cd = 5;
    AxFight busy = autoexplore_decide_fight(&g_tp, &g_tm, &boss, 4);
    check(busy.kind != AX_ABILITY && busy.kind != AX_NOTHING,
          "an ability on cooldown falls through to something else");
    g_tp.party[0].ability_cd = 0;

    /* Adjacent is a swing, whatever else is available -- walking into it is
       the attack, and spending a spell at arm's length is worse. */
    check(autoexplore_decide_fight(&g_tp, &g_tm, &boss, 1).kind == AX_MELEE,
          "anything adjacent is hit rather than shot");

    /* And the bound: a hunt is not a floor-clear. */
    check(autoexplore_decide_fight(&g_tp, &g_tm, &rat, AUTOEXPLORE_HUNT_RADIUS + 1).kind == AX_NOTHING,
          "nothing beyond the hunt radius is chased");
    check(autoexplore_decide_fight(&g_tp, &g_tm, NULL, 3).kind == AX_NOTHING,
          "and no target is no decision");

    /* §92: the progress signature has to move when the *driven* body moves.
       Reading slot 0 while the walk moved somebody else is what made the walk
       report itself stuck the moment the character's AI settled. */
    check(companion_hire(&g_tp, 0), "a hire is taken on");
    g_tp.party[1].x = 40; g_tp.party[1].y = 40;
    g_tp.controlled = 1;
    unsigned long before = autoexplore_progress(&g_tp, &g_tm);
    g_tp.party[1].x = 41;
    check(autoexplore_progress(&g_tp, &g_tm) != before,
          "moving the driven body counts as progress");
    before = autoexplore_progress(&g_tp, &g_tm);
    g_tp.party[0].x = 21;
    check(autoexplore_progress(&g_tp, &g_tm) == before,
          "and moving somebody else does not");
    g_tp.controlled = 0;

    /* The route: hazards are avoided when there is a way round and crossed
       when there is not, which is the two-pass rule every leg of it uses. */
    check(autoexplore_passable(&g_tm, 25, 20, true), "clean floor is passable either way");
    g_tm.tiles[20][25].type = TILE_LAVA;
    check(!autoexplore_passable(&g_tm, 25, 20, true), "lava is refused on the careful pass");
    check(autoexplore_passable(&g_tm, 25, 20, false), "and accepted when there is no other way");
    g_tm.tiles[20][25].type = TILE_FLOOR;

    /* Never take the way back up: a delegated walk that climbs out has ended
       a run the player did not ask to end. */
    g_tm.stairs_down_x = 60; g_tm.stairs_down_y = 20;
    g_tm.tiles[20][60].type = TILE_STAIRS_DOWN;
    for (int d = 0; d < 8; d++) {
        int nx = g_tp.party[0].x + PATH_DX[d], ny = g_tp.party[0].y + PATH_DY[d];
        if (nx > 0 && nx < MAP_W-1 && ny > 0 && ny < MAP_H-1)
            g_tm.tiles[ny][nx].type = TILE_STAIRS_UP;
    }
    int dx = 0, dy = 0; bool known = false;
    bool routed = autoexplore_route(&g_tp, &g_tm, &dx, &dy, &known);
    if (routed) {
        int nx = g_tp.party[0].x + dx, ny = g_tp.party[0].y + dy;
        check(g_tm.tiles[ny][nx].type != TILE_STAIRS_UP,
              "a delegated walk never steps onto the way back up");
    } else {
        check(true, "a delegated walk never steps onto the way back up");
    }

    printf("  the hunter's decisions, out of main.c and finally assertable\n");
}

static void test_spell_pool_integrity(void) {
    printf("the spell pool\n");

    int n = spell_pool_count();
    check(n > 0, "there is a pool at all");

    int bad_school = 0, bad_level = 0, bad_effect = 0, bad_name = 0, bad_cost = 0;
    for (int i = 0; i < n; i++) {
        const SpellTemplate *t = spell_pool_get(i);
        if (!t) { bad_name++; continue; }
        if (t->school < 0 || t->school >= SCHOOL_COUNT) bad_school++;
        if (t->level  < 1 || t->level  > 5)             bad_level++;
        if (t->effect < 0 || t->effect >= EFFECT_COUNT) bad_effect++;
        if (t->name[0] == '\0')                         bad_name++;
        /* A spell that costs nothing and never cools down is a spell you cast
           every turn for ever; the pool is priced on both being real. */
        if (t->charge_cost <= 0 || t->cooldown < 0 || t->learn_price <= 0) bad_cost++;
    }
    check(bad_school == 0, "every spell belongs to a real school");
    check(bad_level  == 0, "every spell has a level between 1 and 5");
    check(bad_effect == 0, "every spell has an effect the caster can resolve");

    /* ---- and each school does more than one kind of thing ----
       Curation fixed identity and left mechanics alone: a school drew on three
       effects and Resonance on *two*, so thirty spells differed by numbers and
       archetype rather than by what they did. The bar is four, which is where
       the variety pass put every school, and it is a bar rather than a count
       because the point is variety and not a particular arrangement of it. */
    for (int school = 0; ; school++) {
        int here = 0, distinct = 0;
        bool used[EFFECT_COUNT];
        memset(used, 0, sizeof used);
        for (int i = 0; i < spell_pool_count(); i++) {
            const SpellTemplate *t = spell_pool_get(i);
            if (!t || t->school != school) continue;
            here++;
            if (t->effect >= 0 && t->effect < EFFECT_COUNT && !used[t->effect]) {
                used[t->effect] = true;
                distinct++;
            }
        }
        if (here == 0) break;              /* past the last school */
        char msg[96];
        snprintf(msg, sizeof msg, "school %d does at least four different things", school);
        check(distinct >= 4, msg);
    }

    /* And the effects the variety pass added are actually *in* the pool --
       an effect nobody casts is an enum value with a comment on it. */
    {
        const int WANTED[] = { EFFECT_CHAIN, EFFECT_PARTY_HEAL, EFFECT_HASTE,
                               EFFECT_REFLECT, EFFECT_SENSE_LIFE, EFFECT_MASS_SLOW };
        for (unsigned w = 0; w < sizeof WANTED / sizeof WANTED[0]; w++) {
            int found = 0;
            for (int i = 0; i < spell_pool_count(); i++) {
                const SpellTemplate *t = spell_pool_get(i);
                if (t && t->effect == WANTED[w]) found++;
            }
            char msg[96];
            snprintf(msg, sizeof msg, "effect %d is on somebody's spell list", WANTED[w]);
            check(found > 0, msg);
        }
    }
    check(bad_name   == 0, "every spell has a name");
    check(bad_cost   == 0, "and a charge cost, a cooldown and a price");

    /* The guild reads the pool through these two, so they have to agree with
       it -- a school that reports spells it cannot index is a menu that shows
       a spell nobody can buy. */
    int indexed = 0, mismatched = 0;
    for (int s = 0; s < SCHOOL_COUNT; s++) {
        int cnt = spell_school_count(s);
        if (cnt <= 0) { mismatched++; continue; }
        for (int k = 0; k < cnt; k++) {
            int idx = spell_school_index(s, k);
            const SpellTemplate *t = (idx >= 0 && idx < n) ? spell_pool_get(idx) : NULL;
            if (!t || t->school != s) { mismatched++; break; }
            indexed++;
        }
    }
    check(mismatched == 0, "every school indexes only its own spells");
    check(indexed == n, "and between them the schools account for the whole pool");

    /* Every school must be able to arm a level-1 caster, or a character
       assigned to it starts with an empty spellbook and no way to fill it. */
    int schools_without_a_first_spell = 0;
    for (int s = 0; s < SCHOOL_COUNT; s++) {
        bool found = false;
        for (int k = 0; k < spell_school_count(s) && !found; k++) {
            const SpellTemplate *t = spell_pool_get(spell_school_index(s, k));
            if (t && t->level == 1) found = true;
        }
        if (!found) schools_without_a_first_spell++;
    }
    check(schools_without_a_first_spell == 0,
          "and every school has a level-1 spell to start somebody on");

    printf("  %d spells, %d schools, all of them real\n", n, SCHOOL_COUNT);
}

static void test_class_table_complete(void) {
    printf("the class table\n");

    int bad_name = 0, bad_arch = 0, bad_kit = 0, bad_override = 0;
    for (int i = 0; i < NUM_CLASSES; i++) {
        const ClassSeed *c = &CLASS_TABLE[i];
        if (!c->name || !c->name[0] || !c->tagline || !c->tagline[0]) bad_name++;
        if (c->archetype < 0 || c->archetype >= ARCHETYPE_COUNT) bad_arch++;
        if (!c->start_weapon_name || !c->start_weapon_name[0]
            || !c->start_armor_name || !c->start_armor_name[0]) bad_kit++;

        /* The overrides list is walked to its ATTR_COUNT sentinel. A list
           without one runs off the end of the table, which is the one defect
           here that is not merely cosmetic. */
        if (!c->overrides) { bad_override++; continue; }
        bool terminated = false;
        for (int k = 0; k < ATTR_COUNT + 4; k++) {
            if (c->overrides[k].attr == ATTR_COUNT) { terminated = true; break; }
            if (c->overrides[k].attr < 0 || c->overrides[k].attr > ATTR_COUNT) { break; }
            if (c->overrides[k].value < 1 || c->overrides[k].value > 12) { bad_override++; break; }
        }
        if (!terminated) bad_override++;
    }
    check(bad_name     == 0, "every class has a name and a tagline");
    check(bad_arch     == 0, "and a real archetype");
    check(bad_kit      == 0, "and something to fight and something to wear");
    check(bad_override == 0, "and an attribute list that is terminated and in range");

    /* The Tavern picks a role first and then a class inside it. An archetype
       with no classes makes archetype_class_index return -1, and a hire built
       from -1 is class 0 wearing somebody else's role. */
    int empty_archetypes = 0;
    for (int a = 0; a < ARCHETYPE_COUNT; a++)
        if (archetype_class_count(a) <= 0) empty_archetypes++;
    check(empty_archetypes == 0, "and every archetype has at least one class in it");

    /* Names are what the player picks from; two identical ones are two
       different characters the list cannot tell apart. */
    int duplicates = 0;
    for (int i = 0; i < NUM_CLASSES; i++)
        for (int j = i + 1; j < NUM_CLASSES; j++)
            if (CLASS_TABLE[i].name && CLASS_TABLE[j].name
                && strcmp(CLASS_TABLE[i].name, CLASS_TABLE[j].name) == 0) duplicates++;
    check(duplicates == 0, "and no two classes share a name");

    printf("  %d classes across %d archetypes, all of them pickable\n",
           NUM_CLASSES, ARCHETYPE_COUNT);
}

static void test_generated_names_fit(void) {
    printf("names the generators make\n");

    memset(&g_tp, 0, sizeof(g_tp));
    apply_class_to_player(&g_tp, 0);
    g_tp.party[0].level = 20;

    /* A Tavern name is written into Hero.name and shown in a column
       TAVERN_NAME_SHOWN wide. Overflowing the buffer is a bug; overflowing the
       column is a hero whose honorific is the part that gets cut off, which is
       the thing the constant exists to prevent. */
    int too_long_for_buffer = 0, too_long_for_column = 0, empty = 0;
    for (unsigned seed = 1; seed <= 40; seed++) {
        g_tp.tavern_seed = seed * 2654435761u;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero c;
            tavern_candidate(&g_tp, i, &c);
            size_t len = strlen(c.name);
            if (len == 0) empty++;
            if (len >= sizeof(c.name)) too_long_for_buffer++;
            if (len > TAVERN_NAME_SHOWN) too_long_for_column++;
        }
    }
    check(empty == 0, "every hired hero has a name");
    check(too_long_for_buffer == 0, "that fits the field it is stored in");
    check(too_long_for_column == 0, "and the column the Tavern shows it in");

    /* Monster names go into Monster.name and into log lines. */
    int mon_bad = 0;
    for (int floor = 1; floor <= MAX_FLOOR; floor++) {
        for (int t = 0; t < 12; t++) {
            Monster mo = make_monster_for_floor(floor, 5, 5);
            size_t len = strlen(mo.name);
            if (len == 0 || len >= sizeof(mo.name)) mon_bad++;
        }
    }
    check(mon_bad == 0, "every monster the generator makes has a name that fits");

    /* And the quest board's, which is copied into quest_monster. */
    int quest_bad = 0;
    for (int floor = 1; floor <= MAX_FLOOR; floor++) {
        const char *nm = pick_quest_monster_name(floor);
        if (!nm || !nm[0] || strlen(nm) >= sizeof(g_tp.quest_monster)) quest_bad++;
    }
    check(quest_bad == 0, "and every bounty target's name fits the bounty");

    printf("  %d rosters, %d floors of monsters, nothing truncated\n",
           40, MAX_FLOOR);
}

/* ---- the watch, and the barrow ------------------------------------------
   The two districts of the 3.1 tranche, both of which the fuzzer can generate
   and neither of which it can reliably *use*: a random-key player attacks
   everything, so it breaks every watch before it reaches the box, and a
   barrow is rare enough that 80,000 keys never walked into one. Measured, not
   assumed -- the strongbox's "worth the noise you made" line fired 18 times
   across two fuzz runs and its paying line fired none. So the success paths
   are pinned here instead. */
static void test_watch_and_barrow(void) {
    printf("the watch, and the barrow\n");

    /* ---- the watch ---- */
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    world_size_apply(WORLD_SHAFT);

    g_tm.floor_num = 12;
    g_tm.district_count = 1;
    g_tm.districts[0] = (MapDistrict){ 10, 10, 20, 14, DIST_WATCH, 0, 0, WATCH_KEEPING };
    for (int y = 10; y < 24; y++)
        for (int x = 10; x < 30; x++) g_tm.tiles[y][x].type = TILE_FLOOR;

    /* Two wardens on post. */
    g_tm.monster_count = 2;
    for (int i = 0; i < 2; i++) {
        Monster *mo = &g_tm.monsters[i];
        memset(mo, 0, sizeof *mo);
        snprintf(mo->name, sizeof mo->name, "Warden %d", i + 1);
        mo->x = 14 + i * 4; mo->y = 14;
        mo->hp = mo->maxhp = 200; mo->atk = 5; mo->def = 1;
        mo->alive = true; mo->aggro = false;
    }

    MapFeature box;
    memset(&box, 0, sizeof box);
    box.type = FEATURE_STRONGBOX; box.x = 20; box.y = 20;

    int before = g_tp.party[0].gold_bonus_pct;
    trigger_feature(&g_tm, &box, &g_tp);
    check(g_tp.party[0].gold_bonus_pct == before + WATCH_FIND_PCT,
          "a box taken quietly pays a permanent cut of what you find");
    check(g_tm.districts[0].state == WATCH_TAKEN, "and the district records that it is gone");

    /* Broken instead: same box, nothing in it. */
    g_tm.districts[0].state = WATCH_BROKEN;
    box.used = false;
    before = g_tp.party[0].gold_bonus_pct;
    trigger_feature(&g_tm, &box, &g_tp);
    check(g_tp.party[0].gold_bonus_pct == before,
          "and a box taken loudly pays nothing at all");

    /* One blow breaks it, and everything in the district knows. */
    g_tm.districts[0].state = WATCH_KEEPING;
    g_tm.monsters[0].aggro = g_tm.monsters[1].aggro = false;
    monster_take_damage(&g_tp, &g_tp.party[0], &g_tm, &g_tm.monsters[0], 3, NULL);
    check(g_tm.districts[0].state == WATCH_BROKEN, "one blow breaks the watch");
    check(g_tm.monsters[1].aggro, "and the warden who did not see it is coming anyway");

    /* The cap holds however many watches a run walks. */
    g_tp.party[0].gold_bonus_pct = 0;
    for (int i = 0; i < 40; i++) {
        g_tm.districts[0].state = WATCH_KEEPING;
        box.used = false;
        trigger_feature(&g_tm, &box, &g_tp);
    }
    check(g_tp.party[0].gold_bonus_pct <= WATCH_FIND_CAP,
          "and forty of them do not exceed the cap");

    /* ---- the barrow ---- */
    FallenRecord roster[FALLEN_MAX];
    int ghosts = load_fallen(roster, FALLEN_MAX);
    check(ghosts >= GAUNTLET_MIN_GHOSTS, "the scratch roster has somebody on it");
    if (ghosts < GAUNTLET_MIN_GHOSTS) return;

    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tm.floor_num = 20;
    g_tm.arena_district = -1;
    g_tm.gauntlet_district = -1;
    g_tm.district_count = 1;
    g_tm.districts[0] = (MapDistrict){ 10, 10, 20, 14, DIST_GAUNTLET, 0, 0, 0 };
    for (int y = 10; y < 24; y++)
        for (int x = 10; x < 30; x++)
            g_tm.tiles[y][x].type =
                (x == 10 || x == 29 || y == 10 || y == 23) ? TILE_WALL : TILE_FLOOR;
    g_tm.tiles[17][29].type = TILE_FLOOR;            /* the one door */
    g_tp.party[0].x = 20; g_tp.party[0].y = 17;      /* standing inside it */
    g_tp.party[0].maxhp = g_tp.party[0].hp = 500;

    districts_tick(&g_tm, &g_tp);
    check(g_tm.gauntlet_wave == 1, "stepping in starts the queue");
    check(g_tm.monster_count == 1, "and stands one of them up");
    check(g_tm.tiles[17][29].type == TILE_SEALED_DOOR, "the door goes down behind you");

    if (g_tm.monster_count == 1) {
        /* It is somebody: a name off the roster, and their purse as the drop. */
        bool named = false;
        for (int i = 0; i < ghosts; i++)
            if (strcmp(g_tm.monsters[0].name, roster[i].name) == 0) named = true;
        check(named, "and it is one of your own dead, by name");
        check(g_tm.monsters[0].gold_reward == roster[0].gold,
              "carrying exactly what that run was carrying");
        check(g_tm.monsters[0].xp_reward == 0,
              "and worth no experience -- killing what you were is not training");
    }

    /* Clear the hall and it opens again, paid. */
    for (int guard = 0; guard < GAUNTLET_MAX_GHOSTS + 3; guard++) {
        for (int i = 0; i < g_tm.monster_count; i++) g_tm.monsters[i].alive = false;
        districts_tick(&g_tm, &g_tp);
    }
    check(g_tm.gauntlet_paid, "clearing the barrow settles it");
    check(g_tm.tiles[17][29].type != TILE_SEALED_DOOR, "and the door grinds up again");

    printf("  a box worth taking quietly, and %d of your own dead to get past\n", ghosts);
}

/* ---- a hire that cannot hurt anything must stop trying -----------------
 *
 * Reported from play: "the AI shoots spells that don't cause damage... they
 * can get stuck doing that for minutes, not moving and shooting the same
 * spells with 0 enemy damage."
 *
 * The cause was one `else`. companion_cast() tested usefulness with a chain
 * ending in a branch commented EFFECT_WARD_SHIELD which was in fact a
 * catch-all, so STUN, SLOW and CURSE -- all three of which a companion may
 * know -- had their usefulness answered by `def_buff_turns == 0`, a field
 * none of them writes. It was therefore true on every turn forever, and
 * because the pick is weighted by spell *level*, a good curse outranked a
 * lesser bolt and was the preferred move. The hire stood there cursing an
 * already-cursed monster until something else killed it.
 *
 * Asserted from the outside, over whole turns, because that is the level the
 * defect lived at: not "does this branch return the right bool" but "does a
 * hire with a target in front of it ever actually hurt it".
 */
static void test_a_hire_gets_on_with_it(void) {
    printf("a hire gets on with it\n");

    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    Hero *c = &g_tp.party[1];
    memset(c, 0, sizeof *c);
    c->in_use = true; c->alive = true;
    c->hp = c->maxhp = 100;
    c->aether = c->aether_max = 999999;

    /* The heaviest curse and the lightest bolt in the pool. Chosen by search
       rather than hard-coded, so a rebalanced pool cannot quietly turn this
       into a test of nothing. */
    int curse_slot = -1, bolt_slot = -1, curse_level = 0, bolt_level = 99;
    for (int i = 0; i < spell_pool_count(); i++) {
        const SpellTemplate *t = spell_pool_get(i);
        if (!t) continue;
        if (t->effect == EFFECT_CURSE && t->level > curse_level) { curse_level = t->level; curse_slot = i; }
        if (t->effect == EFFECT_DAMAGE && t->level < bolt_level) { bolt_level = t->level; bolt_slot = i; }
    }
    check(curse_slot >= 0 && bolt_slot >= 0, "the pool has both a curse and a bolt");
    check(curse_level > bolt_level, "and the curse is the higher-level of the two");
    if (curse_slot < 0 || bolt_slot < 0) return;

    c->magic_school = spell_pool_get(curse_slot)->school;
    c->spell_count = 2;
    c->known_spells[0] = curse_slot;   /* the big one, deliberately first */
    c->known_spells[1] = bolt_slot;
    c->spell_cd[0] = c->spell_cd[1] = 0;

    Monster dummy;
    memset(&dummy, 0, sizeof dummy);
    dummy.alive = true; dummy.hp = dummy.maxhp = 1000;

    /* 1. With something in front of them, the thing that ends the fight wins.
          This is the report: a level-5 curse used to outrank a level-1 bolt on
          level alone, and the hire cast it every turn for no damage. */
    int pick = companion_pick_spell(&g_tp, c, &dummy, true);
    check(pick == 1, "a hire holding a big curse and a small bolt casts the bolt");

    /* 2. Take the bolt away and the curse is worth casting -- once. */
    c->spell_count = 1;
    c->known_spells[0] = curse_slot;
    dummy.jinx_turns_left = 0;
    check(companion_pick_spell(&g_tp, c, &dummy, true) == 0,
          "with nothing better, a curse is worth casting");
    dummy.jinx_turns_left = 5;
    check(companion_pick_spell(&g_tp, c, &dummy, true) < 0,
          "but not at something already cursed -- that was the loop");

    /* 3. Nothing to point it at, nothing to cast. Guards the branch that used
          to answer "useful?" with a field about the caster's own shield. */
    check(companion_pick_spell(&g_tp, c, NULL, false) < 0,
          "and not at nothing at all");

    /* 4. The same shape for the other two control effects, since they shared
          the branch and would have shared the defect. */
    struct { int effect; const char *verb; const char *done; } CONTROL[] = {
        { EFFECT_STUN, "stun", "stunned" },
        { EFFECT_SLOW, "slow", "slowed"  },
    };
    for (unsigned k = 0; k < sizeof CONTROL / sizeof CONTROL[0]; k++) {
        int slot = -1;
        for (int i = 0; i < spell_pool_count(); i++) {
            const SpellTemplate *t = spell_pool_get(i);
            if (t && t->effect == CONTROL[k].effect) { slot = i; break; }
        }
        if (slot < 0) continue;
        c->spell_count = 1;
        c->known_spells[0] = slot;
        c->magic_school = spell_pool_get(slot)->school;
        memset(&dummy, 0, sizeof dummy);
        dummy.alive = true; dummy.hp = dummy.maxhp = 1000;

        char msg[96];
        snprintf(msg, sizeof msg, "a %s is worth casting on something not yet %s",
                 CONTROL[k].verb, CONTROL[k].done);
        check(companion_pick_spell(&g_tp, c, &dummy, true) == 0, msg);

        if (CONTROL[k].effect == EFFECT_STUN) dummy.stun_turns_left = 3;
        else                                  dummy.slow_turns_left = 3;
        snprintf(msg, sizeof msg, "and not on something already %s", CONTROL[k].done);
        check(companion_pick_spell(&g_tp, c, &dummy, true) < 0, msg);
    }

    printf("  the bolt beats the level-%d curse, and no control spell is cast twice\n",
           curse_level);
}

/* ---- the boneyard's work, and the shaft's holes -------------------------
   Work is keyed on the *pair* -- tile type and district -- rather than on a
   tile type of its own, so rubble means "a collapsed machine" in an old
   quarter and "a dead golem" in a boneyard and nothing at all in a jungle.
   That is three answers from one tile and it is worth pinning, because the
   next district that wants rubble to mean something will add a fourth. */
static void test_work_is_keyed_on_the_pair(void) {
    printf("rubble means what the district says it means\n");

    memset(&g_tm, 0, sizeof g_tm);
    world_size_apply(WORLD_SHAFT);
    g_tm.floor_num = 12;
    g_tm.district_count = 3;
    g_tm.districts[0] = (MapDistrict){ 10, 10, 10, 10, DIST_RUINS,    0, 0, 0 };
    g_tm.districts[1] = (MapDistrict){ 30, 10, 10, 10, DIST_BONEYARD, 0, 0, 0 };
    g_tm.districts[2] = (MapDistrict){ 50, 10, 10, 10, DIST_JUNGLE,   0, 0, 0 };
    g_tm.tiles[12][12].type = TILE_DECOR;
    g_tm.tiles[12][32].type = TILE_DECOR;
    g_tm.tiles[12][52].type = TILE_DECOR;

    check(work_available_at(&g_tm, 12, 12) == WORK_SALVAGE,
          "rubble in the old quarter is a wreck to strip");
    check(work_available_at(&g_tm, 32, 12) == WORK_CORE,
          "rubble in the boneyard is a golem to open");
    check(work_available_at(&g_tm, 52, 12) == WORK_NONE,
          "and rubble in the overgrowth is rubble");

    /* The longest commitment in the game should read as the longest. */
    check(work_turns_for(WORK_CORE) >= work_turns_for(WORK_SALVAGE),
          "cracking a core is not quicker than stripping a wreck");
    check(work_turns_for(WORK_CORE) > 0 && work_verb(WORK_CORE)[0] != '\0',
          "and it has a duration and something to call it");

    /* Nothing is workable in town, whatever is lying about. */
    g_tm.floor_num = 0;
    check(work_available_at(&g_tm, 32, 12) == WORK_NONE, "nobody works in the plaza");
}

/* ---- the proving ground forbids the same things of everyone -------------
   §3.1's 76, 78 and 80 built once with three skins. The property worth pinning
   is not that the rule exists but that it is the *same* rule for the player,
   for a hire under AI, and for auto-explore's decision -- a restriction only
   the player obeys is a handicap, and one auto-explore does not know about is
   a walk that picks a refused action and loops on it, which is the shape of
   every stuck-walk defect this project has had. */
static void test_the_proving_ground(void) {
    printf("the proving ground\n");

    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    world_size_apply(WORLD_SHAFT);
    g_tm.floor_num = 12;
    g_tm.district_count = 1;
    for (int y = 10; y < 24; y++)
        for (int x = 10; x < 30; x++) g_tm.tiles[y][x].type = TILE_FLOOR;

    struct { int kind; bool melee, arcane, ranged; const char *what; } SKINS[] = {
        { PROVE_MELEE,   true,  false, false, "blades only"    },
        { PROVE_ARCANE,  false, true,  false, "the arts only"  },
        { PROVE_UNARMED, true,  true,  false, "no weapons"     },
    };
    for (unsigned k = 0; k < sizeof SKINS / sizeof SKINS[0]; k++) {
        g_tm.districts[0] = (MapDistrict){ 10, 10, 20, 14, DIST_PROVING, 0, 0, SKINS[k].kind };
        char msg[128];

        snprintf(msg, sizeof msg, "%s: melee %s", SKINS[k].what, SKINS[k].melee ? "allowed" : "refused");
        check(proving_allows(&g_tm, 20, 17, PROVE_MELEE_ACT) == SKINS[k].melee, msg);
        snprintf(msg, sizeof msg, "%s: casting %s", SKINS[k].what, SKINS[k].arcane ? "allowed" : "refused");
        check(proving_allows(&g_tm, 20, 17, PROVE_ARCANE_ACT) == SKINS[k].arcane, msg);
        /* Every skin refuses shooting: both duels are about closing with
           somebody, and a bow is the way out of that. */
        snprintf(msg, sizeof msg, "%s: shooting refused", SKINS[k].what);
        check(proving_allows(&g_tm, 20, 17, PROVE_RANGED_ACT) == SKINS[k].ranged, msg);

        /* And none of it reaches a square outside the marked ground. */
        snprintf(msg, sizeof msg, "%s: and nothing is forbidden off the ground", SKINS[k].what);
        check(proving_allows(&g_tm, 5, 5, PROVE_ARCANE_ACT)
              && proving_allows(&g_tm, 5, 5, PROVE_MELEE_ACT)
              && proving_allows(&g_tm, 5, 5, PROVE_RANGED_ACT), msg);

        check(proving_rule_name(SKINS[k].kind) != NULL, "the rule has a name to announce");
    }

    /* The unarmed skin takes the weapon's contribution and leaves the body. */
    g_tm.districts[0] = (MapDistrict){ 10, 10, 20, 14, DIST_PROVING, 0, 0, PROVE_UNARMED };
    Hero *h = &g_tp.party[0];
    h->x = 20; h->y = 17;
    h->base_atk = 10; h->weapon_bonus = 40; h->atk_buff = 0; h->set_bonus_atk = 0;
    h->stance_atk_pct = 0;
    check(hero_eff_atk(h) == 50, "off the ground the weapon counts");
    check(hero_eff_atk_on(h, &g_tm) == 10, "on it, only what you are counts");
    h->x = 5; h->y = 5;
    check(hero_eff_atk_on(h, &g_tm) == 50, "and stepping out gives it back");

    /* Auto-explore never picks an action the ground will refuse. That is the
       whole reason the decision asks: a refusal spends no turn, so a walk that
       picked one would pick it again for as long as the player watched. */
    g_tm.districts[0] = (MapDistrict){ 10, 10, 20, 14, DIST_PROVING, 0, 0, PROVE_ARCANE };
    h->x = 20; h->y = 17;
    h->ranged_type = RANGED_NONE;
    h->spell_count = 0;                 /* no spells, and blades are refused */
    Monster dummy;
    memset(&dummy, 0, sizeof dummy);
    dummy.alive = true; dummy.hp = dummy.maxhp = 100;
    dummy.x = h->x + 1; dummy.y = h->y;
    AxFight act = autoexplore_decide_fight(&g_tp, &g_tm, &dummy, 1);
    check(act.kind != AX_MELEE,
          "toe to toe where blades are refused, the walk does not swing anyway");
    check(act.kind == AX_NOTHING,
          "it declines the fight instead, which is the only honest answer");

    printf("  three skins, one rule, and the walk knows it too\n");
}

/* ---- the six new shapes actually do six new things ----------------------
   The fuzzer cannot answer this: a character belongs to one school and has to
   *buy* the spells, so 15,000 keys turned up one chain and nothing else. Cast
   each one directly instead and look at what changed. */
static int pool_slot_for_effect(int effect) {
    for (int i = 0; i < spell_pool_count(); i++) {
        const SpellTemplate *t = spell_pool_get(i);
        if (t && t->effect == effect) return i;
    }
    return -1;
}

static void arm_caster(Hero *h, int pool_idx) {
    h->spell_count = 1;
    h->known_spells[0] = pool_idx;
    h->spell_cd[0] = 0;
    h->aether = h->aether_max = 999999;
    h->magic_school = spell_pool_get(pool_idx)->school;
}

static void test_the_variety_pass(void) {
    printf("six new shapes, not six new numbers\n");

    world_size_apply(WORLD_SHAFT);
    Hero *h = &g_tp.party[0];

    /* A clear room with the caster in the middle of it. */
    #define VARIETY_SETUP()                                                   \
        memset(&g_tm, 0, sizeof g_tm); memset(&g_tp, 0, sizeof g_tp);         \
        apply_class_to_player(&g_tp, 0);                                       \
        g_tm.floor_num = 10;                                                   \
        for (int y = 4; y < 20; y++) for (int x = 4; x < 30; x++) {            \
            g_tm.tiles[y][x].type = TILE_FLOOR;                                \
            g_tm.tiles[y][x].visible = true; g_tm.tiles[y][x].seen = true;     \
        }                                                                      \
        h = &g_tp.party[0]; h->x = 10; h->y = 10;                              \
        h->hp = h->maxhp = 400;

    /* CHAIN: it hits the target and then something else. */
    VARIETY_SETUP();
    int idx = pool_slot_for_effect(EFFECT_CHAIN);
    check(idx >= 0, "the pool has a chain spell");
    if (idx >= 0) {
        arm_caster(h, idx);
        g_tm.monster_count = 2;
        for (int i = 0; i < 2; i++) {
            Monster *mo = &g_tm.monsters[i];
            memset(mo, 0, sizeof *mo);
            snprintf(mo->name, sizeof mo->name, "Dummy %d", i);
            mo->x = 12 + i; mo->y = 10;
            mo->hp = mo->maxhp = 5000; mo->def = 0;
            mo->alive = true;
        }
        bool bk = false;
        check(cast_spell_slot(&g_tp, h, &g_tm, 0, &bk), "the chain casts");
        check(g_tm.monsters[0].hp < 5000, "it hits what you aimed at");
        check(g_tm.monsters[1].hp < 5000, "and jumps to something else -- which no nova does from there");
    }

    /* PARTY_HEAL: everybody standing mends, not just the caster. */
    VARIETY_SETUP();
    idx = pool_slot_for_effect(EFFECT_PARTY_HEAL);
    check(idx >= 0, "the pool has a party heal");
    if (idx >= 0) {
        arm_caster(h, idx);
        h->hp = 100;
        Hero *o = &g_tp.party[1];
        memset(o, 0, sizeof *o);
        o->in_use = true; o->alive = true;
        o->hp = 50; o->maxhp = 400; o->x = 14; o->y = 14;
        bool bk = false;
        check(cast_spell_slot(&g_tp, h, &g_tm, 0, &bk), "the chorus casts");
        check(h->hp > 100, "the caster mends");
        check(o->hp > 50, "and so does the hire -- the first spell that acts on the party");
    }

    /* HASTE: it acts on time. */
    VARIETY_SETUP();
    idx = pool_slot_for_effect(EFFECT_HASTE);
    check(idx >= 0, "the pool has a haste");
    if (idx >= 0) {
        arm_caster(h, idx);
        h->spell_count = 2;
        h->known_spells[1] = pool_slot_for_effect(EFFECT_DAMAGE);
        h->spell_cd[1] = 9;
        h->ability_cd = 7;
        bool bk = false;
        check(cast_spell_slot(&g_tp, h, &g_tm, 0, &bk), "the haste casts");
        check(h->spell_cd[1] == 0, "the other spell is ready again");
        check(h->ability_cd == 0, "and so is the ability");
        check(h->spell_cd[0] > 0, "but not its own cooldown -- that would be no cooldown at all");
    }

    /* REFLECT: a stance that punishes being hit. */
    VARIETY_SETUP();
    idx = pool_slot_for_effect(EFFECT_REFLECT);
    check(idx >= 0, "the pool has a reflect");
    if (idx >= 0) {
        arm_caster(h, idx);
        bool bk = false;
        check(cast_spell_slot(&g_tp, h, &g_tm, 0, &bk), "the ward casts");
        check(h->reflect_pct > 0 && h->reflect_turns > 0, "and it is up");
        /* And it runs out on the body's own clock, like every other timer.
           The bound is read once: hero_tick() decrements the very field the
           loop was counting against, so re-reading it exits early -- which is
           what the first version of this did, and it failed the game for the
           test's arithmetic. */
        int ticks = h->reflect_turns + 2;
        for (int t = 0; t < ticks; t++) hero_tick(&g_tp, h, &g_tm);
        check(h->reflect_turns == 0 && h->reflect_pct == 0, "and it wears off");
    }

    /* SENSE_LIFE: information, which nothing else buys. */
    VARIETY_SETUP();
    idx = pool_slot_for_effect(EFFECT_SENSE_LIFE);
    check(idx >= 0, "the pool has a sense-life");
    if (idx >= 0) {
        arm_caster(h, idx);
        g_tm.monster_count = 1;
        Monster *mo = &g_tm.monsters[0];
        memset(mo, 0, sizeof *mo);
        snprintf(mo->name, sizeof mo->name, "Far Thing");
        /* Well outside any reveal radius, and unseen. */
        mo->x = 28; mo->y = 18; mo->hp = mo->maxhp = 10; mo->alive = true;
        g_tm.tiles[18][28].seen = false;
        g_tm.tiles[18][28].visible = false;
        bool bk = false;
        check(cast_spell_slot(&g_tp, h, &g_tm, 0, &bk), "the sense casts");
        check(g_tm.tiles[18][28].seen, "and finds something the fog was holding");
    }

    /* MASS_SLOW: control over an area rather than a target. */
    VARIETY_SETUP();
    idx = pool_slot_for_effect(EFFECT_MASS_SLOW);
    check(idx >= 0, "the pool has a mass slow");
    if (idx >= 0) {
        arm_caster(h, idx);
        g_tm.monster_count = 3;
        for (int i = 0; i < 3; i++) {
            Monster *mo = &g_tm.monsters[i];
            memset(mo, 0, sizeof *mo);
            snprintf(mo->name, sizeof mo->name, "Dummy %d", i);
            mo->x = 9 + i; mo->y = 11;
            mo->hp = mo->maxhp = 100; mo->alive = true;
        }
        bool bk = false;
        check(cast_spell_slot(&g_tp, h, &g_tm, 0, &bk), "the clause casts");
        int slowed = 0;
        for (int i = 0; i < 3; i++) if (g_tm.monsters[i].slow_turns_left > 0) slowed++;
        check(slowed >= 2, "and it catches more than one of them");
    }
    #undef VARIETY_SETUP

    printf("  chain jumps, the chorus reaches the party, haste clears the clock\n");
}

/* ---- the ground you stand on is yours, and only while you stand on it ----
   The prism floor is a stance re-read every turn rather than a buff, which is
   the whole mechanic: step off and it is gone. It was re-read for the *driven*
   body alone, which made it wrong twice -- a hire on coloured ground got
   nothing, and a body that was driven while standing on it kept the swing for
   the rest of the run, because the only code that clears it runs for whoever
   is being driven now. */
static void test_the_prism_is_where_you_stand(void) {
    printf("the prism floor is where you stand\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tm.floor_num = 8;
    g_tm.district_count = 1;
    g_tm.districts[0] = (MapDistrict){ 10, 10, 12, 8, DIST_PRISM, 0, 0, 0 };
    for (int y = 10; y < 18; y++)
        for (int x = 10; x < 22; x++) g_tm.tiles[y][x].type = TILE_FLOOR;
    g_tm.tiles[12][12].type = TILE_PRISM_RED;

    Hero *mc   = &g_tp.party[0];
    Hero *hire = &g_tp.party[1];
    memset(hire, 0, sizeof *hire);
    hire->in_use = true; hire->alive = true;
    hire->hp = hire->maxhp = 100;

    /* The character stands on red and is driven: the swing is theirs. */
    g_tp.controlled = 0;
    mc->x = 12; mc->y = 12;
    mc->hp = mc->maxhp = 100;
    hire->x = 15; hire->y = 15;
    districts_tick(&g_tm, &g_tp);
    check(mc->stance_atk_pct > 0 && mc->stance_def_pct < 0,
          "standing on the red, you hit harder and fold faster");

    /* Now take over the hire, who is standing on ordinary ground. The
       character walks off the red under AI -- and must not keep the swing. */
    g_tp.controlled = 1;
    mc->x = 18; mc->y = 15;
    districts_tick(&g_tm, &g_tp);
    check(mc->stance_atk_pct == 0 && mc->stance_def_pct == 0,
          "and stepping off it gives the swing back, whoever is driving");

    /* And a hire standing on the red gets it, because the rule is about the
       ground and not about who is holding the controller. */
    hire->x = 12; hire->y = 12;
    districts_tick(&g_tm, &g_tp);
    check(hire->stance_atk_pct > 0,
          "a hire on the red gets the same bargain the character would");

    printf("  the swing follows the body, not the controller\n");
}

/* ---- the AI never takes a turn for the body you are driving --------------
 *
 * companions_take_turn() has skipped the driven body since §92, and skipped it
 * by comparing the loop index against `p->controlled`. Those agree right up
 * until they do not: `controlled` can point at a body that has stopped
 * standing -- a hire you were driving when it went down, in the window before
 * the torch is passed -- and hero_driven() documents a fallback to slot 0 for
 * exactly that case. Every other consumer takes the fallback; this one read
 * the raw index, so the loop skipped the corpse and ran the AI over slot 0,
 * which was the body the human was actually driving.
 *
 * It plays as auto-explore going nowhere: the walk steps you a square, the AI
 * walks the same body back, and the position is identical turn after turn
 * until the no-progress detector gives up. Measured at 35-71 stalls per 14,000
 * fuzzer keys before the fix and none after.
 */
static void test_the_ai_leaves_the_driven_body_alone(void) {
    printf("the AI leaves the driven body alone\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tm.floor_num = 6;
    for (int y = 4; y < 26; y++)
        for (int x = 4; x < 40; x++) {
            g_tm.tiles[y][x].type = TILE_FLOOR;
            g_tm.tiles[y][x].seen = true;
            g_tm.tiles[y][x].visible = true;
        }

    Hero *mc = &g_tp.party[0];
    mc->in_use = true; mc->alive = true;      /* a body the world can act on */
    mc->x = 20; mc->y = 15;
    mc->hp = mc->maxhp = 200;
    mc->base_atk = 10; mc->base_def = 5;

    /* The state that produced it: control pointing at a hire that has gone
       down. hero_driven() falls back to slot 0; `controlled` still says 1. */
    Hero *dead = &g_tp.party[1];
    memset(dead, 0, sizeof *dead);
    dead->in_use = true; dead->alive = false; dead->hp = 0;
    dead->x = 22; dead->y = 15;
    g_tp.controlled = 1;

    check(hero_driven(&g_tp) == mc, "with control on a fallen hire, slot 0 is driven");
    check(g_tp.controlled != 0, "and `controlled` still names the hire -- that is the trap");

    /* Something to chase, so the AI has a reason to walk somebody. */
    g_tm.monster_count = 1;
    Monster *mo = &g_tm.monsters[0];
    memset(mo, 0, sizeof *mo);
    snprintf(mo->name, sizeof mo->name, "Bait");
    mo->x = 30; mo->y = 15; mo->hp = mo->maxhp = 500; mo->alive = true; mo->aggro = true;

    int bx = mc->x, by = mc->y;
    for (int t = 0; t < 6; t++) companions_take_turn(&g_tp, &g_tm);

    check(mc->x == bx && mc->y == by,
          "and the AI does not walk the body the human is driving");

    printf("  six turns of world, and you are where you left yourself\n");
}

/* ---- the town does not inherit the dungeon's fog -------------------------
 *
 * The town marks its whole footprint seen and visible, and then
 * hero_vision_update() paints the *driven body's* remembered map over the Tile
 * flags every frame -- which is the design, and correct, right up until the
 * body's memory is of a floor that is no longer under it.
 *
 * generate_temple_floor() calls hero_vision_forget() for exactly that reason.
 * generate_town_map() did not, so climbing back into the plaza brought the
 * last floor's fog of war with you: the town lit where the dungeon had been
 * explored and black where it had not, in every render mode. Reported from
 * play with a screenshot of a hole in the middle of the city.
 */
static void test_the_town_is_not_fogged(void) {
    printf("the town does not inherit the dungeon's fog\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tp.run_seed = 21u;

    /* A floor, explored only where the body has stood -- which is most of the
       point: the fog that leaked was the *shape* of one floor's exploration. */
    g_tp.floor = 4;
    generate_temple_floor(&g_tm, 4, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
    hero_vision_forget();
    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);

    long dungeon_seen = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) if (g_tm.tiles[y][x].seen) dungeon_seen++;
    check(dungeon_seen > 0, "the floor was explored somewhere");
    check(dungeon_seen < (long)MAP_W * MAP_H, "and not everywhere -- there is fog to leak");

    /* Now climb out -- twice, because the composite can arrive in either of
       two states and both of them used to ruin the plaza.
     *
       The paint only runs when the active actor has changed since it last
       ran, so a test that walks one body from a floor into town does not
       reproduce anything: the paint is skipped and the town's own flags
       survive by luck. Both cases below force it, and they fail in opposite
       directions -- a body that remembers the *dungeon* paints that floor's
       fog onto the plaza, and a body that remembers *nothing* paints the whole
       plaza dark. One bug, two stencils. */
    for (int variant = 0; variant < 2; variant++) {
        generate_town_map(&g_tm);
        g_tp.floor = 0;
        g_tp.party[0].x = 20; g_tp.party[0].y = 10;

        if (variant == 0) {
            /* Remembering the last floor: force the repaint by making the
               paint target stale rather than by changing bodies. */
            hero_vision_seed_from_map(&g_tm, &g_tp);
            g_tp.controlled = 0;
        } else {
            hero_vision_forget();     /* remembering nothing at all */
        }
        hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);

        long dark = 0;
        for (int y = 0; y < VIEW_H; y++)
            for (int x = 0; x < VIEW_W; x++) if (!g_tm.tiles[y][x].seen) dark++;

        printf("  variant %d: %ld of %d town squares unlit\n", variant, dark, VIEW_W * VIEW_H);
        check(dark == 0, variant == 0
              ? "the plaza is lit when the body remembers the last floor"
              : "and lit when the body remembers nothing at all");
    }
}

static void test_biome_rules(void) {
    printf("a rule per biome\n");

    memset(&g_tp, 0, sizeof(g_tp));
    apply_class_to_player(&g_tp, 0);

    /* Each band has its own rule and only its own -- otherwise a "rule per
       biome" is one rule with five names. */
    struct { int floor; int cover, ward, hazard, blind; } want[] = {
        {  5, 1, 0, 0, 0 },   /* Roots:  growth hides       */
        { 25, 0, 0, 0, 0 },   /* Works:  levers, in mapgen  */
        { 45, 0, 1, 0, 0 },   /* Ruins:  warded monsters    */
        { 70, 0, 0, 1, 0 },   /* Wastes: the salt bites     */
        { 95, 0, 0, 0, 1 },   /* Abyss:  the dark blinds    */
    };
    for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
        g_tp.floor = want[i].floor;
        bool ok = (biome_growth_cover(&g_tp)     > 0) == (want[i].cover  > 0)
               && (biome_monster_ward_pct(&g_tp) > 0) == (want[i].ward   > 0)
               && (biome_hazard_extra(&g_tp)     > 0) == (want[i].hazard > 0)
               && (biome_sight_penalty(&g_tp)    > 0) == (want[i].blind  > 0);
        if (!ok) { check(false, "each band has its own rule and only its own"); return; }
    }
    check(true, "each band has its own rule and only its own");

    /* And the one that would be a bug rather than a difficulty: the Abyss must
       never blind a player below a radius they can still navigate by. */
    g_tp.floor = 95;
    check(biome_sight_penalty(&g_tp) < BIOME_ABYSS_SIGHT_FLOOR + 6,
          "and the dark never takes more sight than the map can spare");

    g_tp.floor = 0;
    check(biome_growth_cover(&g_tp) == 0 && biome_sight_penalty(&g_tp) == 0,
          "town has no biome rule at all");

    printf("  five bands, five rules, none of them borrowed\n");
}

static void test_set_procs(void) {
    printf("a proc per completed set\n");

    memset(&g_tp, 0, sizeof(g_tp));
    apply_class_to_player(&g_tp, 0);
    Hero *h = &g_tp.party[0];

    /* Three pieces is not a set. */
    h->weapon_set = h->armor_set = h->ring_set = BIOME_ABYSS;
    h->trinket_set = -1;
    recompute_set_bonus(h);
    check(h->set_complete < 0, "three pieces is not a set");

    Monster mo; memset(&mo, 0, sizeof mo);
    snprintf(mo.name, sizeof mo.name, "rat");
    mo.alive = true; mo.hp = mo.maxhp = 99999; mo.def = 10;
    int fired = 0;
    for (int t = 0; t < 500; t++) if (set_proc_on_hit(&g_tp, h, &mo, 30)) fired++;
    check(fired == 0, "and an incomplete set never procs");

    /* Four does, for every biome, and each does its own thing. */
    for (int b = 0; b < BIOME_COUNT; b++) {
        h->weapon_set = h->armor_set = h->ring_set = h->trinket_set = b;
        recompute_set_bonus(h);
        if (h->set_complete != b) { check(false, "four pieces completes a set"); return; }

        int any = 0;
        for (int t = 0; t < 3000 && !any; t++) {
            mo.stun_turns_left = 0; mo.jinx_turns_left = 0; mo.def = 10;
            int extra = set_proc_on_hit(&g_tp, h, &mo, 30);
            if (extra > 0 || mo.stun_turns_left || mo.jinx_turns_left || mo.def < 10) any = 1;
        }
        if (!any) { check(false, "and every completed set does something"); return; }
    }
    check(true, "four pieces completes a set");
    check(true, "and every completed set does something");

    /* The defensive one is the Ruins', and only the Ruins'. */
    h->weapon_set = h->armor_set = h->ring_set = h->trinket_set = BIOME_RUINS;
    recompute_set_bonus(h);
    int turned = 0;
    for (int t = 0; t < 3000; t++) if (set_proc_on_hurt(&g_tp, h, 40) == 0) turned++;
    check(turned > 0, "the ward-touched set turns a blow aside outright");

    h->weapon_set = h->armor_set = h->ring_set = h->trinket_set = BIOME_JUNGLE;
    recompute_set_bonus(h);
    int jungle_turned = 0;
    for (int t = 0; t < 3000; t++) if (set_proc_on_hurt(&g_tp, h, 40) == 0) jungle_turned++;
    check(jungle_turned == 0, "and no other set does");

    printf("  four pieces is a reason to finish a set, not just a bigger number\n");
}

static void test_quest_kinds(void) {
    printf("six bounty kinds\n");

    memset(&g_tp, 0, sizeof(g_tp));
    apply_class_to_player(&g_tp, 0);
    g_tp.deepest_floor = 10; g_tp.tavern_seed = 9u; g_tp.party[0].level = 6;

    int seen[QUEST_TYPE_COUNT];
    memset(seen, 0, sizeof seen);
    for (int t = 0; t < 600; t++) {
        quest_abandon(&g_tp);
        quest_offer(&g_tp);
        seen[g_tp.quest_type]++;
    }
    for (int i = 0; i < QUEST_TYPE_COUNT; i++)
        if (seen[i] == 0) { check(false, "every bounty kind is actually offered"); return; }
    check(true, "every bounty kind is actually offered");

    /* An escort takes a client on, and every way out of the bounty gives them
       back. A client left holding a slot is one the player can never recover. */
    quest_abandon(&g_tp);
    do { quest_offer(&g_tp); } while (g_tp.quest_type != QUEST_ESCORT);
    int slot = g_tp.quest_escort_slot;
    check(slot >= 1 && slot < MAX_PARTY, "an escort brings a client");
    check(g_tp.party[slot].in_use && hero_is_up(&g_tp.party[slot]),
          "who is an ordinary body in an ordinary party slot");
    quest_abandon(&g_tp);
    check(!g_tp.party[slot].in_use, "abandoning gives the slot back");

    /* Dying is a failure, not a stranding. */
    do { quest_abandon(&g_tp); quest_offer(&g_tp); } while (g_tp.quest_type != QUEST_ESCORT);
    slot = g_tp.quest_escort_slot;
    g_tp.party[slot].alive = false; g_tp.party[slot].hp = 0;
    g_tp.floor = g_tp.quest_floor;
    quest_check_arrival(&g_tp);
    check(!g_tp.quest_active, "arriving without your client fails the bounty");
    check(!g_tp.party[slot].in_use, "and still gives the slot back");

    /* A charm breaks a no-recall bounty, and nothing else. */
    quest_abandon(&g_tp);
    do { quest_offer(&g_tp); } while (g_tp.quest_type != QUEST_NORECALL);
    check(!g_tp.quest_broken, "a fresh no-recall bounty is unbroken");
    quest_note_recall(&g_tp);
    check(g_tp.quest_broken, "and a spent charm breaks it");
    g_tp.floor = g_tp.quest_floor;
    quest_check_arrival(&g_tp);
    check(!g_tp.quest_active, "arriving on a broken bounty pays nothing");

    /* The clock is absolute, so nothing that ticks turns in bulk can drift it. */
    quest_abandon(&g_tp);
    do { quest_offer(&g_tp); } while (g_tp.quest_type != QUEST_TIMED);
    check(g_tp.quest_deadline > g_tp.turns, "a timed bounty has a future deadline");
    g_tp.turns = g_tp.quest_deadline + 1;
    g_tp.floor = g_tp.quest_floor;
    quest_check_arrival(&g_tp);
    check(!g_tp.quest_active, "and arriving late pays nothing");

    printf("  three new kinds, and a client who is just a body in a slot\n");
}

static void test_band_gifts(void) {
    printf("what the place does for you\n");

    memset(&g_tp, 0, sizeof(g_tp));
    apply_class_to_player(&g_tp, 0);
    g_tp.party[0].maxhp = 100;

    /* Coasting: every band silent. */
    g_tp.party[0].hp = 100;
    for (int floor = 5; floor <= 95; floor += 20) {
        g_tp.floor = floor;
        g_tp.dda_pressure = 0; g_tp.dda_heals_used = 0; g_tp.dda_retreated = false;
        if (band_help(&g_tp) != 0) { check(false, "a run that is coasting is given nothing"); return; }
    }
    check(true, "a run that is coasting is given nothing");

    /* Positive pressure must not become a penalty either. */
    g_tp.floor = 5; g_tp.dda_pressure = DDA_MAX;
    check(band_help(&g_tp) == 0, "and a run that is thriving is given nothing");
    check(band_regen_bonus(&g_tp) == 0, "with no gift turning into a cost");

    /* Struggling: each band gives its own thing, and only its own. */
    g_tp.dda_pressure = 0; g_tp.party[0].hp = 12;

    g_tp.floor = 5;
    check(band_regen_bonus(&g_tp) >= 1, "the Roots feed a run that is dying on them");
    check(band_ward_bonus(&g_tp) == 0 && band_aggro_reduction(&g_tp) == 0,
          "and give only what the Roots give");

    g_tp.floor = 25;
    check(band_recharge_bonus(&g_tp) >= 1, "the Works give back what you spend");
    check(band_regen_bonus(&g_tp) == 0, "and not what the Roots give");

    g_tp.floor = 45;
    check(band_ward_bonus(&g_tp) >= 1, "the Ruins' wards still hold");

    g_tp.floor = 95;
    check(band_aggro_reduction(&g_tp) >= 1, "the Abyss hides as it blinds");

    /* The Wastes are a chance rather than a constant, so measure it. */
    g_tp.floor = 75;
    int saved = 0;
    for (int t = 0; t < 2000; t++) if (band_supply_saved(&g_tp)) saved++;
    check(saved > 0, "the salt sometimes preserves a draught");
    check(saved < 2000, "but not every time");

    /* Bounded by the one knob, at the very bottom of the scale. */
    g_tp.dda_pressure = DDA_MIN; g_tp.party[0].hp = 1;
    check(band_help(&g_tp) <= BAND_MAX_HELP, "and nothing exceeds the one knob");

    /* The plaza is not a place that helps. */
    g_tp.floor = 0;
    check(band_help(&g_tp) == 0, "town gives nothing, however badly it is going");

    printf("  five bands, silent, and only ever kinder\n");
}

static void test_dda_reads_the_floor_underfoot(void) {
    printf("DDA and the floor you never leave\n");

    memset(&g_tp, 0, sizeof(g_tp)); memset(&g_tm, 0, sizeof(g_tm));
    apply_class_to_player(&g_tp, 0);
    g_tp.party[0].maxhp = 100; g_tp.party[0].hp = 100;
    g_tp.floor = 1; g_tm.floor_num = 1;
    dda_floor_begin(&g_tp);

    check(dda_pressure(&g_tp) == 0, "an untouched arrival reads as nothing yet");

    g_tp.party[0].hp = 40;
    check(dda_pressure(&g_tp) == DDA_HURT,
          "a floor going badly says so before it is left");

    g_tp.party[0].hp = 12;
    check(dda_pressure(&g_tp) == DDA_ROUTED, "and says so louder as it gets worse");
    check(dda_settled(&g_tp) == 0,
          "with the settled history still untouched -- nothing has been filed");

    /* A good start must not read as coasting: the floor has to be finished
       before credit is given, or a strong opening on a floor you are about to
       die on reads as "this run is comfortable". */
    g_tp.party[0].hp = 100;
    check(dda_floor_so_far(&g_tp) == 0, "a floor going well stays silent until it ends");

    /* And the verdict still lands when the floor is actually left. */
    g_tp.party[0].hp = 12;
    dda_floor_end(&g_tp, &g_tm);
    check(dda_settled(&g_tp) == DDA_ROUTED, "leaving it files the verdict as before");

    /* The label has to describe the gauge it sits next to. */
    memset(&g_tp, 0, sizeof(g_tp));
    apply_class_to_player(&g_tp, 0);
    g_tp.party[0].maxhp = 100; g_tp.party[0].hp = 12;
    g_tp.floor = 1; dda_floor_begin(&g_tp); g_tp.party[0].hp = 12;
    check(strcmp(dda_label(&g_tp), "even") != 0,
          "and the label agrees with the number it describes");

    printf("  a floor that is going badly says so while you are still on it\n");
}

static void test_allies_displace(void) {
    printf("an ally is not a wall\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tp, 0, sizeof(g_tp)); memset(&g_tm, 0, sizeof(g_tm));
    apply_class_to_player(&g_tp, 0);
    g_tp.gold = 100000000; g_tp.tavern_seed = 808u; g_tp.party[0].level = 4;
    g_tp.run_seed = 5u; g_tp.floor = 1;
    generate_temple_floor(&g_tm, 1, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
    check(companion_hire(&g_tp, 0), "a hire is taken on");

    /* Put the hire directly in the character's path, on open ground. */
    int hx = g_tp.party[0].x, hy = g_tp.party[0].y;
    int tx = -1, ty = -1;
    for (int d = 0; d < 8 && tx < 0; d++) {
        int nx = hx + PATH_DX[d], ny = hy + PATH_DY[d];
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
        if (!is_walkable_player(&g_tm, nx, ny)) continue;
        if (monster_at(&g_tm, nx, ny)) continue;
        tx = nx; ty = ny;
    }
    check(tx >= 0, "there is somewhere next to the character to stand");
    g_tp.party[1].x = tx; g_tp.party[1].y = ty;

    /* Walking into them must spend a turn and must move somebody -- the two
       properties the refusal broke. */
    int before_x = g_tp.party[0].x, before_y = g_tp.party[0].y;
    bool moved = party_displace_into(&g_tp, &g_tp.party[0], tx, ty);
    check(moved, "walking into an ally displaces them rather than being refused");
    check(g_tp.party[0].x == tx && g_tp.party[0].y == ty, "you take their square");
    check(g_tp.party[1].x == before_x && g_tp.party[1].y == before_y, "and they take yours");
    check(!(g_tp.party[0].x == g_tp.party[1].x && g_tp.party[0].y == g_tp.party[1].y),
          "and never both the same one");

    /* An empty square is not a displacement, so ordinary movement is
       untouched by the rule. */
    check(!party_displace_into(&g_tp, &g_tp.party[0], before_x, before_y + 100),
          "and nothing is displaced by walking into open ground");

    printf("  allies squeeze past each other instead of blocking\n");
}

static void test_everybody_has_a_school(void) {
    printf("everybody has a school\n");

    static Player p;
    memset(&p, 0, sizeof(p));
    apply_class_to_player(&p, 0);
    p.party[0].level = 6;

    check(p.party[0].magic_school >= 0 && p.party[0].magic_school < SCHOOL_COUNT,
          "the character the run started as has a school");

    int checked = 0;
    for (unsigned seed = 1; seed <= 8; seed++) {
        p.tavern_seed = seed * 7919u;
        for (int i = 0; i < TAVERN_ROSTER; i++) {
            Hero c;
            tavern_candidate(&p, i, &c);
            if (c.magic_school < 0 || c.magic_school >= SCHOOL_COUNT) {
                check(false, "every Tavern candidate has a real school");
                return;
            }
            if (spell_school_count(c.magic_school) <= 0) {
                check(false, "and one the Guild actually stocks spells for");
                return;
            }
            checked++;
        }
    }
    check(checked == TAVERN_ROSTER * 8, "every Tavern candidate has a real school");
    printf("  %d candidates across 8 rosters, none of them a \"?\" caster\n", checked);
}

/* Walks party slot `slot` around the floor for up to `steps` turns, taking
   whichever direction is open and refreshing vision each turn as the game
   does. Returns how many steps it actually took. Wanders rather than heading
   one way, because a straight line runs into the first wall and stops. */
static int walk_hero_about(Map *m, Player *p, int slot, int steps) {
    static const int DX[4] = { 1, 0, -1, 0 };
    static const int DY[4] = { 0, 1, 0, -1 };
    int moved = 0, dir = 0;
    for (int t = 0; t < steps; t++) {
        for (int k = 0; k < 4; k++) {
            int d = (dir + k) % 4;
            int nx = p->party[slot].x + DX[d], ny = p->party[slot].y + DY[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (m->tiles[ny][nx].type == TILE_WALL) continue;
            p->party[slot].x = nx; p->party[slot].y = ny;
            dir = d; moved++;
            break;
        }
        hero_vision_update(m, p, p->party[p->controlled].fov_radius);
    }
    return moved;
}

/* ---- one map per body -----------------------------------------------
   Three bugs reported from play, all one premise: a single set of eyes that
   belonged to nobody and that everything assumed was the character's.

     - the '@' vanished after switching bodies;
     - a second, identical '@' turned up next to you;
     - auto-explore stopped "after a while" while walking perfectly well.

   Each check below is one of those, stated as the property that was false. */
static void test_per_hero_vision(void) {
    printf("one map per body\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tp, 0, sizeof(g_tp)); memset(&g_tm, 0, sizeof(g_tm));
    apply_class_to_player(&g_tp, 3);
    g_tp.gold = 100000000; g_tp.tavern_seed = 4242u; g_tp.party[0].level = 5;
    g_tp.run_seed = 99u; g_tp.difficulty = 0; g_tp.floor = 1;
    generate_temple_floor(&g_tm, 1, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
    /* The floor has just been replaced, and vision.h is explicit that the
       per-body maps have to be dropped when it is. Without this the six maps
       carry whatever the previous test in this process left in them, so the
       counts below measure two bodies' memory of two different floors added
       together -- which is how this test came to be asserting on a number
       that had nothing to do with what it was testing. */
    hero_vision_forget();
    check(companion_hire(&g_tp, 0), "a hire is taken on");
    companions_place(&g_tp, &g_tm);

    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
    check(g_tm.tiles[g_tp.party[0].y][g_tp.party[0].x].visible,
          "the body you are driving can always see its own square");

    /* Walk the hire away so the two maps genuinely differ. Any open direction
       will do -- the point is that it goes somewhere the character has not. */
    int walked = walk_hero_about(&g_tm, &g_tp, 1, 60);
    check(walked > 0, "the hire has somewhere to walk");

    /* And walk the driven body too, somewhere the hire is no longer standing.
       Without this the test cannot tell the two maps apart even when they are
       working perfectly -- see below. */
    int walked_mc = walk_hero_about(&g_tm, &g_tp, 0, 60);
    check(walked_mc > 0, "and the character has somewhere to walk");

    /* Compared as *sets*, and asymmetrically, because the two directions mean
       different things and only one of them is separation.
     *
       This used to assert that the two bodies had seen a different *number*
       of tiles, with the hire walking and the character standing still. That
       is not the property. comp_light() unions every body's sight into the
       driven body's memory on purpose -- its comment says so: "anything any
       actor can see is told to the active one" -- so the character's map is
       always a superset of the hire's, and the count only differed because
       the hire's wandering happened not to cover the few tiles the character
       could see from where it stood. Adding two district kinds reshuffled the
       weight table, floor 1 of seed 99 came out differently, the hire's trail
       happened to cover them, and a test that had never been measuring
       separation started failing for a game that was entirely correct.

       So: the character knows things the hire does not (separation -- this is
       what fails if the six maps are ever one map again), and the hire knows
       nothing the character does not (the union rule, stated rather than
       assumed). */
    int mc_seen = 0, hire_seen = 0, only_mc = 0, only_hire = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            bool a = hero_has_seen(0, x, y), b = hero_has_seen(1, x, y);
            if (a) mc_seen++;
            if (b) hire_seen++;
            if (a && !b) only_mc++;
            if (b && !a) only_hire++;
        }
    check(mc_seen > 0 && hire_seen > 0, "both bodies remember something");
    check(only_mc > 0, "the body you are driving knows things the hire does not");
    check(only_hire == 0, "and is told everything the hire can see");

    /* The bug: switch bodies and the driven square must still be lit, or the
       renderer has nothing to draw the '@' on. */
    g_tp.controlled = 1;
    hero_vision_update(&g_tm, &g_tp, g_tp.party[1].fov_radius);
    check(g_tm.tiles[g_tp.party[1].y][g_tp.party[1].x].visible,
          "after taking over a hire, the square you are standing on is lit");

    g_tp.controlled = 0;
    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
    check(g_tm.tiles[g_tp.party[0].y][g_tp.party[0].x].visible,
          "and after switching back, so is the character's");

    /* Auto-explore's stuck detector watched slot 0 while every step moved the
       driven body, so a run driving a hire read as stuck the moment the
       character's AI settled. The signature has to move when the body that is
       actually walking walks. */
    /* Auto-explore's stuck detector watched slot 0 while every step moved the
       driven body, so a run driving a hire read as stuck the moment the
       character's AI settled. The property that was false: walking the actor
       the human is attached to has to enlarge *that actor's* map. */
    g_tp.controlled = 1;
    int hire_before = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) if (hero_has_seen(1, x, y)) hire_before++;
    int moved = walk_hero_about(&g_tm, &g_tp, 1, 120);
    check(moved > 0, "the driven hire has somewhere left to walk");
    int hire_after = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) if (hero_has_seen(1, x, y)) hire_after++;
    check(hire_after > hire_before,
          "walking the driven actor enlarges that actor's map, whoever slot 0 is");

    /* And the map on screen is the active actor's, not the party's union --
       switch and the floor you know changes with you. */
    int painted_as_hire = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) if (g_tm.tiles[y][x].seen) painted_as_hire++;
    check(painted_as_hire == hire_after, "the screen shows the active actor's map");

    g_tp.controlled = 0;
    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
    int painted_as_mc = 0, mc_knows = 0, painted_but_unknown = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            bool painted = g_tm.tiles[y][x].seen, known = hero_has_seen(0, x, y);
            if (painted) painted_as_mc++;
            if (known) mc_knows++;
            if (painted && !known) painted_but_unknown++;
        }
    /* Stated as an identity rather than as "fewer tiles than the hire".
       The count comparison held only because the hire happened to have walked
       further on this particular floor, and adding a district kind reshuffled
       the weight table and floor 1 of this seed with it. What the test means
       is that the screen is *this* actor's map -- which is what line 5153
       already asks for the hire, and it is the same question. */
    check(painted_as_mc == mc_knows && painted_but_unknown == 0,
          "and switching back redraws it as the floor that actor knows");
    check(painted_as_mc != painted_as_hire || mc_knows == painted_as_hire,
          "so the two actors' screens are not the same picture by accident");

    printf("  six maps, and the one you are looking through is yours\n");
}

/* ---- a scratch state directory, and a roster in it ----------------------
 *
 * Two reasons, and the second one is new.
 *
 * The header of this file promises the suite touches nothing on disk, which
 * was true by the weaker method of simply never calling the save functions.
 * Pointing AETHER_STATE_DIR somewhere disposable makes it true by
 * construction instead, which is the version that survives somebody adding a
 * test that does write.
 *
 * And the Gauntlet of the Fallen only generates where there are dead runs to
 * put in it (see pick_district_kind), so "every district kind still
 * generates" is unanswerable without a roster. Seeding one here tests the
 * real behaviour -- the barrow appears when there are ghosts -- rather than
 * exempting the kind from the sweep and testing nothing.
 */
static void use_scratch_state_dir(void) {
    restore_scratch_state_dir();

    Player ghost;
    memset(&ghost, 0, sizeof ghost);
    apply_class_to_player(&ghost, 0);
    for (int i = 0; i < 4; i++) {
        snprintf(ghost.party[0].name, sizeof ghost.party[0].name, "Testwright %d", i + 1);
        ghost.party[0].level = 4 + i * 3;
        ghost.party[0].maxhp = 40 + i * 25;
        ghost.deepest_floor  = 6 + i * 9;
        ghost.gold           = 900 + i * 400;
        record_fallen(&ghost);
    }
}


/* No two CP_ names may share a curses pair number.
 *
   This reads src/common.h rather than a list kept here, because a list kept
   here is a second copy of the thing under test and would have drifted exactly
   as the header did. What it caught: CP_BIOME_JUNGLE..CP_BIOME_ABYSS were
   written as 67-71 when the district block ended at 66, then the district
   block grew to 74 and took those five numbers as well. Ten names, five pairs.
   init_pair() on an already-live pair is a legal redefinition rather than an
   error, so nothing anywhere complained -- the districts simply won by being
   initialised second, and every biome wash in text and block mode drew in a
   district's colour for as long as both lists existed. */
static void test_no_two_colours_are_the_same_pair(void) {
    const char *paths[] = { "src/common.h", "../src/common.h" };
    FILE *f = NULL;
    for (size_t i = 0; i < sizeof paths / sizeof paths[0] && !f; i++)
        f = fopen(paths[i], "r");
    if (!f) {
        printf("  FAIL: cannot open src/common.h (run from the repo root)\n");
        failures++;
        return;
    }

    struct { char name[64]; int val; } cp[256];
    int n = 0;
    char line[512];
    while (fgets(line, sizeof line, f) && n < (int)(sizeof cp / sizeof cp[0])) {
        char name[64]; int val;
        if (sscanf(line, " #define %63s %d", name, &val) == 2 &&
            strncmp(name, "CP_", 3) == 0) {
            snprintf(cp[n].name, sizeof cp[n].name, "%s", name);
            cp[n].val = val;
            n++;
        }
    }
    fclose(f);

    int clashes = 0;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (cp[i].val == cp[j].val) {
                printf("  FAIL: %s and %s are both pair %d\n",
                       cp[i].name, cp[j].name, cp[i].val);
                clashes++;
            }

    printf("  %d colour pairs declared, %d sharing a number\n", n, clashes);
    check(clashes == 0, "every colour pair name has a number to itself");
}


/* HEARING has to buy something.
 *
   This is the test the stat never had. `hearing_radius` was derived, copied by
   TRAINED(), printed on the character sheet under "How far you hear what you
   cannot see" -- and read by nothing at all, alone among the twenty-two
   derived stats. The School sells it at 200v^2+300 and five of the hundred
   classes build around it at 6-8, so the game was charging gold and build cost
   for an effect that did not exist.

   The property asserted is therefore not "hearing works" but the one a player
   would notice: two identical characters differing only in ATTR_HEARING must
   perceive different amounts of the floor. A test that only checked the
   machinery ran would have passed against the dead stat for as long as the
   machinery existed, which is the mistake this file has now made twice. */
static void test_hearing_buys_something(void) {
    printf("\nhearing reaches what sight does not\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tp.run_seed = 77u;
    g_tp.floor = 5;
    generate_temple_floor(&g_tm, 5, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);

    g_tp.party[0].hearing_radius = 0;

    /* Find somewhere sight is actually blocked, and put something behind the
       wall. Two earlier attempts were wrong in instructive ways: relying on
       where the generator dropped a monster tested the generator (nothing
       spawned within six tiles of the stairs, and the test reported zero for
       both variants -- which reads exactly like a dead stat), and standing at
       the stairs and looking for a dark square within six found none, because
       the stairs sit in open ground and `fov_radius` covers it.
     *
       That second failure is worth keeping in mind about the feature itself:
       hearing clamps to 0..6 and sight is usually at least that, so hearing is
       never a longer sense -- it is the sense that goes *through walls*, and
       nowhere else. So the test has to stand somewhere with a wall in the way,
       and it looks for such a place rather than assuming one. */
    int px = -1, py = -1, tx = -1, ty = -1;
    for (int sy = 1; sy < MAP_H - 1 && tx < 0; sy += 7)
        for (int sx = 1; sx < MAP_W - 1 && tx < 0; sx += 7) {
            if (!is_walkable_player(&g_tm, sx, sy)) continue;
            g_tp.party[0].x = sx; g_tp.party[0].y = sy;
            hero_vision_forget();
            hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
            for (int dy = -6; dy <= 6 && tx < 0; dy++)
                for (int dx = -6; dx <= 6 && tx < 0; dx++) {
                    if (dx * dx + dy * dy > 36 || (dx == 0 && dy == 0)) continue;
                    int x = sx + dx, y = sy + dy;
                    if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1) continue;
                    if (!is_walkable_player(&g_tm, x, y)) continue;
                    if (g_tm.tiles[y][x].visible) continue;   /* must be unseen */
                    px = sx; py = sy; tx = x; ty = y;
                }
        }
    check(tx >= 0, "somewhere on the floor, a wall hides walkable ground six tiles away");
    if (tx < 0) return;

    check(g_tm.monster_count > 0, "the floor has a monster to move");
    if (g_tm.monster_count <= 0) return;
    g_tm.monsters[0].alive = true;
    g_tm.monsters[0].x = tx;
    g_tm.monsters[0].y = ty;

    /* Deaf first. */
    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
    bool deaf_hears = vision_heard_at(tx, ty);

    /* The single variable under test. */
    g_tp.party[0].hearing_radius = 6;
    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
    bool sharp_hears = vision_heard_at(tx, ty);

    printf("  a monster %d tiles away, out of sight: deaf %s, sharp %s\n",
           (int)(abs(tx - px) > abs(ty - py) ? abs(tx - px) : abs(ty - py)),
           deaf_hears ? "hears it" : "does not",
           sharp_hears ? "hears it" : "does not");

    check(!deaf_hears, "a character who cannot hear hears nothing");
    check(sharp_hears, "and one who can, hears what sight does not reach");

    /* Out of range is out of range. Same body, same monster, moved away. */
    int fx = -1, fy = -1;
    for (int y = 1; y < MAP_H - 1 && fx < 0; y++)
        for (int x = 1; x < MAP_W - 1 && fx < 0; x++) {
            int dx = x - px, dy = y - py;
            if (dx * dx + dy * dy <= 12 * 12) continue;
            if (g_tm.tiles[y][x].visible) continue;
            fx = x; fy = y;
        }
    if (fx >= 0) {
        g_tm.monsters[0].x = fx;
        g_tm.monsters[0].y = fy;
        hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
        check(!vision_heard_at(fx, fy), "and nothing beyond the radius is heard");
    }

    /* Nothing already on screen may be reported as heard -- otherwise the mark
       is drawn over a monster that is drawn anyway, and the stat buys a
       duplicate rather than a second sense. */
    int overlapping = 0;
    for (int i = 0; i < g_tm.monster_count; i++) {
        const Monster *mo = &g_tm.monsters[i];
        if (!mo->alive) continue;
        if (vision_heard_at(mo->x, mo->y) && g_tm.tiles[mo->y][mo->x].visible) overlapping++;
    }
    check(overlapping == 0, "and nothing is heard that is already visible");
}


/* The chapel's bargain, both halves.
 *
   The district takes the sense that reaches through walls -- from the player
   standing in it, and (in combat.c) from the monsters standing in it. Half a
   bargain is a penalty, so both halves are asserted: standing inside costs you
   the hearing, and standing one square outside gets it back. The second half
   is the one that makes the district a place you can play around rather than
   a region you avoid. */
static void test_the_chapel_takes_your_ears(void) {
    printf("\nthe silence works on both of you\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tp.run_seed = 91u;
    g_tp.floor = 3;
    generate_temple_floor(&g_tm, 3, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);

    /* Find a place where sight is genuinely blocked, the same way the hearing
       test does -- a first version of this stood at the stairs, found no blind
       square within five tiles, and printed "skipped". A test that skips is a
       test that reports success without checking anything, which on this page
       is the failure mode that keeps recurring. */
    int px = -1, py = -1, tx = -1, ty = -1;
    g_tp.party[0].hearing_radius = 6;
    for (int sy = 1; sy < MAP_H - 1 && tx < 0; sy += 7)
        for (int sx = 1; sx < MAP_W - 1 && tx < 0; sx += 7) {
            if (!is_walkable_player(&g_tm, sx, sy)) continue;
            g_tp.party[0].x = sx; g_tp.party[0].y = sy;
            hero_vision_forget();
            hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
            for (int dy = -5; dy <= 5 && tx < 0; dy++)
                for (int dx = -5; dx <= 5 && tx < 0; dx++) {
                    if (dx * dx + dy * dy > 25 || (dx == 0 && dy == 0)) continue;
                    int x = sx + dx, y = sy + dy;
                    if (x < 1 || y < 1 || x >= MAP_W - 1 || y >= MAP_H - 1) continue;
                    if (!is_walkable_player(&g_tm, x, y)) continue;
                    if (g_tm.tiles[y][x].visible) continue;
                    px = sx; py = sy; tx = x; ty = y;
                }
        }
    check(tx >= 0, "somewhere on the floor, a wall hides ground five tiles away");
    check(g_tm.monster_count > 0, "and there is a monster to put on it");
    if (tx < 0 || g_tm.monster_count <= 0) return;

    /* A chapel laid over that spot, rather than hunting the generator for one
       -- the rule under test is the district's, not the carver's. */
    g_tm.district_count = 1;
    g_tm.districts[0].x = px - 4;  g_tm.districts[0].y = py - 4;
    g_tm.districts[0].w = 9;       g_tm.districts[0].h = 9;
    g_tm.districts[0].kind = DIST_CHAPEL;
    g_tm.districts[0].state = 0;

    g_tm.monsters[0].alive = true;
    g_tm.monsters[0].x = tx;
    g_tm.monsters[0].y = ty;

    /* Standing in the silence. */
    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
    bool inside_hears = vision_heard_at(tx, ty);

    /* Standing outside it, everything else identical -- the district is moved
       off the party rather than the party off the district, so the monster
       stays exactly where it was and only the rule changes. */
    g_tm.districts[0].x = px + 40;
    g_tm.districts[0].y = py + 40;
    hero_vision_update(&g_tm, &g_tp, g_tp.party[0].fov_radius);
    bool outside_hears = vision_heard_at(tx, ty);

    printf("  in the silence: %s.  one step out of it: %s\n",
           inside_hears ? "hears" : "deaf", outside_hears ? "hears" : "deaf");
    check(!inside_hears, "the chapel takes the sense that reaches through walls");
    check(outside_hears, "and gives it back at the door");
}

/* One system, two skins.
 *
   The vent field exists because the Storm-Cage's machinery was already built
   and only the scenery was new. The thing worth asserting is therefore not
   "vents damage you" but that the *same tick* drives both -- if the two ever
   become separate implementations, one of them stops being maintained, which
   is the entire reason the duplicate table in the roadmap exists. */
static void test_one_strike_system_two_skins(void) {
    printf("\nthe vent field is the rod field in stone\n");

    world_size_apply(WORLD_SHAFT);
    int found = 0;
    bool armed = false, marked_vent = false, on_a_vent = false;

    for (int seed = 1; seed <= 60 && !found; seed++) {
        memset(&g_tm, 0, sizeof g_tm);
        memset(&g_tp, 0, sizeof g_tp);
        apply_class_to_player(&g_tp, 0);
        g_tp.run_seed = (unsigned)seed;
        g_tp.floor = 9;
        generate_temple_floor(&g_tm, 9, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);

        bool has = false;
        for (int i = 0; i < g_tm.district_count; i++)
            if (g_tm.districts[i].kind == DIST_GEOTHERMAL) has = true;
        if (!has) continue;
        found = seed;

        /* The tick arms the next strike. Nothing here reaches into the vent
           field's own code -- it drives the same entry point the game does. */
        for (int t = 0; t < 40 && !armed; t++) {
            districts_tick(&g_tm, &g_tp);
            if (g_tm.storm_x >= 0) armed = true;
        }
        if (armed) {
            marked_vent = g_tm.storm_is_vent;
            on_a_vent = (g_tm.tiles[g_tm.storm_y][g_tm.storm_x].type == TILE_VENT);
        }
    }

    check(found > 0, "a vent field turns up on some floor");
    if (!found) return;
    printf("  seed %d: armed %s, flagged as a vent %s, target tile %s\n", found,
           armed ? "yes" : "no", marked_vent ? "yes" : "no", on_a_vent ? "vent" : "not a vent");
    check(armed, "the shared tick arms a strike in it");
    check(marked_vent, "and knows which skin is going off");
    check(on_a_vent, "and aims at a vent rather than a rod");
}


/* The three doors added last, each asserted on the thing that would actually
   be wrong rather than on the thing that is easy to check. */
static void test_the_oracle_sells_what_it_promises(void) {
    printf("\nthe Oracle is paid, and answers once\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tp.run_seed = 5u;

    /* The cheap reading: the way down, and nothing else. The interesting
       assertion is the "nothing else" -- a reveal that quietly showed the
       whole floor would look identical from the buyer's side on the turn they
       paid, and would make the dearer readings pointless. */
    g_tp.oracle_floors = 1;
    g_tp.oracle_full = false;
    g_tp.floor = 4;
    generate_temple_floor(&g_tm, 4, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
    oracle_spend_reading(&g_tm, &g_tp);

    bool stairs_known = g_tm.tiles[g_tm.stairs_down_y][g_tm.stairs_down_x].seen;
    long seen_total = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) if (g_tm.tiles[y][x].seen) seen_total++;

    printf("  the way down: stairs %s, %ld squares known in total\n",
           stairs_known ? "marked" : "NOT marked", seen_total);
    check(stairs_known, "the cheap reading marks the stairs");
    check(seen_total < 200, "and does not quietly hand over the floor as well");
    check(g_tp.oracle_floors == 0, "and is spent when it is used");

    /* The long look covers more than one floor, which is the only thing that
       distinguishes it from the middle reading. */
    g_tp.oracle_floors = ORACLE_LONG_FLOORS;
    g_tp.oracle_full = true;
    for (int i = 0; i < ORACLE_LONG_FLOORS; i++) {
        memset(&g_tm, 0, sizeof g_tm);
        generate_temple_floor(&g_tm, 5 + i, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
        oracle_spend_reading(&g_tm, &g_tp);
        long known = 0;
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) if (g_tm.tiles[y][x].seen) known++;
        if (known < 1000) {
            printf("  FAIL: floor %d of the long look was not mapped (%ld known)\n", i + 1, known);
            failures++;
        }
    }
    check(g_tp.oracle_floors == 0, "the long look runs out after its last floor");

    /* And nothing is revealed once it has. */
    memset(&g_tm, 0, sizeof g_tm);
    generate_temple_floor(&g_tm, 9, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
    oracle_spend_reading(&g_tm, &g_tp);
    long after = 0;
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) if (g_tm.tiles[y][x].seen) after++;
    check(after < 1000, "and the floor after that arrives dark");
}

static void test_the_altar_trades_both_ways(void) {
    printf("\nthe Altar takes a point to give one\n");

    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    Hero *h = &g_tp.party[0];

    /* Find a donor with something to give and a recipient that is not it. */
    int give = -1, take = -1;
    for (int i = 0; i < ATTR_COUNT && give < 0; i++)
        if (h->attrs[i] > ALTAR_MIN_ATTR + 1) give = i;
    for (int i = 0; i < ATTR_COUNT && take < 0; i++)
        if (i != give) take = i;
    check(give >= 0, "the character has a point to spare somewhere");
    if (give < 0) return;

    int before_give = h->attrs[give], before_take = h->attrs[take];
    int before_hp = h->maxhp;

    check(trade_attribute(h, give, take), "a legal trade goes through");
    printf("  %s %d->%d, %s %d->%d\n", ATTR_NAMES[give], before_give, h->attrs[give],
           ATTR_NAMES[take], before_take, h->attrs[take]);
    check(h->attrs[give] == before_give - 1, "the donor loses exactly one");
    check(h->attrs[take] == before_take + 1, "the recipient gains exactly one");

    /* The derived sheet has to move with the table. A trade that changed the
       thirty and left maxhp, FOV and the rest on their old values would look
       right on the character sheet and be wrong everywhere it mattered -- and
       it is exactly what two calls to train_attribute would have produced,
       there being no un-train.

       Checked against the documented formula rather than against a fresh
       recompute, because the recompute is static to classes.c and widening
       that API to satisfy a test would be the test changing the code. maxhp is
       `30 + (BRAWN + STAMINA) * 2`, so a point moved into Brawn from an
       attribute that is neither must move maxhp by exactly two. */
    {
        Hero t = g_tp.party[0];
        apply_class_to_player(&g_tp, 0);
        t = g_tp.party[0];
        int donor = -1;
        for (int i = 0; i < ATTR_COUNT; i++)
            if (i != ATTR_BRAWN && i != ATTR_STAMINA && t.attrs[i] > ALTAR_MIN_ATTR) { donor = i; break; }
        check(donor >= 0, "there is a donor outside the HP formula");
        if (donor >= 0) {
            int hp0 = t.maxhp;
            check(trade_attribute(&t, donor, ATTR_BRAWN), "the trade into Brawn goes through");
            printf("  maxhp %d -> %d after moving one point into Brawn\n", hp0, t.maxhp);
            check(t.maxhp == hp0 + 2, "and maxhp moved by exactly what the formula says");
        }
    }
    (void)before_hp;

    /* Refusals. */
    check(!trade_attribute(h, take, take), "the same attribute both ways is not a trade");
    int floored = -1;
    for (int i = 0; i < ATTR_COUNT; i++)
        if (h->attrs[i] <= ALTAR_MIN_ATTR) { floored = i; break; }
    if (floored >= 0) {
        int keep = h->attrs[floored];
        check(!trade_attribute(h, floored, take), "and nothing may be taken below the floor");
        check(h->attrs[floored] == keep, "a refused trade changes nothing");
    }
}

static void test_the_bazaar_quotes_what_it_charges(void) {
    printf("\nthe Bazaar's price moves, and moves once\n");

    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);

    /* Same day, same price -- walking back in must not re-roll the stalls,
       or the shop is a slot machine rather than a market. */
    g_tp.bazaar_day = 7;
    int a = bazaar_price_pct(&g_tp, 0);
    int b = bazaar_price_pct(&g_tp, 0);
    check(a == b, "the same stall on the same day quotes the same price");

    /* Different days differ, across the run of days a real game produces. */
    int lo = 1000, hi = -1000, moved = 0;
    for (unsigned d = 0; d < 60; d++) {
        g_tp.bazaar_day = d;
        int pct = bazaar_price_pct(&g_tp, 0);
        if (pct < lo) lo = pct;
        if (pct > hi) hi = pct;
        if (d > 0 && pct != a) moved++;
    }
    printf("  stall a over 60 days: %d%%..%d%%\n", lo, hi);
    check(moved > 0, "and a different day is a different price");
    check(lo >= BAZAAR_MIN_PCT && hi <= BAZAAR_MAX_PCT, "always inside the band");
    check(hi - lo > 40, "and the band is actually used, not clustered");

    /* Stalls differ from each other on the same day, or the "market" is one
       number wearing several hats. */
    g_tp.bazaar_day = 3;
    int distinct = 0;
    for (int i = 1; i < APOTHECARY_STOCK_COUNT; i++)
        if (bazaar_price_pct(&g_tp, i) != bazaar_price_pct(&g_tp, 0)) distinct++;
    check(distinct > 0, "and the stalls do not all move together");
}


/* Every door in the plaza has to be on self-play's town tour.
 *
   The tour is the only way the fuzzer gets *inside* a shop screen -- walking
   onto the tile is what opens it -- so a door missing from the list is a screen
   that is never opened by anything except a human. Three doors were missing
   when this was written: the Oracle, the Altar and the Bazaar, which is to say
   the three newest and least-exercised screens in the game were also the only
   three self-play could not reach. Exactly backwards.

   Derived from the town map rather than from a list here, so it keeps holding
   as doors are added. */
static void test_self_play_visits_every_door(void) {
    printf("\nself-play walks into every door\n");

    int save_w = g_view_w, save_h = g_view_h;
    memset(&g_tm, 0, sizeof g_tm);
    generate_town_map(&g_tm);

    /* Everything in the plaza that is not ground, scenery or wall is a door. */
    bool present[TILE_VENT + 1];
    memset(present, 0, sizeof present);
    for (int y = 0; y < TOWN_MAP_H; y++)
        for (int x = 0; x < TOWN_MAP_W; x++) {
            int t = g_tm.tiles[y][x].type;
            if (t == TILE_FLOOR || t == TILE_WALL || t == TILE_DECOR) continue;
            if (t >= 0 && t <= TILE_VENT) present[t] = true;
        }

    int doors = 0, missed = 0;
    for (int t = 0; t <= TILE_VENT; t++) {
        if (!present[t]) continue;
        doors++;
        if (!autoplay_tours_tile(t)) {
            printf("  FAIL: tile type %d stands in the plaza and is not on the tour\n", t);
            missed++;
        }
    }

    g_view_w = save_w; g_view_h = save_h;
    printf("  %d doors in the plaza, %d off the tour\n", doors, missed);
    check(doors >= 15, "the plaza has the doors it should");
    check(missed == 0, "and self-play walks into all of them");
}


/* A hire who dies on the way out must still be rehireable.
 *
   The Tavern takes fallen heroes back at a discount -- "(was yours)" on the
   roster, a cheaper price -- and that path worked, so long as a dungeon turn
   elapsed after the death. Giving up the seat was done by companions_reap(),
   called only at the end of companions_take_turn(), which is a dungeon
   function. Die on the last action before the stairs or before a recall and
   the corpse rides home still holding its slot: the party reads as full, the
   roster still says they are hired, and the Tavern answers "they already drink
   on your coin" about somebody who is dead.

   Reported from play as the revive not working. It was not the revive. */
static void test_a_fallen_hire_can_be_taken_back(void) {
    printf("\na hire who falls can be hired again\n");

    world_size_apply(WORLD_SHAFT);
    memset(&g_tm, 0, sizeof g_tm);
    memset(&g_tp, 0, sizeof g_tp);
    apply_class_to_player(&g_tp, 0);
    g_tp.run_seed = 4u;
    g_tp.floor = 3;
    generate_temple_floor(&g_tm, 3, &g_tp.party[0].x, &g_tp.party[0].y, &g_tp);
    g_tp.gold = 100000000;

    /* A full party, which is the case that actually bites: with a spare slot
       the hire succeeds anyway and the bug is invisible. */
    int first = -1;
    for (int i = 0; i < TAVERN_ROSTER; i++)
        if (companion_hire(&g_tp, i) && first < 0) first = i;
    check(first >= 0, "somebody was hired");
    check(companion_count(&g_tp) == MAX_COMPANIONS, "and the party is full");
    if (first < 0) return;

    Hero *victim = NULL;
    for (int i = 1; i < MAX_PARTY; i++)
        if (g_tp.party[i].in_use && g_tp.party[i].roster_idx == first) victim = &g_tp.party[i];
    check(victim != NULL, "and their body is in the party");
    if (!victim) return;

    hero_take_damage(&g_tp, victim, victim->hp + 999);
    check(!victim->alive, "they go down");

    /* Straight home, with no dungeon turn in between -- the whole point. */
    companions_reap_fallen(&g_tp);

    printf("  after dying with no turn in between: seat free %s, marked fallen %s\n",
           victim->in_use ? "no" : "yes", tavern_has_fallen(&g_tp, first) ? "yes" : "no");
    check(!victim->in_use, "the seat is given up");
    check(tavern_has_fallen(&g_tp, first), "and the roster remembers whose it was");
    check(companion_count(&g_tp) < MAX_COMPANIONS, "so the party is no longer full");
    check(companion_hire(&g_tp, first), "and the Tavern takes them back");
}

int main(void) {
    use_scratch_state_dir();
    test_hazard_predicates();
    test_compaction();
    test_damage_model();
    test_gear_sets();
    test_ranged_readiness();
    test_spell_pick();
    test_school_locked_spells();
    test_curated_pool();
    test_consumables_consistent();
    test_regen();
    test_archetypes();
    test_ammo_is_spendable();
    test_floor_doubling();
    test_inn_snapshot();
    test_archetype_starting_kit();
    test_companions();
    test_companion_behaviour();
    test_seeded_runs();
    test_upgrades();
    test_gold_rush();
    test_junk();
    test_altar();
    test_training();
    test_never_routes_up();
    test_stairs_always_reachable();
    test_room_variety();
    test_wild_districts();
    test_haven();
    test_bank();
    test_sense_districts();
    test_multi_turn_work();
    test_displacement();
    test_granted_companion();
    test_dda_reading();
    test_path_is_shortest();
    test_material_tiers();
    test_race_book();
    test_kitchen();
    test_meat_only_from_blades();
    test_save_round_trip();
    test_black_market();
    test_growth_is_patchy();
    test_guardian_angel();
    test_take_control();
    test_per_hero_vision();
    test_everybody_has_a_school();
    test_allies_displace();
    test_dda_reads_the_floor_underfoot();
    test_band_gifts();
    test_autoexplore_decisions();
    test_spell_pool_integrity();
    test_class_table_complete();
    test_generated_names_fit();
    test_biome_rules();
    test_watch_and_barrow();
    test_a_hire_gets_on_with_it();
    test_work_is_keyed_on_the_pair();
    test_the_proving_ground();
    test_the_variety_pass();
    test_the_prism_is_where_you_stand();
    test_the_ai_leaves_the_driven_body_alone();
    test_the_town_is_not_fogged();
    test_no_two_colours_are_the_same_pair();
    test_hearing_buys_something();
    test_the_chapel_takes_your_ears();
    test_one_strike_system_two_skins();
    test_the_oracle_sells_what_it_promises();
    test_the_altar_trades_both_ways();
    test_the_bazaar_quotes_what_it_charges();
    test_self_play_visits_every_door();
    test_a_fallen_hire_can_be_taken_back();
    test_set_procs();
    test_quest_kinds();
    test_ai_kills_count();
    test_no_body_is_special();

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
