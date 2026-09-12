#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "djarhun.h"

#define W_MIN 100
#define H_MIN 30

#define LOG_KEEP 64
#define LOG_SHOW 4

static char logbuf[LOG_KEEP][160];
static int  log_n;
static FILE *tracefp;
static const char *logpath;

/* Colour matters here: five lands, and a hero needs to know at a glance
 * which one she is looking at. */
enum { P_TITLE = 1, P_FROST, P_TARRI, P_DURACH, P_ALDUN, P_ABYSS, P_URTHE,
       P_H1, P_H2, P_H3, P_H4, P_DIM, P_FOE, P_LOOT };

/* Three ways of drawing the same board.  The rules do not care which, so
 * the player picks with [v] and the engine never knows. */
int view_mode = 4;   /* 1 whole ring, 2 viewport, 3 ribbons, 4 chart+panel */
static char lab_die[16];
static int  die_sides, die_rolled, die_show;

void ui_dice_clear(void) { die_show = 0; }

void ui_die(const char *label, int sides, int rolled)
{
    snprintf(lab_die, sizeof lab_die, "%s", label ? label : "");
    die_sides = sides; die_rolled = rolled; die_show = 1;
}

void ui_set_log(const char *path) { logpath = path; }

/* DJARHUN_SHOT=<file> appends the whole screen after every redraw, the way
 * the other two games do.  A board you cannot capture is a board you cannot
 * check. */
static const char *shotpath;

static void ui_shot(void)
{
    FILE *f;
    int y, x;

    if (!shotpath || !*shotpath) return;
    if (!(f = fopen(shotpath, "a"))) return;
    fprintf(f, "--8<--\n");
    for (y = 0; y < LINES; y++) {
        for (x = 0; x < COLS; x++) {
            chtype cc = mvinch(y, x);
            int ch = (int)(cc & A_CHARTEXT);
            fputc((ch >= 32 && ch < 127) ? ch : ' ', f);
        }
        fputc('\n', f);
    }
    fclose(f);
}

int ai_why;
int checking;

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
    va_list ap;
    if (log_n >= LOG_KEEP) {
        memmove(logbuf[0], logbuf[1], sizeof logbuf - sizeof logbuf[0]);
        log_n = LOG_KEEP - 1;
    }
    va_start(ap, fmt);
    vsnprintf(logbuf[log_n], sizeof logbuf[0], fmt, ap);
    va_end(ap);
    if (tracefp) { fprintf(tracefp, "%s\n", logbuf[log_n]); fflush(tracefp); }
    log_n++;
}

static int land_pair(int land)
{
    switch (land) {
    case LAND_FROST:  return P_FROST;
    case LAND_TARRI:  return P_TARRI;
    case LAND_DURACH: return P_DURACH;
    case LAND_ALDUN:  return P_ALDUN;
    case LAND_ABYSS:  return P_ABYSS;
    default:          return P_URTHE;
    }
}

void ui_init(void)
{
    const char *tr = logpath ? logpath : getenv("DJARHUN_TRACE");
    if (tr && *tr) tracefp = fopen(tr, "w");
    shotpath = getenv("DJARHUN_SHOT");

    initscr(); cbreak(); noecho(); curs_set(0); keypad(stdscr, TRUE);
    if (has_colors()) {
        start_color(); use_default_colors();
        init_pair(P_TITLE,  COLOR_BLACK,   COLOR_YELLOW);
        init_pair(P_FROST,  COLOR_CYAN,    -1);
        init_pair(P_TARRI,  COLOR_BLUE,    -1);
        init_pair(P_DURACH, COLOR_GREEN,   -1);
        init_pair(P_ALDUN,  COLOR_YELLOW,  -1);
        init_pair(P_ABYSS,  COLOR_RED,     -1);
        init_pair(P_URTHE,  COLOR_MAGENTA, -1);
        init_pair(P_H1,     COLOR_WHITE,   -1);
        init_pair(P_H2,     COLOR_CYAN,    -1);
        init_pair(P_H3,     COLOR_GREEN,   -1);
        init_pair(P_H4,     COLOR_MAGENTA, -1);
        init_pair(P_DIM,    COLOR_BLACK,   -1);
        init_pair(P_FOE,    COLOR_RED,     -1);
        init_pair(P_LOOT,   COLOR_YELLOW,  -1);
    }
    if (COLS < W_MIN || LINES < H_MIN) {
        endwin();
        fprintf(stderr, "Djarhun needs a terminal of at least %dx%d. Yours is %dx%d.\n",
                W_MIN, H_MIN, COLS, LINES);
        exit(1);
    }
}

void ui_end(void) { endwin(); if (tracefp) fclose(tracefp); }

/* What is standing on a space: a hero token, a foe, or loot. */
static void mark_of(int land, int idx, char *out, int n)
{
    int i, k = 0;
    out[0] = 0;
    for (i = 0; i < nheroes && k < n - 3; i++)
        if (heroes[i].alive && heroes[i].land == land && heroes[i].idx == idx)
            k += snprintf(out + k, n - k, "@%d", i + 1);
    {
        int sid = space_id(land, idx), j;
        for (j = 0; j < space_ncard[sid] && k < n - 2; j++) {
            const Card *c = &card_proto[space_card[sid][j]];
            k += snprintf(out + k, n - k, "%s", c->type == C_FOE ? "!" : "$");
        }
    }
}

/* ------------------------------------------------------- view 1: whole --
 * The land's whole ring on a rectangle, at eight characters a cell.  You
 * see the shape of the land; the names pay for it. */
static void draw_whole(int land, int top)
{
    int n = land_len(land), cols, rows, i, r, c;
    int best = 0;
    /* smallest rectangle whose perimeter holds the ring */
    for (cols = 4; cols <= 12; cols++) {
        rows = (n + 4 - 2 * cols) / 2;
        if (rows >= 3 && 2 * cols + 2 * rows - 4 >= n) { best = cols; break; }
    }
    if (!best) best = 12;
    cols = best;
    rows = (n + 4 - 2 * cols) / 2;
    if (rows < 3) rows = 3;

    i = 0;
    for (c = 0; c < cols && i < n; c++, i++)                  /* top edge */
        { r = 0; goto place; place: ; {
            char m[8]; const Space *sp = space_at(land, i);
            mark_of(land, i, m, sizeof m);
            attron(COLOR_PAIR(land_pair(land)));
            mvprintw(top + r, c * 9, "%-8.8s", sp->name);
            attroff(COLOR_PAIR(land_pair(land)));
            if (*m) { attron(A_BOLD); mvprintw(top + r, c * 9 + 8 - (int)strlen(m), "%s", m); attroff(A_BOLD); }
        } }
    for (r = 1; r < rows && i < n; r++, i++) {                /* right    */
        char m[8]; const Space *sp = space_at(land, i);
        mark_of(land, i, m, sizeof m);
        attron(COLOR_PAIR(land_pair(land)));
        mvprintw(top + r, (cols - 1) * 9, "%-8.8s", sp->name);
        attroff(COLOR_PAIR(land_pair(land)));
        if (*m) { attron(A_BOLD); mvprintw(top + r, (cols - 1) * 9 + 8 - (int)strlen(m), "%s", m); attroff(A_BOLD); }
    }
    for (c = cols - 2; c >= 0 && i < n; c--, i++) {           /* bottom   */
        char m[8]; const Space *sp = space_at(land, i);
        mark_of(land, i, m, sizeof m);
        attron(COLOR_PAIR(land_pair(land)));
        mvprintw(top + rows - 1, c * 9, "%-8.8s", sp->name);
        attroff(COLOR_PAIR(land_pair(land)));
        if (*m) { attron(A_BOLD); mvprintw(top + rows - 1, c * 9 + 8 - (int)strlen(m), "%s", m); attroff(A_BOLD); }
    }
    for (r = rows - 2; r >= 1 && i < n; r--, i++) {           /* left     */
        char m[8]; const Space *sp = space_at(land, i);
        mark_of(land, i, m, sizeof m);
        attron(COLOR_PAIR(land_pair(land)));
        mvprintw(top + r, 0, "%-8.8s", sp->name);
        attroff(COLOR_PAIR(land_pair(land)));
        if (*m) { attron(A_BOLD); mvprintw(top + r, 8 - (int)strlen(m), "%s", m); attroff(A_BOLD); }
    }
}

/* ---------------------------------------------------- view 2: viewport --
 * Seven spaces of the ring at full width, centred on whoever is playing.
 * A d12 moves you twelve, so this is most of a turn either way. */
static void draw_viewport(int land, int here, int top)
{
    const int cw = 11, n = 7;
    int k, x, len = land_len(land);

    mvprintw(top, 2, "<<");
    mvprintw(top, 4 + n * (cw + 1), ">>");
    for (k = 0; k < n; k++) {
        x = 4 + k * (cw + 1);
        mvprintw(top,     x, "+");  mvhline(top,     x + 1, '-', cw);
        mvprintw(top + 3, x, "+");  mvhline(top + 3, x + 1, '-', cw);
    }
    mvprintw(top, 4 + n * (cw + 1), "+"); mvprintw(top + 3, 4 + n * (cw + 1), "+");

    for (k = 0; k < n; k++) {
        int idx = ((here - n / 2 + k) % len + len) % len;
        const Space *sp = space_at(land, idx);
        char m[8];
        mark_of(land, idx, m, sizeof m);
        x = 4 + k * (cw + 1);
        mvprintw(top + 1, x, "|"); mvprintw(top + 2, x, "|");
        attron(COLOR_PAIR(land_pair(land)));
        if (k == n / 2) attron(A_BOLD | A_REVERSE);
        mvprintw(top + 1, x + 1, "%-*.*s", cw, cw, sp->name);
        if (k == n / 2) attroff(A_BOLD | A_REVERSE);
        attroff(COLOR_PAIR(land_pair(land)));
        mvprintw(top + 2, x + 1, "%-*.*s", cw, cw, "");
        if (*m) { attron(A_BOLD); mvprintw(top + 2, x + 2, "%s", m); attroff(A_BOLD); }
    }
    mvprintw(top + 1, 4 + n * (cw + 1), "|");
    mvprintw(top + 2, 4 + n * (cw + 1), "|");

    /* where you are in the ring, without spending name width on it */
    {
        int bar = 60, i, pos = here * bar / len;
        mvprintw(top + 4, 6, "%-12.12s [", land_tbl[land].name);
        for (i = 0; i < bar; i++) addch(i == pos ? '@' : '.');
        addch(']');
    }
}

/* ----------------------------------------------------- view 3: ribbons --
 * Every land at once, one character a space.  Nothing is hidden and
 * nothing is legible; the line underneath says where you actually are. */
static void draw_ribbons(int here_land, int here, int top)
{
    int land, row = top;
    for (land = 0; land < LAND_COUNT; land++) {
        int n = land_len(land), i;
        mvprintw(row, 2, "%-13.13s", land_tbl[land].name);
        attron(COLOR_PAIR(land_pair(land)));
        for (i = 0; i < n && 16 + i < COLS - 2; i++) {
            char m[8];
            mark_of(land, i, m, sizeof m);
            if (land == here_land && i == here) { attron(A_BOLD | A_REVERSE); addch('@'); attroff(A_BOLD | A_REVERSE); }
            else if (m[0] == '@') { attron(A_BOLD); addch(m[1]); attroff(A_BOLD); }
            else if (m[0] == '!') addch('!');
            else if (m[0] == '$') addch('$');
            else                  addch('.');
        }
        attroff(COLOR_PAIR(land_pair(land)));
        row++;
    }
    {
        const Space *sp = space_at(here_land, here);
        int len = land_len(here_land);
        mvprintw(row + 1, 2, "You are on: %-14s   %s",
                 sp->name, sp->draw ? "draw cards" : "");
        mvprintw(row + 2, 2, "Neighbours: <- %-14s   %s ->",
                 space_at(here_land, (here - 1 + len) % len)->name,
                 space_at(here_land, (here + 1) % len)->name);
    }
}

/* ------------------------------------------------ view 4: chart + panel --
 * The shape of a land and the name of a space cannot share a cell: eleven
 * characters of "Demonblood" leaves no room for the ring, and a ring of
 * four-character cells leaves no room for the name.  So stop trying to put
 * them in the same place.  The chart carries the shape in two characters a
 * space, and the panel spells out wherever you are standing.
 *
 *      ..  nothing here        !!  a Foe waiting      $$  treasure
 *      Mk  a market or town    >>  a way to another land
 *      @n  a hero              **  more than one thing
 */
static void cell_code(int land, int idx, char *out)
{
    int sid = space_id(land, idx), i, foes = 0, loot = 0;
    const Space *sp = space_at(land, idx);

    for (i = 0; i < nheroes; i++)
        if (heroes[i].alive && heroes[i].land == land && heroes[i].idx == idx) {
            out[0] = '@'; out[1] = (char)('1' + i); out[2] = 0;
            return;
        }
    for (i = 0; i < space_ncard[sid]; i++) {
        if (card_proto[space_card[sid][i]].type == C_FOE) foes++;
        else                                              loot++;
    }

    if (foes && loot) { out[0] = out[1] = '*'; }
    else if (foes)    { out[0] = out[1] = '!'; }
    else if (loot)    { out[0] = out[1] = '$'; }
    else switch (sp->kind) {
    case SP_MARKET: case SP_ELIDOR: case SP_GYPSY:
        out[0] = 'M'; out[1] = 'k'; break;
    case SP_CROSSING: case SP_ABYSS_GATE:
        out[0] = out[1] = '>'; break;
    case SP_GHARAD:
        out[0] = 'G'; out[1] = '!'; break;
    case SP_LAKE:
        out[0] = 'L'; out[1] = 'k'; break;
    default:
        out[0] = out[1] = '.'; break;
    }
    out[2] = 0;
}

static void draw_chart(int land, int here, int top)
{
    int n = land_len(land), cols, rows, i, r, c, x, y;
    int pair = land_pair(land);

    /* A perimeter of n cells satisfies cols + rows = (n + 4) / 2, so the
     * shape is a single choice.  Height is the binding constraint -- there
     * are only about nine rows between the title and the roster -- so pick
     * the rows first and let the width follow. */
    {
        int half = (n + 4) / 2;
        rows = 9;
        if (half - rows < 4) rows = half - 4;
        if (rows < 3) rows = 3;
        cols = half - rows;
        if (cols < 3) cols = 3;
        if (cols > 12) { cols = 12; rows = half - cols; if (rows < 3) rows = 3; }
    }

    mvprintw(top, 2, "%-14.14s", land_tbl[land].name);
    for (i = 0; i < n; i++) {
        char code[4];
        /* walk the perimeter: across the top, down the right, back along
         * the bottom, up the left */
        if (i < cols)                  { r = 0;             c = i; }
        else if (i < cols + rows - 1)  { r = i - cols + 1;   c = cols - 1; }
        else if (i < 2 * cols + rows - 2) { r = rows - 1;    c = 2 * cols + rows - 3 - i; }
        else                           { r = 2 * cols + 2 * rows - 4 - i; c = 0; }
        if (r < 0 || r >= rows || c < 0 || c >= cols) continue;
        cell_code(land, i, code);
        y = top + 1 + r;        /* one line a row: the chart must fit */
        x = 3 + c * 5;
        attron(COLOR_PAIR(pair));
        mvprintw(y, x, "[%s]", code);
        attroff(COLOR_PAIR(pair));
        if (i == here) {
            attron(A_BOLD | A_REVERSE);
            mvprintw(y, x + 1, "%s", code);
            attroff(A_BOLD | A_REVERSE);
        }
    }
}

/* The panel: everything about the space you are standing on, in full. */
static void draw_inspect(int land, int here, int x, int top)
{
    const Space *sp = space_at(land, here);
    int sid = space_id(land, here), i, row;
    Hero *p = &heroes[cur_hero];

    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(top, x, "%-*.*s", COLS - x - 1, COLS - x - 1, " INSPECTION");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);

    row = top + 2;
    mvprintw(row++, x, "Space %d of %d", here + 1, land_len(land));
    attron(A_BOLD);
    mvprintw(row++, x, "%-.*s", COLS - x - 1, sp->name);
    attroff(A_BOLD);
    mvprintw(row++, x, "%-.*s", COLS - x - 1, land_tbl[land].name);
    row++;
    if (sp->draw)      mvprintw(row++, x, "Draw %d card%s", sp->draw, sp->draw > 1 ? "s" : "");
    if (sp->kind == SP_CROSSING || sp->kind == SP_ABYSS_GATE)
        mvprintw(row++, x, "-> %.14s", land_tbl[sp->to_land].name);
    if (sp->kind == SP_GHARAD)  mvprintw(row++, x, "Gharad: Str 20 Sor 20");
    if (sp->kind == SP_ELIDOR)  mvprintw(row++, x, "Where a Fair hero");
    if (sp->kind == SP_GYPSY)   mvprintw(row++, x, "Where a Vile hero");

    if (space_ncard[sid]) {
        row++;
        mvprintw(row++, x, "HERE:");
        for (i = 0; i < space_ncard[sid] && row < LINES - 9; i++) {
            const Card *c = &card_proto[space_card[sid][i]];
            attron(COLOR_PAIR(c->type == C_FOE ? P_FOE : P_LOOT));
            mvprintw(row++, x, " %-.*s", COLS - x - 2, c->name);
            attroff(COLOR_PAIR(c->type == C_FOE ? P_FOE : P_LOOT));
            if (c->type == C_FOE)
                mvprintw(row++, x, "  Str %d / Sor %d", c->str, c->sor);
        }
    }

    row = LINES - 8;
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(row++, x, "%-*.*s", COLS - x - 1, COLS - x - 1, " HERO");
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(row++, x, "%-.*s the %s", COLS - x - 6, p->name, p->race);
    mvprintw(row++, x, "Str %-3d Spd %-3d Sor %d", p->str, p->spd, p->sor);
    mvprintw(row++, x, "Health %d/%-3d Gems %d", p->health, max_health(p), p->gems);
    mvprintw(row++, x, "Level %-3d XP %d", p->level, p->xp);
    mvprintw(row++, x, "moves with a d%d", move_die(p->spd));
    if (die_show) {
        attron(A_BOLD);
        mvprintw(row++, x, "%-8.8s d%d -> %d", lab_die, die_sides, die_rolled);
        attroff(A_BOLD);
    }
    if (p->book) { attron(A_BOLD); mvprintw(row++, x, "CARRIES THE BOOK"); attroff(A_BOLD); }
}

static void draw_status(int row)
{
    int i;
    for (i = 0; i < nheroes; i++) {
        Hero *h = &heroes[i];
        attron(COLOR_PAIR(P_H1 + i));
        if (i == cur_hero && h->alive) attron(A_BOLD | A_REVERSE);
        mvprintw(row + i / 2, (i % 2) * 50,
                 "@%d %-8.8s S%-2d P%-2d C%-2d HP%d/%-2d G%-2d L%-2d %s%s",
                 i + 1, h->name, h->str, h->spd, h->sor,
                 h->health, max_health(h), h->gems, h->level,
                 h->morality == MOR_FAIR ? "Fair" :
                 h->morality == MOR_KIND ? "Kind" : "Vile",
                 h->book ? " BOOK" : (h->alive ? "" : " DEAD"));
        if (i == cur_hero && h->alive) attroff(A_BOLD | A_REVERSE);
        attroff(COLOR_PAIR(P_H1 + i));
    }
}

void ui_draw(void)
{
    Hero *p = &heroes[cur_hero];
    char t[160];
    int i, logrow;

    erase();
    snprintf(t, sizeof t, "  D J A R H U N   -   %s   -   %s's turn    [v] view %d   [?] help",
             land_tbl[p->land].name, p->name, view_mode);
    attron(COLOR_PAIR(P_TITLE) | A_BOLD);
    mvprintw(0, 0, "%-*.*s", COLS, COLS, t);
    attroff(COLOR_PAIR(P_TITLE) | A_BOLD);

    if      (view_mode == 1) draw_whole(p->land, 2);
    else if (view_mode == 3) draw_ribbons(p->land, p->idx, 2);
    else if (view_mode == 4) { draw_chart(p->land, p->idx, 2);
                               draw_inspect(p->land, p->idx, COLS - 26, 1); }
    else                     draw_viewport(p->land, p->idx, 2);

    if (view_mode == 2) {
        /* the lands, and who is on them -- a viewport hides everyone else */
        int row = 9, away = 0;
        mvprintw(row, 2, "ELSEWHERE");
        for (i = 0; i < nheroes; i++) {
            if (!heroes[i].alive || i == cur_hero) continue;
            if (heroes[i].land == p->land) continue;
            mvprintw(row + 1 + away, 3, "@%d %-8.8s %-13.13s %s",
                     i + 1, heroes[i].name, land_tbl[heroes[i].land].name,
                     space_at(heroes[i].land, heroes[i].idx)->name);
            away++;
        }
        if (!away) mvprintw(row + 1, 3, "everyone is in %s", land_tbl[p->land].name);
    }

    if (die_show && view_mode != 4) {   /* view 4 shows it in the panel */
        mvprintw(2, COLS - 14, "%-9.9s", lab_die);
        attron(A_BOLD);
        mvprintw(3, COLS - 14, "d%-2d -> %2d", die_sides, die_rolled);
        attroff(A_BOLD);
    }

    logrow = LINES - LOG_SHOW - 2;
    draw_status(logrow - 3);
    for (i = 0; i < LOG_SHOW; i++) {
        int j = log_n - LOG_SHOW + i;
        if (j >= 0) mvprintw(logrow + i, 0, "%-*.*s", COLS, COLS, logbuf[j]);
    }
    refresh();
    ui_shot();
}


/* Everything the game can do, from inside the game.  A switch that lives
 * only in a README may as well not exist. */
static void ui_help(void)
{
    static const char *lines[] = {
    "  D J A R H U N   --   keys and switches",
    "",
    "  AT ANY PROMPT",
    "    ?          this page",
    "    v          change how the board is drawn -- see below",
    "    space      take the offered action / continue",
    "",
    "  ON YOUR TURN",
    "    a  d       move anticlockwise or clockwise (no reversing mid-move)",
    "    i  o       in the Abyss: deeper, or back towards the Lake of Tears",
    "    y  n       answer whatever was asked",
    "    s  p  c    spend a level's points on Strength, Speed or Sorcery",
    "",
    "  THE FOUR BOARD VIEWS  [v]",
    "    1  the whole land at once, names shortened to fit",
    "    2  seven spaces at full width, centred on you, plus ELSEWHERE",
    "    3  every land as a ribbon, one character a space",
    "    4  chart and inspection panel (the default)",
    "",
    "  WHAT THE CHART SHOWS",
    "    ..  empty      !!  a Foe        $$  treasure     **  both",
    "    Mk  a town     >>  a way out    Lk  Lake of Tears  G!  Gharad",
    "    @n  a hero.  The panel spells out whatever you are standing on.",
    "",
    "  WHAT THE STATISTICS BUY YOU",
    "    Strength   how much you can carry   1-2:3  3-5:4  6-9:5  10-13:6  14-19:7",
    "    Speed      which die you move with  1-3:d4  4-9:d6  10-15:d8  16-21:d10",
    "    Sorcery    how many Spells you hold 3-5:1  6-9:2  10-16:3  17+:4",
    "",
    "  STARTING THE GAME",
    "    djarhun --log FILE     write the whole game out as text",
    "",
    "  THE SAME THINGS AS ENVIRONMENT",
    "    DJARHUN_SEED=n     replay a game exactly; the seed is printed at the top",
    "    DJARHUN_AUTO=1     headless: no screen, prints one result line",
    "    DJARHUN_TRACE=f    as --log",
    "    DJARHUN_SHOT=f     append every frame to a file",
    NULL };
    int i, y = 1;

    erase();
    for (i = 0; lines[i]; i++) {
        if (i == 0) attron(A_BOLD);
        mvprintw(y++, 1, "%-*.*s", COLS - 2, COLS - 2, lines[i]);
        if (i == 0) attroff(A_BOLD);
    }
    mvprintw(y + 1, 1, "current: seed %u, view %d", ui_seed(), view_mode);
    mvprintw(LINES - 1, 0, "%-*.*s", COLS, COLS, "  [any key] back to the game");
    refresh();
    getch();
}

int ui_prompt(const char *msg, const char *valid)
{
    int c;
    for (;;) {
        mvprintw(LINES - 1, 0, "%-*.*s", COLS, COLS, msg);
        refresh();
        c = getch();
        if (c == '?') { ui_help(); ui_draw(); continue; }   /* always available */
        if (c == 'v' || c == 'V') {          /* the view is never a game choice */
            view_mode = view_mode % 4 + 1;
            ui_draw();
            continue;
        }
        if (!valid || !*valid || strchr(valid, c)) return c;
    }
}
