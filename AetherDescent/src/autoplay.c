#include "common.h"
#include "autoplay.h"
#include "path.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* This file supplies keystrokes, so it must never route getch() back through
   the hook that calls it. common.h has already undefined ncurses' own macro
   to install its hook, so reach the real terminal read directly. */
#undef getch
#undef getnstr
#if !defined(TUI_NOTCURSES)
/* ncurses ships these as macros over the stdscr window. */
#define getch()        wgetch(stdscr)
#define getnstr(s, n)  wgetnstr(stdscr, (s), (n))
#endif
/* Under the notcurses shim they are real functions declared in tui.h, so the
   undefs above are all that is needed to reach them. */

static FILE *g_log        = NULL;
static bool  g_active     = false;
static long  g_keys       = 0;
static long  g_max_keys   = 0;
static unsigned int g_rng = 1u;

/* Own RNG, kept separate from the game's rand(). Sharing rand() would mean
   the policy's choices consumed draws from the same stream the dungeon is
   generated with, so replaying a seed would produce a different dungeon
   depending on how many keys were pressed -- which makes a recorded run
   impossible to reproduce, and reproducing them is the entire point. */
static unsigned int nextr(void) {
    g_rng = g_rng * 1103515245u + 12345u;
    return (g_rng >> 16) & 0x7fffu;
}

/* The key pool.
 *
   Weighted, not uniform. A uniform draw over the whole keyboard spends
   almost all of its time on keys that do nothing, and the run never gets
   deep enough to reach the code worth breaking. The weights below buy depth:
   movement and confirmation dominate so the game actually progresses, the
   screen-opening keys appear often enough to be exercised, and escape is
   common so no submenu can hold the run hostage.

   'q' is deliberately rare rather than absent. It can end a run early, which
   is a real thing a player does, and the quit path has its own save prompt
   that deserves to be walked into occasionally. */
typedef struct { int key; int weight; const char *what; } KeyWeight;

static const KeyWeight KEYS[] = {
    /* movement -- the bulk of any real session */
    { KEY_UP,    60, "up"    }, { KEY_DOWN,  60, "down"  },
    { KEY_LEFT,  60, "left"  }, { KEY_RIGHT, 60, "right" },
    { 'k',       20, "up"    }, { 'j',       20, "down"  },
    { 'h',       20, "left"  }, { 'l',       20, "right" },

    /* confirmation and refusal: menus are mostly these two */
    { '\n',      70, "enter" }, { 27,        50, "esc"   },
    { 'y',       15, "yes"   }, { 'n',       15, "no"    },

    /* The screens and actions. These are what the simulator can never reach.
     *
       Every key here is one main.c actually binds, which four of them were
       not. 'o' was labelled "auto-explore" at the heaviest weight in the
       whole table -- a quarter of every screen keypress -- and auto-explore
       is 'x'; 'z' was "cast" and the spell menu is 'm'; '>' and '<' were
       "descend" and "ascend" and there are no such keys, because stairs are
       taken by walking onto them. Fifty-three points of weight, the single
       largest share of the table, went on keys the game discards. A fuzzer
       whose most-pressed key does nothing is not fuzzing what it says it is,
       and nothing in the log said so, because a key the game ignores looks
       exactly like a key the game handled quietly. Checked against main.c's
       STATE_TOWN and STATE_TEMPLE handlers, key by key. */
    { 'i',       12, "inventory" }, { 'c',  8, "character" },
    { 'g',       10, "work"      }, { 'f',  10, "fire"     },
    { 'm',        6, "spells"    }, { 'r',  8, "recall"    },
    { 'a',        8, "ability"   }, { 'x', 25, "auto-explore" },
    { 'M',       10, "minimap"   }, { '?',  4, "help"      },
    /* Take control. Added the day the feature landed, because a path the
       fuzzer cannot reach is a path nothing tests: Tab was not in this pool,
       so three hundred lines of role-switching, hero movement and the
       hand-the-torch-on death path were invisible to every silent run. */
    { '\t',      10, "take control" },
    { '.',       12, "wait"      }, { 's',  6, "last spell" },

    /* menu selection by number -- shops, spell lists, quantity pickers */
    { '1', 8, "1" }, { '2', 8, "2" }, { '3', 8, "3" },
    { '4', 6, "4" }, { '5', 6, "5" }, { '6', 4, "6" },
    { '7', 4, "7" }, { '8', 4, "8" }, { '9', 4, "9" },
    { '0', 4, "0" },

    /* 'q' is deliberately absent.
     *
       It was in this pool at weight 2, which sounds harmless and is not: over
       enough keys a quit becomes near-certain, and every run ended at roughly
       twenty-six thousand keys no matter what --keys said. The budget was not
       being tested, the expected time for a random walk to press 'q' was. The
       quit-and-save path still gets exercised -- the budget wind-down below
       presses it deliberately, once, at the end, where it belongs. */
};
#define KEY_COUNT ((int)(sizeof(KEYS) / sizeof(KEYS[0])))

static int g_weight_total = 0;

/* main() is not the only way out, so the footer is written from atexit
   rather than from any one exit path. */
static void autoplay_end_atexit(void) { autoplay_end("process exit"); }

bool autoplay_begin(const char *log_path, unsigned int seed, long max_keys) {
    g_log = fopen(log_path, "w");
    if (!g_log) return false;

    g_active   = true;
    g_keys     = 0;
    g_max_keys = max_keys > 0 ? max_keys : 200000;
    g_rng      = seed ? seed : 1u;

    atexit(autoplay_end_atexit);   /* the game exits through several paths */
    g_weight_total = 0;
    for (int i = 0; i < KEY_COUNT; i++) g_weight_total += KEYS[i].weight;

    fprintf(g_log, "# aether-descent silent run\n");
    fprintf(g_log, "# seed %u, key budget %ld\n", seed, g_max_keys);
    fprintf(g_log, "# policy: deliberate navigation on the map (lines marked 'nav'),\n");
    fprintf(g_log, "#         weighted-random keys with an escape bias everywhere else.\n");
    fprintf(g_log, "# columns: <key-number> <key-name> | <game messages since last key>\n");
    fprintf(g_log, "# a 'nav' line says *why* the step was taken -- 'stairs' and\n");
    fprintf(g_log, "# 'explore (stairs not found)' are opposite diagnoses of the same\n");
    fprintf(g_log, "# outcome, so a run that never descends is readable from this alone.\n");
    fflush(g_log);
    return true;
}

bool autoplay_active(void)     { return g_active; }
long autoplay_keys_served(void) { return g_keys; }

/* ---- navigation --------------------------------------------------------
 *
 * Everything below here exists because the fuzzer could not find a door. See
 * the header for the measurement; the short version is that a random walk in
 * a 140x80 town never steps on the one tile that opens the temple, so sixty
 * thousand keys produced a run whose deepest floor was 1 and whose every
 * state snapshot read turn 0.
 */

static AutoScene    g_scene    = AP_SCENE_MENU;
static const Player *g_scene_p = NULL;
static const Map    *g_scene_m = NULL;

void autoplay_scene(AutoScene scene, const Player *p, const Map *m) {
    if (!g_active) return;
    g_scene = scene;
    g_scene_p = p;
    g_scene_m = m;
}

/* The town tour.
 *
 * Not just "walk to the temple door". Every service in the plaza is a screen
 * the balance harness structurally cannot reach, which is the entire reason
 * this mode exists -- so the policy visits each of them in turn and only then
 * heads for the door. Walking onto the tile is what opens the screen, and the
 * screen is where the scene is already back to AP_SCENE_MENU, so each shop
 * gets the fuzzer turned loose inside it exactly as before.
 *
 * The temple entrance is last on purpose: the tour is what a visit is *for*,
 * and descending ends it. */
static const TileType TOWN_TOUR[] = {
    TILE_JUNKYARD, TILE_BLACKMARKET, TILE_BANK, TILE_GLADIATOR,
    TILE_TAVERN, TILE_SHOP_GENERAL, TILE_SHOP_ARMORY, TILE_SHOP_APOTHECARY,
    TILE_SHOP_ARCANIST, TILE_QUEST_BOARD, TILE_RACES, TILE_KITCHEN,
    TILE_ORACLE, TILE_ALTAR, TILE_BAZAAR,
    TILE_INN, TILE_TEMPLE_ENTRANCE,
};
/* A door missing from this list is a screen self-play never opens, and the
   three newest doors were missing from it -- so the three least-exercised
   screens in the game were also the only three the fuzzer could not reach.
   tests/invariants.c now asserts the list against the town map itself, which
   is the only way this stays true as doors are added. */
#define TOWN_TOUR_COUNT ((int)(sizeof(TOWN_TOUR) / sizeof(TOWN_TOUR[0])))

bool autoplay_tours_tile(int t) {
    for (int i = 0; i < TOWN_TOUR_COUNT; i++)
        if ((int)TOWN_TOUR[i] == t) return true;
    return false;
}

static int  g_tour = 0;
static long g_goal_steps = 0;      /* keys spent chasing the current goal */
static int  g_last_floor = -1;
/* Auto-explore is capped by whether it is *achieving* anything, not by a
   count. A flat cap of three per floor looked safe and was not: auto-explore
   does the work of hundreds of hand-walked frontier steps, so three of them
   and then a permanent fallback meant a soak run spent 189,243 keys walking
   the frontier by hand against about 339 invocations of the thing that
   actually crosses floors. The turn counter is the test -- an auto-explore
   that gave up immediately leaves it where it was, and one that walked half
   the floor does not. */
static int  g_autoexplore_tries = 0;
static int  g_turns_at_autoexplore = -1;
#define AUTOEXPLORE_TRIES 3

/* What the last navigation key was for, written into the log beside it.
   "temple" covers two completely different situations -- walking to a
   staircase you have found, and walking to the edge of the fog because you
   have not -- and a run that never descends means opposite things in each.
   The first says the descent is blocked; the second says the floor is bigger
   than the run's life expectancy. Same key, same log line, until now. */
static const char *g_nav_why = "nav";

/* Generous: a goal on the far corner of the biggest map, walked badly, with
   one key in five going to the fuzzer instead. Past this the goal is assumed
   unreachable (a shop behind a monster, a staircase behind a locked door) and
   the policy moves on rather than pressing into a wall for the rest of the
   run -- which is the failure the old policy had, in slow motion. */
#define GOAL_PATIENCE ((long)(MAP_W + MAP_H) * 6)

static bool find_tile(const Map *m, TileType want, int *out_x, int *out_y) {
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (m->tiles[y][x].type == want) { *out_x = x; *out_y = y; return true; }
    return false;
}

/* The searches step eight ways (path.h's PATH_DX/PATH_DY), and so does the
   game -- but only the four orthogonals have arrows, so the diagonals have to
   go out as y/u/b/n.
 *
   This was written to send the horizontal half of a diagonal and let the next
   key sort out the rest, on the reasoning that the path is recomputed every
   key anyway. It is not equivalent: a diagonal step through a corner has both
   of its orthogonal halves in the wall, main.c refuses an unwalkable move
   without spending a turn, and the policy pressed the same refused key
   forever. Measured as a run pinned at (144,72) on floor 1, advancing forty
   turns across five and a half thousand keys.

   Arrows for the four, per the project owner's laptop keyboard; vi keys only
   where there is no arrow to use. */
static int key_for_step(int dx, int dy) {
    if (dx < 0 && dy < 0) return 'y';
    if (dx > 0 && dy < 0) return 'u';
    if (dx < 0 && dy > 0) return 'b';
    if (dx > 0 && dy > 0) return 'n';
    if (dx < 0) return KEY_LEFT;
    if (dx > 0) return KEY_RIGHT;
    if (dy < 0) return KEY_UP;
    if (dy > 0) return KEY_DOWN;
    return 0;
}

/* One step of the town tour, or 0 if there is nothing sensible to press. */
static int town_step(void) {
    const Player *p = g_scene_p;
    const Map *m = g_scene_m;
    if (!p || !m) return 0;

    for (int tries = 0; tries < TOWN_TOUR_COUNT; tries++) {
        int gx, gy;
        if (!find_tile(m, TOWN_TOUR[g_tour], &gx, &gy)
            || (hero_driven_c(p)->x == gx && hero_driven_c(p)->y == gy)
            || g_goal_steps > GOAL_PATIENCE) {
            /* Arrived, or it is not in this town, or it has had long enough.
               Next stop either way. */
            g_tour = (g_tour + 1) % TOWN_TOUR_COUNT;
            g_goal_steps = 0;
            continue;
        }
        int dx, dy;
        /* require_seen is false: the town is not fogged, and a policy that
           insisted on it would refuse to cross the plaza it is standing in. */
        if (!path_next_step(m, hero_driven_c(p)->x, hero_driven_c(p)->y, gx, gy, true, false, &dx, &dy)
            && !path_next_step(m, hero_driven_c(p)->x, hero_driven_c(p)->y, gx, gy, false, false, &dx, &dy)) {
            g_tour = (g_tour + 1) % TOWN_TOUR_COUNT;
            g_goal_steps = 0;
            continue;
        }
        g_goal_steps++;
        g_nav_why = (TOWN_TOUR[g_tour] == TILE_TEMPLE_ENTRANCE)
                  ? "town->temple" : "town->service";
        return key_for_step(dx, dy);
    }
    g_nav_why = "town->nowhere";
    return 0;
}

/* One step of the descent: the stairs if they have been found, the edge of
   what has been seen if they have not. Deliberately the rushing policy --
   depth per keystroke is the thing this mode was missing, and the one key in
   five that still goes to the pool is what picks things up on the way. */
static int temple_step(void) {
    const Player *p = g_scene_p;
    const Map *m = g_scene_m;
    if (!p || !m) return 0;

    if (m->floor_num != g_last_floor) {      /* a new floor is a new goal */
        g_last_floor = m->floor_num;
        g_goal_steps = 0;
        g_autoexplore_tries = 0;
        g_turns_at_autoexplore = -1;
    }

    int dx, dy;
    bool know_stairs = m->stairs_down_x >= 0 && m->stairs_down_y >= 0
                    && m->tiles[m->stairs_down_y][m->stairs_down_x].seen;

    if (know_stairs
        && (path_next_step(m, hero_driven_c(p)->x, hero_driven_c(p)->y, m->stairs_down_x, m->stairs_down_y,
                           true, true, &dx, &dy)
         || path_next_step(m, hero_driven_c(p)->x, hero_driven_c(p)->y, m->stairs_down_x, m->stairs_down_y,
                           false, false, &dx, &dy))) {
        g_nav_why = "temple->stairs";
        return key_for_step(dx, dy);
    }

    /* Stairs not found yet: hand the floor to the game's own explorer.
     *
       auto-explore fights what it meets, drinks when it is hurt, routes
       around hazards, stops when it is getting nowhere and takes the stairs
       down when it finds them -- all of which this policy would otherwise be
       reimplementing badly, one key at a time. Measured before this: a Normal
       run in The Halls spent 7,840 keys hand-walking the frontier and never
       once put the staircase on the map, because a level-1 character crossing
       one of the big worlds a square at a time does not live that long.

       Gated on whether it is achieving anything rather than on a count, and
       the difference is not cosmetic. A flat three-per-floor looked like a
       safe way to stop a policy whose only idea was 'x' from pressing it
       forever, and instead meant a soak run spent 189,243 keys hand-walking
       the frontier against about 339 invocations of the thing that actually
       crosses floors. The turn counter is the test: an auto-explore that gave
       up immediately leaves it where it was, and one that walked half the
       floor does not. Only a genuine refusal falls through to the frontier
       walk below, which is slower but always makes some progress. */
    if (p->turns != g_turns_at_autoexplore) g_autoexplore_tries = 0;
    if (g_autoexplore_tries < AUTOEXPLORE_TRIES) {
        g_autoexplore_tries++;
        g_turns_at_autoexplore = p->turns;
        g_nav_why = "temple->auto-explore";
        return 'x';
    }

    if (path_frontier_step(m, hero_driven_c(p)->x, hero_driven_c(p)->y, true, &dx, &dy)
        || path_frontier_step(m, hero_driven_c(p)->x, hero_driven_c(p)->y, false, &dx, &dy)) {
        g_nav_why = know_stairs ? "temple->explore (stairs unreachable)"
                                : "temple->explore (stairs not found)";
        return key_for_step(dx, dy);
    }

    g_nav_why = "temple->nowhere";
    return 0;   /* nothing left to walk toward: let the fuzzer have it */
}

/* ---- drinking ----------------------------------------------------------
 *
 * Pathing to the stairs got the run into the temple and no further: 153
 * descents into floor 1 and 150 deaths on it, every one of them at level 1
 * with a full pack. It was walking toward the staircase being eaten, because
 * nothing in the policy ever opened the pack on purpose and the fuzzer's odds
 * of pressing 'i' and then the right letter before dying are not good ones.
 *
 * So this is the second deliberate act, and the only other one. It is not
 * navigation, but it is what navigation needs to survive long enough to mean
 * anything, and it stays honest about the line: the *intent* is deliberate,
 * the screen it goes through is the game's real inventory screen driven by
 * the game's real keys, and if that screen changes shape the sequence breaks
 * loudly instead of silently doing nothing.
 *
 * Note that the threshold below cannot author a finding the way a constant in
 * tools/simulate.c can. This mode does not produce balance numbers; it
 * produces crashes, assertion failures and a depth counter. Being a slightly
 * better or worse drinker changes how deep it gets, not what is true.
 */
#define DRINK_BELOW_PCT 40

/* A short scripted sequence. Everything else the policy does is one key at a
   time, but drinking is three: open the pack, press the row the draught is
   on, come back out. Queued keys are served ahead of everything, including
   the scene logic, so re-arming the scene cannot interrupt a sequence
   half-way and leave the run sitting in an open inventory. */
static int g_pending[4];
static int g_pending_n = 0, g_pending_i = 0;

static void queue_keys(const int *keys, int n) {
    if (n > (int)(sizeof(g_pending) / sizeof(g_pending[0]))) return;
    for (int i = 0; i < n; i++) g_pending[i] = keys[i];
    g_pending_n = n;
    g_pending_i = 0;
}

/* The best drinkable thing on the first page of the pack, as the letter that
   selects it -- run_inventory addresses rows by 'a' + position *on the page*,
   and only the first page is reachable without also scripting the page turns.
   Recall charms are skipped because that screen refuses them by name. */
static int drink_key(const Player *p) {
    int best = -1, best_heal = 0;
    int n = p->inv_count < INV_PAGE ? p->inv_count : INV_PAGE;
    for (int i = 0; i < n; i++) {
        const InvStack *s = &p->inventory[i];
        if (s->count <= 0 || s->is_recall) continue;
        int heal = s->heal + hero_driven_c(p)->maxhp * s->heal_pct / 100;
        if (heal > best_heal) { best_heal = heal; best = i; }
    }
    return best < 0 ? 0 : 'a' + best;
}

/* How often navigation gives way to the pool even when it has a step to
   offer. One in five: enough that spells, the pack, the ability, auto-explore
   and every stray key still fire while a floor is being crossed, and not so
   much that crossing it takes a random walk's worth of keys. */
#define FUZZ_SHARE 5

int autoplay_key(void) {
    if (!g_active) return ERR;

    /* The scene is armed for exactly one key. Whatever screen this keypress
       opens is a menu as far as the policy is concerned, and gets the
       fuzzer -- which is what keeps navigation from having to know about
       screens that did not exist when it was written. */
    AutoScene scene = g_scene;
    g_scene = AP_SCENE_MENU;

    /* A sequence in progress owns the keyboard until it finishes. */
    if (g_pending_i < g_pending_n) {
        int key = g_pending[g_pending_i++];
        if (g_pending_i >= g_pending_n) g_pending_n = g_pending_i = 0;
        g_keys++;
        if (g_log) { fprintf(g_log, "%6ld nav drink\n", g_keys); fflush(g_log); }
        return key;
    }

    /* Out of budget: press quit-ish keys from here on so the run winds down
       through the game's own exit path rather than being killed mid-frame.
       A run that ends by longjmp would never write its save-prompt handling
       to the log, and that path is one of the ones worth watching. */
    if (g_keys >= g_max_keys) {
        g_keys++;
        if (g_keys > g_max_keys + 64) { autoplay_end("key budget exhausted"); exit(0); }
        return (g_keys & 1) ? 'q' : 'y';
    }

    /* "Descend fresh from floor 1" or "continue from the deepest floor". The
       fuzzer answered this by pressing Esc, which the prompt reads as 1 --
       so every descent after the first restarted at the top and the run
       could not accumulate depth however long it ran. Mostly continue; the
       occasional fresh start is a real choice a player makes and the branch
       deserves to be walked. */
    if (scene == AP_SCENE_DESCEND) {
        g_keys++;
        int key = (nextr() % 8 == 0) ? '1' : '2';
        if (g_log) {
            fprintf(g_log, "%6ld nav descend-%c\n", g_keys, (char)key);
            fflush(g_log);
        }
        return key;
    }

    /* Hurt, in the dungeon, with something to drink: open the pack, drink it,
       come out. Ahead of the fuzz share, because this is the branch that
       decides whether the run sees floor 2 at all. */
    /* The fuzzer drinks when the body it is playing is hurt, not when slot 0
       is -- otherwise it nurses the character while the hire it is driving
       bleeds out. */
    if (scene == AP_SCENE_TEMPLE && g_scene_p && hero_driven_c(g_scene_p)->maxhp > 0
        && hero_driven_c(g_scene_p)->hp * 100 / hero_driven_c(g_scene_p)->maxhp < DRINK_BELOW_PCT) {
        int letter = drink_key(g_scene_p);
        if (letter) {
            int seq[2] = { letter, 27 };
            queue_keys(seq, 2);
            g_keys++;
            if (g_log) { fprintf(g_log, "%6ld nav drink (open pack)\n", g_keys); fflush(g_log); }
            return 'i';
        }
    }

    if (scene != AP_SCENE_MENU && (nextr() % FUZZ_SHARE) != 0) {
        int key = (scene == AP_SCENE_TOWN) ? town_step() : temple_step();
        if (key) {
            g_keys++;
            if (g_log) {
                fprintf(g_log, "%6ld nav %s\n", g_keys, g_nav_why);
                fflush(g_log);
            }
            return key;
        }
        /* No step to offer -- fall through to the pool rather than stall. */
    }

    int roll = (int)(nextr() % (unsigned)g_weight_total);
    int i = 0;
    for (; i < KEY_COUNT - 1; i++) {
        roll -= KEYS[i].weight;
        if (roll < 0) break;
    }
    g_keys++;
    if (g_log) {
        fprintf(g_log, "%6ld %s\n", g_keys, KEYS[i].what);
        fflush(g_log);
    }
    return KEYS[i].key;
}

/* One poll in this many is answered with a keypress rather than ERR, so the
   "player calls the walk off" branch still gets exercised. Rare enough that a
   walk across a floor is not cut short by it. */
#define PEEK_INTERRUPT_ODDS 400

int autoplay_peek(void) {
    if (!g_active) return ERR;
    if (nextr() % PEEK_INTERRUPT_ODDS) return ERR;
    if (g_log) {
        fprintf(g_log, "%6ld nav interrupt (calling the walk off)\n", g_keys);
        fflush(g_log);
    }
    return 27;
}

/* Every this-many keys. Often enough to see the shape of a run, rare enough
   that the log stays readable. */
#define SNAPSHOT_EVERY 500
static long g_last_snapshot = 0;
static int  g_deepest = 0;

void autoplay_snapshot(int floor, int level, int hp, int maxhp, long gold,
                       int x, int y, int turns) {
    if (!g_active || !g_log) return;
    if (floor > g_deepest) g_deepest = floor;
    if (g_keys - g_last_snapshot < SNAPSHOT_EVERY) return;
    g_last_snapshot = g_keys;
    fprintf(g_log, "%6ld   @ floor %d lvl %d hp %d/%d gold %ld at (%d,%d) turn %d\n",
            g_keys, floor, level, hp, maxhp, gold, x, y, turns);
    fflush(g_log);
}

void autoplay_note(const char *fmt, ...) {
    if (!g_active || !g_log) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(g_log, "%6ld   | ", g_keys);
    vfprintf(g_log, fmt, ap);
    fputc('\n', g_log);
    va_end(ap);
    fflush(g_log);
}

void autoplay_end(const char *why) {
    if (!g_active) return;
    if (g_log) {
        fprintf(g_log, "\n# run ended: %s\n", why ? why : "unknown");
        fprintf(g_log, "# keys served: %ld\n", g_keys);
        fprintf(g_log, "# deepest floor reached: %d\n", g_deepest);
        fclose(g_log);
        g_log = NULL;
    }
    g_active = false;
}

/* The single point every screen in the game reads input through. common.h
   macros getch() to this; the #undef at the top of this file means the call
   below is the real terminal one. */
int aether_getch(void) {
    if (g_active) return autoplay_key();
    return getch();
}

/* The other way in. A silent run has no person to type a name, so it gets a
   fixed one -- the name is not what is under test, and varying it would only
   make two runs of the same seed diverge. */
int aether_getnstr(char *buf, int n) {
    if (!g_active) return getnstr(buf, n);
    if (n > 0) {
        snprintf(buf, (size_t)n, "fuzzer");
        autoplay_note("(name entered: fuzzer)");
    }
    return 0;
}
