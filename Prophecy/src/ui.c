#include "prophecy.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CW         13                      /* cell interior width  */
#define CH          2                      /* cell interior height */
#define BOARD_TOP   1
#define BOARD_W    (GRID * (CW + 1) + 1)    /* 85 */
#define BOARD_H    (GRID * (CH + 1) + 1)    /* 19 */
#define ROW_STAT   (BOARD_TOP + BOARD_H)    /* 20..22 */
#define ROW_LOG    (ROW_STAT + 3)           /* 23..27 */
#define PANEL_X    (BOARD_W + 1)
#define PANEL_MIN  13
/* The panel used to be a fixed thirteen columns, so a 140-column terminal
 * wasted forty of them.  It now takes whatever is spare, and once it is
 * wide enough it stops printing "@1 0 of 4" and starts naming things --
 * which Race a character is, and which Artifacts she actually holds. */
static int panel_w(void)
{
    int w = COLS - PANEL_X - 1;
    if (w < PANEL_MIN) w = PANEL_MIN;
    if (w > 46) w = 46;
    return w;
}
#define PANEL_W    panel_w()
#define LOG_KEEP   64
#define LOG_MIN     5                       /* what 29 rows can spare */
#define UI_MIN_COLS 86
#define UI_MIN_ROWS 29

/* The layout used to be pinned to the 29-row minimum, so every row past the
 * twenty-ninth was dead space -- eleven wasted lines on a 40-row terminal.
 * The board is a fixed size, but the log has no natural height, so it takes
 * whatever the window gives it and the prompt sits on the last row. */
static int log_rows(void)
{
    int n = LINES - 1 - ROW_LOG;
    if (n < LOG_MIN)  n = LOG_MIN;
    if (n > LOG_KEEP) n = LOG_KEEP;
    return n;
}
#define ROW_PROMPT (ROW_LOG + log_rows())

enum { P_WILD = 1, P_FOREST, P_MOUNT, P_CIV, P_GUILD, P_ENCH, P_PLANE,
       P_FRAME, P_TITLE, P_DIM, P_P1 = 11 };

static char  logbuf[LOG_KEEP][BOARD_W + 1];
static int   log_n;
static const char *shotpath;
static FILE *tracefp;
static const char *logpath;

void ui_set_log(const char *path) { logpath = path; }
static void ui_shot(void);

void ui_init(void)
{
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0);
    shotpath = getenv("PROPHECY_SHOT");
    { const char *tr = logpath ? logpath : getenv("PROPHECY_TRACE");
      if (tr && *tr) tracefp = (tr[0]==0x2d && !tr[1]) ? stderr : fopen(tr, "w"); }
    if (has_colors()) {
        start_color(); use_default_colors();
        init_pair(P_WILD,   COLOR_GREEN,   -1);
        init_pair(P_FOREST, COLOR_GREEN,   -1);
        init_pair(P_MOUNT,  COLOR_WHITE,   -1);
        init_pair(P_CIV,    COLOR_CYAN,    -1);
        init_pair(P_GUILD,  COLOR_BLUE,    -1);
        init_pair(P_ENCH,   COLOR_MAGENTA, -1);
        init_pair(P_PLANE,  COLOR_RED,     -1);
        init_pair(P_FRAME,  COLOR_BLUE,    -1);
        init_pair(P_TITLE,  COLOR_WHITE,   COLOR_BLUE);
        init_pair(P_DIM,    COLOR_WHITE,   -1);
        init_pair(P_P1 + 0, COLOR_WHITE,   -1);
        init_pair(P_P1 + 1, COLOR_CYAN,    -1);
        init_pair(P_P1 + 2, COLOR_MAGENTA, -1);
        init_pair(P_P1 + 3, COLOR_YELLOW,  -1);
        init_pair(P_P1 + 4, COLOR_GREEN,   -1);
    }
}

void ui_end(void) { curs_set(1); endwin(); }

int ai_why;

/* Only ever to the trace file: the on-screen log has a few lines and this
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
    va_list ap; int i;
    if (log_n == LOG_KEEP) {
        for (i = 1; i < LOG_KEEP; i++) memcpy(logbuf[i-1], logbuf[i], sizeof logbuf[0]);
        log_n--;
    }
    va_start(ap, fmt);
    vsnprintf(logbuf[log_n], sizeof logbuf[0], fmt, ap);
    va_end(ap);
    if (tracefp) { fprintf(tracefp, "%s\n", logbuf[log_n]); fflush(tracefp); }
    log_n++;
}

static int kind_pair(SpaceKind k)
{
    switch (k) {
    case SP_PLAINS:      return P_WILD;
    case SP_FOREST:      return P_FOREST;
    case SP_MOUNTAINS:   return P_MOUNT;
    case SP_VILLAGE:
    case SP_CITY:        return P_CIV;
    case SP_WILDERNESS:  return P_ENCH;
    default:             return P_GUILD;      /* the five guilds */
    }
}

/* Only the ring is a grid; the middle of the world is open sea. */
static void draw_ring(void)
{
    int i, r, c, y, x, n, k;

    for (i = 0; i < RING_N; i++) {
        const Space *sp = &ring[i];
        int pair = kind_pair(sp->kind);
        ring_cell(i, &r, &c);
        y = BOARD_TOP + r * (CH + 1);
        x = c * (CW + 1);

        attron(COLOR_PAIR(P_FRAME));
        mvhline(y, x, ACS_HLINE, CW + 2);
        mvhline(y + CH + 1, x, ACS_HLINE, CW + 2);
        mvvline(y, x, ACS_VLINE, CH + 2);
        mvvline(y, x + CW + 1, ACS_VLINE, CH + 2);
        mvaddch(y, x, ACS_PLUS); mvaddch(y, x + CW + 1, ACS_PLUS);
        mvaddch(y + CH + 1, x, ACS_PLUS); mvaddch(y + CH + 1, x + CW + 1, ACS_PLUS);
        attroff(COLOR_PAIR(P_FRAME));

        attron(COLOR_PAIR(pair));
        if (IS_GUILD(sp->kind) || sp->kind == SP_CITY || sp->kind == SP_VILLAGE) attron(A_BOLD);
        mvprintw(y + 1, x + 1, "%-*.*s", CW, CW, sp->name);
        attroff(A_BOLD); attroff(COLOR_PAIR(pair));

        /* second line: who is here (left) and what this place offers (right) */
        mvprintw(y + 2, x + 1, "%-*s", CW, "");
        {
            /* Fill from the right: first the fixed terrain, then whatever
             * is waiting here -- a space with something on it must say so. */
            int gx = x + CW, k2;
            if (sp->safe) { attron(COLOR_PAIR(P_CIV) | A_BOLD); mvaddch(y + 2, gx--, '+'); attroff(COLOR_PAIR(P_CIV) | A_BOLD); }
            if (sp->gate) { attron(COLOR_PAIR(P_ENCH) | A_BOLD); mvaddch(y + 2, gx--, '%'); attroff(COLOR_PAIR(P_ENCH) | A_BOLD); }
            if (sp->port) { attron(COLOR_PAIR(P_CIV)); mvaddch(y + 2, gx--, '~'); attroff(COLOR_PAIR(P_CIV)); }

            if (space_item[i] >= 0 && gx > x + 3) {
                attron(COLOR_PAIR(P_CIV) | A_BOLD);
                mvaddch(y + 2, gx--, '*');
                attroff(COLOR_PAIR(P_CIV) | A_BOLD);
            }
            if (IS_GUILD(sp->kind) && guild_abil[guild_of(sp->kind)] >= 0 && gx > x + 3) {
                attron(COLOR_PAIR(P_GUILD) | A_BOLD);
                mvaddch(y + 2, gx--, '&');
                attroff(COLOR_PAIR(P_GUILD) | A_BOLD);
            }
            for (k2 = 0; k2 < space_nadv[i] && gx > x + 3; k2++) {
                int creature = adv_proto[space_adv[i][k2]].type == A_CREATURE;
                attron(COLOR_PAIR(creature ? P_PLANE : P_WILD) | A_BOLD);
                mvaddch(y + 2, gx--, creature ? '!' : '?');
                attroff(COLOR_PAIR(creature ? P_PLANE : P_WILD) | A_BOLD);
            }
        }
        n = 0;
        for (k = 0; k < nplayers; k++) {
            if (!players[k].alive || players[k].idx != i) continue;
            if (n >= 4) break;                       /* the cell is 13 wide */
            attron(COLOR_PAIR(P_P1 + k) | A_BOLD);
            if (k == cur_player) attron(A_REVERSE);
            mvprintw(y + 2, x + 1 + n * 3, "@%d", k + 1);
            if (k == cur_player) attroff(A_REVERSE);
            attroff(COLOR_PAIR(P_P1 + k) | A_BOLD);
            n++;
        }
    }
}

/* The five Astral Planes float in the sea inside the ring, each one
 * outward of the landmark it guards. */
/* Each box sits toward the landmark its Plane lies beyond: Magic Tower on
 * the top edge, Monastery right, Fortress bottom-right, Mountains
 * bottom-left, Forest left.  dy 7 for the lower pair keeps them clear of
 * the ring's own border. */
static const struct { int dx, dy; } plane_pos[PLANES] = {
    { 21, 0 }, { 41, 3 }, { 33, 7 }, { 8, 7 }, { 0, 3 }
};

static void draw_planes(void)
{
    int ix = CW + 2, iy = BOARD_TOP + (CH + 1) + 1;   /* inside the ring */
    int p, y, x, i;

    for (p = 0; p < PLANES; p++) {
        char state[16];
        y = iy + plane_pos[p].dy;
        x = ix + plane_pos[p].dx;
        attron(COLOR_PAIR(P_PLANE) | (planes[p].closed ? A_DIM : A_BOLD));
        mvprintw(y,     x, "+-----------+");
        mvprintw(y + 1, x, "|%-11.11s|", plane_name(p));
        snprintf(state, sizeof state, "  %c  %c   %c",
                 planes[p].lesser   ? 'L' : '.',
                 planes[p].greater  ? 'G' : '.',
                 planes[p].artifact ? 'A' : '-');
        mvprintw(y + 2, x, "|%-11.11s|", state);
        mvprintw(y + 3, x, "+-----------+");
        attroff(COLOR_PAIR(P_PLANE) | (planes[p].closed ? A_DIM : A_BOLD));
        (void)i;
    }
    attron(COLOR_PAIR(P_ENCH) | A_BOLD);
    mvprintw(iy + 5, ix + 20, "P R O P H E C Y");
    attroff(COLOR_PAIR(P_ENCH) | A_BOLD);
    attron(COLOR_PAIR(P_DIM) | A_DIM);
    mvprintw(iy + 6, ix + 17, "four Artifacts to win");
    attroff(COLOR_PAIR(P_DIM) | A_DIM);
}

static const struct { const char *ch; const char *what; int pair; } legend[] = {
    { "!",  "creature", P_PLANE },
    { "?",  "chance",   P_WILD  },
    { "&",  "ability",  P_GUILD },
    { "*",  "for sale", P_CIV   },
    { "~",  "port",     P_CIV   },
    { "%",  "gate",     P_ENCH  },
    { "+",  "safe",     P_CIV   },
    { "L",  "lesser",   P_PLANE },
    { "G",  "greater",  P_PLANE },
    { "A",  "artifact", P_PLANE },
};

static void draw_panel(void)
{
    int i, n = (int)(sizeof legend / sizeof legend[0]);
    int w = PANEL_W;

    if (COLS < PANEL_X + PANEL_MIN) return;
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(BOARD_TOP, PANEL_X, "%-*.*s", PANEL_W, PANEL_W, " THE WORLD");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    for (i = 0; i < n; i++) {
        mvprintw(BOARD_TOP + 1 + i, PANEL_X, "%-*s", PANEL_W, "");
        attron(COLOR_PAIR(legend[i].pair) | A_BOLD);
        mvprintw(BOARD_TOP + 1 + i, PANEL_X + 1, "%-2s", legend[i].ch);
        attroff(COLOR_PAIR(legend[i].pair) | A_BOLD);
        mvprintw(BOARD_TOP + 1 + i, PANEL_X + 4, "%-8.8s", legend[i].what);
    }
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(BOARD_TOP + 12, PANEL_X, "%-*.*s", PANEL_W, PANEL_W, " ARTIFACTS");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    for (i = 0; i < nplayers; i++) {
        const Player *p = &players[i];
        int row = BOARD_TOP + 13 + i;
        mvprintw(row, PANEL_X, "%-*s", w, "");
        attron(COLOR_PAIR(P_P1 + i) | A_BOLD);
        mvprintw(row, PANEL_X + 1, "@%d %d/%d", i + 1, p->artifacts, WIN_ARTIFACTS);
        attroff(COLOR_PAIR(P_P1 + i) | A_BOLD);
        if (w >= 24) {
            /* room enough to say who she is and what she carries */
            char buf[64];
            const char *race = (p->race >= 0) ? race_proto[p->race].name : "";
            snprintf(buf, sizeof buf, "%-11.11s %s", p->cls, race);
            mvprintw(row, PANEL_X + 8, "%-*.*s", w - 9, w - 9, buf);
        }
    }
    if (w >= 30) {                       /* the Artifacts themselves */
        int row = BOARD_TOP + 13 + nplayers + 1, k, any = 0;
        attron(COLOR_PAIR(P_TITLE) | A_BOLD);
        mvprintw(row, PANEL_X, "%-*.*s", w, w, " RELICS HELD");
        attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
        for (i = 0; i < nplayers && row + 1 + any < ROW_STAT; i++)
            for (k = 0; k < ART_N; k++) {
                if (!((players[i].art_mask >> k) & 1u)) continue;
                if (row + 1 + any >= ROW_STAT) break;
                mvprintw(row + 1 + any, PANEL_X, "%-*s", w, "");
                attron(COLOR_PAIR(P_P1 + i) | A_BOLD);
                mvprintw(row + 1 + any, PANEL_X + 1, "@%d", i + 1);
                attroff(COLOR_PAIR(P_P1 + i) | A_BOLD);
                mvprintw(row + 1 + any, PANEL_X + 4, "%-*.*s",
                         w - 5, w - 5, art_proto[k].name);
                any++;
            }
        while (row + 1 + any < ROW_STAT) {
            mvprintw(row + 1 + any, PANEL_X, "%-*s", w, "");
            any++;
        }
    }
}

static void draw_status(void)
{
    int i, colw = (COLS - 3) / 2;

    if (colw > 48) colw = 48;
    for (i = 0; i < nplayers; i++) {
        Player *p = &players[i];
        char buf[80];
        int y = ROW_STAT + i / 2, x = (i % 2) * (colw + 2);
        /* A damaged Item is face-down and gives nothing, so the count of
         * broken gear has to be visible or a player cannot tell why she
         * has suddenly got weaker. */
        char kit[12] = "";
        int j, broke = 0;
        for (j = 0; j < p->nitems; j++) if (p->dmg[j]) broke++;
        if (p->nitems) snprintf(kit, sizeof kit, " I%d", p->nitems);
        if (broke)     snprintf(kit + strlen(kit), sizeof kit - strlen(kit), "!%d", broke);
        /* Race goes on the status line rather than the side panel, because
         * the panel only has room to name things past about 110 columns and
         * the status line spans the whole width at any size. */
        {
            const char *r = (p->race >= 0) ? race_proto[p->race].name : NULL;
            char who[20];
            if (r) snprintf(who, sizeof who, "%.5s/%.4s", p->cls, r);
            else   snprintf(who, sizeof who, "%.9s", p->cls);
            snprintf(buf, sizeof buf, "%d %-10.10s S%d/%-2d W%d/%-2d G%-2d X%-2d A%d%s %s",
                     i + 1, who, p->str_now, p->str_now + p->str_lost,
                     p->will_now, p->will_now + p->will_lost,
                     p->gold, p->exp, p->artifacts, kit,
                     p->alive ? (p->ai ? "cpu" : "you") : "DEAD");
        }
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
        move(ROW_LOG + i, 0); clrtoeol();
        if (first + i < log_n) {
            if (first + i == log_n - 1) attron(A_BOLD);
            mvprintw(ROW_LOG + i, 0, "%-*.*s", BOARD_W, BOARD_W, logbuf[first + i]);
            if (first + i == log_n - 1) attroff(A_BOLD);
        }
    }
}

void ui_draw(void)
{
    erase();
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(0, 0, "%-*.*s", BOARD_W, BOARD_W,
             "  P R O P H E C Y   -   walk the world, take four Artifacts, take the throne  ");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    draw_ring();
    draw_planes();
    draw_panel();
    draw_status();
    draw_log();
    refresh();
    ui_shot();
}

void ui_banner(const char *msg)
{
    move(ROW_PROMPT, 0); clrtoeol();
    attron(A_BOLD);
    {   /* use the whole screen: clipping to the board hid the keys */
        int w = COLS - 1;
        if (w > (int)sizeof(char) * 200) w = 200;
        mvprintw(ROW_PROMPT, 0, "%-*.*s", w, w, msg);
    }
    attroff(A_BOLD);
    refresh();
    ui_shot();
}

/* Wait up to ms for a key; ms < 0 blocks, 0 polls. */
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


/* Everything the game can do, from inside the game.  A switch that lives
 * only in a README may as well not exist. */
static void ui_draw_help(void);
void ui_shot_help(void) { ui_draw_help(); ui_shot(); }

static void ui_draw_help(void)
{
    static const char *lines[] = {
    "  P R O P H E C Y   --   keys and switches",
    "",
    "  AT ANY PROMPT",
    "    ?          this page",
    "    space      take the offered action / continue",
    "    q          leave the game",
    "",
    "  ON YOUR TURN",
    "    a  d       walk one space, anticlockwise or clockwise",
    "    z  x       ride two spaces, for a Gold",
    "    b          take ship from a Port      g   the Magic Gate, for 2 Gold",
    "    w          labour here instead of moving",
    "    c          cast a Spell instead of moving",
    "    p          assault the Astral Plane beyond this space",
    "",
    "  WATCHING (--watch)",
    "    space      pause and unpause      +  -   faster / slower",
    "",
    "  STARTING THE GAME",
    "    The first screen chooses the game -- standard, apocalypse or team",
    "    play -- and offers [w] to watch.  These do the same from the shell:",
    "    prophecy --watch         every hero played by the computer",
    "    prophecy --apocalypse    the longer variant: the world starts to die",
    "    prophecy --teams         four players as two pairs; crown one, both win",
    "    prophecy --log FILE      write the whole game out as text",
    "",
    "  THE SAME THINGS AS ENVIRONMENT",
    "    PROPHECY_SEED=n    replay a game exactly; the seed is printed at the top",
    "    PROPHECY_DELAY=ms  pace the computer's turns (0 for as fast as possible)",
    "    PROPHECY_AUTO=1    headless: no screen, prints one result line",
    "    PROPHECY_TRACE=f   as --log",
    "    PROPHECY_CHECK=1   assert the rules invariants; anything printed is a bug",
    "    PROPHECY_SHOT=f    append every frame to a file",
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

/* The prompt sits on a computed row, so anything drawing a full screen must
 * stay above it -- ui_banner() clears that row before every prompt. */
int ui_prompt_row(void) { return ROW_PROMPT; }

/* Dragged too small to draw the world.  Say what is needed and what there
 * is, rather than painting something unreadable. */
int ui_too_small(void)
{
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    if (rows >= UI_MIN_ROWS && cols >= UI_MIN_COLS) return 0;
    erase();
    mvprintw(0, 0, "The world needs %dx%d.", UI_MIN_COLS, UI_MIN_ROWS);
    mvprintw(1, 0, "This window is %dx%d.", cols, rows);
    mvprintw(2, 0, "Make it bigger to play.");
    refresh();
    return 1;
}

/* Dump the current screen on demand, for screens that draw themselves
 * without going through ui_draw(). */
void ui_snapshot(void) { ui_shot(); }

int ui_prompt(const char *msg, const char *valid)
{
    int ch;
    for (;;) {
        ui_banner(msg);
        ch = getch();
        if (ch == 0 || ch == ERR) continue;
        /* The window changed shape: the log takes its height from the
         * terminal, so everything has to be laid out again. */
        if (ch == KEY_RESIZE) {
            while (ui_too_small()) {
                ch = getch();
                if (ch != KEY_RESIZE && ch != ERR) break;
            }
            ui_draw();
            continue;
        }
        if (ch == '?') { ui_help(); ui_draw(); continue; }   /* always available */
        if (ch >= 'A' && ch <= 'Z') ch += 32;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) ch = ' ';
        if (strchr(valid, ch)) return ch;
    }
}

static void ui_shot(void)
{
    FILE *f; int y, x, cy, cx;
    if (!shotpath || !*shotpath) return;
    if (!(f = fopen(shotpath, "a"))) return;
    getyx(stdscr, cy, cx);
    fprintf(f, "--8<--\n");
    for (y = 0; y < LINES; y++) {
        for (x = 0; x < COLS; x++) {
            chtype cc = mvinch(y, x);
            int c = (int)(cc & A_CHARTEXT);
            if (cc & A_ALTCHARSET) c = (c == 'q') ? '-' : (c == 'x') ? '|' : '+';
            fputc(c, f);
        }
        fputc('\n', f);
    }
    fclose(f);
    move(cy, cx);
}
