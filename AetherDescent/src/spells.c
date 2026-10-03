#include "spells.h"
#include "combat.h"
#include "mapgen.h"
#include "render.h"

/* ---- the spell pool -------------------------------------------------
   Every spell in the game is hand-written below: 6 schools x 5 levels x 6
   spells = 180, each with its own name, effect and stat profile.

   This replaced a generator that crossed a bank of adjectives with a bank of
   nouns. It produced the right *number* of spells and none of the variety --
   six entries running would share a noun ("Aether Bolt, Volt Bolt, Arc
   Bolt"), and because effect and archetype were picked by counter rather
   than by intent, two spells with different names routinely did the identical
   thing. A curated table costs 180 lines and buys a pool where every entry
   was chosen.

   Stats still come from the level tables below multiplied by an archetype,
   so the balance curve is one place rather than 180 -- but each spell now
   names its own archetype, which is what makes two same-level spells with
   the same effect feel different: one hits harder and recharges slowly, the
   other is cheap and quick. */

typedef enum {
    ARCH_BALANCED = 0,
    ARCH_OVERCHARGED,   /* hits harder, costs more */
    ARCH_EFFICIENT,     /* cheaper and faster, weaker */
    ARCH_HEAVY,         /* strong but slow to recharge */
    ARCH_FRUGAL         /* very cheap, modest power */
} SpellArch;

typedef struct { const char *name; int level; int effect; int arch; } CuratedSpell;

/* Six per level, laid out level 1 -> 5. The guild browses in table order, so
   authoring in level order is also what makes the list read as a
   progression -- there is no separate sort to keep in step. */
#define SPELLS_PER_LEVEL 6
#define CURATED_PER_SCHOOL (5 * SPELLS_PER_LEVEL)

/* ---- Conduit: raw aether as a weapon ------------------------------- */
static const CuratedSpell CONDUIT_CURATED[CURATED_PER_SCHOOL] = {
    {"Spark Jolt",                1, EFFECT_DAMAGE,      ARCH_EFFICIENT},
    {"Filament Lash",             1, EFFECT_DAMAGE,      ARCH_BALANCED},
    {"Static Bite",               1, EFFECT_CHAIN,       ARCH_FRUGAL},
    {"Kindling Flare",            1, EFFECT_DAMAGE_NOVA, ARCH_EFFICIENT},
    {"Brass Arc",                 1, EFFECT_DAMAGE,      ARCH_OVERCHARGED},
    {"Ember Wash",                1, EFFECT_SCORCH,      ARCH_FRUGAL},

    {"Piercing Arc",              2, EFFECT_DAMAGE,      ARCH_BALANCED},
    {"Coil Discharge",            2, EFFECT_DAMAGE,      ARCH_HEAVY},
    {"Voltaic Spear",             2, EFFECT_DAMAGE,      ARCH_OVERCHARGED},
    {"Scatterspark",              2, EFFECT_DAMAGE_NOVA, ARCH_EFFICIENT},
    {"Cinder Bloom",              2, EFFECT_SCORCH,      ARCH_BALANCED},
    {"Live Wire",                 2, EFFECT_CHAIN,       ARCH_FRUGAL},

    {"Detonating Flare",          3, EFFECT_DAMAGE_NOVA, ARCH_BALANCED},
    {"Thunderhead",               3, EFFECT_DAMAGE,      ARCH_HEAVY},
    {"Arc Cascade",               3, EFFECT_DAMAGE_NOVA, ARCH_EFFICIENT},
    {"Ruinous Lance",             3, EFFECT_DAMAGE,      ARCH_OVERCHARGED},
    {"Slagfall",                  3, EFFECT_SCORCH,      ARCH_HEAVY},
    {"Stormcoil",                 3, EFFECT_CHAIN,       ARCH_BALANCED},

    {"Scorched Ground",           4, EFFECT_SCORCH,      ARCH_BALANCED},
    {"Fulminating Spire",         4, EFFECT_DAMAGE,      ARCH_OVERCHARGED},
    {"Wrath of the Coil",         4, EFFECT_DAMAGE_NOVA, ARCH_HEAVY},
    {"Whitefire Lance",           4, EFFECT_DAMAGE,      ARCH_EFFICIENT},
    {"Furnace Bloom",             4, EFFECT_SCORCH,      ARCH_OVERCHARGED},
    {"Riftshock",                 4, EFFECT_CHAIN,       ARCH_HEAVY},

    {"The Devastator's Discharge",5, EFFECT_DAMAGE_NOVA, ARCH_BALANCED},
    {"Sunfall",                   5, EFFECT_DAMAGE,      ARCH_OVERCHARGED},
    {"Aetherfire Cataclysm",      5, EFFECT_DAMAGE_NOVA, ARCH_HEAVY},
    {"The Well's Own Lightning",  5, EFFECT_CHAIN,       ARCH_OVERCHARGED},
    {"Cauterise the Deep",        5, EFFECT_SCORCH,      ARCH_HEAVY},
    {"Last Ember",                5, EFFECT_DAMAGE,      ARCH_EFFICIENT},
};

/* ---- Resonance: amplification, vigour, sound ------------------------ */
static const CuratedSpell RESONANCE_CURATED[CURATED_PER_SCHOOL] = {
    {"Steady Hum",                1, EFFECT_BUFF_ATK,    ARCH_EFFICIENT},
    {"Quickened Pulse",           1, EFFECT_HASTE,       ARCH_FRUGAL},
    {"Knitting Note",             1, EFFECT_HEAL_SELF,   ARCH_EFFICIENT},
    {"First Cadence",             1, EFFECT_BUFF_ATK,    ARCH_BALANCED},
    {"Warm Thrum",                1, EFFECT_PARTY_HEAL,  ARCH_FRUGAL},
    {"Tuning Strike",             1, EFFECT_BUFF_ATK,    ARCH_OVERCHARGED},

    {"Harmonic Chorus",           2, EFFECT_PARTY_HEAL,  ARCH_BALANCED},
    {"Mending Refrain",           2, EFFECT_HEAL_SELF,   ARCH_BALANCED},
    {"Rising Octave",             2, EFFECT_BUFF_ATK,    ARCH_OVERCHARGED},
    {"Bright Accord",             2, EFFECT_HEAL_SELF,   ARCH_EFFICIENT},
    {"Ironsong",                  2, EFFECT_BUFF_ATK,    ARCH_HEAVY},
    {"Second Wind",               2, EFFECT_HASTE,       ARCH_FRUGAL},

    {"Vital Crescendo",           3, EFFECT_HEAL_SELF,   ARCH_BALANCED},
    {"Battle Fanfare",            3, EFFECT_BUFF_ATK,    ARCH_OVERCHARGED},
    {"Resonant Anthem",           3, EFFECT_BUFF_ATK,    ARCH_HEAVY},
    {"Sutured Chord",             3, EFFECT_PARTY_HEAL,  ARCH_EFFICIENT},
    {"Marching Cadence",          3, EFFECT_HASTE,       ARCH_BALANCED},
    {"Bloodwarm Overtone",        3, EFFECT_HEAL_SELF,   ARCH_OVERCHARGED},

    {"Amplified Requiem",         4, EFFECT_BUFF_ATK,    ARCH_HEAVY},
    {"The Surgeon's Refrain",     4, EFFECT_HEAL_SELF,   ARCH_OVERCHARGED},
    {"Thunder in the Chest",      4, EFFECT_BUFF_ATK,    ARCH_OVERCHARGED},
    {"Unbroken Accord",           4, EFFECT_PARTY_HEAL,  ARCH_HEAVY},
    {"Chorus of Brass",           4, EFFECT_HASTE,       ARCH_BALANCED},
    {"Rekindling",                4, EFFECT_HEAL_SELF,   ARCH_EFFICIENT},

    {"Apotheosis",                5, EFFECT_BUFF_ATK,    ARCH_OVERCHARGED},
    {"The Well's Own Song",       5, EFFECT_PARTY_HEAL,  ARCH_OVERCHARGED},
    {"Symphony of Ruin",          5, EFFECT_BUFF_ATK,    ARCH_HEAVY},
    {"Perfect Fifth",             5, EFFECT_HASTE,       ARCH_EFFICIENT},
    {"Flesh Remembers",           5, EFFECT_HEAL_SELF,   ARCH_HEAVY},
    {"Zenith",                    5, EFFECT_BUFF_ATK,    ARCH_BALANCED},
};

/* ---- Warding: brass, stone and refusal ------------------------------ */
static const CuratedSpell WARDING_CURATED[CURATED_PER_SCHOOL] = {
    {"Brass Guard",               1, EFFECT_WARD_SHIELD, ARCH_EFFICIENT},
    {"Riveted Skin",              1, EFFECT_WARD_SHIELD, ARCH_FRUGAL},
    {"Stopgap",                   1, EFFECT_BARRIER,     ARCH_EFFICIENT},
    {"Clean Air",                 1, EFFECT_PURGE,       ARCH_FRUGAL},
    {"Buckler Sign",              1, EFFECT_WARD_SHIELD, ARCH_BALANCED},
    {"Held Breath",               1, EFFECT_REFLECT,     ARCH_OVERCHARGED},

    {"Sealed Bulwark",            2, EFFECT_WARD_SHIELD, ARCH_BALANCED},
    {"Standing Stone",            2, EFFECT_BARRIER,     ARCH_BALANCED},
    {"Scouring Wind",             2, EFFECT_PURGE,       ARCH_EFFICIENT},
    {"Iron Shell",                2, EFFECT_WARD_SHIELD, ARCH_HEAVY},
    {"Tempered Skin",             2, EFFECT_REFLECT,     ARCH_FRUGAL},
    {"Wall of Refusal",           2, EFFECT_BARRIER,     ARCH_OVERCHARGED},

    {"Sanctified Aegis",          3, EFFECT_WARD_SHIELD, ARCH_BALANCED},
    {"Raise the Rampart",         3, EFFECT_BARRIER,     ARCH_HEAVY},
    {"Purging Gale",              3, EFFECT_PURGE,       ARCH_BALANCED},
    {"Unyielding Stance",         3, EFFECT_WARD_SHIELD, ARCH_HEAVY},
    {"Layered Bastion",           3, EFFECT_REFLECT,     ARCH_OVERCHARGED},
    {"Deadbolt",                  3, EFFECT_BARRIER,     ARCH_EFFICIENT},

    {"Redoubt of Brass",          4, EFFECT_WARD_SHIELD, ARCH_HEAVY},
    {"The Warden's Answer",       4, EFFECT_BARRIER,     ARCH_OVERCHARGED},
    {"Cleansing Fire",            4, EFFECT_PURGE,       ARCH_HEAVY},
    {"Immurement",                4, EFFECT_BARRIER,     ARCH_HEAVY},
    {"Adamant Shell",             4, EFFECT_REFLECT,     ARCH_OVERCHARGED},
    {"Held Ground",               4, EFFECT_WARD_SHIELD, ARCH_EFFICIENT},

    {"The Sealed Citadel",        5, EFFECT_WARD_SHIELD, ARCH_OVERCHARGED},
    {"Absolution",                5, EFFECT_PURGE,       ARCH_OVERCHARGED},
    {"Everward",                  5, EFFECT_WARD_SHIELD, ARCH_HEAVY},
    {"Stone Answers Stone",       5, EFFECT_BARRIER,     ARCH_HEAVY},
    {"Nothing Passes",            5, EFFECT_REFLECT,     ARCH_BALANCED},
    {"Sanctum of the Deep",       5, EFFECT_WARD_SHIELD, ARCH_EFFICIENT},
};

/* ---- Aether-Sense: seeing and moving through the Well ---------------- */
static const CuratedSpell AETHER_SENSE_CURATED[CURATED_PER_SCHOOL] = {
    {"Sidestep",                  1, EFFECT_BLINK,       ARCH_EFFICIENT},
    {"Keen Glimpse",              1, EFFECT_REVEAL,      ARCH_FRUGAL},
    {"Borrowed Footing",          1, EFFECT_BRIDGE,      ARCH_EFFICIENT},
    {"Hunch",                     1, EFFECT_SENSE_LIFE,  ARCH_EFFICIENT},
    {"Slip the Gap",              1, EFFECT_BLINK,       ARCH_FRUGAL},
    {"Thin Places",               1, EFFECT_REVEAL,      ARCH_BALANCED},

    {"Wandering Step",            2, EFFECT_BLINK,       ARCH_BALANCED},
    {"Farseeing",                 2, EFFECT_REVEAL,      ARCH_BALANCED},
    {"Aether Causeway",           2, EFFECT_BRIDGE,      ARCH_BALANCED},
    {"Drifting Passage",          2, EFFECT_BLINK,       ARCH_HEAVY},
    {"Clear Water",               2, EFFECT_REVEAL,      ARCH_EFFICIENT},
    {"Guiding Trace",             2, EFFECT_SENSE_LIFE,  ARCH_FRUGAL},

    {"Silent Passage",            3, EFFECT_BLINK,       ARCH_BALANCED},
    {"Survey the Deep",           3, EFFECT_REVEAL,      ARCH_HEAVY},
    {"Span the Drowned",          3, EFFECT_BRIDGE,      ARCH_HEAVY},
    {"Blink Corridor",            3, EFFECT_BLINK,       ARCH_OVERCHARGED},
    {"Whispering Vista",          3, EFFECT_SENSE_LIFE,  ARCH_BALANCED},
    {"Waypoint",                  3, EFFECT_BLINK,       ARCH_EFFICIENT},

    {"The Cartographer's Eye",    4, EFFECT_REVEAL,      ARCH_OVERCHARGED},
    {"Fleeting Corridor",         4, EFFECT_BLINK,       ARCH_HEAVY},
    {"Bridge of Held Breath",     4, EFFECT_BRIDGE,      ARCH_OVERCHARGED},
    {"Revelation",                4, EFFECT_REVEAL,      ARCH_HEAVY},
    {"Fold the Floor",            4, EFFECT_BLINK,       ARCH_OVERCHARGED},
    {"Deep Reckoning",            4, EFFECT_SENSE_LIFE,  ARCH_BALANCED},

    {"Worldsight",                5, EFFECT_REVEAL,      ARCH_OVERCHARGED},
    {"Farstride",                 5, EFFECT_BLINK,       ARCH_OVERCHARGED},
    {"The Well Unveiled",         5, EFFECT_REVEAL,      ARCH_HEAVY},
    {"Walk Between",              5, EFFECT_BLINK,       ARCH_HEAVY},
    {"Roads of Aether",           5, EFFECT_BRIDGE,      ARCH_OVERCHARGED},
    {"Nothing Hidden",            5, EFFECT_SENSE_LIFE,  ARCH_BALANCED},
};

/* ---- Scribing: rules written down and made binding ------------------ */
static const CuratedSpell SCRIBING_CURATED[CURATED_PER_SCHOOL] = {
    {"Stutter Mark",              1, EFFECT_STUN,        ARCH_EFFICIENT},
    {"Drag Sigil",                1, EFFECT_SLOW,        ARCH_FRUGAL},
    {"Picklock Glyph",            1, EFFECT_UNBIND,      ARCH_EFFICIENT},
    {"Halting Tick",              1, EFFECT_STUN,        ARCH_FRUGAL},
    {"Tangled Line",              1, EFFECT_SLOW,        ARCH_EFFICIENT},
    {"Small Clause",              1, EFFECT_MASS_SLOW,   ARCH_BALANCED},

    {"Binding Glyph",             2, EFFECT_STUN,        ARCH_BALANCED},
    {"Leaden Script",             2, EFFECT_MASS_SLOW,   ARCH_BALANCED},
    {"Silent Cipher",             2, EFFECT_STUN,        ARCH_EFFICIENT},
    {"Unwritten Lock",            2, EFFECT_UNBIND,      ARCH_BALANCED},
    {"Chained Cadence",           2, EFFECT_SLOW,        ARCH_HEAVY},
    {"Inked Fetter",              2, EFFECT_STUN,        ARCH_FRUGAL},

    {"Sealing Contract",          3, EFFECT_STUN,        ARCH_BALANCED},
    {"Ledger of Debts",           3, EFFECT_SLOW,        ARCH_HEAVY},
    {"Writ of Stillness",         3, EFFECT_STUN,        ARCH_HEAVY},
    {"Opened Clause",             3, EFFECT_UNBIND,      ARCH_EFFICIENT},
    {"Runic Snare",               3, EFFECT_MASS_SLOW,   ARCH_OVERCHARGED},
    {"Marked for Waiting",        3, EFFECT_STUN,        ARCH_EFFICIENT},

    {"Decree of Silence",         4, EFFECT_STUN,        ARCH_OVERCHARGED},
    {"The Long Covenant",         4, EFFECT_MASS_SLOW,   ARCH_OVERCHARGED},
    {"Unbind the Sealed",         4, EFFECT_UNBIND,      ARCH_HEAVY},
    {"Iron Clause",               4, EFFECT_STUN,        ARCH_HEAVY},
    {"Script of Arrest",          4, EFFECT_STUN,        ARCH_BALANCED},
    {"Ledgered Doom",             4, EFFECT_SLOW,        ARCH_HEAVY},

    {"The Warden's Codex",        5, EFFECT_STUN,        ARCH_OVERCHARGED},
    {"Anathema",                  5, EFFECT_SLOW,        ARCH_OVERCHARGED},
    {"Final Edict",               5, EFFECT_STUN,        ARCH_HEAVY},
    {"Nothing Written Holds",     5, EFFECT_UNBIND,      ARCH_OVERCHARGED},
    {"Testament of Stillness",    5, EFFECT_STUN,        ARCH_BALANCED},
    {"Canon of the Deep",         5, EFFECT_MASS_SLOW,   ARCH_HEAVY},
};

/* ---- Jinx: rot, drain and ill will ---------------------------------- */
static const CuratedSpell JINX_CURATED[CURATED_PER_SCHOOL] = {
    {"Sour Touch",                1, EFFECT_LIFE_DRAIN,  ARCH_EFFICIENT},
    {"Creeping Itch",             1, EFFECT_CURSE,       ARCH_FRUGAL},
    {"Grave Chill",               1, EFFECT_STUN,        ARCH_EFFICIENT},
    {"Thin the Blood",            1, EFFECT_LIFE_DRAIN,  ARCH_FRUGAL},
    {"Pale Ache",                 1, EFFECT_CURSE,       ARCH_BALANCED},
    {"First Rot",                 1, EFFECT_DOT_BURN,    ARCH_EFFICIENT},

    {"Withering Grasp",           2, EFFECT_LIFE_DRAIN,  ARCH_BALANCED},
    {"Black Hex",                 2, EFFECT_CURSE,       ARCH_BALANCED},
    {"Hollow Stare",              2, EFFECT_STUN,        ARCH_EFFICIENT},
    {"Rotting Wound",             2, EFFECT_DOT_BURN,    ARCH_BALANCED},
    {"Forsaken Nip",              2, EFFECT_CURSE,       ARCH_FRUGAL},
    {"Hungering Palm",            2, EFFECT_LIFE_DRAIN,  ARCH_HEAVY},

    {"Grave Blight",              3, EFFECT_DOT_BURN,    ARCH_BALANCED},
    {"Drink Deep",                3, EFFECT_LIFE_DRAIN,  ARCH_HEAVY},
    {"Shroud of Malaise",         3, EFFECT_CURSE,       ARCH_HEAVY},
    {"Silent Reaping",            3, EFFECT_STUN,        ARCH_HEAVY},
    {"Creeping Contagion",        3, EFFECT_DOT_BURN,    ARCH_OVERCHARGED},
    {"Hollowing Curse",           3, EFFECT_CURSE,       ARCH_EFFICIENT},

    {"The Hollowing",             4, EFFECT_LIFE_DRAIN,  ARCH_OVERCHARGED},
    {"Necrotic Bloom",            4, EFFECT_DOT_BURN,    ARCH_OVERCHARGED},
    {"Grave's Appetite",          4, EFFECT_LIFE_DRAIN,  ARCH_HEAVY},
    {"Pale Malediction",          4, EFFECT_CURSE,       ARCH_OVERCHARGED},
    {"Rot the Marrow",            4, EFFECT_DOT_BURN,    ARCH_HEAVY},
    {"Stillborn Hour",            4, EFFECT_STUN,        ARCH_OVERCHARGED},

    {"Undoing",                   5, EFFECT_DOT_BURN,    ARCH_OVERCHARGED},
    {"The Hungering Dark",        5, EFFECT_LIFE_DRAIN,  ARCH_OVERCHARGED},
    {"Oblivion",                  5, EFFECT_CURSE,       ARCH_HEAVY},
    {"Necrosis",                  5, EFFECT_DOT_BURN,    ARCH_HEAVY},
    {"All Things Rot",            5, EFFECT_CURSE,       ARCH_BALANCED},
    {"Doom of the Well",          5, EFFECT_DOT_BURN,    ARCH_OVERCHARGED},
};

static const CuratedSpell *const SCHOOL_CURATED[SCHOOL_COUNT] = {
    CONDUIT_CURATED, RESONANCE_CURATED, WARDING_CURATED,
    AETHER_SENSE_CURATED, SCRIBING_CURATED, JINX_CURATED
};

static const char *SCHOOL_NAMES[SCHOOL_COUNT] = {
    "Conduit", "Resonance", "Warding", "Aether-Sense", "Scribing", "Jinx"
};

static const int SCHOOL_COLOR_PAIRS[SCHOOL_COUNT] = {
    CP_SCHOOL_CONDUIT, CP_SCHOOL_RESONANCE, CP_SCHOOL_WARDING,
    CP_SCHOOL_AETHER_SENSE, CP_SCHOOL_SCRIBING, CP_SCHOOL_JINX
};

static const chtype SCHOOL_GLYPHS[SCHOOL_COUNT] = { '*', '~', '+', 'o', '%', '&' };

const char *school_name(int school) {
    if (school < 0 || school >= SCHOOL_COUNT) return "?";
    return SCHOOL_NAMES[school];
}

int school_color_pair(int school) {
    if (school < 0 || school >= SCHOOL_COUNT) return CP_MAGIC;
    return SCHOOL_COLOR_PAIRS[school];
}

chtype school_glyph(int school) {
    if (school < 0 || school >= SCHOOL_COUNT) return '*';
    return SCHOOL_GLYPHS[school];
}

/* ---- level scaling ---------------------------------------------------- */

/* index 0 unused; levels run 1-5 */
static const int LEVEL_MAGNITUDE[6]  = { 0, 10, 16, 24, 34, 48 };
static const int LEVEL_DURATION[6]   = { 0,  8, 10, 14, 18, 24 };
static const int LEVEL_CHARGE[6]     = { 0,  2,  3,  4,  5,  6 };
static const int LEVEL_COOLDOWN[6]   = { 0,  2,  3,  4,  5,  7 };
/* A spell is a permanent, reusable ability, so the top of the curve should
   read as a serious investment -- the best weapon in the armoury is 1000g,
   and level 3-5 spells used to undercut it. Roughly x2.5 a level now, which
   puts a level-5 spell well beyond any single piece of gear. */
static const int LEVEL_PRICE[6]      = { 0, 60,180,450,1100,2600 };
static const int LEVEL_REACH[6]      = { 0,  5,  6,  7,  8,  9 }; /* target-search range */
static const int LEVEL_AOE[6]        = { 0,  1,  2,  2,  3,  3 }; /* environment/nova radius */

typedef struct { double magnitude, cost, cooldown, price; } SpellArchetype;
/* Indexed by SpellArch -- keep the order in step with that enum. */
static const SpellArchetype SPELL_ARCHETYPES[] = {
    [ARCH_BALANCED]    = {1.00, 1.00, 1.00, 1.00},
    [ARCH_OVERCHARGED] = {1.35, 1.25, 1.15, 1.15},
    [ARCH_EFFICIENT]   = {0.75, 0.75, 0.70, 0.85},
    [ARCH_HEAVY]       = {1.15, 1.00, 1.40, 1.05},
    [ARCH_FRUGAL]      = {0.90, 0.60, 1.00, 0.90},
};
#define ARCHETYPE_COUNT ((int)(sizeof(SPELL_ARCHETYPES) / sizeof(SPELL_ARCHETYPES[0])))

/* ---- pool construction, same shape as monsters.c's tier pools -------- */

#define SCHOOL_POOL_CAPACITY 64

static SpellTemplate g_pool[SCHOOL_COUNT][SCHOOL_POOL_CAPACITY];
static int g_pool_count[SCHOOL_COUNT];
static bool g_tables_built = false;

static SpellTemplate make_template(const char *name, int school, int level, int effect, const SpellArchetype *a) {
    SpellTemplate t;
    memset(&t, 0, sizeof(t));
    strncpy(t.name, name, sizeof(t.name) - 1);
    t.school = school;
    t.level = level;
    t.effect = effect;
    t.magnitude    = (int)(LEVEL_MAGNITUDE[level] * a->magnitude);
    t.duration     = (int)(LEVEL_DURATION[level] * a->magnitude);
    t.charge_cost  = (int)(LEVEL_CHARGE[level] * a->cost);
    t.cooldown     = (int)(LEVEL_COOLDOWN[level] * a->cooldown);
    t.learn_price  = (int)(LEVEL_PRICE[level] * a->price);
    if (t.charge_cost < 1) t.charge_cost = 1;
    if (t.cooldown < 1) t.cooldown = 1;
    if (t.magnitude < 1) t.magnitude = 1;
    return t;
}

static void build_school_pool(int school) {
    const CuratedSpell *curated = SCHOOL_CURATED[school];
    int n = 0;
    for (int i = 0; i < CURATED_PER_SCHOOL && n < SCHOOL_POOL_CAPACITY; i++) {
        int arch = curated[i].arch;
        if (arch < 0 || arch >= ARCHETYPE_COUNT) arch = ARCH_BALANCED;
        g_pool[school][n++] = make_template(curated[i].name, school,
                                            curated[i].level, curated[i].effect,
                                            &SPELL_ARCHETYPES[arch]);
    }
    g_pool_count[school] = n;
}

static void ensure_spell_tables_built(void) {
    if (g_tables_built) return;
    g_tables_built = true;
    for (int s = 0; s < SCHOOL_COUNT; s++) build_school_pool(s);
}

int spell_pool_count(void) {
    ensure_spell_tables_built();
    int total = 0;
    for (int s = 0; s < SCHOOL_COUNT; s++) total += g_pool_count[s];
    return total;
}

const SpellTemplate *spell_pool_get(int idx) {
    ensure_spell_tables_built();
    if (idx < 0) return NULL;
    for (int s = 0; s < SCHOOL_COUNT; s++) {
        if (idx < g_pool_count[s]) return &g_pool[s][idx];
        idx -= g_pool_count[s];
    }
    return NULL;
}

/* A caster studies one school and only one school. Levels 1-4 used to be
   open to everybody, with only level 5 locked -- which in practice meant
   every character browsed the same 200-odd spells and the school a class
   was built around barely mattered. */
bool player_can_learn(const Hero *h, int idx) {
    const SpellTemplate *t = spell_pool_get(idx);
    if (!t) return false;
    return t->school == h->magic_school;
}

int spell_school_count(int school) {
    ensure_spell_tables_built();
    if (school < 0 || school >= SCHOOL_COUNT) return 0;
    return g_pool_count[school];
}

/* The pool is stored per school and flattened in school order, so a school's
   entries are one contiguous run -- this is the offset of that run. */
int spell_school_index(int school, int n) {
    ensure_spell_tables_built();
    if (school < 0 || school >= SCHOOL_COUNT) return -1;
    if (n < 0 || n >= g_pool_count[school]) return -1;

    int base = 0;
    for (int s = 0; s < school; s++) base += g_pool_count[s];
    return base + n;
}

/* ---- turn ticking ------------------------------------------------------ */

void tick_spells(Hero *h) {
    for (int i = 0; i < h->spell_count; i++) {
        if (h->spell_cd[i] > 0) h->spell_cd[i]--;
    }
    if (h->aether < h->aether_max) h->aether++;
}

/* ---- effect helpers ----------------------------------------------------- */

static bool try_blink(Hero *h, Map *m, int range) {
    for (int tries = 0; tries < 30; tries++) {
        int nx = h->x + (rand() % (range * 2 + 1) - range);
        int ny = h->y + (rand() % (range * 2 + 1) - range);
        if (nx == h->x && ny == h->y) continue;
        if (!is_walkable_player(m, nx, ny)) continue;
        if (monster_at(m, nx, ny)) continue;
        h->x = nx;
        h->y = ny;
        return true;
    }
    return false;
}

static void reveal_around(Map *m, int cx, int cy, int radius) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= radius * radius) m->tiles[y][x].seen = true;
        }
    }
}

static int scorch_around(Map *m, int cx, int cy, int radius) {
    int converted = 0;
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x == cx && y == cy) continue;
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) continue;
            if (m->tiles[y][x].type != TILE_FLOOR) continue;
            m->tiles[y][x].type = TILE_MIASMA;
            converted++;
        }
    }
    return converted;
}

static int purge_around(Map *m, int cx, int cy, int radius) {
    int converted = 0;
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) continue;
            TileType t = m->tiles[y][x].type;
            if (t == TILE_LAVA || t == TILE_MIASMA) {
                m->tiles[y][x].type = TILE_FLOOR;
                converted++;
            }
        }
    }
    return converted;
}

static int bridge_around(Map *m, int cx, int cy, int radius) {
    int converted = 0;
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) continue;
            if (m->tiles[y][x].type == TILE_WATER) {
                m->tiles[y][x].type = TILE_BRIDGE;
                converted++;
            }
        }
    }
    return converted;
}

static bool seal_adjacent_barrier(Map *m, const Hero *h) {
    static const int dxs[4] = {-1, 1, 0, 0};
    static const int dys[4] = {0, 0, -1, 1};
    int order[4] = {0, 1, 2, 3};
    for (int i = 3; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
    }
    for (int k = 0; k < 4; k++) {
        int x = h->x + dxs[order[k]], y = h->y + dys[order[k]];
        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
        if (!is_walkable_player(m, x, y)) continue;
        if (monster_at(m, x, y)) continue;
        m->tiles[y][x].type = TILE_WALL;
        return true;
    }
    return false;
}

static bool unbind_nearest(Map *m, const Hero *h, int radius) {
    int best_x = -1, best_y = -1, best_dist2 = radius * radius + 1;
    for (int y = h->y - radius; y <= h->y + radius; y++) {
        for (int x = h->x - radius; x <= h->x + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            if (m->tiles[y][x].type != TILE_LOCKED_DOOR) continue;
            int dx = x - h->x, dy = y - h->y;
            int dist2 = dx * dx + dy * dy;
            if (dist2 > best_dist2) continue;
            best_x = x; best_y = y; best_dist2 = dist2;
        }
    }
    if (best_x < 0) return false;
    m->tiles[best_y][best_x].type = TILE_FLOOR;
    return true;
}

/* ---- casting ------------------------------------------------------------ */

bool cast_spell_slot(Player *p, Hero *h, Map *m, int slot, bool *out_boss_killed) {
    if (out_boss_killed) *out_boss_killed = false;
    if (slot < 0 || slot >= h->spell_count) return false;

    const SpellTemplate *t = spell_pool_get(h->known_spells[slot]);
    if (!t) return false;

    /* The proving ground's rule, asked where the spell is actually cast so
       that no caller can forget it. */
    if (!proving_allows(m, h->x, h->y, PROVE_ARCANE_ACT)) {
        log_msg("Not here. This ground was marked out for blades.");
        return false;
    }

    if (h->spell_cd[slot] > 0) {
        log_msg("%s is still recharging (%d turns).", t->name, h->spell_cd[slot]);
        return false;
    }
    if (h->aether < t->charge_cost) {
        log_msg("Not enough aether charge to cast %s.", t->name);
        return false;
    }

    int magnitude = t->magnitude + t->magnitude * h->spell_power_pct / 100;

    /* Crystal district: the cast may come straight back. Charged and cooled
       down either way -- the aether is spent the moment it leaves you. */
    if (crystal_ricochet(m, p, magnitude, t->name)) {
        h->aether -= t->charge_cost;
        h->spell_cd[slot] = t->cooldown;
        h->last_spell_slot = slot;
        return true;
    }

    int duration = t->duration + t->duration * h->buff_duration_pct / 100;
    int reach = LEVEL_REACH[t->level];
    int aoe = LEVEL_AOE[t->level];
    int color = school_color_pair(t->school);
    chtype glyph = school_glyph(t->school);

    switch (t->effect) {
        case EFFECT_DAMAGE: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("There's nothing in sight to aim %s at.", t->name);
                return false;
            }
            log_msg("%s tears into the %s for %d.", t->name, target->name, magnitude);
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            monster_take_damage(p, h, m, target, magnitude, out_boss_killed);
            break;
        }
        case EFFECT_DAMAGE_NOVA: {
            int hits = damage_all_in_reach(m, p, aoe + 2, magnitude, out_boss_killed);
            if (hits == 0) {
                log_msg("%s finds nothing nearby to strike.", t->name);
                return false;
            }
            anim_burst(m, p, h->x, h->y, aoe + 2, glyph, color);
            log_msg("%s erupts, striking %d foe%s for %d each.", t->name, hits, hits == 1 ? "" : "s", magnitude);
            break;
        }
        case EFFECT_SCORCH: {
            int converted = scorch_around(m, h->x, h->y, aoe);
            anim_burst(m, p, h->x, h->y, aoe, glyph, color);
            log_msg("%s sears the ground around you.", t->name);
            (void)converted;
            break;
        }
        case EFFECT_BUFF_ATK: {
            h->atk_buff = magnitude;
            h->atk_buff_turns = duration;
            anim_flash_cell(m, p, h->x, h->y, '@', color, 2);
            log_msg("%s courses through you. +%d attack for %d turns.", t->name, magnitude, duration);
            break;
        }
        case EFFECT_HEAL_SELF: {
            h->hp += magnitude;
            if (h->hp > h->maxhp) h->hp = h->maxhp;
            anim_flash_cell(m, p, h->x, h->y, '@', color, 2);
            log_msg("%s knits you back together for %d HP.", t->name, magnitude);
            break;
        }
        case EFFECT_WARD_SHIELD: {
            h->def_buff = magnitude;
            h->def_buff_turns = duration;
            anim_flash_cell(m, p, h->x, h->y, '@', color, 2);
            log_msg("%s settles over you. +%d defence for %d turns.", t->name, magnitude, duration);
            break;
        }
        case EFFECT_BARRIER: {
            if (!seal_adjacent_barrier(m, h)) {
                log_msg("%s finds nowhere clear to seal.", t->name);
                return false;
            }
            anim_flash_cell(m, p, h->x, h->y, '@', color, 1);
            log_msg("%s grinds shut behind you.", t->name);
            break;
        }
        case EFFECT_PURGE: {
            int converted = purge_around(m, h->x, h->y, aoe + 1);
            if (converted == 0) {
                log_msg("%s finds nothing here to cleanse.", t->name);
                return false;
            }
            anim_burst(m, p, h->x, h->y, aoe + 1, glyph, color);
            log_msg("%s clears the hazard from around you.", t->name);
            break;
        }
        case EFFECT_BLINK: {
            int old_x = h->x, old_y = h->y;
            anim_flash_cell(m, p, old_x, old_y, '@', color, 2);
            if (!try_blink(h, m, magnitude)) {
                log_msg("%s won't fold -- nowhere clear to land.", t->name);
                return false;
            }
            anim_flash_cell(m, p, h->x, h->y, '@', color, 2);
            log_msg("%s, and you're somewhere else.", t->name);
            break;
        }
        case EFFECT_REVEAL: {
            reveal_around(m, h->x, h->y, magnitude);
            anim_burst(m, p, h->x, h->y, magnitude < 8 ? magnitude : 8, glyph, color);
            log_msg("%s lights up the passage around you.", t->name);
            break;
        }
        case EFFECT_BRIDGE: {
            int converted = bridge_around(m, h->x, h->y, aoe + 1);
            if (converted == 0) {
                log_msg("%s finds no water nearby to span.", t->name);
                return false;
            }
            anim_burst(m, p, h->x, h->y, aoe + 1, glyph, color);
            log_msg("%s spans the water around you.", t->name);
            break;
        }
        case EFFECT_STUN: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("There's nothing in sight for %s to take hold of.", t->name);
                return false;
            }
            target->stun_turns_left = duration > 0 ? duration : 2;
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            log_msg("%s locks the %s in place.", t->name, target->name);
            break;
        }
        case EFFECT_SLOW: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("There's nothing in sight for %s to weigh down.", t->name);
                return false;
            }
            target->slow_turns_left = duration > 0 ? duration : 3;
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            log_msg("%s weighs down the %s.", t->name, target->name);
            break;
        }
        case EFFECT_UNBIND: {
            if (!unbind_nearest(m, h, reach)) {
                log_msg("%s finds no locked door nearby.", t->name);
                return false;
            }
            anim_flash_cell(m, p, h->x, h->y, '@', color, 1);
            log_msg("%s unwrites the lock -- the door swings open.", t->name);
            break;
        }
        case EFFECT_LIFE_DRAIN: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("There's nothing in sight for %s to feed on.", t->name);
                return false;
            }
            int heal = magnitude / 2;
            log_msg("%s drains the %s for %d, healing you for %d.", t->name, target->name, magnitude, heal);
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            monster_take_damage(p, h, m, target, magnitude, out_boss_killed);
            h->hp += heal;
            if (h->hp > h->maxhp) h->hp = h->maxhp;
            anim_flash_cell(m, p, h->x, h->y, '@', color, 1);
            break;
        }
        case EFFECT_CURSE: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("There's nothing in sight for %s to curse.", t->name);
                return false;
            }
            target->jinx_atk_penalty = magnitude;
            target->jinx_turns_left = duration > 0 ? duration : 4;
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            log_msg("%s settles over the %s -- its attacks falter.", t->name, target->name);
            break;
        }
        case EFFECT_DOT_BURN: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("There's nothing in sight for %s to take hold of.", t->name);
                return false;
            }
            log_msg("%s festers in the %s for %d, and its strength fails.", t->name, target->name, magnitude);
            target->jinx_atk_penalty = magnitude / 3;
            target->jinx_turns_left = duration > 0 ? duration : 4;
            anim_bolt(m, p, h->x, h->y, target->x, target->y, glyph, color);
            anim_flash_cell(m, p, target->x, target->y, (chtype)target->glyph, color, 2);
            monster_take_damage(p, h, m, target, magnitude, out_boss_killed);
            break;
        }
        /* ---- the variety pass ---- */
        case EFFECT_CHAIN: {
            Monster *target = find_nearest_target(m, p, reach);
            if (!target) {
                log_msg("There's nothing in sight for %s to earth through.", t->name);
                return false;
            }
            int tx = target->x, ty = target->y;
            log_msg("%s earths through the %s for %d.", t->name, target->name, magnitude);
            anim_bolt(m, p, h->x, h->y, tx, ty, glyph, color);
            monster_take_damage(p, h, m, target, magnitude, out_boss_killed);

            /* And jumps. Multi-target without being a radius, which is the
               shape Conduit did not have: a nova hits what is near *you*, this
               hits what is near *what you hit*. */
            Monster *next = find_nearest_target_excluding(m, p, reach, target);
            int jumped = 0;
            int arc = magnitude / 2;
            if (arc < 1) arc = 1;
            while (next && jumped < 2) {
                int nx2 = next->x, ny2 = next->y;
                anim_bolt(m, p, tx, ty, nx2, ny2, glyph, color);
                monster_take_damage(p, h, m, next, arc, out_boss_killed);
                log_msg("The arc jumps to the %s for %d.", next->name, arc);
                tx = nx2; ty = ny2;
                jumped++;
                arc /= 2;
                if (arc < 1) break;
                next = find_nearest_target_excluding(m, p, reach, next);
            }
            break;
        }
        case EFFECT_PARTY_HEAL: {
            /* The first spell in the game that treats the party as the thing
               being acted on. Resonance is named for carrying something
               between bodies and drew on two effects, both of which acted on
               the caster alone. */
            int mended = 0;
            for (int b = 0; b < MAX_PARTY; b++) {
                Hero *o = &p->party[b];
                if (!hero_is_up(o) || o->hp >= o->maxhp) continue;
                o->hp += magnitude;
                if (o->hp > o->maxhp) o->hp = o->maxhp;
                mended++;
            }
            if (mended == 0) {
                log_msg("%s finds nobody who needs it.", t->name);
                return false;
            }
            anim_flash_cell(m, p, h->x, h->y, glyph, color, 2);
            log_msg("%s runs through the party -- %d of you mend by %d.", t->name, mended, magnitude);
            break;
        }
        case EFFECT_HASTE: {
            /* Acts on time rather than on a body: everything you were waiting
               for is ready. Deliberately does not touch this spell's own
               cooldown, which is set below -- a spell that cleared its own
               cooldown would be a spell with no cooldown. */
            int cleared = 0;
            for (int k = 0; k < h->spell_count; k++) {
                if (k == slot || h->spell_cd[k] <= 0) continue;
                h->spell_cd[k] = 0;
                cleared++;
            }
            if (h->ability_cd > 0) { h->ability_cd = 0; cleared++; }
            if (h->ranged_cooldown > 0) { h->ranged_cooldown = 0; cleared++; }
            if (cleared == 0) {
                log_msg("%s finds nothing waiting on you.", t->name);
                return false;
            }
            anim_flash_cell(m, p, h->x, h->y, glyph, color, 2);
            log_msg("%s runs ahead of the beat -- %d things are ready again.", t->name, cleared);
            break;
        }
        case EFFECT_REFLECT: {
            /* Warding prevented damage three ways and punished it none. This
               is the third answer to being hit, after "take less" and "do not
               be there". */
            int pct = 20 + t->level * 8;
            if (pct > 60) pct = 60;
            h->reflect_pct = pct;
            h->reflect_turns = duration > 0 ? duration : 5;
            anim_flash_cell(m, p, h->x, h->y, glyph, color, 2);
            log_msg("%s settles over you: %d%% of what lands comes back, %d turns.",
                    t->name, pct, h->reflect_turns);
            break;
        }
        case EFFECT_SENSE_LIFE: {
            /* Information, bought. Aether-Sense could reveal *ground* and move
               you across it and had no way to answer "what is on this floor",
               which on a 1400x800 map is the question that matters. */
            int found = 0;
            for (int i = 0; i < m->monster_count; i++) {
                Monster *mo = &m->monsters[i];
                if (!mo->alive) continue;
                m->tiles[mo->y][mo->x].seen = true;
                m->tiles[mo->y][mo->x].visible = true;
                found++;
            }
            anim_burst(m, p, h->x, h->y, aoe + 2, glyph, color);
            if (found == 0) log_msg("%s reaches out and finds this floor empty.", t->name);
            else            log_msg("%s finds them: %d things are moving on this floor.", t->name, found);
            break;
        }
        case EFFECT_MASS_SLOW: {
            /* Scribing held a target and this holds a room. Same verb, and a
               different question -- which one, against how many. */
            int caught = 0;
            for (int i = 0; i < m->monster_count; i++) {
                Monster *mo = &m->monsters[i];
                if (!mo->alive) continue;
                int dx = mo->x - h->x, dy = mo->y - h->y;
                if (dx * dx + dy * dy > (aoe + 2) * (aoe + 2)) continue;
                mo->slow_turns_left += duration > 0 ? duration : 4;
                caught++;
            }
            if (caught == 0) {
                log_msg("%s is written out over nothing in particular.", t->name);
                return false;
            }
            anim_burst(m, p, h->x, h->y, aoe + 2, glyph, color);
            log_msg("%s settles over %d of them. Everything here is slower now.", t->name, caught);
            break;
        }
        default:
            return false;
    }

    h->aether -= t->charge_cost;
    h->spell_cd[slot] = t->cooldown;
    h->last_spell_slot = slot;
    return true;
}

/* Effects that damage something. Buffs, heals and terrain spells are all
   useful, but not for the question "can I hurt that thing from here" -- and
   auto-casting a Warding shield at a rat would just burn the charge a real
   fight needs. */
static bool effect_is_offensive(int effect) {
    switch (effect) {
        case EFFECT_DAMAGE:
        case EFFECT_DAMAGE_NOVA:
        case EFFECT_LIFE_DRAIN:
        case EFFECT_DOT_BURN:
        /* Chain is damage that jumps. Without it here, auto-explore would
           never pick a Conduit chain spell and a fifth of that school would be
           invisible to the hunter. */
        case EFFECT_CHAIN:
            return true;
        default:
            return false;
    }
}

/* How far each offensive effect actually lands, which is not always the
   spell's target-search range: a nova only hurts what's inside its blast. */
static int offensive_reach(const SpellTemplate *t) {
    if (t->level < 1 || t->level > 5) return 0;
    if (t->effect == EFFECT_DAMAGE_NOVA) return LEVEL_AOE[t->level] + 2;
    return LEVEL_REACH[t->level];
}

int spell_pick_attack_slot(const Hero *h, int target_dist) {
    if (!h) return -1;

    int best_slot = -1, best_magnitude = 0;
    for (int slot = 0; slot < h->spell_count; slot++) {
        if (h->spell_cd[slot] > 0) continue;

        const SpellTemplate *t = spell_pool_get(h->known_spells[slot]);
        if (!t) continue;
        if (!effect_is_offensive(t->effect)) continue;
        if (h->aether < t->charge_cost) continue;
        if (target_dist > offensive_reach(t)) continue;

        int magnitude = t->magnitude + t->magnitude * h->spell_power_pct / 100;
        if (magnitude > best_magnitude) { best_magnitude = magnitude; best_slot = slot; }
    }
    return best_slot;
}
