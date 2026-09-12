#include "prophecy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

Player players[MAX_PLAYERS];
Plane  planes[PLANES];
int    nplayers = 4, cur_player = 0;

#define TURN_CAP     4000

int space_adv[RING_N][MAX_ON_SPACE], space_nadv[RING_N];
int space_item[RING_N];                 /* on sale in the City / Village */
int guild_abil[G_COUNT];                /* the Ability lying at a guild  */
static int plane_lesser[PLANES], plane_greater[PLANES];
static int plane_art[PLANES];   /* which Artifact lies beyond each Plane */

/* --- the Dragon Realm ------------------------------------------------
 * Three paths climbing from the Gate to the Lake of Fire.  The middle is
 * the shortest and the right the longest, which is the whole decision a
 * character makes on entering: hurry, or grow stronger on the way. */
static int use_dragon;                 /* PROPHECY_DRAGON=1                */
static int power_up;                   /* +1/+1 on a Plane that is not the Gate */
static int dr_gate = -1;               /* which Plane the Gate replaced    */
static DragonPath dr_path[DR_PATHS];
static const int DR_LEN[DR_PATHS] = { 7, 6, 8 };   /* left, middle, right */

static void water_setup(void);

static void dragon_setup(void)
{
    int deck[64], n = 0, i, k, pos[DR_PATHS];

    if (!use_dragon) { dr_gate = -1; return; }
    for (i = 0; dr_proto[i].name; i++) deck[n++] = i;
    for (i = n - 1; i > 0; i--) { int j = rand() % (i+1), t = deck[i];
                                  deck[i] = deck[j]; deck[j] = t; }
    for (i = 0; i < DR_PATHS; i++) { dr_path[i].len = DR_LEN[i]; pos[i] = 0; }
    /* deal round the three paths until they are full; whatever is left over
     * is set aside unseen, as the rulebook says */
    k = 0;
    for (i = 0; i < n; i++) {
        int tries = 0;
        while (pos[k] >= dr_path[k].len && tries++ < DR_PATHS) k = (k+1) % DR_PATHS;
        if (pos[k] >= dr_path[k].len) break;
        dr_path[k].card[pos[k]] = deck[i];
        dr_path[k].seen[pos[k]] = 0;
        pos[k]++;
        k = (k + 1) % DR_PATHS;
    }
    for (i = 0; i < DR_PATHS; i++)
        for (k = pos[i]; k < dr_path[i].len; k++) dr_path[i].card[k] = -1;
    /* the Gate replaces one Lesser Guardian: nobody knows which Plane */
    dr_gate = rand() % PLANES;
    glog("(a Gate to the Dragon Realm lies behind one of the Astral Planes)");
}
static int quit_flag, turn_over, autoplay, checking;
static int final_battle;
static int watching;      /* --watch: every hero is played for you */
static int apocalypse_variant;  /* --apocalypse: the longer, crueller game */
static int apocalypse;          /* it has actually begun                   */
static int team_play;           /* --teams: four players, two pairs        */

/* "The goal of each pair is for one of them to become King.  It does not
 * matter which one.  If one becomes King, both of them win." */
static int allied(const Player *a, const Player *b)
{
    return a->team >= 0 && a->team == b->team;
}   /* all five Artifacts out, nobody has four */
static long round_no;
static unsigned game_seed;
unsigned ui_seed(void) { return game_seed; }
static int ai_delay = 500;

int ui_delay(void) { return ai_delay; }

int roll(void) { return rand() % 6 + 1; }

Guild guild_of(SpaceKind k)
{
    switch (k) {
    case SP_MONASTERY:     return G_MONASTERY;
    case SP_FOREST_CAMP:   return G_CAMP;
    case SP_MAGIC_TOWER:   return G_TOWER;
    case SP_THIEVES_GUILD: return G_THIEVES;
    case SP_FORTRESS:      return G_FORTRESS;
    default:               return G_NONE;
    }
}

static void speed_up(void)  { ai_delay = (ai_delay > 100) ? ai_delay - 100 : 0; }
static void slow_down(void) { ai_delay = (ai_delay < 3000) ? ai_delay + 100 : 3000; }

/* While watching, the pause between actions is also where the controls
 * live -- otherwise a key would not be read until the next prompt. */
static void watch_wait(void)
{
    int ch;

    if (quit_flag) return;
    ch = ui_wait_key(ai_delay > 0 ? ai_delay : 0);
    if (ch == ERR) return;
    for (;;) {
        if (ch == 'q')              { quit_flag = 1; return; }
        if (ch == '+' || ch == '=') { speed_up();  return; }
        if (ch == '-' || ch == '_') { slow_down(); return; }
        if (ch != ' ' && ch != '\n') return;
        for (;;) {
            char b[100];
            snprintf(b, sizeof b, "  PAUSED   [space] resume   [+/-] speed (%dms)   [q] quit", ai_delay);
            ui_banner(b);
            ch = ui_wait_key(-1);
            if (ch == '+' || ch == '=') { speed_up();  continue; }
            if (ch == '-' || ch == '_') { slow_down(); continue; }
            break;
        }
        if (ch == 'q') { quit_flag = 1; return; }
        return;
    }
}

static void pace(void)
{
    if (watching) {
        char b[100];
        snprintf(b, sizeof b, "  WATCHING   [space] pause   [+/-] speed (%dms)   [q] quit", ai_delay);
        ui_banner(b);
        watch_wait();
    } else if (ai_delay) {
        ui_banner("  ... the world turns ...");
        napms(ai_delay);
    }
}

/* Paint the world and let the computer's move be seen.  Never stops a
 * human: the log is on screen and they are the ones driving. */
static void redraw(Player *p)
{
    if (autoplay) return;
    ui_draw();
    if (p->ai) pace();
}

/* A moment worth stopping on -- dice, an outcome, an Artifact. */
static void beat(Player *p)
{
    if (autoplay) return;
    ui_draw();
    if (p->ai) pace();
    else       ui_banner("  [any key]"), getch();
}

/* ---------------------------------------------------------- the cubes -- */
static int has_perk(const Player *p, Perk k);
static const Race *racial(const Player *p);
static int win_chance(int mine, int theirs, int lives);
static int abil_cap(const Player *p);
static int item_cap(const Player *p);

static void hurt(Player *p, int n)
{
    while (n-- > 0) {
        if (p->str_now > 0) { p->str_now--; p->str_lost++; }
        else {
            /* Sanctity of Life: the last blow may be refused, but only
             * once -- after that the quest really does end. */
            if (racial(p)->frail) {
                p->alive = 0;
                glog("*** the elf's fairy body fails at once. ***");
                return;
            }
            if (has_perk(p, PK_SANCTITY) && !p->spared) {
                p->spared = 1;
                glog("  Sanctity of Life holds %s back from the edge.", p->name);
                return;
            }
            p->alive = 0;
            {   /* what was still in her pack when she died?  A hero dying
                 * with a Potion of Healing unopened is a bug in judgement,
                 * not in luck. */
                int q, heals = 0;
                for (q = 0; q < p->nitems; q++)
                    if (!p->dmg[q] && item_proto[p->items[q]].oneshot &&
                        (item_proto[p->items[q]].u_heal ||
                         item_proto[p->items[q]].u_magic)) heals++;
                glog("*** %s falls, and the quest ends for her. (unused potions: %d) ***",
                     p->name, heals);
            }
            return;
        }
    }
}
/* items[] and dmg[] are parallel, so nothing may touch one without the
 * other -- every gain and loss of an Item goes through these two. */
static void give_gold(Player *p, int n);

static int has_perk(const Player *p, Perk k)
{
    int i;
    for (i = 0; i < p->nabils; i++) if (abil_proto[p->abils[i]].perk == k) return 1;
    return 0;
}

/* Rulebook: Counterfeiting is spendable "only once when paying for
 * something", and repairing several Items or buying several Health
 * "counts as one payment" -- so it is charged per payment, once a round. */
static int quote_price(const Player *p, int gold)
{
    if (has_perk(p, PK_HAGGLE) && gold > 0) gold--;
    if (gold > 0 && !p->forged_this_round && has_perk(p, PK_COUNTERFEIT)) gold -= 2;
    return gold < 0 ? 0 : gold;
}

/* Hand over a quoted price, spending the Counterfeiting if it was in it. */
static void pay(Player *p, int quoted)
{
    if (!p->forged_this_round && has_perk(p, PK_COUNTERFEIT)) {
        p->forged_this_round = 1;
        glog("  %s passes bad coin.", p->name);
    }
    give_gold(p, -quoted);
}

static int  item_give(Player *p, int proto);
static void item_take(Player *p, int slot);

static void heal(Player *p, int n)
{
    while (n-- > 0 && p->str_lost > 0) { p->str_lost--; p->str_now++; }
}
static void drain(Player *p, int n)
{
    while (n-- > 0 && p->will_now > 0) { p->will_now--; p->will_lost++; }
}
static void recharge(Player *p, int n)
{
    while (n-- > 0 && p->will_lost > 0) { p->will_lost--; p->will_now++; }
}
static void gain_str(Player *p)
{
    if (p->str_now + p->str_lost < MAX_STRENGTH) p->str_now++;
    else heal(p, 1);                       /* already at the cap: heal instead */
}
static void gain_will(Player *p)
{
    if (p->will_now + p->will_lost < MAX_WILL) p->will_now++;
    else recharge(p, 1);
}
static void give_gold(Player *p, int n)
{
    p->gold += n;
    if (p->gold > MAX_GOLD) p->gold = MAX_GOLD;
    if (p->gold < 0) p->gold = 0;
}
static void give_exp(Player *p, int n)
{
    p->exp += n;
    if (p->exp > MAX_EXP) p->exp = MAX_EXP;
    if (p->exp < 0) p->exp = 0;
}

/* ---------------------------------------------------------- the decks -- */
static int adeck[256], adeck_n, adeck_pos;
static int cdeck[64],  cdeck_n, cdeck_pos;
static int extra_turn;      /* Peaceful Times: a second turn this round */
static int canon_only;      /* PROPHECY_CANON=1: the printed deck alone   */
static int use_races;       /* PROPHECY_RACES=1: the Ancient Races option */

/* Everything about a character that her Race changes.  RACE_NONE gives a
 * table of zeroes, so the rest of the code need not ask whether Races are
 * even in play -- the rulebook says as much: "you can use these new cards
 * even if you are not using Races: just treat all characters as humans." */
/* How many Abilities this character may hold.  Only the ABILITY limit moves
 * with Race -- the Human learns one more, the Troll two fewer.  The Item
 * limit is untouched by Race, and conflating the two was worth about eleven
 * hundred spurious invariant reports. */
static int abil_cap(const Player *p)
{
    int n = MAX_ABILITIES + racial(p)->max_abil;
    if (n < 1) n = 1;
    if (n > CARD_SLOTS) n = CARD_SLOTS;
    return n;
}

static int item_cap(const Player *p)
{
    (void)p;
    return MAX_ITEMS;
}

static const Race *racial(const Player *p)
{
    static const Race none = { "", 0, 0, 0, 0, 0, 0, 0, 0, 0,
                               0, 0, 0, 0, 0, 0, "" };
    return (p->race >= 0 && p->race < RACE_N) ? &race_proto[p->race] : &none;
}
static BattleKind last_kind;  /* how the last battle was decided */
static int spells_barred;     /* an Anti-magic Aura is in force */
static int ideck[128], ideck_n, ideck_pos;   /* 61 printed Items, more with fan cards */
/* Each guild teaches its own trade: the Magic Tower does not hand out
 * Swordsmanship, and the Thieves' Guild has no business with Arcane Lore.
 * One shuffled deck per guild, drawn without replacement. */
static int gdeck[G_COUNT][32], gdeck_n[G_COUNT], gdeck_pos[G_COUNT];

static void deck_build(void)
{
    int i, k;
    adeck_n = ideck_n = 0;
    for (i = 0; adv_proto[i].name; i++) {
        /* PROPHECY_CANON=1 builds the printed 2nd-edition deck and nothing
         * else.  Mixing fan cards in is the normal way to play, but it does
         * change how often any one real card comes up, so the pure deck has
         * to be available to compare against. */
        if (canon_only && adv_proto[i].src != SRC_BASE) continue;
        for (k = 0; k < adv_proto[i].copies && adeck_n < 256; k++) adeck[adeck_n++] = i;
    }
    for (i = 0; item_proto[i].name; i++)
    {
        if (canon_only && item_proto[i].src != SRC_BASE) continue;
        for (k = 0; k < item_proto[i].copies && ideck_n < 128; k++) ideck[ideck_n++] = i;
    }
    dragon_setup();
    water_setup();
    glog("(deck: %d Adventure, %d Item%s)", adeck_n, ideck_n,
         canon_only ? " -- printed cards only" : "");
    for (i = adeck_n - 1; i > 0; i--) { k = rand() % (i+1); int t=adeck[i]; adeck[i]=adeck[k]; adeck[k]=t; }
    for (i = ideck_n - 1; i > 0; i--) { k = rand() % (i+1); int t=ideck[i]; ideck[i]=ideck[k]; ideck[k]=t; }
    cdeck_n = 0;
    for (i = 0; chance_proto[i].name; i++)
        for (k = 0; k < chance_proto[i].copies && cdeck_n < 64; k++) cdeck[cdeck_n++] = i;
    for (i = cdeck_n - 1; i > 0; i--) { k = rand() % (i+1); int t=cdeck[i]; cdeck[i]=cdeck[k]; cdeck[k]=t; }
    for (i = 0; i < G_COUNT; i++) gdeck_n[i] = gdeck_pos[i] = 0;
    for (i = 0; abil_proto[i].name; i++) {
        Guild g = abil_proto[i].guild;
        if (canon_only && abil_proto[i].src != SRC_BASE) continue;
        if (g >= 0 && g < G_COUNT && gdeck_n[g] < 32) gdeck[g][gdeck_n[g]++] = i;
    }
    for (k = 0; k < G_COUNT; k++)
        for (i = gdeck_n[k] - 1; i > 0; i--) {
            int j = rand() % (i+1), t = gdeck[k][i];
            gdeck[k][i] = gdeck[k][j]; gdeck[k][j] = t;
        }
    adeck_pos = ideck_pos = cdeck_pos = 0;
}
static int adv_draw(void)
{
    if (adeck_n <= 0) return -1;
    if (adeck_pos >= adeck_n) { adeck_pos = 0; glog("(the Adventure deck is shuffled anew)"); }
    return adeck[adeck_pos++];
}
static int item_draw(void)
{
    if (ideck_n <= 0) return -1;
    if (ideck_pos >= ideck_n) { ideck_pos = 0; }
    return ideck[ideck_pos++];
}

static void place_adventure(int idx)
{
    int ci = adv_draw();
    if (ci < 0 || space_nadv[idx] >= MAX_ON_SPACE) return;
    space_adv[idx][space_nadv[idx]++] = ci;
    glog("  %s appears in the %s.", adv_proto[ci].name, ring[idx].name);
}
static void remove_adventure(int idx, int slot)
{
    int i;
    for (i = slot; i < space_nadv[idx] - 1; i++) space_adv[idx][i] = space_adv[idx][i+1];
    if (space_nadv[idx] > 0) space_nadv[idx]--;
}

/* ------------------------------------------------------------ battles -- *
 * Both sides roll one die and add the characteristic being fought with.
 * Higher total wins; equal is a draw.  A Battle of Wills takes precedence
 * but the one who starts it pays 2 Magic first, which lowers the very
 * Willpower she is about to fight with.
 */
/* Rulebook: a Spell is "activated by paying one or two Magic", and Magic
 * is the same blue pool as Willpower -- so a Battle of Wills spell that
 * costs 2 and gives 3 is only worth +1.  Spells "cannot be used in the
 * same moment for the same goal", so each is offered once per battle. */
static int battle_spell(Player *p, BattleKind k, const char *foe)
{
    SpellKind want = (k == B_WILL) ? SP_BATTLE_WILL : SP_BATTLE_STR;
    int i, bi = -1, bnet = 0;

    if (spells_barred) return 0;      /* Anti-magic Aura */

    for (i = 0; i < p->nabils; i++) {
        const AbilityCard *a = &abil_proto[p->abils[i]];
        int net;
        {
            int cost = a->magic;
            if (cost > 0 && has_perk(p, PK_SPELLCAST)) cost--;
            if (a->kind != want || cost > p->will_now) continue;
        }
        /* spending Magic lowers Willpower, so a Wills spell nets less */
        net = a->power - (k == B_WILL ? a->magic : 0);
        if (net > bnet) { bnet = net; bi = i; }
    }
    if (bi < 0) return 0;
    {
        const AbilityCard *a = &abil_proto[p->abils[bi]];
        if (p->ai) {
            if (bnet <= 0) return 0;
        } else {
            char m[160];
            snprintf(m, sizeof m, "Cast %s against %s?  %d Magic for +%d %s   [y]/[n]",
                     a->name, foe, a->magic, a->power,
                     k == B_WILL ? "Willpower" : "Strength");
            ui_draw();
            if (ui_prompt(m, "yn") != 'y') return 0;
        }
        drain(p, a->magic);
        glog("  %s casts %s (%d Magic): +%d.", p->name, a->name, a->magic, a->power);
        return a->power;
    }
}

/* Rulebook, and its own worked example: a throwing weapon may be thrown
 * in a Battle of Strength for an extra bonus, after which "he turns the
 * card face-down and may not use it again" until it is repaired.  The
 * weapon still counts for the battle it is thrown in -- it is only from
 * the next one that it is dead -- and only a Battle of Strength allows it. */
static int throw_weapon(Player *p, BattleKind k, const char *foe, int mine, int theirs)
{
    int i, bi = -1, best = 0;

    if (k != B_STRENGTH) return 0;
    for (i = 0; i < p->nitems; i++) {
        const Item *it = &item_proto[p->items[i]];
        int th = it->thrown;
        /* "You can even throw a one-handed weapon that is not normally
         * thrown, but this will damage the weapon." */
        if (!th && has_perk(p, PK_THROWN) &&
            (it->wtype & (WT_EDGED | WT_CRUSHING)) && !(it->wtype & WT_TWOHAND))
            th = 1;
        if (th && has_perk(p, PK_THROWN)) th++;      /* +1 when you throw */
        if (p->dmg[i] || !th) continue;
        if (th > best) { best = th; bi = i; }
    }
    if (bi < 0) return 0;
    {
        const Item *it = &item_proto[p->items[bi]];
        
        /* NEW: Only throw if we are losing/tied, AND the weapon bridges the gap */
        if (p->ai && (mine > theirs || mine + it->thrown <= theirs)) return 0;
        
        if (!p->ai) {
            char m[160];
            snprintf(m, sizeof m,
                     "Throw the %s at %s?  +%d now, but it breaks until repaired   [y]/[n]",
                     it->name, foe, it->thrown);
            ui_draw();
            if (ui_prompt(m, "yn") != 'y') return 0;
        }
        p->dmg[bi] = 1;
        glog("  %s throws the %s (+%d); it is damaged.", p->name, it->name, it->thrown);
        return it->thrown;
    }
}

/* The Thug's card: "He calls you out for a fist fight.  If you use a weapon,
 * Shield or spell, you do not receive any Experience for winning."  He was
 * an ordinary Strength 4 brawler, and the fist fight was not in it. */
static int fought_bare_handed(const Player *p)
{
    int i;
    for (i = 0; i < p->nitems; i++) {
        const Item *it = &item_proto[p->items[i]];
        if (p->dmg[i]) continue;
        if (it->d_str > 0) return 0;          /* something in his hands */
    }
    return 1;
}

static int fight_roll(Player *p, const char *foe, int mine, int theirs, BattleKind k)
{
    int d1 = roll(), d2 = roll();
    int a, b;

    if (racial(p)->lucky && d1 == 1) {
        glog("  a halfling's luck: the 1 counts as an 8.");
        d1 = 8;
    }
    if (p->divine) {
        p->divine = 0;
        d1 = 6; d2 = 2;
        glog("  the Scroll of Divine Will settles it before a blow is struck.");
    }

    /* Prayer and Turn Back both amount to "roll twice and keep what you
     * like".  Only worth spending on a roll that is actually losing. */
    if (has_perk(p, PK_TURNBACK) && mine + d1 <= theirs + d2) {
        int again = roll();
        if (again > d1) {
            glog("  %s turns the die back: %d becomes %d.", p->name, d1, again);
            d1 = again;
        }
    }
    a = mine + d1; b = theirs + d2;

    /* Chapel: "you get +2 for your first die roll, then discard this card" */
    if (p->blessed) {
        p->blessed = 0;
        a += 2;
        glog("  the Chapel's blessing holds: +2.");
    }

    glog("%s (%s %d + %d = %d)  vs  %s (%d + %d = %d)",
         p->name, k == B_WILL ? "Will" : "Str", mine, d1, a, foe, theirs, d2, b);
    /* Toughness: "if you lose a Battle of Strength or Wills exactly by 1,
     * then it is considered a draw" */
    if (b - a == 1 && has_perk(p, PK_TOUGH)) {
        glog("  %s takes it on the shoulder: a draw.", p->name);
        return -1;
    }
    return (a > b) ? 1 : (a == b) ? -1 : 0;
}

/* Both Banners are rolled at the start of your round, before the Chance card.
 * They are the only Artifacts that do something every round whether or not
 * you fight, which is most of why they are worth racing for. */
static void art_banners(Player *p)
{
    int i, r;

    if (has_art(p, ART_HOPE)) {
        r = roll();
        glog("  %s raises the Banner of Hope (a %d).", p->name, r);
        if (r <= 3) { p->hope_round = 1;
                      glog("    +1 in every Battle this round."); }
        else if (r == 4) { recharge(p, 2); glog("    2 Magic returns."); }
        else if (r == 5) { heal(p, 1);     glog("    a Health returns."); }
        else {
            glog("    the whole world takes heart.");
            for (i = 0; i < nplayers; i++)
                if (players[i].alive) { recharge(&players[i], 2); heal(&players[i], 1); }
        }
    }
}


/* The type-keyed Abilities: now that a Creature knows what it IS, Hunting,
 * Exorcism and Duelling can finally read it.  `foe_type` is a CT_* mask,
 * and CT_NONE for another character -- except Duelling and Disguise, which
 * name characters explicitly alongside humanoids. */
/* What sorts of weapon this character actually has in hand, and whether any
 * Item of hers is especially suited to this foe.  A damaged Item is face
 * down and counts for nothing. */
/* Some Items simply make the road cheaper while you carry them. */
static int has_item_use(const Player *p, ItemUse u)
{
    int i;
    for (i = 0; i < p->nitems; i++)
        if (!p->dmg[i] && item_proto[p->items[i]].use == u) return 1;
    return 0;
}

static unsigned held_types(const Player *p)
{
    int i; unsigned m = 0;
    for (i = 0; i < p->nitems; i++)
        if (!p->dmg[i]) m |= item_proto[p->items[i]].wtype;
    return m;
}

static int item_vs(const Player *p, unsigned foe_type)
{
    int i, b = 0;
    if (!foe_type) return 0;
    for (i = 0; i < p->nitems; i++) {
        const Item *it = &item_proto[p->items[i]];
        if (p->dmg[i] || !it->vs_bonus) continue;
        if (it->vs_type & foe_type) {
            b += it->vs_bonus;
            glog("  the %s bites deeper here: +%d.", it->name, it->vs_bonus);
        }
    }
    return b;
}

static int perk_battle(const Player *p, BattleKind k, unsigned foe_type,
                       int vs_character)
{
    unsigned w = held_types(p);
    int b = 0;

    /* the weapon-keyed Abilities: each wants a particular sort in hand */
    if (k == B_STRENGTH) {
        if (has_perk(p, PK_EDGED)    && (w & WT_EDGED))    b++;
        if (has_perk(p, PK_CRUSHING) && (w & WT_CRUSHING)) b++;
        if (has_perk(p, PK_LONG)     && (w & WT_LONG))     b++;
        if (has_perk(p, PK_TWOHAND)  && (w & WT_TWOHAND))  b++;
        /* Ambidexterity: two one-handed weapons at once, so a second +1 */
        if (has_perk(p, PK_AMBI) && !(w & WT_TWOHAND) &&
            (w & (WT_EDGED | WT_CRUSHING))) b++;
        b += item_vs(p, foe_type);
    }
    if (racial(p)->shield_bonus && (w & WT_SHIELD) && k == B_STRENGTH) {
        b++; glog("  the goblin fights well behind a shield: +1.");
    }
    if (racial(p)->long_penalty && (w & WT_LONG) && k == B_STRENGTH) {
        b--; glog("  the long weapon is unwieldy in small hands: -1.");
    }
    if (has_perk(p, PK_STAFF) && (w & WT_STAFF)) b++;
    if (has_perk(p, PK_FORESTWISE) &&
        (ring[p->idx].kind == SP_FOREST || ring[p->idx].kind == SP_MOUNTAINS)) b++;
    /* Poisoned Blade: the cut tells on anything that has to be beaten twice */
    if (k == B_STRENGTH && has_perk(p, PK_POISON) && (w & WT_EDGED)) b++;
    if (k == B_WILL && has_perk(p, PK_STAFF) && (w & WT_WAND)) b++;
    if (has_perk(p, PK_HUNT)     && (foe_type & CT_ANIMAL))   b++;
    if (has_perk(p, PK_EXORCISM) && (foe_type & (CT_UNDEAD | CT_DEMON))) b++;
    if (has_perk(p, PK_DUEL)     && ((foe_type & CT_HUMANOID) || vs_character)) b++;
    if (k == B_WILL && has_perk(p, PK_DISGUISE) &&
        ((foe_type & CT_HUMANOID) || vs_character)) b++;
    if (b) glog("  %s has the measure of this foe: +%d.", p->name, b);
    return b;
}

/* The Astral Sword adds +2 in a Battle of Strength, and +1 more against
 * humanoids and other characters.  It is a weapon, so it does nothing in a
 * Battle of Wills. */
static int art_battle(const Player *p, BattleKind k, int vs_character)
{
    int b = 0;
    if (p->hope_round) b += 1;      /* Banner of Hope: "in all Battles" */
    b += (k == B_WILL) ? p->potion_will : p->potion_str;
    if (p->dragon_heart && vs_character) b += 1;
    if (p->pearl        && vs_character) b += 1;
    if (k != B_STRENGTH) return b;
    if (has_art(p, ART_SWORD)) {
        b += art_proto[ART_SWORD].battle_str;
        if (vs_character) b += art_proto[ART_SWORD].vs_char;
    }
    return b;
}

/* The Mirrored Shield turns a loss into a draw on a 5 or a 6, and makes the
 * winner pay for it.  Returns 1 if the loss was reflected. */
static int art_mirror(Player *p, Player *foe, const char *foe_name)
{
    int r;
    if (!has_art(p, ART_MIRROR)) return 0;
    r = roll();
    glog("  %s raises the Mirrored Shield (a %d).", p->name, r);
    if (r < art_proto[ART_MIRROR].save) return 0;
    glog("  the blow returns whence it came: a draw.");
    if (foe) {
        hurt(foe, 1);
        drain(foe, 1);
        glog("  %s loses a Health and a Magic to her own stroke.", foe->name);
    } else {
        glog("  the %s recoils from its own reflection.", foe_name);
    }
    return 1;
}

/* Choose Strength or Wills against a Creature, per the rulebook's cases. */
static BattleKind choose_kind(Player *p, const Adventure *c, int *paid)
{
    *paid = 0;
    if (c->will && !c->str) return B_WILL;              /* it forces the issue, free */
    if (c->str && !c->will) return B_STRENGTH;          /* unintelligent: no choice   */

    /* It attacks with Strength but can be met mentally, for 2 Magic. */
    {
        int want;
        if (p->ai) want = (eff_will(p) - 2 > eff_str(p)) && p->will_now >= 2;
        else {
            char m[140];
            if (p->will_now < 2) return B_STRENGTH;
            snprintf(m, sizeof m,
                     "%s   [s] Strength %d v %d    [w] Wills %d v %d  (costs 2 Magic)",
                     c->name, eff_str(p), c->str, eff_will(p) - 2, c->will);
            ui_draw();
            want = (ui_prompt(m, "sw") == 'w');
        }
        if (want && p->will_now >= 2) {
            drain(p, 2);
            *paid = 2;
            glog("%s reaches out mentally, paying 2 Magic.", p->name);
            return B_WILL;
        }
    }
    return B_STRENGTH;
}

/* Practical Training pays Experience for the kill; Pickpocketing takes the
 * purse of anything that walks upright. */
static void kill_perks(Player *p, const Adventure *c)
{
    if (racial(p)->trophy) {
        if (c->t_gold < 1) { give_gold(p, 1); glog("  the goblin takes a trophy: 1 Gold."); }
        if (c->exp < 3) {
            p->exp = p->exp + (3 - c->exp) > MAX_EXP ? MAX_EXP : p->exp + (3 - c->exp);
            glog("  and counts the kill for 3 Experience.");
        }
    }
    if (has_perk(p, PK_TRAINING)) {
        p->exp = p->exp + 1 > MAX_EXP ? MAX_EXP : p->exp + 1;
        glog("  %s learns from the fight: an extra Experience.", p->name);
    }
    if (has_perk(p, PK_PICKPOCKET) && (c->ctype & CT_HUMANOID)) {
        give_gold(p, 2);
        glog("  %s lightens its purse by 2 Gold.", p->name);
    }
}

static void creature_reward(Player *p, const Adventure *c)
{
    kill_perks(p, c);
    /* "If you agree, you gain no Experience.  Instead, you may take
     * 1 Common Item" -- the Sold One and the Centaur both offer this. */
    if (c->special == AS_SPARE) {
        int spare;
        if (p->ai) spare = p->nitems < MAX_ITEMS;
        else {
            char m[150];
            snprintf(m, sizeof m,
                     "The beaten %s offers to serve if you spare it: an Item instead of %d Experience?  [y]/[n]",
                     c->name, c->exp);
            ui_draw();
            spare = (ui_prompt(m, "yn") == 'y');
        }
        if (spare) {
            int ii = item_draw();
            if (item_give(p, ii))
                glog("%s spares the %s, and is given the %s.", p->name, c->name,
                     item_proto[ii].name);
            else
                glog("%s spares the %s, but has no hand free.", p->name, c->name);
            return;
        }
    }

    /* "If you use a weapon, Shield or spell, you do not receive any
     * Experience for winning." */
    if (c->special == AS_FIST && !fought_bare_handed(p)) {
        glog("%s defeats the %s -- but not with his fists, and learns nothing.",
             p->name, c->name);
    } else {
        give_exp(p, c->exp);
        glog("%s defeats the %s. +%d Experience.", p->name, c->name, c->exp);
    }
    if (c->t_gold) { give_gold(p, c->t_gold); glog("  treasure: +%d Gold.", c->t_gold); }
    if (c->t_str)  { gain_str(p);  glog("  treasure: +1 Strength."); }
    if (c->t_will) { gain_will(p); glog("  treasure: +1 Willpower."); }
    /* "If you defeat him with Strength, gain 1 Willpower; if you defeat
     * him with Willpower, gain 1 Strength." */
    if (c->special == AS_SWAP_REWARD) {
        if (last_kind == B_STRENGTH) { gain_will(p); glog("  his head speaks: +1 Willpower."); }
        else                         { gain_str(p);  glog("  his head speaks: +1 Strength."); }
    }
    if (c->t_item) {
        int ii = item_draw();
        if (item_give(p, ii))
            glog("  treasure: %s (%s).", item_proto[ii].name, item_proto[ii].text);
    }
}

static int item_give(Player *p, int proto)
{
    if (proto < 0 || p->nitems >= MAX_ITEMS) return 0;
    p->dmg[p->nitems] = 0;                 /* it arrives sound */
    p->items[p->nitems++] = proto;
    return 1;
}

static void item_take(Player *p, int slot)
{
    int i;
    if (slot < 0 || slot >= p->nitems) return;
    for (i = slot; i < p->nitems - 1; i++) {
        p->items[i] = p->items[i+1];
        p->dmg[i]   = p->dmg[i+1];
    }
    p->nitems--;
    p->dmg[p->nitems] = 0;      /* leave no damage behind on the free slot */
}

static void creature_loss(Player *p, const Adventure *c)
{
    if (c->l_health || c->l_magic || c->l_gold) {
        if (c->l_health) { glog("The %s costs %s %d Health.", c->name, p->name, c->l_health); hurt(p, c->l_health); }
        if (c->l_magic)  { glog("The %s drains %d Magic.", c->name, c->l_magic); drain(p, c->l_magic); }
        if (c->l_gold)   { glog("The %s takes %d Gold.", c->name, c->l_gold); give_gold(p, -c->l_gold); }
    } else {
        glog("The %s wounds %s.", c->name, p->name);
        hurt(p, 1);
    }
}

/* 1 win, 0 loss, -1 draw.  A loss or a draw ends the turn. */
/* Skin of stone: "if you use a Weapon, it becomes damaged after the
 * battle" -- win or lose, the best weapon carried is turned face-down. */
static void stone_skin(Player *p, const Adventure *c)
{
    int i, bi = -1, best = 0;
    for (i = 0; i < p->nitems; i++) {
        const Item *it = &item_proto[p->items[i]];
        if (p->dmg[i] || it->d_str <= 0) continue;
        if (it->d_str > best) { best = it->d_str; bi = i; }
    }
    if (bi < 0) return;
    p->dmg[bi] = 1;
    glog("  the %s's stone hide ruins %s's %s.", c->name, p->name,
         item_proto[p->items[bi]].name);
}

/* Thievery: "Instead of a Health, you must give him an Item." */
static void take_an_item(Player *p, const Adventure *c)
{
    if (p->nitems <= 0) { hurt(p, 1); return; }   /* nothing to take but blood */
    glog("  the %s relieves %s of the %s.", c->name, p->name,
         item_proto[p->items[0]].name);
    item_take(p, 0);
}

static int battle_creature(Player *p, const Adventure *c)
{
    int paid, lives = c->lives ? c->lives : 1, i, res = 1;
    BattleKind k;
    int mine, theirs;

    glog("%s meets %s%s (Str %d / Will %d).", p->name,
         c->lives ? "a " : "the ", c->name, c->str, c->will);
    redraw(p);

    /* Nobility: "You do not need to fight." */
    if (c->special == AS_NOBILITY) {
        int pass;
        if (p->ai) pass = eff_str(p) < c->str + 2;
        else {
            char m[140];
            snprintf(m, sizeof m, "The %s lets you pass unchallenged.  Fight it anyway?  [y]/[n]",
                     c->name);
            ui_draw();
            pass = (ui_prompt(m, "yn") != 'y');
        }
        /* The encounter is resolved either way, so the Lion moves on.  If
         * it stayed, a character could decline it forever and the space
         * would never clear -- which is exactly what it did: 4271 passes
         * across 50 games, and one game ran to the turn cap. */
        if (pass) { glog("  %s passes the %s in peace.", p->name, c->name); beat(p); return 2; }
    }

    k = choose_kind(p, c, &paid);
    last_kind = k;
    p->refought = 0;          /* Fanaticism is once per battle, not per game */
    /* The Vampire must be broken in spirit first, and only then in body. */
    if (c->special == AS_TWO_BATTLES) {
        glog("  it must be beaten in a Battle of Wills first, then in Strength.");
        k = B_WILL;
    }
    mine   = (k == B_WILL) ? eff_will(p) : eff_str(p);
    theirs = (k == B_WILL) ? c->will     : c->str;
    /* Doppelganger: "Its Strength is twice your current Health." */
    if (c->special == AS_MIRROR) {
        theirs = p->str_now * 2;
        glog("  it wears %s's own face, and fights at Strength %d.", p->name, theirs);
    }
    mine  += battle_spell(p, k, c->name);
    mine  += throw_weapon(p, k, c->name, mine, theirs);
    /* The Sword's extra +1 is "against humanoids and other characters".
     * Adventure has no creature TYPE field yet, so only the character half
     * is applied here; see docs/base-cards.md -- Hunting, the Axe and the
     * Radiant Shield all want the same missing field. */
    mine  += art_battle(p, k, (c->ctype & CT_HUMANOID) != 0);
    mine  += perk_battle(p, k, c->ctype, 0);
    if (c->lives) glog("  it fights as a band: three rolls must all be won.");

    for (i = 0; i < lives; i++) {
        res = fight_roll(p, c->name, mine, theirs, k);
        beat(p);
        if (res != 1) break;
    }

    /* the Vampire's second battle: having broken its will, now its body */
    if (res == 1 && c->special == AS_TWO_BATTLES) {
        mine   = eff_str(p) + battle_spell(p, B_STRENGTH, c->name);
        theirs = c->str;
        mine  += throw_weapon(p, B_STRENGTH, c->name, mine, theirs);
        res = fight_roll(p, c->name, mine, theirs, B_STRENGTH);
        beat(p);
    }

    if (c->special == AS_STONE_SKIN) stone_skin(p, c);

    if (res == 1) {
        creature_reward(p, c);
        /* Poison works whether or not you won the fight. */
        if (c->special == AS_POISON) {
            glog("  the venom works regardless: %s loses 2 Health.", p->name);
            hurt(p, 2);
        }
    } else if (res == 0) {
        if (art_mirror(p, NULL, c->name)) {
            turn_over = 1;              /* a draw ends the turn just the same */
        } else {
        if (c->special == AS_THIEVERY)   take_an_item(p, c);
        else                             creature_loss(p, c);
        if (c->special == AS_STUN) { glog("  stunned: it takes 2 Magic as well."); drain(p, 2); }
        turn_over = 1;
        }
    } else {
        if (has_perk(p, PK_FANATIC) && !p->refought) {
            p->refought = 1;
            glog("  %s will not accept a draw, and sets on it again.", p->name);
            res = fight_roll(p, c->name, mine, theirs, k);
            beat(p);
            if (res == 1)      creature_reward(p, c);
            else if (res == 0) creature_loss(p, c);
        }
        if (res != 1) {
            glog("Neither can master the other. %s's turn ends.", p->name);
            turn_over = 1;
        }
    }
    beat(p);
    return res;
}

/* Rulebook: a Battle of Wills against a character costs 2 Magic plus one
 * more for every Artifact she carries -- legends are hard to reach. */
static int wills_price(const Player *foe) { return 2 + foe->artifacts; }

static int player_safe(const Player *p)
{
    return (ring[p->idx].safe || p->safe_until_turn) && p->artifacts == 0;
}

/* Rulebook: "the loser chooses an Artifact to give to the winner", so the
 * loser parts with the one that helps her least.  A Banner she is about to
 * roll is worth more to her than a flat bonus she can replace. */
static int worst_art(const Player *p)
{
    int i, worst = -1, wv = 1 << 30;
    for (i = 0; i < ART_N; i++) {
        int v;
        if (!has_art(p, i)) continue;
        v = art_proto[i].banner ? 3 : 0;
        v += art_proto[i].goanywhere ? 2 : 0;
        v += art_proto[i].battle_str * 2 + art_proto[i].d_str * 2;
        v += art_proto[i].save ? 2 : 0;
        if (v < wv) { wv = v; worst = i; }
    }
    return worst;
}

static void take_artifact(Player *win, Player *lose)
{
    int id = worst_art(lose);

    if (id >= 0) {
        lose->art_mask &= ~(1u << id);
        win->art_mask  |=  (1u << id);
        glog("  the %s changes hands.", art_proto[id].name);
    }
    lose->artifacts--;
    win->artifacts++;
    glog("  %s surrenders an Artifact to %s (%d to %d).",
         lose->name, win->name, lose->artifacts, win->artifacts);
    if (lose->artifacts <= 0) {
        lose->alive = 0;
        glog("*** %s holds nothing, and is cast out of the story. ***", lose->name);
    }
}

static void battle_players(Player *a, Player *d)
{
    BattleKind k = B_STRENGTH;
    int mine, theirs, res, want;

    glog("%s challenges %s.", a->name, d->name);
    redraw(a);

    /* the attacker may reach for a Battle of Wills, then the defender may */
    if (a->ai) want = (eff_will(a) - wills_price(d) > eff_str(a)) && a->will_now >= wills_price(d);
    else {
        char m[160];
        snprintf(m, sizeof m, "%s   [s] Strength %d v %d    [w] Wills %d v %d  (costs %d Magic)",
                 d->name, eff_str(a), eff_str(d),
                 eff_will(a) - wills_price(d), eff_will(d), wills_price(d));
        ui_draw();
        want = a->will_now >= wills_price(d) && ui_prompt(m, "sw") == 'w';
    }
    if (want) { drain(a, wills_price(d)); k = B_WILL;
                glog("%s forces mental contact.", a->name); }

    mine   = (k == B_WILL) ? eff_will(a) : eff_str(a);
    theirs = (k == B_WILL) ? eff_will(d) : eff_str(d);
    mine  += battle_spell(a, k, d->name);
    theirs+= battle_spell(d, k, a->name);
    mine  += art_battle(a, k, 1);
    theirs+= art_battle(d, k, 1);
    mine  += perk_battle(a, k, d->ctype, 1);
    theirs+= perk_battle(d, k, a->ctype, 1);
    if (k == B_WILL && has_perk(a, PK_MAGICDRAIN) && d->will_now > 0) {
        drain(d, 1);
        glog("  %s draws a Magic out of %s as their minds touch.", a->name, d->name);
    }
    mine  += throw_weapon(a, k, d->name, mine, theirs);
    theirs+= throw_weapon(d, k, a->name, theirs, mine);
    res = fight_roll(a, d->name, mine, theirs, k);
    beat(a);

    if (res == -1) { glog("They break apart, neither the better."); return; }
    {
        Player *w = res == 1 ? a : d, *l = res == 1 ? d : a;
        if (art_mirror(l, w, w->name)) return;   /* the loss is reflected */
        glog("%s overcomes %s.", w->name, l->name);
        /* Astral Sword: "you drain 1 Magic: she loses 1 Magic and you
         * recharge 1 Magic" -- only against another character. */
        if (has_art(w, ART_SWORD) && l->will_now > 0) {
            drain(l, 1);
            if (w->will_now + w->will_lost > 0 && w->will_lost > 0) {
                w->will_lost--; w->will_now++;
            }
            glog("  the Astral Sword drinks a Magic from %s.", l->name);
        }
        if (final_battle && l->artifacts > 0) { take_artifact(w, l); return; }
        if (l->nitems > 0) {                     /* the winner may take an Item */
            int slot = 0;
            if (w->nitems < MAX_ITEMS) {
                /* loot arrives in whatever state it was in: a plundered
                 * shield is still a broken shield */
                int dam = l->dmg[slot];
                item_give(w, l->items[slot]);
                w->dmg[w->nitems - 1] = dam;
                glog("  %s takes the %s%s.", w->name,
                     item_proto[l->items[slot]].name, dam ? " (damaged)" : "");
                item_take(l, slot);
                return;
            }
        }
        hurt(l, 1);
    }
}

/* ------------------------------------------------- the Astral Planes -- */
/* Set a Guardian up as a Creature and exact its toll first.  Returns what
 * battle_creature returns, or 0 if the toll alone laid the character out. */
static int fight_guardian(Player *p, const Guardian *g, int treasure)
{
    Adventure a;
    int r;

    memset(&a, 0, sizeof a);
    a.name = g->name; a.str = g->str; a.will = g->will;
    if (power_up) {                 /* Dragon Realm strengthens the rest */
        if (a.str)  a.str++;
        if (a.will) a.will++;
        glog("  the %s is the stronger for the dragons' coming.", g->name);
    }
    a.str_first = g->str_first; a.exp = g->exp; a.special = g->special;
    if (treasure) { a.t_item = 1; a.t_will = 1; }

    if (g->text) glog("  %s", g->text);
    if (g->t_health) { glog("  the toll is %d Health.", g->t_health); hurt(p, g->t_health); }
    if (g->t_magic)  { glog("  the toll is %d Magic.",  g->t_magic);  drain(p, g->t_magic); }
    if (!p->alive) return 0;

    spells_barred = g->no_spells;
    r = battle_creature(p, &a);
    spells_barred = 0;
    return r;
}

static void assault_plane(Player *p, int pl)
{
    const Guardian *g;

    /* A Plane whose Artifact is already gone has nothing left to fight for.
     * The main board never offers the move, but both Realms call this
     * directly when a character reaches the Lake of Fire or the Undersea
     * Palace -- and without this the dead Guardian was fought again and the
     * Artifact counted twice, while the mask kept the one bit it had. */
    if (pl < 0 || pl >= PLANES || planes[pl].closed) {
        glog("  the way beyond the %s is empty; its Artifact is long gone.",
             plane_name(pl));
        return;
    }
    glog(">>> %s attacks the Astral Plane beyond the %s. <<<", p->name, plane_name(pl));
    planes[pl].revealed = 1;
    redraw(p);

    if (planes[pl].lesser) {
        g = &guard_proto[plane_lesser[pl]];
        glog("The %s bars the way.", g->name);
        if (fight_guardian(p, g, 1) != 1) return;        /* thrown back */
        planes[pl].lesser = 0;
        glog("The lesser guard is broken.");
        /* Two Guardians back to back in one turn, with no chance to stop
         * between them, is what killed most of the heroes who died at a
         * Plane: they survived the Lesser and were finished by the Greater.
         * Breaking the outer guard is enough for one day. */
        if (p->ai) {
            const Guardian *gr = &guard_proto[plane_greater[pl]];
            int mine = eff_str(p) > eff_will(p) ? eff_str(p) : eff_will(p);
            int need = (gr->will && !gr->str) ? gr->will : gr->str;
            if (p->str_now <= 3 || win_chance(mine, need, 1) < 60) {
                glog("  %s draws back to gather herself before the inner guard.",
                     p->name);
                return;
            }
        }
    }
    if (!p->alive) return;

    g = &guard_proto[plane_greater[pl]];
    glog("The %s rises between %s and the Artifact.", g->name, p->name);
    /* "guardians of all other Astral Planes are strengthened.  Both their
     * Strength and their Willpower is higher by 1." */
    if (use_dragon && pl != dr_gate) power_up = 1;
    if (fight_guardian(p, g, 0) != 1) { power_up = 0; return; }
    power_up = 0;
    planes[pl].greater = planes[pl].artifact = 0;
    planes[pl].closed  = 1;
    p->artifacts++;
    p->art_mask |= 1u << plane_art[pl];
    glog("*** %s takes the %s from beyond the %s -- %d of %d. ***",
         p->name, art_proto[plane_art[pl]].name, plane_name(pl),
         p->artifacts, WIN_ARTIFACTS);
    glog("    %s", art_proto[plane_art[pl]].text);
    beat(p);
}

/* ------------------------------------------------- what a place offers -- */
static void learn_ability(Player *p, Guild g)
{
    int ai = guild_abil[g], cost, extra, want;
    const AbilityCard *a;

    if (ai < 0 || p->nabils >= abil_cap(p)) return;
    a = &abil_proto[ai];
    cost  = a->cost + racial(p)->abil_cost;
    if (cost < 1) cost = 1;
    /* not one of your guilds? then it costs the same again in Gold --
     * unless you wear the Crown: "you do not pay Gold for any training
     * in any guild", which quietly opens all five guilds to you. */
    extra = (p->guild_a == g || p->guild_b == g) ? 0 : cost;
    if (extra && p->free_training) {
        extra = 0; p->free_training = 0;
        glog("  the Letter of Recommendation settles the fee.");
    }
    if (extra && has_art(p, ART_CROWN)) {
        extra = 0;
        glog("  the Crown of the Ancient Kings opens the doors: no Gold owed.");
    }
    if (p->exp < cost || p->gold < extra) return;

    if (p->ai) want = 1;
    else {
        char m[140];
        snprintf(m, sizeof m, "Learn %s (%s) for %d Experience%s?  [y]/[n]",
                 a->name, a->text, cost, extra ? " and as much Gold" : "");
        ui_draw();
        want = (ui_prompt(m, "yn") == 'y');
    }
    if (!want) return;
    p->exp -= cost;
    give_gold(p, -extra);
    p->abils[p->nabils++] = ai;
    guild_abil[g] = -1;
    glog("%s learns %s at the %s (%s).", p->name, a->name, guild_name[g], a->text);
    redraw(p);
}

static void buy_item(Player *p)
{
    int ii = space_item[p->idx], want, price;
    const Item *it;

    if (ii < 0 || p->nitems >= MAX_ITEMS) return;
    it = &item_proto[ii];
    /* quote the price once: asking twice would spend the Counterfeiting on
     * the question and then charge her full price for the answer */
    price = quote_price(p, it->price);
    if (p->gold < price) return;
    if (p->ai) want = 1;
    else {
        char m[160];
        snprintf(m, sizeof m, "Buy the %s (%s) for %d Gold?%s  [y]/[n]",
                 it->name, it->text, price,
                 price < it->price ? "  (haggled down)" : "");
        ui_draw();
        want = (ui_prompt(m, "yn") == 'y');
    }
    if (!want) return;
    pay(p, price);
    item_give(p, ii);
    space_item[p->idx] = -1;
    glog("%s buys the %s for %d Gold.", p->name, it->name, price);
    redraw(p);
}

/* Rulebook: "In a Civilization space (a blue space: any guild, the City
 * or the Village), it is possible to repair an Item.  For each repaired
 * Item you must pay 1 Gold." */
static void repair_items(Player *p)
{
    int i;
    if (!IS_CIV(ring[p->idx].kind)) return;
    /* A smith needs no Gold, so the purse may be empty and she still works. */
    for (i = 0; i < p->nitems && (p->gold >= 1 || has_perk(p, PK_SMITH)); i++) {
        int price;
        if (!p->dmg[i]) continue;
        price = quote_price(p, 1);
        if (has_perk(p, PK_SMITH)) {
            price = 0;                 /* "You can repair an Item" -- freely */
            glog("  %s works the forge and mends the %s.", p->name,
                 item_proto[p->items[i]].name);
        }
        if (p->gold < price) break;
        if (!p->ai) {
            char m[140];
            snprintf(m, sizeof m, "Repair the %s for %d Gold?  [y]/[n]",
                     item_proto[p->items[i]].name, price);
            ui_draw();
            if (ui_prompt(m, "yn") != 'y') continue;
        }
        pay(p, price);
        p->dmg[i] = 0;
        glog("%s has the %s mended for %d Gold.", p->name,
             item_proto[p->items[i]].name, price);
    }
}

/* Rulebook: "the price is half, rounded up... You must repair damaged
 * Items before selling them.  Sold Items do not stay on the same space --
 * they are discarded." */
static int resale(const Player *p, int proto)
{
    int g = (item_proto[proto].price + 1) / 2;
    return has_perk(p, PK_HAGGLE) ? g + 1 : g;   /* she drives a harder bargain */
}

static void sell_items(Player *p)
{
    SpaceKind k = ring[p->idx].kind;
    int i, sound = 0;

    if (k != SP_CITY && k != SP_VILLAGE) return;
    for (i = 0; i < p->nitems; i++) if (!p->dmg[i]) sound++;   /* mend before selling */
    if (!sound) return;

    if (p->ai) {
        /* The AI parts with something only when it is flat broke and has
         * more than one Item -- then it sells the least useful. */
        int worst = -1, wv = 99;
        if (p->gold > 0 || p->nitems < 2) return;
        for (i = 0; i < p->nitems; i++) {
            const Item *it = &item_proto[p->items[i]];
            int v = it->d_str + it->d_will + it->thrown;
            if (!p->dmg[i] && v < wv) { wv = v; worst = i; }
        }
        if (worst < 0) return;
        give_gold(p, resale(p, p->items[worst]));
        glog("%s sells the %s for %d Gold.", p->name,
             item_proto[p->items[worst]].name, resale(p, p->items[worst]));
        item_take(p, worst);
        return;
    }

    /* A human is asked once, not once per Item every time she passes through. */
    ui_draw();
    if (ui_prompt("Sell anything here?  [y]/[n]", "yn") != 'y') return;
    for (i = 0; i < p->nitems; i++) {
        const Item *it = &item_proto[p->items[i]];
        char m[160];
        if (p->dmg[i]) continue;
        snprintf(m, sizeof m, "Sell the %s (%s) for %d Gold?  [y]/[n]",
                 it->name, it->text, resale(p, p->items[i]));
        ui_draw();
        if (ui_prompt(m, "yn") != 'y') continue;
        give_gold(p, resale(p, p->items[i]));
        glog("%s sells the %s for %d Gold.", p->name, it->name, resale(p, p->items[i]));
        item_take(p, i);
        i--;
    }
}

static void use_possibilities(Player *p)
{
    const Space *sp = &ring[p->idx];

    repair_items(p);
    sell_items(p);

    switch (sp->kind) {
    case SP_VILLAGE:
        if (p->gold >= 1 && (p->str_lost || p->will_lost)) {
            give_gold(p, -1); heal(p, 1); recharge(p, 1);
            p->safe_until_turn = 1;
            glog("%s takes lodging in the Village: 1 Gold, heal 1, recharge 1, and safe.", p->name);
        }
        buy_item(p);
        break;
    case SP_CITY:
        buy_item(p);
        break;
    case SP_MONASTERY:
        if (p->str_lost) { heal(p, 1); glog("The Monastery heals %s a Health, freely.", p->name); }
        learn_ability(p, G_MONASTERY);
        break;
    case SP_FOREST_CAMP:
        while (p->str_lost && p->gold >= 1) { give_gold(p, -1); heal(p, 1); }
        glog("%s rests at the Forest Camp.", p->name);
        learn_ability(p, G_CAMP);
        break;
    case SP_MAGIC_TOWER:
        while (p->will_lost >= 2 && p->gold >= 1) { give_gold(p, -1); recharge(p, 2); }
        learn_ability(p, G_TOWER);
        break;
    case SP_THIEVES_GUILD:
        learn_ability(p, G_THIEVES);
        break;
    case SP_FORTRESS:
        learn_ability(p, G_FORTRESS);
        break;
    case SP_WILDERNESS:
        if (p->will_lost) { recharge(p, 3); glog("The Enchanted Wilderness gives %s 3 Magic.", p->name); }
        break;
    default:
        break;                                  /* wilderness: the cards were it */
    }
}

/* An Opportunity is optional, and may not be used for nothing. */
static void use_opportunity(Player *p, const Adventure *c, int idx, int slot)
{
    int useful = (c->o_bargain && p->gold && (p->str_lost || p->will_lost >= 2))
               || (c->o_heal && p->str_lost) || (c->o_magic && p->will_lost)
               || (c->o_full_heal && p->str_lost) || (c->o_full_magic && p->will_lost)
               || c->o_gold || c->o_exp || c->o_str || c->o_will
               || (c->o_bless && !p->blessed);
    int want;

    if (!useful) return;
    if (c->cost_gold   && p->gold     < c->cost_gold)   return;
    if (c->cost_exp    && p->exp      < c->cost_exp)    return;
    /* never let a card kill you for a favour */
    if (c->cost_health && p->str_now <= c->cost_health) return;
    if (p->ai) want = 1;
    else {
        char m[140];
        snprintf(m, sizeof m, "%s -- %s%s.  Use it?  [y]/[n]", c->name, c->text,
                 c->cost_gold ? "" : " (free)");
        ui_draw();
        want = (ui_prompt(m, "yn") == 'y');
    }
    if (!want) return;
    if (c->cost_gold)   give_gold(p, -c->cost_gold);
    if (c->cost_exp)    give_exp(p, -c->cost_exp);
    if (c->cost_health) hurt(p, c->cost_health);
    /* The Apothecary's card: "Heal any amount of Health (pay 1 Gold per
     * point) and recharge any amount of Magic (4 Gold per 2 points)."  It
     * was a flat two-and-two for two Gold, which is both capped and about a
     * third of the price the card asks. */
    if (c->o_bargain) {
        int spend, mag;
        spend = p->str_lost < p->gold ? p->str_lost : p->gold;
        if (spend > 0) {
            give_gold(p, -spend);
            heal(p, spend);
            glog("  %d Gold for %d Health.", spend, spend);
        }
        mag = p->will_lost / 2;                    /* 4 Gold buys two points */
        if (mag > p->gold / 4) mag = p->gold / 4;
        if (mag > 0) {
            give_gold(p, -mag * 4);
            recharge(p, mag * 2);
            glog("  %d Gold for %d Magic.", mag * 4, mag * 2);
        }
        if (spend <= 0 && mag <= 0) glog("  ...but there is no coin for it.");
    } else {
        if (c->o_heal)  heal(p, c->o_heal);
        if (c->o_magic) recharge(p, c->o_magic);
    }
    if (c->o_full_heal)  { heal(p, p->str_lost);      glog("  healed to the last drop."); }
    if (c->o_full_magic) { recharge(p, p->will_lost); glog("  every drop of Magic returns."); }
    if (c->o_bless) { p->blessed = 1; glog("  the blessing will hold for one battle."); }
    if (c->o_gold)  give_gold(p, c->o_gold);
    if (c->o_exp)   give_exp(p, c->o_exp);
    if (c->o_str)   gain_str(p);
    if (c->o_will)  gain_will(p);
    glog("%s uses %s: %s.", p->name, c->name, c->text);
    remove_adventure(idx, slot);
    redraw(p);
}

/* ----------------------------------------------------------- movement -- */
static int space_worth(const Player *p, int idx);

/* The Spells you use instead of moving, and the two that ARE a move.
 * Returns the abils[] index of the best one, or -1. */
static int pick_utility_spell(const Player *p, int *dest, int *worth)
{
    int i, bi = -1, bv = 0;
    *dest = p->idx; *worth = 0;
    for (i = 0; i < p->nabils; i++) {
        const AbilityCard *a = &abil_proto[p->abils[i]];
        int v = 0, d = p->idx;
        if (a->magic > p->will_now) continue;
        switch (a->kind) {
        /* what a Spell is worth depends on how badly you need it: Magic you
         * have no room for is worth nothing, a heal at one Health is worth
         * a great deal more than the same heal at full Health. */
        case SP_MEDITATE: {
            int room = MAX_WILL - p->will_now;
            v = room < a->power ? 0 : a->power;
            if (v && p->will_now <= MAX_WILL / 2) v += 3;   /* half empty  */
            if (v && p->will_now <= 2)            v += 4;   /* nearly dry  */
            break;
        }
        case SP_HEAL:
            v = p->str_lost ? (p->str_lost < a->power ? p->str_lost : a->power)
                              + (p->str_now <= 2 ? 5 : 0) : 0;
            break;
        case SP_SACRIFICE: v = (p->str_now > 3 && MAX_WILL - p->will_now >= a->power)
                               ? a->power - 2 : 0; break;  /* a Health is worth ~2 */
        case SP_MOVE_FOREST: case SP_MOVE_MOUNTAIN: {
            SpaceKind t = (a->kind == SP_MOVE_FOREST) ? SP_FOREST : SP_MOUNTAINS;
            int j;
            for (j = 0; j < RING_N; j++) {
                int w;
                if (ring[j].kind != t || j == p->idx) continue;
                w = space_worth(p, j);
                if (w > v) { v = w; d = j; }
            }
            break;
        }
        default: break;
        }
        if (v > bv) { bv = v; bi = i; *dest = d; }
    }
    *worth = bv;
    return bi;
}

/* Every Spell this character could cast instead of moving.  The AI picks
 * by score; a human is shown the lot and chooses for herself. */
static int list_utility_spells(const Player *p, int *out)
{
    int i, n = 0;
    for (i = 0; i < p->nabils && n < 9; i++) {
        SpellKind k = abil_proto[p->abils[i]].kind;
        if (k == SP_PASSIVE || k == SP_BATTLE_STR || k == SP_BATTLE_WILL) continue;
        if (abil_proto[p->abils[i]].magic > p->will_now) continue;
        out[n++] = i;
    }
    return n;
}

/* Where a Movement Spell should take you: the best space of its terrain. */
static int spell_dest(Player *p, int si)
{
    const AbilityCard *a = &abil_proto[p->abils[si]];
    SpaceKind t;
    int j, best = p->idx, bv = -1000;
    if (a->kind != SP_MOVE_FOREST && a->kind != SP_MOVE_MOUNTAIN) return p->idx;
    t = (a->kind == SP_MOVE_FOREST) ? SP_FOREST : SP_MOUNTAINS;
    for (j = 0; j < RING_N; j++) {
        int w;
        if (ring[j].kind != t || j == p->idx) continue;
        w = space_worth(p, j);
        if (w > bv) { bv = w; best = j; }
    }
    return best;
}

/* Apply a Spell chosen instead of moving.  Movement spells have already
 * had their destination resolved into m.dest by the caller. */
static void cast_utility(Player *p, int si)
{
    const AbilityCard *a = &abil_proto[p->abils[si]];
    drain(p, a->magic);
    switch (a->kind) {
    case SP_MEDITATE:  recharge(p, a->power);
        glog("%s meditates and recharges %d Magic.", p->name, a->power); break;
    case SP_HEAL:      heal(p, a->power);
        glog("%s calls down %s: %d Health.", p->name, a->name, a->power); break;
    case SP_SACRIFICE: hurt(p, 1); recharge(p, a->power);
        glog("%s sacrifices a Health for %d Magic.", p->name, a->power); break;
    default:
        glog("%s takes the hidden ways (%s).", p->name, a->name); break;
    }
}

enum { MV_STAY, MV_WALK, MV_HORSE, MV_BOAT, MV_GATE, MV_WORK, MV_PLANE, MV_SPELL };

typedef struct { int kind, dest, dir, plane, spell; } Move;

static int gate_other(int from)
{
    int i, best = -1;
    for (i = 0; i < RING_N; i++)
        if (ring[i].gate && i != from) { best = i; break; }
    return best;
}

/* ------------------------------------------------------ win probability --
 * A Battle here is `mine + d6` against `theirs + d6`, so the chance of
 * winning one is a sum over the 36 ways two dice fall, not a guess.  A tie
 * is a draw that ends the turn, so only a strict win counts.
 *
 * A band is the case an approximation cannot reach: it "fights as a band:
 * three rolls must all be won", so the real chance is p^lives -- against a
 * Pack of Wolves at even odds that is 41%^3, about one in fifteen, not the
 * "a bit harder" that adding to its Strength suggests. */
static int win_chance(int mine, int theirs, int lives)
{
    int start = 1 - (mine - theirs), k, ways = 0, p1, i;

    if (start < -5) start = -5;
    for (k = start; k <= 5; k++) ways += 6 - (k < 0 ? -k : k);
    p1 = ways * 100 / 36;
    for (i = 1; i < (lives > 1 ? lives : 1); i++) p1 = p1 * ways / 36;
    return p1;
}

#define WORTH_FIGHTING 50
#define DESPERATE      33

/* Which fight this creature will actually be, and with what on each side.
 *
 * space_worth() used to assume Strength against anything carrying both
 * stats, while choose_kind() reaches for Will whenever the hero is the
 * better mind and can pay the 2 Magic -- so the scorer was pricing a
 * different fight from the one the hero would walk into.  One function
 * now, so the two cannot drift apart. */
static void matchup(const Player *p, const Adventure *c, int *mine, int *theirs)
{
    if (c->will && !c->str) { *mine = eff_will(p); *theirs = c->will; return; }
    if (c->str  && !c->will){ *mine = eff_str(p);  *theirs = c->str;  return; }
    if (eff_will(p) - 2 > eff_str(p) && p->will_now >= 2) {
        *mine = eff_will(p) - 2; *theirs = c->will;   /* the 2 Magic is paid */
    } else {
        *mine = eff_str(p);      *theirs = c->str;
    }
}

/* How much this hero would like to be standing on that space. */
static int space_worth(const Player *p, int idx)
{
    const Space *sp = &ring[idx];
    int v = 0, i;

    /* SURVIVAL REFLEX: If health is critical, prioritize safety and healing */
    if (p->str_now <= 2) {
        if (sp->kind == SP_VILLAGE || sp->kind == SP_MONASTERY || sp->kind == SP_FOREST_CAMP) {
            v += 20; /* Massive weight boost to healing/safe havens */
        }
        /* Heavily penalize spaces that have unvanquished creatures */
        for (i = 0; i < space_nadv[idx]; i++) {
            const Adventure *c = &adv_proto[space_adv[idx][i]];
            if (c->type == A_CREATURE) {
                v -= 30; 
            }
        }
    }

    for (i = 0; i < space_nadv[idx]; i++) {
        const Adventure *c = &adv_proto[space_adv[idx][i]];
        if (c->type == A_OPPORTUNITY) v += 6;
        else {
            int mine, theirs, win;
            matchup(p, c, &mine, &theirs);

            /* A band must be beaten three times over, which the odds
             * below already account for exactly. */
            win = win_chance(mine, theirs, c->lives);

            /* Venom is paid whether or not you win, so at low Health a
             * winnable fight is still a fatal one. */
            if (c->special == AS_POISON && p->str_now <= 2) win = 0;

            /* A loss is a scratch at full Health and the end of the run at
             * two, so the same bad fight is not the same bad idea.  This
             * penalty used to be flat, which is how a hero with three
             * Health walked onto a space for a nine-point item and met the
             * thing standing on it. */
            {
                int frail = (p->str_now <= 2) ? 5 : (p->str_now <= 4) ? 2 : 1;
                v += (win >= 65) ? 5 + c->exp
                   : (win >= WORTH_FIGHTING) ? 2
                   : (win >= DESPERATE) ? -2 * frail : -8 * frail;
            }
        }
    }
    switch (sp->kind) {
    case SP_MONASTERY:   v += p->str_lost  ? 5 : 0; break;
    case SP_FOREST_CAMP: v += (p->str_lost && p->gold) ? 5 : 0; break;
    case SP_MAGIC_TOWER: v += (p->will_lost >= 2 && p->gold) ? 4 : 0; break;
    case SP_WILDERNESS:  v += p->will_lost ? 6 : 0; break;
    case SP_VILLAGE:     v += (p->gold && (p->str_lost || p->will_lost)) ? 3 : 0; break;
    default: break;
    }
    if (IS_GUILD(sp->kind)) {
        Guild g = guild_of(sp->kind);
        if (guild_abil[g] >= 0 && p->exp >= abil_proto[guild_abil[g]].cost) v += 8;
    }
    if (space_item[idx] >= 0 && p->gold >= item_proto[space_item[idx]].price) v += 9;
    return v;
}

/* Is this hero ready to walk into an Astral Plane? */
static int plane_ready(Player *p, int pl)
{
    const Guardian *l = &guard_proto[plane_lesser[pl]];
    const Guardian *g = &guard_proto[plane_greater[pl]];
    
    /* NEW: Check if the Guardian forces a Will battle */
    int l_req = (l->will && !l->str) ? l->will : l->str;
    int g_req = (g->will && !g->str) ? g->will : g->str;
    
    int needed = planes[pl].lesser ? (l_req > g_req ? l_req : g_req) : g_req;
    int mine   = eff_str(p) > eff_will(p) ? eff_str(p) : eff_will(p);

    if (planes[pl].closed) return 0;

    /* `mine >= needed` is a coin flip at parity, and the Guardians take a
     * toll before a blow is struck -- which is how a Scout with 5 Strength
     * walked into the Tortured Souls on round five, and how a wounded one
     * kept attacking the Fire-Breathing Dragon until it killed her.  Ask
     * the odds instead, and refuse the fight while hurt. */
    {
        int toll = (planes[pl].lesser ? l->t_health + l->t_magic : 0)
                 + g->t_health + g->t_magic;
        int odds = win_chance(mine, needed, 1);
        if (p->str_now <= 3 || p->will_now <= 2) return 0;   /* heal first */
        if (p->str_now + p->will_now <= toll + 4) return 0;  /* the toll alone would break her */
        return odds >= 60;
    }
}

/* Where the nearest place that will mend her is, and which way to walk.
 * The old survival reflex only weighed the squares she could already reach,
 * so a hero on two Health with the nearest Monastery five steps off simply
 * stood among the wolves. */
static int nearest_haven(const Player *p, int *dir)
{
    int d, best = -1, bd = 99;
    for (d = 0; d < RING_N; d++) {
        SpaceKind k = ring[d].kind;
        int gap;
        if (k != SP_MONASTERY && k != SP_VILLAGE && k != SP_FOREST_CAMP &&
            k != SP_CITY) continue;
        gap = ring_step(d - p->idx, 0);
        if (gap > RING_N / 2) gap = RING_N - gap;
        if (gap < bd) { bd = gap; best = d; }
    }
    if (best < 0) return -1;
    { int fwd = ring_step(best - p->idx, 0);
      *dir = (fwd <= RING_N / 2) ? 1 : -1; }
    return bd;
}

/* Which of ai_move()'s branches chose the move.  A hero who walks two
 * spaces because the ground is good and one who walks two because she is
 * bleeding look identical in the log unless it says which. */
static const char *why_rule = "-";

/* The name of a ring space, for the trace. */
static const char *why_where(int idx)
{
    return (idx >= 0 && idx < RING_N) ? ring[idx].name : "-";
}

static Move ai_move(Player *p)
{
    Move best; int i, bv = -1000;
    int pl = plane_at(p->idx);
    int runner = -1000;              /* the best option that was not taken */

    why_rule = "-";
    best.kind = MV_STAY; best.dest = p->idx; best.dir = 0; best.plane = -1;

    /* Survival override: badly hurt, and not already somewhere that mends,
     * she walks for the nearest haven and does nothing else. */
    if (p->str_now <= 2 && !p->in_realm && !p->in_water && !final_battle) {
        int dir, gap = nearest_haven(p, &dir);
        SpaceKind here = ring[p->idx].kind;
        int mending = (here == SP_MONASTERY || here == SP_VILLAGE ||
                       here == SP_FOREST_CAMP || here == SP_CITY);
        if (!mending && gap > 0) {
            int hop = (gap >= 2 && p->gold >= 1) ? 2 : 1;
            best.kind = (hop == 2) ? MV_HORSE : MV_WALK;
            best.dest = ring_step(p->idx, dir * hop);
            best.dir  = dir;
            glog("  %s is in no state to fight, and makes for shelter.", p->name);
            why_rule = "shelter";
            wlog("  why %s  [shelter]  to %s, %d space%s away  (Str %d)",
                 p->name, why_where(best.dest), gap, gap == 1 ? "" : "s",
                 p->str_now);
            return best;
        }
    }

    if (pl >= 0 && plane_ready(p, pl)) {
        best.kind = MV_PLANE; best.plane = pl;
        why_rule = "take-the-plane";
        wlog("  why %s  [take-the-plane]  at %s, %d Artifact%s held",
             p->name, why_where(p->idx), p->artifacts,
             p->artifacts == 1 ? "" : "s");
        return best;
    }
    if (final_battle) {                 /* nothing is left but each other */
        int j, want = -1, wd = 99;
        for (j = 0; j < nplayers; j++) {
            Player *q = &players[j];
            int d;
            if (q == p || !q->alive || q->artifacts == 0) continue;
            d = ring_step(q->idx - p->idx, 0);
            if (d > RING_N / 2) d = RING_N - d;
            if (d < wd) { wd = d; want = j; }
        }
        if (want >= 0) {
            int fwd = ring_step(players[want].idx - p->idx, 0);
            int dir = (fwd <= RING_N / 2) ? 1 : -1;
            /* CHANGE: Make them more willing to spend gold on rapid transit (horses) to catch the target */
            int hop = (wd >= 2 && p->gold >= 1) ? 2 : (wd >= 1 ? 1 : 0);
            best.kind = hop == 0 ? MV_STAY : hop == 1 ? MV_WALK : MV_HORSE;
            best.dest = ring_step(p->idx, dir * hop);
            why_rule = "run-them-down";
            wlog("  why %s  [run-them-down]  after %s, %d away  -> %s",
                 p->name, players[want].name, wd, why_where(best.dest));
            return best;
        }
    }
    {   /* a Spell cast instead of moving, weighed like any other option */
        int d, w, si = pick_utility_spell(p, &d, &w);
        if (si >= 0) {
            const AbilityCard *a = &abil_proto[p->abils[si]];
            int v = (a->kind == SP_MOVE_FOREST || a->kind == SP_MOVE_MOUNTAIN)
                    ? space_worth(p, d) : w;
            if (v > bv) { runner = bv; bv = v; why_rule = "a-spell";
                          best.kind = MV_SPELL; best.spell = si; best.dest = d; }
            else if (v > runner) runner = v;
        }
    }
    /* the three "instead of moving" labours, when they are worth it */
    /* The Fortress branch below asks whether training beats what is already
     * on the table; this one did not -- it set the score to 4 whatever it
     * had been, so a City labour displaced a Spell worth 12.  Eight times in
     * a hundred games the hero took the move she had scored worse, which is
     * the only rule in here that ever did. */
    if (ring[p->idx].kind == SP_CITY && p->will_now >= 1 && p->gold <= MAX_GOLD - 2) {
        if (4 > bv) { runner = bv; bv = 4; best.kind = MV_WORK;
                      why_rule = "work-for-gold"; }
        else if (4 > runner) runner = 4;
    }
    if (ring[p->idx].kind == SP_FORTRESS && p->str_now >= 3 && p->exp <= MAX_EXP - 2) {
        if (5 > bv) { runner = bv; bv = 5; best.kind = MV_WORK;
                      why_rule = "train-for-experience"; }
    }
    for (i = -2; i <= 2; i++) {
        int dest = ring_step(p->idx, i), v;
        if (i && (i < -1 || i > 1) && p->gold < 1) continue;
        v = space_worth(p, dest) - (i ? 0 : 1) - ((i < -1 || i > 1) ? 2 : 0);
        /* Every neighbour holding a creature she cannot beat scores worse
         * than standing still, and standing still was free -- so a weak
         * hero surrounded by bad ground stopped playing for good.  Idling
         * now costs more the longer it goes on, which guarantees she moves
         * again eventually even if every direction looks poor. */
        if (i == 0) v -= p->idle;
        {   /* getting next to a Plane you can take is worth more than anything */
            int q = plane_at(dest);
            if (q >= 0 && plane_ready(p, q)) v += 25;
        }
        if (v > bv) {
            runner = bv; bv = v; best.dest = dest; best.dir = i < 0 ? -1 : 1;
            best.kind = (i == 0) ? MV_STAY : (i == -1 || i == 1) ? MV_WALK : MV_HORSE;
            why_rule = (i == 0) ? "stand-still" : "best-ground";
        } else if (v > runner) runner = v;
    }
    wlog("  why %s  [%s]  %s %s  worth %d  (next best %d, margin %d)",
         p->name, why_rule,
         best.kind == MV_STAY  ? "stays at" :
         best.kind == MV_HORSE ? "rides to" :
         best.kind == MV_WORK  ? "works at" :
         best.kind == MV_SPELL ? "casts at" : "walks to",
         /* a labour happens where she stands; best.dest belongs to the
          * walking options and would name the wrong space here */
         why_where(best.kind == MV_WORK ? p->idx : best.dest), bv,
         runner <= -1000 ? 0 : runner,
         runner <= -1000 ? 0 : bv - runner);
    return best;
}

static Move ask_move(Player *p)
{
    Move m; char msg[200]; char keys[16] = "sadq"; int n = 4;
    int pl = plane_at(p->idx);
    int can_boat = ring[p->idx].port && (p->gold >= 1 || has_perk(p, PK_WHARF));
    int can_gate = ring[p->idx].gate && p->gold >= 2 && gate_other(p->idx) >= 0;
    int can_work = (ring[p->idx].kind == SP_CITY && p->will_now >= 1)
                || (ring[p->idx].kind == SP_THIEVES_GUILD && p->str_now >= 1)
                || (ring[p->idx].kind == SP_FORTRESS && p->str_now >= 1);
    int sdest, sworth, si = pick_utility_spell(p, &sdest, &sworth);
    int c;

    m.kind = MV_STAY; m.dest = p->idx; m.dir = 1; m.plane = -1; m.spell = si;
    if (p->gold >= 1 || has_perk(p, PK_HORSE) || has_perk(p, PK_STAMINA) ||
        has_perk(p, PK_FLEET) || has_item_use(p, IU_STRIDE))
        { keys[n++] = 'z'; keys[n++] = 'x'; }
    if (can_boat) keys[n++] = 'b';
    if (can_gate) keys[n++] = 'g';
    if (can_work) keys[n++] = 'w';
    if (pl >= 0 && !planes[pl].closed) keys[n++] = 'p';
    if (si >= 0) keys[n++] = 'c';
    keys[n++] = '?';        /* ui_prompt intercepts it, but keep it legal */
    keys[n] = 0;

    snprintf(msg, sizeof msg,
             "[s]stay [a]%.11s [d]%.11s%s%s%s%s [?]help [q]quit",
             ring[ring_step(p->idx,-1)].name, ring[ring_step(p->idx,1)].name,
             p->gold >= 1 ? " [zx]ride" : "",
             can_boat ? " [b]boat" : "",
             can_gate ? " [g]gate" : "",
             can_work ? " [w]work" : "");
    if (si >= 0) {
        char sp[64]; int lst[9];
        if (list_utility_spells(p, lst) > 1) snprintf(sp, sizeof sp, " [c]cast");
        else snprintf(sp, sizeof sp, " [c]%.20s", abil_proto[p->abils[si]].name);
        strncat(msg, sp, sizeof msg - strlen(msg) - 1);
    }
    if (pl >= 0 && !planes[pl].closed)
        strncat(msg, " [p]ASSAULT", sizeof msg - strlen(msg) - 1);

    ui_draw();
    c = ui_prompt(msg, keys);
    switch (c) {
    case 'q': quit_flag = 1; break;
    case 'a': m.kind = MV_WALK;  m.dest = ring_step(p->idx, -1); break;
    case 'd': m.kind = MV_WALK;  m.dest = ring_step(p->idx,  1); break;
    case 'z': m.kind = MV_HORSE; m.dest = ring_step(p->idx, -2); break;
    case 'x': m.kind = MV_HORSE; m.dest = ring_step(p->idx,  2); break;
    case 'b': {
        int d = ui_prompt("Sail which way?  [a] anticlockwise  [d] clockwise", "ad") == 'a' ? -1 : 1;
        int dest = nearest_port(p->idx, d);
        if (dest >= 0) { m.kind = MV_BOAT; m.dest = dest; }
        break;
    }
    case 'g': m.kind = MV_GATE; m.dest = gate_other(p->idx); break;
    case 'w': m.kind = MV_WORK; break;
    case 'p': m.kind = MV_PLANE; m.plane = pl; break;
    case 'c': {
        int lst[9], ln = list_utility_spells(p, lst);
        int pick = lst[0];
        if (ln > 1) {                       /* more than one: let her choose */
            char sm[220] = "Cast which Spell?  ", sk[12] = "", *e;
            int j;
            for (j = 0; j < ln; j++) {
                char one[64];
                const AbilityCard *a = &abil_proto[p->abils[lst[j]]];
                snprintf(one, sizeof one, "[%d]%.14s ", j + 1, a->name);
                strncat(sm, one, sizeof sm - strlen(sm) - 1);
                sk[j] = (char)('1' + j);
            }
            sk[ln] = 0;
            ui_draw();
            e = strchr(sk, ui_prompt(sm, sk));
            pick = lst[e ? (int)(e - sk) : 0];
        }
        m.kind = MV_SPELL; m.spell = pick; m.dest = spell_dest(p, pick);
        break;
    }
    default: break;
    }
    return m;
}

/* --------------------------------------------------------------- turn -- */
static void do_work(Player *p)
{
    switch (ring[p->idx].kind) {
    case SP_CITY:
        drain(p, 1); give_gold(p, 2);
        glog("%s does skilled labour in the City: 1 Magic for 2 Gold.", p->name);
        break;
    case SP_THIEVES_GUILD:
        hurt(p, 1); give_gold(p, 3);
        glog("%s does dirty work for the Guild: 1 Health for 3 Gold.", p->name);
        break;
    case SP_FORTRESS:
        hurt(p, 1); give_exp(p, 2);
        glog("%s trains at the Fortress: 1 Health for 2 Experience.", p->name);
        break;
    default: break;
    }
}

/* The Opportunities lying in a space.  Normally you reach them only once the
 * Creatures are dealt with; Stealth is precisely the Ability that lets you
 * slip past and use one first, which is why this is a function and not a
 * loop buried at the end of resolve_space(). */
static void take_opportunities(Player *p)
{
    int i;
    for (i = 0; i < space_nadv[p->idx] && p->alive; ) {
        const Adventure *c = &adv_proto[space_adv[p->idx][i]];
        int before = space_nadv[p->idx];
        if (c->type != A_OPPORTUNITY) { i++; continue; }
        use_opportunity(p, c, p->idx, i);
        if (space_nadv[p->idx] == before) i++;    /* declined: leave it lying */
    }
}

/* Is there any point reading this scroll here and now?  Same principle as
 * an Opportunity: you may not take one you have no use for. */
static int foe_here(const Player *p)
{
    int i;
    for (i = 0; i < nplayers; i++) {
        const Player *q = &players[i];
        if (q != p && q->alive && q->idx == p->idx) return i;
    }
    return -1;
}

static int creature_here(const Player *p)
{
    int i;
    for (i = 0; i < space_nadv[p->idx]; i++)
        if (adv_proto[space_adv[p->idx][i]].type == A_CREATURE) return i;
    return -1;
}

static int item_use_wanted(const Player *p, const Item *it)
{
    int f;
    switch (it->use) {
    case IU_DIVINE:     return creature_here(p) >= 0 || foe_here(p) >= 0;
    case IU_DESTROY:    return creature_here(p) >= 0;
    case IU_STEAL:      f = foe_here(p);
                        return f >= 0 && players[f].nitems > 0 && p->nitems < MAX_ITEMS;
    case IU_DECAY:      f = foe_here(p); return f >= 0 && players[f].nitems > 0;
    case IU_REDEAL:     return space_nadv[p->idx] > 0 && creature_here(p) >= 0;
    case IU_WISH:       return p->nitems < MAX_ITEMS;
    case IU_AGAIN:      return 1;
    case IU_TRAIN_FREE: return IS_GUILD(ring[p->idx].kind);
    case IU_TRADE_EXP:  return p->gold >= 4 && p->exp < MAX_EXP;
    case IU_TELEPORT:   return 0;   /* movement: handled by ai_move, not here */
    case IU_STRIDE:     return 0;   /* passive */
    default:            return 0;
    }
}

static void do_item_use(Player *p, const Item *it)
{
    int f, c;
    switch (it->use) {
    case IU_DIVINE:  p->divine = 1;
        glog("    the roll is spoken for: a 6 against a 2."); break;
    case IU_DESTROY: c = creature_here(p);
        if (c >= 0) { glog("    the %s is unmade, and yields nothing.",
                           adv_proto[space_adv[p->idx][c]].name);
                      remove_adventure(p->idx, c); } break;
    case IU_STEAL:   f = foe_here(p);
        if (f >= 0 && players[f].nitems > 0) {
            int id = players[f].items[0];
            item_give(p, id);
            item_take(&players[f], 0);
            glog("    %s takes the %s from %s.", p->name,
                 item_proto[id].name, players[f].name);
        } break;
    case IU_DECAY:   f = foe_here(p);
        if (f >= 0 && players[f].nitems > 0) {
            glog("    %s's %s rots away.", players[f].name,
                 item_proto[players[f].items[0]].name);
            item_take(&players[f], 0);
        } break;
    case IU_REDEAL:  c = creature_here(p);
        if (c >= 0) { glog("    the world shifts, and this place is dealt anew.");
                      remove_adventure(p->idx, c); place_adventure(p->idx); } break;
    case IU_WISH:    glog("    the wish is granted.");
                     item_give(p, item_draw()); break;
    case IU_AGAIN:   extra_turn = 1;
        glog("    the spiral turns: the round begins again for %s.", p->name); break;
    case IU_TRAIN_FREE: p->free_training = 1;
        glog("    the letter opens the guild's door."); break;
    case IU_TRADE_EXP:
        give_gold(p, -4);
        p->exp = p->exp + 4 > MAX_EXP ? MAX_EXP : p->exp + 4;
        glog("    4 Gold becomes 4 Experience."); break;
    default: break;
    }
}

/* Potions and scrolls: a third of the Item deck is used once and gone.
 * Drunk on your own turn and never in a battle, so this runs before the
 * space is resolved -- which is also when a potion of Strength is worth
 * having, since its bonus lasts only until your turn ends. */
static void use_consumables(Player *p)
{
    int i;
    for (i = 0; i < p->nitems; i++) {
        const Item *it = &item_proto[p->items[i]];
        int want = 0;

        if (!it->oneshot || p->dmg[i]) continue;
        /* only drink what you can actually use: the rulebook is firm that
         * an Opportunity you have no use for may not be taken, and the
         * same good sense applies to a potion. */
        if (it->u_heal  && p->str_lost  > 0) want = 1;
        if (it->u_magic && p->will_lost > 0) want = 1;
        if ((it->u_str || it->u_will) && space_nadv[p->idx] > 0) want = 1;
        if (it->use) want = item_use_wanted(p, it);
        if (!want) continue;

        if (it->u_heal)  heal(p, it->u_heal);
        if (it->u_magic) recharge(p, it->u_magic);
        p->potion_str  += it->u_str;
        p->potion_will += it->u_will;
        if (it->use) do_item_use(p, it);
        glog("  %s uses the %s.", p->name, it->name);
        item_take(p, i);
        i--;                       /* the array closed up behind us */
    }
}

static void resolve_space(Player *p)
{
    if (has_perk(p, PK_STEALTH) && space_nadv[p->idx] > 0) {
        glog("  %s goes quietly, and takes what is here before the fight.", p->name);
        take_opportunities(p);
        if (!p->alive) return;
    }
    int i;

    /* Creatures first, and they are not optional. */
    for (i = 0; i < space_nadv[p->idx] && p->alive && !turn_over; ) {
        const Adventure *c = &adv_proto[space_adv[p->idx][i]];
        if (c->type != A_CREATURE) { i++; continue; }
        {   /* 1 = beaten, 2 = resolved without a fight; both clear the space */
            int r = battle_creature(p, c);
            if (r == 1 || r == 2) { remove_adventure(p->idx, i); continue; }
        }
        return;                                  /* a loss or draw ends the turn */
    }
    if (!p->alive || turn_over) return;

    /* "you can only give away Items and Gold" -- and only to your comrade,
     * and only when the two of you are standing in the same place. */
    if (team_play) {
        int j;
        for (j = 0; j < nplayers; j++) {
            Player *q = &players[j];
            if (q == p || !q->alive || q->idx != p->idx || !allied(p, q)) continue;
            if (p->gold >= 3 && q->gold <= 2) {
                give_gold(p, -2); give_gold(q, 2);
                glog("%s presses 2 Gold on %s.", p->name, q->name);
            }
            if (p->nitems == MAX_ITEMS && q->nitems < MAX_ITEMS) {
                int w = 0, k2;
                for (k2 = 1; k2 < p->nitems; k2++) {
                    const Item *a2 = &item_proto[p->items[k2]], *b2 = &item_proto[p->items[w]];
                    if (a2->d_str + a2->d_will < b2->d_str + b2->d_will) w = k2;
                }
                glog("%s passes the %s to %s.", p->name,
                     item_proto[p->items[w]].name, q->name);
                item_give(q, p->items[w]);
                q->dmg[q->nitems - 1] = p->dmg[w];
                item_take(p, w);
            }
            break;
        }
    }

    /* With the Creatures cleared, you may take on one other character. */
    if (!player_safe(p)) {
        int j, best = -1;
        for (j = 0; j < nplayers; j++) {
            Player *q = &players[j];
            if (q == p || !q->alive || q->idx != p->idx || player_safe(q)) continue;
            /* Comrades do not draw on each other -- until the Final Battle.
             * A pair may only give away Items and Gold, never Artifacts, so
             * the sole way to gather four into one pair of hands is to take
             * them, and that includes taking them from your own comrade. */
            if (allied(p, q) && !final_battle) continue;
            if (best < 0 || q->artifacts > players[best].artifacts) best = j;
        }
        if (best >= 0) {
                    int want;
                    Player *target = &players[best];
                    
                    /* TABLE POLITICS: If someone is winning or holding artifacts, 
                     * prioritize ganging up on them, lowering our required strength advantage. */
                    if (p->ai) {
                        int is_leader_threat = (target->artifacts > 0 || final_battle);
                        int manageable_fight = final_battle ? (eff_str(p) >= eff_str(target) - 3) : (eff_str(p) >= eff_str(target) - 1);
                
                        want = final_battle || (is_leader_threat && manageable_fight);
                    } else {
                        char m[140];
                        snprintf(m, sizeof m, "Attack %s the %s (Str %d, Will %d)?  [y]/[n]",
                                 target->name, target->cls,
                                 eff_str(target), eff_will(target));
                        ui_draw();
                        want = (ui_prompt(m, "yn") == 'y');
                    }
                    if (want) battle_players(p, target);
                }
    }
    if (!p->alive) return;

    take_opportunities(p);
    if (p->alive) use_possibilities(p);
}

/* --- the Water Realm -------------------------------------------------- */
static int use_water;                  /* PROPHECY_WATER=1                 */
static int wr_gate = -1;               /* which Plane the Gate replaced    */
static int wr_card[WR_SPACES];         /* wr_proto index, or -1            */
static int wr_seen[WR_SPACES];
#define WR_PALACE  (WR_SPACES - 1)     /* the Undersea Palace, top corner  */

static void water_setup(void)
{
    int deck[64], n = 0, i, s = 0;

    if (!use_water) { wr_gate = -1; return; }
    for (i = 0; wr_proto[i].name; i++) deck[n++] = i;
    for (i = n - 1; i > 0; i--) { int j = rand() % (i+1), t = deck[i];
                                  deck[i] = deck[j]; deck[j] = t; }
    for (i = 0; i < WR_SPACES; i++) { wr_card[i] = -1; wr_seen[i] = 0; }
    for (i = 0; i < n && s < WR_SPACES; i++, s++) {
        if (s == 0 || s == WR_PALACE) { s++; if (s >= WR_SPACES) break; }
        wr_card[s] = deck[i];
    }
    do { wr_gate = rand() % PLANES; } while (wr_gate == dr_gate && PLANES > 1);
    glog("(a Gate to the Water Realm lies behind one of the Astral Planes)");
}

/* Every hand holding an Item is -1 in an underwater battle: a two-handed
 * weapon is -2.  It is the one rule in either expansion that makes carrying
 * less the better choice. */
static int water_penalty(const Player *p)
{
    int i, hands = 0;
    for (i = 0; i < p->nitems; i++) {
        unsigned w = item_proto[p->items[i]].wtype;
        if (p->dmg[i] || !w) continue;
        hands += (w & WT_TWOHAND) ? 2 : 1;
    }
    if (has_perk(p, PK_UNDERWATER)) return 0;   /* Underwater Combat */
    return -hands;
}

/* One card of the Realm, resolved.  Returns 1 if the way is now clear. */
static int dragon_card(Player *p, int path, int slot)
{
    int ci = dr_path[path].card[slot];
    const DragonCard *c;

    if (ci < 0) return 1;                       /* an empty space */
    c = &dr_proto[ci];
    if (!dr_path[path].seen[slot]) {
        dr_path[path].seen[slot] = 1;
        glog("  %s is here.", c->name);
    }
    if (c->toll_health) hurt(p, c->toll_health);
    if (c->toll_magic)  drain(p, c->toll_magic);
    if (!p->alive) return 0;

    if (!c->negative) {                          /* a gift, freely taken */
        if (c->gift_gold) give_gold(p, c->gift_gold);
        if (c->gift_exp)  p->exp = p->exp + c->gift_exp > MAX_EXP ? MAX_EXP
                                                                 : p->exp + c->gift_exp;
        if (c->gift_heal)  heal(p, c->gift_heal);
        if (c->gift_magic) recharge(p, c->gift_magic);
        if (c->gift_str)  gain_str(p);   /* at the cap this heals */
        if (c->gift_will) gain_will(p);
        glog("    %s", c->text);
        dr_path[path].card[slot] = -1;           /* taken, and gone */
        return 1;
    }
    if (c->obstacle) {
        /* An Obstacle is never removed: it stays to block the next hero.
         * Overcome it by a roll against the toll it has already taken. */
        int r;
        if (has_perk(p, PK_DRAGONLORE) && p->will_now > 0) {
            drain(p, 1);
            glog("    %s knows this ground, and slips past for a Magic.", p->name);
            return 1;
        }
        r = roll();
        glog("    %s tries the %s (a %d).", p->name, c->name, r);
        if (r >= 3) { glog("    the way opens."); return 1; }
        glog("    and is turned back.");
        return 0;
    }
    {   /* a Creature of the Realm */
        int lives = c->lives ? c->lives : 1, i, res = -1;
        BattleKind k = (c->str && !c->will) ? B_STRENGTH
                     : (c->will && !c->str) ? B_WILL
                     : (eff_str(p) >= eff_will(p) ? B_STRENGTH : B_WILL);
        int mine   = (k == B_WILL) ? eff_will(p) : eff_str(p);
        int theirs = (k == B_WILL) ? c->will     : c->str;
        mine += battle_spell(p, k, c->name);
        mine += perk_battle(p, k, CT_ANIMAL, 0);   /* dragons are animals */
        /* "Combat bonuses apply only to combat against Dragon Realm
         * Creatures" -- so they are added here and nowhere else. */
        if (k == B_STRENGTH && has_perk(p, PK_DRAGONSLAY)) {
            mine += 2; glog("    Dragonslaying tells: +2.");
        }
        if (k == B_WILL && has_perk(p, PK_DRAGONMIND)) {
            mine += 3; glog("    he knows how a dragon thinks: +3.");
        }
        for (i = 0; i < lives; i++) {
            res = fight_roll(p, c->name, mine, theirs, k);
            beat(p);
            if (res != 1) break;
        }
        if (res == 1 && k == B_WILL && has_perk(p, PK_DRAGONMIND)) recharge(p, 1);
        if (res == 1) {
            p->exp = p->exp + c->exp > MAX_EXP ? MAX_EXP : p->exp + c->exp;
            glog("    the %s is beaten: %d Experience.", c->name, c->exp);
            dr_path[path].card[slot] = -1;
            return 1;
        }
        if (res == 0) {
            hurt(p, 1);
            if (has_perk(p, PK_DRAKE)) {
                glog("    the drake holds her: %s keeps her place.", p->name);
            } else {
                glog("    %s is thrown out of the Realm.", p->name);
                p->in_realm = 0;
                p->idx = plane_adjacent(dr_gate, 0);
            }
        }
        return 0;
    }
}

/* A turn spent inside the Dragon Realm: forward, or out. */
static void dragon_turn(Player *p)
{
    DragonPath *path;

    if (p->dr_pos < 0) {                 /* still on the Gate: choose a path */
        p->dr_path = rand() % DR_PATHS;
        p->dr_pos  = 0;
        glog("%s sets out on the %s path.", p->name,
             p->dr_path == 1 ? "middle" : p->dr_path == 0 ? "left" : "right");
    }
    path = &dr_path[p->dr_path];
    if (p->dr_pos >= path->len) {        /* the Lake of Fire */
        glog("%s stands at the Lake of Fire.", p->name);
        assault_plane(p, dr_gate);
        if (p->alive && planes[dr_gate].closed && !p->dragon_heart) {
            p->dragon_heart = 1;
            heal(p, MAX_STRENGTH); recharge(p, MAX_WILL);
            glog("*** %s takes a Dragon Heart, and is made whole. ***", p->name);
            p->in_realm = 0;
            p->idx = plane_adjacent(dr_gate, 1);
        }
        return;
    }
    if (dragon_card(p, p->dr_path, p->dr_pos)) {
        p->dr_pos++;
        glog("  %s climbs on (%d of %d).", p->name, p->dr_pos, path->len);
    }
}

/* The Gate stands on one of the Astral Planes.  Entering costs 3 Gold --
 * "rent a drake" -- and puts you on the Gate itself, which counts as the
 * first space of all three paths. */
static int dragon_enter(Player *p)
{
    if (!use_dragon || dr_gate < 0 || p->in_realm) return 0;
    if (planes[dr_gate].closed) return 0;
    if (plane_at(p->idx) != dr_gate) return 0;
    if (p->gold < DR_ENTRY_GOLD && !has_perk(p, PK_DRAKE)) return 0;
    /* only worth the journey once you could survive it */
    if (p->ai && eff_str(p) < 6 && eff_will(p) < 6) return 0;

    if (has_perk(p, PK_DRAKE)) glog("  the drake answers to her, and asks no fee.");
    else                       give_gold(p, -DR_ENTRY_GOLD);
    p->in_realm = 1; p->dr_pos = -1; p->dr_path = -1; p->dr_tries++;
    glog(">>> %s rents a drake and passes into the Dragon Realm. <<<", p->name);
    return 1;
}

/* A turn under the sea.  The air goes first, before anything else. */
static void water_turn(Player *p)
{
    int step, dest, ci;

    /* "At the beginning of each round, you must return one Bubble... If you
     * have no Bubbles left, you must lose 1 Health instead." */
    if (p->bubbles > 0) {
        p->bubbles--;
        if (p->bubbles <= 1)
            glog("  %s is down to %d Bubble%s.", p->name, p->bubbles,
                 p->bubbles == 1 ? "" : "s");
    } else {
        glog("  %s has no air left, and the sea takes its due.", p->name);
        hurt(p, 1);
        if (!p->alive) { glog("*** %s drowns. ***", p->name); return; }
    }
    if (has_perk(p, PK_SWIMMING) && p->bubbles > 0 && (rand() % 4) == 0) {
        if (p->bubbles < p->bag) p->bubbles++;   /* never past the bag */
        glog("  %s rests and takes a breath.", p->name);
        return;
    }

    /* "You cannot simply choose to leave the Water Realm... you have to
     * move off the edge of the board."  So turning back is a real journey,
     * and a character short of air must start it in time. */
    /* Turning back takes as many moves as coming in did, so a hero who
     * waits until the air is gone drowns on the way out.  Leave while
     * there is still enough to get home. */
    if (p->bubbles == 0 ||
        (p->wr_pos != WR_PALACE && p->bubbles <= p->wr_pos / WR_W)) {
        if (p->wr_pos == WR_PALACE) p->wr_pos = WR_PALACE - WR_W;
        if (p->wr_pos <= 0) {
            glog("  %s breaks the surface, out of air and out of the Realm.", p->name);
            p->in_water = 0; p->bubbles = 0;
            p->idx = plane_adjacent(wr_gate, 0);
            return;
        }
        p->wr_pos -= (p->wr_pos >= WR_W) ? WR_W : 1;
        glog("  %s turns back for the light (%d to go).", p->name, p->wr_pos);
        return;
    }

    if (p->wr_pos == WR_PALACE) {          /* the Undersea Palace */
        glog("%s reaches the Undersea Palace.", p->name);
        assault_plane(p, wr_gate);
        if (p->alive && planes[wr_gate].closed && !p->pearl) {
            p->pearl = 1;
            heal(p, MAX_STRENGTH); recharge(p, MAX_WILL);
            glog("*** %s takes a Pearl of the Abyss, and is made whole. ***", p->name);
            p->in_water = 0; p->bubbles = 0;
            p->idx = plane_adjacent(wr_gate, 1);
        }
        return;
    }

    /* move one square towards the Palace, or two if the way is empty */
    step = (p->wr_pos + WR_W <= WR_PALACE) ? WR_W : 1;
    dest = p->wr_pos + step;
    if (dest > WR_PALACE) dest = WR_PALACE;
    ci = wr_card[dest];
    if (ci >= 0) {
        const WaterCard *c = &wr_proto[ci];
        if (!wr_seen[dest]) { wr_seen[dest] = 1; glog("  %s lies ahead.", c->name); }
        if (c->no_entry) {
            glog("    the reef bars the way; %s must go round.", p->name);
            dest = p->wr_pos + 1 <= WR_PALACE ? p->wr_pos + 1 : p->wr_pos;
            if (dest == p->wr_pos) return;
            ci = wr_card[dest];
            if (ci >= 0 && wr_proto[ci].no_entry) return;
        }
    }
    p->wr_pos = dest;
    ci = wr_card[dest];
    if (ci < 0) { glog("  %s swims on.", p->name); return; }
    {
        const WaterCard *c = &wr_proto[ci];
        if (c->toll_bubble) {
            int t = c->toll_bubble;
            while (t-- > 0 && p->bubbles > 0) p->bubbles--;
            glog("    it costs %s air.", p->name);
        }
        if (c->toll_health) hurt(p, c->toll_health);
        if (c->toll_magic)  drain(p, c->toll_magic);
        if (!p->alive) return;

        if (!c->negative) {
            if (c->gift_gold)  give_gold(p, c->gift_gold);
            if (c->gift_exp)   p->exp = p->exp + c->gift_exp > MAX_EXP ? MAX_EXP
                                                                      : p->exp + c->gift_exp;
            if (c->gift_heal)  heal(p, c->gift_heal);
            if (c->gift_magic) recharge(p, c->gift_magic);
            if (c->gift_bubble) { p->bubbles += c->gift_bubble;
                                  if (p->bubbles > p->bag) p->bubbles = p->bag; }
            if (c->gift_str)  gain_str(p);
            if (c->gift_will) gain_will(p);
            glog("    %s", c->text);
            wr_card[dest] = -1;
            return;
        }
        if (c->obstacle) return;             /* currents and whirlpools stay */
        {
            int lives = c->lives ? c->lives : 1, i, res = -1;
            BattleKind k = (c->will && !c->str) ? B_WILL : B_STRENGTH;
            int mine   = (k == B_WILL) ? eff_will(p) : eff_str(p);
            int theirs = (k == B_WILL) ? c->will     : c->str;
            if (k == B_STRENGTH) mine += water_penalty(p);
            if (k == B_WILL && has_perk(p, PK_COMMUNE)) mine += 2;
            mine += battle_spell(p, k, c->name);
            for (i = 0; i < lives; i++) {
                res = fight_roll(p, c->name, mine, theirs, k);
                beat(p);
                if (res != 1) break;
            }
            if (res == 1) {
                p->exp = p->exp + c->exp > MAX_EXP ? MAX_EXP : p->exp + c->exp;
                if (has_perk(p, PK_COMMUNE) && k == B_WILL) {
                    p->bubbles += 2;
                    if (p->bubbles > p->bag) p->bubbles = p->bag;
                }
                glog("    the %s is beaten.", c->name);
                wr_card[dest] = -1;
            } else if (res == 0) {
                hurt(p, 1);
                glog("    %s is driven back towards the light.", p->name);
                p->in_water = 0; p->bubbles = 0;
                p->idx = plane_adjacent(wr_gate, 0);
            }
        }
    }
}

static int water_enter(Player *p)
{
    int bag;
    if (!use_water || wr_gate < 0 || p->in_water) return 0;
    if (planes[wr_gate].closed) return 0;
    if (plane_at(p->idx) != wr_gate) return 0;
    if (p->ai && eff_str(p) < 6 && eff_will(p) < 6) return 0;
    /* Surfacing without the Pearl means the journey was beyond her.  Going
     * straight back down with the same character and the same purse just
     * repeats it, which is how 209 entries produced no arrivals at all. */
    if (p->wr_tries >= 2 && !p->pearl) return 0;
    {
        /* "The price of the Bubble Bags is stated on the Gate card (e.g.
         * 3 Bubbles for 1 Gold)" -- so how much air you take is a spending
         * decision, and the bag cannot be topped up once you are under.
         * The Palace is six moves from the entrance, so anything less than
         * about seven Bubbles is a journey you cannot finish -- which is
         * exactly the trap that had characters bobbing in and out for ever. */
        int want = 5, spend;
        if (has_perk(p, PK_WATERBREATH))  want--;      /* a cheaper bag */
        spend = p->gold < want ? p->gold : want;
        bag = spend * WR_BAG_SIZE + (has_perk(p, PK_UNDERCONTACT) ? 3 : 0);
        if (bag < WR_MIN_AIR) return 0;                /* not enough to try */
        give_gold(p, -spend);
    }
    p->in_water = 1; p->wr_pos = 0; p->bag = bag; p->bubbles = bag;
    p->wr_tries++;
    glog(">>> %s buys a bag of %d Bubbles and goes under. <<<", p->name, bag);
    return 1;
}

static void take_turn(Player *p)
{
    if (p->in_water) { water_turn(p); return; }
    if (p->in_realm) { dragon_turn(p); return; }
    if (dragon_enter(p)) return;
    if (water_enter(p))  return;
    Move m;

    if (!p->alive) return;
    turn_over = 0;
    p->potion_str = p->potion_will = 0;   /* a potion lasts one turn only */
    if (p->safe_until_turn) p->safe_until_turn = 0;
    use_consumables(p);

    m = p->ai ? ai_move(p) : ask_move(p);
    if (m.kind == MV_STAY) { if (p->idle < 40) p->idle++; }
    else                     p->idle = 0;
    if (quit_flag) return;

    switch (m.kind) {
    case MV_STAY:  glog("%s stays in the %s.", p->name, ring[p->idx].name); break;
    case MV_WALK:  p->idx = m.dest; glog("%s walks to the %s.", p->name, ring[p->idx].name); break;
    case MV_HORSE:
        /* Horsemanship pays nothing to ride; Stamina simply walks the
         * distance, so neither owes the Gold a two-space move costs. */
        if (has_perk(p, PK_HORSE)) {
            p->idx = m.dest;
            glog("%s rides to the %s for nothing.", p->name, ring[p->idx].name);
        } else if (has_item_use(p, IU_STRIDE)) {
            p->idx = m.dest;
            glog("%s covers the ground in good boots.", p->name);
        } else if (has_perk(p, PK_FLEET)) {
            p->idx = m.dest;
            glog("%s is there before the dust settles.", p->name);
        } else if (has_perk(p, PK_STAMINA)) {
            p->idx = m.dest;
            glog("%s walks it, and is not winded.", p->name);
        } else {
            give_gold(p, -1); p->idx = m.dest;
            glog("%s rides to the %s (1 Gold).", p->name, ring[p->idx].name);
        }
        break;
    case MV_BOAT:
        if (has_perk(p, PK_WHARF)) glog("  %s sails as a Wharf Rat, and pays nothing.", p->name);
        else                       give_gold(p, -1);
        if (has_perk(p, PK_NAUTICAL)) { give_gold(p, 1);
            glog("  the Nautical Rites earn %s a Gold on the crossing.", p->name); }
        p->idx = m.dest;
                   glog("%s takes ship to the %s (1 Gold).", p->name, ring[p->idx].name); break;
    case MV_GATE:  give_gold(p, -2); p->idx = m.dest;
                   glog("%s steps through the Magic Gate to the %s (2 Gold).", p->name, ring[p->idx].name); break;
    case MV_WORK:  do_work(p); redraw(p); return;   /* labour replaces the move */
    case MV_SPELL: {                               /* so does a Spell */
        SpellKind k = abil_proto[p->abils[m.spell]].kind;
        cast_utility(p, m.spell);
        if (k == SP_MOVE_FOREST || k == SP_MOVE_MOUNTAIN) {
            p->idx = m.dest;      /* a Movement Spell IS the move */
            glog("%s emerges in the %s.", p->name, ring[p->idx].name);
            break;
        }
        redraw(p);
        return;
    }
    case MV_PLANE: assault_plane(p, m.plane); return;
    }
    redraw(p);
    resolve_space(p);
}

/* ------------------------------------------------------ chance cards -- */
static void stock_guild(Guild g)
{
    if (g < 0 || g >= G_COUNT || gdeck_n[g] <= 0) return;
    if (gdeck_pos[g] >= gdeck_n[g]) gdeck_pos[g] = 0;   /* the guild's deck comes round again */
    guild_abil[g] = gdeck[g][gdeck_pos[g]++];
    glog("  %s is taught at the %s.", abil_proto[guild_abil[g]].name, guild_name[g]);
}

/* Rulebook deck: the vast majority of these cards stock the world. */
static void chance_card(Player *me)
{
    const Chance *c;
    int i, n;

    if (cdeck_n <= 0) return;
    if (cdeck_pos >= cdeck_n) {
        /* "If the Chance deck is exhausted and the game remains undecided,
         * then the deck is not reshuffled -- instead the Apocalypse begins." */
        if (apocalypse_variant && final_battle && !apocalypse) {
            apocalypse = 1;
            glog("*** THE APOCALYPSE BEGINS.  The world starts to die. ***");
            return;
        }
        if (apocalypse) return;
        cdeck_pos = 0;
        glog("(the Chance deck is shuffled anew)");
    }
    c = &chance_proto[cdeck[cdeck_pos++]];
    glog("Chance: %s -- %s.", c->name, c->text);

    switch (c->kind) {
    case CH_TERRAIN:                     /* EVERY space of that terrain */
        for (i = 0; i < RING_N; i++)
            if (ring[i].kind == c->terrain) place_adventure(i);
        break;
    case CH_TRAINING:
        if (c->guild == G_NONE) {        /* the drawer chooses the guild */
            Guild g = me->ai ? (me->guild_a) : me->guild_a;
            stock_guild(g);
        } else stock_guild(c->guild);
        break;
    case CH_STOCK: {
        int where = c->rare ? 7 : 0;     /* the City takes Rare, the Village Common */
        for (n = 0; n < c->count; n++) {
            int ii = item_draw();
            if (ii < 0) break;
            if (item_proto[ii].rare == c->rare) { space_item[where] = ii; break; }
            space_item[where] = ii;      /* close enough: something new is on sale */
        }
        if (space_item[where] >= 0)
            glog("  a %s is on sale in the %s (%d Gold).",
                 item_proto[space_item[where]].name, ring[where].name,
                 item_proto[space_item[where]].price);
        break;
    }
    case CH_BOON:
        if (!strcmp(c->name, "Good Times")) {
            give_gold(me, c->count);
            for (i = 0; i < nplayers; i++)
                if (players[i].alive && &players[i] != me) give_gold(&players[i], c->other);
        } else if (!strcmp(c->name, "Refreshing")) {
            heal(me, c->count);
            for (i = 0; i < nplayers; i++)
                if (players[i].alive && &players[i] != me) heal(&players[i], c->other);
        } else if (!strcmp(c->name, "Wind")) {
            heal(me, 1); recharge(me, 2);
        } else {
            recharge(me, c->count);
            for (i = 0; i < nplayers; i++)
                if (players[i].alive && &players[i] != me) recharge(&players[i], c->other);
        }
        break;
    case CH_CHARITY: {
        Player *poor = NULL, *drained = NULL, *hurt_most = NULL;
        for (i = 0; i < nplayers; i++) {
            Player *p = &players[i];
            if (!p->alive) continue;
            if (!poor      || p->gold      < poor->gold)           poor = p;
            if (!drained   || p->will_lost > drained->will_lost)   drained = p;
            if (!hurt_most || p->str_lost  > hurt_most->str_lost)  hurt_most = p;
        }
        if (poor)      { give_gold(poor, 3); glog("  %s is given 3 Gold.", poor->name); }
        if (drained)   { recharge(drained, 2); glog("  %s recharges 2 Magic.", drained->name); }
        if (hurt_most) { heal(hurt_most, 1);  glog("  %s is healed.", hurt_most->name); }
        break;
    }
    case CH_PEACE:
        extra_turn = 1;
        break;
    case CH_PROPHETIC:
        for (i = 0; i < PLANES; i++)
            if (!planes[i].closed && !planes[i].revealed) {
                planes[i].revealed = 1;
                glog("  the Plane beyond the %s is seen: %s stands guard.",
                     plane_name(i), guard_proto[plane_lesser[i]].name);
                break;
            }
        break;
    case CH_ECONOMIC: {
        int r = roll() + nplayers;
        glog("  the harvest roll is %d.", r);
        if (r >= 7) for (i = 0; i < nplayers; i++) { if (players[i].alive) give_gold(&players[i], 2); }
        else        for (i = 0; i < nplayers; i++) { if (players[i].alive) give_gold(&players[i], -1); }
        break;
    }
    }
}

/* ------------------------------------------------------- invariants --- */
static void check_state(void)
{
    int i, s;

    if (!checking) return;
    for (i = 0; i < nplayers; i++) {
        Player *p = &players[i];
        if (!p->alive) continue;
        if (p->str_now < 0 || p->will_now < 0)         glog("INV neg %s", p->name);
        if (p->str_now + p->str_lost > MAX_STRENGTH)   glog("INV str %s %d", p->name, p->str_now + p->str_lost);
        if (p->will_now + p->will_lost > MAX_WILL)     glog("INV will %s %d", p->name, p->will_now + p->will_lost);
        if (p->gold < 0 || p->gold > MAX_GOLD)         glog("INV gold %s %d", p->name, p->gold);
        if (p->exp  < 0 || p->exp  > MAX_EXP)          glog("INV exp %s %d", p->name, p->exp);
        if (p->gold  > MAX_GOLD) glog("INV gold %s %d", p->name, p->gold);
        if (p->exp   > MAX_EXP)  glog("INV exp %s %d",  p->name, p->exp);
        if (p->str_now  < 0 || p->str_lost  < 0) glog("INV negstr %s", p->name);
        if (p->will_now < 0 || p->will_lost < 0) glog("INV negwill %s", p->name);
        if (p->bubbles > p->bag) glog("INV air %s %d/%d", p->name, p->bubbles, p->bag);
        if (p->in_water && p->in_realm) glog("INV two realms %s", p->name);
        if (p->artifacts > PLANES) glog("INV artcount %s %d", p->name, p->artifacts);
        {   /* the mask and the count must agree */
            int k, n = 0;
            for (k = 0; k < ART_N; k++) if ((p->art_mask >> k) & 1u) n++;
            if (n != p->artifacts) glog("INV artmask %s %d vs %d", p->name, n, p->artifacts);
        }
        if (p->nitems > item_cap(p) ||
            p->nabils > abil_cap(p)) glog("INV cards %s", p->name);
        {   /* dmg[] runs alongside items[]: it must never say more than
             * items[] holds, or a repair would mend a card nobody carries */
            int j;
            for (j = 0; j < p->nitems; j++)
                if (p->dmg[j] != 0 && p->dmg[j] != 1) glog("INV dmg %s", p->name);
            for (j = p->nitems; j < MAX_ITEMS; j++)
                if (p->dmg[j]) glog("INV stale dmg %s", p->name);
        }
        if (p->idx < 0 || p->idx >= RING_N)            glog("INV idx %s %d", p->name, p->idx);
        if (p->artifacts < 0 || p->artifacts > PLANES) glog("INV art %s", p->name);
    }
    for (s = 0; s < RING_N; s++)
        if (space_nadv[s] < 0 || space_nadv[s] > MAX_ON_SPACE) glog("INV space %d", s);
}

/* --------------------------------------------------------------- main -- */
/* The ten characters of the base game, from the printed cards.
 *
 * Two properties of this table are not decoration, and breaking either one
 * means the roster is no longer the game's:
 *
 *   1. There are only THREE stat lines -- 5/2, 4/4 and 3/6 -- so Strength and
 *      Willpower always sum to 7, 8 or 9.  Nothing here is 3/5 or 4/3.
 *   2. The ten characters use all ten possible guild PAIRS, each exactly once.
 *      Five guilds, C(5,2) = 10 pairs, 10 characters.  A character IS a guild
 *      pair.  Add or drop one and the set no longer covers the pairs; change
 *      one character's guild and it necessarily duplicates another's.
 *
 * Guilds are a discount, not a gate: any character may learn from any guild,
 * but pays extra Gold outside her two (see learn_here()).
 *
 * NOTE: starting Gold is NOT verified.  The character card does carry a Gold
 * field, so the real values almost certainly differ per character, but no
 * source to hand prints them -- the rulebook's setup section does not list
 * them and the card photographs are not legible at that spot.  A uniform 2 is
 * a placeholder chosen so as not to invent differences between characters;
 * fix it when a legible card or the card list turns up. */
static const struct { const char *cls; int str, will, gold; Guild a, b; } heroes[] = {
    { "Mercenary",   5, 2, 2, G_THIEVES,   G_FORTRESS  },
    { "Scout",       5, 2, 2, G_THIEVES,   G_CAMP      },
    { "Ranger",      5, 2, 2, G_CAMP,      G_FORTRESS  },
    { "Spellblade",  4, 4, 2, G_FORTRESS,  G_TOWER     },
    { "Paladin",     4, 4, 2, G_FORTRESS,  G_MONASTERY },
    { "Illusionist", 4, 4, 2, G_TOWER,     G_THIEVES   },
    { "Wand.Monk",   4, 4, 2, G_MONASTERY, G_THIEVES   },
    { "Mystic",      3, 6, 2, G_MONASTERY, G_TOWER     },
    { "Druid",       3, 6, 2, G_CAMP,      G_MONASTERY },
    { "Enchantress", 3, 6, 2, G_TOWER,     G_CAMP      },
};
#define NHEROES ((int)(sizeof heroes / sizeof heroes[0]))

/* The roster's two structural properties, checked once at start-up: three
 * stat lines only, and all ten guild pairs distinct.  Both are easy to break
 * with a well-meaning edit and neither shows up as a crash -- it just quietly
 * stops being Prophecy's roster. */
static void check_roster(void)
{
    int i, j, sum;

    for (i = 0; i < NHEROES; i++) {
        sum = heroes[i].str + heroes[i].will;
        if (sum != 7 && sum != 8 && sum != 9)
            glog("INV roster %s has Str+Will %d (expected 7, 8 or 9)",
                 heroes[i].cls, sum);
        if (heroes[i].a == heroes[i].b)
            glog("INV roster %s trains at one guild twice", heroes[i].cls);
        for (j = i + 1; j < NHEROES; j++)
            if ((heroes[i].a == heroes[j].a && heroes[i].b == heroes[j].b) ||
                (heroes[i].a == heroes[j].b && heroes[i].b == heroes[j].a))
                glog("INV roster %s and %s share a guild pair",
                     heroes[i].cls, heroes[j].cls);
    }
}

static void deal(Player *p, int h, int idx, int ai)
{
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", heroes[h].cls);
    p->cls = heroes[h].cls;
    p->guild_a = heroes[h].a; p->guild_b = heroes[h].b;
    p->str_now = heroes[h].str; p->will_now = heroes[h].will;
    p->gold = heroes[h].gold;
    p->idx = idx; p->alive = 1; p->ai = ai;
    p->race = RACE_NONE;
    if (use_races) {
        /* the ten cards, not the six Races: multiples are how the box
         * reflects how common each Race is */
        int deck[16], n = 0, r, k;
        for (r = 0; r < RACE_N; r++)
            for (k = 0; k < race_proto[r].copies; k++) deck[n++] = r;
        p->race = deck[rand() % n];
        p->str_now  += race_proto[p->race].d_str;
        p->will_now += race_proto[p->race].d_will;
        if (p->str_now  < 1) p->str_now  = 1;
        if (p->will_now < 1) p->will_now = 1;
        if (race_proto[p->race].abil_cost < 0) p->exp += 2;   /* Human */
        p->gold   += race_proto[p->race].bank;                /* Dwarf */
        p->banked  = race_proto[p->race].bank;
        glog("%s is a %s.", p->name, race_proto[p->race].name);
    }
    p->team = -1;                  /* everyone plays for herself by default */
}

/* Who you are matters here: the two guilds decide which Abilities are
 * cheap for you, and Strength-vs-Willpower decides how you will fight. */
static int choose_hero(void)
{
    int i, c;

    erase();
    attron(A_BOLD);
    mvprintw(1, 2, "CHOOSE YOUR HERO");
    attroff(A_BOLD);
    mvprintw(2, 2, "Strength is also your Health; Willpower is also your Magic.");
    for (i = 0; i < NHEROES; i++)
        mvprintw(4 + i, 4, "[%c] %-11s Str %d  Will %d  Gold %d    trains cheaply at %s and %s",
                 (i < 9) ? '1' + i : 'a', heroes[i].cls,
                 heroes[i].str, heroes[i].will, heroes[i].gold,
                 guild_name[heroes[i].a], guild_name[heroes[i].b]);
    refresh();
    c = ui_prompt("  Press a number", "123456789a");
    i = (c == 'a') ? 9 : c - '1';   /* ten heroes now, so 'a' is the tenth */
    return (i >= 0 && i < NHEROES) ? i : 0;
}

/* Prophecy has no expansions, but it has three ways to play, and until now
 * two of them were reachable only by a command-line flag.  A mode you have
 * to remember the spelling of is a mode nobody uses. */
/* The drawing, split from the asking, so tools/screenshot.sh can render this
 * screen at any size without a human pressing a key at it. */
static void draw_mode_screen(void)
{
    erase();
    attron(A_BOLD);
    mvprintw(1, 2, "P R O P H E C Y");
    attroff(A_BOLD);
    mvprintw(3, 2, "Which game?");
    mvprintw(5, 4, "[1]  Standard");
    mvprintw(6, 9, "Four of the five Artifacts and the throne is yours.");
    mvprintw(8, 4, "[2]  Apocalypse");
    mvprintw(9, 9, "The longer game.  When the Chance deck runs out during the");
    mvprintw(10, 9, "Final Battle the world begins to die: everyone loses a Health");
    mvprintw(11, 9, "and a Magic every round until somebody is crowned.");
    mvprintw(13, 4, "[3]  Team play");
    mvprintw(14, 9, "Four heroes as two pairs, seated opposite.  Comrades do not");
    mvprintw(15, 9, "fight until the Final Battle, and if one is crowned both win.");
    mvprintw(17, 2, "[w]  Sit back and watch the computer play itself");
    mvprintw(18, 2, "[?]  keys and switches");
    refresh();
}

static void choose_mode(void)
{
    draw_mode_screen();
    /* [w] belongs here rather than on the next screen: watching is
     * orthogonal to which game you are playing, and Team play skips the
     * hero-count screen entirely -- so offering it only there left no way
     * to watch a team game at all. */
    switch (ui_prompt("  Choose a game:   [1] standard   [2] apocalypse   [3] teams   [w] watch",
                      "123w")) {
    case '2': apocalypse_variant = 1; break;
    case '3': team_play = 1;          break;
    case 'w': watching = 1;           break;
    default:                          break;
    }
}

static int choose_count(void)
{
    if (team_play) return 4;              /* "this four-player variant" */
    erase();
    attron(A_BOLD);
    mvprintw(1, 2, "P R O P H E C Y");
    attroff(A_BOLD);
    mvprintw(3, 2, "Twenty spaces around the world, five Astral Planes inside it.");
    mvprintw(4, 2, "Take four of the five Artifacts and the throne is yours.");
    refresh();
    return ui_prompt("  How many heroes walk the world?   [2] [3] [4] [5]", "2345") - '0';
}

static int alive_count(void)
{
    int i, n = 0;
    for (i = 0; i < nplayers; i++) if (players[i].alive) n++;
    return n;
}

int main(int argc, char **argv)
{
    check_roster();
    int i, winner = -1, taken[NHEROES];
    const char *e;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("usage: %s [--watch] [--apocalypse] [--teams] [--log FILE]\n\n"
                   "  -w, --watch   every hero is played by the computer; watch it,\n"
                   "                pause with space, +/- for speed, q to stop\n"
                   "  --apocalypse  the longer variant: when the Chance deck runs out\n"
                   "                during the Final Battle, the world begins to die\n"
                   "  --teams       four players as two pairs; crown one and both win\n"
                   "  --log FILE    write a full text log of the game to FILE\n"
                   "  -h, --help    this message\n\n"
                   "environment: PROPHECY_SEED (replay a game), PROPHECY_TRACE (= --log),\n"
                   "             PROPHECY_AUTO (headless, prints a result line),\n"
                   "             PROPHECY_DELAY (ms), PROPHECY_CHECK (assert invariants),\n"
                   "             PROPHECY_SHOT (dump every frame)\n", argv[0]);
            return 0;
        }
        else if (!strcmp(argv[i], "--watch") || !strcmp(argv[i], "-w")) watching = 1;
        else if (!strcmp(argv[i], "--apocalypse")) apocalypse_variant = 1;
        else if (!strcmp(argv[i], "--teams")) team_play = 1;
        else if (!strncmp(argv[i], "--log=", 6)) ui_set_log(argv[i] + 6);
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) ui_set_log(argv[++i]);
        else { fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]); return 1; }
    }
    if ((e = getenv("PROPHECY_AUTO")))  autoplay = atoi(e);
    if ((e = getenv("PROPHECY_CANON"))) canon_only = atoi(e);
    if ((e = getenv("PROPHECY_RACES"))) use_races  = atoi(e);
    if ((e = getenv("PROPHECY_DRAGON"))) use_dragon = atoi(e);
    if ((e = getenv("PROPHECY_WATER")))  use_water  = atoi(e);
    if ((e = getenv("PROPHECY_CHECK"))) checking = atoi(e);
    if ((e = getenv("PROPHECY_WHY")))   ai_why   = atoi(e);
    if ((e = getenv("PROPHECY_DELAY"))) ai_delay = atoi(e);
    game_seed = (e = getenv("PROPHECY_SEED")) ? (unsigned)atoi(e)
                                              : (unsigned)(time(NULL) ^ getpid());
    srand(game_seed);

    ui_init();
    if (LINES < 29 || COLS < 86) {
        int l = LINES, c = COLS;
        ui_end();
        fprintf(stderr, "This world needs a terminal of at least 86x29. Yours is %dx%d.\n", c, l);
        return 1;
    }

    deck_build();
    memset(taken, 0, sizeof taken);
    for (i = 0; i < RING_N; i++) { space_nadv[i] = 0; space_item[i] = -1; }
    for (i = 0; i < G_COUNT; i++) guild_abil[i] = -1;
    for (i = 0; i < PLANES; i++) {
        planes[i].lesser = planes[i].greater = planes[i].artifact = 1;
        planes[i].revealed = planes[i].closed = 0;
        plane_art[i] = i;
    }
    /* Draw five Lesser and five Greater Guardians from whatever is in the
     * box.  With the expansion Guardians shuffled in there are more than
     * ten, and the rulebook is explicit: "there will be some Guardians left
     * after game preparation -- lay them aside without looking at them." */
    {
        int pool[2][32], n[2] = {0, 0}, s, t;
        for (i = 0; guard_proto[i].name; i++) {
            int gr = guard_proto[i].greater ? 1 : 0;
            if (canon_only && i >= 10) continue;      /* printed ten only */
            if (n[gr] < 32) pool[gr][n[gr]++] = i;
        }
        for (s = 0; s < 2; s++)
            for (i = n[s] - 1; i > 0; i--) {
                int j = rand() % (i + 1);
                t = pool[s][i]; pool[s][i] = pool[s][j]; pool[s][j] = t;
            }
        for (i = 0; i < PLANES; i++) {
            plane_lesser[i]  = pool[0][i % (n[0] ? n[0] : 1)];
            plane_greater[i] = pool[1][i % (n[1] ? n[1] : 1)];
        }
    }
    for (i = PLANES - 1; i > 0; i--) {   /* shuffle: five Artifacts, five Planes */
        int j = rand() % (i + 1), t = plane_art[i];
        plane_art[i] = plane_art[j]; plane_art[j] = t;
    }
    /* PROPHECY_SCREEN=<name>: draw one screen at the current terminal size,
     * dump it through PROPHECY_SHOT, and exit.  See tools/screenshot.sh --
     * without this every layout claim is arithmetic, and arithmetic is what
     * hides a line that sits one row below the bottom of the window. */
    {
        const char *scr = getenv("PROPHECY_SCREEN");
        if (scr && *scr) {
            if (!strcmp(scr, "mode")) {
                draw_mode_screen();
                ui_snapshot();
            } else {
                int k, h, tk[NHEROES] = {0};
                autoplay = 1;
                nplayers = 4;
                for (k = 0; k < nplayers; k++) {
                    do { h = rand() % NHEROES; } while (tk[h]);
                    tk[h] = 1;
                    deal(&players[k], h, (k * RING_N) / nplayers, 1);
                }
                if (!strcmp(scr, "help")) ui_shot_help();
                else                      ui_draw();
                ui_snapshot();
            }
            ui_end();
            return 0;
        }
    }

    if (!autoplay && !watching && !apocalypse_variant && !team_play) choose_mode();
    if (team_play)                 nplayers = 4;   /* "this four-player variant" */
    else if (autoplay || watching) nplayers = 4;
    else                           nplayers = choose_count();
    for (i = 0; i < nplayers; i++) {
        int h;
        if (i == 0 && !autoplay && !watching) h = choose_hero();
        else do { h = rand() % NHEROES; } while (taken[h]);
        taken[h] = 1;
        deal(&players[i], h, (i * RING_N) / nplayers, (autoplay || watching) ? 1 : (i != 0));
        /* "Players on the same team should sit opposite each other so that
         * each team gets an alternate turn" -- turn order is 0,1,2,3, so
         * alternating team ids seats them correctly. */
        if (team_play) players[i].team = i % 2;
    }
    if (team_play)
        glog("Team play: %s and %s stand together, against %s and %s.",
             players[0].name, players[2].name, players[1].name, players[3].name);

    glog("=== PROPHECY ===");
    glog("seed %u   (replay with PROPHECY_SEED=%u)", game_seed, game_seed);
    /* say which game this is, so a log can be read months later without
     * having to remember which flags were passed */
    glog("game: %s%s", team_play ? "team play" :
                       apocalypse_variant ? "apocalypse" : "standard",
                       watching ? ", watched" : "");
    for (i = 0; i < nplayers; i++)
        glog("  @%d %-10s Str %d  Will %d  Gold %d  (%s)", i + 1, players[i].cls,
             players[i].str_now, players[i].will_now, players[i].gold,
             players[i].ai ? "computer" : "you");
    for (i = 0; i < 6; i++) {                 /* the world starts with some life in it */
        int idx;
        do { idx = rand() % RING_N; } while (!IS_WILD(ring[idx].kind));
        place_adventure(idx);
    }

    for (;;) {
        Player *p = &players[cur_player];

        round_no++;
        if (round_no > TURN_CAP) break;
        check_state();
        if (apocalypse) {
            /* "at the beginning of every round, all characters lose
             * 1 Health and 1 Magic" -- every character, every round */
            glog("The dying world takes 1 Health and 1 Magic from everyone.");
            for (i = 0; i < nplayers; i++)
                if (players[i].alive) { hurt(&players[i], 1); drain(&players[i], 1); }
        }
        if (p->alive) {
            glog("-- round %ld: %s --", round_no, p->name);
            p->hope_round = 0;
            art_banners(p);          /* before the Chance card, per the cards */
            chance_card(p);
            take_turn(p);
        }
        if (quit_flag) break;
        check_state();

        if (!final_battle) {
            int closed = 0, most = 0;
            for (i = 0; i < PLANES; i++) closed += planes[i].closed;
            for (i = 0; i < nplayers; i++)
                if (players[i].alive && players[i].artifacts > most) most = players[i].artifacts;
            if (closed == PLANES && most < WIN_ARTIFACTS) {
                final_battle = 1;
                if (apocalypse_variant) {
                    /* "shuffle the entire deck of Chance cards, including
                     * the discarded cards... the deck now counts down the
                     * time towards the Apocalypse" */
                    int q, t;
                    for (q = cdeck_n - 1; q > 0; q--) {
                        int r2 = rand() % (q+1);
                        t = cdeck[q]; cdeck[q] = cdeck[r2]; cdeck[r2] = t;
                    }
                    cdeck_pos = 0;
                    glog("*** The world grows unstable.  The hourglass is turned. ***");
                }
                glog("*** Every Artifact is claimed, yet no hero holds four.");
                glog("*** THE FINAL BATTLE BEGINS -- the losers give up their Artifacts. ***");
                if (apocalypse_variant)
                    for (i = 0; i < nplayers; i++)
                        if (players[i].alive && players[i].artifacts == 0) {
                            players[i].alive = 0;
                            glog("*** %s holds no Artifact, and is swept away. ***",
                                 players[i].name);
                        }
            }
        }
        for (i = 0; i < nplayers; i++)
            if (players[i].alive && players[i].artifacts >= WIN_ARTIFACTS) winner = i;
        if (winner >= 0 || alive_count() <= 1) {
            if (winner < 0)
                for (i = 0; i < nplayers; i++) if (players[i].alive) winner = i;
            break;
        }
        if (!extra_turn && players[cur_player].alive &&
            has_perk(&players[cur_player], PK_TIMELOOP) &&
            !players[cur_player].looped) {
            players[cur_player].looped = 1;
            extra_turn = 1;
            glog("  the Time Loop closes: %s goes again.", players[cur_player].name);
        }
        if (extra_turn && players[cur_player].alive) {
            /* Peaceful Times: she takes a second turn in the same round,
             * which is why Counterfeiting is reset per round and not here */
            extra_turn = 0;
            glog("  %s takes a second turn in these peaceful times.",
                 players[cur_player].name);
            continue;
        }
        extra_turn = 0;
        do { cur_player = (cur_player + 1) % nplayers; }
        while (!players[cur_player].alive);
        players[cur_player].forged_this_round = 0;   /* a fresh round for her */
        players[cur_player].looped = 0;              /* and the Loop may close once more */
    }

    if (winner >= 0) {
        cur_player = winner;
        glog("=== %s the %s takes the throne with %d Artifacts, after %ld rounds ===",
             players[winner].name, players[winner].cls, players[winner].artifacts, round_no);
        if (team_play) {
            int j;
            for (j = 0; j < nplayers; j++)
                if (j != winner && players[j].team == players[winner].team)
                    glog("=== %s shares the crown: they set out together. ===",
                         players[j].name);
        }
    } else if (quit_flag) {
        glog("=== the quest is abandoned after %ld rounds ===", round_no);
    } else {
        glog("=== the world grows old and no hero rises (%ld rounds) ===", round_no);
    }
    if (!autoplay) {
        ui_draw();
        ui_banner(winner >= 0 ? "  A hero is crowned.  [press a key]" : "  [press a key]");
        getch();
    }
    ui_end();
    if (autoplay)
        printf("%s|%s|%d|%ld\n", winner >= 0 ? "WIN" : "CAP",
               winner >= 0 ? players[winner].cls : "-",
               winner >= 0 ? players[winner].artifacts : 0, round_no);
    return 0;
}
