#include "talisman.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* The only platform-specific call in the game is the one that seeds the
 * random number generator from the process id.  Windows spells it
 * differently and has no <unistd.h>; everywhere else it is POSIX, which
 * -std=c99 hides unless asked for (see the Makefile). */
#if defined(_WIN32)
#  include <process.h>
#  define talisman_getpid() _getpid()
#else
#  include <unistd.h>
#  define talisman_getpid() getpid()
#endif

Player players[MAX_PLAYERS];
int    nplayers   = 4;
int    cur_player = 0;
int    enabled_sets = 1 << SET_BASE;      /* the base game is always in */

static const char *set_key[SET_COUNT] = {
    "base", "reaper", "frostmarch", "sacred", "bloodmoon", "firelands",
    "cataclysm", "dungeon", "highland", "woodland", "harbinger"
};

static int  deck[MAX_DECK], deck_n, deck_pos;
static int  sdeck[MAX_SDECK], sdeck_n, sdeck_pos;   /* the Spell deck */
static int  extra_turn;                      /* Temporal Warp  */
static int  quit_flag;
static int  ai_delay = 700;   /* ms; TALISMAN_DELAY overrides            */

unsigned ui_seed(void);
int      ui_delay(void) { return ai_delay; }
static int  autoplay;         /* TALISMAN_AUTO=1: headless, no rendering  */
static int  sets_chosen;      /* --sets or TALISMAN_SETS given            */
static int  look_depth = 1;   /* plies of movement lookahead              */
static int  look_ab;          /* TALISMAN_LOOK_AB: half the table looks   */
static int  road_on = 1;      /* consult the distance tables when moving  */
static int  road_ab;          /* TALISMAN_ROAD_AB: half the table does    */
static int  forced_char = -1; /* TALISMAN_CHAR: seat one's Character      */
static unsigned game_seed;    /* recorded in the log so a game can be replayed */
unsigned ui_seed(void) { return game_seed; }
static int  watching;         /* --watch: all-AI but drawn, at a pace     */
static long turn_count;
static int  turn_over;   /* rulebook: a defeat or stand-off ends the turn at once */
static int  crown_reached;   /* once true, death is permanent for everyone   */

/* ------------------------------------------------- alternative endings --
 * "Shuffle the six cards and pick one randomly; without looking at the card
 * chosen place it facedown on the Crown of Command.  The first player to
 * reach the space reveals the card."  Until then nobody -- player or AI --
 * knows what is waiting, which is the whole point of the rule. */
int         alt_endings;             /* the optional rule is in play       */
int         henchmen_on;             /* Adventure rule 3: Henchmen         */
int         chaos_on;                /* Adventure rule 4: Chaos Bloodbath  */
static Ending ending = END_CROWN;
static int  ending_revealed;
static unsigned ending_spent;        /* cards already turned and discarded */
static int  demon_lives;             /* the Demon Lord's four             */
static int  win_override = -1;       /* an ending won outright, not by survival */
static int  belt_wearer = -1;        /* who is wearing the Belt, if anyone */

int ending_kind(void)  { return ending_revealed ? (int)ending : -1; }
int belt_holder(void)  { return belt_wearer; }

/* The Void discards itself and "a new End card is placed" -- so the deck
 * really does shrink, and the AI is entitled to know that. */
static void deal_ending(void)
{
    int pick, tries;
    ending_revealed = 0;
    for (tries = 0; tries < 64; tries++) {
        pick = rand() % END_COUNT;
        if (!(ending_spent & (1u << pick))) { ending = (Ending)pick; return; }
    }
    ending = END_CROWN;
}
/* the AI needs to know: once somebody is up there, everyone else is on a
 * clock and playing safe is just a slower way of losing */
int endgame(void) { return crown_reached; }
/* One flag per character card.  This was sized 16 when there were only the
 * fourteen base characters; the expansions bring 52, and dealing one of the
 * later ones wrote straight off the end of the array. */
static int  char_taken[MAX_CHARS];  /* character cards already in play       */

#define TURN_CAP 4000

int roll(void) { return rand() % 6 + 1; }

/* ---------------------------------------------------------------- deck */

static void deck_shuffle(void)
{
    int i, j, t;

    for (i = deck_n - 1; i > 0; i--) {
        j = rand() % (i + 1);
        t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    deck_pos = 0;
}

/* Each side board draws from its own stack, reshuffling when it runs out. */
static int citydeck[128], citydeck_n, citydeck_pos;
static int timedeck[128], timedeck_n, timedeck_pos;

static void deck_build(void)
{
    int i, k;
    int talismans_in = 0;

    deck_n = citydeck_n = timedeck_n = 0;
    for (i = 0; deck_proto[i].name; i++) {
        /* The City and Timescape stacks are never shuffled into the main
         * Adventure deck, not even with --sets=all: they are drawn only on
         * their own boards. */
        if (deck_proto[i].set == SET_CITYD) {
            for (k = 0; k < deck_proto[i].copies && citydeck_n < 128; k++)
                citydeck[citydeck_n++] = i;
            continue;
        }
        if (deck_proto[i].set == SET_TIMED) {
            for (k = 0; k < deck_proto[i].copies && timedeck_n < 128; k++)
                timedeck[timedeck_n++] = i;
            continue;
        }
        if (!SET_ON(deck_proto[i].set)) continue;
        {
            int copies = deck_proto[i].copies;
            /* Chaos Bloodbath: "Use only one of the Talisman cards, instead
             * of all four."  With the expansions in there are more than
             * four, so the rule is read as its intent: exactly one, total. */
            if (chaos_on && deck_proto[i].talisman)
                copies = talismans_in ? 0 : 1;
            if (deck_proto[i].talisman) talismans_in += copies;
            for (k = 0; k < copies && deck_n < MAX_DECK; k++)
                deck[deck_n++] = i;
        }
    }
    for (i = citydeck_n - 1; i > 0; i--) { int j = rand() % (i+1), t = citydeck[i];
                                            citydeck[i] = citydeck[j]; citydeck[j] = t; }
    for (i = timedeck_n - 1; i > 0; i--) { int j = rand() % (i+1), t = timedeck[i];
                                            timedeck[i] = timedeck[j]; timedeck[j] = t; }
    deck_shuffle();
}

/* returns an index into deck_proto, so the card can be left on the board */
static int side_draw(int *d, int n, int *pos)
{
    if (n <= 0) return -1;
    if (*pos >= n) *pos = 0;
    return d[(*pos)++];
}
static int city_draw(void) { return side_draw(citydeck, citydeck_n, &citydeck_pos); }
static int time_draw(void) { return side_draw(timedeck, timedeck_n, &timedeck_pos); }

static int deck_draw(void)
{
    if (deck_n <= 0) return -1;
    if (deck_pos >= deck_n) { deck_shuffle(); glog("(the adventure deck is reshuffled)"); }
    return deck[deck_pos++];
}

static void sdeck_shuffle(void)
{
    int i, j, t;

    for (i = sdeck_n - 1; i > 0; i--) {
        j = rand() % (i + 1);
        t = sdeck[i]; sdeck[i] = sdeck[j]; sdeck[j] = t;
    }
    sdeck_pos = 0;
}

static void sdeck_build(void)
{
    int i, k;

    sdeck_n = 0;
    for (i = 0; spell_proto[i].name; i++) {
        if (!SET_ON(spell_proto[i].set)) continue;
        for (k = 0; k < spell_proto[i].copies && sdeck_n < MAX_SDECK; k++)
            sdeck[sdeck_n++] = i;
    }
    sdeck_shuffle();
}

static int sdeck_draw(void)
{
    if (sdeck_n <= 0) return -1;
    if (sdeck_pos >= sdeck_n) { sdeck_shuffle(); glog("(the Spell deck is reshuffled)"); }
    return sdeck[sdeck_pos++];
}

static int random_monster(void)
{
    int i, n = 0, pick;

    for (i = 0; deck_proto[i].name; i++)
        if (deck_proto[i].type == C_ENEMY || deck_proto[i].type == C_SPIRIT) n++;
    pick = rand() % n;
    for (i = 0; deck_proto[i].name; i++)
        if (deck_proto[i].type == C_ENEMY || deck_proto[i].type == C_SPIRIT)
            if (pick-- == 0) return i;
    return 0;
}

/* -------------------------------------------------------------- helpers */

static void speed_up(void)   { ai_delay = (ai_delay > 100) ? ai_delay - 100 : 0; }
static void slow_down(void)  { ai_delay = (ai_delay < 3000) ? ai_delay + 100 : 3000; }

/* Let the watcher pause, change speed or quit between AI actions. */
static void watch_wait(void)
{
    int ch;

    if (quit_flag) return;
    ch = ui_wait_key(ai_delay > 0 ? ai_delay : 0);
    if (ch == ERR) return;                      /* nobody touched anything */

    for (;;) {
        if (ch == 'q')                    { quit_flag = 1; return; }
        if (ch == '+' || ch == '=')       { speed_up();  return; }
        if (ch == '-' || ch == '_')       { slow_down(); return; }
        if (ch != ' ' && ch != '\n')      return;

        for (;;) {
            char b[96];
            snprintf(b, sizeof b,
                     "  PAUSED   [space] resume   [+/-] speed (%dms)   [q] quit", ai_delay);
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

static void step(Player *p)
{
    if (autoplay) return;
    ui_draw();
    if (!p->ai) { ui_pause(""); return; }

    if (watching) {
        char b[96];
        snprintf(b, sizeof b,
                 "  WATCHING   [space] pause   [+/-] speed (%dms)   [q] quit", ai_delay);
        ui_banner(b);
        watch_wait();
    } else {
        ui_banner("  ... thinking ...");
        if (ai_delay > 0) napms(ai_delay);
    }
}

static int alive_count(void)
{
    int i, n = 0;
    for (i = 0; i < nplayers; i++) if (players[i].alive) n++;
    return n;
}

static void deal_character(Player *p, int t, int idx);
static void enforce_limit(Player *p);

static void spell_discard(Player *p, int slot)
{
    int i;

    if (slot < 0 || slot >= p->nspells) return;
    glog("%s loses %s.", p->name, spell_proto[p->spells[slot]].name);
    for (i = slot; i < p->nspells - 1; i++) p->spells[i] = p->spells[i + 1];
    p->nspells--;
}

/* Rulebook: over your Craft limit, the surplus goes straight to the
 * discard pile and cannot be cast. */
static void spell_enforce(Player *p)
{
    int guard = 0;

    while (p->nspells > spell_limit(p) && guard++ < MAX_SPELLS + 1) {
        int slot = p->ai ? p->nspells - 1
                         : ui_pick_spell(p, "  TOO MANY SPELLS -- give one up", -1);
        if (slot < 0) slot = p->nspells - 1;
        glog("%s cannot hold so much magic.", p->name);
        spell_discard(p, slot);
    }
}

static void gain_spell(Player *p)
{
    int si;

    if (spell_limit(p) == 0) {
        glog("%s has not the Craft to hold a Spell.", p->name);
        return;
    }
    si = sdeck_draw();
    if (si < 0) { glog("There are no Spells left to learn."); return; }
    if (p->nspells < MAX_SPELLS) p->spells[p->nspells++] = si;
    glog("%s learns %s -- %s.", p->name, spell_proto[si].name, spell_proto[si].text);
    spell_enforce(p);
}

/* Rulebook: until somebody reaches the Crown, a killed player draws a new
 * character at random and carries on.  After that, dead is dead -- which
 * is what makes the Crown the only way to win an early game. */
static void respawn(Player *p)
{
    int t, tries = 0;

    do { t = rand() % char_count; } while (char_taken[t] && ++tries < 60);
    deal_character(p, t, rand() % OUTER_N);
    glog("*** A new hero, %s the %s, takes up the quest. ***", p->name, p->cls);
}

/* Returns 1 if this killed them.  Callers need to know, because a
 * respawn puts them back on their feet somewhere else entirely. */
static int lose_life(Player *p, int n)
{
    int sid;

    if (p->preserve && n > 0) {
        p->preserve = 0;
        glog("Preservation shields %s -- no life is lost.", p->name);
        return 0;
    }
    p->lives -= n;
    if (p->lives > 0) return 0;

    p->lives = 0;
    p->alive = 0;
    glog("*** %s the %s is dead. ***", p->name, p->cls);

    /* whatever they were carrying stays where they fell */
    sid = space_id(p->region, p->idx);
    if (p->gold > 0) {
        res_gold[sid] += p->gold;
        glog("%d Gold spills across the %s.", p->gold, space_at(p->region, p->idx)->name);
        p->gold = 0;
    }
    if (p->talisman) {
        res_tal[sid] = 1;
        p->talisman  = 0;
        glog("Their TALISMAN lies there for the taking.");
    }
    if (p->nitems > 0) {
        glog("Their belongings scatter across the ground.");
        while (p->nitems > 0) {
            if (!res_add(sid, p->carried[0]))
                glog("There is no room here; the %s is lost.",
                     deck_proto[p->carried[0]].name);
            item_drop(p, 0);
        }
    }

    if (p == &players[cur_player]) turn_over = 1;
    /* Chaos Bloodbath: "any player whose Character is killed immediately
     * loses the game."  Short, and very, very bloody. */
    if (chaos_on)
        glog("*** %s is out of the game for good. ***", p->name);
    else if (!crown_reached) respawn(p);   /* else they stay out anyway */
    return 1;
}

/* Rulebook: Strength trophies buy Strength and Craft trophies buy Craft.
 * The two piles are separate and cannot be traded against each other. */
static void gain_trophies(Player *p, int ci)
{
    const Card *c    = &deck_proto[ci];
    int        *pool = c->craftfight ? &p->troph_craft : &p->troph_str;
    int        *stat = c->craftfight ? &p->base_craft  : &p->base_str;
    const char *what = c->craftfight ? "Craft"         : "Strength";

    if (p->nkills < MAX_KILLS) p->kills[p->nkills++] = ci;
    *pool += c->power;
    while (*pool >= 7) {
        *pool -= 7;
        (*stat)++;
        glog("%s turns in %s trophies: +1 %s (now %d).", p->name, what, what, *stat);
    }
}

/* ------------------------------------------------------------- magic */

static void spell_remove(Player *p, int slot)
{
    int i;
    for (i = slot; i < p->nspells - 1; i++) p->spells[i] = p->spells[i + 1];
    if (p->nspells > 0) p->nspells--;
}

/* The rival worth hexing: whoever is furthest along. */
static Player *lead_rival(Player *p)
{
    Player *best = NULL;
    int i;

    for (i = 0; i < nplayers; i++) {
        Player *q = &players[i];
        if (q == p || !q->alive || q->region != p->region) continue;
        if (!best) { best = q; continue; }
        if (q->talisman != best->talisman) {          /* a carrier always wins */
            if (q->talisman) best = q;
            continue;
        }
        if (eff_str(q) > eff_str(best)) best = q;
    }
    return best;
}

static Player *rival_here(Player *p)
{
    int i;
    for (i = 0; i < nplayers; i++) {
        Player *q = &players[i];
        if (q != p && q->alive && q->region == p->region && q->idx == p->idx) return q;
    }
    return NULL;
}

/* How far apart two spaces are on a ring, whichever way round is shorter. */
static int dist_either(int a, int b, int len)
{
    int f = ((a - b) % len + len) % len;
    int g = ((b - a) % len + len) % len;
    return f < g ? f : g;
}

static void become_toad(Player *p);

static int steal_kind(Player *win, Player *lose, CardType type)
{
    /* Take the best one, not whichever happens to sit in the first slot.
     * A Thief who lifts the Broken Helmet and leaves the Power Glove is not
     * much of a Thief. */
    int i = item_best_of(lose, type);
    {
        int ci;
        if (i < 0) return 0;
        ci = lose->carried[i];
        item_drop(lose, i);
        spell_enforce(lose);            /* less Craft may mean fewer Spells */
        enforce_limit(lose);            /* and losing a Mule shrinks the pack */
        if (!item_give(win, ci)) { res_add(space_id(win->region, win->idx), ci); return 0; }
        glog("%s spirits the %s away from %s.", win->name, deck_proto[ci].name, lose->name);
        enforce_limit(win);
        return 1;
    }
}

/* Cast the Spell in `slot`.  Battle-only Spells are handled in battle(). */
static void cast_spell(Player *p, int slot)
{
    const Spell *sp = &spell_proto[p->spells[slot]];
    int sid = space_id(p->region, p->idx);
    Player *q;
    int i, r;
    /* res_remove() shortens the residents, so "the loop ran to the end"
     * stops meaning "found nothing" the moment the spell succeeds on the
     * last card -- which made every successful Destroy Magic report itself
     * as a failure.  Track it explicitly. */
    int found;

    glog("%s casts %s!", p->name, sp->name);
    spell_remove(p, slot);
    if (p->casts_left > 0) p->casts_left--;

    switch (sp->kind) {
    case SK_HEAL:
        if (p->lives < eff_maxlives(p)) { p->lives++; glog("  %s is mended. Lives %d/%d.", p->name, p->lives, eff_maxlives(p)); }
        else glog("  %s is already whole.", p->name);
        break;
    case SK_DIVINATION:
        p->base_maxfate++; p->fate++;
        glog("  The future opens. +1 Fate (%d).", p->fate);
        break;
    case SK_HEX:
        if ((q = lead_rival(p))) { glog("  %s is struck!", q->name); lose_life(q, 1); }
        else glog("  ...but no rival is near.");
        break;
    case SK_IMMOBILITY:
        if ((q = lead_rival(p))) { q->miss++; glog("  %s is rooted to the spot.", q->name); }
        else glog("  ...but no rival is near.");
        break;
    case SK_COUNTER:
        for (i = 0; i < nplayers; i++) {
            q = &players[i];
            if (q != p && q->alive && q->nspells > 0) {
                glog("  %s's magic unravels.", q->name);
                spell_discard(q, q->nspells - 1);
                break;
            }
        }
        break;
    case SK_TELEPORT: {
        int dest = (p->region == REG_OUTER)  ? SENTINEL_IDX
                 : (p->region == REG_MIDDLE) ? PORTAL_IDX
                 : (p->region == REG_INNER)  ? GATE_IDX : p->idx;
        p->idx = dest;
        glog("  %s reappears at %s.", p->name, space_at(p->region, p->idx)->name);
        break;
    }
    case SK_WARP:
        extra_turn = 1;
        glog("  Time folds -- %s will act again at once.", p->name);
        break;
    case SK_ACQUIRE:
        if ((q = rival_here(p)) && steal_kind(p, q, C_OBJECT)) enforce_limit(p);
        else glog("  ...but there is nothing here to take.");
        break;
    case SK_MESMERISE:
        if ((q = rival_here(p)) && steal_kind(p, q, C_FOLLOWER)) ;
        else glog("  ...but no follower heeds the call.");
        break;

    /* The Toad card: "For 3 Turns while you are a Toad you have MOVE: 1
     * Space per Turn, LIVES: retain your Character's Lives", at Strength 1
     * and Craft 1 -- and rule 19:2 takes everything else off you.  This
     * spell was coded as Divination and quietly handed out Fate instead. */
    case SK_TOAD:
        if ((q = rival_here(p))) {
            glog("  %s is a slimy little toad for three Turns!", q->name);
            become_toad(q);
        } else {
            glog("  ...but there is no one here to curse.");
        }
        break;
    case SK_ALCHEMY: {
        int slot2 = item_worst(p);
        if (slot2 >= 0) {
            glog("  The %s becomes 3 Gold.", deck_proto[p->carried[slot2]].name);
            item_drop(p, slot2);
            spell_enforce(p);
            p->gold += 3;
        } else glog("  ...but there is nothing to transmute.");
        break;
    }
    case SK_DESTRUCTION:
        found = 0;
        for (i = 0; i < res_n[sid]; i++) {
            const Card *c2 = &deck_proto[res_card[sid][i]];
            if (c2->type == C_ENEMY || c2->type == C_SPIRIT) {
                glog("  The %s is blasted from the world.", c2->name);
                res_remove(sid, i);
                found = 1;
                break;
            }
        }
        if (!found) glog("  ...but nothing here is alive to destroy.");
        break;
    case SK_DESTROYMAGIC:
        found = 0;
        for (i = 0; i < res_n[sid]; i++) {
            const Card *c2 = &deck_proto[res_card[sid][i]];
            if (c2->type == C_PLACE || c2->type == C_STRANGER) {
                glog("  The %s fades away.", c2->name);
                res_remove(sid, i);
                found = 1;
                break;
            }
        }
        if (!found) glog("  ...but there is no magic here to undo.");
        break;
    case SK_RANDOM:
        r = roll();
        ui_dice_one("Random", r, -1);
        if (r >= 5)      { p->base_str++;   glog("  Raw power! +1 Strength (%d).", eff_str(p)); }
        else if (r >= 3) { p->gold += 3;    glog("  Gold from nowhere. +3 Gold (%d).", p->gold); }
        else             { glog("  It rebounds. -1 Life."); lose_life(p, 1); }
        break;
    default:
        break;                              /* battle Spells: see battle() */
    }
    step(p);
}

static void cast_phase(Player *p)
{
    while (p->nspells > 0 && p->casts_left > 0 && p->alive && !quit_flag) {
        int slot, i, any = 0;

        for (i = 0; i < p->nspells; i++)
            if (!SPELL_IN_BATTLE(spell_proto[p->spells[i]].kind)) any = 1;
        if (!any) break;

        if (p->ai) slot = ai_spell_choice(p, 0);
        else {
            ui_draw();
            if (ui_prompt("Cast a Spell before you move?   [c] cast   [n] no", "cn") == 'n') break;
            slot = ui_pick_spell(p, "  CAST WHICH SPELL?", 0);
        }
        if (slot < 0) break;
        cast_spell(p, slot);
    }
}

/* -------------------------------------------------------------- combat */

/* returns 1 = won, 0 = lost, -1 = standoff */
/* Rulebook: Armour prevents the loss of a life when you are defeated. */
static int armour_saves(Player *p)
{
    int r;

    if (!has_armour(p)) return 0;
    r = roll();
    ui_dice_one("Armour", r, -1);
    if (r >= 5) { glog("%s's armour turns the blow (roll %d).", p->name, r); return 1; }
    glog("%s's armour does not hold (roll %d).", p->name, r);
    return 0;
}

/* "At the start of any combat, you can declare that your Henchman is going
 * to fight on your behalf.  Henchmen fight using the base values printed on
 * their cards for Strength and Craft.  They gain no benefit from any
 * Objects, Followers or extra Strength and Craft that your Character may
 * have acquired." */
static int hench_ready(const Player *p)
{
    return henchmen_on && p->hench.ct >= 0 && p->hench.lives > 0;
}

static int hench_stat(const Player *p, int craftfight)
{
    const CharTemplate *h = &char_tbl[p->hench.ct];
    return craftfight ? h->craft : h->str;
}

/* Would you rather he took this one?  Only when he is likelier to win it
 * than you are -- his Lives are a resource, not a shield to hide behind. */
static int send_hench(Player *p, int craftfight)
{
    int mine, his;

    if (!hench_ready(p)) return 0;
    mine = craftfight ? eff_craft(p) : eff_str(p);
    his  = hench_stat(p, craftfight);
    if (p->ai) return his > mine || (his == mine && p->lives <= 1);
    if (his <= mine && p->lives > 1) return 0;      /* do not even ask */
    {
        char m[160];
        snprintf(m, sizeof m,
                 "Send %s to fight for you?  (his %s is %d, yours %d)   [y] yes   [n] no",
                 char_tbl[p->hench.ct].cls, craftfight ? "Craft" : "Strength",
                 his, mine);
        ui_draw();
        return ui_prompt(m, "yn") == 'y';
    }
}

static int battle(Player *p, const char *foe, int power, int craftfight)
{
    int mine = craftfight ? eff_craft(p) : eff_str(p);
    int d1, d2, score, fscore;
    int nullify = 0;
    int by_hench = send_hench(p, craftfight);

    if (by_hench) {
        mine = hench_stat(p, craftfight);
        glog("%s sends %s to fight the %s. (%s %d, and nothing else)",
             p->name, char_tbl[p->hench.ct].cls, foe,
             craftfight ? "Craft" : "Str", mine);
    }

    /* Card: "You may add your Craft to your Strength in Combat." */
    if (!craftfight && !by_hench && has_ab(p, AB_CRAFT_TO_STR)) {
        mine += eff_craft(p);
        glog("%s fights with hands and mind alike. (+%d)", p->name, eff_craft(p));
    }

    /* Rulebook: Spells are cast before the attack roll is made. */
    while (p->nspells > 0 && p->casts_left > 0) {
        int slot, i, any = 0;

        for (i = 0; i < p->nspells; i++)
            if (SPELL_IN_BATTLE(spell_proto[p->spells[i]].kind)) any = 1;
        if (!any) break;

        if (p->ai) slot = ai_spell_choice(p, 1);
        else {
            ui_draw();
            if (ui_prompt("Cast a Spell before the dice?   [c] cast   [n] no", "cn") == 'n') break;
            slot = ui_pick_spell(p, "  CAST WHICH SPELL?", 1);
        }
        if (slot < 0) break;

        {
            SpellKind k = spell_proto[p->spells[slot]].kind;
            glog("%s casts %s!", p->name, spell_proto[p->spells[slot]].name);
            spell_remove(p, slot);
            if (p->casts_left > 0) p->casts_left--;
            if (k == SK_INVISIBLE) {
                glog("  %s melts into the air. There is no battle.", p->name);
                step(p);
                return 2;                        /* evaded: no result either way */
            }
            if (k == SK_PSIONIC) {
                mine += eff_craft(p);
                glog("  Mind over matter: attack score is now %d + roll.", mine);
            }
            if (k == SK_PRESERVE) { p->preserve = 1; glog("  A ward settles over %s.", p->name); }
            if (k == SK_NULLIFY)  { nullify = 1;     glog("  The %s's luck is unmade.", foe); }
            step(p);
        }
    }

    d1 = roll();
    /* Card: "You may roll 2 dice in Combat and use the higher one." */
    if (!by_hench && has_ab(p, AB_COMBAT_2DICE)) {
        int d1b = roll();
        glog("%s rolls twice: %d and %d.", p->name, d1, d1b);
        if (d1b > d1) d1 = d1b;
    }
    d2 = roll();
    if (nullify) {
        int again = roll();
        glog("Nullify: the %s must roll again (%d -> %d).", foe, d2, again);
        d2 = again;
    }
    score  = mine + d1;
    fscore = power + d2;
    {   /* name whoever is actually swinging, or the log tells a small lie */
        const char *who = by_hench ? char_tbl[p->hench.ct].cls : p->name;
        glog("%s (%s %d + roll %d = %d)  vs  %s (%d + %d = %d)",
             who, craftfight ? "Craft" : "Str", mine, d1, score, foe, power, d2, fscore);
        ui_dice_two(who, d1, score, foe, d2, fscore);
    }
    step(p);

    if (score < fscore && p->fate > 0) {
        int use;
        int deficit = fscore - score;
        if (p->ai) {
            use = ai_use_fate(p, deficit, d1);
        } else {
            ui_draw();
            use = (ui_prompt("Losing. Spend a Fate point to reroll your die?  [y] yes  [n] no", "yn") == 'y');
        }
        if (use) {
            p->fate--;
            d1 = roll();
            score = mine + d1;
            glog("%s burns Fate and rerolls: %d + %d = %d  (vs %d)",
                 p->name, mine, d1, score, fscore);
            ui_dice_two(p->name, d1, score, foe, d2, fscore);
            step(p);
        }
    }

    if (score > fscore) {
        glog("%s defeats the %s!",
             by_hench ? char_tbl[p->hench.ct].cls : p->name, foe);
        return 1;
    }
    turn_over = 1;                 /* rulebook: the turn ends immediately */
    if (score == fscore) {
        glog("Stand-off -- neither lands a blow. %s's turn ends.", p->name);
        return -1;
    }
    if (by_hench) {
        /* "They gain no benefit from any Objects" -- no armour saves him,
         * and "if they are killed, they are out of the game and may not be
         * replaced." */
        glog("The %s wounds %s. The turn ends.", foe, char_tbl[p->hench.ct].cls);
        if (--p->hench.lives <= 0) {
            glog("*** %s's Henchman, the %s, is slain and gone for good. ***",
                 p->name, char_tbl[p->hench.ct].cls);
            p->hench.ct = -1;
        }
        return 0;
    }
    glog("The %s wounds %s. The turn ends.", foe, p->name);
    if (!armour_saves(p)) lose_life(p, 1);
    return 0;
}

/* ---------------------------------------------------------- encounters */

/* Rulebook: over the limit, you choose what to keep and the rest is
 * "immediately placed faceup on the character's space". */
static void enforce_limit(Player *p)
{
    int guard = 0;

    while (item_objects(p) > item_limit(p) && guard++ < MAX_ITEMS) {
        int slot = p->ai ? item_worst(p)
                         : ui_pick_item(p, "  OVER YOUR CARRYING LIMIT -- leave one behind", 1);
        int ci;
        if (slot < 0) break;
        ci = p->carried[slot];
        item_drop(p, slot);
        spell_enforce(p);
        res_add(space_id(p->region, p->idx), ci);
        glog("%s cannot carry it all and leaves %s on the ground.",
             p->name, deck_proto[ci].name);
        step(p);
    }
}

static void take_card(Player *p, int ci)
{
    const Card *c = &deck_proto[ci];

    if (c->talisman) {
        p->talisman = 1;
        glog("*** %s gains a TALISMAN! ***", p->name);
        return;
    }
    p->gold += c->d_gold;
    if (p->gold < 0) p->gold = 0;

    if (c->type == C_OBJECT || c->type == C_FOLLOWER) {
        if (!item_give(p, ci)) {          /* hands full: it stays on the space */
            res_add(space_id(p->region, p->idx), ci);
            glog("%s cannot carry %s and leaves it here.", p->name, c->name);
            return;
        }
        glog("%s gains %s%s%s.", p->name, c->name,
             c->text ? " -- " : "", c->text ? c->text : "");
        enforce_limit(p);
        spell_enforce(p);      /* a cursed Follower can cost you Craft */
        return;
    }
    glog("%s gains %s%s%s.", p->name, c->name,
         c->text ? " -- " : "", c->text ? c->text : "");
}

static int apply_place(Player *p, const Card *c, int depth);
static void enter_crown(Player *p, const char *how);
static int  ui_offer_swap(Player *p, int ci);
static Alignment ui_pick_align(Player *p);
static int  align_choice_matters(Player *p);

/* Rulebook 5.3: "The Horse, the Warhorse and the Horse and Cart may not be
 * taken into the Dungeon; you must discard these cards when you enter." */
static void drop_mounts(Player *p)
{
    int i;
    for (i = 0; i < p->nitems; i++) {
        const char *n = deck_proto[p->carried[i]].name;
        if (strcmp(n, "Horse") && strcmp(n, "Warhorse") && strcmp(n, "Horse and Cart"))
            continue;
        glog("  the %s will not go below; %s leaves it behind.", n, p->name);
        item_drop(p, i);
        i--;
    }
}

/* Where a character climbing out of the Dungeon comes up: any Doorway on
 * the main board (8.2).  -1 if every one of them has gone. */
static int first_doorway(int *region, int *idx)
{
    int g, i, k;
    /* Every Region of the main board, not just the Outer: a Doorway is an
     * ordinary Place card and is left wherever it was drawn, which may be
     * the Middle or Inner Region.  Searching only the Outer ring while the
     * two-Doorway cap counted them all meant two Doorways landing inland
     * quietly closed rule 8.2's way out of the Dungeon. */
    for (g = REG_OUTER; g <= REG_INNER; g++)
        for (i = 0; i < ring_len(g); i++) {
            int sid = space_id(g, i);
            for (k = 0; k < res_n[sid]; k++)
                if (deck_proto[res_card[sid][k]].place == PLACE_DOORWAY) {
                    *region = g; *idx = i; return 1;
                }
        }
    return 0;
}

/* The Donjon: "You must either Bribe, Escape or be Judged."  Bribing costs
 * 2G for yourself and 1G for each Follower; escape is a roll of 1; being
 * judged is its own table.  Either way the Warrant is spent by the end. */
static void donjon_turn(Player *p)
{
    int r2;

    if (p->gold >= 2) {
        p->gold -= 2;
        p->warrant = 0;
        p->region  = REG_CITY;
        p->idx     = 0;
        glog("%s bribes the gaoler with 2 Gold and walks out to the Gate.", p->name);
        step(p);
        return;
    }
    r2 = roll();
    ui_dice_one("ESCAPE", r2, -1);
    if (r2 == 1) {
        p->warrant = 0;
        p->region  = REG_CITY;
        p->idx     = 0;
        glog("%s slips the Donjon and reaches the Gate.", p->name);
        step(p);
        return;
    }
    r2 = roll();
    ui_dice_one("JUDGED", r2, -1);
    if (r2 == 1)      { glog("Beaten and jailed."); lose_life(p, 1); }
    else if (r2 == 2) glog("Jailed. Only a bribe or an escape will serve now.");
    else if (r2 == 3) { if (p->gold >= 4) { p->gold -= 4; p->warrant = 0;
                                            p->region = REG_CITY; p->idx = 0;
                                            glog("Fined 4 Gold, and released."); }
                        else glog("Fined 4 Gold, which %s cannot pay.", p->name); }
    else if (r2 == 4) { if (p->gold >= 2) { p->gold -= 2; p->warrant = 0;
                                            p->region = REG_CITY; p->idx = 0;
                                            glog("Fined 2 Gold, and released."); }
                        else { p->warrant = 0; p->region = REG_CITY; p->idx = 0;
                               glog("%s cannot pay, and is thrown out at the Gate.", p->name); } }
    else if (r2 == 5) { p->warrant = 0; p->region = REG_CITY; p->idx = 0;
                        glog("Pardoned.  %s may leave.", p->name); }
    else              { p->warrant = 0; p->gold++; p->region = REG_CITY; p->idx = 0;
                        glog("Wrongful arrest: %s is paid 1 Gold in compensation.", p->name); }
    step(p);
}

/* The City is where the Gold is, and Gold is what buys the road to the
 * Crown.  She goes in poor and comes out when she can afford to. */
static int ai_wants_city(const Player *p)
{
    /* A Warrant is only ever cleared by the Watch -- either she beats them
     * and tries again, or she loses and the Donjon settles it.  Loitering
     * inside keeps it forever, so a warranted character makes for the Gate. */
    if (p->warrant) return 0;
    return p->gold < 6 && !p->talisman;
}

/* Whether the Dungeon is worth it is a question about distance, and the
 * distance table answers it properly: without a Talisman the Portal of Power
 * is shut, so every space on the surface returns -1 -- there is no road to
 * the Crown at all -- and the Dungeon is the only way to win.  Carrying a
 * Talisman, the surface is 6-8 turns out and the Dungeon 10, so she walks.
 * The old test guessed at this with a Strength threshold. */
static int ai_wants_dungeon(const Player *p)
{
    int here = turns_to_crown(p->region, p->idx, p->talisman);
    int down = turns_to_crown(REG_DUNGEON, 0, p->talisman);

    if (down < 0) return 0;                    /* it leads nowhere either */
    if (p->lives < 2) return 0;                /* too thin to survive it  */
    if (here < 0) return 1;                    /* no other road exists    */
    return down < here;                        /* genuinely the shorter   */
}

/* Once she is down there the question is asked from where she stands, not
 * from the door: half way to the Treasure Chamber, turning back is waste. */
static int ai_stays_below(const Player *p)
{
    int here = turns_to_crown(p->region, p->idx, p->talisman);
    int up   = turns_to_crown(REG_OUTER, 0, p->talisman);

    if (p->lives < 2) return 0;
    if (here < 0) return 0;
    return up < 0 || here <= up;
}

/* The table printed in the Treasure Chamber:
 *   1 Castle  2 Temple  3 Warlock's Cave  4 Portal of Power
 *   5 Plain of Peril  6 Crown of Command
 * "Add 1 to the die roll for each character on the Crown of Command space,
 * counting scores over 6 as 6."  Two of those names are not on this board:
 * it has no Castle and its cave is plain Cave, so those two are mapped to
 * the City and the Cave.  The other four are exact. */
static void leave_treasure(Player *p)
{
    static const char *table[6] = {
        "City", "Temple", "Cave", "PORTAL", "PlainPeril", "CROWN"
    };
    int r2 = roll(), i, on_crown = 0, region, idx;

    for (i = 0; i < nplayers; i++)
        if (players[i].alive && players[i].region == REG_CROWN) on_crown++;
    r2 += on_crown;
    if (r2 > 6) r2 = 6;
    ui_dice_one("WAY OUT", r2, -1);

    if (space_by_name(table[r2 - 1], &region, &idx)) {
        glog(">>> %s comes up out of the Dungeon at the %s. <<<",
             p->name, space_at(region, idx)->name);
        /* The Crown is not a space you may simply be assigned to: reaching
         * it is what stops the dead coming back, and that lives in
         * enter_crown().  Setting region directly here left crown_reached
         * false, so respawns never ended and the game could not finish. */
        if (region == REG_CROWN) { enter_crown(p, "rises from the Dungeon"); step(p); return; }
        p->region = region;
        p->idx    = idx;
    } else {
        p->region = REG_OUTER;
        p->idx    = 0;
        glog(">>> %s comes up somewhere near the Village. <<<", p->name);
    }
    p->fleeing = 0;
    step(p);
}

/* How many Doorways stand open, and where the first one is. */
static int doorways_out(void)
{
    int sid, i, n = 0;
    for (sid = 0; sid < NSPACES; sid++)
        for (i = 0; i < res_n[sid]; i++)
            if (deck_proto[res_card[sid][i]].place == PLACE_DOORWAY) n++;
    return n;
}

static int best_stat_of(const Player *p)
{ int a = eff_str(p), b = eff_craft(p); return a > b ? a : b; }

/* The slot a named card sits in, or -1. */
static int item_slot(const Player *p, const char *name)
{
    int i;
    for (i = 0; i < p->nitems; i++)
        if (!strcmp(deck_proto[p->carried[i]].name, name)) return i;
    return -1;
}

/* A card's index in the prototype table, by name. */
static int card_named(const char *name)
{
    int i;
    for (i = 0; deck_proto[i].name; i++)
        if (!strcmp(deck_proto[i].name, name)) return i;
    return -1;
}

static int has_item(const Player *p, const char *name)
{
    int i;

    for (i = 0; i < p->nitems; i++)
        if (!strcmp(deck_proto[p->carried[i]].name, name)) return 1;
    return 0;
}

/* Which of two drawn cards the Prophetess would rather meet: anything is
 * better than an Enemy that outmatches her, and treasure beats a fight. */
static int better_card_for(const Player *p, int a, int b)
{
    int i, best = a, bv = -99;
    int cand[2];
    cand[0] = a; cand[1] = b;
    for (i = 0; i < 2; i++) {
        const Card *c = &deck_proto[cand[i]];
        int v = 0;
        switch (c->type) {
        case C_ENEMY: case C_SPIRIT: {
            int mine = c->craftfight ? eff_craft(p) : eff_str(p);
            v = mine - c->power;             /* a fight you can win is fine */
            break;
        }
        case C_OBJECT: case C_FOLLOWER: v = 5; break;
        default:                        v = 2; break;
        }
        if (v > bv) { bv = v; best = cand[i]; }
    }
    return best;
}

static void resolve_card(Player *p, int ci, int depth)
{
    const Card *c = &deck_proto[ci];

    switch (c->type) {
    case C_ENEMY:
    case C_SPIRIT:
        glog("%s encounters a %s (%s %d)!", p->name, c->name,
             c->craftfight ? "Craft" : "Strength", c->power);
        step(p);
        /* Card: "You automatically destroy any Spirit, without resort to
         * Combat."  The Priest still takes the trophy for it. */
        if (c->type == C_SPIRIT && has_ab(p, AB_DESTROY_SPIRIT)) {
            glog("%s destroys the %s outright; it does not come to blows.",
                 p->name, c->name);
            gain_trophies(p, ci);
            step(p);
            break;
        }
        /* Card: "You may Evade Spirits." */
        if (c->type == C_SPIRIT && has_ab(p, AB_EVADE_SPIRIT)) {
            int slip = p->ai ? (eff_craft(p) < c->power)
                             : (ui_draw(), ui_prompt("Slip past the spirit unfought?  [y]/[n]",
                                                     "yn") == 'y');
            if (slip) {
                glog("%s slips past the %s.", p->name, c->name);
                res_add(space_id(p->region, p->idx), ci);
                step(p);
                break;
            }
        }
        /* Card: "Animals will not attack you." */
        if (has_ab(p, AB_CHARM_ANIMAL) && c->animal) {
            glog("The %s will not touch %s -- it is charmed.", c->name, p->name);
            res_add(space_id(p->region, p->idx), ci);
            step(p);
            break;
        }
        if (battle(p, c->name, c->power, c->craftfight) == 1) {
            gain_trophies(p, ci);
        } else if (p->alive) {
            res_add(space_id(p->region, p->idx), ci);
            glog("The %s holds this ground. It will be here next time.", c->name);
            /* 7.3: "When a character is defeated in Combat or Psychic
             * Combat he or she must move towards the Entrance on the next
             * turn."  9.2 keeps a fleeing character fleeing regardless. */
            if (p->region == REG_DUNGEON) {
                p->dungeon_out = 1;
                glog("  %s is driven back towards the Entrance.", p->name);
            }
        }
        break;
    case C_PLACE:
    case C_STRANGER: {
        int sid = space_id(p->region, p->idx);
        /* Rulebook 4.1: "When there are two Doorways on the board, any
         * subsequent Doorway cards which are drawn are ignored; they are
         * placed on the discard pile and a new card is drawn in their
         * place." */
        if (c->place == PLACE_DOORWAY && doorways_out() >= 2) {
            int again = deck_draw();
            glog("A third Doorway crumbles shut.");
            if (again >= 0) resolve_card(p, again, depth);
            return;
        }
        res_add(sid, ci);
        glog("%s -- %s.", c->name, c->text);
        step(p);
        if (apply_place(p, c, depth) && res_n[sid] > 0 &&
            res_card[sid][res_n[sid] - 1] == ci)
            res_remove(sid, res_n[sid] - 1);       /* used up: discard it */
        return;
    }
    case C_OBJECT:
    case C_FOLLOWER:
    case C_GOLD:
        take_card(p, ci);
        break;
    case C_EVENT:
        glog("%s: %s -- %s", p->name, c->name, c->text ? c->text : "");
        if (!strcmp(c->name, "Book of Spells")) { gain_spell(p); step(p); return; }
        p->base_str   += c->d_str;
        p->base_craft += c->d_craft;
        if (p->base_craft < 0) p->base_craft = 0;
        if (p->base_str   < 0) p->base_str   = 0;
        p->fate += c->d_fate;
        if (c->d_fate > 0) p->base_maxfate += c->d_fate;
        p->gold += c->d_gold;
        if (p->gold < 0) p->gold = 0;   /* rulebook: no gold to lose, no effect */
        if (c->miss) p->miss += c->miss;
        /* rulebook: healing can never take you above your life value */
        if (c->d_life > 0) {
            p->lives += c->d_life;
            clamp_pools(p);
        }
        if (c->d_life < 0) lose_life(p, -c->d_life);
        break;
    }
    step(p);
}

static int find_proto(const char *name)
{
    int i;
    for (i = 0; deck_proto[i].name; i++)
        if (!strcmp(deck_proto[i].name, name)) return i;
    return -1;
}

/* ------------------------------------------------------------- shops */

#define SHOP_WARES     1     /* the Purchase deck */
#define SHOP_MYSTIC    2     /* the Village Mystic: a Spell for a coin */
#define SHOP_DOCTOR    4     /* heal, but only if wounded */
#define SHOP_ALCHEMIST 8     /* turn an Object into gold */
#define SHOP_STABLES  16     /* a Horse and Cart */

static int stock[16];        /* what the Purchase deck has left */

static void stock_init(void)
{
    int i;
    for (i = 0; purchase_proto[i].name && i < (int)(sizeof stock / sizeof stock[0]); i++)
        stock[i] = purchase_proto[i].copies;
}


/* Rulebook: you buy Objects here, not raw Strength -- so everything
 * bought can later be stolen, dropped or left behind like any other kit. */
static void do_shop(Player *p, const char *title, int flags)
{
    for (;;) {
        ShopLine line[24];
        int wi[24];
        const int LMAX = (int)(sizeof line / sizeof line[0]);                    /* line -> purchase_proto index */
        int n = 0, i, c, wares_end;

        if (flags & SHOP_WARES) {
            for (i = 0; purchase_proto[i].name && n < 12; i++) {
                int ci = find_proto(purchase_proto[i].name);
                if (ci < 0) continue;
                line[n].key   = '1' + n;
                line[n].label = purchase_proto[i].name;
                line[n].note  = deck_proto[ci].text;
                line[n].price = purchase_proto[i].price;
                /* An Object you have no room for is not for sale: buying it
                 * would only force you to put it straight back down. */
                line[n].avail = (deck_proto[ci].type == C_OBJECT &&
                                 item_objects(p) >= item_limit(p)) ? 0 : stock[i];
                line[n].proto = ci;
                wi[n] = i;             /* `continue` above can desync n from i */
                n++;
            }
        }
        wares_end = n;
        if (flags & SHOP_STABLES) {
            if (n >= LMAX) goto ready;
            int ci = find_proto("Horse and Cart");
            if (ci >= 0) { line[n].key='c'; line[n].label="Horse and Cart"; line[n].note=deck_proto[ci].text;
                           line[n].price=5; line[n].avail=-1; line[n].proto=ci; n++; }
        }
        if (flags & SHOP_MYSTIC) {
            if (n >= LMAX) goto ready;
            line[n].key='m'; line[n].label="the Mystic"; line[n].note="learn a Spell";
            line[n].price=1; line[n].avail = (spell_limit(p) > p->nspells) ? -1 : 0; line[n].proto=-1; n++;
        }
        if (flags & SHOP_DOCTOR) {
            if (n >= LMAX) goto ready;
            line[n].key='h'; line[n].label="the Doctor"; line[n].note="heal 1 Life";
            line[n].price=1; line[n].avail = (p->lives < eff_maxlives(p)) ? -1 : 0; line[n].proto=-1; n++;
        }
        if (flags & SHOP_ALCHEMIST) {
            if (n >= LMAX) goto ready;
            line[n].key='a'; line[n].label="the Alchemist"; line[n].note="an Object becomes 3 Gold";
            line[n].price=0; line[n].avail = item_objects(p) ? -1 : 0; line[n].proto=-1; n++;
        }

    ready:
        c = p->ai ? ai_shop_choice(p, line, n) : ui_shop(p, title, line, n);
        if (c == 0) break;

        if (c >= '1' && c < '1' + wares_end) {
            i = wi[c - '1'];
            {
                int ci = find_proto(purchase_proto[i].name);
                if (ci < 0 || stock[i] == 0 || p->gold < purchase_proto[i].price) break;
                if (!item_give(p, ci)) { glog("%s has no hands free.", p->name); break; }
                p->gold -= purchase_proto[i].price;
                stock[i]--;
                glog("%s buys a %s for %dg (%d Gold left).",
                     p->name, purchase_proto[i].name, purchase_proto[i].price, p->gold);
            }
        } else if (c == 'c') {
            int ci = find_proto("Horse and Cart");
            if (ci < 0 || p->gold < 5 || !item_give(p, ci)) break;
            p->gold -= 5;
            glog("%s buys a Horse and Cart. %d Object slots now.", p->name, item_limit(p));
        } else if (c == 'm') {
            if (p->gold < 1) break;
            p->gold--;
            glog("%s pays the Mystic a coin.", p->name);
            gain_spell(p);
        } else if (c == 'h') {
            if (p->gold < 1 || p->lives >= eff_maxlives(p)) break;
            p->gold--;
            p->lives++;
            glog("%s pays the Doctor. Lives %d/%d.", p->name, p->lives, eff_maxlives(p));
        } else if (c == 'a') {
            int slot = item_worst(p);
            if (slot < 0) break;
            glog("The Alchemist turns %s's %s into 3 Gold.",
                 p->name, deck_proto[p->carried[slot]].name);
            item_drop(p, slot);
            spell_enforce(p);
            p->gold += 3;
        } else break;

        if (p->ai) break;
        step(p);
    }
}

/* `how` because there are two ways up here now, and saying she passed the
 * Dread Gate when she climbed out of the Treasure Chamber would be a lie
 * printed straight into the log. */
static void enter_crown(Player *p, const char *how)
{
    /* "While you are fighting the Demon Lord, an impenetrable mystic
     * barrier prevents any other players from entering the Valley of
     * Fire."  So the Demon is fought one challenger at a time, in a queue,
     * and there is no contesting the Crown at all while he holds it. */
    if (ending_revealed && ending == END_DEMON) {
        int i;
        for (i = 0; i < nplayers; i++)
            if (players[i].alive && &players[i] != p &&
                players[i].region == REG_CROWN) {
                glog("An impenetrable mystic barrier turns %s back -- %s is "
                     "still fighting.", p->name, players[i].name);
                return;
            }
    }

    /* "The first player to reach the space reveals the card" -- the reveal
     * happens on the crossing, before anyone stands on the Crown at all. */
    if (alt_endings && !ending_revealed) {
        ending_revealed = 1;
        glog("*** %s crosses the Valley of Fire and turns the face-down card...", p->name);
        glog("*** It is %s! ***", ending_name[ending]);
        if (ending == END_VOID) {
            /* "The first player to cross the Bridge of Fire is sucked into a
             * seething mass of darkness and annihilated along with all they
             * carry.  After one player has been destroyed the Horrible Black
             * Void moves to the discard pile and a new End card is placed." */
            {
                /* "(Editor's Note: If the Timescape is being used, a
                 * Character falling through the Horrible Black Void is
                 * transported to the Warp Gate space rather than being
                 * destroyed.)" */
                int wreg, widx;
                if (SET_ON(SET_TIMED) && space_by_name("WARP GATE", &wreg, &widx)) {
                    glog("  The darkness takes %s -- and spits them out at the Warp Gate.",
                         p->name);
                    p->region = wreg;
                    p->idx    = widx;
                } else {
                    glog("  A seething mass of darkness takes %s and everything",
                         p->name);
                    glog("  they carried. They have lost the game!");
                    p->lives   = 0;
                    p->alive   = 0;
                    p->nitems  = 0;
                    p->nspells = 0;
                    p->gold    = 0;
                    p->talisman = 0;
                }
            }
            crown_reached = 1;
            ending_spent |= 1u << END_VOID;
            deal_ending();
            glog("  The Void withdraws to the discard pile. A new card is laid down.");
            return;
        }
        if (ending == END_DEMON)  demon_lives = 4;
    }
    p->region     = REG_CROWN;
    p->idx        = 0;
    if (!crown_reached) {
        crown_reached = 1;
        glog("*** The Crown is reached -- from now on, the dead stay dead. ***");
    }
    glog("*** %s %s and seizes the Crown of Command! ***", p->name, how);
}

static void resolve_space(Player *p, int depth);

/* Rule 19:2 -- "Toads cannot have Objects, Magic Objects, Gold or Followers.
 * Any in the Character's possession must immediately [be placed in] the
 * Space where the transformation occurred."  Being a Toad was three skipped
 * turns with everything still in hand, which is not much of a curse. */
static void become_toad(Player *p)
{
    int sid = space_id(p->region, p->idx);

    p->toad = 3;      /* 19:1 -- "reverts back at the end of their third Turn" */
    if (p->nitems || p->gold) {
        glog("  Everything %s carried falls where they stood.", p->name);
        while (p->nitems > 0) {
            if (!res_add(sid, p->carried[0]))
                glog("  There is no room here; the %s is lost.",
                     deck_proto[p->carried[0]].name);
            item_drop(p, 0);
        }
        if (p->gold) { res_gold[sid] += p->gold; p->gold = 0; }
    }
    spell_enforce(p);
}

/* Rule 2:6: "This limit may only be exceeded by a Character possessing the
 * Wand", and the card says how: "While the Wand is in your possession you
 * will always have at least 1 Spell more than you started the game with
 * (take one immediately each time you drop to your starting number)."  The
 * card was in the deck giving +1 Craft and nothing else. */
static void wand_top_up(Player *p)
{
    if (!p->alive || !has_item(p, "Wand")) return;
    if (p->nspells > p->start_spells)      return;
    glog("The Wand will not let %s go empty-handed.", p->name);
    gain_spell(p);
}

static int  draw_char_page(const int *avail, int navail, int page, char *keys);
static void wand_top_up(Player *p);

/* Rule 7:3 -- "If a Character possesses any Magic Objects not permitted by
 * their new Alignment, those Magic Objects must immediately be placed face up
 * in the Space they occupy."  The Druid card puts it plainly: "if you are
 * carrying the Runesword and you wish to pray at the Chapel, you must drop
 * the Runesword and leave it there." */
static void shed_forbidden(Player *p)
{
    int sid = space_id(p->region, p->idx), i = 0;

    while (i < p->nitems) {
        int ci = p->carried[i];
        if (align_allows(p, ci)) { i++; continue; }
        item_drop(p, i);
        res_add(sid, ci);
        glog("  %s can no longer bear the %s, and leaves it face up here.",
             p->name, deck_proto[ci].name);
    }
    spell_enforce(p);
}

/* Rule 7:1 -- the Alignment Change Card, and 7:2 -- "No Character, including
 * the Druid, may change Alignment more than once in any Turn." */
static int change_align(Player *p, Alignment to, const char *why)
{
    static const char *nm[3] = { "Good", "Neutral", "Evil" };

    if (p->align == to) return 0;
    if (p->realigned) {
        glog("%s has already turned once this Turn.", p->name);
        return 0;
    }
    p->realigned = 1;
    p->align     = to;
    if (to == p->card_align)
        glog("*** %s returns to their true Alignment: %s. (%s) ***",
             p->name, nm[to], why);
    else
        glog("*** %s takes an Alignment Change Card -- now %s. (%s) ***",
             p->name, nm[to], why);
    shed_forbidden(p);
    return 1;
}

/* ---------------------------------------------------- character combat */

/* Prefer robbing whoever is carrying a Talisman. */
static Player *pvp_target(Player *p)
{
    Player *best = NULL;
    int i;

    for (i = 0; i < nplayers; i++) {
        Player *q = &players[i];
        if (q == p || !q->alive) continue;
        if (q->region != p->region || q->idx != p->idx) continue;
        if (!best || (q->talisman && !best->talisman)) best = q;
    }
    return best;
}

/* Rulebook step 5: the winner takes ONE of a life, a gold, or an Object.
 * Kill them and everything they carried is yours; the rest stays put. */
static void claim_reward(Player *win, Player *lose)
{
    int i;
    int can_tal  = lose->talisman && !win->talisman;
    int can_gold = lose->gold > 0;
    int can_obj  = item_objects(lose) > 0 && win->nitems < MAX_ITEMS;
    int c;

    if (win->ai) {
        /* A Talisman is worth taking only while it is still a key to a door
         * they have not yet walked through.  Once the Crown is reached the
         * dead stay dead, and a Life taken is the only thing that is
         * actually progress -- two characters swapping the same Talisman
         * back and forth at the Crown is a game that never ends. */
        int tal_worth = can_tal && !endgame() && lose->region < REG_INNER;

        c = (endgame() || lose->lives <= 1) ? 'l'
          : (tal_worth ? 't'
          : (can_obj ? 'o' : (can_gold ? 'g' : 'l')));
    } else {
        char m[160];
        char keys[8];
        snprintf(keys, sizeof keys, "l%s%s%s",
                 can_obj ? "o" : "", can_tal ? "t" : "", can_gold ? "g" : "");
        snprintf(m, sizeof m, "Claim one:   [l] cost them a life%s%s%s",
                 can_obj  ? "   [o] take an Object" : "",
                 can_tal  ? "   [t] take their TALISMAN" : "",
                 can_gold ? "   [g] take a Gold" : "");
        ui_draw();
        c = ui_prompt(m, keys);
    }

    if (c == 'o' && can_obj) {
        int slot = win->ai ? item_best(lose)
                           : ui_pick_item(lose, "  TAKE ONE OBJECT", 1);
        if (slot >= 0) {
            int ci = lose->carried[slot];
            item_drop(lose, slot);
            spell_enforce(lose);
            enforce_limit(lose);
            item_give(win, ci);
            glog("%s takes the %s from %s.",
                 win->name, deck_proto[ci].name, lose->name);
            enforce_limit(win);
            return;
        }
    }
    if (c == 't' && can_tal) {
        lose->talisman = 0;
        win->talisman  = 1;
        glog("%s takes the TALISMAN from %s!", win->name, lose->name);
        return;
    }
    if (c == 'g' && can_gold) {
        lose->gold--;
        win->gold++;
        glog("%s takes a Gold from %s.", win->name, lose->name);
        return;
    }

    glog("%s wounds %s.", win->name, lose->name);
    if (armour_saves(lose)) return;
    /* Card: "Whenever you defeat a player, if you choose to take one of
     * their Lives, you add it to your own." */
    if (has_ab(win, AB_LIFE_STEAL) && win->lives < eff_maxlives(win)) {
        win->lives++;
        glog("  %s feeds on the wound, and is the stronger for it.", win->name);
    }
    {
        /* Take the location BEFORE the blow: a respawn moves them, and
         * pre-Crown it also puts them back on their feet, so `alive` is
         * no use as the test. */
        int sid  = space_id(lose->region, lose->idx);
        int died = lose_life(lose, 1);
    if (died) {                         /* a kill: take what fell */
        if (res_gold[sid] > 0) {
            win->gold += res_gold[sid];
            glog("%s takes the %d Gold from the body.", win->name, res_gold[sid]);
            res_gold[sid] = 0;
        }
        if (res_tal[sid] && !win->talisman) {
            res_tal[sid]  = 0;
            win->talisman = 1;
            glog("%s takes the TALISMAN from the body.", win->name);
        }
        for (i = 0; i < res_n[sid] && win->nitems < MAX_ITEMS; ) {
            const Card *c2  = &deck_proto[res_card[sid][i]];
            int         obj = (c2->type == C_OBJECT);
            if ((obj || c2->type == C_FOLLOWER) &&
                (!obj || item_objects(win) < item_limit(win))) {
                item_give(win, res_card[sid][i]);
                glog("%s takes the %s from the body.", win->name, c2->name);
                res_remove(sid, i);
                continue;
            }
            i++;                       /* no room, or not takeable: it stays */
        }
    }
    }
}

static void pvp_encounter(Player *att, Player *def, int forced)
{
    int d1, d2, a, b, want;
    int psychic = 0, astat, dstat;
    int a_hench = 0, d_hench = 0;
    Player *win, *lose;

    glog("%s finds %s the %s here%s.", att->name, def->name, def->cls,
         def->talisman ? ", carrying a TALISMAN" : "");
    step(att);

    if (forced) {
        want = 1;                       /* on the Crown the fight is not optional */
    } else if (att->ai) {
        want = ai_attack(att, def);
    } else {
        char m[140];
        snprintf(m, sizeof m, "Attack %s the %s (Strength %d, %d lives)?   [y] yes   [n] no",
                 def->name, def->cls, eff_str(def), def->lives);
        ui_draw();
        want = (ui_prompt(m, "yn") == 'y');
    }
    if (!want) { glog("%s lets them be.", att->name); step(att); return; }

    /* Card: "Instead of attacking a player you land on, you may Enchant
     * them.  On their next Turn they can do nothing." */
    if (has_ab(att, AB_ENCHANT) && !forced && !def->miss) {
        int spell;
        if (att->ai) spell = eff_str(att) <= eff_str(def);   /* when arms would fail */
        else {
            char m[150];
            snprintf(m, sizeof m, "Enchant %s instead of fighting?  they lose their next turn  [y]/[n]",
                     def->name);
            ui_draw();
            spell = (ui_prompt(m, "yn") == 'y');
        }
        if (spell) {
            def->miss = 1;
            glog("%s enchants %s, who will stand idle a turn.", att->name, def->name);
            turn_over = 1;
            step(att);
            return;
        }
    }

    /* Cards: the Wizard, Sorceress and Ghoul "may choose to make the Combat
     * Psychic" when attacking -- never when attacked. */
    /* The Chameleon Suit: "With this you have a 50% chance..." -- to blend
     * into the surroundings and have the attack find nothing there. */
    if (has_item(def, "Chameleon Suit") && roll() >= 4) {
        glog("%s loses %s against the background -- the attack finds nothing.",
             att->name, def->name);
        turn_over = 1;
        step(att);
        return;
    }

    psychic = has_ab(att, AB_PSYCHIC_ATTACK)
              && (att->ai ? eff_craft(att) > eff_str(att)
                          : (ui_draw(), ui_prompt("Make this a battle of minds?  [y]/[n]", "yn") == 'y'));
    astat = psychic ? eff_craft(att) : eff_str(att);
    dstat = psychic ? eff_craft(def) : eff_str(def);

    /* "At the start of any combat" -- a duel is a combat like any other, and
     * either side may put their Henchman forward. */
    if (send_hench(att, psychic)) {
        a_hench = 1;
        astat   = hench_stat(att, psychic);
        glog("  %s sends %s forward. (%s %d, and nothing else)", att->name,
             char_tbl[att->hench.ct].cls, psychic ? "Craft" : "Str", astat);
    }
    if (send_hench(def, psychic)) {
        d_hench = 1;
        dstat   = hench_stat(def, psychic);
        glog("  %s answers with %s. (%s %d, and nothing else)", def->name,
             char_tbl[def->hench.ct].cls, psychic ? "Craft" : "Str", dstat);
    }
    if (psychic) glog("  %s turns it into a battle of minds.", att->name);

    d1 = roll(); d2 = roll();
    /* Card: "Combat takes place as normal except that your victim may not
     * roll a die to add to their Strength." */
    if (has_ab(att, AB_ASSASSINATE)) {
        glog("  %s strikes from the dark: %s adds no die.", att->name, def->name);
        d2 = 0;
    }
    a  = astat + d1;
    b  = dstat + d2;
    glog("%s (%s %d + %d = %d)  vs  %s (%s %d + %d = %d)",
         att->name, psychic ? "Craft" : "Str", astat, d1, a,
         def->name, psychic ? "Craft" : "Str", dstat, d2, b);
    ui_dice_two(att->name, d1, a, def->name, d2, b);
    step(att);

    /* The attacker must decide about fate first, then the defender. */
    if (a <= b && att->fate > 0) {
        want = att->ai ? ai_use_fate(att, b - a + 1, d1)
                       : (ui_draw(), ui_prompt("Spend Fate to reroll your attack?   [y] yes   [n] no", "yn") == 'y');
        if (want) {
            att->fate--; d1 = roll(); a = astat + d1;
            glog("%s burns Fate: %d + %d = %d", att->name, astat, d1, a);
            ui_dice_two(att->name, d1, a, def->name, d2, b);
            step(att);
        }
    }
    if (b <= a && def->fate > 0 && !has_ab(att, AB_ASSASSINATE)) {
        want = def->ai ? ai_use_fate(def, a - b + 1, d2)
                       : (ui_draw(), ui_prompt("Spend Fate to reroll your defence?   [y] yes   [n] no", "yn") == 'y');
        if (want) {
            def->fate--; d2 = roll(); b = dstat + d2;
            glog("%s burns Fate: %d + %d = %d", def->name, dstat, d2, b);
            ui_dice_two(att->name, d1, a, def->name, d2, b);
            step(att);
        }
    }

    turn_over = 1;                       /* either way the turn ends here */
    if (a == b) { glog("Stand-off. Neither gives ground."); step(att); return; }

    win  = (a > b) ? att : def;
    lose = (a > b) ? def : att;
    glog("%s overcomes %s.", win->name, lose->name);

    /* If the loser put their Henchman forward, it is the Henchman who pays.
     * "If they are killed, they are out of the game and may not be replaced"
     * -- and the loser keeps everything else, because the Character was
     * never the one beaten. */
    if ((lose == att && a_hench) || (lose == def && d_hench)) {
        glog("  %s's Henchman, the %s, takes the blow.",
             lose->name, char_tbl[lose->hench.ct].cls);
        if (--lose->hench.lives <= 0) {
            glog("*** %s's Henchman, the %s, is slain and gone for good. ***",
                 lose->name, char_tbl[lose->hench.ct].cls);
            lose->hench.ct = -1;
        }
        step(att);
        return;
    }
    claim_reward(win, lose);
    step(att);
}

/* Rule 2.3: "Characters never encounter other Characters in the Timescape;
 * if you land on the same space as another Character, [ignore] the space
 * rather than encountering them."  Each reality is its own world, and two
 * pieces on one space are not really in the same place at all. */
/* In the Inner Region characters may only meet on the Plain of Peril. */
/* Rule 13:2, as amended by Talisman the Adventure: "A Character must choose
 * to encounter either one Character of their choice who is in that space, or
 * in the space itself."  Two things follow that the old code got wrong: the
 * choice of *which* Character was never offered, and landing on an occupied
 * space used to resolve the fight *and* the space, when the rule says one or
 * the other.
 *
 * Returns the chosen Character, or NULL to take the Space instead. */
static Player *choose_encounter(Player *p)
{
    int who[MAX_PLAYERS], n = 0, i;
    char keys[MAX_PLAYERS + 2], line[200];

    for (i = 0; i < nplayers; i++) {
        Player *q = &players[i];
        if (q == p || !q->alive) continue;
        if (q->region != p->region || q->idx != p->idx) continue;
        who[n++] = i;
    }
    if (!n) return NULL;

    if (p->ai) {
        /* Fight the one worth fighting; otherwise take what the Space
         * offers, which is what a person does when the odds are bad. */
        Player *best = NULL;
        for (i = 0; i < n; i++) {
            Player *q = &players[who[i]];
            if (!ai_attack(p, q) && !has_ab(p, AB_STEAL)) continue;
            if (!best || lead_score_of(q) > lead_score_of(best)) best = q;
        }
        return best;
    }

    line[0] = 0;
    for (i = 0; i < n; i++) {
        keys[i] = (char)('1' + i);
        snprintf(line + strlen(line), sizeof line - strlen(line),
                 "  [%d] %s the %s", i + 1, players[who[i]].name,
                 players[who[i]].cls);
    }
    keys[n] = 's'; keys[n + 1] = 0;
    snprintf(line + strlen(line), sizeof line - strlen(line),
             "   [s] the Space itself");
    ui_draw();
    glog("You may encounter one Character here, or the Space -- not both.");
    {
        int c = ui_prompt(line, keys);
        if (c == 's') return NULL;
        return &players[who[c - '1']];
    }
}

/* Returns 1 if a Character was encountered, so the Space is not. */
static int pvp_phase(Player *p)
{
    Player *q;

    if (p->region == REG_TIME) return 0;    /* no one ever meets out there */
    if (p->region == REG_INNER &&
        strcmp(space_at(p->region, p->idx)->name, "PlainPeril") != 0)
        return 0;
    if ((q = choose_encounter(p)) == NULL) return 0;

    /* The Witch Doctor's card: "When you encounter another character, you can
     * choose to throw a Curse on them rather than fight them (you cannot do
     * both)."  The rulebook's own FAQ says what a Curse then does: "The
     * cursed Character must move by the fastest possible route to the
     * chapel.  Otherwise, all normal rules apply." */
    if (has_ab(p, AB_EVIL_EYE) && !q->cursed) {
        int curse;
        if (p->ai) curse = !ai_attack(p, q) || q->talisman;
        else {
            char m[140];
            snprintf(m, sizeof m, "Throw the Evil Eye on %s instead of fighting?"
                     "   [y] curse   [n] fight", q->name);
            ui_draw();
            curse = (ui_prompt(m, "yn") == 'y');
        }
        if (curse) {
            q->cursed = 1;
            glog("%s throws the Evil Eye on %s.", p->name, q->name);
            glog("  %s must make for the %s before the quest goes on.",
                 q->name, q->align == AL_EVIL ? "Ruins" : "Chapel");
            turn_over = 1;
            step(p);
            return 1;
        }
    }

    /* The Thief takes what he wants without drawing a blade. */
    if (has_ab(p, AB_STEAL)) {
        int take;
        if (p->ai) take = (q->talisman && !p->talisman) || item_objects(q) > 0 || q->gold > 0;
        else {
            char m[140];
            snprintf(m, sizeof m, "Rob %s the %s without a fight?   [y] yes   [n] fight instead",
                     q->name, q->cls);
            ui_draw();
            take = (ui_prompt(m, "yn") == 'y');
        }
        if (take) {
            glog("%s picks %s's pockets.", p->name, q->name);
            if (q->talisman && !p->talisman) {
                q->talisman = 0; p->talisman = 1;
                glog("  ...and lifts the TALISMAN!");
            } else if (item_objects(q) > 0 && item_objects(p) < item_limit(p)) {
                steal_kind(p, q, C_OBJECT);
            } else if (q->gold > 0) {
                int g = q->gold < 2 ? q->gold : 2;
                q->gold -= g; p->gold += g;
                glog("  ...and lifts %d Gold.", g);
            } else {
                glog("  ...but their pockets are empty.");
            }
            turn_over = 1;
            step(p);
            return 1;
        }
    }
    pvp_encounter(p, q, 0);
    return 1;
}

/* Returns 1 if the card is used up and should leave the board. */
static int apply_place(Player *p, const Card *c, int depth)
{
    int len, dest, r;

    switch (c->place) {
    /* Rulebook 5.1-5.2: "To enter the Dungeon you must first land on a
     * Doorway.  On your next turn you may move onto the Entrance space...
     * You must stop there."  The Doorway stays: 4.2 keeps it safe even
     * from Destruction and the Earthquake. */
    case PLACE_DOORWAY:
        p->at_doorway = 1;
        glog("%s finds a way down.  The Dungeon lies open from here.", p->name);
        return 0;
    case PLACE_SHOP:
        do_shop(p, "  THE MARKET", SHOP_WARES);
        break;

    case PLACE_SPELL:
        gain_spell(p);
        step(p);
        break;

    case PLACE_FOUNTAIN:
        /* one-shot: a Place that stayed would be an endless Craft tap */
        p->base_craft++;
        glog("%s draws deep wisdom here. +1 Craft (%d).", p->name, eff_craft(p));
        step(p);
        return 1;

    case PLACE_FAIRY:
        p->base_maxfate++;
        p->fate++;
        glog("%s is granted a boon. +1 Fate (%d).", p->name, p->fate);
        step(p);
        return 1;

    case PLACE_POOL:
        if (p->lives < eff_maxlives(p)) {
            p->lives++;
            glog("The waters mend %s. Lives %d/%d.", p->name, p->lives, eff_maxlives(p));
        } else {
            glog("%s rests a while, already whole.", p->name);
        }
        step(p);
        break;

    case PLACE_MAZE:
        p->miss += 1;
        glog("%s loses the way and misses a turn.", p->name);
        step(p);
        break;

    case PLACE_MARSH:
        glog("The marsh drags at %s. -1 Life.", p->name);
        lose_life(p, 1);
        step(p);
        break;

    case PLACE_SPIRIT:
        glog("The %s bars the way (Craft %d)!", c->name, c->power);
        step(p);
        if (battle(p, c->name, c->power, 1) == 1) {
            p->base_craft++;
            glog("%s prevails. +1 Craft (%d).", p->name, eff_craft(p));
            step(p);
            return 1;
        }
        break;

    case PLACE_WITCH:
        r = roll();
        ui_dice_one("Witch", r, -1);
        glog("The witch weighs %s (roll %d)...", p->name, r);
        step(p);
        if (r >= 5)      { p->base_craft++; glog("...and favours them. +1 Craft (%d).", eff_craft(p)); }
        else if (r >= 3) { glog("...and lets them pass."); }
        else             { glog("...and curses them. -1 Life."); lose_life(p, 1); }
        step(p);
        break;

    case PLACE_SHRINE:
        if (p->lives < eff_maxlives(p)) {
            p->lives++;
            glog("The shrine mends %s. Lives %d/%d.", p->name, p->lives, eff_maxlives(p));
        } else {
            glog("%s leaves an offering at the shrine.", p->name);
        }
        step(p);
        break;

    case PLACE_DEN: {
        r = roll();
        ui_dice_one("Den", r, -1);
        glog("The den stirs (roll %d)...", r);
        step(p);
        if (r >= 3) {
            int m = random_monster();
            res_add(space_id(p->region, p->idx), m);
            glog("A %s crawls out of the den!", deck_proto[m].name);
        } else {
            glog("...nothing comes out this time.");
        }
        step(p);
        break;
    }

    case PLACE_PORTAL:
        if (depth >= 2) { glog("The portal is spent for now."); step(p); break; }
        len  = ring_len(p->region);
        r    = roll();
        ui_dice_one("Gate", r, -1);
        dest = (p->idx + (r * 3)) % len;
        glog("The Magic Portal seizes %s (roll %d)...", p->name, r);
        step(p);
        p->idx = dest;
        glog("...and sets them down on %s.", space_at(p->region, p->idx)->name);
        step(p);
        resolve_space(p, depth + 1);
        break;

    default:
        break;
    }
    return 0;
}

/* Anything already sitting on this space is dealt with before new cards. */
static void resolve_residents(Player *p, int depth)
{
    int sid = space_id(p->region, p->idx);
    int i;

    if (res_gold[sid] > 0) {
        p->gold += res_gold[sid];
        glog("%s picks up %d Gold lying here (%d).", p->name, res_gold[sid], p->gold);
        res_gold[sid] = 0;
        step(p);
    }
    /* Leave it lying if we already have one.  Sweeping it up regardless
     * cleared res_tal for no gain and quietly removed a Talisman from the
     * game -- the one card everybody else still needs to pass the Portal. */
    if (res_tal[sid] && !p->talisman) {
        res_tal[sid] = 0;
        p->talisman  = 1;
        glog("*** %s picks up the TALISMAN lying here! ***", p->name);
        step(p);
    }

    for (i = 0; i < res_n[sid] && p->alive && !turn_over; ) {
        int ci = res_card[sid][i];
        const Card *c = &deck_proto[ci];

        if (c->type == C_ENEMY || c->type == C_SPIRIT) {
            glog("The %s is still here (%s %d)!", c->name,
                 c->craftfight ? "Craft" : "Strength", c->power);
            step(p);
            if (battle(p, c->name, c->power, c->craftfight) == 1) {
                gain_trophies(p, ci);
                res_remove(sid, i);
                continue;                    /* same slot, next resident */
            }
            i++;
        } else if (c->type == C_PLACE || c->type == C_STRANGER) {
            int used = apply_place(p, c, depth);
            if (space_id(p->region, p->idx) != sid) return;   /* a portal moved us */
            if (used) { res_remove(sid, i); continue; }
            i++;
        } else if (c->type == C_FOLLOWER ||
                   (c->type == C_OBJECT && item_objects(p) < item_limit(p))) {
            /* Only lift what there is room for.  Taking it and then being
             * forced to put it straight back drops it here again, and the
             * scan would meet it forever. */
            if (item_give(p, ci)) {
                glog("%s picks up the %s lying here.", p->name, c->name);
                res_remove(sid, i);
                spell_enforce(p);          /* it may have been a cursed one */
                step(p);
                continue;
            }
            i++;
        } else if (c->type == C_OBJECT && !align_allows(p, ci)) {
            glog("%s may not bear the %s, and leaves it face up here.",
                 p->name, c->name);
            i++;
        } else if (c->type == C_OBJECT) {
            /* Hands full -- but full of what?  A Character standing over a
             * Sword with nothing but a Cracked Mirror to their name should
             * trade, not walk away.  Swap only for a clear improvement, so
             * two comparable Objects never start a shuffle back and forth. */
            int worst = p->ai ? item_best_swap(p, ci) : ui_offer_swap(p, ci);
            if (worst >= 0) {
                int drop = p->carried[worst];
                item_drop(p, worst);
                res_add(sid, drop);
                item_give(p, ci);
                res_remove(sid, i);
                glog("%s trades the %s for the %s.",
                     p->name, deck_proto[drop].name, c->name);
                spell_enforce(p);
                step(p);
                continue;
            }
            glog("%s has no room for the %s and leaves it.", p->name, c->name);
            i++;
        } else {
            i++;
        }
    }
}

static void resolve_space(Player *p, int depth)
{
    const Space *sp = space_at(p->region, p->idx);
    int i, r, c;

    glog("%s lands on %s.", p->name, sp->name);
    step(p);
    /* "either one Character of their choice who is in that space, or in the
     * space itself" -- taking the fight means leaving the Space alone. */
    if (pvp_phase(p)) return;
    if (!p->alive || turn_over) return;
    {
        /* If a resident card carries us off -- the Magic Portal is the only
         * one that does -- it resolves wherever it drops us, and this call
         * is finished.  Falling through and re-reading the space applied the
         * destination a second time: the Spy in seed 150 rolled against the
         * waste twice in one turn and died of it. */
        int was = space_id(p->region, p->idx);
        resolve_residents(p, depth);
        if (!p->alive || turn_over) return;
        if (space_id(p->region, p->idx) != was) return;
    }
    sp = space_at(p->region, p->idx);

    switch (sp->kind) {
    case SP_PLAIN:
        break;
    case SP_ADV:
        /* "A cursed Character must move to the Chapel (or Ruins, if Evil)." */
        if (p->cursed && p->align == AL_EVIL &&
            !strcmp(space_at(p->region, p->idx)->name, "Ruins")) {
            p->cursed = 0;
            glog("*** The Ruins lift the Evil Eye from %s. ***", p->name);
        }
        {
            /* Rulebook: you top a space UP to its number, you do not add to
             * it -- "the player only draws one new card to bring the total
             * to two".  Without this, spaces silt up with unkilled monsters. */
            int sid   = space_id(p->region, p->idx);
            int want  = sp->draw - res_n[sid];
            if (want < 0) want = 0;
            /* Cards: "You need not roll the die in the Crags/Forest; you
             * are always safe there."  The Troll, Dwarf, Elf and Druid each
             * name one such space, and nothing is drawn on it. */
            if (p->safe && !strcmp(sp->name, p->safe)) {
                glog("The %s holds no danger for %s.", sp->name, p->name);
                return;
            }
            /* Card: "you may draw one Card more than necessary and then
             * discard one of your choice."  Drawing more of the same deck
             * is the whole of it -- the discard is the choosing. */
            if (want > 0 && has_ab(p, AB_DRAW_EXTRA)) {
                int a = deck_draw(), b = deck_draw(), keep;
                if (a < 0) a = b;
                if (b < 0) b = a;
                if (a >= 0) {
                    keep = better_card_for(p, a, b);
                    glog("%s foresees both %s and %s, and takes the latter road.",
                         p->name, deck_proto[a].name, deck_proto[b].name);
                    resolve_card(p, keep, depth);
                    want--;
                }
            }
            for (i = 0; i < want && p->alive && !turn_over; i++) {
                /* The City has its own Adventure deck, and its streets draw
                 * from that rather than from the Kingdom's. */
                int ci = (p->region == REG_CITY) ? city_draw() : deck_draw();
                if (ci < 0) break;
                resolve_card(p, ci, depth);
            }
        }
        return;
    case SP_VILLAGE:
        do_shop(p, "  THE VILLAGE", SHOP_WARES | SHOP_MYSTIC | SHOP_DOCTOR);
        return;
    case SP_TAVERN:
        r = roll();
        ui_dice_one("Tavern", r, -1);
        glog("%s rolls %d at the Tavern.", p->name, r);
        if (r == 1)      { p->miss += 1; glog("Too much ale. %s misses a turn.", p->name); }
        else if (r <= 3) { p->gold += 2; glog("%s wins at dice. +2 Gold (%d).", p->name, p->gold); }
        else if (r <= 5) { glog("Only rumours and stale bread."); }
        else             { p->base_maxfate++; p->fate++; glog("A stranger toasts your luck. +1 Fate (%d).", p->fate); }
        break;
    case SP_CHAPEL:
        if (p->cursed && p->align != AL_EVIL) {
            p->cursed = 0;
            glog("*** The Chapel lifts the Evil Eye from %s. ***", p->name);
        }
        /* The Druid's card: "if you are carrying the Runesword and you wish
         * to pray at the Chapel, you must drop the Runesword" -- the Chapel
         * is not a place an Evil Character finds welcome. */
        if (p->align == AL_EVIL) {
            glog("The Chapel offers %s nothing; the doors stay shut to the Evil.",
                 p->name);
            break;
        }
        if (p->lives < eff_maxlives(p)) { p->lives++; glog("The chapel heals %s. Lives %d/%d.", p->name, p->lives, eff_maxlives(p)); }
        else glog("%s prays, already whole.", p->name);
        break;
    /* The Evil counterpart of the Chapel: "Evil characters may replenish
     * Fate here for free.  Good characters lose 1 Life.  Neutral characters
     * may pay 1 Gold to replenish 1 Fate."  (Board text, which the rulebook
     * does not reproduce -- taken on report rather than from a scan.) */
    case SP_GRAVE:
        if (p->align == AL_GOOD) {
            glog("The dead have no welcome for %s. -1 Life.", p->name);
            lose_life(p, 1);
        } else if (p->align == AL_EVIL) {
            if (p->fate < eff_maxfate(p)) {
                p->fate++;
                glog("The old stones restore %s. +1 Fate (%d).", p->name, p->fate);
            } else glog("%s walks among friends, and needs nothing.", p->name);
        } else if (p->gold > 0 && p->fate < eff_maxfate(p)) {
            p->gold--; p->fate++;
            glog("%s pays a Gold to the gravedigger. +1 Fate (%d).", p->name, p->fate);
        } else {
            glog("Cold wind, nothing more.");
        }
        break;
    case SP_CRYPT:
        glog("The restless dead rise (Craft 4)!");
        step(p);
        if (battle(p, "Restless Dead", 4, 1) == 1) { p->base_craft++;
            glog("%s takes the crypt's secret. +1 Craft (%d).", p->name, eff_craft(p)); }
        break;
    case SP_TEMPLE:
        r = roll();
        ui_dice_one("Temple", r, -1);
        glog("%s rolls %d at the Temple.", p->name, r);
        if (r == 6)      { p->talisman = 1; glog("*** The priests grant %s a TALISMAN! ***", p->name); }
        else if (r >= 4) { p->base_craft++; glog("%s learns a rite. +1 Craft (%d).", p->name, eff_craft(p)); }
        else if (r >= 2) { glog("The temple is silent."); }
        else             { glog("%s is judged unworthy. -1 Life.", p->name); lose_life(p, 1); }
        break;
    case SP_MINE:
        r = roll();
        ui_dice_one("Mine", r, -1);
        glog("%s rolls %d in the Mine.", p->name, r);
        if (has_ab(p, AB_MINER)) {              /* the Dwarf knows these seams */
            p->gold += r;
            glog("%s knows the seams and digs out %d Gold (%d).", p->name, r, p->gold);
        } else if (r == 1) {
            glog("Cave-in! -1 Life.");
            lose_life(p, 1);
        } else {
            p->gold += r / 2;
            glog("%s digs out %d Gold (%d).", p->name, r / 2, p->gold);
        }
        break;
    case SP_DESERT:
        if (has_item(p, "Water Bottle")) {
            glog("%s drinks from the Water Bottle and crosses unharmed.", p->name);
            break;
        }
        r = roll();
        ui_dice_one("Waste", r, -1);
        glog("%s rolls %d against the waste.", p->name, r);
        if (r >= 5) glog("%s finds shade and water. No harm done.", p->name);
        else { glog("The waste burns. -1 Life."); lose_life(p, 1); }
        break;
    /* 14:8 -- "The die is rolled each time any Character enters the Space.
     * This is the number of Pitfiends that attack that Character.  [Fight]
     * them one at a time, in succession, until the Character loses a Life
     * whereupon that Turn ends.  The Character must then [fight the]
     * remainder next Turn." */
    case SP_PITFIENDS:
        if (p->pitfiends <= 0) {
            p->pitfiends = roll();
            ui_dice_one("PITFIENDS", p->pitfiends, -1);
            glog("%d Pitfiends come at %s out of the dark.", p->pitfiends, p->name);
        } else {
            glog("%d Pitfiends are still on %s.", p->pitfiends, p->name);
        }
        while (p->pitfiends > 0 && p->alive) {
            int before = p->lives;
            p->pitfiends--;
            if (battle(p, "Pitfiend", 3, 0) != 1 && p->lives < before) {
                glog("  %s goes down under them; the rest can wait.", p->name);
                turn_over = 1;
                break;
            }
        }
        if (p->pitfiends <= 0 && p->alive) glog("  The last of them is driven off.");
        break;

    /* 14:7 -- "The die is rolled for a Werewolf's Strength each time any
     * Character enters the Space."  This was an ordinary card space with no
     * Werewolf in it at all. */
    case SP_WEREDEN: {
        int str = roll();
        ui_dice_one("WEREWOLF", str, -1);
        glog("A Werewolf comes out of the den at Strength %d.", str);
        step(p);
        battle(p, "Werewolf", str, 0);
        break;
    }

    case SP_PIT:
        p->miss += 1;
        glog("%s climbs back out. Misses a turn.", p->name);
        break;
    case SP_FIRE:
        glog("The Valley of Fire scorches %s. -1 Life.", p->name);
        lose_life(p, 1);
        break;
    case SP_CITY:
        /* Rulebook: "the City space on the main board is standing at the
         * City Gates", and rule 1 of the expansion makes the old City
         * space's own contents redundant.  Landing here -- which in this
         * engine is always with no movement left over -- puts her on the
         * Gates, ready to go in next turn. */
        p->region = REG_CITY;
        p->idx    = 0;
        glog(">>> %s comes to the CITY GATES. <<<", p->name);
        step(p);
        return;
    case SP_SENTINEL: {
        int fight;
        glog("The Sentinel bars the bridge (Strength 6, or Craft 6 to outwit).");
        if (p->ai) fight = ai_fight_sentinel(p);
        else {
            ui_draw();
            c = ui_prompt("Cross?   [s] fight it (Strength 6)   [c] outwit it (Craft 6)   [n] not yet",
                          "scn");
            fight = (c == 's') ? 1 : (c == 'c') ? 2 : 0;
        }
        if (!fight) { glog("%s keeps to the outer road.", p->name); break; }
        step(p);
        if (battle(p, "Sentinel", 6, fight == 2) == 1) {
            p->region = REG_MIDDLE;
            p->idx    = MID_ENTRY_IDX;
            glog(">>> %s crosses into the MIDDLE REGION. <<<", p->name);
        }
        break;
    }
    case SP_PORTAL:
        if (p->talisman) {
            p->region = REG_INNER;
            p->idx    = INNER_ENTRY_IDX;
            glog(">>> The Talisman opens the Portal. %s enters the INNER REGION. <<<", p->name);
        } else {
            glog("The Portal of Power will not open without a Talisman.");
        }
        break;
    case SP_GATE:
        /* With an ending card face down on the Crown, crossing is a
         * decision and not a formality -- one card in six kills whoever
         * crosses first, and three of the others punish arriving
         * unprepared.  Without the optional rule, the old certainty holds. */
        if (alt_endings) {
            int go = p->ai ? ai_cross_gate(p)
                           : (ui_prompt("The Dread Gate stands open, and something waits beyond."
                                        "   [c] cross   [w] wait", "cw") == 'c');
            if (!go) {
                /* Remember it, so we stop walking back to a door we have
                 * already decided not to open. */
                /* Remember the Craft we turned it down at, not merely that
                 * we did.  A plain flag was never cleared, so a Character
                 * who declined once at Craft 4 would still be avoiding the
                 * Gate at Craft 14, and three of them could circle the
                 * Inner Region until the turn cap. */
                p->gate_shut = eff_craft(p) + 1;
                glog("%s halts before the Dread Gate and lets the moment pass.", p->name);
                break;
            }
            p->gate_shut = 0;
        }
        enter_crown(p, "passes the Dread Gate");
        break;
    case SP_CROWN_SP:
        break;

    /* ---- the Dungeon's rooms, each straight off the board ---- */
    case SP_D_ENTRANCE:
        break;                               /* you simply stop here */

    case SP_D_GUARD: {
        /* "The Guard has a Strength 5.  If you bribe him with 2G he will
         * let you pass, otherwise you must fight him.  The Guard remains
         * here if defeated." */
        int bribe = p->gold >= 2 &&
                    (p->ai ? eff_str(p) < 6
                           : (ui_draw(), ui_prompt("The Guard wants 2 Gold to let you pass.  Pay?  [y]/[n]",
                                                   "yn") == 'y'));
        if (bribe) {
            p->gold -= 2;
            glog("%s buys the Guard's goodwill for 2 Gold.", p->name);
        } else {
            glog("The Guard bars the way (Strength 5).");
            if (battle(p, "Guard", 5, 0) != 1) { p->dungeon_out = 1; turn_over = 1; }
        }
        break;
    }
    case SP_D_LIBRARY: {
        /* "1-2 You have a good read.  3-4 You discover a secret passage.
         * Take an extra move.  5-6 You gain a Spell." */
        int r2 = roll();
        ui_dice_one("LIBRARY", r2, -1);
        if (r2 <= 2)      glog("%s has a good read, and nothing more.", p->name);
        else if (r2 <= 4) { extra_turn = 1; glog("%s finds a secret passage: an extra move.", p->name); }
        else              gain_spell(p);
        break;
    }
    case SP_D_CELL:
        /* "You must leave one of your Followers in the cell.  If there is a
         * follower already here, you may take that Follower with you." */
        {   /* k, not i: the enclosing space-resolution loop owns i */
            int k;
            for (k = 0; k < p->nitems; k++)
                if (deck_proto[p->carried[k]].type == C_FOLLOWER) {
                    glog("The cell door shuts on %s's %s.",
                         p->name, deck_proto[p->carried[k]].name);
                    res_add(space_id(p->region, p->idx), p->carried[k]);
                    item_drop(p, k);
                    break;
                }
            if (k == p->nitems) glog("%s has no Follower to leave in the cell.", p->name);
        }
        break;

    case SP_D_TORTURE:
        /* "You must pay the Torturer 1G or lose 1 point of either Strength
         * or Craft." */
        if (p->gold >= 1) {
            p->gold--;
            glog("%s pays the Torturer his Gold.", p->name);
        } else if (p->base_str > 1) {
            p->base_str--;
            glog("The Torturer takes a point of %s's Strength.", p->name);
        } else if (p->base_craft > 1) {
            p->base_craft--;
            glog("The Torturer takes a point of %s's Craft.", p->name);
        }
        break;

    case SP_D_KITCHEN: {
        /* "1 Poison - lose 1 Life.  2-3 Tastes great!  4-5 Heal 1 Life.
         * 6 Heal 2 Lives." */
        int r2 = roll();
        ui_dice_one("KITCHEN", r2, -1);
        if (r2 == 1)      { glog("Poison!  %s loses a Life.", p->name); lose_life(p, 1); }
        else if (r2 <= 3) glog("It tastes surprisingly good.");
        else {
            int gain = (r2 <= 5) ? 1 : 2, k;
            for (k = 0; k < gain && p->lives < eff_maxlives(p); k++) p->lives++;
            glog("%s is fed, and heals. Lives %d/%d.", p->name, p->lives, eff_maxlives(p));
        }
        break;
    }
    case SP_D_DARK: {
        /* "1. 3 spaces back.  2. 2 spaces back.  3. 1 space back.
         *  4. 1 space forward.  5. 2 spaces forward.  6. 3 spaces forward." */
        static const int shift[6] = { -3, -2, -1, 1, 2, 3 };
        int r2 = roll(), dest;
        ui_dice_one("DARK", r2, -1);
        dest = p->idx + shift[r2 - 1];
        if (dest < 0) dest = 0;
        if (dest > DUNGEON_N - 1) dest = DUNGEON_N - 1;
        glog("In the dark %s stumbles %d space%s %s.", p->name,
             shift[r2-1] < 0 ? -shift[r2-1] : shift[r2-1],
             (shift[r2-1] == 1 || shift[r2-1] == -1) ? "" : "s",
             shift[r2-1] < 0 ? "back" : "on");
        if (dest != p->idx && depth < 2) { p->idx = dest; resolve_space(p, depth + 1); }
        else p->idx = dest;
        break;
    }
    /* ---- the Timescape, off the board and the Data Sheet ---- */
    case SP_T_WARPGATE:
        glog("%s stands at the Warp Gate.  Your Turn ends here.", p->name);
        turn_over = 1;
        break;

    case SP_T_TIMELOOP:
        /* "Return to the space you have just moved from."  Guarded like the
         * Magic Portal is: no warp line leads back into the Time Loop today,
         * so this cannot cycle -- but that is a property of the generated
         * line table, not of this code, and the table may change. */
        glog("The Time Loop folds: %s is returned whence she came.", p->name);
        p->idx = p->warp_from;
        if (depth < 2) resolve_space(p, depth + 1);
        break;

    case SP_T_VORTEX: {
        /* "When you reach the Vortex roll one die and teleport to:
         *  1 Crags 2 Warlock's Cave 3 Village 4 Temple 5 Plain of Peril
         *  6 Warp Gate" -- five ways home and one back to the beginning. */
        static const char *out[6] = {
            "Crags", "Cave", "Village", "Temple", "PlainPeril", NULL
        };
        int r2 = roll(), region, idx2;
        ui_dice_one("VORTEX", r2, -1);
        if (out[r2 - 1] && space_by_name(out[r2 - 1], &region, &idx2)) {
            p->region = region;
            p->idx    = idx2;
            glog(">>> The Vortex casts %s out at the %s. <<<",
                 p->name, space_at(region, idx2)->name);
        } else {
            p->idx = 0;                       /* back to the Warp Gate */
            glog("The Vortex returns %s to the Warp Gate.", p->name);
        }
        turn_over = 1;
        break;
    }
    case SP_T_DEMON:
        /* "He has Strength 12 and Craft 12.  You may choose which type of
         * combat to fight.  [If you win, gain 1 point of] the attribute
         * used in the combat." */
        glog("The Warp Demon rises: Strength 12 and Craft 12 alike.");
        {
            int mind = eff_craft(p) > eff_str(p);
            if (battle(p, "Warp Demon", 12, mind) == 1) {
                if (mind) p->base_craft++; else p->base_str++;
                glog("  %s is the stronger for it. (+1 %s)", p->name,
                     mind ? "Craft" : "Strength");
            }
        }
        break;

    case SP_T_NEXUS: {
        /* "Draw five Adventure cards.  Choose one you wish to encounter
         * and discard the others." */
        int cand[5], n2 = 0, k, best;
        for (k = 0; k < 5; k++) { int ci = deck_draw(); if (ci >= 0) cand[n2++] = ci; }
        if (!n2) break;
        best = cand[0];
        for (k = 1; k < n2; k++) best = better_card_for(p, best, cand[k]);
        glog("The Nexus offers %d futures; %s walks into the %s.",
             n2, p->name, deck_proto[best].name);
        resolve_card(p, best, depth + 1);
        break;
    }
    case SP_T_RADZONE: {
        /* "1 Mutate - gain 2 Craft; 2 Mutate - lose 1 Craft; 3 Radiation
         * Poisoning - lose 2 Lives; 5 Mutate - lose 1 Strength; 6 Mutate -
         * gain 2 Strength."  The sheet prints no 4. */
        int r2 = roll();
        ui_dice_one("RAD ZONE", r2, -1);
        if (r2 == 1)      { p->base_craft += 2; glog("  Mutation: +2 Craft."); }
        else if (r2 == 2) { if (p->base_craft > 1) p->base_craft--; glog("  Mutation: -1 Craft."); }
        else if (r2 == 3) { glog("  Radiation poisoning."); lose_life(p, 2); }
        else if (r2 == 4) glog("  The dust settles, and nothing changes.");
        else if (r2 == 5) { if (p->base_str > 1) p->base_str--; glog("  Mutation: -1 Strength."); }
        else              { p->base_str += 2; glog("  Mutation: +2 Strength."); }
        break;
    }
    case SP_T_DEATHWORLD: {
        /* "1 Poison atmosphere - lose one Life; 2 Fight an Alien - Craft 9;
         * 3 Fight an Alien - Strength 9; 4-6 draw one, two, three Adventure
         * cards."  Beating an Alien gains a point of what you fought with. */
        int r2 = roll(), k;
        ui_dice_one("DEATHWORLD", r2, -1);
        if (r2 == 1)      { glog("  A poison atmosphere."); lose_life(p, 1); }
        else if (r2 <= 3) {
            int mind = (r2 == 2);
            if (battle(p, "Alien", 9, mind) == 1) {
                if (mind) p->base_craft++; else p->base_str++;
                glog("  %s learns from the kill. (+1 %s)", p->name, mind ? "Craft" : "Strength");
            }
        } else for (k = 0; k < r2 - 3 && p->alive && !turn_over; k++) {
            int ci = deck_draw();
            if (ci >= 0) resolve_card(p, ci, depth + 1);
        }
        break;
    }
    case SP_T_FORTRESS:
        /* "Robo-Doc - Heal up to the starting quota of Lives at a cost of
         * one Gold each.  Rogue Trader - you may buy..." */
        do_shop(p, "  THE SPACE FORTRESS", SHOP_WARES | SHOP_DOCTOR);
        break;

    case SP_T_SENTINEL: {
        /* "The Sentinels police the Timescape.  1 Judged a threat -- move
         * to the Vortex next Turn; 2 Imprisoned; 3-4 Pay fine of 2 Gold or
         * be imprisoned; 5-6 Judged innocent." */
        int r2 = roll();
        ui_dice_one("SENTINELS", r2, -1);
        if (r2 == 1)      { p->idx = 8; glog("  Judged a threat: %s is sent to the Vortex.", p->name); }
        else if (r2 == 2) { p->miss += 2; glog("  %s is imprisoned by the Sentinels.", p->name); }
        else if (r2 <= 4) {
            if (p->gold >= 2) { p->gold -= 2; glog("  Fined 2 Gold."); }
            else { p->miss += 2; glog("  %s cannot pay, and is imprisoned.", p->name); }
        } else glog("  Judged innocent.");
        break;
    }
    case SP_T_DRAW: {
        /* "Draw one Timescape card" -- and Planetfall draws two.  These come
         * from the Timescape's own stack, OCR'd off TIME_A/B.JPG. */
        int k, n2 = (p->idx == 10) ? 2 : 1;   /* Planetfall draws two */
        for (k = 0; k < n2 && p->alive && !turn_over; k++) {
            int ci = time_draw();
            if (ci >= 0) resolve_card(p, ci, depth + 1);
        }
        break;
    }

    /* ---- the City's Locations, each straight off the board ---- */
    case SP_C_GATE:
        break;                       /* the way in and out; see take_turn */

    case SP_C_TEMPLE: {              /* "You may Pray." */
        int r2 = roll() + roll();
        ui_dice_one("PRAY", r2, -1);
        if (r2 <= 3)      { p->base_craft++; glog("The High Temple grants %s Craft.", p->name); }
        else if (r2 <= 5) { if (p->lives < eff_maxlives(p)) p->lives++;
                            glog("%s is made whole at the High Temple.", p->name); }
        else if (r2 <= 9) glog("%s prays, and is heard in silence.", p->name);
        else              { p->base_str++; glog("The High Temple grants %s Strength.", p->name); }
        break;
    }
    case SP_C_APOTHECARY: {
        /* "you may buy a Potion for 1G.  1 Poison: lose 1 Life; 2 No effect;
         * 3 Gain 1 Craft; 4 Gain 1 Strength; 5 Gain 1 Life; 6 Anger the
         * Apothecary." */
        int r2;
        if (p->gold < 1) break;
        if (p->ai ? (p->lives < eff_maxlives(p))
                  : (ui_draw(), ui_prompt("Buy a potion for 1 Gold?  [y]/[n]", "yn") == 'y')) {
            p->gold--;
            r2 = roll();
            ui_dice_one("POTION", r2, -1);
            if (r2 == 1)      { glog("Poison!"); lose_life(p, 1); }
            else if (r2 == 2) glog("It does nothing at all.");
            else if (r2 == 3) { p->base_craft++; glog("  +1 Craft."); }
            else if (r2 == 4) { p->base_str++;   glog("  +1 Strength."); }
            else if (r2 == 5) { if (p->lives < eff_maxlives(p)) p->lives++; glog("  +1 Life."); }
            else              glog("The Apothecary takes offence and throws %s out.", p->name);
        }
        break;
    }
    case SP_C_BANK:
        /* "Neutral/Evil Characters: You may attempt to rob the bank.
         * 1 Become a toad; 2 Spotted, take a Warrant; 3 No opportunity;
         * 4 Steal 1G; 5 Steal 2G; 6 Steal 4G and take a Warrant." */
        if (p->align != AL_GOOD && p->gold < 4) {
            int r2 = roll();
            ui_dice_one("ROBBERY", r2, -1);
            if (r2 == 1)      { glog("%s is turned into a toad for the attempt!", p->name); become_toad(p); }
            else if (r2 == 2) { p->warrant = 1; glog("Spotted!  The Watch want %s.", p->name); }
            else if (r2 == 3) glog("No opportunity presents itself.");
            else if (r2 == 4) { p->gold += 1; glog("%s lifts 1 Gold.", p->name); }
            else if (r2 == 5) { p->gold += 2; glog("%s lifts 2 Gold.", p->name); }
            else { p->gold += 4; p->warrant = 1;
                   glog("%s takes 4 Gold -- and is seen doing it.", p->name); }
        }
        break;

    case SP_C_WHARF:
        break;                       /* boats leave at the start of a turn */

    case SP_C_INN: {
        /* "1-3 You get drunk... Miss 1 Turn; 4 A riverman offers...;
         * 5 You hear a ruckus.  Draw a City Adventure Card; 6 the Big
         * Money Card Game: roll and add your Craft." */
        int r2 = roll();
        ui_dice_one("THE INN", r2, -1);
        if (r2 <= 3)      { p->miss++; glog("%s drinks too deep and sleeps it off.", p->name); }
        else if (r2 == 4) glog("A riverman offers %s passage, and is refused.", p->name);
        else if (r2 == 5) { int ci = deck_draw(); if (ci >= 0) resolve_card(p, ci, depth + 1); }
        else {
            int g = roll() + eff_craft(p);
            glog("%s sits down to the Big Money game (%d).", p->name, g);
            if (g <= 3)       { glog("  fleeced: everything, and a Life."); p->nitems = 0; p->gold = 0; lose_life(p, 1); }
            else if (g == 4)  { p->gold -= 2; if (p->gold < 0) p->gold = 0; glog("  down 2 Gold."); }
            else if (g == 5)  { p->gold -= 1; if (p->gold < 0) p->gold = 0; glog("  down 1 Gold."); }
            else if (g <= 7)  glog("  breaks even.");
            else if (g == 8)  { p->gold += 1; glog("  up 1 Gold."); }
            else if (g == 9)  { p->gold += 2; glog("  up 2 Gold."); }
            else              { p->gold *= 2; glog("  doubles his Gold."); }
        }
        break;
    }
    case SP_C_ARMOURY: {
        /* "This Helmet is useless as is, but may be repaired.  Take it to
         * the Armourers, pay 1G."  The Armoury has always been here and the
         * comment has always quoted the rule; the repair itself was never
         * written, which left the Broken Helmet and the Damaged Armour as
         * two cards that could be drawn, carried and stolen and could never
         * do anything at all. */
        static const struct { const char *broken, *whole; } mend[] = {
            { "Broken Helmet",  "Helmet" },
            { "Damaged Armour", "Armour" },
        };
        int m;
        for (m = 0; m < 2; m++) {
            int slot = item_slot(p, mend[m].broken), fixed;
            if (slot < 0 || p->gold < 1) continue;
            if (!p->ai) {
                char q[120];
                snprintf(q, sizeof q, "The Armourers will mend your %s for 1 Gold. "
                         "  [y] pay   [n] not today", mend[m].broken);
                ui_draw();
                if (ui_prompt(q, "yn") == 'n') continue;
            }
            fixed = card_named(mend[m].whole);
            if (fixed < 0) continue;
            p->gold--;
            item_drop(p, slot);
            item_give(p, fixed);
            glog("The Armourers take a Gold and hand %s back a whole %s.",
                 p->name, mend[m].whole);
            step(p);
        }
        do_shop(p, "  THE ARMOURY", SHOP_WARES);
        break;
    }

    case SP_C_STABLES:
        do_shop(p, "  THE STABLES", SHOP_WARES);
        break;

    case SP_C_SURGERY: {
        /* "You may heal up to 2 Lives for 1 Gold each." */
        int k;
        for (k = 0; k < 2 && p->gold >= 1 && p->lives < eff_maxlives(p); k++) {
            p->gold--; p->lives++;
        }
        if (k) glog("The Doctor mends %s for %d Gold. Lives %d/%d.",
                    p->name, k, p->lives, eff_maxlives(p));
        break;
    }
    case SP_C_SQUARE: {
        /* the Town Square, and the Guard who keeps it */
        int r2 = roll();
        ui_dice_one("SQUARE", r2, -1);
        if (r2 == 1)      { p->warrant = 1; glog("%s is denounced in the Square.", p->name); }
        else if (r2 <= 3) glog("The Square is quiet today.");
        else if (r2 <= 5) { if (p->gold) { p->gold--; glog("%s pays a toll of 1 Gold.", p->name); } }
        else              glog("Nothing comes of it.");
        break;
    }
    case SP_C_EMPORIUM:
        /* "You may buy a Spell for 2G." */
        if (p->gold >= 2 && spell_limit(p) > p->nspells) {
            if (p->ai || (ui_draw(), ui_prompt("Buy a Spell for 2 Gold?  [y]/[n]", "yn") == 'y')) {
                p->gold -= 2;
                gain_spell(p);
            }
        }
        break;

    case SP_C_CASTLE:
        /* "You may buy a Warrant for 1G and place it on another Character." */
        if (p->gold >= 1) {
            Player *foe = lead_rival(p);
            if (foe && !foe->warrant &&
                (p->ai || (ui_draw(), ui_prompt("Buy a Warrant for 1 Gold and set it on your rival?  [y]/[n]",
                                                "yn") == 'y'))) {
                p->gold--;
                foe->warrant = 1;
                glog("%s buys a Warrant, and the Watch now want %s.", p->name, foe->name);
            }
        }
        break;

    case SP_C_ENCHANTRESS: {
        /* "If you wish to enter the Talisman Timescape, roll 2 dice.  If
         * the total is equal to or less than your combined Strength and
         * Craft, she will send you to the Warp Gate space." */
        /* "IF YOU WISH to enter the Talisman Timescape" -- it is an offer,
         * not a summons, and the old gate was a stat check rather than a
         * decision.  Once through, Rule 4 gives you no control over where
         * the warp lines take you: the way home is the Vortex, roughly
         * fifteen moves away, with one roll in six sending you back to the
         * start.  Worth it hunting the Space Fortress; not worth it while
         * there is a Talisman to find and a Portal to walk through. */
        /* Measured, not assumed: making the AI decline halved the trips and
         * made games twenty turns LONGER, so the Timescape evidently pays
         * for itself in cards and stats despite the ride home.  The AI
         * takes the offer; the choice is the player's, as the card says. */
        int wish = p->ai ? 1
                         : (ui_draw(), ui_prompt("The Enchantress offers a Warp Gate to the "
                                                 "Timescape.   [y] step through   [n] decline",
                                                 "yn") == 'y');
        if (wish) {
            int t2 = roll() + roll();
            ui_dice_one("THE GATE", t2, -1);
            if (t2 <= eff_str(p) + eff_craft(p)) {
                drop_mounts(p);          /* no horses through a Warp Gate */
                p->region    = REG_TIME;
                p->idx       = 0;
                p->warp_from = 0;
                glog(">>> The Enchantress opens a Warp Gate, and %s steps through. <<<",
                     p->name);
                turn_over = 1;
                step(p);
                break;
            }
            glog("The Enchantress finds %s wanting; the Gate stays shut.", p->name);
        }
        /* "You may also ask for her blessing.  1 Toad for 3 Turns;
         * 2 Lose 1 Strength; 3 Lose 1 Craft; 4-5 Gain 1 Craft; 6 Gain 1 Spell" */
        int r2 = roll();
        ui_dice_one("BLESSING", r2, -1);
        if (r2 == 1)      { glog("%s is a toad for three turns.", p->name); become_toad(p); }
        else if (r2 == 2) { if (p->base_str > 1)   p->base_str--;   glog("  -1 Strength."); }
        else if (r2 == 3) { if (p->base_craft > 1) p->base_craft--; glog("  -1 Craft."); }
        else if (r2 <= 5) { p->base_craft++; glog("  +1 Craft."); }
        else              gain_spell(p);
        break;
    }
    case SP_C_DONJON:
        break;                       /* resolved at the start of her turn */

    case SP_D_TREASURE:
        /* 8.1: you stop here, and leave on your next turn by the table
         * printed in the room. */
        glog("%s stands in the Treasure Chamber.  The way out opens next turn.", p->name);
        turn_over = 1;
        break;
    }
    step(p);
}

/* ------------------------------------------------------------- the turn */

/* Asked only after the die is on the table, so you can see both places
 * the roll could take you before you commit to a direction. */
static int ask_direction(Player *p, int steps)
{
    int len  = ring_len(p->region);
    int cwi  = ((p->idx + steps) % len + len) % len;
    int ccwi = ((p->idx - steps) % len + len) % len;
    char msg[160];
    int c;

    if (p->region == REG_INNER)
        snprintf(msg, sizeof msg, "One step:   [a/<-] %s   [d/->] %s   [q] quit",
                 space_at(p->region, ccwi)->name, space_at(p->region, cwi)->name);
    else
        snprintf(msg, sizeof msg, "You rolled %d:   [a/<-] %s   [d/->] %s   [q] quit",
                 steps, space_at(p->region, ccwi)->name, space_at(p->region, cwi)->name);

    ui_draw();
    c = ui_prompt(msg, "adq");
    if (c == 'q') { quit_flag = 1; return 0; }
    return (c == 'd') ? 1 : -1;
}

static void command_spell(Player *p)
{
    int i;

    glog("%s casts the Command Spell from the Crown!", p->name);
    for (i = 0; i < nplayers; i++) {
        /* "Roll 1 die.  If a 4-6, victim must admit defeat or lose 1 Life.
         * 1-3 means no effect."  It was a guaranteed Life off everyone,
         * which made the throne about twice as deadly as the card. */
        Player *v = &players[i];
        int d;
        if (i == cur_player || !v->alive) continue;
        d = roll();
        if (d < 4) { glog("  The Spell passes over %s. (%d)", v->name, d); continue; }
        /* "must admit defeat or lose 1 Life" -- conceding is quitting the
         * game, so nobody sane takes it while they still have Lives. */
        if (!v->ai && v->lives <= 1 &&
            ui_prompt("The Command Spell has you.   [d] admit defeat   [l] lose a Life",
                      "dl") == 'd') {
            glog("  %s admits defeat. (%d)", v->name, d);
            v->lives = 0;
            v->alive = 0;
            continue;
        }
        glog("  %s is struck down. -1 Life. (%d)", v->name, d);
        lose_life(v, 1);
    }
    step(p);
}

/* --------------------------------------------- the other five endings --
 * Each card replaces the Crown of Command with something else entirely,
 * so each one is a different game in its last few turns. */

/* "You must defeat this Spirit in Psychic Combat... a Craft of 12 and 4
 * Lives.  To defeat him you must take all of his Lives."  Fleeing is
 * allowed, but "if you leave the Crown of Command space, the Demon Lord
 * regains all his Lives." */
#define PLAIN_IDX 0        /* inner: the Plain of Peril, where the King throws you */

/* Hands full, and something better on the ground: offer the trade. */
static int ui_offer_swap(Player *p, int ci)
{
    char m[160];
    int slot;

    snprintf(m, sizeof m, "No room for the %s.   [t] trade something for it   [n] leave it",
             deck_proto[ci].name);
    ui_draw();
    if (ui_prompt(m, "tn") == 'n') return -1;
    slot = ui_pick_item(p, "  GIVE UP WHICH OBJECT?", 1);
    return slot;
}

/* Being able to change Alignment at will is only worth being asked about
 * when something here turns on it -- otherwise a human Druid would face the
 * same question every Turn of the game for nothing. */
static int align_choice_matters(Player *p)
{
    int sid = space_id(p->region, p->idx), k;

    if (p->align == AL_EVIL && p->lives < eff_maxlives(p)) return 1;
    for (k = 0; k < p->nitems; k++) {
        unsigned need = deck_proto[p->carried[k]].needs;
        if (need && need != NEEDS_ANY) return 1;
    }
    for (k = 0; k < res_n[sid]; k++) {
        const Card *c = &deck_proto[res_card[sid][k]];
        if (c->type == C_OBJECT && c->needs && c->needs != NEEDS_ANY) return 1;
    }
    return 0;
}

/* The Druid choosing what to be this Turn. */
static Alignment ui_pick_align(Player *p)
{
    int c;
    static const char *nm[3] = { "Good", "Neutral", "Evil" };

    ui_draw();
    {
        char m[160];
        snprintf(m, sizeof m,
                 "You are %s.  Change Alignment?   [g] Good   [n] Neutral   [e] Evil   [k] keep",
                 nm[p->align]);
        c = ui_prompt(m, "gnek");
    }
    return c == 'g' ? AL_GOOD : c == 'n' ? AL_NEUTRAL
         : c == 'e' ? AL_EVIL : p->align;
}

/* Endings that let you strike at a named opponent need someone chosen. */
static int ui_pick_victim(Player *p, const char *msg)
{
    char valid[MAX_PLAYERS + 2];
    char line[160];
    int i, n = 0, c;

    line[0] = 0;
    for (i = 0; i < nplayers; i++) {
        if (i == (int)(p - players) || !players[i].alive) continue;
        valid[n++] = (char)('1' + i);
        snprintf(line + strlen(line), sizeof line - strlen(line),
                 "  [%d] %s (%d)", i + 1, players[i].name, players[i].lives);
    }
    if (!n) return -1;
    valid[n] = 0;
    ui_draw();
    glog("%s", msg);
    c = ui_prompt(line, valid);
    return c - '1';
}

/* "eats one of your Followers" -- the first one to hand. */
static void drop_follower(Player *p)
{
    int i;
    for (i = 0; i < p->nitems; i++)
        if (deck_proto[p->carried[i]].type == C_FOLLOWER) {
            glog("  %s is devoured.", deck_proto[p->carried[i]].name);
            p->carried[i] = p->carried[--p->nitems];
            return;
        }
}

static void demon_lord(Player *p)
{
    int r, flee;

    /* "You may choose to flee from the Demon Lord... if you leave the Crown
     * of Command space, the Demon Lord regains all his Lives."  Without
     * this, anyone dumped on the Crown out of the Dungeon with no Craft is
     * simply ground to death with no way out. */
    flee = p->ai ? ai_flee_demon(p, demon_lives)
                 : (ui_prompt("The Demon Lord bars the Crown.   [f] fight   [r] retreat",
                              "fr") == 'r');
    if (flee) {
        /* "you may choose to flee from the Demon Lord at any time in which
         * case you return to the Plain of Peril and the Demon Lord regains
         * all his Lives." */
        glog("%s flees the Demon Lord and returns to the Plain of Peril.", p->name);
        glog("  His wounds close behind them.");
        p->region   = REG_INNER;
        p->idx      = PLAIN_IDX;
        demon_lives = 4;
        return;
    }

    glog("The Demon Lord rises. (Craft 12, %d %s left)",
         demon_lives, demon_lives == 1 ? "Life" : "Lives");
    r = battle(p, "Demon Lord", 12, 1);
    if (r != 1) {
        /* "If you are killed, the Demon Lord regains his Lives and awaits
         * the next challenger." */
        if (!p->alive) {
            demon_lives = 4;
            glog("  The Demon Lord is whole again, and waits for the next.");
        }
        return;
    }
    if (--demon_lives > 0) {
        glog("  The Demon Lord reels -- %d %s remain.",
             demon_lives, demon_lives == 1 ? "Life" : "Lives");
        return;
    }
    glog("*** %s destroys the Demon Lord and wins the game! ***", p->name);
    demon_lives = 0;
    win_override = (int)(p - players);
}

/* "Roll one die for the number of Spells you pick up each turn... You must
 * use these on the turn that you collect them.  They may be used against
 * any of the other players." */
static void pandoras_box(Player *p)
{
    /* "Roll one die for the number of Spells you pick up each turn and one
     * die for the number of Adventure cards you pick up each turn."  The
     * Adventure cards were missing, which halved the chest's output. */
    int nspell = roll(), nadv = roll(), n = nspell + nadv, i, t;

    glog("%s opens the chest -- %d %s and %d Adventure %s spill out.",
         p->name, nspell, nspell == 1 ? "Spell" : "Spells",
         nadv, nadv == 1 ? "card" : "cards");
    for (i = 0; i < n; i++) {
        Player *q;
        t = p->ai ? ai_bolt_target(p) : ui_pick_victim(p, "  HURL THE SPELL AT WHOM?");
        if (t < 0) break;
        q = &players[t];
        {
            const char *what = (i < nspell) ? "Spell" : "Adventure card";
            if (roll() >= 4) {
                glog("  The %s strikes %s. -1 Life.", what, q->name);
                lose_life(q, 1);
            } else {
                glog("  The %s gutters out short of %s.", what, q->name);
            }
        }
        if (alive_count() <= 1) break;
    }
}

/* "If you are alone, you may don the belt... transformed into a superhumanly
 * strong Character with the power of Teleportation and 5 Lives.  You must
 * move to a space occupied by another Character and challenge them to a
 * duel." */
static void belt_of_hercules(Player *p)
{
    int me = (int)(p - players), t;
    Player *q;

    if (belt_wearer < 0) {
        belt_wearer = me;
        p->belt  = 1;
        p->lives = 5;
        glog("*** %s dons the Belt of Hercules -- Strength 12, 5 Lives, and the",
             p->name);
        glog("*** power of Teleportation. ***");
        return;                       /* donning it is the whole turn */
    }
    if (belt_wearer != me) return;    /* someone else wears it; nothing here */

    t = p->ai ? ai_bolt_target(p) : ui_pick_victim(p, "  TELEPORT TO WHOM?");
    if (t < 0) return;
    q = &players[t];
    glog("%s teleports to %s and calls them out.", p->name, q->name);
    p->region = q->region;
    p->idx    = q->idx;
    pvp_encounter(p, q, 1);
    if (!p->alive) {
        /* "If the Character wearing the belt is killed, it transports
         * itself back to the Crown of Command space." */
        glog("  The Belt tears free and flies back to the Crown of Command.");
        belt_wearer = -1;
    }
}

/* "Roll one die:" -- and the whole game turns on it. */
static void dragon_king(Player *p)
{
    int r = roll(), i;

    glog("%s stands before the Dragon King. The die falls... %d", p->name, r);
    switch (r) {
    case 1:
        glog("  He thanks you kindly for the meal, eats one of your Followers,");
        glog("  and throws you into the Plain of Peril.");
        drop_follower(p);
        p->region = REG_INNER; p->idx = PLAIN_IDX;
        break;
    case 2:
        glog("  The King's three younger brothers step forward.");
        for (i = 0; i < 3; i++) {
            if (battle(p, "Young Dragon", 9, 0) != 1) {
                glog("  You are thrown back into the Plain of Peril. Try again!");
                p->region = REG_INNER; p->idx = PLAIN_IDX;
                return;
            }
        }
        glog("*** All three lie slain. %s wins the game! ***", p->name);
        win_override = (int)(p - players);
        break;
    case 3:
        glog("  The King himself. Strength 12, Craft 12, 5 Lives -- and you");
        glog("  must fight him in Combat and Psychic Combat at the same time.");
        {
            /* "If you lose all your Lives, he has eaten you and all your
             * Followers, etc.  You lose this game!"  There is no walking
             * away from this one -- it runs until he has five wounds or
             * you are dead.  It used to bounce you to the Plain of Peril
             * on any lost round, which turned the deadliest face of the
             * die into the safest. */
            int wounds = 0, guard = 0;
            while (wounds < 5 && p->alive && guard++ < 200) {
                int a = battle(p, "Dragon King", 12, 0);
                int b = battle(p, "Dragon King", 12, 1);
                if (a == 1) wounds++;
                if (b == 1) wounds++;
                if (wounds) glog("  The Dragon King bears %d %s.",
                                 wounds, wounds == 1 ? "wound" : "wounds");
            }
            if (!p->alive) {
                glog("  He has eaten you and all your Followers. You lose this game!");
                return;
            }
            glog("*** %s slays the Dragon King and wins the game! ***", p->name);
            win_override = (int)(p - players);
        }
        break;
    case 4:
        glog("  The King decides he really likes you, and flies off to eat");
        glog("  your opponents at your request.");
        for (i = 0; i < nplayers; i++) {
            Player *q = &players[i];
            if (q == p || !q->alive) continue;
            /* "always gets his Lives regenerated between Combats" */
            if (battle(q, "Dragon King", 12, 0) == 1) {
                glog("*** %s kills the Dragon King and takes the throne -- and the game! ***",
                     q->name);
                win_override = i;
                return;
            }
        }
        if (alive_count() <= 1)
            glog("*** The board is cleared. %s sits the throne and wins! ***", p->name);
        break;
    case 5:
        glog("  He sleeps atop his treasure. Roll under your Craft to strike.");
        if (roll() < eff_craft(p)) {
            glog("*** The blade goes home. %s wins the game! ***", p->name);
            win_override = (int)(p - players);
        } else {
            glog("  He wakes, eats one of your Followers, and hurls you out.");
            drop_follower(p);
            p->region = REG_INNER; p->idx = PLAIN_IDX;
        }
        break;
    default:
        glog("*** The Dragon King is out to lunch! %s takes all his treasure", p->name);
        glog("*** and magic, and wins the game! ***");
        win_override = (int)(p - players);
        break;
    }
}

static void crown_turn(Player *p)
{
    switch (ending_revealed ? ending : END_CROWN) {
    case END_DEMON:   demon_lord(p);       break;
    case END_PANDORA: pandoras_box(p);     break;
    case END_BELT:    belt_of_hercules(p); break;
    case END_DRAGON:  dragon_king(p);      break;
    default:          command_spell(p);    break;
    }
}

static void take_turn(Player *p)
{
    int dir, steps, len;

    if (!p->alive) return;
    ui_dice_clear();
    turn_over  = 0;
    extra_turn = 0;
    p->rerolled_move = 0;
    p->realigned     = 0;  /* 7:2 -- one Alignment change per Turn */
    wand_top_up(p);        /* "take one immediately each time you drop" */

    /* The Druid: "You may change your Alignment at will.  At any given time,
     * though, you can only be of one Alignment."  Offered at the top of the
     * Turn, so the choice is made before the Runesword or the Chapel is. */
    if (has_ab(p, AB_ANY_ALIGN) && align_choice_matters(p)) {
        Alignment want = p->ai ? ai_pick_align(p) : ui_pick_align(p);
        if (want != p->align) change_align(p, want, "at will");
    }

    enforce_limit(p);      /* backstop: the limit applies at any time */
    spell_enforce(p);
    p->casts_left = p->nspells;      /* rulebook: as many as held at turn start */

    if (p->miss > 0) {
        p->miss--;
        glog("%s misses this turn.", p->name);
        step(p);
        return;
    }
    /* The Samurai's card, and the third option rule 13:2 offers as amended:
     * "Instead of moving, you may fire your bow at a Character or face up
     * Enemy that is not more than 3 spaces away from you in the same Region
     * (not the Inner Region).  The shot is resolved as normal Combat.  If
     * you lose, the shot misses.  If you win... a Character loses a Life."
     * Losing costs the archer nothing, which is what makes a bow a bow. */
    if (has_ab(p, AB_BOW) && p->region != REG_INNER && p->region != REG_TIME) {
        int len2 = ring_len(p->region), k, best = -1, bd = 99;
        for (k = 0; k < nplayers; k++) {
            Player *q = &players[k];
            int d;
            if (q == p || !q->alive || q->region != p->region) continue;
            d = dist_either(p->idx, q->idx, len2);
            if (d > 3 || d == 0) continue;      /* range 3, and not underfoot */
            if (p->ai && !ai_attack(p, q)) continue;
            if (d < bd) { bd = d; best = k; }
        }
        if (best >= 0) {
            Player *q = &players[best];
            int shoot;
            if (p->ai) shoot = 1;
            else {
                char m[140];
                snprintf(m, sizeof m, "Fire the bow at %s, %d spaces off, instead of moving?"
                         "   [y] loose   [n] move", q->name, bd);
                ui_draw();
                shoot = (ui_prompt(m, "yn") == 'y');
            }
            if (shoot) {
                int a = eff_str(p) + roll(), b = eff_str(q) + roll();
                glog("%s looses an arrow at %s, %d %s off.", p->name, q->name,
                     bd, bd == 1 ? "space" : "spaces");
                glog("  %s (%d)  vs  %s (%d)", p->name, a, q->name, b);
                if (a > b) { glog("  It finds its mark. -1 Life."); lose_life(q, 1); }
                else         glog("  The shot misses.");
                step(p);
                return;
            }
        }
    }

    /* 19:4 -- "A Toad does not roll the die for Movement, but must Move one
     * Space per Turn."  It used to simply miss three Turns, which made the
     * curse a rest rather than a punishment: nothing could reach it and it
     * risked nothing.  Now it is out there at Strength 1 with empty hands. */
    if (p->toad > 0) {
        glog("%s is a toad, and hops one space. (%d %s left)", p->name,
             p->toad, p->toad == 1 ? "Turn" : "Turns");
        len   = ring_len(p->region);
        dir   = p->ai ? ai_direction(p, 1)
                      : (ui_draw(), ui_prompt("Hop which way?   [a] left   [d] right", "ad") == 'a' ? -1 : 1);
        p->idx = (((p->idx + dir) % len) + len) % len;
        if (--p->toad == 0)
            glog("*** %s is themselves again. ***", p->name);
        resolve_space(p, 0);
        return;
    }
    /* Rulebook: only a character ALONE on the Crown may work the ending.
     * With a rival up there, the turn is spent fighting them instead. */
    cast_phase(p);
    if (!p->alive || quit_flag) return;

    /* The Donjon is resolved before anything else: she is not free to move
     * until she is out of it. */
    if (p->region == REG_DONJON) { donjon_turn(p); return; }

    /* At the Gates, she may step back out onto the main board -- and the
     * Watch will stop her if there is a Warrant out. */
    if (p->region == REG_CITY && p->idx == 0) {
        int out;
        if (p->ai) out = !ai_wants_city(p);
        else {
            ui_draw();
            out = (ui_prompt("Leave the City by the Gate?  [y]/[n]", "yn") == 'y');
        }
        if (out) {
            if (p->warrant) {
                glog("The Watch bar the Gate: there is a Warrant out for %s.", p->name);
                if (battle(p, "the Watch", 7, 0) == 0) {
                    p->region = REG_DONJON;
                    p->idx    = 0;
                    glog(">>> %s is taken to the DONJON. <<<", p->name);
                    step(p);
                    return;
                }
                glog("%s shakes them off, but may not leave this turn.", p->name);
                step(p);
                return;
            }
            {   /* the City Gates ARE the City space, so she steps back onto it */
                int back = outer_by_name("City");
                p->region = REG_OUTER;
                p->idx    = back >= 0 ? back : 0;
                glog(">>> %s leaves the City. <<<", p->name);
                step(p);
                return;
            }
        }
    }

    /* "If a Character is at the Wharf at the beginning of his turn he may
     * pay 2G to be transported to any space of your choice in the Outer
     * Region" -- and not at all while a Warrant stands. */
    if (p->region == REG_CITY && space_at(p->region, p->idx)->kind == SP_C_WHARF
        && p->gold >= 2 && !p->warrant) {
        if (p->ai || (ui_draw(), ui_prompt("Hire a boat out of the City for 2 Gold?  [y]/[n]",
                                           "yn") == 'y')) {
            p->gold  -= 2;
            p->region = REG_OUTER;
            p->idx    = outer_by_name("Village");
            if (p->idx < 0) p->idx = 0;
            glog(">>> %s takes ship from the Wharf to the %s. <<<",
                 p->name, space_at(REG_OUTER, p->idx)->name);
            step(p);
            return;
        }
    }

    /* 8.1: the turn after reaching the Treasure Chamber is spent leaving it,
     * and that is the whole move. */
    if (p->region == REG_DUNGEON && p->idx == DUNGEON_N - 1) {
        leave_treasure(p);
        return;
    }

    /* 5.2: having landed on a Doorway, this turn may be spent stepping onto
     * the Entrance -- and no further: "You must stop there."  Optional, so
     * she may simply carry on round the main board instead. */
    if (p->at_doorway && p->region != REG_DUNGEON) {
        int go;
        if (p->ai) go = ai_wants_dungeon(p);
        else {
            ui_draw();
            go = (ui_prompt("Take the stair down to the Dungeon Entrance?  [y]/[n]", "yn") == 'y');
        }
        p->at_doorway = 0;
        if (go) {
            drop_mounts(p);                  /* 5.3: no horses below */
            p->region      = REG_DUNGEON;
            p->idx         = 0;
            p->dungeon_out = 0;
            p->fleeing     = 0;
            p->just_entered = 1;
            glog(">>> %s goes down, and stops at the Dungeon Entrance. <<<", p->name);
            step(p);
            return;                          /* the stair is the whole move */
        }
        glog("%s leaves the Doorway shut.", p->name);
    }

    /* 8.2: back at the Entrance on a later turn, she may climb out to any
     * Doorway on the main board, and that is her whole move. */
    if (p->region == REG_DUNGEON && p->idx == 0 && !p->just_entered) {
        int dreg, didx;
        if (first_doorway(&dreg, &didx)) {
            int go;
            if (p->ai) go = !ai_stays_below(p);
            else {
                ui_draw();
                go = (ui_prompt("Climb out of the Dungeon to the Doorway above?  [y]/[n]", "yn") == 'y');
            }
            if (go) {
                p->region  = dreg;
                p->idx     = didx;
                p->fleeing = 0;
                glog(">>> %s climbs out at the %s. <<<", p->name, space_at(dreg, didx)->name);
                step(p);
                return;
            }
        }
    }
    p->just_entered = 0;

    /* The Belt carries its own endgame with it: "the power of Teleportation
     * ... You must move to a space occupied by another Character and
     * challenge them to a duel."  That is every turn, wherever they stand,
     * not only while they happen to be on the Crown. */
    if (p->belt && p->alive) { belt_of_hercules(p); return; }

    if (p->region == REG_CROWN) {
        Player *rival = pvp_target(p);
        if (rival) {
            glog("%s cannot cast while %s contests the Crown!", p->name, rival->name);
            pvp_encounter(p, rival, 1);
        } else {
            crown_turn(p);
        }
        return;
    }

    /* Rulebook: in a Woods or Forest, with an Axe, you may spend your whole
     * move building a Raft; next turn it carries you across the Storm River
     * to the space directly opposite, and no die is rolled at all. */
    /* Rule 11:18 names no direction -- "A Character with a Raft may cross the
     * river to any Space of their choice directly opposite the one they are
     * in" -- so a Raft in the Middle Region carries you back out again. */
    if (p->region == REG_MIDDLE && (p->raft || has_item(p, "Raft"))) {
        int dest;
        p->raft = 1;
        dest = back_across_river(p->idx);
        if (dest < 0) p->raft = 0;
        else {
            char m[140];
            int go;
            snprintf(m, sizeof m,
                     "Take the Raft back across to the %s?   [y] yes   [n] stay",
                     space_at(REG_OUTER, dest)->name);
            go = p->ai ? ai_recross_river(p) : (ui_draw(), ui_prompt(m, "yn") == 'y');
            if (go) {
                p->raft   = 0;
                p->region = REG_OUTER;
                p->idx    = dest;
                glog(">>> %s poles back across the Storm River to the %s. <<<",
                     p->name, space_at(REG_OUTER, p->idx)->name);
                step(p);
                resolve_space(p, 0);
                return;
            }
        }
    }

    if (p->region == REG_OUTER) {
        const char *here = space_at(REG_OUTER, p->idx)->name;
        int wooded = !strcmp(here, "Woods") || !strcmp(here, "Forest")
                  || !strcmp(here, "Thicket");
        if (!p->raft && has_item(p, "Raft")) p->raft = 1;
        if (p->raft) {
            int dest = across_river(p->idx);
            if (dest < 0) p->raft = 0;
            else {
                char m[140];
                int go;
                snprintf(m, sizeof m,
                         "Push the Raft out and cross to the %s?   [y] yes   [n] let it go",
                         space_at(REG_MIDDLE, dest)->name);
                go = p->ai ? 1 : (ui_draw(), ui_prompt(m, "yn") == 'y');
                p->raft = 0;                   /* used or not, the Raft is gone */
                if (go) {
                    p->region = REG_MIDDLE;
                    p->idx    = dest;
                    glog(">>> %s poles across the Storm River to the %s. <<<",
                         p->name, space_at(REG_MIDDLE, p->idx)->name);
                    step(p);
                    resolve_space(p, 0);
                    return;
                }
                glog("%s lets the Raft drift away.", p->name);
            }
        } else if (wooded && has_item(p, "Axe")) {
            int build;
            if (p->ai) build = (best_stat_of(p) < 5 && !p->talisman);
            else {
                ui_draw();
                build = (ui_prompt("Spend your move felling timber for a Raft?   [y] yes   [n] no",
                                   "yn") == 'y');
            }
            if (build) {
                p->raft = 1;
                glog("%s spends the day building a Raft in the %s.", p->name, here);
                step(p);
                return;                        /* building IS the move */
            }
        }
    }

    /* Roll first, THEN choose a direction -- you cannot pick a road
     * sensibly without knowing how far down it you are going. */
    if (p->region == REG_INNER) {
        steps = 1;
        ui_dice_clear();
        glog("%s may take one step through the Inner Region.", p->name);
    } else {
        if (!p->ai) {
            for (;;) {
                int k;
                ui_draw();
                k = ui_prompt("Your turn.   [space] roll   [t] sheet   [?] help   [q] quit", " tq");
                if (k == 'q') { quit_flag = 1; return; }
                if (k == 't') { ui_sheet(p); continue; }
                break;
            }
        }
        steps = roll();
        ui_dice_one("MOVE", steps, -1);
        glog("%s rolls %d.", p->name, steps);

        if (p->ai) step(p);              /* let the table see the AI's die */
    }

    /* The Dungeon is a PATH, not a ring.  Rulebook 6.2: "in the Dungeon you
     * may only move towards the center"; 6.4 allows the other way only when
     * a card says so, when you have just been beaten (next turn only), or
     * when fleeing; 7.3: "You must stop moving if you reach the Entrance
     * space itself".  Wrapping here would carry a character out of the
     * deepest chamber and back to the door without a word. */
    /* Board legend: "Follow the coloured warp line corresponding with the
     * dice roll shown below: 1,2 / 3,4 / 5,6.  Movement is One Way Only."
     * Rule 4: "Characters have no control over their movement... No
     * Character may use Followers, Objects, Spells or Abilities to affect
     * where they move."  So there is no direction to ask for here. */
    if (topology(p->region) == TOPO_WARP) {
        int line = (steps <= 2) ? 0 : (steps <= 4) ? 1 : 2;
        int dest = warp_line[p->idx][line];
        p->warp_from = p->idx;
        p->idx = dest;
        glog("%s is drawn along the %s warp line to the %s.", p->name,
             line == 0 ? "white" : line == 1 ? "blue" : "red",
             space_at(p->region, p->idx)->name);
        resolve_space(p, 0);
        return;
    }

    if (topology(p->region) == TOPO_PATH) {
        int len_p = ring_len(p->region);
        int back  = p->dungeon_out || p->fleeing;
        int dest  = p->idx + (back ? -steps : steps);

        p->dungeon_out = 0;                  /* it lasts one turn only */
        if (dest < 0) dest = 0;              /* stop at the Entrance   */
        if (dest > len_p - 1) dest = len_p - 1;   /* and at the end     */
        p->idx = dest;
        glog("%s goes %d %s.", p->name, steps,
             back ? "back towards the Entrance" : "deeper");
        resolve_space(p, 0);
        return;
    }

    dir = p->ai ? ai_direction(p, steps) : ask_direction(p, steps);
    if (quit_flag) return;

    len   = ring_len(p->region);
    p->idx = (((p->idx + dir * steps) % len) + len) % len;
    glog("%s moves %d %s.", p->name, steps, dir > 0 ? "clockwise" : "anti-clockwise");

    resolve_space(p, 0);
}

/* ---------------------------------------------------------------- setup */

/* Fill in a fresh character, for setup and for respawns alike. */
static void deal_character(Player *p, int t, int idx)
{
    const CharTemplate *ct = &char_tbl[t];

    char_taken[t] = 1;
    snprintf(p->name, sizeof p->name, "%s", ct->cls);
    p->cls           = ct->cls;
    p->base_str      = ct->str;
    p->base_craft    = ct->craft;
    p->lives         = p->base_maxlives = ct->lives;
    p->fate          = p->base_maxfate  = ct->fate;
    p->gold          = ct->gold;
    p->region        = REG_OUTER;
    p->idx           = idx;
    p->alive         = 1;
    p->talisman      = 0;
    p->troph_str     = p->troph_craft = 0;
    p->nitems        = 0;
    p->nkills        = 0;
    p->miss          = 0;
    p->abil          = ct->abil;
    p->align         = ct->align;
    p->card_align    = ct->align;   /* what the Character Card prints */
    p->safe          = ct->safe;
    p->rerolled_move = 0;
    p->nspells       = 0;      /* a new character does not inherit magic */
    p->toad          = 0;      /* nor the last one's curse */
    p->cursed        = 0;
    p->pitfiends     = 0;
    p->miss          = 0;
    p->gate_shut     = 0;
    p->fire_shy      = 0;
    p->hedged        = 0;
    {   /* the card says how many Spells she opens the game holding */
        int k;
        for (k = 0; k < ct->spells; k++) gain_spell(p);
        p->start_spells = p->nspells;   /* the Wand measures from here */
    }
    p->casts_left    = 0;
    p->preserve      = 0;
}

static void setup(void)
{
    /* the alternative-ending card is shuffled in fresh for every game */
    ending_spent = 0;
    ending_revealed = 0;
    belt_wearer = -1;
    win_override = -1;
    demon_lives = 4;
    if (alt_endings) deal_ending(); else ending = END_CROWN;

    int i, c, pick = 0;

    memset(char_taken, 0, sizeof char_taken);
    /* char_taken is indexed by character id; if the table ever outgrows it,
     * say so here rather than scribble over whatever follows. */
    if (char_count > MAX_CHARS) {
        endwin();
        fprintf(stderr, "setup: %d characters but MAX_CHARS is %d\n",
                char_count, MAX_CHARS);
        exit(1);
    }
    if (autoplay) {
        nplayers = 4;
        /* TALISMAN_CHAR forces seat one's Character, so a rule that only a
         * particular card can reach still gets exercised. */
        pick = (forced_char >= 0 && forced_char < char_count)
             ? forced_char : rand() % char_count;
        goto deal;
    }
    if (watching) { nplayers = 4; pick = rand() % char_count; goto deal; }

    erase();
    mvprintw(1, 2, "T A L I S M A N   (C / ncurses)");
    mvprintw(3, 2, "The board is three concentric squares: 24 spaces outside,");
    mvprintw(4, 2, "16 in the middle, 8 inside, and the Crown of Command at the centre.");
    mvprintw(5, 2, "You can only move inward at the SENTINEL, the PORTAL and the DREAD GATE.");
    mvprintw(7, 2, "How many characters in this game?   [2] [3] [4]");
    mvprintw(8, 2, "Or sit back and watch the computer play itself:   [w]");
    refresh();
    c = ui_prompt("", "234w");
    if (c == 'w') {
        watching = 1;
        nplayers = 4;
        pick     = rand() % char_count;
        goto deal;
    }
    nplayers = c - '0';

    /* The expansions bring characters as well as cards, so the roster
     * follows whatever was shuffled in -- 14 in the base game, more with
     * the Dungeon, City or Timescape.  Too many for one screen, so it
     * pages. */
    {
        int avail[64], navail = 0, page = 0, per;
        char keys[40];

        for (i = 0; i < char_count && navail < 64; i++)
            if (SET_ON(char_tbl[i].set)) avail[navail++] = i;
        for (;;) {
            int first;
            per   = draw_char_page(avail, navail, page, keys);
            first = page * per;
            c = ui_prompt("", keys);
            if (c == ' ') { page = (page + 1) % ((navail + per - 1) / per); continue; }
            pick = avail[first + ((c >= 'a') ? (c - 'a' + 9) : (c - '1'))];
            break;
        }
        goto chosen;
    }

    erase();
    mvprintw(1, 2, "Choose your character   (2nd Edition, all fourteen):");
    /* Fourteen characters will not fit two rows each on a 29-row terminal,
     * so each gets one line and the keys run 1-9 then a-e. */
    {
        char keys[24];
        int n = 0;
        for (i = 0; i < char_count && i < 14; i++) {
            char k = (i < 9) ? (char)('1' + i) : (char)('a' + i - 9);
            keys[n++] = k;
            mvprintw(2 + i, 2, "[%c] %-10s S%d C%d L%d F%d G%d  %-7s %.34s",
                     k, char_tbl[i].cls, char_tbl[i].str, char_tbl[i].craft,
                     char_tbl[i].lives, char_tbl[i].fate, char_tbl[i].gold,
                     char_tbl[i].align == AL_GOOD ? "Good" :
                     char_tbl[i].align == AL_EVIL ? "Evil" : "Neutral",
                     char_tbl[i].power);
        }
        keys[n] = 0;
        mvprintw(3 + i, 2, "Each begins on the space named on her card.");
        refresh();
        c = ui_prompt("", keys);
        pick = (c >= 'a') ? (c - 'a' + 9) : (c - '1');
    }
    if (pick < 0 || pick >= char_count) pick = 0;

chosen:
deal:
    for (i = 0; i < nplayers; i++) {
        int t;
        if (i == 0) t = pick;
        else {
            int guard = 0;
            do { t = rand() % char_count; }
            while ((char_taken[t] || !SET_ON(char_tbl[t].set)) && ++guard < 400);
        }
        {   /* The card names where she begins, and an expansion character may
         * begin somewhere that is not on the Outer Region at all -- the
         * Timescape ones start at the Warp Gate or the Space Fortress. */
        int hreg, hidx;
        if (space_by_name(char_tbl[t].start, &hreg, &hidx)) {
            deal_character(&players[i], t, hidx);
            players[i].region = hreg;
        } else {
            deal_character(&players[i], t, (i * 6) % OUTER_N);
        }
    }
        players[i].ai = watching ? 1 : (i != 0);
        /* How far ahead this character looks when choosing a direction.
         * TALISMAN_LOOK_AB seats the new brain and the old one at the same
         * table -- even seats look ahead, odd seats do not -- which is the
         * only honest way to find out whether the extra ply is worth it. */
        players[i].look = look_ab ? ((i % 2 == look_ab % 2) ? look_depth : 0) : look_depth;
        players[i].road = road_ab ? (i % 2 == road_ab % 2) : road_on;
        players[i].hench.ct    = -1;
        players[i].hench.lives = 0;
    }
    /* "Players choose or draw Characters as normal, and then randomly draw
     * additional Characters from the remaining Character cards for use as
     * Henchmen.  We suggest that players not choose Henchmen Characters as
     * this would give them an unfair advantage."  So these are dealt, never
     * picked -- including for the human. */
    if (henchmen_on) {
        int i;
        for (i = 0; i < nplayers; i++) {
            int t, guard = 0;
            do { t = rand() % char_count; }
            while ((char_taken[t] || !SET_ON(char_tbl[t].set)) && ++guard < 400);
            if (char_taken[t] || !SET_ON(char_tbl[t].set)) continue;
            char_taken[t] = 1;
            players[i].hench.ct = t;
            /* "they cannot increase their Strength, Craft or Lives.  They
             * can therefore never have more than 4 Lives." */
            players[i].hench.lives = char_tbl[t].lives > 4 ? 4 : char_tbl[t].lives;
            glog("  @%d %-11s Henchman: %-11s Str %-2d Craft %-2d %d Lives",
                 i + 1, players[i].name, char_tbl[t].cls,
                 char_tbl[t].str, char_tbl[t].craft, players[i].hench.lives);
        }
    }

    {
        int k;
        for (k = 0; k < nplayers; k++)
            /* The log column is 85 wide; this line used to run to 94 and
             * lost its tail.  Who is human is already on the status bar. */
            glog("  @%d %-11s Str %-2d Craft %-2d Lives %d  Fate %d  Gold %d  %-7s at %s",
                 k + 1, players[k].cls, players[k].base_str, players[k].base_craft,
                 players[k].lives, players[k].fate, players[k].gold,
                 players[k].align == AL_GOOD ? "Good" :
                 players[k].align == AL_EVIL ? "Evil" : "Neutral",
                 space_at(players[k].region, players[k].idx)->name);
    }
    if (watching)
        glog("Four characters, no player. [space] pause, [+/-] speed, [q] quit.");
    else
        glog("The quest begins. You are @1, the %s.", players[0].cls);
}

/* ----------------------------------------------------------------- main */

static void game_over(const char *msg)
{
    if (autoplay) return;
    ui_draw();
    ui_banner(msg);
    getch();
}

/* "all", or a comma-separated list of the short names above. */
static int parse_sets(const char *spec)
{
    char buf[256], *tok;
    int i, ok = 1;

    if (!strcmp(spec, "all")) {
        for (i = 0; i < SET_COUNT; i++) enabled_sets |= 1 << i;
        return 1;
    }
    snprintf(buf, sizeof buf, "%s", spec);
    for (tok = strtok(buf, ","); tok; tok = strtok(NULL, ",")) {
        for (i = 0; i < SET_COUNT; i++)
            if (!strcmp(tok, set_key[i])) { enabled_sets |= 1 << i; break; }
        if (i == SET_COUNT) { fprintf(stderr, "unknown set: %s\n", tok); ok = 0; }
    }
    return ok;
}

static int deck_size_for(int mask)
{
    int i, n = 0;

    for (i = 0; deck_proto[i].name; i++)
        if (mask & (1 << deck_proto[i].set)) n += deck_proto[i].copies;
    return n;
}

/* How many Character cards a set brings, for the startup screen -- counted
 * rather than written down, so it cannot drift from the table. */
static int chars_in_set(int set)
{
    int i, n = 0;
    for (i = 0; i < char_count; i++)
        if ((int)char_tbl[i].set == set) n++;
    return n;
}

/* Some sets bring a whole board rather than just cards. */
static const char *board_of_set(int set)
{
    switch (set) {
    case SET_DUNGEON: return "and the Dungeon board";
    case SET_CITYD:   return "and the City board";
    case SET_TIMED:   return "and the Timescape board";
    default:          return "";
    }
}

static int spell_size_for(int mask)
{
    int i, n = 0;

    for (i = 0; spell_proto[i].name; i++)
        if (mask & (1 << spell_proto[i].set)) n += spell_proto[i].copies;
    return n;
}

/* Tick the expansions to shuffle in, the way the boxes intend. */
/* One page of the character roster.  Returns how many fit, and fills `keys`
 * with what the prompt should accept.  Split out of setup() so the game and
 * tools/screenshot.sh draw the identical screen -- a copy in the tool would
 * drift from the real one and report on a layout nobody plays. */
static int draw_char_page(const int *avail, int navail, int page, char *keys)
{
    /* Two rows each: numbers on one, what she actually does on the next.  A
     * wide terminal gets two columns, which halves the number of pages --
     * fifty-two characters is a lot of [space] otherwise.  The prompt row is
     * fixed, so height above it is what there is to work with. */
    int foot      = ui_prompt_row() - 2;
    int rows_used = (foot - 2) / 2;
    int twocol    = (COLS >= 150);
    int colw      = twocol ? (COLS - 4) / 2 : COLS - 4;
    int per, shown = 0, first, k;

    if (rows_used > 20) rows_used = 20;
    if (rows_used < 6)  rows_used = 6;
    per = twocol ? rows_used * 2 : rows_used;
    if (per > 40) per = 40;
    first = page * per;

    erase();
    mvprintw(0, 2, "Choose your character   (%d available)", navail);
    for (k = first; k < navail && shown < per; k++, shown++) {
        const CharTemplate *ct = &char_tbl[avail[k]];
        char key = (shown < 9) ? (char)('1' + shown) : (char)('a' + shown - 9);
        int  col = twocol ? (shown / rows_used) : 0;
        int  x   = 2 + col * colw;
        int  row = 2 + (shown % rows_used) * 2;
        keys[shown] = key;
        mvprintw(row, x, "[%c] %-13s S%d C%d L%d F%d G%d  %-7s  %s",
                 key, ct->cls, ct->str, ct->craft, ct->lives, ct->fate, ct->gold,
                 ct->align == AL_GOOD ? "Good" :
                 ct->align == AL_EVIL ? "Evil" : "Neutral",
                 ct->start);
        mvprintw(row + 1, x + 6, "%.*s", colw - 8, ct->power ? ct->power : "");
    }
    keys[shown] = 0;
    /* Footer pinned above the prompt row, not to the end of the list: a full
     * page used to push it off a short terminal entirely. */
    if (navail > per) {
        strncat(keys, " ", 40 - strlen(keys) - 1);
        mvprintw(foot, 2, "[space] more  (page %d of %d)",
                 page + 1, (navail + per - 1) / per);
    }
    mvprintw(foot + 1, 2, "Each begins on the space named on her card.");
    refresh();
    return per;
}

/* The drawing, split from the asking, so tools/screenshot.sh can render this
 * screen at any size without a human pressing a key at it. */
static void draw_sets_screen(void)
{
    int i, row;

    erase();
    attron(A_BOLD);
    mvprintw(0, 2, "Which expansions shall we shuffle in?");
    attroff(A_BOLD);
    mvprintw(1, 2, "Always in: %d Adventure cards, %d Spells, %d Characters "
                   "(the base fourteen and the",
             deck_size_for(1 << SET_BASE), spell_size_for(1 << SET_BASE),
             chars_in_set(SET_BASE));
    mvprintw(2, 2, "2e Expansion), and the Kingdom board: three rings and the Crown.");

    /* 1-9 then a, b, c...  An older formula gave every set past the ninth
     * the letter 'a', so the City and Timescape decks were printed with a
     * key that already belonged to the Harbinger. */
    row = 4;
    for (i = 1; i < SET_COUNT; i++, row++) {
        int one = 1 << i;
        char what[64];
        int  nc = deck_size_for(one), ns = spell_size_for(one), nh = chars_in_set(i);
        int  k  = 0;

        what[0] = 0;
        if (nc) k += snprintf(what + k, sizeof what - k, "%3d cards ", nc);
        else    k += snprintf(what + k, sizeof what - k, "          ");
        if (ns) k += snprintf(what + k, sizeof what - k, "%3d spells ", ns);
        else    k += snprintf(what + k, sizeof what - k, "           ");
        if (nh) snprintf(what + k, sizeof what - k, "%2d characters", nh);

        if (SET_ON(i)) attron(A_BOLD);
        mvprintw(row, 2, "[%c] %-16s %-38s %s",
                 SET_KEY(i), set_name[i], what, board_of_set(i));
        if (SET_ON(i)) { attroff(A_BOLD); mvprintw(row, 0, "->"); }
    }

    row++;
    {   /* char_count is the size of the table, not the number in play --
         * this line claimed 52 Characters with every expansion switched off. */
        int nch = 0;
        for (i = 0; i < char_count; i++)
            if (SET_ON(char_tbl[i].set)) nch++;
        attron(A_BOLD);
        mvprintw(row++, 2, "Shuffled in: %3d Adventure cards, %2d Spells, %2d Characters",
                 deck_size_for(enabled_sets), spell_size_for(enabled_sets), nch);
        attroff(A_BOLD);
    }

    row++;
    attron(A_BOLD);
    mvprintw(row++, 2, "Optional rules   (all of them want everyone to agree first)");
    attroff(A_BOLD);

    /* Two lines of description each, clipped to the window.  Curses wraps
     * rather than truncating, so a line one column too long does not
     * overflow the screen -- it silently reappears at the start of the next
     * one, which is how "nothing more" once became "nothing mo/re". */
    {
        static const struct { char key; const char *name; const char *a, *b; } modes[] = {
            { 'e', "Alternative Endings",
              "six cards; one lies face down on the Crown, and",
              "the first across the Valley of Fire turns it" },
            { 'h', "Henchmen",
              "a second Character each, dealt not chosen, who",
              "fights on his printed values and nothing more" },
            { 'x', "Chaos Bloodbath",
              "one Talisman card, not four; a Character who dies",
              "is out of the game. Short, and very bloody." },
        };
        int m, x = 33, w = COLS - x - 1;
        if (w < 10) w = 10;
        for (m = 0; m < 3; m++) {
            int on = (modes[m].key == 'e') ? alt_endings
                   : (modes[m].key == 'h') ? henchmen_on : chaos_on;
            if (on) { attron(A_BOLD); mvprintw(row, 0, "->"); }
            mvprintw(row,     2, "[%c] %-21s %s", modes[m].key, modes[m].name,
                     on ? " IN" : "out");
            mvprintw(row,     x, "%.*s", w, modes[m].a);
            mvprintw(row + 1, x, "%.*s", w, modes[m].b);
            if (on) attroff(A_BOLD);
            row += 2;
        }
    }

    mvprintw(ui_prompt_row() - 1, 2, "[*] everything    [space] start");
    refresh();
}

static void choose_sets(void)
{
    int i, c;

    for (;;) {
        draw_sets_screen();
        {
            char valid[SET_COUNT + 10];
            int  k = 0;
            for (i = 1; i < SET_COUNT; i++) valid[k++] = SET_KEY(i);
            valid[k++] = 'e'; valid[k++] = 'h'; valid[k++] = 'x';
            valid[k++] = '*'; valid[k++] = ' '; valid[k] = 0;
            c = ui_prompt("", valid);
        }
        if (c == ' ') return;
        /* Sets are matched first: an option key must never shadow one of
         * them.  [c] was doing exactly that to the Timescape deck. */
        {
            int hit = 0;
            for (i = 1; i < SET_COUNT; i++)
                if (SET_KEY(i) == c) { enabled_sets ^= 1 << i; hit = 1; break; }
            if (hit) continue;
        }
        if (c == 'e') { alt_endings = !alt_endings; continue; }
        if (c == 'h') { henchmen_on = !henchmen_on; continue; }
        if (c == 'x') { chaos_on    = !chaos_on;    continue; }
        if (c == '*') {
            for (i = 0; i < SET_COUNT; i++) enabled_sets |= 1 << i;
            alt_endings = 1;
            henchmen_on = 1;
            continue;
        }

    }
}

static void usage(const char *me)
{
    int i;

    printf("usage: %s [--watch] [--sets=LIST] [--log FILE]\n\n"
           "  -w, --watch   all four characters are played by the computer;\n"
           "                watch it, pause with space, +/- for speed, q to quit\n"
           "  --sets=LIST   comma-separated expansions to shuffle in, or 'all'.\n"
           "                Without this you are asked at startup.\n"
           "  --endings     shuffle in the six Alternative Ending cards\n"
           "  --henchmen    deal each Character a Henchman to fight for them\n"
           "  --log FILE    write a full text log of the game to FILE\n"
           "  -h, --help    this message\n\n"
           "sets:", me);
    for (i = 0; i < SET_COUNT; i++)
        printf("%s %s", (i && i % 4 == 0) ? "\n     " : "", set_key[i]);
    printf("\n\nenvironment: TALISMAN_DELAY (ms between AI actions), TALISMAN_SEED,\n"
           "             TALISMAN_TRACE (log to file), TALISMAN_AUTO (headless),\n"
           "             TALISMAN_SETS (same as --sets)\n");
}

int main(int argc, char **argv)
{
    int i, winner = -1;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--watch") || !strcmp(argv[i], "-w"))
            watching = 1;
        else if (!strncmp(argv[i], "--log=", 6)) {
            ui_set_log(argv[i] + 6);
        }
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            ui_set_log(argv[++i]);
        }
        else if (!strcmp(argv[i], "--endings")) {
            alt_endings = 1;
        }
        else if (!strcmp(argv[i], "--henchmen")) {
            henchmen_on = 1;
        }
        else if (!strcmp(argv[i], "--chaos")) {
            chaos_on = 1;
        }
        else if (!strncmp(argv[i], "--sets=", 7)) {
            if (!parse_sets(argv[i] + 7)) return 1;
            sets_chosen = 1;
        }
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    {
        const char *e;
        if ((e = getenv("TALISMAN_DELAY"))) ai_delay = atoi(e);
        if ((e = getenv("TALISMAN_AUTO")))  autoplay  = atoi(e);
        if ((e = getenv("TALISMAN_SETS")) && *e) { parse_sets(e); sets_chosen = 1; }
        if ((e = getenv("TALISMAN_ENDINGS")) && *e && *e != '0') alt_endings = 1;
        if ((e = getenv("TALISMAN_HENCH"))   && *e && *e != '0') henchmen_on = 1;
        if ((e = getenv("TALISMAN_CHAOS"))   && *e && *e != '0') chaos_on    = 1;
        if ((e = getenv("TALISMAN_LOOK")))    look_depth = atoi(e);
        if ((e = getenv("TALISMAN_LOOK_AB"))) look_ab    = atoi(e);
        if ((e = getenv("TALISMAN_ROAD")))    road_on    = atoi(e);
        if ((e = getenv("TALISMAN_ROAD_AB"))) road_ab    = atoi(e);
        if ((e = getenv("TALISMAN_CHAR")))    forced_char = atoi(e);
        game_seed = (e = getenv("TALISMAN_SEED")) ? (unsigned)atoi(e)
                                                 : (unsigned)(time(NULL) ^ talisman_getpid());
        srand(game_seed);
    }
    board_init();
    ui_init();

    if (LINES < 29 || COLS < 86) {
        int l = LINES, c = COLS;
        ui_end();
        fprintf(stderr, "This board needs a terminal of at least 86x29. Yours is %dx%d.\n", c, l);
        return 1;
    }

    /* TALISMAN_SCREEN=<name>: draw one screen at the current terminal size,
     * dump it through TALISMAN_SHOT, and exit.  See tools/screenshot.sh --
     * without this every layout claim is arithmetic, and arithmetic is what
     * put the character-list footer on the prompt row at thirty rows and at
     * no other height. */
    {
        const char *scr = getenv("TALISMAN_SCREEN");
        if (scr && *scr) {
            enabled_sets |= (1 << SET_BASE);
            deck_build(); sdeck_build(); stock_init();
            if (!strcmp(scr, "sets")) {
                draw_sets_screen();
                ui_snapshot();
            } else if (!strcmp(scr, "chars")) {
                nplayers = 4;
                {
                    int avail[64], navail = 0, i2;
                    char keys[40];
                    for (i2 = 0; i2 < char_count && navail < 64; i2++)
                        if (SET_ON(char_tbl[i2].set)) avail[navail++] = i2;
                    draw_char_page(avail, navail, 0, keys);
                    ui_snapshot();
                }
            } else {
                /* setup() asks how many are playing; autoplay answers for it
                 * so the tool never blocks on a key nobody is there to press. */
                autoplay = 1;
                nplayers = 4;
                setup();
                if (!strcmp(scr, "sheet"))      ui_sheet(&players[0]);
                else if (!strcmp(scr, "help"))  ui_shot_help();
                else                            ui_draw();
                ui_snapshot();
            }
            ui_end();
            return 0;
        }
    }

    if (!sets_chosen && !autoplay && !watching) choose_sets();
    deck_build();
    sdeck_build();
    stock_init();
    {
        char sets[256] = "";
        int  k;
        for (k = 0; k < SET_COUNT; k++)
            if (SET_ON(k)) {
                if (*sets) strncat(sets, ", ", sizeof sets - strlen(sets) - 1);
                strncat(sets, set_name[k], sizeof sets - strlen(sets) - 1);
            }
        glog("=== TALISMAN ===");
        glog("seed %u   (replay with TALISMAN_SEED=%u)", game_seed, game_seed);
        glog("sets: %s", sets);
        glog("Adventure deck: %d cards. Spell deck: %d.",
             deck_size_for(enabled_sets), spell_size_for(enabled_sets));
    }

    setup();
    if (autoplay) for (i = 0; i < nplayers; i++) players[i].ai = 1;

    for (;;) {
        Player *p = &players[cur_player];

        if (p->alive) take_turn(p);
        if (quit_flag) break;
        if (++turn_count > TURN_CAP) break;

        if (win_override >= 0) { winner = win_override; break; }
        if (alive_count() <= 1) {
            for (i = 0; i < nplayers; i++) if (players[i].alive) winner = i;
            break;
        }
        if (extra_turn) { extra_turn = 0; continue; }   /* Temporal Warp */
        do { cur_player = (cur_player + 1) % nplayers; }
        while (!players[cur_player].alive);
    }

    if (quit_flag) {
        glog("=== the quest is abandoned after %ld turns ===", turn_count);
        game_over("You abandon the quest. [press a key]");
    } else if (winner >= 0) {
        char buf[128];
        snprintf(buf, sizeof buf, "*** %s the %s %s.%s ***  [press a key]",
                 players[winner].name, players[winner].cls,
                 players[winner].region == REG_CROWN
                     ? "wears the Crown of Command"
                     : "is the last quester left alive",
                 watching ? "" : (winner == 0 ? " You win!" : " You lose."));
        glog("=== %s the %s %s, after %ld turns ===",
             players[winner].name, players[winner].cls,
             players[winner].region == REG_CROWN
                 ? "wears the Crown of Command"
                 : "is the last quester left alive",
             turn_count);
        cur_player = winner;
        game_over(buf);
    }
    ui_end();
    if (autoplay)
        printf("%s|%s|%s|%ld\n",
               winner >= 0 ? "WIN" : "CAP",
               winner >= 0 ? (players[winner].region == REG_CROWN ? "CROWN" : "SURVIVOR") : "-",
               winner >= 0 ? players[winner].cls : "-",
               turn_count);
    return 0;
}
