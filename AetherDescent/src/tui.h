#ifndef TUI_H
#define TUI_H

/* Terminal abstraction.
 *
 * The game draws through a small, deliberately ncurses-shaped surface --
 * mvprintw / mvaddch / attron / init_pair / getch and friends. Two backends
 * implement it:
 *
 *   TUI_NOTCURSES  -- notcurses. 24-bit colour, and the groundwork for real
 *                     pixel tilesets (sixel / kitty / iTerm2). Requires a
 *                     terminal that answers capability queries.
 *   (default)      -- ncurses. Runs anywhere, including headless/detached
 *                     terminals and pipes, which is what the automated
 *                     playtest harness drives.
 *
 * Under the ncurses backend this header is a thin pass-through, so that path
 * carries no risk of behaving differently from the original code. Under
 * notcurses every symbol below is reimplemented in tui.c.
 *
 * Build:  make            (ncurses)
 *         make notcurses  (notcurses)
 */

#include <stdbool.h>
#include <stdint.h>

#if defined(TUI_NOTCURSES)

#include <stddef.h>

typedef unsigned int chtype;
typedef unsigned int attr_t;
typedef struct tui_window WINDOW;

/* Attribute word layout, mirroring ncurses closely enough that the game's
   existing `COLOR_PAIR(n) | A_BOLD` expressions keep working unchanged:
   low 16 bits are the character, bits 16-23 the colour-pair index, and the
   high byte the style flags. */
#define TUI_CHAR_MASK  0x0000FFFFu
#define TUI_PAIR_MASK  0x00FF0000u
#define TUI_PAIR_SHIFT 16

#define A_NORMAL   0x00000000u
#define A_BOLD     0x01000000u
#define A_DIM      0x02000000u
#define A_REVERSE  0x04000000u
#define COLOR_PAIR(n) ((attr_t)(((unsigned)(n) << TUI_PAIR_SHIFT) & TUI_PAIR_MASK))

#define COLOR_BLACK   0
#define COLOR_RED     1
#define COLOR_GREEN   2
#define COLOR_YELLOW  3
#define COLOR_BLUE    4
#define COLOR_MAGENTA 5
#define COLOR_CYAN    6
#define COLOR_WHITE   7

#define ERR (-1)
#define OK  (0)

#ifndef TRUE
#define TRUE  true
#define FALSE false
#endif

/* Synthesised keys. Chosen above the Unicode range so they can never collide
   with a real codepoint returned for an ordinary keypress. */
#define KEY_DOWN   0x10000001
#define KEY_UP     0x10000002
#define KEY_LEFT   0x10000003
#define KEY_RIGHT  0x10000004
#define KEY_NPAGE  0x10000005
#define KEY_PPAGE  0x10000006
#define KEY_ENTER  0x10000007
#define KEY_RESIZE 0x10000008
#define KEY_BACKSPACE 0x10000009

extern WINDOW *stdscr;
extern int LINES, COLS;

WINDOW *initscr(void);
int endwin(void);
int noecho(void);
int echo(void);
int cbreak(void);
int keypad(WINDOW *win, bool on);
int curs_set(int visibility);
int start_color(void);
int use_default_colors(void);
bool has_colors(void);
int init_pair(short pair, short fg, short bg);

int erase(void);
int refresh(void);
int clrtoeol(void);
int move(int y, int x);
int attron(attr_t attrs);
int attroff(attr_t attrs);
int addch(chtype ch);
int mvaddch(int y, int x, chtype ch);
int printw(const char *fmt, ...);
int mvprintw(int y, int x, const char *fmt, ...);
int mvhline(int y, int x, chtype ch, int n);
int mvvline(int y, int x, chtype ch, int n);

int getch(void);

int getnstr(char *str, int n);
int nodelay(WINDOW *win, bool on);
int flushinp(void);
int napms(int ms);

void tui_dims(int *rows, int *cols);
/* getmaxyx is a macro in ncurses and is used that way by the game. */
#define getmaxyx(win, y, x) do { (void)(win); tui_dims(&(y), &(x)); } while (0)

#else  /* ---- ncurses backend: pass straight through ---- */

#include <ncurses.h>

#endif

/* Is there a keystroke already waiting?
 *
   For the one thing a turn-based game on a slow renderer has to get right.
   The play loop is draw-then-read-then-act, one frame per key, which is
   correct and cheap right up until a frame costs more than the terminal's key
   repeat interval. Then the queue fills faster than it drains, and the
   character keeps walking for seconds after the key is released -- reported
   from play, in tile mode, as "ghosting".

   With this, the loop can skip the frame it was about to draw when there is
   already another key behind it: the turns still all happen, in order, but the
   *drawing* happens once at the end instead of once per turn, so the game
   keeps up with the typing and there is no backlog to run on with. Does not
   consume the keystroke. */
bool input_waiting(void);



/* ---- backend-agnostic capability reporting ---------------------------
   Both backends answer these. ncurses reports a text-only terminal, which
   is the truth: it cannot do better regardless of what the terminal
   supports. notcurses reports what it actually negotiated. */

typedef enum {
    TUI_GFX_NONE = 0,   /* characters only */
    TUI_GFX_HALFBLOCK,  /* 2x1 subcells */
    TUI_GFX_QUADRANT,   /* 2x2 subcells */
    TUI_GFX_SEXTANT,    /* 3x2 subcells */
    TUI_GFX_PIXEL       /* true pixel graphics: sixel / kitty / iTerm2 */
} TuiGraphics;

typedef struct {
    const char *backend;      /* "ncurses" or "notcurses" */
    const char *terminal;     /* $TERM as seen at init */
    const char *pixel_proto;  /* "sixel", "kitty", "iTerm2", "none", ... */
    TuiGraphics best;         /* richest mode this terminal can actually do */
    bool truecolor;
    bool utf8;
} TuiCaps;

/* Valid after initscr(). */
const TuiCaps *tui_caps(void);
const char *tui_graphics_name(TuiGraphics g);

/* How the player wants the world drawn. The selection machinery is complete;
   only RENDER_TEXT actually draws today. BLOCKS and TILES are listed by the
   settings screen so the terminal's real capabilities are visible, and are
   marked unavailable until the renderers behind them exist -- the screen
   never offers a mode it cannot deliver. */
typedef enum {
    RENDER_TEXT = 0,   /* one character per cell -- always available */
    RENDER_BLOCKS,     /* Unicode subcell blocks; needs utf8 + a block-capable font */
    RENDER_TILES,      /* a real tileset; needs pixel graphics */
    RENDER_MODE_COUNT
} RenderMode;

/* How many pixels tile mode is allowed to ship to the terminal per changed
   frame. This is the only real speed control the mode has: everything else
   about it is fixed by the terminal's cell size. The right value depends on
   how fast that particular terminal ingests images -- which nothing here can
   measure -- so it's the player's to set, and the display screen shows the
   resulting tile count next to it. */
/* Raised, because the old range could not express "a full map on a modern
   display" and was quietly costing more than it saved.
 *
   The numbers were calibrated on an 8x16 cell, where 360,000 pixels buys a
   75x37 viewport -- a decent map. A retina terminal reports *physical* pixels,
   so the same apparent cell is 16x32 or 20x44, and the same budget buys
   58x18: the floor. Which is the part that makes the old default indefensible
   rather than merely conservative -- **at that cell size the floor itself
   costs 918,720 pixels**, so the budget was already being exceeded by a factor
   of nearly three and its only remaining effect was to pin the map at its
   minimum however large the window. It was not protecting anybody; it was
   taking the map away and charging for it anyway.

   Reported from play as the viewport being "so small on tileset that breaks
   the layout", with a screenshot of a 58x18 picture in the corner of a very
   large terminal.

   So: a default that a retina cell can actually meet, and a ceiling high
   enough that `+` reaches a *full-window* map on one -- a 190x50 viewport at
   20x44 is 8.4 million pixels, so a ceiling below that cannot express "show me
   the whole window" however hard the player leans on the key. Both are still the
   player's to set -- what a given terminal can ingest per frame is the one
   thing nothing here can measure, which is why this is a lever and not a
   constant. If it is slow, `-` on the display screen is the answer and it
   still goes lower than it ever did. */
#define TILE_PIXELS_MIN     120000
#define TILE_PIXELS_DEFAULT 1000000
#define TILE_PIXELS_MAX     12000000

const char *render_mode_name(RenderMode m);
/* True once the backend and the terminal both support the mode. Anything
   else is reported to the player with the reason. */
bool render_mode_available(RenderMode m, const char **why_not);

/* ---- graphics primitives --------------------------------------------
   Everything above this line is the text surface. These three groups are
   what the richer render modes draw through. The ncurses backend answers
   all of them honestly-negatively (no true colour, no pixels), which is
   also why render_mode_available() only ever offers TEXT there. */

typedef struct { unsigned char r, g, b; } TuiRGB;

/* One cell, arbitrary fg/bg, arbitrary Unicode codepoint. Used by
   RENDER_BLOCKS. `ucs` must be single-width or the grid tears. */
void tui_put_cell(int y, int x, uint32_t ucs, TuiRGB fg, TuiRGB bg);

/* The foreground colour of one of the game's colour pairs, as RGB.
 *
   Exists so the graphical modes can honour a colour the *game* chose rather
   than one the art table did. The party are all drawn as '@' and told apart by
   colour alone, and that colour lives in a pair -- so a mode that takes its
   foreground from the sprite makes five hires identical. Returns false for a
   pair that was never set, and the caller keeps whatever it had. */
bool tui_pair_rgb(int pair, TuiRGB *fg);

/* Terminal cell size in pixels. False when the backend can't say, which is
   the same condition as "no pixel graphics". */
bool tui_cell_pixels(int *w, int *h);

/* Largest bitmap in pixels this terminal will accept, if it states one.
   False means no stated limit. Exceeding a stated limit is undefined and in
   practice fills the screen with garbage, so tui_blit_rgba refuses rather
   than trying. */
bool tui_max_bitmap(int *w, int *h);

/* Draw a pixw x pixh RGBA image with its top-left at cell (y,x). Sizing the
   image to a whole number of cells is the caller's job -- see tui_cell_pixels.
   Returns false if the backend can't blit pixels, or if this particular blit
   failed; the caller is expected to fall back to a text mode rather than
   leave the player looking at nothing. Each call replaces the previous
   image, and erase() drops it -- so a full-screen text screen is never
   drawn underneath leftover graphics. */
bool tui_blit_rgba(int y, int x, const unsigned char *rgba, int pixw, int pixh);

/* Pixel graphics sit above the text surface, so a glyph drawn normally would
   be hidden underneath them. Drawing between these two calls goes to a
   transparent plane above the graphics instead -- that's how the combat
   animations stay visible in tile mode. Coordinates stay in screen space
   either way; (y,x,rows,cols) only bounds how much of the screen the overlay
   is allowed to cover. Nested calls are not supported. No-ops on backends
   without pixel graphics. */
void tui_overlay_begin(int y, int x, int rows, int cols);
void tui_overlay_end(void);
void tui_overlay_clear_cell(int y, int x);

#endif
