/* Is each difficulty actually winnable?
 *
 *   make sim                 -- 40 runs per mode, summary table
 *   ./bin/simulate 200       -- 200 runs per mode
 *   ./bin/simulate 40 2      -- only mode 2 (Swarm)
 *
 * This drives the *real* systems: mapgen makes the floors, monsters.c makes
 * and escalates the monsters, combat.c resolves every blow, items.c stocks
 * the shops, spells.c supplies the magic. Nothing here re-implements a
 * formula. A model that re-derived the damage curve could only ever confirm
 * its own arithmetic; the point of this one is that it can be wrong, and
 * that when it says a mode is unwinnable that is a fact about the game.
 *
 * What it cannot be is a claim about *players* -- plural, and that plural is
 * the point. For a long time this harness modelled exactly one: someone who
 * takes the stairs the moment they see them. Every winnability number it
 * produced was that one person's win rate wearing the whole game's name.
 *
 * There are several ways people actually play a game like this, and they are
 * not the same run:
 *
 *   RUSH    take the stairs on sight. The original policy, and the honest
 *           floor: if a mode is unwinnable even with unlimited patience, a
 *           rusher was never going to be the one to find that out.
 *   STEADY  explore each floor out, then descend. What auto-explore does,
 *           and what most people do without thinking about it.
 *   FARM    stay on a floor until strong enough for the next one, letting
 *           respawns come to you. Farming as insurance.
 *   OVERKILL the same instinct with no ceiling on it: farm until the fights
 *           stop being fights. Farming as the goal rather than the tax.
 *   PRESTIGE farm, go home nearly dead, sell every level across the black
 *           market counter, spend it on the things a reset cannot take --
 *           shares, attribute points, hired heroes -- and walk back in at
 *           floor 1. The only style that gives something up on purpose, and
 *           the one the project owner actually plays.
 *
 * FARM is the one worth being careful about. An early version of this
 * harness ground to level 16 on floor 1 and stayed there, and the fix at
 * the time was to delete grinding from the policy -- which answered "can
 * this be finished quickly" and quietly stopped answering "can this be
 * finished". Grinding is not an artifact of a bad agent; it is a thing
 * players do on purpose. So it is a *style* here, bounded by a per-floor
 * turn budget and reported alongside the turns it cost, because the design
 * claim being tested is that time is the price of power. If FARM does not
 * win more than RUSH, the farming is decorative. If it wins more without
 * costing more turns, rushing is being punished for no reason.
 *
 * Shared by every style:
 *
 *   - fights what is next to it, at range when it has the reach
 *   - drinks below 45% health, and retreats toward the stairs below 25%
 *     with nothing left to drink
 *   - goes back to town at every waygate to spend everything on armour
 *     first, then a weapon, then draughts
 *   - does not kite, does not lure monsters into corridors, does not skip
 *     floors, and does not reroll a bad start
 *
 * A real player is better than this at tactics and worse at consistency.
 * Treat the numbers as a floor on what is achievable, not a forecast.
 */

#include "../src/common.h"
#include "../src/classes.h"
#include "../src/combat.h"
#include "../src/mapgen.h"
#include "../src/dda.h"
#include "../src/monsters.h"
#include "../src/items.h"
#include "../src/spells.h"
#include "../src/ranged.h"
#include "../src/abilities.h"
#include "../src/path.h"
#include "../src/vision.h"
#include "../src/gearsets.h"
#include "../src/render.h"
#include "../src/features.h"
#include "../src/companions.h"
#include "../src/dda.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A run is abandoned at this many turns on one floor: something has gone
   wrong (walled-off stairs, an unreachable frontier) and the run would
   otherwise spin forever. Counted and reported rather than hidden -- if this
   number is not near zero the simulation is measuring the wrong thing. */
/* Scaled off the map, not a flat number: a 280x160 floor takes four times
   the walking of the 140x80 one this was tuned for, and a budget that does
   not move with the map turns "big floor" into "stuck run". */
/* The old flat value. Now the floor under the style budgets rather than the
   ceiling over them -- see floor_stuck_budget(), which has to sit *above*
   whatever a style is willing to spend or a farmer deliberately staying put
   gets reported as an agent that could not find the stairs. Those are
   opposite findings and must not share a counter. */
#define TURN_BUDGET_BASE SCALE_BY_AREA(4000)
#define MAX_FLOOR 100

static bool g_verbose = false;

/* ---- playstyles -------------------------------------------------------- */

typedef enum {
    PLAY_RUSH, PLAY_STEADY, PLAY_FARM, PLAY_OVERKILL, PLAY_PRESTIGE, PLAYSTYLE_COUNT
} Playstyle;

static const char *const STYLE_NAME[PLAYSTYLE_COUNT] = {
    "rush", "steady", "farm", "overkill", "prestige"
};

/* PRESTIGE is FARM plus one thing, and the "plus one thing" is load-bearing.
 *
   It reads FARM's descent gate, FARM's patience, FARM's turn budget and
   FARM's consumable reserve -- not values that happen to be equal, the same
   case labels -- so the two rows in the results are produced by identical
   play everywhere except the cash-out. Anything that separates them was
   caused by the cycle and by nothing I chose. Every place below where
   PRESTIGE appears next to FARM is one of those shared labels, and a future
   edit that gives PRESTIGE its own number there is authoring a finding
   (EVALUATION §73).

   The one exception is Hardcore, where recall does not exist -- see the gate
   in sim_turn -- so the cycle can never fire and PRESTIGE degenerates into
   FARM exactly. Those two rows must come out identical, digit for digit.
   That is not a wasted run; it is the control that proves the prestige code
   has not leaked into the shared policy. */
static Playstyle g_style = PLAY_RUSH;

static bool style_farms(void) {
    return g_style == PLAY_FARM || g_style == PLAY_OVERKILL || g_style == PLAY_PRESTIGE;
}

/* Set by simulate_run on entering each floor, read by the descent gate. */
static int g_floor_start_turn = 0;

/* When a farmer decides a floor is finished with.
 *
   The first version of this asked for a level target and nothing else, and
   measuring it exposed two separate mistakes.

   The first was the target itself. A rusher already arrives at roughly two
   and a half times the floor number for free (floor 20 at level 51, floor 38
   at level 102), and the target asked for 4 + 2xfloor -- below that line
   everywhere past floor 1. The farmer met it on arrival and behaved exactly
   like a rusher, and the harness would have reported "farming does nothing"
   with a straight face.

   The second only showed up once the targets were raised: at depth they
   become unreachable, so every floor ran to its turn budget instead. The
   budget is a safety net, and a safety net that catches every single fall is
   not measuring anything -- the reported numbers were being produced by a
   constant. Ten minutes of wall clock for one difficulty, all of it spent
   confirming the value of TURN_BUDGET_BASE.

   So the real rule is the one players actually use: stay while the floor is
   still paying, leave when it is not. A floor that has been cleared out pays
   in a trickle of respawns, and the moment that trickle drops well below
   what the floor paid when it was full is the moment a person goes down the
   stairs. That is measurable -- experience per turn against the best rate
   this floor ever managed -- and it terminates on its own.

   The two farming tiers are then the same rule at two levels of patience,
   which is exactly the difference between farming because a boss killed you
   and farming because you intend to walk through the boss:

     FARM      leaves when the floor drops to half its best rate, and stops
               entirely once comfortably ahead of the curve.
     OVERKILL  grinds the floor down to a sixth of its best rate, and wants
               to be far enough ahead that the fights stop being fights. */
#define FARM_LEVEL_SLOPE      4
#define OVERKILL_LEVEL_SLOPE  7
static int farm_level_target(int floor) {
    int slope = (g_style == PLAY_OVERKILL) ? OVERKILL_LEVEL_SLOPE : FARM_LEVEL_SLOPE;
    return 4 + floor * slope;
}

/* How much of the purse to hold back for healing rather than spend on gear.
   Scales with how long the style intends to stay on each floor. */
static int consumable_reserve(void) {
    switch (g_style) {
        case PLAY_OVERKILL: return 3000;
        case PLAY_FARM:
        case PLAY_PRESTIGE: return 1200;
        default:            return 200;
    }
}

/* Percent of this floor's best experience rate below which the style gives
   up on it. Lower means more patient. */
static int style_patience(void) {
    return (g_style == PLAY_OVERKILL) ? 16 : 50;
}

/* Total experience ever earned. The Player only carries progress within the
   current level, so this reconstructs the cumulative figure from the game's
   own curve (combat.c: xp_next = 20 + (level-1)*15). Monotone, which is all
   the rate sampler needs. */
static long total_xp(const Player *p) {
    long L = p->party[0].level;
    return 20 * (L - 1) + 15 * (L - 1) * (L - 2) / 2 + p->party[0].xp;
}

/* How often to ask "is this floor still paying", and how long to let a floor
   prove itself before believing the answer -- the first rooms of a floor are
   too noisy to judge it on.
 *
   Both are windows measured in *walking*, so both scale off the map like
   TURN_BUDGET_BASE does, and for the same reason. As flat numbers tuned on
   the Shaft they were quietly catastrophic on the big worlds: a floor of the
   Well is 627,000 walkable tiles against the Shaft's 11,200, so 400 turns is
   not "enough to judge a floor by", it is the first corridor. A farmer would
   have sampled two rooms, decided the floor had stopped paying and taken the
   stairs -- which is FARM behaving exactly like RUSH, and is the same failure
   §71 records for the first farm target. It would not have looked like a bug.
   It would have looked like a finding that farming does nothing on large
   maps. */
#define XP_SAMPLE_TURNS SCALE_BY_AREA(250)
#define MIN_FLOOR_STAY  SCALE_BY_AREA(400)

static long g_floor_peak_rate;
static long g_last_sample_xp;
static int  g_last_sample_turn;
static bool g_floor_tapped;

/* Why the floor ended, so the model can be checked rather than trusted. If
   BUDGET dominates, the stopping rule is not doing the work and the numbers
   are a constant in disguise -- which is the exact failure this replaced. */
typedef enum { END_TARGET, END_TAPPED, END_BUDGET, END_ONSIGHT, END_RECALLED, END_COUNT } EndReason;
static const char *const END_NAME[END_COUNT] = {
    "strong enough", "floor dry", "budget", "on sight", "went home"
};
static EndReason g_end_reason = END_ONSIGHT;

static void sample_floor_yield(const Player *p) {
    int since = p->turns - g_last_sample_turn;
    if (since < XP_SAMPLE_TURNS) return;
    long now  = total_xp(p);
    long rate = (now - g_last_sample_xp) * 1000 / since;   /* xp per 1000 turns */
    g_last_sample_xp   = now;
    g_last_sample_turn = p->turns;
    if (rate > g_floor_peak_rate) g_floor_peak_rate = rate;
    if (p->turns - g_floor_start_turn < MIN_FLOOR_STAY) return;
    if (g_floor_peak_rate > 0 && rate * 100 < g_floor_peak_rate * style_patience())
        g_floor_tapped = true;
}

/* No style may spend forever on one floor. This is what keeps FARM a
   measurement rather than an infinite loop: when the budget runs out the
   farmer descends underlevelled, which is exactly what a real player does
   when they get bored, and it shows up honestly as a loss. */
static int floor_turn_budget(void) {
    switch (g_style) {
        /* Enough laps of a floor to matter, and few enough that a hundred
           of them still finishes this decade. A farmer that wants more than
           this is one the budget is supposed to catch. */
        case PLAY_OVERKILL: return TURN_BUDGET_BASE * 6;
        case PLAY_FARM:
        case PLAY_PRESTIGE: return TURN_BUDGET_BASE * 3;
        case PLAY_STEADY: return TURN_BUDGET_BASE;
        default:          return TURN_BUDGET_BASE / 2;
    }
}

/* No run may spend forever, whatever style it is playing.
 *
   For the four descending styles this is just the sum of the per-floor
   budgets they are already allowed, and none of them comes within an order
   of magnitude of it -- farm spends about 83,000 turns of a permitted
   1,200,000. It is there for PRESTIGE, whose cycle has no natural end: a
   prestige run that neither dies nor reaches the bottom will restart at
   floor 1 forever.

   It is deliberately the same formula rather than a number picked for
   prestige, so prestige is handed exactly the time farm is handed and the
   two are compared at equal cost rather than at equal patience. Runs that
   end on it are counted and reported: if that share is large, the ceiling is
   the finding and the win rate underneath it is not one. */
static long run_turn_ceiling(void) {
    return (long)MAX_FLOOR * floor_turn_budget();
}

/* How many floors between town stops. Follows the game (waygate_interval())
   unless overridden on the command line.
 *
   The override is a control, and it exists because the world probe produced
   two effects at once and could not separate them. Going from the Shaft to
   the Halls is 6.25x the floor area with the waygate rule unchanged: Normal
   went 14% -> 70%, Hard went nowhere (best 24 -> 25). Going on to the Deeps
   is 4x the area again *and* the point where the game switches to a gate on
   every floor: Hard went 90th 5 -> 32, Hardcore 90th 2 -> 22, both arriving
   at 100% health.

   Two variables moved, so the obvious reading -- "the hard modes are starved
   of town stops, not of experience" -- is an inference and not a measurement.
   Forcing the interval makes it one: run the Halls at 1 and at 5, where floor
   size is held still, and the difference is the town stops on their own. */
static int g_waygate_override = -1;

static int town_stop_interval(void) {
    return g_waygate_override > 0 ? g_waygate_override : waygate_interval();
}

/* What ends a prestige run, as opposed to what backstops it.
 *
   The ceiling above is a clock, and a measurement decided by a clock is the
   failure EVALUATION §71 records. Measured before this rule existed: half of
   two Hard runs ended on the ceiling at 600,000 turns and eighteen minutes of
   wall clock apiece -- a number produced by a constant, and too expensive to
   sample properly besides.

   So the loop ends the way the floor rule ends: on diminishing returns. A lap
   that beat the run's own deepest floor paid. So did one that bought a hero
   or a bank share, because those are the purchases with a real price step in
   front of them -- a lap that clears one has converted its levels into
   something it could not afford before. Attribute points deliberately do not
   count: at 200v^2+300 against a purse fed by the level curve, there is
   always another one affordable, so counting them would mean the loop never
   ends and the clock decides after all.

   This many laps in a row that pay nothing, and the loop is finished with.

   The number is a knob (argv[9]) and not a constant, for the reason this file
   keeps relearning: any value I pick by hand is a place where the finding is
   authored. The way to know it is not deciding is to run the same sample at
   two or three settings and watch whether the answer moves -- which is a
   thing you can only do if it is on the command line. */
static int g_prestige_patience = 6;

/* Genuinely wedged -- no route to the stairs at all -- as opposed to staying
   on purpose. The headroom is what tells them apart. */
static int floor_stuck_budget(void) {
    return floor_turn_budget() + TURN_BUDGET_BASE;
}


/* The one decision that separates the styles: is this floor finished with?
   Everything else in the policy -- how it fights, when it drinks, when it
   retreats -- is identical, so any difference in the results is caused by
   this and nothing else. */
static bool ready_to_descend(const Player *p, const Map *m, bool frontier_left) {
    (void)m;
    if (p->turns - g_floor_start_turn > floor_turn_budget()) {
        g_end_reason = END_BUDGET;
        return true;
    }
    switch (g_style) {
        case PLAY_RUSH:   g_end_reason = END_ONSIGHT; return true;
        case PLAY_STEADY: g_end_reason = END_ONSIGHT; return !frontier_left;
        case PLAY_FARM:
        case PLAY_OVERKILL:
        case PLAY_PRESTIGE:
            if (p->party[0].level >= farm_level_target(p->floor)) {
                g_end_reason = END_TARGET; return true;
            }
            if (g_floor_tapped) { g_end_reason = END_TAPPED; return true; }
            return false;
        default:          return true;
    }
}

/* A farmer with nothing left to explore goes looking for something to kill.
   Nearest live monster by walking distance, not by straight line, so it does
   not commit to something on the far side of a wall. */
static bool hunt_step(const Player *p, const Map *m, int *dx, int *dy) {
    int best = -1, best_d = 1 << 30;
    for (int i = 0; i < m->monster_count; i++) {
        if (!m->monsters[i].alive) continue;
        int d = abs(m->monsters[i].x - p->party[0].x) + abs(m->monsters[i].y - p->party[0].y);
        if (d < best_d) { best_d = d; best = i; }
    }
    if (best < 0) return false;
    return path_next_step(m, p->party[0].x, p->party[0].y, m->monsters[best].x, m->monsters[best].y,
                          true, false, dx, dy);
}

/* Starting purse. The control: if even a player handed the best gear in the
   shops cannot reach the bottom, the difficulty is not the agent's tactics.
   If a rich agent wins and a poor one does not, the binding constraint is
   the economy, not the combat. Two very different findings, and without this
   switch there is no way to tell them apart. */
static int g_start_gold = 60;

/* Everything the run picked up, so "is money the constraint" can be answered
   rather than assumed. */
static long g_total_income = 0;

/* Hire the full party of five at every town stop that can afford it. The
   best case the game offers: five autonomous heroes, each scaled to the
   player's own standing, on top of the best gear in the shops. If floor 100
   is out of reach even here, it is out of reach. */
static bool g_hire_party = false;

/* Override for the escalation cap, so the biggest single term in the monster
   attack curve can be measured instead of argued about. -1 leaves the game's
   own ESCALATION_CAP alone. */
static int g_escalation_cap = -1;
static bool g_use_bank = true;   /* argv[6]: 0 to measure the run without it */

typedef struct {
    int  deepest;
    bool won;
    bool stuck;
    int  killed_on_floor;
    char killer[48];
    int  level;
    int  turns;
    int  recalls;
    int  worst_crowd;      /* most live monsters seen on any one floor */
    int  worst_crowd_floor;
    long gold_earned;      /* everything the run ever took in */
    int  rush_floors;
    /* Win rate says whether you got there; this says what it felt like.
       A run that clears every floor at 90% health never had a fight it could
       lose, which is the power fantasy working. One that arrives at 15% won
       the same number of times and was a different game. Without this the
       two farming tiers are indistinguishable in the results even when they
       produce completely different experiences. */
    long hp_pct_sum;
    int  hp_pct_n;
    int  ended[END_COUNT];
    /* What DDA *would* have read, had anything been listening. Phase one is
       measurement only -- nothing in the game consumes pressure yet. */
    int  dda_final;
    int  dda_peak, dda_trough;

    /* The prestige cycle, reported so it can be checked rather than assumed.
       A prestige row with zero cycles is farm wearing a different name, and
       the difference between "the loop does nothing" and "the loop never
       ran" is the whole of whether the row means anything. */
    int  prestige_cycles;
    long levels_sold;
    long level_gold;       /* what the market actually paid for them */
    int  attrs_bought;
    int  heroes;           /* hired and still standing at the end */
    bool out_of_time;      /* stopped by run_turn_ceiling rather than by play */
    bool pinned;           /* alive, stairs reachable, not getting there */

    /* What the prestige loop does to the *party*, which is the question
       take-control raises and nothing here has measured.
     *
       Selling every level empties the character and leaves the hires
       untouched -- they keep their own levels, because level is body-level
       and only gold and charms are shared. So after a cash-out the strongest
       thing in the party is very often not the character, and a player with
       Tab would simply stop driving them. These two numbers say how big that
       gap is; they do not model the swap, and the win rates below are still
       a player who never presses Tab. */
    long sale_mc_level_sum;    /* the character's level right after selling */
    long sale_hire_level_sum;  /* the best hire's level at that moment */
    int  sale_samples;
    bool loop_dry;         /* stopped because the laps stopped buying depth */
} RunResult;

/* ---- the player agent ------------------------------------------------- */

static int potion_slot(const Player *p) {
    int best = -1, best_heal = 0;
    for (int i = 0; i < p->inv_count; i++) {
        const InvStack *s = &p->inventory[i];
        if (s->count <= 0) continue;
        int heal = s->heal + p->party[0].maxhp * s->heal_pct / 100;
        if (heal > best_heal) { best_heal = heal; best = i; }
    }
    return best;
}

/* Buys the way a person does: stop dying first, then hit harder, then carry
   something to drink. Runs at every town visit. */
static void go_shopping(Player *p) {
    /* Armour, best affordable that is an upgrade -- and never over a set
       piece. A relic dropped by the dungeon carries a set bonus that a shop
       item does not, and the game asks the player before it lets them make
       that trade. An agent that silently took the trade was destroying its
       own gear sets and then reporting the result as the game's difficulty. */
    for (int i = ARMOR_STOCK_COUNT - 1; p->party[0].armor_set < 0 && i >= 0; i--) {
        if (ARMOR_STOCK[i].bonus <= p->party[0].armor_bonus) break;
        if (p->gold >= ARMOR_STOCK[i].price) {
            p->gold -= ARMOR_STOCK[i].price;
            snprintf(p->party[0].armor_name, sizeof(p->party[0].armor_name), "%s", ARMOR_STOCK[i].name);
            p->party[0].armor_bonus = ARMOR_STOCK[i].bonus;
            p->party[0].armor_set = -1;
            recompute_set_bonus(hero_driven(p));
            break;
        }
    }
    /* Weapon, same two rules. */
    for (int i = WEAPON_STOCK_COUNT - 1; p->party[0].weapon_set < 0 && i >= 0; i--) {
        if (WEAPON_STOCK[i].bonus <= p->party[0].weapon_bonus) break;
        if (p->gold >= WEAPON_STOCK[i].price) {
            p->gold -= WEAPON_STOCK[i].price;
            snprintf(p->party[0].weapon_name, sizeof(p->party[0].weapon_name), "%s", WEAPON_STOCK[i].name);
            p->party[0].weapon_bonus = WEAPON_STOCK[i].bonus;
            p->party[0].weapon_set = -1;
            recompute_set_bonus(hero_driven(p));
            break;
        }
    }
    /* Something to shoot with. The agent knew how to fire and had nothing
       to fire: only the classes that start with a weapon ever used the
       ranged branch at all, so most runs were fighting the whole dungeon in
       melee. */
    {
        int best = -1;
        for (int i = 0; i < RANGED_STOCK_COUNT; i++) {
            const RangedTemplate *t = &RANGED_STOCK[i];
            if (t->price > p->gold) continue;
            if (t->bonus <= p->party[0].ranged_bonus) continue;
            if (best < 0 || t->bonus > RANGED_STOCK[best].bonus) best = i;
        }
        if (best >= 0) {
            const RangedTemplate *t = &RANGED_STOCK[best];
            p->gold -= t->price;
            equip_ranged(hero_mc(p), t->name, t->type, t->bonus + t->bonus * p->party[0].gear_bonus_pct / 100,
                         t->ammo_cost, t->cooldown);
        }
    }

    /* The regeneration ring, once it is affordable -- EVALUATION calls it the
       deliberate way out of attrition, so the agent takes it when offered. */
    if (p->party[0].ring_stat == ACC_NONE) {
        for (int i = 0; i < ACCESSORY_STOCK_COUNT; i++) {
            if (ACCESSORY_STOCK[i].stat != ACC_REGEN) continue;
            if (p->gold >= ACCESSORY_STOCK[i].price) {
                p->gold -= ACCESSORY_STOCK[i].price;
                equip_accessory(p, ACCESSORY_STOCK[i].name,
                                ACCESSORY_STOCK[i].stat, ACCESSORY_STOCK[i].bonus);
            }
            break;
        }
    }
    /* Recall charms, bought fresh at every town stop.
     *
       This was a flat two for every style, then a per-style count I picked by
       hand -- and both were wrong for the same reason: they capped something
       the game does not cap. Charms stack without limit (give_consumable just
       increments the count; INV_CAP bounds distinct stacks, not quantity),
       and the player is standing in the shop at every waygate. Nothing stops
       them buying more except what is in the purse.

       So the purse is the rule. A share of it goes on charms each visit, and
       how many that buys falls out of how the run has been going: a farmer
       who has been killing things for three thousand turns a floor is rich
       and walks out with a fistful, a rusher is poor and walks out with two.
       The difference between the styles stops being a constant I chose and
       becomes a consequence of how they play, which is the only version of
       it I can defend.

       The floor of two is kept so a broke early run still carries the game's
       own answer to "this is going badly" -- that was the original policy and
       it is not the part that was wrong. */
    long charm_budget = p->gold / 5;
    long charm_spend  = 0;
    for (int i = 0; i < APOTHECARY_STOCK_COUNT; i++) {
        if (!APOTHECARY_STOCK[i].is_recall) continue;
        int price = APOTHECARY_STOCK[i].price;
        while (p->gold >= price
               && (count_recall_charms(p) < 2 || charm_spend + price <= charm_budget)) {
            if (!give_consumable(p, &APOTHECARY_STOCK[i], 1)) break;
            p->gold    -= price;
            charm_spend += price;
        }
    }

    /* The party, if this run is testing the best case. */
    if (g_hire_party) {
        for (int i = 0; i < TAVERN_ROSTER && companion_count(p) < MAX_COMPANIONS; i++)
            companion_hire(p, i);
    }

    /* Health first of everything: hits-to-die is HP x (atk+def) / atk^2, and
       HP is the only term in it that nothing but levelling used to grow. */
    for (int guard = 0; guard < 5000; guard++) {
        int hp = upgrade_price(p->party[0].hp_plus);
        if (p->gold < hp + 200) break;
        p->gold -= hp;
        p->party[0].hp_plus++;
        p->party[0].maxhp += UPGRADE_HP_STEP;
        p->party[0].hp += UPGRADE_HP_STEP;
    }
    if (p->party[0].spell_count > 0) {
        for (int guard = 0; guard < 2000; guard++) {
            int ae = upgrade_price(p->party[0].aether_plus);
            if (p->gold < ae + 200) break;
            p->gold -= ae;
            p->party[0].aether_plus++;
            p->party[0].aether_max += UPGRADE_AETHER_STEP;
            p->party[0].aether = p->party[0].aether_max;
        }
    }
    if (p->party[0].ranged_type != RANGED_NONE) {
        for (int guard = 0; guard < 2000; guard++) {
            int am = upgrade_price(p->party[0].ammo_plus);
            if (p->gold < am + 200) break;
            p->gold -= am;
            p->party[0].ammo_plus++;
            p->party[0].ranged_ammo_max += UPGRADE_AMMO_STEP;
            p->party[0].ranged_ammo = p->party[0].ranged_ammo_max;
        }
    }

    /* The smith, until the purse says stop. This is the uncapped track --
       the shops top out at +27 and the dungeon does not top out at all --
       so a descending player keeps a reserve and spends the rest here.
       Armour first: the wall EVALUATION §22 measured is a defence wall. */
    for (int guard = 0; guard < 5000; guard++) {
        int ap = upgrade_price(p->party[0].armor_plus);
        /* Reserve for draughts before sinking the purse into plate. A farmer
           drinks its way through a floor it refuses to leave; a rusher is
           past the monsters before it needs a second potion. */
        if (p->gold < ap + consumable_reserve()) break;
        p->gold -= ap;
        p->party[0].armor_plus++;
        p->party[0].armor_bonus += UPGRADE_STEP;
    }
    for (int guard = 0; guard < 5000; guard++) {
        int wp = upgrade_price(p->party[0].weapon_plus);
        if (p->gold < wp + consumable_reserve()) break;
        p->gold -= wp;
        p->party[0].weapon_plus++;
        p->party[0].weapon_bonus += UPGRADE_STEP;
    }

    /* Whatever is left, on the best healing the purse can carry. */
    for (int guard = 0; guard < 40; guard++) {
        int pick = -1;
        for (int i = 0; i < APOTHECARY_STOCK_COUNT; i++) {
            const ConsumableTemplate *t = &APOTHECARY_STOCK[i];
            if (t->heal <= 0 && t->heal_pct <= 0) continue;
            if (t->price > p->gold) continue;
            if (pick < 0 || t->heal + t->heal_pct > APOTHECARY_STOCK[pick].heal + APOTHECARY_STOCK[pick].heal_pct)
                pick = i;
        }
        if (pick < 0) break;
        if (!give_consumable(p, &APOTHECARY_STOCK[pick], 1)) break;
        p->gold -= APOTHECARY_STOCK[pick].price;
    }
}

/* A town stop, as the game offers it: the Inn heals for a fifth of the purse,
   then whatever is left goes on gear. Without this the agent descends at
   whatever health it happened to finish the last floor on, which is not how
   anybody plays -- an early version walked into floor 2 at 19/184 and the
   run was over before the question had been asked. */
/* A share is worth buying only if the run continues long enough to earn
   through it, so this buys whenever the purse can stand it three times over
   -- keeping the rest for gear, which is what actually keeps you alive. */
static void visit_bank(Player *p) {
    if (!g_use_bank) return;
    for (int guard = 0; guard < 8; guard++) {
        long price = bank_price(p->gold_mult);
        if (price < 0) return;
        long paid = price - price * p->party[0].shop_discount_pct / 100;
        if (paid < 0) paid = 0;
        if ((long)p->gold < paid * 3) return;
        p->gold -= (int)paid;
        p->gold_mult++;
    }
}

static void visit_town(Player *p) {
    /* The junkyard first: it is where the money for everything else comes
       from, and a player who walked past it would be shopping with a
       fraction of the purse they actually earned. */
    /* Sells scrap only. Platinum and diamond are what the deep upgrade rungs
       want (items.c), so an agent that liquidated them would be measuring a
       strategy no sensible player would use -- and would hide the material
       gate entirely. */
    if (p->mat_count[MAT_SCRAP] > 0) {
        long raw = p->mat_value[MAT_SCRAP];
        if (raw > 2000000000L) raw = 2000000000L;
        long paid = player_gain_gold(p, (int)raw);
        g_total_income += paid;
        p->mat_count[MAT_SCRAP] = 0;
        p->mat_value[MAT_SCRAP] = 0;
    }
    if (p->party[0].hp < p->party[0].maxhp) {
        p->gold -= p->gold / 5;       /* the Inn's price */
        p->party[0].hp = p->party[0].maxhp;
    }
    p->party[0].aether = p->party[0].aether_max;
    visit_bank(p);
    go_shopping(p);
}

/* Learns what the guild will sell, cheapest first, whenever there is coin
   spare. Casters that never buy magic are not what the mode is being tested
   with. */
static void go_to_guild(Player *p) {
    for (int guard = 0; guard < 40; guard++) {
        int pick = -1, pick_price = 0;
        int n = spell_school_count(p->party[0].magic_school);
        for (int i = 0; i < n; i++) {
            int idx = spell_school_index(p->party[0].magic_school, i);
            const SpellTemplate *t = spell_pool_get(idx);
            if (!t || t->learn_price > p->gold) continue;
            bool known = false;
            for (int k = 0; k < p->party[0].spell_count; k++)
                if (p->party[0].known_spells[k] == idx) known = true;
            if (known) continue;
            /* Heaviest affordable, so the purse buys power not variety. */
            if (pick < 0 || t->learn_price > pick_price) { pick = idx; pick_price = t->learn_price; }
        }
        if (pick < 0 || p->party[0].spell_count >= MAX_KNOWN_SPELLS) break;
        p->gold -= pick_price;
        p->party[0].known_spells[p->party[0].spell_count++] = pick;
    }
}

/* ---- the prestige cycle ------------------------------------------------
 *
 * The one thing PRESTIGE does that no other style does. Everything above is
 * shared; this is the whole of the difference.
 *
 * The loop being modelled, in the project owner's own words: farm, recall to
 * town when nearly dead on whatever floor you happen to be on, sell *every*
 * level at the black market, spend the gold on the bank and attributes and
 * heroes, then start again from floor 1.
 *
 * What makes it a loop rather than a slow suicide is which side of the reset
 * each thing falls on. Levels are the only thing given up: the market takes
 * them, and with them the attack, defence and hit points they carried. Shares,
 * attribute points, hired heroes, gear and smith upgrades all survive, because
 * they live on the Player and the Player does not restart. So the cycle
 * converts something impermanent into something that is not, at a price -- and
 * whether that price is worth paying is exactly what has never been measured.
 *
 * Two things in the game push back on it without being told to, and both are
 * the game's own rules rather than anything modelled here:
 *
 *   - the Inn takes a fifth of the purse, so cashing out rich costs more;
 *   - escalation reads the player's *level* as well as their floor entries
 *     (monsters.c), so selling down to 1 makes the next descent's monsters
 *     weaker -- and the floor_entries term keeps climbing regardless, so the
 *     restarts get harder anyway. Which of the two wins is a finding.
 */

/* Attribute points, cheapest first.
 *
   Which of the thirty to buy is precisely the kind of decision that authors
   its own finding (EVALUATION §73), so it is not made here. training_price
   is 200v^2+300, so the cheapest point on offer is always whichever attribute
   currently sits lowest; buying the cheapest repeatedly needs no opinion from
   me about which attributes are worth having, and it buys the most points for
   the gold. All thirty feed something in compute_derived, so none of them is
   spent on nothing.

   A player who knows the sheet will beat this comfortably -- they would pour
   it into Brawn and Stamina and read the max-HP term. So what the prestige
   row measures is a floor on the loop, not its ceiling, in the same way the
   rest of this harness is a floor on the game. */
static void prestige_train(Player *p, RunResult *r) {
    for (int guard = 0; guard < 20000; guard++) {
        int cheapest = 0;
        for (int a = 1; a < ATTR_COUNT; a++)
            if (p->party[0].attrs[a] < p->party[0].attrs[cheapest]) cheapest = a;

        int price = training_price(p->party[0].attrs[cheapest]);
        price -= price * p->party[0].shop_discount_pct / 100;    /* as main.c charges it */
        if (price < 0) price = 0;
        if (p->gold < price + consumable_reserve()) break;

        p->gold -= price;
        train_attribute(hero_driven(p), cheapest);
        r->attrs_bought++;
    }
}

/* Heroes: 1,000, then 10,000, 100,000, a million, ten million. Eleven million
   for the five, which is deeper than any purse this harness has ever recorded
   a descending run holding. It is a prestige-scale sink by construction, so a
   prestige style that never reaches past the first one or two is a finding
   about the loop's income rather than an oversight in the shopping -- and the
   counter is there so the two can be told apart. */
static int prestige_hire(Player *p, RunResult *r) {
    int before = companion_count(p);
    for (int i = 0; i < TAVERN_ROSTER && companion_count(p) < MAX_COMPANIONS; i++) {
        long price = companion_hire_cost(companion_count(p));
        if ((long)p->gold < price + consumable_reserve()) break;
        companion_hire(p, i);
    }
    r->heroes = companion_count(p);
    return r->heroes - before;
}

/* Returns true if the lap bought something it could not have bought before --
   a bank share or a hero. See g_prestige_patience for why attribute points do
   not count toward that. */
static bool prestige_cash_out(Player *p, RunResult *r) {
    /* The market, first and completely -- every level, which is what makes
       this the prestige loop rather than a trim. Sold one at a time through
       the game's own player_sell_level so the stat arithmetic is the game's,
       and paid through player_gain_gold so the bank's share applies exactly
       as main.c's counter applies it. The price is read *before* the sale,
       because selling is what changes the level it is priced from. */
    while (p->party[0].level > 1) {
        long price = level_sale_price(p->party[0].level, p->deepest_floor);
        if (!player_sell_level(p)) break;
        if (price > 2000000000L) price = 2000000000L;
        long paid = player_gain_gold(p, (int)price);
        g_total_income += paid;
        r->levels_sold += 1;
        r->level_gold  += paid;
    }

    /* Then the permanent half of the shopping list, ahead of the descending
       half. The order is a real choice and this is the one the loop is named
       for: shares, points and people are what the reset cannot take. */
    int shares_before = p->gold_mult;
    visit_bank(p);
    prestige_train(p, r);
    int hired = prestige_hire(p, r);

    /* And then the ordinary town stop every style makes -- junkyard, the
       Inn's fifth, the bank again, gear, draughts, charms. Whatever the
       permanent purchases left is spent the way every other style spends it,
       so the descending half of the two policies stays identical. */
    visit_town(p);
    go_to_guild(p);

    /* Sampled after the market and after the shopping, which is the moment a
       player looking at the party would decide who to be. */
    {
        int best = 0;
        for (int i = 0; i < MAX_COMPANIONS; i++)
            if (p->party[(i) + 1].in_use && p->party[(i) + 1].alive
                && p->party[(i) + 1].level > best)
                best = p->party[(i) + 1].level;
        r->sale_mc_level_sum   += p->party[0].level;
        r->sale_hire_level_sum += best;
        r->sale_samples++;
    }

    r->prestige_cycles++;
    return hired > 0 || p->gold_mult > shares_before;
}

/* Everything the game does for a player who walks onto a square, which
   main.c does inline and this had been silently skipping: loot, and the
   shrines and fountains and machines. An agent that walked over every coin
   on every floor and picked up none of them was being asked to fund a
   hundred-floor run on kill rewards alone, and its poverty was being read as
   the game's difficulty. */
static void arrive_at(Player *p, Map *m) {
    for (int i = 0; i < m->item_count; i++) {
        FloorItem *fi = &m->items[i];
        if (fi->used || fi->x != p->party[0].x || fi->y != p->party[0].y) continue;
        fi->used = true;

        bool boon = p->floor > 0 && p->floor <= p->gold_boon_until_floor;
        if (fi->is_gold) {
            int amount = fi->gold_amount;
            if (p->party[0].gold_bonus_pct > 0) amount += amount * p->party[0].gold_bonus_pct / 100;
            if (boon) amount *= 2;
            p->gold += amount;
            g_total_income += amount;
        } else if (fi->is_junk) {
            int tier = material_tier_for_floor(m->floor_num);
            p->mat_count[tier]++;
            p->mat_value[tier] += fi->junk_value * (boon ? 2 : 1);
        } else if (fi->is_key) {
            p->keys++;
        } else if (fi->is_quest_item) {
            p->quest_item_found = true;
        } else {
            ConsumableTemplate t;
            memset(&t, 0, sizeof(t));
            snprintf(t.name, sizeof(t.name), "%s", fi->name);
            t.heal = fi->heal;
            t.atk_buff = fi->atk_buff;
            t.atk_buff_turns = fi->atk_buff_turns;
            t.def_buff = fi->def_buff;
            t.def_buff_turns = fi->def_buff_turns;
            t.is_recall = fi->is_recall;
            give_consumable(p, &t, 1);
        }
    }

    for (int i = 0; i < m->feature_count; i++) {
        MapFeature *f = &m->features[i];
        if (f->x == p->party[0].x && f->y == p->party[0].y) trigger_feature(m, f, p);
    }
}

/* Take one step, or hit whatever is standing in it. Returns false if the
   move was impossible for some other reason.

   The "or hit" half is not a flourish. Without it the agent freezes the
   moment a monster parks itself on the stairs or plugs the only corridor:
   the step is refused, no other branch applies, and the run spins until the
   turn budget kills it. Measured as a stuck run on floor 8 before this
   existed. */
static bool step_or_fight(Player *p, Map *m, int dx, int dy, bool *out_won) {
    int nx = p->party[0].x + dx, ny = p->party[0].y + dy;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return false;

    Monster *blocker = monster_at(m, nx, ny);
    if (blocker && blocker->alive) {
        bool boss_killed = false;
        hero_attack_monster(p, hero_mc(p), m, blocker, &boss_killed);
        if (boss_killed) *out_won = true;
        return true;
    }
    if (!is_walkable_player(m, nx, ny)) return false;
    p->party[0].x = nx; p->party[0].y = ny;
    arrive_at(p, m);
    return true;
}

/* One player turn. Returns false once the player is dead. */
static bool sim_turn(Player *p, Map *m, bool *out_won, bool *out_recalled) {
    if (p->party[0].hp <= 0) return false;

    int hp_pct = p->party[0].maxhp > 0 ? p->party[0].hp * 100 / p->party[0].maxhp : 0;

    /* PRESTIGE goes home on the same reading everybody else goes home on --
       thirty percent, the game's own charm, the branch further down -- but
       without drinking its way back into the fight first.
     *
       That one dropped condition is the difference between a style and a
       rounding error. For the other four the charm is what you reach for
       once the draughts have run out, so the trigger is "hurt *and* out of
       options"; a farmer who has spent its purse on forty healing draughts
       essentially never satisfies it. Measured: prestige with the shared
       trigger ran zero laps in three runs, which is farm wearing a different
       name. For this style the trip home is the plan rather than the
       failure, so a floor that has taken two thirds of the bar has made its
       point and the levels are worth more across the counter than they are
       spent staying.

       Note what is *not* changed: not the threshold, not the charm, not the
       Hardcore refusal, not what happens after. A new number here would be
       a finding I authored (EVALUATION §73); reusing the existing one leaves
       only the condition that the cycle actually requires. */
    if (g_style == PLAY_PRESTIGE && hp_pct < 30
        && p->difficulty != DIFFICULTY_HARDCORE
        && count_recall_charms(p) > 0) {
        consume_recall_charm(p);
        *out_recalled = true;
        return true;
    }

    /* Drink before it is too late to matter. */
    if (hp_pct < 45) {
        int slot = potion_slot(p);
        if (slot >= 0) {
            use_inventory_item(p, slot);
            goto resolve;
        }
    }

    {
        Monster *adj = NULL;
        for (int dy = -1; dy <= 1 && !adj; dy++)
            for (int dx = -1; dx <= 1 && !adj; dx++)
                if (dx || dy) {
                    Monster *c = monster_at(m, p->party[0].x + dx, p->party[0].y + dy);
                    if (c && c->alive) adj = c;
                }

        /* Badly hurt and nothing to drink: break off toward the stairs.
           A player who fights every fight to the end is not a competent
           player, and modelling one would understate every mode. */
        /* Out of health and out of draughts: crack a charm and go home.
           That is what the charm is for, and a model that never uses one is
           measuring a game the player is not playing. */
        /* Except in Hardcore, where main.c refuses the key outright ("Recall
           is disabled in Hardcore mode. You're on your own."). consume_recall_charm
           does not know about difficulty -- the refusal lives in the input
           handler -- so this harness had been letting Hardcore characters
           walk home from a lost fight for as long as the branch has existed.
           Every Hardcore number this project has quoted was measured against
           an escape hatch the mode does not have. It matters most to
           PRESTIGE, whose entire cycle is a recall, and it is the reason the
           prestige and farm rows are identical on Hardcore. */
        if (hp_pct < 30 && potion_slot(p) < 0
            && p->difficulty != DIFFICULTY_HARDCORE
            && count_recall_charms(p) > 0) {
            consume_recall_charm(p);
            *out_recalled = true;
            return true;
        }

        if (hp_pct < 25 && potion_slot(p) < 0) {
            int dx, dy;
            if (path_next_step(m, p->party[0].x, p->party[0].y, m->stairs_down_x, m->stairs_down_y,
                               true, false, &dx, &dy)
                && step_or_fight(p, m, dx, dy, out_won)) {
                if (*out_won) return false;
                goto resolve;
            }
        }

        if (adj) {
            bool boss_killed = false;
            /* The class ability is free and on a cooldown, so a competent
               player spends it whenever it is up and something is in front
               of them. */
            if (p->party[0].ability_cd == 0) use_ability(p, hero_mc(p), m, &boss_killed);
            else hero_attack_monster(p, hero_mc(p), m, adj, &boss_killed);
            if (boss_killed) { *out_won = true; return false; }
            goto resolve;
        }

        /* At range: spell first (it is the bigger number), then the weapon. */
        Monster *far_target = find_nearest_target(m, p, 7);
        if (far_target) {
            int d = abs(far_target->x - p->party[0].x) > abs(far_target->y - p->party[0].y)
                  ? abs(far_target->x - p->party[0].x) : abs(far_target->y - p->party[0].y);
            int slot = spell_pick_attack_slot(hero_mc(p), d);
            bool boss_killed = false;
            if (slot >= 0 && cast_spell_slot(p, hero_mc(p), m, slot, &boss_killed)) {
                if (boss_killed) { *out_won = true; return false; }
                goto resolve;
            }
            if (p->party[0].ranged_type != RANGED_NONE && p->party[0].ranged_cooldown == 0
                && p->party[0].ranged_ammo >= p->party[0].ranged_ammo_cost) {
                if (fire_ranged(p, hero_mc(p), m, &boss_killed)) {
                    if (boss_killed) { *out_won = true; return false; }
                    goto resolve;
                }
            }
            /* Deliberately NOT closing on it. A descending player shoots
               what is in the way and keeps walking; an agent that chased
               every monster within seven squares never explored at all,
               which on a floor with respawns is a loop that only ends one
               way -- measured at 1,996 live monsters on floor 8 of Normal,
               against a cap of 2,000, because the doubling timer kept firing
               while it fought. Chasing is a choice, and it is not the one
               being tested. */
        }

        /* Nothing to fight, so the only question left is whether this floor
           is finished with -- and that is the whole of the playstyle. The
           order below is the same for everyone; what changes is whether the
           stairs are tried before exploring or after. */
        int dx, dy;
        bool know_stairs = m->stairs_down_x >= 0 && m->stairs_down_y >= 0
                        && m->tiles[m->stairs_down_y][m->stairs_down_x].seen;
        bool frontier_left = path_frontier_step(m, p->party[0].x, p->party[0].y, true, &dx, &dy);
        bool descend = ready_to_descend(p, m, frontier_left);

        /* Done here: take the stairs the moment they are known. For RUSH
           this is always, which is the original policy exactly. */
        if (descend && know_stairs
            && path_next_step(m, p->party[0].x, p->party[0].y, m->stairs_down_x, m->stairs_down_y, true, false, &dx, &dy)
            && step_or_fight(p, m, dx, dy, out_won)) {
            if (*out_won) return false;
            goto resolve;
        }
        /* Not done: walk to the edge of the map and keep looking. */
        if (frontier_left
            && path_frontier_step(m, p->party[0].x, p->party[0].y, true, &dx, &dy)
            && step_or_fight(p, m, dx, dy, out_won)) {
            if (*out_won) return false;
            goto resolve;
        }
        /* Floor explored out and still not strong enough: go and find
           something. Only a farmer does this, and only while under its
           level target -- everyone else falls through to the stairs. */
        if (!descend && style_farms()
            && hunt_step(p, m, &dx, &dy)
            && step_or_fight(p, m, dx, dy, out_won)) {
            if (*out_won) return false;
            goto resolve;
        }
        if (know_stairs
            && path_next_step(m, p->party[0].x, p->party[0].y, m->stairs_down_x, m->stairs_down_y, true, false, &dx, &dy)
            && step_or_fight(p, m, dx, dy, out_won)) {
            if (*out_won) return false;
            goto resolve;
        }
        /* Hazards are the last resort: a floor whose only route is through
           lava is still a floor that has to be crossed. */
        if (path_next_step(m, p->party[0].x, p->party[0].y, m->stairs_down_x, m->stairs_down_y, false, false, &dx, &dy)
            && step_or_fight(p, m, dx, dy, out_won)) {
            if (*out_won) return false;
            goto resolve;
        }
        /* Genuinely wedged: spend the turn rather than spin. */
    }

resolve:
    /* Exactly what main.c's resolve_after_player_turn does, minus drawing. */
    if (m->floor_num > 0) companions_take_turn(p, m);
    hero_tick_buffs(p, hero_driven(p));
    hero_tick_status(p, hero_driven(p));
    hero_tick_regen(p, hero_driven(p));
    tick_spells(hero_mc(p));
    tick_ranged(p, hero_mc(p));
    tick_ability(hero_mc(p));
    if (p->party[0].hp <= 0) {
        if (!guardian_catch(p)) return false;
        guardian_follow_through(p, m);
    }

    p->turns++;
    sample_floor_yield(p);
    if (m->floor_num > 0 && p->turns % JUNK_STEPS_PER_PIECE == 0) {
        bool boon = p->floor > 0 && p->floor <= p->gold_boon_until_floor;
        int tier = material_tier_for_floor(m->floor_num);
        p->mat_count[tier]++;
        p->mat_value[tier] += junk_value_for_floor(m->floor_num) * (boon ? 2 : 1);
    }

    bool died = false;
    process_monster_turns(m, p, &died);
    if (died) return false;
    if (p->party[0].slow_turns_left > 0) {
        p->party[0].slow_turns_left--;
        process_monster_turns(m, p, &died);
        if (died) return false;
    }
    compact_dead_monsters(m);
    maybe_respawn_monsters(m, p);
    /* The same call the game makes. This was compute_fov() plus
       companions_reveal(); the game composites six per-body maps now, and a
       harness that lights the floor differently from the game is a harness
       that measures its own lighting (EVALUATION 68, 79). */
    hero_vision_update(m, p, hero_mc_c(p)->fov_radius);
    return true;
}

/* On the stairs *and* done with the floor. Costs a frontier search, but only
   on the one tile in the floor where the answer can be yes. */
static bool floor_finished(const Player *p, const Map *m) {
    if (p->party[0].x != m->stairs_down_x || p->party[0].y != m->stairs_down_y) return false;
    int dx, dy;
    bool frontier_left = path_frontier_step(m, p->party[0].x, p->party[0].y, true, &dx, &dy);
    return ready_to_descend(p, m, frontier_left);
}

/* Whatever is standing next to the player when they fall -- the closest the
   simulation gets to "what killed you". */
static void note_killer(const Map *m, const Player *p, char *out, size_t n) {
    const Monster *best = NULL;
    int best_d = 1 << 30;
    for (int i = 0; i < m->monster_count; i++) {
        const Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        int dx = mo->x - p->party[0].x, dy = mo->y - p->party[0].y;
        int d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = mo; }
    }
    snprintf(out, n, "%s", best ? best->name : "attrition");
}

static RunResult simulate_run(int difficulty, int class_id, unsigned int seed) {
    RunResult r;
    memset(&r, 0, sizeof(r));

    static Map m;
    Player p;
    memset(&p, 0, sizeof(p));
    p.difficulty = difficulty;
    p.run_seed = seed ? seed : 1u;
    p.tavern_seed = seed ^ 0x5bf03635u;
    apply_class_to_player(&p, class_id);
    p.party[0].level = 1;                      /* main.c does this at creation */
    p.gold = g_start_gold;
    p.gold_mult = 1;
    grant_starting_kit(&p);
    snprintf(p.party[0].name, sizeof(p.party[0].name), "sim");

    srand(seed);
    g_total_income = 0;

    /* "Not the first floor of the run" rather than "not floor 1". PRESTIGE
       comes back to floor 1 over and over, and those arrivals are floors like
       any other -- counting them by number would drop every one of them out
       of the end-reason tally and the comfort sample, which is most of the
       evidence about the style. */
    bool first_floor = true;
    bool prestige_restart = false;
    int  deepest_at_last_lap = 0, fruitless_laps = 0;

    for (int floor = 1; floor <= MAX_FLOOR; floor++) {
        /* Town stop at every waygate: the run's chance to convert gold into
           survival, which is a real part of whether a mode is winnable. */
        /* The game's own rule, not a copy of it. mapgen places a waygate on
           every floor where `floor_num % waygate_interval() == 0`, and that
           interval is 1 rather than 5 on the Deeps and the Well -- a floor
           there is a journey, so "climb four floors to reach a gate" would be
           an evening of retracing cleared ground. Hardcoding 5 gave the agent
           a fifth of the town stops the game actually offers on the two large
           worlds: a fifth of the shopping, the banking and the upgrades. Any
           winnability number measured for those worlds would have been low,
           and low for a reason that is not in the game. */
        if (floor == 1 || floor % town_stop_interval() == 0) {
            visit_town(&p);
            go_to_guild(&p);
        }

        if (!first_floor) r.ended[g_end_reason]++;
        if (p.party[0].maxhp > 0 && !first_floor) {
            /* Health on arriving at this floor is health on leaving the last
               one: the cheapest honest sample of how the run is going. */
            r.hp_pct_sum += p.party[0].hp * 100 / p.party[0].maxhp;
            r.hp_pct_n++;
        }
        if (m.gold_rush) r.rush_floors++;
        if (g_verbose && g_hire_party)
            printf("        party: %d alive\n", companion_count(&p));

        p.floor = floor;
        g_floor_start_turn = p.turns;     /* the descent gate's clock */
        g_floor_peak_rate  = 0;
        g_last_sample_xp   = total_xp(&p);
        g_last_sample_turn = p.turns;
        g_floor_tapped     = false;
        if (floor > p.deepest_floor) p.deepest_floor = floor;
        /* The high-water mark, not the current floor. It was an assignment,
           which is the same thing while floor only ever goes up -- and stops
           being the same thing the moment PRESTIGE walks back to floor 1 and
           reports the run as having got there. */
        if (floor > r.deepest) r.deepest = floor;
        first_floor = false;

        /* Same two lines main.c runs on entering a floor. */
        p.floor_entries++;
        {
            monsters_set_difficulty(p.difficulty);
            int esc = monsters_escalation_for(p.party[0].level, p.floor_entries)
                    + avenger_escalation(&p);   /* the master, if switched on */
            if (g_escalation_cap >= 0 && esc > g_escalation_cap) esc = g_escalation_cap;
            monsters_set_escalation(esc);
        }

        if (g_verbose)
            printf("  floor %-3d lvl %-2d hp %3d/%-3d atk %-3d def %-3d gold %-6d "
                   "potions %d spells %d  +%d/+%d hp+%d ae+%d am+%d  ranged %s\n",
                   floor, p.party[0].level, p.party[0].hp, p.party[0].maxhp, hero_eff_atk(hero_driven_c(&p)), hero_eff_def(hero_driven_c(&p)),
                   p.gold, p.inv_count, p.party[0].spell_count, p.party[0].weapon_plus, p.party[0].armor_plus,
                   p.party[0].hp_plus, p.party[0].aether_plus, p.party[0].ammo_plus,
                   p.party[0].ranged_type == RANGED_NONE ? "none" : p.party[0].ranged_name);

        int sx = 0, sy = 0;
        dda_floor_begin(&p);
        generate_temple_floor(&m, floor, &sx, &sy, &p);
        p.party[0].x = sx; p.party[0].y = sy;
        companions_place(&p, &m);
        hero_vision_update(&m, &p, p.party[0].fov_radius);

        bool won = false, recalled = false;
        int budget = 0;
        /* Standing on the staircase is not the same as choosing to use it.
           This loop used to end the moment the player's feet touched the
           tile, which quietly made every style a rusher: an explorer whose
           route happened to cross the stairs was pulled down a floor it had
           not finished with. The gate decides; the tile is only where the
           decision gets acted on. */
        while (!floor_finished(&p, &m)) {
            if (p.turns > run_turn_ceiling()) {
                /* Out of time rather than out of luck. Only PRESTIGE can
                   realistically get here; for anyone else it would mean the
                   per-floor budgets had all been spent, which the stuck
                   counter would have caught first. */
                r.out_of_time = true;
                break;
            }
            if (++budget > floor_stuck_budget()) {
                /* Two very different situations wore this one counter, and
                   the guardian angel made the difference matter: with the
                   master on, Hard's "stuck" runs went 1 in 60 to 9 in 60, and
                   reading it as "the harness cannot find the stairs nine
                   times as often" would have been wrong. A player who cannot
                   be hit and cannot kill fast enough is *pinned* in a crowd --
                   alive, on a floor with a perfectly reachable staircase, not
                   getting there. That is a fact about the fight, not a defect
                   in the search, and it belongs in a different column.

                   Wedged is the original meaning: no route to the stairs at
                   all. That is the one that says the simulation is measuring
                   the wrong thing. */
                int dx0, dy0;
                bool reachable =
                    m.stairs_down_x >= 0 && m.stairs_down_y >= 0
                    && (path_next_step(&m, p.party[0].x, p.party[0].y, m.stairs_down_x, m.stairs_down_y,
                                       true, false, &dx0, &dy0)
                     || path_next_step(&m, p.party[0].x, p.party[0].y, m.stairs_down_x, m.stairs_down_y,
                                       false, false, &dx0, &dy0));
                if (reachable) r.pinned = true; else r.stuck = true;
                if (g_verbose) {
                    int alive = 0;
                    for (int k = 0; k < m.monster_count; k++) if (m.monsters[k].alive) alive++;
                    printf("  STUCK floor %d: at (%d,%d) stairs (%d,%d) seen=%d "
                           "walkable=%d alive=%d hp=%d/%d\n",
                           floor, p.party[0].x, p.party[0].y, m.stairs_down_x, m.stairs_down_y,
                           m.stairs_down_y >= 0 ? m.tiles[m.stairs_down_y][m.stairs_down_x].seen : -1,
                           is_walkable_player(&m, m.stairs_down_x, m.stairs_down_y),
                           alive, p.party[0].hp, p.party[0].maxhp);
                }
                break;
            }
            if (recalled) {
                r.recalls++;
                recalled = false;

                /* For PRESTIGE the charm is not a round trip, it is the end
                   of a lap: cash out, and go back in at the top. Setting the
                   end reason rather than incrementing the counter here keeps
                   the tally single-entry -- the next floor's header records
                   it, the same way it records every other reason a floor
                   ended. */
                if (g_style == PLAY_PRESTIGE) {
                    bool bought_up = prestige_cash_out(&p, &r);
                    g_end_reason = END_RECALLED;

                    /* Did the lap buy anything a previous lap could not?
                       Depth, or a step up a price ladder. */
                    if (r.deepest > deepest_at_last_lap || bought_up) {
                        deepest_at_last_lap = r.deepest;
                        fruitless_laps = 0;
                    } else if (++fruitless_laps >= g_prestige_patience) {
                        r.loop_dry = true;
                        break;
                    }
                    prestige_restart = true;
                    break;
                }

                /* Everyone else: home, patched up, restocked -- then straight
                   back down to the floor that went badly, which is the round
                   trip the recall charm actually buys. */
                visit_town(&p);
                go_to_guild(&p);
                generate_temple_floor(&m, floor, &sx, &sy, &p);
                p.party[0].x = sx; p.party[0].y = sy;
                companions_place(&p, &m);
                hero_vision_update(&m, &p, p.party[0].fov_radius);
                continue;
            }
            if ((budget & 63) == 0) {
                int alive = 0;
                for (int k = 0; k < m.monster_count; k++) if (m.monsters[k].alive) alive++;
                if (alive > r.worst_crowd) { r.worst_crowd = alive; r.worst_crowd_floor = floor; }
            }
            if (!sim_turn(&p, &m, &won, &recalled)) {
                if (won) {
                    r.won = true; r.level = p.party[0].level; r.turns = p.turns;
                    /* This line was missing, so every run that killed the
                       boss reported nought gold found and dragged the mode's
                       income average toward zero in proportion to how often
                       it won -- the one measurement most likely to be quoted
                       as "the economy is short", worsened by success. */
                    r.gold_earned = g_total_income;
                    r.heroes = companion_count(&p);
                    if (p.party[0].maxhp > 0) { r.hp_pct_sum += p.party[0].hp * 100 / p.party[0].maxhp; r.hp_pct_n++; }
                    return r;
                }
                r.killed_on_floor = floor;
                r.heroes = companion_count(&p);
                note_killer(&m, &p, r.killer, sizeof(r.killer));
                if (g_verbose)
                    printf("  DIED on floor %d at level %d, killed by %s (turn %d)\n",
                           floor, p.party[0].level, r.killer, p.turns);
                r.level = p.party[0].level;
                r.turns = p.turns;
                r.gold_earned = g_total_income;
                return r;
            }
        }
        /* Read the floor that just ended, and remember the extremes -- the
           final number matters less than how far the curve travelled. */
        dda_floor_end(&p, &m);
        /* The settled history, not the total. This samples immediately after
           dda_floor_end() has filed the floor's verdict, and at that moment
           dda_floor_so_far() is still reading the same floor's low health --
           so dda_pressure() would count it twice and the report would show
           roughly double the movement that actually happened. Reporting the
           half that changed is what this line has always meant. */
        r.dda_final = dda_settled(&p);
        if (r.dda_final > r.dda_peak)   r.dda_peak = r.dda_final;
        if (r.dda_final < r.dda_trough) r.dda_trough = r.dda_final;

        if (r.stuck || r.pinned || r.out_of_time || r.loop_dry) break;

        /* Back to the top of the temple. floor++ turns this into 1, which
           re-runs the waygate town stop as well -- a lap starts in town for
           the same reason every other run does. */
        if (prestige_restart) { prestige_restart = false; floor = 0; }
    }

    r.level = p.party[0].level;
    r.turns = p.turns;
    r.gold_earned = g_total_income;
    r.heroes = companion_count(&p);
    /* Reaching the bottom counts as a win; being *abandoned* on the bottom
       does not. `stuck` means the harness could not find a way onward -- an
       unreachable staircase, a walled-off frontier -- and a run that ends
       that way on floor 100 was scored as a victory purely because
       `r.deepest` had been set on arrival. Rare, and it showed up as
       arithmetic that would not add: Normal steady reported 10 wins, 18
       deaths and 3 stuck out of 30 runs. A run cannot be two of those.

       The rows in EVALUATION §83 were measured before this line changed. The
       error can only ever flatter a style, and only by the bracketed stuck
       count printed beside it -- at most one run in a hundred on every row
       there except Normal steady. */
    if (!r.stuck && !r.pinned && r.deepest >= MAX_FLOOR) r.won = true;
    return r;
}

/* ---- reporting -------------------------------------------------------- */

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/* Averages of small counts, to two places. Printed as whole numbers, "laps
   per run" read 0 for a sample that had run three of them -- which is the
   same sentence as "the cycle never fired" and meant something else
   entirely. Anything that can legitimately average below one gets this. */
static void print_avg(const char *label, long total, int runs) {
    long hundredths = runs > 0 ? (total * 100 + runs / 2) / runs : 0;
    printf("%s%ld.%02ld", label, hundredths / 100, hundredths % 100);
}

static void run_mode(int difficulty, const char *label, int runs) {
    int *deep = calloc((size_t)runs, sizeof(int));
    int wins = 0, stuck = 0, pinned = 0, worst_crowd = 0, worst_crowd_floor = 0;
    long income_sum = 0; int rush_sum = 0;
    int band[11] = {0};                 /* deaths per 10-floor band */
    int early[11] = {0};                /* deaths on floors 1..10, one each */
    int lvl_sum = 0, lvl_n = 0;
    long turn_sum = 0, end_lvl_sum = 0;
    long comfort_sum = 0; int comfort_n = 0;
    long end_sum[END_COUNT] = {0};
    long dda_sum = 0, dda_peak_sum = 0, dda_trough_sum = 0;
    char first_killer[48] = "";
    int best = 0;
    long cycles_sum = 0, sold_sum = 0, sold_gold_sum = 0, attrs_sum = 0, heroes_sum = 0;
    long recall_sum = 0;
    long sale_mc = 0, sale_hire = 0; long sale_n = 0;
    int out_of_time = 0, loop_dry = 0;

    for (int i = 0; i < runs; i++) {
        /* Spread the classes so one lucky archetype cannot carry a mode. */
        int class_id = (i * 7 + difficulty * 13) % NUM_CLASSES;
        RunResult r = simulate_run(difficulty, class_id, (unsigned int)(1000 + i * 37));
        deep[i] = r.deepest;
        if (r.won) wins++;
        if (r.stuck) stuck++;
        if (r.pinned) pinned++;
        if (r.deepest > best) best = r.deepest;
        if (r.worst_crowd > worst_crowd) { worst_crowd = r.worst_crowd; worst_crowd_floor = r.worst_crowd_floor; }
        income_sum += r.gold_earned; rush_sum += r.rush_floors;
        turn_sum += r.turns; end_lvl_sum += r.level;
        if (r.hp_pct_n) { comfort_sum += r.hp_pct_sum / r.hp_pct_n; comfort_n++; }
        for (int e = 0; e < END_COUNT; e++) end_sum[e] += r.ended[e];
        dda_sum += r.dda_final; dda_peak_sum += r.dda_peak; dda_trough_sum += r.dda_trough;
        recall_sum += r.recalls;
        sale_mc += r.sale_mc_level_sum; sale_hire += r.sale_hire_level_sum;
        sale_n  += r.sale_samples;
        cycles_sum += r.prestige_cycles; sold_sum += r.levels_sold;
        sold_gold_sum += r.level_gold;   attrs_sum += r.attrs_bought;
        heroes_sum += r.heroes;
        if (r.out_of_time) out_of_time++;
        if (r.loop_dry) loop_dry++;
        /* Deaths only. A run stopped by the clock did not die on floor 0, and
           counting it as one would put every prestige run that ran long into
           the 1-10 band and drag "avg level at death" down to whatever it
           happened to be holding at the time. */
        if (!r.won && !r.stuck && !r.pinned && !r.out_of_time && !r.loop_dry) {
            int b = r.killed_on_floor / 10;
            if (b > 10) b = 10;
            band[b]++;
            if (r.killed_on_floor >= 1 && r.killed_on_floor <= 10) early[r.killed_on_floor]++;
            lvl_sum += r.level; lvl_n++;
            if (!first_killer[0]) snprintf(first_killer, sizeof(first_killer), "%s", r.killer);
        }
    }

    qsort(deep, (size_t)runs, sizeof(int), cmp_int);
    int median = deep[runs / 2];
    int p90 = deep[(runs * 9) / 10];

    printf("  %-8s %-6s  win %3d%%   median %3d   best %3d   90th %3d   "
           "worst crowd %d (fl %d)",
           label, STYLE_NAME[g_style], wins * 100 / runs, median, best, p90,
           worst_crowd, worst_crowd_floor);
    if (stuck) printf("   [%d wedged]", stuck);
    if (pinned) printf("   [%d pinned]", pinned);
    printf("\n              deaths by depth: ");
    for (int b = 0; b < 10; b++) printf("%d-%d:%-3d ", b * 10 + 1, b * 10 + 10, band[b]);
    printf("\n              of those, floors 1-10: ");
    for (int f = 1; f <= 10; f++) printf("%d:%-2d ", f, early[f]);
    if (lvl_n) printf("  avg level at death %d", lvl_sum / lvl_n);
    printf("\n              gold found per run: %ld   gold-rush floors seen: %d\n",
           income_sum / runs, rush_sum / (runs ? runs : 1));
    /* The price of the power fantasy, in the only currency the player
       actually spends. A style that wins more must show up here as costing
       more, or the extra wins came from somewhere unearned. */
    /* Recalls are on every row, not just prestige's. The charm branch is
       shared policy, and until it was printed there was no way to tell a
       style that never needed to go home from one whose trigger could not
       fire -- which is what prestige turned out to be doing. */
    printf("              turns per run: %ld   final level: %ld   "
           "health between floors: %ld%%",
           turn_sum / runs, end_lvl_sum / runs,
           comfort_n ? comfort_sum / comfort_n : 0);
    print_avg("   recalls: ", recall_sum, runs);
    printf("\n");
    if (style_farms()) {
        long tot = 0;
        for (int e = 0; e < END_COUNT; e++) tot += end_sum[e];
        printf("              floors left because:");
        for (int e = 0; e < END_COUNT; e++)
            if (end_sum[e]) printf("  %s %ld%%", END_NAME[e], tot ? end_sum[e] * 100 / tot : 0);
        printf("   (budget high = the rule is not deciding)\n");
    }
    /* Did the loop this row is named after actually run? A prestige row with
       zero cycles is a farm row, and "the loop does nothing" and "the loop
       never happened" are opposite findings that the win rate alone cannot
       tell apart. Same for the clock: if most runs end on it, the ceiling is
       the result and the win rate under it is not one. */
    if (g_style == PLAY_PRESTIGE) {
        printf("             ");
        print_avg(" prestige laps per run: ", cycles_sum, runs);
        print_avg("   levels sold: ", sold_sum, runs);
        printf(" (%ld gold)", sold_gold_sum / runs);
        print_avg("   attribute points: ", attrs_sum, runs);
        print_avg("   heroes: ", heroes_sum, runs);
        if (sale_n > 0)
            printf("\n              after a cash-out: character at level %ld, best hire at %ld"
                   "   (a player with Tab drives the bigger number)",
                   sale_mc / sale_n, sale_hire / sale_n);
        printf("\n              runs that stopped because the laps stopped paying: %d%%"
               "   on the clock instead: %d%%"
               "   (clock high = the ceiling is the finding, not the win rate)\n",
               loop_dry * 100 / runs, out_of_time * 100 / runs);
    }
    /* Phase one: what DDA *would* have seen. Nothing acts on it yet. */
    printf("              dda pressure: final %ld  peak %ld  trough %ld  (bounds %d..%d)\n",
           dda_sum / runs, dda_peak_sum / runs, dda_trough_sum / runs, DDA_MIN, DDA_MAX);
    free(deep);
}

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 40;
    int only = argc > 2 ? atoi(argv[2]) : -1;
    if (argc > 3) g_start_gold = atoi(argv[3]);
    if (argc > 4) g_hire_party = atoi(argv[4]) != 0;
    if (argc > 5) g_escalation_cap = atoi(argv[5]);
    if (argc > 6) g_use_bank = atoi(argv[6]) != 0;
    /* argv[8]: one playstyle, or -1 (the default) to run the whole matrix. */
    int only_style = (argc > 8) ? atoi(argv[8]) : -1;
    /* argv[9]: how many unpaying laps end a prestige run. On the command line
       so it can be swept -- see g_prestige_patience. */
    if (argc > 9) g_prestige_patience = atoi(argv[9]);
    if (g_prestige_patience < 1) g_prestige_patience = 1;
    /* argv[10]: floors between town stops, overriding the game's own rule.
       A control for separating floor size from waygate frequency -- see
       town_stop_interval(). */
    if (argc > 10) g_waygate_override = atoi(argv[10]);
    /* argv[11]: guardian angel strength, 0..100 (dda.h). 0 is the control --
       the same runs with the thumb off the scale. Every claim about the angel
       has to be a difference between two of these, not a single number. */
    if (argc > 11) guardian_set_strength(atoi(argv[11]));
    /* argv[12]: the Swarm/Hardcore experience divisor (combat.h). 10 ships;
       1 removes the penalty. The one constant every Hardcore measurement so
       far has pointed at. */
    if (argc > 12) xp_penalty_set_divisor(atoi(argv[12]));
    /* argv[7]: which world. Floor size changes monster counts, income, XP and
       walking distance all at once, so "is it winnable" has four answers now
       and the harness has to be told which one it is measuring. */
    int world = (argc > 7) ? atoi(argv[7]) : WORLD_SHAFT;
    if (world < 0 || world >= WORLD_SIZE_COUNT) world = WORLD_SHAFT;
    world_size_apply(world);
    if (getenv("AETHER_SIM_VERBOSE")) g_verbose = true;
    if (runs < 1) runs = 40;

    /* No terminal, and no sleeping in the animation primitives. */
    render_set_headless(true);
    if (runs == 1) g_verbose = true;

    static const char *const NAMES[DIFFICULTY_COUNT] = {
        "Normal", "Hard", "Swarm", "Hardcore"
    };

    printf("World: %s\n", world_size_name(world));
    printf("Winnability -- %d runs per mode per style, real mapgen/combat/monsters,\n", runs);
    printf("a competent player who shops at every waygate and retreats when hurt.\n");
    printf("rush = stairs on sight; steady = explore the floor first;\n");
    printf("farm  = stay until level 4+%dxfloor; overkill = 4+%dxfloor.\n",
           FARM_LEVEL_SLOPE, OVERKILL_LEVEL_SLOPE);
    printf("(a rusher reaches about 2.5xfloor on its own, so both are real targets.)\n");
    printf("prestige = farm's gate exactly, plus: recall home nearly dead, sell every\n");
    printf("level, buy shares/attributes/heroes with it, and start again at floor 1.\n");
    printf("Recall does not exist in Hardcore, so prestige and farm must match there.\n");
    if (g_start_gold != 60) printf("CONTROL RUN: starting purse %d gold.\n", g_start_gold);
    if (g_hire_party) printf("CONTROL RUN: a full party of five hired heroes.\n");
    if (g_escalation_cap >= 0)
        printf("CONTROL RUN: escalation capped at %d%% (game default is 100%%).\n", g_escalation_cap);
    printf("\n");

    for (int d = 0; d < DIFFICULTY_COUNT; d++) {
        if (only >= 0 && d != only) continue;
        for (int st = 0; st < PLAYSTYLE_COUNT; st++) {
            if (only_style >= 0 && st != only_style) continue;
            g_style = (Playstyle)st;
            run_mode(d, NAMES[d], runs);
        }
        printf("\n");
    }
    return 0;
}
