/* Renders one screen at a given size and prints it as text.
 *
 * The project has had "walk through the shops at the minimum size" open as
 * ROADMAP 2.1e for a long time, and the reason it stayed open is that there
 * was no way to *look* at a screen without a human sitting at a terminal.
 * Every layout claim was therefore arithmetic -- and arithmetic said the
 * armory was fine while the smith's two lines sat six rows below the bottom of
 * a 30-row terminal, which is how the armour upgrade came to be invisible for
 * as long as it was (EVALUATION 95).
 *
 * This draws into a real curses buffer and dumps the cells. It needs a pty,
 * so run it through tools/screenshot.sh rather than a pipe.
 *
 *   ./bin/screenshot ROWS COLS SCREEN [OUT]
 *
 * SCREEN is one of the names in SCREENS below; "all" walks every one of them
 * and reports the ones that do not fit.
 *
 * Only the `draw_*` screens are listed. The `screen_*` ones run their own key
 * loop and block -- a distinction render.h already draws and which is worth
 * knowing before adding one here, because a blocking screen turns this into a
 * hang with no output rather than an error.
 */
#include "../src/common.h"
#include "../src/classes.h"
#include "../src/items.h"
#include "../src/render.h"
#include "../src/tui.h"
#include "../src/mapgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Player p;

static void furnish(void) {
    memset(&p, 0, sizeof p);
    apply_class_to_player(&p, 0);
    p.gold = 600000000;
    p.party[0].weapon_plus = 3;
    p.party[0].armor_plus  = 2;
    p.party[0].level = 12;
    p.mat_count[MAT_SCRAP] = 40;
    p.mat_count[MAT_PLATINUM] = 12;
    p.tavern_seed = 4242u;
    p.gold_mult = 1;
    p.deepest_floor = 30;
}

typedef struct { const char *name; void (*draw)(void); } Screen;

static void s_armory(void)     { draw_shop_armory(&p, hero_driven_c(&p)); }
static void s_general(void)    { draw_shop_general(&p, hero_driven_c(&p)); }
static void s_apothecary(void) { draw_shop_apothecary(&p, hero_driven_c(&p)); }
static void s_ranged(void)     { draw_shop_ranged(&p, hero_driven_c(&p)); }
static void s_arcanist(void)   { draw_shop_arcanist(&p, hero_driven_c(&p), 0); }
static void s_bank(void)       { draw_bank(&p, hero_driven_c(&p)); }
static void s_market(void)     { draw_black_market(&p, hero_driven_c(&p), 3); }
static void s_school(void)     { draw_gladiator_school(&p, hero_driven_c(&p), 0); }
static void s_oracle(void)     { draw_oracle(&p, hero_driven_c(&p)); }
/* Stage 1 rather than 0: the second stage is the taller of the two, since it
   adds the confirmation line under the grid. Checking the short one would
   pass a screen the player never has trouble with. */
static void s_altar(void)      { draw_altar(&p, hero_driven_c(&p), 1, 0, 5); }
static void s_bazaar(void)     { draw_bazaar(&p, hero_driven_c(&p)); }
static void s_tavern(void)     { draw_tavern(&p, 0); }
static void s_quests(void)     { draw_quest_board(&p, hero_driven_c(&p)); }
static void s_inventory(void)  { draw_inventory_screen(&p, hero_driven_c(&p), 0); }
static void s_spells(void)     { draw_spell_menu(hero_driven_c(&p), 0); }
static void s_records(void)    { draw_records_screen(&p, hero_driven_c(&p)); }

static const Screen SCREENS[] = {
    { "armory",     s_armory     },
    { "general",    s_general    },
    { "apothecary", s_apothecary },
    { "ranged",     s_ranged     },
    { "arcanist",   s_arcanist   },
    { "bank",       s_bank       },
    { "market",     s_market     },
    { "school",     s_school     },
    { "tavern",     s_tavern     },
    { "quests",     s_quests     },
    { "inventory",  s_inventory  },
    { "spells",     s_spells     },
    { "records",    s_records    },
    { "oracle",     s_oracle     },
    { "altar",      s_altar      },
    { "bazaar",     s_bazaar     },
};
#define SCREEN_COUNT ((int)(sizeof(SCREENS) / sizeof(SCREENS[0])))

/* Reads the drawn cells back out. `last_row` comes back as the lowest row with
   anything on it other than the frame, which is what says whether a screen has
   run off the bottom. */
static void dump(int rows, int cols, FILE *out, int *last_row, bool *border_ok) {
    *last_row = 0;
    *border_ok = true;
    for (int y = 0; y < rows; y++) {
        for (int x = 0; x < cols; x++) {
            chtype ch = mvinch(y, x);
            int c = (int)(ch & A_CHARTEXT);
            if (!c) c = ' ';
            if (out) fputc(c, out);
            /* Anything inside the frame that is not blank counts as content. */
            if (x > 0 && x < cols - 1 && y > 0 && y < rows - 1 && c != ' ') *last_row = y;
        }
        if (out) fputc('\n', out);
        /* The frame's right-hand border must survive on every row it covers. */
        if (y > 0 && y < rows - 1) {
            int edge = (int)(mvinch(y, cols - 1) & A_CHARTEXT);
            if (edge != '|') *border_ok = false;
        }
    }
}

/* ---- the play screen, drawn over a dirty terminal ----------------------
 *
 * Every screen above is drawn after an erase(), and the play screen is the one
 * screen in the game that deliberately never erases -- see the long note in
 * draw_game_screen(). It repaints the map, the panel and the log and trusts
 * that between them they cover everything, which means a column no region
 * claims keeps whatever the *last* screen left in it.
 *
 * That shipped: MAP_ORIGIN_X + VIEW_W .. sb_x - 1 was a two-column gutter
 * owned by neither the map loop nor the panel clear, so walking out of a shop
 * left a stripe of the shop down the side of the panel -- reported from play
 * as the panel growing a ragged extra column ("eLevel", "gGold", "sRecall").
 *
 * The property asserted here needs no knowledge of where the regions are, and
 * catches any missed clear rather than that one: **drawing the play screen
 * over a dirty terminal must produce exactly what drawing it over a clean one
 * does.** If it does not, some cell on screen is showing a previous screen.
 */
static Map *g_map;

static bool play_screen_is_self_covering(int rows, int cols, int *out_x, int *out_y) {
    static char clean[256 * 512], dirty[256 * 512];
    if (rows > 256) rows = 256;
    if (cols > 512) cols = 512;

    erase();
    draw_game_screen(g_map, &p, hero_driven_c(&p), "Floor 12 -- the bog", true);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            clean[y * cols + x] = (char)(mvinch(y, x) & A_CHARTEXT);

    /* Every cell dirtied, then the play screen on top -- rather than a
       particular shop, which only tests the columns that shop happens to
       write. The first version of this check drew draw_shop_general() and
       passed against the very bug it was written for, because at 100 columns
       that screen does not reach the gutter. A screen either covers what it
       owns or it does not; filling the terminal is what asks that question. */
    erase();
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) mvaddch(y, x, '#');
    draw_game_screen(g_map, &p, hero_driven_c(&p), "Floor 12 -- the bog", true);
    /* The key hints are the one row that has to survive the smallest window:
       a player who cannot see them cannot find the screens that hold the
       rest. Checked here rather than by eye because the row is composed from
       a literal and clipped at draw time, so it fails silently. */
    {
        int seen_hint = 0;
        for (int y = 0; y < rows; y++) {
            char line[512];
            int n = 0;
            for (int x = 0; x < cols && n < (int)sizeof line - 1; x++)
                line[n++] = (char)(mvinch(y, x) & A_CHARTEXT);
            line[n] = 0;
            if (strstr(line, "? keys")) seen_hint = 1;
        }
        if (!seen_hint) fprintf(stderr, "  key hints missing or clipped past \"? keys\"\n");
    }
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            dirty[y * cols + x] = (char)(mvinch(y, x) & A_CHARTEXT);

    int leaks = 0;
    for (int y = 0; y < rows; y++) {
        int run_start = -1;
        for (int x = 0; x < cols; x++) {
            bool bad_cell = clean[y * cols + x] != dirty[y * cols + x];
            if (bad_cell && run_start < 0) run_start = x;
            if ((!bad_cell || x == cols - 1) && run_start >= 0) {
                int end = bad_cell ? x : x - 1;
                if (leaks < 12)
                    fprintf(stderr, "  leak: row %2d, columns %d..%d\n", y, run_start, end);
                leaks++;
                if (out_x && *out_x < 0) { *out_x = run_start; *out_y = y; }
                run_start = -1;
            }
        }
    }
    if (leaks > 12) fprintf(stderr, "  ... and %d more\n", leaks - 12);
    return leaks == 0;
}

/* ---- the peek does not eat the keystroke --------------------------------
 *
 * The play loop skips its frame when a key is already waiting, which is what
 * stops a slow renderer running on after the player stops typing -- reported
 * from play in tile mode as "ghosting". That is only safe if peeking is free:
 * a peek that consumed the key would drop every second keystroke of a burst,
 * which reads as an unresponsive game rather than a laggy one and is worse
 * than the problem it fixes.
 *
 * It lives here rather than in tests/invariants.c because it needs a real
 * curses screen, and that suite is headless -- the first version of this check
 * sat there and failed for want of an initscr(), which is the suite being
 * right about what it is rather than the code being wrong.
 */
static const char *peek_is_free(void) {
    flushinp();                                  /* the pty may have sent keys */
    if (input_waiting())  return "an empty queue did not read as empty";
    ungetch('k');
    if (!input_waiting()) return "a queued key was not noticed";
    if (!input_waiting()) return "noticing a key twice ate it";
    int c = getch();
    if (c != 'k')         return "the peeked key was not the one read back";
    if (input_waiting())  return "the queue still reads as full after reading";
    return NULL;
}

/* ---- a small viewport must not leave a big window empty ------------------
 *
 * Tile mode's per-frame pixel budget shrinks the *map viewport* and nothing
 * else (render_clamp_tile_viewport). On a retina display, where a cell reports
 * physical pixels, it shrinks it hard. Everything downstream used to be
 * anchored to the map rather than to the window, so a large terminal drew a
 * small picture in one corner, the panel hard against it, and several hundred
 * square characters of framed nothing -- reported from play with a screenshot.
 *
 * Simulated here rather than measured in tile mode, because notcurses will not
 * start under a pipe and no instrument in this repository can open it. The
 * simulation is exact where it matters: the clamp's only effect is to make
 * VIEW_W/VIEW_H smaller than the window, and that is what this does.
 */
static const char *layout_fills_the_window(int rows, int cols) {
    int save_w = g_view_w, save_h = g_view_h;
    /* A viewport genuinely shorter than the window can hold -- which is the
       whole point, and which this was not doing. It pinned the height at 18
       while the sweep runs in a 24-row window, where the map's natural height
       is 15; an 18-row viewport is *taller* than the window allows, so the
       short-map path this function exists to exercise never ran and the
       divider bug walked straight past it.
     *
       Derived from the window instead, so it stays short whatever size the
       tool is called at. */
    g_view_w = 46;
    g_view_h = rows - 14;
    if (g_view_h < 6) g_view_h = 6;

    erase();
    draw_game_screen(g_map, &p, hero_driven_c(&p), "Floor 12 -- the bog", true);

    int right = 0, bottom = 0;
    for (int y = 1; y < rows - 1; y++)
        for (int x = 1; x < cols - 1; x++) {
            int c = (int)(mvinch(y, x) & A_CHARTEXT);
            if (c && c != ' ') { if (x > right) right = x; if (y > bottom) bottom = y; }
        }

    /* The divider under the map used to be drawn the full width of the window
       regardless of where the map ended, so with a short viewport it was ruled
       straight across the middle of the sidebar: "Weapon: Piston Fist--------".
       Reported from play as the text on the right being cut. Measure the run of
       dashes to the right of the sidebar's divider column on that row -- the
       sidebar's own lines never contain one that long. */
    int run = 0, longest = 0;
    {
        /* render.c's MAP_ORIGIN_X / MAP_ORIGIN_Y, which are file-static there.
           If the map ever moves, this check goes quiet rather than wrong --
           it would measure a row with no divider on it and find nothing. */
        int div_x = 1 + g_view_w;
        int div_y = 2 + g_view_h;
        if (div_y > 0 && div_y < rows - 1)
            for (int x = div_x + 1; x < cols - 1; x++) {
                int c = (int)(mvinch(div_y, x) & A_CHARTEXT);
                if (c == '-') { if (++run > longest) longest = run; } else run = 0;
            }
    }
    run = longest;

    /* The log must stop at the sidebar. It used to clip at the window edge, so
       a long line wrote straight over the panel -- "...Warbow.rmour: Void-
       tempered Plate", the log eating the A of "Armour:".
     *
       Done as a difference rather than by looking for a marker character: the
       first version filled the log with 'A's and searched the sidebar columns
       for one, and found the 'A' of "ATK/DEF" -- the sidebar's own text, at
       the sidebar's own first column. Drawing the same screen twice with a
       short log and a very long one, and comparing everything right of the
       divider, cannot be fooled that way: whatever differs is the log. */
    int log_bleed = -1;
    {
        int div_x = 1 + g_view_w;
        static chtype before[64][256];

        log_reset();
        log_msg("short");
        erase();
        draw_game_screen(g_map, &p, hero_driven_c(&p), "Floor 12 -- the bog", true);
        for (int y = 0; y < rows && y < 64; y++)
            for (int x = 0; x < cols && x < 256; x++) before[y][x] = mvinch(y, x);

        log_reset();
        log_msg("%s", "the quick brown fox jumps over the lazy dog and keeps on going "
                      "well past anywhere it has any business being, twice over, and "
                      "then some more for good measure");
        erase();
        draw_game_screen(g_map, &p, hero_driven_c(&p), "Floor 12 -- the bog", true);

        /* Every row, not a row computed here. Working out which row the log
           lands on duplicates layout arithmetic that lives in render.c, and the
           first version of this test got it wrong and reported clean. */
        int bleed = 0;
        for (int y = 1; y < rows - 1 && y < 64; y++)
            for (int x = div_x; x < cols - 1 && x < 256; x++)
                if (mvinch(y, x) != before[y][x]) bleed++;
        if (bleed > 0) log_bleed = bleed;
        log_reset();
    }

    g_view_w = save_w; g_view_h = save_h;

    static char msg[128];
    /* The panel has to reach the right-hand side and the log the bottom. Slack
       of a few cells for the panel's own margin and the footer's short line. */
    if (right < cols - 12) {
        snprintf(msg, sizeof msg,
                 "a %dx%d window with a %dx18 viewport is blank past column %d of %d",
                 cols, rows, 46, right, cols);
        return msg;
    }
    if (bottom < rows - 6) {
        snprintf(msg, sizeof msg,
                 "...and blank below row %d of %d", bottom, rows);
        return msg;
    }
    if (log_bleed >= 0) {
        snprintf(msg, sizeof msg,
                 "a long log line changes %d columns of the sidebar", log_bleed);
        return msg;
    }
    if (run > 8) {
        snprintf(msg, sizeof msg,
                 "the map's bottom divider is struck through %d columns of the sidebar",
                 run);
        return msg;
    }
    return NULL;
}

int main(int argc, char **argv) {
    int rows = argc > 1 ? atoi(argv[1]) : 30;
    int cols = argc > 2 ? atoi(argv[2]) : 100;
    const char *which = argc > 3 ? argv[3] : "armory";
    const char *outp  = argc > 4 ? argv[4] : NULL;

    if (rows < 4 || cols < 20) { fprintf(stderr, "size too small\n"); return 2; }

    furnish();
    initscr();
    resizeterm(rows, cols);

    /* The report is buffered and printed after endwin(). Written as it goes it
       interleaves with the curses output on the same pty and comes out as
       escape codes with words in. */
    char report[SCREEN_COUNT][128];
    int reported = 0;

    int bad = 0;
    for (int i = 0; i < SCREEN_COUNT; i++) {
        bool all = (strcmp(which, "all") == 0);
        if (!all && strcmp(which, SCREENS[i].name) != 0) continue;

        erase();
        SCREENS[i].draw();

        FILE *f = NULL;
        if (!all) f = outp ? fopen(outp, "w") : stdout;
        int last_row = 0; bool border_ok = true;
        dump(rows, cols, f, &last_row, &border_ok);
        if (f && f != stdout) fclose(f);

        /* The last usable content row is rows-2: rows-1 is the frame. */
        bool fits = (last_row <= rows - 2);
        if (!fits || !border_ok) bad++;
        if (all)
            snprintf(report[reported++], sizeof(report[0]),
                     "%-11s last content row %2d of %2d   %-9s  %s",
                     SCREENS[i].name, last_row, rows - 2,
                     fits ? "fits" : "OVERFLOWS",
                     border_ok ? "border ok" : "BORDER LOST");
    }

    /* The play screen, only on the "all" walk -- it is a different question
       from "does this fit", and it needs a floor to draw. */
    int leak_x = -1, leak_y = -1;
    bool ran_play = false, play_clean = true;
    const char *layout_bad = NULL;
    if (strcmp(which, "all") == 0 || strcmp(which, "game") == 0) {
        g_map = calloc(1, sizeof *g_map);
        if (g_map) {
            world_size_apply(WORLD_SHAFT);
            p.run_seed = 12u;
            p.floor = 12;
            generate_temple_floor(g_map, 12, &p.party[0].x, &p.party[0].y, &p);
            ran_play = true;
            play_clean = play_screen_is_self_covering(rows, cols, &leak_x, &leak_y);
            if (!play_clean) bad++;
            layout_bad = layout_fills_the_window(rows, cols);   /* while g_map lives */
            free(g_map);
            g_map = NULL;
        }
    }

    const char *peek_bad = peek_is_free();

    endwin();

    printf("input peek: %s\n", peek_bad
           ? peek_bad
           : "free -- a frame can be skipped without losing the key that caused it");
    if (peek_bad) bad++;

    if (ran_play) {
        printf("small viewport in a big window: %s\n",
               layout_bad ? layout_bad : "the panel and the log still reach the edges");
        if (layout_bad) bad++;
        printf("\nplay screen at %dx%d: %s\n", rows, cols,
               play_clean ? "covers itself -- no cell survives from the previous screen"
                          : "LEAKS the previous screen");
        if (!play_clean)
            printf("  first stale cell at column %d, row %d\n", leak_x, leak_y);
    }

    if (reported > 0) {
        printf("\n%d screens at %dx%d\n", reported, rows, cols);
        for (int i = 0; i < reported; i++) printf("  %s\n", report[i]);
        printf("%s\n", bad ? "  -- at least one screen does not fit this size"
                            : "  all fit, all keep their border");
    }
    return bad ? 1 : 0;
}
