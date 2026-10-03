#include "common.h"
#include "autoplay.h"
#include <stdlib.h>   /* getenv, strtol -- see tunable() */

char g_msg_log[MSG_LOG_CAP][96];
int g_msg_log_count = 0;

/* Overwritten once at startup (see main()) from the real terminal size. */
int g_view_w = 58;
int g_view_h = 18;

bool g_hp_flash = false;

const char *difficulty_name(int difficulty) {
    switch (difficulty) {
        case DIFFICULTY_NORMAL:   return "Normal";
        case DIFFICULTY_HARD:     return "Hard";
        case DIFFICULTY_SWARM:    return "Swarm";
        case DIFFICULTY_HARDCORE: return "Hardcore";
        default:                   return "?";
    }
}

void log_reset(void) { g_msg_log_count = 0; }

void log_msg(const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* A silent run's log is only worth reading if it says what happened, not
       just which keys were pressed. */
    autoplay_note("%s", buf);

    if (g_msg_log_count < MSG_LOG_CAP) {
        strncpy(g_msg_log[g_msg_log_count], buf, sizeof(g_msg_log[0]) - 1);
        g_msg_log[g_msg_log_count][sizeof(g_msg_log[0]) - 1] = '\0';
        g_msg_log_count++;
    } else {
        /* shift the ring buffer down */
        for (int i = 1; i < MSG_LOG_CAP; i++) {
            memcpy(g_msg_log[i - 1], g_msg_log[i], sizeof(g_msg_log[0]));
        }
        strncpy(g_msg_log[MSG_LOG_CAP - 1], buf, sizeof(g_msg_log[0]) - 1);
        g_msg_log[MSG_LOG_CAP - 1][sizeof(g_msg_log[0]) - 1] = '\0';
    }
}

int hero_eff_atk(const Hero *h) {
    int atk = h->base_atk + h->weapon_bonus + h->atk_buff + h->set_bonus_atk;
    atk += atk * h->stance_atk_pct / 100;      /* the ground you chose to stand on */
    return atk < 1 ? 1 : atk;
}

/* The same number with the proving ground's third skin applied: your weapon
   counts for nothing here, so what is left is the body and what it learned.
 *
   Takes the Map because the rule is a property of the square, and stays
   separate from hero_eff_atk() because that one is asked from the character
   sheet and half a dozen shop screens, none of which are standing anywhere. */
int hero_eff_atk_on(const Hero *h, const Map *m) {
    if (!m || !proving_unarmed_at(m, h->x, h->y)) return hero_eff_atk(h);
    int atk = h->base_atk + h->atk_buff + h->set_bonus_atk;
    atk += atk * h->stance_atk_pct / 100;
    return atk < 1 ? 1 : atk;
}

int hero_eff_def(const Hero *h) {
    int def = h->base_def + h->armor_bonus + h->def_buff + h->set_bonus_def;
    def += def * h->stance_def_pct / 100;
    return def < 0 ? 0 : def;
}

int effective_stat(const Hero *h, int which) {
    int base = 0;
    switch (which) {
        case ACC_CRIT:     base = h->crit_pct + h->set_bonus_crit; break;
        case ACC_EVASION:  base = h->evasion_pct + h->set_bonus_evasion; break;
        case ACC_WARD:     base = h->ward_pct + h->set_bonus_ward; break;
        case ACC_GOLD:     base = h->gold_bonus_pct; break;
        case ACC_XP:       base = h->xp_bonus_pct; break;
        case ACC_REGEN:    base = 0; break;   /* no innate regen -- entirely from gear */
        default: break;
    }
    if (h->ring_stat == which) base += h->ring_bonus;
    if (h->trinket_stat == which) base += h->trinket_bonus;
    return base;
}

/* ---- world size --------------------------------------------------------
 * Named for the place rather than the file size: "normal" would imply the
 * others are compromises, and none of them is. Defaults to the Deeps so that
 * anything which forgets to choose -- a test harness, a tool -- still gets a
 * coherent world rather than a zero-sized one. */
int g_map_w = 700;
int g_map_h = 400;

static const struct { const char *name; int w, h; const char *blurb; } WORLD_SIZES[WORLD_SIZE_COUNT] = {
    { "The Shaft", 140,  80, "Tight and quick. A floor is cleared in minutes, and the stairs are never far." },
    { "The Halls", 350, 200, "Room to get lost in without losing the afternoon. A floor is one sitting." },
    { "The Deeps", 700, 400, "Each floor is a journey. You will not see all of one, and that is the point." },
    { "The Well", 1400, 800, "Vast. A hundred times the Shaft. Crossing a single floor is the evening's work." },
};

const char *world_size_name(int size) {
    if (size < 0 || size >= WORLD_SIZE_COUNT) size = WORLD_DEEPS;
    return WORLD_SIZES[size].name;
}

const char *world_size_blurb(int size) {
    if (size < 0 || size >= WORLD_SIZE_COUNT) size = WORLD_DEEPS;
    return WORLD_SIZES[size].blurb;
}

void world_size_dims(int size, int *out_w, int *out_h) {
    if (size < 0 || size >= WORLD_SIZE_COUNT) size = WORLD_DEEPS;
    if (out_w) *out_w = WORLD_SIZES[size].w;
    if (out_h) *out_h = WORLD_SIZES[size].h;
}

void world_size_apply(int size) {
    int w, h;
    world_size_dims(size, &w, &h);
    g_map_w = w;
    g_map_h = h;
}

int work_available_at(const Map *m, int x, int y) {
    if (!m || m->floor_num <= 0) return WORK_NONE;
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return WORK_NONE;

    if (m->tiles[y][x].type == TILE_ORE) return WORK_MINE;
    if (m->tiles[y][x].type == TILE_ROD)  return WORK_HARVEST_ROD;
    if (m->tiles[y][x].type == TILE_VENT) return WORK_HARVEST_VENT;

    /* Rubble in an old quarter is a collapsed machine worth stripping.
       The same rubble anywhere else is just rubble. */
    if (m->tiles[y][x].type == TILE_DECOR && district_at(m, x, y) == DIST_RUINS)
        return WORK_SALVAGE;

    /* And rubble in a boneyard is a dead golem worth opening. Same tile, same
       reasoning, different district -- which is why work is keyed on the pair
       rather than on a tile type of its own. */
    if (m->tiles[y][x].type == TILE_DECOR && district_at(m, x, y) == DIST_BONEYARD)
        return WORK_CORE;

    return WORK_NONE;
}

int work_turns_for(int kind) {
    switch (kind) {
        case WORK_HARVEST_ROD:
        case WORK_HARVEST_VENT: return WORK_ROD_TURNS;
        case WORK_SALVAGE:     return WORK_SALVAGE_TURNS;
        case WORK_MINE:        return WORK_MINE_TURNS;
        case WORK_CORE:        return WORK_CORE_TURNS;
        default:               return 0;
    }
}

const char *work_verb(int kind) {
    switch (kind) {
        case WORK_HARVEST_ROD: return "cutting the ore out of the rod";
        case WORK_HARVEST_VENT: return "breaking the crust off the vent";
        case WORK_SALVAGE:     return "stripping the wreck";
        case WORK_MINE:        return "cutting the vein out";
        case WORK_CORE:        return "cracking the core open";
        default:               return "working";
    }
}

static const char *MATERIAL_NAMES[MAT_COUNT] = { "scrap", "platinum", "diamond" };

const char *material_name(int tier) {
    if (tier < 0 || tier >= MAT_COUNT) return "?";
    return MATERIAL_NAMES[tier];
}

int material_tier_for_floor(int floor_num) {
    /* The cuts follow the biome bands (get_biome_for_floor: 15/35/60/85), and
       that is a correction rather than a tidy-up.
     *
       Platinum used to start at floor 41 while the smith starts demanding it
       at +20 (UPGRADE_PLATINUM_FROM). So the rung that needs platinum arrived
       long before the floor that drops it, and the only way through was to
       reach floor 41 on upgrades the smith would not sell you. The comment
       already said "Ruins and Wastes"; the Ruins begin at 36, so the number
       did not even match its own description.

       Same for diamond: it read 81 against an Abyss that begins at 86. */
    if (floor_num >= 86) return MAT_DIAMOND;    /* the Abyss */
    if (floor_num >= 36) return MAT_PLATINUM;   /* Ruins and Wastes */
    return MAT_SCRAP;
}

int waygate_interval(void) {
    /* Off the live map size rather than the WorldSize enum, so it stays
       right if the dimensions are ever set some other way. */
    return ((long)g_map_w * g_map_h) >= 280000L ? 1 : 5;
}

bool is_walkable_player(const Map *m, int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return false;
    TileType t = m->tiles[y][x].type;
    /* lava and miasma are walkable but hazardous -- handled by the caller.
       A locked door blocks like a wall for pathing purposes; unlocking it
       is a deliberate special-cased interaction, not a walkability check. */
    return t != TILE_WALL && t != TILE_WATER && t != TILE_LOCKED_DOOR &&
           t != TILE_SEALED_DOOR && t != TILE_THICKET &&
           t != TILE_CRYSTAL && t != TILE_ROD &&
           t != TILE_VENT;    /* TILE_CURRENT is walkable */
}

bool is_walkable_monster(const Map *m, int x, int y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return false;
    switch (m->tiles[y][x].type) {
        case TILE_WALL:
        case TILE_WATER:
        case TILE_LOCKED_DOOR:
        case TILE_SEALED_DOOR:
        case TILE_LAVA:
        case TILE_MIASMA:
        case TILE_THICKET:
        case TILE_CRYSTAL:
        case TILE_ROD:
        case TILE_VENT:
        /* Nothing else walks into a snare. That is the point of one: ground
           you can take and they cannot, at a price only you pay. */
        case TILE_SNARE:
        /* Nor into a hole in the mine floor, and for the same reason twice
           over. It is a *decision* -- health for a floor skipped -- and a
           decision is not something to be walked into by a hire following
           orders or by auto-explore taking the shortest route. It would also
           be a floor transition nobody asked for, which is exactly the rule
           the path searches already apply to the way back up. */
        case TILE_PIT:
            return false;
        default:
            return true;
    }
}

/* Walkability for anything walking under orders rather than under its own
   judgement: the player on auto-explore, and hired companions.
 *
   Everything is_walkable_monster() rejects, plus every tile that *moves you*
   -- the aqueduct's current and the assembly line's belts. Both are walkable
   and a monster is welcome to be carried by one; that is the district's whole
   mechanic. But a *delegated* walk that steps onto conveyed ground gets
   carried off its route, re-paths, steps back on, and is carried off again.
   It never trips the stuck detector either, because the position keeps
   changing: from the outside it looks like progress, and from the player's
   chair it is minutes of being shoved against a wall.

   So a delegated walk routes around conveyed ground when it can. The
   hazard-allowing second pass still crosses it when there is genuinely no
   other way, and being carried is then the fast way through rather than a
   trap.

   The belt was missing from this list for as long as the belt has existed:
   the reasoning above was written for the current and then not re-applied
   when the assembly line shipped the same idea with a different tile. It is
   asserted over the tile *types* in tests/movement.c now, so the next
   conveying tile somebody adds fails the suite rather than a play session. */
bool is_walkable_delegated(const Map *m, int x, int y) {
    if (!is_walkable_monster(m, x, y)) return false;
    TileType t = m->tiles[y][x].type;
    return t != TILE_CURRENT && t != TILE_BELT;
}

Monster *monster_at(Map *m, int x, int y) {
    for (int i = 0; i < m->monster_count; i++) {
        if (m->monsters[i].alive && m->monsters[i].x == x && m->monsters[i].y == y) {
            return &m->monsters[i];
        }
    }
    return NULL;
}

/* See common.h. Looked up every call rather than cached: these are read a
   handful of times per floor generation, never in a turn loop, and a cache
   would mean a sweep could not change them between runs in one process. */
int tunable(const char *env_name, int fallback) {
    const char *v = getenv(env_name);
    if (!v || !*v) return fallback;
    long n = strtol(v, NULL, 10);
    return (int)n;
}

/* ---- whose body you are driving ---------------------------------------
 *
 * `controlled` indexes party[] directly. Slot 0 is the character the run was
 * started as, which is also what a memset gives, so a run with nobody hired
 * drives the character without anybody having to write it down.
 *
 * There is no MC branch anywhere in this section any more, and that is the
 * whole point of the refactor rather than a tidy-up: the character used to be
 * a different kind of thing from a hire, so every one of these functions had
 * to say so twice, and the places that forgot were the bugs. One array, one
 * predicate, one loop.
 */

int party_x(const Player *p) { return p ? hero_driven_c(p)->x : 0; }
int party_y(const Player *p) { return p ? hero_driven_c(p)->y : 0; }

const Hero *party_body(const Player *p) {
    /* Kept returning NULL for "the character is the one being driven", which
       is what the existing callers test. Everything else should be asking
       hero_driven() and not caring. */
    if (!p || p->controlled == 0) return NULL;
    const Hero *h = hero_driven_c(p);
    return h == &p->party[0] ? NULL : h;
}

bool party_anyone_alive(const Player *p) {
    if (!p) return false;
    for (int i = 0; i < MAX_PARTY; i++)
        if (hero_is_up(&p->party[i])) return true;
    return false;
}

/* The body you were driving has fallen but the party has not. Hands the role
   on rather than ending the run -- "as soon as we change to the new hero, he
   de facto becomes the MC", so a run continues as whoever is left holding it.
   Returns false only when there is genuinely nobody, which is the game over.

   Note what is *not* here: nothing transfers. The purse and the pack belong to
   the party, and every body has always had its own weapon and spells, so
   handing the role on is a change of index and nothing else. */
bool party_pass_the_torch(Player *p) {
    if (!p) return false;
    if (hero_is_up(hero_driven_c(p))) return true;   /* still standing */

    for (int i = 0; i < MAX_PARTY; i++) {
        if (i == p->controlled || !hero_is_up(&p->party[i])) continue;
        p->controlled = i;
        log_msg("%s takes up the lantern. The run is theirs now.", p->party[i].name);
        return true;
    }
    return false;
}

bool party_switch_next(Player *p) {
    if (!p) return false;

    /* Walk the ring from wherever we are and stop at the first body that can
       be driven. Starting from the *current* index means a press always moves
       on rather than re-selecting whoever is nearest.

       Landing back where we started is a failure, not a success: a lone
       character pressing Tab has not switched to anybody, and reporting that
       as a switch would print "You are Wanderer" at somebody who never
       stopped being Wanderer. */
    int from = p->controlled;
    for (int step = 1; step < MAX_PARTY; step++) {
        int next = (from + step) % MAX_PARTY;
        if (hero_is_up(&p->party[next])) { p->controlled = next; return true; }
    }
    return false;
}


/* Spend one of the Oracle's readings on the floor just generated.
 *
   Lives here rather than in main.c because everything in main.c is static and
   therefore untestable, and "did the player get what they paid for" is exactly
   the assertion worth having on a shop that sells information: the cheap
   reading and the dear one look identical from the buyer's side on the turn
   they pay, and only differ in how much of the floor is actually known. */
void oracle_spend_reading(Map *m, Player *p) {
    if (!m || !p || p->oracle_floors <= 0) return;

    if (p->oracle_full) {
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++)
                if (m->tiles[y][x].type != TILE_WALL) m->tiles[y][x].seen = true;
        log_msg("The Oracle's reading settles over the floor. You have been here before.");
    } else {
        /* Only the way down, and only the square itself -- a corridor of
           memory leading to it would be a different, larger purchase. */
        if (m->stairs_down_x >= 0 && m->stairs_down_x < MAP_W &&
            m->stairs_down_y >= 0 && m->stairs_down_y < MAP_H)
            m->tiles[m->stairs_down_y][m->stairs_down_x].seen = true;
        log_msg("You know where the stairs are. Nothing else, but that.");
    }
    p->oracle_floors--;
}
