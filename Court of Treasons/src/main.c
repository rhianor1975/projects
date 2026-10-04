/* Entry point.  Headless from the first commit, because "a game that
 * cannot be played ten thousand times without a screen is a game that
 * cannot be measured".
 *
 *   COURT_SEED=n     replay a game exactly
 *   COURT_TRACE=f    write the log ('-' for stdout)
 *   COURT_CHECK=1    arm the rules invariants
 *   COURT_HOUSES=n   2, 3 or 4 (default 4)
 *   COURT_EPIC=1     4 Ambitions of 6 rather than 3
 */
#include "court.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Clients that have connected but not yet said whether they want a
 * person.  Small: this is a holding area measured in milliseconds, not
 * a queue. */
#define MAX_HOLD    8
#define HOLD_TICK   50     /* how often the quiet ones are aged, ms   */
#define HOLD_QUIET  500    /* silence that means "deal me a machine"  */

static void hold_drop(Chan **hold, int *age, int *n, int i)
{
    int k;
    for (k = i; k + 1 < *n; k++) {
        hold[k] = hold[k + 1];
        age[k] = age[k + 1];
    }
    (*n)--;
}

int main(int argc, char **argv)
{
    static Game g;
    const char *e;
    int i, nh = 4, epic = 0, serve_seat = -1, port = 0;
    const char *join = 0;
    int trusted = 1;
    int duel = 0;
    int solo = 0;
    int want = -1;
    unsigned long seed = (unsigned long)time(NULL);

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts("court [--seed N] [--houses N] [--epic] [--log FILE]\n"
                 "\n"
                 "  COURT_SEED=n     replay a game exactly\n"
                 "  COURT_SEATS=1    add a per-seat end-state line per game\n"
                 "  COURT_TRACE=f    write the log ('-' for stdout)\n"
                 "  COURT_CHECK=1    arm the rules invariants\n"
                 "  COURT_HOUSES=n   2, 3 or 4 (default 4)\n"
                 "  COURT_EPIC=1     4 Ambitions of 6 rather than 3\n"
                 "\n"
                 "  --serve N         hand seat N to the wire\n"
                 "  --port N          listen on N instead of using stdin\n"
                 "  --join HOST:PORT  take a seat on somebody else's game\n"
                 "  --central         do not announce a trusted host\n"
                 "\n"
                 "  tools/soak.sh N FIRST   play a batch and print the\n"
                 "                          five measures the design names");
            return 0;
        }
        if (!strcmp(argv[i], "--seed")   && i + 1 < argc) seed = (unsigned long)atol(argv[++i]);
        else if (!strcmp(argv[i], "--houses") && i + 1 < argc) nh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--epic")) epic = 1;
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) court_open_trace(argv[++i]);
        /* --serve N hands seat N to whatever is on stdin.  The same loop
         * is a central server and a trusted host; here it is a pipe, so
         * the single-player build exercises the protocol and it cannot
         * quietly rot between networked games. */
        else if (!strcmp(argv[i], "--serve") && i + 1 < argc) serve_seat = atoi(argv[++i]);
        /* --port makes the authority listen instead of reading stdin.
         * Central server and trusted host are this same flag; which one
         * it is depends on whose machine runs it, and the client says
         * so. */
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--join") && i + 1 < argc) join = argv[++i];
        /* A central server is a machine the players do not own, so it
         * does not have to announce itself as a trusted host.  It is the
         * same binary; only the claim differs, and the claim is the
         * operator's to make. */
        else if (!strcmp(argv[i], "--central")) trusted = 0;
        else if (!strcmp(argv[i], "--duel")) duel = 1;
        /* One person, on their own machine, against the engine.  It is
         * --duel that never pairs: a client that starts its own server
         * on a random port wants a game, not a lobby, and must not be
         * joined by whoever else finds the port. */
        else if (!strcmp(argv[i], "--solo")) { duel = 1; solo = 1; }
        /* Which House this seat would like.  -1, the default, is
         * whatever the seed deals. */
        else if (!strcmp(argv[i], "--house") && i + 1 < argc)
            want = atoi(argv[++i]);
    }
    if ((e = getenv("COURT_SEED")))   seed = (unsigned long)atol(e);
    if ((e = getenv("COURT_HOUSES"))) nh = atoi(e);
    if (getenv("COURT_EPIC"))         epic = 1;
    if ((e = getenv("COURT_TRACE")))  court_open_trace(e);
    court_set_check(getenv("COURT_CHECK") != NULL);
    if (nh < 2) nh = 2;
    if (nh > MAX_HOUSES) nh = MAX_HOUSES;

    g.want_house = (signed char)want;
    court_seed(&g, seed);
    court_setup(&g, nh, epic);
    /* The Court is impressed only by a promise that cost something,
     * and crowns at +8.  Both settled by soak; see <neutral_court>. */
    g.favour_mode  = (e = getenv("COURT_FAVOUR")) ? atoi(e) : 1;
    g.favour_crown = (e = getenv("COURT_CROWN")) ? atoi(e) : 8;
    g.throne_gold  = (e = getenv("COURT_THRONE_GOLD")) ? atoi(e) : 7;
    g.throne_cap   = (e = getenv("COURT_THRONE_CAP"))  ? atoi(e) : 3;
    g.golden_throne = (e = getenv("COURT_GOLDEN")) ? atoi(e) : 12;
    /* Two votes to be elected.  One made the Paramountcy a
     * consolation prize for holding a single lord more than nobody. */
    g.council_quorum = (e = getenv("COURT_QUORUM")) ? atoi(e) : 2;
    /* A fallen House keeps its claim and may be restored; the Court
     * ends it for good.  design.xml <victory> still says death ends
     * the game and Q10 still asks -- Rhianor settled it 2026-09-20
     * and the XML has not caught up, so this is the flag that says
     * which rule is running. */
    g.fallen_rule = (e = getenv("COURT_FALLEN")) ? atoi(e) : 1;
    if (join) {
        /* A client: it plays nothing itself, it only carries a person's
         * answers to an authority somewhere else. */
        char host[128]; int p = 9017;
        const char *colon = strchr(join, ':');
        snprintf(host, sizeof host, "%.*s",
                 colon ? (int)(colon - join) : (int)strlen(join), join);
        if (colon) p = atoi(colon + 1);
        return court_play_client(host, p);
    }
    if (serve_seat >= 0 && port) {
        /* A server serves more than one game.  It takes a player, plays
         * a game, says who won, and goes back to waiting -- one at a
         * time, which is the honest shape for a duel: there is no lobby
         * and no matchmaking, only the next person to connect.
         *
         * Each game gets a fresh Game and a fresh seed, because a server
         * that reused either would deal the same cards to everyone who
         * ever connected. */
        unsigned long n = 0;
        Chan *pending = 0;          /* one player waiting for another */
        Listener *L = net_listen(port);
        if (!L) return 1;
        /* The trusted-host warning exists for the OTHER player, so that
         * somebody handing their hidden state to a stranger's machine is
         * told.  Playing alone there is no other player, and printing it
         * anyway alarmed the one person it could not concern. */
        if (trusted && !solo)
            fputs("TRUSTED HOST: the other seat's hidden Bonds and "
                  "Ambitions are held on this machine\n", stderr);
        /* A room, unless asked for the old shape.  --duel is the server
         * that was here before: it takes a player, plays one game, and
         * goes back to waiting.  Every test written against that still
         * has it, and a machine that only ever hosts one game does not
         * need a lobby. */
        if (!duel)
            return court_lobby(L, nh, epic, seed);
        {
        Chan *hold[MAX_HOLD];
        int   hold_age[MAX_HOLD];
        int   hold_n = 0;
        for (;;) {
            Chan *c = 0;
            Chan *cs[MAX_HOUSES];
            int child, k, pvp = 0;

            /* Does this one want a person or the machine?
             *
             * A client that wants a person says so on joining.  One that
             * says nothing gets the machine, which is what every client
             * written before this did.
             *
             * Waiting for that half second used to happen HERE, in the
             * accept loop, before the fork.  A client playing the
             * machine sends nothing, so the wait always ran in full and
             * always blocked the door: four clients arriving together
             * finished 0.5s apart, in a staircase, and the fork per
             * game bought nothing because nothing got as far as the
             * fork.  The fourth waited two seconds to be dealt a hand.
             *
             * So the unclassified ones are held in a set and waited on
             * together, the same way the lobby waits on its room.  A
             * client that speaks is classified at once; one that stays
             * quiet for its half second wants the machine.  Nobody
             * waits behind anybody. */
            if (solo) {
                c = net_accept(L);
                if (!c) continue;
            } else {
                int w = net_wait_listen(L, hold, hold_n,
                                        hold_n ? HOLD_TICK : -1);
                if (w == -3) {
                    Chan *nc = net_accept(L);
                    if (!nc) continue;
                    if (hold_n < MAX_HOLD) {
                        hold[hold_n] = nc;
                        hold_age[hold_n] = 0;
                        hold_n++;
                    } else {
                        /* The holding area is full of clients that have
                         * not said anything.  Refusing is better than
                         * forgetting one. */
                        const char *f = "{\"note\":\"the server is busy\"}\n";
                        chan_write(nc, f, strlen(f));
                        chan_close(nc);
                    }
                    continue;
                }
                if (w >= 0) {
                    char line[512];
                    c = hold[w];
                    pvp = chan_readline(c, line, sizeof line)
                       && strstr(line, "\"join\"") && strstr(line, "pvp")
                        ? 1 : 0;
                    hold_drop(hold, hold_age, &hold_n, w);
                } else {
                    int i;
                    for (i = 0; i < hold_n; i++) {
                        hold_age[i] += HOLD_TICK;
                        if (hold_age[i] >= HOLD_QUIET) {
                            c = hold[i];
                            hold_drop(hold, hold_age, &hold_n, i);
                            break;
                        }
                    }
                    if (!c) continue;
                }
            }
            if (pvp) {
                if (!pending) {
                    pending = c;
                    {
                        /* strlen, not a counted literal.  It was 13 for
                         * a 14-byte string, which ate the newline and
                         * glued this message to the next one. */
                        const char *w = "{\"waiting\":1}\n";
                        chan_write(c, w, strlen(w));
                    }
                    fprintf(stderr, "a player is waiting for another\n");
                    continue;
                }
            }
            n++;
            /* Hand the game to a child and go back to waiting, so the
             * next player is not held up by this one.  Where there is no
             * fork, play it here: one at a time is slower to the second
             * player and is not wrong. */
            /* No fork for a solo engine.  Forking gave a listener AND
             * a game process where one client wanted one engine, and
             * killing the listener left the game running -- two strays
             * per launch.  Playing it in this process means there is
             * exactly one thing to end, and it ends itself. */
            child = solo ? 1 : net_spawn();
            if (child == 0) {                              /* the parent */
                chan_close(c);
                if (pvp && pending) { chan_close(pending); pending = 0; }
                continue;
            }
            if (child == 1) net_listener_close(L);         /* the child  */

            memset(&g, 0, sizeof g);
            /* After the memset, not before: zeroing the Game would
             * otherwise ask for House 0 every time. */
            g.want_house = (signed char)want;
            court_seed(&g, seed + n * 2654435761UL);
            court_setup(&g, nh, epic);
            g.favour_mode = (e = getenv("COURT_FAVOUR")) ? atoi(e) : 1;
            g.favour_crown = 8;
            g.council_quorum = 2;
            g.throne_gold = 7;
            g.throne_cap = 3;
            g.golden_throne = 12;
            /* Set after court_setup, which memsets the Game it is
             * handed -- the same trap want_house fell into. */
            g.fallen_rule = (e = getenv("COURT_FALLEN")) ? atoi(e) : 1;
            for (k = 0; k < MAX_HOUSES; k++) cs[k] = 0;
            if (pvp && pending) {
                /* Two people.  The one who waited takes the first seat,
                 * because they have been waiting.
                 *
                 * Only when this client asked for a person too.  Pairing
                 * a waiting player with one who asked for the machine
                 * would give the second a human opponent they did not
                 * ask for, and leave them both surprised. */
                cs[0] = pending; cs[1] = c;
                fprintf(stderr, "game %lu: two players\n", n);
            } else if (serve_seat >= 0 && serve_seat < nh) {
                cs[serve_seat] = c;
            }
            court_serve_n(&g, cs);
            fprintf(stderr, "game %lu: %s wins by %s\n", n,
                    g.winner >= 0 ? HOUSE_NAME[g.h[g.winner].id] : "nobody",
                    g.win_reason ? g.win_reason : "none");
            for (k = 0; k < MAX_HOUSES; k++)
                if (cs[k]) chan_close(cs[k]);
            if (pvp) pending = 0;
            /* One game and out.
             *
             * --solo is an engine a client started for itself, so there
             * is no next player to wait for -- and waiting for one left
             * a listener on the machine every time a client went away
             * without killing it, which is two processes and not one
             * because the game itself is forked.  A server loops; this
             * is not a server. */
            if (solo) return 0;          /* the listener is already shut */
            if (child == 1) return 0;                      /* child done */
        }
        }
    }
    if (serve_seat >= 0) {
        Chan *c = chan_stdio(stdin, stdout);
        if (!c) return 1;
        court_serve(&g, serve_seat, c);
        chan_close(c);
    } else {
        while (!court_round(&g))
            ;
    }

    /* One line per game, which is what the soak reads.  Every field on it
     * is one of the measures the design says the engine must produce. */
    {
        int i2, alive = 0, lp = -1;
        for (i2 = 0; i2 < g.nhouses; i2++)
            if (lp < 0 || g.h[i2].rounds_paramount > g.h[lp].rounds_paramount)
                lp = i2;
        if (g.nhouses == 2
         && g.h[0].rounds_paramount == g.h[1].rounds_paramount) lp = -1;
        for (i2 = 0; i2 < g.nhouses; i2++) {

            alive += g.h[i2].alive;
        }
        /* The reason field is printed with underscores: a soak reads
         * this line with awk, and "The Last House" is three fields. */
        char reason[32];
        int k;
        snprintf(reason, sizeof reason, "%s",
                 g.win_reason ? g.win_reason : "none");
        for (k = 0; reason[k]; k++) if (reason[k] == ' ') reason[k] = '_';
        printf("seed=%lu houses=%d rounds=%d winner=%s by=%s "
               "plays=%d inert=%d promises=%d kept=%d broken=%d "
               "grievance=%.2f pinned=%.3f alive=%d lpheld=%s lpwon=%d "
               "unspent=%.3f\n",
               seed, g.nhouses, g.round,
               g.winner >= 0 ? HOUSE_NAME[g.h[g.winner].id] : "none",
               reason,
               g.total_plays, g.inert_plays,
               g.promises_made, g.promises_kept, g.promises_broken,
               g.grievance_samples
                   ? (double)g.grievance_sum / g.grievance_samples : 0.0,
               g.grievance_samples
                   ? (double)g.grievance_pinned / g.grievance_samples : 0.0,
               alive,
               /* Who led first stopped meaning anything once Accession
                * became an election: the first Council usually has no
                * revealed lords and elects nobody, so the measure read
                * zero in every game.  What still means something is
                * whether holding the Paramountcy more often wins. */
               lp >= 0 ? HOUSE_NAME[g.h[lp].id] : "none",
               lp >= 0 && g.winner == lp,
               g.levy_given ? (double)g.levy_unspent / g.levy_given : 0.0);

        /* Per-seat end state.  The line above says who won; it has never
         * said who was even at the table, which in a duel drawn from four
         * Houses is the axis every question about balance turns on.  A
         * House that loses because it is never dealt in and a House that
         * loses at the table are different faults with different fixes,
         * and three wrong guesses about Vipren were all made without
         * being able to tell them apart. */
        for (i2 = 0; getenv("COURT_SEATS") && i2 < g.nhouses; i2++) {
            extern int r_vassal_count(const Game *, int);
            const House *h = &g.h[i2];
            int r, st = 0, lv = 0, am = 0;
            for (r = 0; r < R_COUNT; r++) { st += h->standing[r]; lv += h->levy[r]; }
            for (r = 0; r < AMBITIONS_DEALT; r++) am += h->amb_done[r] ? 1 : 0;
            printf("seat%d=%s st%d=%d lv%d=%d ld%d=%d am%d=%d fv%d=%d "
                   "pm%d=%d cw%d=%d gv%d=%d al%d=%d tw%d=%d us%d=%d\n",
                   i2, HOUSE_NAME[h->id], i2, st, i2, lv, i2, r_vassal_count(&g, i2),
                   i2, am, i2, h->favour, i2, h->rounds_paramount,
                   i2, h->combats_won, i2, h->grievance, i2, h->alive,
                   i2, h->throneworthy,
                   /* Levy given and never spent, as a percentage.  The
                    * whole-game figure has sat near 47% all along; per
                    * House is how you tell a flooded one from a starved
                    * one, and the two want opposite fixes. */
                   i2, h->levy_given ? (int)(100 * h->levy_unspent
                                             / h->levy_given) : 0);
        }
    }
    return 0;
}
