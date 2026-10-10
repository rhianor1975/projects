/* Deck lists: one card per line, "3 Copper Mine" or "Copper Mine",
 * '#' starts a comment.  Names are the Oracle's titles in the era's
 * cards.tsv; a name that is not there stops the load rather than
 * quietly dealing a short deck. */
#include "rokugan.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int deck_load(const char *path, int *out, int max, char *err, int errlen)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int n = 0, lineno = 0;
    if (!f) {
        snprintf(err, errlen, "cannot open deck %s", path);
        return -1;
    }
    while (fgets(line, sizeof line, f)) {
        char *s = line, *e;
        int copies = 1, d;
        lineno++;
        if ((e = strchr(s, '#')))
            *e = 0;
        while (isspace((unsigned char)*s))
            s++;
        e = s + strlen(s);
        while (e > s && isspace((unsigned char)e[-1]))
            *--e = 0;
        if (!*s)
            continue;
        if (isdigit((unsigned char)*s)) {
            copies = (int)strtol(s, &s, 10);
            while (isspace((unsigned char)*s))
                s++;
        }
        if ((d = def_by_name(s)) < 0) {
            snprintf(err, errlen, "%s:%d: no card '%s' in this era", path, lineno, s);
            fclose(f);
            return -1;
        }
        while (copies-- > 0 && n < max)
            out[n++] = d;
    }
    fclose(f);
    return n;
}
