/* A View: what one seat may see, as one line of JSON.
 *
 * Built per seat from the Game, so there is nothing to filter: the other
 * player's hand is a count, face-down Province cards are "down" with no
 * card, and the decks are sizes.  Cards are sent as the instance number
 * and the Oracle id; the client reads names, text and art from the same
 * data/ERA/cards.tsv the engine loaded.
 */
#include "rokugan.h"
#include <string.h>

#define D(g, i) (&defs[(g)->c[i].def])

static void jstr(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            fprintf(f, "\\%c", *s);
        else if ((unsigned char)*s < 0x20)
            fprintf(f, "\\u%04x", *s);
        else
            fputc(*s, f);
    }
    fputc('"', f);
}

static void card(FILE *f, const Game *g, int i)
{
    const Inst *c = &g->c[i];
    const Def *d = D(g, i);
    fprintf(f, "{\"i\":%d,\"o\":%d,\"n\":", i, d->oid);
    jstr(f, d->name);
    fprintf(f, ",\"t\":\"%s\",\"b\":%d,\"auto\":%d", type_name[d->type], c->bowed, d->understood);
    if (d->type == T_PERSONALITY && c->zone == Z_PLAY) {
        int k, first = 1;
        fprintf(f, ",\"f\":%d,\"c\":%d,\"at\":%d,\"fb\":%d,\"att\":[",
                unit_force(g, i), card_chi(g, i), c->at, c->fbonus);
        for (k = 0; k < g->nc; k++)
            if (g->c[k].zone == Z_ATTACHED && g->c[k].host == i) {
                if (!first)
                    fputc(',', f);
                first = 0;
                card(f, g, k);
            }
        fputc(']', f);
    } else if (d->type == T_FOLLOWER || d->type == T_ITEM) {
        fprintf(f, ",\"f\":%d,\"c\":%d", d->force + c->fbonus, d->chi);
    }
    fputc('}', f);
}

static int count(const Game *g, int p, Zone z)
{
    int i, n = 0;
    for (i = 0; i < g->nc; i++)
        n += g->c[i].owner == p && g->c[i].zone == z;
    return n;
}

static void player(FILE *f, const Game *g, int p, int seat)
{
    int k, i, first;
    fprintf(f, "{\"clan\":");
    jstr(f, player_clan(g, p));
    fprintf(f, ",\"honor\":%d,\"pool\":%d,\"gold\":%d,\"hand\":%d,\"fate\":%d,"
               "\"dynasty\":%d,\"fdisc\":%d,\"ddisc\":%d,\"rings\":%d,\"provinces\":[",
            g->p[p].honor, g->p[p].pool, gold_available(g, p), count(g, p, Z_HAND),
            count(g, p, Z_FATEDECK), count(g, p, Z_DYNDECK), count(g, p, Z_FATEDISC),
            count(g, p, Z_DYNDISC), rings_in_play(g, p));
    for (k = 0; k < NPROV; k++) {
        int c = -1, region = -1;
        for (i = 0; i < g->nc; i++) {
            if (g->c[i].owner != p)
                continue;
            if (g->c[i].zone == Z_PROVINCE && g->c[i].prov == k)
                c = i;
            if (g->c[i].zone == Z_ATTACHED && g->c[i].host < 0 && g->c[i].prov == k)
                region = i;
        }
        fprintf(f, "%s{\"alive\":%d,\"str\":%d,\"down\":%d,\"card\":", k ? "," : "",
                g->p[p].alive[k], prov_strength(g, p, k), c >= 0 && g->c[c].down);
        if (c >= 0 && !g->c[c].down)
            card(f, g, c);
        else
            fputs("null", f);
        fputs(",\"region\":", f);
        if (region >= 0)
            card(f, g, region);
        else
            fputs("null", f);
        fputc('}', f);
    }
    fputs("],\"play\":[", f);
    first = 1;
    for (i = 0; i < g->nc; i++)
        if (g->c[i].owner == p && g->c[i].zone == Z_PLAY) {
            if (!first)
                fputc(',', f);
            first = 0;
            card(f, g, i);
        }
    fputs("]", f);
    if (p == seat) {
        fputs(",\"handcards\":[", f);
        first = 1;
        for (i = 0; i < g->nc; i++)
            if (g->c[i].owner == p && g->c[i].zone == Z_HAND) {
                if (!first)
                    fputc(',', f);
                first = 0;
                card(f, g, i);
            }
        fputc(']', f);
    }
    fputc('}', f);
}

void view_write(FILE *f, const Game *g, int seat, const Action *acts, int n)
{
    int i;
    char label[160];
    fprintf(f, "{\"seat\":%d,\"era\":\"%s\",\"turn\":%d,\"active\":%d,\"phase\":\"%s\","
               "\"decider\":%d,\"battle\":%d,\"winner\":%d,\"how\":\"%s\",\"players\":[",
            seat, g->era->key, (g->turn + 1) / 2, g->active, phase_name[g->phase],
            l5r_decider(g), g->phase == PH_BATTLE ? g->battle : -1, g->winner, win_name[g->how]);
    player(f, g, 0, seat);
    fputc(',', f);
    player(f, g, 1, seat);
    fputs("],\"actions\":[", f);
    if (l5r_decider(g) == seat)
        for (i = 0; i < n; i++) {
            l5r_label(g, acts[i], label, sizeof label);
            fprintf(f, "%s{\"k\":%d,\"s\":%d,\"t\":%d,\"p\":%d,\"v\":%d,\"l\":",
                    i ? "," : "", acts[i].k, acts[i].src, acts[i].tgt, acts[i].perf, acts[i].prov);
            jstr(f, label);
            fputc('}', f);
        }
    fputs("],\"log\":[", f);
    {
        int from = g->nlog > LOG_KEEP ? g->nlog - LOG_KEEP : 0, first = 1;
        for (i = from; i < g->nlog; i++) {
            if (!first)
                fputc(',', f);
            first = 0;
            jstr(f, g->log[i % LOG_KEEP]);
        }
    }
    fputs("]}\n", f);
    fflush(f);
}
