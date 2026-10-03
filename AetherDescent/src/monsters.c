#include "monsters.h"

typedef struct {
    char name[40];
    char glyph;
    int color_pair;
    int base_hp, base_atk, base_def;
    int base_xp, base_gold;
} MonsterTemplate;

/* Tier 1: floors 1-15, jungle vermin, tribal warbands and river pirates
   around the temple's outer roots */
static const MonsterTemplate TIER1[] = {
    {"Vine Rat",             'r', CP_MON_VERMIN, 11,  4, 0,  4, 2},
    {"Bog Leech",             'l', CP_MON_VERMIN, 10,  5, 0,  4, 2},
    {"Quill-Back Boar",       'b', CP_MON_VERMIN, 20,  7, 2,  7, 5},
    {"Canopy Adder",          's', CP_MON_VERMIN, 11,  8, 1,  6, 3},
    {"Tribal Skirmisher",     'a', CP_MON_VERMIN, 17,  9, 2,  7, 6},
    {"Tribal Shaman",         'm', CP_MON_VERMIN, 14, 10, 1,  8, 6},
    {"River Pirate",          'i', CP_MON_VERMIN, 16,  8, 2,  7, 8},
    {"Canopy Stalking Cat",   'f', CP_MON_VERMIN, 15,  9, 1,  7, 4},
};

/* Tier 2: floors 16-35, the Company's abandoned clockwork, deserters and
   scrap-clan raiders */
static const MonsterTemplate TIER2[] = {
    {"Rusted Automaton",   'A', CP_MON_CLOCKWORK, 30, 7, 4, 12, 10},
    {"Steam Hound",        'h', CP_MON_CLOCKWORK, 22, 9, 2, 10,  8},
    {"Company Enforcer",   'E', CP_MON_CLOCKWORK, 26, 8, 5, 13, 14},
    {"Pressure Wraith",    'w', CP_MON_CLOCKWORK, 18,11, 1, 11,  9},
    {"Company Deserter",   'd', CP_MON_CLOCKWORK, 28, 9, 4, 13, 16},
    {"Scrap Clan Raider",  'C', CP_MON_CLOCKWORK, 24,10, 3, 12, 12},
    {"Steam Pirate Boarder",'B',CP_MON_CLOCKWORK, 26, 9, 3, 13, 20},
    {"Boiler Wisp",        'e', CP_MON_CLOCKWORK, 16,11, 1, 11,  9},
};

/* Tier 3: floors 36-60, the deep ruins beneath the works, cultists and
   rival relic hunters */
static const MonsterTemplate TIER3[] = {
    {"Crystal Wraith",     'W', CP_MON_RUINS, 45,14, 6, 22, 20},
    {"Star-iron Golem",    'G', CP_MON_RUINS, 70,12,12, 28, 26},
    {"Vent Wisp",          'w', CP_MON_RUINS, 30,16, 3, 20, 16},
    {"Buried King's Echo", 'k', CP_MON_RUINS, 50,15, 8, 25, 22},
    {"Ruin Cultist",       'C', CP_MON_RUINS, 38,15, 5, 23, 18},
    {"Crystal-Touched Zealot",'z',CP_MON_RUINS,42,16, 6, 24, 20},
    {"Relic Hunter",       'u', CP_MON_RUINS, 36,14, 7, 22, 30},
    {"Deep Ruin Stalker",  'D', CP_MON_RUINS, 40,17, 4, 23, 16},
    {"Star-iron Sentinel", 'I', CP_MON_RUINS, 75,18,14, 30, 28},
    {"The Ward Unclaimed", 'P', CP_MON_RUINS, 55,20, 9, 32, 35},
    {"Builder's Remnant",  'B', CP_MON_RUINS, 90,16,16, 33, 30},
    {"Sealed-Door Wraith", 'V', CP_MON_RUINS, 48,22, 7, 28, 24},
    {"Record-Hall Keeper", 'K', CP_MON_RUINS, 65,19,11, 31, 32},
};

/* Tier 4: floors 61-85, outrider-touched ground -- salt nomads, cultists
   and wasteland raider bands */
static const MonsterTemplate TIER4[] = {
    {"Hollow Shade",       'S', CP_MON_OUTRIDER, 60,20, 8, 38, 34},
    {"Cold Patch",         'c', CP_MON_OUTRIDER, 40,24, 4, 34, 28},
    {"Silent Stalker",     't', CP_MON_OUTRIDER, 75,22,10, 42, 38},
    {"Salt-Broken Warden", 'x', CP_MON_OUTRIDER, 90,18,14, 44, 40},
    {"Salt Nomad",         'n', CP_MON_OUTRIDER, 55,21, 7, 36, 30},
    {"Outrider Cultist",   'u', CP_MON_OUTRIDER, 50,23, 6, 37, 28},
    {"Wasteland Raider",   'B', CP_MON_OUTRIDER, 58,22, 8, 38, 34},
    {"Ash-Choked Horror",  'F', CP_MON_OUTRIDER, 80,19,11, 40, 30},
    {"Salt-Iron Revenant", 'I', CP_MON_OUTRIDER, 95,25,12, 45, 36},
    {"The Unspoken Name",  'T', CP_MON_OUTRIDER, 70,27, 8, 46, 34},
    {"Djinn-Touched Miner",'M', CP_MON_OUTRIDER, 85,23,10, 42, 38},
    {"Ash-Wrapped Prospector",'P',CP_MON_OUTRIDER, 78,24, 9, 41, 40},
    {"Salt-Ring Breaker",  'K', CP_MON_OUTRIDER, 100,26,13, 48, 42},
};

/* Tier 5: floors 86-99, the abyssal approach to the Well -- zealots,
   drowned pirates and things that used to be maintenance crews */
static const MonsterTemplate TIER5[] = {
    {"Root Horror",         'R', CP_MON_ABYSSAL, 110,28,14, 60, 55},
    {"Ember Revenant",      'v', CP_MON_ABYSSAL,  90,34,10, 58, 52},
    {"Chained Warden",      '&', CP_MON_ABYSSAL, 150,26,20, 70, 65},
    {"Abyss Cult Zealot",   'z', CP_MON_ABYSSAL, 100,30,12, 62, 50},
    {"Drowned Pirate Captain",'C',CP_MON_ABYSSAL,130,32,16, 75, 90},
    {"Wellspring Wraith",   'W', CP_MON_ABYSSAL,  95,33,10, 60, 48},
    {"Cinder Wretch",       'g', CP_MON_ABYSSAL,  85,29, 9, 58, 45},
    {"Star-Forge Guardian", 'I', CP_MON_ABYSSAL, 160,35,22, 80, 70},
    {"The Herald's Shadow", 'J', CP_MON_ABYSSAL, 140,38,18, 85, 75},
    {"Nine-Hearth Wraith",  'N', CP_MON_ABYSSAL, 130,36,16, 78, 68},
    {"Outrider-Touched Colossus",'O',CP_MON_ABYSSAL,180,33,24, 82, 72},
    {"The Well's Last Custodian",'U',CP_MON_ABYSSAL,155,37,20, 88, 80},
};

/* --------------------------------------------------------------------
   Beyond the hand-authored roster above, each tier also fields a much
   larger pool of procedurally-named variants: a descriptor + a role,
   drawn from the same naming style as the curated set, with stats built
   from an archetype multiplier over that tier's average baseline. This
   is how ~40 hand-placed monsters becomes ~20x that many distinct types
   without hand-writing hundreds of individual stat rows.
   -------------------------------------------------------------------- */

static const char *T1_PREFIXES[] = {
    "Vine","Bog","Moss","Root","Thorn","Fang","Claw","River","Canopy","Swamp",
    "Feral","Wild","Mud","Reed","Leech","Howling","Prowling","Sunken"
};
static const char *T1_NOUNS[] = {
    "Rat","Adder","Boar","Stalker","Skirmisher","Shaman","Pirate","Cat",
    "Lurker","Raider","Wretch","Howler","Warband","Prowler","Biter"
};

static const char *T2_PREFIXES[] = {
    "Rusted","Steam","Company","Scrap","Boiler","Piston","Riveted","Pressure",
    "Coal","Clockwork","Soot","Iron","Brass","Corroded","Sparking","Leaking",
    "Grinding","Discarded"
};
static const char *T2_NOUNS[] = {
    "Automaton","Hound","Enforcer","Wraith","Deserter","Raider","Boarder",
    "Wisp","Sentry","Golem","Drone","Trooper","Guard","Hulk","Servitor"
};

static const char *T3_PREFIXES[] = {
    "Crystal","Star-iron","Buried","Ruin","Deep","Hollow","Ancient","Sunken",
    "Cracked","Silent","Glass","Echoing","Forgotten","Ashen","Drowned","Pale",
    "Ghostly","Shattered"
};
static const char *T3_NOUNS[] = {
    "Wraith","Golem","Wisp","Echo","Cultist","Zealot","Hunter","Stalker",
    "Guardian","Warden","Sentinel","Watcher","Remnant","Husk","Shade"
};

static const char *T4_PREFIXES[] = {
    "Salt","Hollow","Cold","Silent","Ash","Outrider","Wasteland","Bone",
    "Dust","Sand","Grey","Nomad","Broken","Withered","Cracked","Bleached",
    "Whispering","Forsaken"
};
static const char *T4_NOUNS[] = {
    "Shade","Patch","Stalker","Warden","Nomad","Cultist","Raider","Horror",
    "Wanderer","Reaver","Drifter","Husk","Watcher","Fiend","Marauder"
};

static const char *T5_PREFIXES[] = {
    "Root","Ember","Chained","Abyss","Drowned","Wellspring","Cinder","Void",
    "Black","Molten","Weeping","Screaming","Ashen","Ancient","Deep",
    "Hungering","Burning","Sunken"
};
static const char *T5_NOUNS[] = {
    "Horror","Revenant","Warden","Zealot","Captain","Wraith","Wretch",
    "Behemoth","Terror","Devourer","Specter","Colossus","Fiend","Herald",
    "Abomination"
};

static const char *const *const TIER_PREFIXES[5] = { T1_PREFIXES, T2_PREFIXES, T3_PREFIXES, T4_PREFIXES, T5_PREFIXES };
static const int TIER_PREFIX_COUNT[5] = {
    (int)(sizeof(T1_PREFIXES) / sizeof(T1_PREFIXES[0])),
    (int)(sizeof(T2_PREFIXES) / sizeof(T2_PREFIXES[0])),
    (int)(sizeof(T3_PREFIXES) / sizeof(T3_PREFIXES[0])),
    (int)(sizeof(T4_PREFIXES) / sizeof(T4_PREFIXES[0])),
    (int)(sizeof(T5_PREFIXES) / sizeof(T5_PREFIXES[0])),
};
static const char *const *const TIER_NOUNS[5] = { T1_NOUNS, T2_NOUNS, T3_NOUNS, T4_NOUNS, T5_NOUNS };
static const int TIER_NOUN_COUNT[5] = {
    (int)(sizeof(T1_NOUNS) / sizeof(T1_NOUNS[0])),
    (int)(sizeof(T2_NOUNS) / sizeof(T2_NOUNS[0])),
    (int)(sizeof(T3_NOUNS) / sizeof(T3_NOUNS[0])),
    (int)(sizeof(T4_NOUNS) / sizeof(T4_NOUNS[0])),
    (int)(sizeof(T5_NOUNS) / sizeof(T5_NOUNS[0])),
};

/* Average of each tier's curated base stats -- the seed generated variants
   scale from, so they land in the same range as the hand-placed ones. */
static const double TIER_BASE[5][5] = {
    /* hp,   atk,  def,  xp,   gold */
    {  14,   7,    1,    6,    4  }, /* jungle    */
    {  24,   9,    3,   12,   12  }, /* clockwork */
    {  44,  15,    6,   23,   21  }, /* ruins     */
    {  64,  21,    9,   39,   33  }, /* wastes    */
    { 109,  30,   13,   63,   58  }, /* abyss     */
};

typedef struct { double hp, atk, def, xp, gold; } Archetype;

static const Archetype ARCHETYPES[] = {
    {1.00, 1.00, 1.00, 1.00, 1.00}, /* Grunt        */
    {1.30, 1.15, 1.10, 1.15, 1.10}, /* Brute        */
    {0.85, 1.20, 0.80, 1.05, 1.00}, /* Skirmisher   */
    {0.80, 1.30, 0.70, 1.10, 1.05}, /* Ambusher     */
    {1.40, 0.85, 1.40, 1.10, 1.00}, /* Tank         */
    {0.70, 1.40, 0.60, 1.15, 1.10}, /* Glass Cannon */
    {1.15, 1.10, 1.15, 1.20, 1.20}, /* Veteran      */
    {0.60, 0.80, 0.60, 0.70, 0.60}, /* Swarmling    */
};
#define ARCHETYPE_COUNT ((int)(sizeof(ARCHETYPES) / sizeof(ARCHETYPES[0])))

#define GEN_PER_TIER 148
#define TIER_CAPACITY 200

static MonsterTemplate g_pool[5][TIER_CAPACITY];
static int g_pool_count[5];
static bool g_tables_built = false;

static void build_generated_tier(int t, const MonsterTemplate *curated, int curated_n) {
    int n = 0;
    for (int i = 0; i < curated_n && n < TIER_CAPACITY; i++) g_pool[t][n++] = curated[i];

    int np = TIER_PREFIX_COUNT[t];
    int nn = TIER_NOUN_COUNT[t];
    int total_combos = np * nn;
    int want = GEN_PER_TIER;
    if (want > total_combos) want = total_combos;
    if (n + want > TIER_CAPACITY) want = TIER_CAPACITY - n;

    for (int gi = 0; gi < want; gi++) {
        int combo = gi % total_combos;
        int pi = combo % np;
        int ni = combo / np;

        MonsterTemplate mt;
        memset(&mt, 0, sizeof(mt));
        snprintf(mt.name, sizeof(mt.name), "%s %s", TIER_PREFIXES[t][pi], TIER_NOUNS[t][ni]);
        mt.glyph = curated[gi % curated_n].glyph;
        mt.color_pair = curated[0].color_pair;

        const Archetype *a = &ARCHETYPES[gi % ARCHETYPE_COUNT];
        const double *base = TIER_BASE[t];
        mt.base_hp   = (int)(base[0] * a->hp);
        mt.base_atk  = (int)(base[1] * a->atk);
        mt.base_def  = (int)(base[2] * a->def);
        mt.base_xp   = (int)(base[3] * a->xp);
        mt.base_gold = (int)(base[4] * a->gold);
        if (mt.base_hp < 1) mt.base_hp = 1;
        if (mt.base_atk < 1) mt.base_atk = 1;

        g_pool[t][n++] = mt;
    }

    g_pool_count[t] = n;
}

static void ensure_tables_built(void) {
    if (g_tables_built) return;
    g_tables_built = true;
    build_generated_tier(0, TIER1, (int)(sizeof(TIER1) / sizeof(TIER1[0])));
    build_generated_tier(1, TIER2, (int)(sizeof(TIER2) / sizeof(TIER2[0])));
    build_generated_tier(2, TIER3, (int)(sizeof(TIER3) / sizeof(TIER3[0])));
    build_generated_tier(3, TIER4, (int)(sizeof(TIER4) / sizeof(TIER4[0])));
    build_generated_tier(4, TIER5, (int)(sizeof(TIER5) / sizeof(TIER5[0])));
}

static void get_tier(int floor_num, const MonsterTemplate **arr, int *count) {
    ensure_tables_built();
    int t;
    if (floor_num <= 15)       t = 0;
    else if (floor_num <= 35)  t = 1;
    else if (floor_num <= 60)  t = 2;
    else if (floor_num <= 85)  t = 3;
    else                       t = 4;
    *arr = g_pool[t];
    *count = g_pool_count[t];
}

/* Percentage added to monster HP and attack, from how long the run has been
   going. Module state rather than a parameter because every construction site
   -- floor generation, respawns, elites, bosses -- would otherwise have to
   thread it through, and they all want the same value. */
static int g_escalation_pct = 0;

/* Tenths of a percent, so the level term can be finer than 1% a level. */
#define ESCALATION_PER_LEVEL 10  /* +1.0% per player level */
#define ESCALATION_PER_ENTRY  5  /* +0.5% per floor entered */
/* CHANGE B (EVALUATION §58): 100 -> 25.
 *
   The level term is what made grinding self-defeating -- every level gained
   raised every monster's attack, permanently, whether you were steamrolling
   or barely alive. Capping it low keeps the "levelling alone should not
   trivialise depth" intent while letting depth carry the difficulty, which is
   what an RPG wants and what DDA (dda.c) is meant to replace it with. */
#define ESCALATION_CAP       25  /* percent -- at most a quarter again */

/* The player's level is the main input, because the player's level is the
   thing that made the game easy: power grows faster than any depth curve, so
   scaling off depth alone always loses the race eventually. Floor entries add
   a smaller term so a run that wanders back and forth still tightens.

   Modelled before it shipped, on two axes:
     - deep floors are dangerous:   a mid-play character goes from 11 ordinary
       hits of survival at floor 99 to 4;
     - levelling still pays:        at a fixed floor, survivability still
       rises with level (roughly 3 -> 13 hits from level 20 to 120). Scaling
       monsters 1:1 with the player would have made every level-up worthless,
       which is the usual way this mechanic goes wrong. tests/balance.c pins
       that second property. */
int monsters_escalation_for(int level, int floor_entries) {
    if (level < 1) level = 1;
    if (floor_entries < 0) floor_entries = 0;
    int pct = (level * ESCALATION_PER_LEVEL + floor_entries * ESCALATION_PER_ENTRY) / 10;
    return pct > ESCALATION_CAP ? ESCALATION_CAP : pct;
}

/* A swarm monster is a fraction of a monster, and the *same* fraction the
   experience already pays -- one constant, one claim, so the two can never
   drift into "a tenth of the reward for a whole monster" again. See
   grant_xp() in combat.c, which divides by this. */
int monsters_swarm_fraction(void) { return SWARM_MONSTER_FRACTION; }

/* Attack is deliberately a gentler cut than hit points, and this is the one
   number here that is not derived.
 *
   Only eight things can reach you at once however many are on the floor, so
   an attack cut in the same proportion as hit points would make a horde
   *safer* than a single Normal monster -- eight attackers at a tenth each is
   four fifths of one ordinary blow. At a third, a fully surrounded player
   takes roughly two and a half times what one Normal monster deals, which is
   the shape the mode is after: individually trivial, collectively lethal,
   and survivable if you avoid being flanked. */
#define SWARM_ATTACK_PCT 35

static int g_difficulty = DIFFICULTY_NORMAL;

void monsters_set_difficulty(int difficulty) {
    g_difficulty = (difficulty >= 0 && difficulty < DIFFICULTY_COUNT)
                 ? difficulty : DIFFICULTY_NORMAL;
}

static bool swarm_mode(void) {
    return g_difficulty == DIFFICULTY_SWARM || g_difficulty == DIFFICULTY_HARDCORE;
}

void monsters_set_escalation(int pct) {
    if (pct < 0) pct = 0;
    if (pct > ESCALATION_CAP) pct = ESCALATION_CAP;
    g_escalation_pct = pct;
}

static Monster instantiate(const MonsterTemplate *t, int floor_num, int x, int y, double mult_hp, double mult_atk, double mult_def, double mult_reward, const char *name_prefix) {
    Monster mo;
    memset(&mo, 0, sizeof(mo));

    if (name_prefix && name_prefix[0]) {
        snprintf(mo.name, sizeof(mo.name), "%s%s", name_prefix, t->name);
    } else {
        strncpy(mo.name, t->name, sizeof(mo.name) - 1);
    }

    mo.x = x;
    mo.y = y;
    mo.glyph = t->glyph;
    mo.color_pair = t->color_pair;

    /* Attack tracks depth 1:1. The player gains roughly 1.5 levels per
       floor -- +12 max HP and +1.5 attack each -- while `floor_num / 3`
       gave monsters barely a third of an attack point per floor. Over 99
       floors that gap is what let a geared character wade through the
       endgame untouched. Matching attack to floor keeps floor 1 almost
       unchanged (base + 1) and only diverges where the player's own
       growth has already run away. */
    int hp = t->base_hp + floor_num * 3 + (rand() % (floor_num / 2 + 2));
    /* Attack tracks depth 1:1 plus an accelerating term. Linear alone lost
       the race: player defence outgrows it, so by the deep floors an
       ordinary hit cost ~5% of the bar and no fight was dangerous. The
       quadratic term is worth nothing at floor 1 (where the game is already
       hardest) and roughly doubles incoming damage at the bottom. */
    /* CHANGE A (EVALUATION §58): the quadratic depth term is gone.
     *
       Survivability is HP x (atk + def) / atk^2 -- your money buys the
       numerator linearly, so an attack curve that grows with the square of
       depth needs gear growing with the square of that again. The floor^2/140
       term roughly doubled floor-100 attack on its own, and every gold piece
       of the ~200M requirement was paying for it. */
    int atk = t->base_atk + floor_num;
    int def = t->base_def + floor_num / 5;
    /* Rewards climb with the square of the depth, not just with the depth.
     *
       Flat-ish linear growth meant a floor-1 kill paid 7 gold and 6 xp while
       a floor-100 kill paid 67 and 56 -- under ten times, for a hundred
       floors of difference. That made the shallows a perfectly good place to
       farm anything measured per *kill*, which is most things, and it is why
       levelling on the first floor and cashing out was strictly better than
       descending. The quadratic term barely registers in the shallows and
       dominates by the Abyss, which is the shape the depth is supposed to
       have. */
    int xp = t->base_xp + floor_num / 2 + floor_num * floor_num / 60;
    int gold = t->base_gold + floor_num / 2 + floor_num * floor_num / 50
             + (rand() % (floor_num / 3 + 2));

    /* Escalation lands on attack only. Not HP or defence: those make fights
       longer rather than riskier, which is the tedious kind of hard. Not the
       rewards either, or a long run would pay for its own difficulty. */
    mo.maxhp = (int)(hp * mult_hp);
    mo.hp = mo.maxhp;
    mo.atk = (int)(atk * mult_atk) * (100 + g_escalation_pct) / 100;
    mo.def = (int)(def * mult_def);

    /* The horde, made of horde. Applied after escalation so a swarm monster
       is a fraction of whatever the curve currently says a monster is, rather
       than a fraction of the base that then gets escalated back up. */
    if (swarm_mode()) {
        mo.maxhp /= tunable("AETHER_SWARM_FRAC", SWARM_MONSTER_FRACTION);
        mo.atk    = mo.atk * tunable("AETHER_SWARM_ATK", SWARM_ATTACK_PCT) / 100;
        mo.def   /= 2;          /* soft as well as frail -- you carve through */
        if (mo.maxhp < 1) mo.maxhp = 1;
        if (mo.atk   < 1) mo.atk   = 1;
        if (mo.def   < 0) mo.def   = 0;
        mo.hp = mo.maxhp;
    }
    mo.xp_reward = (int)(xp * mult_reward);
    mo.gold_reward = (int)(gold * mult_reward);
    mo.alive = true;
    mo.aggro = false;
    mo.is_boss = false;
    mo.is_elite = false;

    return mo;
}

Monster make_monster_for_floor(int floor_num, int x, int y) {
    const MonsterTemplate *arr;
    int count;
    get_tier(floor_num, &arr, &count);
    const MonsterTemplate *t = &arr[rand() % count];
    return instantiate(t, floor_num, x, y, 1.0, 1.0, 1.0, 1.0, NULL);
}

const char *pick_quest_monster_name(int floor_num) {
    const MonsterTemplate *arr;
    int count;
    get_tier(floor_num, &arr, &count);
    return arr[rand() % count].name;
}

Monster make_elite_for_floor(int floor_num, int x, int y) {
    const MonsterTemplate *arr;
    int count;
    get_tier(floor_num, &arr, &count);
    const MonsterTemplate *t = &arr[rand() % count];
    Monster mo = instantiate(t, floor_num, x, y, 1.8, 1.3, 1.3, 2.5, "Elder ");
    mo.is_elite = true;
    return mo;
}

Monster make_boss(int x, int y) {
    Monster mo;
    memset(&mo, 0, sizeof(mo));
    strncpy(mo.name, "Warden of the Deep Well", sizeof(mo.name) - 1);
    mo.x = x;
    mo.y = y;
    mo.glyph = '&';
    mo.color_pair = CP_MON_BOSS;
    /* Deliberately clears the floor-85 biome boss's stats by a wide margin
       -- it's the last fight in the game and must never feel like a step
       down from what came before it. */
    mo.maxhp = 1100;
    mo.hp = mo.maxhp;
    mo.atk = 100;
    mo.def = 40;
    mo.xp_reward = 800;
    mo.gold_reward = 1000;
    mo.alive = true;
    mo.aggro = true;
    mo.is_boss = true;
    mo.is_elite = false;
    return mo;
}

bool is_biome_boss_floor(int floor_num) {
    return floor_num == 15 || floor_num == 35 || floor_num == 60 || floor_num == 85;
}

Monster make_biome_boss(int floor_num, int x, int y) {
    Monster mo;
    memset(&mo, 0, sizeof(mo));
    mo.x = x;
    mo.y = y;
    mo.alive = true;
    mo.aggro = true;
    mo.is_boss = false; /* only the floor-100 Warden ends the game */
    mo.is_elite = true; /* bold rendering, same as a regular elite */

    /* Scaled by floor_num rather than hand-picked per boss -- a naturally
       leveled character is already well past a fixed early number by the
       time they reach it (a level-20-ish character by floor 15 isn't rare),
       so a boss whose stats don't track that growth dies in a couple of
       hits and can't land one back. This keeps each boss a real step up
       from the floor_num-scaled regular monsters around it, not a fixed
       snapshot that only worked the day it was written. */
    int hp = 220 + floor_num * 8;
    int atk = 16 + floor_num * 9 / 10;
    int def = 6 + floor_num * 7 / 20;

    switch (floor_num) {
        case 15:
            strncpy(mo.name, "Root-Drowned Matriarch", sizeof(mo.name) - 1);
            mo.glyph = '&'; mo.color_pair = CP_MON_VERMIN;
            mo.xp_reward = 90; mo.gold_reward = 120;
            break;
        case 35:
            strncpy(mo.name, "Foreman's Engine", sizeof(mo.name) - 1);
            mo.glyph = '&'; mo.color_pair = CP_MON_CLOCKWORK;
            mo.xp_reward = 200; mo.gold_reward = 260;
            break;
        case 60:
            strncpy(mo.name, "Flooded Custodian", sizeof(mo.name) - 1);
            mo.glyph = '&'; mo.color_pair = CP_MON_RUINS;
            mo.xp_reward = 350; mo.gold_reward = 420;
            break;
        case 85:
        default:
            strncpy(mo.name, "Salt-Cured Colossus", sizeof(mo.name) - 1);
            mo.glyph = '&'; mo.color_pair = CP_MON_OUTRIDER;
            mo.xp_reward = 500; mo.gold_reward = 600;
            break;
    }
    mo.maxhp = hp;
    mo.atk = atk;
    mo.def = def;
    mo.hp = mo.maxhp;
    return mo;
}

void compact_dead_monsters(Map *m) {
    int live = 0;
    for (int i = 0; i < m->monster_count; i++) {
        if (!m->monsters[i].alive) continue;
        if (live != i) m->monsters[live] = m->monsters[i];
        live++;
    }
    m->monster_count = live;
}

/* Both of these are turn counts, and turns-to-cross-a-floor scales with the
   map. Left flat, a 280x160 floor gets four times the respawn waves and four
   times the doublings for the same walk -- measured, that took a Normal
   floor 1 from 362 monsters at generation to 7,983 live, which is the array
   cap, not a difficulty setting. */
#define RESPAWN_INTERVAL SCALE_BY_AREA(25)
#define RESPAWN_MIN_DIST 15

/* Every this many turns spent on one floor, whatever is still alive on it
   doubles. */
/* Raised from 1,000 after the district work (EVALUATION §53).
 *
   Doubling exists to punish *loitering*, and it only does that if crossing a
   floor normally takes less than one interval. Districts pushed the time to
   find the stairs from ~800 turns to ~1,057 (worst 1,675) at 140x80 -- they
   block sightlines and lengthen routes -- so a 1,000-turn interval had
   quietly started firing during ordinary exploration. A floor you are
   *walking* should not double under you; a floor you are camping should. */
#define DOUBLING_INTERVAL SCALE_BY_AREA(2500)
/* ...but a floor that has been cleared out has nothing to double, and the
   point is that lingering is dangerous, not that clearing makes you safe. So
   a doubling never delivers fewer than this. */
#define DOUBLING_MIN_WAVE 8

/* Places `want` monsters at a distance from the player, on open floor.
   Returns how many it managed -- a crowded or cramped floor may fit fewer,
   and that is fine; the alternative is spinning forever looking for room. */
static int spawn_wave(Map *m, const Player *p, int want) {
    int placed = 0;
    for (int tries = 0; tries < want * 25 && placed < want; tries++) {
        if (m->monster_count >= MAX_MONSTERS - 4) break;
        int x = rand() % MAP_W;
        int y = rand() % MAP_H;
        if (m->tiles[y][x].type != TILE_FLOOR) continue;
        if (in_haven(m, x, y)) continue;   /* the camp holds, or it is not one */
        int dx = x - hero_driven_c(p)->x, dy = y - hero_driven_c(p)->y;
        if (dx * dx + dy * dy < RESPAWN_MIN_DIST * RESPAWN_MIN_DIST) continue;
        if (monster_at(m, x, y)) continue;
        m->monsters[m->monster_count++] = make_monster_for_floor(m->floor_num, x, y);
        placed++;
    }
    return placed;
}

/* Standing still on a floor should cost something. Every DOUBLING_INTERVAL
   turns in one place, the population doubles -- so a floor you are picking
   over slowly becomes a floor you have to leave, and no two visits to the
   same depth feel the same. Applies on every difficulty; on Swarm and
   Hardcore the array cap simply bites immediately, which is the right answer
   for modes that already start saturated. */
static void maybe_double_monsters(Map *m, const Player *p) {
    if (m->turns_on_floor <= 0) return;
    if (m->turns_on_floor % DOUBLING_INTERVAL != 0) return;
    if (m->monster_count >= MAX_MONSTERS - 4) return;

    int live = 0;
    for (int i = 0; i < m->monster_count; i++)
        if (m->monsters[i].alive) live++;

    int want = live;                       /* doubling = add as many again */
    if (want < DOUBLING_MIN_WAVE) want = DOUBLING_MIN_WAVE;

    int room = MAX_MONSTERS - 4 - m->monster_count;
    if (want > room) want = room;
    if (want <= 0) return;

    int placed = spawn_wave(m, p, want);
    if (placed > 0) {
        log_msg("The floor answers your loitering. There are twice as many now.");
    }
}

int monster_doubling_interval(void) { return DOUBLING_INTERVAL; }
int monster_respawn_interval(void) { return RESPAWN_INTERVAL; }

void maybe_respawn_monsters(Map *m, const Player *p) {
    if (m->floor_num <= 0) return; /* town never spawns monsters */

    m->turns_on_floor++;
    maybe_double_monsters(m, p);

    /* The infestation.
     *
       Swarm and Hardcore used to return here -- no respawns at all, on the
       reasoning that a floor generated at 10-20x density is saturated enough
       already and topping it up would only flood the array. The effect was
       that the two horde modes were the *only* ones where clearing a room
       cleared it permanently: Normal and Hard got the slow trickle, and the
       infestation did not spread. That is backwards. A horde you can grind
       down to nothing is a stockpile, not an infestation.

       They refill toward the density the floor was *generated* at rather than
       adding a flat wave, which is what keeps it an infestation instead of an
       avalanche: clearing never sticks, and the array cannot run away either,
       because the target is a level and not a rate. Kill faster than it
       refills and you still make progress -- you just cannot bank it. */
    bool infested = (p->difficulty == DIFFICULTY_SWARM
                  || p->difficulty == DIFFICULTY_HARDCORE);

    int interval = m->overrun ? RESPAWN_INTERVAL / 3 : RESPAWN_INTERVAL;
    if (infested) interval = RESPAWN_INTERVAL / tunable("AETHER_INFEST_RATE", 4);
    if (interval < 1) interval = 1;
    int per_wave = m->overrun ? 5 : 2;

    if (m->turns_on_floor % interval != 0) return;
    if (m->monster_count >= MAX_MONSTERS - 4) return;

    if (infested) {
        /* How full the floor is against how full it started. Counting the
           living rather than the array, so corpses awaiting compaction do not
           read as population. */
        int alive = 0;
        for (int i = 0; i < m->monster_count; i++) if (m->monsters[i].alive) alive++;
        if (m->infest_target <= 0) m->infest_target = alive;   /* first tick sets the level */
        if (alive >= m->infest_target) return;                 /* still saturated */
        /* Refill in proportion to the hole that has been made in it: a room
           cleared is answered quickly, a floor half-emptied is answered hard. */
        per_wave = (m->infest_target - alive) / 8;
        if (per_wave < 2) per_wave = 2;
        if (per_wave > 40) per_wave = 40;
    }

    int spawned = 0;
    for (int tries = 0; tries < 50 && spawned < per_wave; tries++) {
        int x = rand() % MAP_W;
        int y = rand() % MAP_H;
        if (m->tiles[y][x].type != TILE_FLOOR) continue;
        if (in_haven(m, x, y)) continue;   /* the camp holds, or it is not one */
        int dx = x - hero_driven_c(p)->x, dy = y - hero_driven_c(p)->y;
        if (dx * dx + dy * dy < RESPAWN_MIN_DIST * RESPAWN_MIN_DIST) continue;
        if (monster_at(m, x, y)) continue;

        Monster mo = make_monster_for_floor(m->floor_num, x, y);
        m->monsters[m->monster_count++] = mo;
        spawned++;
    }

    if (spawned > 0) {
        log_msg(infested ? "The floor is filling in behind you."
              : m->overrun ? "More of them. The floor keeps producing."
                           : "You hear something stir, back the way you came.");
    }
}
