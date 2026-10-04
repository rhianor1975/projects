/* The one file that knows about operating systems.  See net.h. */
#include "net.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef SOCKET sock_t;
#  define BAD_SOCK INVALID_SOCKET
#  define CLOSESOCK closesocket
#else
#  include <sys/types.h>
#  include <sys/select.h>
#  include <sys/time.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <signal.h>
   typedef int sock_t;
#  define BAD_SOCK (-1)
#  define CLOSESOCK close
#endif

struct Chan {
    FILE *in, *out;          /* a local channel                         */
    sock_t s;                /* or a socket                            */
    char   rx[8192];         /* a line may arrive in pieces            */
    size_t rn;
};

static int net_start(void)
{
#ifndef _WIN32
    /* Writing to a socket the other end has closed raises SIGPIPE, and
     * the default action for SIGPIPE is to kill the process.
     *
     * Fork-per-game hid this: the write happened in a child that was
     * about to exit anyway.  A lobby is one long-lived process holding
     * everybody's connection, so one person closing their client while
     * the server was mid-sentence took the whole room down with them --
     * exit 141, which is 128 plus 13.  Ignored here, so a dead socket is
     * a failed write that the caller can see instead. */
    static int piped;
    if (!piped) { signal(SIGPIPE, SIG_IGN); piped = 1; }
#endif
#ifdef _WIN32
    static int done;
    WSADATA w;
    if (!done && WSAStartup(MAKEWORD(2, 2), &w) != 0) return 0;
    done = 1;
#endif
    return 1;
}

Chan *chan_stdio(FILE *in, FILE *out)
{
    Chan *c = calloc(1, sizeof *c);
    if (!c) return 0;
    c->in = in; c->out = out; c->s = BAD_SOCK;
    return c;
}

static Chan *chan_sock(sock_t s)
{
    Chan *c = calloc(1, sizeof *c);
    int one = 1;
    if (!c) { CLOSESOCK(s); return 0; }
    c->s = s;
    /* A turn is one small message and the reply matters immediately, so
     * Nagle's algorithm has nothing to coalesce and everything to
     * delay. */
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    return c;
}

struct Listener { sock_t l; int port; };

Listener *net_listen(int port)
{
    struct sockaddr_in a;
    Listener *L;
    sock_t l;
    int one = 1;
    if (!net_start()) return 0;
    l = socket(AF_INET, SOCK_STREAM, 0);
    if (l == BAD_SOCK) { fprintf(stderr, "net: cannot make a socket\n"); return 0; }
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((unsigned short)port);
    /* A backlog, so a second player waits in the queue rather than being
     * refused while the first is being dealt with. */
    if (bind(l, (struct sockaddr *)&a, sizeof a) != 0 || listen(l, 8) != 0) {
        fprintf(stderr, "net: cannot listen on %d\n", port);
        CLOSESOCK(l); return 0;
    }
    L = calloc(1, sizeof *L);
    if (!L) { CLOSESOCK(l); return 0; }
    L->l = l; L->port = port;
    fprintf(stderr, "listening on port %d\n", port);
    return L;
}

Chan *net_accept(Listener *L)
{
    sock_t s;
    if (!L) return 0;
    s = accept(L->l, 0, 0);
    if (s == BAD_SOCK) return 0;
    return chan_sock(s);
}

void net_listener_close(Listener *L)
{
    if (!L) return;
    CLOSESOCK(L->l);
    free(L);
}

/* Fork, where there is one.  Windows has no fork and this program has no
 * business pretending otherwise: it says so and the caller serves one
 * game at a time, which is what it did before. */
int net_spawn(void)
{
#ifdef _WIN32
    return -1;
#else
    pid_t pid;
    static int reaping;
    if (!reaping) { signal(SIGCHLD, SIG_IGN); reaping = 1; }
    pid = fork();
    if (pid < 0) return -1;
    return pid == 0 ? 1 : 0;
#endif
}

Chan *chan_listen(int port)
{
    struct sockaddr_in a;
    sock_t l, s;
    int one = 1;
    if (!net_start()) return 0;
    l = socket(AF_INET, SOCK_STREAM, 0);
    if (l == BAD_SOCK) { fprintf(stderr, "net: cannot make a socket\n"); return 0; }
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons((unsigned short)port);
    if (bind(l, (struct sockaddr *)&a, sizeof a) != 0
     || listen(l, 1) != 0) {
        fprintf(stderr, "net: cannot listen on %d\n", port);
        CLOSESOCK(l); return 0;
    }
    fprintf(stderr, "waiting for a player on port %d\n", port);
    s = accept(l, 0, 0);
    CLOSESOCK(l);
    if (s == BAD_SOCK) { fprintf(stderr, "net: nobody came\n"); return 0; }
    fprintf(stderr, "a player has joined\n");
    return chan_sock(s);
}

Chan *chan_connect(const char *host, int port)
{
    struct addrinfo hints, *res = 0;
    char portstr[16];
    sock_t s;
    if (!net_start()) return 0;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof portstr, "%d", port);
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) {
        fprintf(stderr, "net: cannot find %s\n", host); return 0;
    }
    s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == BAD_SOCK || connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        fprintf(stderr, "net: cannot reach %s:%d\n", host, port);
        if (s != BAD_SOCK) CLOSESOCK(s);
        freeaddrinfo(res); return 0;
    }
    freeaddrinfo(res);
    return chan_sock(s);
}

int net_wait(Chan **cs, int n, int ms)
{
    fd_set rd;
    struct timeval tv;
    sock_t hi = 0;
    int i, r;
    /* Anything already in hand, before anything on the wire. */
    for (i = 0; i < n; i++) {
        if (!cs[i]) continue;
        if (cs[i]->in) return i;             /* a pipe: fgets will block */
        if (memchr(cs[i]->rx, '\n', cs[i]->rn)) return i;
    }
    FD_ZERO(&rd);
    for (i = 0; i < n; i++) {
        if (!cs[i] || cs[i]->s == BAD_SOCK) continue;
        FD_SET(cs[i]->s, &rd);
        if (cs[i]->s > hi) hi = cs[i]->s;
    }
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    r = select((int)hi + 1, &rd, 0, 0, ms < 0 ? 0 : &tv);
    if (r < 0)  return -2;
    if (r == 0) return -1;
    for (i = 0; i < n; i++)
        if (cs[i] && cs[i]->s != BAD_SOCK && FD_ISSET(cs[i]->s, &rd))
            return i;
    return -1;
}

int net_wait_listen(Listener *L, Chan **cs, int n, int ms)
{
    fd_set rd;
    struct timeval tv;
    sock_t hi = 0;
    int i, r;
    for (i = 0; i < n; i++) {
        if (!cs[i]) continue;
        if (cs[i]->in) return i;
        if (memchr(cs[i]->rx, '\n', cs[i]->rn)) return i;
    }
    FD_ZERO(&rd);
    for (i = 0; i < n; i++) {
        if (!cs[i] || cs[i]->s == BAD_SOCK) continue;
        FD_SET(cs[i]->s, &rd);
        if (cs[i]->s > hi) hi = cs[i]->s;
    }
    if (L && L->l != BAD_SOCK) {
        FD_SET(L->l, &rd);
        if (L->l > hi) hi = L->l;
    }
    tv.tv_sec  = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    r = select((int)hi + 1, &rd, 0, 0, ms < 0 ? 0 : &tv);
    if (r < 0)  return -2;
    if (r == 0) return -1;
    for (i = 0; i < n; i++)
        if (cs[i] && cs[i]->s != BAD_SOCK && FD_ISSET(cs[i]->s, &rd))
            return i;
    if (L && L->l != BAD_SOCK && FD_ISSET(L->l, &rd))
        return -3;                      /* somebody new is knocking */
    return -1;
}

int chan_readline(Chan *c, char *buf, size_t max)
{
    if (!c) return 0;
    if (c->in) return fgets(buf, (int)max, c->in) ? 1 : 0;
    for (;;) {
        char *nl = memchr(c->rx, '\n', c->rn);
        if (nl) {
            size_t len = (size_t)(nl - c->rx) + 1;
            size_t take = len < max - 1 ? len : max - 1;
            memcpy(buf, c->rx, take);
            buf[take] = 0;
            memmove(c->rx, c->rx + len, c->rn - len);
            c->rn -= len;
            return 1;
        }
        if (c->rn >= sizeof c->rx - 1) return 0;       /* a line too long */
        {
            int n = (int)recv(c->s, c->rx + c->rn,
                              (int)(sizeof c->rx - c->rn), 0);
            if (n <= 0) return 0;                      /* gone quiet      */
            c->rn += (size_t)n;
        }
    }
}

int chan_write(Chan *c, const char *s, size_t n)
{
    if (!c) return 0;
    if (c->out) { fwrite(s, 1, n, c->out); fflush(c->out); return 1; }
    while (n) {
        int w = (int)send(c->s, s, (int)n, 0);
        if (w <= 0) return 0;
        s += w; n -= (size_t)w;
    }
    return 1;
}

void chan_close(Chan *c)
{
    if (!c) return;
    if (c->s != BAD_SOCK) CLOSESOCK(c->s);
    free(c);
}

/* ------------------------------------------------------------- buffers */
void buf_add(Buf *b, const char *fmt, ...)
{
    va_list ap;
    int need;
    for (;;) {
        size_t room = b->cap - b->n;
        va_start(ap, fmt);
        need = vsnprintf(b->p + b->n, room, fmt, ap);
        va_end(ap);
        if (need >= 0 && (size_t)need < room) { b->n += (size_t)need; return; }
        {
            size_t cap = b->cap ? b->cap * 2 : 1024;
            char *q;
            while (cap < b->n + (size_t)need + 2) cap *= 2;
            q = realloc(b->p, cap);
            if (!q) return;
            b->p = q; b->cap = cap;
        }
    }
}

void buf_free(Buf *b) { free(b->p); b->p = 0; b->n = b->cap = 0; }
