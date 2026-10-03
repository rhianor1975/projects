#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================== *
 *  ncurses backend -- capability reporting only; tui.h passes the
 *  drawing calls straight through to the real library.
 * ===================================================================== */
/* The 8 classic colours as RGB. Shared by both backends: ncurses uses it to
   answer "which pair is closest to this colour", and notcurses to answer the
   reverse -- "what colour is this pair" -- for tui_pair_rgb(). One table, so
   a '@' cannot be one green in text mode and a different one in blocks. */
static const unsigned char ANSI8[8][3] = {
    {  0,   0,   0}, {224,  64,  64}, { 88, 200,  96}, {224, 184,  72},
    { 96, 144, 232}, {200, 112, 216}, { 96, 208, 208}, {216, 216, 216},
};

#if !defined(TUI_NOTCURSES)

static TuiCaps g_caps;

const TuiCaps *tui_caps(void) {
    if (!g_caps.backend) {
        const char *term = getenv("TERM");
        g_caps.backend     = "ncurses";
        g_caps.terminal    = (term && *term) ? term : "(unset)";
        g_caps.pixel_proto = "none";
        /* Honest answer: this backend draws characters and nothing else,
           however capable the terminal underneath happens to be. */
        g_caps.best        = TUI_GFX_NONE;
        g_caps.truecolor   = false;
        g_caps.utf8        = false;
    }
    return &g_caps;
}

static short nearest_ansi(TuiRGB c) {
    short best = 7;
    long best_d = -1;
    for (short i = 0; i < 8; i++) {
        long dr = (long)c.r - ANSI8[i][0];
        long dg = (long)c.g - ANSI8[i][1];
        long db = (long)c.b - ANSI8[i][2];
        long d = dr * dr + dg * dg + db * db;
        if (best_d < 0 || d < best_d) { best_d = d; best = i; }
    }
    return best;
}

/* Pairs 1..48 belong to the game's own palette (see common.h). 64..127 are
   reserved here for the fg/bg combinations tui_put_cell needs, registered on
   first use so an unused combination costs nothing. */
#define TUI_BLOCK_PAIR_BASE 64
static bool g_block_pair_ready[64];

void tui_put_cell(int y, int x, uint32_t ucs, TuiRGB fg, TuiRGB bg) {
    short f = nearest_ansi(fg), b = nearest_ansi(bg);
    int idx = f * 8 + b;
    int pair = TUI_BLOCK_PAIR_BASE + idx;
    if (!g_block_pair_ready[idx]) {
        init_pair((short)pair, f, b);
        g_block_pair_ready[idx] = true;
    }
    /* No wide-character output on this backend, so anything outside ASCII
       degrades to a solid-ish stand-in rather than emitting broken bytes.
       render_mode_available() never offers RENDER_BLOCKS here, so this is a
       safety net rather than a code path the game takes. */
    chtype ch = (ucs < 128) ? (chtype)ucs : (chtype)'#';
    attr_t a = COLOR_PAIR(pair);
    attron(a);
    mvaddch(y, x, ch);
    attroff(a);
}

bool tui_cell_pixels(int *w, int *h) { (void)w; (void)h; return false; }
bool tui_max_bitmap(int *w, int *h)  { (void)w; (void)h; return false; }

bool tui_blit_rgba(int y, int x, const unsigned char *rgba, int pixw, int pixh) {
    (void)y; (void)x; (void)rgba; (void)pixw; (void)pixh;
    return false;
}

/* Peek without consuming, via real ncurses: read non-blocking and push back
   whatever came. Cheap, and the pushback is ncurses' own one-slot queue. */
bool input_waiting(void) {
    nodelay(stdscr, TRUE);
    int c = getch();
    nodelay(stdscr, FALSE);
    if (c == ERR) return false;
    ungetch(c);
    return true;
}

/* This backend draws characters, so the graphical modes never run here and
   nothing asks. Answered anyway rather than left to the linker. */
bool tui_pair_rgb(int pair, TuiRGB *fg) {
    short f = 0, b = 0;
    if (pair < 0 || pair_content((short)pair, &f, &b) == ERR) return false;
    if (f < 0 || f > 7 || !fg) return false;
    fg->r = ANSI8[f][0]; fg->g = ANSI8[f][1]; fg->b = ANSI8[f][2];
    return true;
}

/* No pixel graphics means nothing can cover the text, so there is nothing
   for an overlay to sit above. */
void tui_overlay_begin(int y, int x, int rows, int cols) { (void)y; (void)x; (void)rows; (void)cols; }
void tui_overlay_end(void) {}
void tui_overlay_clear_cell(int y, int x) { (void)y; (void)x; }

#else
/* ===================================================================== *
 *  notcurses backend
 * ===================================================================== */

#include <notcurses/notcurses.h>
#include <stdarg.h>
#include <time.h>
#include <wchar.h>

static struct notcurses *g_nc;
static struct ncplane   *g_std;      /* the text surface */
static struct ncplane   *g_plane;    /* current draw target: g_std or g_overlay */
static TuiCaps           g_caps;
static char              g_term_buf[64];

/* Tile graphics live on their own plane above the text, and transient
   animation glyphs on a transparent plane above that -- otherwise a bolt
   drawn on the text surface would be hidden underneath the tiles. Both are
   torn down by erase(), so a full-screen text screen (inventory, a shop)
   never has leftover graphics floating over it. */
static struct ncplane   *g_gfx;
static struct ncplane   *g_overlay;
static int               g_ovl_y, g_ovl_x, g_ovl_rows, g_ovl_cols;

/* The game addresses everything in screen coordinates, but a plane is
   addressed relative to its own origin. The text surface starts at (0,0) so
   the two coincide there; the overlay does not. Every put goes through these
   so a glyph aimed at a screen cell lands on that screen cell whichever plane
   is currently the target. */
static int plane_y(int y) { return (g_plane == g_overlay) ? y - g_ovl_y : y; }
static int plane_x(int x) { return (g_plane == g_overlay) ? x - g_ovl_x : x; }

/* The tile image is expensive to hand to the terminal, so the plane it lives
   on is kept alive across frames and only re-blitted when the picture
   actually changed: notcurses elides an unchanged sprixel entirely, but only
   if the plane survives. Destroying and recreating it every frame -- which is
   what this did first -- forces a delete-and-retransmit every single time.

   erase() can't destroy it either, for the same reason: draw_game_screen
   erases before it blits. Instead erase() marks it stale, a blit clears the
   mark, and refresh() drops the plane if the mark survived -- so a text
   screen still never renders over leftover graphics. */
static bool           g_gfx_stale;
static unsigned char *g_gfx_last;       /* bytes of the last image blitted */
static size_t         g_gfx_last_size;
static int            g_gfx_y, g_gfx_x, g_gfx_w, g_gfx_h;

static void gfx_drop(void) {
    if (g_gfx) { ncplane_destroy(g_gfx); g_gfx = NULL; }
    g_gfx_w = g_gfx_h = 0;
    g_gfx_last_size = 0;   /* the next blit must not be elided */
}

static attr_t g_attr;            /* current attribute word */
static int    g_cy, g_cx;        /* cursor, mirroring ncurses' notion */
static bool   g_nodelay;
static bool   g_echo;

static struct tui_window { int unused; } g_stdscr_obj;
WINDOW *stdscr = &g_stdscr_obj;
int LINES, COLS;

/* Colour-pair table. The game calls init_pair() with the 8 classic colour
   constants and -1 for "terminal default background". */
#define TUI_MAX_PAIRS 256
typedef struct { short fg, bg; bool set; } PairDef;
static PairDef g_pairs[TUI_MAX_PAIRS];

/* The 8 ANSI colours as explicit RGB. With truecolour available there's no
   reason to leave these to the terminal's palette -- picking them here means
   the game looks the same everywhere instead of inheriting whatever theme
   the user happens to run. */
static const unsigned char PALETTE[8][3] = {
    {  0,   0,   0},  /* black   */
    {224,  64,  64},  /* red     */
    { 88, 200,  96},  /* green   */
    {224, 184,  72},  /* yellow  */
    { 96, 144, 232},  /* blue    */
    {200, 112, 216},  /* magenta */
    { 96, 208, 208},  /* cyan    */
    {216, 216, 216},  /* white   */
};

static void refresh_dims(void) {
    unsigned r = 0, c = 0;
    ncplane_dim_yx(g_std, &r, &c);
    LINES = (int)r;
    COLS  = (int)c;
}

/* Push the current attribute word onto the plane before drawing. */
static void apply_attr(void) {
    unsigned pair = (g_attr & TUI_PAIR_MASK) >> TUI_PAIR_SHIFT;
    short fg = COLOR_WHITE, bg = -1;
    if (pair < TUI_MAX_PAIRS && g_pairs[pair].set) {
        fg = g_pairs[pair].fg;
        bg = g_pairs[pair].bg;
    }

    if (fg >= 0 && fg < 8) {
        unsigned char r = PALETTE[fg][0], g = PALETTE[fg][1], b = PALETTE[fg][2];
        /* A_DIM is used for remembered-but-not-visible map tiles. */
        if (g_attr & A_DIM) { r = (unsigned char)(r / 2); g = (unsigned char)(g / 2); b = (unsigned char)(b / 2); }
        ncplane_set_fg_rgb8(g_plane, r, g, b);
    } else {
        ncplane_set_fg_default(g_plane);
    }

    if (bg >= 0 && bg < 8) {
        ncplane_set_bg_rgb8(g_plane, PALETTE[bg][0], PALETTE[bg][1], PALETTE[bg][2]);
    } else if (g_plane == g_overlay) {
        /* "No background" has to mean transparent on the overlay, or each
           animation glyph blanks the tile it is drawn over. */
        ncplane_set_bg_default(g_plane);
        ncplane_set_bg_alpha(g_plane, NCALPHA_TRANSPARENT);
    } else {
        ncplane_set_bg_default(g_plane);
    }

    ncplane_set_styles(g_plane, (g_attr & A_BOLD) ? NCSTYLE_BOLD : NCSTYLE_NONE);

    /* notcurses has no reverse-video style -- it's a channel swap. */
    if (g_attr & A_REVERSE) {
        ncplane_set_channels(g_plane, ncchannels_reverse(ncplane_channels(g_plane)));
    }
}

WINDOW *initscr(void) {
    notcurses_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.flags = NCOPTION_SUPPRESS_BANNERS | NCOPTION_NO_WINCH_SIGHANDLER;

    g_nc = notcurses_init(&opts, NULL);
    if (!g_nc) {
        fprintf(stderr,
                "aether-descent: notcurses could not initialise.\n"
                "This backend needs a terminal that answers capability queries;\n"
                "it will not run under a pipe, a detached tmux session, or with\n"
                "TERM unset. Try the ncurses build instead:  make && ./bin/aether-descent\n");
        exit(1);
    }
    g_std   = notcurses_stdplane(g_nc);
    g_plane = g_std;
    refresh_dims();

    g_attr = A_NORMAL;
    g_cy = g_cx = 0;
    memset(g_pairs, 0, sizeof(g_pairs));

    /* Record what we actually negotiated, for the render-mode screen. */
    const char *term = getenv("TERM");
    snprintf(g_term_buf, sizeof(g_term_buf), "%s", (term && *term) ? term : "(unset)");
    g_caps.backend  = "notcurses";
    g_caps.terminal = g_term_buf;

    ncpixelimpl_e px = notcurses_check_pixel_support(g_nc);
    switch (px) {
        case NCPIXEL_SIXEL:          g_caps.pixel_proto = "sixel";   break;
        case NCPIXEL_LINUXFB:        g_caps.pixel_proto = "linuxfb"; break;
        case NCPIXEL_ITERM2:         g_caps.pixel_proto = "iTerm2";  break;
        case NCPIXEL_KITTY_STATIC:
        case NCPIXEL_KITTY_ANIMATED:
        case NCPIXEL_KITTY_SELFREF:  g_caps.pixel_proto = "kitty";   break;
        default:                     g_caps.pixel_proto = "none";    break;
    }

    if (px != NCPIXEL_NONE)                  g_caps.best = TUI_GFX_PIXEL;
    else if (notcurses_cansextant(g_nc))     g_caps.best = TUI_GFX_SEXTANT;
    else if (notcurses_canquadrant(g_nc))    g_caps.best = TUI_GFX_QUADRANT;
    else if (notcurses_canhalfblock(g_nc))   g_caps.best = TUI_GFX_HALFBLOCK;
    else                                     g_caps.best = TUI_GFX_NONE;

    g_caps.truecolor = notcurses_cantruecolor(g_nc);
    g_caps.utf8      = notcurses_canutf8(g_nc);

    return stdscr;
}

int endwin(void) {
    if (g_nc) {
        notcurses_stop(g_nc);
        g_nc = NULL;
        g_std = g_plane = NULL;
        g_gfx = g_overlay = NULL;   /* owned by notcurses; stopped with it */
        g_gfx_w = g_gfx_h = 0;
        free(g_gfx_last); g_gfx_last = NULL; g_gfx_last_size = 0;
    }
    return OK;
}

/* Input/echo/buffering are all handled by notcurses' own input layer, so
   these exist to satisfy the call sites and record intent. */
int noecho(void) { g_echo = false; return OK; }
int echo(void)   { g_echo = true;  return OK; }
int cbreak(void) { return OK; }
int keypad(WINDOW *win, bool on) { (void)win; (void)on; return OK; }

int curs_set(int visibility) {
    if (!g_nc) return ERR;
    if (visibility == 0) notcurses_cursor_disable(g_nc);
    else                 notcurses_cursor_enable(g_nc, g_cy, g_cx);
    return OK;
}

int start_color(void)        { return OK; }
int use_default_colors(void) { return OK; }
bool has_colors(void)        { return true; }

int init_pair(short pair, short fg, short bg) {
    if (pair < 0 || pair >= TUI_MAX_PAIRS) return ERR;
    g_pairs[pair].fg  = fg;
    g_pairs[pair].bg  = bg;
    g_pairs[pair].set = true;
    return OK;
}

bool tui_pair_rgb(int pair, TuiRGB *fg) {
    if (!fg || pair < 0 || pair >= TUI_MAX_PAIRS || !g_pairs[pair].set) return false;
    short f = g_pairs[pair].fg;
    if (f < 0 || f > 7) return false;
    fg->r = ANSI8[f][0]; fg->g = ANSI8[f][1]; fg->b = ANSI8[f][2];
    return true;
}

/* Clearing the screen marks the tile image stale rather than destroying it
   outright -- see the note by g_gfx_stale. The animation overlay is cheap and
   goes immediately. */
int erase(void) {
    g_plane = g_std;
    if (g_overlay) { ncplane_destroy(g_overlay); g_overlay = NULL; g_ovl_rows = g_ovl_cols = 0; }
    g_gfx_stale = true;
    ncplane_erase(g_std);
    g_cy = g_cx = 0;
    return OK;
}

int refresh(void) {
    /* Nothing re-blitted since the last erase, so whatever is being drawn is
       a text screen: the tile image goes now, before it can render over it. */
    if (g_gfx_stale && g_gfx) gfx_drop();
    return notcurses_render(g_nc) ? ERR : OK;
}

int clrtoeol(void) {
    for (int x = g_cx; x < COLS; x++)
        ncplane_putchar_yx(g_plane, plane_y(g_cy), plane_x(x), ' ');
    return OK;
}

int move(int y, int x) { g_cy = y; g_cx = x; return OK; }

int attron(attr_t attrs)  { g_attr |= attrs;  return OK; }
int attroff(attr_t attrs) { g_attr &= ~attrs; return OK; }

int addch(chtype ch) {
    apply_attr();
    ncplane_putchar_yx(g_plane, plane_y(g_cy), plane_x(g_cx), (char)(ch & TUI_CHAR_MASK));
    g_cx++;
    return OK;
}

int mvaddch(int y, int x, chtype ch) {
    g_cy = y; g_cx = x;
    return addch(ch);
}

static int put_va(int y, int x, const char *fmt, va_list ap) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    apply_attr();
    /* Clip rather than letting a long line wrap: ncurses truncates at the
       right edge and several screens (long titles especially) rely on it. */
    if (y < 0 || y >= LINES || x >= COLS) return OK;
    int room = COLS - x;
    if (room <= 0) return OK;
    if ((int)strlen(buf) > room) buf[room] = '\0';
    ncplane_putstr_yx(g_plane, plane_y(y), plane_x(x), buf);
    g_cy = y;
    g_cx = x + (int)strlen(buf);
    return OK;
}

int printw(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = put_va(g_cy, g_cx, fmt, ap);
    va_end(ap);
    return r;
}

int mvprintw(int y, int x, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = put_va(y, x, fmt, ap);
    va_end(ap);
    return r;
}

int mvhline(int y, int x, chtype ch, int n) {
    apply_attr();
    for (int i = 0; i < n && x + i < COLS; i++)
        ncplane_putchar_yx(g_plane, plane_y(y), plane_x(x + i), (char)(ch & TUI_CHAR_MASK));
    return OK;
}

int mvvline(int y, int x, chtype ch, int n) {
    apply_attr();
    for (int i = 0; i < n && y + i < LINES; i++)
        ncplane_putchar_yx(g_plane, plane_y(y + i), plane_x(x), (char)(ch & TUI_CHAR_MASK));
    return OK;
}

static int map_key(uint32_t id) {
    switch (id) {
        case NCKEY_UP:     return KEY_UP;
        case NCKEY_DOWN:   return KEY_DOWN;
        case NCKEY_LEFT:   return KEY_LEFT;
        case NCKEY_RIGHT:  return KEY_RIGHT;
        case NCKEY_PGUP:   return KEY_PPAGE;
        case NCKEY_PGDOWN: return KEY_NPAGE;
        case NCKEY_ENTER:  return KEY_ENTER;
        case NCKEY_RESIZE: return KEY_RESIZE;
        case NCKEY_ESC:    return 27;
        case NCKEY_TAB:    return '\t';
        case NCKEY_BACKSPACE: return KEY_BACKSPACE;
        default: break;
    }
    if (id > 0x10FFFF) return ERR;   /* some other synthesised event */
    return (int)id;
}

/* Peek without consuming. notcurses has no ungetch, so anything read here is
   held in a one-slot pushback that getch() drains first -- which is enough,
   because the only caller asks "is there anything?" and then immediately
   reads it. */
static uint32_t g_peeked_id;
static ncinput  g_peeked_ni;
static bool     g_have_peek;

int getch(void) {
    ncinput ni;
    for (;;) {
        uint32_t id;
        if (g_have_peek) {
            id = g_peeked_id; ni = g_peeked_ni; g_have_peek = false;
        } else if (g_nodelay) {
            struct timespec ts = { 0, 0 };
            id = notcurses_get(g_nc, &ts, &ni);
            if (id == 0) return ERR;            /* nothing pending */
        } else {
            id = notcurses_get_blocking(g_nc, &ni);
        }
        if (id == (uint32_t)-1) return ERR;

        /* With the kitty keyboard protocol notcurses reports releases and
           repeats as well as presses. Only presses are keystrokes -- without
           this filter every key would register twice. */
        if (ni.evtype == NCTYPE_RELEASE) continue;

        if (id == NCKEY_RESIZE) { refresh_dims(); return KEY_RESIZE; }

        int k = map_key(id);
        if (k != ERR) return k;
    }
}

int nodelay(WINDOW *win, bool on) { (void)win; g_nodelay = on; return OK; }

int flushinp(void) {
    if (!g_nc) return OK;
    ncinput ni;
    struct timespec ts = { 0, 0 };
    while (notcurses_get(g_nc, &ts, &ni) > 0) { /* drain */ }
    return OK;
}

bool input_waiting(void) {
    if (g_have_peek) return true;
    if (!g_nc) return false;
    struct timespec ts = { 0, 0 };
    uint32_t id = notcurses_get(g_nc, &ts, &g_peeked_ni);
    if (id == 0 || id == (uint32_t)-1) return false;
    g_peeked_id = id;
    g_have_peek = true;
    return true;
}

int napms(int ms) {
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
    return OK;
}

/* Line editor for the one place the game asks for typed text (the name
   prompt). ncurses' getnstr echoes as you type; so does this. */
int getnstr(char *str, int n) {
    int len = 0;
    int start_y = g_cy, start_x = g_cx;

    if (n < 1) { if (str) str[0] = '\0'; return OK; }
    str[0] = '\0';

    for (;;) {
        refresh();
        int ch = getch();
        if (ch == ERR) continue;
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) break;

        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0) {
                str[--len] = '\0';
                ncplane_putchar_yx(g_plane, start_y, start_x + len, ' ');
            }
            continue;
        }
        if (ch >= 32 && ch < 127 && len < n) {
            str[len++] = (char)ch;
            str[len] = '\0';
            if (g_echo) {
                apply_attr();
                ncplane_putchar_yx(g_plane, start_y, start_x + len - 1, (char)ch);
            }
        }
    }
    return OK;
}

void tui_dims(int *rows, int *cols) {
    refresh_dims();
    if (rows) *rows = LINES;
    if (cols) *cols = COLS;
}

const TuiCaps *tui_caps(void) { return &g_caps; }

/* ---- graphics ------------------------------------------------------- */

void tui_put_cell(int y, int x, uint32_t ucs, TuiRGB fg, TuiRGB bg) {
    if (y < 0 || y >= LINES || x < 0 || x >= COLS) return;
    ncplane_set_fg_rgb8(g_plane, fg.r, fg.g, fg.b);
    ncplane_set_bg_rgb8(g_plane, bg.r, bg.g, bg.b);
    ncplane_set_styles(g_plane, NCSTYLE_NONE);
    ncplane_putwc_yx(g_plane, plane_y(y), plane_x(x), (wchar_t)ucs);
    g_cy = y;
    g_cx = x + 1;
}

bool tui_cell_pixels(int *w, int *h) {
    if (!g_std) return false;
    unsigned pxy = 0, pxx = 0, celly = 0, cellx = 0, bmy = 0, bmx = 0;
    ncplane_pixel_geom(g_std, &pxy, &pxx, &celly, &cellx, &bmy, &bmx);
    if (celly == 0 || cellx == 0) return false;
    if (w) *w = (int)cellx;
    if (h) *h = (int)celly;
    return true;
}

bool tui_max_bitmap(int *w, int *h) {
    if (!g_std) return false;
    unsigned pxy = 0, pxx = 0, celly = 0, cellx = 0, bmy = 0, bmx = 0;
    ncplane_pixel_geom(g_std, &pxy, &pxx, &celly, &cellx, &bmy, &bmx);
    if (bmy == 0 || bmx == 0) return false;   /* terminal states no limit */
    if (w) *w = (int)bmx;
    if (h) *h = (int)bmy;
    return true;
}

bool tui_blit_rgba(int y, int x, const unsigned char *rgba, int pixw, int pixh) {
    if (!g_nc || !rgba || pixw <= 0 || pixh <= 0) return false;
    if (g_caps.best < TUI_GFX_PIXEL) return false;

    /* Refuse an image the terminal has said it won't take. Blitting past the
       stated maximum is undefined, and what it looks like in practice is a
       screen full of garbage. */
    int maxw = 0, maxh = 0;
    if (tui_max_bitmap(&maxw, &maxh) && (pixw > maxw || pixh > maxh)) {
        gfx_drop();
        return false;
    }

    size_t bytes = (size_t)pixw * (size_t)pixh * 4u;

    /* The picture is identical to the one already on screen: leave the plane
       alone. notcurses elides an unchanged sprixel, so this turns a
       multi-megabyte retransmit into nothing at all -- which is most of what
       makes redraws that don't move the camera feel instant. */
    if (g_gfx && g_gfx_w == pixw && g_gfx_h == pixh &&
        g_gfx_y == y && g_gfx_x == x &&
        g_gfx_last && g_gfx_last_size == bytes &&
        memcmp(g_gfx_last, rgba, bytes) == 0) {
        g_gfx_stale = false;
        if (g_overlay) ncplane_move_top(g_overlay);
        return true;
    }

    /* Geometry changed (a resize, or a different render mode): the old plane
       can't be reused, and leaving it would be exactly the stale-sprixel case
       that paints over everything. */
    if (g_gfx && (g_gfx_w != pixw || g_gfx_h != pixh || g_gfx_y != y || g_gfx_x != x))
        gfx_drop();

    struct ncvisual *ncv = ncvisual_from_rgba(rgba, pixh, pixw * 4, pixw);
    if (!ncv) return false;

    struct ncvisual_options vopts;
    memset(&vopts, 0, sizeof(vopts));
    vopts.blitter = NCBLIT_PIXEL;
    /* NODEGRADE: if pixels aren't really available, fail here so the caller
       can fall back to a text mode, instead of silently dropping to block
       characters the tile art was never drawn for. */
    vopts.flags   = NCVISUAL_OPTION_NODEGRADE;

    if (g_gfx) {
        vopts.n = g_gfx;             /* reuse: replaces the sprixel in place */
    } else {
        vopts.n     = g_std;         /* first time: let notcurses size it */
        vopts.y     = y;
        vopts.x     = x;
        vopts.flags |= NCVISUAL_OPTION_CHILDPLANE;
    }

    struct ncplane *pl = ncvisual_blit(g_nc, ncv, &vopts);
    ncvisual_destroy(ncv);
    if (!pl) { gfx_drop(); return false; }

    g_gfx   = pl;
    g_gfx_y = y; g_gfx_x = x;
    g_gfx_w = pixw; g_gfx_h = pixh;
    g_gfx_stale = false;

    /* Remember the bytes so the next identical frame can be elided. */
    if (g_gfx_last_size != bytes) {
        unsigned char *nb = realloc(g_gfx_last, bytes);
        if (nb) { g_gfx_last = nb; g_gfx_last_size = bytes; }
        else    { free(g_gfx_last); g_gfx_last = NULL; g_gfx_last_size = 0; }
    }
    if (g_gfx_last) memcpy(g_gfx_last, rgba, bytes);

    /* Keep the animation overlay on top of the image we just placed. */
    if (g_overlay) ncplane_move_top(g_overlay);
    return true;
}

void tui_overlay_begin(int y, int x, int rows, int cols) {
    if (!g_nc || rows <= 0 || cols <= 0) return;

    /* Cover only the region the caller animates in. A plane no bigger than
       it needs is a plane that can't blank anything it shouldn't. */
    if (g_overlay && (g_ovl_y != y || g_ovl_x != x ||
                      g_ovl_rows != rows || g_ovl_cols != cols)) {
        ncplane_destroy(g_overlay);
        g_overlay = NULL;
    }

    if (!g_overlay) {
        struct ncplane_options opts;
        memset(&opts, 0, sizeof(opts));
        opts.y    = y;
        opts.x    = x;
        opts.rows = (unsigned)rows;
        opts.cols = (unsigned)cols;
        g_overlay = ncplane_create(g_std, &opts);
        if (!g_overlay) return;

        /* The base cell is what notcurses renders for every cell whose
           gcluster is 0 -- which, on a fresh plane, is all of them. Its
           default value is all zeroes, and NCALPHA_OPAQUE *is* zero, so an
           untouched plane paints an opaque block over everything beneath it.
           That is not a subtlety: it is why an overlay created the moment a
           blow landed turned the map into a flat rectangle and took the tiles
           with it. It has to be made transparent explicitly. */
        uint64_t transparent = 0;
        ncchannels_set_fg_alpha(&transparent, NCALPHA_TRANSPARENT);
        ncchannels_set_bg_alpha(&transparent, NCALPHA_TRANSPARENT);
        ncplane_set_base(g_overlay, "", 0, transparent);

        g_ovl_y = y; g_ovl_x = x;
        g_ovl_rows = rows; g_ovl_cols = cols;
    }

    ncplane_move_top(g_overlay);
    g_plane = g_overlay;
}

void tui_overlay_end(void) {
    g_plane = g_std;
}

void tui_overlay_clear_cell(int y, int x) {
    if (!g_overlay) return;
    int py = y - g_ovl_y, px = x - g_ovl_x;
    /* ncplane_erase_region reads a negative start as "from the cursor", and
       a zero length as "everything left" -- so a coordinate outside the
       overlay would clear far more than the one cell asked for. */
    if (py < 0 || px < 0 || py >= g_ovl_rows || px >= g_ovl_cols) return;
    ncplane_erase_region(g_overlay, py, px, 1, 1);
}

#endif /* TUI_NOTCURSES */

/* ---- shared by both backends ----------------------------------------- */

const char *tui_graphics_name(TuiGraphics g) {
    switch (g) {
        case TUI_GFX_PIXEL:     return "pixel graphics";
        case TUI_GFX_SEXTANT:   return "sextant blocks (3x2)";
        case TUI_GFX_QUADRANT:  return "quadrant blocks (2x2)";
        case TUI_GFX_HALFBLOCK: return "half blocks (2x1)";
        default:                return "text only";
    }
}

const char *render_mode_name(RenderMode m) {
    switch (m) {
        case RENDER_TEXT:   return "Text";
        case RENDER_BLOCKS: return "Block graphics";
        case RENDER_TILES:  return "Tileset";
        default:            return "?";
    }
}

bool render_mode_available(RenderMode m, const char **why_not) {
    const TuiCaps *c = tui_caps();
    const char *reason = NULL;
    bool ok = false;

    switch (m) {
        case RENDER_TEXT:
            ok = true;
            break;

        case RENDER_BLOCKS:
            /* Draws terrain as solid Unicode blocks in true colour, so it
               needs both a terminal that renders block glyphs at single
               width and a backend that can address a colour per cell. */
            /* Capability before locale: on the ncurses backend the answer is
               always "this build draws characters", and blaming the locale
               for that would send the player off fixing the wrong thing. */
            if (c->best < TUI_GFX_HALFBLOCK) reason = "this build/terminal draws characters only";
            else if (!c->utf8)               reason = "terminal is not in a UTF-8 locale";
            else                             ok = true;
            break;

        case RENDER_TILES:
            if (c->best < TUI_GFX_PIXEL) reason = "no pixel-graphics protocol on this terminal";
            else                         ok = true;
            break;

        default:
            reason = "unknown mode";
            break;
    }

    if (why_not) *why_not = reason;
    return ok;
}
