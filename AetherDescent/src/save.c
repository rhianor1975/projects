#include "save.h"
#include "vision.h"
#include "mapgen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Where the four state files live. $HOME by default, which is what a normal
   single-player install wants.

   AETHER_STATE_DIR overrides it, and exists for one reason: served over the
   network (`make serve`) every session runs as the same OS user, so without
   it two people playing at once share one save slot and silently overwrite
   each other's suspended run. tools/play.sh gives each named player their
   own directory. */
static const char *state_dir(void) {
    const char *dir = getenv("AETHER_STATE_DIR");
    if (dir && *dir) {
        /* Created on demand rather than assumed -- a missing directory would
           otherwise turn every save into a silent no-op. */
        mkdir(dir, 0700);
        return dir;
    }
    const char *home = getenv("HOME");
    return (home && *home) ? home : ".";
}

static void highscore_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.aether_descent_highscore", state_dir());
}

int load_highscore(void) {
    char path[512];
    highscore_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    int best = 0;
    if (fscanf(f, "%d", &best) != 1) best = 0;
    fclose(f);
    return best;
}

int save_highscore_if_better(int floor_reached) {
    int best = load_highscore();
    if (floor_reached > best) {
        best = floor_reached;
        char path[512];
        highscore_path(path, sizeof(path));
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%d\n", best);
            fclose(f);
        }
    }
    return best;
}

#define SAVE_MAGIC   0xAE7DE501u
/* 3: the spell pool was rebuilt from a generator into a curated table, which
      renumbered it. Player.known_spells stores pool indices, so a version-2
      save would load and quietly hand the character a different spellbook --
      the struct sizes were unchanged, so nothing else would have caught it.
   4: consumables gained a heal-percentage field (which moves sizeof(Player)
      via InvStack, so the size guard catches that part on its own) and
      AccessoryStat gained ACC_REGEN, renumbering ACC_NONE -- an old save's
      ring_stat would now mean something different.
   5: Map gained an `overrun` flag for Hard's swarm floors, which moves
      sizeof(Map).

   6: Player gained a five-strong companion party plus the Tavern roster
      seed and masks, which moves sizeof(Player).
*/
/* 39: Player gained `controlled` -- which body holds the main-character role
   (see ROADMAP's take-control entry). The header's sizeof() check would have
   caught an old save on its own, but the rule in this project is that a
   Player layout change bumps this too.

   40: the Hero refactor. Player is no longer a character with five hires
   bolted on -- it is `Hero party[6]` plus the state the party shares, and
   every body field moved out of Player and into a Hero. This is not an
   appended field; it is a different shape, and sizeof(Player) roughly
   doubled. Old saves cannot be migrated and are not worth migrating: there
   is no field-by-field reader to teach, because the file has always been
   the struct.

   41: the layout settled after the Hero refactor's follow-up work
   (EVALUATION 96). A `boss_felled` field was added to Player and then taken
   back out again -- the Warden's death is turn-scoped, so it belongs in a
   module flag rather than in saved run state, where it would only ever be
   written false. The bump stays: the layout moved twice and 40 no longer
   names any shape this build can read.

   Worth recording why it needed catching at all. The field went in without
   the version moving, and the sizeof() guard in the header would have refused
   the mismatched saves without ever saying why. The rule is that a Player
   layout change bumps this, and it exists because the guard catches the
   corruption but not the confusion. */
/* 42: three more bounty kinds and the state they need -- a deadline, a broken
   flag, and which party slot the escort's client is in (EVALUATION 100). Also
   Hero gained `set_complete`, for the four-piece gear procs. */
/* 43: the two districts of the 3.1 tranche. MapDistrict gained `state` (what
   a kind remembers about itself between turns -- the watch uses it for
   whether it has been broken), Map gained the gauntlet's district/wave/paid
   triple, and FeatureType gained FEATURE_STRONGBOX. The first two move
   sizeof(Map) and the header's guard would catch them on its own; the rule
   here is that a layout change bumps this anyway, because the guard catches
   the corruption and not the confusion. */
/* 44: the C tranche's five districts. `TileType` gained TILE_PIT and
   `WorkKind` gained WORK_CORE, both appended; neither moves a struct, so the
   header's sizeof() guard would let an old save through and it would then
   describe a floor whose tile numbering it does not share. This is exactly
   the case the version number exists for and the size check cannot see. */
/* 45: the proving ground. No struct moves -- DIST_PROVING is an appended
   enum value and its rule lives in the MapDistrict.state that already exists
   -- but a version-44 floor's districts[i].state means "the watch is
   unbroken" where a version-45 build reads it as "blades only". Same class as
   44: an appended value that the size guard cannot see and that changes what
   an old file's bytes mean. */
/* 46: the variety pass. Six effects appended to SpellEffect -- which alone
   would be a renumbering of nothing, since they are appended -- but Hero also
   gained `reflect_pct` and `reflect_turns` for Warding's reflect, and that
   moves sizeof(Player). The size guard would catch the second and never see
   the first. */
#define SAVE_VERSION 48u

/* The header carries the struct sizes the file was written with, and a
   load refuses anything that doesn't match the running build exactly.
   A hand-maintained version number is not enough on its own: Player and
   Map gain fields often, and an older save is usually *larger* than the
   current structs, so both fread()s succeed and the size mismatch goes
   unnoticed -- the run then loads with every field read at the wrong
   offset. Deriving the check from sizeof() means it cannot rot even if
   someone forgets to bump SAVE_VERSION. */
typedef struct {
    unsigned int magic;
    unsigned int version;
    unsigned int player_size;
    unsigned int map_size;
} SaveHeader;

static void save_run_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.aether_descent_save", state_dir());
}

/* A resumed run drives array indexing and rendering directly, so anything
   that survives the header check still has to look like a real character
   before it's allowed to become the live game state. Cheap insurance
   against a truncated write, a half-finished copy, or a file from a build
   whose structs happened to be the same size. */
static bool player_state_is_sane(const Player *p, const Map *m) {
    if (p->party[0].x < 0 || p->party[0].x >= MAP_W || p->party[0].y < 0 || p->party[0].y >= MAP_H) return false;
    if (p->party[0].maxhp < 1 || p->party[0].hp < 1 || p->party[0].hp > p->party[0].maxhp) return false;
    if (p->party[0].level < 1) return false;
    if (p->party[0].class_id < 0 || p->party[0].class_id >= NUM_CLASSES) return false;
    if (p->floor < 0 || p->floor > MAX_FLOOR) return false;
    if (p->inv_count < 0 || p->inv_count > INV_CAP) return false;
    if (p->party[0].spell_count < 0 || p->party[0].spell_count > MAX_KNOWN_SPELLS) return false;
    if (p->difficulty < 0 || p->difficulty >= DIFFICULTY_COUNT) return false;

    if (m->biome < 0 || m->biome >= BIOME_COUNT) return false;
    if (m->monster_count < 0 || m->monster_count > MAX_MONSTERS) return false;
    if (m->item_count < 0 || m->item_count > MAX_FLOOR_ITEMS) return false;
    if (m->feature_count < 0 || m->feature_count > MAX_FEATURES) return false;
    return true;
}

/* Header-only check, so a save this build can't read never gets as far as
   offering the player a "continue?" prompt it would then have to silently
   walk back. Deliberately does not read the payload -- that's load_run's
   job, and it needs the caller's Player/Map to read into. */
bool has_saved_run(void) {
    char path[512];
    save_run_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    SaveHeader h;
    memset(&h, 0, sizeof(h));
    bool ok = fread(&h, sizeof(h), 1, f) == 1
           && h.magic == SAVE_MAGIC
           && h.version == SAVE_VERSION
           && h.player_size == (unsigned int)sizeof(Player)
           && h.map_size == (unsigned int)sizeof(Map);

    /* Length has to match exactly too -- that's what catches a half-written
       file or one with junk appended, both of which can carry a perfectly
       valid header. */
    if (ok && fseek(f, 0, SEEK_END) == 0) {
        long want = (long)sizeof(SaveHeader) + (long)sizeof(Player) + (long)sizeof(Map);
        if (ftell(f) != want) ok = false;
    }
    fclose(f);

    if (!ok) delete_saved_run();
    return ok;
}

void save_run(const Player *p, const Map *m) {
    char path[512];
    save_run_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;

    SaveHeader h;
    h.magic       = SAVE_MAGIC;
    h.version     = SAVE_VERSION;
    h.player_size = (unsigned int)sizeof(*p);
    h.map_size    = (unsigned int)sizeof(*m);

    fwrite(&h, sizeof(h), 1, f);
    fwrite(p, sizeof(*p), 1, f);
    fwrite(m, sizeof(*m), 1, f);
    fclose(f);
}

bool load_run(Player *p, Map *m) {
    char path[512];
    save_run_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    SaveHeader h;
    memset(&h, 0, sizeof(h));

    bool ok = fread(&h, sizeof(h), 1, f) == 1
           && h.magic == SAVE_MAGIC
           && h.version == SAVE_VERSION
           && h.player_size == (unsigned int)sizeof(*p)
           && h.map_size == (unsigned int)sizeof(*m)
           && fread(p, sizeof(*p), 1, f) == 1
           && fread(m, sizeof(*m), 1, f) == 1;

    /* Reject trailing bytes too -- a file that is longer than it should be
       is not a save this build wrote, whatever its header claims. */
    if (ok) {
        char extra;
        if (fread(&extra, 1, 1, f) == 1) ok = false;
    }

    fclose(f);

    /* Before the sanity check, not after: it validates coordinates against
       MAP_W/MAP_H, and those are now whatever world this run was started at.
       Checking a Well save against the Shaft's bounds would reject a
       perfectly good file. */
    if (ok) world_size_apply(p->world_size);

    if (ok && !player_state_is_sane(p, m)) ok = false;

    if (!ok) {
        delete_saved_run();
        return false;
    }
    /* The renderer's lit-tile cache describes the map we just replaced. */
    fov_forget();
    /* The save carries the party's shared memory of the floor, not six
       private copies of it -- the file is the Map struct and always has been.
       So a resumed run hands every body what the party knew and they diverge
       again from there, which costs a distinction nobody can see and saves
       six million tiles of save file. */
    hero_vision_forget();
    hero_vision_seed_from_map(m, p);
    return true;
}

void delete_saved_run(void) {
    char path[512];
    save_run_path(path, sizeof(path));
    remove(path);
}

/* ---- the Inn snapshot ------------------------------------------------
   Same integrity rules as the suspended run: magic, version, the running
   build's sizeof(Player), and an exact file length. A snapshot that fails
   any of them is deleted rather than half-loaded -- coming back from the
   dead as a corrupted character would be worse than not coming back. */
#define INN_MAGIC 0xAE7D1004u

static void inn_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.aether_descent_inn", state_dir());
}

typedef struct {
    unsigned int magic, version, player_size;
} InnHeader;

bool has_inn_snapshot(void) {
    char path[512];
    inn_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    InnHeader h;
    memset(&h, 0, sizeof(h));
    bool ok = fread(&h, sizeof(h), 1, f) == 1
           && h.magic == INN_MAGIC
           && h.version == SAVE_VERSION
           && h.player_size == (unsigned int)sizeof(Player);
    if (ok && fseek(f, 0, SEEK_END) == 0) {
        long want = (long)sizeof(InnHeader) + (long)sizeof(Player);
        if (ftell(f) != want) ok = false;
    }
    fclose(f);
    if (!ok) delete_inn_snapshot();
    return ok;
}

void save_inn_snapshot(const Player *p) {
    char path[512];
    inn_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;

    InnHeader h;
    h.magic = INN_MAGIC;
    h.version = SAVE_VERSION;
    h.player_size = (unsigned int)sizeof(*p);
    fwrite(&h, sizeof(h), 1, f);
    fwrite(p, sizeof(*p), 1, f);
    fclose(f);
}

bool load_inn_snapshot(Player *p) {
    char path[512];
    inn_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    InnHeader h;
    memset(&h, 0, sizeof(h));
    bool ok = fread(&h, sizeof(h), 1, f) == 1
           && h.magic == INN_MAGIC
           && h.version == SAVE_VERSION
           && h.player_size == (unsigned int)sizeof(*p)
           && fread(p, sizeof(*p), 1, f) == 1;
    if (ok) {
        char extra;
        if (fread(&extra, 1, 1, f) == 1) ok = false;
    }
    fclose(f);

    /* Same reason as load_run: the world size is the run's, not the last
       one played, and everything downstream is bounded by it. */
    if (ok) world_size_apply(p->world_size);

    /* Reuse the run-save sanity gate on the parts that don't need a Map. */
    if (ok && (p->party[0].maxhp < 1 || p->party[0].level < 1 ||
               p->party[0].class_id < 0 || p->party[0].class_id >= NUM_CLASSES ||
               p->inv_count < 0 || p->inv_count > INV_CAP ||
               p->party[0].spell_count < 0 || p->party[0].spell_count > MAX_KNOWN_SPELLS ||
               p->difficulty < 0 || p->difficulty >= DIFFICULTY_COUNT)) ok = false;

    if (!ok) { delete_inn_snapshot(); return false; }
    return true;
}

void delete_inn_snapshot(void) {
    char path[512];
    inn_path(path, sizeof(path));
    remove(path);
}

static int g_class_records[NUM_CLASSES];
static bool g_class_records_loaded = false;

static void class_records_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.aether_descent_class_records", state_dir());
}

static void load_class_records(void) {
    memset(g_class_records, 0, sizeof(g_class_records));
    char path[512];
    class_records_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (f) {
        fread(g_class_records, sizeof(int), NUM_CLASSES, f);
        fclose(f);
    }
    g_class_records_loaded = true;
}

int class_best_floor(int class_id) {
    if (!g_class_records_loaded) load_class_records();
    if (class_id < 0 || class_id >= NUM_CLASSES) return 0;
    return g_class_records[class_id];
}

void save_class_record_if_better(int class_id, int floor_reached) {
    if (!g_class_records_loaded) load_class_records();
    if (class_id < 0 || class_id >= NUM_CLASSES) return;
    if (floor_reached <= g_class_records[class_id]) return;

    g_class_records[class_id] = floor_reached;
    char path[512];
    class_records_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(g_class_records, sizeof(int), NUM_CLASSES, f);
        fclose(f);
    }
}

/* ---- the fallen ---------------------------------------------------------
   The roster the Gauntlet of the Fallen fights. See save.h for why this is a
   summary record with its own version rather than a Hero written raw like
   everything else in this file. */
#define FALLEN_MAGIC   0xFA11E401u
#define FALLEN_VERSION 1

static void fallen_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.aether_descent_fallen", state_dir());
}

int load_fallen(FallenRecord *out, int max) {
    if (!out || max <= 0) return 0;

    char path[512];
    fallen_path(path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    unsigned magic = 0, version = 0, size = 0;
    int count = 0;
    if (fread(&magic,   sizeof magic,   1, f) != 1 ||
        fread(&version, sizeof version, 1, f) != 1 ||
        fread(&size,    sizeof size,    1, f) != 1 ||
        fread(&count,   sizeof count,   1, f) != 1) { fclose(f); return 0; }

    /* Discarded rather than migrated, on purpose -- a lost roster costs a few
       ghosts and a lost run save costs a run. */
    if (magic != FALLEN_MAGIC || version != FALLEN_VERSION ||
        size != (unsigned)sizeof(FallenRecord) ||
        count < 0 || count > FALLEN_MAX) { fclose(f); return 0; }

    if (count > max) count = max;
    int got = (int)fread(out, sizeof(FallenRecord), (size_t)count, f);
    fclose(f);
    return got < 0 ? 0 : got;
}

void record_fallen(const Player *p) {
    if (!p) return;
    const Hero *h = hero_driven_c(p);
    if (!h) return;

    FallenRecord ring[FALLEN_MAX];
    int count = load_fallen(ring, FALLEN_MAX);

    /* Newest first, so the barrow holds the runs you remember losing. */
    if (count > FALLEN_MAX - 1) count = FALLEN_MAX - 1;
    for (int i = count; i > 0; i--) ring[i] = ring[i - 1];
    count++;

    FallenRecord *r = &ring[0];
    memset(r, 0, sizeof *r);
    snprintf(r->name, sizeof r->name, "%s", h->name[0] ? h->name : "the nameless");
    r->class_id = h->class_id;
    r->level    = h->level;
    /* deepest_floor rather than the floor underfoot: a run that died on the
       way back up still got as deep as it got, and that is the number the
       ghost is remembered by. */
    r->floor    = p->deepest_floor > 0 ? p->deepest_floor : p->floor;
    r->gold     = p->gold;
    r->maxhp    = h->maxhp;
    r->atk      = hero_eff_atk(h);
    r->def      = hero_eff_def(h);

    char path[512];
    fallen_path(path, sizeof(path));
    FILE *f = fopen(path, "wb");
    if (!f) return;

    unsigned magic = FALLEN_MAGIC, version = FALLEN_VERSION;
    unsigned size = (unsigned)sizeof(FallenRecord);
    fwrite(&magic,   sizeof magic,   1, f);
    fwrite(&version, sizeof version, 1, f);
    fwrite(&size,    sizeof size,    1, f);
    fwrite(&count,   sizeof count,   1, f);
    fwrite(ring, sizeof(FallenRecord), (size_t)count, f);
    fclose(f);
}

static void settings_path(char *buf, size_t buflen) {
    snprintf(buf, buflen, "%s/.aether_descent_settings", state_dir());
}

/* The settings file is a handful of key=value lines. Read as a whole and
   rewritten as a whole, so adding a key later doesn't drop the others --
   which the old single-fscanf version would have done. */
typedef struct {
    int render_mode;
    int tile_pixels;
} Settings;

static void settings_defaults(Settings *s) {
    s->render_mode = RENDER_TEXT;
    s->tile_pixels = TILE_PIXELS_DEFAULT;
}

static void settings_read(Settings *s) {
    settings_defaults(s);

    char path[512];
    settings_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        int v;
        if (sscanf(line, "render_mode=%d", &v) == 1) s->render_mode = v;
        else if (sscanf(line, "tile_pixels=%d", &v) == 1) s->tile_pixels = v;
    }
    fclose(f);

    if (s->render_mode < 0 || s->render_mode >= RENDER_MODE_COUNT)
        s->render_mode = RENDER_TEXT;
    if (s->tile_pixels < TILE_PIXELS_MIN || s->tile_pixels > TILE_PIXELS_MAX)
        s->tile_pixels = TILE_PIXELS_DEFAULT;
}

static void settings_write(const Settings *s) {
    char path[512];
    settings_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "render_mode=%d\n", s->render_mode);
    fprintf(f, "tile_pixels=%d\n", s->tile_pixels);
    fclose(f);
}

RenderMode load_render_mode(void) {
    Settings s;
    settings_read(&s);
    /* The settings file is shared across terminals; never trust it to
       describe what *this* run can do. */
    if (!render_mode_available((RenderMode)s.render_mode, NULL)) return RENDER_TEXT;
    return (RenderMode)s.render_mode;
}

void save_render_mode(RenderMode m) {
    Settings s;
    settings_read(&s);
    s.render_mode = (int)m;
    settings_write(&s);
}

int load_tile_pixels(void) {
    Settings s;
    settings_read(&s);
    return s.tile_pixels;
}

void save_tile_pixels(int px) {
    Settings s;
    settings_read(&s);
    s.tile_pixels = px;
    settings_write(&s);
}

const char *title_for_floor(int floor_reached) {
    if (floor_reached >= 100) return "Conqueror of the Deep Well";
    if (floor_reached >= 85)  return "Warden's Equal";
    if (floor_reached >= 60)  return "Well-Touched";
    if (floor_reached >= 35)  return "Depth-Breaker";
    if (floor_reached >= 15)  return "Delver";
    if (floor_reached >= 5)   return "Wreck-Trail Survivor";
    return "Wanderer";
}
