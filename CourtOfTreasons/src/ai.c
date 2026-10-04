/* The machine seats.
 *
 * Every function here takes a View and never a Game.  That is the whole
 * defence against the bug the design names: "A machine House that reads
 * another's hidden Bond is a bug, and the invariant checker asserts it."
 * The assertion is cheap because there is no field in a View to read.
 *
 * The one part of this file that is not heuristics is the promise
 * valuation, which the design is unusually specific about because the
 * parent game measured what happens when it is got wrong:
 *
 *   "A machine House prices a Promise by what the term would buy the
 *    House being promised, adds any consideration already taken to the
 *    cost of breaking, and uses the same valuation function whether it is
 *    buying or selling.  All three are required together."
 *
 * All three are here, in term_value(), and they are here once.  Pricing
 * from where the promiser stands produces scrupulously honest liars;
 * pricing by what it would buy the promisee, with no cost to reneging,
 * produces a game nobody can win.  The measure that says whether this is
 * right is promises broken, which should sit near one in ten.
 */
#include "court.h"
#include <stdlib.h>
#include <string.h>

/* How a House plays, as opposed to what its rules let it do.
 *
 * The House traits in design.xml are all permissions and prices --
 * Ravenmark declares war free, Vipren sends two Instigators, Goldwyn
 * buys votes.  None of that makes a machine House *behave* like its
 * House: an AI given Ravenmark's free war will still sit and count coin
 * if counting coin scores higher.
 *
 * So style is a table, one row per House, added to the score of whole
 * kinds of action.  A row, not a branch: the engine already carries 22
 * hardcoded id == H_RAVENMARK tests and that was the thing that made
 * adding a fifth House a treasure hunt.  Here a fifth House is six
 * numbers.
 *
 * The numbers are read off the House's own strengths and weaknesses.
 * Ravenmark cannot send Instigators and pays extra for Promises, so it
 * is pushed towards the one thing it is good at.  Vipren pays double for
 * Combat cards and has its Broken Words counted double, so it whispers
 * rather than fights and keeps its word better than its reputation
 * suggests.  Goldwyn buys.  Aldemar sits on the Council.
 */
typedef struct {
    signed char war;       /* declaring war, combat, Military cards      */
    signed char intrigue;  /* Instigators, spying, turning lords         */
    signed char money;     /* growth, buying, Gold                       */
    signed char council;   /* lords, Levers, votes                       */
    signed char promise;   /* proposing, and being willing to be bound   */
    signed char patience;  /* how far behind it will let itself fall     */
} Style;

static const Style STYLE[H_COUNT] = {
    /*                war intr money counc prom  pat */
    /* Ravenmark */ {  26, -22,   -4,   -6, -16,  -8 },
    /* Vipren    */ { -16,  26,    4,    6,   6,   6 },
    /* Goldwyn   */ { -12,   2,   26,   10,  12,   4 },
    /* Aldemar   */ {  -6, -12,   -6,   22,  20,   8 },
    /* The four added 2026-09-20.  Each leans hard into the subsystem it
     * champions, because a House that only mildly prefers a thing plays
     * like the others and the measurements say the others already do
     * not use these systems. */
    /* Leoward   */ {   4, -18,   -4,    8,  30,   6 },
    /* Stonegarth*/ {   2,  -6,    6,   24,   4,  14 },
    /* Wulfren   */ {  14,  10,   -8,    2, -20,  -6 },
    /* Everhold  */ {  20,  -8,   -6,   -8, -10,  16 },
};

static const Style *style_of(const View *v)
{
    /* The View carries the seat's House so a machine can know how it
     * plays without being told which seat it is sitting in. */
    return &STYLE[v->house[v->me] % H_COUNT];
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static unsigned long nxt(unsigned long *r)
{
    unsigned long x = *r;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return *r = x;
}

static int power(const View *v, int s)
{
    return v->standing[s][R_MIL] + v->standing[s][R_CAP] + v->standing[s][R_GOLD];
}

/* Trust, from the piles, exactly as the engine derives it.  The AI is not
 * given a number the rules do not have. */
static int trust(const View *v, int of, int toward)
{
    int i, t = 0;
    for (i = 0; i < v->promise_n; i++) {
        const Promise *p = &v->promise[i];
        if (p->promiser != toward || p->promisee != of) continue;
        if (p->state == P_KEPT)   t++;
        if (p->state == P_BROKEN) t--;
    }
    return t;
}

/* ------------------------------------------------- the one valuation -- */
/* What a term is worth to the House it is promised to.  Not to the one
 * promising it: that is the mistake the design names first, and it is the
 * one that produces a liar who never lies because lying never looks worth
 * it from where they are standing.
 *
 * The same function is called when deciding whether to offer, whether to
 * accept, and what breaking would cost.  Using a different one in any of
 * the three is the other half of the mistake. */
static int term_value(const View *v, int term, int beneficiary)
{
    int val = 0;
    switch (term) {
    case TERM_VOTE:
        /* A vote is worth most when the beneficiary is close to a
         * majority, which is the moment it is also most worth breaking --
         * which is exactly why consideration exists. */
        val = 5;
        if (v->throneworthy[beneficiary]) val += 4;
        break;
    case TERM_PEACE:
        val = 3 + clampi(v->standing[beneficiary][R_MIL] < 4 ? 4 : 1, 0, 6);
        break;
    case TERM_FORBEARANCE:
        /* The strongest promise two players have: a challenge unopposed
         * decides the game at the moment it comes due, which is exactly
         * the promise the design says is hardest to price and most worth
         * breaking. */
        val = 9 + (v->throneworthy[beneficiary] ? 6 : 0);
        break;
    case TERM_RESTRAINT:  val = 5; break;
    case TERM_DISCLOSURE: val = 4; break;
    case TERM_TRIBUTE:     val = 3; break;
    case TERM_MANUMISSION: val = 5; break;
    case TERM_ABSTENTION:  val = 3; break;
    case TERM_ANY:         val = 4; break;
    }
    return val;
}

/* What it costs to be known to have broken your word.  Reputation is a
 * ratchet in this design -- <open_questions> Q4 asks whether it should be
 * -- so the cost grows with the pile rather than being flat. */
/* What breaking costs, which at two players is almost entirely what the
 * Court thinks.  There is no table for an offence to be against: every
 * living House is the promisee, so the Grievance clause cannot tell a
 * paid betrayal from an unpaid one, and the Court is the only thing that
 * still can -- -2, or -3 when consideration was taken.
 *
 * So it is priced in Favour.  A point buys a share of the Council's
 * deciding vote at +3 and a share of the game at the crown, which is
 * worth about three; two points is six and three is nine, and that
 * difference is the whole of what stops a bought promise being worth
 * exactly as much as a free one.
 *
 * The scales have to meet.  The version this replaces cost 15 to 29
 * against a gain of 4 to 8, so breaking was never close and 400 games
 * produced not one broken promise -- which the design names as its own
 * failure: "near zero means priced from where the liar stands". */
#define FAVOUR_POINT 3

static int break_cost(const View *v, int me, int paid_consideration)
{
    int cost = 2 * FAVOUR_POINT + v->broken_n[me] * 3;
    if (paid_consideration > 0)
        cost += FAVOUR_POINT + paid_consideration;
    /* Near the Court's crown a point of Favour is worth more than three,
     * because the points left are the ones that win. */
    if (v->favour[me] >= v->favour_crown - 3) cost += 6;
    return cost;
}

int ai_price_promise(const View *v, int term, int promisee)
{
    return term_value(v, term, promisee);
}

int ai_accept_promise(const View *v, int promiser, int term,
                      int cons_res, int cons_amt)
{
    int worth = term_value(v, term, v->me);
    int t = trust(v, v->me, promiser);
    (void)cons_res;
    /* "A House that has never dealt with you is neither trusted nor
     * distrusted.  Zero is ignorance, not suspicion", so a first deal is
     * taken on its merits and nothing is subtracted for being a stranger.
     * A known oathbreaker is discounted by what they have actually done.*/
    worth += cons_amt * 2;
    worth += t * 3;
    worth += style_of(v)->promise / 2;   /* some Houses deal, some do not */
    worth -= v->broken_n[promiser] * 3;
    /* Accepting is not free: a promise kept moves the Court a step
     * towards crowning the House that kept it, and at two players that
     * House is the only other one.
     *
     * But only a House *near* the crown is worth refusing on those
     * grounds.  Subtracting four for every point of Favour refused 63%
     * of all offers -- 4.3 refusals a game against 2.5 accepted -- which
     * is not caution, it is a House that will not deal. */
    {
        int close = v->favour[promiser] - (v->favour_crown - 4);
        if (close > 0) worth -= close * 6;
    }
    return worth > 2;
}

int ai_keep_promise(const View *v, int i)
{
    const Promise *p;
    int gain, cost;
    if (i < 0 || i >= v->promise_n) return 1;
    p = &v->promise[i];
    /* Carrying out the term costs me roughly what it buys them.  That is
     * the same number, read from the other end, and using a second number
     * here is the third of the three mistakes. */
    gain = term_value(v, p->term, p->promisee);
    cost = break_cost(v, v->me, p->cons_amt);

    /* "A vote is always worth breaking at the moment it decides
     * something."  The design says so and this was not listening: it
     * weighed keeping against what a term is worth in the abstract,
     * which is a number that never changes, so it kept everything.
     *
     * What a promise is worth breaking is what it is worth *now*, and
     * the only thing that makes now different is how close the House you
     * promised is to winning. */
    if (v->throneworthy[p->promisee]) gain += 7;
    if (p->promisee == v->paramount)  gain += 3;
    {
        int close = v->favour[p->promisee] - (v->favour_crown - 4);
        if (close > 0) gain += close * 5;   /* the Court is about to crown them */
    }
    {
        int them = power(v, p->promisee), me2 = power(v, v->me);
        if (them > me2 + 6) gain += 4;
    }

    return gain < cost;
}

/* -------------------------------------------------------------- voting */
int ai_vote(const View *v)
{
    int i, best = v->me, score, bs = -1000;
    for (i = 0; i < v->nhouses; i++) {
        if (!v->alive[i]) continue;
        score = (i == v->me) ? 20 : 0;
        score += trust(v, v->me, i) * 4;
        score -= v->broken_n[i] * 2;
        /* Voting for the strongest rival crowns them; the AI would rather
         * crown a friend, and failing that nobody. */
        if (i != v->me) score -= power(v, i) / 2;
        if (score > bs) { bs = score; best = i; }
    }
    return best;
}

/* "Each side commits Military Levy secretly, then reveals."  The estimate
 * is made from Standing, which is public, and never from the other side's
 * Levy, which is not in the View. */
int ai_commit(const View *v, int max, int defending)
{
    int i, threat = 0;
    if (max <= 0) return 0;
    for (i = 0; i < v->nhouses; i++)
        if (i != v->me && v->alive[i] && v->standing[i][R_MIL] > threat)
            threat = v->standing[i][R_MIL];
    /* A defender keeps nothing back: losing costs Standing, and Levy is
     * gone at end of turn anyway.  An attacker commits what it thinks it
     * needs and no more, because the rest is still spendable this turn. */
    if (defending) return max;
    return clampi(threat + 1, 1, max);
}

/* Whether a lord that could leave does.  There are no uninvolved Houses
 * to take sides any more, so what was a four-way simultaneous choice is
 * now the only question left underneath it: has this lord had enough?
 *
 * Asked of the House that does not hold it, because the lord is a card
 * and cards do not have Views.  A lord leaves sooner when the House
 * holding it is strong, which is the pressure that keeps a winning
 * player from simply accumulating. */
int ai_side(const View *v, int holder, int lord_idx)
{
    int i;
    for (i = 0; i < v->lord_n; i++) {
        if (i != lord_idx) continue;
        if (v->lord[i].revolution >= 4) return 2;
        break;
    }
    if (holder >= 0 && holder < v->nhouses
     && power(v, holder) > power(v, v->me) + 4) return 2;
    return 0;
}

/* ------------------------------------------------------- choosing a play */
/* One score per legal action.  This is a heuristic and says so; what it
 * is not allowed to be is a heuristic that reads something it should not,
 * and the View is what guarantees that. */
/* Which seats hold something back in First Court.
 *
 * A bitmask, one bit per seat, so ONE binary can seat this AI against
 * the AI without it -- COURT_AI_RESERVE=1 gives it to seat 0 only.
 * Default 3: both seats, which is the shipped behaviour.
 *
 * A self-play soak cannot measure strength, because both seats get
 * every change and the difference cancels.  This is the switch that
 * lets tools/ab-ai.sh put the two of them on opposite sides of the
 * same table. */
static int ai_reserve_on(int seat)
{
    static int mask = -1;
    if (mask < 0) {
        const char *e = getenv("COURT_AI_RESERVE");
        mask = e ? atoi(e) : 3;
    }
    return (mask >> seat) & 1;
}

static int score_action(const View *v, const Action *a, unsigned long *rng)
{
    const Card *c = a->card >= 0 ? &CARDS[a->card] : 0;
    const Style *st = style_of(v);
    int s = 0, t = a->target, me = v->me;

    switch (a->kind) {
    case A_PASS:
        /* Leaving First Court with something still in hand.
         *
         * Pass scored a flat zero, so any option worth anything at all
         * beat it and the machine drained First Court every turn: 158
         * turns traced, Second Court empty 158 times.  It therefore
         * never answered a war it had just lost, and a person who held
         * two cards back could count on that.
         *
         * Bond and Instigator are First Court only, so they are never
         * what is held -- they are gone by the time this can fire.
         * What is held is levy and a card to spend it on afterwards,
         * and only when a war is actually in prospect: holding back
         * against nothing is just unspent levy, which the design
         * already counts as a fault. */
        if (v->step == ST_COURT1 && v->hand_n > 0 && ai_reserve_on(v->me)) {
            int r, lv = 0, sd = 0;
            /* Open War costs 2 Grievance -- free to Ravenmark, 4 to
             * Vipren.  Only THIS seat's war counts: answering the other
             * seat's war happens inside combat, through combat_card,
             * not in Second Court, so reserving against it buys
             * nothing and costs a First Court play. */
            int war = v->grievance[v->me] >= 2;
            for (r = 0; r < R_COUNT; r++) {
                lv += v->levy[r];
                sd += v->standing[v->me][r];
            }
            /* Half the turn's levy spent is enough of a First Court.
             * Below that it is still early and there is no reserve
             * worth calling one. */
            if (war && lv * 4 <= sd * 3) return 26;
        }
        return 0;

    case A_DRAW:
        /* Cards are the engine of everything, so drawing is nearly always
         * right; the question the design cares about is which deck, and
         * the answer is the deck matching what this House is short of. */
        s = 30;
        if (a->a == (int)D_WORLD) s = 8;          /* it resolves on you   */
        if (a->a >= D_WAR && a->a <= D_INTRIGUE) {
            int r = a->a - D_WAR;
            s = 22 + v->levy[r] * 2;
            if (a->b) s -= 6;                     /* three Levy for two   */
        }
        if (a->a == (int)D_AMBITION) s = 26;
        /* Gold is where the lords and the Levers that reach them live,
         * and Capital is where the promises are.  A House that never
         * binds a lord never votes, and a House that never promises
         * never earns the Court. */
        /* Intrigue holds the whispers *and* the Minor Lords since the
         * merge, so a House that wants the Council has to come here even
         * if it despises the whispering.  Aldemar bound 0.12 lords a
         * game without this, which is a House whose whole style is the
         * Council failing to reach the only things that vote in it. */
        if (a->a == (int)D_INTRIGUE)
            s += 8 + (st->intrigue > st->council ? st->intrigue
                                                 : st->council) / 2;
        if (a->a == (int)D_POLITICAL) s += 10 + st->council / 2;
        if (a->a == (int)D_WAR)       s += st->war / 2;
        if (a->a == (int)D_THRONE)   s = 34;
        return s;

    case A_PLAY:
        if (!c) return 0;
        switch (c->eff[0].op) {
        /* Growth is the land-like play: it compounds, and the tax takes
         * Standing, so a House that never grows is taxed to death. */
        case OP_STANDING:          s = 40 + st->money; break;
        case OP_THRONE_DISCOUNT:   s = 30; break;
        case OP_LEVY:              s = 10; break;
        case OP_UNREST_CLEAR:
        case OP_CRACKDOWN: {
            int r, u = 0;
            for (r = 0; r < R_COUNT; r++) u += v->unrest[me][r];
            s = u * 9;
            break;
        }
        case OP_COUNCIL:
            /* Only call a Council you can win, or the vote crowns a
             * rival.  At two Houses that is a harder test than it looks:
             * neither House will ever vote for the other, so the Court's
             * single vote decides every Council, and calling one from
             * behind on Favour is simply conceding. */
            if (v->nhouses == 2) {
                /* The Court will not speak for a House below +3, and
                 * without the Court a House at two cannot be crowned at
                 * all, so a Council called from below +3 is a Council
                 * called for nothing. */
                int them = me ? 0 : 1;
                s = (v->favour[me] >= FAVOUR_BOUGHT_MAX
                     && v->favour[me] > v->favour[them]) ? 40 : -20;
            } else {
                s = v->throneworthy[me] ? 36 : 4;
            }
            break;
        case OP_SERVITUDE:         s = 26 + st->council; break;
        case OP_STANDING_LOSS:
        case OP_STANDING_LOSS_LP:  s = t >= 0 ? 24 + power(v, t) / 3 : 20; break;
        case OP_LEVY_STEAL:
        case OP_LEVY_STEAL_LP:     s = 14; break;
        case OP_UNREST_TARGET:     s = 16; break;
        case OP_ASSASSINATE:       s = v->paramount >= 0 && v->paramount != me
                                     ? 18 + st->intrigue : 0; break;
        case OP_REALM_REMEMBERS: {
            /* A reputation weapon is worth what the table's reputation is,
             * and nothing at all at a clean table.  This is the category
             * the design says the new game lives in. */
            int i, them = 0, mine = v->broken_n[me];
            for (i = 0; i < v->nhouses; i++) if (i != me) them += v->broken_n[i];
            s = them * 12 - mine * 12;
            break;
        }
        case OP_CALL_DEBT: {
            int i, owed = 0;
            for (i = 0; i < v->promise_n; i++)
                if (v->promise[i].state == P_BROKEN
                 && v->promise[i].promisee == me) owed++;
            s = owed * 14;
            break;
        }
        case OP_LEVY_PER_KEPT:     s = v->kept_n[me] * 8; break;
        case OP_VOTE_PER_KEPT:     s = v->kept_n[me] * 6; break;
        case OP_BROKEN_TRANSFER:   s = v->broken_n[me] * 14; break;
        case OP_PROMISE_LAUNDER:   s = 12; break;
        case OP_REVOLUTION_ZERO:
        case OP_REVOLUTION_FREEZE: s = 20; break;
        case OP_SPY: case OP_SPY_ANY: s = 14 + st->intrigue; break;
        case OP_PEEK_HAND:         s = 6 + st->intrigue; break;
        case OP_DECLARE_WAR:
            /* patience is how far behind a House will start a fight: a
             * militarist needs less of an edge than a banker does. */
            s = t >= 0 && v->standing[me][R_MIL]
                        > v->standing[t][R_MIL] + 2 + st->patience / 4
              ? 24 + st->war : 0;
            break;
        case OP_COMBAT_LEVY: case OP_COMBAT_LEVY_DEF:
        case OP_THRONE_LEVY: case OP_COMBAT_REDUCE:
            /* A reaction played on your own turn does nothing at all.  The
             * design counts that as an inert play and prints the count, so
             * the AI is told not to, rather than being allowed to inflate
             * the one number the engine is honest about. */
            return -5;
        /* ---- the pool conversion, 2026-09-20 -----------------------
         * Everything below was falling through to `default: 6` while
         * OP_STANDING scored 40, so the machine was quietly refusing to
         * play most of its own deck: 615 cards of 997 used an opcode
         * this switch had never heard of.  Every balance figure taken
         * before this was measuring a game played with a fraction of
         * the cards, which is worth more than the numbers it produced.
         *
         * The scores are set relative to the ones already here --
         * growth 40, a Bond 26, taking Standing 24, a raid 14, Levy 10
         * -- and steered by Style the same way, so a House still plays
         * in character. */

        /* Winning outright beats everything, and is checked rather
         * than hoped: OP_WIN_GOLD does nothing without the Standing
         * already in hand, so scoring it high costs nothing when it
         * is out of reach. */
        case OP_WIN_GOLD:
            s = v->standing[me][R_GOLD] >= v->golden_throne ? 1000 : 0;
            break;
        case OP_CLAIM_THRONE:     s = 34 + st->money / 2; break;
        /* An optimizer is worth what it multiplies, so it scales with
         * the Levy already in hand rather than sitting at a constant. */
        case OP_LEVY_DOUBLE:      s = 8 + v->levy[c->eff[0].a] * 3; break;
        /* The sink: worth most to a House with Levy it cannot spend,
         * which is exactly when nothing else on this list scores. */
        case OP_INVEST:           s = 20 + st->money / 2; break;
        case OP_CANCEL_NEXT:      s = 20; break;
        case OP_STEAL_CARD:       s = 18; break;
        case OP_GIVE_CARD:        s = 6 + st->promise / 2; break;
        case OP_NO_SPOIL:         s = 10 + (v->standing[me][R_MIL] < 3 ? 12 : 0); break;
        case OP_SHIELD:           s = 12; break;
        case OP_TAX_DOUBLE:       s = v->paramount == me ? 22 : 0; break;
        case OP_MAKE_PARAMOUNT:   s = 24; break;
        /* Coming back from a fall is the whole of a fallen House's
         * game; nothing else it can do matters as much. */
        case OP_RESTORE:          s = 120; break;
        case OP_EXTINGUISH:       s = 60; break;
        case OP_SURVIVE:          s = 30; break;

        /* Combat.  These are mostly Reactions and reach this switch
         * through combat_card, where the fight is already happening. */
        case OP_COMBAT_LEVY_IF:   s = 14 + st->war / 2; break;
        case OP_COMBAT_ATT_LOSS:  s = 20 + st->war / 2; break;
        /* Stopping a war is worth most to whoever would lose one. */
        case OP_NO_COMBAT:        s = 12 - st->war / 2
                                    + (v->standing[me][R_MIL] < 3 ? 14 : 0);
            break;
        case OP_NO_ASSASSIN:      s = 8; break;

        /* The Council, which elects the title the tax hangs off. */
        case OP_VOTE_GAIN:        s = 14 + st->council / 2; break;
        case OP_VOTE_DOUBLE:      s = 18 + st->council / 2; break;
        case OP_TIE_BREAK:        s = 18 + st->council / 2; break;
        case OP_VOTE_NULLIFY:     s = 16 + st->council / 2; break;

        /* Lords and the hidden tracks -- Vipren's whole game. */
        case OP_REVOLUTION_THEIRS: s = 20 + st->intrigue / 2; break;
        case OP_SERVITUDE_ALL:    s = 16 + st->council / 2; break;
        case OP_EXPOSE:           s = 12 + st->intrigue / 2; break;
        case OP_NO_SPY:           s = 8 + st->intrigue / 2; break;

        /* Cards, which are the engine of everything -- the A_DRAW arm
         * above says so. */
        case OP_DRAW:             s = 16; break;
        case OP_DRAW_ALL:         s = 8; break;
        case OP_DRAW_DECK:        s = 12; break;
        case OP_DISCARD_TARGET:   s = 14; break;
        case OP_SKIP_ACTION:      s = 18; break;

        /* Everyone at once: worth playing when you are ahead on the
         * resource being taken, and not when you are behind. */
        case OP_STANDING_ALL_LOSS:
        case OP_LEVY_ALL_LOSE:    s = 8 + (power(v, me) > 6 ? 8 : -4); break;
        case OP_LEVY_ALL_GAIN:    s = 6; break;
        case OP_RANDOM_LOSS:      s = 8; break;
        case OP_GRIEVANCE_SCALE:  s = 8; break;
        case OP_LOWEST_GAIN:      s = 10; break;
        case OP_LP_GAIN:
        case OP_LP_PAY:           s = v->paramount == me ? 18 : 4; break;
        case OP_LEVY_GIVE:        s = 4 + st->promise / 2; break;
        case OP_NO_PROMISE:       s = 10 - st->promise / 2; break;

        default:                   s = 6; break;
        }
        /* A card that reads a reputation nobody has is inert by
         * construction; the target conditions already gate legality, so
         * this only nudges the ordering. */
        if ((c->reads & READS_BROKEN) && t >= 0) s += v->broken_n[t] * 6;
        return s;

    case A_BOND: {
        /* A Bond is worth most when it is nearly full, because 5 is where
         * it pays: a revealed lord is a vote, and a vote is the
         * Paramountcy. */
        int i, best = 0;
        for (i = 0; i < v->lord_n; i++)
            if (v->lord[i].alive && v->lord[i].holder == me
             && v->lord[i].servitude > best) best = v->lord[i].servitude;
        return 30 + st->council + (best >= 3 ? 24 : 0);
    }

    case A_INSTIGATE:
        /* At a lord if there is one worth turning -- a revealed lord is a
         * vote, and taking a vote off the Council is worth more than any
         * amount of unrest.  Otherwise at a resource. */
        if (a->a == 1) {
            int i = a->target;
            if (i < 0 || i >= v->lord_n || !v->lord[i].alive) return 0;
            if (v->lord[i].holder == me) return -5;    /* a false flag    */
            return 24 + st->intrigue + (v->lord[i].revealed ? 16 : 0);
        }
        if (a->a == 2) return 10 + st->intrigue;       /* at the Court    */
        return 14 + st->intrigue + (t >= 0 ? power(v, t) / 4 : 0);

    case A_PROPOSE: {
        /* What a promise is worth to make, which is not what it is worth
         * to receive.  Scoring it by what it buys the other House had it
         * exactly backwards: the AI offered the most expensive terms it
         * could find, every turn, and the other House accepted every one
         * -- 6.6 promises a game, none refused, none broken.  A ritual,
         * not a ledger.
         *
         * Promising is a purchase: you are buying Favour with an
         * obligation you will have to honour.  So it is worth doing when
         * the term is cheap to keep and the Favour is worth having, and
         * not when the term is dear. */
        int worth, want;
        if (t < 0) return 0;
        worth = term_value(v, a->a, t);
        /* How far from the crown, read from the View rather than
         * written in.  This said 5 after the crown moved to 8, so the
         * bonus never fired and the AI never promised on purpose. */
        want  = v->favour_crown - v->favour[me];
        s = 16 - worth + st->promise + (want > 0 && want <= 4 ? 18 : 0);
        if (v->broken_n[t] > 1) s -= v->broken_n[t] * 4;
        return s;
    }

    case A_REVEAL_BOND: return 70;   /* a revealed lord is a vote         */


    case A_DECLARE_WAR:
        if (t < 0) return 0;
        s = v->standing[me][R_MIL] - v->standing[t][R_MIL];
        return s > 2 + st->patience / 4 ? 20 + s * 3 + st->war : -10;

    case A_CHALLENGE: {
        /* "The challenger must exceed the field's total."  The field is
         * every other living House's Military Levy, and Levy is set from
         * Standing, so the field is estimable from public state alone --
         * which is the only reason a machine may estimate it. */
        int i, field = 0;
        for (i = 0; i < v->nhouses; i++)
            if (i != v->me && v->alive[i]) field += v->standing[i][R_MIL];
        return v->standing[v->me][R_MIL] > field ? 200 : -50;
    }

    case A_SPEND_GRIEVANCE:
        /* Grievance sat pinned at its maximum in every game next door.
         * These are the sinks, and the AI values them by how close to
         * pinned it is, so that the currency actually moves. */
        s = 6 + v->grievance[me] * 3;
        if (a->a == 6) {
            if (v->nhouses == 2) {
                int them = me ? 0 : 1;
                s = (v->favour[me] >= FAVOUR_BOUGHT_MAX
                     && v->favour[me] > v->favour[them]) ? 30 : -20;
            } else if (!v->throneworthy[me]) s = 2;
        }
        return s;

    case A_BUY_FAVOUR:
        /* The Court crowns at +5 and only three points can be bought, so
         * the purchase is worth most at the start of the road. */
        return 22 + st->money - v->favour[me] * 3;
    }
    return (int)(nxt(rng) % 3);
}

Action ai_choose(const View *v, const Action *opts, int n, unsigned long *rng)
{
    int i, best = 0, bs = -100000, s;
    for (i = 0; i < n; i++) {
        s = score_action(v, &opts[i], rng) * 8 + (int)(nxt(rng) % 7);
        if (s > bs) { bs = s; best = i; }
    }
    /* Passing is always legal and always scores zero, so an option worth
     * less than nothing is never taken.  That is what keeps a Reaction
     * from being fired on an empty turn and counted as an inert play. */
    if (bs < 0) {
        for (i = 0; i < n; i++) if (opts[i].kind == A_PASS) return opts[i];
    }
    return opts[best];
}
