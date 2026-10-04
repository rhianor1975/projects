/* A channel, and the only file in this program that knows what an
 * operating system is.
 *
 * Everything else -- the engine, the wire, the authority loop -- deals
 * in a Chan and never in a socket or a FILE.  That is deliberate: this
 * game is meant to be played by a Mac against a Linux box, so the
 * portable part has to be all of it except one file, and this is the
 * file.  Windows sockets and Berkeley sockets differ in enough small
 * ways that mixing them into the protocol code would put #ifdef through
 * the middle of the thing that has to stay readable.
 */
#ifndef COURT_NET_H
#define COURT_NET_H

#include <stddef.h>
#include <stdio.h>

typedef struct Chan Chan;

/* A channel over a pair of streams: what a local game uses, and what
 * makes the single-player build exercise the protocol. */
Chan *chan_stdio(FILE *in, FILE *out);

/* Wait for one player on a port, or go and find one.  Both return NULL
 * on failure and say why on stderr. */
Chan *chan_listen(int port);
Chan *chan_connect(const char *host, int port);

/* A listener that outlives the games played on it, so a server can take
 * the next player while the last one is still playing. */
typedef struct Listener Listener;
Listener *net_listen(int port);
Chan     *net_accept(Listener *l);
void      net_listener_close(Listener *l);

/* Hand this game to a child process and go back to waiting.  Returns 1
 * in the child, 0 in the parent, and -1 where the platform has no way to
 * do it -- on which the caller plays the game itself, one at a time,
 * which is what this program did before and is not wrong, only slower to
 * the second player. */
int  net_spawn(void);

/* Which of these channels has something to say?  Returns the index of
 * the first one with a line ready, -1 if none within ms, -2 on error.
 *
 * The authority needs this because talk arrives when the sender chooses
 * and not when they are asked (N6).  A loop that only ever read the
 * channel it was waiting on would hold the other player's words in a
 * socket buffer until their turn came round.
 *
 * It looks at what is already buffered before it looks at the sockets:
 * chan_readline reads in blocks, so a channel can hold a whole unread
 * line while select says it is idle.  Waiting on select first would
 * sleep on a message already in memory. */
int  net_wait(Chan **cs, int n, int ms);

/* The same, with the listener in the set.  Returns -3 when somebody new
 * is knocking, otherwise exactly what net_wait returns.
 *
 * A lobby cannot block on accept: it has to hear the people already in
 * the room while it waits for the next one. */
int  net_wait_listen(Listener *l, Chan **cs, int n, int ms);

/* One line in, one buffer out.  Returns 0 at end of stream, which every
 * caller must treat as "this seat has gone quiet" rather than as an
 * error -- see N5. */
int  chan_readline(Chan *c, char *buf, size_t max);
int  chan_write(Chan *c, const char *s, size_t n);
void chan_close(Chan *c);

/* A growable buffer, so the wire format can be built without knowing
 * whether it is going to a file or a socket. */
/* net.h owns this type.  court.h refers to it as `struct Buf` rather
 * than typedefing it again: repeating a typedef is a C11 feature, and
 * this is C99, so the older compiler on the server rejected it while
 * the newer one here did not.  A portability bug found by the machine
 * it was being deployed to, which is the right machine to find it. */
typedef struct Buf { char *p; size_t n, cap; } Buf;
void buf_add(Buf *b, const char *fmt, ...);
void buf_free(Buf *b);

#endif
