#ifndef SAVE_H
#define SAVE_H

#include "common.h"

/* Best floor ever reached, read from ~/.aether_descent_highscore (0 if none). */
int load_highscore(void);

/* Writes the new best if floor_reached beats the stored one. Returns the
   (possibly updated) best floor. */
int save_highscore_if_better(int floor_reached);

/* Mid-run save/resume: a single suspended-run slot at
   ~/.aether_descent_save, written when the player quits from town or the
   temple and read back on next launch. Permadeath is still the rule --
   this only covers pausing and coming back, not undoing a death, so it's
   deleted on game over and on a win. Player and Map are plain data (no
   pointers), so the whole struct is written/read as one raw block. */
bool has_saved_run(void);
void save_run(const Player *p, const Map *m);
bool load_run(Player *p, Map *m);
void delete_saved_run(void);

/* The Inn. Resting there banks the character exactly as they stand -- level,
   attributes, gold, spells, gear, inventory -- and dying restores that
   snapshot instead of ending the run. Only the Player is stored: you come
   back to town, so the floor you died on is gone either way.

   Deliberately not the same file as the suspended run: that one is "I closed
   the game mid-descent", this one is "I paid for a bed". They have different
   lifetimes and one must not overwrite the other. */
bool has_inn_snapshot(void);
void save_inn_snapshot(const Player *p);
bool load_inn_snapshot(Player *p);
void delete_inn_snapshot(void);

/* Meta-progression: purely a completionist/bragging record, no class is
   ever locked. Best floor ever reached with each class, indexed by
   class_id, persisted in ~/.aether_descent_class_records -- gives a
   reason to revisit a class you haven't gone deep with yet. Lazily loads
   itself on first use, so callers don't need an explicit init step. */
int class_best_floor(int class_id);
void save_class_record_if_better(int class_id, int floor_reached);

/* ---- the fallen ---------------------------------------------------------
 *
 * Every run that ends badly writes down who it was, in a small ring at
 * ~/.aether_descent_fallen. The Gauntlet of the Fallen reads it back: the
 * ghosts you fight in a barrow are the characters this save directory has
 * actually lost, carrying the gold they were actually carrying.
 *
 * Deliberately a *summary* and not a Hero. A Hero is large, it changes shape
 * whenever the party does, and the record only has to be enough to stand
 * something up and put a name on it. The alternative -- write the struct, as
 * the run save and the inn snapshot both do -- would tie the roster to
 * SAVE_VERSION and throw the whole barrow away every time the party layout
 * moved, which this session alone would have done twice.
 *
 * Losing the roster is cheap by design: a version or size mismatch discards
 * the file rather than trying to migrate it. The cost is some ghosts, which
 * the next few deaths replace. That is the opposite of the rule for a run
 * save, and it is the reason this is its own file and its own format.
 */
#define FALLEN_MAX 16

typedef struct {
    char name[32];
    int  class_id;
    int  level;
    int  floor;        /* where it ended */
    int  gold;         /* what they had on them -- the whole reward */
    int  maxhp, atk, def;
} FallenRecord;

/* Newest death first, so a barrow that holds five ghosts holds the five most
   recent. Returns how many were read (0 if there is no file yet). */
int load_fallen(FallenRecord *out, int max);

/* Writes the driven body onto the front of the ring. Called when a run ends
   badly and never when it ends well: the barrow is for the runs you lost. */
void record_fallen(const Player *p);

/* A short title derived from the best floor ever reached (any class,
   same value load_highscore() returns) -- shown on the character sheet
   and the class-select screen. */
const char *title_for_floor(int floor_reached);

/* Display preference, persisted in ~/.aether_descent_settings so a terminal
   that can do more than text doesn't have to be re-selected every launch.
   Falls back to RENDER_TEXT if the stored mode isn't available on this
   run -- e.g. the same home directory used from Ghostty and then from a
   terminal that can't manage it. */
RenderMode load_render_mode(void);
void save_render_mode(RenderMode m);

/* Tile mode's per-frame pixel budget (see TILE_PIXELS_* in tui.h). Stored
   alongside the render mode; clamped to the valid range on read, so an
   edited or truncated settings file can't produce an unusable viewport. */
int  load_tile_pixels(void);
void save_tile_pixels(int px);

#endif
