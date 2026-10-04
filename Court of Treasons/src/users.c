/* Who is allowed in.
 *
 * A line per person: name, salt, rounds, hash.  The password is never
 * stored and never written anywhere -- what is kept is PBKDF2 over it,
 * with a salt that is unique per person so that two people who choose
 * the same password do not get the same line, and a round count high
 * enough that guessing the file is slow.
 *
 * A name that is not in the file is created on first use with the
 * password given.  There is no separate registration step, which suits a
 * game played by people who know each other: the first person to use a
 * name owns it, and after that the password is what proves it.  A typo
 * in your name makes a new person rather than an error, and the room
 * shows you the name you actually got.
 *
 * What this does NOT do is protect the password in transit.  There is no
 * TLS here; the login crosses the network in the clear and anyone on the
 * wire can read it.  That is a real limitation, it is stated in the
 * interface, and it is the reason this is a game for a network you
 * control.
 */
#include "court.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ROUNDS 120000UL
#define SALTN  16

static const char *store(void)
{
    const char *e = getenv("COURT_USERS");
    return e ? e : "users.tsv";
}

static void hex(const unsigned char *b, size_t n, char *out)
{
    size_t i;
    for (i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
    out[n * 2] = 0;
}

static int unhex(const char *s, unsigned char *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + i * 2, "%2x", &v) != 1) return 0;
        out[i] = (unsigned char)v;
    }
    return 1;
}

/* Unique, and unpredictable where the platform will say.  A salt has to
 * be different per person; it does not have to be secret, so a fallback
 * that mixes the clock and the name is a weaker salt and not a broken
 * one. */
static void make_salt(const char *name, unsigned char out[SALTN])
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t got = fread(out, 1, SALTN, f);
        fclose(f);
        if (got == SALTN) return;
    }
    {
        static unsigned long seq;
        unsigned char h[32];
        char mix[128];
        snprintf(mix, sizeof mix, "%s|%lu|%lu", name,
                 (unsigned long)time(0), ++seq);
        court_sha256(mix, strlen(mix), h);
        memcpy(out, h, SALTN);
    }
}

/* Equal, in time that does not depend on where they differ. */
static int same32(const unsigned char *a, const unsigned char *b)
{
    unsigned char d = 0;
    int i;
    for (i = 0; i < 32; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

static int valid_name(const char *n)
{
    size_t i, len = strlen(n);
    if (len < 1 || len > 18) return 0;
    for (i = 0; i < len; i++) {
        char c = n[i];
        /* No tab or newline: they are the file's structure.  No quote or
         * backslash: they are the wire's. */
        if (c == '\t' || c == '\n' || c == '"' || c == '\\') return 0;
        if ((unsigned char)c < 0x20) return 0;
    }
    return 1;
}

/* 1 the password is right, 0 it is wrong, -1 the name is unusable. */
int court_user_check(const char *name, const char *pass)
{
    char line[512];
    FILE *f;
    int found = 0, ok = 0;

    if (!valid_name(name) || !pass || !*pass) return -1;
    f = fopen(store(), "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char *n = line, *s, *r, *h, *nl;
            if ((nl = strchr(line, '\n'))) *nl = 0;
            s = strchr(n, '\t'); if (!s) continue; *s++ = 0;
            r = strchr(s, '\t'); if (!r) continue; *r++ = 0;
            h = strchr(r, '\t'); if (!h) continue; *h++ = 0;
            if (strcmp(n, name)) continue;
            found = 1;
            {
                unsigned char salt[SALTN], want[32], got[32];
                if (!unhex(s, salt, SALTN) || !unhex(h, want, 32)) break;
                court_pbkdf2(pass, salt, SALTN, strtoul(r, 0, 10), got);
                ok = same32(want, got);
            }
            break;
        }
        fclose(f);
    }
    if (found) return ok;

    /* New name: it is theirs now. */
    {
        unsigned char salt[SALTN], h[32];
        char shex[SALTN * 2 + 1], hhex[65];
        make_salt(name, salt);
        court_pbkdf2(pass, salt, SALTN, ROUNDS, h);
        hex(salt, SALTN, shex);
        hex(h, 32, hhex);
        f = fopen(store(), "a");
        if (!f) return -1;
        fprintf(f, "%s\t%s\t%lu\t%s\n", name, shex, ROUNDS, hhex);
        fclose(f);
        return 1;
    }
}
