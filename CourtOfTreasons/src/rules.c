/* The rules: state, the view a seat is entitled to, and what a card does.
 *
 * design.xml is the authority.  Every rule below carries the sentence it
 * came from where the sentence is short enough to quote, because the
 * sibling project learned that a rule without its source is a rule
 * somebody will later "fix".
 *
 * Nothing in this file writes to a Game except through court_apply() and
 * the phase functions in phases.c.  That is not tidiness: the networking
 * decision is undecided, and two of the three options need every change
 * to be a described action that an authority can validate.
 */
#include "court.h"
#include <stdarg.h>
#include <stdlib.h>

int  r_deck_draw(Game *, int);
void r_card_play_world(Game *, int, int);
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------- logging */
static FILE *trace;
static int   checking;

void court_open_trace(const char *path)
{
    if (!path) return;
    trace = (path[0] == '-' && !path[1]) ? stdout : fopen(path, "w");
}
void court_set_check(int on) { checking = on; }
int  court_checking(void)    { return checking; }

void court_log(const char *fmt, ...)
{
    va_list ap;
    if (!trace) return;
    va_start(ap, fmt); vfprintf(trace, fmt, ap); va_end(ap);
    fputc('\n', trace);
}

/* An INV line is a bug and never a quirk.  It is printed to the trace and
 * to stderr, because a soak reads the trace and a person reads stderr. */
void court_inv(const char *fmt, ...)
{
    va_list ap;
    char buf[256];
    va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (trace) fprintf(trace, "INV %s\n", buf);
    fprintf(stderr, "INV %s\n", buf);
}

/* ----------------------------------------------------------------- rng */
/* One stream, owned by the Game.  "Every random draw comes from a seeded
 * stream.  A game replays exactly from its seed."  It lives in the Game
 * and not in a static so that two games in one process cannot interleave,
 * which is what a soak with threads would otherwise do to it. */
void court_seed(Game *g, unsigned long seed)
{
    g->rng = seed ? seed : 0x9E3779B97F4A7C15UL;
}
static unsigned long rnd(Game *g)
{
    unsigned long x = g->rng;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return g->rng = x;
}
static int roll(Game *g, int n) { return n > 0 ? (int)(rnd(g) % (unsigned long)n) : 0; }

/* -------------------------------------------------------------- limits */
static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

int court_total_power(const Game *g, int s)
{
    /* "Total Power = Military + Capital + Gold, counted from Standing
     * only."  Levy never counts, which is the whole point of splitting
     * the two: what you are, not what you can spend today. */
    return g->h[s].standing[R_MIL] + g->h[s].standing[R_CAP]
         + g->h[s].standing[R_GOLD];
}

/* Trust, derived every time it is needed, from the piles and nothing else.
 * "A House that has never dealt with you is neither trusted nor
 * distrusted.  Zero is ignorance, not suspicion." */
int court_trust(const Game *g, int of, int toward)
{
    int i, t = 0;
    for (i = 0; i < g->promise_n; i++) {
        const Promise *p = &g->promise[i];
        if (p->promiser != toward || p->promisee != of) continue;
        if (p->state == P_KEPT)   t++;
        if (p->state == P_BROKEN) t--;
    }
    return t;
}

/* Reputation is the count of your Broken Words.  Vipren's weakness is
 * that cards which read reputation read its pile double. */
int court_reputation(const Game *g, int s)
{
    int n = g->h[s].broken_n;
    return g->h[s].id == H_VIPREN ? n * 2 : n;
}

/* --------------------------------------------------------------- bonds */
/* A Bond now runs between a House and a Minor Lord.  There is no Bond
 * between the two Houses: an opponent cannot be bound, only beaten,
 * bought or outlasted. */
static int vassal_count(const Game *g, int seat)
{
    int i, n = 0;
    for (i = 0; i < g->lord_n; i++)
        if (g->lord[i].alive && g->lord[i].revealed
         && g->lord[i].holder == seat) n++;
    return n;
}

/* Which lord a Lever can reach.  A Lever names a Trait and reaches only a
 * lord that carries it, which is what replaced the conditions that used
 * to read a House -- "a House with 3 or more Unrest" became "a lord that
 * is Afraid". */
static int trait_of_cond(int cond)
{
    switch (cond) {
    case TC_LORD_INDEBT:    return LT_INDEBT;
    case TC_LORD_AFRAID:    return LT_AFRAID;
    case TC_LORD_AMBITIOUS: return LT_AMBITIOUS;
    case TC_LORD_PROUD:     return LT_PROUD;
    case TC_LORD_OWED:      return LT_OWED;
    }
    return -1;
}
static int find_lord(const Game *g, int seat, int cond, int mine)
{
    int i, want = trait_of_cond(cond);
    for (i = 0; i < g->lord_n; i++) {
        const Lord *l = &g->lord[i];
        if (!l->alive) continue;
        if (mine == 1 && l->holder != seat) continue;
        if (mine == 0 && l->holder == seat) continue;
        if (want >= 0 && l->trait != want) continue;
        if (mine == 2 && l->holder >= 0 && l->holder != seat) continue;
        return i;
    }
    return -1;
}

/* "When Revolution passes Servitude, the lord abstains at Councils."
 * This is how a hidden track resolves in public without printing the
 * number: you learn a lord has turned by watching it not raise its
 * hand. */
static int lord_abstains(const Lord *l)
{
    return l->revolution > l->servitude;
}

/* ---------------------------------------------------------------- view */
/* What one seat is entitled to see, and nothing more.  Everything a seat
 * may not know is either absent from the View or set to -1.
 *
 * This is the only route the AI has to the state.  "The AI decides with
 * only what the seat legitimately knows.  A machine House that reads
 * another's hidden Bond is a bug, and the invariant checker asserts it."
 * The assertion is cheap because the structure makes the bug hard to
 * write: there is no field here to read.
 */
void court_view(const Game *g, int seat, View *v)
{
    int i, j, r;
    memset(v, 0, sizeof *v);
    v->me = (signed char)seat;
    v->nhouses = g->nhouses;
    v->round   = g->round;
    v->phase   = g->phase;
    v->step    = g->step;
    v->paramount = g->paramount;
    v->favour_crown = g->favour_crown;
    v->golden_throne = g->golden_throne;

    for (i = 0; i < g->nhouses; i++) {
        for (r = 0; r < R_COUNT; r++)
            v->standing[i][r] = g->h[i].standing[r];   /* public          */
        for (r = 0; r < R_COUNT; r++)
            v->unrest[i][r] = g->h[i].unrest[r];       /* public          */
        v->grievance[i] = g->h[i].grievance;           /* public          */
        v->alive[i]     = g->h[i].alive;
        v->house[i]     = g->h[i].id;
        v->throneworthy[i] = g->h[i].throneworthy;
        v->kept_n[i]    = g->h[i].kept_n;              /* the piles are   */
        v->broken_n[i]  = g->h[i].broken_n;            /* public          */
        v->favour[i]    = g->h[i].favour;
    }
    for (r = 0; r < R_COUNT; r++) v->levy[r] = g->h[seat].levy[r];

    /* Your own hand and your own Ambitions.  Nobody else's, ever --
     * Whisper Network looks at a hand, and when it does the engine hands
     * the result to the AI as a one-shot fact, not by widening the View.*/
    v->hand_n = g->h[seat].hand_n;
    for (i = 0; i < v->hand_n && i < (int)(sizeof v->hand / sizeof v->hand[0]); i++)
        v->hand[i] = g->h[seat].hand[i];
    for (i = 0; i < AMBITIONS_DEALT; i++) {
        v->ambition[i] = g->h[seat].ambition[i];
        v->amb_done[i] = g->h[seat].amb_done[i];
    }

    /* Lords.  Both Houses see every lord, who holds it and whether it is
     * revealed.  A seat sees the Servitude it holds itself; it sees a
     * Revolution count only where it has legitimately spied, and a lord
     * never sees anything, because a lord is not a seat. */
    v->lord_n = g->lord_n;
    for (i = 0; i < g->lord_n; i++) {
        const Lord *l = &g->lord[i];
        v->lord[i].card     = l->card;
        v->lord[i].trait    = l->trait;
        v->lord[i].holder   = l->holder;
        v->lord[i].revealed = l->revealed;
        v->lord[i].alive    = l->alive;
        v->lord[i].servitude  = (l->holder == seat || l->revealed)
                              ? (signed char)l->servitude : -1;
        v->lord[i].revolution = (l->holder == seat && l->lord_knows)
                              ? (signed char)l->revolution : -1;
    }
    (void)j;

    /* "Everything about a Promise is public except what the promiser
     * intends to do about it", so the whole pile copies across. */
    v->promise_n = g->promise_n;
    for (i = 0; i < g->promise_n; i++) v->promise[i] = g->promise[i];
}

/* --------------------------------------------------------------- decks */
static void deck_build(Game *g, int d)
{
    int i;
    Deck *k = &g->deck[d];
    k->n = DECK_SIZE[d];
    for (i = 0; i < k->n; i++) k->card[i] = (short)(DECK_FIRST[d] + i);
    k->draw = 0; k->discard_n = 0;
}
static void deck_shuffle(Game *g, int d)
{
    Deck *k = &g->deck[d];
    int i, j;
    short t;
    for (i = k->n - 1; i > 0; i--) {
        j = roll(g, i + 1);
        t = k->card[i]; k->card[i] = k->card[j]; k->card[j] = t;
    }
    k->draw = 0;
}
static int deck_draw(Game *g, int d)
{
    Deck *k = &g->deck[d];
    if (k->n == 0) return -1;              /* a deck the seed set lacks   */
    if (k->draw >= k->n) {
        /* Reshuffle the discard back in.  With a 124-card seed set and a
         * 30-round cap this happens often; with the full 1,135 it would
         * be rare, and neither case changes the rule. */
        int i;
        for (i = 0; i < k->discard_n; i++) k->card[i] = k->discard[i];
        if (k->discard_n) { k->n = k->discard_n; k->discard_n = 0; }
        deck_shuffle(g, d);
        if (k->draw >= k->n) { deck_build(g, d); deck_shuffle(g, d); }
    }
    return k->card[k->draw++];
}
static void deck_discard(Game *g, int d, int card)
{
    Deck *k = &g->deck[d];
    if (k->discard_n < MAX_CARDS) k->discard[k->discard_n++] = (short)card;
}

static void hand_add(Game *g, int s, int card)
{
    House *h = &g->h[s];
    if (card < 0) return;
    if (h->hand_n < (int)(sizeof h->hand / sizeof h->hand[0]))
        h->hand[h->hand_n++] = (short)card;
}
static void hand_remove(Game *g, int s, int idx)
{
    House *h = &g->h[s];
    if (idx < 0 || idx >= h->hand_n) return;
    h->hand[idx] = h->hand[--h->hand_n];
}
static int hand_find(const Game *g, int s, int card)
{
    int i;
    for (i = 0; i < g->h[s].hand_n; i++)
        if (g->h[s].hand[i] == card) return i;
    return -1;
}

/* ------------------------------------------------------- state changes */
/* Standing and Levy are the invention this design turns on, so the two
 * are moved by two different functions and neither is ever called by
 * accident: "Effects that say 'spend' take Levy.  Effects that say
 * 'permanently' take or give Standing." */
static void levy_add(Game *g, int s, int r, int n)
{
    if (r < 0 || r >= R_COUNT) return;
    g->h[s].levy[r] = (signed char)clampi(g->h[s].levy[r] + n, 0, 99);
}
static void standing_add(Game *g, int s, int r, int n)
{
    if (r < 0 || r >= R_COUNT) return;
    /* "Military cannot be reduced this round."  A shield stops the
     * loss and not the gain, and is checked here rather than at each
     * caller so nothing can route around it. */
    if (n < 0 && g->shield[s][r]) return;
    g->h[s].standing[r] = (signed char)clampi(g->h[s].standing[r] + n,
                                              STANDING_MIN, STANDING_MAX);
}
static void grievance_add(Game *g, int s, int n)
{
    /* THE WOLF, the Grudge: "the pack remembers."  Every wrong done to
     * it is remembered one harder, which is the whole of its economy --
     * Grievance is a resource for the Wolf and a nuisance for everyone
     * else.  Measured before it existed, Grievance sat at 0.80 of a
     * maximum ten at the end of a game, with five sinks nobody could
     * afford; a House that banks it is what those sinks were for. */
    if (n > 0 && g->h[s].id == H_WULFREN) n++;
    g->h[s].grievance = (signed char)clampi(g->h[s].grievance + n,
                                            0, GRIEVANCE_MAX);
}
/* Unrest is three tracks.  A negative resource means "the attacker
 * chooses", which is the whole point of splitting it: one track let the
 * House suffering it spread the loss where it hurt least. */
static int worst_unrest(const Game *g, int s)
{
    int r, best = 0;
    for (r = 1; r < R_COUNT; r++)
        if (g->h[s].standing[r] > g->h[s].standing[best]) best = r;
    return best;
}
static void unrest_add(Game *g, int s, int r, int n)
{
    if (r < 0) r = worst_unrest(g, s);
    g->h[s].unrest[r] = (signed char)clampi(g->h[s].unrest[r] + n, 0, UNREST_MAX);
}
static int unrest_total(const Game *g, int s)
{
    int r, n = 0;
    for (r = 0; r < R_COUNT; r++) n += g->h[s].unrest[r];
    return n;
}
static void favour_add(Game *g, int s, int n)
{
    
    g->h[s].favour = (signed char)clampi(g->h[s].favour + n,
                                         -FAVOUR_MAX, FAVOUR_MAX);
}

/* Which Standing a House gives up when something takes "of their
 * choosing".  Reads only that House's own Standing, which is public, so
 * this is not a hidden-information leak however it is called.
 *
 * It sheds from the largest pile, which keeps a House alive longest --
 * death is all three at 0 -- and is therefore the choice a House that
 * wants to survive actually makes. */
static int pick_own_loss(const Game *g, int s)
{
    int r, best = 0;
    for (r = 1; r < R_COUNT; r++)
        if (g->h[s].standing[r] > g->h[s].standing[best]) best = r;
    return best;
}
/* And which one an enemy takes from you: the one that hurts, which is the
 * smallest non-zero, because death is all three at zero. */
static int pick_enemy_loss(const Game *g, int s)
{
    int r, best = -1;
    for (r = 0; r < R_COUNT; r++) {
        if (g->h[s].standing[r] <= 0) continue;
        if (best < 0 || g->h[s].standing[r] < g->h[s].standing[best]) best = r;
    }
    return best < 0 ? R_MIL : best;
}

static void check_death(Game *g, int s)
{
    if (!g->h[s].alive) return;
    if (g->h[s].standing[R_MIL] || g->h[s].standing[R_CAP]
     || g->h[s].standing[R_GOLD]) return;
    /* "Survive elimination with 1 Military."  Spent when it is used, so
     * it saves a House once and not for the rest of the game. */
    if (g->h[s].survives) {
        g->h[s].survives = 0;
        g->h[s].standing[R_MIL] = 1;
        court_log("  %s should have fallen, and does not",
                  HOUSE_NAME[g->h[s].id]);
        return;
    }
    /* A House at 0 in all three has FALLEN, not ended.  Its claim
     * outlives it -- design.xml: "A Ghost may be resurrected by the
     * Resurrection deck, returning at 1 Standing in each resource with
     * its Promise piles intact.  What you did is not forgotten by
     * dying."  The game ends when the claim is extinguished, not when
     * the Standings hit zero; see claim_check in phases.c. */
    if (g->fallen_rule && !g->h[s].extinguished) {
        g->h[s].alive = 0;
        g->h[s].fallen = 1;
        g->h[s].dead_round = (unsigned char)g->round;
        court_log("  %s falls, and its claim does not",
                  HOUSE_NAME[g->h[s].id]);
        return;
    }
    g->h[s].alive = 0;
    g->h[s].dead_round = (unsigned char)g->round;
    court_log("  %s is dead: all three Standings at 0",
              HOUSE_NAME[g->h[s].id]);
}

/* ------------------------------------------------------------- targets */
static int target_ok(const Game *g, int actor, int t, int cond)
{
    int i;
    if (t < 0 || t >= g->nhouses || t == actor || !g->h[t].alive) return 0;
    switch (cond) {
    case TC_NONE:    return 1;
    case TC_BROKEN1: return court_reputation(g, t) >= 1;
    case TC_BROKEN2: return court_reputation(g, t) >= 2;
    case TC_UNREST3: return unrest_total(g, t) >= 3;
    case TC_PARAMOUNT: return t == g->paramount;
    case TC_VASSAL:  return vassal_count(g, t) > 0;
    case TC_POOR_BONUS: return 1;          /* a rider, never a gate       */
    case TC_DEFEATED:
        return g->war[actor][t] == 2;      /* 2: beaten by actor, this round */
    case TC_KEPT:
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_KEPT
             && g->promise[i].promiser == actor
             && g->promise[i].promisee == t) return 1;
        return 0;
    case TC_DEBT:
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_LIVE
             && g->promise[i].promiser == t
             && g->promise[i].promisee == actor) return 1;
        return 0;
    case TC_PEACE:
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_LIVE
             && g->promise[i].promiser == actor
             && g->promise[i].promisee == t
             && g->promise[i].term == TERM_PEACE) return 1;
        return 0;
    }
    return 0;
}

/* --------------------------------------------------------- the promise */
int promise_new(Game *g, int promiser, int promisee, int term,
                int cons_res, int cons_amt, int card)
{
    Promise *p;
    if (g->promise_n >= MAX_PROMISES) return -1;
    p = &g->promise[g->promise_n];
    memset(p, 0, sizeof *p);
    p->card = (short)card;
    p->promiser = (signed char)promiser;
    p->promisee = (signed char)promisee;
    p->term  = (unsigned char)term;
    p->state = P_LIVE;
    p->cons_res = (signed char)cons_res;
    p->cons_amt = (signed char)cons_amt;
    p->sworn_round = (short)g->round;
    p->twin = 0xff;
    p->due_round = -1;
    switch (term) {
    case TERM_VOTE: case TERM_ABSTENTION: p->due_at_council = 1; break;
    case TERM_FORBEARANCE: p->due_on_attack = 1; break;
    case TERM_PEACE:    p->due_round = (short)(g->round + 2); break;
    default:            p->due_round = (short)(g->round + 1); break;
    }
    /* "Consideration is paid from Levy the moment the Promise is
     * accepted, and it is not returned, whatever happens afterwards." */
    if (cons_amt > 0 && cons_res >= 0) {
        int pay = cons_amt;
        if (pay > g->h[promiser].levy[cons_res]) pay = g->h[promiser].levy[cons_res];
        levy_add(g, promiser, cons_res, -pay);
        levy_add(g, promisee, cons_res,  pay);
        p->cons_amt = (signed char)pay;
    }
    g->promises_made++;
    court_log("  %s promises %s (%d) to %s%s",
              HOUSE_NAME[g->h[promiser].id], "term", term,
              HOUSE_NAME[g->h[promisee].id],
              p->cons_amt ? ", consideration paid" : "");
    return g->promise_n++;
}

/* Keeping and breaking, which is the whole game.  Both are public. */
void promise_keep(Game *g, int i)
{
    Promise *p = &g->promise[i];
    p->state = P_KEPT;
    if (g->h[p->promisee].kept_n < MAX_PROMISES)
        g->h[p->promisee].kept[g->h[p->promisee].kept_n++] = (short)i;
    if (p->bonus_capital) standing_add(g, p->promiser, R_CAP, p->bonus_capital);
    /* THE LION, the Word: "a lion's word is a lion's claw."  A kept
     * Promise pays it a point of Capital Standing outright, which is
     * the only routine way any House GROWS Standing -- everything else
     * in this game takes it away.  Measured before the Lion existed,
     * Promises were made 1.69 times a game with eight terms, two piles
     * and the Court's whole opinion hanging off them; a House that
     * lives on its word is what that system was built for. */
    if (g->h[p->promiser].id == H_LEOWARD) standing_add(g, p->promiser, R_CAP, 1);
    /* What the Court is impressed by.  Measured; see the README.
     *   0  every kept promise, as design.xml has it
     *   1  only a promise that cost the promiser something
     *   2  the Court weighs the two Houses against each other */
    if (g->favour_mode == 1) {
        if (p->cons_amt > 0) favour_add(g, p->promiser, 1);
    } else if (g->favour_mode == 2) {
        favour_add(g, p->promiser, 1);
        favour_add(g, p->promiser ? 0 : 1, -1);
    } else {
        favour_add(g, p->promiser, 1);
    }
    g->promises_kept++;
    court_log("  %s keeps their word to %s",
              HOUSE_NAME[g->h[p->promiser].id],
              HOUSE_NAME[g->h[p->promisee].id]);
}

void promise_break(Game *g, int i)
{
    Promise *p = &g->promise[i];
    int j, paid = p->cons_amt > 0;
    p->state = P_BROKEN;

    /* Vipren's Nothing Was Agreed launders one break, once per game.  It
     * is the only thing in the design that clears a Broken Word, and
     * <open_questions> Q4 asks whether it should stay the only one. */
    if (p->promiser >= 0 && g->h[p->promiser].laundered == 2) {
        g->h[p->promiser].laundered = 1;       /* spent                   */
        court_log("  %s breaks their word and it is not written down",
                  HOUSE_NAME[g->h[p->promiser].id]);
    } else {
        if (g->h[p->promiser].broken_n < MAX_PROMISES)
            g->h[p->promiser].broken[g->h[p->promiser].broken_n++] = (short)i;
        court_log("  %s BREAKS their word to %s",
                  HOUSE_NAME[g->h[p->promiser].id],
                  HOUSE_NAME[g->h[p->promisee].id]);
    }

    /* "Breaking a Promise that carried consideration gives 1 Grievance to
     * every living House, not only the promisee.  Taking payment and
     * reneging is an offence against the table."  This is the rule that
     * closes the pricing problem; without it a vote is always worth
     * breaking at the moment it decides something. */
    if (paid || p->public_oath) {
        int n = p->public_oath ? 2 : 1;
        for (j = 0; j < g->nhouses; j++)
            if (g->h[j].alive && j != p->promiser) grievance_add(g, j, n);
    } else {
        grievance_add(g, p->promisee, 1);
    }
    /* THE LION pays double for a broken word.  A House whose whole
     * strength is its word has the most to lose by breaking one, and
     * without that the Lion would simply be a House that promises
     * freely and means none of it. */
    {
        int cost = paid ? 3 : 2;
        if (g->h[p->promiser].id == H_LEOWARD) cost *= 2;
        favour_add(g, p->promiser, -cost);
    }

    /* Aldemar "loses 2 Capital Standing the first time it breaks a
     * Promise each game".  The Legitimist pays for the first lie only. */
    if (g->h[p->promiser].id == H_ALDEMAR
     && !g->h[p->promiser].aldemar_first_break) {
        g->h[p->promiser].aldemar_first_break = 1;
        standing_add(g, p->promiser, R_CAP, -2);
        check_death(g, p->promiser);
    }
    g->promises_broken++;

    /* Two Tongues: "only one can be kept", so the twin voids with it. */
    if (p->twin != 0xff && p->twin < g->promise_n) {
        Promise *q = &g->promise[p->twin];
        if (q->state == P_LIVE) q->state = P_VOID;
    }
}

/* "A Promise that can no longer be kept because its term became
 * impossible is discarded, not broken.  The engine decides this, not the
 * promiser." */
static int promise_impossible(const Game *g, int i)
{
    const Promise *p = &g->promise[i];
    if (!g->h[p->promiser].alive || !g->h[p->promisee].alive) return 1;
    if (p->term == TERM_MANUMISSION && vassal_count(g, p->promiser) == 0)
        return 1;
    
    return 0;
}

/* ------------------------------------------------------------- effects */
/* One switch, one opcode each, and a return value saying whether anything
 * actually changed.  That return is the inert-play measure -- "the share
 * of played cards that changed nothing", which the parent game reported
 * at 53% and which the design calls the most honest number the engine
 * produces.  It is only honest if this function is scrupulous, so every
 * arm that can do nothing says so. */
/* A House's whole Standing.  phases.c has its own copy for the combat
 * floor; this one is for "the player with the most total power". */
static int total_standing_(const Game *g, int s)
{
    int r, t = 0;
    for (r = 0; r < R_COUNT; r++) t += g->h[s].standing[r];
    return t;
}

static int apply_effect(Game *g, int s, int t, const Card *c, int e)
{
    int op = c->eff[e].op, a = c->eff[e].a, b = c->eff[e].b;
    int cond = c->eff[e].cond, i, j, did = 0, r;

    /* A World card or a Court card has no owner: "the world turns" for
     * both Houses, so card_play_world passes seat -1.  Most opcodes
     * index g->h[s] on their first line, and an effect that reaches
     * for an actor that is not there reads g->h[-1].
     *
     * This was latent and became reachable when the pool conversion
     * put World-deck cards onto actor-dependent opcodes.  ASan found
     * it; nothing else would have, because g->h[-1] is inside the Game
     * struct and reads as plausible rubbish rather than crashing.
     *
     * The whitelist is explicit rather than a guard on each opcode:
     * these are the things that can happen to the whole table at once
     * with nobody having done them, and anything not named here simply
     * does not resolve without a House behind it. */
    if (s < 0) {
        switch (op) {
        case OP_UNREST_ALL: case OP_LEVY_ALL_GAIN: case OP_LEVY_ALL_LOSE:
        case OP_STANDING_ALL_LOSS: case OP_GRIEVANCE_SCALE:
        case OP_SERVITUDE_ALL: case OP_NO_COMBAT: case OP_NO_ASSASSIN:
        case OP_LP_PAY: case OP_LP_GAIN: case OP_MAKE_PARAMOUNT:
        case OP_RANDOM_LOSS: case OP_COUNCIL: case OP_NO_CROWNING:
        case OP_FAVOUR_LOCK: case OP_COURT_SILENT: case OP_LOWEST_GAIN:
        case OP_NO_TAX: case OP_REVOLUTION_ALL: case OP_DISCARD_TARGET:
        /* Added after the first list cost the Court its whole road.
         * OP_FAVOUR_CMP is what the Court deck IS -- "the Court,
         * without preference", comparing the two Houses and giving
         * Favour to one -- and leaving it out took Favour of the Court
         * from 32% of wins to 3.3%.  A whitelist that is too short
         * does not fail, it quietly removes a way to win.
         *
         * Every opcode here was checked against its own body for a
         * use of `s` rather than guessed at a second time. */
        case OP_FAVOUR_CMP: case OP_MERC_MARKET: case OP_RECKONING_NOW:
        case OP_STANDING_LOSS_LP: case OP_VOTE_FREE:
            break;
        default:
            return 0;
        }
    }

    switch (op) {
    case OP_NONE: return 0;

    case OP_LEVY:      levy_add(g, s, a, b); return 1;
    case OP_STANDING:
        if (g->h[s].standing[a] >= STANDING_MAX) return 0;
        standing_add(g, s, a, b); return 1;
    case OP_LEVY_STEAL:
        if (t < 0 || g->h[t].levy[a] <= 0) return 0;
        i = b < g->h[t].levy[a] ? b : g->h[t].levy[a];
        levy_add(g, t, a, -i); levy_add(g, s, a, i); return 1;
    case OP_LEVY_STEAL_LP:
        t = g->paramount;
        if (t < 0 || t == s || g->h[t].levy[a] <= 0) return 0;
        i = b < g->h[t].levy[a] ? b : g->h[t].levy[a];
        levy_add(g, t, a, -i); levy_add(g, s, a, i); return 1;
    case OP_STANDING_LOSS:
        if (t < 0 || !target_ok(g, s, t, cond)) return 0;
        r = (a < 0) ? pick_enemy_loss(g, t) : a;
        if (g->h[t].standing[r] <= 0) return 0;
        standing_add(g, t, r, -b); check_death(g, t); return 1;
    case OP_STANDING_LOSS_LP:
        t = g->paramount;
        if (t < 0) return 0;
        r = pick_own_loss(g, t);
        if (g->h[t].standing[r] <= 0) return 0;
        standing_add(g, t, r, -b); check_death(g, t); return 1;
    case OP_LEVY_ALL_LOSE:
        for (i = 0; i < g->nhouses; i++)
            if (g->h[i].alive && g->h[i].levy[a] > 0) { levy_add(g, i, a, -b); did = 1; }
        return did;
    case OP_LEVY_ALL_GAIN:
        /* a < 0 is every resource, the same shape OP_STANDING_ALL_LOSS
         * takes: "all players gain +2 in all stats". */
        for (i = 0; i < g->nhouses; i++) {
            if (!g->h[i].alive) continue;
            if (a >= 0) levy_add(g, i, a, b);
            else for (r = 0; r < R_COUNT; r++) levy_add(g, i, r, b);
            did = 1;
        }
        return did;
    case OP_STANDING_ALL_LOSS:
        /* a < 0 is every resource at once, which is what a Cataclysm
         * means by "all players lose half their resources".  Indexing
         * standing[-1] was reachable the moment the Cataclysm deck was
         * dealt, and ASan found it the same evening. */
        for (i = 0; i < g->nhouses; i++) {
            if (!g->h[i].alive) continue;
            if (a >= 0) {
                if (g->h[i].standing[a] > 0) {
                    standing_add(g, i, a, -b); did = 1;
                }
            } else {
                for (r = 0; r < R_COUNT; r++)
                    if (g->h[i].standing[r] > 0) {
                        standing_add(g, i, r, -b); did = 1;
                    }
            }
            check_death(g, i);
        }
        return did;
    case OP_MERC_MARKET:
        /* Open to every House, and taken by any that can pay.  A World
         * card acts on the realm without preference. */
        for (i = 0; i < g->nhouses; i++)
            if (g->h[i].alive && g->h[i].levy[R_GOLD] >= 3) {
                levy_add(g, i, R_GOLD, -3); levy_add(g, i, R_MIL, b); did = 1;
            }
        return did;
    case OP_LEVY_PER_KEPT:
        i = g->h[s].kept_n * b;
        if (!i) return 0;
        levy_add(g, s, a, i); return 1;
    case OP_LEVY_KEEP:
        g->h[s].keep_levy[a] = 1; return g->h[s].levy[a] > 0;
    case OP_VASSAL_LEVY:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].revealed
             && g->lord[i].holder == s) { did = 1; }
        if (did) levy_add(g, s, a, b);   /* dues flow to the House */
        return did;

    case OP_UNREST_CLEAR:
        if (!unrest_total(g, s)) return 0;
        unrest_add(g, s, a, b < 0 ? -UNREST_MAX : -b); return 1;
    case OP_UNREST_TARGET:
        if (t < 0 || !target_ok(g, s, t, cond)) return 0;
        unrest_add(g, t, a, b); return 1;
    case OP_UNREST_SELF:   unrest_add(g, s, a, b); return 1;
    case OP_CRACKDOWN:
        /* "military crackdown which clears 2 and gives every other House
         * 1 Grievance against you" -- a sink for your Unrest bought with
         * everyone else's Grievance, which is itself a currency. */
        if (!unrest_total(g, s)) return 0;
        unrest_add(g, s, a, -b);
        for (i = 0; i < g->nhouses; i++)
            if (i != s && g->h[i].alive) grievance_add(g, i, 1);
        return 1;
    case OP_GRIEVANCE_SELF:
        if (g->h[s].grievance >= GRIEVANCE_MAX) return 0;
        grievance_add(g, s, b); return 1;
    case OP_GRIEVANCE_TARGET:
        if (t < 0) return 0;
        grievance_add(g, t, b); return 1;
    case OP_UNREST_ALL:
        for (i = 0; i < g->nhouses; i++)
            if (g->h[i].alive) { unrest_add(g, i, a, b); did = 1; }
        return did;
    case OP_NO_TAX:
        g->regent = -2;                    /* -2: no tax at all this round */
        if (g->paramount >= 0) unrest_add(g, g->paramount, -1, b);
        return 1;
    case OP_REGENCY:
        if (g->paramount < 0 || g->paramount == s) return 0;
        g->regent = (signed char)s; return 1;

    case OP_SERVITUDE: {
        /* A Lever reaches a lord by Trait.  Unbound lords and the other
         * House's lords are both fair game: binding one of theirs is how
         * you take a vote off the Council. */
        Lord *l;
        i = find_lord(g, s, cond, 2);
        if (i < 0) i = find_lord(g, s, cond, 0);
        if (i < 0) return 0;
        l = &g->lord[i];
        if (l->servitude >= SERVITUDE_MAX && l->holder == s) return 0;
        if (l->holder != s) { l->holder = (signed char)s; l->servitude = 0;
                              l->revealed = 0; }
        /* THE OX: the yoke goes on one notch deeper. */
        if (g->h[s].id == H_STONEGARTH) b++;
        l->servitude = (unsigned char)clampi(l->servitude + b, 0, SERVITUDE_MAX);
        g->last_lord = (signed char)i;
        court_log("  %s tightens a Bond on %s", HOUSE_NAME[g->h[s].id],
                  CARDS[l->card].name);
        return 1;
    }
    case OP_BOND_RIDER:
        if (g->last_lord < 0) return 0;
        if (a == BR_NO_BRUTAL)  g->lord[g->last_lord].no_betray  = 1;
        if (a == BR_DOUBLE_REV) g->lord[g->last_lord].double_rev = 1;
        return 1;
    case OP_LORD_TRAIT:
        /* A lord is not played for an effect; playing it puts it on the
         * table unbound, where either House may reach it. */
        if (g->lord_n >= MAX_LORDS) return 0;
        memset(&g->lord[g->lord_n], 0, sizeof g->lord[g->lord_n]);
        g->lord[g->lord_n].card   = (short)(c - CARDS);
        g->lord[g->lord_n].trait  = (unsigned char)a;
        g->lord[g->lord_n].holder = -1;
        g->lord[g->lord_n].alive  = 1;
        if (a == LT_AMBITIOUS) g->lord[g->lord_n].double_rev = 1;
        if (a == LT_PROUD)     g->lord[g->lord_n].no_betray  = 1;
        g->lord_n++;
        return 1;
    case OP_LORD_FREE:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].revealed) {
                int who = g->lord[i].holder;
                g->lord[i].holder = -1; g->lord[i].revealed = 0;
                g->lord[i].servitude = g->lord[i].revolution = 0;
                if (who >= 0) unrest_add(g, who, a, b);
                return 1;
            }
        return 0;
    case OP_LORD_DIRECT:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].revealed
             && g->lord[i].holder != s && g->lord[i].holder >= 0) {
                g->directed[i] = (signed char)(s + 1);
                return 1;
            }
        return 0;
    case OP_REVOLUTION_ZERO:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder == s
             && g->lord[i].revolution) { g->lord[i].revolution = 0; return 1; }
        return 0;
    case OP_REVOLUTION_FREEZE:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder == s
             && g->lord[i].revealed) { g->lord[i].frozen = 1; return 1; }
        return 0;
    case OP_REVOLUTION_ALL:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder >= 0
             && g->lord[i].revolution < REVOLUTION_MAX) {
                g->lord[i].revolution = (unsigned char)
                    clampi(g->lord[i].revolution + b, 0, REVOLUTION_MAX);
                did = 1;
            }
        return did;
    case OP_REVOLUTION_THEIRS:
        /* Their lords, not every lord.
         *
         * "Add hidden Revolution to a lord that is not yours" is what
         * an Instigator does; OP_REVOLUTION_ALL reaches every Bond in
         * play including the caster's own.  Vipren binds lords for a
         * living and a third of its deck was translated onto the
         * indiscriminate one, so it spent the game poisoning itself:
         * 5.7% of wins against 36.7% for Ravenmark. */
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder >= 0
             && g->lord[i].holder != s
             && g->lord[i].revolution < REVOLUTION_MAX) {
                g->lord[i].revolution = (unsigned char)
                    clampi(g->lord[i].revolution + b, 0, REVOLUTION_MAX);
                did = 1;
            }
        return did;
    case OP_REVOLT_CANCEL:
    case OP_PARDON:
        /* Aldemar's pardon and Ravenmark's crackdown both reach a lord
         * that is turning, and do opposite things about it. */
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder == s
             && g->lord[i].revolution >= 3) {
                g->lord[i].revolution = 0;
                if (op == OP_PARDON)
                    g->lord[i].servitude = (unsigned char)
                        clampi(g->lord[i].servitude + b, 0, SERVITUDE_MAX);
                return 1;
            }
        return 0;
    case OP_MANUMIT:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].revealed) {
                int who = g->lord[i].holder;
                g->lord[i].holder = -1; g->lord[i].revealed = 0;
                g->lord[i].servitude = g->lord[i].revolution = 0;
                if (who >= 0) unrest_add(g, who, -1, b);
                return 1;
            }
        return 0;
    case OP_SPY: case OP_SPY_ANY: {
        /* "A House may spy on a lord it holds at a cost, learning the
         * count, with a risk of detection that gives the lord 1 free
         * Revolution if it fails." */
        int fail;
        for (i = 0; i < g->lord_n; i++) {
            if (!g->lord[i].alive) continue;
            if (op == OP_SPY && g->lord[i].holder != s) continue;
            if (g->lord[i].holder < 0) continue;
            /* "Your trackers cannot be spied on this round." */
            if (g->lord[i].holder != s
             && g->round < g->no_spy_until[g->lord[i].holder]) continue;
            /* Ravenmark's spying always fails; the Ox does not spy at
             * all.  It binds in the open and has no use for whispers. */
            if (g->h[s].id == H_STONEGARTH) return 0;
            fail = g->h[s].id == H_RAVENMARK ? 1
                 : g->h[s].id == H_VIPREN    ? 0
                 : roll(g, 3) == 0;
            if (fail) g->lord[i].revolution = (unsigned char)
                          clampi(g->lord[i].revolution + 1, 0, REVOLUTION_MAX);
            else      g->lord[i].lord_knows = 1;
            return 1;
        }
        return 0;
    }
    case OP_EXPOSE:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder >= 0
             && g->lord[i].holder != s) {
                g->lord[i].lord_knows = 1; g->lord[i].revealed = 1;
                return 1;
            }
        return 0;
    case OP_SELL_SECRET:
        for (i = 0; i < g->lord_n; i++)
            if (g->lord[i].alive && g->lord[i].holder >= 0
             && g->lord[i].holder != s && !g->lord[i].revealed) {
                g->lord[i].revealed = 1;
                levy_add(g, s, a, b);
                return 1;
            }
        return 0;
    case OP_INSTIGATOR:
        /* "Ravenmark cannot send Instigators." */
        if (g->h[s].id == H_RAVENMARK) return 0;
        g->instigators_left = (unsigned char)(g->instigators_left + b);
        return 1;
    case OP_INSTIGATOR_REDIRECT:
        for (i = 0; i < g->pending_n; i++)
            if (g->pending[i].from != s) {
                g->pending[i].to = (signed char)(t >= 0 ? t : s);
                return 1;
            }
        return 0;

    case OP_PROPOSE: case OP_PROPOSE_BONUS: case OP_PROPOSE_PUBLIC:
    case OP_PROPOSE_DOUBLE:
        return 0;   /* proposals are an action, not an effect; see A_PROPOSE */
    case OP_PROMISE_CANCEL:
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_LIVE
             && (g->promise[i].promisee == s || g->promise[i].promiser == s)) {
                g->promise[i].state = P_VOID; return 1;
            }
        return 0;
    case OP_PROMISE_LAUNDER:
        if (g->h[s].laundered) return 0;
        g->h[s].laundered = 2;             /* 2: armed.  1: spent.        */
        return 1;
    case OP_PROMISE_CLAIM:
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_LIVE && g->promise[i].promisee != s
             && g->promise[i].promiser != s) {
                g->promise[i].promisee = (signed char)s; return 1;
            }
        return 0;
    case OP_PROMISE_COERCE:
        if (t < 0) return 0;
        standing_add(g, t, a, -b); check_death(g, t);
        return 1;
    case OP_BROKEN_TRANSFER:
        if (t < 0 || !g->h[s].broken_n) return 0;
        i = g->h[s].broken[--g->h[s].broken_n];
        if (g->h[t].broken_n < MAX_PROMISES)
            g->h[t].broken[g->h[t].broken_n++] = (short)i;
        return 1;
    case OP_CALL_DEBT:
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_BROKEN && g->promise[i].promisee == s) {
                int who = g->promise[i].promiser;
                for (r = 0; r < R_COUNT; r++)
                    if (g->h[who].levy[r] > 0) {
                        levy_add(g, who, r, -b); levy_add(g, s, r, b); did = 1;
                    }
            }
        return did;
    case OP_BROKEN_TRUCE:
        if (t < 0 || !target_ok(g, s, t, TC_PEACE)) return 0;
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_LIVE && g->promise[i].promiser == s
             && g->promise[i].promisee == t
             && (g->promise[i].term == TERM_PEACE)) {
                promise_break(g, i); break;
            }
        g->war[s][t] = 1; g->peace_broken_ok = 1;
        return 1;
    case OP_RUMOUR:
        if (t < 0) return 0;
        grievance_add(g, t, b * (g->nhouses - 1));
        return 1;
    case OP_REALM_REMEMBERS:
        for (i = 0; i < g->nhouses; i++) {
            int rep = g->h[i].broken_n;
            if (!g->h[i].alive || !rep) continue;
            standing_add(g, i, R_CAP, -b * rep); check_death(g, i); did = 1;
        }
        return did;
    case OP_RECKONING_NOW:
        for (i = 0; i < g->promise_n; i++)
            if (g->promise[i].state == P_LIVE) {
                g->promise[i].due_round = (short)g->round;
                g->promise[i].due_at_council = 0;
                g->promise[i].due_on_attack  = 0;
                did = 1;
            }
        return did;

    case OP_DECLARE_WAR:
        if (t < 0 || !g->h[t].alive || t == s) return 0;
        g->war[s][t] = 1;
        if (a == 1) g->peace_broken_ok = 1;      /* No Quarter commits first */
        court_log("  %s declares Open War on %s", HOUSE_NAME[g->h[s].id],
                  HOUSE_NAME[g->h[t].id]);
        return 1;
    case OP_COMBAT_LEVY:     g->combat_bonus[s]     += (signed char)b; return 1;
    case OP_COMBAT_LEVY_DEF: g->combat_bonus_def[s] += (signed char)b; return 1;
    case OP_COMBAT_REDUCE:   g->combat_bonus_def[s] += (signed char)b; return 1;
    case OP_COMBAT_ATT_LOSS:
        /* "Attacker loses 1 Military."  The Boiling Oil, The Murder
         * Holes, The Winter: the price of attacking, paid whoever wins.
         * It comes off Standing rather than the commitment, because the
         * card says Military and not committed Military -- and because
         * a game whose only cost of attacking is Grievance that
         * Ravenmark does not pay is the game this is here to end. */
        for (i = 0; i < g->nhouses; i++)
            if (i != s && g->h[i].alive && g->war[i][s]) {
                standing_add(g, i, R_MIL, -b);
                check_death(g, i);
                did = 1;
            }
        return did;
    case OP_NO_COMBAT:
        /* The King's Peace, Martial Decree, The Price of Peace.  A hard
         * stop for one round, which is the only answer in the game to a
         * House that declares Open War free every single turn. */
        g->no_combat_until = (unsigned char)(g->round + 1);
        return 1;
    case OP_CANCEL_NEXT:
        /* "Cancel a Council Event", "Cancel enemy's next card", "Cancel
         * an Instigator targeting you", "negate a bribe against you".
         * All the same move: the next thing the other House plays does
         * not happen.  It needs no card-in-play zone -- the card that
         * is cancelled has not been played yet -- which is what makes
         * this family reachable when stealing a card off the table is
         * not. */
        if (t < 0 || !g->h[t].alive) return 0;
        g->cancel_next[t] = 1;
        return 1;
    case OP_STEAL_CARD: {
        /* Off the end of the hand, the same place OP_DISCARD_TARGET
         * takes from: the least recently drawn, and the one its owner
         * has most plainly chosen not to play. */
        int card;
        if (t < 0 || !g->h[t].alive || g->h[t].hand_n <= 0) return 0;
        card = g->h[t].hand[g->h[t].hand_n - 1];
        hand_remove(g, t, g->h[t].hand_n - 1);
        hand_add(g, s, card);
        return 1;
    }
    case OP_GIVE_CARD: {
        /* "Give target a card; it costs them 1 resource."  A gift with
         * a price on it, which is a bribe -- and this is the House
         * whose whole game is buying people. */
        int card;
        if (t < 0 || !g->h[t].alive || g->h[s].hand_n <= 0) return 0;
        card = g->h[s].hand[g->h[s].hand_n - 1];
        hand_remove(g, s, g->h[s].hand_n - 1);
        hand_add(g, t, card);
        levy_add(g, t, pick_enemy_loss(g, t), -(b ? b : 1));
        return 1;
    }
    case OP_NO_SPOIL:
        /* "Retreat without penalty", "Attacker gains nothing".  There
         * is no retreat in this design -- combat is one blind commit --
         * but what a retreat BOUGHT was getting out without paying, and
         * that is a thing the combat already has a step for. */
        g->no_spoil = 1;
        return 1;
    case OP_SHIELD:
        g->shield[s][a] = 1;
        return 1;
    case OP_TAX_DOUBLE:
        g->tax_double = 1;
        return 1;
    case OP_LEVY_DOUBLE:
        /* The optimizer.  Levy is SET to Standing each turn rather than
         * drawn, so nothing in this game multiplied it -- every economy
         * card was a flat one-off.  A doubler is the first card that
         * scales with what you have already built, which is what makes
         * a resource worth growing rather than merely holding. */
        if (g->h[s].levy[a] <= 0) return 0;
        levy_add(g, s, a, g->h[s].levy[a]);
        return 1;
    case OP_INVEST: {
        /* The mine, and the sink.  Measured over 400 games, Goldwyn
         * threw away 61% of every Levy it was handed: it starts on Gold
         * 8, gains 2 more a turn, and the things Gold buys are all
         * capped -- purchased Favour at +3 EVER, its bought votes at 2
         * a Council.  Past that its Gold is dead money.
         *
         * This is the one purchase with no ceiling: surplus Levy
         * becomes permanent Standing, which is both next turn's Levy
         * and the thing The Final King is counted on.  It is priced
         * high on purpose -- a House should reach it when it is
         * drowning, not on turn one. */
        int pay = b > 0 ? b : 4;
        if (g->h[s].levy[a] < pay) return 0;
        if (g->h[s].standing[a] >= STANDING_MAX) return 0;
        levy_add(g, s, a, -pay);
        standing_add(g, s, a, 1);
        court_log("  %s puts its %s to work", HOUSE_NAME[g->h[s].id],
                  a == R_MIL ? "swords" : a == R_CAP ? "name" : "gold");
        return 1;
    }
    case OP_CLAIM_THRONE:
        /* "Claim the throne by force."  Not a win: it makes the House
         * Throneworthy, which is the gate on A_CHALLENGE, so the card
         * buys the right to try and the challenge still has to be won.
         * A card that simply handed over the Throne would be a win
         * condition bought for one Gold. */
        if (g->h[s].throneworthy) return 0;
        g->h[s].throneworthy = 1;
        court_log("  %s lays claim to the Throne", HOUSE_NAME[g->h[s].id]);
        return 1;
    case OP_MAKE_PARAMOUNT: {
        /* "Player with most Gold becomes Lord Paramount."  The office
         * carries the tax, so moving it is a real effect even though
         * the design elects it at a Council; these cards are the
         * Chaos-deck way of overturning that election. */
        /* Which House the office passes to.  The pool names five
         * different tests and they are all questions about Standing,
         * so they share one opcode and differ in a. */
        int pick = -1, better;
        for (i = 0; i < g->nhouses; i++) {
            if (!g->h[i].alive) continue;
            if (pick < 0) { pick = i; continue; }
            switch (a) {
            case 1:  better = g->h[i].standing[R_GOLD] > g->h[pick].standing[R_GOLD]; break;
            case 2:  better = g->h[i].standing[R_MIL]  > g->h[pick].standing[R_MIL];  break;
            case 3:  better = g->h[i].standing[R_CAP]  > g->h[pick].standing[R_CAP];  break;
            case 4:  better = total_standing_(g, i) > total_standing_(g, pick);       break;
            default: better = g->h[i].standing[R_MIL]  < g->h[pick].standing[R_MIL];  break;
            }
            if (better) pick = i;
        }
        if (pick < 0 || pick == g->paramount) return 0;
        g->paramount = (signed char)pick;
        court_log("  the Paramountcy passes to %s", HOUSE_NAME[g->h[pick].id]);
        return 1;
    }
    case OP_LP_GAIN:
        /* The Lord Paramount's own purse rather than a transfer.  With
         * nobody elected there is no Paramount to pay, which is the
         * usual state of a Council of two that elects nobody. */
        if (g->paramount < 0) return 0;
        levy_add(g, g->paramount, a, b);
        return 1;
    case OP_SERVITUDE_ALL:
        /* Every Bond on the table moved at once.  Servitude is held by
         * the House and never drops below 0; a Proud lord "cannot be
         * bound below Servitude 2", which holds here as everywhere. */
        for (i = 0; i < g->lord_n; i++) {
            Lord *l = &g->lord[i];
            int floor_ = l->trait == LT_PROUD ? 2 : 0;
            int v;
            if (!l->alive || l->holder < 0) continue;
            v = l->servitude + b;
            if (v < floor_) v = floor_;
            if (v > SERVITUDE_MAX) v = SERVITUDE_MAX;
            if (v != l->servitude) { l->servitude = (unsigned char)v; did = 1; }
        }
        return did;
    case OP_NO_SPY:
        g->no_spy_until[s] = (unsigned char)(g->round + 1);
        return 1;
    case OP_RANDOM_LOSS: {
        /* "A random player loses 1 Capital", "All players lose 1 random
         * resource".  Chance is already a thing the design prices --
         * an assassination is one in three -- so a card that names it
         * is not out of keeping.  a < 0 means the resource is rolled
         * too. */
        int who = roll(g, g->nhouses);
        int res = a < 0 ? roll(g, R_COUNT) : a;
        if (!g->h[who].alive) return 0;
        standing_add(g, who, res, -b);
        check_death(g, who);
        return 1;
    }
    case OP_LP_PAY: {
        /* "All players pay 1 Gold to the Lord Paramount", and the same
         * card run backwards.  b is signed: positive collects, negative
         * disburses.  With no Paramount elected there is nobody to pay,
         * which is the usual state of a Council that elects nobody. */
        int lp = g->paramount;
        if (lp < 0) return 0;
        for (i = 0; i < g->nhouses; i++) {
            if (i == lp || !g->h[i].alive) continue;
            levy_add(g, i, R_GOLD, -b);
            levy_add(g, lp, R_GOLD, b);
            did = 1;
        }
        return did;
    }
    case OP_NO_ASSASSIN:
        g->no_assassin_until = (unsigned char)(g->round + 1);
        return 1;
    case OP_SKIP_ACTION:
        /* "Cancel target's next House Action", "Target cannot act next
         * turn".  Taken off the draws the target gets, which is what a
         * House Action is here. */
        if (t < 0 || !g->h[t].alive) return 0;
        g->skip_actions[t] = (unsigned char)(g->skip_actions[t] + (b ? b : 1));
        return 1;
    case OP_LEVY_GIVE:
        /* A gift, which is a real card: it buys a Promise, or it is the
         * bribe half of a Lever.  Distinct from OP_LEVY_ALL_GAIN, which
         * pays you as well and is therefore a different card. */
        if (t < 0 || !g->h[t].alive) return 0;
        levy_add(g, t, a, b);
        return 1;
    case OP_SURVIVE:
        g->h[s].survives = 1;
        return 1;
    case OP_COMBAT_LEVY_IF: {
        int ok2 = 0, other = -1;
        for (i = 0; i < g->nhouses; i++) if (i != s && g->h[i].alive) other = i;
        if (other < 0) return 0;
        switch (a) {
        case CIF_STRONG:     ok2 = g->h[s].standing[R_MIL] >= 3; break;
        case CIF_OUTNUMBER:  ok2 = g->h[s].levy[R_MIL] > g->h[other].levy[R_MIL]; break;
        case CIF_MORE_CARDS: ok2 = g->h[s].hand_n > g->h[other].hand_n; break;
        case CIF_TRUSTED:    ok2 = target_ok(g, s, other, TC_KEPT); break;
        }
        if (!ok2) return 0;
        g->combat_bonus[s] += (signed char)b;
        return 1;
    }
    case OP_DISCARD_TARGET: {
        /* design.xml <combat> already names this as one of the three
         * things a winner may take -- "or forces the discard of two
         * cards" -- so the discard is a mechanic the design has and the
         * engine did not.  Taken off the end of the hand, which is the
         * least recently drawn card and the one a House has most
         * plainly chosen not to play. */
        int k;
        if (t < 0 || !g->h[t].alive) return 0;
        for (k = 0; k < b && g->h[t].hand_n > 0; k++) {
            int card = g->h[t].hand[g->h[t].hand_n - 1];
            hand_remove(g, t, g->h[t].hand_n - 1);
            deck_discard(g, CARDS[card].deck, card);
            did = 1;
        }
        return did;
    }
    case OP_GRIEVANCE_SCALE:
        /* "All Grievances are doubled" / "are halved".  Grievance is
         * capped at 10 and grievance_add does the capping, so this goes
         * through it rather than writing the field. */
        for (i = 0; i < g->nhouses; i++)
            if (g->h[i].alive && g->h[i].grievance) {
                int gr = g->h[i].grievance;
                grievance_add(g, i, b == 2 ? gr : -(gr / 2));
                did = 1;
            }
        return did;
    case OP_LOWEST_GAIN: {
        /* "Player with lowest Military gains +2 Military Levy."  With
         * four players that is a lottery; in a duel it is exactly the
         * House being ground down.  Ties give nobody the bonus, so the
         * card does nothing rather than paying it twice. */
        int lo = -1, tie = 0;
        for (i = 0; i < g->nhouses; i++) {
            if (!g->h[i].alive) continue;
            if (lo < 0 || g->h[i].standing[a] < g->h[lo].standing[a]) {
                lo = i; tie = 0;
            } else if (g->h[i].standing[a] == g->h[lo].standing[a]) tie = 1;
        }
        if (lo < 0 || tie) return 0;
        levy_add(g, lo, a, b);
        return 1;
    }
    case OP_COALITION:
        /* "add your Military Levy to another House's attack this round" */
        for (i = 0; i < g->nhouses; i++)
            if (i != s && g->h[i].alive)
                for (j = 0; j < g->nhouses; j++)
                    if (g->war[i][j] == 1) {
                        g->combat_bonus[i] += g->h[s].levy[R_MIL];
                        g->h[s].levy[R_MIL] = 0;
                        return 1;
                    }
        return 0;
    case OP_THRONE_LEVY:  g->throne_bonus[s] += (signed char)b; return 1;
    case OP_THRONE_BAR:
        if (t < 0) return 0;
        g->throne_bonus[t] = -100;         /* may not commit              */
        return 1;
    case OP_THRONE_OATH:
        for (i = 0; i < g->nhouses; i++)
            if (i != s && g->h[i].broken_n) { g->throne_bonus[i] -= (signed char)b; did = 1; }
        return did;
    case OP_THRONE_DISCOUNT:
        g->h[s].throne_price[a] = (unsigned char)(g->h[s].throne_price[a] + b);
        return 1;
    case OP_ASSASSINATE: {
        /* "Aldemar cannot assassinate."  Nobody else is stopped, and the
         * attempt is a coin the design does not price, so it is priced
         * here at one in three and said so in the log. */
        int lp = g->paramount;
        if (g->h[s].id == H_ALDEMAR || lp < 0 || lp == s) return 0;
        if (g->round < g->no_assassin_until) return 0;
        if (roll(g, 3) == 0) {
            standing_add(g, lp, pick_enemy_loss(g, lp), -3);
            check_death(g, lp);
            court_log("  the Lord Paramount is struck down");
        } else {
            unrest_add(g, s, -1, 1);
            court_log("  the blade misses; %s is blamed", HOUSE_NAME[g->h[s].id]);
        }
        return 1;
    }
    case OP_EXTRA_DRAW:
        /* Warlord: "After winning a combat, take a second Draw action
         * this turn."  The condition is that card's text and belongs
         * to it, not to the opcode -- the pool has a dozen plain
         * "Draw 2 War cards" that were routed here and silently did
         * nothing until a combat had been won, which is most of the
         * game.  Those go to OP_DRAW. */
        if (!g->h[s].combats_won) return 0;
        g->draws_left = (unsigned char)(g->draws_left + b); return 1;
    case OP_DRAW:
        g->draws_left = (unsigned char)(g->draws_left + (b ? b : 1));
        return 1;
    case OP_RESTORE: {
        /* design.xml sets the baseline: "returning at 1 Standing in each
         * resource with its Promise piles intact".  A card that returns
         * you better than that is doing what cards do -- Winter Camp is
         * already "the one exception" to the Levy rule -- so b carries
         * the level rather than the pool's 26 "full stats" cards being
         * flattened into one.
         *
         * The Court decides whether the claim is heard at all.  It
         * already "casts one vote, for the House with the higher
         * Favour", so it is the organ that says who is legitimate, and
         * outbidding a pretender for recognition is the exorcism. */
        int r2, base;
        if (!g->h[s].fallen || g->h[s].extinguished) return 0;
        for (i = 0; i < g->nhouses; i++)
            if (i != s && g->h[i].alive && g->h[i].favour >= g->h[s].favour) {
                court_log("  the Court does not hear %s's claim",
                          HOUSE_NAME[g->h[s].id]);
                return 0;
            }
        base = b >= 3 ? 3 : b == 2 ? 2 : 1;
        for (r2 = 0; r2 < R_COUNT; r2++)
            g->h[s].standing[r2] = (signed char)base;
        g->h[s].fallen = 0;
        g->h[s].alive = 1;
        court_log("  %s is restored, and remembers",
                  HOUSE_NAME[g->h[s].id]);
        return 1;
    }
    case OP_EXTINGUISH:
        /* "Final Death: permanently eliminated, no resurrection
         * possible", and the several cards that cancel a restoration.
         * Drawn by the fallen House itself off its own pile, it is its
         * own bad luck; played by the living one, it is the knife. */
        for (i = 0; i < g->nhouses; i++)
            if (g->h[i].fallen && !g->h[i].extinguished) {
                g->h[i].extinguished = 1;
                court_log("  %s's claim dies with it", HOUSE_NAME[g->h[i].id]);
                did = 1;
            }
        return did;
    case OP_VOTE_DOUBLE:
        /* "Your vote counts twice."  design.xml <council>: "Neither
         * House votes" -- the Minor Lords vote, for whoever holds
         * their Servitude.  So a House's vote counting double is its
         * LORDS each casting one more, which is also why a House with
         * no lords gets nothing from this: "A House with no lords has
         * no voice, which is the cost of never binding one." */
        g->vote_double[s] = 1;
        return 1;
    case OP_TIE_BREAK:
        g->tie_break = (signed char)s;
        return 1;
    case OP_VOTE_NULLIFY:
        if (t < 0 || !g->h[t].alive) return 0;
        g->vote_nullified = (signed char)t;
        return 1;
    case OP_NO_PROMISE:
        /* "Target cannot form alliances for 3 rounds."  An alliance in
         * this design is a Promise -- design.xml names one, PRM008 The
         * Sworn Alliance, Term Restraint -- so barring alliances is
         * barring the other House from offering. */
        if (t < 0 || !g->h[t].alive) return 0;
        g->no_promise_until[t] = (unsigned char)(g->round + (b ? b : 3));
        return 1;
    case OP_WIN_GOLD:
        /* The Golden Throne.  A fifth road, and the only one that is
         * purely Gold: no Throneworthy gate and no Capital, which is
         * what makes it Goldwyn's and not Aldemar's.  It is checked
         * rather than granted -- the card does nothing unless the
         * Standing is already there -- so it is the last step of a
         * road and not a shortcut past one. */
        if (g->h[s].standing[R_GOLD] < g->golden_throne) return 0;
        g->winner = (signed char)s;
        g->win_reason = "The Golden Throne";
        court_log("  %s buys the realm outright", HOUSE_NAME[g->h[s].id]);
        return 1;
    case OP_DRAW_ALL:
        /* "All players draw 1 extra House Action."  Only the House
         * whose turn it is has draws left to add to, so the others
         * bank theirs and take them when their turn comes. */
        for (i = 0; i < g->nhouses; i++)
            if (g->h[i].alive) {
                if (i == g->seat_turn)
                    g->draws_left = (unsigned char)(g->draws_left + b);
                else
                    g->banked_draws[i] = (unsigned char)(g->banked_draws[i] + b);
                did = 1;
            }
        return did;
    case OP_DRAW_DECK: {
        /* "Draw from the Cataclysm deck."  Not a card into a hand: the
         * Cataclysm and Restoration piles are drawn FROM and resolve at
         * once, the way the World deck does.  design.xml calls a
         * Cataclysm "a World card with a larger hammer", and a hammer
         * you could hold in hand and time would be a different thing
         * entirely. */
        int card = r_deck_draw(g, a);
        if (card < 0) return 0;
        court_log("  the realm shakes: %s", CARDS[card].name);
        r_card_play_world(g, s, card);
        return 1;
    }

    case OP_COUNCIL:
        g->council_pending = 1;
        g->council_mod = (unsigned char)b;
        return 1;
    case OP_VOTE_BUY:
        if (g->h[s].id != H_GOLDWYN && g->h[s].levy[R_GOLD] < 2) return 0;
        g->commanded[s] = (signed char)(g->commanded[s] + b);
        return 1;
    case OP_VOTE_GAIN: case OP_VOTE_PER_KEPT:
        i = (op == OP_VOTE_GAIN) ? b : (b ? g->h[s].kept_n / b : 0);
        if (!i) return 0;
        g->commanded[s] = (signed char)(g->commanded[s] + i);
        return 1;
    case OP_VOTE_COMMAND:
        if (t < 0 || !g->h[t].alive) return 0;
        g->commanded[t] = (signed char)(-1 - s);   /* negative: commanded by s */
        return 1;
    case OP_VOTE_FREE:   g->free_votes = 1; return 1;
    case OP_VOTE_CHANGE: return 0;   /* resolved inside the Council       */
    case OP_NO_CROWNING: g->no_crowning_until = (unsigned char)(g->round + 1); return 1;
    case OP_OBJECT_FREE: return 1;

    case OP_AMBITION_DONE:
        for (i = 0; i < AMBITIONS_DEALT; i++) {
            const Card *ac;
            if (g->h[s].amb_done[i] || g->h[s].ambition[i] < 0) continue;
            ac = &CARDS[g->h[s].ambition[i]];
            if (ac->eff[0].op == OP_AMB_STANDING && ac->eff[0].a == a) {
                g->h[s].amb_done[i] = 1; return 1;
            }
        }
        return 0;
    case OP_FAVOUR:
        favour_add(g, s, b); return 1;
    case OP_FAVOUR_CMP: {
        int x = 0, y = 1, win;
        if (g->nhouses < 2) return 0;
        if (a == 0) {                      /* more Broken Words loses     */
            if (g->h[x].broken_n == g->h[y].broken_n) return 0;
            win = g->h[x].broken_n > g->h[y].broken_n ? x : y;
        } else {                           /* higher Capital gains        */
            if (g->h[x].standing[R_CAP] == g->h[y].standing[R_CAP]) return 0;
            win = g->h[x].standing[R_CAP] > g->h[y].standing[R_CAP] ? x : y;
        }
        favour_add(g, win, b); return 1;
    }
    case OP_FAVOUR_LOCK:  g->favour_locked = 1; return 1;
    case OP_COURT_SILENT: g->court_silent  = 1; return 1;

    case OP_PEEK_HAND:
        /* The effect is real -- the seat learns a hand -- but the View is
         * not widened for it, because a widened View is a permanent hole
         * and this is a one-turn fact.  The AI is told through the
         * opponent-model in ai.c and nowhere else. */
        return t >= 0 && g->h[t].hand_n > 0;

    default:
        /* Ambition conditions are checked, never played.  Reaching here
         * with one means a card was put in a hand that should not be in
         * a hand, and that is worth an INV line. */
        if (op >= OP_AMB_STANDING) {
            if (checking) court_inv("ambition %s played as a card", c->id);
            return 0;
        }
        return 0;
    }
}

/* What one card costs this House in one resource, House rules included.
 *
 * This is one function because it was briefly two.  court_actions offered
 * every card in hand and card_play then refused the ones that could not
 * be paid for, so a House picked its best card, had it rejected, and
 * ended its turn -- which read in the log as a House with a full hand
 * and a full purse doing nothing at all.  The invariant checker caught it
 * on the first armed game.  Two copies of a rule is one copy too many.
 */
int card_cost(const Game *g, int s, const Card *c, int r)
{
    int cost = c->cost[r];
    /* "Vipren pays double for Combat cards." */
    if (c->category == C_COMBAT && g->h[s].id == H_VIPREN) cost *= 2;
    /* "Ravenmark ... pays 2 Capital more for every Promise it proposes",
     * which is a cost on the card, not on the action. */
    if (c->type == T_PROMISE && g->h[s].id == H_RAVENMARK && r == RC_CAP)
        cost += 2;
    return cost;
}

int card_affordable(const Game *g, int s, const Card *c)
{
    int r;
    for (r = 0; r < RC_COUNT; r++) {
        int cost = card_cost(g, s, c, r);
        if (!cost) continue;
        if (r == RC_GRV) { if (g->h[s].grievance < cost) return 0; }
        else if (g->h[s].levy[r] < cost) return 0;
    }
    /* A cost in Standing is written "2G!" and is paid from Standing. */
    for (r = 0; r < RC_COUNT; r++)
        if (c->perm[r] && r < R_COUNT && g->h[s].standing[r] < c->perm[r])
            return 0;
    return 1;
}

/* Play a card: pay, resolve, discard, and count whether it did anything. */
int card_play(Game *g, int s, int card, int target)
{
    const Card *c = &CARDS[card];
    int idx = hand_find(g, s, card), r, did = 0, e;
    if (idx < 0) return 0;
    if (!card_affordable(g, s, c)) return 0;

    for (r = 0; r < RC_COUNT; r++) {
        int cost = card_cost(g, s, c, r);
        if (!cost) continue;
        if (r == RC_GRV) grievance_add(g, s, -cost);
        else             levy_add(g, s, r, -cost);
    }
    for (r = 0; r < RC_COUNT; r++)
        if (c->perm[r] && r < R_COUNT) standing_add(g, s, r, -c->perm[r]);

    for (e = 0; e < 2; e++)
        if (apply_effect(g, s, target, c, e)) did = 1;

    hand_remove(g, s, idx);
    deck_discard(g, c->deck, card);
    g->total_plays++;
    if (!did) {
        g->inert_plays++;
        /* Which cards are inert, not just how many.  "Classified" is
         * not "implemented", and the way that lie is told is a card
         * that compiles, plays, and does nothing -- so the name is
         * printable and a soak can count them. */
        if (getenv("COURT_INERT"))
            fprintf(stderr, "INERT\t%s\t%s\t%s\n", c->id, c->name, c->text);
    }
    court_log("  %s plays %s%s", HOUSE_NAME[g->h[s].id], c->name,
              did ? "" : " (inert)");
    return 1;
}

/* A House dragged into a fight on someone else's turn has no Levy at all:
 * Levy is set from Standing at the start of your turn and lost at the end
 * of it, so outside your own turn you hold nothing.  Taken literally that
 * makes every rule about combat meaningless -- "each side commits
 * Military Levy secretly, then reveals" is not a secret if one side can
 * only ever commit zero, and "the challenger must exceed the field's
 * total" is trivially satisfied by a field that cannot field anything.
 *
 * The soak showed exactly that: every attack was a walkover, Claim by
 * Force won 0.8% of games because a challenge was 0 against 0 and 0 does
 * not exceed 0, and half of all games ended with the realm dead because
 * nobody could ever defend.
 *
 * So a House called to fight outside its own turn musters: its Military
 * Levy is set from its Military Standing, less its Unrest, exactly as
 * turn_levy would set it.  This is an engine decision standing in for a
 * design one -- <turn_structure> and <combat> do not say what a defender
 * commits with -- and it belongs in the design before it is trusted.
 */
void r_muster(Game *g, int s)
{
    int n = g->h[s].standing[R_MIL];
    if (g->h[s].unrest[R_MIL] < UNREST_MAX) n -= g->h[s].unrest[R_MIL];
    if (n < 0) n = 0;
    if (g->h[s].levy[R_MIL] < n) g->h[s].levy[R_MIL] = (signed char)n;
}

/* Expose the pieces phases.c needs without exposing the whole file. */
int  r_apply_effect(Game *g, int s, int t, const Card *c, int e)
{ return apply_effect(g, s, t, c, e); }
int  r_roll(Game *g, int n)            { return roll(g, n); }
int  r_deck_draw(Game *g, int d)       { return deck_draw(g, d); }
void r_deck_discard(Game *g, int d, int c) { deck_discard(g, d, c); }
void r_deck_build(Game *g, int d)      { deck_build(g, d); }
void r_deck_shuffle(Game *g, int d)    { deck_shuffle(g, d); }
void r_hand_add(Game *g, int s, int c) { hand_add(g, s, c); }
void r_hand_remove(Game *g, int s, int i) { hand_remove(g, s, i); }
void r_levy_add(Game *g, int s, int r, int n)     { levy_add(g, s, r, n); }
void r_standing_add(Game *g, int s, int r, int n) { standing_add(g, s, r, n); }
void r_grievance_add(Game *g, int s, int n)       { grievance_add(g, s, n); }
void r_unrest_add(Game *g, int s, int r, int n)   { unrest_add(g, s, r, n); }
int  r_unrest_total(const Game *g, int s)         { return unrest_total(g, s); }
int  r_vassal_count_(const Game *g, int s)        { return vassal_count(g, s); }
int  r_lord_abstains(const Lord *l)               { return lord_abstains(l); }
void r_favour_add(Game *g, int s, int n)          { favour_add(g, s, n); }
void r_check_death(Game *g, int s)                { check_death(g, s); }
int  r_pick_own_loss(const Game *g, int s)        { return pick_own_loss(g, s); }
int  r_pick_enemy_loss(const Game *g, int s)      { return pick_enemy_loss(g, s); }

int  r_vassal_count(const Game *g, int l)         { return vassal_count(g, l); }
int  r_target_ok(const Game *g, int a, int t, int c) { return target_ok(g, a, t, c); }
int  r_promise_impossible(const Game *g, int i)   { return promise_impossible(g, i); }
