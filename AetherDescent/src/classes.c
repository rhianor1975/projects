#include "classes.h"
#include "items.h"
#include "spells.h"
#include "ranged.h"
#include "gearsets.h"

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void build_attrs(int class_id, int *out) {
    for (int i = 0; i < ATTR_COUNT; i++) out[i] = 3; /* baseline: average person */

    if (class_id < 0 || class_id >= NUM_CLASSES) class_id = 0;
    const AttrOverride *ov = CLASS_TABLE[class_id].overrides;
    for (int i = 0; ov[i].attr != ATTR_COUNT; i++) {
        out[ov[i].attr] = ov[i].value;
    }
}

static void hero_compute_derived(Hero *h) {
    int *a = h->attrs;

    h->base_atk = 4 + a[ATTR_MIGHT] / 2 + a[ATTR_BRAWN] / 4;
    h->base_def = 1 + a[ATTR_GRIT] / 2 + a[ATTR_FORTITUDE] / 4;
    h->maxhp = 30 + (a[ATTR_BRAWN] + a[ATTR_STAMINA]) * 2;
    h->hp = h->maxhp;

    h->fov_radius        = clampi(6 + (a[ATTR_VISION] - 3) / 2, 5, 11);
    h->hearing_radius    = clampi(a[ATTR_HEARING] - 3, 0, 6);
    h->evasion_pct       = clampi((a[ATTR_AGILITY] + a[ATTR_REFLEXES] + a[ATTR_BALANCE]) / 3 * 2 - 4, 0, 30);
    h->crit_pct          = clampi((a[ATTR_PRECISION] + a[ATTR_FORTUNE]) * 2 - 8, 0, 30);
    h->ward_pct          = clampi(a[ATTR_WARDING] * 2 - 4, 0, 25);
    h->hazard_resist_pct = clampi((a[ATTR_FORTITUDE] - 3) * 6 + (a[ATTR_RESOLVE] - 3) * 2, 0, 55);
    h->xp_bonus_pct      = clampi((a[ATTR_INTELLECT] - 3) * 5, 0, 45);
    h->gold_bonus_pct    = clampi((a[ATTR_CUNNING] + a[ATTR_FORTUNE] - 6) * 3, 0, 45);
    h->shop_discount_pct = clampi((a[ATTR_CHARM] - 3) * 3 + (a[ATTR_MEMORY] - 3), 0, 25);
    h->potion_potency_pct= clampi((a[ATTR_CHEMISTRY] - 3) * 5, 0, 50);
    h->gear_bonus_pct    = clampi((a[ATTR_CRAFTING] - 3) * 3, 0, 30);
    h->buff_duration_pct = clampi((a[ATTR_CONDUIT] - 3) * 4, 0, 40);
    h->aggro_reduction   = clampi(a[ATTR_PRESENCE] - 3, 0, 6);
    h->ambush_resist_pct = clampi((a[ATTR_GUILE] - 3) * 5 + (a[ATTR_INSTINCT] - 3) * 2, 0, 40);
    h->recall_turns      = clampi(3 - (a[ATTR_STAMINA] - 3) / 3, 1, 3);
    h->machine_luck_pct  = clampi((a[ATTR_TECH_WIT] - 3) * 4, 0, 30);
    h->fortune_luck_pct  = clampi((a[ATTR_FORTUNE] + a[ATTR_EMPATHY] - 6) * 3, 0, 30);
    h->jinx_pct          = clampi(a[ATTR_JINX] * 2 - 4, 0, 25);
    h->relic_quality_bonus = clampi(a[ATTR_RESONANCE] - 3, 0, 6);
    h->scribing_reveal   = a[ATTR_SCRIBING] >= 7;
    h->aether_reveal     = a[ATTR_AETHER_SENSE] >= 7;

    h->aether_max = 8 + a[ATTR_CONDUIT] * 2;
    h->spell_power_pct   = clampi((a[ATTR_RESONANCE] - 3) * 8, 0, 80);
    h->aether      = h->aether_max;

    h->ranged_ammo_max      = 6 + a[ATTR_PRECISION] * 2;
    h->ranged_dmg_bonus_pct = clampi((a[ATTR_PRECISION] - 3) * 6, 0, 60);
    h->ranged_ammo          = h->ranged_ammo_max;
    h->ranged_type          = RANGED_NONE;
}

/* Every class is assigned to whichever school's attribute it leans highest
   toward (Conduit/Resonance/Warding/Aether-Sense/Scribing/Jinx). Classes
   with no elevated magic attribute at all (most of them -- this system
   isn't just for "mage" classes) spread evenly across schools by class_id
   instead of all defaulting to school 0. */
static int compute_magic_school(int class_id, const int *a) {
    int candidates[SCHOOL_COUNT] = {
        a[ATTR_CONDUIT], a[ATTR_RESONANCE], a[ATTR_WARDING],
        a[ATTR_AETHER_SENSE], a[ATTR_SCRIBING], a[ATTR_JINX]
    };
    int best = 0;
    for (int i = 1; i < SCHOOL_COUNT; i++) {
        if (candidates[i] > candidates[best]) best = i;
    }
    if (candidates[best] <= 3) best = class_id % SCHOOL_COUNT;
    return best;
}

/* Same idea as compute_magic_school, but over the physical/combat side of
   the sheet -- every class gets exactly one innate ability this way, not
   just the ones with an obvious combat lean. Order must match AbilityId's
   comment in common.h. */
static int compute_ability(int class_id, const int *a) {
    int candidates[ABILITY_COUNT] = {
        a[ATTR_MIGHT], a[ATTR_BRAWN], a[ATTR_AGILITY], a[ATTR_REFLEXES], a[ATTR_PRECISION],
        a[ATTR_GRIT], a[ATTR_FORTITUDE], a[ATTR_STAMINA], a[ATTR_BALANCE], a[ATTR_INSTINCT]
    };
    int best = 0;
    for (int i = 1; i < ABILITY_COUNT; i++) {
        if (candidates[i] > candidates[best]) best = i;
    }
    if (candidates[best] <= 3) best = class_id % ABILITY_COUNT;
    return best;
}

static void grant_named(Player *p, const char *name, int qty) {
    const ConsumableTemplate *t = consumable_by_name(name);
    if (t) give_consumable(p, t, qty);
}

/* What each archetype walks out of the plaza carrying, on top of the
   attribute-derived kit above.

   A class's numbers already come from its attributes, but those say nothing
   about *role*: a Marksman with no bow is just a worse Skirmisher, and an
   Arcanist with no spell cannot do the one thing the archetype is named for
   until they can afford the guild. So the fighting archetypes get the better
   weapons and armour, and everyone else gets the thing that makes them what
   they are. */
typedef struct {
    int weapon, armor;   /* added to the attribute-derived bonus */
    int gold;            /* added to the starting purse */
} ArchetypeKit;

static const ArchetypeKit ARCHETYPE_KITS[ARCHETYPE_COUNT] = {
    [ARCH_VANGUARD]   = { 3, 4, 0 },   /* front line: best of both */
    [ARCH_SKIRMISHER] = { 3, 2, 0 },   /* hits hard, wears little */
    [ARCH_MARKSMAN]   = { 1, 1, 0 },   /* pays for it in the bow below */
    [ARCH_ARCANIST]   = { 0, 1, 0 },   /* pays for it in the spell below */
    [ARCH_ARTIFICER]  = { 1, 2, 0 },   /* brings supplies instead */
    [ARCH_SURVIVOR]   = { 1, 3, 0 },   /* built to absorb, not to swing */
    [ARCH_ENVOY]      = { 1, 2, 60 },  /* arrives with money and contacts */
};

/* The cheapest bow in the Armory, handed over rather than sold. Kept in step
   with RANGED_STOCK by looking it up, not by copying its numbers. */
static void grant_starting_ranged(Hero *h) {
    for (int i = 0; i < RANGED_STOCK_COUNT; i++) {
        const RangedTemplate *t = &RANGED_STOCK[i];
        if (t->type != RANGED_BOW) continue;
        int bonus = t->bonus + t->bonus * h->gear_bonus_pct / 100;
        equip_ranged(h, t->name, t->type, bonus, t->ammo_cost, t->cooldown);
        h->ranged_slot = i + 1;
        return;
    }
}

/* The first level-1 spell of the caster's own school. Their school, because
   that is the only one the guild will ever teach them (see spells.h), so
   anything else would be a spell they can never build on. */
static void grant_starting_spell(Hero *h) {
    if (h->spell_count > 0) return;
    int n = spell_school_count(h->magic_school);
    for (int i = 0; i < n; i++) {
        int idx = spell_school_index(h->magic_school, i);
        const SpellTemplate *t = spell_pool_get(idx);
        if (!t || t->level != 1) continue;
        h->known_spells[h->spell_count] = idx;
        h->spell_cd[h->spell_count] = 0;
        h->spell_count++;
        return;
    }
}

/* The starting kit, which is the party's business as much as the body's: the
   weapon and armour go on the hero, the draughts go in the shared pack and the
   Envoy's contacts pay into the shared purse. That split is why this takes
   both, and it is the same line drawn everywhere else. */
static void apply_archetype_kit(Player *p, Hero *h, int archetype) {
    if (archetype < 0 || archetype >= ARCHETYPE_COUNT) archetype = ARCH_VANGUARD;
    const ArchetypeKit *k = &ARCHETYPE_KITS[archetype];

    h->weapon_bonus += k->weapon;
    h->armor_bonus  += k->armor;
    p->gold         += k->gold;

    switch (archetype) {
        case ARCH_MARKSMAN:  grant_starting_ranged(h); break;
        case ARCH_ARCANIST:  grant_starting_spell(h);  break;
        case ARCH_ARTIFICER:
            /* Turns up with their own supplies, which is the whole trade. */
            grant_named(p, "Minor Healing Draught", 1);
            grant_named(p, "Guard Tonic (Lesser)", 1);
            break;
        case ARCH_SURVIVOR:
            grant_named(p, "Field Rations, Preserved", 2);
            break;
        default: break;
    }
}

/* ---- the one constructor ----------------------------------------------
 *
 * Everything that makes a body comes through here: the character at new-game,
 * a hire at the Tavern, a golem off the assembly line. There used to be two of
 * these -- this one, building a character out of a class seed, and
 * tavern_candidate(), building a hire out of an archetype tilt -- and every
 * difference between a character and a hire fell out of that split rather than
 * out of any decision anybody made. A hire had no attributes because the
 * function that wrote attributes was not the function that made hires.
 *
 * `p` is the party the body is joining and is touched only for what is shared:
 * the starting kit goes in the party's pack and the Envoy's contacts pay into
 * the party's purse. Pass starting_kit = false for a body that is being rolled
 * rather than outfitted -- a Tavern candidate you are only looking at -- and
 * the party is not touched at all.
 */
void hero_create(Player *p, Hero *h, int class_id, bool starting_kit) {
    if (!h) return;
    if (class_id < 0 || class_id >= NUM_CLASSES) class_id = 0;
    const ClassSeed *c = &CLASS_TABLE[class_id];

    h->class_id  = class_id;
    /* Read back off the class rather than passed in, so the two can never
       disagree -- the bug that shape produces is a Vanguard that fights like
       an Arcanist and reads as neither. */
    h->archetype = c->archetype;

    /* `alive` is a fact about the body and belongs here. `in_use` is a fact
       about the *party* -- which seat, if any, they occupy -- so it is set by
       whoever seats them, and a Tavern candidate you are only looking at
       stays out of the party until you pay for them. */
    h->alive = true;
    if (h->level < 1) h->level = 1;

    /* Sentinels, not zeroes. Every one of these has a real meaning at 0 --
       roster candidate 0, map square 0, spell slot 0 -- so a memset body that
       never came through here would claim to be the first Tavern hero, to have
       just stepped off the top-left corner of the map, and to have cast its
       first spell already. */
    h->roster_idx      = -1;
    h->last_x = h->last_y = -1;
    h->scout_x = h->scout_y = -1;
    h->last_spell_slot = -1;

    build_attrs(class_id, h->attrs);
    hero_compute_derived(h);
    h->magic_school = compute_magic_school(class_id, h->attrs);
    h->ability_id = compute_ability(class_id, h->attrs);
    h->ability_cd = 0;

    int *a = h->attrs;
    int base_weapon = clampi(a[ATTR_MIGHT] / 3 + a[ATTR_PRECISION] / 4, 1, 6);
    int base_armor  = clampi(a[ATTR_GRIT] / 3 + a[ATTR_FORTITUDE] / 4, 1, 6);
    h->weapon_bonus = base_weapon + base_weapon * h->gear_bonus_pct / 100;
    h->armor_bonus  = base_armor + base_armor * h->gear_bonus_pct / 100;
    strncpy(h->weapon_name, c->start_weapon_name, sizeof(h->weapon_name) - 1);
    h->weapon_name[sizeof(h->weapon_name) - 1] = '\0';
    strncpy(h->armor_name, c->start_armor_name, sizeof(h->armor_name) - 1);
    h->armor_name[sizeof(h->armor_name) - 1] = '\0';

    if (!starting_kit || !p) return;

    /* Attribute perks. Both hand out the *real* shop entry rather than a
       locally-built copy -- the copy of the healing draught here was still a
       flat 15 HP long after shop draughts started scaling with max HP. */
    if (a[ATTR_CUNNING] >= 7 || a[ATTR_TECH_WIT] >= 7) grant_named(p, "Recall Charm", 1);
    if (a[ATTR_FORTITUDE] >= 7 || a[ATTR_EMPATHY] >= 7) grant_named(p, "Minor Healing Draught", 1);

    apply_archetype_kit(p, h, c->archetype);
}

/* The character at new-game: slot 0, with the full starting kit. Kept as its
   own name because "apply a class to the person the run is about" is what the
   creation screen is doing, and because every caller of it means slot 0
   specifically -- there is no run yet for anybody else to be in. */
void apply_class_to_player(Player *p, int class_id) {
    if (!p) return;
    hero_create(p, &p->party[0], class_id, true);
    p->party[0].in_use = true;   /* slot 0 is always somebody: the run is theirs */
}

/* ---- archetypes ------------------------------------------------------ */

static const char *const ARCHETYPE_NAMES[ARCHETYPE_COUNT] = {
    "Vanguard", "Skirmisher", "Marksman", "Arcanist",
    "Artificer", "Survivor", "Envoy",
};

static const char *const ARCHETYPE_BLURBS[ARCHETYPE_COUNT] = {
    "Stands in front and stays there. Might, brawn, and the patience to be hit.",
    "Fast, slippery, hard to pin down. Fights by not being where the blow lands.",
    "Kills at a distance. Precision and eyesight over anything that swings.",
    "Works in aether. The six schools, and the charge to spend on them.",
    "Builds, brews and rigs. Turns scrap and chemistry into an advantage.",
    "Outlasts. Fortitude, stamina, instinct, and a nose for what's worth taking.",
    "Leads, bluffs, and is listened to. Presence and guile over muscle.",
};

const char *archetype_name(int a) {
    if (a < 0 || a >= ARCHETYPE_COUNT) return "?";
    return ARCHETYPE_NAMES[a];
}

const char *archetype_blurb(int a) {
    if (a < 0 || a >= ARCHETYPE_COUNT) return "";
    return ARCHETYPE_BLURBS[a];
}

int archetype_class_count(int a) {
    if (a < 0 || a >= ARCHETYPE_COUNT) return 0;
    int n = 0;
    for (int i = 0; i < NUM_CLASSES; i++)
        if (CLASS_TABLE[i].archetype == a) n++;
    return n;
}

/* Walks the table rather than caching an index: 100 entries scanned once per
   keypress on a menu screen is not worth a lookup table that could fall out
   of step with CLASS_TABLE. */
int archetype_class_index(int a, int n) {
    if (a < 0 || a >= ARCHETYPE_COUNT || n < 0) return -1;
    for (int i = 0; i < NUM_CLASSES; i++) {
        if (CLASS_TABLE[i].archetype != a) continue;
        if (n-- == 0) return i;
    }
    return -1;
}

/* The School sells one point of one attribute. It used to sell it by calling
   hero_compute_derived() in place and then putting back the three things that were
   known to be lost, and the list was three entries long where it needed to be
   about eight.
 *
   hero_compute_derived() *assigns* the sheet from the attribute table, and a
   character who has been played is not the sum of their attributes any more.
   Nineteen levels are +19 attack, +9 defence and +152 max HP that live in
   base_atk, base_def and maxhp and nowhere else. Shrines add to base_atk
   directly. The bow is an assignment to ranged_type, which compute_derived
   clears on its way past. Measured on a level-20 character carrying a
   shortbow: one point of Might took attack 28 -> 10, defence 16 -> 6, max HP
   238 -> 86, and left them unarmed at range. Buying a point of anything
   deleted the run.
 *
   So the point is applied as a *difference*. Two sheets are derived from the
   attribute table alone -- one as it stands, one with the point bought -- and
   every stat moves by the gap between them. For a stat nothing else writes,
   moving by the gap gives exactly what assigning it would have; for one that
   levels or shrines or the smith also add to, only the attribute's own share
   moves. Nothing has to be listed and kept in step, which is the property the
   old hand-written restore list did not have. */
/* Move the attribute table by `delta` at each named index, and carry every
   derived stat along by the difference the move makes.
 *
   This is train_attribute's body, lifted so the Altar can use it too. The
   Altar takes a point from one attribute and puts it on another, and doing
   that as two calls to train_attribute would be wrong twice over: there is no
   un-train, and even if there were, two sequential recomputes are not the same
   as one recompute of the finished sheet wherever a derived stat is a
   non-linear function of the thirty -- which several are, being clamped.

   So: one before-sheet, one after-sheet, one set of deltas, whatever the move
   was. `take` may be -1 for the plain School case. */
static void shift_attributes(Hero *h, int give, int take) {
    /* Two sheets derived from the attribute table alone. A Hero is small
       enough to sit on the stack -- the old version copied a whole Player
       twice and needed statics to do it, which is one more thing the split
       was costing. */
    Hero before = *h, after = *h;
    hero_compute_derived(&before);
    if (give >= 0) after.attrs[give]--;
    if (take >= 0) after.attrs[take]++;
    hero_compute_derived(&after);

    if (give >= 0) h->attrs[give]--;
    if (take >= 0) h->attrs[take]++;

#define TRAINED(f) h->f += after.f - before.f
    TRAINED(base_atk);
    TRAINED(base_def);
    TRAINED(maxhp);
    TRAINED(fov_radius);
    TRAINED(hearing_radius);
    TRAINED(evasion_pct);
    TRAINED(crit_pct);
    TRAINED(ward_pct);
    TRAINED(hazard_resist_pct);
    TRAINED(xp_bonus_pct);
    TRAINED(gold_bonus_pct);
    TRAINED(shop_discount_pct);
    TRAINED(potion_potency_pct);
    TRAINED(gear_bonus_pct);
    TRAINED(buff_duration_pct);
    TRAINED(aggro_reduction);
    TRAINED(ambush_resist_pct);
    TRAINED(recall_turns);
    TRAINED(machine_luck_pct);
    TRAINED(fortune_luck_pct);
    TRAINED(jinx_pct);
    TRAINED(relic_quality_bonus);
    TRAINED(aether_max);
    TRAINED(spell_power_pct);
    TRAINED(ranged_ammo_max);
    TRAINED(ranged_dmg_bonus_pct);
#undef TRAINED

    /* Thresholds rather than sums: nothing else in the game writes these, so
       the derived answer is the answer. */
    h->scribing_reveal = after.scribing_reveal;
    h->aether_reveal   = after.aether_reveal;

    /* The ceilings moved; what is in the pools did not. The School is not an
       Inn and not a resupply -- a free heal every visit would make it both. */
    if (h->hp > h->maxhp) h->hp = h->maxhp;
    if (h->hp < 1) h->hp = 1;
    if (h->aether > h->aether_max) h->aether = h->aether_max;
    if (h->ranged_ammo > h->ranged_ammo_max)     h->ranged_ammo = h->ranged_ammo_max;

    recompute_set_bonus(h);
}

void train_attribute(Hero *h, int attr) {
    if (!h || attr < 0 || attr >= ATTR_COUNT) return;
    shift_attributes(h, -1, attr);
}

bool trade_attribute(Hero *h, int give, int take) {
    if (!h) return false;
    if (give < 0 || give >= ATTR_COUNT || take < 0 || take >= ATTR_COUNT) return false;
    if (give == take) return false;
    if (h->attrs[give] <= ALTAR_MIN_ATTR) return false;
    shift_attributes(h, give, take);
    return true;
}
