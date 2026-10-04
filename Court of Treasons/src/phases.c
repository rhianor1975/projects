/* The round, the turn, and the four places where the game is decided:
 * combat, the Council, a revolt, and the Throne.
 *
 * Every simultaneous decision in here is collected from all seats before
 * any is revealed to any seat.  That is a rule from the design --
 * "Simultaneous choices ... are collected and revealed together.  No turn
 * order advantage in a simultaneous decision" -- and it is also
 * networking requirement N3, which is why the collection is written out
 * longhand each time instead of being folded into the loop that applies
 * it.  Folding them together is exactly the bug the rule forbids.
 */
#include "court.h"
#include <string.h>
#include <stdlib.h>

/* The pieces rules.c owns. */
int  r_roll(Game *, int);
int  r_deck_draw(Game *, int);
void r_deck_discard(Game *, int, int);
void r_deck_build(Game *, int);
void r_deck_shuffle(Game *, int);
void r_hand_add(Game *, int, int);
void r_hand_remove(Game *, int, int);
void r_levy_add(Game *, int, int, int);
void r_standing_add(Game *, int, int, int);
void r_grievance_add(Game *, int, int);
void r_unrest_add(Game *, int, int, int);
int  r_unrest_total(const Game *, int);
int  r_lord_abstains(const Lord *);
void r_favour_add(Game *, int, int);
void r_check_death(Game *, int);
int  r_pick_own_loss(const Game *, int);
int  r_pick_enemy_loss(const Game *, int);

int  r_vassal_count(const Game *, int);
int  r_target_ok(const Game *, int, int, int);
int  r_promise_impossible(const Game *, int);
void r_muster(Game *, int);

/* Ask a seat, not the AI.  A null callback means nobody has taken this
 * seat, so the machine answers -- which is what makes a game with one
 * human seat a matter of setting one field. */
#define ASK(g, i, fn, ...) \
    ((g)->seat[i].fn ? (g)->seat[i].fn(&(g)->seat[i], __VA_ARGS__) : 0)

/* choose returns an Action rather than an int, so it cannot share the
 * macro: a seat with no choose passes, which is the one answer that is
 * always legal. */
static Action ask_choose(Game *g, int i, const View *v,
                         const Action *opts, int n)
{
    Action pass;
    if (g->seat[i].choose)
        return g->seat[i].choose(&g->seat[i], v, opts, n);
    memset(&pass, 0, sizeof pass);
    pass.kind = A_PASS; pass.seat = (signed char)i;
    pass.target = -1; pass.card = -1;
    return pass;
}
int  card_play(Game *, int, int, int);
int  card_affordable(const Game *, int, const Card *);
int  r_apply_effect(Game *, int, int, const Card *, int);
void card_play_world(Game *, int, int);
int  court_reputation(const Game *, int);
int  promise_new(Game *, int, int, int, int, int, int);
void promise_keep(Game *, int);
void promise_break(Game *, int);

static const int DECK_COST_RES[D_COUNT] = {
    -1, -1, -1, -1,            /* the House decks: free                  */
    -1, -1, -1, -1,            /* Lion, Ox, Wolf, Boar                    */
    R_MIL,                     /* War       -- and the fights in it       */
    R_CAP,                     /* Political -- and the Council, and Promises */
    R_GOLD,                    /* Intrigue  -- and lords, Levers, Instigators */
    R_CAP,                     /* Ambition, at 2                          */
    -1,                        /* World, free and immediate               */
    -1, -1, -1,                /* Court, Throne, the Dead: never chosen   */
    -1                         /* Cataclysm: drawn FROM, never chosen     */
};

/* ------------------------------------------------------------- setup -- */
void court_setup(Game *g, int nhouses, int epic)
{
    /* One row per House, not per seat: two chairs, four Houses. */
    static const signed char START[H_COUNT][R_COUNT] = {
        { 5, 2, 3 },   /* Ravenmark: +2 Military, -1 Capital             */
        { 2, 5, 3 },   /* Vipren:    +2 Capital,  -1 Military            */
        { 2, 3, 8 },   /* Goldwyn:   +5 Gold,     -1 Military            */
        { 5, 5, 5 },   /* Aldemar:   +2 in all three                     */
        /* The four added 2026-09-20.  Each is built around a subsystem
         * rather than a resource, so their Standing is nearer the base
         * of 3 than the first four's: what makes them different is what
         * they DO, not what they start holding. */
        { 3, 5, 3 },   /* Leoward:    +2 Capital -- the Word            */
        { 4, 3, 4 },   /* Stonegarth: +1 Mil +1 Gold -- the Yoke        */
        { 4, 4, 2 },   /* Wulfren:    +1 Mil +1 Cap, -1 Gold -- Grudge  */
        { 5, 2, 3 }    /* Everhold:   +2 Military, -1 Capital -- Beast  */
    };
    /* <open_questions> Q6: "Starting hand size and opening draw, which
     * cannot be chosen except by soak."  Five is a placeholder with a
     * name on it, not a decision. */
    const int OPENING_HAND = 5;
    int i, j, d, order[H_COUNT];
    unsigned long seed = g->rng;
    /* Carried across the memset, like the seed.  Setting it before the
     * call and reading it after is the obvious way to use this function
     * and it silently did nothing, because the function clears the Game
     * it is handed. */
    signed char wish = g->want_house;

    memset(g, 0, sizeof *g);
    /* Outside a turn there is no step, and court_actions must not
     * filter by one.  The memset above would otherwise leave ST_LEVY,
     * which allows nothing, so a World card resolving in PH_WORLD
     * would be refused by court_legal. */
    g->step = ST_COUNT;
    g->rng = seed;
    g->want_house = wish;
    g->nhouses = nhouses;
    g->epic = epic;
    g->paramount = -1;
    g->winner = -1;
    g->tie_break = -1;
    g->vote_nullified = -1;
    g->challenger = -1;
    g->first_paramount = -1;
    g->regent = -1;

    /* Which Houses are at the table is drawn from the seed.  The shuffle
     * runs over H_COUNT and not MAX_HOUSES: there are four Houses and two
     * chairs, and shuffling two entries of a four-entry list gave four
     * hundred games of Ravenmark against Vipren. */
    for (i = 0; i < H_COUNT; i++) order[i] = i;
    for (i = H_COUNT - 1; i > 0; i--) {
        j = r_roll(g, i + 1);
        d = order[i]; order[i] = order[j]; order[j] = d;
    }
    /* A seat may ask for a House.  It is pulled to the front of the
     * shuffled order rather than written straight in, so the other seat
     * still gets a House drawn from the seed and never the same one.
     *
     * It matters now in a way it did not: the Houses no longer hold the
     * same number of cards, so which one you are is a real difference
     * and not a colour. */
    if (g->want_house >= 0 && g->want_house < H_COUNT) {
        for (i = 0; i < H_COUNT; i++)
            if (order[i] == g->want_house) {
                d = order[0]; order[0] = order[i]; order[i] = d;
                break;
            }
    }

    for (d = 0; d < D_COUNT; d++) { r_deck_build(g, d); r_deck_shuffle(g, d); }

    for (i = 0; i < nhouses; i++) {
        House *h = &g->h[i];
        h->id = (unsigned char)order[i];
        h->alive = 1;
        for (j = 0; j < R_COUNT; j++) h->standing[j] = START[h->id][j];
        for (j = 0; j < AMBITIONS_DEALT; j++) h->ambition[j] = -1;

        /* "At setup each House is dealt 6 Ambitions, face down, known
         * only to itself."  <open_questions> Q2 asks whether the six come
         * from a shared deck or per House; with ten written and four
         * Houses wanting six each, per House is the only one that deals,
         * so that is what this does and Q2 stays open. */
        for (j = 0; j < AMBITIONS_DEALT; j++) {
            int c, tries = 0, k, dup;
            do {
                c = r_deck_draw(g, D_AMBITION);
                for (k = 0, dup = 0; k < j; k++)
                    if (h->ambition[k] == c) dup = 1;
            } while (dup && ++tries < 64);
            h->ambition[j] = (short)c;
            r_deck_discard(g, D_AMBITION, c);
        }
        for (j = 0; j < OPENING_HAND; j++)
            r_hand_add(g, i, r_deck_draw(g, (int)h->id));
    }


    /* The Court is always in play.  It was a fix for the two-player case
     * and the two-player case is the game. */
    g->last_lord = -1;
    for (i = 0; i < MAX_HOUSES; i++) court_seat_ai(&g->seat[i]);
    /* Four Minor Lords start unbound on the table, which is what makes a
     * first Council possible at all: a House with no lords has no voice. */
    for (i = 0; i < 4 && g->lord_n < MAX_LORDS; i++) {
        /* Minor Lords live inside the Intrigue deck since the merge, so
         * drawing from it gives whatever comes -- Poison as often as a
         * baron.  A lord has to be a lord, so the draw is repeated until
         * one is, and the drawn-and-rejected cards go to the discard
         * rather than being lost. */
        int c = -1, tries = 0;
        while (tries++ < 200) {
            int d = r_deck_draw(g, D_INTRIGUE);
            if (d < 0) break;
            if (CARDS[d].type == T_LORD) { c = d; break; }
            r_deck_discard(g, D_INTRIGUE, d);
        }
        if (c < 0) break;
        memset(&g->lord[g->lord_n], 0, sizeof g->lord[g->lord_n]);
        g->lord[g->lord_n].card = (short)c;
        g->lord[g->lord_n].trait = (unsigned char)CARDS[c].eff[0].a;
        g->lord[g->lord_n].holder = -1;
        g->lord[g->lord_n].alive = 1;
        if (CARDS[c].eff[0].a == LT_AMBITIOUS) g->lord[g->lord_n].double_rev = 1;
        if (CARDS[c].eff[0].a == LT_PROUD)     g->lord[g->lord_n].no_betray  = 1;
        g->lord_n++;
    }
    g->round = 0;
    court_log("setup: a duel, the Court in play, %d lords on the table%s",
              g->lord_n, epic ? ", Epic length" : "");
}

/* ------------------------------------------------------- the ambitions */
static int ambition_met(const Game *g, int s, int card)
{
    const Card *c = &CARDS[card];
    int i, n;
    switch (c->eff[0].op) {
    case OP_AMB_STANDING: return g->h[s].standing[c->eff[0].a] >= c->eff[0].b;
    case OP_AMB_VASSALS:  return r_vassal_count(g, s) >= c->eff[0].b;
    case OP_AMB_UNBROKEN: return g->round >= c->eff[0].b && g->h[s].broken_n == 0;
    case OP_AMB_KEPT:     return g->h[s].kept_n >= c->eff[0].b;
    case OP_AMB_COMBATS:  return g->h[s].combats_won >= c->eff[0].b;
    case OP_AMB_PARAMOUNT:return g->h[s].rounds_paramount >= c->eff[0].b;
    case OP_AMB_RIVAL_UNREST:
        for (i = 0; i < g->nhouses; i++)
            if (i != s && g->h[i].alive
             && r_unrest_total(g, i) >= c->eff[0].b) return 1;
        return 0;
    case OP_AMB_BEAT_OATHBREAKER:
        for (i = 0, n = 0; i < g->nhouses; i++)
            if (g->war[s][i] == 2 && g->h[i].broken_n >= c->eff[0].b) n = 1;
        return n;
    }
    return 0;
}

static void ambitions_check(Game *g, int s)
{
    int i, done = 0;
    int need = g->epic ? 4 : AMBITIONS_NEEDED;
    for (i = 0; i < AMBITIONS_DEALT; i++) {
        if (g->h[s].ambition[i] < 0) continue;
        /* "A completed Ambition cannot be un-completed by later events",
         * so this only ever sets the bit and never clears it. */
        if (!g->h[s].amb_done[i] && ambition_met(g, s, g->h[s].ambition[i])) {
            g->h[s].amb_done[i] = 1;
            court_log("  %s completes an Ambition: %s",
                      HOUSE_NAME[g->h[s].id], CARDS[g->h[s].ambition[i]].name);
        }
        done += g->h[s].amb_done[i];
    }
    if (done >= need && !g->h[s].throneworthy) {
        g->h[s].throneworthy = 1;
        court_log("  %s is Throneworthy", HOUSE_NAME[g->h[s].id]);
    }
}

/* ------------------------------------------------------------- combat  */

/* A House's whole Standing, across the three resources.  Combat may
 * grind it down to the last point and may not take that: death is a
 * thing the other roads do, not a thing an unanswerable loop does. */
static int total_standing(const Game *g, int s)
{
    int r, t = 0;
    for (r = 0; r < R_COUNT; r++) t += g->h[s].standing[r];
    return t;
}

/* "Each side commits Military Levy secretly, then reveals.  Each may
 * then play one Combat card."  The second sentence was never built.
 *
 * Everything that punishes a House for being weak at arms was:
 * the loser drops 1 Military Standing, a House at 0 Military "cannot
 * commit and automatically loses any combat it is forced into",
 * Ravenmark declares Open War free, Vipren pays 4 to declare and double
 * for Combat cards.  The one clause that let a House answer an attack
 * it could not out-muster was the one missing, so the penalties ran
 * with no brake: measured as Ravenmark beating Vipren 83-0 and Goldwyn
 * 55-0, every game decided by the starting Military Standing in
 * court_setup's four-row table and nothing that happened afterwards.
 *
 * The cards were all there and unreachable -- Turn the Guard (4G,
 * reduce an attacker's committed Military by 3), Mercenaries (4G, +4),
 * Defend the Realm (2M, +3 when defending).  They could only be played
 * on your own turn, which is the one moment a combat is not happening.
 *
 * It is asked through `choose`, the same callback a turn uses, so the
 * wire, the client and the AI need no new question: a combat card is a
 * short list of A_PLAY and an A_PASS.
 *
 * Two limits, both deliberate:
 *  - A card that declares war is not offered.  Playing one inside a
 *    combat would start a combat, and the recursion has no floor.
 *  - The commits are not in the View, so the choice is made without
 *    seeing the reveal that the rule says precedes it.  Widening the
 *    View is a permanent hole for a fact that lives one moment, so the
 *    fidelity is bought later or not at all. */
static void combat_card(Game *g, int s, int defending)
{
    Action opts[64], pick;
    View v;
    int i, n = 0;

    if (!g->h[s].alive) return;
    for (i = 0; i < g->h[s].hand_n && n < 63; i++) {
        const Card *c = &CARDS[g->h[s].hand[i]];
        /* A Combat REACTION, not any card filed under Combat.  The
         * category also holds "Play on turn" cards -- The Standing Army
         * buys a permanent Standing, The Iron Crown gives Capital Levy
         * -- and those are turn actions that happen to be martial, not
         * answers to a fight.  Reaction is the type the design gives
         * the cards it means: "+3 Military Levy in one combat, this
         * combat only", "Reduce an attacker's committed Military Levy
         * by 3". */
        if (c->category != C_COMBAT || c->type != T_REACTION) continue;
        if (c->eff[0].op == OP_DECLARE_WAR || c->eff[1].op == OP_DECLARE_WAR)
            continue;
        /* Cards that only do anything for a defender are only offered to
         * one.  Both opcodes land in combat_bonus_def[], which is read
         * for the defender and not for the attacker, so offering them to
         * an attacker is offering a card that is guaranteed to do
         * nothing -- and the machine, given a legal option, takes it. */
        if (!defending && (c->eff[0].op == OP_COMBAT_REDUCE
                        || c->eff[0].op == OP_COMBAT_LEVY_DEF)) continue;
        if (!card_affordable(g, s, c)) continue;
        opts[n].kind = A_PLAY;  opts[n].seat = (signed char)s;
        opts[n].target = -1;    opts[n].card = g->h[s].hand[i];
        opts[n].a = opts[n].b = opts[n].c = 0;
        n++;
    }
    if (!n) return;
    opts[n].kind = A_PASS;  opts[n].seat = (signed char)s;
    opts[n].target = -1;    opts[n].card = -1;
    opts[n].a = opts[n].b = opts[n].c = 0;
    n++;

    court_view(g, s, &v);
    pick = ask_choose(g, s, &v, opts, n);
    if (pick.kind == A_PLAY && pick.card >= 0)
        card_play(g, s, pick.card, -1);
}
/* "Each side commits Military Levy secretly, then reveals."  Both sides
 * choose from a View before either number is looked at, which is the only
 * way this is a commitment and not an auction. */
static int combat(Game *g, int att, int def)
{
    View va, vd;
    int a_commit, d_commit, i, winner, loser;

    /* The defender musters.  Both sides are brought to their Standing
     * before either is asked what it commits, so the choice is still made
     * blind, which is the rule this is protecting. */
    if (def != g->seat_turn) r_muster(g, def);
    if (att != g->seat_turn) r_muster(g, att);

    /* Promises of type Aid come due when the promisee is attacked. */
    for (i = 0; i < g->promise_n; i++) {
        Promise *p = &g->promise[i];
        if (p->state != P_LIVE || !p->due_on_attack || p->promisee != def)
            continue;
        court_view(g, p->promiser, &va);
        if (ASK(g, p->promiser, keep_promise, &va, i)) {
            promise_keep(g, i);
            r_muster(g, p->promiser);     /* an ally musters to answer    */
            g->combat_bonus_def[def] += g->h[p->promiser].levy[R_MIL];
            r_levy_add(g, p->promiser, R_MIL, -g->h[p->promiser].levy[R_MIL]);
        } else {
            promise_break(g, i);
        }
    }

    court_view(g, att, &va);
    court_view(g, def, &vd);
    a_commit = ASK(g, att, commit, &va, g->h[att].levy[R_MIL], 0);
    d_commit = ASK(g, def, commit, &vd, g->h[def].levy[R_MIL], 1);

    /* "A House reduced to 0 Military Standing cannot commit and
     * automatically loses any combat it is forced into." */
    if (g->h[att].standing[R_MIL] == 0) a_commit = 0;
    if (g->h[def].standing[R_MIL] == 0) d_commit = 0;

    r_levy_add(g, att, R_MIL, -a_commit);     /* spent either way         */
    r_levy_add(g, def, R_MIL, -d_commit);

    /* "...then reveals.  Each may then play one Combat card."  The
     * defender is asked first: the attacker chose this fight and the
     * answer to it is the thing being restored. */
    combat_card(g, def, 1);
    combat_card(g, att, 0);

    a_commit += g->combat_bonus[att];
    d_commit += g->combat_bonus[def] + g->combat_bonus_def[def];
    if (a_commit < 0) a_commit = 0;
    if (d_commit < 0) d_commit = 0;

    /* "Higher committed total wins.  Ravenmark wins ties; otherwise the
     * defender wins ties." */
    if (a_commit > d_commit) winner = att;
    else if (d_commit > a_commit) winner = def;
    else winner = (g->h[att].id == H_RAVENMARK) ? att : def;
    loser = winner == att ? def : att;

    court_log("  combat: %s %d vs %s %d -- %s wins",
              HOUSE_NAME[g->h[att].id], a_commit,
              HOUSE_NAME[g->h[def].id], d_commit, HOUSE_NAME[g->h[winner].id]);

    if (loser == att || total_standing(g, loser) > 1)
        r_standing_add(g, loser, R_MIL, -1);
    g->h[winner].combats_won++;
    g->war[winner][loser] = 2;                /* 2: beaten, this round    */

    /* "The winner takes 1 Standing of their choice from the loser, OR 1
     * Unrest into a resource of their choosing, or forces the discard of
     * two cards."  Three choices joined by "or".  This took both: it
     * moved a Standing AND added an Unrest, so a lost combat cost the
     * loser three things (1 Military Standing, 1 Standing of the
     * winner's pick, 1 Unrest) where the design prices two.
     *
     * That is what made losing terminal rather than expensive.  Levy is
     * Standing minus Unrest, so the doubled spoil took a point off the
     * Levy twice over, and a House that lost one combat could not commit
     * to the next one -- measured as Ravenmark beating Vipren 83-0 and
     * Goldwyn 55-0 with no game in either, and as three card-level
     * hypotheses that all measured as noise because no card matters to a
     * House that is dead on round 7.
     *
     * Which of the three the winner takes is the winner's decision and
     * is NOT yet asked of the seat -- that needs a vtable entry and the
     * wire to carry it.  The machine picks below; a person does not get
     * the choice yet.  Advancing Servitude is not among the options: a
     * Bond runs to a lord, and beating a House does not hand you one. */
    /* No spoils from the defenceless.  A House at 0 Military Standing
     * "cannot commit and automatically loses any combat it is forced
     * into" -- the design says so, and says nothing about what then
     * stops the winner attacking it every round until it is dead.
     * Nothing did.  Ravenmark declares Open War free once a turn, every
     * turn; each win took 2 Standing; at 0 Military the loser cannot
     * contest and cannot get back, because every card restoring
     * Military Standing costs either 4M -- which a House at 0 Military
     * cannot have -- or 4G, above Vipren's Gold Standing of 3, which
     * only falls.  Ten Standing draining at two a round is dead by
     * round 7 with no line of play that avoids it, measured as
     * Ravenmark 83-0 over Vipren and 55-0 over Goldwyn.
     *
     * So the auto-loss stands and the spoil does not.  Beating a House
     * that cannot fight back still wins the combat, still counts toward
     * Blood Drawn, still holds it at 0 Military for the rest of the
     * game -- Ravenmark can cripple a House permanently and lock it out
     * of war entirely.  What it can no longer do is execute one.  A
     * militarily broken House stays crippled and has to win by Favour,
     * the Final King or its Ambitions, which are three roads the design
     * already built and which a dead House cannot walk.
     * VARIANT B, under measurement: the spoil is NOT withheld, only
     * the killing blow is.  Withholding the spoil entirely was tried
     * first and inverted the game -- Ravenmark fell from 68% to 3.5%,
     * because the Standing it takes from the loser IS its economy (16.4
     * end-state Standing in its wins, nearly all of it stolen) and 102
     * of its 138 wins were The Last House.  Cripple-but-not-execute has
     * to leave both of those standing. */
    if (!g->no_spoil && (loser == att || total_standing(g, loser) > 1)) {
        int r = r_pick_enemy_loss(g, loser);
        /* Taking the Standing is the harder blow and is taken while it
         * is available; Unrest is the fallback when there is nothing
         * left to take, which is also when Unrest still bites. */
        if (g->h[loser].standing[r] > 0) {
            r_standing_add(g, loser, r, -1);
            r_standing_add(g, winner, r, 1);
        } else {
            r_unrest_add(g, loser, r, 1);
        }
    }
    r_check_death(g, loser);
    g->no_spoil = 0;
    g->combat_bonus[att] = g->combat_bonus[def] = 0;
    g->combat_bonus_def[att] = g->combat_bonus_def[def] = 0;
    return winner;
}

/* ------------------------------------------------------------- council */
/* The Council is the lords.  A Council of two Houses is an argument; a
 * Council of the Minor Lords in play is a real one, and everything it
 * needs is already on the table.
 *
 * Returns the seat elected Lord Paramount, or -1 if neither was. */
static int council(Game *g)
{
    int tally[MAX_HOUSES], i, top, best = -1, bought[MAX_HOUSES];
    View v;

    memset(tally, 0, sizeof tally);
    memset(bought, 0, sizeof bought);
    court_log("  the Council sits");

    /* Promises of type Vote and Abstention come due here, and bind how a
     * House directs its lords.  Decided before any vote is counted. */
    for (i = 0; i < g->promise_n; i++) {
        Promise *p = &g->promise[i];
        if (p->state != P_LIVE || !p->due_at_council) continue;
        court_view(g, p->promiser, &v);
        if (ASK(g, p->promiser, keep_promise, &v, i)) promise_keep(g, i);
        else                                          promise_break(g, i);
    }

    for (i = 0; i < g->lord_n; i++) {
        Lord *l = &g->lord[i];
        int n, who;
        if (!l->alive || !l->revealed || l->holder < 0) continue;
        /* "A lord whose Revolution has passed its Servitude abstains."
         * Neither House is told the counts; they are told who did not
         * vote, which is the whole of how a hidden track resolves in
         * public. */
        if (r_lord_abstains(l)) {
            court_log("    %s does not raise its hand", CARDS[l->card].name);
            continue;
        }
        who = l->holder;
        if (g->directed[i]) who = g->directed[i] - 1;   /* Everyone Has a Price */
        if (g->free_votes) who = l->holder;             /* The Old Rites */
        /* "The dead ... do not vote." */
        if (!g->h[who].alive) {
            court_log("    %s serves nobody now", CARDS[l->card].name);
            continue;
        }
        if (who == g->vote_nullified) {
            court_log("    %s is kept from the chamber", CARDS[l->card].name);
            continue;
        }
        n = (l->trait == LT_AMBITIOUS) ? 2 : 1;
        if (g->h[who].id == H_ALDEMAR) n++;             /* the Legitimist */
        /* "Your vote counts twice" reaches the lords, not the House:
         * the House does not vote at all. */
        if (g->vote_double[who]) n++;
        if (g->council_mod == CM_OATH_READ && g->h[who].broken_n) n--;
        if (n > 0) tally[who] += n;
    }

    /* "Goldwyn may buy votes at 2 Gold Levy each, declared before the
     * reveal." */
    if (g->council_mod != CM_SEALED)
        for (i = 0; i < g->nhouses; i++) {
            if (!g->h[i].alive || g->h[i].id != H_GOLDWYN) continue;
            while (g->h[i].levy[R_GOLD] >= 2 && bought[i] < 2) {
                r_levy_add(g, i, R_GOLD, -2); tally[i]++; bought[i]++;
            }
        }
    for (i = 0; i < g->nhouses; i++) tally[i] += g->extra_votes[i];

    /* The Court holds one vote and casts it for the higher Favour,
     * abstaining below +3 -- because Favour starts at nothing and a
     * single purchase would otherwise sell the Council for three Gold. */
    if (!g->court_silent && g->nhouses >= 2) {
        int a = g->h[0].favour, b = g->h[1].favour;
        if (a >= FAVOUR_BOUGHT_MAX && a > b)      tally[0]++;
        else if (b >= FAVOUR_BOUGHT_MAX && b > a) tally[1]++;
    }

    top = 0;
    for (i = 1; i < g->nhouses; i++) if (tally[i] > tally[top]) top = i;
    /* "Choose who wins a tied vote."  A duel ties often -- two Houses
     * and a handful of lords -- so a card that settles one is worth
     * more here than at a table of four. */
    if (g->tie_break >= 0 && g->tie_break < g->nhouses
     && tally[g->tie_break] == tally[top]) {
        top = g->tie_break;
        court_log("    the tie falls to %s", HOUSE_NAME[g->h[top].id]);
    }
    for (i = 0; i < g->nhouses; i++)
        court_log("    %s: %d vote%s", HOUSE_NAME[g->h[i].id], tally[i],
                  tally[i] == 1 ? "" : "s");

    /* A minimum of votes to be elected at all.  Without one the
     * Paramountcy went to whoever held a single lord more than nobody,
     * which made it a consolation prize rather than a thing to play for
     * -- and it handed the tax to a House that had done nothing to earn
     * it.  Below the minimum the Council elects nobody and nobody is
     * taxed that round. */
    if (tally[top] >= g->council_quorum) {
        int tie = 0;
        for (i = 0; i < g->nhouses; i++)
            if (i != top && tally[i] == tally[top]) tie = 1;
        if (!tie) best = top;
        else if (g->paramount >= 0) best = g->paramount;   /* incumbent holds */
    }

    g->council_pending = 0;
    g->council_mod = CM_NONE;
    g->free_votes = 0;
    g->tie_break = -1;
    g->vote_nullified = -1;
    memset(g->vote_double, 0, sizeof g->vote_double);
    memset(g->shield, 0, sizeof g->shield);
    g->tax_double = 0;
    memset(g->directed, 0, sizeof g->directed);
    memset(g->extra_votes, 0, sizeof g->extra_votes);
    return best;
}

/* -------------------------------------------------------------- throne */
static void throne_challenge(Game *g, int s)
{
    int i, field = 0, mine;
    View v;

    court_log("  %s challenges for the Throne", HOUSE_NAME[g->h[s].id]);
    /* "Every other living House may commit Military Levy against the
     * challenger, together."  At Consequence nobody is on turn, so the
     * realm musters -- challenger included, and before any commitment is
     * asked for. */
    for (i = 0; i < g->nhouses; i++) if (g->h[i].alive) r_muster(g, i);
    court_view(g, s, &v);
    mine = ASK(g, s, commit, &v, g->h[s].levy[R_MIL], 0) + g->throne_bonus[s];
    r_levy_add(g, s, R_MIL, -g->h[s].levy[R_MIL]);

    /* "Every other living House may commit Military Levy against the
     * challenger, together."  Collected before any is revealed. */
    for (i = 0; i < g->nhouses; i++) {
        int c;
        if (i == s || !g->h[i].alive) continue;
        if (g->throne_bonus[i] <= -100) continue;   /* Acclaim bars them  */
        court_view(g, i, &v);
        c = ASK(g, i, commit, &v, g->h[i].levy[R_MIL], 1) + g->throne_bonus[i];
        if (c < 0) c = 0;
        r_levy_add(g, i, R_MIL, -g->h[i].levy[R_MIL]);
        field += c;
    }

    court_log("    challenger %d, the field %d", mine, field);
    if (mine > field) {                   /* "must exceed the field's total" */
        g->winner = (signed char)s;
        g->win_reason = "Claim by Force";
        return;
    }
    /* "Failing costs the challenger 3 Military Standing and 2 Grievance
     * to every other House, and they may not challenge again next round."*/
    r_standing_add(g, s, R_MIL, -3);
    for (i = 0; i < g->nhouses; i++)
        if (i != s && g->h[i].alive) r_grievance_add(g, i, 2);
    g->h[s].no_challenge_until = (unsigned char)(g->round + 2);
    r_check_death(g, s);
    court_log("    the challenge fails");
}

/* "A challenger may instead pay the Throne's price: 12 Gold Standing and
 * 8 Capital Standing, which no House will ever quite have, and which
 * exists so that Goldwyn has a road nobody else has." */
/* The Gold road.  It has existed since the seed set and has never once
 * been walked: 12 Gold Standing and 8 Capital is at least 20 between
 * two resources, and measured over 300 games Goldwyn's TOTAL Standing
 * across all three averages 4.2 and peaks at 11.  Standing in this game
 * mostly goes down -- it is what a lost combat takes -- so a threshold
 * above where a House starts is not a hard road, it is off the map.
 *
 * Those two numbers came from the four-player board game's economy.
 * They are priced here against this one, by sweep, and the thresholds
 * are readable so the next sweep does not need a recompile. */
static int purchased_throne(const Game *g, int s)
{
    int gold = g->throne_gold - g->h[s].throne_price[R_GOLD];
    int cap  = g->throne_cap  - g->h[s].throne_price[R_CAP];
    if (!g->h[s].throneworthy) return 0;
    return g->h[s].standing[R_GOLD] >= gold && g->h[s].standing[R_CAP] >= cap;
}

/* -------------------------------------------------------------- revolt */
/* A lord turns.  There are no uninvolved Houses to take sides, so what
 * was a four-way simultaneous choice becomes what it always was
 * underneath: a lord deciding it has had enough, and going. */
static void lord_turns(Game *g, int li)
{
    Lord *l = &g->lord[li];
    int who = l->holder, other = who ? 0 : 1;
    int kind = l->revolution >= 5 ? 3 : l->revolution >= 4 ? 2 : 1;
    if (kind == 3 && l->no_betray) kind = 2;

    court_log("  %s leaves %s (%s)", CARDS[l->card].name,
              who >= 0 ? HOUSE_NAME[g->h[who].id] : "nobody",
              kind == 1 ? "departure" : kind == 2 ? "defection" : "betrayal");

    l->servitude = 0; l->revolution = 0; l->revealed = 0;
    l->lord_knows = 0; l->frozen = 0;
    if (kind == 1) {
        l->holder = -1;                       /* Departure: back to the table */
    } else {
        l->holder = (signed char)other;       /* Defection                    */
        l->servitude = 1;
        if (kind == 3) {                      /* Betrayal                     */
            l->servitude = 2;
            if (who >= 0) {
                r_standing_add(g, who, R_CAP, -2);
                r_check_death(g, who);
            }
        }
    }
}

/* ------------------------------------------------------- legal actions */
static int deck_affordable(const Game *g, int s, int d, int big)
{
    int r = DECK_COST_RES[d];
    int cost;
    if (d == D_WORLD) return !big;
    if (d == D_AMBITION) {
        int i, open = 0;
        for (i = 0; i < AMBITIONS_DEALT; i++)
            if (g->h[s].ambition[i] >= 0 && !g->h[s].amb_done[i]) open = 1;
        if (!open) return 0;
        return !big && g->h[s].levy[R_CAP] >= 2;
    }
    if (d == D_THRONE) return g->h[s].throneworthy && !big;
    /* A gap in the design, filled here and flagged rather than papered
     * over.  <deck_access> names House, the three shared, World, Ambition,
     * Throne and Ghost, and no others -- but the Promise deck's own note
     * says its cards "are drawn like anything else", and the Lever deck is
     * the reason Servitude can be advanced at all.  Between them that is
     * 70 cards of the 100 the design says are new, with no way into a
     * hand.  Without access here the promise system is unmeasurable: a
     * soak made 2.96 promises a game, all of them from the five Promise
     * cards that happen to sit in House decks.
     *
     * So: Promise costs 1 Capital, Lever costs 1 Gold, by analogy with
     * the Political and Intrigue decks they most resemble.  This is an
     * engine decision standing in for a design one, and it belongs in
     * <deck_access> and <open_questions> before it is trusted. */
    if (d == D_COURT || d == D_DEAD) return 0;
    if (r < 0) return (int)d == (int)g->h[s].id && !big;   /* your own hundred */
    cost = big ? 3 : 1;
    /* "Ravenmark ... pays 1 less for War deck access." */
    if (g->h[s].id == H_RAVENMARK && d == D_WAR) cost--;
    return g->h[s].levy[r] >= cost;
}

/* Does this card do nothing except inside a combat?  Those are offered
 * by combat_card when a fight is actually happening, and nowhere else. */
static int combat_only(const Card *c)
{
    int e, seen = 0;
    for (e = 0; e < 2; e++) {
        int op = c->eff[e].op;
        if (op == OP_NONE) continue;
        seen = 1;
        if (op != OP_COMBAT_LEVY && op != OP_COMBAT_LEVY_DEF
         && op != OP_COMBAT_REDUCE && op != OP_COMBAT_ATT_LOSS
         && op != OP_COMBAT_LEVY_IF) return 0;
    }
    return seen;
}

/* design.xml <turn_structure><sequence>: which step allows which action.
 *
 * Only the seat's own free choices are placed.  Everything else -- a
 * Reaction, a forced discard, a response -- is reactive or compelled,
 * is not part of the sequence, and is allowed wherever it arises.
 *
 * Bond and Instigator are First Court only.  That is the one thing the
 * sequence takes away that the old open loop allowed: you cannot send
 * an Instigator after seeing how the war went. */
static int step_allows(int step, int kind)
{
    switch (kind) {
    case A_DRAW:             return step == ST_DRAW;
    case A_BOND:
    case A_INSTIGATE:        return step == ST_COURT1;
    case A_PLAY:
    case A_PROPOSE:
    case A_SPEND_GRIEVANCE:
    case A_BUY_FAVOUR:       return step == ST_COURT1 || step == ST_COURT2;
    case A_DECLARE_WAR:
    case A_CHALLENGE:
    case A_REVEAL_BOND:      return step == ST_DECLARE;
    default:                 return 1;
    }
}

int court_actions(const Game *g, int seat, Action *out, int max)
{
    int n = 0, i, d;
    const House *h = &g->h[seat];
    Action a;

#define PUSH(k, tg, cd, x, y, z) do { \
        if (n < max) { memset(&a, 0, sizeof a); a.kind = (k); \
            a.seat = (signed char)seat; a.target = (signed char)(tg); \
            a.card = (short)(cd); a.a = (signed char)(x); \
            a.b = (signed char)(y); a.c = (signed char)(z); out[n++] = a; } \
    } while (0)

    if (!h->alive) { PUSH(A_PASS, -1, -1, 0, 0, 0); return n; }

    if (g->draws_left)
        for (d = 0; d < D_COUNT; d++) {
            if (deck_affordable(g, seat, d, 0)) PUSH(A_DRAW, -1, -1, d, 0, 0);
            if (deck_affordable(g, seat, d, 1)) PUSH(A_DRAW, -1, -1, d, 1, 0);
        }

    for (i = 0; i < h->hand_n; i++) {
        int card = h->hand[i];
        const Card *c = &CARDS[card];
        if (c->type == T_AMBITION) continue;
        if (c->type == T_LEVER) continue;        /* levers go through A_BOND */
        /* A Reaction whose whole effect happens inside a combat is
         * offered by combat_card, at the moment it can do something.
         * Offering it here as well is offering a card that is
         * guaranteed to be wasted: declaring resolves the fight at
         * once, so a Military bonus played on your own turn has no
         * fight to apply to.  Measured over 120 games before this,
         * those went 34 played uselessly against 1 played in time.
         *
         * The machine, given a legal option, takes it -- so the only
         * way to stop the waste is to stop offering it.  A turn
         * sequence was tried for this and removed; it made the player
         * track five steps to prevent a mistake that four lines can
         * make impossible. */
        if (c->type == T_REACTION && combat_only(c)) continue;
        if (!card_affordable(g, seat, c)) continue;
        if (c->type == T_PROMISE) {
            int t;
            /* "Target cannot form alliances for 3 rounds." */
            if (g->round < g->no_promise_until[seat]) continue;
            for (t = 0; t < g->nhouses; t++)
                if (t != seat && g->h[t].alive)
                    PUSH(A_PROPOSE, t, card, c->eff[0].a, c->eff[0].b, 0);
            continue;
        }
        if (c->eff[0].op == OP_STANDING_LOSS || c->eff[0].op == OP_LEVY_STEAL
         || c->eff[0].op == OP_UNREST_TARGET || c->eff[0].op == OP_SERVITUDE
         || c->eff[0].op == OP_DECLARE_WAR   || c->eff[0].op == OP_PEEK_HAND
         || c->eff[0].op == OP_BROKEN_TRUCE  || c->eff[0].op == OP_RUMOUR
         || c->eff[0].op == OP_VOTE_COMMAND  || c->eff[0].op == OP_EXPOSE
         || c->eff[0].op == OP_PROMISE_COERCE|| c->eff[0].op == OP_BROKEN_TRANSFER
         || c->eff[0].op == OP_THRONE_BAR
         /* Added with the pool conversion.  An effect that needs a
          * target and is not named here is offered with target -1,
          * fails its own guard and resolves to nothing -- which shows
          * up as an inert play and not as a missing line. */
         || c->eff[0].op == OP_DISCARD_TARGET
         || c->eff[0].op == OP_SKIP_ACTION
         || c->eff[0].op == OP_LEVY_GIVE
         || c->eff[0].op == OP_NO_PROMISE
         || c->eff[0].op == OP_VOTE_NULLIFY
         || c->eff[0].op == OP_CANCEL_NEXT
         || c->eff[0].op == OP_STEAL_CARD
         /* Opens with "if (t < 0) return 0" and was not on this list,
          * so all four cards using it resolved to nothing. */
         || c->eff[0].op == OP_GRIEVANCE_TARGET
         || c->eff[0].op == OP_GIVE_CARD) {
            int t;
            for (t = 0; t < g->nhouses; t++)
                if (t != seat && g->h[t].alive
                 && r_target_ok(g, seat, t, c->eff[0].cond))
                    PUSH(A_PLAY, t, card, 0, 0, 0);
        } else {
            PUSH(A_PLAY, -1, card, 0, 0, 0);
        }
    }

    /* "Build Bond, once: advance Servitude against one House, if you hold
     * a lever."  The lever is the card; the action is the once. */
    if (!g->bond_used)
        for (i = 0; i < h->hand_n; i++) {
            const Card *c = &CARDS[h->hand[i]];
            if (c->type != T_LEVER) continue;
            if (!card_affordable(g, seat, c)) continue;
            /* A Lever reaches a lord by Trait, and the effect finds it.
             * The action only has to say which Lever. */
            PUSH(A_BOND, -1, h->hand[i], 0, 0, 0);
        }

    /* "Ravenmark cannot send Instigators.  Vipren sends two per turn."
     * An Instigator now goes at a lord, at a named resource, or at the
     * Court -- the three things two players leave to aim at. */
    if (g->instigators_left && h->id != H_RAVENMARK) {
        int other = seat ? 0 : 1;
        for (i = 0; i < g->lord_n; i++) {
            if (!g->lord[i].alive || g->lord[i].holder < 0) continue;
            /* Aldemar "cannot send Instigators against an unbound lord". */
            if (h->id == H_ALDEMAR && g->lord[i].holder < 0) continue;
            PUSH(A_INSTIGATE, i, -1, 1, 0, 0);
        }
        if (g->h[other].alive) {
            int r;
            for (r = 0; r < R_COUNT; r++)
                PUSH(A_INSTIGATE, other, -1, 3 + r, 0, 0);
            PUSH(A_INSTIGATE, other, -1, 2, 0, 0);   /* at the Court      */
        }
    }

    /* Revealing a lord puts it on the Council, which is the only reason
     * to do it and the only reason Servitude is worth building. */
    if (!g->declare_used)
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder == seat
             && g->lord[i].servitude >= SERVITUDE_MAX
             && !g->lord[i].revealed)
                PUSH(A_REVEAL_BOND, i, -1, 0, 0, 0);

    if (!g->declare_used) {
        /* "Declaring Open War costs 2 Grievance, except for Ravenmark,
         * who declares free.  Vipren cannot declare without paying 4." */
        int cost = h->id == H_RAVENMARK ? 0 : h->id == H_VIPREN ? 4 : 2;
        /* The King's Peace and its kin stop the declaration, not the
         * fight: a combat already under way resolves. */
        if (g->round < g->no_combat_until) { /* nobody declares */ }
        else if (h->grievance >= cost && h->standing[R_MIL] > 0)
            for (i = 0; i < g->nhouses; i++)
                if (i != seat && g->h[i].alive)
                    PUSH(A_DECLARE_WAR, i, -1, cost, 0, 0);
        if (h->throneworthy && g->round >= h->no_challenge_until
         && g->challenger < 0)
            PUSH(A_CHALLENGE, -1, -1, 0, 0, 0);
    }

    /* The Grievance sinks.  They are built first this time, "rather than
     * declared and left unbuilt, because next door it sat pinned at its
     * maximum in every single game". */
    if (h->grievance >= 5 && g->paramount >= 0 && g->paramount != seat
     && h->id != H_ALDEMAR)
        PUSH(A_SPEND_GRIEVANCE, g->paramount, -1, 5, 0, 0);
    if (h->grievance >= 6 && !g->council_pending)
        PUSH(A_SPEND_GRIEVANCE, -1, -1, 6, 0, 0);
    if (h->grievance >= 4)
        for (d = 0; d < D_COUNT; d++) {
            const Deck *k = &g->deck[d];
            for (i = 0; i < k->discard_n; i++)
                if (!strcmp(CARDS[k->discard[i]].name, "Rebellion")) {
                    PUSH(A_SPEND_GRIEVANCE, -1, k->discard[i], 4, 0, 0);
                    break;
                }
        }

    /* "Favour may be bought at 3 Gold Levy per point, once per round, to
     * a maximum of +3 by purchase.  The last two points cannot be bought."*/
    if (!g->favour_locked && !h->favour_bought
     && h->levy[R_GOLD] >= 3 && h->favour < FAVOUR_BOUGHT_MAX)
        /* The Boar has nothing the Court wants. */
        if (h->id != H_EVERHOLD) PUSH(A_BUY_FAVOUR, -1, -1, 0, 0, 0);

    /* The sequence, applied last so it governs court_legal too --
     * court_apply trusts court_legal, so a step that does not offer an
     * action also refuses it.  Pass is added after the filter: it is
     * the one thing every step must always offer. */
    if (g->step < ST_COUNT) {
        int w = 0, q;
        for (q = 0; q < n; q++)
            if (step_allows(g->step, out[q].kind)) out[w++] = out[q];
        n = w;
    }
    PUSH(A_PASS, -1, -1, 0, 0, 0);
#undef PUSH
    return n;
}

/* A client proposes; it never asserts.  court_legal is court_actions read
 * backwards, and it is the only thing court_apply trusts. */
int court_legal(const Game *g, const Action *a)
{
    Action buf[256];
    int n, i;
    if (!a || a->seat < 0 || a->seat >= g->nhouses) return 0;
    n = court_actions(g, a->seat, buf, 256);
    for (i = 0; i < n; i++)
        if (buf[i].kind == a->kind && buf[i].target == a->target
         && buf[i].card == a->card && buf[i].a == a->a
         && buf[i].b == a->b) return 1;
    return 0;
}

static int court_apply_1(Game *g, const Action *a);
static CourtWatch watcher;

void court_watch(CourtWatch fn) { watcher = fn; }

/* Wrapped rather than hooked at each return: court_apply leaves by two
 * dozen different doors and only one thing needed to happen at all of
 * them. */
int court_apply(Game *g, const Action *a)
{
    int ok = court_apply_1(g, a);
    if (ok && watcher) watcher(g, a);
    return ok;
}

static int court_apply_1(Game *g, const Action *a)
{
    int s;
    if (!court_legal(g, a)) return 0;
    s = a->seat;

    switch (a->kind) {
    case A_PASS: return 1;

    case A_DRAW: {
        int d = a->a, big = a->b, r = DECK_COST_RES[d], cost;
        if (d == D_AMBITION) r_levy_add(g, s, R_CAP, -2);

        else if (r >= 0) {
            cost = big ? 3 : 1;
            if (g->h[s].id == H_RAVENMARK && d == D_WAR) cost--;
            r_levy_add(g, s, r, -cost);
        }
        g->draws_left--;
        if (d == D_WORLD) {
            /* "Drawing from the World deck is free, but the card resolves
             * immediately and is not held.  The reason a free draw is not
             * free." */
            int c = r_deck_draw(g, d);
            if (c >= 0) {
                court_log("  %s draws the World: %s", HOUSE_NAME[g->h[s].id],
                          CARDS[c].name);
                card_play_world(g, s, c);
            }
            return 1;
        }
        court_log("  %s draws from the %s deck%s", HOUSE_NAME[g->h[s].id],
                  d == (int)g->h[s].id ? "House" :
                  d == D_WAR ? "War" : d == D_POLITICAL ? "Political" :
                  d == D_INTRIGUE ? "Intrigue" :
                  d == D_AMBITION ? "Ambition" :
                  d == D_THRONE ? "Throne" : "?", big ? " (deep)" : "");
        if (big) {
            /* "Paying 3 Levy of that deck's resource instead draws 2 and
             * keeps 1." */
            int c1 = r_deck_draw(g, d), c2 = r_deck_draw(g, d);
            int keep = c1, drop = c2;
            if (c2 >= 0 && c1 >= 0
             && CARDS[c2].cost[RC_GRV] < CARDS[c1].cost[RC_GRV]) {
                keep = c2; drop = c1;
            }
            r_hand_add(g, s, keep);
            if (drop >= 0) r_deck_discard(g, d, drop);
        } else {
            r_hand_add(g, s, r_deck_draw(g, d));
            /* "Vipren draws 2 from its own House deck each turn." */
            if (g->h[s].id == H_VIPREN && d == (int)g->h[s].id)
                r_hand_add(g, s, r_deck_draw(g, d));
        }
        return 1;
    }

    case A_PLAY:
        /* "Cancel enemy's next card."  Spent on use, so it eats one
         * play and not every play after it.  The card is still spent
         * by its owner -- that is what being cancelled costs. */
        if (g->cancel_next[s]) {
            g->cancel_next[s] = 0;
            court_log("  %s's move comes to nothing", HOUSE_NAME[g->h[s].id]);
            { int hi = -1, k;
              for (k = 0; k < g->h[s].hand_n; k++)
                  if (g->h[s].hand[k] == a->card) hi = k;
              if (hi >= 0) r_hand_remove(g, s, hi); }
            r_deck_discard(g, CARDS[a->card].deck, a->card);
            return 1;
        }
        return card_play(g, s, a->card, a->target);

    case A_BOND:
        g->bond_used = 1;
        return card_play(g, s, a->card, a->target);

    case A_INSTIGATE:
        /* "Instigators are sent face down and resolve at Consequence.
         * The target learns the effect but not the sender." */
        g->instigators_left--;
        /* Logged at the sending as well as the landing.  The Consequence
         * line says an effect arrived; this one says who paid for it,
         * which is the only way a trace shows that Vipren whispers and
         * Ravenmark cannot. */
        court_log("  %s sends a whisper%s", HOUSE_NAME[g->h[s].id],
                  a->a == 1 ? " to a lord" : a->a == 2 ? " to the Court"
                  : " into the realm");
        if (a->a == 2) {                      /* at the Court             */
            r_favour_add(g, a->target, -1);
            return 1;
        }
        if (g->pending_n < MAX_PENDING) {
            g->pending[g->pending_n].from = (signed char)s;
            g->pending[g->pending_n].to   = a->target;
            g->pending[g->pending_n].kind = (unsigned char)a->a;
            g->pending_n++;
        }
        if (a->target == g->paramount && a->a != 1) r_grievance_add(g, s, 1);
        return 1;

    case A_PROPOSE: {
        /* "Offering costs nothing.  Refusing costs nothing."  The card is
         * spent either way -- it was the offer. */
        View v;
        int idx, term = a->a, cons = a->b, card = a->card;
        const Card *c = &CARDS[card];
        int cons_res = -1;
        if (cons > 0) {
            cons_res = c->eff[0].op == OP_PROPOSE && c->cost[RC_MIL] ? R_MIL
                     : c->cost[RC_GOLD] ? R_GOLD : R_CAP;
            if (cons > g->h[s].levy[cons_res]) cons = g->h[s].levy[cons_res];
        }
        court_view(g, a->target, &v);
        idx = g->promise_n;
        if (ASK(g, a->target, accept_promise, &v, s, term, cons_res, cons)) {
            idx = promise_new(g, s, a->target, term, cons_res, cons, card);
            if (idx >= 0) {
                if (c->eff[0].op == OP_PROPOSE_BONUS)
                    g->promise[idx].bonus_capital = (signed char)c->eff[0].b;
                if (c->eff[0].op == OP_PROPOSE_PUBLIC)
                    g->promise[idx].public_oath = 1;
                if (c->eff[0].op == OP_PROPOSE_DOUBLE) {
                    /* "Propose Vote to two Houses at once; only one can
                     * be kept."  The twin is made at once and the pair
                     * point at each other. */
                    int t2;
                    for (t2 = 0; t2 < g->nhouses; t2++)
                        if (t2 != s && t2 != a->target && g->h[t2].alive) {
                            int j = promise_new(g, s, t2, term, -1, 0, card);
                            if (j >= 0) {
                                g->promise[j].twin = (unsigned char)idx;
                                g->promise[idx].twin = (unsigned char)j;
                            }
                            break;
                        }
                }
            }
        } else {
            court_log("  %s refuses %s's offer",
                      HOUSE_NAME[g->h[a->target].id], HOUSE_NAME[g->h[s].id]);
        }
        { int hi = -1, k;
          for (k = 0; k < g->h[s].hand_n; k++) if (g->h[s].hand[k] == card) hi = k;
          if (hi >= 0) r_hand_remove(g, s, hi); }
        r_deck_discard(g, c->deck, card);
        return 1;
    }

    case A_DECLARE_WAR:
        g->declare_used = 1;
        r_grievance_add(g, s, -a->a);
        g->war[s][a->target] = 1;
        court_log("  %s declares Open War on %s", HOUSE_NAME[g->h[s].id],
                  HOUSE_NAME[g->h[a->target].id]);
        combat(g, s, a->target);
        return 1;

    case A_CHALLENGE:
        g->declare_used = 1;
        g->challenger = (signed char)s;    /* answered at Consequence     */
        return 1;

    case A_REVEAL_BOND:
        g->declare_used = 1;
        if (a->target < 0 || a->target >= g->lord_n) return 0;
        g->lord[a->target].revealed = 1;
        court_log("  %s reveals a Bond: %s is sworn to it",
                  HOUSE_NAME[g->h[s].id], CARDS[g->lord[a->target].card].name);
        return 1;

    case A_SPEND_GRIEVANCE:
        r_grievance_add(g, s, -a->a);
        if (a->a == 5) {
            /* The five-Grievance sink is the attempt itself, so the
             * Grievance just spent is the whole price and no card is
             * charged for it.  The odds are The Long Knife's odds. */
            int lp = g->paramount;
            if (lp >= 0 && r_roll(g, 3) == 0) {
                r_standing_add(g, lp, r_pick_enemy_loss(g, lp), -3);
                r_check_death(g, lp);
                court_log("  the Lord Paramount is struck down");
            } else {
                r_unrest_add(g, s, -1, 1);
                court_log("  the blade misses; %s is blamed",
                          HOUSE_NAME[g->h[s].id]);
            }
        } else if (a->a == 6) {
            g->council_pending = 1;
        } else if (a->a == 4 && a->card >= 0) {
            r_hand_add(g, s, a->card);
            card_play(g, s, a->card, -1);
        }
        return 1;

    case A_BUY_FAVOUR:
        r_levy_add(g, s, R_GOLD, -3);
        g->h[s].favour_bought = 1;
        r_favour_add(g, s, 1);
        return 1;

    case A_DISCARD: {
        int hi = -1, k;
        for (k = 0; k < g->h[s].hand_n; k++)
            if (g->h[s].hand[k] == a->card) hi = k;
        if (hi < 0) return 0;
        r_deck_discard(g, CARDS[a->card].deck, a->card);
        r_hand_remove(g, s, hi);
        return 1;
    }
    }
    return 0;
}

/* A World card "resolves at once, on everyone", so it is played by the
 * realm and not by the House that turned it -- the drawer is passed only
 * as the seat an effect needing one falls back to, and no World card in
 * the seed set needs one. */
void card_play_world(Game *g, int s, int card)
{
    const Card *c = &CARDS[card];
    int e;
    for (e = 0; e < 2; e++) r_apply_effect(g, s, -1, c, e);
    r_deck_discard(g, c->deck, card);
}

/* ---------------------------------------------------------- the turn -- */
static void turn_levy(Game *g, int s)
{
    House *h = &g->h[s];
    int r, cut;
    /* "At the start of your turn your Levy is set to your Standing in
     * each resource.  It is not added to what is left; it is set."  The
     * one exception is Winter Camp, which the card names explicitly. */
    for (r = 0; r < R_COUNT; r++) {
        int kept = h->keep_levy[r] ? h->levy[r] : 0;
        h->levy[r] = (signed char)(h->standing[r] + kept);
        h->keep_levy[r] = 0;
    }
    /* "Goldwyn: +2 Gold Levy every turn beyond Standing." */
    if (h->id == H_GOLDWYN) r_levy_add(g, s, R_GOLD, 2);

    /* "At 1 to 4, your Levy is reduced by that many, spread as you
     * choose."  Spread off the largest pile each point, which is the
     * spread a House that wants to keep its options open picks. */
    /* Unrest is three tracks now, and each one bites its own resource.
     * The House causing it chose where it landed; the House suffering it
     * no longer gets to spread the loss somewhere cheaper. */
    for (r = 0; r < R_COUNT; r++) {
        cut = h->unrest[r] >= UNREST_MAX ? 0 : h->unrest[r];
        /* THE BOAR, the beast that will not fall: a realm already in
         * uproar is the one it rules best.  Unrest cuts its Levy at
         * half rate, which is the whole of why it can be pushed to the
         * edge and keep coming. */
        if (h->id == H_EVERHOLD) cut /= 2;
        h->levy[r] = (signed char)(h->levy[r] > cut ? h->levy[r] - cut : 0);
    }
}

static void do_turn(Game *g, int s)
{
    Action opts[256], pick;
    int n, guard = 0, st;
    View v;

    /* "The dead draw from the Ghost deck, may haunt once per round, do
     * not vote, and cannot win."  A fallen House still has a turn --
     * one card off the Ghost pile, resolved where it is drawn -- which
     * is the whole of what it can do until the Court hears its claim
     * or stops hearing it for good. */
    if (!g->h[s].alive) {
        int gc;
        if (!g->h[s].fallen || g->h[s].extinguished) return;
        g->seat_turn = s;
        gc = r_deck_draw(g, D_DEAD);
        if (gc >= 0) {
            court_log("-- %s haunts: %s", HOUSE_NAME[g->h[s].id],
                      CARDS[gc].name);
            card_play_world(g, s, gc);
        }
        return;
    }
    g->seat_turn = s;
    g->step = ST_LEVY;
    turn_levy(g, s);
    /* "Cancel target's next House Action."  A House Action is a draw
     * here, so a cancelled one is a draw not given.  Spent on use, so
     * the card costs its target one turn and not every turn after. */
    if (g->skip_actions[s]) {
        g->skip_actions[s]--;
        g->draws_left = 0;
        court_log("  %s's council is in disarray and does not act",
                  HOUSE_NAME[g->h[s].id]);
    } else {
        g->draws_left = (unsigned char)(1 + g->banked_draws[s]);
        g->banked_draws[s] = 0;
    }
    g->bond_used = 0;
    g->declare_used = 0;
    /* "Ravenmark cannot send Instigators.  Vipren sends two per turn." */
    g->instigators_left = g->h[s].id == H_RAVENMARK ? 0
                        : g->h[s].id == H_VIPREN ? 2 : 1;
    court_log("-- %s (M%d C%d G%d levy, unrest %d/%d/%d, grievance %d, "
              "lords %d)",
              HOUSE_NAME[g->h[s].id], g->h[s].levy[R_MIL], g->h[s].levy[R_CAP],
              g->h[s].levy[R_GOLD], g->h[s].unrest[R_MIL],
              g->h[s].unrest[R_CAP], g->h[s].unrest[R_GOLD],
              g->h[s].grievance, r_vassal_count(g, s));

    /* A turn ends when the seat passes.  The guard is not a rule, it is a
     * guard: an AI that returns an action the engine then refuses would
     * otherwise spin, and a spin in a ten-thousand-game soak looks
     * exactly like a hang. */
    for (st = ST_DRAW; st < ST_COUNT; st++) {
        g->step = (unsigned char)st;
        for (;;) {
            if (++guard > 200) {
                if (court_checking())
                    court_inv("seat %d took 200 actions in one turn", s);
                goto turn_done;
            }
            if (g->winner >= 0) { g->step = ST_COUNT; return; }
            n = court_actions(g, s, opts, 256);
            /* Only Pass left, so there is no decision here: the step is
             * skipped rather than asked.  A Draw step with nothing
             * affordable, or a Declare step with no war to declare, used
             * to be three dead prompts a turn. */
            if (getenv("COURT_STEPTRACE")) {
                const House *hh = &g->h[s];
                fprintf(stderr, "STEP seat%d step%d n=%d hand=%d "
                        "levy=%d/%d/%d grv=%d\n", s, st, n, hh->hand_n,
                        hh->levy[R_MIL], hh->levy[R_CAP], hh->levy[R_GOLD],
                        hh->grievance);
            }
            if (n <= 1) break;
            court_view(g, s, &v);
            pick = ask_choose(g, s, &v, opts, n);
            /* Pass advances to the next step.  Passing the last one ends
             * the turn, which is why there is no separate end-turn. */
            if (pick.kind == A_PASS) break;
            if (!court_apply(g, &pick)) {
                if (court_checking())
                    court_inv("seat %d proposed an illegal action %d", s,
                              pick.kind);
                goto turn_done;
            }
            if (g->council_pending) council(g);
            if (g->winner >= 0) { g->step = ST_COUNT; return; }
        }
    }
turn_done:
    g->step = ST_COUNT;

    /* "Hand limit 7, checked at end of turn, discarding your own choice."
     * The choice made here is to drop what costs most, which is the card
     * least likely to be affordable next turn. */
    while (g->h[s].hand_n > HAND_LIMIT) {
        int i, worst = 0, wc = -1;
        for (i = 0; i < g->h[s].hand_n; i++) {
            const Card *c = &CARDS[g->h[s].hand[i]];
            int t = c->cost[RC_MIL] + c->cost[RC_CAP] + c->cost[RC_GOLD]
                  + c->cost[RC_GRV] * 2;
            if (t > wc) { wc = t; worst = i; }
        }
        r_deck_discard(g, CARDS[g->h[s].hand[worst]].deck, g->h[s].hand[worst]);
        r_hand_remove(g, s, worst);
    }
    /* "Unspent Levy is lost at end of turn.  There is no banking."  It
     * is counted on the way out: the design's fifth measure is what is
     * still in hand here, against what the turn started with. */
    {   int r;
        for (r = 0; r < R_COUNT; r++) {
            g->levy_unspent += g->h[s].levy[r];
            g->levy_given   += g->h[s].standing[r];
            g->h[s].levy_unspent += g->h[s].levy[r];
            g->h[s].levy_given   += g->h[s].standing[r];
            if (!g->h[s].keep_levy[r]) g->h[s].levy[r] = 0;
        }
        g->turns_taken++;
    }
}

/* ------------------------------------------------------- the reckoning */
static int promise_due(const Game *g, int i)
{
    const Promise *p = &g->promise[i];
    if (p->state != P_LIVE) return 0;
    if (p->due_at_council || p->due_on_attack) return 0;   /* their event */
    return p->due_round >= 0 && g->round >= p->due_round;
}

static void reckoning(Game *g)
{
    int decide[MAX_PROMISES], i;
    View v;

    /* Collected in seat order and revealed together, which is the design's
     * own wording and matters more here than anywhere else in the game:
     * a promiser who could see the earlier decisions would be choosing
     * with information the others did not have. */
    for (i = 0; i < g->promise_n; i++) {
        decide[i] = -1;
        if (!promise_due(g, i)) continue;
        if (r_promise_impossible(g, i)) { decide[i] = 2; continue; }
        court_view(g, g->promise[i].promiser, &v);
        decide[i] = ASK(g, g->promise[i].promiser, keep_promise, &v, i) ? 1 : 0;
    }
    for (i = 0; i < g->promise_n; i++) {
        if (decide[i] < 0) continue;
        if (decide[i] == 2) {
            /* "discarded, not broken.  The engine decides this, not the
             * promiser." */
            g->promise[i].state = P_VOID;
            court_log("  a promise between %s and %s can no longer be kept",
                      HOUSE_NAME[g->h[g->promise[i].promiser].id],
                      HOUSE_NAME[g->h[g->promise[i].promisee].id]);
        } else if (decide[i]) {
            promise_keep(g, i);
            /* The term is carried out.  Tribute is the only one that
             * moves anything at the moment of keeping; the rest are
             * carried out where they bite -- a vote at the Council, a
             * defence in combat, a freeing at Manumission. */
            if (g->promise[i].term == TERM_TRIBUTE) {
                int r = R_GOLD, amt = 2;
                if (g->h[g->promise[i].promiser].levy[r] < amt)
                    amt = g->h[g->promise[i].promiser].levy[r];
                r_levy_add(g, g->promise[i].promiser, r, -amt);
                r_levy_add(g, g->promise[i].promisee, r,  amt);
            }
            if (g->promise[i].term == TERM_MANUMISSION) {
                int j, who = g->promise[i].promiser;
                for (j = 0; j < g->lord_n; j++)
                    if (g->lord[j].alive && g->lord[j].holder == who
                     && g->lord[j].revealed) {
                        g->lord[j].holder = -1; g->lord[j].revealed = 0;
                        g->lord[j].servitude = g->lord[j].revolution = 0;
                        break;
                    }
            }
        } else {
            promise_break(g, i);
        }
    }
}

/* ------------------------------------------------------- consequence -- */
static void consequence(Game *g)
{
    int i, r;

    /* Instigators, face down until now.  "The target learns the effect
     * but not the sender." */
    for (i = 0; i < g->pending_n; i++) {
        int to = g->pending[i].to, kind = g->pending[i].kind;
        if (kind == 1) {                      /* aimed at a lord          */
            int li = g->pending[i].to;
            if (li >= 0 && li < g->lord_n && g->lord[li].alive
             && !g->lord[li].frozen) {
                Lord *l = &g->lord[li];
                int step = l->double_rev ? 2 : 1;
                if (l->holder >= 0 && g->h[l->holder].id == H_GOLDWYN) step *= 2;
                if (l->trait == LT_AFRAID) step = step > 1 ? step / 2 : 0;
                if (l->holder >= 0 && g->h[l->holder].id == H_ALDEMAR
                 && l->revolution == 0) step -= 1;
                /* THE OX, the Yoke: "what is bound stays bound."
                 * Revolution advances at half rate against it, so a
                 * lord it has taken is slow to turn. */
                if (l->holder >= 0 && g->h[l->holder].id == H_STONEGARTH)
                    step = step > 1 ? step / 2 : 0;
                if (step > 0)
                    l->revolution = (unsigned char)
                        (l->revolution + step > REVOLUTION_MAX
                         ? REVOLUTION_MAX : l->revolution + step);
                court_log("  a whisper reaches %s", CARDS[l->card].name);
            }
        } else if (to >= 0 && to < g->nhouses && g->h[to].alive) {
            r_unrest_add(g, to, g->pending[i].kind == 2 ? -1
                                : g->pending[i].kind - 3, 1);
            court_log("  unrest stirs in %s's realm", HOUSE_NAME[g->h[to].id]);
        }
    }
    g->pending_n = 0;

    /* A lord at Revolution 5 turns whether or not it wants to; at 3 or 4
     * it is the lord's own decision, which is what makes an Instigator
     * worth sending at somebody else's lord. */
    for (i = 0; i < g->lord_n; i++) {
        Lord *l = &g->lord[i];
        if (!l->alive || l->holder < 0 || l->revolution < 3) continue;
        if (l->revolution < REVOLUTION_MAX) {
            View v;
            court_view(g, l->holder ? 0 : 1, &v);
            if (ASK(g, l->holder ? 0 : 1, lord_turns, &v, l->holder, i) != 2) continue;
        }
        lord_turns(g, i);
    }

    /* "At 5 that resource revolts: lose 2 Standing in it, reset to 0."
     * A revolt is a thing you own turning on you, which is what the word
     * meant before there was a board. */
    for (i = 0; i < g->nhouses; i++) {
        if (!g->h[i].alive) continue;
        for (r = 0; r < R_COUNT; r++)
            if (g->h[i].unrest[r] >= UNREST_MAX) {
                r_standing_add(g, i, r, -2);
                g->h[i].unrest[r] = 0;
                r_check_death(g, i);
                court_log("  %s's %s rises", HOUSE_NAME[g->h[i].id],
                          RES_NAME[r]);
            }
    }

    for (i = 0; i < g->nhouses; i++)
        if (g->h[i].alive) ambitions_check(g, i);

    if (g->challenger >= 0 && g->h[g->challenger].alive) {
        throne_challenge(g, g->challenger);
        memset(g->throne_bonus, 0, sizeof g->throne_bonus);
    }
    g->challenger = -1;
}

/* ------------------------------------------------------- win checking - */
/* The Court denies the claim.
 *
 * A fallen House keeps its claim, so death alone no longer ends the
 * game -- which would make killing your rival pointless if there were
 * no way to finish it.  There is: the Court.  design.xml already makes
 * the Court the organ that says who is legitimate, "one vote, for the
 * House with the higher Favour", so a pretender is not banished, it is
 * outbid for recognition.  Drive your own Favour to the crown value
 * while the fallen House is down and its claim is extinguished, which
 * is the second death and the one that ends the game.
 *
 * Favour is already purchasable at 3 Gold a point to a maximum of +3
 * by purchase, "the five points above that cannot be bought, only
 * earned" -- so finishing a fallen House costs a campaign at Court and
 * not a coin, which is the right price for ending a rival for good. */
static void claim_check(Game *g)
{
    int i, j;
    if (!g->fallen_rule) return;
    for (i = 0; i < g->nhouses; i++) {
        if (!g->h[i].fallen || g->h[i].extinguished) continue;
        for (j = 0; j < g->nhouses; j++) {
            if (j == i || !g->h[j].alive) continue;
            if (g->h[j].favour < g->favour_crown) continue;
            g->h[i].extinguished = 1;
            court_log("  the Court will not hear %s again",
                      HOUSE_NAME[g->h[i].id]);
        }
    }
}

static int check_win(Game *g)
{
    int i, alive = 0, last = -1;
    if (g->winner >= 0) return 1;

    claim_check(g);
    /* A fallen House is not out of the game: it haunts, and it may be
     * restored while the Court still hears it.  Only an extinguished
     * claim stops counting. */
    for (i = 0; i < g->nhouses; i++)
        if (g->h[i].alive || (g->h[i].fallen && !g->h[i].extinguished)) {
            alive++; last = i;
        }
    /* The survivor is whoever is actually standing, not a ghost. */
    if (alive == 1 && last >= 0 && !g->h[last].alive)
        for (i = 0; i < g->nhouses; i++) if (g->h[i].alive) last = i;

    /* "In a two-player game death ends the game and the survivor wins."
     * At three and four it is The Last House, which is the same rule. */
    if (alive <= 1) {
        if (last < 0) {
            /* Every House died in the same Consequence -- a Cataclysm
             * without a Cataclysm deck.  "The Last House" still has a
             * meaning: the one that was standing longest. */
            int j;
            for (j = 0; j < g->nhouses; j++)
                if (last < 0 || g->h[j].dead_round > g->h[last].dead_round)
                    last = j;
        }
        g->winner = (signed char)last;
        g->win_reason = "The Last House";
        return 1;
    }
    for (i = 0; i < g->nhouses; i++) {
        if (!g->h[i].alive) continue;      /* "...and cannot win." */
        if (purchased_throne(g, i)) {
            g->winner = (signed char)i;
            g->win_reason = "The Purchased Throne";
            return 1;
        }
        /* "At Favour +5 the Court crowns that House and the game ends.
         * This is a third road, and it is the only one a House can walk
         * while losing every fight." */
        if (g->h[i].favour >= g->favour_crown) {
            g->winner = (signed char)i;
            g->win_reason = "Favour of the Court";
            return 1;
        }
    }
    return 0;
}

static void final_king(Game *g)
{
    int i, best = -1;
    for (i = 0; i < g->nhouses; i++) {
        if (!g->h[i].alive) continue;
        if (best < 0) { best = i; continue; }
        if (court_total_power(g, i) > court_total_power(g, best)) best = i;
        else if (court_total_power(g, i) == court_total_power(g, best)
              && g->h[i].broken_n < g->h[best].broken_n) best = i;
    }
    g->winner = (signed char)best;
    g->win_reason = "The Final King";
}

/* ------------------------------------------------------------ a round - */
int court_round(Game *g)
{
    int i, s;

    if (g->winner >= 0) return 1;
    g->round++;
    if (g->round > ROUND_CAP) { final_king(g); return 1; }
    court_log("\n== round %d ==", g->round);

    /* ---- 1. Accession.  The Council sits and elects.  A title that
     * accrues to whoever is ahead cannot be contested, and Accession by
     * Total Power was the worst-measured rule in this game: the first
     * Paramount won 54% of four-player games and 66% of two-player ones,
     * because the title fed the tax and the tax fed the title. */
    g->phase = PH_ACCESSION;
    {
        int elected = council(g);
        if (elected >= 0 && g->h[elected].alive) {
            g->paramount = (signed char)elected;
            g->h[elected].rounds_paramount++;
            court_log("  Lord Paramount: %s", HOUSE_NAME[g->h[elected].id]);
        } else {
            g->paramount = -1;
            court_log("  the Council elects nobody");
        }
        if (g->round == 1) g->first_paramount = g->paramount;
        for (i = 0; i < g->nhouses; i++)
            if (g->h[i].alive) {
                g->grievance_sum += g->h[i].grievance;
                if (g->h[i].grievance >= GRIEVANCE_MAX) g->grievance_pinned++;
                g->grievance_samples++;
            }
    }

    /* ---- 2. Tax.  "The Lord Paramount takes 1 Standing of their choosing
     * from each other House and gains 1 Gold Standing per House taxed.
     * Each taxed House gains 1 Grievance."
     *
     * <open_questions> Q5 asks whether this should take Standing, as
     * written, or Levy: "Standing is the stronger catch-up and the more
     * painful; it may be too strong."  It takes Standing, because that is
     * what the design says, and the soak is where the question gets
     * answered rather than here. */
    g->phase = PH_TAX;
    if (g->paramount >= 0 && g->regent != -2) {
        int lp = g->regent >= 0 ? g->regent : g->paramount;
        for (i = 0; i < g->nhouses; i++) {
            int r;
            if (i == g->paramount || !g->h[i].alive) continue;
            if (g->h[i].grievance >= 2) {
                r_grievance_add(g, i, -2);
                court_log("  %s buys off the tax", HOUSE_NAME[g->h[i].id]);
                continue;
            }
            /* The tax takes Levy and not Standing.  Measured over three
             * ranges of 300 seeds a Standing tax gave a 14.6x win spread;
             * Levy gave 1.55x.  Electing the title broke half the loop
             * and this breaks the other half: the Paramountcy buys a good
             * turn, not a permanent lead. */
            {
                /* "The Lord Paramount taxes twice this round." */
                int takes = g->tax_double ? 2 : 1, k;
                for (k = 0; k < takes; k++) {
                    r = r_pick_enemy_loss(g, i);
                    if (g->h[i].levy[r] > 0) r_levy_add(g, i, r, -1);
                    r_levy_add(g, lp, R_GOLD, 1);
                    r_grievance_add(g, i, 1);
                }
            }
        }
    }
    g->regent = -1;
    if (check_win(g)) return 1;

    /* ---- 3. Turns.  "Each living House takes a turn in seat order." */
    g->phase = PH_TURNS;
    memset(g->war, 0, sizeof g->war);
    for (i = 0; i < g->nhouses; i++) g->h[i].favour_bought = 0;
    for (s = 0; s < g->nhouses; s++) {
        do_turn(g, s);
        if (check_win(g)) return 1;
    }

    /* ---- 4. Reckoning. */
    g->phase = PH_RECKONING;
    reckoning(g);
    if (check_win(g)) return 1;

    /* ---- 5. Consequence. */
    g->phase = PH_CONSEQUENCE;
    consequence(g);
    if (g->council_pending) council(g);
    if (check_win(g)) return 1;

    /* ---- 6. World.  "If the round number is even, the World deck turns
     * one card face up on the table." */
    g->phase = PH_WORLD;
    if (g->round % 2 == 0) {
        int c = r_deck_draw(g, D_WORLD);
        if (c >= 0) {
            court_log("  the world turns: %s", CARDS[c].name);
            card_play_world(g, -1, c);
        }
    }
    /* "At the end of each round the Court turns one card, which acts on
     * the realm without preference." */
    {
        int c = r_deck_draw(g, D_COURT);
        if (c >= 0) {
            court_log("  the Court: %s", CARDS[c].name);
            card_play_world(g, -1, c);
        }
    }
    /* A World or Court card can trigger a Council, and the World phase is
     * the last of the round, so it has to be answered here.  Without this
     * the Council survived into the next round and fired on whatever
     * action a House happened to take first -- which read in the log as a
     * House drawing a card from the Intrigue deck and being crowned. */
    if (g->council_pending) {
        council(g);
        if (check_win(g)) return 1;
    }

    g->favour_locked = 0;
    g->court_silent = 0;
    for (i = 0; i < g->lord_n; i++) g->lord[i].frozen = 0;
    for (i = 0; i < g->nhouses; i++) if (g->h[i].alive) ambitions_check(g, i);

    if (check_win(g)) return 1;
    if (g->round >= ROUND_CAP) { final_king(g); return 1; }
    return 0;
}

/* rules.c reaches for this when a card draws off the Cataclysm or
 * Restoration pile: those resolve where they are drawn, like the World
 * deck, rather than going to a hand. */
void r_card_play_world(Game *g, int s, int card) { card_play_world(g, s, card); }
