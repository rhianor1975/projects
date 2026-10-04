/* The authority.
 *
 * The same loop serves a central server and a trusted host.  The
 * difference between those two is who runs it and whether the other
 * player has been told -- not what it does.  It is also the loop a local
 * game uses over a pipe, so the protocol is exercised by the single
 * player build rather than only by the networked one, which is how a
 * protocol stays honest.
 *
 * It reads a line, treats it as an Action, and refuses it if it is not
 * legal.  A client proposes; it never asserts.  Requirement N4 is this
 * function and nothing else.
 */
#include "court.h"
#include "net.h"
#include <stdio.h>
#include <string.h>

/* Every channel in the game, indexed by seat.  A seat played by the
 * machine is a null.
 *
 * The authority holds all of them rather than one because of N6: talk
 * arrives when the sender chooses, and a loop that only read the channel
 * it was waiting on would hold one player's words until their turn came
 * round.  It is also what lets the seat who is not being asked watch the
 * table move, instead of staring at a frozen board until it is their
 * go. */
typedef struct {
    Chan *c[MAX_HOUSES];
    int   n;
} Table;

/* A seat driven from the wire.  The five callbacks write a question and
 * block on the answer; a seat that goes quiet is handled by the caller,
 * not here.
 *
 * One of these per remote seat.  It used to be a single static, which
 * was correct for exactly as long as only one seat could be remote. */
typedef struct {
    Table      *t;
    int         seat;
    const Game *g;
} Remote;

static Chan *mine(Remote *r) { return r->t->c[r->seat]; }

/* Talk is not an action.  It is never validated, never enters the Game,
 * and never reaches court_apply; this function is the whole of what the
 * authority does with it.  The text is passed on, not read.
 *
 * What it does check is that the text cannot break the wire for the
 * other player: a quote becomes an apostrophe and a backslash is
 * dropped, so a client that sends malformed JSON -- by accident or on
 * purpose -- cannot make the authority emit malformed JSON at somebody
 * else.  Escaped quotes are the price, and they are cheap. */
static void relay(Table *t, int from, const char *line)
{
    char text[201];
    size_t k = 0;
    const char *p = strstr(line, "\"say\"");
    Buf out = {0};
    int i;
    /* Whitespace between the colon and the string is legal JSON and a
     * couple of encoders emit it.  A wire that accepts only one spelling
     * of a message is a trap for the next client somebody writes in
     * another language -- this one was found by a test harness whose
     * json.dumps puts a space there. */
    if (!p) return;
    p += 5;
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != ':') return;
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != '"') return;
    for (; *p && *p != '\n' && k < sizeof text - 1; p++) {
        if (*p == '"')  break;
        if (*p == '\\') continue;
        if ((unsigned char)*p < 0x20) continue;
        text[k++] = *p;
    }
    text[k] = 0;
    if (!k) return;
    buf_add(&out, "{\"chat\":1,\"seat\":%d,\"text\":\"%s\"}\n", from, text);
    /* Back to the sender too: a line you cannot see arrive is a line you
     * do not know was sent. */
    for (i = 0; i < t->n; i++)
        if (t->c[i]) chan_write(t->c[i], out.p, out.n);
    buf_free(&out);
}

/* Wait for a line from this seat, carrying anyone's talk across while we
 * wait.  Returns 0 if this seat has gone quiet, which every caller must
 * treat as N5 and not as an error. */
static int hear(Remote *r, char *line, size_t max)
{
    Table *t = r->t;
    for (;;) {
        int i = net_wait(t->c, t->n, -1);
        if (i < 0) return 0;
        if (!chan_readline(t->c[i], line, max)) {
            if (i == r->seat) return 0;        /* ours: N5 */
            /* Somebody else left.  Forget their channel, or select will
             * report it readable for ever and this loop will spin. */
            t->c[i] = 0;
            continue;
        }
        if (strstr(line, "\"say\":")) { relay(t, i, line); continue; }
        if (i == r->seat) return 1;
        /* A line from a seat we did not ask.  It is not an answer to
         * any question, so it is not an action: dropped. */
    }
}

/* Build a message and send it.  The buffer exists so the wire format
 * does not have to know whether it is talking to a pipe or a socket. */
static void say(Remote *r, const View *v, const Action *opts, int n,
                const char *ask, int a, int b)
{
    Buf out = {0};
    int i;
    if (opts) buf_add(&out, "{\"ask\":\"%s\"}\n", ask);
    else      buf_add(&out, "{\"ask\":\"%s\",\"a\":%d,\"b\":%d}\n", ask, a, b);
    wire_write_view(&out, v);
    if (opts) wire_write_actions(&out, opts, n);
    chan_write(mine(r), out.p, out.n);
    buf_free(&out);

    /* The other seats get their own View and no question, so the player
     * whose turn it is not can watch the table move.  Each one is built
     * by court_view for that seat, so this sends nobody anything they
     * were not already entitled to: N1 is not weakened by saying it more
     * often. */
    for (i = 0; i < r->t->n; i++) {
        View o;
        Buf b2 = {0};
        if (i == r->seat || !r->t->c[i]) continue;
        court_view(r->g, i, &o);
        wire_write_view(&b2, &o);
        chan_write(r->t->c[i], b2.p, b2.n);
        buf_free(&b2);
    }
}

static int ask_int(Remote *r, const char *what, const View *v, int a, int b)
{
    char line[4096];
    say(r, v, 0, 0, what, a, b);
    if (!hear(r, line, sizeof line)) return 0;
    {
        long n = 0;
        const char *p = strstr(line, "\"answer\":");
        if (p) sscanf(p + 9, "%ld", &n);
        return (int)n;
    }
}

static Action rm_choose(Seat *s, const View *v, const Action *opts, int n)
{
    Remote *r = s->ctx;
    char line[4096];
    Action a;
    say(r, v, opts, n, "choose", 0, 0);
    if (hear(r, line, sizeof line) && wire_read_action(line, &a)) {
        a.seat = v->me;
        return a;
    }
    /* "A disconnected or timed-out seat is played by the machine from
     * what that seat legitimately knows, and the game does not stall." */
    memset(&a, 0, sizeof a);
    a.kind = A_PASS; a.seat = v->me; a.target = -1; a.card = -1;
    return a;
}
static int rm_commit(Seat *s, const View *v, int max, int def)
{
    int n = ask_int(s->ctx, "commit", v, max, def);
    return n < 0 ? 0 : n > max ? max : n;
}
static int rm_keep(Seat *s, const View *v, int i)
{
    /* N5: "A seat that goes quiet at Reckoning keeps its promises by
     * default, because defaulting to betrayal would make disconnection a
     * strategy."  ask_int returns 0 on a closed pipe, so silence has to
     * mean keep, and the question is phrased so that it does. */
    return !ask_int(s->ctx, "break_promise", v, i, 0);
}
static int rm_accept(Seat *s, const View *v, int who, int term,
                     int cres, int camt)
{
    (void)cres; (void)camt;
    return ask_int(s->ctx, "accept_promise", v, who, term);
}
static int rm_lord(Seat *s, const View *v, int holder, int idx)
{
    return ask_int(s->ctx, "lord_turns", v, holder, idx);
}

void court_seat_remote(Seat *s, void *remote_ctx)
{
    s->ctx            = remote_ctx;
    s->choose         = rm_choose;
    s->commit         = rm_commit;
    s->keep_promise   = rm_keep;
    s->accept_promise = rm_accept;
    s->lord_turns     = rm_lord;
}

/* The authority's own loop.  Every seat with a channel is played by the
 * person on the other end of it; every seat without one is played by the
 * machine.  One channel is a person against the machine, two is a game
 * between two people, and the loop does not know the difference. */
/* The other seat's move, as far as it is entitled to see it.
 *
 * The engine chooses WHAT is public; the client chooses the words,
 * because the deck names and House names already live there.
 *
 * Bond and Instigator are not here on purpose.  Servitude is advanced
 * in private and an Instigator adds hidden Revolution -- naming either
 * would hand over the one thing the other seat is not allowed to know.
 * A Pass is not news. */
static Table *watch_t;

static void tell_action(const Game *g, const Action *a)
{
    Buf out = {0};
    int i;
    if (!watch_t || a->seat < 0) return;
    switch (a->kind) {
    case A_DRAW: case A_PLAY: case A_PROPOSE:
    case A_DECLARE_WAR: case A_CHALLENGE: case A_REVEAL_BOND:
    case A_SPEND_GRIEVANCE: case A_BUY_FAVOUR:
        break;
    default:
        return;
    }
    buf_add(&out, "{\"did\":{\"kind\":%d,\"seat\":%d,\"target\":%d,"
                  "\"card\":%d,\"a\":%d,\"b\":%d,\"c\":%d},\"house\":%d}\n",
            a->kind, a->seat, a->target, a->card, a->a, a->b, a->c,
            g->h[a->seat].id);
    for (i = 0; i < watch_t->n; i++)
        if (i != a->seat && watch_t->c[i])
            chan_write(watch_t->c[i], out.p, out.n);
    buf_free(&out);
}

int court_serve_n(Game *g, Chan **cs)
{
    Remote r[MAX_HOUSES];
    Table  t;
    Buf out = {0};
    int i;
    memset(&t, 0, sizeof t);
    t.n = g->nhouses;
    for (i = 0; i < g->nhouses; i++) t.c[i] = cs[i];
    for (i = 0; i < g->nhouses; i++) {
        if (!cs[i]) continue;
        r[i].t = &t; r[i].seat = i; r[i].g = g;
        court_seat_remote(&g->seat[i], &r[i]);
    }
    watch_t = &t;
    court_watch(tell_action);
    while (!court_round(g))
        ;
    court_watch(0);
    watch_t = 0;
    buf_add(&out, "{\"over\":1,\"winner\":%d,\"reason\":\"%s\"}\n",
            g->winner, g->win_reason ? g->win_reason : "none");
    for (i = 0; i < g->nhouses; i++)
        if (t.c[i]) chan_write(t.c[i], out.p, out.n);
    buf_free(&out);
    return g->winner;
}

/* One seat on the wire: the shape every caller used before there was a
 * game between two people. */
int court_serve(Game *g, int seat, Chan *c)
{
    Chan *cs[MAX_HOUSES];
    int i;
    for (i = 0; i < MAX_HOUSES; i++) cs[i] = 0;
    if (seat >= 0 && seat < g->nhouses) cs[seat] = c;
    return court_serve_n(g, cs);
}

/* The client end: it holds no state and decides nothing.  It carries a
 * person's answers to an authority and shows them what came back.
 *
 * There is deliberately no Game here.  A client that kept one would be a
 * client that could be asked what it thinks the other seat's Revolution
 * counts are, and the answer to that question has to be "I was never
 * told", not "I was told and I am not saying". */
int court_play_client(const char *host, int port)
{
    Chan *c = chan_connect(host, port);
    char line[8192];
    if (!c) return 1;
    fprintf(stderr, "joined %s:%d\n", host, port);
    while (chan_readline(c, line, sizeof line)) {
        fputs(line, stdout);
        fflush(stdout);
        if (strstr(line, "\"over\"")) break;
        if (strstr(line, "\"ask\"")) {
            /* Whatever is driving this -- a UI, a person at a terminal --
             * answers on stdin and it goes straight out. */
            char reply[4096];
            if (!fgets(reply, sizeof reply, stdin)) break;
            if (!chan_write(c, reply, strlen(reply))) break;
        }
    }
    chan_close(c);
    return 0;
}
