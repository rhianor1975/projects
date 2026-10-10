/* The card pool, read from data/ERA/cards.tsv at startup, and the
 * hand-written ability encodings from fx/ERA.tsv on top of it.
 *
 * cards.tsv is written by tools/build-cards.py from the Oracle of the
 * Void and is not in the repository (AEG's text).  fx/ERA.tsv is: it holds
 * nothing but card names and this program's own notation, and a line in
 * it replaces whatever the text reader made of that card.
 *
 * Refused, loudly: an unknown type, timing, target or effect in an
 * encoding.  A typo there should stop the program at the door rather
 * than deal a card that quietly does nothing.
 */
#include "rokugan.h"
#include <stdlib.h>
#include <string.h>

Def defs[MAX_DEFS];
int ndefs;

const char *type_name[T_COUNT] = {
    "Stronghold", "Personality", "Holding", "Event", "Region",
    "Follower", "Item", "Strategy", "Spell", "Ring", "Other"
};
static const char *timing_name[TM_COUNT] = {
    "battle", "limited", "open", "enter", "produce", "static", "reveal", "battleopen"
};
static const char *target_name[TG_COUNT] = {
    "none", "self", "opers", "epers", "efol", "ecard", "eunit", "ounit",
    "eholding", "apers", "afol", "acard", "ocard", "eattach", "oholding"
};
static const char *eff_name[E_COUNT] = {
    "force", "chi", "destroy", "bow", "straighten", "home", "gain", "lose",
    "olose", "draw", "produce", "ranged", "melee", "fear",
    "attforce", "defforce", "pstr", "ph", "kw", "attachonly", "discount",
    "noenlighten", "pforce", "pchi", "pph", "bowfollowers", "tobattle", "odiscard",
    "rangedchi", "fearchi", "bowunit", "duel", "fduelbow", "provstr"
};

/* Each era's rules, where they changed.  The values are the ones this
 * program could support from the period rulebooks and from card text
 * that states a rule ("if you have less than four provinces", "-20 or
 * lower Honor"); where an edition is not certain the comment says so. */
const Era eras[] = {
    /* Gold Edition, the Four Winds arc (2000-2002). */
    { "gold", "Gold (Four Winds)",
      2, 1, 1, 0, 0, 2 },
    /* Celestial Edition, the Destroyer War arc (2009-2011).  The gold
     * pool lasting the phase arrived with Lotus. */
    { "celestial", "Celestial (Destroyer War)",
      2, 1, 1, 1, 1, 2 },
    /* Ivory Edition, A Brother's Destiny (2013-2015). */
    { "ivory", "Ivory (A Brother's Destiny)",
      2, 1, 1, 1, 1, 2 },
};
const int neras = sizeof eras / sizeof eras[0];

static int lookup(const char *s, const char **tab, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(s, tab[i]))
            return i;
    return -1;
}

int def_by_oid(int oid)
{
    int i;
    for (i = 0; i < ndefs; i++)
        if (defs[i].oid == oid)
            return i;
    return -1;
}

int def_by_name(const char *name)
{
    int i;
    for (i = 0; i < ndefs; i++)
        if (!strcmp(defs[i].name, name))
            return i;
    return -1;
}

static int split(char *s, char **f, int max, char sep)
{
    int n = 0;
    char *nl = strpbrk(s, "\r\n");
    if (nl && sep == '\t')
        *nl = 0;
    while (n < max) {
        f[n++] = s;
        s = strchr(s, sep);
        if (!s)
            break;
        *s++ = 0;
    }
    return n;
}

static int num(const char *s)
{
    if (!strcmp(s, "-"))
        return HR_NONE;
    return atoi(s);
}

/* "battle|bow,gold:2|epers|force:-2,bow;produce|bow|none|produce:2" */
int fx_parse(const char *enc, Def *d, char *err, int errlen)
{
    char buf[1024], *abil[MAX_ABIL + 1];
    int na, i;

    d->nabil = 0;
    if (!enc || !*enc)
        return 0;
    snprintf(buf, sizeof buf, "%s", enc);
    na = split(buf, abil, MAX_ABIL + 1, ';');
    if (na > MAX_ABIL) {
        snprintf(err, errlen, "%s: more than %d abilities", d->name, MAX_ABIL);
        return -1;
    }
    for (i = 0; i < na; i++) {
        Ability *a = &d->abil[d->nabil];
        char *part[4], *c, *e, *save;
        int np, x;
        memset(a, 0, sizeof *a);
        np = split(abil[i], part, 4, '|');
        if (np != 4) {
            snprintf(err, errlen, "%s: ability '%s' needs four parts", d->name, abil[i]);
            return -1;
        }
        if ((x = lookup(part[0], timing_name, TM_COUNT)) < 0) {
            snprintf(err, errlen, "%s: unknown timing '%s'", d->name, part[0]);
            return -1;
        }
        a->timing = (Timing)x;
        for (c = strtok_r(part[1], ",", &save); c; c = strtok_r(NULL, ",", &save)) {
            if (!strcmp(c, "bow"))
                a->cost |= CO_BOW;
            else if (!strcmp(c, "destroy"))
                a->cost |= CO_DESTROY;
            else if (!strncmp(c, "gold:", 5))
                a->gold = atoi(c + 5);
            else if (!strcmp(c, "athome"))
                a->cost |= CO_ATHOME;
            else if (!strncmp(c, "discard:", 8))
                a->discard = atoi(c + 8);
            else if (!strncmp(c, "destroyperf=", 12)) {
                a->cost |= CO_BOWPERF | CO_DESTROYPERF;
                snprintf(a->perfkw, sizeof a->perfkw, "%s", c + 12);
            } else if (!strncmp(c, "bowperf=", 8)) {
                a->cost |= CO_BOWPERF;
                snprintf(a->perfkw, sizeof a->perfkw, "%s", c + 8);
            } else if (!strncmp(c, "perf=", 5))
                snprintf(a->perfkw, sizeof a->perfkw, "%s", c + 5);
            else {
                snprintf(err, errlen, "%s: unknown cost '%s'", d->name, c);
                return -1;
            }
        }
        a->maxforce = a->maxchi = a->minph = a->maxph = -1;
        {
            char *br = strchr(part[2], '['), *cond, *s2;
            if (br) {
                char *end = strchr(br, ']');
                *br++ = 0;
                if (end)
                    *end = 0;
                for (cond = strtok_r(br, ",", &s2); cond; cond = strtok_r(NULL, ",", &s2)) {
                    if (!strcmp(cond, "att")) a->filt |= F_ATT;
                    else if (!strcmp(cond, "def")) a->filt |= F_DEF;
                    else if (!strcmp(cond, "bowed")) a->filt |= F_BOWED;
                    else if (!strcmp(cond, "unbowed")) a->filt |= F_UNBOWED;
                    else if (!strcmp(cond, "opposed")) a->filt |= F_OPPOSED;
                    else if (!strcmp(cond, "nofol")) a->filt |= F_NOFOL;
                    else if (!strcmp(cond, "noatt")) a->filt |= F_NOATT;
                    else if (!strcmp(cond, "lowerforce")) a->filt |= F_LOWERF;
                    else if (!strcmp(cond, "lowerchi")) a->filt |= F_LOWERC;
                    else if (!strcmp(cond, "home")) a->filt |= F_HOME;
                    else if (!strcmp(cond, "lechi")) a->filt |= F_LECHI;
                    else if (!strncmp(cond, "kw=", 3))
                        snprintf(a->tkw, sizeof a->tkw, "%s", cond + 3);
                    else if (!strncmp(cond, "force<=", 7)) a->maxforce = atoi(cond + 7);
                    else if (!strncmp(cond, "chi<=", 5)) a->maxchi = atoi(cond + 5);
                    else if (!strncmp(cond, "ph>=", 4)) a->minph = atoi(cond + 4);
                    else if (!strncmp(cond, "ph<=", 4)) a->maxph = atoi(cond + 4);
                    else {
                        snprintf(err, errlen, "%s: unknown condition '%s'", d->name, cond);
                        return -1;
                    }
                }
            }
        }
        if ((x = lookup(part[2], target_name, TG_COUNT)) < 0) {
            snprintf(err, errlen, "%s: unknown target '%s'", d->name, part[2]);
            return -1;
        }
        a->target = (Target)x;
        for (e = strtok_r(part[3], ",", &save); e; e = strtok_r(NULL, ",", &save)) {
            char *colon = strchr(e, ':');
            if (colon)
                *colon++ = 0;
            if ((x = lookup(e, eff_name, E_COUNT)) < 0) {
                snprintf(err, errlen, "%s: unknown effect '%s'", d->name, e);
                return -1;
            }
            if (a->neff == MAX_EFF) {
                snprintf(err, errlen, "%s: more than %d effects", d->name, MAX_EFF);
                return -1;
            }
            a->eff[a->neff].op = (EffOp)x;
            if (x == E_KW || x == E_ATTACHONLY || x == E_DISCOUNT)
                snprintf(a->arg, sizeof a->arg, "%s", colon ? colon : "");
            else
                a->eff[a->neff].n = colon ? atoi(colon) : 0;
            a->neff++;
        }
        d->nabil++;
    }
    return 0;
}

static CType type_of(const char *s)
{
    int x = lookup(s, type_name, T_COUNT);
    return x < 0 ? T_OTHER : (CType)x;
}

static unsigned kw_bits(const char *s)
{
    unsigned k = 0;
    if (strstr(s, "Unique"))   k |= KW_UNIQUE;
    if (strstr(s, "Shugenja")) k |= KW_SHUGENJA;
    if (strstr(s, "Cavalry"))  k |= KW_CAVALRY;
    if (strstr(s, "Courtier")) k |= KW_COURTIER;
    if (strstr(s, "Monk"))     k |= KW_MONK;
    if (strstr(s, "Samurai"))  k |= KW_SAMURAI;
    return k;
}

static char line[32768];

int data_load(const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "r");
    char hbuf[1024], *hdr[32], *fld[32];
    int nh, lineno = 1;

    if (!f) {
        snprintf(err, errlen, "cannot open %s -- run tools/import-ootv.py fetch "
                 "and tools/build-cards.py first", path);
        return -1;
    }
    if (!fgets(hbuf, sizeof hbuf, f)) {
        snprintf(err, errlen, "%s is empty", path);
        fclose(f);
        return -1;
    }
    nh = split(hbuf, hdr, 32, '\t');
    ndefs = 0;
    while (fgets(line, sizeof line, f)) {
        Def *d;
        int nf, i;
        lineno++;
        if (ndefs == MAX_DEFS) {
            snprintf(err, errlen, "%s: more than %d cards", path, MAX_DEFS);
            fclose(f);
            return -1;
        }
        d = &defs[ndefs];
        memset(d, 0, sizeof *d);
        nf = split(line, fld, 32, '\t');
        for (i = 0; i < nh && i < nf; i++) {
            const char *k = hdr[i], *v = fld[i];
            if (!strcmp(k, "id"))            d->oid = atoi(v);
            else if (!strcmp(k, "name"))     snprintf(d->name, sizeof d->name, "%s", v);
            else if (!strcmp(k, "type"))     d->type = type_of(v);
            else if (!strcmp(k, "deck"))     d->fate = !strcmp(v, "Fate");
            else if (!strcmp(k, "clan"))     snprintf(d->clan, sizeof d->clan, "%s", v);
            else if (!strcmp(k, "keywords")) {
                snprintf(d->kwtext, sizeof d->kwtext, "%s", v);
                d->kw = kw_bits(v);
            }
            else if (!strcmp(k, "cost"))     d->cost = atoi(v);
            else if (!strcmp(k, "force"))    d->force = atoi(v);
            else if (!strcmp(k, "chi"))      d->chi = atoi(v);
            else if (!strcmp(k, "hreq"))     d->hreq = num(v);
            else if (!strcmp(k, "ph"))       d->ph = atoi(v);
            else if (!strcmp(k, "focus"))    d->focus = atoi(v);
            else if (!strcmp(k, "gold"))     d->gold = atoi(v);
            else if (!strcmp(k, "pstr"))     d->pstr = atoi(v);
            else if (!strcmp(k, "shonor"))   d->shonor = atoi(v);
            else if (!strcmp(k, "auto"))     d->understood = !strcmp(v, "all");
            else if (!strcmp(k, "fx")) {
                char e2[256];
                if (fx_parse(v, d, e2, sizeof e2) < 0) {
                    snprintf(err, errlen, "%s:%d: %s", path, lineno, e2);
                    fclose(f);
                    return -1;
                }
            }
        }
        /* The deck column is missing on a few records; the type decides. */
        if (d->type >= T_FOLLOWER && d->type <= T_RING)
            d->fate = 1;
        if (!d->name[0])
            continue;
        ndefs++;
    }
    fclose(f);
    if (!ndefs) {
        snprintf(err, errlen, "%s holds no cards", path);
        return -1;
    }
    return 0;
}

/* fx/ERA.tsv: "name<TAB>encoding", '#' comments.  Each line replaces the
 * card's abilities and marks it understood. */
int fx_overrides(const char *path, char *err, int errlen)
{
    FILE *f = fopen(path, "r");
    int lineno = 0, n = 0;
    if (!f)
        return 0;                /* none yet is fine */
    while (fgets(line, sizeof line, f)) {
        char *fld[3];
        int d;
        lineno++;
        if (line[0] == '#' || line[0] == '\n')
            continue;
        if (split(line, fld, 3, '\t') < 2)
            continue;
        /* By Oracle id: one file serves every era, and two cards that
         * share a name -- there are three Yasuki Jinn-Kuen -- cannot be
         * confused.  A card not in this era's pool is skipped. */
        if (fld[0][0] >= '0' && fld[0][0] <= '9')
            d = def_by_oid(atoi(fld[0]));
        else
            d = def_by_name(fld[0]);
        if (d < 0)
            continue;
        if (fx_parse(fld[1], &defs[d], err, errlen) < 0) {
            fclose(f);
            return -1;
        }
        defs[d].understood = 1;
        n++;
    }
    fclose(f);
    return n;
}
