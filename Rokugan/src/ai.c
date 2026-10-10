/* The machine opponent.
 *
 * For most decisions it looks one action ahead: copy the game, apply the
 * action, score the result, keep the best -- and pass when nothing beats
 * passing.  That works for any ability the engine can run, which is the
 * point: a new encoding in fx/ERA.tsv is something the machine can use
 * the moment it loads, with no strategy written for it.
 *
 * What it may see: the table, both honor totals, its own hand, and the
 * size of the other.  The score reads nothing else.  The copy it applies
 * actions to does hold both decks, so a draw in the copy "knows" the
 * card -- which is why the score counts cards in hand and never reads
 * them.
 *
 * Attacking and defending are planned, not searched: a one-action look
 * ahead cannot see that sending one unit means nothing until the battle
 * resolves.
 */
#include "rokugan.h"
#include <string.h>

#define D(g, i) (&defs[(g)->c[i].def])

static int is_pers(const Game *g, int i)
{
    return g->c[i].zone == Z_PLAY && D(g, i)->type == T_PERSONALITY;
}

/* A unit's worth, bowed or not, and without bonuses that last only the
 * turn: those are counted where they matter, in the battle projection.
 * Counting them here made the machine spend +Force actions at home. */
static double unit_value(const Game *g, int i)
{
    int f = D(g, i)->force + g->c[i].pf, k;
    for (k = 0; k < g->nc; k++)
        if (g->c[k].zone == Z_ATTACHED && g->c[k].host == i)
            f += D(g, k)->force;
    return 2.0 + f * 1.2 + card_chi(g, i) * 0.25 + D(g, i)->ph * 0.2;
}

static int hand_count(const Game *g, int p)
{
    int i, n = 0;
    for (i = 0; i < g->nc; i++)
        n += g->c[i].owner == p && g->c[i].zone == Z_HAND;
    return n;
}

/* What the current battle would do if it resolved now. */
static double battle_projection(const Game *g, int s)
{
    int att = g->active, def = 1 - att, b = g->battle, i;
    int A = army_force(g, att, b), D = army_force(g, def, b);
    double lost_att = 0, lost_def = 0, v = 0;
    for (i = 0; i < g->nc; i++)
        if (is_pers(g, i) && g->c[i].at == b) {
            if (g->c[i].owner == att) lost_att += unit_value(g, i);
            else                      lost_def += unit_value(g, i);
        }
    if (A > D) {
        v -= (def == s ? 1 : -1) * lost_def;
        if (A > D + prov_strength(g, def, b))
            v -= (def == s ? 1 : -1) * 9.0;
    } else if (D > A) {
        v -= (att == s ? 1 : -1) * lost_att;
    } else if (A || D) {
        v -= (att == s ? 1 : -1) * lost_att + (def == s ? 1 : -1) * lost_def;
    }
    return v;
}

double ai_eval(const Game *g, int s)
{
    int o = 1 - s, i;
    double v = 0;

    if (g->phase == PH_OVER)
        return g->winner == s ? 10000 : g->winner < 0 ? 0 : -10000;
    v += 1.4 * (g->p[s].honor - g->p[o].honor);
    if (g->p[s].honor >= 30) v += (g->p[s].honor - 30) * 0.8;
    if (g->p[o].honor >= 30) v -= (g->p[o].honor - 30) * 0.8;
    v += 9.0 * (provinces_left(g, s) - provinces_left(g, o));
    v += 4.0 * (rings_in_play(g, s) - rings_in_play(g, o));
    v += 0.7 * (hand_count(g, s) - hand_count(g, o));
    for (i = 0; i < g->nc; i++) {
        const Inst *c = &g->c[i];
        double x = 0;
        if (c->zone != Z_PLAY)
            continue;
        if (D(g, i)->type == T_PERSONALITY)
            x = unit_value(g, i);
        else if (D(g, i)->type == T_HOLDING)
            x = 1.0 + D(g, i)->gold * 1.6;
        v += c->owner == s ? x : -x;
    }
    if (g->phase == PH_BATTLE)
        v += battle_projection(g, s);
    return v;
}

/* --------------------------------------------------------- the attack */
static int home_force(const Game *g, int p, int unbowed_only)
{
    int i, f = 0;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && is_pers(g, i) && g->c[i].at < 0
            && (!unbowed_only || !g->c[i].bowed))
            f += unit_force(g, i);
    return f;
}

static int weakest_province(const Game *g, int p)
{
    int k, best = -1;
    for (k = 0; k < NPROV; k++)
        if (g->p[p].alive[k]
            && (best < 0 || prov_strength(g, p, k) < prov_strength(g, p, best)))
            best = k;
    return best;
}

static int worth_attacking(const Game *g, int s)
{
    int M = home_force(g, s, 1), O = home_force(g, 1 - s, 1);
    int k = weakest_province(g, 1 - s);
    if (k < 0 || M <= 0)
        return 0;
    return M > O + prov_strength(g, 1 - s, k)   /* the province falls     */
        || M > O + 2;                           /* the defenders die      */
}

static int find(const Action *acts, int n, AKind k, int src, int prov)
{
    int i;
    for (i = 0; i < n; i++)
        if (acts[i].k == k && (src < 0 || acts[i].src == src)
            && (prov < 0 || acts[i].prov == prov))
            return i;
    return -1;
}

static int choose_assign(const Game *g, int s, const Action *acts, int n)
{
    int k = weakest_province(g, 1 - s), i;
    for (i = 0; i < n; i++)
        if (acts[i].k == A_ASSIGN && acts[i].prov == k)
            return i;
    return find(acts, n, A_DONE, -1, -1);
}

/* Defend where it wins the battle, or where it saves a province the
 * player cannot afford to lose; otherwise keep the units alive. */
static int choose_defend(const Game *g, int s, const Action *acts, int n)
{
    int k, att = 1 - s;
    for (k = 0; k < NPROV; k++) {
        int A = army_force(g, att, k), D = army_force(g, s, k), S, i, best = -1, bf = -1;
        int avail = 0;
        if (!A)
            continue;
        S = prov_strength(g, s, k);
        for (i = 0; i < n; i++)
            if (acts[i].k == A_ASSIGN && acts[i].prov == k) {
                int f = unit_force(g, acts[i].src);
                avail += f;
                if (f > bf) { bf = f; best = i; }
            }
        if (best < 0)
            continue;
        if (D > A)                               /* already winning   */
            continue;
        if (D + avail > A)                       /* can win: add more */
            return best;
        if (A > D + S && A <= D + avail + S && provinces_left(g, s) <= 2)
            return best;                         /* save the province */
    }
    return find(acts, n, A_DONE, -1, -1);
}

static int choose_discard(const Game *g, const Action *acts, int n)
{
    int i, best = 0;
    double bv = 1e9;
    for (i = 0; i < n; i++) {
        const Def *d = D(g, acts[i].src);
        double v = d->understood * 3.0 + d->focus * 0.5 - d->cost * 0.2;
        if (v < bv) { bv = v; best = i; }
    }
    return best;
}

/* -------------------------------------------------------- one look ahead */
static Game scratch;

static int searchable(const Game *g, Action a)
{
    if (a.k == A_PLAY && a.abil < 0 && a.perf < 0)
        return 0;                /* by hand, or a Ring: the machine cannot judge it */
    if (a.k == A_CYCLE)
        return 0;
    (void)g;
    return 1;
}

int ai_choose(const Game *g, int s, const Action *acts, int n)
{
    int i, best = -1;
    double base, bv;

    switch (g->phase) {
    case PH_ATTACK:
        return find(acts, n, worth_attacking(g, s) ? A_ATTACK : A_NOATTACK, -1, -1);
    case PH_ASSIGN:
        return choose_assign(g, s, acts, n);
    case PH_DEFEND:
        return choose_defend(g, s, acts, n);
    case PH_DISCARD:
        return choose_discard(g, acts, n);
    default:
        break;
    }

    /* Action, Battle, Dynasty: the pass (or end of turn) is the baseline. */
    base = ai_eval(g, s);
    bv = base + (g->phase == PH_DYNASTY ? 0.5 : 0.8);   /* an action must earn its card */
    for (i = 0; i < n; i++) {
        double v;
        if (acts[i].k == A_PASS || !searchable(g, acts[i]))
            continue;
        memcpy(&scratch, g, sizeof scratch);
        scratch.trace = NULL;
        scratch.check = 0;
        l5r_apply(&scratch, acts[i]);
        v = ai_eval(&scratch, s);
        if (v > bv) {
            bv = v;
            best = i;
        }
    }
    if (best >= 0)
        return best;
    /* First turn: clear a province of a card it cannot afford to keep. */
    if (g->phase == PH_DYNASTY && g->p[s].first_turn)
        for (i = 0; i < n; i++)
            if (acts[i].k == A_CYCLE && D(g, acts[i].src)->type != T_HOLDING
                && D(g, acts[i].src)->cost > gold_available(g, s) + 4)
                return i;
    return find(acts, n, A_PASS, -1, -1);
}
