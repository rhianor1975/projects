/* The card list, read from cards.tsv at startup.
 *
 * Data rather than code so that a fuller card pool can be dropped in
 * without recompiling: one row per card, tab-separated, a header line
 * naming the columns.  Columns may come in any order; unknown ones are
 * ignored, and a missing one reads as zero or empty.
 *
 * What is refused, loudly: an unknown type, clan, timing or fx keyword.
 * A card list with a typo in it should stop the program at the door,
 * not deal a card that does nothing and leave you to find out why.
 */
#include "rokugan.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Def defs[MAX_DEFS];
int ndefs;

const char *clan_name[CL_COUNT] = {
    "Ronin", "Crab", "Crane", "Dragon", "Lion", "Phoenix", "Scorpion", "Unicorn"
};
const char *type_name[T_COUNT] = {
    "Stronghold", "Personality", "Holding", "Event",
    "Follower", "Item", "Strategy", "Spell", "Ring"
};
const char *fx_name[FX_COUNT] = {
    "", "provstr", "honorbow", "ev_honor", "ev_draw", "ev_storm", "ev_plague",
    "force", "ambush", "killfol", "rally", "withdraw", "duel", "gainhonor",
    "losehonor", "insult", "assassin", "draw", "straighten",
    "ring_earth", "ring_water", "ring_fire", "ring_air", "ring_void"
};
static const char *when_name[] = { "", "open", "limited", "battle" };

static int lookup(const char *s, const char **tab, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(s, tab[i]))
            return i;
    return -1;
}

int def_find(const char *name)
{
    int i;
    for (i = 0; i < ndefs; i++)
        if (!strcmp(defs[i].name, name))
            return i;
    return -1;
}

/* Split a line on tabs in place.  Returns the field count. */
static int split(char *s, char **f, int max)
{
    int n = 0;
    char *nl = strpbrk(s, "\r\n");
    if (nl)
        *nl = 0;
    while (n < max) {
        f[n++] = s;
        s = strchr(s, '\t');
        if (!s)
            break;
        *s++ = 0;
    }
    return n;
}

/* copies: "Crab:3 Crane:1 all:2" -- all applies to every Great Clan. */
static int parse_copies(Def *d, const char *s, char *err, int errlen)
{
    char buf[256], *tok;
    strncpy(buf, s, sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (tok = strtok(buf, " ,"); tok; tok = strtok(NULL, " ,")) {
        char *colon = strchr(tok, ':');
        int n, c;
        if (!colon) {
            snprintf(err, errlen, "%s: copies entry '%s' has no ':'", d->name, tok);
            return -1;
        }
        *colon = 0;
        n = atoi(colon + 1);
        if (!strcmp(tok, "all")) {
            for (c = CL_CRAB; c < CL_COUNT; c++)
                d->copies[c] += n;
            continue;
        }
        c = lookup(tok, clan_name, CL_COUNT);
        if (c < 1) {
            snprintf(err, errlen, "%s: unknown clan '%s' in copies", d->name, tok);
            return -1;
        }
        d->copies[c] += n;
    }
    return 0;
}

int data_load(const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    char *hdr[32], *fld[32];
    char hbuf[1024];
    int nh, lineno = 1;

    if (!f) {
        snprintf(err, errlen, "cannot open %s", path);
        return -1;
    }
    if (!fgets(hbuf, sizeof hbuf, f)) {
        snprintf(err, errlen, "%s is empty", path);
        fclose(f);
        return -1;
    }
    nh = split(hbuf, hdr, 32);
    ndefs = 0;
    while (fgets(line, sizeof line, f)) {
        Def *d;
        int nf, i;
        lineno++;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (ndefs == MAX_DEFS) {
            snprintf(err, errlen, "%s: more than %d cards", path, MAX_DEFS);
            fclose(f);
            return -1;
        }
        d = &defs[ndefs];
        memset(d, 0, sizeof *d);
        nf = split(line, fld, 32);
        for (i = 0; i < nh && i < nf; i++) {
            const char *k = hdr[i], *v = fld[i];
            int x;
            if (!strcmp(k, "name"))
                snprintf(d->name, sizeof d->name, "%s", v);
            else if (!strcmp(k, "type")) {
                if ((x = lookup(v, type_name, T_COUNT)) < 0) goto bad;
                d->type = (CType)x;
            } else if (!strcmp(k, "clan")) {
                if ((x = lookup(v, clan_name, CL_COUNT)) < 0) goto bad;
                d->clan = (Clan)x;
            } else if (!strcmp(k, "when")) {
                if ((x = lookup(v, when_name, 4)) < 0) goto bad;
                d->when = (When)x;
            } else if (!strcmp(k, "fx")) {
                if ((x = lookup(v, fx_name, FX_COUNT)) < 0) goto bad;
                d->fx = (Fx)x;
            } else if (!strcmp(k, "kw")) {
                if (strstr(v, "unique"))   d->kw |= KW_UNIQUE;
                if (strstr(v, "shugenja")) d->kw |= KW_SHUGENJA;
                if (strstr(v, "cavalry"))  d->kw |= KW_CAVALRY;
            } else if (!strcmp(k, "copies")) {
                if (parse_copies(d, v, err, errlen) < 0) { fclose(f); return -1; }
            } else if (!strcmp(k, "text"))
                snprintf(d->text, sizeof d->text, "%s", v);
            else if (!strcmp(k, "cost"))   d->cost = atoi(v);
            else if (!strcmp(k, "force"))  d->force = atoi(v);
            else if (!strcmp(k, "chi"))    d->chi = atoi(v);
            else if (!strcmp(k, "hreq"))   d->hreq = atoi(v);
            else if (!strcmp(k, "ph"))     d->phonor = atoi(v);
            else if (!strcmp(k, "focus"))  d->focus = atoi(v);
            else if (!strcmp(k, "gold"))   d->gold = atoi(v);
            else if (!strcmp(k, "pstr"))   d->pstr = atoi(v);
            else if (!strcmp(k, "honor"))  d->shonor = atoi(v);
            else if (!strcmp(k, "n"))      d->n = atoi(v);
            continue;
        bad:
            snprintf(err, errlen, "%s:%d: unknown %s '%s'", path, lineno, k, v);
            fclose(f);
            return -1;
        }
        if (!d->name[0]) {
            snprintf(err, errlen, "%s:%d: a card with no name", path, lineno);
            fclose(f);
            return -1;
        }
        if (def_find(d->name) >= 0) {
            snprintf(err, errlen, "%s:%d: '%s' twice", path, lineno, d->name);
            fclose(f);
            return -1;
        }
        if (!IS_DYNASTY(d->type) && d->type != T_RING && d->type != T_FOLLOWER
            && d->type != T_ITEM && d->when == W_NONE) {
            snprintf(err, errlen, "%s:%d: '%s' is a %s with no timing",
                     path, lineno, d->name, type_name[d->type]);
            fclose(f);
            return -1;
        }
        ndefs++;
    }
    fclose(f);
    if (!ndefs) {
        snprintf(err, errlen, "%s holds no cards", path);
        return -1;
    }
    return 0;
}
