#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "djarhun.h"

Hero heroes[MAX_PLAYERS];
int  nheroes = 4, cur_hero;

static int autoplay, quit_flag, turn_over;
static int book_taken;   /* "you can no longer encounter Gharad" */
static int book_land = -1, book_idx = -1;   /* where it lies, if dropped */
static long turn_count;
static unsigned game_seed;
unsigned ui_seed(void) { return game_seed; }
#define TURN_CAP 12000

/* the per-land decks: "Heroes draw the cards appropriate to the area they
 * are in (Heroes in Aldun only draw the Desert cards, for example)" */
/* Sized for the real decks: 577 cards, of which the shared ones go into
 * every land.  At 64 the land decks filled with shared Events before a
 * single Foe was reached, and heroes met nothing to fight for a whole
 * game -- 0 wins in 25 with no foe encountered at all. */
#define DECK_MAX 1536   /* a land deck is its own cards plus every shared one: ~955 */
static int deck[LAND_COUNT][DECK_MAX], deck_n[LAND_COUNT], deck_pos[LAND_COUNT];

static void beat(void)
{
    if (!autoplay) { ui_draw(); ui_prompt("[space]", " "); }
}

/* A land's deck is its own cards plus every shared one, and when that
 * overflowed at 64 the shared Events filled it before a single Foe was
 * reached: 0 wins in 25 games and not one Foe encountered.  It is sized for
 * the whole table now, and says so out loud rather than silently truncating
 * -- which is the form the bug took both times. */
static void deck_overflow(int land)
{
    static int said[LAND_COUNT];
    if (said[land]) return;
    said[land] = 1;
    glog("!! %s's deck is full at %d cards -- raise DECK_MAX.",
         land_tbl[land].name, DECK_MAX);
}

static void decks_build(void)
{
    int i, k, land;
    for (land = 0; land < LAND_COUNT; land++) deck_n[land] = deck_pos[land] = 0;
    for (i = 0; card_proto[i].name; i++) {
        const Card *c = &card_proto[i];
        for (k = 0; k < (c->copies ? c->copies : 1); k++) {
            if (c->land >= 0) {
                if (deck_n[c->land] < DECK_MAX) deck[c->land][deck_n[c->land]++] = i;
                else deck_overflow(c->land);
            } else {
                /* shared items and treasure sit in every land's deck */
                for (land = 0; land < LAND_COUNT; land++)
                    if (deck_n[land] < DECK_MAX) deck[land][deck_n[land]++] = i;
                    else deck_overflow(land);
                land = LAND_COUNT;
            }
        }
    }
    for (land = 0; land < LAND_COUNT; land++)
        for (i = deck_n[land] - 1; i > 0; i--) {
            int j = rand() % (i + 1), t = deck[land][i];
            deck[land][i] = deck[land][j]; deck[land][j] = t;
        }
}

/* "taking a Spell from the Spell deck" -- there is no separate Spell deck
 * here, the Spells are shuffled through every land's, so drawing one means
 * choosing among the Spells rather than hoping a land draw turns one up.
 * Doing it the other way gave the Scroll about one chance in eight and it
 * withered every time in a hundred games. */
static int deck_of_type(CardType want)
{
    static int list[3][512], n[3], built;
    int slot = (want == C_SPELL) ? 0 : (want == C_ITEM) ? 1 : 2;

    if (!built) {
        int i;
        for (i = 0; card_proto[i].name; i++) {
            CardType t = card_proto[i].type;
            int k = (t == C_SPELL) ? 0 : (t == C_ITEM) ? 1 : -1;
            if (k >= 0 && n[k] < 512) list[k][n[k]++] = i;
        }
        built = 1;
    }
    if (slot > 1 || n[slot] == 0) return -1;
    return list[slot][rand() % n[slot]];
}

static int deck_draw(int land)
{
    if (land < 0 || land >= LAND_COUNT || deck_n[land] <= 0) return -1;
    if (deck_pos[land] >= deck_n[land]) deck_pos[land] = 0;
    return deck[land][deck_pos[land]++];
}

/* ------------------------------------------------------------- carrying --*/
static int item_bonus(const Hero *h, int which)
{
    int i, v = 0;
    for (i = 0; i < h->nitems; i++) {
        const Card *c = &card_proto[h->items[i]];
        v += which == 0 ? c->d_str : which == 1 ? c->d_spd : c->d_sor;
    }
    return v;
}
/* "The Warrior Merod offers to join you on your quest.  He will add 2 to
 * your Strength during battle."  Henchmen carry their bonus on the card,
 * parsed into d_str/d_sor along with the Items' -- and MAX_HENCH has sat
 * in the header unused since it was written. */
static int hench_bonus(const Hero *h, int which)
{
    int i, v = 0;
    for (i = 0; i < h->nhench; i++) {
        const Card *c = &card_proto[h->hench[i]];
        v += which == 0 ? c->d_str : which == 1 ? c->d_spd : c->d_sor;
    }
    return v;
}

/* A Spell cast this turn lends its numbers to the next battle and no
 * further: "add 3 to your Strength for that turn only". */
static int spell_str, spell_sor, spell_guard;
static int extra_turn, extra_run;

/* Defined with the rest of the casting, below; battle() needs it above. */
static void cast_before_battle(Hero *h, int mine, int theirs);
static void cast_on_turn(Hero *h);

static int eff_str(const Hero *h) { return h->str + item_bonus(h, 0) + hench_bonus(h, 0) + spell_str; }
static int eff_spd(const Hero *h) { return h->spd + item_bonus(h, 1) + hench_bonus(h, 1); }
static int eff_sor(const Hero *h) { return h->sor + item_bonus(h, 2) + hench_bonus(h, 2) + spell_sor; }

/* "If a Hero loses in a Strength (or ranged) Battle, they may only make a
 * defence roll if they have at least 1 Item that gives a defence roll
 * bonus.  The Hero may roll a d20... If they roll an 18 or higher, they
 * survive the Strength Battle and it is considered a draw." */
static int has_defence(const Hero *h)
{
    int i;
    if (spell_guard) return 1;         /* Bark Skin, Wizard Armor and their like */
    for (i = 0; i < h->nitems; i++) if (card_proto[h->items[i]].defends) return 1;
    return 0;
}

static void hench_join(Hero *h, int ci)
{
    if (ci < 0) return;
    if (h->nhench >= MAX_HENCH) {
        glog("  %s has no room for the %s.", h->name, card_proto[ci].name);
        return;
    }
    h->hench[h->nhench++] = ci;
    glog("  the %s joins %s.", card_proto[ci].name, h->name);
}

/* "The table shows how many Spells a Hero may hold at any one time" -- by
 * Sorcery, 3-5:1, 6-9:2, 10-16:3, 17+:4.  Below Sorcery 3 you hold none,
 * which is the rules saying magic is not for everyone. */
static void spell_give(Hero *h, int ci)
{
    if (ci < 0) return;
    if (h->nspells >= spells_allowed(h->sor) || h->nspells >= MAX_SPELLS) {
        glog("  %s cannot hold the %s.", h->name, card_proto[ci].name);
        return;
    }
    h->spells[h->nspells++] = ci;
    glog("  %s learns %s.", h->name, card_proto[ci].name);
}

static void spell_drop(Hero *h, int slot)
{
    int i;
    for (i = slot; i < h->nspells - 1; i++) h->spells[i] = h->spells[i + 1];
    h->nspells--;
}

/* What a Hero may carry is a function of a statistic, and a statistic can
 * go down: dying costs a Level, and a Level costs 3 points.  Both limits
 * were checked only when something was picked up, so a Sorcery 6 Hero
 * holding two Spells kept both after falling to Sorcery 4 -- which the
 * invariants reported the moment they were armed, in 9 of 40 games. */
static void enforce_limits(Hero *h)
{
    int lim = spells_allowed(h->sor);

    while (h->nspells > lim && h->nspells > 0) {
        glog("  %s can no longer hold %s, and forgets it.",
             h->name, card_proto[h->spells[h->nspells - 1]].name);
        spell_drop(h, h->nspells - 1);
    }
    lim = items_allowed(h->str);
    while (h->nitems > lim && h->nitems > 0) {
        glog("  %s can no longer carry the %s, and leaves it.",
             h->name, card_proto[h->items[h->nitems - 1]].name);
        h->nitems--;
    }
}

static void item_give(Hero *h, int ci)
{
    if (ci < 0) return;
    if (h->nitems >= items_allowed(h->str) || h->nitems >= MAX_ITEMS) {
        glog("  %s cannot carry the %s.", h->name, card_proto[ci].name);
        return;
    }
    h->items[h->nitems++] = ci;
    glog("  %s takes the %s.", h->name, card_proto[ci].name);
}

/* ---------------------------------------------------------------- levels --
 * "Every time a Hero goes up a Level, they are awarded 3 points that they
 * may use to increase Strength, Speed or Sorcery.  A Hero may not put more
 * than 2 points into a single statistic when they gain a Level." */
static void spend_points(Hero *h)
{
    int left = 3, into_one[3] = {0, 0, 0};
    while (left > 0) {
        int pick;
        if (h->ai) {
            /* Specialise.  Gharad has Strength 20 and Sorcery 20, and a
             * level gives only 3 points with at most 2 into one statistic,
             * so ten levels of spreading them evenly tops out around 13 in
             * everything -- which is never enough to go down.  Two points
             * into a chosen statistic every level reaches the low twenties,
             * which is.  Spreading them was why heroes hit Level 9 and
             * still never entered the Abyss. */
            int best = h->plan;                         /* arms or magic, decided once */
            pick = (into_one[best] < 2) ? best : 1;     /* the spare point buys Speed */
            if (into_one[pick] >= 2) pick = (pick + 1) % 3;
            if (into_one[pick] >= 2) pick = (pick + 1) % 3;
        } else {
            char m[120];
            snprintf(m, sizeof m,
                     "%d point%s to spend:  [s]Strength %d  [p]Speed %d  [c]Sorcery %d",
                     left, left == 1 ? "" : "s", h->str, h->spd, h->sor);
            ui_draw();
            switch (ui_prompt(m, "spc")) {
            case 's': pick = 0; break;
            case 'p': pick = 1; break;
            default:  pick = 2; break;
            }
            if (into_one[pick] >= 2) {
                glog("  no more than 2 points may go into one statistic.");
                continue;
            }
        }
        into_one[pick]++;
        if      (pick == 0) h->str++;
        else if (pick == 1) h->spd++;
        else                h->sor++;
        left--;
    }
    glog("  %s is now Str %d, Speed %d, Sorcery %d.", h->name, h->str, h->spd, h->sor);
}

static void gain_xp(Hero *h, int n)
{
    if (n <= 0 || h->level >= MAX_LEVEL) return;
    h->xp += n;
    glog("  %s gains %d Experience (%d).", h->name, n, h->xp);
    while (h->level < MAX_LEVEL && h->xp >= xp_for_level(h->level + 1)) {
        h->level++;
        glog("*** %s reaches Level %d. ***", h->name, h->level);
        spend_points(h);
    }
}

static void hero_dies(Hero *h)
{
    int loss = 0, i;
    glog("*** %s falls. ***", h->name);
    for (i = 0; i < h->level; i++) loss += die(6);
    h->xp -= loss;
    if (h->xp < 0) h->xp = 0;
    glog("  %d Experience is lost in the dark (%d left).", loss, h->xp);
    while (h->level > 0 && h->xp < xp_for_level(h->level)) {
        h->level--;
        /* "the Hero must also deduct 3 points from their Strength, Speed
         * and/or Sorcery without bringing the statistic below starting
         * quota" */
        {
            /* Take the points off whatever she does NOT fight with.
             * Deducting Strength first stripped the very statistic the
             * hero had been specialising in, so a hero who died a few
             * times ended at Level 9 with Speed 21 and Strength 16 and
             * could never face Gharad. */
            int left = 3, guard = 0, arms = (h->str >= h->sor);
            while (left > 0 && guard++ < 30) {
                if      (h->spd > h->base_spd)              { h->spd--; left--; }
                else if (!arms && h->str > h->base_str)     { h->str--; left--; }
                else if (arms  && h->sor > h->base_sor)     { h->sor--; left--; }
                else if (h->str > h->base_str)              { h->str--; left--; }
                else if (h->sor > h->base_sor)              { h->sor--; left--; }
                else break;
            }
        }
        glog("  %s slips back to Level %d.", h->name, h->level);
    }
    /* "The player, to the Hero's right, must choose 2 Items/Henchman that
     * will go to the discard pile" */
    for (i = 0; i < 2 && h->nitems > 0; i++) {
        int slot = rand() % h->nitems, k;
        glog("  the %s is looted from the body.", card_proto[h->items[slot]].name);
        for (k = slot; k < h->nitems - 1; k++) h->items[k] = h->items[k + 1];
        h->nitems--;
    }
    /* After the looting, not before: the two Items taken from the body
     * usually bring a Hero back under the new limit on their own, and
     * trimming first threw away kit the rules had already spoken for. */
    enforce_limits(h);
    if (h->book) {
        /* "If the player dies while holding the Book of Avrakar, it drops
         * onto the space they died to wait for someone to claim it."  It
         * used to simply cease to exist, which made every game in which a
         * carrier died unwinnable -- and one in four of them capped for
         * exactly that reason. */
        h->book = 0;
        book_land = h->land; book_idx = h->idx;
        glog("  the Book of Avrakar falls at %s, and lies there.",
             space_at(h->land, h->idx)->name);
    }
    h->land = h->home_land; h->idx = h->home_idx;
    h->health = 4;
    glog("  %s wakes again at %s.", h->name, space_at(h->land, h->idx)->name);
    turn_over = 1;
}

static void hurt(Hero *h, int n)
{
    h->health -= n;
    if (h->health <= 0) hero_dies(h);
}

/* --------------------------------------------------------------- battle --
 * "someone rolls a d8 for the Foe and the Hero rolls a d8 for themselves.
 * They add their statistic and die roll together... Whoever gets the
 * highest score is the victor.  If it is a tie, then the Battle is a draw
 * and the turn ends for that Hero." */
/* What battle() is fighting, for the Skills that care which kind it is.
 * battle() takes a name and two numbers, which is right for Gharad and the
 * Watch and everything else that is not a card -- so the card's category
 * comes alongside rather than through it. */
static int cur_foe_kind;

static int  ai_ready(const Hero *h);
static void ship_damage(Hero *h);
static int  tarri_port(void);

static int battle(Hero *h, const char *foe, int fstr, int fsor)
{
    int use_sorcery = fsor > fstr;
    int mine, theirs = use_sorcery ? fsor : fstr;
    int d1, d2;

    if (h->ai)
        cast_before_battle(h, use_sorcery ? eff_sor(h) : eff_str(h), theirs);
    mine = use_sorcery ? eff_sor(h) : eff_str(h);
    d1 = die(8); d2 = die(8);
    /* "You may add 2 to your battle die roll against Undead" -- and its
     * cousins that ask where you are standing rather than what you face. */
    if (h->s_die) {
        int applies = 0;
        if (h->s_die_vs && cur_foe_kind == h->s_die_vs) applies = 1;
        if (h->s_die_at && strstr(space_at(h->land, h->idx)->name, h->s_die_at))
            applies = 1;
        if (applies) {
            d1 += h->s_die;
            glog("  %s fights in her element: +%d.", h->name, h->s_die);
        }
    }
    int a = mine + d1, b = theirs + d2;

    glog("%s (%s %d + %d = %d)  vs  %s (%d + %d = %d)",
         h->name, use_sorcery ? "Sorcery" : "Strength", mine, d1, a, foe, theirs, d2, b);
    ui_die("BATTLE", 8, d1);
    beat();

    if (a > b) {
        gain_xp(h, theirs);        /* "The statistic used for Battle is the
                                    * number of Experience Points gained" */
        /* "Every Animal you slay, you may eat it, fortifying 1 Health." */
        if (h->s_fortify && h->health < max_health(h) &&
            (h->s_fortify == FK_ANY || h->s_fortify == cur_foe_kind)) {
            h->health++;
            glog("  %s is the stronger for it. Health %d.", h->name, h->health);
        }
        return 1;
    }
    if (a == b) { glog("  a draw; %s's turn ends.", h->name); turn_over = 1; return -1; }

    if (!use_sorcery && has_defence(h)) {
        int d = die(20);
        ui_die("DEFENCE", 20, d);
        glog("  %s throws up a guard: d20 -> %d.", h->name, d);
        if (d >= 18) { glog("  it holds. The battle is a draw."); turn_over = 1; return -1; }
    }
    /* "During sea battles, your Ship may be damaged... Hull points are lost
     * if you lose a battle against that Foe."  Out on the water it is the
     * ship that takes it, which is what makes a big hull worth its Gems. */
    if (h->land == LAND_TARRI && h->ship >= 0) {
        ship_damage(h);
        turn_over = 1;
        return 0;
    }
    glog("  %s is wounded.", h->name);
    hurt(h, 1);
    turn_over = 1;
    return 0;
}

/* ------------------------------------------------------------- casting --
 * The AI casts on the two occasions a person would: before a fight it is
 * not sure of, and when it is hurt.  A Spell it cannot use is one it holds.
 *
 * The kinds that do something here are the ones the card text can be read
 * for: 65 of the 139 printed Spells.  The rest are carried and read. */
static int spell_cast(Hero *h, int slot, const char *why)
{
    const Card *c = &card_proto[h->spells[slot]];
    int used = 1;

    switch (c->skind) {
    case SK_BOOST:
        /* the size of the boost is printed on the card */
        if (c->d_sor || c->sor) spell_sor += c->sval ? c->sval : 2;
        else                    spell_str += c->sval ? c->sval : 2;
        glog("  %s casts %s: +%d %s for this battle.", h->name, c->name,
             c->sval ? c->sval : 2, (c->d_sor || c->sor) ? "Sorcery" : "Strength");
        break;
    case SK_GUARD:
        spell_guard = 1;
        glog("  %s casts %s: it will turn one blow aside.", h->name, c->name);
        break;
    case SK_HEAL:
        if (h->health >= max_health(h)) return 0;
        h->health += c->sval ? c->sval : 1;
        if (h->health > max_health(h)) h->health = max_health(h);
        glog("  %s casts %s: Health %d.", h->name, c->name, h->health);
        break;
    case SK_EXTRA:
        extra_turn = 1;
        glog("  %s casts %s and the turn is not over.", h->name, c->name);
        break;
    default:
        used = 0;
        break;
    }
    if (used) {
        (void)why;
        spell_drop(h, slot);
    }
    return used;
}

/* Before a fight: something that swings it, if we hold one. */
static void cast_before_battle(Hero *h, int mine, int theirs)
{
    int i;

    if (mine + 3 >= theirs + 3 && mine >= theirs) return;   /* already ahead */
    for (i = 0; i < h->nspells; i++) {
        int k = card_proto[h->spells[i]].skind;
        if (k == SK_BOOST || k == SK_GUARD) {
            if (spell_cast(h, i, "battle")) return;
        }
    }
}

/* At the top of a turn: heal when it matters, and take the extra turn. */
static void cast_on_turn(Hero *h)
{
    int i;

    for (i = 0; i < h->nspells; i++) {
        int k = card_proto[h->spells[i]].skind;
        if (k == SK_HEAL && h->health < max_health(h) && h->health <= 2) {
            if (spell_cast(h, i, "hurt")) return;
        }
    }
}

/* ----------------------------------------------------------- encounters --
 * "Each trap has a Speed rating.  You must do a Speed battle with the trap
 * to determine if you avoid it.  If you fail to avoid the trap, you would
 * normally lose 1 Health.  If a Hero successfully avoids the trap, they must
 * discard it (they get Experience Points equal to the Trap's Speed).  If you
 * tie during this Speed battle, then you avoid the trap, it is not discarded
 * and you may continue encountering any cards left on the space."
 *
 * A tie is not what a tie is anywhere else in this game -- everywhere else
 * it ends your turn -- so this cannot go through battle().  41 Traps were
 * typed C_EVENT until now and printed their text.
 *
 * Returns 1 if the Trap stays on the space. */
static int trap_kept;

static void trap_spring(Hero *h, const Card *c)
{
    int mine = eff_spd(h), theirs = c->spd ? c->spd : 8;
    int d1 = die(8), d2 = die(8);
    int a = mine + d1, b = theirs + d2;

    trap_kept = 0;
    glog("%s springs %s (Speed %d).", h->name, c->name, theirs);
    glog("  %s (Speed %d + %d = %d)  vs  the trap (%d + %d = %d)",
         h->name, mine, d1, a, theirs, d2, b);
    ui_die("TRAP", 8, d1);
    beat();

    if (a > b) {
        glog("  %s is quick enough.", h->name);
        gain_xp(h, theirs);
        return;
    }
    if (a == b) {
        /* "you avoid the trap, it is not discarded and you may continue" --
         * so this neither wounds nor ends the turn, and the trap stays. */
        glog("  a hair's breadth: the trap is avoided, and stays sprung.");
        trap_kept = 1;
        return;
    }
    glog("  %s is caught by it.", h->name);
    hurt(h, 1);
    turn_over = 1;      /* "they would immediately end their turn" */
}

/* "You will discover secret places on your journey.  Unless otherwise
 * noted, they remain on the space they were drawn."  Every one of the 65
 * was being discarded after a single visit, which is the opposite of what
 * a Place is for -- they are what gives a board its memory.
 *
 * A fixture that hands out a reward on every visit forever is a farm, and
 * the cards know it: the ones that give anything come with a counter.  So
 * only the countered rewards do anything, and the rest stay and are read.
 *
 * Returns 1 if the Place stays. */
static int place_left;

static int place_visit(Hero *h, const Card *c, int slot, int sid)
{
    int *left = &space_left[sid][slot];

    switch (c->pkind) {
    case PK_TOLL:
        /* "You must pay the Demon 1 Gem to cross the Lake of Fire.  If you
         * do not, you lose 1 Health." */
        if (h->gems > 0) {
            h->gems--;
            glog("  %s pays the toll at %s. (%d Gems)", h->name, c->name, h->gems);
        } else {
            glog("  %s cannot pay at %s.", h->name, c->name);
            hurt(h, 1);
            turn_over = 1;
        }
        return 1;
    case PK_STAT: case PK_HEAL: case PK_DRAW:
        if (*left <= 0) {
            glog("  %s is spent, and crumbles away.", c->name);
            return 0;
        }
        (*left)--;
        if (c->pkind == PK_STAT) {
            /* the point goes where it is worth most, as a Level's points do */
            if (h->sor > h->str) h->sor++; else h->str++;
            glog("  %s at %s: +1 %s (%d left).", h->name, c->name,
                 h->sor > h->str ? "Sorcery" : "Strength", *left);
        } else if (c->pkind == PK_HEAL) {
            if (h->health < max_health(h)) h->health++;
            glog("  %s rests at %s: Health %d (%d left).",
                 h->name, c->name, h->health, *left);
        } else {
            /* "they may draw a Treasure Card" -- and the Treasure deck is
             * headed ITEM on the sheets, so this used to test for a card
             * type that does not exist in the table and quietly give
             * nothing at all. */
            int t = deck_of_type(C_ITEM);
            glog("  %s searches %s (%d left).", h->name, c->name, *left);
            if (t >= 0) {
                if (card_proto[t].tkind == TK_GEM) {
                    h->gems += card_proto[t].gems ? card_proto[t].gems : 1;
                    glog("    %s: %d Gems (%d).", card_proto[t].name,
                         card_proto[t].gems ? card_proto[t].gems : 1, h->gems);
                } else item_give(h, t);
            }
        }
        if (*left <= 0) {
            glog("  the last counter is spent; %s is gone.", c->name);
            return 0;
        }
        return 1;
    default:
        /* "Unless otherwise noted, they remain on the space they were
         * drawn" -- and 57 of the 65 Places are ones whose text cannot be
         * read well enough to do anything.  Leaving those on the board is
         * faithful to the sentence and ruinous in practice: a space draws
         * only "enough to bring the number of cards instructed", so a
         * permanent card on a draw-1 space sterilises it for the rest of the
         * game.  Forty games went from 39 finished to 33, and fights per
         * game from 312 to 270, as the board filled up with cards that do
         * nothing.  So a Place stays when it is something to come back to,
         * and is read and discarded when it is not. */
        glog("%s finds %s.", h->name, c->name);
        return 0;
    }
}

/* A Stranger sells one thing and stands there selling it.  "Unless
 * otherwise noted, they usually stay on the space" -- so one with a service
 * is a fixture worth walking to, and one whose service will not read is met
 * once and moves on, because a permanent card that does nothing sterilises
 * the space it sits on.  That was measured with the Places: all 65 made
 * permanent cost six games in forty.
 *
 * Returns 1 if the Stranger stays. */
static int ai_goal(const Hero *h);

static int stranger_visit(Hero *h, const Card *c)
{
    int price = c->gems ? c->gems : 1;

    switch (c->nkind) {
    case NK_HEAL:
        if (h->health < max_health(h) && h->gems >= price) {
            h->gems -= price;
            h->health++;
            glog("  %s pays %s %d Gem%s and is mended. Health %d (%d Gems).",
                 h->name, c->name, price, price > 1 ? "s" : "",
                 h->health, h->gems);
        } else glog("%s passes %s by.", h->name, c->name);
        return 1;
    case NK_SPELL:
        if (h->gems >= price && h->nspells < spells_allowed(h->sor)
            && h->nspells < MAX_SPELLS) {
            int sp = deck_of_type(C_SPELL);
            if (sp >= 0) {
                h->gems -= price;
                h->spells[h->nspells++] = sp;
                glog("  %s buys %s from %s for %d Gem%s.",
                     card_proto[sp].name, "the spell", c->name, price,
                     price > 1 ? "s" : "");
            }
        } else glog("%s passes %s by.", h->name, c->name);
        return 1;
    case NK_TRAIN:
        if (h->gems >= price) {
            h->gems -= price;
            if (h->sor > h->str) h->sor++; else h->str++;
            glog("  %s trains with %s: +1 %s (%d Gems).", h->name, c->name,
                 h->sor > h->str ? "Sorcery" : "Strength", h->gems);
        } else glog("%s passes %s by.", h->name, c->name);
        return 1;
    case NK_BUY:
        /* "he will buy any Item" -- and an Item you cannot use is worth more
         * as a Gem than as a slot. */
        if (h->nitems > items_allowed(h->str) - 1 && h->nitems > 0) {
            glog("  %s sells the %s to %s. (%d Gems)", h->name,
                 card_proto[h->items[h->nitems - 1]].name, c->name, h->gems + 1);
            h->nitems--;
            h->gems++;
        } else glog("%s passes %s by.", h->name, c->name);
        return 1;
    case NK_FLY:
        /* "The Pegasus offers to fly you to any space on the board (but not
         * to the Abyss)."  A border has one crossing space on it and nothing
         * but dice to reach it, so this is the answer to the longest-running
         * complaint in this game: a Hero who cannot land where she means to.
         * The flight ends the turn, so the space she lands on is met next
         * turn rather than in the middle of resolving this one. */
        if (h->gems >= price) {
            int want = h->ai ? ai_goal(h) : -1;
            if (want >= 0 && want != h->idx) {
                h->gems -= price;
                h->idx = want;
                glog("  %s pays %s %d Gem%s and is carried to %s.",
                     h->name, c->name, price, price > 1 ? "s" : "",
                     space_at(h->land, h->idx)->name);
                turn_over = 1;
                return 1;
            }
        }
        glog("%s passes %s by.", h->name, c->name);
        return 1;
    default:
        glog("%s meets %s.", h->name, c->name);
        return 0;                       /* nothing to come back for */
    }
}

static void resolve_card(Hero *h, int ci)
{
    const Card *c = &card_proto[ci];
    switch (c->type) {
    case C_FOE:
        glog("%s meets %s (Str %d / Sor %d).", h->name, c->name, c->str, c->sor);
        cur_foe_kind = c->foekind;
        battle(h, c->name, c->str, c->sor);
        cur_foe_kind = FK_NONE;
        break;
    case C_TREASURE:
        h->gems += c->gems;
        glog("%s finds %s: %d Gems (%d).", h->name, c->name, c->gems, h->gems);
        break;
    case C_ITEM:
        /* Some of the Treasure deck is spent where it is found rather than
         * carried, and being headed ITEM on the sheet is not the same as
         * being one. */
        if (c->tkind == TK_GEM) {
            h->gems += c->gems ? c->gems : 1;
            glog("  %s finds %s: %d Gem%s (%d).", h->name, c->name,
                 c->gems ? c->gems : 1, (c->gems > 1) ? "s" : "", h->gems);
        } else if (c->tkind == TK_SPELL) {
            /* "If your Sorcery allows, you may memorize the Scroll, taking a
             * Spell from the Spell deck.  Whether you can or not the Scroll
             * withers to the discard pile." */
            if (h->nspells < spells_allowed(h->sor) && h->nspells < MAX_SPELLS) {
                int sp = deck_of_type(C_SPELL);
                if (sp >= 0) {
                    h->spells[h->nspells++] = sp;
                    glog("  %s reads the Scroll and learns %s.",
                         h->name, card_proto[sp].name);
                } else glog("  the Scroll is beyond %s, and withers.", h->name);
            } else {
                glog("  %s cannot hold what the Scroll teaches; it withers.",
                     h->name);
            }
        } else {
            item_give(h, ci);
        }
        break;
    case C_TRAP:
        trap_spring(h, c);
        break;
    case C_PLACE:
        /* handled by the caller, which knows the slot the counters live in */
        break;
    case C_EVENT:
        /* "If a Hero is told to lose a turn or Health, then they would
         * immediately end their turn and no longer deal with any other
         * cards/scenarios.  If a Hero is told to lose a turn, not only do
         * they end their turn at that point, but they miss their next turn
         * as well."  That is the whole Luck rule, and it is why Luck is
         * numbered I: it is dealt with before anything else on the space,
         * and it can stop the rest happening at all. */
        switch (c->lkind) {
        case LK_TURN:
            glog("%s: %s.", c->name, "the turn is lost, and the next one too");
            h->miss += 1;
            turn_over = 1;
            break;
        case LK_STAT:
            /* "You may gain 1 Strength, Speed or Sorcery" -- into whatever
             * this Hero actually fights with. */
            if (h->sor > h->str) h->sor++; else h->str++;
            glog("%s: %s gains a point of %s.", c->name, h->name,
                 h->sor > h->str ? "Sorcery" : "Strength");
            break;
        case LK_DROP:
            if (h->nitems > 0) {
                glog("%s: %s loses the %s.", c->name, h->name,
                     card_proto[h->items[h->nitems - 1]].name);
                h->nitems--;
            } else if (h->gems > 0) {
                h->gems--;
                glog("%s: %s loses a Gem (%d).", c->name, h->name, h->gems);
            }
            break;
        default:
            glog("%s -- %s.", c->name, c->text ? c->text : "nothing comes of it");
            break;
        }
        break;
    case C_SPELL:
        spell_give(h, ci);
        break;
    case C_HENCH:
        hench_join(h, ci);
        break;
    default:
        glog("%s -- %s.", c->name, c->text ? c->text : "nothing comes of it");
        break;
    }
}

/* ------------------------------------------------------- ranged battle --
 * "Ranged Battle occurs when two parties are 1-3 spaces away from each
 * other and they Battle against Speed.  As long as one of the Heroes has a
 * ranged weapon, ranged Battle can occur."
 *
 * This is the third use the game has for Speed and the only one that makes
 * it a fighting statistic; the `ranged` flag has been parsed onto the cards
 * since the sheets were read and never once looked at.
 *
 * The rules that shape it:
 *   1  "If the attacker is the only party with a ranged weapon, then no one
 *       loses Health when the attacker loses the Battle."
 *   2  "If both parties have ranged weapons, then whoever loses the Battle
 *       loses 1 Health."
 *   5  "If you win a ranged Battle, you only gain Experience Points for the
 *       Foe's Speed score."
 *   6  "You gain no Treasure/Gems as a result of Speed battles."
 *   7  "Any ranged Battle that is going to take place happens first."
 *   -  "Heroes may not use ranged Battle if they are on the same space as
 *       the Foe."
 *
 * A Foe fires back only if its card carries the bow-and-arrow symbol, which
 * is a picture and cannot be read off the sheet -- so in practice the shot
 * is nearly always free, and the thing to watch in the soak is whether that
 * turns into a farm. */
static int has_ranged(const Hero *h)
{
    int i;
    for (i = 0; i < h->nitems; i++) if (card_proto[h->items[i]].ranged) return 1;
    for (i = 0; i < h->nhench; i++) if (card_proto[h->hench[i]].ranged) return 1;
    return 0;
}

/* Returns 1 if the Hero's turn ends here. */
static int ranged_phase(Hero *h)
{
    int len = land_len(h->land), d, best = -1, best_slot = -1, best_sid = -1;

    if (!has_ranged(h)) return 0;
    if (land_tbl[h->land].topo != TOPO_RING) return 0;   /* not down a spiral */

    /* the nearest Foe 1 to 3 spaces off, either way round */
    for (d = 1; d <= 3 && best < 0; d++) {
        int side;
        for (side = 0; side < 2 && best < 0; side++) {
            int at  = ((h->idx + (side ? -d : d)) % len + len) % len;
            int sid = space_id(h->land, at), k;
            for (k = 0; k < space_ncard[sid]; k++)
                if (card_proto[space_card[sid][k]].type == C_FOE) {
                    best = space_card[sid][k]; best_slot = k; best_sid = sid;
                    break;
                }
        }
    }
    if (best < 0) return 0;

    {
        const Card *foe = &card_proto[best];
        int mine = eff_spd(h), theirs = foe->spd;
        int d1, d2, a, b, k;

        /* A Foe with no Speed printed cannot be shot at meaningfully. */
        if (theirs <= 0) return 0;

        d1 = die(8); d2 = die(8);
        a = mine + d1; b = theirs + d2;
        glog("%s looses a shot at %s (Speed %d).", h->name, foe->name, theirs);
        glog("  %s (Speed %d + %d = %d)  vs  %s (%d + %d = %d)",
             h->name, mine, d1, a, foe->name, theirs, d2, b);
        ui_die("RANGED", 8, d1);
        beat();

        if (a > b) {
            glog("  %s falls to the shot.", foe->name);
            gain_xp(h, theirs);         /* rule 5: the Foe's Speed, not its Strength */
            for (k = best_slot; k < space_ncard[best_sid] - 1; k++) {
                space_card[best_sid][k] = space_card[best_sid][k + 1];
                space_left[best_sid][k] = space_left[best_sid][k + 1];
            }
            space_ncard[best_sid]--;
            return 0;                   /* "If you win, or tie, you then..." */
        }
        if (a == b) { glog("  the shot goes wide."); return 0; }
        if (foe->ranged) {              /* rule 2: it shoots back */
            glog("  %s returns fire.", foe->name);
            hurt(h, 1);
            return 1;                   /* "If you lose the Battle, your turn ends" */
        }
        /* rule 1: the attacker was the only one armed, so nothing is lost */
        glog("  the shot misses, and nothing comes back.");
        return 0;
    }
}

/* ----------------------------------------------------------------- sea --
 * "To travel the high seas, you need a ship.  Ships have a Speed rate
 * indicated on the card that overrides a Hero's Speed."  The Tar'ri Ocean
 * has exactly one crossing on it -- Waves Elidor, onto Durach -- so a ship
 * bought at the City of Elidor puts you out there, and docking brings you
 * back to the same place.
 *
 * "If you are at sea, and your Ship is destroyed, you will begin your next
 * turn at an adjacent space (either Frostburn or Durach, Hero's choice)." */
/* "The most common way to travel from Urthe to Durach, is to bring Propha
 * some Ancient Bones.  You can find these around Urthe."  Eight of them are
 * in the Urthe deck and they had no purpose whatever until now: Prophas
 * Keep let anyone through.  Gedwin Springs is the way home that costs
 * nothing, so wanting the Bones is never a trap -- they buy you the shorter
 * road on a thirty-two space ring. */
static int bones_ok(Hero *h, const Space *sp)
{
    int i;

    if (strcmp(sp->name, "Prophas Keep")) return 1;
    for (i = 0; i < h->nitems; i++)
        if (!strcmp(card_proto[h->items[i]].name, "ANCIENT BONES")) {
            glog("  %s gives Propha the Ancient Bones.", h->name);
            for (; i < h->nitems - 1; i++) h->items[i] = h->items[i + 1];
            h->nitems--;
            return 1;
        }
    glog("  Propha turns %s away: she wants Ancient Bones.", h->name);
    return 0;
}

static int tarri_port(void)
{
    int i, n = land_len(LAND_TARRI);
    for (i = 0; i < n; i++)
        if (space_at(LAND_TARRI, i)->kind == SP_CROSSING) return i;
    return 0;
}

static void ship_buy(Hero *h)
{
    int i, best = -1;

    /* The best ship she can afford and still have something left to spend:
     * a Hero who sinks every Gem into a hull has nothing to heal with. */
    for (i = 0; i < ship_count; i++)
        if (ship_tbl[i].value + 1 <= h->gems &&
            (best < 0 || ship_tbl[i].speed > ship_tbl[best].speed))
            best = i;
    if (best < 0) return;
    /* An AI only goes to sea for what is on it, and there is nothing on it
     * she needs: the road to the Abyss does not run through the Ocean.  So
     * she buys one when she is not yet ready for the deep end and can spare
     * the Gems, and not otherwise. */
    if (h->ai && ai_ready(h)) return;

    h->gems -= ship_tbl[best].value;
    h->ship  = best;
    h->hull  = ship_tbl[best].hull;
    glog("  %s buys the %s at the shipyards: Hull %d, Speed %d, %d Gems.",
         h->name, ship_tbl[best].name, h->hull, ship_tbl[best].speed,
         ship_tbl[best].value);
    h->land = LAND_TARRI;
    h->idx  = tarri_port();
    glog(">>> %s puts to sea at %s. <<<", h->name,
         space_at(h->land, h->idx)->name);
    turn_over = 1;
}

/* Losing at sea costs the hull, not the Hero.  "If your Ship's Hull value
 * reaches zero, it is destroyed." */
static void ship_damage(Hero *h)
{
    if (h->ship < 0) return;
    h->hull--;
    glog("  the %s takes a hit: Hull %d.", ship_tbl[h->ship].name, h->hull);
    if (h->hull > 0) return;
    glog("*** the %s goes down. ***", ship_tbl[h->ship].name);
    h->ship = -1;
    h->land = LAND_DURACH;
    h->idx  = space_at(LAND_TARRI, tarri_port())->to_idx;
    glog("  %s washes ashore at %s.", h->name,
         space_at(h->land, h->idx)->name);
}

static void resolve_space(Hero *h)
{
    const Space *sp = space_at(h->land, h->idx);
    int sid = space_id(h->land, h->idx), i;

    if (book_land == h->land && book_idx == h->idx) {
        h->book = 1; book_land = book_idx = -1;
        glog("*** %s takes up the fallen BOOK OF AVRAKAR. ***", h->name);
    }

    /* "You may Heal up to your maximum Health at the Mujarin Crypt."  The
     * board abbreviates its cells, so the Skill keeps the one distinctive
     * word of the place and looks for it in the name. */
    if (h->s_heal_at && h->health < max_health(h) &&
        strstr(sp->name, h->s_heal_at)) {
        h->health = max_health(h);
        glog("%s is made whole at %s.", h->name, sp->name);
    }

    /* "Any ranged Battle that is going to take place happens first." */
    if (ranged_phase(h)) { turn_over = 1; return; }
    if (!h->alive) return;

    switch (sp->kind) {
    case SP_GHARAD:
        if (book_taken) { glog("Gharad's tower stands empty."); return; }
        /* "If a Hero is victorious, they may take the Book of Avrakar...
         * If a Hero is defeated by Gharad, then they lose 2 Health and are
         * immediately teleported back to the Lake of Tears." */
        glog(">>> Gharad rises. Strength 20, Sorcery 20. <<<");
        if (battle(h, "Gharad", 20, 20) == 1) {
            h->book = 1; book_taken = 1;
            glog("*** %s takes the BOOK OF AVRAKAR. ***", h->name);
        } else if (h->alive) {
            hurt(h, 1);
            h->idx = 0;
            glog("  %s is flung back to the Lake of Tears.", h->name);
        }
        return;
    case SP_ELIDOR:
        if (h->book && MOR_GOOD(h->morality)) return;      /* the game ends */
        glog("%s rests in the City of Elidor.", h->name);
        if (h->gems >= 1 && h->health < max_health(h)) {
            h->gems--; h->health++;
            glog("  the Healer mends a Health for 1 Gem.");
        }
        /* "Ships can be purchased from the shipbuilders of Elidor."  The
         * only way onto the Tar'ri Ocean, which is forty spaces and 127
         * cards that until now no Hero could reach unless she began there. */
        if (h->ship < 0) ship_buy(h);
        return;
    case SP_GYPSY:
        if (h->book && !MOR_GOOD(h->morality)) return;
        glog("%s is watched by the Gypsies.", h->name);
        return;
    case SP_MARKET:
        glog("%s comes to %s.", h->name, sp->name);
        if (h->gems >= 2 && h->nitems < items_allowed(h->str)) {
            int ci = deck_draw(h->land);
            if (ci >= 0 && card_proto[ci].type == C_ITEM) {
                h->gems -= 2;
                glog("  buys the %s for 2 Gems.", card_proto[ci].name);
                item_give(h, ci);
            }
        }
        return;
    default:
        break;
    }

    /* "If there are already cards on a space that instructs to draw cards,
     * then you only draw enough to bring the number of cards instructed on
     * the space." */
    {
        int want = sp->draw - space_ncard[sid];
        for (i = 0; i < want && space_ncard[sid] < MAX_ON_SPACE; i++) {
            int ci = deck_draw(h->land);
            if (ci < 0) break;
            space_left[sid][space_ncard[sid]] = card_proto[ci].counters;
            space_card[sid][space_ncard[sid]++] = ci;
        }
    }
    /* "There is a roman numeral at the top right of each card.  The lowest
     * number is dealt with first, and so on, until they have all been dealt
     * with -- or the Hero loses a turn or Health."  It has been parsed since
     * the sheets were read and never once consulted, so a Trap (II) and a
     * Foe (III) on one space were met in whatever order they were drawn.
     * Cards with no numeral read go last: an unread numeral is not a I. */
    {
        /* A Trap that ties is avoided, stays on the space, and does NOT stop
         * you dealing with the rest -- so it has to be passed over for the
         * remainder of this visit without being removed.  `done` is that:
         * one bit a card, which is why MAX_ON_SPACE being 4 is comfortable. */
        unsigned done = 0;

        while (h->alive && !turn_over) {
            int pick = -1, k, ci;
            for (k = 0; k < space_ncard[sid]; k++) {
                int a, b;
                if (done & (1u << k)) continue;
                if (pick < 0) { pick = k; continue; }
                a = card_proto[space_card[sid][k]].order;
                b = card_proto[space_card[sid][pick]].order;
                if (!a) a = 99;                /* nothing unnumbered goes first */
                if (!b) b = 99;
                if (a < b) pick = k;
            }
            if (pick < 0) break;               /* everything here is dealt with */
            ci = space_card[sid][pick];
            {
                int keep = (card_proto[ci].type == C_FOE);
                trap_kept = 0;
                place_left = 0;
                if (card_proto[ci].type == C_PLACE)
                    place_left = place_visit(h, &card_proto[ci], pick, sid);
                else if (card_proto[ci].type == C_STRANGER)
                    place_left = stranger_visit(h, &card_proto[ci]);
                else
                    resolve_card(h, ci);
                if (keep && turn_over) break;  /* it beat you; it holds the ground */
                /* A tied Trap and a Place both stay where they are, and both
                 * have to be passed over rather than removed or the loop
                 * would meet them again for ever. */
                if (trap_kept || place_left) { done |= 1u << pick; continue; }
            }
            for (k = pick; k < space_ncard[sid] - 1; k++) {
                space_card[sid][k] = space_card[sid][k + 1];
                space_left[sid][k] = space_left[sid][k + 1];
                /* the bits move with the cards they mark */
                done = (done & ~(1u << k)) | (((done >> (k + 1)) & 1u) << k);
            }
            space_ncard[sid]--;
        }
    }
}

/* ---------------------------------------------------------- invariants --
 * Under DJARHUN_CHECK these are asserted after every turn.  A log with
 * `INV ` in it is a bug rather than a quirk -- which is the whole point:
 * the things that went wrong in this game and its siblings were all quiet.
 * The Book ceasing to exist when its carrier died was invisible for as long
 * as nobody counted, and every one of these would have said so at once. */
void check_all(const char *when)
{
    int i, books = 0;

    if (!checking) return;
    for (i = 0; i < nheroes; i++) {
        const Hero *h = &heroes[i];
        if (!h->alive) continue;
        if (h->health < 1 || h->health > max_health(h))
            glog("INV %s: Health %d of %d (%s)", h->name, h->health,
                 max_health(h), when);
        if (h->level < 0 || h->level > MAX_LEVEL)
            glog("INV %s: Level %d (%s)", h->name, h->level, when);
        /* "no more than 2 points into a single statistic" per level, and the
         * starting quota is a floor -- losing a level must never take a
         * statistic below what the card was printed with. */
        if (h->str < h->base_str || h->spd < h->base_spd || h->sor < h->base_sor)
            glog("INV %s: below starting quota  %d/%d %d/%d %d/%d (%s)",
                 h->name, h->str, h->base_str, h->spd, h->base_spd,
                 h->sor, h->base_sor, when);
        if (h->nitems > items_allowed(h->str) || h->nitems > MAX_ITEMS)
            glog("INV %s: %d Items at Strength %d, allowed %d (%s)",
                 h->name, h->nitems, h->str, items_allowed(h->str), when);
        if (h->nspells > spells_allowed(h->sor) || h->nspells > MAX_SPELLS)
            glog("INV %s: %d Spells at Sorcery %d, allowed %d (%s)",
                 h->name, h->nspells, h->sor, spells_allowed(h->sor), when);
        if (h->land < 0 || h->land >= LAND_COUNT ||
            h->idx  < 0 || h->idx  >= land_len(h->land))
            glog("INV %s: off the board at land %d space %d (%s)",
                 h->name, h->land, h->idx, when);
        if (h->gems < 0) glog("INV %s: %d Gems (%s)", h->name, h->gems, when);
        books += h->book;
    }
    /* The Book is in exactly one place at a time: with a hero, lying where
     * its carrier fell, or still in Gharad's tower.  It went missing once
     * already -- the flag was simply cleared when a carrier died, and any
     * game after that was unwinnable and said nothing. */
    if (books > 1) glog("INV the Book is being carried by %d heroes (%s)", books, when);
    if (books && book_land >= 0)
        glog("INV the Book is both carried and lying on the board (%s)", when);
    if (book_taken && !books && book_land < 0)
        glog("INV the Book has been taken and is nowhere (%s)", when);
    for (i = 0; i < MAX_SPACES; i++)
        if (space_ncard[i] < 0 || space_ncard[i] > MAX_ON_SPACE) {
            glog("INV space %d holds %d cards (%s)", i, space_ncard[i], when);
            break;
        }
}

/* ------------------------------------------------------ win probability --
 * A Battle here is `mine + d8` against `theirs + d8`, so the chance of
 * winning one is a sum over the 64 ways two dice fall: there are 8 - |k|
 * ways to roll a difference of k, for k in -7..7.  You win outright when
 * d1 - d2 > theirs - mine; a tie ends your turn.
 *
 * The real Foes run from Strength 4 to 23, so this matters far more than it
 * did against the placeholder deck: walking onto an Ancient Wyrm at
 * Strength 8 is not a fight, it is a way to lose a Health. */
static int win_chance(int mine, int theirs)
{
    int start = 1 - (mine - theirs), k, ways = 0;

    if (start < -7) start = -7;
    for (k = start; k <= 7; k++) ways += 8 - (k < 0 ? -k : k);
    return ways * 100 / 64;
}

/* What is waiting on a space, as a number: how badly the Foes lying there
 * would go for this hero.  0 is safe. */
/* What a space is worth walking onto, as a risk: higher is worse, and
 * ai_direction() negates it.
 *
 * This used to `continue` on everything that was not a Foe and never look
 * at the space's own kind at all, so seven card types and the whole board
 * were invisible to it -- markets, healers, Strangers, Traps, the Places
 * with counters on them.  Over a hundred games that left 84.4% of every
 * direction choice an exact tie, decided by a coin.  A hero who cannot see
 * a healer two spaces away when she is on her last Health is not being
 * careful; she is not looking. */
static int space_risk(const Hero *h, int land, int idx)
{
    int sid = space_id(land, idx), i, risk = 0;
    const Space *sp = space_at(land, idx);
    int hurt_bad = (h->health <= 2);

    for (i = 0; i < space_ncard[sid]; i++) {
        const Card *c = &card_proto[space_card[sid][i]];
        int mine, win;
        switch (c->type) {
        case C_FOE:
            mine = (c->sor > c->str) ? eff_sor(h) : eff_str(h);
            win  = win_chance(mine, (c->sor > c->str) ? c->sor : c->str);
            /* a fight you will probably lose costs a Health, and at one
             * Health it costs the game */
            if      (win >= 55) risk -= 2;      /* worth walking into  */
            else if (win >= 35) risk += 2;
            else                risk += hurt_bad ? 12 : 6;
            break;
        case C_TRAP:
            /* a Trap prints its Speed, so the odds are exact: win and it is
             * Experience, lose and it is a Health */
            win = win_chance(eff_spd(h), c->spd ? c->spd : 8);
            if      (win >= 60) risk -= 1;
            else                risk += hurt_bad ? 6 : 3;
            break;
        case C_PLACE:
            /* only the ones with something left on them are worth a walk */
            if (space_left[sid][i] > 0 &&
                (c->pkind == PK_STAT || c->pkind == PK_HEAL || c->pkind == PK_DRAW))
                risk -= (c->pkind == PK_HEAL && h->health < max_health(h)) ? 5 : 3;
            else if (c->pkind == PK_TOLL && h->gems == 0)
                risk += hurt_bad ? 6 : 2;       /* it takes a Health instead */
            break;
        case C_STRANGER:
            if (c->nkind == NK_HEAL && h->health < max_health(h)
                && h->gems >= (c->gems ? c->gems : 1)) risk -= 5;
            else if (c->nkind == NK_FLY && h->gems >= (c->gems ? c->gems : 1)) risk -= 2;
            else if (c->nkind == NK_SPELL || c->nkind == NK_BUY) risk -= 1;
            break;
        case C_ITEM:
            /* something on the ground is free if there is room for it, and
             * worth going out of the way for if it feeds the plan */
            if (h->nitems < items_allowed(h->str))
                risk -= 1 + ((h->plan == 2) ? c->d_sor : c->d_str) * 2;
            break;
        case C_HENCH:
            if (h->nhench < MAX_HENCH) risk -= 1;
            break;
        case C_SPELL:
            if (h->nspells < spells_allowed(h->sor)) risk -= 1;
            break;
        default: break;
        }
    }

    /* And the board itself, which this had never once consulted. */
    switch (sp->kind) {
    case SP_MARKET:
        risk -= (h->health < max_health(h) && h->gems >= 1) ? 4
              : (h->gems >= 2) ? 2 : 0;
        break;
    case SP_HEALER:
        risk -= (h->health < max_health(h)) ? 5 : 0;
        break;
    case SP_ELIDOR:
        risk -= (h->health < max_health(h) && h->gems >= 1) ? 4 : 0;
        break;
    case SP_DRAW:
        risk -= sp->draw;                       /* cards are Experience */
        break;
    default: break;
    }
    return risk;
}

/* ----------------------------------------------------------- AI purpose --
 * A hero wandering at random takes thousands of turns to cross a board of
 * 133 spaces, so the AI plays to a goal instead.  There are only three
 * things worth wanting, in this order:
 *
 *   1. carrying the Book -- get to Elidor or the Gypsy Camp, whichever
 *      the Morality points at;
 *   2. strong enough for Gharad -- get to the Oasis of Ezrabar and go down;
 *   3. otherwise -- stay where the fighting is and gain levels.
 *
 * Gharad has Strength 20 and Sorcery 20, so "strong enough" is a real bar:
 * a hero needs most of ten levels behind her before the Abyss is anything
 * but a way to die. */
/* A land is worth entering once you have the levels for it.  This replaces
 * a single hard-coded "strong enough for Gharad" number, which was both a
 * magic constant and the wrong shape: the lands are not equally dangerous,
 * and what a hero really wants to know is whether THIS one will kill her. */
static int ai_may_enter(const Hero *h, int land)
{
    return h->level >= land_tbl[land].min_level;
}

static int ai_ready(const Hero *h)
{
    return ai_may_enter(h, LAND_ABYSS);
}

/* Which rule below picked the destination.  A goal held for twenty turns
 * and a goal that changes every turn look identical in a trace unless it
 * says which question produced it. */
static const char *why_rule = "-";

/* Every crossing on this board leads to Durach and only to Durach, except
 * Durach's own two: Springvale into Frostburn and the Wolfbane Hills into
 * Aldun.  So Durach is the hub, and the road to the Abyss is the same from
 * anywhere -- reach Durach, cross to Aldun, and the Oasis of Ezrabar is the
 * way down.  (Tar'ri wants a ship and Urthe wants time travel; neither is
 * built, so nothing on this board leads back into them.) */
static int next_land_toward_abyss(int land)
{
    switch (land) {
    case LAND_FROST: case LAND_TARRI: case LAND_URTHE: return LAND_DURACH;
    case LAND_DURACH:                                  return LAND_ALDUN;
    default:                                           return -1;
    }
}

/* The crossing in this land that opens onto `to`, or -1. */
static int crossing_to(int land, int to)
{
    int i, n = land_len(land);
    for (i = 0; i < n; i++) {
        const Space *sp = space_at(land, i);
        if (sp->kind == SP_CROSSING && sp->to_land == to) return i;
    }
    return -1;
}

/* Which space in this land the hero is trying to reach, or -1 for none. */
static int ai_goal(const Hero *h)
{
    int i, n = land_len(h->land);
    why_rule = "-";
    if (h->book) {
        SpaceKind want = MOR_GOOD(h->morality) ? SP_ELIDOR : SP_GYPSY;
        why_rule = MOR_GOOD(h->morality) ? "book-to-elidor" : "book-to-gypsies";
        for (i = 0; i < n; i++) if (space_at(h->land, i)->kind == want) return i;
        /* not in this land: make for whichever crossing leads onward */
        why_rule = "book-heading-home";
        for (i = 0; i < n; i++) {
            const Space *sp = space_at(h->land, i);
            if (sp->kind == SP_CROSSING && sp->to_land == LAND_DURACH) return i;
        }
        for (i = 0; i < n; i++)
            if (space_at(h->land, i)->kind == SP_CROSSING) return i;
        why_rule = "-";
        return -1;
    }
    /* --- the objectives, in the order a Hero would want them ------------
     * Everything below is a space on the ring she is already standing on, so
     * none of it is a travel plan that can override ai_direction()'s reading
     * of what is waiting at the other end.  That distinction matters: an
     * earlier attempt gave the wandering turns a destination in another
     * land, and heroes commuted between lands and died in the harder one --
     * six games in forty.  An objective says where on this ring to head; the
     * direction code still decides whether the next step is survivable. */
    {
        int i, want = -1, best = -1000;

        /* 1. Mend.  On two Health or fewer, the nearest thing that heals is
         *    worth more than anything else on the board. */
        if (h->health <= 2) {
            for (i = 0; i < n; i++) {
                const Space *sp = space_at(h->land, i);
                int sid = space_id(h->land, i), k, v = 0;
                if (sp->kind == SP_HEALER) v = 10;
                else if ((sp->kind == SP_MARKET || sp->kind == SP_ELIDOR)
                         && h->gems >= 1) v = 9;
                for (k = 0; k < space_ncard[sid]; k++) {
                    const Card *c = &card_proto[space_card[sid][k]];
                    if (c->type == C_STRANGER && c->nkind == NK_HEAL
                        && h->gems >= (c->gems ? c->gems : 1)) v = 9;
                    if (c->type == C_PLACE && c->pkind == PK_HEAL
                        && space_left[sid][k] > 0) v = 8;
                }
                if (v > best) { best = v; want = i; }
            }
            if (best > 0) { why_rule = "mend"; return want; }
        }
    }

    if (ai_ready(h)) {
        why_rule = "abyss-gate";
        for (i = 0; i < n; i++) if (space_at(h->land, i)->kind == SP_ABYSS_GATE) return i;
        /* The next land on the road, not whichever crossing came first in
         * the table.  On this board Durach is the hub and every other land
         * has exactly one way out, so "toward the Abyss" is a lookup -- but
         * this used to fall through to `any-crossing`, which took the first
         * crossing it found and is as likely to lead away as toward: 4,038
         * turns of it against 598 of abyss-gate over forty games. */
        {
            int want = next_land_toward_abyss(h->land), at;
            if (want >= 0 && (at = crossing_to(h->land, want)) >= 0) {
                why_rule = "toward-the-abyss";
                return at;
            }
        }
    }
    /* 2. Grow.  A Place with counters left on it hands out Strength or
     *    Sorcery for nothing, and those are what the Abyss wants. */
    {
        int i;
        for (i = 0; i < n; i++) {
            int sid = space_id(h->land, i), k;
            for (k = 0; k < space_ncard[sid]; k++) {
                const Card *c = &card_proto[space_card[sid][k]];
                if (c->type == C_PLACE && c->pkind == PK_STAT
                    && space_left[sid][k] > 0) { why_rule = "grow"; return i; }
            }
        }
    }

    /* 3. Spend.  Gems do nothing in a purse. */
    if (h->gems >= 4) {
        int i;
        for (i = 0; i < n; i++) {
            SpaceKind k = space_at(h->land, i)->kind;
            if (k == SP_MARKET || k == SP_ELIDOR) { why_rule = "spend"; return i; }
        }
    }

    /* 4. Hunt.  Experience is the whole of the ladder to the Abyss and it
     *    comes off Foes, so a Foe she would comfortably beat is a reason to
     *    walk somewhere -- but only in health, and only at odds she would
     *    take deliberately rather than stumble into. */
    if (h->health >= 3) {
        int i, want = -1, best = 0;
        for (i = 0; i < n; i++) {
            int sid = space_id(h->land, i), k;
            if (i == h->idx) continue;
            for (k = 0; k < space_ncard[sid]; k++) {
                const Card *c = &card_proto[space_card[sid][k]];
                int mine, win, worth;
                if (c->type != C_FOE) continue;
                mine = (c->sor > c->str) ? eff_sor(h) : eff_str(h);
                worth = (c->sor > c->str) ? c->sor : c->str;
                win = win_chance(mine, worth);
                if (win < 60) continue;
                /* "The statistic used for Battle is the number of Experience
                 * Points gained", so a fight in her own discipline pays her
                 * twice: the Experience, and the practice of the statistic
                 * she is actually building. */
                if ((c->sor > c->str) == (h->plan == 2)) worth += 3;
                if (worth > best) { best = worth; want = i; }
            }
        }
        if (want >= 0) { why_rule = "hunt"; return want; }
    }

    why_rule = "-";
    return -1;                       /* nothing worth walking to */
}

/* Clockwise or anti: whichever closes on the goal, unless what is waiting
 * there would kill her.  Progress is worth something, but not a Health --
 * and at one Health, not anything at all. */
static int ai_direction(const Hero *h, int steps)
{
    int n = land_len(h->land), goal = ai_goal(h);
    int fwd = ((h->idx + steps) % n + n) % n;
    int bck = ((h->idx - steps) % n + n) % n;
    int score_f = -space_risk(h, h->land, fwd);
    int score_b = -space_risk(h, h->land, bck);

    if (goal >= 0) {
        int cw  = ((goal - fwd) % n + n) % n;
        int ccw = ((goal - bck) % n + n) % n;
        if (cw  > n / 2) cw  = n - cw;
        if (ccw > n / 2) ccw = n - ccw;
        score_f += 10 - cw;
        score_b += 10 - ccw;
    }
    {
        int take = (score_f == score_b) ? ((rand() % 2) ? 1 : -1)
                                        : (score_f > score_b ? 1 : -1);
        wlog("  why %s  goal %s [%s]  fwd %s %d  bck %s %d  -> %s by %d",
             h->name,
             goal >= 0 ? space_at(h->land, goal)->name : "wander", why_rule,
             space_at(h->land, fwd)->name, score_f,
             space_at(h->land, bck)->name, score_b,
             take > 0 ? "fwd" : "bck",
             score_f > score_b ? score_f - score_b : score_b - score_f);
        return take;
    }
}

/* ------------------------------------------------------------- the turn --*/
static void take_turn(Hero *h)
{
    int steps, dir, sides, len;

    if (!h->alive) return;
    turn_over = 0;
    /* "for that turn only" -- whatever was cast last turn is spent. */
    spell_str = spell_sor = spell_guard = extra_turn = 0;
    ui_dice_clear();
    if (h->ai) cast_on_turn(h);

    if (h->miss > 0) { h->miss--; glog("%s loses the turn.", h->name); beat(); return; }

    /* "When traveling in the Abyss, Heroes always start at the Lake of
     * Tears.  They move one space per turn in a spiral." */
    if (land_tbl[h->land].topo == TOPO_SPIRAL) {
        /* "They move one space per turn in a spiral... until they reach
         * Gharad's tower.  If a Hero chooses to head back to the Lake of
         * Tears, they would move back in the opposite direction, ignoring
         * the instructions on the spaces."  Without the way back a hero who
         * reached the Tower stayed there for the rest of the game -- which
         * is exactly what happened: the Book was taken 245 times across 20
         * games and never once carried home. */
        int out;
        if (h->ai) out = h->book || h->health <= 1;
        else if (h->idx == 0) out = 0;
        else {
            ui_draw();
            out = ui_prompt("[i] deeper into the Abyss   [o] back towards the Lake of Tears",
                            "io") == 'o';
        }
        if (out) {
            if (h->idx > 0) h->idx--;
            glog("%s climbs back towards the Lake of Tears: %s.", h->name,
                 space_at(h->land, h->idx)->name);
            if (h->idx == 0) {
                /* out at the Oasis she came down by */
                h->land = LAND_ALDUN; h->idx = 10;
                glog(">>> %s comes up at the Oasis of Ezrabar. <<<", h->name);
                resolve_space(h);
            }
            beat();
            return;
        }
        if (h->idx < land_len(h->land) - 1) h->idx++;
        glog("%s presses deeper into the Abyss: %s.", h->name,
             space_at(h->land, h->idx)->name);
        resolve_space(h);
        beat();
        return;
    }

    /* "on the Sea you only move as fast as your ship" -- the movement die
     * does not apply out there, and neither does the Hero's own Speed. */
    if (h->land == LAND_TARRI && h->ship >= 0) {
        int len2 = land_len(h->land), port = tarri_port(), step = ship_tbl[h->ship].speed;
        int d = h->ai ? ai_direction(h, step) : 1;
        int to = ((h->idx + d * step) % len2 + len2) % len2;
        /* "You do not need to roll the exact die number to reach a port, but
         * simply have enough of a die roll to reach a port."  So a move that
         * would carry her past the harbour puts in at it instead. */
        {
            int k, at = h->idx;
            for (k = 1; k <= step; k++) {
                at = ((h->idx + d * k) % len2 + len2) % len2;
                if (at == port) { to = port; break; }
            }
        }
        h->idx = to;
        glog("%s sails %d to %s.", h->name, step, space_at(h->land, h->idx)->name);
        if (h->idx == port) {
            const Space *pt = space_at(h->land, h->idx);
            h->land = pt->to_land; h->idx = pt->to_idx;
            glog(">>> %s docks the %s and comes ashore at %s. <<<", h->name,
                 ship_tbl[h->ship].name, space_at(h->land, h->idx)->name);
        }
        resolve_space(h);
        beat();
        return;
    }

    sides = move_die(eff_spd(h));
    /* "A Hero may also choose any dice lower than what they can roll.  This
     * means, if a Hero rolls a d8 for movement, they may choose to roll a d4
     * or d6 instead."  This is not a flourish: a border has one crossing
     * space on it, and reaching a particular space needs an exact landing.
     * A hero who can only throw her largest die orbits the ring for
     * hundreds of turns without ever stopping on the one space she wants. */
    {
        int goal = h->ai ? ai_goal(h) : -1;
        if (goal >= 0) {
            static const int ladder[5] = { 4, 6, 8, 10, 12 };
            int len2 = land_len(h->land), k;
            int cw  = ((goal - h->idx) % len2 + len2) % len2;
            int ccw = len2 - cw;
            int want = cw < ccw ? cw : ccw;
            for (k = 0; k < 5; k++)          /* smallest die that can reach it */
                if (ladder[k] <= sides && ladder[k] >= want) { sides = ladder[k]; break; }
        }
    }
    steps = die(sides);
    ui_die("MOVE", sides, steps);
    glog("%s rolls d%d for movement: %d.", h->name, sides, steps);

    if (h->ai) dir = ai_direction(h, steps);
    else {
        ui_draw();
        dir = ui_prompt("Which way?   [a] anti-clockwise   [d] clockwise", "ad") == 'a' ? -1 : 1;
    }
    len = land_len(h->land);
    h->idx = ((h->idx + dir * steps) % len + len) % len;
    glog("%s moves to %s.", h->name, space_at(h->land, h->idx)->name);

    /* a crossing is taken by landing on it */
    {
        const Space *sp = space_at(h->land, h->idx);
        if ((sp->kind == SP_CROSSING || sp->kind == SP_ABYSS_GATE) && sp->to_land >= 0) {
            /* cross only when the far side is where she is trying to get */
            int go = 0;
            if (h->ai) {
                /* Walking to a door and refusing it is the shape of every
                 * orbit in these games: ai_goal() sends a ready Hero to the
                 * crossing into Durach when she is out in Frostburn, and
                 * this used to answer "only if it opens onto Aldun" and
                 * turn her away.  She stepped off, was sent back, and did it
                 * again -- 7,669 turns of `toward-the-abyss` in seed 31
                 * without ever crossing.  The two questions have to be
                 * answered by the same rule, so it is the same lookup. */
                if (!ai_may_enter(h, sp->to_land))  go = 0;   /* not yet */
                else if (!bones_ok(h, sp))          go = 0;   /* Propha wants her Bones */
                else if (sp->kind == SP_ABYSS_GATE) go = !h->book;
                else if (h->book)                   go = (sp->to_land == LAND_DURACH);
                else if (ai_ready(h))
                    go = (sp->to_land == next_land_toward_abyss(h->land));
                else                                go = (rand() % 4 == 0);
            }
            if (h->ai && go && !bones_ok(h, sp)) go = 0;
            if (!h->ai) {
                char m[120];
                snprintf(m, sizeof m, "%s opens onto %s.  Cross?  [y]/[n]",
                         sp->name, land_tbl[sp->to_land].name);
                ui_draw();
                go = ui_prompt(m, "yn") == 'y';
            }
            if (go) {
                h->land = sp->to_land;
                h->idx  = sp->to_idx;
                glog(">>> %s crosses into %s, at %s. <<<", h->name,
                     land_tbl[h->land].name, space_at(h->land, h->idx)->name);
            }
        }
    }
    resolve_space(h);
    beat();
}

/* --------------------------------------------------------------- setup --*/
/* The Skills name their kit in the card's words -- "a Bow", "the Grimblade"
 * -- and the deck shouts: BOW, GRIMBLADE.  So match on the words rather than
 * the exact string, and take the first Item that carries them all. */
static int find_proto_like(const char *want)
{
    int i, j;
    for (i = 0; card_proto[i].name; i++) {
        const char *n = card_proto[i].name;
        const char *w = want;
        int ok = 1;
        if (card_proto[i].type != C_ITEM) continue;
        while (*w && ok) {
            char word[24]; int k = 0;
            while (*w == ' ') w++;
            while (*w && *w != ' ' && k < (int)sizeof word - 1) word[k++] = *w++;
            word[k] = 0;
            if (k < 3) continue;                 /* "a", "of" and the like */
            for (j = 0; n[j]; j++) {
                int m = 0;
                while (word[m] && n[j + m] &&
                       (n[j + m] | 32) == (word[m] | 32)) m++;
                if (!word[m]) break;
            }
            if (!n[j]) ok = 0;
        }
        if (ok) return i;
    }
    return -1;
}

static void deal_hero(Hero *h, int t, int ai)
{
    const HeroTemplate *ht = &hero_tbl[t];
    memset(h, 0, sizeof *h);
    snprintf(h->name, sizeof h->name, "%s", ht->name);
    h->race = ht->race; h->skill = ht->skill;
    h->str = h->base_str = ht->str;
    h->spd = h->base_spd = ht->spd;
    h->sor = h->base_sor = ht->sor;
    h->morality = ht->morality;
    h->land = h->home_land = ht->home_land;
    h->idx  = h->home_idx  = ht->home_idx;
    h->health = 4; h->gems = 1; h->alive = 1; h->ai = ai;
    h->ship = -1; h->hull = 0;
    /* "The statistic used for Battle is determined by the highest statistic
     * of the Foe" -- but what a Hero brings to it is her own, and the card
     * says which she is.  Ties go to Strength, which more Foes fight with. */
    h->plan = (h->sor > h->str) ? 2 : 0;
    h->s_die = ht->s_die; h->s_die_vs = ht->s_die_vs; h->s_die_at = ht->s_die_at;
    h->s_fortify = ht->s_fortify; h->s_heal_at = ht->s_heal_at;
    h->s_start_spell = ht->s_start_spell; h->s_start_item = ht->s_start_item;
    /* "You start your journey with a Bow."  Kit that is on the card is kit
     * she has before the first roll. */
    if (h->s_start_spell) {
        int sp2 = deck_of_type(C_SPELL);
        if (sp2 >= 0 && h->nspells < MAX_SPELLS) {
            h->spells[h->nspells++] = sp2;
            glog("  %s sets out knowing %s.", h->name, card_proto[sp2].name);
        }
    }
    if (h->s_start_item) {
        int ci = find_proto_like(h->s_start_item);
        if (ci >= 0 && h->nitems < MAX_ITEMS) {
            h->items[h->nitems++] = ci;
            glog("  %s sets out carrying the %s.", h->name, card_proto[ci].name);
        }
    }
}

static int winner_check(void)
{
    int i;
    for (i = 0; i < nheroes; i++) {
        Hero *h = &heroes[i];
        const Space *sp;
        if (!h->alive || !h->book) continue;
        sp = space_at(h->land, h->idx);
        if (MOR_GOOD(h->morality) && sp->kind == SP_ELIDOR) return i;
        if (!MOR_GOOD(h->morality) && sp->kind == SP_GYPSY)  return i;
    }
    return -1;
}

int main(int argc, char **argv)
{
    int i, winner = -1;
    const char *seedenv = getenv("DJARHUN_SEED");

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--log") && i + 1 < argc) ui_set_log(argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("usage: %s [--log FILE]\n\n"
                   "  --log FILE   write a full text log of the game\n\n"
                   "in play: [v] switches between the three board views\n\n"
                   "environment: DJARHUN_SEED (replay), DJARHUN_AUTO (headless),\n"
                   "             DJARHUN_TRACE (= --log)\n", argv[0]);
            return 0;
        }
    }
    autoplay = getenv("DJARHUN_AUTO") != NULL;
    ai_why   = getenv("DJARHUN_WHY")   != NULL;
    checking = getenv("DJARHUN_CHECK") != NULL;
    game_seed = seedenv ? (unsigned)atoi(seedenv) : (unsigned)time(NULL);
    srand(game_seed);

    ui_init();
    glog("=== DJARHUN: the Quest for the Book of Avrakar ===");
    glog("seed %u", game_seed);

    board_check();
    decks_build();
    {
        /* Sized from the table, not guessed: this was int taken[32] when
         * there were eight Heroes, and reading the 54 real ones off the
         * cards turned it into a stack overflow that ASan caught but that
         * had already been running silently in every soak. */
        int taken[MAX_HERO_TEMPLATES] = {0};
        int guard = 0;
        for (i = 0; i < nheroes; i++) {
            int t;
            do { t = rand() % hero_count; } while (taken[t] && ++guard < 500);
            taken[t] = 1;
            deal_hero(&heroes[i], t, autoplay ? 1 : (i != 0));
            glog("  @%d %-8s %-6s Str %d Speed %d Sorcery %d  %s  home: %s",
                 i + 1, heroes[i].name, heroes[i].race, heroes[i].str,
                 heroes[i].spd, heroes[i].sor,
                 heroes[i].morality == MOR_FAIR ? "Fair" :
                 heroes[i].morality == MOR_KIND ? "Kind" : "Vile",
                 space_at(heroes[i].land, heroes[i].idx)->name);
        }
    }

    extra_run = 0;
    for (;;) {
        take_turn(&heroes[cur_hero]);
        check_all(heroes[cur_hero].name);
        if (quit_flag) break;
        if (++turn_count > TURN_CAP) break;
        if ((winner = winner_check()) >= 0) break;
        /* "you may take a total of 3 consecutive turns" -- a Spell that
         * buys another turn has to actually buy one.  Setting a flag that
         * nothing reads is how the sibling project turned a reward into a
         * punishment, so this is capped and consumed here where the turn
         * order is decided. */
        if (extra_turn && heroes[cur_hero].alive && ++extra_run < 3) continue;
        extra_run = 0;
        do { cur_hero = (cur_hero + 1) % nheroes; } while (!heroes[cur_hero].alive);
    }

    if (winner >= 0)
        glog("=== %s carries the Book of Avrakar home, after %ld turns ===",
             heroes[winner].name, turn_count);
    else
        glog("=== the Book is never recovered (%ld turns) ===", turn_count);
    if (!autoplay) { ui_draw(); ui_prompt("[press a key]", ""); }
    ui_end();
    if (autoplay)
        printf("%s|%s|%ld\n", winner >= 0 ? "WIN" : "CAP",
               winner >= 0 ? heroes[winner].name : "-", turn_count);
    return 0;
}
