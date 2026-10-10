/* The rules.  Every change to a Game is made here, through l5r_apply()
 * or l5r_manual(); everything else only reads.
 *
 * A turn: Straighten (your cards straighten, your face-down Province
 * cards turn up and Events among them resolve), Action (Open and
 * Limited actions alternate until both players pass), Attack (optional:
 * send units at enemy Provinces, the defender meets them, each
 * battlefield is fought and resolved), Dynasty (recruit from your
 * Provinces), End (draw a Fate card, discard down to eight).
 */
#include "rokugan.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

const char *phase_name[] = {
    "Action", "Attack", "Assign", "Defend", "Battle", "Dynasty", "Discard", "Over"
};
const char *win_name[] = {
    "", "Honor Victory", "Military Victory", "Enlightenment",
    "Dishonor", "Deck exhaustion", "Draw"
};
const char *manual_name[M_COUNT] = {
    "bow", "straighten", "destroy", "home", "force+", "force-",
    "honor+", "honor-", "draw", "discard"
};

#define D(g, i)   (&defs[(g)->c[i].def])
#define OPP(p)    (1 - (p))

/* ------------------------------------------------------------- utilities */
static unsigned rnd(Game *g)
{
    g->rng = g->rng * 6364136223846793005UL + 1442695040888963407UL;
    return (unsigned)(g->rng >> 33);
}

void glog(Game *g, const char *fmt, ...)
{
    va_list ap;
    char *line = g->log[g->nlog % LOG_KEEP];
    va_start(ap, fmt);
    vsnprintf(line, sizeof g->log[0], fmt, ap);
    va_end(ap);
    g->nlog++;
    if (g->trace)
        fprintf(g->trace, "T%d %s\n", g->turn, line);
}

static const char *nm(const Game *g, int i) { return D(g, i)->name; }

const char *player_clan(const Game *g, int p)
{
    return D(g, g->p[p].stronghold)->clan;
}

static int is_type(const Game *g, int i, CType t) { return D(g, i)->type == t; }
static int in_play(const Game *g, int i) { return g->c[i].zone == Z_PLAY; }

static int is_pers_in_play(const Game *g, int i)
{
    return in_play(g, i) && is_type(g, i, T_PERSONALITY);
}

/* The Personality a card belongs to, for a card in play or attached. */
static int unit_of(const Game *g, int i)
{
    if (g->c[i].zone == Z_ATTACHED && g->c[i].host >= 0)
        return g->c[i].host;
    if (is_pers_in_play(g, i))
        return i;
    return -1;
}

static int location(const Game *g, int i)
{
    int u = unit_of(g, i);
    return u < 0 ? -1 : g->c[u].at;
}

int has_followers(const Game *g, int i)
{
    int k;
    for (k = 0; k < g->nc; k++)
        if (g->c[k].zone == Z_ATTACHED && g->c[k].host == i && is_type(g, k, T_FOLLOWER))
            return 1;
    return 0;
}

static int static_sum(const Game *g, int i, EffOp op)
{
    const Def *d = D(g, i);
    int a, e, s = 0;
    for (a = 0; a < d->nabil; a++)
        if (d->abil[a].timing == TM_STATIC)
            for (e = 0; e < d->abil[a].neff; e++)
                if (d->abil[a].eff[e].op == op)
                    s += d->abil[a].eff[e].n;
    return s;
}

/* Force a single card contributes: bowed cards contribute none. */
static int card_force(const Game *g, int i)
{
    const Inst *c = &g->c[i];
    int f;
    if (c->bowed)
        return 0;
    f = D(g, i)->force + c->fbonus + c->pf;
    if (location(g, i) >= 0) {
        int owner = c->owner;
        f += static_sum(g, i, owner == g->active ? E_ATTFORCE : E_DEFFORCE);
    }
    return f;
}

int unit_force(const Game *g, int i)
{
    int k, f = card_force(g, i);
    for (k = 0; k < g->nc; k++) {
        if (g->c[k].zone != Z_ATTACHED || g->c[k].host != i)
            continue;
        if (is_type(g, k, T_FOLLOWER))
            f += card_force(g, k);
        else if (is_type(g, k, T_ITEM) && !g->c[i].bowed)
            f += D(g, k)->force + g->c[k].fbonus;
    }
    return f < 0 ? 0 : f;
}

int card_chi(const Game *g, int i)
{
    int k, c = D(g, i)->chi + g->c[i].cbonus + g->c[i].pc;
    for (k = 0; k < g->nc; k++)
        if (g->c[k].zone == Z_ATTACHED && g->c[k].host == i && is_type(g, k, T_ITEM))
            c += D(g, k)->chi;
    return c;
}

int army_force(const Game *g, int p, int prov)
{
    int i, f = 0;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && is_pers_in_play(g, i) && g->c[i].at == prov)
            f += unit_force(g, i);
    return f;
}

static int army_size(const Game *g, int p, int prov)
{
    int i, n = 0;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && is_pers_in_play(g, i) && g->c[i].at == prov)
            n++;
    return n;
}

int prov_strength(const Game *g, int p, int prov)
{
    int i, s = D(g, g->p[p].stronghold)->pstr + g->p[p].pbonus[prov];
    for (i = 0; i < g->nc; i++) {
        const Inst *c = &g->c[i];
        if (c->owner != p)
            continue;
        if (c->zone == Z_ATTACHED && c->host < 0 && c->prov == prov)
            s += static_sum(g, i, E_PSTR);           /* a Region */
        else if (c->zone == Z_PLAY && (is_type(g, i, T_HOLDING) || is_type(g, i, T_STRONGHOLD)))
            s += static_sum(g, i, E_PSTR);
    }
    return s < 0 ? 0 : s;
}

int provinces_left(const Game *g, int p)
{
    int k, n = 0;
    for (k = 0; k < NPROV; k++)
        n += g->p[p].alive[k];
    return n;
}

int rings_in_play(const Game *g, int p)
{
    int i, j, n = 0;
    for (i = 0; i < g->nc; i++) {
        int dup = 0;
        if (g->c[i].owner != p || !in_play(g, i) || !is_type(g, i, T_RING))
            continue;
        for (j = 0; j < i; j++)
            if (g->c[j].owner == p && in_play(g, j) && is_type(g, j, T_RING)
                && !strcmp(nm(g, j), nm(g, i)))
                dup = 1;
        n += !dup;
    }
    return n;
}

static int producer(const Game *g, int i)
{
    return in_play(g, i) && !g->c[i].bowed && D(g, i)->gold > 0
        && (is_type(g, i, T_HOLDING) || is_type(g, i, T_STRONGHOLD));
}

int gold_available(const Game *g, int p)
{
    int i, n = g->p[p].pool;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && producer(g, i))
            n += D(g, i)->gold;
    return n;
}

static int clan_is(const char *clan, const char *want)
{
    /* "Crane Clan" in a discount line, "Crane" in a clan column */
    return !strncmp(clan, want, strlen(clan)) && clan[0];
}

int buy_cost(const Game *g, int p, int i)
{
    const Def *d = D(g, i);
    int a, cost = d->cost;
    if (d->type == T_PERSONALITY && !strcmp(d->clan, player_clan(g, p)))
        cost -= g->era->clan_discount;
    for (a = 0; a < d->nabil; a++) {
        const Ability *ab = &d->abil[a];
        if (ab->timing == TM_STATIC && ab->neff && ab->eff[0].op == E_DISCOUNT) {
            int n = atoi(ab->arg);
            const char *who = strchr(ab->arg, ':');
            if (who && clan_is(player_clan(g, p), who + 1))
                cost -= n;
        }
    }
    return cost < 0 ? 0 : cost;
}

static int honor_ok(const Game *g, int p, int i)
{
    int hr = D(g, i)->hreq;
    return hr == HR_NONE || g->p[p].honor >= hr;
}

static int unique_ok(const Game *g, int p, int i)
{
    int k;
    if (!(D(g, i)->kw & KW_UNIQUE))
        return 1;
    for (k = 0; k < g->nc; k++)
        if (k != i && g->c[k].owner == p
            && (g->c[k].zone == Z_PLAY || g->c[k].zone == Z_ATTACHED)
            && !strcmp(nm(g, k), nm(g, i)))
            return 0;
    return 1;
}

/* --------------------------------------------------------------- victory */
static void win(Game *g, int p, WinHow how)
{
    if (g->phase == PH_OVER)
        return;
    g->winner = p;
    g->how = how;
    g->phase = PH_OVER;
    if (p >= 0)
        glog(g, "%s wins: %s.", player_clan(g, p), win_name[how]);
    else
        glog(g, "The game is drawn.");
}

static void honor_change(Game *g, int p, int n)
{
    if (!n || g->phase == PH_OVER)
        return;
    g->p[p].honor += n;
    if (g->p[p].honor <= HONOR_LOSE)
        win(g, OPP(p), WIN_DISHONOR);
}

/* ------------------------------------------------------------------ decks */
static int top_of(const Game *g, int p, Zone z)
{
    int i, best = -1;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && g->c[i].zone == z
            && (best < 0 || g->c[i].order < g->c[best].order))
            best = i;
    return best;
}

static void shuffle_into(Game *g, int p, Zone from, Zone to)
{
    int i;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && g->c[i].zone == from) {
            g->c[i].zone = to;
            g->c[i].order = (int)(rnd(g) % 1000000);
            g->c[i].bowed = 0;
        }
}

static int count_zone(const Game *g, int p, Zone z)
{
    int i, n = 0;
    for (i = 0; i < g->nc; i++)
        n += g->c[i].owner == p && g->c[i].zone == z;
    return n;
}

/* An empty deck is reshuffled from its discard pile.  HOUSE RULE: the
 * editions disagree on what an empty deck costs; this one costs nothing,
 * and a player who has neither deck nor discard simply draws nothing. */
static int draw_from(Game *g, int p, Zone deck, Zone disc)
{
    int i = top_of(g, p, deck);
    if (i < 0) {
        if (!count_zone(g, p, disc))
            return -1;
        shuffle_into(g, p, disc, deck);
        glog(g, "%s reshuffles a discard pile.", player_clan(g, p));
        i = top_of(g, p, deck);
    }
    return i;
}

static void draw_fate(Game *g, int p, int n)
{
    while (n-- > 0) {
        int i = draw_from(g, p, Z_FATEDECK, Z_FATEDISC);
        if (i < 0)
            return;
        g->c[i].zone = Z_HAND;
    }
}

static void refill(Game *g, int p, int prov)
{
    int i;
    if (!g->p[p].alive[prov])
        return;
    i = draw_from(g, p, Z_DYNDECK, Z_DYNDISC);
    if (i < 0)
        return;
    g->c[i].zone = Z_PROVINCE;
    g->c[i].prov = prov;
    g->c[i].down = 1;
}

static int in_province(const Game *g, int p, int prov)
{
    int i;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && g->c[i].zone == Z_PROVINCE && g->c[i].prov == prov)
            return i;
    return -1;
}

/* ---------------------------------------------------------------- leaving */
static void to_discard(Game *g, int i)
{
    Inst *c = &g->c[i];
    c->zone = D(g, i)->fate ? Z_FATEDISC : Z_DYNDISC;
    c->bowed = c->down = 0;
    c->host = -1;
    c->at = -1;
    c->fbonus = c->cbonus = 0;
}

static void destroy(Game *g, int i)
{
    int k;
    if (g->c[i].zone != Z_PLAY && g->c[i].zone != Z_ATTACHED)
        return;
    for (k = 0; k < g->nc; k++)
        if (g->c[k].zone == Z_ATTACHED && g->c[k].host == i)
            to_discard(g, k);
    to_discard(g, i);
}

static void move_home(Game *g, int i)
{
    int u = unit_of(g, i);
    if (u >= 0)
        g->c[u].at = -1;
}

/* ---------------------------------------------------------------- paying */
/* Gold is produced by bowing Holdings and the Stronghold as a cost is
 * paid.  The bowing is chosen to waste as little as possible, which is
 * what a careful player does and what the person would otherwise have to
 * click through for every purchase. */
static int pay(Game *g, int p, int cost)
{
    int prod[32], idx[32], n = 0, i, best = -1, bestsum = 1 << 30;
    unsigned m, bestm = 0;
    int need = cost - g->p[p].pool;

    if (cost <= 0)
        return 1;
    if (need <= 0) {
        g->p[p].pool -= cost;
        return 1;
    }
    for (i = 0; i < g->nc && n < 32; i++)
        if (g->c[i].owner == p && producer(g, i)) {
            prod[n] = D(g, i)->gold;
            idx[n++] = i;
        }
    if (n > 16) {                       /* greedy past 65536 subsets */
        int sum = 0;
        for (i = 0; i < n && sum < need; i++) {
            sum += prod[i];
            bestm |= 1u << i;
        }
        if (sum < need)
            return 0;
        bestsum = sum;
    } else {
        for (m = 1; m < (1u << n); m++) {
            int sum = 0;
            for (i = 0; i < n; i++)
                if (m & (1u << i))
                    sum += prod[i];
            if (sum >= need && sum < bestsum) {
                bestsum = sum;
                bestm = m;
                best = 1;
            }
        }
        if (best < 0)
            return 0;
    }
    for (i = 0; i < n; i++)
        if (bestm & (1u << i))
            g->c[idx[i]].bowed = 1;
    g->p[p].pool += bestsum - cost;
    if (!g->era->pool_per_phase)
        g->p[p].pool = 0;               /* the excess is lost */
    return 1;
}

static void end_phase_pool(Game *g)
{
    g->p[0].pool = g->p[1].pool = 0;
}

/* --------------------------------------------------------------- targets */
static int at_battle(const Game *g, int i)
{
    return g->phase == PH_BATTLE && location(g, i) == g->battle;
}

static int follower_in_play(const Game *g, int i)
{
    return g->c[i].zone == Z_ATTACHED && g->c[i].host >= 0 && is_type(g, i, T_FOLLOWER);
}

static int has_attachments(const Game *g, int i)
{
    int k;
    for (k = 0; k < g->nc; k++)
        if (g->c[k].zone == Z_ATTACHED && g->c[k].host == i)
            return 1;
    return 0;
}

static int has_kw(const Game *g, int i, const char *kw)
{
    const Def *d = D(g, i);
    return strstr(d->kwtext, kw) || strstr(d->clan, kw) || strstr(d->name, kw);
}

/* Does card i suit target code t for player q?  In a battle, battle
 * actions reach only the current battlefield.  perf is the performing
 * Personality, for conditions measured against him ("lower Chi"). */
static int target_ok(const Game *g, int q, const Ability *ab, int i, int battle, int perf)
{
    int own = g->c[i].owner == q;
    int pers = is_pers_in_play(g, i), fol = follower_in_play(g, i), ok;
    unsigned f = ab->filt;
    if (battle && !at_battle(g, i) && !(f & F_HOME))
        return 0;
    switch (ab->target) {
    case TG_OPERS: case TG_OUNIT: ok = own && pers; break;
    case TG_EPERS: case TG_EUNIT: ok = !own && pers; break;
    case TG_EFOL:  ok = !own && fol; break;
    case TG_ECARD: ok = !own && (pers || fol); break;
    case TG_EHOLD: ok = !own && in_play(g, i) && is_type(g, i, T_HOLDING); break;
    case TG_OHOLD: ok = own && in_play(g, i) && is_type(g, i, T_HOLDING); break;
    case TG_APERS: ok = pers; break;
    case TG_AFOL:  ok = fol; break;
    case TG_ACARD: ok = pers || fol; break;
    case TG_OCARD: ok = own && (pers || fol); break;
    case TG_EATT:  ok = !own && g->c[i].zone == Z_ATTACHED && g->c[i].host >= 0; break;
    default:       ok = 0;
    }
    if (!ok)
        return 0;
    if ((f & F_ATT) && !(location(g, i) >= 0 && g->c[i].owner == g->active)) return 0;
    if ((f & F_DEF) && !(location(g, i) >= 0 && g->c[i].owner != g->active)) return 0;
    if ((f & F_HOME) && location(g, i) >= 0) return 0;
    if ((f & F_BOWED) && !g->c[i].bowed) return 0;
    if ((f & F_UNBOWED) && g->c[i].bowed) return 0;
    if ((f & F_OPPOSED) && !(g->phase == PH_BATTLE && location(g, i) == g->battle
                             && army_size(g, 1 - g->c[i].owner, g->battle))) return 0;
    if ((f & F_NOFOL) && pers && has_followers(g, i)) return 0;
    if ((f & F_NOATT) && pers && has_attachments(g, i)) return 0;
    if (ab->tkw[0] && !has_kw(g, i, ab->tkw)) return 0;
    if (ab->maxforce >= 0 && (pers ? unit_force(g, i) : card_force(g, i)) > ab->maxforce) return 0;
    if (ab->maxchi >= 0 && (!pers || card_chi(g, i) > ab->maxchi)) return 0;
    if (ab->minph >= 0 && (!pers || D(g, i)->ph + g->c[i].pph < ab->minph)) return 0;
    if (ab->maxph >= 0 && (!pers || D(g, i)->ph + g->c[i].pph > ab->maxph)) return 0;
    if (perf >= 0) {
        if ((f & F_LOWERF) && unit_force(g, i) >= unit_force(g, perf)) return 0;
        if ((f & F_LOWERC) && card_chi(g, i) >= card_chi(g, perf)) return 0;
        if ((f & F_LECHI) && card_chi(g, i) > card_chi(g, perf)) return 0;
    }
    return 1;
}

/* The effect that decides what a target must be, if any: a Ranged
 * Attack needs a Follower or a Personality without Followers, with no
 * more Force than its strength; a destroy needs something to destroy. */
static int ability_targets(const Game *g, int q, const Ability *ab, int battle, int perf, int *out)
{
    int i, e, n = 0;
    for (e = 0; e < ab->neff; e++) {
        EffOp op = ab->eff[e].op;
        if (op == E_RANGED || op == E_MELEE || op == E_FEAR || op == E_RANGEDCHI || op == E_FEARCHI) {
            int str = ab->eff[e].n;
            if (op == E_RANGEDCHI || op == E_FEARCHI)
                str = perf >= 0 ? card_chi(g, perf) : 0;
            for (i = 0; i < g->nc; i++) {
                int ok;
                if (g->c[i].owner == q || !at_battle(g, i))
                    continue;
                ok = follower_in_play(g, i)
                   || (is_pers_in_play(g, i) && !has_followers(g, i));
                if (ok && card_force(g, i) <= str
                    && !((op == E_FEAR || op == E_FEARCHI) && g->c[i].bowed))
                    out[n++] = i;
            }
            return n;
        }
    }
    if (ab->target == TG_NONE || ab->target == TG_SELF) {
        out[0] = -1;
        return 1;
    }
    for (i = 0; i < g->nc; i++) {
        if (!target_ok(g, q, ab, i, battle, perf))
            continue;
        /* skip targets the effect cannot change */
        for (e = 0; e < ab->neff; e++) {
            if (ab->eff[e].op == E_BOW && g->c[i].bowed) break;
            if (ab->eff[e].op == E_STRAIGHTEN && !g->c[i].bowed) break;
            if (ab->eff[e].op == E_HOME && location(g, i) < 0) break;
            if (ab->eff[e].op == E_TOBATTLE && (location(g, i) >= 0 || g->c[i].bowed)) break;
        }
        if (e == ab->neff)
            out[n++] = i;
    }
    return n;
}

/* ------------------------------------------------------------ resolving */
/* A duel.  HOUSE RULE: in the printed game each player focuses cards from
 * the hand, in turn, until both strike.  Here each side focuses the top
 * card of its Fate deck, unseen, and adds its Focus value -- a duel the
 * engine can resolve without a dialogue it does not have yet.  The higher
 * total wins; a tie, both lose. */
static int focus_top(Game *g, int p)
{
    int i = draw_from(g, p, Z_FATEDECK, Z_FATEDISC), f;
    if (i < 0)
        return 0;
    f = D(g, i)->focus;
    to_discard(g, i);
    return f;
}

static void duel(Game *g, int a, int b, int on_force, int honor)
{
    int pa = g->c[a].owner, pb = g->c[b].owner;
    int sa = (on_force ? unit_force(g, a) : card_chi(g, a)) + focus_top(g, pa);
    int sb = (on_force ? unit_force(g, b) : card_chi(g, b)) + focus_top(g, pb);
    glog(g, "  Duel: %s %d against %s %d.", nm(g, a), sa, nm(g, b), sb);
    if (sa != sb) {
        int win = sa > sb ? a : b, lose = sa > sb ? b : a;
        honor_change(g, g->c[win].owner, honor);
        glog(g, "  %s wins the duel.", nm(g, win));
        if (on_force)
            g->c[lose].bowed = 1;
        else
            destroy(g, lose);
    } else if (on_force) {
        g->c[a].bowed = g->c[b].bowed = 1;
    } else {
        destroy(g, a);
        destroy(g, b);
    }
}

static void resolve(Game *g, int q, int src, const Ability *ab, int tgt, int perf)
{
    int e, self = perf >= 0 ? perf : src;
    for (e = 0; e < ab->neff && g->phase != PH_OVER; e++) {
        int n = ab->eff[e].n, who = tgt >= 0 ? tgt : self;
        switch (ab->eff[e].op) {
        case E_FORCE:
            if (who >= 0) g->c[unit_of(g, who) >= 0 && ab->target != TG_AFOL
                               && ab->target != TG_EFOL ? who : who].fbonus += n;
            break;
        case E_CHI:        if (who >= 0) g->c[who].cbonus += n; break;
        case E_DESTROY:
        case E_RANGED:
        case E_RANGEDCHI:
        case E_MELEE:      if (tgt >= 0) {
                               glog(g, "  %s is destroyed.", nm(g, tgt));
                               destroy(g, tgt);
                           }
                           break;
        case E_BOW:
        case E_FEAR:
        case E_FEARCHI:    if (who >= 0) g->c[who].bowed = 1; break;
        case E_BOWUNIT:
            if (who >= 0 && unit_of(g, who) >= 0) {
                int k, u = unit_of(g, who);
                g->c[u].bowed = 1;
                for (k = 0; k < g->nc; k++)
                    if (g->c[k].zone == Z_ATTACHED && g->c[k].host == u)
                        g->c[k].bowed = 1;
            }
            break;
        case E_DUEL:
        case E_FDUELBOW:
            if (perf >= 0 && tgt >= 0)
                duel(g, perf, tgt, ab->eff[e].op == E_FDUELBOW, n);
            break;
        case E_PROVSTR:
            if (g->phase == PH_BATTLE)
                g->p[OPP(g->active)].pbonus[g->battle] += n;
            break;
        case E_STRAIGHTEN: if (who >= 0) g->c[who].bowed = 0; break;
        case E_HOME:       if (who >= 0) move_home(g, who); break;
        case E_GAIN:       honor_change(g, q, n); break;
        case E_LOSE:       honor_change(g, q, -n); break;
        case E_OLOSE:      honor_change(g, OPP(q), -n); break;
        case E_DRAW:       draw_fate(g, q, n); break;
        case E_PRODUCE:    g->p[q].pool += n; break;
        case E_PFORCE:     if (who >= 0) g->c[who].pf += n; break;
        case E_PCHI:       if (who >= 0) g->c[who].pc += n; break;
        case E_PPH:        if (who >= 0) g->c[who].pph += n; break;
        case E_BOWFOL:
            if (who >= 0) {
                int k, u = unit_of(g, who);
                for (k = 0; k < g->nc; k++)
                    if (g->c[k].zone == Z_ATTACHED && g->c[k].host == u && is_type(g, k, T_FOLLOWER))
                        g->c[k].bowed = 1;
            }
            break;
        case E_TOBATTLE:
            if (who >= 0 && g->phase == PH_BATTLE && unit_of(g, who) >= 0) {
                g->c[unit_of(g, who)].at = g->battle;
                glog(g, "  %s joins the battle.", nm(g, who));
            }
            break;
        case E_ODISCARD: {
            int k;
            for (k = 0; k < n; k++) {
                int pick = -1, seen = 0, j;
                for (j = 0; j < g->nc; j++)
                    if (g->c[j].owner == OPP(q) && g->c[j].zone == Z_HAND
                        && (int)(rnd(g) % (unsigned)(++seen)) == 0)
                        pick = j;
                if (pick >= 0) {
                    glog(g, "  %s discards %s.", player_clan(g, OPP(q)), nm(g, pick));
                    to_discard(g, pick);
                }
            }
            break;
        }
        default:           break;     /* statics are read, not resolved */
        }
    }
}

static void enters_play(Game *g, int i)
{
    const Def *d = D(g, i);
    int a;
    for (a = 0; a < d->nabil; a++)
        if (d->abil[a].timing == TM_ENTER)
            resolve(g, g->c[i].owner, i, &d->abil[a], -1, -1);
}

/* ---------------------------------------------------------------- turns */
static void check_counts(Game *g);

static void reveal(Game *g, int p)
{
    int k;
    for (k = 0; k < NPROV; k++) {
        int i = in_province(g, p, k), guard = 0;
        while (i >= 0 && g->c[i].down && guard++ < 8) {
            const Def *d = D(g, i);
            int a;
            g->c[i].down = 0;
            if (d->type == T_EVENT) {
                glog(g, "%s reveals the Event %s.", player_clan(g, p), d->name);
                for (a = 0; a < d->nabil; a++)
                    if (d->abil[a].timing == TM_REVEAL && d->abil[a].target == TG_NONE)
                        resolve(g, p, i, &d->abil[a], -1, -1);
                to_discard(g, i);
                refill(g, p, k);
                i = in_province(g, p, k);
                if (i >= 0)
                    g->c[i].down = 0;   /* turned up as the Event left */
                continue;
            }
            if (d->type == T_REGION) {
                int r;
                for (r = 0; r < g->nc; r++)       /* one Region a province */
                    if (g->c[r].owner == p && g->c[r].zone == Z_ATTACHED
                        && g->c[r].host < 0 && g->c[r].prov == k)
                        to_discard(g, r);
                g->c[i].zone = Z_ATTACHED;
                g->c[i].host = -1;
                glog(g, "%s's province gains the Region %s.", player_clan(g, p), d->name);
                refill(g, p, k);
                i = in_province(g, p, k);
                if (i >= 0)
                    g->c[i].down = 0;
                continue;
            }
        }
    }
}

static void start_turn(Game *g, int p)
{
    int i;
    g->active = p;
    g->turn++;
    if (g->turn > TURN_CAP * 2) {
        win(g, -1, WIN_DRAW);
        return;
    }
    if (g->p[p].honor >= HONOR_WIN) {
        win(g, p, WIN_HONOR);
        return;
    }
    for (i = 0; i < g->nc; i++) {
        g->c[i].fbonus = g->c[i].cbonus = 0;
        if (g->c[i].owner == p && (g->c[i].zone == Z_PLAY || g->c[i].zone == Z_ATTACHED))
            g->c[i].bowed = 0;
    }
    end_phase_pool(g);
    g->p[p].won_battle = 0;
    memset(g->p[0].pbonus, 0, sizeof g->p[0].pbonus);
    memset(g->p[1].pbonus, 0, sizeof g->p[1].pbonus);
    glog(g, "Turn %d: %s.", (g->turn + 1) / 2, player_clan(g, p));
    reveal(g, p);
    g->phase = PH_ACTION;
    g->priority = p;
    g->passes = 0;
}

static void end_turn(Game *g)
{
    int p = g->active;
    draw_fate(g, p, 1);
    if (count_zone(g, p, Z_HAND) > HAND_MAX) {
        g->phase = PH_DISCARD;
        return;
    }
    g->p[p].first_turn = 0;
    start_turn(g, OPP(p));
}

static int can_attack(const Game *g)
{
    int i;
    if (!provinces_left(g, OPP(g->active)))
        return 0;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == g->active && is_pers_in_play(g, i) && !g->c[i].bowed)
            return 1;
    return 0;
}

static void to_dynasty(Game *g)
{
    int i;
    for (i = 0; i < g->nc; i++)
        if (is_pers_in_play(g, i))
            g->c[i].at = -1;
    end_phase_pool(g);
    g->phase = PH_DYNASTY;
}

static int next_battle(const Game *g, int after)
{
    int k;
    for (k = after + 1; k < NPROV; k++)
        if (army_size(g, g->active, k))
            return k;
    return -1;
}

static void begin_battle(Game *g, int prov)
{
    g->battle = prov;
    g->phase = PH_BATTLE;
    g->priority = OPP(g->active);    /* the defender acts first */
    g->passes = 0;
    glog(g, "Battle at %s's province %d: %d Force against %d.",
         player_clan(g, OPP(g->active)), prov + 1,
         army_force(g, g->active, prov), army_force(g, OPP(g->active), prov));
}

static int destroy_army(Game *g, int p, int prov)
{
    int i, k, n = 0;
    for (i = 0; i < g->nc; i++) {
        if (g->c[i].owner != p || !is_pers_in_play(g, i) || g->c[i].at != prov)
            continue;
        n++;
        for (k = 0; k < g->nc; k++)
            if (g->c[k].zone == Z_ATTACHED && g->c[k].host == i && is_type(g, k, T_FOLLOWER))
                n++;
        destroy(g, i);
    }
    return n;
}

static void resolve_battle(Game *g)
{
    int att = g->active, def = OPP(att), b = g->battle, i;
    int A = army_force(g, att, b), D = army_force(g, def, b);
    int S = prov_strength(g, def, b);
    int na = army_size(g, att, b), nd = army_size(g, def, b);

    if (A > D) {
        int n = destroy_army(g, def, b);
        g->p[att].won_battle = 1;
        glog(g, "%s wins the battle, %d to %d.", player_clan(g, att), A, D);
        if (n) {
            honor_change(g, att, n * g->era->kill_honor);
            glog(g, "  %d defending cards destroyed.", n);
        }
        if (A > D + S) {
            int c = in_province(g, def, b);
            if (c >= 0)
                to_discard(g, c);
            for (i = 0; i < g->nc; i++)          /* its Region goes too */
                if (g->c[i].owner == def && g->c[i].zone == Z_ATTACHED
                    && g->c[i].host < 0 && g->c[i].prov == b)
                    to_discard(g, i);
            g->p[def].alive[b] = 0;
            glog(g, "  %s's province %d is destroyed.", player_clan(g, def), b + 1);
            if (!provinces_left(g, def))
                win(g, att, WIN_MILITARY);
        }
    } else if (D > A) {
        destroy_army(g, att, b);
        g->p[def].won_battle = 1;
        glog(g, "%s holds, %d to %d.", player_clan(g, def), D, A);
    } else if (na || nd) {
        destroy_army(g, att, b);
        destroy_army(g, def, b);
        glog(g, "The battle is a tie at %d: both armies are destroyed.", A);
    }
    /* Survivors go home: attackers bowed, defenders as they are. */
    for (i = 0; i < g->nc; i++)
        if (is_pers_in_play(g, i) && g->c[i].at == b) {
            if (g->c[i].owner == att)
                g->c[i].bowed = 1;
            g->c[i].at = -1;
        }
    if (g->phase == PH_OVER)
        return;
    b = next_battle(g, b);
    if (b >= 0)
        begin_battle(g, b);
    else
        to_dynasty(g);
}

/* ---------------------------------------------------------------- legal */
static int has_presence(const Game *g, int q)
{
    return army_size(g, q, g->battle) > 0;
}

/* The Personalities who could perform an ability that names one -- "your
 * performing Monk or Shugenja" -- or a Spell, which a Shugenja casts. */
static int performers(const Game *g, int q, const Ability *ab, int battle, int *out)
{
    char kws[32], *k, *save;
    int i, n = 0;
    snprintf(kws, sizeof kws, "%s", ab->perfkw);
    for (i = 0; i < g->nc; i++) {
        int ok = 0;
        if (g->c[i].owner != q || !is_pers_in_play(g, i))
            continue;
        if ((ab->cost & CO_BOWPERF) && g->c[i].bowed)
            continue;
        if (battle && !at_battle(g, i))
            continue;
        snprintf(kws, sizeof kws, "%s", ab->perfkw);
        for (k = strtok_r(kws, "/", &save); k; k = strtok_r(NULL, "/", &save))
            if (!strcmp(k, "any") || has_kw(g, i, k))
                ok = 1;
        if (ok)
            out[n++] = i;
    }
    return n;
}

static int shugenja_for(const Game *g, int q, int battle, int *out)
{
    int i, n = 0;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == q && is_pers_in_play(g, i) && !g->c[i].bowed
            && (D(g, i)->kw & KW_SHUGENJA) && (!battle || at_battle(g, i)))
            out[n++] = i;
    return n;
}

static int timing_now(const Game *g, int q, Timing t)
{
    if (g->phase == PH_BATTLE)
        return t == TM_BATTLE || t == TM_BATTLEOPEN;
    if (g->phase == PH_ACTION)
        return t == TM_OPEN || t == TM_BATTLEOPEN || (t == TM_LIMITED && q == g->active);
    return 0;
}

static void add(Action *out, int *n, AKind k, int src, int abil, int tgt, int perf, int prov)
{
    Action a;
    if (*n >= MAX_ACTS)
        return;
    a.k = k; a.src = src; a.abil = abil; a.tgt = tgt; a.perf = perf; a.prov = prov;
    out[(*n)++] = a;
}

static int attach_ok(const Game *g, int card, int pers)
{
    const Def *d = D(g, card), *pd = D(g, pers);
    int a;
    if (d->type == T_SPELL && !(pd->kw & KW_SHUGENJA))
        return 0;                 /* a Spell is equipped by a Shugenja */
    for (a = 0; a < d->nabil; a++)
        if (d->abil[a].timing == TM_STATIC && d->abil[a].neff
            && d->abil[a].eff[0].op == E_ATTACHONLY) {
            const char *want = d->abil[a].arg;
            char word[32];
            sscanf(want, "%31s", word);
            if (!strstr(pd->kwtext, word) && !strstr(pd->clan, word) && !strstr(pd->name, word))
                return 0;
        }
    return 1;
}

static void legal_abilities(const Game *g, int q, Action *out, int *n)
{
    int i, battle = g->phase == PH_BATTLE;
    int tg[MAX_INST];

    if (battle && !has_presence(g, q))
        return;
    for (i = 0; i < g->nc; i++) {
        const Inst *c = &g->c[i];
        const Def *d = D(g, i);
        int a;
        if (c->owner != q)
            continue;
        /* Fate cards from the hand */
        if (c->zone == Z_HAND && (d->type == T_STRATEGY
                                  || (d->type == T_SPELL && !g->era->spells_equip))) {
            int perf[MAX_INST], np = 1, pi;
            perf[0] = -1;
            for (a = 0; a < d->nabil; a++) {
                const Ability *ab = &d->abil[a];
                int nt, t;
                if (!timing_now(g, q, ab->timing))
                    continue;
                if (gold_available(g, q) < ab->gold + d->cost)
                    continue;
                np = 1;
                perf[0] = -1;
                if (ab->perfkw[0])
                    np = performers(g, q, ab, battle, perf);
                else if (d->type == T_SPELL)
                    np = shugenja_for(g, q, battle, perf);
                for (pi = 0; pi < np; pi++) {
                    nt = ability_targets(g, q, ab, battle, perf[pi], tg);
                    for (t = 0; t < nt; t++)
                        add(out, n, A_PLAY, i, a, tg[t], perf[pi], -1);
                }
            }
            /* A card the engine cannot read is still playable by hand. */
            if (!d->understood && (g->phase == PH_ACTION || battle)
                && gold_available(g, q) >= d->cost)
                add(out, n, A_PLAY, i, -1, -1, -1, -1);
        }
        /* Followers and Items attach, as a Limited action, at home */
        if (c->zone == Z_HAND && (d->type == T_FOLLOWER || d->type == T_ITEM
                                  || (d->type == T_SPELL && g->era->spells_equip))
            && g->phase == PH_ACTION && q == g->active
            && honor_ok(g, q, i) && gold_available(g, q) >= d->cost) {
            int k;
            for (k = 0; k < g->nc; k++)
                if (g->c[k].owner == q && is_pers_in_play(g, k) && g->c[k].at < 0
                    && attach_ok(g, i, k))
                    add(out, n, A_PLAY, i, -1, -1, k, -1);
        }
        /* A Ring is played when its condition is met.  The conditions are
         * one-offs the engine does not read, so the person judges them. */
        if (c->zone == Z_HAND && d->type == T_RING && g->phase == PH_ACTION && q == g->active)
            add(out, n, A_PLAY, i, -1, -1, -1, -1);
        /* Abilities of cards in play */
        if (c->zone == Z_PLAY || (c->zone == Z_ATTACHED && c->host >= 0)) {
            for (a = 0; a < d->nabil; a++) {
                const Ability *ab = &d->abil[a];
                int nt, t;
                if (!timing_now(g, q, ab->timing))
                    continue;
                if ((ab->cost & CO_BOW) && c->bowed)
                    continue;
                if ((ab->cost & CO_BOWPERF) && (unit_of(g, i) < 0 || g->c[unit_of(g, i)].bowed))
                    continue;
                if (battle && unit_of(g, i) >= 0 && !at_battle(g, i))
                    continue;
                if (gold_available(g, q) < ab->gold)
                    continue;
                nt = ability_targets(g, q, ab, battle, unit_of(g, i), tg);
                for (t = 0; t < nt; t++)
                    add(out, n, A_USE, i, a, tg[t], -1, -1);
            }
        }
    }
}

int l5r_decider(const Game *g)
{
    switch (g->phase) {
    case PH_ACTION: case PH_BATTLE: return g->priority;
    case PH_DEFEND:                 return OPP(g->active);
    case PH_OVER:                   return -1;
    default:                        return g->active;
    }
}

int l5r_legal(const Game *g, Action *out)
{
    int n = 0, i, k, q = l5r_decider(g);
    if (q < 0)
        return 0;
    switch (g->phase) {
    case PH_ACTION:
    case PH_BATTLE:
        add(out, &n, A_PASS, -1, -1, -1, -1, -1);
        legal_abilities(g, q, out, &n);
        break;
    case PH_ATTACK:
        add(out, &n, A_NOATTACK, -1, -1, -1, -1, -1);
        add(out, &n, A_ATTACK, -1, -1, -1, -1, -1);
        break;
    case PH_ASSIGN:
        add(out, &n, A_DONE, -1, -1, -1, -1, -1);
        for (i = 0; i < g->nc; i++)
            if (g->c[i].owner == q && is_pers_in_play(g, i) && !g->c[i].bowed && g->c[i].at < 0)
                for (k = 0; k < NPROV; k++)
                    if (g->p[OPP(q)].alive[k])
                        add(out, &n, A_ASSIGN, i, -1, -1, -1, k);
        break;
    case PH_DEFEND:
        add(out, &n, A_DONE, -1, -1, -1, -1, -1);
        for (i = 0; i < g->nc; i++)
            if (g->c[i].owner == q && is_pers_in_play(g, i) && !g->c[i].bowed && g->c[i].at < 0)
                for (k = 0; k < NPROV; k++)
                    if (army_size(g, g->active, k))
                        add(out, &n, A_ASSIGN, i, -1, -1, -1, k);
        break;
    case PH_DYNASTY:
        add(out, &n, A_PASS, -1, -1, -1, -1, -1);
        for (k = 0; k < NPROV; k++) {
            int c = in_province(g, q, k);
            if (c < 0 || g->c[c].down)
                continue;
            if (honor_ok(g, q, c) && unique_ok(g, q, c)
                && gold_available(g, q) >= buy_cost(g, q, c))
                add(out, &n, A_BUY, c, -1, -1, -1, k);
            if (g->p[q].first_turn)
                add(out, &n, A_CYCLE, c, -1, -1, -1, k);
        }
        break;
    case PH_DISCARD:
        for (i = 0; i < g->nc; i++)
            if (g->c[i].owner == q && g->c[i].zone == Z_HAND)
                add(out, &n, A_DISCARD, i, -1, -1, -1, -1);
        break;
    default:
        break;
    }
    return n;
}

/* ---------------------------------------------------------------- apply */
static void after_action(Game *g)
{
    g->passes = 0;
    g->priority = OPP(g->priority);
}

static void play_card(Game *g, int q, Action a)
{
    int i = a.src;
    const Def *d = D(g, i);

    if (d->type == T_FOLLOWER || d->type == T_ITEM || (d->type == T_SPELL && a.abil < 0 && a.perf >= 0)) {
        pay(g, q, d->cost);
        g->c[i].zone = Z_ATTACHED;
        g->c[i].host = a.perf;
        g->c[i].bowed = 0;
        glog(g, "%s attaches %s to %s.", player_clan(g, q), d->name, nm(g, a.perf));
        enters_play(g, i);
        return;
    }
    if (d->type == T_RING) {
        g->c[i].zone = Z_PLAY;
        glog(g, "%s puts %s into play.", player_clan(g, q), d->name);
        if (rings_in_play(g, q) >= 5)
            win(g, q, WIN_ENLIGHTEN);
        return;
    }
    if (a.abil < 0) {
        /* By hand: the card is paid for and discarded, and its text goes
         * to the log for the person to carry out with the table commands. */
        pay(g, q, d->cost);
        to_discard(g, i);
        glog(g, "%s plays %s (by hand).", player_clan(g, q), d->name);
        return;
    }
    {
        const Ability *ab = &d->abil[a.abil];
        pay(g, q, d->cost + ab->gold);
        g->c[i].zone = Z_FATEDISC;      /* gone from the hand while it resolves */
        if (a.perf >= 0 && (ab->cost & (CO_BOW | CO_BOWPERF)))
            g->c[a.perf].bowed = 1;
        if (a.perf >= 0 && (ab->cost & CO_DESTROYPERF)) {
            destroy(g, a.perf);
            a.perf = -1;
        }
        if (a.perf >= 0 && (ab->cost & CO_DESTROY))
            destroy(g, a.perf);
        glog(g, "%s plays %s%s%s.", player_clan(g, q), d->name,
             a.tgt >= 0 ? " on " : "", a.tgt >= 0 ? nm(g, a.tgt) : "");
        resolve(g, q, i, ab, a.tgt, a.perf);
        to_discard(g, i);
    }
}

static void use_ability(Game *g, int q, Action a)
{
    int i = a.src;
    const Ability *ab = &D(g, i)->abil[a.abil];
    pay(g, q, ab->gold);
    if (ab->cost & CO_BOW)
        g->c[i].bowed = 1;
    if ((ab->cost & CO_BOWPERF) && unit_of(g, i) >= 0)
        g->c[unit_of(g, i)].bowed = 1;
    glog(g, "%s uses %s%s%s.", player_clan(g, q), nm(g, i),
         a.tgt >= 0 ? " on " : "", a.tgt >= 0 ? nm(g, a.tgt) : "");
    /* an equipped Spell is performed by the Shugenja carrying it */
    resolve(g, q, i, ab, a.tgt, is_type(g, i, T_SPELL) ? unit_of(g, i) : -1);
    if (ab->cost & CO_DESTROY)
        destroy(g, i);
}

static void recruit(Game *g, int q, Action a)
{
    int i = a.src;
    const Def *d = D(g, i);
    int cost = buy_cost(g, q, i);

    if (!pay(g, q, cost))
        return;
    g->c[i].zone = Z_PLAY;
    g->c[i].down = 0;
    g->c[i].at = -1;
    g->c[i].bowed = d->type == T_HOLDING && g->era->holdings_bowed;
    glog(g, "%s recruits %s for %d gold.", player_clan(g, q), d->name, cost);
    if (d->type == T_PERSONALITY && g->era->recruit_honor
        && !strcmp(d->clan, player_clan(g, q)) && d->ph > 0)
        honor_change(g, q, d->ph);
    refill(g, q, a.prov);
    enters_play(g, i);
}

void l5r_apply(Game *g, Action a)
{
    int q = l5r_decider(g);
    if (q < 0)
        return;
    switch (a.k) {
    case A_PASS:
        if (g->phase == PH_DYNASTY) {
            end_phase_pool(g);
            end_turn(g);
            break;
        }
        g->passes++;
        g->priority = OPP(g->priority);
        if (g->passes >= 2) {
            end_phase_pool(g);
            if (g->phase == PH_BATTLE)
                resolve_battle(g);
            else if (can_attack(g))
                g->phase = PH_ATTACK;
            else
                to_dynasty(g);
        }
        break;
    case A_PLAY:
        play_card(g, q, a);
        after_action(g);
        break;
    case A_USE:
        use_ability(g, q, a);
        after_action(g);
        break;
    case A_NOATTACK:
        to_dynasty(g);
        break;
    case A_ATTACK:
        glog(g, "%s attacks.", player_clan(g, q));
        g->phase = PH_ASSIGN;
        break;
    case A_ASSIGN:
        g->c[a.src].at = a.prov;
        glog(g, "  %s %s province %d.", nm(g, a.src),
             q == g->active ? "marches on" : "defends", a.prov + 1);
        break;
    case A_DONE:
        if (g->phase == PH_ASSIGN) {
            if (next_battle(g, -1) < 0) {
                glog(g, "  ...and thinks better of it.");
                to_dynasty(g);
            } else
                g->phase = PH_DEFEND;
        } else {
            begin_battle(g, next_battle(g, -1));
        }
        break;
    case A_BUY:
        recruit(g, q, a);
        break;
    case A_CYCLE:
        glog(g, "%s discards %s from a province.", player_clan(g, q), nm(g, a.src));
        to_discard(g, a.src);
        refill(g, q, a.prov);
        break;
    case A_DISCARD:
        to_discard(g, a.src);
        if (count_zone(g, q, Z_HAND) <= HAND_MAX) {
            g->p[q].first_turn = 0;
            start_turn(g, OPP(q));
        }
        break;
    default:
        break;
    }
    if (g->check)
        l5r_check(g);
}

/* --------------------------------------------------------------- by hand */
int l5r_manual(Game *g, int seat, Manual m, int i)
{
    if (g->phase == PH_OVER)
        return -1;
    if (m == M_HONOR_UP || m == M_HONOR_DOWN) {
        honor_change(g, seat, m == M_HONOR_UP ? 1 : -1);
        glog(g, "%s %s 1 honor (by hand).", player_clan(g, seat), m == M_HONOR_UP ? "gains" : "loses");
        return 0;
    }
    if (m == M_DRAW) {
        draw_fate(g, seat, 1);
        glog(g, "%s draws a card (by hand).", player_clan(g, seat));
        return 0;
    }
    if (i < 0 || i >= g->nc)
        return -1;
    switch (m) {
    case M_BOW:        if (g->c[i].zone != Z_PLAY && g->c[i].zone != Z_ATTACHED) return -1;
                       g->c[i].bowed = 1; break;
    case M_STRAIGHTEN: if (g->c[i].zone != Z_PLAY && g->c[i].zone != Z_ATTACHED) return -1;
                       g->c[i].bowed = 0; break;
    case M_DESTROY:    if (g->c[i].zone != Z_PLAY && g->c[i].zone != Z_ATTACHED) return -1;
                       destroy(g, i); break;
    case M_HOME:       move_home(g, i); break;
    case M_FORCE_UP:   g->c[i].fbonus++; break;
    case M_FORCE_DOWN: g->c[i].fbonus--; break;
    case M_DISCARD:    if (g->c[i].zone != Z_HAND || g->c[i].owner != seat) return -1;
                       to_discard(g, i); break;
    default:           return -1;
    }
    glog(g, "%s: %s %s (by hand).", player_clan(g, seat), manual_name[m], nm(g, i));
    if (g->check)
        l5r_check(g);
    return 0;
}

/* ---------------------------------------------------------------- labels */
void l5r_label(const Game *g, Action a, char *buf, int n)
{
    const char *tn = a.tgt >= 0 ? nm(g, a.tgt) : "";
    switch (a.k) {
    case A_PASS:
        snprintf(buf, n, g->phase == PH_DYNASTY ? "End turn" : "Pass");
        break;
    case A_BUY:
        snprintf(buf, n, "Recruit %s (%d gold)", nm(g, a.src), buy_cost(g, g->active, a.src));
        break;
    case A_CYCLE:
        snprintf(buf, n, "Discard %s from province %d", nm(g, a.src), a.prov + 1);
        break;
    case A_PLAY:
        if (a.abil < 0 && a.perf >= 0)
            snprintf(buf, n, "Attach %s to %s (%d gold)", nm(g, a.src), nm(g, a.perf), D(g, a.src)->cost);
        else if (a.abil < 0)
            snprintf(buf, n, "Play %s (by hand)", nm(g, a.src));
        else if (a.perf >= 0)
            snprintf(buf, n, "Cast %s with %s%s%s", nm(g, a.src), nm(g, a.perf), *tn ? " on " : "", tn);
        else
            snprintf(buf, n, "Play %s%s%s", nm(g, a.src), *tn ? " on " : "", tn);
        break;
    case A_USE:
        snprintf(buf, n, "Use %s%s%s", nm(g, a.src), *tn ? " on " : "", tn);
        break;
    case A_ATTACK:   snprintf(buf, n, "Declare an attack"); break;
    case A_NOATTACK: snprintf(buf, n, "No attack"); break;
    case A_ASSIGN:
        snprintf(buf, n, "%s %s to province %d",
                 g->phase == PH_ASSIGN ? "Send" : "Defend with", nm(g, a.src), a.prov + 1);
        break;
    case A_DONE:     snprintf(buf, n, "Done"); break;
    case A_DISCARD:  snprintf(buf, n, "Discard %s", nm(g, a.src)); break;
    default:         snprintf(buf, n, "?"); break;
    }
}

/* ----------------------------------------------------------------- setup */
static int add_card(Game *g, int def, int owner, Zone z)
{
    Inst *c;
    if (g->nc == MAX_INST)
        return -1;
    c = &g->c[g->nc];
    memset(c, 0, sizeof *c);
    c->def = def;
    c->owner = owner;
    c->zone = z;
    c->host = -1;
    c->at = -1;
    c->order = (int)(rnd(g) % 1000000);
    return g->nc++;
}

/* The face on a player's portrait: the deck's Clan Champion if it has
 * one, else its grandest Unique of the clan.  Only the client reads it. */
static int champion(const int *deck, int n, const char *clan)
{
    int i, best = -1, bs = -1;
    for (i = 0; i < n; i++) {
        const Def *d = &defs[deck[i]];
        int s;
        if (d->type != T_PERSONALITY || strcmp(d->clan, clan))
            continue;
        s = (strstr(d->kwtext, "Champion") ? 1000 : 0) + (d->kw & KW_UNIQUE ? 100 : 0) + d->cost;
        if (s > bs) { bs = s; best = d->oid; }
    }
    return best;
}

int l5r_setup(Game *g, const Era *era, const int *deck0, int n0,
              const int *deck1, int n1, unsigned long seed)
{
    const int *decks[2];
    int ns[2], p, i, k;
    FILE *trace = g->trace;
    int check = g->check;

    memset(g, 0, sizeof *g);
    g->trace = trace;
    g->check = check;
    g->era = era;
    g->rng = seed * 2654435761UL + 1;
    g->winner = -1;
    decks[0] = deck0; decks[1] = deck1;
    ns[0] = n0; ns[1] = n1;
    for (p = 0; p < 2; p++) {
        g->p[p].stronghold = -1;
        for (i = 0; i < ns[p]; i++) {
            const Def *d = &defs[decks[p][i]];
            int c;
            if (d->type == T_STRONGHOLD) {
                if (g->p[p].stronghold >= 0)
                    continue;           /* one stronghold */
                c = add_card(g, decks[p][i], p, Z_PLAY);
                g->p[p].stronghold = c;
            } else
                c = add_card(g, decks[p][i], p, d->fate ? Z_FATEDECK : Z_DYNDECK);
            if (c < 0)
                return -1;
        }
        if (g->p[p].stronghold < 0)
            return -1;
        g->p[p].honor = D(g, g->p[p].stronghold)->shonor;
        g->p[p].face = champion(decks[p], ns[p], D(g, g->p[p].stronghold)->clan);
        g->p[p].first_turn = 1;
        for (k = 0; k < NPROV; k++)
            g->p[p].alive[k] = 1;
    }
    for (p = 0; p < 2; p++) {
        for (k = 0; k < NPROV; k++)
            refill(g, p, k);
        draw_fate(g, p, START_HAND);
        g->counts[p] = 0;
        for (i = 0; i < g->nc; i++)
            g->counts[p] += g->c[i].owner == p;
    }
    glog(g, "%s (%d honor) against %s (%d honor), %s rules.",
         player_clan(g, 0), g->p[0].honor, player_clan(g, 1), g->p[1].honor, era->name);
    start_turn(g, (int)(rnd(g) & 1));
    return 0;
}

/* ------------------------------------------------------------ invariants */
#define INV(cond, ...) do { if (!(cond)) { \
        fprintf(g->trace ? g->trace : stderr, "INV T%d ", g->turn); \
        fprintf(g->trace ? g->trace : stderr, __VA_ARGS__); \
        fputc('\n', g->trace ? g->trace : stderr); } } while (0)

static void check_counts(Game *g)
{
    int p, i, n;
    for (p = 0; p < 2; p++) {
        for (n = i = 0; i < g->nc; i++)
            n += g->c[i].owner == p;
        INV(n == g->counts[p], "player %d holds %d cards, began with %d", p, n, g->counts[p]);
    }
}

void l5r_check(Game *g)
{
    int i, p, k;
    check_counts(g);
    for (i = 0; i < g->nc; i++) {
        const Inst *c = &g->c[i];
        if (c->zone == Z_ATTACHED && c->host >= 0) {
            INV(is_pers_in_play(g, c->host), "%s attached to %s, not in play", nm(g, i), nm(g, c->host));
            INV(g->c[c->host].owner == c->owner, "%s attached across players", nm(g, i));
        }
        if (c->zone == Z_PROVINCE)
            INV(g->p[c->owner].alive[c->prov], "%s in a destroyed province", nm(g, i));
        if (c->down)
            INV(c->zone == Z_PROVINCE, "%s face down outside a province", nm(g, i));
        if (c->at >= 0) {
            INV(is_pers_in_play(g, i), "%s at a battlefield but not a Personality in play", nm(g, i));
            INV(g->phase >= PH_ASSIGN && g->phase <= PH_BATTLE, "%s at a battlefield in the %s phase",
                nm(g, i), phase_name[g->phase]);
        }
        if (c->bowed)
            INV(c->zone == Z_PLAY || c->zone == Z_ATTACHED, "%s bowed outside play", nm(g, i));
    }
    for (p = 0; p < 2; p++) {
        INV(g->p[p].pool >= 0, "player %d gold pool %d", p, g->p[p].pool);
        if (g->phase != PH_OVER)
            INV(g->p[p].honor > HONOR_LOSE, "player %d on %d honor and playing", p, g->p[p].honor);
        for (k = 0; k < NPROV; k++) {
            int n = 0;
            for (i = 0; i < g->nc; i++)
                n += g->c[i].owner == p && g->c[i].zone == Z_PROVINCE && g->c[i].prov == k;
            INV(n <= 1, "player %d province %d holds %d cards", p, k, n);
        }
    }
}
