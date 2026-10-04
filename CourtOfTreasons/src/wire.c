/* The wire: a View out, an Action in.
 *
 * One format for all three networking options, because they differ in
 * where the authority runs and who is trusted, not in what is said.  A
 * central server and a trusted host are the same binary at different
 * addresses; commit-reveal is a layer over this, not a replacement.
 *
 * JSON, and line-delimited: one message per line, so a reader needs no
 * framing and a person can watch the traffic go past.  It is not the
 * most compact choice and compactness is not the constraint -- a turn in
 * this game is one message and a game is a few hundred.  What matters is
 * that a client can be written in any language without linking to this
 * one, since the UI may well be Godot, or Java, or a browser.
 *
 * The authority never writes a Game.  It writes a View, per seat, built
 * by court_view -- which is requirement N1 satisfied by construction:
 * there is nothing in a View to leak, so there is no filtering step to
 * get wrong.
 */
#include "court.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------- writing */
static void j_int(Buf *f, const char *k, long v, int *first)
{
    buf_add(f, "%s\"%s\":%ld", *first ? "" : ",", k, v);
    *first = 0;
}
static void j_key(Buf *f, const char *k, int *first)
{
    buf_add(f, "%s\"%s\":", *first ? "" : ",", k);
    *first = 0;
}
static void j_arr3(Buf *f, const signed char *a)
{
    buf_add(f, "[%d,%d,%d]", a[0], a[1], a[2]);
}

void wire_write_view(struct Buf *f, const View *v)
{
    int first = 1, i, r;
    buf_add(f, "{");
    j_int(f, "me", v->me, &first);
    j_int(f, "round", v->round, &first);
    j_int(f, "phase", v->phase, &first);
    j_int(f, "step", v->step, &first);
    j_int(f, "paramount", v->paramount, &first);
    j_int(f, "crown", v->favour_crown, &first);

    j_key(f, "houses", &first);
    buf_add(f, "[");
    for (i = 0; i < v->nhouses; i++) {
        int hf = 1;
        buf_add(f, "%s{", i ? "," : "");
        j_int(f, "house", v->house[i], &hf);
        j_int(f, "alive", v->alive[i], &hf);
        j_int(f, "grievance", v->grievance[i], &hf);
        j_int(f, "favour", v->favour[i], &hf);
        j_int(f, "kept", v->kept_n[i], &hf);
        j_int(f, "broken", v->broken_n[i], &hf);
        j_int(f, "throneworthy", v->throneworthy[i], &hf);
        j_key(f, "standing", &hf); j_arr3(f, v->standing[i]);
        j_key(f, "unrest", &hf);   j_arr3(f, v->unrest[i]);
        buf_add(f, "}");
    }
    buf_add(f, "]");

    /* Only your own Levy is here, because only your own is yours. */
    j_key(f, "levy", &first); j_arr3(f, v->levy);

    j_key(f, "hand", &first);
    buf_add(f, "[");
    for (i = 0; i < v->hand_n; i++) buf_add(f, "%s%d", i ? "," : "", v->hand[i]);
    buf_add(f, "]");

    j_key(f, "ambitions", &first);
    buf_add(f, "[");
    for (i = 0; i < AMBITIONS_DEALT; i++)
        buf_add(f, "%s[%d,%d]", i ? "," : "", v->ambition[i], v->amb_done[i]);
    buf_add(f, "]");

    j_key(f, "lords", &first);
    buf_add(f, "[");
    for (i = 0; i < v->lord_n; i++) {
        int lf = 1;
        if (!v->lord[i].alive) continue;
        buf_add(f, "%s{", i ? "," : "");
        j_int(f, "i", i, &lf);
        j_int(f, "card", v->lord[i].card, &lf);
        j_int(f, "trait", v->lord[i].trait, &lf);
        j_int(f, "holder", v->lord[i].holder, &lf);
        j_int(f, "revealed", v->lord[i].revealed, &lf);
        /* -1 for a count this seat may not see.  It is -1 here because it
         * was -1 in the View; nothing is hidden at this layer. */
        j_int(f, "servitude", v->lord[i].servitude, &lf);
        j_int(f, "revolution", v->lord[i].revolution, &lf);
        buf_add(f, "}");
    }
    buf_add(f, "]");

    j_key(f, "promises", &first);
    buf_add(f, "[");
    for (i = 0, r = 0; i < v->promise_n; i++) {
        const Promise *p = &v->promise[i];
        int pf = 1;
        buf_add(f, "%s{", r++ ? "," : "");
        j_int(f, "i", i, &pf);
        j_int(f, "promiser", p->promiser, &pf);
        j_int(f, "promisee", p->promisee, &pf);
        j_int(f, "term", p->term, &pf);
        j_int(f, "state", p->state, &pf);
        j_int(f, "cons_res", p->cons_res, &pf);
        j_int(f, "cons_amt", p->cons_amt, &pf);
        j_int(f, "due_round", p->due_round, &pf);
        buf_add(f, "}");
    }
    buf_add(f, "]");
    buf_add(f, "}\n");
}

void wire_write_actions(struct Buf *f, const Action *a, int n)
{
    int i;
    buf_add(f, "{\"legal\":[");
    for (i = 0; i < n; i++)
        buf_add(f, "%s{\"kind\":%d,\"seat\":%d,\"target\":%d,\"card\":%d,"
                   "\"a\":%d,\"b\":%d,\"c\":%d}",
                i ? "," : "", a[i].kind, a[i].seat, a[i].target, a[i].card,
                a[i].a, a[i].b, a[i].c);
    buf_add(f, "]}\n");
}

/* ------------------------------------------------------------- reading */
/* A client proposes; it never asserts.  So this reads seven small
 * integers and hands them to court_apply, which decides whether they
 * were a legal thing to say.  Nothing here trusts anything. */
static long field(const char *s, const char *key, long dflt)
{
    char pat[32];
    const char *p;
    long v;
    snprintf(pat, sizeof pat, "\"%s\":", key);
    p = strstr(s, pat);
    if (!p) return dflt;
    p += strlen(pat);
    if (sscanf(p, "%ld", &v) != 1) return dflt;
    return v;
}

int wire_read_action(const char *line, Action *out)
{
    if (!line || !strchr(line, '{')) return 0;
    memset(out, 0, sizeof *out);
    out->kind   = (unsigned char)field(line, "kind", A_PASS);
    out->seat   = (signed char)  field(line, "seat", 0);
    out->target = (signed char)  field(line, "target", -1);
    out->card   = (short)        field(line, "card", -1);
    out->a      = (signed char)  field(line, "a", 0);
    out->b      = (signed char)  field(line, "b", 0);
    out->c      = (signed char)  field(line, "c", 0);
    return 1;
}
