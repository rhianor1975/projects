/* Entry point.  Headless first, as next door: a game that cannot be
 * played a thousand times without a screen cannot be measured.
 *
 *   rokugan --era gold --games 200          machine against machine
 *   rokugan --era gold --serve               seat 0 speaks the wire on
 *                                            stdin/stdout (the client)
 *
 *   ROKUGAN_CHECK=1    arm the rules invariants (INV lines on stderr)
 *   ROKUGAN_TRACE=f    write the game log to f ('-' for stdout)
 *
 * The wire, one line each way:
 *   out: a View (view.c), whenever seat 0 must decide or the game ends
 *   in:  act N | manual OP INST | new ERA CLAN0 CLAN1 SEED | quit
 */
#include "rokugan.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>


static const char *CLANS[] = {
    "Crab", "Crane", "Dragon", "Lion", "Phoenix", "Scorpion", "Unicorn",
    "Mantis", "Spider"
};
#define NCLANS (int)(sizeof CLANS / sizeof CLANS[0])

static char home[512] = ".";
static const Era *cur_era;

static const Era *era_by_key(const char *k)
{
    int i;
    for (i = 0; i < neras; i++)
        if (!strcmp(eras[i].key, k))
            return &eras[i];
    return NULL;
}

static int load_era(const Era *e)
{
    char path[700], err[400];
    int n;
    if (cur_era == e)
        return 0;
    snprintf(path, sizeof path, "%s/data/%s/cards.tsv", home, e->key);
    if (data_load(path, err, sizeof err) < 0) {
        fprintf(stderr, "rokugan: %s\n", err);
        return -1;
    }
    /* fx/cards.tsv by Oracle id serves every era; fx/ERA.tsv, if there
     * is one, has the last word for that era alone. */
    snprintf(path, sizeof path, "%s/fx/cards.tsv", home);
    if ((n = fx_overrides(path, err, sizeof err)) < 0) {
        fprintf(stderr, "rokugan: %s\n", err);
        return -1;
    }
    snprintf(path, sizeof path, "%s/fx/%s.tsv", home, e->key);
    if (fx_overrides(path, err, sizeof err) < 0) {
        fprintf(stderr, "rokugan: %s\n", err);
        return -1;
    }
    cur_era = e;
    return 0;
}

static int load_deck(const Era *e, const char *clan, const char *file, int *out)
{
    char path[700], err[400];
    int n;
    if (file)
        snprintf(path, sizeof path, "%s", file);
    else
        snprintf(path, sizeof path, "%s/decks/%s/%s.txt", home, e->key, clan);
    if ((n = deck_load(path, out, MAX_INST / 2, err, sizeof err)) < 0)
        fprintf(stderr, "rokugan: %s\n", err);
    return n;
}

static int have_deck(const Era *e, const char *clan)
{
    char path[700];
    FILE *f;
    snprintf(path, sizeof path, "%s/decks/%s/%s.txt", home, e->key, clan);
    if (!(f = fopen(path, "r")))
        return 0;
    fclose(f);
    return 1;
}

static const char *random_clan(const Era *e, unsigned long *r)
{
    int tries;
    for (tries = 0; tries < 64; tries++) {
        const char *c;
        *r = *r * 6364136223846793005UL + 1;
        c = CLANS[(*r >> 33) % NCLANS];
        if (have_deck(e, c))
            return c;
    }
    return NULL;
}

static Game game;
static Action acts[MAX_ACTS];

static int start(const Era *e, const char *c0, const char *c1,
                 const char *f0, const char *f1, unsigned long seed)
{
    static int d0[MAX_INST], d1[MAX_INST];
    int n0, n1;
    if (load_era(e) < 0)
        return -1;
    if ((n0 = load_deck(e, c0, f0, d0)) < 0 || (n1 = load_deck(e, c1, f1, d1)) < 0)
        return -1;
    if (l5r_setup(&game, e, d0, n0, d1, n1, seed) < 0) {
        fprintf(stderr, "rokugan: a deck has no Stronghold, or the decks are too big\n");
        return -1;
    }
    return 0;
}

/* Let the machine play every decision that is not the person's. */
static void machine_until(int human)
{
    int guard = 0;
    while (game.phase != PH_OVER && guard++ < 20000) {
        int q = l5r_decider(&game), n;
        if (q == human)
            return;
        n = l5r_legal(&game, acts);
        if (!n)
            return;
        l5r_apply(&game, acts[ai_choose(&game, q, acts, n)]);
    }
}

static int serve(const Era *e, const char *c0, const char *c1, unsigned long seed)
{
    char line[512];
    if (start(e, c0, c1, NULL, NULL, seed) < 0)
        return 1;
    for (;;) {
        int n = 0;
        machine_until(0);
        if (l5r_decider(&game) == 0)
            n = l5r_legal(&game, acts);
        view_write(stdout, &game, 0, acts, n);
        if (!fgets(line, sizeof line, stdin))
            return 0;
        if (!strncmp(line, "quit", 4))
            return 0;
        if (!strncmp(line, "act ", 4)) {
            int k = atoi(line + 4);
            if (k >= 0 && k < n)
                l5r_apply(&game, acts[k]);
        } else if (!strncmp(line, "manual ", 7)) {
            char op[32];
            int inst = -1, m;
            if (sscanf(line + 7, "%31s %d", op, &inst) >= 1)
                for (m = 0; m < M_COUNT; m++)
                    if (!strcmp(op, manual_name[m]))
                        l5r_manual(&game, 0, (Manual)m, inst);
        } else if (!strncmp(line, "new ", 4)) {
            char ek[32], a[32], b[32];
            unsigned long s = 0;
            const Era *ne;
            if (sscanf(line + 4, "%31s %31s %31s %lu", ek, a, b, &s) >= 3
                && (ne = era_by_key(ek)) != NULL) {
                if (!s)
                    s = (unsigned long)time(NULL);
                if (start(ne, a, b, NULL, NULL, s) < 0)
                    return 1;
            }
        }
    }
}

int main(int argc, char **argv)
{
    const Era *e = &eras[0];
    const char *c0 = NULL, *c1 = NULL, *f0 = NULL, *f1 = NULL, *env;
    unsigned long seed = (unsigned long)time(NULL), r;
    int games = 1, i, do_serve = 0, wins[2] = {0, 0}, how[8] = {0}, turns = 0;
    FILE *trace = NULL;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--era") && i + 1 < argc) {
            if (!(e = era_by_key(argv[++i]))) {
                fprintf(stderr, "rokugan: unknown era %s\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(argv[i], "--clan0") && i + 1 < argc) c0 = argv[++i];
        else if (!strcmp(argv[i], "--clan1") && i + 1 < argc) c1 = argv[++i];
        else if (!strcmp(argv[i], "--deck0") && i + 1 < argc) f0 = argv[++i];
        else if (!strcmp(argv[i], "--deck1") && i + 1 < argc) f1 = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = strtoul(argv[++i], 0, 10);
        else if (!strcmp(argv[i], "--games") && i + 1 < argc) games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--home") && i + 1 < argc)
            snprintf(home, sizeof home, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--serve")) do_serve = 1;
        else {
            puts("rokugan [--era gold|celestial|ivory] [--clan0 C] [--clan1 C]\n"
                 "        [--deck0 FILE] [--deck1 FILE] [--seed N] [--games N]\n"
                 "        [--home DIR] [--serve]\n\n"
                 "  ROKUGAN_CHECK=1   arm the rules invariants\n"
                 "  ROKUGAN_TRACE=f   write the game log ('-' for stdout)");
            return 2;
        }
    }
    if ((env = getenv("ROKUGAN_TRACE")))
        trace = strcmp(env, "-") ? fopen(env, "w") : stdout;
    game.trace = trace;
    game.check = getenv("ROKUGAN_CHECK") != NULL;

    r = seed;
    if (do_serve) {
        if (load_era(e) < 0)
            return 1;
        if (!c0) c0 = random_clan(e, &r);
        if (!c1) c1 = random_clan(e, &r);
        if (!c0 || !c1) {
            fprintf(stderr, "rokugan: no decks in %s/decks/%s/\n", home, e->key);
            return 1;
        }
        return serve(e, c0, c1, seed);
    }
    if (load_era(e) < 0)
        return 1;
    for (i = 0; i < games; i++) {
        const char *a = c0, *b = c1;
        if (!f0 && !a) a = random_clan(e, &r);
        if (!f1 && !b) b = random_clan(e, &r);
        if ((!f0 && !a) || (!f1 && !b)) {
            fprintf(stderr, "rokugan: no decks in %s/decks/%s/\n", home, e->key);
            return 1;
        }
        if (start(e, a, b, f0, f1, seed + (unsigned long)i) < 0)
            return 1;
        machine_until(-1);
        if (game.winner >= 0)
            wins[game.winner]++;
        how[game.how]++;
        turns += (game.turn + 1) / 2;
        if (games == 1)
            printf("%s beat %s: %s on turn %d (honor %d to %d, provinces %d to %d)\n",
                   game.winner >= 0 ? player_clan(&game, game.winner) : "nobody",
                   game.winner >= 0 ? player_clan(&game, 1 - game.winner) : "",
                   win_name[game.how], (game.turn + 1) / 2,
                   game.p[0].honor, game.p[1].honor,
                   provinces_left(&game, 0), provinces_left(&game, 1));
    }
    if (games > 1) {
        printf("%d games, %s: seat 0 won %d, seat 1 won %d, %.1f turns a game\n",
               games, e->name, wins[0], wins[1], (double)turns / games);
        for (i = 1; i < 7; i++)
            if (how[i])
                printf("  %-18s %d\n", win_name[i], how[i]);
    }
    if (trace && trace != stdout)
        fclose(trace);
    return 0;
}
