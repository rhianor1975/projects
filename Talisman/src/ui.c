#include "talisman.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CW        11                       /* cell interior width  */
#define CH        2                        /* cell interior height */
#define BOARD_TOP 1
#define BOARD_LEFT 0
#define BOARD_W   (GRID * (CW + 1) + 1)     /* 85 */
#define BOARD_H   (GRID * (CH + 1) + 1)     /* 22 */
#define ROW_STAT  (BOARD_TOP + BOARD_H)     /* 23, 24 */
#define ROW_LOG   (ROW_STAT + 2)            /* 25, 26, 27 */
#define UI_MIN_COLS 86
#define UI_MIN_ROWS 29
#define LOG_KEEP  64
#define LOG_MIN   3                         /* what 29 rows can spare */

/* The layout used to be fixed at the 29-row minimum, so every row past the
 * twenty-ninth was dead space -- on a 40-row terminal, eleven wasted lines
 * under the prompt.  The board is a fixed size (it is a board), but the log
 * has no natural height, so it takes whatever the window gives it and the
 * prompt sits on the last row. */
static int log_rows(void)
{
    int n = LINES - 1 - ROW_LOG;            /* last row belongs to the prompt */
    if (n < LOG_MIN)  n = LOG_MIN;
    if (n > LOG_KEEP) n = LOG_KEEP;
    return n;
}
#define ROW_PROMPT (ROW_LOG + log_rows())
#define PANEL_X   (BOARD_W + 1)   /* dice + legend column, needs COLS >= 99 */
#define PANEL_W   13

enum {
    P_OUTER = 1, P_MIDDLE, P_INNER, P_CROWN, P_CROSS, P_FRAME, P_TITLE, P_DIM,
    P_P1 = 11, P_P2, P_P3, P_P4
};

static char  logbuf[LOG_KEEP][BOARD_W + 1];
static int   log_n;
static FILE *tracefp;      /* TALISMAN_TRACE=<path>|- : plain-text game log */
static const char *shotpath;   /* TALISMAN_SHOT=<path> : append each frame */
static const char *logpath;    /* --log FILE, overrides TALISMAN_TRACE      */

void ui_set_log(const char *path) { logpath = path; }
static void ui_shot(void);

/* last dice thrown, so the player can actually see them */
static char lab1[16], lab2[16];
static int  die1, die2, tot1, tot2, show1, show2;

void ui_dice_clear(void) { show1 = show2 = 0; }

void ui_dice_one(const char *label, int die, int total)
{
    snprintf(lab1, sizeof lab1, "%s", label ? label : "");
    die1 = die; tot1 = total; show1 = 1; show2 = 0;
}

void ui_dice_two(const char *l1, int d1, int t1, const char *l2, int d2, int t2)
{
    snprintf(lab1, sizeof lab1, "%s", l1 ? l1 : "");
    snprintf(lab2, sizeof lab2, "%s", l2 ? l2 : "");
    die1 = d1; tot1 = t1; die2 = d2; tot2 = t2; show1 = show2 = 1;
}

void ui_init(void)
{
    const char *tr = logpath ? logpath : getenv("TALISMAN_TRACE");

    if (tr && *tr)
        tracefp = (tr[0] == '-' && tr[1] == '\0') ? stderr : fopen(tr, "w");
    shotpath = getenv("TALISMAN_SHOT");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(P_OUTER,  COLOR_GREEN,   -1);
        init_pair(P_MIDDLE, COLOR_YELLOW,  -1);
        init_pair(P_INNER,  COLOR_RED,     -1);
        init_pair(P_CROWN,  COLOR_MAGENTA, -1);
        init_pair(P_CROSS,  COLOR_CYAN,    -1);
        init_pair(P_FRAME,  COLOR_BLUE,    -1);
        init_pair(P_TITLE,  COLOR_WHITE,   COLOR_BLUE);
        init_pair(P_DIM,    COLOR_WHITE,   -1);
        init_pair(P_P1,     COLOR_WHITE,   -1);
        init_pair(P_P2,     COLOR_CYAN,    -1);
        init_pair(P_P3,     COLOR_MAGENTA, -1);
        init_pair(P_P4,     COLOR_YELLOW,  -1);
    }
}

void ui_end(void) { curs_set(1); endwin(); }

int ai_why;

/* Only ever to the trace file: the on-screen log has three lines and this
 * would fill all of them every turn. */
void wlog(const char *fmt, ...)
{
    va_list ap;

    if (!ai_why || !tracefp) return;
    va_start(ap, fmt);
    vfprintf(tracefp, fmt, ap);
    va_end(ap);
    fputc('\n', tracefp);
    fflush(tracefp);
}

void glog(const char *fmt, ...)
{
    va_list ap;
    int i;

    if (log_n == LOG_KEEP) {
        for (i = 1; i < LOG_KEEP; i++)
            memcpy(logbuf[i - 1], logbuf[i], sizeof logbuf[0]);
        log_n--;
    }
    va_start(ap, fmt);
    vsnprintf(logbuf[log_n], sizeof logbuf[0], fmt, ap);
    va_end(ap);
    if (tracefp) { fprintf(tracefp, "%s\n", logbuf[log_n]); fflush(tracefp); }
    log_n++;
}

/* colour of a space, by region and by whether it is a crossing */
static int space_pair(int region, SpaceKind k)
{
    if (k == SP_SENTINEL || k == SP_PORTAL || k == SP_GATE) return P_CROSS;
    if (k == SP_CROWN_SP) return P_CROWN;
    switch (region) {
    case REG_OUTER:  return P_OUTER;
    case REG_MIDDLE: return P_MIDDLE;
    default:         return P_INNER;
    }
}

/* what a thing left on a space looks like on the board */
static int res_glyph(int card, int *pair, int *bold)
{
    const Card *c = &deck_proto[card];

    *bold = 1;
    switch (c->type) {
    case C_ENEMY:
    case C_SPIRIT:
        *pair = P_INNER;  return '!';
    case C_STRANGER:
        *pair = P_MIDDLE; return '?';
    case C_PLACE:
        switch (c->place) {
        case PLACE_SHOP:     *pair = P_CROSS;  return '&';
        case PLACE_PORTAL:   *pair = P_CROWN;  return '%';
        case PLACE_SHRINE:
        case PLACE_POOL:
        case PLACE_FOUNTAIN:
        case PLACE_FAIRY:    *pair = P_OUTER;  return '+';
        case PLACE_MAZE:
        case PLACE_MARSH:    *pair = P_INNER;  return '~';
        default:             *pair = P_INNER;  return 'M';
        }
    default:
        *pair = P_DIM; *bold = 0; return '*';
    }
}

static void draw_frame(void)
{
    int r, c, y, x;

    attron(COLOR_PAIR(P_FRAME));
    for (r = 0; r <= GRID; r++)
        mvhline(BOARD_TOP + r * (CH + 1), BOARD_LEFT, ACS_HLINE, BOARD_W);
    for (c = 0; c <= GRID; c++)
        mvvline(BOARD_TOP, BOARD_LEFT + c * (CW + 1), ACS_VLINE, BOARD_H);
    for (r = 0; r <= GRID; r++)
        for (c = 0; c <= GRID; c++) {
            y = BOARD_TOP + r * (CH + 1);
            x = BOARD_LEFT + c * (CW + 1);
            mvaddch(y, x, ACS_PLUS);
        }
    attroff(COLOR_PAIR(P_FRAME));
}

/* Only one board is on screen: the one the character whose turn it is is
 * standing on.  A cell that board has no space at is left blank. */
static int shown_board(void)
{
    return region_board(players[cur_player].region);
}

static void draw_cells(void)
{
    int r, c, y, x, i, n, pair;
    int b = shown_board();
    const Space *sp;

    for (r = 0; r < GRID; r++)
        for (c = 0; c < GRID; c++) {
            int region = gridmap[b][r][c].region;
            int idx    = gridmap[b][r][c].idx;
            if (region < 0) continue;             /* nothing here on this board */
            sp   = space_at(region, idx);
            pair = space_pair(region, sp->kind);
            y = BOARD_TOP + r * (CH + 1) + 1;
            x = BOARD_LEFT + c * (CW + 1) + 1;

            attron(COLOR_PAIR(pair));
            if (pair == P_CROSS || sp->kind == SP_CROWN_SP) attron(A_BOLD);
            mvprintw(y, x, "%-*.*s", CW, CW, sp->name);
            if (pair == P_CROSS || sp->kind == SP_CROWN_SP) attroff(A_BOLD);
            attroff(COLOR_PAIR(pair));

            /* second line: what is lying here (right), players (left) */
            {
                int sid = space_id(region, idx);
                int gx  = x + CW - 1, k, gp, gb, gc;

                mvprintw(y + 1, x, "%-*s", CW, "");
                /* The card count belongs to the space itself, so it keeps
                 * the last column; what is lying here shuffles left of it. */
                if (sp->draw > 0) {
                    attron(COLOR_PAIR(P_DIM) | A_DIM);
                    mvprintw(y + 1, gx--, "%d", sp->draw);
                    attroff(COLOR_PAIR(P_DIM) | A_DIM);
                }
                if (res_tal[sid] && gx >= x) {
                    attron(COLOR_PAIR(P_CROSS) | A_BOLD | A_REVERSE);
                    mvaddch(y + 1, gx--, 'T');
                    attroff(COLOR_PAIR(P_CROSS) | A_BOLD | A_REVERSE);
                }
                if (res_gold[sid] && gx >= x) {
                    attron(COLOR_PAIR(P_MIDDLE) | A_BOLD);
                    mvaddch(y + 1, gx--, '$');
                    attroff(COLOR_PAIR(P_MIDDLE) | A_BOLD);
                }
                for (k = 0; k < res_n[sid] && gx >= x + 3; k++) {
                    gc = res_glyph(res_card[sid][k], &gp, &gb);
                    attron(COLOR_PAIR(gp) | (gb ? A_BOLD : 0));
                    mvaddch(y + 1, gx--, gc);
                    attroff(COLOR_PAIR(gp) | (gb ? A_BOLD : 0));
                }
            }
            n = 0;
            for (i = 0; i < nplayers; i++) {
                if (!players[i].alive) continue;
                if (players[i].region != region || players[i].idx != idx) continue;
                attron(COLOR_PAIR(P_P1 + i) | A_BOLD);
                if (i == cur_player) attron(A_REVERSE);
                mvprintw(y + 1, x + n * 3, "@%d", i + 1);
                if (i == cur_player) attroff(A_REVERSE);
                attroff(COLOR_PAIR(P_P1 + i) | A_BOLD);
                n++;
            }
        }
}

static void draw_status(void)
{
    int i, y, x, colw;
    char buf[80];

    colw = (COLS - 3) / 2;                 /* spread into the panel column */
    if (colw > 48) colw = 48;
    if (colw < 40) colw = 40;

    for (i = 0; i < nplayers; i++) {
        Player *p = &players[i];
        y = ROW_STAT + i / 2;
        x = (i % 2) * (colw + 2);
        snprintf(buf, sizeof buf, "%d %-8.8s S%-2d C%-2d L%d/%d F%d/%d G%-2d t%d/%-2d z%d %s%s",
                 i + 1, p->cls, eff_str(p), eff_craft(p), p->lives, eff_maxlives(p),
                 p->fate, eff_maxfate(p), p->gold, p->troph_str, p->troph_craft, p->nspells,
                 p->talisman ? "T " : "  ",
                 p->alive ? (p->ai ? "cpu" : "you") : "DEAD");
        attron(COLOR_PAIR(P_P1 + i));
        if (i == cur_player && p->alive) attron(A_BOLD | A_REVERSE);
        mvprintw(y, x, "%-*.*s", colw, colw, buf);
        if (i == cur_player && p->alive) attroff(A_BOLD | A_REVERSE);
        attroff(COLOR_PAIR(P_P1 + i));
    }
}

static void draw_log(void)
{
    int show = log_rows();
    int i, first = log_n - show;

    if (first < 0) first = 0;
    for (i = 0; i < show; i++) {
        int src = first + i;
        move(ROW_LOG + i, 0);
        clrtoeol();
        if (src < log_n) {
            if (src == log_n - 1) attron(A_BOLD);
            mvprintw(ROW_LOG + i, 0, "%-*.*s", BOARD_W, BOARD_W, logbuf[src]);
            if (src == log_n - 1) attroff(A_BOLD);
        }
    }
}

static const char *pipface[7][3] = {
    { "     ", "     ", "     " },
    { "     ", "  o  ", "     " },   /* 1 */
    { "o    ", "     ", "    o" },   /* 2 */
    { "o    ", "  o  ", "    o" },   /* 3 */
    { "o   o", "     ", "o   o" },   /* 4 */
    { "o   o", "  o  ", "o   o" },   /* 5 */
    { "o   o", "o   o", "o   o" },   /* 6 */
};

static void draw_die(int y, int x, int v, int pair)
{
    int i;

    if (v < 1 || v > 6) v = 1;
    attron(COLOR_PAIR(pair) | A_BOLD);
    mvprintw(y, x, "+-----+");
    for (i = 0; i < 3; i++)
        mvprintw(y + 1 + i, x, "|%s|", pipface[v][i]);
    mvprintw(y + 4, x, "+-----+");
    attroff(COLOR_PAIR(pair) | A_BOLD);
}

static const struct { const char *ch; const char *what; int pair; } legend[] = {
    { "!",   "monster",   P_INNER  },
    { "?",   "stranger",  P_MIDDLE },
    { "M",   "den",       P_INNER  },
    { "~",   "hazard",    P_INNER  },
    { "&%",  "shop/gate", P_CROSS  },
    { "+",   "boon",      P_OUTER  },
    { "*$T", "loot",      P_MIDDLE },
    { "1-3", "cards",     P_DIM    },
};

static void draw_panel(void)
{
    int i, n = (int)(sizeof legend / sizeof legend[0]);

    if (COLS < PANEL_X + PANEL_W) return;      /* no room: log still shows rolls */

    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(BOARD_TOP, PANEL_X, "%-*.*s", PANEL_W, PANEL_W, "   D I C E");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);

    for (i = BOARD_TOP + 1; i < BOARD_TOP + 13; i++)
        mvprintw(i, PANEL_X, "%-*s", PANEL_W, "");

    if (show1) {
        attron(A_BOLD);
        if (tot1 >= 0) mvprintw(BOARD_TOP + 1, PANEL_X, "%-9.9s %3d", lab1, tot1);
        else           mvprintw(BOARD_TOP + 1, PANEL_X, "%-13.13s", lab1);
        attroff(A_BOLD);
        draw_die(BOARD_TOP + 2, PANEL_X + 3, die1, P_P1 + cur_player);
    }
    if (show2) {
        attron(A_BOLD);
        if (tot2 >= 0) mvprintw(BOARD_TOP + 7, PANEL_X, "%-9.9s %3d", lab2, tot2);
        else           mvprintw(BOARD_TOP + 7, PANEL_X, "%-13.13s", lab2);
        attroff(A_BOLD);
        draw_die(BOARD_TOP + 8, PANEL_X + 3, die2, P_INNER);
    }

    /* Anyone standing on another board cannot be seen on this one, so the
     * panel names them.  While every character shares a board -- which is
     * the whole game until the Dungeon opens -- there is nothing to say and
     * the legend keeps its rows. */
    {
        int away = 0, b = shown_board();
        for (i = 0; i < nplayers; i++)
            if (players[i].alive && region_board(players[i].region) != b) away++;
        if (away) {
            int row = BOARD_TOP + 14;
            attron(COLOR_PAIR(P_TITLE) | A_BOLD);
            mvprintw(BOARD_TOP + 13, PANEL_X, "%-*.*s", PANEL_W, PANEL_W, " ELSEWHERE");
            attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
            for (i = 0; i < nplayers && row < BOARD_TOP + BOARD_H; i++) {
                int pb = region_board(players[i].region);
                if (!players[i].alive || pb == b) continue;
                mvprintw(row, PANEL_X, "%-*s", PANEL_W, "");
                attron(COLOR_PAIR(P_P1 + i) | A_BOLD);
                mvprintw(row, PANEL_X, "@%d", i + 1);
                attroff(COLOR_PAIR(P_P1 + i) | A_BOLD);
                /* The panel is 13 columns.  Indenting the board name by 3
                 * left only 10, which clipped "The Timescape" -- the one
                 * name that needs all 13 -- to "The Timesc".  So the marker
                 * shares its row with the space, and the board name gets the
                 * full width on its own. */
                mvprintw(row, PANEL_X + 3, "%-*.*s", PANEL_W - 3, PANEL_W - 3,
                         space_at(players[i].region, players[i].idx)->name);
                row++;
                if (row < BOARD_TOP + BOARD_H) {
                    mvprintw(row, PANEL_X, "%-*.*s", PANEL_W, PANEL_W,
                             board_tbl[pb].name);
                    row++;
                }
            }
            while (row < BOARD_TOP + BOARD_H)
                mvprintw(row++, PANEL_X, "%-*s", PANEL_W, "");
            return;
        }
    }

    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(BOARD_TOP + 13, PANEL_X, "%-*.*s", PANEL_W, PANEL_W, " ON THE MAP");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    for (i = 0; i < n; i++) {
        mvprintw(BOARD_TOP + 14 + i, PANEL_X, "%-*s", PANEL_W, "");
        attron(COLOR_PAIR(legend[i].pair) | A_BOLD);
        mvprintw(BOARD_TOP + 14 + i, PANEL_X, "%-3s", legend[i].ch);
        attroff(COLOR_PAIR(legend[i].pair) | A_BOLD);
        mvprintw(BOARD_TOP + 14 + i, PANEL_X + 4, "%-9.9s", legend[i].what);
    }
}

/* Dump the virtual screen for documentation/tests (TALISMAN_SHOT). */
static void ui_shot(void)
{
    FILE *f;
    int y, x, cy, cx;

    if (!shotpath || !*shotpath) return;
    if (!(f = fopen(shotpath, "a"))) return;

    getyx(stdscr, cy, cx);
    fprintf(f, "--8<--\n");
    for (y = 0; y < LINES; y++) {
        for (x = 0; x < COLS; x++) {
            chtype cc = mvinch(y, x);
            int    ch = (int)(cc & A_CHARTEXT);
            if (cc & A_ALTCHARSET)            /* line-drawing -> plain ASCII */
                ch = (ch == 'q') ? '-' : (ch == 'x') ? '|' : '+';
            fputc(ch, f);
        }
        fputc('\n', f);
    }
    fclose(f);
    move(cy, cx);
}

void ui_draw(void)
{
    erase();
    {   /* With one board on screen at a time, the title has to say which
         * board you are looking at and whose turn put it there -- otherwise
         * the view changes under you with no explanation. */
        char t[160];
        const BoardDef *bd = &board_tbl[shown_board()];
        snprintf(t, sizeof t, "  T A L I S M A N   -   %s   -   %s's turn  ",
                 bd->name, players[cur_player].name);
        attron(COLOR_PAIR(P_TITLE) | A_BOLD);
        mvprintw(0, 0, "%-*.*s", BOARD_W, BOARD_W, t);
        attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    }
    draw_frame();
    draw_cells();
    draw_panel();
    draw_status();
    draw_log();
    refresh();

    ui_shot();
}

void ui_banner(const char *msg)
{
    move(ROW_PROMPT, 0);
    clrtoeol();
    attron(A_BOLD);
    mvprintw(ROW_PROMPT, 0, "%-*.*s", BOARD_W, BOARD_W, msg);
    attroff(A_BOLD);
    refresh();
    ui_shot();
}


/* Everything the game can do, from inside the game.  Anything that lives
 * only in a README may as well not exist: you are at the table, not at the
 * repository. */
/* Dump the current virtual screen on demand -- screens that draw themselves
 * without going through ui_draw() have no other way to be looked at. */
void ui_snapshot(void) { ui_shot(); }

/* Drawn but not waited on, so the screenshot tool can look at it. */
static void ui_draw_help(void);
void ui_shot_help(void) { ui_draw_help(); ui_shot(); }

static void ui_draw_help(void)
{
    static const char *lines[] = {
    "  T A L I S M A N   --   keys and switches",
    "",
    "  AT ANY PROMPT",
    "    ?          this page",
    "    t          your character sheet",
    "    space      take the offered action / continue",
    "    q          leave the game",
    "",
    "  WATCHING (--watch, or while the computer plays)",
    "    space      pause and unpause",
    "    +  -       faster / slower",
    "    q          stop watching",
    "",
    "  STARTING THE GAME",
    "    talisman --watch            every character played by the computer",
    "    talisman --sets=all         shuffle in every expansion",
    "    talisman --sets=reaper,city  or name the ones you want",
    "    talisman --log FILE         write the whole game out as text",
    "",
    "  THE SAME THINGS AS ENVIRONMENT (useful for scripting a soak)",
    "    TALISMAN_SEED=n    replay a game exactly; the seed is printed at the top",
    "    TALISMAN_DELAY=ms  pace the computer's turns (0 for as fast as it will go)",
    "    TALISMAN_AUTO=1    headless: no screen, prints one result line",
    "    TALISMAN_SETS=all  as --sets",
    "    TALISMAN_TRACE=f   as --log",
    "    TALISMAN_CHECK=1   assert the rules invariants; anything printed is a bug",
    "    TALISMAN_SHOT=f    append every frame to a file",
    NULL };
    int i, y = 1;

    erase();
    for (i = 0; lines[i]; i++) {
        if (i == 0) attron(A_BOLD);
        mvprintw(y++, 1, "%-*.*s", COLS - 2, COLS - 2, lines[i]);
        if (i == 0) attroff(A_BOLD);
    }
    mvprintw(y + 1, 1, "current: seed %u, delay %d ms", ui_seed(), ui_delay());
    mvprintw(LINES - 1, 0, "%-*.*s", COLS, COLS, "  [any key] back to the game");
    refresh();
}

static void ui_help(void)
{
    ui_draw_help();
    getch();
}

/* The prompt sits on a fixed row, so every full-screen menu has the same
 * usable area no matter how tall the terminal is.  Screens that laid
 * themselves out from LINES were writing their last line straight onto the
 * prompt row, where ui_banner()'s clrtoeol() wiped it. */
int ui_prompt_row(void) { return ROW_PROMPT; }

/* Dragged too small to draw the board.  Say what is needed and what there
 * is, rather than painting something unreadable. */
int ui_too_small(void)
{
    int rows, cols;

    getmaxyx(stdscr, rows, cols);
    if (rows >= UI_MIN_ROWS && cols >= UI_MIN_COLS) return 0;
    erase();
    mvprintw(0, 0, "The board needs %dx%d.", UI_MIN_COLS, UI_MIN_ROWS);
    mvprintw(1, 0, "This window is %dx%d.", cols, rows);
    mvprintw(2, 0, "Make it bigger to play.");
    refresh();
    return 1;
}

int ui_prompt(const char *msg, const char *valid)
{
    int ch;

    for (;;) {
        ui_banner(msg);
        ch = getch();
        if (ch == 0 || ch == ERR) continue;
        /* The window changed shape.  The log takes its height from the
         * terminal, so everything has to be laid out again -- and if it has
         * been dragged below the minimum, say so instead of drawing a board
         * that runs off the edge. */
        if (ch == KEY_RESIZE) {
            while (ui_too_small()) {
                ch = getch();
                if (ch != KEY_RESIZE && ch != ERR) break;
            }
            ui_draw();
            continue;
        }
        if (ch == '?') { ui_help(); ui_draw(); continue; }   /* always available */
        if (ch == KEY_LEFT)  ch = 'a';
        if (ch == KEY_RIGHT) ch = 'd';
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) ch = ' ';
        if (strchr(valid, ch)) return ch;
    }
}

/* Wait up to `ms` for a keypress. ms < 0 blocks, 0 polls. */
int ui_wait_key(int ms)
{
    int ch;

    timeout(ms);
    ch = getch();
    timeout(-1);
    if (ch >= 'A' && ch <= 'Z') ch += 32;
    if (ch == '\r' || ch == KEY_ENTER) ch = '\n';
    return ch;
}

/* Choose one of a character's carried items. Returns a slot, or -1. */
int ui_pick_item(Player *p, const char *title, int objects_only)
{
    int i, ch, n = 0, slot[MAX_ITEMS];

    if (p->nitems <= 0) return -1;
    erase();
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(0, 0, "%-*.*s", COLS - 1, COLS - 1, title);
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(2, 2, "%s the %s is carrying:", p->name, p->cls);
    for (i = 0; i < p->nitems && n < 9; i++) {
        const Card *c = &deck_proto[p->carried[i]];
        if (objects_only && c->type != C_OBJECT) continue;
        mvprintw(4 + n, 4, "[%d] %-16s %s", n + 1, c->name, c->text ? c->text : "");
        slot[n++] = i;
    }
    if (n == 0) return -1;
    ui_banner("  Press a number to choose");
    for (;;) {
        ch = getch();
        if (ch >= '1' && ch <= '9' && ch - '1' < n) return slot[ch - '1'];
    }
}

/* Choose a Spell to cast (or to give up).  battle_only splits the two
 * casting moments: some Spells only work with a creature in front of you. */
int ui_pick_spell(Player *p, const char *title, int battle_only)
{
    int i, ch, n = 0, slot[MAX_SPELLS];

    if (p->nspells <= 0) return -1;
    erase();
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(0, 0, "%-*.*s", COLS - 1, COLS - 1, title);
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(2, 2, "%s the %s knows:", p->name, p->cls);
    for (i = 0; i < p->nspells; i++) {
        const Spell *sp = &spell_proto[p->spells[i]];
        if (battle_only >= 0 && (SPELL_IN_BATTLE(sp->kind) != battle_only)) continue;
        mvprintw(4 + n, 4, "[%d] %-15s %s", n + 1, sp->name, sp->text);
        slot[n++] = i;
    }
    if (n == 0) return -1;
    mvprintw(6 + n, 2, "[0] cast nothing");
    ui_banner("  Press a number");
    for (;;) {
        ch = getch();
        if (ch == '0') return -1;
        if (ch >= '1' && ch <= '9' && ch - '1' < n) return slot[ch - '1'];
    }
}

/* A shop screen: wares with prices and stock, plus any services offered. */
int ui_shop(Player *p, const char *title, const ShopLine *lines, int n)
{
    int i, ch, row;

    erase();
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(0, 0, "%-*.*s", COLS - 1, COLS - 1, title);
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(2, 2, "%s the %s -- %d Gold, %d of %d Object slots used, %d Life",
             p->name, p->cls, p->gold, item_objects(p), item_limit(p), p->lives);

    row = 4;
    for (i = 0; i < n && row < LINES - 3; i++) {
        int poor = lines[i].price > p->gold;
        int gone = lines[i].avail == 0;
        if (poor || gone) attron(A_DIM);
        mvprintw(row, 4, "[%c] %-15s %2dg  %-22s %s",
                 lines[i].key, lines[i].label, lines[i].price,
                 lines[i].note ? lines[i].note : "",
                 gone ? "(sold out)" : (poor ? "(too dear)" : ""));
        if (poor || gone) attroff(A_DIM);
        row++;
    }
    mvprintw(row + 1, 4, "[0] leave");
    ui_banner("  Choose, or 0 to leave");

    for (;;) {
        ch = getch();
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        if (ch == '0') return 0;
        for (i = 0; i < n; i++)
            if (ch == lines[i].key && lines[i].price <= p->gold && lines[i].avail != 0)
                return ch;
    }
}

/* The trophy pile the physical game keeps face-down in front of you. */
void ui_sheet(Player *p)
{
    int i, j, row, done[MAX_KILLS], ndone = 0, power = 0;

    erase();
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(0, 0, "%-*.*s", COLS - 1, COLS - 1, "  CHARACTER SHEET");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);

    attron(A_BOLD);
    mvprintw(2, 2, "%s the %s", p->name, p->cls);
    if (p->toad > 0)
        printw("  -- A TOAD for %d more %s", p->toad, p->toad == 1 ? "Turn" : "Turns");
    attroff(A_BOLD);
    mvprintw(3, 2, "Strength %-3d  Craft %-3d  Lives %d/%d  Fate %d/%d  Gold %d",
             eff_str(p), eff_craft(p), p->lives, eff_maxlives(p),
             p->fate, eff_maxfate(p), p->gold);
    mvprintw(4, 2, "  (base %d / %d -- the rest comes from what you carry)",
             p->base_str, p->base_craft);

    {
        int t;
        for (t = 0; t < char_count; t++)
            if (!strcmp(char_tbl[t].cls, p->cls)) {
                attron(A_BOLD);
                mvprintw(5, 2, "Power: %s", char_tbl[t].power);
                attroff(A_BOLD);
                break;
            }
    }
    mvprintw(6, 2, "Talisman: %-4s   Trophy points: %d Strength, %d Craft  (7 buys +1)",
             p->talisman ? "yes" : "no", p->troph_str, p->troph_craft);
    /* Row 7 was free, and this must stay inside the 86-column minimum. */
    if (henchmen_on && p->hench.ct >= 0 && p->hench.lives > 0) {
        const CharTemplate *h = &char_tbl[p->hench.ct];
        mvprintw(7, 2, "Henchman: %-12s Str %-2d Craft %-2d %d %s (base values only)",
                 h->cls, h->str, h->craft, p->hench.lives,
                 p->hench.lives == 1 ? "Life " : "Lives");
    }

    row = 8;
    attron(A_BOLD);
    mvprintw(row++, 2, "Carried -- %d of %d Object slots used (Followers are free)",
             item_objects(p), item_limit(p));
    attroff(A_BOLD);
    if (p->nitems == 0) mvprintw(row++, 4, "(nothing)");
    for (i = 0; i < p->nitems && row < LINES - 4; i++) {
        const Card *c = &deck_proto[p->carried[i]];
        mvprintw(row++, 4, "%-16s %s", c->name, c->text ? c->text : "");
    }

    row++;
    attron(A_BOLD);
    mvprintw(row++, 2, "Spells -- %d of %d allowed at Craft %d",
             p->nspells, spell_limit(p), eff_craft(p));
    attroff(A_BOLD);
    if (p->nspells == 0) mvprintw(row++, 4, "(none)");
    for (i = 0; i < p->nspells && row < LINES - 6; i++)
        mvprintw(row++, 4, "%-15s %s", spell_proto[p->spells[i]].name,
                 spell_proto[p->spells[i]].text);
    row++;
    for (i = 0; i < p->nkills; i++) power += deck_proto[p->kills[i]].power;
    attron(A_BOLD);
    mvprintw(row++, 2, "Trophy pile -- %d slain, %d points earned in total",
             p->nkills, power);
    attroff(A_BOLD);
    if (p->nkills == 0) mvprintw(row++, 4, "(nothing yet)");

    for (i = 0; i < p->nkills && row < LINES - 3; i++) {
        int ci = p->kills[i], n = 0, seen = 0;
        for (j = 0; j < ndone; j++) if (done[j] == ci) seen = 1;
        if (seen) continue;
        if (ndone < MAX_KILLS) done[ndone++] = ci;
        for (j = 0; j < p->nkills; j++) if (p->kills[j] == ci) n++;
        mvprintw(row++, 4, "%2d x %-14s (%s %d)", n, deck_proto[ci].name,
                 deck_proto[ci].craftfight ? "Craft" : "Str", deck_proto[ci].power);
    }

    ui_banner("  [press a key to go back]");
    getch();
}

void ui_pause(const char *msg)
{
    char buf[BOARD_W + 1];

    snprintf(buf, sizeof buf, "%s  [press a key]", msg);
    ui_banner(buf);
    getch();
}
