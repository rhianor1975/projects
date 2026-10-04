/* The room.
 *
 * A game is two sockets and no shared state, which is why it can be
 * forked and forgotten.  A lobby is shared state by definition -- who is
 * here, what they said, who is looking for a game -- so it cannot be
 * forked at all.  The parent holds every connection and multiplexes
 * them, and the only blocking call in this file waits on all of them at
 * once, the listener included.
 *
 * What it does NOT do is hold hidden state.  When two people agree to
 * play, their channels are handed to a child and this process forgets
 * them; the child is the authority for that game.  Nothing in N1 moves.
 */
#include "court.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_USERS 32
#define NAMELEN   20

typedef struct {
    Chan *c;
    char  name[NAMELEN];
    int   named;              /* has given a name          */
    int   seeking;            /* open to a game            */
} User;

static User    U[MAX_USERS];
static Chan   *CH[MAX_USERS];         /* U[i].c, for net_wait_listen */

/* --- the smallest JSON reader that will do ------------------------- */

/* The value of a string field, unescaped only as far as this protocol
 * needs: quotes and backslashes are dropped rather than decoded, so a
 * client cannot make this process emit something it cannot parse back. */
static int jstr(const char *line, const char *key, char *out, size_t max)
{
    char pat[32];
    const char *p;
    size_t k = 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != '"') return 0;
    for (; *p && *p != '"' && *p != '\n' && k < max - 1; p++) {
        if (*p == '\\') continue;
        if ((unsigned char)*p < 0x20) continue;
        out[k++] = *p;
    }
    out[k] = 0;
    return k > 0;
}

static int jint(const char *line, const char *key, int *out)
{
    char pat[32];
    const char *p;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != ':') return 0;
    while (*p == ' ' || *p == '\t') p++;
    return sscanf(p, "%d", out) == 1;
}

/* --- talking to the room ------------------------------------------ */

/* A write that fails is a person who is no longer there.  It is not
 * acted on here -- to_all() is often mid-loop over everybody -- but it
 * is remembered, and the room loop drops them on the next pass. */
static int wedged[MAX_USERS];

static void to_one(int i, const char *s)
{
    if (!U[i].c) return;
    if (!chan_write(U[i].c, s, strlen(s))) {
        wedged[i] = 1;
    }
}

static void to_all(const char *s)
{
    int i;
    for (i = 0; i < MAX_USERS; i++)
        if (U[i].c && U[i].named) to_one(i, s);
}

/* Everyone who is here, and whether they are looking for a game.  Sent
 * whole on every change rather than as a diff: the list is at most
 * thirty names and a diff is a second thing to get wrong. */
static void send_users(void)
{
    Buf b = {0};
    int i, first = 1;
    buf_add(&b, "{\"users\":[");
    for (i = 0; i < MAX_USERS; i++) {
        if (!U[i].c || !U[i].named) continue;
        buf_add(&b, "%s{\"i\":%d,\"name\":\"%s\",\"seek\":%d}",
                first ? "" : ",", i, U[i].name, U[i].seeking);
        first = 0;
    }
    buf_add(&b, "]}\n");
    to_all(b.p);
    buf_free(&b);
}

static void note(const char *fmt, const char *who)
{
    Buf b = {0};
    buf_add(&b, "{\"note\":\"");
    buf_add(&b, fmt, who);
    buf_add(&b, "\"}\n");
    to_all(b.p);
    buf_free(&b);
}

static void drop(int i, const char *why)
{
    char gone[NAMELEN];
    int  was_named;
    if (!U[i].c) return;
    /* Out of the room first, announced second.  Announcing first sent
     * the departure notice to the person who had just left, which is a
     * write to a socket that is closing. */
    was_named = U[i].named;
    snprintf(gone, sizeof gone, "%s", U[i].name);
    chan_close(U[i].c);
    U[i].c = 0; CH[i] = 0;
    U[i].named = U[i].seeking = 0;
    U[i].name[0] = 0;
    (void)why;
    if (was_named) note("%s has left", gone);
    send_users();
}

/* --- starting a game ----------------------------------------------- */

/* Two people have agreed.  Hand their channels to a child and forget
 * them.  The child closes everything else it inherited -- the listener
 * and every other person's socket -- because a game has no business
 * holding the room's connections open. */
/* b may be -1: one person against the machine.  court_serve_n takes a
 * channel per seat and a null seat is played by the engine's own AI, so
 * a solo game is the same call with one of the two channels missing.
 *
 * It matters because a room with one person in it should still be a game
 * and not a waiting list. */
static void start_game(int a, int b, Listener *L, int nh, int epic,
                       unsigned long seed, unsigned long n, int want)
{
    Chan *cs[MAX_HOUSES];
    Game  g;
    int   k, child;
    const char *e;
    /* Say whether the other seat is a person.  The client draws a talk
     * box only when there is somebody to talk to; against the machine it
     * would be a lie. */
    const char *go   = "{\"start\":1,\"human\":1}\n";
    const char *solo = "{\"start\":1,\"human\":0}\n";

    to_one(a, b >= 0 ? go : solo);
    if (b >= 0) to_one(b, go);

    child = net_spawn();
    if (child == 0) {                                   /* the parent */
        fprintf(stderr, "lobby: %s vs %s\n", U[a].name,
                b >= 0 ? U[b].name : "the machine");
        chan_close(U[a].c); U[a].c = 0; CH[a] = 0;
        U[a].named = U[a].seeking = 0;
        if (b >= 0) {
            chan_close(U[b].c); U[b].c = 0; CH[b] = 0;
            U[b].named = U[b].seeking = 0;
        }
        send_users();
        return;
    }
    if (child == 1) {                                   /* the child  */
        net_listener_close(L);
        for (k = 0; k < MAX_USERS; k++)
            if (k != a && k != b && U[k].c) chan_close(U[k].c);
    }
    memset(&g, 0, sizeof g);
    g.want_house = (signed char)want;
    court_seed(&g, seed + n * 2654435761UL);
    court_setup(&g, nh, epic);
    g.favour_mode   = (e = getenv("COURT_FAVOUR")) ? atoi(e) : 1;
    g.favour_crown  = 8;
    g.council_quorum = 2;
    for (k = 0; k < MAX_HOUSES; k++) cs[k] = 0;
    cs[0] = U[a].c;
    if (b >= 0) cs[1] = U[b].c;          /* null stays null: the AI */
    court_serve_n(&g, cs);
    chan_close(U[a].c);
    if (b >= 0) chan_close(U[b].c);
    if (child == 1) exit(0);
    /* No fork on this platform: the room waited for this game, which is
     * slower for everyone else and is not wrong. */
    U[a].c = 0; CH[a] = 0;
    U[a].named = U[a].seeking = 0;
    if (b >= 0) {
        U[b].c = 0; CH[b] = 0;
        U[b].named = U[b].seeking = 0;
    }
    send_users();
}

/* --- one line from one person -------------------------------------- */

static void heard(int i, const char *line, Listener *L, int nh, int epic,
                  unsigned long seed, unsigned long *games)
{
    char buf[256];
    int  n;

    if (!U[i].named) {
        /* Until somebody has logged in they are not in the room, and
         * nothing else they send means anything. */
        char pass[128];
        int  k, verdict;
        if (!jstr(line, "hello", buf, sizeof buf)) return;
        if (!jstr(line, "pass", pass, sizeof pass)) {
            to_one(i, "{\"denied\":\"a name and a password are needed\"}\n");
            drop(i, "no password");
            return;
        }
        /* The password first, then whether they are already here.
         *
         * The other order told anyone who asked which names were logged
         * in, without their having to know a password for any of them. */
        verdict = court_user_check(buf, pass);
        memset(pass, 0, sizeof pass);
        if (verdict != 1) {
            to_one(i, verdict == 0
                   ? "{\"denied\":\"wrong password\"}\n"
                   : "{\"denied\":\"that name cannot be used\"}\n");
            /* Dropped rather than asked again, so guessing costs a new
             * connection and another PBKDF2 every time. */
            drop(i, "denied");
            return;
        }
        /* One person, one seat.  Taking over a live session from the
         * outside would let anyone with the password push the person
         * holding it off the board mid-game. */
        for (k = 0; k < MAX_USERS; k++)
            if (k != i && U[k].c && U[k].named && !strcmp(U[k].name, buf)) {
                to_one(i, "{\"denied\":\"already in the room\"}\n");
                drop(i, "duplicate");
                return;
            }
        snprintf(U[i].name, sizeof U[i].name, "%s", buf);
        U[i].named = 1;
        {
            Buf b = {0};
            buf_add(&b, "{\"lobby\":1,\"you\":%d,\"name\":\"%s\"}\n",
                    i, U[i].name);
            to_one(i, b.p);
            buf_free(&b);
        }
        note("%s has arrived", U[i].name);
        send_users();
        return;
    }
    if (jstr(line, "say", buf, sizeof buf)) {
        Buf b = {0};
        buf_add(&b, "{\"chat\":1,\"from\":\"%s\",\"text\":\"%s\"}\n",
                U[i].name, buf);
        to_all(b.p);
        buf_free(&b);
        return;
    }
    if (jint(line, "seek", &n)) {
        U[i].seeking = n ? 1 : 0;
        send_users();
        return;
    }
    if (jint(line, "alone", &n)) {
        int want = -1;
        jint(line, "house", &want);
        (*games)++;
        start_game(i, -1, L, nh, epic, seed, *games, want);
        return;
    }
    if (jint(line, "sit", &n)) {
        if (n < 0 || n >= MAX_USERS || n == i || !U[n].c || !U[n].named) {
            to_one(i, "{\"note\":\"they are not here\"}\n");
            return;
        }
        if (!U[n].seeking) {
            to_one(i, "{\"note\":\"they are not looking for a game\"}\n");
            return;
        }
        {
            int want = -1;
            jint(line, "house", &want);
            (*games)++;
            start_game(n, i, L, nh, epic, seed, *games, want);
        }
        return;
    }
}

/* --- the room ------------------------------------------------------ */

int court_lobby(Listener *L, int nh, int epic, unsigned long seed)
{
    unsigned long games = 0;
    int i;
    for (i = 0; i < MAX_USERS; i++) { U[i].c = 0; CH[i] = 0; }
    if (!court_pw_selftest()) {
        fprintf(stderr, "lobby: the password vectors do not pass; "
                        "refusing to accept logins\n");
        return 1;
    }
    fprintf(stderr, "lobby open (passwords are hashed; the login itself "
                    "is NOT encrypted in transit)\n");
    for (;;) {
        char line[4096];
        int w = net_wait_listen(L, CH, MAX_USERS, -1);
        if (w == -3) {
            Chan *c = net_accept(L);
            int slot = -1;
            if (!c) continue;
            for (i = 0; i < MAX_USERS; i++)
                if (!U[i].c) { slot = i; break; }
            if (slot < 0) {
                const char *full = "{\"note\":\"the room is full\"}\n";
                chan_write(c, full, strlen(full));
                chan_close(c);
                continue;
            }
            memset(&U[slot], 0, sizeof U[slot]);
            U[slot].c = c;
            CH[slot] = c;
            continue;
        }
        for (i = 0; i < MAX_USERS; i++)
            if (wedged[i]) { wedged[i] = 0; drop(i, "write failed"); }
        if (w < 0) continue;
        if (!U[w].c) { CH[w] = 0; continue; }
        if (!chan_readline(U[w].c, line, sizeof line)) {
            drop(w, "gone");
            continue;
        }
        heard(w, line, L, nh, epic, seed, &games);
    }
}
