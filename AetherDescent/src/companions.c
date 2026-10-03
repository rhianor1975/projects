#include "companions.h"
#include "bands.h"
#include "combat.h"
#include "monsters.h"
#include "classes.h"
#include "render.h"
#include "mapgen.h"
#include "path.h"
#include "gearsets.h"
#include "items.h"
#include "spells.h"
#include "abilities.h"   /* use_ability -- the MC keeps their own */
#include "ranged.h"      /* fire_ranged -- and their own bow */

/* ---- names -----------------------------------------------------------
   Victorian drawing-room by way of the engine deck: airship captains,
   calculating-machine professors, safe-crackers and society ladies who fund
   expeditions. The earlier list read like dock workers and deserters, which
   is the wrong century for a city of brass domes and ether-lanterns.

   Titles are role-appropriate and only land on about half the roster --
   everyone being a Captain or a Professor stops meaning anything. */
/* `sex` is only here so the honorifics land right -- "Lady Mordecai" is the
   sort of thing a generator does that a reader notices immediately. 'a'
   marks names that take either. */
typedef struct { const char *name; char sex; } GivenName;
static const GivenName GIVEN[] = {
    { "Alistair", 'm' }, { "Zephyrine", 'f' }, { "Thaddeus", 'm' },
    { "Seraphina", 'f' }, { "Barnaby", 'm' }, { "Orion", 'm' },
    { "Isadora", 'f' }, { "Magnus", 'm' }, { "Cordelia", 'f' },
    { "Peregrine", 'a' }, { "Archimedes", 'm' }, { "Emmeline", 'f' },
    { "Nikola", 'a' }, { "Genevieve", 'f' }, { "Horatio", 'm' },
    { "Theodosia", 'f' }, { "Kelvin", 'm' }, { "Rosalind", 'f' },
    { "Silas", 'm' }, { "Mordecai", 'm' }, { "Lorelei", 'f' },
    { "Augustus", 'm' }, { "Katrina", 'f' }, { "Victor", 'm' },
    { "Penelope", 'f' }, { "Reginald", 'm' }, { "Violetta", 'f' },
    { "Geoffrey", 'm' }, { "Esmeralda", 'f' }, { "Percival", 'm' },
    { "Eleanor", 'f' }, { "Winston", 'm' }, { "Clarissa", 'f' },
    { "Ambrose", 'm' }, { "Octavia", 'f' }, { "Ignatius", 'm' },
    { "Wilhelmina", 'f' }, { "Bartholomew", 'm' }, { "Arabella", 'f' },
    { "Cornelius", 'm' },
};
static const char *const EPITHET[] = {
    "Finnegan", "Voss", "Roche", "Skye", "Quill", "Blackwood", "Vance",
    "Sterling", "Wainwright", "Hawks", "Babbage", "Gearhart", "Kessel",
    "Vex", "Thorne", "Wrench", "Flux", "Croft", "Crank", "Nettle",
    "Devereux", "Grimshaw", "Raven", "Locke", "Vancour", "Pierce",
    "Vesper", "Ashford", "Bumblethorpe", "Heliotrope", "Wainscott", "Vane",
    "Ravenswood", "Quick", "Smythe", "Bramble", "Cogsworth", "Ironside",
    "Pellingham", "Marchbanks",
};

/* Which honorifics suit which role, and who can carry them. A NULL text
   means "no title this time" -- there is one in every row so a plain name
   stays common, and a title whose sex doesn't match the given name is
   dropped the same way, which keeps roughly half the roster untitled. */
typedef struct { const char *text; char sex; } Title;
static const Title TITLES[ARCHETYPE_COUNT][4] = {
    { {"Capt.",'a'}, {"Cmdr.",'a'}, {"Sir",'m'},     {NULL,'a'} },  /* Vanguard   */
    { {NULL,'a'},    {NULL,'a'},    {"Sgt.",'a'},    {NULL,'a'} },  /* Skirmisher */
    { {"Capt.",'a'}, {NULL,'a'},    {NULL,'a'},      {NULL,'a'} },  /* Marksman   */
    { {"Prof.",'a'}, {"Dr.",'a'},   {"Madame",'f'},  {NULL,'a'} },  /* Arcanist   */
    { {"Prof.",'a'}, {"Dr.",'a'},   {"Baron",'m'},   {NULL,'a'} },  /* Artificer  */
    { {NULL,'a'},    {NULL,'a'},    {NULL,'a'},      {NULL,'a'} },  /* Survivor   */
    { {"Lord",'m'},  {"Lady",'f'},  {"Duchess",'f'}, {NULL,'a'} },  /* Envoy      */
};

#define GIVEN_N   ((int)(sizeof(GIVEN)   / sizeof(GIVEN[0])))
#define EPITHET_N ((int)(sizeof(EPITHET) / sizeof(EPITHET[0])))

/* Companions are drawn as '@' like the player, so colour is the only thing
   telling them apart -- deliberately none of them cyan, which is the
   player's. */
static const int COMPANION_COLORS[] = {
    CP_MON_VERMIN, CP_MON_CLOCKWORK, CP_MON_RUINS,
    CP_MON_OUTRIDER, CP_GOLD, CP_SHOP,
};
#define COMPANION_COLOR_N ((int)(sizeof(COMPANION_COLORS) / sizeof(COMPANION_COLORS[0])))

/* Defined further down, next to the rest of the kit logic; needed here
   because a roster candidate is equipped the moment it is built. */
static void companion_equip(Hero *c, unsigned int h);

/* ---- the smith works for the whole party ------------------------------
   A hire cannot be handed a weapon, so the only way your gear reaches them
   is this: whatever the smith has done to your sword, their weapon has too.
   Your +4 is their +4.

   It is also the mechanism that keeps a party worth paying for at depth.
   Their stats are frozen at the level you hired them, so without this a
   hero bought on floor 5 is a hero from floor 5 for the rest of the run --
   the upgrade track is what carries them down with you. */
/* The smith's work, shared out to the party.
 *
   These read slot 0's upgrade counters, which was right while only the
   character could walk into a shop. Buy an upgrade while driving a hire and it
   lands on the hire -- so the party bonus read zero and the coin bought
   nothing for anybody. The best in the party is the honest reading: the smith
   has been to that weapon, whoever was carrying it when they went.

   Written out four times rather than folded into one offsetof() helper,
   because a byte-offset read of a struct field is a clever way to say
   something this file can say plainly. */
#define PARTY_BEST(p, field)                                   \
    do {                                                       \
        int best_ = 0;                                         \
        /* No in_use guard: an empty slot is zeroed, so it cannot win a  \
           maximum, and skipping it would only make this depend on a flag \
           that a hand-built Player may not have set. */                  \
        for (int i_ = 0; i_ < MAX_PARTY; i_++)                 \
            if ((p)->party[i_].field > best_)                  \
                best_ = (p)->party[i_].field;                  \
        return best_;                                          \
    } while (0)

static int party_weapon_plus(const Player *p) { PARTY_BEST(p, weapon_plus); }
static int party_armor_plus(const Player *p)  { PARTY_BEST(p, armor_plus); }
static int party_hp_plus(const Player *p)     { PARTY_BEST(p, hp_plus); }
static int party_aether_plus(const Player *p) { PARTY_BEST(p, aether_plus); }
#undef PARTY_BEST

int party_atk_bonus(const Player *p) { return party_weapon_plus(p) * UPGRADE_STEP; }
int party_def_bonus(const Player *p) { return party_armor_plus(p)  * UPGRADE_STEP; }

/* The bars carry across the same way the gear does. Health matters most:
   a hire's own maxhp is frozen at the level you took them on, so without
   this the party quietly stops being able to stand in a fight somewhere
   around the depth the player's own bar stopped being enough. */
static int party_hp_bonus(const Player *p)  { return party_hp_plus(p)     * UPGRADE_HP_STEP; }
static int party_aeth_bonus(const Player *p){ return party_aether_plus(p) * UPGRADE_AETHER_STEP; }

/* A companion's real health ceiling: what they were hired with, plus
   whatever the apothecary has done for the party since. c->maxhp stays the
   base so the bonus tracks the player instead of being baked in. */
int companion_max_hp(const Player *p, const Hero *c) {
    return c->maxhp + party_hp_bonus(p);
}

int companion_max_aether(const Player *p, const Hero *c) {
    return c->aether_max + party_aeth_bonus(p);
}


/* ---- what the class itself is worth -----------------------------------
   Two Vanguard classes used to be the same hero with different names: their
   stats came from the archetype tilt alone, and the class was a label.
   The class's own attribute overrides are just data, and the handful that map
   onto health, attack and defence are worth reading as a variation on top of
   the tilt.

   The reason this could not simply call the character's constructor is gone --
   hero_create() builds a hire now, sheet and all. This stays because it is a
   different question: not "what are they" but "how much does being a
   Brass-Collar rather than a Rivet-Legionnaire move the numbers".

   Baseline is 3 ("an average person"), so an override is read as a
   deviation from that; the result is a few percent either way, which is the
   right size for "these two are both Vanguards, but not the same Vanguard". */
static const Attribute CLASS_HP_ATTRS[]  = { ATTR_BRAWN, ATTR_FORTITUDE, ATTR_STAMINA };
static const Attribute CLASS_ATK_ATTRS[] = { ATTR_MIGHT, ATTR_PRECISION, ATTR_CONDUIT };
static const Attribute CLASS_DEF_ATTRS[] = { ATTR_GRIT, ATTR_BALANCE, ATTR_WARDING };

static int class_attr_edge(const ClassSeed *seed, const Attribute *want, int n) {
    if (!seed || !seed->overrides) return 0;
    int edge = 0;
    for (const AttrOverride *o = seed->overrides; o->attr != ATTR_COUNT; o++)
        for (int i = 0; i < n; i++)
            if (o->attr == want[i]) edge += o->value - 3;   /* 3 is the baseline */
    return edge;
}

/* Percent adjustments, clamped so a wildly-specialised class cannot turn its
   archetype inside out -- a Vanguard with a high Might is a hard-hitting
   Vanguard, not a Skirmisher. */
void companion_class_variation(int class_id, int *hp_pct_out, int *atk_pct_out, int *def_pct_out) {
    *hp_pct_out = *atk_pct_out = *def_pct_out = 0;
    if (class_id < 0 || class_id >= NUM_CLASSES) return;
    const ClassSeed *seed = &CLASS_TABLE[class_id];

    int hp  = class_attr_edge(seed, CLASS_HP_ATTRS,  3) * 2;
    int atk = class_attr_edge(seed, CLASS_ATK_ATTRS, 3) * 2;
    int def = class_attr_edge(seed, CLASS_DEF_ATTRS, 3) * 2;

    if (hp  >  15) hp  =  15; if (hp  < -15) hp  = -15;
    if (atk >  15) atk =  15; if (atk < -15) atk = -15;
    if (def >  15) def =  15; if (def < -15) def = -15;

    *hp_pct_out = hp; *atk_pct_out = atk; *def_pct_out = def;
}

const char *personality_name(int personality) {
    switch (personality) {
        case PERSONALITY_BOLD:     return "Bold";
        case PERSONALITY_STEADY:   return "Steady";
        case PERSONALITY_CAUTIOUS: return "Cautious";
        default:                   return "?";
    }
}

/* ---- prices ----------------------------------------------------------
   Ten times the last, every time. The fifth hire costs ten million, which
   is not a purchase so much as an ambition. */
long companion_hire_cost(int already_hired) {
    long cost = 1000;
    for (int i = 0; i < already_hired && i < MAX_COMPANIONS; i++) cost *= 10;
    return cost;
}

long companion_rehire_cost(int tier) {
    /* A quarter. The Tavern knows they died down there and prices the risk
       accordingly -- losing one hurts without ending the investment. */
    long full = companion_hire_cost(tier);
    long cut = full / 4;
    return cut < 1 ? 1 : cut;
}

/* ---- the roster ------------------------------------------------------
   Regenerated from the seed rather than stored. Same seed, same twenty
   people, every visit and across a save -- for the cost of four bytes
   instead of twenty structs. */
static unsigned int mix(unsigned int a, unsigned int b) {
    unsigned int h = a * 2654435761u ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return h;
}

/* What each kind of hero is better and worse at, as percentages of the
   yardstick below rather than flat points -- a flat +10 HP is a character
   at level 1 and a rounding error at level 50. Rows follow the Archetype
   enum, kept as a table so the balance is legible at a glance. */
typedef struct { int hp, atk, def; } ArchTilt;
static const ArchTilt ARCH_TILT[ARCHETYPE_COUNT] = {
    {  25,  -5,  30 },   /* Vanguard   -- stands in front and stays there */
    { -10,  15,   0 },   /* Skirmisher -- fast, fragile, hits often */
    { -12,  20,  -8 },   /* Marksman   -- kills at a distance, folds up close */
    { -22,  28,  -8 },   /* Arcanist   -- hits hardest, takes the least */
    {   5,   5,  10 },   /* Artificer  -- rigged up, quietly solid */
    {  35,  -8,  10 },   /* Survivor   -- outlasts everything */
    {   0,   5,   5 },   /* Envoy      -- talks more than it fights */
};

/* What the player is actually worth in a fight, which is the thing a hire
   has to match. Level is the wrong measure: a level-50 character with a set
   weapon, a ring and fifteen spells is a different proposition from a
   level-50 character who has been hoarding gold, and a hero who is "level 50"
   but carries nothing is not a peer, they are baggage.
 *
 * Note this is one number, not a copy of the player's kit. A fighter who
 * hires an Arcanist should get a caster of their own standing -- proficient
 * in magic, not a swordsman's stats with a robe on -- so the yardstick
 * measures how far the player has come and the archetype decides what shape
 * that takes. Companions have no spellbook of their own; a caster's magic is
 * folded into what they hit for. */
static void player_yardstick(const Player *p, int *hp, int *atk, int *def) {
    /* Deliberately excludes atk_buff/def_buff: hiring with a draught
       running should not buy you a better hero. */
    const Hero *ref = hero_driven_c(p);   /* a hire is a peer of whoever hired them */
    int melee  = ref->base_atk + ref->weapon_bonus + ref->set_bonus_atk;

    /* Spells never show up in ATK, but fifteen of them is most of a caster's
       damage, scaled by the power the player has behind them. */
    int magic  = (ref->spell_count * (100 + ref->spell_power_pct)) / 200;

    /* Likewise a ranged weapon, discounted for the cooldown between shots. */
    int ranged = ref->ranged_bonus > 0
               ? (ref->ranged_bonus * (100 + ref->ranged_dmg_bonus_pct)) / 200 : 0;

    /* Whichever channel the player leans on is what they are; the others are
       a genuine but smaller advantage, so they count for a quarter. Summing
       all three would price a hybrid as three characters. */
    int best = melee > magic ? melee : magic;
    if (ranged > best) best = ranged;
    int power = best + (melee + magic + ranged - best) / 4;

    *atk = power > 1 ? power : 1;
    *def = ref->base_def + ref->armor_bonus + ref->set_bonus_def;
    if (*def < 0) *def = 0;
    *hp  = ref->maxhp > 20 ? ref->maxhp : 20;
}

/* One hash per attribute rather than slices of a single one: taking `h >> 4`
   for the archetype and `h` for the name made the two correlate, and the
   roster came out eight Artificers deep with four heroes sharing a name. */
static unsigned int trait_hash(const Player *p, int idx, unsigned int field) {
    /* The reroll counter is part of the key, so letting someone go really
       does put a different person on that line -- same seed, same everyone
       else. */
    unsigned int seed = p->tavern_seed ? p->tavern_seed : 1u;
    unsigned int slot = (unsigned int)idx * 977u + p->tavern_reroll[idx] * 31u;
    return mix(mix(seed, slot), field * 2654435761u + 0x51ED270Bu);
}

/* Names for the whole roster at once, because uniqueness is a property of
   the list, not of one entry: 28 given names by 26 epithets is a big enough
   space that two heroes rarely collide, but "rarely" still reads as a bug
   when it happens. On a clash the later entry yields and re-rolls -- so a
   given line depends only on the lines above it. */
/* The role a candidate is drawn as. Needed before the name, because the
   honorific follows the role -- a Duchess is not a Marksman. */
static int candidate_role(const Player *p, int idx) {
    return (int)(trait_hash(p, idx, 5u) % ARCHETYPE_COUNT);
}

static void roster_names(const Player *p, char names[TAVERN_ROSTER][32]) {
    for (int i = 0; i < TAVERN_ROSTER; i++) {
        int role = candidate_role(p, i);
        for (unsigned int salt = 0; salt < 32; salt++) {
            unsigned int g = trait_hash(p, i, 1u + salt * 4u);
            unsigned int e = trait_hash(p, i, 2u + salt * 4u);
            unsigned int t = trait_hash(p, i, 3u + salt * 4u);
            const GivenName *gn = &GIVEN[g % GIVEN_N];
            const char *sur = EPITHET[e % EPITHET_N];
            const Title *ti = &TITLES[role][t % 4];
            const char *title = ti->text;

            /* A gendered title on a name that doesn't take it. */
            if (title && ti->sex != 'a' && gn->sex != 'a' && ti->sex != gn->sex)
                title = NULL;

            /* Titles are decoration; a name cut off in the list is worse
               than a name without one, so the title is what gives way.
               TAVERN_NAME_SHOWN is the width the roster renders. */
            if (title && strlen(title) + strlen(gn->name) + strlen(sur) + 2 > TAVERN_NAME_SHOWN)
                title = NULL;

            if (title) snprintf(names[i], 32, "%s %s %s", title, gn->name, sur);
            else       snprintf(names[i], 32, "%s %s", gn->name, sur);

            bool clash = false;
            for (int j = 0; j < i && !clash; j++)
                if (strcmp(names[i], names[j]) == 0) clash = true;
            if (!clash) break;
        }
    }
}

void tavern_candidate(const Player *p, int idx, Hero *out) {
    memset(out, 0, sizeof(*out));
    if (idx < 0 || idx >= TAVERN_ROSTER) return;

    char names[TAVERN_ROSTER][32];
    roster_names(p, names);
    snprintf(out->name, sizeof(out->name), "%s", names[idx]);

    unsigned int h = trait_hash(p, idx, 3u);

    /* Role first, then a class inside it -- the same order the player picks
       in. Drawing the class straight out of the hundred instead would let
       the roster inherit the class table's own lopsidedness (nineteen
       Skirmisher classes against twelve Survivor ones), and a tavern that
       is a third Skirmishers most nights is a worse list to shop from than
       one with a bit of everything.

       They are real classes from the same table the player chooses from, not
       a parallel list of NPC types, and the archetype is read back off the
       class so the two can never disagree. */
    int role  = candidate_role(p, idx);
    int n     = archetype_class_count(role);
    int class_id = n > 0 ? archetype_class_index(role, (int)(trait_hash(p, idx, 6u) % (unsigned)n)) : 0;

    /* The same constructor the character is built by, and that is the whole
       change here. A hire used to be assembled in place out of an archetype
       tilt, which is why they had no attributes, no derived sheet, no innate
       ability and a school drawn from a hash -- not because anybody decided a
       hire should lack those, but because the function that wrote them only
       ever wrote them onto a Player.

       starting_kit is false and the party is NULL: a candidate is somebody you
       are looking at in a list, and looking at them must not put a draught in
       your pack. What they walk in carrying is companion_equip's business,
       below, because that is theirs rather than the party's. */
    hero_create(NULL, out, class_id, false);

    /* Re-stamped after the constructor, which sets them to the sentinels a
       body with no roster seat carries. */
    out->roster_idx  = idx;
    out->personality = (int)(trait_hash(p, idx, 4u) % PERSONALITY_COUNT);
    out->color_pair  = COMPANION_COLORS[idx % COMPANION_COLOR_N];

    /* They start at the player's level -- a hero you take on is a peer, not
       an apprentice -- with a spread of stats around it so two hires of the
       same level still feel different. */
    int lv = hero_driven_c(p)->level > 0 ? hero_driven_c(p)->level : 1;
    out->level = lv;

    int y_hp, y_atk, y_def;
    player_yardstick(p, &y_hp, &y_atk, &y_def);

    /* Around the player rather than exactly on them, so two hires of the same
       level still feel different and there is a reason to read the list. */
    out->maxhp    = y_hp  * (70 + (int)((h >> 20) % 26)) / 100;   /*  70- 95% */
    out->base_atk = y_atk * (70 + (int)((h >>  8) % 26)) / 100;   /*  70- 95% */
    out->base_def = y_def * (70 + (int)((h >> 24) % 31)) / 100;   /*  70-100% */

    /* Archetype is not just a label on the list: it decides what shape that
       standing takes. A Vanguard soaks it, an Arcanist spends it on damage. */
    const ArchTilt *t = &ARCH_TILT[out->archetype];
    out->maxhp    = out->maxhp    * (100 + t->hp)  / 100;
    out->base_atk = out->base_atk * (100 + t->atk) / 100;
    out->base_def = out->base_def * (100 + t->def) / 100;

    /* Then the class on top of the role, so a Rivet-Legionnaire and a
       Brass-Collar are both Vanguards without being the same Vanguard. */
    int cv_hp, cv_atk, cv_def;
    companion_class_variation(out->class_id, &cv_hp, &cv_atk, &cv_def);
    out->maxhp    = out->maxhp    * (100 + cv_hp)  / 100;
    out->base_atk = out->base_atk * (100 + cv_atk) / 100;
    out->base_def = out->base_def * (100 + cv_def) / 100;

    if (out->maxhp < 12)    out->maxhp    = 12;
    if (out->base_atk < 1)  out->base_atk = 1;
    if (out->base_def < 0)  out->base_def = 0;
    out->hp = out->maxhp;

    /* What they walk in carrying, fixed at the moment you look at them --
       the same rule as their level and their stats. */
    companion_equip(out, trait_hash(p, idx, 9u));
    out->xp       = 0;
    out->xp_next  = 20 + (lv - 1) * 15;
}

bool tavern_is_hired(const Player *p, int idx) {
    if (idx < 0 || idx >= TAVERN_ROSTER) return false;
    return (p->tavern_hired_mask & (1u << idx)) != 0;
}

bool tavern_has_fallen(const Player *p, int idx) {
    if (idx < 0 || idx >= TAVERN_ROSTER) return false;
    return (p->tavern_fallen_mask & (1u << idx)) != 0;
}

int companion_count(const Player *p) {
    int n = 0;
    for (int i = 0; i < MAX_COMPANIONS; i++) if (p->party[(i) + 1].in_use) n++;
    return n;
}

bool companion_hire(Player *p, int idx) {
    if (idx < 0 || idx >= TAVERN_ROSTER) return false;
    if (tavern_is_hired(p, idx)) { log_msg("They already drink on your coin."); return false; }

    int filled = companion_count(p);
    if (filled >= MAX_COMPANIONS) {
        log_msg("Six is a crowd already. Bury someone or travel lighter.");
        return false;
    }

    /* A hero who died in your service comes cheaper the second time. */
    bool fallen = tavern_has_fallen(p, idx);
    long price = fallen ? companion_rehire_cost(filled) : companion_hire_cost(filled);

    if ((long)p->gold < price) {
        log_msg("%s wants %ld gold. You have %d.", "That one", price, p->gold);
        return false;
    }

    int slot = -1;
    for (int i = 0; i < MAX_COMPANIONS; i++) if (!p->party[(i) + 1].in_use) { slot = i; break; }
    if (slot < 0) return false;

    Hero c;
    tavern_candidate(p, idx, &c);
    c.in_use = true;
    c.alive = true;
    c.tier  = filled;
    c.x = hero_driven(p)->x; c.y = hero_driven(p)->y;

    p->party[(slot) + 1] = c;
    p->gold -= (int)price;
    p->tavern_hired_mask |= (1u << idx);
    p->tavern_fallen_mask &= ~(1u << idx);

    log_msg("%s takes your coin (%ld) and shoulders a pack.", c.name, price);
    if (fallen) log_msg("They do not mention the last time.");
    return true;
}

bool companion_grant_golem(Player *p, int floor_num) {
    int filled = companion_count(p);
    if (filled >= MAX_COMPANIONS) {
        log_msg("The frame stands up, looks at your crowd, and sits back down.");
        return false;
    }

    int slot = -1;
    for (int i = 0; i < MAX_COMPANIONS; i++) if (!p->party[(i) + 1].in_use) { slot = i; break; }
    if (slot < 0) return false;

    /* Start from a real candidate so every field -- gear, spells, ranged
       slot, cooldowns -- is populated the way the rest of the module expects,
       then make it a golem. */
    Hero c;
    tavern_candidate(p, 0, &c);

    c.roster_idx = -1;          /* never hired, never released, never mourned */
    c.temporary  = true;
    c.in_use      = true;
    c.alive      = true;
    c.tier       = filled;
    /* It stands up beside whoever built it. */
    c.x = hero_driven(p)->x; c.y = hero_driven(p)->y;
    snprintf(c.name, sizeof(c.name), "Assembly Golem");

    /* Built things do not cast and do not shoot: it walks up and hits. It
       keeps its school, though -- an empty spellbook is spell_count, not a
       missing school, and a golem you take over should be able to walk into
       the Guild like anybody else. */
    c.spell_count  = 0;
    c.ranged_slot  = 0;
    c.aether = c.aether_max = 0;

    /* Sturdy and slow rather than sharp -- it is a wall you can put in front
       of yourself, which is what the district's fight actually wants. */
    c.level    = floor_num > 1 ? floor_num : 1;
    c.maxhp    = 40 + floor_num * 12;
    c.hp       = c.maxhp;
    c.base_atk = 8 + floor_num * 2;
    c.base_def = 10 + floor_num * 3;

    p->party[(slot) + 1] = c;
    log_msg("The frame shudders, finds its feet, and falls in beside you.");
    return true;
}

void companions_dismiss_temporary(Player *p) {
    for (int i = 0; i < MAX_COMPANIONS; i++) {
        Hero *c = &p->party[(i) + 1];
        if (!c->in_use || !c->temporary) continue;
        memset(c, 0, sizeof(*c));
    }
}

const Hero *companion_by_roster(const Player *p, int idx) {
    for (int i = 0; i < MAX_COMPANIONS; i++)
        if (p->party[(i) + 1].in_use && p->party[(i) + 1].roster_idx == idx)
            return &p->party[(i) + 1];
    return NULL;
}

bool tavern_can_release(const Player *p, int idx) {
    if (idx < 0 || idx >= TAVERN_ROSTER) return false;
    return tavern_is_hired(p, idx) || tavern_has_fallen(p, idx);
}

bool companion_release(Player *p, int idx) {
    if (!tavern_can_release(p, idx)) {
        log_msg("They don't work for you. There's nothing to end.");
        return false;
    }

    Hero who;
    tavern_candidate(p, idx, &who);
    char name[32];
    snprintf(name, sizeof(name), "%s", who.name);
    bool was_out = tavern_is_hired(p, idx);

    /* Half the fee back, but only for someone who is still walking. A hero
       who died in your service hands nothing back -- there is nobody to pay
       off. Costs are tiered by how many you already had, so the refund is
       priced against the slot being freed, not against the first hire.
     *
       Without this, changing your mind about a hire cost the whole fee, and
       at 1,000 / 10,000 / 100,000 gold a slot that is a punishing amount of
       money to lose to a misclick. */
    long refund = 0;
    if (was_out) {
        int filled = companion_count(p);
        int tier = filled > 0 ? filled - 1 : 0;
        refund = companion_hire_cost(tier) / 2;
    }

    /* Clear the party slot first: a released hero leaves no body behind. */
    for (int i = 0; i < MAX_COMPANIONS; i++)
        if (p->party[(i) + 1].in_use && p->party[(i) + 1].roster_idx == idx)
            memset(&p->party[(i) + 1], 0, sizeof(p->party[(i) + 1]));

    p->tavern_hired_mask  &= ~(1u << idx);
    p->tavern_fallen_mask &= ~(1u << idx);
    p->tavern_reroll[idx]++;               /* a new face takes that chair */

    if (was_out) {
        int back = player_credit_gold(p, refund);
        log_msg("%s hands back the pack and doesn't argue. (+%d gold, half the fee)",
                name, back);
    } else {
        log_msg("You strike %s off the books for good.", name);
    }

    Hero now;
    tavern_candidate(p, idx, &now);
    log_msg("%s is drinking in their place.", now.name);
    return true;
}

/* ---- in the dungeon -------------------------------------------------- */

/* Everybody the stairs did not already move.
 *
   The loop ran over slots 1..5, so slot 0 was only ever repositioned by being
   the one who took the stairs. Descend while driving a hire and the character
   kept the coordinates they had on the *previous* floor -- measured landing
   them inside solid terrain on the new one, where nothing could reach them and
   they could not walk out. */
void companions_place(Player *p, Map *m) {
    for (int i = 0; i < MAX_PARTY; i++) {
        Hero *c = &p->party[i];
        if (i == p->controlled) continue;   /* already standing at the arrival point */
        if (!hero_is_up(c)) continue;

        /* Drop them in a ring around the arrival point -- which is where the
           actor who took the stairs is standing, not where slot 0 is. Descend
           while driving a hire and the party used to materialise around the
           character, who was somewhere else entirely. */
        int ax = hero_driven_c(p)->x, ay = hero_driven_c(p)->y;
        bool placed = false;
        for (int r = 1; r <= 6 && !placed; r++) {
            for (int dy = -r; dy <= r && !placed; dy++) {
                for (int dx = -r; dx <= r && !placed; dx++) {
                    int x = ax + dx, y = ay + dy;
                    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                    if (!is_walkable_monster(m, x, y)) continue;
                    if (monster_at(m, x, y)) continue;
                    if (x == ax && y == ay) continue;
                    if (companion_at(p, x, y)) continue;
                    c->x = x; c->y = y;
                    c->last_x = c->last_y = -1;   /* a new floor, no history */
                    c->scout_x = c->scout_y = -1;
                    c->scout_turns = 0;
                    placed = true;
                }
            }
        }
        if (!placed) {
            c->x = ax; c->y = ay;
            c->last_x = c->last_y = -1;
            c->scout_x = c->scout_y = -1;
            c->scout_turns = 0;
        }
    }
}

void companions_reveal(Player *p, Map *m) {
    if (m->floor_num == 0) return;          /* town: see below */
    for (int i = 0; i < MAX_COMPANIONS; i++) {
        const Hero *c = &p->party[(i) + 1];
        if (!c->in_use || !c->alive) continue;
        add_fov(m, c->x, c->y, COMPANION_FOV);
    }
}

/* Any body standing at (x,y) that is *not* the one being driven.
 *
   The driven body is excluded because every caller handles it separately and
   differently: a monster swings at it through monster_attack_player, and the
   movement checks test it by name. What the callers want from here is "is
   somebody else in this square".

   It scanned slots 1..5 -- hires only -- which was the same thing while the
   character could never be anything but driven. It is not the same thing now.
   The consequence was that **the character was invulnerable whenever you were
   playing somebody else**: monsters find an adjacent body to hit through this
   function, it never returned slot 0, and monster_attack_player only ever
   targets whoever holds the controller. Switch away from your character and
   nothing in the temple could touch them. */
Hero *companion_at(Player *p, int x, int y) {
    for (int i = 0; i < MAX_PARTY; i++) {
        if (i == p->controlled) continue;
        Hero *c = &p->party[i];
        if (hero_is_up(c) && c->x == x && c->y == y) return c;
    }
    return NULL;
}

/* companion_grant_xp() used to stand here: a second levelling curve, for
   hires only, with proportional gains (maxhp/12) against the player's flat
   ones (+8). It is gone with the second kill path that was its only caller.

   Every actor now levels through grant_xp() on one curve. That is a real
   change in what a high-level hire is worth -- proportional gains are larger
   than flat ones once maxhp passes about 96 -- and it is the change the
   project owner's rule asks for: a hero bought at the Tavern is the same kind
   of thing as the one the run started as, including how they grow. It is
   listed in ROADMAP as wanting a measurement. */

/* Damage landing on a body, after whatever computed it decided how much.
 *
   The defence maths used to live in here -- the party's plate, the Warding
   shield -- which meant every caller that had *already* worked out a defended
   number got it subtracted a second time, and every caller that had not got a
   different defence model from the player's. Both now happen where the number
   is calculated (monster_hit_hero), and what is left here is the part that is
   the same however the damage arose: the Vanguard's guard halves it, it lands,
   and if it kills the body the role moves on.

   Renamed from companion_take_damage because it is not a companion's, and the
   old name is how it stayed a reduced pipeline for so long. */
bool hero_take_damage(Player *p, Hero *c, int dmg) {
    if (!hero_is_up(c)) return false;
    if (c->guard_turns > 0) dmg = (dmg + 1) / 2;   /* Bulwark */
    if (dmg < 1) dmg = 1;
    c->hp -= dmg;
    if (c->hp > 0) return false;

    c->hp = 0;
    c->alive = false;
    log_msg("%s goes down and does not get up.", c->name);

    /* If that was the body the player was driving, the role moves on rather
       than the run ending -- the party are lives. party_pass_the_torch picks
       the next one standing, and falls back to the character. */
    if (hero_driven(p) == c) party_pass_the_torch(p);
    return true;
}

/* ---- what a role can do ----------------------------------------------
   A Marksman who has to walk into melee to fight is a Marksman in name
   only, so reach is where the archetype stops being a stat block and starts
   being behaviour. Melee roles get 1 -- adjacent, as before. */
static int archetype_reach(int archetype) {
    switch (archetype) {
        case ARCH_MARKSMAN:  return 6;
        case ARCH_ARCANIST:  return 5;
        case ARCH_ARTIFICER: return 3;   /* whatever it is, it is thrown */
        default:             return 1;
    }
}

/* How far this particular hire can actually reach, which is a question about
   what they are carrying rather than what role they are. A Marksman who
   somehow has no weapon has no business standing off at six squares, and the
   archetype figure is the fallback, not the answer. */
static int companion_reach(const Hero *c) {
    if (c->spell_count > 0) return archetype_reach(ARCH_ARCANIST);
    if (c->ranged_slot > 0) {
        const RangedTemplate *r = &RANGED_STOCK[c->ranged_slot - 1];
        bool thrown = (r->type == RANGED_THROWN || r->type == RANGED_GRENADE);
        return thrown ? 3 : 6;
    }
    return 1;
}

/* Firing from six squares away with no risk would make the Marksman strictly
   better than everyone; a shot every other turn is the same trade the
   player's own ranged weapons make. */
#define COMPANION_SHOT_COOLDOWN 2

static const char *hit_verb(int archetype) {
    switch (archetype) {
        case ARCH_ARCANIST:  return "sears";
        case ARCH_MARKSMAN:  return "puts a shot into";
        case ARCH_ARTIFICER: return "lobs something at";
        default:             return "hits";
    }
}

/* ---- temperament ------------------------------------------------------
   Bold, Steady and Cautious differ in three things: how far they will range
   from you, how far out they notice a fight, and -- the part that reads as
   character rather than as a number -- when they decide a fight is going
   badly enough to back out of. */
/* `sight` is how far they look for something to fight; `chase` is how far
   they will travel to reach it before losing interest and going back to
   exploring.
 *
   These used to be `sight` and `leash`, and the leash was a tether to the
   player: stray past it and the hire turned round and walked home. That made
   five hired adventurers into five escorts, all shuffling in the same
   corridor the player was standing in -- which is where most of the dancing
   came from, and it is a strange thing to buy for a hundred thousand gold.

   They are loose now. The personality is pure aggression: how far a hire
   ranges looking for trouble, and how much damage it takes before it stops
   looking. Bold covers ground and dies doing it; Cautious stays near what it
   has already cleared. Neither has any idea where the player is. */
static void personality_ranges(int personality, int *chase, int *sight) {
    switch (personality) {
        case PERSONALITY_BOLD:     *chase = 40; *sight = 12; break;
        case PERSONALITY_CAUTIOUS: *chase = 16; *sight =  5; break;
        default:                   *chase = 26; *sight =  8; break;
    }
}

/* `retreat_pct` is the health at which they break off and back away from
   whatever is hitting them; `engage_pct` is the health below which they stop
   starting fights but will still defend themselves. Bold does neither -- that
   is the whole point of Bold, and it is why Bold heroes are the ones who
   die. They back away from the threat, not toward the player: nobody is
   coming to help, and that is the deal. */
static void personality_nerve(int personality, int *retreat_pct, int *engage_pct) {
    switch (personality) {
        case PERSONALITY_BOLD:     *retreat_pct =  0; *engage_pct =  0; break;
        case PERSONALITY_CAUTIOUS: *retreat_pct = 40; *engage_pct = 55; break;
        default:                   *retreat_pct = 20; *engage_pct = 30; break;
    }
}

static int hp_pct(const Player *p, const Hero *c) {
    int max = companion_max_hp(p, c);
    return max > 0 ? c->hp * 100 / max : 0;
}

/* ---- what they find --------------------------------------------------
   Companions do not compete with the player for relics on the floor. That
   was the obvious way to do it and it is the wrong one: losing a gear-set
   piece to a hire you are paying reads as the game taking something from
   you, not as the hire being a real character. They earn their own kit off
   their own kills instead, which is also the only thing they do that the
   player cannot do for them. */
static const char *const GEAR_PREFIX[] = {
    "Etched", "Blackened", "Ether-touched", "Salvaged", "Fluted", "Hollow",
    "Verdigris", "Bone-inlaid", "Storm-cut", "Old Imperial",
};
static const char *const GEAR_NOUN[ARCHETYPE_COUNT][3] = {
    { "Pauldron",  "Greatshield", "Warplate"   },   /* Vanguard   */
    { "Stiletto",  "Half-cloak",  "Spurs"      },   /* Skirmisher */
    { "Longarm",   "Scope",       "Bandolier"  },   /* Marksman   */
    { "Focus",     "Sigil-ring",  "Grimoire"   },   /* Arcanist   */
    { "Toolrig",   "Charge-pack", "Bracer"     },   /* Artificer  */
    { "Talisman",  "Field-kit",   "Hide-wrap"  },   /* Survivor   */
    { "Signet",    "Gorget",      "Standard"   },   /* Envoy      */
};
#define GEAR_PREFIX_N ((int)(sizeof(GEAR_PREFIX) / sizeof(GEAR_PREFIX[0])))

/* Deep floors and hard kills turn up better things. Returns true if anything
   was found. */
static bool maybe_find_gear(Hero *c, const Monster *victim, int floor_num) {
    if (c->gear_count >= COMPANION_GEAR_SLOTS) return false;

    int chance = 2 + floor_num / 12;             /* 2% at the top, 10% at the bottom */
    if (victim->is_elite) chance *= 3;
    if (victim->is_boss)  chance *= 8;
    if (chance > 60) chance = 60;
    if (rand() % 100 >= chance) return false;

    /* Scaled off their own stats so a find is worth the same at level 40 as
       at level 4 -- the same reason the archetype tilt is a percentage. */
    CompanionGear *g = &c->gear[c->gear_count++];
    g->atk = c->base_atk / 8 + 1;
    g->def = c->base_def / 8 + 1;
    g->hp  = c->maxhp / 8 + 2;
    if (victim->is_boss) { g->atk *= 2; g->def *= 2; g->hp *= 2; }

    int arch = (c->archetype >= 0 && c->archetype < ARCHETYPE_COUNT) ? c->archetype : 0;
    snprintf(g->name, sizeof(g->name), "%s %s",
             GEAR_PREFIX[rand() % GEAR_PREFIX_N], GEAR_NOUN[arch][rand() % 3]);

    /* No inventory, no equip screen: it is theirs the moment they have it. */
    c->base_atk += g->atk;
    c->base_def += g->def;
    c->maxhp    += g->hp;
    c->hp       += g->hp;

    log_msg("%s digs a %s out of the mess and keeps it.", c->name, g->name);
    return true;
}

/* ---- their kit --------------------------------------------------------
   A hired caster carries real spells out of the same pool the guild sells
   from, and a hired shooter a real weapon out of RANGED_STOCK. What they do
   NOT get is the player's whole apparatus: no ammo economy (it would be a
   resource the player cannot manage on their behalf), and no terrain magic
   at all.

   That second exclusion is the important one. The pool contains spells that
   seal tiles into walls, conjure bridges, scorch ground into miasma, purge
   hazards, blink the caster somewhere random and open locked doors. Every
   one of those is fine in the player's hands and a menace in an autonomous
   ally's: a hire that walls off the corridor you were walking down, or
   burns the floor you are standing on, is not a companion, it is a hazard
   you paid for. Companions may only learn spells that affect an enemy or
   themselves. */
static bool effect_fit_for_companion(int effect) {
    switch (effect) {
        case EFFECT_DAMAGE:
        case EFFECT_DAMAGE_NOVA:
        case EFFECT_LIFE_DRAIN:
        case EFFECT_DOT_BURN:
        case EFFECT_STUN:
        case EFFECT_SLOW:
        case EFFECT_CURSE:
        case EFFECT_BUFF_ATK:
        case EFFECT_HEAL_SELF:
        case EFFECT_WARD_SHIELD:
            return true;
        /* SCORCH, BARRIER, PURGE, BLINK, REVEAL, BRIDGE, UNBIND: all reshape
           the floor or move the caster, and nobody is steering.

           The variety-pass effects are excluded for the same reason and one
           more. CHAIN and PARTY_HEAL would both be fine in a hire's hands, and
           are the obvious next thing to give them; HASTE, REFLECT, SENSE_LIFE
           and MASS_SLOW are all decisions about *when*, which is exactly what
           an autonomous caster is worst at -- companion_pick_spell() has no
           way to know that this is the turn worth spending them on, and a hire
           that cast Haste on an empty corridor would be the cursing bug (§104)
           wearing a new spell. Left out deliberately rather than by omission,
           which is why this comment names them. */
        default:
            return false;
    }
}

/* Does casting this actually reduce something's hit points?
 *
   This replaced an effect_is_offensive() that answered a different question --
   "is it aimed at somebody else" -- and counted stun, slow and curse among the
   offensive effects. That is a fair reading of the word and useless as a
   priority: a rule that said "prefer the offensive spell" preferred the curse
   right back, so a level-5 curse still beat a level-1 bolt and the hire still
   stood there doing nothing.

   Worth recording because the first fix looked obviously correct and changed
   nothing whatsoever. What found it was a print inside the loop -- the pick
   reported the curse as offensive, weight 1555 against the bolt's 1107 -- and
   nothing short of that would have, because both spells were being considered
   and the arithmetic was doing exactly what it said. */
static bool effect_deals_damage(int effect) {
    switch (effect) {
        case EFFECT_DAMAGE:
        case EFFECT_DAMAGE_NOVA:
        case EFFECT_LIFE_DRAIN:
        case EFFECT_DOT_BURN:
            return true;
        default:
            return false;
    }
}

/* Spells are dealt by level, scaled to who is casting them: a level-1 hire
   knows level-1 magic, and a hire taken on at level 40 arrives with the kind
   of spell you would have had to save up for. */
static int companion_spell_tier(int level) {
    int tier = 1 + level / 9;
    if (tier > 5) tier = 5;
    return tier;
}

static void give_spells(Hero *c, unsigned int h) {
    c->spell_count = 0;
    int school = c->magic_school;
    int n = spell_school_count(school);
    if (n <= 0) return;

    int top = companion_spell_tier(c->level);

    /* One spell per tier from the top down, so the rotation has a heavy
       option and a cheap one rather than three of the same weight. */
    for (int want = top; want >= 1 && c->spell_count < COMPANION_SPELL_SLOTS; want--) {
        int found = -1, seen = 0;
        for (int i = 0; i < n; i++) {
            int idx = spell_school_index(school, i);
            const SpellTemplate *t = spell_pool_get(idx);
            if (!t || t->level != want) continue;
            if (!effect_fit_for_companion(t->effect)) continue;
            /* Reservoir-pick, so the same tier does not always hand out the
               same spell to every hire of that school. */
            seen++;
            if ((int)((h >> (unsigned)(want * 3)) % (unsigned)seen) == 0) found = idx;
        }
        if (found >= 0) {
            bool dupe = false;
            for (int k = 0; k < c->spell_count; k++) if (c->known_spells[k] == found) dupe = true;
            if (!dupe) c->known_spells[c->spell_count++] = found;
        }
    }

    /* Enough charge to open with the heaviest thing they know and still have
       something left, or the spellbook is decoration. */
    int most = 0;
    for (int k = 0; k < c->spell_count; k++) {
        const SpellTemplate *t = spell_pool_get(c->known_spells[k]);
        if (t && t->charge_cost > most) most = t->charge_cost;
    }
    c->aether_max = most * 3 + 6;
    c->aether = c->aether_max;
}

static void give_ranged(Hero *c, unsigned int h, bool thrown_only) {
    c->ranged_slot = 0;
    if (RANGED_STOCK_COUNT <= 0) return;

    /* Tier by level, the same shape the armory prices in. */
    int tier = c->level >= 30 ? 2 : (c->level >= 12 ? 1 : 0);
    int best = -1, seen = 0;
    for (int i = 0; i < RANGED_STOCK_COUNT; i++) {
        const RangedTemplate *r = &RANGED_STOCK[i];
        bool is_thrown = (r->type == RANGED_THROWN || r->type == RANGED_GRENADE);
        if (thrown_only != is_thrown) continue;
        /* RANGED_STOCK runs three tiers per type, in ascending price. */
        if (i % 3 != tier) continue;
        seen++;
        if ((int)((h >> 9) % (unsigned)seen) == 0) best = i;
    }
    if (best < 0) best = 0;
    c->ranged_slot = best + 1;

    /* And actually equip it.
     *
       This used to set ranged_slot and stop. The AI reads RANGED_STOCK through
       that index, so a hire shot perfectly well -- but ranged_type stayed
       RANGED_NONE, and *that* is what fire_ranged, ranged_ready, the character
       sheet and the Armory all test. So a Marksman you took over was carrying
       a bow that the game would not let them fire: "You have no ranged weapon
       equipped", from a hero whose whole archetype is the bow.

       Two representations of "what I am shooting with", one for the AI and one
       for the player, and a body could only use the one it was born to. The
       slot stays as provenance -- which stock item it is -- and the real
       fields are filled from the same template. */
    const RangedTemplate *t = &RANGED_STOCK[best];
    int bonus = t->bonus + t->bonus * c->gear_bonus_pct / 100;
    equip_ranged(c, t->name, t->type, bonus, t->ammo_cost, t->cooldown);
}

/* What a hire brings to the fight beyond their own arms. Called once, when
   the roster candidate is built, so it is fixed at the moment you hire them
   -- the same rule as their level and their stats. */
static void companion_equip(Hero *c, unsigned int h) {
    /* Their class's own school, as hero_create derived it -- the same rule the
       character is assigned by, and it is *kept*.
     *
       This used to be blanked to -1 here and only restored for Arcanists, so
       every other hire came out of the Tavern as a "? caster". That was
       harmless while a hire was a follower who could never visit a shop. The
       moment a hire could be the one you are playing, it meant the Guild
       looked at whoever you had taken over and said "nothing to teach a ?
       caster" -- a body locked out of an entire system because of a sentinel
       written for a different design.

       Every class has a school. compute_magic_school() gives one even to
       classes with no magic attribute at all, spreading them by class_id
       precisely so that nobody is excluded. A hire is a class like any other,
       so a hire has a school like any other.

       What -1 was really being used for is "carries no spellbook", and that
       has its own field: spell_count, which is zero here until something puts
       spells in it. Every guard in the module already tests it. */
    int own_school = c->magic_school;
    if (own_school < 0 || own_school >= SCHOOL_COUNT) own_school = 0;
    c->magic_school = own_school;
    c->ranged_slot = 0;
    c->spell_count = 0;

    switch (c->archetype) {
        case ARCH_ARCANIST:
            /* Their own school first, then the rest. Not every school has
               anything a companion may cast: Aether-Sense is blink, reveal and
               conjured bridges end to end, all of which are barred above, and
               an Arcanist who ends up with an empty spellbook is a robed man
               with a stick. So the walk stays -- it is a fallback now rather
               than the whole selection.

               It used to pick by hash alone, because a hire had no class to
               ask. They do now, which is the point of there being one
               constructor: a Coil-Adept casts Conduit because that is what a
               Coil-Adept is, not because their name hashed to 0. */
            for (unsigned int try_n = 0; try_n < SCHOOL_COUNT; try_n++) {
                if (try_n == 0 && own_school >= 0 && own_school < SCHOOL_COUNT)
                    c->magic_school = own_school;
                else
                    c->magic_school = (int)((h + try_n) % SCHOOL_COUNT);
                give_spells(c, h + try_n * 7919u);
                if (c->spell_count > 0) break;
            }
            if (c->spell_count == 0) {
                /* Nothing in the whole pool suits them -- fall back to a
                   thrown weapon rather than to nothing. Their school goes back
                   to their class's own: they walk out with no spellbook, not
                   with no school, so the Guild will still teach them. */
                c->magic_school = own_school;
                give_ranged(c, h, true);
            }
            break;
        case ARCH_MARKSMAN:
            give_ranged(c, h, false);       /* a real weapon: bow, gun, laser */
            break;
        case ARCH_ARTIFICER:
            give_ranged(c, h, true);        /* thrown and grenades */
            break;
        default:
            break;                          /* the rest fight with their hands */
    }
}

/* ---- what their attacks look like -------------------------------------
   Companions were silent because five of them animating in sequence, every
   turn, made the game crawl. So they do not animate in sequence: every blow
   struck during one companion turn is recorded here and played as a single
   volley at the end of it, which costs the same whether you hired one hero
   or five. */
/* The party's animation buffer used to live here -- its own AnimShot array,
   its own add and reset. It is the renderer's now (anim_batch_begin/end), and
   that is what unblocked the AI calling cast_spell_slot() and fire_ranged():
   the batching was the only reason it could not, and while the batching was
   private to this file the only way to have it was to reimplement the spell
   and the weapon here. */
static void volley_add(int x0, int y0, int x1, int y1, int radius, chtype ch, int pair) {
    if (radius > 0)          anim_burst(NULL, NULL, x1, y1, radius, ch, pair);
    else if (x0 == x1 && y0 == y1) anim_flash_cell(NULL, NULL, x1, y1, ch, pair, 1);
    else                     anim_bolt(NULL, NULL, x0, y0, x1, y1, ch, pair);
}

/* One glyph and colour per role, so you can tell from across the room which
   of your heroes is shooting. Melee roles get a strike on the target rather
   than a travelling bolt -- there is nothing in flight to draw. */
static void archetype_shot_look(int archetype, chtype *ch, int *pair) {
    switch (archetype) {
        case ARCH_MARKSMAN:   *ch = '*'; *pair = CP_MON_OUTRIDER; break;
        case ARCH_ARCANIST:   *ch = '*'; *pair = CP_MAGIC;        break;
        case ARCH_ARTIFICER:  *ch = 'o'; *pair = CP_MACHINE;      break;
        case ARCH_SKIRMISHER: *ch = '/'; *pair = CP_HIT;          break;
        case ARCH_VANGUARD:   *ch = 'X'; *pair = CP_HIT;          break;
        case ARCH_SURVIVOR:   *ch = 'x'; *pair = CP_HIT;          break;
        default:              *ch = '+'; *pair = CP_HIT;          break;
    }
}

/* ---- signature moves --------------------------------------------------
   One per role. Nobody triggers these: a companion is not a unit you order
   around, so they fire themselves when the situation calls for it and the
   player reads about it afterwards. That is the whole design constraint --
   an ability the player has to activate would make these pets. */

#define COMPANION_ABILITY_CD 10

static const ClassSeed *companion_class(const Hero *c) {
    int id = (c->class_id >= 0 && c->class_id < NUM_CLASSES) ? c->class_id : 0;
    return &CLASS_TABLE[id];
}

const char *companion_class_name(const Hero *c)    { return companion_class(c)->name; }
const char *companion_class_tagline(const Hero *c) { return companion_class(c)->tagline; }
const char *companion_class_weapon(const Hero *c)  { return companion_class(c)->start_weapon_name; }

const char *companion_weapon_name(const Hero *c) {
    if (c->ranged_slot <= 0 || c->ranged_slot > RANGED_STOCK_COUNT) return NULL;
    return RANGED_STOCK[c->ranged_slot - 1].name;
}

const char *companion_spell_name(const Hero *c, int slot) {
    if (slot < 0 || slot >= c->spell_count) return NULL;
    const SpellTemplate *t = spell_pool_get(c->known_spells[slot]);
    return t ? t->name : NULL;
}

const char *companion_ability_name(int archetype) {
    switch (archetype) {
        case ARCH_VANGUARD:   return "Bulwark";
        case ARCH_SKIRMISHER: return "Flurry";
        case ARCH_MARKSMAN:   return "Called Shot";
        case ARCH_ARCANIST:   return "Detonation";
        case ARCH_ARTIFICER:  return "Charge Bomb";
        case ARCH_SURVIVOR:   return "Second Wind";
        case ARCH_ENVOY:      return "Rally";
        default:              return "--";
    }
}

const char *companion_ability_desc(int archetype) {
    switch (archetype) {
        case ARCH_VANGUARD:   return "Digs in: takes half damage for a few turns.";
        case ARCH_SKIRMISHER: return "Strikes twice in the time it takes to strike once.";
        case ARCH_MARKSMAN:   return "A shot that ignores armour entirely.";
        case ARCH_ARCANIST:   return "Detonates the air around its target.";
        case ARCH_ARTIFICER:  return "Throws something that scatters and stuns.";
        case ARCH_SURVIVOR:   return "Gets back up: heals a third of its health.";
        case ARCH_ENVOY:      return "Steadies the party and sharpens your next swings.";
        default:              return "";
    }
}

/* Damage everything alive within `radius` of (cx,cy), optionally stunning it.
   Used by the two roles whose move is an area effect. */
/* ---- one door for every blow an actor lands ---------------------------
 *
 * The AI had five separate ways to hurt a monster -- a strike, a spell, a
 * shot, a blast, an ability -- and not one of them went through
 * monster_take_damage(). Each did `mo->hp -= dmg` and its own small pile of
 * bookkeeping, so a monster killed by a hire produced:
 *
 *   no gold for the party, no bounty progress, no larder, no set-piece drop,
 *   and no boss kill.
 *
 * That last one ends runs. A hire landing the final blow on the Warden left it
 * dead on the floor with the game still going, because STATE_WIN is reached
 * only through the flag monster_take_damage sets and nothing else ever looks.
 * Measured on a probe: 3 of 3, every time.
 *
 * Returns true if this blow was the one that killed it. */
/* The Warden has been killed and the turn loop has not acted on it yet.
 *
   Turn-scoped, so it is a module flag rather than a field on Player: it is set
   and consumed inside the same turn, and run state that is saved should not
   include something that is never true at the moment a save is taken.

   It exists because a kill can be landed by any of the six now, and an actor
   taking its turn under AI has no GameState to write to -- so it leaves the
   news here and resolve_after_player_turn picks it up. Without it a hire could
   kill the Warden and the run would carry on with nothing left to fight. */
static bool g_boss_felled = false;

bool companions_boss_felled(void) {
    bool b = g_boss_felled;
    g_boss_felled = false;      /* read once; the caller acts on it */
    return b;
}

static bool companion_hurt(Player *p, Map *m, Hero *c, Monster *mo, int dmg) {
    if (!mo || !mo->alive) return false;
    bool boss = false;
    monster_take_damage(p, c, m, mo, dmg, &boss);
    if (boss) g_boss_felled = true;
    return !mo->alive;
}

static void companion_blast(Player *p, Map *m, Hero *c,
                            int cx, int cy, int radius, int dmg, int stun) {
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        int dx = mo->x - cx, dy = mo->y - cy;
        if (dx * dx + dy * dy > radius * radius) continue;
        if (stun > 0) mo->stun_turns_left += stun;
        if (companion_hurt(p, m, c, mo, dmg)) maybe_find_gear(c, mo, m->floor_num);
    }
}

/* ---- what they pick up ------------------------------------------------
   They walk the floor like a second player, so they walk over loot like one.
   Where it ends up is the part that is not symmetric, and deliberately so:
   you are the one paying for them.

   - Gold and relics go to the player. A hire pocketing your gear-set piece
     would read as the game taking something from you, not as them being a
     real character.
   - Healing draughts they keep and drink, because a companion who dies next
     to a potion it was carrying for you is just annoying.
   - Anything else -- buffs, recall charms, keys, quest items -- goes to the
     player too, because companions have no status or inventory model to
     spend it on, and silently destroying it would be worse than either. */

static bool companion_keeps_potion(Hero *c, const FloorItem *fi) {
    if (fi->heal <= 0) return false;
    if (c->potion_count >= COMPANION_POTION_SLOTS) return false;
    CompanionPotion *pot = &c->potions[c->potion_count++];
    snprintf(pot->name, sizeof(pot->name), "%s", fi->name);
    pot->heal = fi->heal;
    pot->heal_pct = 0;
    return true;
}

static void companion_loot_here(Player *p, Map *m, Hero *c) {
    for (int i = 0; i < m->item_count; i++) {
        FloorItem *fi = &m->items[i];
        if (fi->used || fi->x != c->x || fi->y != c->y) continue;
        fi->used = true;

        if (fi->is_gold) {
            int amount = fi->gold_amount;
            if (hero_driven(p)->gold_bonus_pct > 0)
                amount += amount * hero_driven(p)->gold_bonus_pct / 100;
            amount = player_gain_gold(p, amount);
            log_msg("%s finds %d gold and hands it over.", c->name, amount);
        } else if (fi->is_key) {
            p->keys++;
            log_msg("%s tosses you a brass key.", c->name);
        } else if (fi->is_quest_item) {
            p->quest_item_found = true;
            log_msg("%s turns up %s and passes it to you.", c->name, fi->name);
        } else if (companion_keeps_potion(c, fi)) {
            log_msg("%s pockets a %s.", c->name, fi->name);
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
            if (give_consumable(p, &t, 1))
                log_msg("%s brings you a %s.", c->name, fi->name);
        }
    }

    /* Relics are the player's, full stop -- the set bonus is built around
       the player's four gear slots and there is nowhere else for it to go. */
    for (int i = 0; i < m->feature_count; i++) {
        MapFeature *f = &m->features[i];
        if (f->type != FEATURE_RELIC || f->used) continue;
        if (f->x != c->x || f->y != c->y) continue;
        f->used = true;
        log_msg("%s prises something out of the wall and brings it to you.", c->name);
        apply_set_gear(hero_driven(p), f->relic_set_id, f->relic_kind);
    }
}

/* The nearest thing worth walking over to, on ground the party has actually
   revealed. Companions used to collect only what they happened to tread on,
   which over three hundred turns and five of them came to seven items out of
   twenty-nine -- they were not looting, they were tripping over things. The
   player walks to loot deliberately, so they do too. */
#define COMPANION_LOOT_RANGE 24

static bool companion_loot_target(const Map *m, const Hero *c, int *tx, int *ty) {
    int best = COMPANION_LOOT_RANGE * COMPANION_LOOT_RANGE + 1;
    bool found = false;

    for (int i = 0; i < m->item_count; i++) {
        const FloorItem *fi = &m->items[i];
        if (fi->used) continue;
        if (!m->tiles[fi->y][fi->x].seen) continue;
        int dx = fi->x - c->x, dy = fi->y - c->y;
        int d = dx * dx + dy * dy;
        if (d == 0 || d >= best) continue;
        best = d; *tx = fi->x; *ty = fi->y; found = true;
    }

    for (int i = 0; i < m->feature_count; i++) {
        const MapFeature *f = &m->features[i];
        if (f->type != FEATURE_RELIC || f->used) continue;
        if (!m->tiles[f->y][f->x].seen) continue;
        int dx = f->x - c->x, dy = f->y - c->y;
        int d = dx * dx + dy * dy;
        if (d == 0 || d >= best) continue;
        best = d; *tx = f->x; *ty = f->y; found = true;
    }

    return found;
}

/* Drink when it is actually going badly, not at the first scratch -- a
   companion that burns a draught on 90% health has wasted it. */
static bool companion_maybe_drink(const Player *p, Hero *c) {
    if (c->potion_count <= 0) return false;
    if (hp_pct(p, c) > 45) return false;

    CompanionPotion pot = c->potions[--c->potion_count];
    int heal = pot.heal + companion_max_hp(p, c) * pot.heal_pct / 100;
    c->hp += heal;
    if (c->hp > c->maxhp) c->hp = c->maxhp;
    log_msg("%s drinks a %s (+%d).", c->name, pot.name, heal);
    return true;
}

/* ---- moving ----------------------------------------------------------- */

static int sgn(int v) { return (v > 0) - (v < 0); }

static bool square_is_free(Player *p, Map *m, Hero *c, int nx, int ny) {
    if (nx == c->x && ny == c->y) return false;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return false;
    if (!is_walkable_monster(m, nx, ny)) return false;
    /* The stairs are the player's decision. A companion standing on them --
       let alone drifting down them -- is the party leaving without you. */
    if (m->tiles[ny][nx].type == TILE_STAIRS_DOWN) return false;
    if (m->tiles[ny][nx].type == TILE_STAIRS_UP) return false;
    if (nx == hero_driven(p)->x && ny == hero_driven(p)->y) return false;
    if (monster_at(m, nx, ny)) return false;
    if (companion_at(p, nx, ny)) return false;
    return true;
}

/* Step one square toward (tx,ty), by an actual shortest path. This used to
   be greedy -- step in roughly the right direction -- which looked like
   character right up until you watched one press into the outside of a
   corner for twenty turns because the target was directly through the wall.
   The greedy move is kept as the fallback for when no path exists at all
   (target sealed off, or a wander target inside rock), where doing something
   beats standing still. */
/* Would this step just undo the last one? */
static bool step_is_backtrack(const Hero *c, int nx, int ny) {
    return c->last_x >= 0 && nx == c->last_x && ny == c->last_y;
}

/* One actor per tile, and an ally in the way is somebody to squeeze past.
 *
   Returns true if (nx,ny) held a party member and the two swapped places. The
   rule lives here, in one named function, rather than inline in whoever
   happens to be moving -- there were three movers once and only some of them
   knew about it, which is how the character walked through hires while a
   driven hire was stopped dead by them.

   Swapping rather than refusing is the important half. The path searches only
   know about terrain, so any delegated walk will sooner or later pick a square
   an ally is standing on; a refusal there spends no turn, changes nothing, and
   reads to the stuck-detector as a walk that has stopped getting anywhere.
   Reported from play as "when two or more @ are nearby the autoexplore is
   stopping", which is precisely what it was. */
bool party_displace_into(Player *p, Hero *h, int nx, int ny) {
    for (int i = 0; i < MAX_PARTY; i++) {
        Hero *o = &p->party[i];
        if (o == h || !hero_is_up(o)) continue;
        if (o->x != nx || o->y != ny) continue;
        o->x = h->x; o->y = h->y;
        h->x = nx;   h->y = ny;
        h->still_turns = 0;
        o->still_turns = 0;
        o->last_x = o->last_y = -1;   /* their footing changed; the target is stale */
        return true;
    }
    return false;
}

static void companion_move_to(Player *p, Map *m, Hero *c, int nx, int ny) {
    if (party_displace_into(p, c, nx, ny)) { companion_loot_here(p, m, c); return; }
    c->last_x = c->x; c->last_y = c->y;
    c->x = nx; c->y = ny;
    companion_loot_here(p, m, c);
}

/* Returns whether the companion actually moved. Every caller cares: a branch
   that decides on an action and then silently fails to carry it out is how a
   hero ends up standing in one place for forty turns while the player walks
   off without them. Nothing in the turn below may end on a no-op. */
static bool step_toward(Player *p, Map *m, Hero *c, int tx, int ty) {
    /* Every candidate step, best first, then the same list again allowing the
       square we just came from.
     *
       Walking back onto the previous square is how a chase turns into a
       dance: the nearest monster flips between two candidates as they move,
       and the hire steps one square toward each in turn forever. Cutting the
       leash made this the dominant kind of movement -- measured at 39% of a
       five-hire party's moves -- because everything that used to be "walk
       home" is now "walk at something".

       It is a preference and not a prohibition. A corridor that genuinely
       requires going back must still be walkable, so the backtrack is tried
       on the second pass rather than refused outright. */
    int cand[4][2];
    int n = 0;

    int dx, dy;
    if (path_next_step(m, c->x, c->y, tx, ty, true, false, &dx, &dy)) {
        cand[n][0] = dx; cand[n][1] = dy; n++;
    }
    int gx = sgn(tx - c->x), gy = sgn(ty - c->y);
    int fallback[3][2] = { { gx, gy }, { gx, 0 }, { 0, gy } };
    for (int i = 0; i < 3 && n < 4; i++) {
        cand[n][0] = fallback[i][0]; cand[n][1] = fallback[i][1]; n++;
    }

    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            int nx = c->x + cand[i][0], ny = c->y + cand[i][1];
            if (!square_is_free(p, m, c, nx, ny)) continue;
            if (pass == 0 && step_is_backtrack(c, nx, ny)) continue;
            companion_move_to(p, m, c, nx, ny);
            return true;
        }
    }
    return false;
}

/* Away from (tx,ty) and, where possible, toward the player -- a companion
   backing out of a fight is heading for the party, not for a corner. */
static bool step_away_from(Player *p, Map *m, Hero *c, int tx, int ty) {
    int here = (c->x - tx) * (c->x - tx) + (c->y - ty) * (c->y - ty);
    int best_score = -1, bx = c->x, by = c->y;

    for (int d = 0; d < 8; d++) {
        int nx = c->x + PATH_DX[d], ny = c->y + PATH_DY[d];
        if (!square_is_free(p, m, c, nx, ny)) continue;
        int away = (nx - tx) * (nx - tx) + (ny - ty) * (ny - ty);
        if (away <= here) continue;       /* only moves that actually open distance */
        int hx = hero_driven_c(p)->x, hy = hero_driven_c(p)->y;
        int home = (nx - hx) * (nx - hx) + (ny - hy) * (ny - hy);

        /* Getting clear comes first and closing on the party is the
           tiebreak, not an equal term -- weighted evenly, a companion whose
           party happened to be standing behind the monster would retreat
           *into* it. */
        int score = away * 128 - home;
        if (score > best_score) { best_score = score; bx = nx; by = ny; }
    }

    if (bx == c->x && by == c->y) return false;
    companion_move_to(p, m, c, bx, by);
    return true;
}

static Monster *nearest_monster_to(Map *m, int x, int y, int radius) {
    Monster *best = NULL;
    int best_d = radius * radius + 1;
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        int dx = mo->x - x, dy = mo->y - y;
        int d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = mo; }
    }
    return best;
}

/* The nearest monster this companion can actually hit right now: inside
   their reach and in line of sight, so nobody shoots through a wall. */
static Monster *target_in_reach(Map *m, Hero *c) {
    int reach = companion_reach(c);
    Monster *best = NULL;
    int best_d = reach * reach + 1;
    for (int i = 0; i < m->monster_count; i++) {
        Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        int dx = mo->x - c->x, dy = mo->y - c->y;
        int d = dx * dx + dy * dy;
        if (d > reach * reach || d >= best_d) continue;
        if (d > 2 && !line_of_sight(m, c->x, c->y, mo->x, mo->y)) continue;
        best_d = d; best = mo;
    }
    return best;
}

/* Anyone who fell this turn goes back on the Tavern's books, where they can
   be taken on again at a reduced price, and their party slot frees up.

   This lives here rather than in the caller's turn resolver so that the
   module owns the whole lifecycle -- hire, fight, fall, return to the roster
   -- and so a test can drive it without standing up a game loop. */
/* Free the slot of anyone who fell, and mark their roster seat so the Tavern
   will take them back at the rehire price.
 *
   Public, and called from town as well as from the AI's turn. It used to run
   only at the end of companions_take_turn(), which is a *dungeon* function --
   so a hire who died on the last action before you took the stairs, or before
   a recall, kept their slot all the way home. In town that reads as: the party
   is full when one of them is a corpse, and the Tavern says "they already
   drink on your coin" about somebody who is dead. Reported from play as the
   Tavern's revive not working, and it was not the revive: the seat was never
   given up. */
void companions_reap_fallen(Player *p) {
    for (int i = 0; i < MAX_COMPANIONS; i++) {
        Hero *c = &p->party[(i) + 1];
        if (!c->in_use || c->alive) continue;
        /* A built companion has no roster seat to return to. Shifting by a
           negative index here would be undefined behaviour, not merely
           wrong. */
        if (c->roster_idx >= 0 && c->roster_idx < TAVERN_ROSTER) {
            p->tavern_hired_mask  &= ~(1u << c->roster_idx);
            p->tavern_fallen_mask |=  (1u << c->roster_idx);
        }
        c->in_use = false;
    }
}

/* Companions have no spellbook or quiver of their own -- their whole kit is
   one attack number. The log is where the archetype becomes visible, so an
   Arcanist reads as a caster instead of a swordsman with a different label. */
static const char *kill_verb(int archetype) {
    switch (archetype) {
        case ARCH_ARCANIST:   return "burns down";
        case ARCH_MARKSMAN:   return "drops";
        case ARCH_SKIRMISHER: return "opens up";
        case ARCH_ARTIFICER:  return "blows apart";
        case ARCH_SURVIVOR:   return "wears down";
        case ARCH_ENVOY:      return "talks past and finishes";
        default:              return "cuts down";
    }
}

/* ---- casting ----------------------------------------------------------
   A compact re-implementation of the effects a companion is allowed to have,
   rather than a call into cast_spell_slot -- that function reads and writes
   two dozen Player fields a Hero does not have, and reports its
   refusals to the message log, which an autonomous caster deciding fifty
   times a turn would flood.

   The numbers come from the same SpellTemplate the player would be casting,
   so a Conduit bolt hits for what a Conduit bolt hits for. */
/* Which spell, if any, this hire should cast right now. Pure: it decides and
   does nothing, so the decision can be asserted on without a floor, a fight or
   a turn loop -- the same split autoexplore.h made for the same reason, and
   after the same kind of defect. `can_reach` is the caller's answer to "is
   there something in front of me I can actually point this at". */
int companion_pick_spell(const Player *p, const Hero *c,
                         const Monster *target, bool can_reach) {
    if (!p || !c || c->magic_school < 0 || c->spell_count <= 0) return -1;

    int best = -1, best_weight = -1;
    for (int i = 0; i < c->spell_count; i++) {
        if (c->spell_cd[i] > 0) continue;
        const SpellTemplate *t = spell_pool_get(c->known_spells[i]);
        if (!t || t->charge_cost > c->aether) continue;

        /* Written as a switch with no catch-all, because the catch-all is
           what broke it.
         *
           The last branch of this test was an `else` commented
           EFFECT_WARD_SHIELD, and it was not one: it swallowed STUN, SLOW and
           CURSE as well, all three of which are fit for a companion to know.
           Their usefulness therefore read `def_buff_turns == 0`, a field none
           of them touches, so it was true every turn forever. A hire holding a
           curse would cast it, deal nothing, find the same condition still
           true next turn and cast it again -- and `weight` below is driven by
           spell *level*, so a level-5 curse outranked a level-2 bolt and it
           was the *preferred* move. Reported from play as an AI that stands
           still for minutes casting the same spell for no damage, which is
           exactly what it was doing.

           Every effect states its own condition now, and anything unlisted is
           not useful rather than silently useful. */
        bool useful;
        switch (t->effect) {
            case EFFECT_DAMAGE:
            case EFFECT_DAMAGE_NOVA:
            case EFFECT_LIFE_DRAIN:
            case EFFECT_DOT_BURN:
                useful = can_reach;
                break;
            case EFFECT_HEAL_SELF:
                useful = hp_pct(p, c) < 60;
                break;
            case EFFECT_BUFF_ATK:
                useful = (c->atk_buff_turns == 0) && target != NULL;
                break;
            case EFFECT_WARD_SHIELD:
                useful = (c->def_buff_turns == 0) && target != NULL;
                break;
            /* Control: worth one cast, and not again until it has worn off.
               Each asks about the state it actually sets, which is the whole
               of the bug above. */
            case EFFECT_STUN:
                useful = can_reach && target->stun_turns_left == 0;
                break;
            case EFFECT_SLOW:
                useful = can_reach && target->slow_turns_left == 0;
                break;
            case EFFECT_CURSE:
                useful = can_reach && target->jinx_turns_left == 0;
                break;
            default:
                useful = false;
                break;
        }
        if (!useful) continue;

        /* Level and magnitude decide *which* spell of a kind, and the kind
           decides first. Without the bands, a fight is chosen by whatever
           happens to be the highest level in the book -- which is how holding
           a good curse made a hire stop killing things.

           Staying up, then ending the fight, then making it easier. */
        int weight = t->level * 100 + t->magnitude;
        if (t->effect == EFFECT_HEAL_SELF)        weight += 2000;
        else if (effect_deals_damage(t->effect))  weight += 1000;
        if (weight > best_weight) { best_weight = weight; best = i; }
    }
    return best;
}

static bool companion_cast(Player *p, Map *m, Hero *c, Monster *target) {
    if (c->magic_school < 0 || c->spell_count <= 0) return false;

    int dist2 = target ? (target->x - c->x) * (target->x - c->x)
                       + (target->y - c->y) * (target->y - c->y) : 0;
    int reach = companion_reach(c);
    bool can_reach = target && dist2 <= reach * reach
                     && line_of_sight(m, c->x, c->y, target->x, target->y);

    if (!proving_allows(m, c->x, c->y, PROVE_ARCANE_ACT)) return false;

    int best = companion_pick_spell(p, c, target, can_reach);
    if (best < 0) return false;

    const SpellTemplate *t = spell_pool_get(c->known_spells[best]);
    c->aether -= t->charge_cost;
    c->spell_cd[best] = t->cooldown;

    /* Scale with the caster, the way the player's spell power does -- a
       level-40 hire casting a level-2 spell should not be casting it for
       level-2 numbers. */
    int power = t->magnitude + (c->base_atk + party_atk_bonus(p)) / 3;
    chtype glyph = school_glyph(c->magic_school);
    int pair = school_color_pair(c->magic_school);

    switch (t->effect) {
        case EFFECT_DAMAGE:
            volley_add(c->x, c->y, target->x, target->y, 0, glyph, pair);
            companion_hurt(p, m, c, target, power);
            break;

        case EFFECT_DAMAGE_NOVA:
            volley_add(c->x, c->y, target->x, target->y, 2, glyph, pair);
            companion_blast(p, m, c, target->x, target->y, 2, power, 0);
            break;

        case EFFECT_LIFE_DRAIN:
            volley_add(c->x, c->y, target->x, target->y, 0, glyph, pair);
            companion_hurt(p, m, c, target, power);
            c->hp += power / 2;
            if (c->hp > companion_max_hp(p, c)) c->hp = companion_max_hp(p, c);
            break;

        case EFFECT_DOT_BURN:
            volley_add(c->x, c->y, target->x, target->y, 0, glyph, pair);
            companion_hurt(p, m, c, target, power);
            target->jinx_atk_penalty += t->magnitude / 3 + 1;
            target->jinx_turns_left = t->duration;
            break;

        case EFFECT_STUN:
            volley_add(c->x, c->y, target->x, target->y, 0, glyph, pair);
            target->stun_turns_left += t->duration;
            break;

        case EFFECT_SLOW:
            volley_add(c->x, c->y, target->x, target->y, 0, glyph, pair);
            target->slow_turns_left += t->duration;
            break;

        case EFFECT_CURSE:
            volley_add(c->x, c->y, target->x, target->y, 0, glyph, pair);
            target->jinx_atk_penalty += t->magnitude;
            target->jinx_turns_left = t->duration;
            break;

        case EFFECT_HEAL_SELF:
            volley_add(c->x, c->y, c->x, c->y, 1, glyph, pair);
            c->hp += power;
            if (c->hp > companion_max_hp(p, c)) c->hp = companion_max_hp(p, c);
            break;

        case EFFECT_BUFF_ATK:
            volley_add(c->x, c->y, c->x, c->y, 1, glyph, pair);
            c->atk_buff = t->magnitude;
            c->atk_buff_turns = t->duration;
            break;

        case EFFECT_WARD_SHIELD:
            volley_add(c->x, c->y, c->x, c->y, 1, glyph, pair);
            c->def_buff = t->magnitude;
            c->def_buff_turns = t->duration;
            break;

        default:
            return false;       /* effect_fit_for_companion should have caught it */
    }

    log_msg("%s casts %s.", c->name, t->name);

    /* The kill itself is companion_hurt's business now -- the tally, the
       experience and the world's half all happen in there. What is left here
       is the line the player reads. */
    if (target && !target->alive) {
        log_msg("%s %s the %s.", c->name, kill_verb(c->archetype), target->name);
        maybe_find_gear(c, target, m->floor_num);
    }
    return true;
}

/* ---- shooting ---------------------------------------------------------
   Same idea: the weapon's own damage and cooldown out of RANGED_STOCK, with
   the type deciding what it does on arrival. No ammo -- the player's ammo
   bar is a resource the player manages, and a companion's would be one
   nobody could. The cooldown is what rations it instead. */
static bool companion_fire(Player *p, Map *m, Hero *c, Monster *target) {
    /* A hire under AI obeys the proving ground too. A rule the player keeps
       and their party does not is not a rule, it is a handicap -- and it would
       read as the game cheating, which dda.h already says players are right to
       resent. */
    if (!proving_allows(m, c->x, c->y, PROVE_RANGED_ACT)) return false;
    if (c->ranged_slot <= 0 || !target) return false;
    if (c->ranged_cooldown > 0) return false;

    const RangedTemplate *r = &RANGED_STOCK[c->ranged_slot - 1];
    int dx = target->x - c->x, dy = target->y - c->y;
    int reach = companion_reach(c);
    if (dx * dx + dy * dy > reach * reach) return false;
    if (!line_of_sight(m, c->x, c->y, target->x, target->y)) return false;

    c->ranged_cooldown = r->cooldown;
    int dmg = r->bonus + (c->base_atk + party_atk_bonus(p)) / 3;

    chtype ch; int pair;
    archetype_shot_look(c->archetype, &ch, &pair);

    switch (r->type) {
        case RANGED_GRENADE:
            volley_add(c->x, c->y, target->x, target->y, 2, '#', pair);
            companion_blast(p, m, c, target->x, target->y, 2, dmg * 2 / 3, 0);
            break;
        case RANGED_BLOWGUN:
            volley_add(c->x, c->y, target->x, target->y, 0, ch, pair);
            target->slow_turns_left += 3;      /* it is the point of a blowgun */
            companion_hurt(p, m, c, target, dmg);
            break;
        default:
            volley_add(c->x, c->y, target->x, target->y, 0, ch, pair);
            companion_hurt(p, m, c, target, dmg);
            break;
    }

    log_msg("%s looses a shot from the %s.", c->name, r->name);

    if (!target->alive) {
        log_msg("%s %s the %s.", c->name, kill_verb(c->archetype), target->name);
        maybe_find_gear(c, target, m->floor_num);
    }
    return true;
}

/* One attack, melee or at range. Anything adjacent is a plain melee swing
   with no cooldown, whatever the role carries -- a Marksman with something
   in its face does not stand there working the bolt, it hits the thing.
   Only a shot taken at distance is rationed. Returns true if the turn was
   spent. */
static bool companion_has_kit(const Hero *c) {
    return c->spell_count > 0 || c->ranged_slot > 0;
}

static bool companion_strike(Player *p, Map *m, Hero *c, Monster *mo) {
    if (!proving_allows(m, c->x, c->y, PROVE_MELEE_ACT)) return false;
    int dx = mo->x - c->x, dy = mo->y - c->y;
    bool at_range = (dx * dx + dy * dy) > 2;

    if (at_range) {
        /* Anyone carrying a spellbook or a weapon reaches through that, and
           it has its own cooldown. Letting them also take a generic ranged
           swing here gave shooters two independent cooldowns and, between
           them, a shot every turn -- which is exactly the thing the weapon's
           cooldown exists to prevent. */
        if (companion_has_kit(c)) return false;
        if (c->shot_cd > 0) return false;
        c->shot_cd = COMPANION_SHOT_COOLDOWN;
    }

    chtype ch; int pair;
    archetype_shot_look(c->archetype, &ch, &pair);
    if (at_range) volley_add(c->x, c->y, mo->x, mo->y, 0, ch, pair);
    else          volley_add(mo->x, mo->y, mo->x, mo->y, 0, ch, pair);

    /* The same roll the player's swing uses, so a hire's weapon, set pieces,
       stance, crit and -- the reason this changed -- their ring and trinket
       all count for exactly what they count for on anybody else. The smith's
       party bonus is still added on top, since that is the party's work
       rather than the body's. */
    bool crit = false;
    int dmg = hero_damage_with_procs(p, c, m, mo, &crit) + party_atk_bonus(p);

    /* Through the same door the player's blow goes through.
     *
       This used to be `mo->hp -= dmg` and its own little pile of bookkeeping,
       which meant a monster killed by a hire produced no gold for the party,
       no bounty progress, no larder -- and no boss kill. A hire landing the
       last blow on the Warden left it dead on the floor with the run still
       running, because STATE_WIN is only ever reached through the flag this
       function sets and nothing else looks. All three measured on a probe
       before this changed.

       The kill and the experience are credited to `c`, because those belong to
       the body; everything else belongs to the party regardless of whose arm
       it was. */
    if (companion_hurt(p, m, c, mo, dmg)) {
        log_msg("%s %s the %s.", c->name, kill_verb(c->archetype), mo->name);
        maybe_find_gear(c, mo, m->floor_num);
    } else if (at_range) {
        log_msg("%s %s the %s from cover.", c->name, hit_verb(c->archetype), mo->name);
    }
    return true;
}

/* Fires the role's signature move if it is off cooldown and this is a moment
   where it helps. Returns true if it went off and the turn is spent.

   `target` may be NULL -- the self-preservation moves (Second Wind, Rally)
   are worth using with nothing in reach; the offensive ones are not. */
static bool companion_try_ability(Player *p, Map *m, Hero *c, Monster *target) {
    if (c->ability_cd > 0) return false;

    int health = hp_pct(p, c);

    switch (c->archetype) {
        case ARCH_SURVIVOR:
            /* Only when it would actually save them. */
            if (health > 45) return false;
            c->hp += companion_max_hp(p, c) / 3;
            if (c->hp > companion_max_hp(p, c)) c->hp = companion_max_hp(p, c);
            volley_add(c->x, c->y, c->x, c->y, 1, '+', CP_HP_OK);
            log_msg("%s gets back up -- Second Wind.", c->name);
            break;

        case ARCH_VANGUARD:
            /* Worth spending only with something already swinging at them. */
            if (!target || c->guard_turns > 0) return false;
            c->guard_turns = 6;
            volley_add(c->x, c->y, c->x, c->y, 1, '#', CP_HP_OK);
            log_msg("%s plants itself and digs in -- Bulwark.", c->name);
            break;

        case ARCH_ENVOY: {
            /* Rally is for the party, which is the only reason to bring one:
               steadies the other hires and sharpens the player's next swings. */
            const Hero *led = hero_driven_c(p);
            int hurt = (led->maxhp > 0 && led->hp * 100 / led->maxhp < 70) ? 1 : 0;
            for (int i = 0; i < MAX_COMPANIONS; i++) {
                const Hero *o = &p->party[(i) + 1];
                if (o->in_use && o->alive && o != c && hp_pct(p, o) < 70) hurt++;
            }
            if (hurt < 1) return false;
            /* Every actor, including whoever is being played. A rally that
               healed the five hires and not the body you were fighting with
               was the same slot-0 premise wearing a friendlier face. */
            for (int i = 0; i < MAX_PARTY; i++) {
                Hero *o = &p->party[i];
                if (!hero_is_up(o)) continue;
                int cap = companion_max_hp(p, o);
                o->hp += cap / 6;
                if (o->hp > cap) o->hp = cap;
            }
            /* The one being played gets the shout, not slot 0. */
            Hero *shouted_at = hero_driven(p);
            shouted_at->atk_buff += 4;
            if (shouted_at->atk_buff_turns < 8) shouted_at->atk_buff_turns = 8;
            volley_add(c->x, c->y, c->x, c->y, 3, '\'', CP_GOLD);
            log_msg("%s calls the party back together -- Rally.", c->name);
            break;
        }

        case ARCH_SKIRMISHER:
            if (!target) return false;
            log_msg("%s goes in twice -- Flurry.", c->name);
            companion_strike(p, m, c, target);
            if (target->alive) companion_strike(p, m, c, target);
            break;

        case ARCH_MARKSMAN: {
            if (!target) return false;
            /* A heavier round than the ordinary shot, and it reads as one. */
            volley_add(c->x, c->y, target->x, target->y, 0, '=', CP_UI_WARN);
            /* Straight through armour -- the one thing a plain shot can't do. */
            int atk = c->base_atk + party_atk_bonus(p);
            int dmg = atk + atk / 2;
            log_msg("%s takes its time -- Called Shot, %d through the plate.", c->name, dmg);
            if (companion_hurt(p, m, c, target, dmg))
                maybe_find_gear(c, target, m->floor_num);
            break;
        }

        case ARCH_ARCANIST:
            if (!target) return false;
            log_msg("%s tears the air open -- Detonation.", c->name);
            volley_add(c->x, c->y, target->x, target->y, 2, '*', CP_MAGIC);
            companion_blast(p, m, c, target->x, target->y, 2, c->base_atk + party_atk_bonus(p), 0);
            break;

        case ARCH_ARTIFICER:
            if (!target) return false;
            log_msg("%s lobs a charge -- it scatters.", c->name);
            volley_add(c->x, c->y, target->x, target->y, 2, '#', CP_MACHINE);
            companion_blast(p, m, c, target->x, target->y, 2, (c->base_atk + party_atk_bonus(p)) * 2 / 3, 2);
            break;

        default:
            return false;
    }

    c->ability_cd = COMPANION_ABILITY_CD;
    return true;
}

/* ---- one tick, for one actor ------------------------------------------
 *
 * Time passing, for exactly one body, exactly once per turn. Every actor gets
 * this and only this -- there is no second version for the character.
 *
 * There were two. The character's lived in main.c as six separate calls
 * (tick_buffs, tick_status, tick_regen, tick_spells, tick_ranged,
 * tick_ability) and the hires' lived inline in the loop below, and they did
 * not agree: only the character's ticked poison and regeneration, only the
 * hires' ticked the firing pace and the Vanguard's guard. So a driven hire
 * never regenerated and an undriven character never reloaded, and both were
 * invisible until somebody played long enough to notice.
 *
 * Ticking is something that happens *to* a body. Deciding is something done
 * *with* one. Only the second has an owner, which is why this runs for all six
 * and the AI below runs for five.
 */
void hero_tick(Player *p, Hero *h, Map *m) {
    (void)m;
    if (!hero_is_up(h)) return;

    if (h->shot_cd > 0)         h->shot_cd--;
    if (h->ability_cd > 0)      h->ability_cd--;
    if (h->guard_turns > 0)     h->guard_turns--;
    if (h->ranged_cooldown > 0) h->ranged_cooldown--;
    /* Warding's reflect. Ticked here with the rest of the timers rather than
       anywhere clever: this is the one place a body's clock advances, and it
       runs for all six -- which is exactly what §92's driven-hire bug was
       about. */
    if (h->reflect_turns > 0 && --h->reflect_turns == 0) h->reflect_pct = 0;
    for (int k = 0; k < h->spell_count; k++)
        if (h->spell_cd[k] > 0) h->spell_cd[k]--;

    /* Ammo regenerates on the party's clock rather than a field per body --
       see RANGED_AMMO_REGEN_TURNS. The Works give a little back on top, for a
       run that is going badly: it is the band where a caster first runs dry
       and a shooter first runs empty. See bands.h. */
    int recharge = band_recharge_bonus(p);
    if (p->turns % RANGED_AMMO_REGEN_TURNS == 0)
        h->ranged_ammo += 1 + recharge;
    if (h->ranged_ammo > h->ranged_ammo_max) h->ranged_ammo = h->ranged_ammo_max;

    int aeth_max = companion_max_aether(p, h);
    h->aether += 1 + recharge;
    if (h->aether > aeth_max) h->aether = aeth_max;

    hero_tick_buffs(p, h);
    hero_tick_status(p, h);
    hero_tick_regen(p, h);
}

/* Every actor the human is not attached to, taking its own turn.
 *
   The loop runs over all six slots, not five. Slot 0 -- the character the run
   was started as -- is an actor like any other the moment somebody else holds
   the role, and it now thinks with the same head as the rest.

   It used to have its own function, mc_take_turn(), whose last branch was
   "otherwise keep up with whoever is being driven". That is the dog: the
   character pathing to your feet every single turn, standing next to you,
   drawn in the player's own colour. It was written when the character was the
   only thing that could be undriven and "the party" meant "near the player",
   and it is exactly the premise this refactor exists to delete. Hires never
   had a leash -- it was removed deliberately, and the comment where step 5
   used to be says why: they are adventurers with their own opinions, not an
   escort. The character gets the same deal.

   Nothing is left that follows anybody. */
/* Time passing, for all six, wherever they are.
 *
   Deliberately its own pass rather than the first half of the AI loop below:
   that loop returns early in town, and hanging the tick off it meant a buff
   taken downstairs never expired while you shopped. Ticking and deciding run
   on different schedules -- everybody ticks, everywhere; only the five the
   human is not attached to decide, and only underground. */
void party_tick(Player *p, Map *m) {
    for (int i = 0; i < MAX_PARTY; i++) hero_tick(p, &p->party[i], m);
}

void companions_take_turn(Player *p, Map *m) {
    anim_batch_begin();

    /* Nobody walks around town. They meet you at the temple mouth. Letting
       them loose up here meant one of them parking on a shop door, where the
       '@' hid the building glyph, and it also gave them a town's worth of
       nothing to explore. There is no upside to model. */
    if (m->floor_num == 0) return;

    for (int i = 0; i < MAX_PARTY; i++) {
        Hero *c = &p->party[i];
        if (!hero_is_up(c)) continue;

        /* Not the one the player is driving. Without this the AI took a turn
           for the body the player was steering -- so every keypress was
           answered by the hire walking somewhere else of its own accord,
           which plays exactly as "my keys move me in the wrong direction"
           and "I am surrounded by invisible walls". The player's input and
           the AI were both moving the same actor, and the AI moved last.

           Asked as `is this the driven body`, not as `is this index the one
           controlled names`. Those are the same thing right up until they are
           not: `controlled` can point at a body that has stopped standing --
           a hire you were driving when it went down, in the window before the
           torch is passed -- and `hero_driven()` documents a fallback to slot
           0 for exactly that. Every other consumer takes the fallback. This
           one compared the raw index, so with `controlled` at 1 and slot 1
           down, the loop skipped the corpse and ran the AI over slot 0, which
           is the body the human was actually driving.

           It plays as auto-explore going nowhere: the walk steps you one
           square, the AI walks the same body back, and the position is
           identical turn after turn until the no-progress detector gives up.
           Sixty iterations of that, 35 to 71 times per 14,000 fuzzer keys.
           The bug this comment was written about, returning through the one
           path that was not looking at the body. */
        if (c == hero_driven(p)) continue;

        /* 0. Look after yourself first. A draught in a dead companion's
              pocket helps nobody, and the moves that fire with no target --
              Second Wind, Rally -- have to get their look in before the
              retreat branch below, or a hero who is hurt enough to use them
              is always already running. */
        if (companion_maybe_drink(p, c)) continue;
        if (companion_try_ability(p, m, c, NULL)) continue;

        int chase, sight, retreat_pct, engage_pct;
        personality_ranges(c->personality, &chase, &sight);
        personality_nerve(c->personality, &retreat_pct, &engage_pct);
        int health = hp_pct(p, c);

        /* Every branch below is "try this; if it turns into nothing, fall
           through to the next". An earlier version used `continue` on
           intent rather than on outcome, and a Marksman that had a monster
           inside its reach but behind a wall could neither shoot it nor
           decide to move -- measured at forty-one consecutive turns rooted
           to one square while the player walked away. */

        /* 1. Hurt badly enough to want out. A Bold hero never is, which is
              why Bold heroes are the ones who die. */
        if (health <= retreat_pct) {
            Monster *threat = nearest_monster_to(m, c->x, c->y, sight);
            if (threat) {
                /* Still shoot on the way out if they can do it for free. */
                /* A parting shot on the way out, through whatever they
                   actually carry. */
                Monster *shot = target_in_reach(m, c);
                if (shot && companion_reach(c) > 1) {
                    if (companion_cast(p, m, c, shot)) continue;
                    if (companion_fire(p, m, c, shot)) continue;
                    if (c->shot_cd == 0 && companion_strike(p, m, c, shot)) continue;
                }
                if (step_away_from(p, m, c, threat->x, threat->y)) continue;
                /* Cornered: nowhere to back into. Fight rather than freeze. */
                if (shot && companion_strike(p, m, c, shot)) continue;
            }
        }

        /* 2. Something they can hit from where they stand. For a melee hero
              that means adjacent; for a Marksman it can be most of a room.
              The signature move gets first refusal -- it is the interesting
              thing they do, and holding it back for a better moment they
              cannot evaluate would just mean never using it. */
        Monster *target = target_in_reach(m, c);
        if (companion_try_ability(p, m, c, target)) continue;
        /* Their own kit before their fists: a caster who had a spell ready
           and punched instead was the whole complaint. */
        if (companion_cast(p, m, c, target)) continue;
        if (companion_fire(p, m, c, target)) continue;
        if (target && companion_strike(p, m, c, target)) continue;

        /* 3. Something worth walking to -- unless they are too hurt to want
              to start anything.
         *
              `sight` is how far they notice; `chase` is how far they will go.
              With no leash to the player, this is the only thing bounding how
              far a hire will be drawn off across a floor by one monster after
              another, and it is what makes Bold and Cautious feel different
              now that neither of them is following anybody. */
        if (health >= engage_pct) {
            int look = sight < chase ? sight : chase;
            Monster *seen = nearest_monster_to(m, c->x, c->y, look);
            if (seen) {
                int reach = companion_reach(c);
                int dx0 = seen->x - c->x, dy0 = seen->y - c->y;
                bool in_reach = dx0 * dx0 + dy0 * dy0 <= reach * reach;
                bool can_see  = line_of_sight(m, c->x, c->y, seen->x, seen->y);

                /* Out of reach, behind a wall, or reloading: in all three
                   cases, close. The version that held position while the
                   shot came back up looked exactly like a bug from the
                   player's seat -- a hero rooted to one square with a
                   monster in plain view -- and a person with an empty gun
                   and something coming at them closes and swings. Melee is
                   never on cooldown, so closing always ends in an attack. */
                if ((!in_reach || !can_see || c->shot_cd > 0)
                    && step_toward(p, m, c, seen->x, seen->y))
                    continue;
            }
        }

        /* 4. Loot in sight. They pick things up the way the player does --
              by going and getting them, not by happening to tread on them.
              Gold and relics end up in the player's hands either way. */
        int lx, ly;
        if (companion_loot_target(m, c, &lx, &ly) && step_toward(p, m, c, lx, ly))
            continue;

        /* 5. (There is no step 5 any more.)

              This was the leash: past a certain distance from the player, turn
              round and walk back. It is gone. A hire goes where the floor
              takes it, and if that is the far corner while the player is on
              the stairs, so be it -- they are adventurers with their own
              opinions, not an escort. `heading_back` is dead with it. */

        /* 6. Explore, the same way auto-explore does -- walk to the edge of
              what the party has revealed. They share the player's map, so
              this is real scouting rather than a private walk, and because
              the search never looks past the fog they cannot shortcut their
              way anywhere. They are not looking for the stairs; that is the
              player's decision, not theirs. */
        /* 6. Scout -- but commit to a destination.
         *
              Re-picking the nearest frontier every turn is what made a hire
              orbit a junction: two candidates trade places as it moves
              between them. A per-tick position trace caught one running a
              clean ten-tile loop for hundreds of ticks, which the two-cycle
              detector used before reported as perfectly healthy. Pick a
              frontier tile, walk to it, and only re-pick when it is reached,
              stops being a frontier, or the walk has gone on long enough
              that something has plainly changed. */
        if (c->scout_x >= 0) {
            bool arrived = (c->x == c->scout_x && c->y == c->scout_y);
            bool stale   = (++c->scout_turns > COMPANION_SCOUT_PATIENCE);
            bool gone    = !m->tiles[c->scout_y][c->scout_x].seen;
            if (arrived || stale || gone) { c->scout_x = c->scout_y = -1; c->scout_turns = 0; }
        }
        if (c->scout_x < 0) {
            int tx, ty;
            if (path_frontier_target(m, c->x, c->y, true, &tx, &ty)) {
                c->scout_x = tx; c->scout_y = ty; c->scout_turns = 0;
            }
        }

        if (c->scout_x >= 0) {
            if (step_toward(p, m, c, c->scout_x, c->scout_y)) continue;
            /* Blocked, almost always by another hire in the same corridor.
               Waiting is invisible; shuffling is not. */
            continue;
        }

        /* 7. Nothing left to scout, or the frontier is walled off from where
              they stand.
         *
              This used to pick a random square within three and step toward
              it, "drift rather than freeze". A random walk returns to the
              square it just left about a quarter of the time, so the party
              spent the back half of every cleared floor visibly jittering on
              the spot -- and because step 6 fell through to here whenever a
              hire was blocked, they jittered in the *first* half too.

              Nothing to scout and nothing to fight: hold position. Walking
              back to the player was the last piece of the escort behaviour
              and it went with the leash. A hire standing still in a cleared
              corner is what an adventurer with nothing left to do looks
              like. */
    }

    companions_reap_fallen(p);

    /* One animation for the whole party, after everyone has acted. Drawing
       it here rather than inside each strike is the difference between five
       animations a turn and one -- and anim_volley drops anything the player
       could not see before it costs a frame, so a party fighting across the
       floor from you is still free. */
    anim_batch_end(m, p);
}

/* ---- the party will not leave you ------------------------------------- */

bool companions_attempt_rescue(Player *p, Map *m) {
    if (!p || !m) return false;
    if (m->floor_num <= 0) return false;          /* nobody dies in town */
    if (count_recall_charms(p) <= 0) return false;

    /* Nearest hire still standing. Nearest rather than any, because the one
       who reaches you is the one the message names, and a rescue narrated by
       somebody on the far side of the floor reads as a bug. */
    /* Whoever went down -- the body being played, not slot 0. Everything below
       measures from them and puts *them* back on their feet. */
    Hero *fallen = hero_driven(p);

    int best = -1;
    long best_d2 = 0;
    for (int i = 0; i < MAX_PARTY; i++) {
        const Hero *c = &p->party[i];
        if (c == fallen || !hero_is_up(c)) continue;
        long dx = c->x - fallen->x, dy = c->y - fallen->y;
        long d2 = dx * dx + dy * dy;
        if (best < 0 || d2 < best_d2) { best = i; best_d2 = d2; }
    }
    if (best < 0) return false;

    if (!consume_recall_charm(p)) return false;

    /* They cross to you. Placed adjacent rather than on top: two actors on
       one tile is the invariant the whole combat system assumes. */
    Hero *rescuer = &p->party[best];
    for (int i = 0; i < 8; i++) {
        int nx = fallen->x + PATH_DX[i], ny = fallen->y + PATH_DY[i];
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
        if (!is_walkable_player(m, nx, ny)) continue;
        if (monster_at(m, nx, ny)) continue;
        rescuer->x = nx; rescuer->y = ny;
        break;
    }

    /* Whoever fell is the one picked up. This read slot 0, so a hire you were
       driving could go down and the *character* would stand back up -- the
       charm spent, the run continuing, and the body you were playing still on
       the floor. */
    fallen->hp = 1;
    fallen->alive = true;

    /* The charm's own channel, already written and already handled by the
       temple loop -- one turn of it, because the hard part is over and the
       player did not choose to start it. */
    if (fallen->recall_channel_left < 1) fallen->recall_channel_left = 1;

    log_msg("You go down. %s reaches you first.", rescuer->name);
    log_msg("A charm breaks over you -- \"Not here. Not today.\"");
    return true;
}

/* The second mover used to stand here: companion_player_move(), a driven
   hire's step, which could walk and swing and nothing else. It is gone.
   There is one movement function -- actor_step() in main.c -- and all six
   actors go through it, so a hire you take over walks the same ground under
   the same rules as anybody else. */

/* mc_take_turn() used to stand here: a second AI, for the character alone,
   whose final branch was "otherwise keep up with whoever is being driven".

   It is gone, and so is the following. The character is an actor like the
   other five and thinks with the same head -- companions_take_turn() runs over
   all six slots and skips whichever one the human is attached to. That is the
   whole of the difference between "the protagonist" and "a hire" now, and it
   lasts exactly as long as you hold the key. */
