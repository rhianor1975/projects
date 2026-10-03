#ifndef RENDER_H
#define RENDER_H

#include "common.h"
#include "kitchen.h"

void init_colors(void);

/* How the map area is drawn. Set it once at startup from the saved
   preference, and again whenever the player changes it on the display
   screen. A mode the current terminal can't do silently resolves to
   RENDER_TEXT, so callers never have to check first. Menus, shops and the
   message log are text regardless. */
void render_set_mode(RenderMode m);
RenderMode render_get_mode(void);

/* Sizes the map viewport (g_view_w/g_view_h) to the terminal. Call once at
   startup; render_set_mode and a detected resize both re-run it. Tile mode
   gets a smaller viewport than text mode on the same window -- it hands the
   terminal a viewport-sized image every time the picture changes, so the
   cost is set by how many cells are tiled. */
void render_layout_viewport(void);

/* The rule behind that: shrink a viewport until one frame's image fits the
   per-frame pixel budget, never going below the minimum the rest of the game
   needs. Exposed (and pure -- cell size in, viewport out) so the budget can
   be tested without a terminal that does pixel graphics; nothing else should
   need to call it. */
void render_clamp_tile_viewport(int cell_w, int cell_h, int *w, int *h);

/* Tile mode's per-frame pixel budget, clamped to TILE_PIXELS_MIN..MAX.
   Setting it re-lays-out the viewport. */
void render_set_tile_pixels(int px);
int  render_get_tile_pixels(void);

/* Full-screen blocking scenes: draw, wait for any key, return. */
void screen_intro(void);
void screen_gameover(const Player *p, const Hero *actor, int best_floor);
void screen_win(const Player *p, const Hero *actor, int best_floor);

/* Returns a Difficulty value. */
int screen_mode_select(void);
/* How big every floor of this run will be. Chosen once, right after the
   difficulty, because it changes the game more than the difficulty does. */
int screen_world_size_select(void);

/* The key reference, on '?'. The controls scroll past once at the intro and
   never appear again, the footer only has room for the common ones, and a
   player who arrives through a browser link may never have seen the intro at
   all. Blocking: draws, waits, returns. `in_town` trims the list to what
   actually does something where the player is standing. */
void screen_help(bool in_town);

/* Reports what the current build and terminal can actually do, and lets the
   player pick how the game draws. Reachable from the intro screen. Blocking:
   draws, handles its own keys, returns when dismissed. */
void screen_display_settings(void);

/* Shown at launch when a suspended run exists. Returns true to resume it,
   false to abandon it and start fresh. */
bool screen_continue_prompt(void);

/* Character creation, in two stages -- a hundred names in one flat list is a
   scroll, not a choice. First pick a role, then a class inside it.

   screen_archetype_select returns an Archetype. screen_class_select returns a
   CLASS_TABLE index, or -1 if the player backed out to the archetype list;
   pass a negative archetype to browse all 100 at once. */
int screen_archetype_select(void);
int screen_class_select(int archetype);
void screen_name_entry(char *out, size_t outlen);

/* Blocking prompt shown when stepping onto the temple entrance in town with
   a deepest_floor > 1. Returns 1 to start over from floor 1, or
   deepest_floor to resume there. ESC/q also resumes at floor 1. */
int prompt_floor_choice(int deepest_floor);

/* One frame of the live map view. `scrolling` = true follows the player
   with a camera (temple); false pins the camera at (0,0) (town, which
   always fits inside one viewport). */
void draw_game_screen(const Map *m, const Player *p, const Hero *actor, const char *floor_label, bool scrolling);

/* Shop screens: one frame each, do not block. */
void draw_shop_general(const Player *p, const Hero *actor);
void draw_shop_apothecary(const Player *p, const Hero *actor);
void draw_shop_armory(const Player *p, const Hero *actor);
void draw_shop_merchant(const Player *p, const Hero *actor);
/* Arcanist's Guild: paginated browse/learn list over the whole spell pool.
   `cursor` is the selected pool index. One frame, does not block. */
void draw_shop_arcanist(const Player *p, const Hero *actor, int cursor);

/* Ranged weapons sub-screen, reached from the armory with 'w'. One frame,
   does not block. */
void draw_shop_ranged(const Player *p, const Hero *actor);

/* The Gladiator School: the thirty attributes, what each currently sits at,
   and what the next point costs. `cursor` is the selected attribute. One
   frame, does not block. */
void draw_gladiator_school(const Player *p, const Hero *actor, int cursor);

/* The thirty, by name, for anything outside render.c that has to say which
   one just changed. */
extern const char *const ATTR_NAMES[ATTR_COUNT];


/* The Tavern: the 20-strong hire roster. One frame, does not block. */
void draw_tavern(const Player *p, int cursor);
int  biome_color_pair(Biome biome);
/* 0 = bare stone, 1 = the floor's biome showing through, 2 = a wild district. */
int  terrain_wash_at(const Map *m, int x, int y);
void draw_bank(const Player *p, const Hero *actor);
void draw_oracle(const Player *p, const Hero *actor);
void draw_altar(const Player *p, const Hero *actor, int stage, int give, int take);
void draw_bazaar(const Player *p, const Hero *actor);

/* Today's price for one Bazaar stall, as a percentage of the list price.
   Lives with the screen that shows it so the quote and the till cannot
   disagree -- the one bug a shop with moving prices is prone to. */
int bazaar_price_pct(const Player *p, int stall);
void draw_black_market(const Player *p, const Hero *actor, int count);
void draw_kitchen(const Player *p, const Patron *pat, int served, int total,
                  int cursor, long taken);

/* The lizard track. draw_races paints the card; race_run animates one race
   and returns the winner, so the betting logic in main.c never has to know
   how a lizard is drawn. */
void draw_races(const Player *p, const int *odds, const char **names, int cursor, int stake);
/* Runs one race and returns the winner. `watch` stages it tile by tile;
   without it the result is simply drawn. Skipping is the default because the
   track is meant to be a quick way to turn coin over, and a forty-second
   animation you did not ask for stops being a treat the third time. */
int  race_run(const char **names, const int *odds, bool watch);

/* The colour a wild district paints its plain ground, or -1 for "not a
   district". Exposed so the mapping can be asserted -- ten districts sharing
   eight terminal colours is exactly the kind of table that rots quietly. */
int district_color_pair(int kind);

/* Quest board: one frame, does not block. */
/* The meta-progression, made lookable-at: this run, the lifetime best, the
   deepest six classes, and the fallen roster the barrows are built from. */
void draw_records_screen(const Player *p, const Hero *actor);

void draw_quest_board(const Player *p, const Hero *actor);

/* In-dungeon spell-cast menu: paginated list over the player's known
   spells. `cursor` is the selected slot in p->known_spells. One frame,
   does not block. */
void draw_spell_menu(const Hero *actor, int cursor);

/* Inventory screen: one frame, does not block. */
void draw_inventory_screen(const Player *p, const Hero *actor, int page);

/* Full attribute/derived-stat breakdown. Blocking: draws, waits, returns. */
/* ---- the screens take the actor they are drawing -----------------------
 *
 * Every screen below is handed the body it is about, rather than being given
 * the party and left to work out which member is "the player". That is the
 * whole of the rule: the UI is agnostic about *who* is active, so it cannot
 * be wrong about it.
 *
 * It used to reach for the active actor itself, which was correct and was
 * still the wrong shape -- correct because every one of them happened to call
 * the same accessor, wrong because nothing stopped the next screen from
 * reaching for slot 0 instead, and several of them had. A parameter cannot be
 * forgotten; a convention can.
 *
 * `p` is still there for what the party shares -- the purse, the pack, the
 * quest, how deep the run has got. `actor` is the body: its bars, its sheet,
 * its spells, its gear. The split is the same one drawn everywhere else.
 */
void screen_character_sheet(const Player *p, const Hero *actor);

/* Downsampled view of the whole revealed floor at once (floors are
   140x80, far bigger than any terminal viewport). Blocking: draws,
   waits, returns. */
/* Takes no Player at all, and that is the point rather than an accident: a
   minimap is a map and a body's position on it, and once the body is passed
   in there is nothing left for the party to contribute. Same for the spell
   menu below -- a spellbook belongs to whoever is holding it. */
void screen_minimap(const Map *m, const Hero *actor);

/* Turns every animation into a no-op. Set by headless harnesses -- the
   animation primitives call napms(), so a simulation that runs thousands of
   fights spends nearly all of its wall-clock asleep rather than computing.
   (Measured: 100 seconds of which 0.27 was CPU.) Nothing in the game sets
   this; it exists so tools/ can drive real combat at full speed. */
void render_set_headless(bool on);

/* Combat feedback overlays -- brief, synchronous, drawn directly on top of
   whatever draw_game_screen last put on screen. Callers (combat/spells/
   ranged/abilities) pass world coordinates and just call these inline at
   the moment an attack lands; no redraw needed before or after. */
void anim_flash_cell(const Map *m, const Player *p, int x, int y, chtype ch, int pair, int pulses);
void anim_bolt(const Map *m, const Player *p, int x0, int y0, int x1, int y1, chtype ch, int pair);
void anim_burst(const Map *m, const Player *p, int cx, int cy, int radius, chtype ch, int pair);

/* ---- volleys ---------------------------------------------------------
 * A whole party's attacks, drawn at once.
 *
 * The one-at-a-time primitives above are right for the player, who acts
 * once per turn. Five hired heroes firing in sequence would be five
 * animations back to back, every turn, and that is the reason companions
 * were silent: not that their attacks could not be drawn, but that drawing
 * them serially made the game crawl.
 *
 * A volley advances every shot together -- all the bolts travel on the same
 * frames, all the bursts expand on the same frames -- so the cost is set by
 * the longest single shot rather than by how many heroes you hired.
 *
 * Shots whose ends are off-camera or in unlit ground are dropped before
 * anything is timed, so a party fighting across the floor from you costs
 * nothing at all.
 */
typedef struct {
    int x0, y0;      /* where it comes from */
    int x1, y1;      /* where it lands */
    int radius;      /* >0: a burst at (x1,y1) instead of a travelling bolt */
    chtype ch;
    int pair;
} AnimShot;

#define ANIM_VOLLEY_MAX 16

void anim_volley(const Map *m, const Player *p, const AnimShot *shots, int n);

/* While a batch is open, anim_bolt/anim_flash_cell/anim_burst collect instead
   of playing, and anim_batch_end plays the lot as one volley.
 *
   This is what lets the AI call the player's own cast_spell_slot() and
   fire_ranged() without animating once per body per turn. Before it, the
   batching lived inside companions.c and the AI had grown parallel
   implementations of both rather than animate five times a turn. */
void anim_batch_begin(void);
void anim_batch_end(const Map *m, const Player *p);

#endif
