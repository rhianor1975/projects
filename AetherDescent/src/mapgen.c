#include "mapgen.h"
#include "vision.h"
#include "monsters.h"
#include "items.h"
#include "gearsets.h"
#include "save.h"   /* the fallen roster: a barrow only generates if there is
                       somebody to put in it -- see pick_district_kind() */

#define MAX_ROOMS SCALE_BY_AREA(100)
#define MAX_ROOMS_MAX SCALE_BY_AREA_MAX(100)
#define ROOM_MIN 5
#define ROOM_MAX 11
#define ROOM_ATTEMPTS SCALE_BY_AREA(30000)

/* ---- room shapes ------------------------------------------------------
 * Every room used to be 5-11 on both axes: sixty draws from one shape, which
 * makes a floor read as sixty of the same place. These are the shapes a
 * floor is actually made of.
 *
 * Placement runs largest-first. A plaza that cannot find 26x18 of clear rock
 * has to be tried before the map is full of cells, or it never lands at all;
 * cells fit almost anywhere and are the right thing to fill the gaps with.
 */
typedef enum {
    ROOM_PLAZA,    /* big and open -- the room you fight in the middle of */
    ROOM_HUB,      /* square, and wired to more of the floor than anything else */
    ROOM_HALL,     /* long and narrow, one axis or the other */
    ROOM_CHAMBER,  /* the old default */
    ROOM_CELL,     /* small, tight, everywhere */
    ROOM_KIND_COUNT
} RoomKind;

/* How many of each to attempt, and the size band for each. Counts are for a
   140x80 floor and scale with area, so the mix survives a bigger map. */
typedef struct {
    int per_10k_tiles;              /* how many to try, per 10,000 tiles */
    int w_min, w_max, h_min, h_max;
} RoomSpec;

static const RoomSpec ROOM_SPECS[ROOM_KIND_COUNT] = {
    /* plaza   */ {  3, 16, 26, 10, 16 },
    /* hub     */ {  4, 10, 14, 10, 14 },
    /* hall    */ { 10, 14, 24,  4,  6 },
    /* chamber */ { 31,  7, 11,  6,  9 },
    /* cell    */ { 35,  4,  6,  4,  6 },
};
/* These counts went up by half when the corridors got shorter. The old
   numbers only reached 54% walkable because every corridor crossed the whole
   map and the passages were doing a sixth of the filling; once rooms join
   their nearest neighbour instead, the rooms have to carry it themselves. */

static int spec_count(const RoomSpec *sp) {
    long tiles = (long)MAP_W * MAP_H;
    int n = (int)(sp->per_10k_tiles * tiles / 10000);
    return n < 1 ? 1 : n;
}

typedef struct { int x, y, w, h; } Room;

Biome get_biome_for_floor(int floor_num) {
    if (floor_num <= 15) return BIOME_JUNGLE;
    if (floor_num <= 35) return BIOME_INDUSTRIAL;
    if (floor_num <= 60) return BIOME_RUINS;
    if (floor_num <= 85) return BIOME_WASTES;
    return BIOME_ABYSS;
}

const char *biome_name(Biome b) {
    switch (b) {
        case BIOME_JUNGLE:     return "the Sunken Jungle Roots";
        case BIOME_INDUSTRIAL: return "the Company Works";
        case BIOME_RUINS:      return "the Flooded Ruins";
        case BIOME_WASTES:     return "the Salt Wastes";
        case BIOME_ABYSS:      return "the Abyssal Approach";
        default:                return "the temple";
    }
}

/* How much a pile is actually worth on this floor. One place, so a gold
   rush cannot apply to some piles and not others. */
static int gold_value(const Map *m, int amount) {
    return m->gold_rush ? amount * GOLD_RUSH_MULT : amount;
}

static void biome_terrain_params(Biome b, int *lake_min, int *lake_max,
                                  int *lava_min, int *lava_max,
                                  int *miasma_min, int *miasma_max) {
    switch (b) {
        case BIOME_JUNGLE:
            *lake_min = 2; *lake_max = 4; *lava_min = 0; *lava_max = 0; *miasma_min = 0; *miasma_max = 1;
            break;
        case BIOME_INDUSTRIAL:
            *lake_min = 0; *lake_max = 1; *lava_min = 3; *lava_max = 6; *miasma_min = 1; *miasma_max = 3;
            break;
        case BIOME_RUINS:
            *lake_min = 2; *lake_max = 4; *lava_min = 0; *lava_max = 1; *miasma_min = 1; *miasma_max = 2;
            break;
        case BIOME_WASTES:
            *lake_min = 0; *lake_max = 1; *lava_min = 1; *lava_max = 2; *miasma_min = 3; *miasma_max = 6;
            break;
        default: /* BIOME_ABYSS */
            *lake_min = 1; *lake_max = 2; *lava_min = 3; *lava_max = 6; *miasma_min = 3; *miasma_max = 6;
            break;
    }
}

/* Room placement asks the same question tens of thousands of times: is this
   rectangle clear? Asking it by scanning every room already placed is
   O(attempts x rooms), which at four hundred rooms is most of the cost of
   generating a floor -- and it is the reason the packing used to run out of
   attempts before it ran out of space.
 *
   So keep a grid instead. Everything a room must avoid -- other rooms, wild
   districts, the camp, open water and lava -- is stamped into it *dilated by
   one tile*, so testing the bare rectangle enforces the same one-tile gap
   `rooms_overlap` did. An attempt now costs the area of the room it is
   trying to place, and usually bails on the first few tiles. */
static bool g_occ[MAP_H_MAX][MAP_W_MAX];

static void occ_clear(void) { memset(g_occ, 0, sizeof(g_occ)); }

static void occ_mark_rect(int x0, int y0, int w, int h) {
    for (int y = y0 - 1; y <= y0 + h; y++) {
        if (y < 0 || y >= MAP_H) continue;
        for (int x = x0 - 1; x <= x0 + w; x++) {
            if (x < 0 || x >= MAP_W) continue;
            g_occ[y][x] = true;
        }
    }
}

/* Water and lava, each dilated by one, so a room never opens onto a lake. */
static void occ_mark_hazards(const Map *m) {
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            TileType t = m->tiles[y][x].type;
            if (t != TILE_WATER && t != TILE_LAVA) continue;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int ny = y + dy, nx = x + dx;
                    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
                    g_occ[ny][nx] = true;
                }
        }
    }
}

static bool occ_free(int x0, int y0, int w, int h) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            if (g_occ[y][x]) return false;
    return true;
}

static bool rooms_overlap(Room a, Room b) {
    return a.x <= b.x + b.w + 1 && a.x + a.w + 1 >= b.x &&
           a.y <= b.y + b.h + 1 && a.y + a.h + 1 >= b.y;
}

/* Can the player physically walk from (sx,sy) to (tx,ty), fog
   ignored? Hazards count as passable here: wading lava is a bad floor, not
   an impossible one, and the caller only wants to know whether a route
   exists at all. */
bool tile_reachable(const Map *m, int sx, int sy, int tx, int ty) {

    static bool seen[MAP_H_MAX][MAP_W_MAX];
    static int qx[MAP_W_MAX * MAP_H_MAX], qy[MAP_W_MAX * MAP_H_MAX];
    memset(seen, 0, sizeof(seen));

    int h = 0, t = 0;
    qx[t] = sx; qy[t] = sy; t++;
    seen[sy][sx] = true;

    while (h < t) {
        int cx = qx[h], cy = qy[h]; h++;
        if (cx == tx && cy == ty) return true;
        for (int d = 0; d < 8; d++) {
            static const int ddx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
            static const int ddy[8] = { 0, 0,-1, 1, -1,-1,  1, 1};
            int nx = cx + ddx[d], ny = cy + ddy[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (seen[ny][nx]) continue;
            if (!is_walkable_player(m, nx, ny)) continue;
            seen[ny][nx] = true;
            qx[t] = nx; qy[t] = ny; t++;
        }
    }
    return false;
}

static void carve_room(Map *m, Room r) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            m->tiles[y][x].type = TILE_FLOOR;
        }
    }
}

static void carve_blob_over(Map *m, int cx, int cy, int radius, TileType from, TileType to) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            int dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy > radius * radius) continue;
            if (rand() % 100 >= 78) continue; /* ragged edge */
            if (m->tiles[y][x].type == from) m->tiles[y][x].type = to;
        }
    }
}

/* WALL -> FLOOR as usual; WATER -> BRIDGE so a corridor that must cross a
   lake gets a crossing built into it. LAVA is left alone: some corridors
   end up routed straight through a hazard, on purpose.
 *
   A corridor is a long room, so it has a width. Most are one tile -- a
   passage -- but some are two or three, which is an avenue, and a floor
   reads completely differently when the route between two quarters is
   something you can walk four abreast down rather than another crawlspace.
   The width is centred on the requested line so the corridor still meets
   the rooms at their middles. */
static void carve_strip(Map *m, int x, int y) {
    if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) return;
    TileType t = m->tiles[y][x].type;
    if (t == TILE_WALL) m->tiles[y][x].type = TILE_FLOOR;
    else if (t == TILE_WATER) m->tiles[y][x].type = TILE_BRIDGE;
}

static void carve_h_corridor(Map *m, int x0, int x1, int y, int width) {
    int lo = x0 < x1 ? x0 : x1, hi = x0 < x1 ? x1 : x0;
    int off = (width - 1) / 2;
    for (int x = lo; x <= hi; x++)
        for (int k = 0; k < width; k++) carve_strip(m, x, y - off + k);
}

static void carve_v_corridor(Map *m, int y0, int y1, int x, int width) {
    int lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
    int off = (width - 1) / 2;
    for (int y = lo; y <= hi; y++)
        for (int k = 0; k < width; k++) carve_strip(m, x - off + k, y);
}

/* One passage in four is an avenue, one in twelve a boulevard. Rolled per
   corridor rather than per floor, so a floor has both. */
static int corridor_width(void) {
    int roll = rand() % 100;
    if (roll < 8)  return 3;
    if (roll < 26) return 2;
    return 1;
}

static void room_center(Room r, int *cx, int *cy) {
    *cx = r.x + r.w / 2;
    *cy = r.y + r.h / 2;
}

/* The nth-nearest room to `from`, or -1. Used wherever a room wants another
   room to link to: on a big floor "a random other room" is a road across the
   whole level, which is neither useful to walk nor cheap in wall. */
static int nearest_room(const Room *rooms, int count, int from, int nth) {
    int chosen = -1;
    long chosen_d = -1;
    for (int rank = 0; rank < nth; rank++) {
        int best = -1;
        long best_d = -1;
        int fx, fy; room_center(rooms[from], &fx, &fy);
        for (int i = 0; i < count; i++) {
            if (i == from) continue;
            int ix, iy; room_center(rooms[i], &ix, &iy);
            long dd = (long)(ix - fx) * (ix - fx) + (long)(iy - fy) * (iy - fy);
            if (dd <= chosen_d) continue;              /* already taken by an earlier rank */
            if (best_d < 0 || dd < best_d) { best_d = dd; best = i; }
        }
        if (best < 0) return chosen;
        chosen = best; chosen_d = best_d;
    }
    return chosen;
}

static void connect_rooms_wide(Map *m, Room a, Room b, int width) {
    int cx0, cy0, cx1, cy1;
    room_center(a, &cx0, &cy0);
    room_center(b, &cx1, &cy1);
    if (rand() % 2) {
        carve_h_corridor(m, cx0, cx1, cy0, width);
        carve_v_corridor(m, cy0, cy1, cx1, width);
    } else {
        carve_v_corridor(m, cy0, cy1, cx0, width);
        carve_h_corridor(m, cx0, cx1, cy1, width);
    }
}

static void connect_rooms(Map *m, Room a, Room b) {
    connect_rooms_wide(m, a, b, corridor_width());
}

static void place_feature(Map *m, Room *rooms, int room_count, FeatureType type) {
    if (m->feature_count >= MAX_FEATURES || room_count < 2) return;

    for (int tries = 0; tries < 40; tries++) {
        int ri = 1 + rand() % (room_count - 1);
        Room r = rooms[ri];
        int x = r.x + rand() % r.w;
        int y = r.y + rand() % r.h;

        if (m->tiles[y][x].type != TILE_FLOOR) continue;
        if (x == m->stairs_up_x && y == m->stairs_up_y) continue;
        if (x == m->stairs_down_x && y == m->stairs_down_y) continue;
        if (monster_at(m, x, y)) continue;

        bool occupied = false;
        for (int k = 0; k < m->feature_count; k++) {
            if (m->features[k].x == x && m->features[k].y == y) { occupied = true; break; }
        }
        if (occupied) continue;

        MapFeature *f = &m->features[m->feature_count++];
        memset(f, 0, sizeof(*f));
        f->type = type;
        f->x = x;
        f->y = y;
        f->used = false;

        if (type == FEATURE_RELIC) {
            f->relic_kind = rand() % RELIC_KIND_COUNT;
            f->relic_set_id = m->biome;
        }
        return;
    }
}

/* Placed in the arrival room itself (unlike place_feature, which
   deliberately avoids it) so it's found the moment you land on the floor. */
static void place_town_gate(Map *m, Room *rooms, int room_count) {
    if (room_count < 1 || m->feature_count >= MAX_FEATURES) return;
    Room r = rooms[0];

    for (int tries = 0; tries < 40; tries++) {
        int x = r.x + rand() % r.w;
        int y = r.y + rand() % r.h;
        if (m->tiles[y][x].type != TILE_FLOOR) continue;
        if (x == m->stairs_up_x && y == m->stairs_up_y) continue;

        MapFeature *f = &m->features[m->feature_count++];
        memset(f, 0, sizeof(*f));
        f->type = FEATURE_TOWN_GATE;
        f->x = x;
        f->y = y;
        f->used = false;
        /* The room it stands in, for the auto-explore stop. Recorded here
           because Room is local to this file and the Map does not keep one. */
        m->waygate_room_x = r.x;
        m->waygate_room_y = r.y;
        m->waygate_room_w = r.w;
        m->waygate_room_h = r.h;
        return;
    }
}

/* Walls off a small corner of an already-carved room into a self-contained
   vault with a single TILE_LOCKED_DOOR entrance and a guaranteed reward,
   then drops the key that opens it in a different room entirely. Never
   touches corridor topology, so it can't break floor connectivity. */
static void maybe_place_vault(Map *m, Room *rooms, int room_count, int floor_num) {
    if (room_count < 3) return;
    if (rand() % 100 >= 18) return;

    for (int tries = 0; tries < 20; tries++) {
        int ri = 1 + rand() % (room_count - 1);
        Room r = rooms[ri];
        if (r.w < 6 || r.h < 5) continue;

        int vx = r.x + 1, vy = r.y + 1;
        int vw = 3, vh = 3;
        if (vx + vw >= r.x + r.w - 1 || vy + vh >= r.y + r.h - 1) continue;

        for (int yy = vy - 1; yy <= vy + vh; yy++) {
            for (int xx = vx - 1; xx <= vx + vw; xx++) {
                bool border = (yy == vy - 1 || yy == vy + vh || xx == vx - 1 || xx == vx + vw);
                if (border) m->tiles[yy][xx].type = TILE_WALL;
            }
        }

        int door_x = vx + vw;
        int door_y = vy + vh / 2;
        m->tiles[door_y][door_x].type = TILE_LOCKED_DOOR;

        if (m->item_count < MAX_FLOOR_ITEMS) {
            FloorItem *fi = &m->items[m->item_count++];
            memset(fi, 0, sizeof(*fi));
            fi->x = vx + vw / 2;
            fi->y = vy + vh / 2;
            fi->is_gold = true;
            fi->gold_amount = gold_value(m, 80 + floor_num * 3 + rand() % 60);
        }

        int key_ri;
        do { key_ri = 1 + rand() % (room_count - 1); } while (key_ri == ri && room_count > 2);
        Room kr = rooms[key_ri];
        if (m->item_count < MAX_FLOOR_ITEMS) {
            FloorItem *fi = &m->items[m->item_count++];
            memset(fi, 0, sizeof(*fi));
            fi->x = kr.x + rand() % kr.w;
            fi->y = kr.y + rand() % kr.h;
            fi->is_key = true;
            strncpy(fi->name, "Brass Key", sizeof(fi->name) - 1);
        }
        return;
    }
}

/* Same walled-corner-vault construction as maybe_place_vault, but sealed by
   a lever elsewhere on the floor instead of a key -- a real interact-to-
   open mechanism rather than an inventory check. Company Works floors get
   this far more often, since it's their signature: actual machinery, not
   just the random-roll FEATURE_MACHINE. At most one per floor; the pairing
   is recorded on the Map so actor_step can open the right door
   when the lever is pulled. */
static void maybe_place_lever_vault(Map *m, Room *rooms, int room_count, int floor_num) {
    m->lever_x = -1;
    m->lever_y = -1;
    if (room_count < 3) return;
    int chance = (m->biome == BIOME_INDUSTRIAL) ? 35 : 15;
    if (rand() % 100 >= chance) return;

    for (int tries = 0; tries < 20; tries++) {
        int ri = 1 + rand() % (room_count - 1);
        Room r = rooms[ri];
        if (r.w < 6 || r.h < 5) continue;

        int vx = r.x + 1, vy = r.y + 1;
        int vw = 3, vh = 3;
        if (vx + vw >= r.x + r.w - 1 || vy + vh >= r.y + r.h - 1) continue;

        for (int yy = vy - 1; yy <= vy + vh; yy++) {
            for (int xx = vx - 1; xx <= vx + vw; xx++) {
                bool border = (yy == vy - 1 || yy == vy + vh || xx == vx - 1 || xx == vx + vw);
                if (border) m->tiles[yy][xx].type = TILE_WALL;
            }
        }

        int door_x = vx + vw;
        int door_y = vy + vh / 2;
        m->tiles[door_y][door_x].type = TILE_SEALED_DOOR;

        if (m->item_count < MAX_FLOOR_ITEMS) {
            FloorItem *fi = &m->items[m->item_count++];
            memset(fi, 0, sizeof(*fi));
            fi->x = vx + vw / 2;
            fi->y = vy + vh / 2;
            fi->is_gold = true;
            fi->gold_amount = gold_value(m, 90 + floor_num * 3 + rand() % 70);
        }

        int lever_ri;
        do { lever_ri = 1 + rand() % (room_count - 1); } while (lever_ri == ri && room_count > 2);
        Room lr = rooms[lever_ri];
        int lx = lr.x + rand() % lr.w, ly = lr.y + rand() % lr.h;
        m->tiles[ly][lx].type = TILE_LEVER;

        m->lever_x = lx;
        m->lever_y = ly;
        m->lever_door_x = door_x;
        m->lever_door_y = door_y;
        return;
    }
}

static bool has_feature_type(const Map *m, FeatureType type) {
    for (int i = 0; i < m->feature_count; i++) {
        if (m->features[i].type == type) return true;
    }
    return false;
}

/* What one piece of scrap off this floor is worth at the junkyard.
 *
 * Multiplicative in depth, not additive. Gold piles grow as `80 + floor*3`,
 * which is linear, while the upgrade tracks demand something far steeper --
 * EVALUATION §26 measured the mismatch at four and a half orders of
 * magnitude. Scrap is the lever that can actually bend: each biome doubles
 * it, so the Abyss pays about sixteen times what the Jungle does for the
 * same armful. */
static int junk_worth(int floor_num);

int junk_value_for_floor(int floor_num) { return junk_worth(floor_num); }

static int junk_worth(int floor_num) {
    int biome_step = floor_num / 20;          /* 0..4 across the five biomes */
    int base = 8 + floor_num;
    int mult = 1 << (biome_step > 4 ? 4 : biome_step);
    return base * mult;
}

/* Scrap is no longer strewn about the floor. It was, briefly, and it was the
   wrong shape: a second kind of pickup competing for the same attention as
   loot, drawing auto-explore into detours for litter, and cluttering the map
   with '%' that had to be told apart from a potion at a glance.

   It arrives by walking instead (JUNK_STEPS_PER_PIECE). Same currency, same
   per-floor value curve, none of the busywork -- and it was always the larger
   share of the income anyway. */

static void add_gold_pile(Map *m, Room *rooms, int room_count, int amount) {
    if (m->item_count >= MAX_FLOOR_ITEMS || room_count < 2) return;
    int ri = 1 + rand() % (room_count - 1);
    Room r = rooms[ri];
    FloorItem *fi = &m->items[m->item_count++];
    memset(fi, 0, sizeof(*fi));
    fi->x = r.x + rand() % r.w;
    fi->y = r.y + rand() % r.h;
    fi->is_gold = true;
    fi->gold_amount = gold_value(m, amount);
}

static void apply_random_event(Map *m, int floor_num, Room *rooms, int room_count, const Player *p) {
    if (floor_num >= MAX_FLOOR) return;
    if (room_count < 3) return;
    if (rand() % 100 >= 45) return; /* 45% chance something happens */

    int pick = rand() % 9;
    if ((pick == 0 || pick == 6) && p && rand() % 100 < hero_driven_c(p)->ambush_resist_pct) {
        pick = 8; /* instinct and guile talk you out of the ambush */
    }

    switch (pick) {
        case 0: { /* Ambush! */
            strncpy(m->event_name, "Ambush!", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "Something was waiting for you.", sizeof(m->event_desc) - 1);
            int ri = 1 + rand() % (room_count - 1);
            Room r = rooms[ri];
            int n = 3 + rand() % 3;
            for (int k = 0; k < n && m->monster_count < MAX_MONSTERS; k++) {
                int x = r.x + rand() % r.w, y = r.y + rand() % r.h;
                if (monster_at(m, x, y)) continue;
                m->monsters[m->monster_count] = make_monster_for_floor(floor_num, x, y);
                m->monsters[m->monster_count].aggro = true;
                m->monster_count++;
            }
            break;
        }
        case 1: { /* Trader's Camp */
            strncpy(m->event_name, "Trader's Camp", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "Someone set up shop down here.", sizeof(m->event_desc) - 1);
            if (!has_feature_type(m, FEATURE_MERCHANT)) place_feature(m, rooms, room_count, FEATURE_MERCHANT);
            break;
        }
        case 2: { /* Old Ones Shrine */
            strncpy(m->event_name, "Old Ones Shrine", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "Something here still remembers being worshipped.", sizeof(m->event_desc) - 1);
            if (!has_feature_type(m, FEATURE_SHRINE)) place_feature(m, rooms, room_count, FEATURE_SHRINE);
            break;
        }
        case 3: { /* Flooded Passage */
            strncpy(m->event_name, "Flooded Passage", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "Water found a way in a long time ago.", sizeof(m->event_desc) - 1);
            int cx = 5 + rand() % (MAP_W - 10), cy = 5 + rand() % (MAP_H - 10);
            carve_blob_over(m, cx, cy, 3 + rand() % 3, TILE_WALL, TILE_WATER);
            break;
        }
        case 4: { /* Toxic Vent Rupture */
            strncpy(m->event_name, "Toxic Vent Rupture", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "The air here tastes like a struck match.", sizeof(m->event_desc) - 1);
            for (int i = 0; i < 2; i++) {
                int ri = 1 + rand() % (room_count - 1);
                Room r = rooms[ri];
                carve_blob_over(m, r.x + rand() % r.w, r.y + rand() % r.h, 1 + rand() % 2, TILE_FLOOR, TILE_MIASMA);
            }
            break;
        }
        case 5: { /* Buried Cache */
            strncpy(m->event_name, "Buried Cache", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "Somebody hid something down here and never came back for it.", sizeof(m->event_desc) - 1);
            if (!has_feature_type(m, FEATURE_RELIC)) place_feature(m, rooms, room_count, FEATURE_RELIC);
            else add_gold_pile(m, rooms, room_count, 50 + floor_num * 2 + rand() % 50);
            break;
        }
        case 6: { /* Raiders' Den */
            strncpy(m->event_name, "Raiders' Den", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "A whole band of them, working together.", sizeof(m->event_desc) - 1);
            int ri = 1 + rand() % (room_count - 1);
            Room r = rooms[ri];
            Monster proto = make_monster_for_floor(floor_num, r.x + rand() % r.w, r.y + rand() % r.h);
            int n = 3 + rand() % 2;
            for (int k = 0; k < n && m->monster_count < MAX_MONSTERS; k++) {
                int x = r.x + rand() % r.w, y = r.y + rand() % r.h;
                if (monster_at(m, x, y)) continue;
                Monster copy = proto;
                copy.x = x;
                copy.y = y;
                copy.aggro = true;
                m->monsters[m->monster_count++] = copy;
            }
            add_gold_pile(m, rooms, room_count, 20 + floor_num);
            break;
        }
        case 7: { /* Ancient Machinery Awakens */
            strncpy(m->event_name, "Ancient Machinery Awakens", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "Something down here is still, technically, running.", sizeof(m->event_desc) - 1);
            if (!has_feature_type(m, FEATURE_MACHINE)) place_feature(m, rooms, room_count, FEATURE_MACHINE);
            break;
        }
        default: { /* Eerie Calm */
            strncpy(m->event_name, "Eerie Calm", sizeof(m->event_name) - 1);
            strncpy(m->event_desc, "Whatever usually lives here isn't home.", sizeof(m->event_desc) - 1);
            for (int i = 0; i < m->monster_count; i++) {
                if (rand() % 100 < 40) m->monsters[i].alive = false;
            }
            if (m->item_count < MAX_FLOOR_ITEMS) {
                const ConsumableTemplate *t = pick_floor_loot(floor_num);
                FloorItem *fi = &m->items[m->item_count++];
                memset(fi, 0, sizeof(*fi));
                Room r = rooms[1 + rand() % (room_count - 1)];
                fi->x = r.x + rand() % r.w;
                fi->y = r.y + rand() % r.h;
                fi->is_gold = false;
                strncpy(fi->name, t->name, sizeof(fi->name) - 1);
                fi->heal = t->heal;
                fi->atk_buff = t->atk_buff;
                fi->atk_buff_turns = t->atk_buff_turns;
                fi->def_buff = t->def_buff;
                fi->def_buff_turns = t->def_buff_turns;
                fi->is_recall = t->is_recall;
            }
            break;
        }
    }
}

unsigned int floor_seed(unsigned int run_seed, int floor_num) {
    unsigned int h = (run_seed ? run_seed : 1u) * 2654435761u
                   ^ ((unsigned int)floor_num + 0x9E3779B9u);
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return h ? h : 1u;
}


/* ---- wild districts ----------------------------------------------------
 * The floor is a cave that people built inside, and the cave was there
 * first. Not all of it got built on. Growth comes in through a crack and
 * takes a quarter of the level; water stands in the low ground and never
 * drained; a block of older ruins sits where nobody could be bothered to
 * clear it. Those are the districts, and they are laid out *before* any room
 * is, so the rooms have to go around them rather than the other way about.
 *
 * They are terrain, not rooms: no rectangle of clean floor, no doors, and
 * they get monsters and loot of their own. A district you can see across is
 * a district that failed.
 */

#define DIST_PER_10K 8   /* on the Shaft; scaled by world size below */

/* Which wilds turn up in which biome, as weights. The jungle biome is mostly
   overgrowth and standing water; industrial is old ruins with growth pushing
   in at the edges; the wastes are sour bog; the abyss is black water. */
/* The watch is a built thing, so it is commonest where people built: the
   works and the old quarter. The barrow is rare everywhere and rarest in the
   jungle, which never buried anybody -- and it is gated on there being ghosts
   to put in it besides (see pick_district_kind). */
static const int DIST_WEIGHT[BIOME_COUNT][DIST_KIND_COUNT] = {
    /*              jungle sea swamp ruins myc hive mire cryst quiet storm arena aque blood prism asm gard sand watch barrow eye mirror stone shaft bone prov chapel vent */
    /* JUNGLE     */ {  18,  8,  11,   5,  9,   6,   4,   4,    6,    5,    3,   5,    5,    3,   2,  10,   3,    3,    1,   2,    2,    8,    2,   1,   2,     2,    2 },
    /* INDUSTRIAL */ {   5,  4,   9,  20,  5,   6,   5,   6,    6,    8,    3,   8,    2,    3,  11,   2,   3,    9,    2,   5,    3,    2,    9,  10,   4,     3,    8 },
    /* RUINS      */ {  12,  7,   5,  17,  6,   5,   6,   7,    8,    4,    3,   7,    3,    2,   5,   4,   4,    7,    3,   4,    5,    5,    5,   6,   6,     6,    3 },
    /* WASTES     */ {   3,  3,  17,  17,  4,   5,  10,   6,    5,    8,    2,   4,    5,    2,   3,   3,  10,    5,    3,   7,    3,    6,    6,   4,   4,     4,    8 },
    /* ABYSS      */ {   3, 16,  12,   9,  7,   7,   7,   8,    6,    5,    2,   5,    4,    5,   3,   4,   5,    4,    4,   6,    7,    3,    4,   3,   5,     6,    6 },
};

/* A row short of DIST_KIND_COUNT is legal C and silently zero-fills, so a kind
   added to the enum without a column here simply never comes up. That is what
   happened to the chapel and the vent field on the first build of this change,
   and nothing warned -- tests/invariants.c caught it, which is what "every kind
   of wild turns up somewhere" is for. */

/* The short name of a place, for the moment you set foot in it. The note
   above is the floor-arrival blurb -- a paragraph you read once. This is what
   the ground under you is called, said as you cross into it. */
const char *wild_kind_name(int kind) {
    switch (kind) {
        case DIST_JUNGLE:     return "the overgrowth";
        case DIST_SEA:        return "the flooded quarter";
        case DIST_SWAMP:      return "the bog";
        case DIST_RUINS:      return "the old quarter";
        case DIST_MYCELIUM:   return "the fungal mats";
        case DIST_HIVE:       return "the comb";
        case DIST_MIRE:       return "the mist";
        case DIST_CRYSTAL:    return "the crystal field";
        case DIST_QUIET:      return "the quiet quarter";
        case DIST_STORM:      return "the rod field";
        case DIST_ARENA:      return "the sand";
        case DIST_AQUEDUCT:   return "the aqueduct";
        case DIST_BLOODMARSH: return "the blood marsh";
        case DIST_PRISM:      return "the prism floor";
        case DIST_ASSEMBLY:   return "the assembly line";
        case DIST_GARDEN:     return "the garden";
        case DIST_QUICKSAND:  return "the quicksand";
        case DIST_WATCH:      return "the watch";
        case DIST_GAUNTLET:   return "the barrow";
        case DIST_EYE:        return "the eye";
        case DIST_MIRROR:     return "the mirror";
        case DIST_PETRIFIED:  return "the stone wood";
        case DIST_SHAFT:      return "the workings";
        case DIST_BONEYARD:   return "the boneyard";
        case DIST_PROVING:    return "the proving ground";
        case DIST_CHAPEL:     return "the silence";
        case DIST_GEOTHERMAL: return "the vent field";
        default:              return NULL;
    }
}

const char *wild_kind_note(int kind) {
    switch (kind) {
        case DIST_JUNGLE: return "Growth has come in through the rock and taken a piece of this floor.";
        case DIST_SEA:    return "Standing water lies across part of it, with causeways laid over.";
        case DIST_SWAMP:  return "Part of it is bog -- reeds, gas off the surface, footing that shifts.";
        case DIST_RUINS:  return "An older quarter stands here, half-collapsed and long since left.";
        case DIST_MYCELIUM: return "Fungal mats breathe light somewhere below. The floor carries every step.";
        case DIST_HIVE:   return "Comb-work on the walls, and whatever built it does not hunt alone.";
        case DIST_MIRE:   return "Mist lies over part of this floor. You will not see far in it.";
        case DIST_CRYSTAL: return "Aether-charged crystal grows through one quarter. It throws magic back.";
        case DIST_QUIET:  return "Somewhere here, nothing is awake. It would stay that way if you let it.";
        case DIST_STORM:  return "Iron rods stand in rows under a ceiling that will not stop arcing.";
        case DIST_ARENA:  return "Somebody cut an amphitheatre into the rock. The sand is dark.";
        case DIST_AQUEDUCT: return "Cut channels run through here, and the water in them is moving.";
        case DIST_BLOODMARSH: return "The water here is red and warm, and what wades in it does not stay hurt.";
        case DIST_PRISM:  return "Pools of liquid light. The ground you fight on will matter here.";
        case DIST_ASSEMBLY: return "Belt-work, still running. The lanes go opposite ways, and they do not stop.";
        case DIST_GARDEN: return "Something is flowering down here, and the flowers have teeth.";
        case DIST_QUICKSAND: return "Flat pale sand, and a ripple in it that is not the wind.";
        case DIST_WATCH:  return "Something is posted here and it is awake. Start nothing and it stays that way.";
        case DIST_GAUNTLET: return "Graves cut into the rock, and the names on them are ones you gave.";
        case DIST_EYE:    return "The ceiling turns. One quarter of this place is quiet at a time, and it moves.";
        case DIST_MIRROR: return "Flat water, or glass. Something in it is keeping pace with you.";
        case DIST_PETRIFIED: return "The trees here went to stone standing up. You will not see far, or far ahead.";
        case DIST_SHAFT:  return "Somebody dug here and stopped. There are holes in the floor, and they go down.";
        case DIST_BONEYARD: return "Machines died here in numbers. Whatever is still in them is still in them.";
        case DIST_PROVING: return "Ground marked out for a fight with rules. They are not your rules.";
        case DIST_CHAPEL: return "Nothing carries here. You will not hear them coming, and they will not hear you.";
        case DIST_GEOTHERMAL: return "Vents in the rock, on a schedule the rock keeps. Stand clear when one sings.";
        default:          return NULL;
    }
}

/* How many dead runs this save directory can field, read once per floor.
   The barrow is the only kind whose *existence* depends on state outside the
   map, so this is the only place a carve decision reads a file. A floor's
   worth of fopen is nothing against 46 ms of generation, and caching it would
   mean a barrow that stays empty for the rest of the session after the run
   that would have filled it. */
static int fallen_available(void) {
    FallenRecord ring[FALLEN_MAX];
    return load_fallen(ring, FALLEN_MAX);
}

static DistrictKind pick_district_kind(Biome b) {
    const int *w = DIST_WEIGHT[b];

    /* A barrow with nobody in it is a room with piers in it. Until this save
       directory has lost a run, the kind simply does not come up. */
    bool have_ghosts = fallen_available() >= GAUNTLET_MIN_GHOSTS;

    int total = 0;
    for (int i = 0; i < DIST_KIND_COUNT; i++) {
        if (i == DIST_GAUNTLET && !have_ghosts) continue;
        total += w[i];
    }
    if (total <= 0) return DIST_RUINS;

    int roll = rand() % total;
    for (int i = 0; i < DIST_KIND_COUNT; i++) {
        if (i == DIST_GAUNTLET && !have_ghosts) continue;
        if (roll < w[i]) return (DistrictKind)i;
        roll -= w[i];
    }
    return DIST_RUINS;
}

/* A district has no edge, because nothing outdoors does. The rim tiles are
   dropped most of the time and the ring inside them sometimes, so the shape
   frays into the rock instead of stopping at a surveyor's line. */
static void fill_ragged(Map *m, Room r, TileType t) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            bool rim  = (x == r.x || x == r.x + r.w - 1 || y == r.y || y == r.y + r.h - 1);
            bool near = (x <= r.x + 1 || x >= r.x + r.w - 2 || y <= r.y + 1 || y >= r.y + r.h - 2);
            if (rim  && rand() % 100 < 72) continue;
            if (near && rand() % 100 < 28) continue;
            m->tiles[y][x].type = t;
        }
    }
}

static void scatter_blobs(Map *m, Room r, int count, int rmin, int rspread,
                          TileType from, TileType to) {
    for (int i = 0; i < count; i++) {
        int cx = r.x + rand() % r.w;
        int cy = r.y + rand() % r.h;
        carve_blob_over(m, cx, cy, rmin + rand() % rspread, from, to);
    }
}

/* Where a road can be brought in to meet the district. Walking out of a
   corridor into standing water would be a bug, so this hunts outward from
   the middle for ground you can actually stand on and the caller drops the
   district if there is none. */
static bool district_anchor(const Map *m, Room r, int *ax, int *ay) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    for (int rad = 0; rad < r.w + r.h; rad++) {
        for (int y = cy - rad; y <= cy + rad; y++) {
            for (int x = cx - rad; x <= cx + rad; x++) {
                if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
                TileType t = m->tiles[y][x].type;
                if (t == TILE_FLOOR || t == TILE_BRIDGE) { *ax = x; *ay = y; return true; }
            }
        }
    }
    return false;
}

/* ---- making a district one place rather than several -------------------
 *
 * A district is joined to the rest of the floor at exactly *one* tile: the
 * anchor above, which two roads are run to. That is fine for a district whose
 * inside is one connected piece and silently lossy for one that is not --
 * and most of them are not. Every carve function that scatters blocking
 * terrain (thickets in the jungle and the bog, standing water in the mire and
 * the sea, crystal veins, the ruins' huts) can cut its own interior into
 * pieces, and the pieces the anchor does not happen to land in are stranded:
 * unreachable unless a corridor bound somewhere else cuts through by luck.
 *
 * Nobody gets *stuck* in one, because nobody can get in. What is lost is the
 * monsters, the loot and the district's own rule -- quietly, on a large
 * fraction of floors. Measured before this pass existed, over 456 floors
 * (tests/movement.c): the overgrowth stranded 15.9% of its own ground and
 * lost more than a tenth of itself on 161 instances out of 308; the bog 8.0%,
 * the mist 9.5%, the flooded quarter 4.7%.
 *
 * The fix is deliberately *here* and not in the seventeen carve functions.
 * Every one of them would need the same repair written a slightly different
 * way, the eighteenth would forget, and none of them can see the thing that
 * matters anyway -- whether the blobs they scattered happened to close a
 * ring. This is one of the few places a global guard is the honest answer
 * rather than the lazy one: the property is global, so the check is.
 *
 * The threshold below started at 8 tiles, on the reasoning that a small gap
 * behind a thicket is the thicket doing its job and cutting a trail to it
 * would erode the exact thing the jungle exists for. Measurement said that
 * was backwards twice over. It did not work -- the overgrowth's residue is
 * *made of* small pockets, and at 8 it still lost a tenth of itself on 62
 * instances in 308 -- and the erosion argument runs the wrong way, because a
 * two-tile pocket is by definition adjacent to reachable ground and needs a
 * one-tile trail, where an eight-tile one needs a real cut. Small pockets are
 * the cheapest to open and the most numerous.
 *
 * At 2, the overgrowth loses a tenth of itself on 4 instances in 308 and
 * strands 3.9% of its ground, all of it single tiles nobody could stand in
 * usefully anyway. Those are left alone on purpose: a district with no
 * unreachable nook in it reads as a room.
 */
#define DISTRICT_POCKET_MIN 2   /* a single tile is scenery; two is a place */
#define DISTRICT_RELINK_MAX 12  /* trails cut per district, as a backstop */
/* A district rect is at most (20+21) x (12+14) scaled by district_size_pct(),
   which tops out at 316% on the Well: 129 x 82, or 10,578 tiles. Rounded up
   to the next power of two so the frontier stacks below cannot be overrun by
   a later change to either number. */
#define MAX_DISTRICT_TILES 16384

static unsigned char relink_seen[MAP_H_MAX][MAP_W_MAX];

/* Opens one tile for a trail. Wider than carve_strip's remit: that one runs
   corridors between rooms and has no business felling a thicket, whereas this
   is explicitly cutting a path through the district's own terrain. Water
   still becomes a bridge rather than dry land -- a trail across the flooded
   quarter is a plank, not a drained sea. */
static void trail_open(Map *m, int x, int y) {
    if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) return;
    switch (m->tiles[y][x].type) {
        case TILE_WATER:  m->tiles[y][x].type = TILE_BRIDGE; break;
        case TILE_WALL:
        case TILE_THICKET:
        case TILE_CRYSTAL:
        case TILE_ROD:
        case TILE_VENT:   m->tiles[y][x].type = TILE_FLOOR;  break;
        default: break;   /* already passable, including the hazards */
    }
}

/* Everything walkable inside the rect that can be reached from (ax,ay),
   left in relink_seen. Returns the count. */
static int relink_flood(const Map *m, Room r, int ax, int ay) {
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++)
            if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H)
                relink_seen[y][x] = 0;

    if (ax < r.x || ax >= r.x + r.w || ay < r.y || ay >= r.y + r.h) return 0;

    /* The rect is at most a few thousand tiles, so the frontier is bounded
       by it and a fixed stack is enough. */
    static int sx[MAX_DISTRICT_TILES], sy[MAX_DISTRICT_TILES];
    int top = 0, count = 0;
    relink_seen[ay][ax] = 1;
    sx[top] = ax; sy[top] = ay; top++;

    while (top > 0) {
        top--;
        int x = sx[top], y = sy[top];
        count++;
        static const int DX[4] = { 1, -1, 0, 0 };
        static const int DY[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < r.x || nx >= r.x + r.w || ny < r.y || ny >= r.y + r.h) continue;
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (relink_seen[ny][nx]) continue;
            if (!is_walkable_player(m, nx, ny)) continue;
            if (top >= MAX_DISTRICT_TILES) continue;
            relink_seen[ny][nx] = 1;
            sx[top] = nx; sy[top] = ny; top++;
        }
    }
    return count;
}

/* Cuts a trail from (px,py) back toward the anchor, stopping the moment it
   touches ground the anchor already reaches. Toward the anchor rather than
   to the nearest connected tile because the anchor is connected by
   definition, so the walk always terminates -- and because hunting for the
   nearest pair is quadratic in the rect and this is linear in it.

   It stops only on relink_seen == 1, the anchor's own component. A pocket
   that has merely been *counted* this pass reads as 2, and stopping there
   would dead-end the trail into ground that is itself cut off. */
static void relink_trail(Map *m, Room r, int px, int py, int ax, int ay) {
    int x = px, y = py;
    for (int steps = 0; steps < r.w + r.h + 4; steps++) {
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h &&
            x >= 0 && x < MAP_W && y >= 0 && y < MAP_H &&
            relink_seen[y][x] == 1 && is_walkable_player(m, x, y))
            return;                       /* joined up */
        trail_open(m, x, y);
        if (x == ax && y == ay) return;
        /* One axis at a time, the long one first, so the trail reads as a
           path somebody wore rather than as a diagonal cut. */
        if (abs(ax - x) >= abs(ay - y) && x != ax) x += (ax > x) ? 1 : -1;
        else if (y != ay)                          y += (ay > y) ? 1 : -1;
        else if (x != ax)                          x += (ax > x) ? 1 : -1;
    }
}

/* A district that shuts behind you is exempt: a second way in is not a
   repair, it is the removal of the mechanic. Those districts are carved as
   one connected space on purpose and the measurement agrees -- the sand
   stranded 0.09% of itself with no help at all. */
static bool district_seals(int kind) {
    return kind == DIST_ARENA || kind == DIST_GAUNTLET;
}

static void district_relink(Map *m, Room r, int kind, int ax, int ay) {
    if (district_seals(kind)) return;

    for (int pass = 0; pass < DISTRICT_RELINK_MAX; pass++) {
        relink_flood(m, r, ax, ay);

        /* Every pocket worth walking to, in one sweep, and a trail cut for
           each of them before re-flooding.
         *
           One trail per pass was the first shape of this and it did not
           converge: the overgrowth scatters a thicket blob every 26 tiles, so
           a jungle routinely comes out in a dozen pieces and twelve passes ran
           out before the pieces did (measured: 62 of 308 instances still
           losing a tenth of themselves). Cutting them all per pass converges
           in two, because the only reason to re-flood at all is to notice
           pockets that the trails just joined to each other. */
        int cut = 0;
        for (int y = r.y; y < r.y + r.h; y++) {
            for (int x = r.x; x < r.x + r.w; x++) {
                if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
                if (relink_seen[y][x]) continue;
                if (!is_walkable_player(m, x, y)) continue;

                /* Measure this pocket, marking it as we go so the scan does
                   not re-walk it. Value 2 = counted, not reached. */
                static int px[MAX_DISTRICT_TILES], py[MAX_DISTRICT_TILES];
                int top = 0, size = 0;
                int near_x = x, near_y = y;
                long near_d = -1;
                relink_seen[y][x] = 2;
                px[top] = x; py[top] = y; top++;
                while (top > 0) {
                    top--;
                    int cxx = px[top], cyy = py[top];
                    size++;
                    long dd = (long)abs(cxx - ax) + abs(cyy - ay);
                    if (near_d < 0 || dd < near_d) { near_d = dd; near_x = cxx; near_y = cyy; }
                    static const int DX[4] = { 1, -1, 0, 0 };
                    static const int DY[4] = { 0, 0, 1, -1 };
                    for (int k = 0; k < 4; k++) {
                        int nx = cxx + DX[k], ny = cyy + DY[k];
                        if (nx < r.x || nx >= r.x + r.w || ny < r.y || ny >= r.y + r.h) continue;
                        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
                        if (relink_seen[ny][nx]) continue;
                        if (!is_walkable_player(m, nx, ny)) continue;
                        if (top >= MAX_DISTRICT_TILES) continue;
                        relink_seen[ny][nx] = 2;
                        px[top] = nx; py[top] = ny; top++;
                    }
                }
                if (size < DISTRICT_POCKET_MIN) continue;   /* scenery, leave it */

                /* Cut toward the anchor now rather than collecting first.
                   The anchor is reachable by definition, so a trail is always
                   valid whatever else this pass has already opened -- and the
                   pocket-marking above (value 2) keeps the scan from finding
                   this same piece again. */
                relink_trail(m, r, near_x, near_y, ax, ay);
                cut++;
            }
        }

        if (cut == 0) return;   /* nothing left but scenery */
    }
}

/* Undergrowth with clearings in it. The thickets are the point: they break
   the sightlines, so you meet things in a jungle at four paces. */
static void carve_jungle(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 26, 1, 3, TILE_FLOOR, TILE_THICKET);
    scatter_blobs(m, r, 1 + rand() % 2, 2, 2, TILE_FLOOR, TILE_WATER);
    if (rand() % 2) scatter_blobs(m, r, 1, 1, 2, TILE_FLOOR, TILE_MIASMA);
    scatter_blobs(m, r, area / 90, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* Standing water with islands, and causeways over it -- one across, and
   often a second the other way, because a sea with a single crossing is a
   corridor wearing a costume. */
static void carve_sea(Map *m, Room r) {
    fill_ragged(m, r, TILE_WATER);

    int isles = 2 + rand() % 3;
    for (int i = 0; i < isles; i++) {
        int cx = r.x + 2 + rand() % (r.w - 4);
        int cy = r.y + 2 + rand() % (r.h - 4);
        carve_blob_over(m, cx, cy, 2 + rand() % 3, TILE_WATER, TILE_FLOOR);
    }

    int cy = r.y + 1 + rand() % (r.h - 2);
    for (int x = r.x; x < r.x + r.w; x++)
        if (m->tiles[cy][x].type == TILE_WATER) m->tiles[cy][x].type = TILE_BRIDGE;

    if (rand() % 100 < 65) {
        int cx = r.x + 1 + rand() % (r.w - 2);
        for (int y = r.y; y < r.y + r.h; y++)
            if (m->tiles[y][cx].type == TILE_WATER) m->tiles[y][cx].type = TILE_BRIDGE;
    }

    scatter_blobs(m, r, r.w * r.h / 120, 1, 2, TILE_FLOOR, TILE_THICKET);
}

/* Neither water nor ground for long enough to say which. Reeds, gas off the
   surface, and footing that keeps changing under you. */
static void carve_swamp(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 34, 1, 3, TILE_FLOOR, TILE_WATER);
    scatter_blobs(m, r, area / 64, 1, 2, TILE_FLOOR, TILE_MIASMA);
    scatter_blobs(m, r, area / 46, 1, 2, TILE_FLOOR, TILE_THICKET);
    scatter_blobs(m, r, area / 100, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* A quarter that was already old when the current tenants moved in: streets
   still legible, buildings standing to about knee height. The gaps in the
   walls are the character -- an intact grid of huts is a housing estate. */
static void carve_ruins(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);

    int want = r.w * r.h / 55;
    Room built[24];
    int nbuilt = 0;

    for (int tries = 0; tries < want * 8 && nbuilt < want && nbuilt < 24; tries++) {
        int bw = 4 + rand() % 5, bh = 3 + rand() % 4;
        if (bw + 4 >= r.w || bh + 4 >= r.h) continue;
        Room b = { r.x + 1 + rand() % (r.w - bw - 2),
                   r.y + 1 + rand() % (r.h - bh - 2), bw, bh };

        bool clash = false;
        for (int i = 0; i < nbuilt; i++)
            if (rooms_overlap(b, built[i])) { clash = true; break; }
        if (clash) continue;
        built[nbuilt++] = b;

        for (int y = b.y; y < b.y + b.h; y++) {
            for (int x = b.x; x < b.x + b.w; x++) {
                if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
                bool edge = (x == b.x || x == b.x + b.w - 1 || y == b.y || y == b.y + b.h - 1);
                if (edge) {
                    /* two courses in three still standing */
                    m->tiles[y][x].type = (rand() % 100 < 66) ? TILE_WALL : TILE_FLOOR;
                } else if (rand() % 100 < 22) {
                    m->tiles[y][x].type = TILE_DECOR;
                }
            }
        }
    }

    scatter_blobs(m, r, r.w * r.h / 70, 1, 2, TILE_FLOOR, TILE_DECOR);
    if (rand() % 100 < 40) scatter_blobs(m, r, 1, 2, 2, TILE_FLOOR, TILE_WATER);
}

/* Bioluminescent mats and giant caps. Reads like the jungle at a glance --
   thickets, damp -- and plays nothing like it: the growth is what carries
   sound, so the whole point is the *floor*, not the cover. Kept open enough
   that standing still is a real option; a maze would make stillness useless. */
static void carve_mycelium(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 40, 1, 2, TILE_FLOOR, TILE_THICKET);   /* caps, sparser than jungle */
    scatter_blobs(m, r, area / 55, 1, 3, TILE_FLOOR, TILE_DECOR);     /* the mats themselves */
    if (rand() % 100 < 60) scatter_blobs(m, r, 1, 1, 2, TILE_FLOOR, TILE_MIASMA);
}

/* Comb-work: cells, and a lot of them. Tight and repetitive on purpose --
   there is nowhere in a hive that is not overlooked by the rest of it. */
static void carve_hive(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);

    /* Rows of small chambers with gaps, so it reads as structure rather than
       rubble, and so a fight in one is heard in all of them. */
    for (int cy = r.y + 2; cy < r.y + r.h - 2; cy += 4) {
        for (int cx = r.x + 2; cx < r.x + r.w - 2; cx += 5) {
            for (int y = cy; y < cy + 3 && y < r.y + r.h - 1; y++) {
                for (int x = cx; x < cx + 4 && x < r.x + r.w - 1; x++) {
                    if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
                    bool edge = (x == cx || x == cx + 3 || y == cy || y == cy + 2);
                    if (edge && rand() % 100 < 55) m->tiles[y][x].type = TILE_WALL;
                }
            }
        }
    }
    scatter_blobs(m, r, r.w * r.h / 90, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* Standing water and low mist. Terrain-wise it is a bog; the mist is not a
   tile at all, it is a rule applied while you stand in the rectangle. */
static void carve_mire(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 30, 1, 3, TILE_FLOOR, TILE_WATER);
    scatter_blobs(m, r, area / 70, 1, 2, TILE_FLOOR, TILE_MIASMA);
    scatter_blobs(m, r, area / 50, 1, 2, TILE_FLOOR, TILE_THICKET);
}

/* Crystal: you can see clean across it and not walk across it. The pillars
   are placed in loose lines rather than blobs so the sightlines stay long --
   the whole character of the place is being able to watch something coming
   and not being able to cast at it. */
static void carve_crystal(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    int veins = 3 + rand() % 4;
    for (int v = 0; v < veins; v++) {
        int x = r.x + 1 + rand() % (r.w - 2);
        int y = r.y + 1 + rand() % (r.h - 2);
        int dx = (rand() % 3) - 1, dy = (rand() % 3) - 1;
        if (dx == 0 && dy == 0) dx = 1;
        int len = 4 + rand() % 10;
        for (int i = 0; i < len; i++) {
            if (x < r.x + 1 || x >= r.x + r.w - 1 || y < r.y + 1 || y >= r.y + r.h - 1) break;
            if (x >= 1 && x < MAP_W - 1 && y >= 1 && y < MAP_H - 1 &&
                m->tiles[y][x].type == TILE_FLOOR && rand() % 100 < 75)
                m->tiles[y][x].type = TILE_CRYSTAL;
            x += dx; y += dy;
        }
    }
    scatter_blobs(m, r, r.w * r.h / 80, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The quiet quarter: rooms and streets, nothing unusual to look at. What is
   unusual is that none of it is awake, which is a thing you find out by
   walking through rather than by looking. */
static void carve_quiet(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    int blocks = r.w * r.h / 70;
    for (int i = 0; i < blocks; i++) {
        int bw = 3 + rand() % 5, bh = 3 + rand() % 4;
        int x0 = r.x + 1 + rand() % (r.w - bw - 2);
        int y0 = r.y + 1 + rand() % (r.h - bh - 2);
        for (int y = y0; y < y0 + bh; y++)
            for (int x = x0; x < x0 + bw; x++)
                if (x >= 1 && x < MAP_W - 1 && y >= 1 && y < MAP_H - 1)
                    m->tiles[y][x].type = TILE_WALL;
    }
    scatter_blobs(m, r, r.w * r.h / 100, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* Storm-cage: an open plateau, deliberately open, with rods in it. Cover
   would defeat the point -- the decision here is where you stand relative to
   the rods, and you cannot make it if you cannot see them. */
static void carve_storm(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    int rods = 6 + (r.w * r.h) / 60;
    for (int i = 0; i < rods; i++) {
        int x = r.x + 2 + rand() % (r.w - 4);
        int y = r.y + 2 + rand() % (r.h - 4);
        if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
        if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_ROD;
    }
    scatter_blobs(m, r, r.w * r.h / 120, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The vent field. Deliberately carve_storm with two words changed -- the whole
   argument for building this was that the Storm-Cage's system already exists
   and only the scenery is new, and a carve function that drifted from its twin
   would be quietly re-litigating that. Vents cluster more than rods do, which
   is the one real difference: rods were planted in rows and vents are where the
   rock happened to crack. */
static void carve_geothermal(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    int vents = 6 + (r.w * r.h) / 60;
    for (int i = 0; i < vents; i++) {
        int x = r.x + 2 + rand() % (r.w - 4);
        int y = r.y + 2 + rand() % (r.h - 4);
        if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
        if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_VENT;
    }
    scatter_blobs(m, r, r.w * r.h / 120, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The chapel. Open stone with pillars -- the rule is the silence, not the
   shape, so the shape stays legible: nothing here should make it hard to walk
   through, because the district already takes away the sense you would use to
   find your way out of trouble. Pillars rather than a maze for exactly that
   reason. */
static void carve_chapel(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    for (int y = r.y + 2; y < r.y + r.h - 2; y += 3)
        for (int x = r.x + 3; x < r.x + r.w - 3; x += 4) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_WALL;
        }
    scatter_blobs(m, r, r.w * r.h / 150, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* An amphitheatre: a ring of seating around open sand, with one way in.
   Deliberately bare in the middle -- the fight is the content, and cover
   would let you take ten waves from a doorway. */
static void carve_arena(Map *m, Room r) {
    int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    int rx = r.w / 2 - 1, ry = r.h / 2 - 1;
    if (rx < 4) rx = 4;
    if (ry < 3) ry = 3;

    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            /* Elliptical, so it reads as a bowl rather than another room. */
            long ex = (long)(x - cx) * (x - cx) * ry * ry;
            long ey = (long)(y - cy) * (y - cy) * rx * rx;
            long lim = (long)rx * rx * ry * ry;
            if (ex + ey <= lim)            m->tiles[y][x].type = TILE_FLOOR;
            else if (ex + ey <= lim * 9 / 5) m->tiles[y][x].type = TILE_WALL;  /* the seats */
        }
    }

    /* One gate, on a side, so "the gates shut behind you" has somewhere to
       shut. Left open until the fight starts. */
    int gx = cx, gy = r.y + r.h / 2;
    for (int x = cx; x < r.x + r.w; x++) {
        if (x < 1 || x >= MAP_W - 1) break;
        m->tiles[gy][x].type = TILE_FLOOR;
        gx = x;
    }
    (void)gx;
}

/* Channels running one way across the district, with banks between them so
   you can choose to walk instead. The water is the fast route and not your
   route -- that tension only exists if staying out of it is possible. */
static void carve_aqueduct(Map *m, Room r, int *out_dx, int *out_dy) {
    fill_ragged(m, r, TILE_FLOOR);

    bool horizontal = (r.w >= r.h);
    int dir = (rand() % 2) ? 1 : -1;
    *out_dx = horizontal ? dir : 0;
    *out_dy = horizontal ? 0 : dir;

    int channels = 2 + rand() % 3;
    for (int c = 0; c < channels; c++) {
        if (horizontal) {
            int y = r.y + 2 + rand() % (r.h - 4);
            int wide = 1 + (rand() % 100 < 40);
            for (int k2 = 0; k2 < wide; k2++)
                for (int x = r.x + 1; x < r.x + r.w - 1; x++)
                    if (x >= 1 && x < MAP_W - 1 && y + k2 >= 1 && y + k2 < MAP_H - 1)
                        m->tiles[y + k2][x].type = TILE_CURRENT;
        } else {
            int x = r.x + 2 + rand() % (r.w - 4);
            int wide = 1 + (rand() % 100 < 40);
            for (int k2 = 0; k2 < wide; k2++)
                for (int y = r.y + 1; y < r.y + r.h - 1; y++)
                    if (x + k2 >= 1 && x + k2 < MAP_W - 1 && y >= 1 && y < MAP_H - 1)
                        m->tiles[y][x + k2].type = TILE_CURRENT;
        }
    }
    scatter_blobs(m, r, r.w * r.h / 90, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* Shallow red water in broad pools -- walkable by everything, which is the
   point: a hazard that helps them is only interesting if they can get to it. */
static void carve_bloodmarsh(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 26, 2, 3, TILE_FLOOR, TILE_BLOODPOOL);
    scatter_blobs(m, r, area / 70, 1, 2, TILE_FLOOR, TILE_THICKET);
    scatter_blobs(m, r, area / 90, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* Liquid light in patches. Generous, because the district only works if you
   are usually within a step or two of a choice. */
static void carve_prism(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 45, 1, 3, TILE_FLOOR, TILE_PRISM_RED);
    scatter_blobs(m, r, area / 45, 1, 3, TILE_FLOOR, TILE_PRISM_BLUE);
    scatter_blobs(m, r, area / 60, 1, 2, TILE_FLOOR, TILE_PRISM_GREEN);
    scatter_blobs(m, r, area / 90, 1, 2, TILE_FLOOR, TILE_CRYSTAL);
}

/* Lanes of belt with walkable strips between them. The lanes alternate
   direction, which is what makes this navigation rather than a current: you
   cannot walk against a belt, so getting anywhere means picking the lane that
   is already going your way. */
static void carve_assembly(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    for (int y = r.y + 2; y < r.y + r.h - 2; y += 3) {
        for (int x = r.x + 1; x < r.x + r.w - 1; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_BELT;
        }
    }
    scatter_blobs(m, r, r.w * r.h / 80, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* One snare, two gardens. The garden hides its teeth among pollen you have
   to breathe to cross; the sand is open and honest and simply does not let
   go. Same tile, and the district decides what it costs. */
static void carve_garden(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 30, 1, 2, TILE_FLOOR, TILE_SNARE);
    scatter_blobs(m, r, area / 50, 1, 3, TILE_FLOOR, TILE_MIASMA);   /* pollen */
    scatter_blobs(m, r, area / 60, 1, 2, TILE_FLOOR, TILE_THICKET);
}

static void carve_quicksand(Map *m, Room r) {
    int area = r.w * r.h;
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, area / 22, 2, 3, TILE_FLOOR, TILE_SNARE);
    scatter_blobs(m, r, area / 90, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The watch. A built compound, and the only district whose terrain exists to
   let you *avoid* what is in it: blocks in rows with lanes between them, so
   there is always something to put between you and a warden. The quiet
   quarter's streets with the density turned up -- which is deliberate, since
   the two are siblings and a player should read the ground and think "this
   is one of those" before finding out which. */
static void carve_watch(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);

    /* Blocks on a loose grid rather than scattered. Scattered cover leaves
       gaps you cannot plan through; rows leave *lanes*, and a lane is a
       decision. */
    for (int by = r.y + 2; by < r.y + r.h - 3; by += 4) {
        for (int bx = r.x + 2; bx < r.x + r.w - 3; bx += 5) {
            if (rand() % 100 < 22) continue;      /* gaps, so it is not a grid */
            int bw = 2 + rand() % 2, bh = 1 + rand() % 2;
            for (int y = by; y < by + bh; y++)
                for (int x = bx; x < bx + bw; x++)
                    if (x >= 1 && x < MAP_W - 1 && y >= 1 && y < MAP_H - 1)
                        m->tiles[y][x].type = TILE_WALL;
        }
    }
    scatter_blobs(m, r, r.w * r.h / 70, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The barrow. A walled hall with piers down the long sides for them to be
   standing between, and one way in that shuts.
 *
   Rectangular where the arena is elliptical, because an amphitheatre was cut
   for a crowd to watch and this was built to keep something in. */
static void carve_gauntlet(Map *m, Room r) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            bool edge = (x == r.x || x == r.x + r.w - 1 ||
                         y == r.y || y == r.y + r.h - 1);
            m->tiles[y][x].type = edge ? TILE_WALL : TILE_FLOOR;
        }
    }

    for (int x = r.x + 3; x < r.x + r.w - 3; x += 4) {
        if (x < 1 || x >= MAP_W - 1) continue;
        int top = r.y + 2, bot = r.y + r.h - 3;
        if (top >= 1 && top < MAP_H - 1) m->tiles[top][x].type = TILE_WALL;
        if (bot >= 1 && bot < MAP_H - 1 && bot > top) m->tiles[bot][x].type = TILE_WALL;
    }

    /* The one door, in the middle of a long wall. gauntlet_seal() shuts the
       whole rim rather than this tile alone -- the two roads mapgen runs to
       the anchor punch their own holes through the wall on the way in, and a
       seal that only knew about this door would be a seal with two ways
       around it. */
    int gy = r.y + r.h / 2, gx = r.x + r.w - 1;
    if (gy >= 1 && gy < MAP_H - 1 && gx >= 1 && gx < MAP_W - 1)
        m->tiles[gy][gx].type = TILE_FLOOR;
}

/* The eye. Open ground, like the rod field and for the same reason: the
   decision is where you are standing relative to something you can see
   coming, and cover would let you ignore it. Marked out in quarters by two
   lines of rubble so the shelter has visible edges -- a safe quarter you
   cannot locate is not a mechanic, it is weather. */
static void carve_eye(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    int mx = r.x + r.w / 2, my = r.y + r.h / 2;
    for (int x = r.x + 1; x < r.x + r.w - 1; x++)
        if (x >= 1 && x < MAP_W - 1 && my >= 1 && my < MAP_H - 1 && rand() % 100 < 62)
            m->tiles[my][x].type = TILE_DECOR;
    for (int y = r.y + 1; y < r.y + r.h - 1; y++)
        if (y >= 1 && y < MAP_H - 1 && mx >= 1 && mx < MAP_W - 1 && rand() % 100 < 62)
            m->tiles[y][mx].type = TILE_DECOR;
}

/* The mirror. A still, flat, reflective floor with nothing on it -- the room
   is bare because the fight is the content and because you should see the
   thing that stands up. */
static void carve_mirror(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, r.w * r.h / 110, 1, 2, TILE_FLOOR, TILE_CRYSTAL);
    scatter_blobs(m, r, r.w * r.h / 130, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The stone wood. Trees turned to rock: they block sight and they block the
   way, in loose stands rather than a grid, so the district is all blind
   corners and no corridors. */
static void carve_petrified(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    int stands = r.w * r.h / 30;
    for (int i = 0; i < stands; i++) {
        int cx = r.x + 1 + rand() % (r.w - 2);
        int cy = r.y + 1 + rand() % (r.h - 2);
        int n = 1 + rand() % 3;
        for (int k = 0; k < n; k++) {
            int x = cx + rand() % 3 - 1, y = cy + rand() % 3 - 1;
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_WALL;
        }
    }
    scatter_blobs(m, r, r.w * r.h / 100, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The shaft. A worked-out mine: galleries, spoil, and holes in the floor that
   go somewhere. The holes are *visible* -- see PIT_FALL_HP_PCT for why a
   hidden one would be a trap rather than a decision. */
static void carve_shaft(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);

    /* Galleries: long thin rooms with pillars, so it reads as dug rather than
       eroded. */
    for (int y = r.y + 2; y < r.y + r.h - 2; y += 3) {
        for (int x = r.x + 2; x < r.x + r.w - 2; x += 4) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            if (rand() % 100 < 45) m->tiles[y][x].type = TILE_WALL;
        }
    }
    int pits = 1 + (r.w * r.h) / 260;
    for (int i = 0; i < pits; i++) {
        int x = r.x + 2 + rand() % (r.w - 4);
        int y = r.y + 2 + rand() % (r.h - 4);
        if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
        if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_PIT;
    }
    scatter_blobs(m, r, r.w * r.h / 90, 1, 2, TILE_FLOOR, TILE_ORE);
    scatter_blobs(m, r, r.w * r.h / 110, 1, 2, TILE_FLOOR, TILE_DECOR);
}

/* The boneyard. Half-buried machines, thick with the rubble that is the work
   here -- the density is the point, because the district *is* the salvage. */
static void carve_boneyard(Map *m, Room r) {
    fill_ragged(m, r, TILE_FLOOR);
    scatter_blobs(m, r, r.w * r.h / 24, 1, 2, TILE_FLOOR, TILE_DECOR);
    int hulks = r.w * r.h / 70;
    for (int i = 0; i < hulks; i++) {
        int x = r.x + 1 + rand() % (r.w - 2);
        int y = r.y + 1 + rand() % (r.h - 2);
        if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
        if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_WALL;
    }
}

/* The proving ground. A floor somebody swept and marked out: open in the
   middle so there is nowhere to fight from, with a low wall around it so it
   reads as somewhere you step into rather than somewhere you pass through.
   Not sealed -- you can always walk out, and that choice is most of the
   mechanic. */
static void carve_proving(Map *m, Room r) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            bool rim = (x == r.x || x == r.x + r.w - 1 ||
                        y == r.y || y == r.y + r.h - 1);
            /* A rim with gaps in it: a fence, not a seal. */
            if (rim) { if (rand() % 100 < 62) m->tiles[y][x].type = TILE_WALL; }
            else       m->tiles[y][x].type = TILE_FLOOR;
        }
    }
    scatter_blobs(m, r, r.w * r.h / 120, 1, 2, TILE_FLOOR, TILE_DECOR);
}

static void carve_district(Map *m, Room r, DistrictKind k) {
    switch (k) {
        case DIST_JUNGLE: carve_jungle(m, r); break;
        case DIST_SEA:    carve_sea(m, r);    break;
        case DIST_SWAMP:  carve_swamp(m, r);  break;
        case DIST_MYCELIUM: carve_mycelium(m, r); break;
        case DIST_HIVE:   carve_hive(m, r);   break;
        case DIST_MIRE:   carve_mire(m, r);   break;
        case DIST_CRYSTAL: carve_crystal(m, r); break;
        case DIST_QUIET:  carve_quiet(m, r);  break;
        case DIST_STORM:  carve_storm(m, r);  break;
        case DIST_ARENA:  carve_arena(m, r);  break;
        case DIST_BLOODMARSH: carve_bloodmarsh(m, r); break;
        case DIST_PRISM:  carve_prism(m, r);  break;
        case DIST_ASSEMBLY: carve_assembly(m, r); break;
        case DIST_WATCH:  carve_watch(m, r);  break;
        case DIST_EYE:    carve_eye(m, r);    break;
        case DIST_MIRROR: carve_mirror(m, r); break;
        case DIST_PETRIFIED: carve_petrified(m, r); break;
        case DIST_SHAFT:  carve_shaft(m, r);  break;
        case DIST_BONEYARD: carve_boneyard(m, r); break;
        case DIST_PROVING: carve_proving(m, r); break;
        case DIST_CHAPEL: carve_chapel(m, r); break;
        case DIST_GEOTHERMAL: carve_geothermal(m, r); break;
        case DIST_GAUNTLET: carve_gauntlet(m, r); break;
        case DIST_GARDEN: carve_garden(m, r); break;
        case DIST_QUICKSAND: carve_quicksand(m, r); break;
        case DIST_AQUEDUCT: { int fx = 1, fy = 0; carve_aqueduct(m, r, &fx, &fy); break; }
        default:          carve_ruins(m, r);  break;
    }
}


/* ---- the camp ----------------------------------------------------------
 * Somewhere down here, somebody walled a piece of the cave off and held it.
 * A palisade with gates in it, tents inside, a trader who came down and
 * decided the margins were worth it, and water you can drink. Nothing spawns
 * in it and nothing respawns into it: it is the one square of the floor
 * where you can stop.
 *
 * Rare on purpose -- roughly one floor in six. A safe room you can count on
 * is a rest stop; one you cannot is a relief.
 */
static void carve_haven(Map *m, Room r) {
    for (int y = r.y; y < r.y + r.h; y++) {
        for (int x = r.x; x < r.x + r.w; x++) {
            if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;
            bool edge = (x == r.x || x == r.x + r.w - 1 || y == r.y || y == r.y + r.h - 1);
            m->tiles[y][x].type = edge ? TILE_WALL : TILE_FLOOR;
        }
    }

    /* tents, crates, the mess anyone leaves who lives somewhere a while */
    int clutter = r.w * r.h / 14;
    for (int i = 0; i < clutter; i++) {
        int x = r.x + 1 + rand() % (r.w - 2);
        int y = r.y + 1 + rand() % (r.h - 2);
        if (m->tiles[y][x].type == TILE_FLOOR) m->tiles[y][x].type = TILE_DECOR;
    }
}

/* A gate, and the road out of it. One hole in the wall apiece, and the road
   runs dead straight away from that hole until it meets ground that is
   already open.

   The obvious thing -- run a normal corridor from a room to the middle of
   the camp -- does not work: the L-shaped corridor cuts the palisade
   wherever its legs happen to fall, and often runs the whole length of a
   wall, leaving a fence with eight gaps in it. A gate has to be a hole you
   can point at. */
static bool haven_gate_road(Map *m, Room r, int side) {
    int x, y, dx = 0, dy = 0;
    switch (side) {
        case 0:  x = r.x + 1 + rand() % (r.w - 2); y = r.y;             dy = -1; break;
        case 1:  x = r.x + 1 + rand() % (r.w - 2); y = r.y + r.h - 1;   dy =  1; break;
        case 2:  x = r.x;                          y = r.y + 1 + rand() % (r.h - 2); dx = -1; break;
        default: x = r.x + r.w - 1;                y = r.y + 1 + rand() % (r.h - 2); dx =  1; break;
    }
    if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) return false;

    /* Walk it first and only cut it if it arrives somewhere. A camp near the
       edge of the map can point a gate at nothing but rock and the border,
       and a road that dead-ends there is a hole in the palisade that buys
       the player exactly nothing -- measured at one camp in 132, all of them
       on the small maps where a camp sits proportionally nearer the edge. */
    /* "Arrives somewhere" has to mean *walkable*, not merely "not a wall".
       Thicket, crystal and rods are all impassable, so a road that stopped at
       the first one of those was a gate opening onto a wall of growth -- the
       same unreachable-camp bug as the border case, found the same way when
       three more district kinds reshuffled the floors. */
    bool arrives = false;
    for (int cx = x + dx, cy = y + dy;
         cx >= 1 && cx < MAP_W - 1 && cy >= 1 && cy < MAP_H - 1;
         cx += dx, cy += dy) {
        if (is_walkable_player(m, cx, cy)) { arrives = true; break; }
    }
    if (!arrives) return false;

    m->tiles[y][x].type = TILE_FLOOR;
    for (int cx = x + dx, cy = y + dy;
         cx >= 1 && cx < MAP_W - 1 && cy >= 1 && cy < MAP_H - 1;
         cx += dx, cy += dy) {
        if (is_walkable_player(m, cx, cy)) break;   /* met the floor */
        TileType t = m->tiles[cy][cx].type;
        if (t == TILE_WATER) m->tiles[cy][cx].type = TILE_BRIDGE;
        else                 m->tiles[cy][cx].type = TILE_FLOOR;  /* rock, growth, crystal */
    }
    return true;
}

/* place_feature picks a random room; this one puts a feature on a named
   patch of floor, which is what the camp needs -- its trader and its water
   are the reason to walk in. */
static bool place_feature_in(Map *m, Room r, FeatureType type) {
    if (m->feature_count >= MAX_FEATURES) return false;
    for (int tries = 0; tries < 60; tries++) {
        int x = r.x + 1 + rand() % (r.w - 2);
        int y = r.y + 1 + rand() % (r.h - 2);
        if (m->tiles[y][x].type != TILE_FLOOR) continue;

        bool occupied = false;
        for (int k = 0; k < m->feature_count; k++)
            if (m->features[k].x == x && m->features[k].y == y) { occupied = true; break; }
        if (occupied) continue;

        MapFeature *f = &m->features[m->feature_count++];
        memset(f, 0, sizeof(*f));
        f->type = type;
        f->x = x;
        f->y = y;
        f->used = false;
        /* No relic branch: the camp stocks water, a trader and sometimes a
           shrine. Relics are found out in the floor, not handed over inside
           the walls. */
        return true;
    }
    return false;
}

/* How much bigger this world is than the Shaft, as a plain multiple. */
static double world_area_multiple(void) {
    double s = (double)MAP_AREA_SCALE / 16.0;
    return s < 1.0 ? 1.0 : s;
}

/* Districts have to grow with the world, not just multiply.
 *
   Counting them per ten thousand tiles alone looked right and measured
   wrong: the count rose with area while each district stayed a fixed
   20x12-ish rectangle, so a Well floor wanted 896 of them, could only fit
   278 non-overlapping ones inside a sane try budget, and ended up 13% wild
   against the Shaft's 33%. The biggest world was the greyest, which is the
   opposite of the intent.

   So the linear size grows as the fourth root of the area multiple (area as
   the square root) and the count as the square root. Multiply those and the
   share of a floor that is wild comes out the same on every world, while
   both the number of places and their individual size still rise with the
   map -- a Well floor gets more districts than a Shaft floor *and* bigger
   ones, which is what "a giant cave with an ecosystem in it" should mean. */
static int district_count_for_area(void) {
    int n = (int)(DIST_PER_10K * sqrt(world_area_multiple()));
    return n < 1 ? 1 : n;
}

/* Linear size multiplier for one district, in percent. */
static int district_size_pct(void) {
    int pct = (int)(100.0 * sqrt(sqrt(world_area_multiple())));
    return pct < 100 ? 100 : pct;
}

/* ---- structure above the district --------------------------------------
 *
 * ROADMAP 2.1f's last open piece. Room shapes and wild districts gave a big
 * floor variety at the scale of a room and the scale of a region, and left the
 * scale between them untouched: at 700x400 and above the floor is still a
 * *uniform corridor mesh*, so a Well floor is a very large amount of the same
 * connectivity. Everywhere is reachable from everywhere by about as many
 * routes, which means no route is a decision and no place is behind anything.
 *
 * So: quarters, divided by walls, joined at a handful of chokepoints. The
 * point is not the walls -- it is that crossing the floor now goes *through*
 * somewhere, and that "the stairs are in the north-east" starts to mean
 * something a player can act on.
 *
 * Only on the big worlds. On the Shaft a floor is crossed in minutes and
 * quartering it would be a wall in a corridor.
 *
 * ---- on not stranding anything ----
 *
 * This is the most dangerous thing in mapgen: it *removes* connectivity on
 * purpose, late, after everything else has been placed. So it is built to be
 * undoable and it verifies itself. Each dividing line is laid down one at a
 * time, recording every tile it changes; the floor is then flooded from the
 * arrival point, and if the way down has stopped being reachable -- or if the
 * line stranded more ground than a nook -- the line is rolled back tile for
 * tile and the floor keeps the mesh it had. A quartered floor is better than a
 * mesh; a mesh is infinitely better than a floor you cannot finish.
 */
#define QUARTER_MIN_AREA   280000L   /* the Deeps and up; see waygate_interval() */
#define QUARTER_GATES      3         /* chokepoints per dividing line */
#define QUARTER_GATE_HALF  1         /* gate width is 2*this + 1 */
/* How much ground a dividing line may strand before it is undone, as a
   fraction of what the floor actually has.
 *
   This was an absolute 400 tiles and that was the wrong shape, not the wrong
   number: the Deeps has ~195,000 walkable tiles and the Well ~800,000, so one
   figure cannot mean the same thing on both. Measured after the repair pass,
   the residue a surviving line leaves is 417-920 tiles on floors of
   195,000-811,000 -- between 0.06% and 0.21% -- and the absolute bar was
   throwing away **every** line on the Well for stranding six hundredths of one
   per cent of it.

   A quarter of a per cent admits all of that and still rejects a line that
   severs a quarter, which would lose tens of thousands. For scale, the wild
   districts already strand 1.1% of their own ground by design (§103), so this
   is the stricter bar of the two. */
#define QUARTER_LOST_PERMILLE 25     /* 2.5 per thousand of reachable ground */
#define QUARTER_LOST_FLOOR   400     /* ...or this many tiles, whichever is larger */

typedef struct { int x, y; unsigned char was; } QuarterEdit;

static unsigned char quarter_seen[MAP_H_MAX][MAP_W_MAX];

/* Walkable ground reachable from (sx,sy), counted, with quarter_seen left as
   the answer. Whole-map, and it runs at most a handful of times per floor. */
static long quarter_flood(const Map *m, int sx, int sy) {
    /* The whole array, every time, even though only MAP_W x MAP_H of it is in
       use. Clearing row by row is the obvious saving and it is not taken, for
       two measured reasons: it is worth **nothing** -- the quartering pass
       costs 80.8 ms/floor against 81.0 without it at the Deeps, which is
       noise -- and when it was tried it silently stopped the pass working
       altogether, 35 surviving dividing lines out of 36 becoming 0.

       The cause was not chased down, because an optimisation that buys
       nothing and breaks the thing it optimises is not worth understanding;
       it is worth deleting. Recorded so nobody has the same clever idea
       twice. */
    memset(quarter_seen, 0, sizeof quarter_seen);
    if (sx < 0 || sx >= MAP_W || sy < 0 || sy >= MAP_H) return 0;

    static int qx[MAP_W_MAX * MAP_H_MAX], qy[MAP_W_MAX * MAP_H_MAX];
    int head = 0, tail = 0;
    quarter_seen[sy][sx] = 1;
    qx[tail] = sx; qy[tail] = sy; tail++;
    long n = 0;

    while (head < tail) {
        int x = qx[head], y = qy[head]; head++;
        n++;
        static const int DX[4] = { 1, -1, 0, 0 };
        static const int DY[4] = { 0, 0, 1, -1 };
        for (int k = 0; k < 4; k++) {
            int nx = x + DX[k], ny = y + DY[k];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (quarter_seen[ny][nx]) continue;
            if (!is_walkable_player(m, nx, ny)) continue;
            quarter_seen[ny][nx] = 1;
            qx[tail] = nx; qy[tail] = ny; tail++;
        }
    }
    return n;
}

/* Lay one dividing line, keeping `QUARTER_GATES` ways through it.
 *
   `vertical` means the line runs down the map at x = `at`. Gates are chosen
   only where the crossing is real -- walkable on the line and walkable on both
   sides of it -- so a gate is always a passage rather than a hole in rock. */
static long quarter_lost_budget(long before) {
    long pct = before * QUARTER_LOST_PERMILLE / 10000;
    return pct > QUARTER_LOST_FLOOR ? pct : QUARTER_LOST_FLOOR;
}

static bool quarter_line(Map *m, bool vertical, int at, int ux, int uy,
                         int sx_down, int sy_down, long before, long *out_after) {
    static QuarterEdit edits[MAP_W_MAX > MAP_H_MAX ? MAP_W_MAX : MAP_H_MAX];
    int nedit = 0;

    int span = vertical ? MAP_H : MAP_W;

    /* Where a gate would actually let somebody through. */
    static int gate_at[MAP_W_MAX > MAP_H_MAX ? MAP_W_MAX : MAP_H_MAX];
    int ngate = 0;
    for (int i = 1; i < span - 1; i++) {
        int x = vertical ? at : i;
        int y = vertical ? i  : at;
        if (!is_walkable_player(m, x, y)) continue;
        int ax = vertical ? x - 1 : x, ay = vertical ? y : y - 1;
        int bx = vertical ? x + 1 : x, by = vertical ? y : y + 1;
        if (!is_walkable_player(m, ax, ay) || !is_walkable_player(m, bx, by)) continue;
        if (ngate < span) gate_at[ngate++] = i;
    }
    /* Nothing crosses here anyway, or too little to be worth dividing. */
    if (ngate <= QUARTER_GATES) return false;   /* nothing crosses here anyway */

    /* Spread the gates rather than taking the first few: chokepoints bunched
       at one end are a wall with a door, not a quartered floor. */
    bool keep[QUARTER_GATES];
    int chosen[QUARTER_GATES];
    for (int g = 0; g < QUARTER_GATES; g++) {
        keep[g] = true;
        chosen[g] = gate_at[(ngate * (2 * g + 1)) / (2 * QUARTER_GATES)];
    }
    (void)keep;

    for (int i = 0; i < span; i++) {
        int x = vertical ? at : i;
        int y = vertical ? i  : at;
        if (x < 1 || x >= MAP_W - 1 || y < 1 || y >= MAP_H - 1) continue;

        bool is_gate = false;
        for (int g = 0; g < QUARTER_GATES; g++)
            if (i >= chosen[g] - QUARTER_GATE_HALF && i <= chosen[g] + QUARTER_GATE_HALF)
                is_gate = true;
        if (is_gate) continue;

        /* Never wall the arrival point, the way down, or a staircase. */
        if ((x == ux && y == uy) || (x == sx_down && y == sy_down)) continue;
        TileType t = m->tiles[y][x].type;
        if (t == TILE_WALL || t == TILE_STAIRS_UP || t == TILE_STAIRS_DOWN) continue;

        /* Nor through a district at all.
         *
           First written to spare only the districts that shut behind you --
           the arena and the barrow, whose whole mechanic is having exactly one
           door -- on the reasoning that a wall through a bog is a bog with a
           wall in it. The suite disagreed on its first run: a straight line
           through the overgrowth severed 811 of its 814 walkable tiles from
           the floor, and the whole-floor budget did not notice because the
           line reconnected as much ground elsewhere as it cut off here.

           So the line divides the *built* floor -- rooms and the corridor mesh
           between them, which is what was uniform and what this pass exists to
           break up -- and leaves the authored places alone. That is better
           than a compromise: a district on a dividing line becomes a way
           through it, which is a more interesting chokepoint than a gap in a
           wall and costs nothing to arrange. */
        if (district_index_at(m, x, y) >= 0) continue;

        if (nedit < (int)(sizeof edits / sizeof edits[0])) {
            edits[nedit].x = x; edits[nedit].y = y;
            edits[nedit].was = (unsigned char)t;
            nedit++;
            m->tiles[y][x].type = TILE_WALL;
        }
    }
    if (nedit == 0) return false;

    /* Now prove it -- and, where it fails, fix it rather than give up.
     *
       The first cut of this only proved it: wall the line, flood, and roll the
       whole line back if too much ground had been cut off. Measured, that
       threw away *every* dividing line on the Well and three in five on the
       Deeps, because the test was an absolute number of tiles against floors
       whose walkable area differs by a factor of four. Loosening the number
       would have been the wrong fix twice over -- it would have shipped the
       stranded ground rather than stopped producing it.

       So: open more gates instead. A tile on the line with reached ground on
       one side and cut-off ground on the other is, by construction, exactly
       the place a gate belongs -- it is a crossing the line took away. Re-open
       those and flood again. Three passes is plenty; the roll-back stays as
       the backstop for a line that still cannot be made to work. */
    long after = 0;
    for (int pass = 0; pass < 3; pass++) {
        after = quarter_flood(m, ux, uy);
        bool down_ok = (sx_down < 0) || quarter_seen[sy_down][sx_down];
        if (down_ok && before - after <= quarter_lost_budget(before)) {
            if (out_after) *out_after = after;
            return true;
        }

        int reopened = 0;
        for (int e = 0; e < nedit; e++) {
            int x = edits[e].x, y = edits[e].y;
            if (m->tiles[y][x].type != TILE_WALL) continue;
            int ax = vertical ? x - 1 : x, ay = vertical ? y : y - 1;
            int bx = vertical ? x + 1 : x, by = vertical ? y : y + 1;
            if (ax < 0 || bx >= MAP_W || ay < 0 || by >= MAP_H) continue;
            bool a_ok = is_walkable_player(m, ax, ay) && quarter_seen[ay][ax];
            bool b_ok = is_walkable_player(m, bx, by) && quarter_seen[by][bx];
            bool a_lost = is_walkable_player(m, ax, ay) && !quarter_seen[ay][ax];
            bool b_lost = is_walkable_player(m, bx, by) && !quarter_seen[by][bx];
            if ((a_ok && b_lost) || (b_ok && a_lost)) {
                m->tiles[y][x].type = (TileType)edits[e].was;
                reopened++;
            }
        }
        if (reopened == 0) break;
    }

    after = quarter_flood(m, ux, uy);
    bool down_ok = (sx_down < 0) || quarter_seen[sy_down][sx_down];
    if (down_ok && before - after <= quarter_lost_budget(before)) {
        if (out_after) *out_after = after;
        return true;
    }

    for (int e = 0; e < nedit; e++)
        m->tiles[edits[e].y][edits[e].x].type = (TileType)edits[e].was;
    return false;
}

static int carve_quarters(Map *m, int ux, int uy, int sx_down, int sy_down) {
    if ((long)MAP_W * MAP_H < QUARTER_MIN_AREA) return 0;

    long before = quarter_flood(m, ux, uy);
    if (before <= 0) return 0;

    int laid = 0;
    /* Two down and one across: six quarters, which is enough for a floor to
       have a north-east without being a set of corridors again. */
    const int VX[2] = { MAP_W / 3, (MAP_W * 2) / 3 };
    long after = before;
    for (int i = 0; i < 2; i++) {
        /* The line hands back what is reachable now, so the next one does not
           have to flood the floor again to find out. */
        if (quarter_line(m, true, VX[i], ux, uy, sx_down, sy_down, before, &after)) {
            laid++;
            before = after;
        }
    }
    if (quarter_line(m, false, MAP_H / 2, ux, uy, sx_down, sy_down, before, &after)) laid++;
    return laid;
}

void generate_temple_floor(Map *m, int floor_num, int *out_x, int *out_y, const Player *p) {
    /* Re-seed from the run seed and the depth, rather than carrying on from
       wherever the global stream happens to be. Otherwise the floor you get
       depends on how many attacks you rolled on the way down, and "floor 12
       of seed 44815" stops meaning anything. Everything after generation --
       combat, drops, respawns -- carries on from here, which is still
       deterministic for identical play. */
    srand(floor_seed(p ? p->run_seed : 1u, floor_num));

    memset(m, 0, sizeof(*m));
    m->floor_num = floor_num;
    m->biome = get_biome_for_floor(floor_num);
    m->storm_x = m->storm_y = -1;
    m->storm_countdown = 0;
    m->arena_district = -1;
    m->arena_wave = 0;
    m->gauntlet_district = -1;
    m->gauntlet_wave = 0;
    m->gauntlet_paid = false;
    m->arena_paid = false;
    bool boss_floor = (floor_num >= MAX_FLOOR);

    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            m->tiles[y][x].type = TILE_WALL;
        }
    }

    int lake_min = 0, lake_max = 0, lava_min = 0, lava_max = 0, miasma_min = 0, miasma_max = 0;
    if (!boss_floor) {
        biome_terrain_params(m->biome, &lake_min, &lake_max, &lava_min, &lava_max, &miasma_min, &miasma_max);

        int lakes = lake_min + (lake_max > lake_min ? rand() % (lake_max - lake_min + 1) : 0);
        for (int i = 0; i < lakes; i++) {
            int cx = 6 + rand() % (MAP_W - 12), cy = 6 + rand() % (MAP_H - 12);
            carve_blob_over(m, cx, cy, 3 + rand() % 4, TILE_WALL, TILE_WATER);
        }

        int lavas = lava_min + (lava_max > lava_min ? rand() % (lava_max - lava_min + 1) : 0);
        for (int i = 0; i < lavas; i++) {
            int cx = 6 + rand() % (MAP_W - 12), cy = 6 + rand() % (MAP_H - 12);
            carve_blob_over(m, cx, cy, 2 + rand() % 3, TILE_WALL, TILE_LAVA);
        }
    }

    /* Wild districts go down before anything else is laid out, because the
       rooms have to be placed around them and not through them. One in six
       floors gets none at all -- an entirely built floor should still turn
       up, or the wilds stop being a thing you notice. */
    Room districts[MAX_DISTRICTS_MAX];
    int dist_anchor_x[MAX_DISTRICTS_MAX], dist_anchor_y[MAX_DISTRICTS_MAX];
    int dist_count = 0;

    /* Every floor is somewhere.
     *
       This used to give 45% of floors no wild district at all and another
       35% exactly one, on the theory that "the ordinary built floor has to
       stay the baseline or the exception stops reading as one". Measured,
       that came to 5.2% of an average floor's tiles being inside any
       district -- so the baseline was not "ordinary built floor", it was
       grey corridor, and the biome in the floor's name never appeared in the
       floor. The exception was so rare it read as absence rather than
       contrast.

       The contrast now comes from how much of the floor is wild rather than
       from whether any of it is: a sparse floor still has a handful of
       places in it, and a rich one is mostly wilderness with buildings in
       the gaps. Rooms are carved after this and route around what is
       already here, so the built floor is what fills the space left over. */
    int wild_roll = rand() % 100;
    int wilds_want = 0;
    if (!boss_floor) {
        int typical = district_count_for_area();
        if (wild_roll < 20)      wilds_want = typical / 2;      /* sparser */
        else if (wild_roll < 80) wilds_want = typical;
        else                     wilds_want = MAX_DISTRICTS;    /* overgrown */
        if (wilds_want < 2) wilds_want = 2;
    }

    if (wilds_want > 0) {
        if (wilds_want > MAX_DISTRICTS) wilds_want = MAX_DISTRICTS;

        /* More tries than before: the districts must not overlap, and once
           they are asked to cover a third of the floor a random placement
           misses far more often than it did at four of them. */
        int pct = district_size_pct();
        for (int tries = 0; tries < 60 * wilds_want + 400 && dist_count < wilds_want; tries++) {
            int w = (20 + rand() % 22) * pct / 100;
            int h = (12 + rand() % 15) * pct / 100;
            if (w >= MAP_W - 6 || h >= MAP_H - 6) continue;
            Room d = { 2 + rand() % (MAP_W - w - 4), 2 + rand() % (MAP_H - h - 4), w, h };

            bool clash = false;
            for (int i = 0; i < dist_count; i++)
                if (rooms_overlap(d, districts[i])) { clash = true; break; }
            if (clash) continue;

            DistrictKind k = pick_district_kind(m->biome);
            int flow_dx = 0, flow_dy = 0;
            if (k == DIST_AQUEDUCT) carve_aqueduct(m, d, &flow_dx, &flow_dy);
            else                    carve_district(m, d, k);
            m->wild_kinds |= 1u << (unsigned)k;

            /* A district nobody can set foot in is worse than none: undo it
               rather than ship a rectangle of unreachable water. */
            if (!district_anchor(m, d, &dist_anchor_x[dist_count], &dist_anchor_y[dist_count])) {
                for (int y = d.y; y < d.y + d.h; y++)
                    for (int x = d.x; x < d.x + d.w; x++)
                        if (x >= 1 && x < MAP_W - 1 && y >= 1 && y < MAP_H - 1)
                            m->tiles[y][x].type = TILE_WALL;
                m->wild_kinds &= ~(1u << (unsigned)k);
                continue;
            }
            /* One place, not four. The carve above may have closed a ring
               with its own terrain, and the two roads below only ever reach
               the anchor -- see district_relink(). */
            district_relink(m, d, (int)k, dist_anchor_x[dist_count], dist_anchor_y[dist_count]);

            if (m->district_count < MAX_DISTRICTS_MAX) {
                MapDistrict *md = &m->districts[m->district_count++];
                md->x = d.x; md->y = d.y; md->w = d.w; md->h = d.h; md->kind = (int)k;
                md->flow_dx = flow_dx; md->flow_dy = flow_dy;
                md->state = 0;   /* WATCH_KEEPING, and nothing at all elsewhere */
                /* The proving ground's rule is a property of the place, rolled
                   once here so the arrival note can say which fight this is
                   and the player can decide before stepping in. */
                if (k == DIST_PROVING) md->state = rand() % PROVE_KIND_COUNT;
            }
            districts[dist_count++] = d;
        }
    }

    /* One floor in six, and never on the boss floor: there is no camp at the
       bottom of the world. Placed after the wilds so it can sit against one
       -- a camp at the edge of the bog reads better than a camp in a field. */
    Room haven = {0, 0, 0, 0};
    bool have_haven = false;
    if (!boss_floor && rand() % 6 == 0) {
        for (int tries = 0; tries < 40 && !have_haven; tries++) {
            int w = 14 + rand() % 8, h = 9 + rand() % 5;
            Room hv = { 2 + rand() % (MAP_W - w - 4), 2 + rand() % (MAP_H - h - 4), w, h };
            bool clash = false;
            for (int i = 0; i < dist_count; i++)
                if (rooms_overlap(hv, districts[i])) { clash = true; break; }
            if (clash) continue;
            /* Reserved now, walled later. A palisade carved before the
               corridors are run gets punched full of holes by every road
               that happens to cross it, and a fence with eight gaps in it is
               not a fence. */
            haven = hv;
            have_haven = true;
        }
    }
    if (have_haven) {
        m->haven_x = haven.x; m->haven_y = haven.y;
        m->haven_w = haven.w; m->haven_h = haven.h;
    }

    occ_clear();
    occ_mark_hazards(m);
    for (int i = 0; i < dist_count; i++)
        occ_mark_rect(districts[i].x, districts[i].y, districts[i].w, districts[i].h);
    if (have_haven) occ_mark_rect(haven.x, haven.y, haven.w, haven.h);

    Room rooms[MAX_ROOMS_MAX];
    /* Largest shapes first: a plaza needs a clear 26x18 and will never find
       one once the map is full of cells. Cells go last because they fit in
       whatever is left, which is exactly what should be filling the gaps. */
    int room_count = 0;
    bool is_hub[MAX_ROOMS_MAX];
    memset(is_hub, 0, sizeof(is_hub));

    for (int kind = 0; kind < ROOM_KIND_COUNT && room_count < MAX_ROOMS; kind++) {
        const RoomSpec *sp = &ROOM_SPECS[kind];
        int want = spec_count(sp);
        int placed = 0, attempts = 0;
        int budget = ROOM_ATTEMPTS / ROOM_KIND_COUNT;

        while (placed < want && room_count < MAX_ROOMS && attempts < budget) {
            attempts++;
            int w = sp->w_min + rand() % (sp->w_max - sp->w_min + 1);
            int h = sp->h_min + rand() % (sp->h_max - sp->h_min + 1);

            /* A hall is long on one axis or the other, never both. */
            if (kind == ROOM_HALL && (rand() & 1)) { int t = w; w = h; h = t; }

            if (w >= MAP_W - 3 || h >= MAP_H - 3) continue;
            int x = 1 + rand() % (MAP_W - w - 2);
            int y = 1 + rand() % (MAP_H - h - 2);
            if (!occ_free(x, y, w, h)) continue;

            occ_mark_rect(x, y, w, h);
            is_hub[room_count] = (kind == ROOM_HUB);
            rooms[room_count++] = (Room){x, y, w, h};
            placed++;
        }
    }
    if (room_count < 2) {
        rooms[0] = (Room){2, 2, 6, 6};
        rooms[1] = (Room){MAP_W - 10, MAP_H - 10, 6, 6};
        room_count = 2;
    }

    for (int i = 0; i < room_count; i++) carve_room(m, rooms[i]);

    /* Join each room to the nearest one already on the network, rather than
       to whichever room happened to be placed next.
     *
       The old chain ran through the rooms in placement order, which is
       effectively random position order, so every single corridor crossed
       most of the map. That is survivable at 140x80 and not at all
       survivable when the map grows: rooms scale with area, but each of
       those corridors scales with the map's *span*, so corridor tiles grow
       faster than the floor does. Scaled to 280x160 it took the walkable
       fraction from 54% to 68% -- the floor stopped being rooms joined by
       passages and became one open plain with some walls in it.

       Nearest-first keeps corridors short, and short corridors mean the
       rooms near each other end up genuinely near each other: the floor
       falls into neighbourhoods on its own, without anything having to
       plan them. */
    {
        /* Prim, carrying the best-so-far distance per room: one O(n) scan to
           pick the next room and one O(n) scan to relax, rather than
           rescanning every joined room every time. At sixty rooms either
           works; at four hundred and forty the naive form is cubic. */
        static bool joined[MAX_ROOMS_MAX];
        static long best_d[MAX_ROOMS_MAX];
        static int  best_a[MAX_ROOMS_MAX];
        static int  cx[MAX_ROOMS_MAX], cy[MAX_ROOMS_MAX];

        for (int i = 0; i < room_count; i++) {
            joined[i] = false;
            room_center(rooms[i], &cx[i], &cy[i]);
        }
        joined[0] = true;
        for (int i = 1; i < room_count; i++) {
            long ddx = cx[i] - cx[0], ddy = cy[i] - cy[0];
            best_d[i] = ddx * ddx + ddy * ddy;
            best_a[i] = 0;
        }

        for (int done = 1; done < room_count; done++) {
            int pick = -1;
            for (int i = 1; i < room_count; i++)
                if (!joined[i] && (pick < 0 || best_d[i] < best_d[pick])) pick = i;
            if (pick < 0) break;

            connect_rooms(m, rooms[best_a[pick]], rooms[pick]);
            joined[pick] = true;

            for (int i = 1; i < room_count; i++) {
                if (joined[i]) continue;
                long ddx = cx[i] - cx[pick], ddy = cy[i] - cy[pick];
                long dd = ddx * ddx + ddy * ddy;
                if (dd < best_d[i]) { best_d[i] = dd; best_a[i] = pick; }
            }
        }
    }

    /* Loops, so the floor is not a tree you have to back out of. Also to a
       near neighbour: a random cross-link on a big map is a motorway. */
    int extra = room_count / 6;
    for (int i = 0; i < extra; i++) {
        int a = rand() % room_count;
        int b = nearest_room(rooms, room_count, a, 1 + rand() % 4);
        if (b >= 0) connect_rooms(m, rooms[a], rooms[b]);
    }

    /* Roads in. Two apiece, from whichever rooms happen to come up, so a
       district is somewhere you pass through rather than a dead end you have
       to back out of. carve_*_corridor turns water into bridge on the way,
       which is how a road crosses the sea without draining it. */
    for (int i = 0; i < dist_count; i++) {
        Room anchor = { dist_anchor_x[i], dist_anchor_y[i], 1, 1 };
        for (int k = 0; k < 2; k++)
            connect_rooms(m, anchor, rooms[rand() % room_count]);
    }

    /* A hub is only a hub if more of the floor runs through it. Three or four
       extra spokes each is what turns a square room into somewhere you keep
       passing back through, which is the whole point of having one. */
    for (int i = 0; i < room_count; i++) {
        if (!is_hub[i]) continue;
        int spokes = 3 + rand() % 2;
        for (int k = 0; k < spokes; k++) {
            int b = nearest_room(rooms, room_count, i, k + 1);
            /* A hub's approaches are wide: that is most of what makes it
               read as a junction rather than another square room. */
            if (b >= 0) connect_rooms_wide(m, rooms[i], rooms[b], 2 + (rand() % 100 < 35));
        }
    }

    if (have_haven) {
        /* Wall it now that every other corridor has been run: whatever
           crossed the ground gets built over, and the only ways in are the
           gates and the roads cut below. */
        carve_haven(m, haven);

        /* Three gates on three different sides, so the camp is somewhere you
           pass through rather than a pocket you back out of -- but only
           counting the ones that actually reach open ground, and trying the
           fourth side if an earlier one pointed into the border. */
        int first = rand() % 4;
        int roads = 0;
        for (int k = 0; k < 4 && roads < 3; k++)
            if (haven_gate_road(m, haven, (first + k) % 4)) roads++;

        /* Every side was a dead end. A shredded palisade beats a camp nobody
           can walk into, so fall back to an ordinary corridor. */
        if (roads == 0) {
            Room anchor = { haven.x + haven.w / 2, haven.y + haven.h / 2, 1, 1 };
            connect_rooms(m, anchor, rooms[rand() % room_count]);
        }

    }



    int ux, uy, dx, dy;
    room_center(rooms[0], &ux, &uy);
    room_center(rooms[room_count - 1], &dx, &dy);

    m->stairs_up_x = ux;
    m->stairs_up_y = uy;
    m->tiles[uy][ux].type = TILE_STAIRS_UP;

    if (!boss_floor) {
        m->stairs_down_x = dx;
        m->stairs_down_y = dy;
        m->tiles[dy][dx].type = TILE_STAIRS_DOWN;
    } else {
        m->stairs_down_x = -1;
        m->stairs_down_y = -1;
    }

    *out_x = ux;
    *out_y = uy;

    /* Quarters, last, once there is an arrival point and a way down to verify
       against. Returns how many dividing lines survived their own check; on
       the small worlds it does nothing at all. */
    m->quarter_lines = carve_quarters(m, ux, uy, m->stairs_down_x, m->stairs_down_y);

    if (!boss_floor) {
        int miasmas = miasma_min + (miasma_max > miasma_min ? rand() % (miasma_max - miasma_min + 1) : 0);
        for (int i = 0; i < miasmas; i++) {
            int ri = 1 + rand() % (room_count - 1);
            Room r = rooms[ri];
            carve_blob_over(m, r.x + rand() % r.w, r.y + rand() % r.h, 1 + rand() % 2, TILE_FLOOR, TILE_MIASMA);
        }
    }

    /* monsters: skip the player's arrival room. Swarm and Hardcore share the
       same 10-20x density profile (Hardcore is Swarm's density plus no
       respawns and no recall, for players who've already beaten Swarm);
       Hard sits at a modest 2-3x, a real but much gentler step up from
       Normal. */
    bool heavy = p && (p->difficulty == DIFFICULTY_SWARM || p->difficulty == DIFFICULTY_HARDCORE) && !boss_floor;
    bool moderate = p && p->difficulty == DIFFICULTY_HARD && !boss_floor;

    /* One Hard floor in eight is overrun: it generates at Swarm density
       rather than Hard's 2-3x. Hard was a flat step up from Normal, which a
       built character walks through as easily as Normal -- this puts an
       occasional floor in the way that has to be fought or fled rather than
       auto-explored, without making every floor a slog. Boss floors are
       exempt; they are already the fight. */
    m->overrun = moderate && (rand() % 8 == 0);
    if (m->overrun) { heavy = true; moderate = false; }

    /* One floor in eight is a gold rush, on every difficulty. The economy is
       what a hundred-floor run is now gated on (EVALUATION §25), and grinding
       ordinary floors for it is the least interesting way to fix that. This
       makes the money a find: a floor you recognise on arrival and choose to
       clear out, against the same monsters as any other.

       Not on boss floors -- those already have your attention. */
    m->gold_rush = !boss_floor && (rand() % 8 == 0);
    /* Density ramps with depth rather than sitting flat.
     *
       A flat 10-20x meant floor 1 of Swarm was ~870 monsters against a level-1
       character with a starting weapon and no spells -- arithmetic, not a
       fight, and it killed every run before the first staircase (Phase 0.3).
       Flattening the mode was never the answer: the horde *is* the mode.
       Ramping means you grow into the invasion instead of opening inside it,
       and it lets the deep end go considerably *past* where the flat number
       capped out -- floor 60+ is denser now than floor 100 used to be. */
    /* The base is what floor 1 opens at, and it is the number that decides
       whether a mode's opening is survivable -- which is where every death in
       Hard, Swarm and Hardcore happens.
     *
       Before EVALUATION §89 these were 2 for Hard and 1 for Swarm, which put
       floor-1 total incoming attack at Normal 609, Hard 1294, Swarm 186 --
       the difficulty ladder exactly inverted. The values below are what the
       sweep landed on and what §89 measures; the environment overrides stay
       so the next pass does not need a rebuild per point. */
    int swarm_mult = 1;
    if (heavy) {
        int base = tunable("AETHER_SWARM_BASE", SWARM_FLOOR1_DENSITY);
        swarm_mult = base + floor_num * 11 / 20;   /* fl 10 -> 6, fl 30 -> 17, fl 60 -> 34 */
        /* Hardcore is meant to sit *above* Swarm and had no way to: the two
           generated byte-identical floors, differing only in respawns and
           recall. This is the one lever that separates them. */
        if (p && p->difficulty == DIFFICULTY_HARDCORE)
            swarm_mult = swarm_mult * tunable("AETHER_HC_DENSITY_PCT", HARDCORE_DENSITY_PCT) / 100;
        if (swarm_mult > 40) swarm_mult = 40;
        if (swarm_mult < 1) swarm_mult = 1;
        swarm_mult += rand() % 3;                  /* a little floor-to-floor variance */
    } else if (moderate) {
        swarm_mult = tunable("AETHER_HARD_BASE", HARD_FLOOR1_DENSITY) + floor_num / 20;
        if (swarm_mult > 8) swarm_mult = 8;
        if (swarm_mult < 1) swarm_mult = 1;
    }
    int spawn_chance = heavy ? 100 : (moderate ? 80 : 65);

    /* How many things are on the floor is budgeted off the floor's *area*,
       not off how many rooms the current room mix happens to produce. Room
       counts move every time the shapes are retuned -- they went from 60 to
       roughly 400 when the map grew -- and the number of things trying to
       kill you should not move with them. `spawn_chance` still decides how
       clumped they are: a low chance means fewer rooms holding more each. */
    int monster_budget = SCALE_BY_AREA(58) * swarm_mult;
    if (monster_budget > MAX_MONSTERS - 8) monster_budget = MAX_MONSTERS - 8;

    m->monster_count = 0;
    for (int i = 1; i < room_count && m->monster_count < monster_budget; i++) {
        if (rand() % 100 < spawn_chance && m->monster_count < MAX_MONSTERS - 4) {
            int n = (1 + rand() % 2) * swarm_mult;
            for (int k = 0; k < n && m->monster_count < MAX_MONSTERS - 4
                                  && m->monster_count < monster_budget; k++) {
                int mx = rooms[i].x + rand() % rooms[i].w;
                int my = rooms[i].y + rand() % rooms[i].h;
                if (mx == ux && my == uy) continue;
                if (mx == dx && my == dy && !boss_floor) continue;
                if (in_haven(m, mx, my)) continue;
                if (m->tiles[my][mx].type != TILE_FLOOR) continue;
                if (monster_at(m, mx, my)) continue;
                Monster mo = make_monster_for_floor(floor_num, mx, my);
                if (heavy) {
                    /* same stats per monster, and they still only aggro
                       once they can actually see you -- but 10-20x the
                       count means the payout per kill has to shrink or the
                       economy runs away */
                    mo.xp_reward = mo.xp_reward / 5;
                    if (mo.xp_reward < 1) mo.xp_reward = 1;
                    mo.gold_reward = mo.gold_reward / 5;
                    if (mo.gold_reward < 1) mo.gold_reward = 1;

                    /* An overrun floor is a hazard, not a harvest. Swarm and
                       Hardcore take a further 10x off in grant_xp because the
                       whole run is at this density; a one-off Hard floor does
                       not, so without this an overrun floor would hand over
                       2-4x a normal floor's XP and make Hard *easier*. */
                    if (m->overrun) {
                        mo.xp_reward = mo.xp_reward / 4;
                        if (mo.xp_reward < 1) mo.xp_reward = 1;
                    }
                } else if (moderate) {
                    mo.xp_reward = mo.xp_reward / 2;
                    if (mo.xp_reward < 1) mo.xp_reward = 1;
                    mo.gold_reward = mo.gold_reward / 2;
                    if (mo.gold_reward < 1) mo.gold_reward = 1;
                }
                m->monsters[m->monster_count++] = mo;
            }
        }
    }

    /* One pass over the rooms rarely spends the whole budget -- most rooms
       roll no monsters at all -- so keep sweeping until it is spent or the
       floor plainly cannot hold any more. */
    for (int pass = 0; pass < 12 && m->monster_count < monster_budget; pass++) {
        int before = m->monster_count;
        for (int i = 1; i < room_count && m->monster_count < monster_budget; i++) {
            if (rand() % 100 >= spawn_chance) continue;
            int mx = rooms[i].x + rand() % rooms[i].w;
            int my = rooms[i].y + rand() % rooms[i].h;
            if (mx == ux && my == uy) continue;
            if (mx == dx && my == dy && !boss_floor) continue;
            if (in_haven(m, mx, my)) continue;
            if (m->tiles[my][mx].type != TILE_FLOOR) continue;
            if (monster_at(m, mx, my)) continue;
            Monster mo = make_monster_for_floor(floor_num, mx, my);
            if (heavy) {
                mo.xp_reward = mo.xp_reward / 5;
                if (mo.xp_reward < 1) mo.xp_reward = 1;
                mo.gold_reward = mo.gold_reward / 5;
                if (mo.gold_reward < 1) mo.gold_reward = 1;
                if (m->overrun) {
                    mo.xp_reward = mo.xp_reward / 4;
                    if (mo.xp_reward < 1) mo.xp_reward = 1;
                }
            } else if (moderate) {
                mo.xp_reward = mo.xp_reward / 2;
                if (mo.xp_reward < 1) mo.xp_reward = 1;
                mo.gold_reward = mo.gold_reward / 2;
                if (mo.gold_reward < 1) mo.gold_reward = 1;
            }
            m->monsters[m->monster_count++] = mo;
        }
        if (m->monster_count == before) break;   /* nowhere left to put them */
    }

    /* An ecosystem, not scenery. A district holds rather more than a room of
       the same footprint would -- it is bigger, and nothing has been clearing
       it out. */
    for (int i = 0; i < dist_count; i++) {
        int n = (3 + rand() % 4) * swarm_mult;
        for (int k = 0; k < n && m->monster_count < MAX_MONSTERS - 4; k++) {
            for (int tries = 0; tries < 12; tries++) {
                int mx = districts[i].x + rand() % districts[i].w;
                int my = districts[i].y + rand() % districts[i].h;
                if (m->tiles[my][mx].type != TILE_FLOOR) continue;
                if ((mx == ux && my == uy) || (mx == dx && my == dy)) continue;
                if (monster_at(m, mx, my)) continue;
                Monster mo = make_monster_for_floor(floor_num, mx, my);
                if (heavy) {
                    mo.xp_reward = mo.xp_reward / 5;
                    if (mo.xp_reward < 1) mo.xp_reward = 1;
                    mo.gold_reward = mo.gold_reward / 5;
                    if (mo.gold_reward < 1) mo.gold_reward = 1;
                    if (m->overrun) {
                        mo.xp_reward = mo.xp_reward / 4;
                        if (mo.xp_reward < 1) mo.xp_reward = 1;
                    }
                } else if (moderate) {
                    mo.xp_reward = mo.xp_reward / 2;
                    if (mo.xp_reward < 1) mo.xp_reward = 1;
                    mo.gold_reward = mo.gold_reward / 2;
                    if (mo.gold_reward < 1) mo.gold_reward = 1;
                }
                m->monsters[m->monster_count++] = mo;
                break;
            }
        }
    }

    if (floor_num % 10 == 0 && !boss_floor && m->monster_count < MAX_MONSTERS) {
        Room lr = rooms[room_count - 1];
        for (int tries = 0; tries < 20; tries++) {
            int mx = lr.x + rand() % lr.w;
            int my = lr.y + rand() % lr.h;
            if (mx == dx && my == dy) continue;
            if (monster_at(m, mx, my)) continue;
            m->monsters[m->monster_count++] = make_elite_for_floor(floor_num, mx, my);
            break;
        }
    }

    if (is_biome_boss_floor(floor_num) && !boss_floor && m->monster_count < MAX_MONSTERS) {
        Room lr = rooms[room_count - 1];
        for (int tries = 0; tries < 20; tries++) {
            int mx = lr.x + rand() % lr.w;
            int my = lr.y + rand() % lr.h;
            if (mx == dx && my == dy) continue;
            if (monster_at(m, mx, my)) continue;
            m->monsters[m->monster_count++] = make_biome_boss(floor_num, mx, my);
            break;
        }
    }

    if (boss_floor && m->monster_count < MAX_MONSTERS) {
        m->monsters[m->monster_count++] = make_boss(dx, dy);
    }

    /* loot: gold and consumables scattered through non-arrival rooms.
       Budgeted off area for the same reason the monsters are -- otherwise a
       retune of the room mix quietly changes how much money a floor is
       worth, and the economy is the one number this game is gated on. */
    int item_budget = SCALE_BY_AREA(34);

    /* A gold rush you can see from the door.
     *
       It used to be a value multiplier and nothing else -- the same handful of
       piles, worth more each -- so a rush floor looked exactly like every other
       floor and the only way to discover you were on one was to walk onto a
       pile and read the number. The floor event announces it, the frame carries
       a flag for it, and the ground said nothing. A rush is supposed to be a
       floor you *recognise on arrival and choose to clear out*, which means it
       has to be visible as ground covered in gold.

       So the budget carries it too, and the split below leans hard toward
       coin. Both scale off the same GOLD_RUSH_MULT that already sets what a
       pile is worth, so a rush is one constant and not three. */
    int gold_chance = 55;
    if (m->gold_rush) {
        item_budget *= GOLD_RUSH_MULT;
        gold_chance = 90;         /* nine piles in ten are coin */
    }
    if (item_budget > MAX_FLOOR_ITEMS - 6) item_budget = MAX_FLOOR_ITEMS - 6;

    m->item_count = 0;
    /* A rush needs more passes than there are rooms to spend its budget:
       one pass drops an item in about half of them, and the budget is now
       several times that. Without this the extra allowance is simply never
       spent and the floor looks ordinary again. */
    int passes = m->gold_rush ? GOLD_RUSH_MULT : 1;
    for (int pass = 0; pass < passes && m->item_count < item_budget; pass++)
    for (int i = 1; i < room_count; i++) {
        if (m->item_count >= item_budget) break;
        if (rand() % 100 >= 55 || m->item_count >= MAX_FLOOR_ITEMS - 2) continue;
        int ix = 0, iy = 0, tries = 0;
        bool placed = false;
        while (tries < 20) {
            ix = rooms[i].x + rand() % rooms[i].w;
            iy = rooms[i].y + rand() % rooms[i].h;
            tries++;
            if ((ix == ux && iy == uy) || monster_at(m, ix, iy)) continue;
            if (m->tiles[iy][ix].type != TILE_FLOOR) continue;
            placed = true;
            break;
        }
        if (!placed) continue;

        FloorItem *fi = &m->items[m->item_count++];
        memset(fi, 0, sizeof(*fi));
        fi->x = ix;
        fi->y = iy;
        if (rand() % 100 < gold_chance) {
            fi->is_gold = true;
            fi->gold_amount = gold_value(m, 3 + floor_num / 2 + rand() % (floor_num + 5));
        } else {
            const ConsumableTemplate *t = pick_floor_loot(floor_num);
            fi->is_gold = false;
            strncpy(fi->name, t->name, sizeof(fi->name) - 1);
            fi->heal = t->heal;
            fi->atk_buff = t->atk_buff;
            fi->atk_buff_turns = t->atk_buff_turns;
            fi->def_buff = t->def_buff;
            fi->def_buff_turns = t->def_buff_turns;
            fi->is_recall = t->is_recall;
        }
    }

    /* What washed up, or was dropped by whoever did not walk back out. Two
       or three per district: enough that crossing one pays. */
    for (int i = 0; i < dist_count; i++) {
        int drops = 2 + rand() % 2;
        for (int k = 0; k < drops && m->item_count < MAX_FLOOR_ITEMS - 2; k++) {
            for (int tries = 0; tries < 20; tries++) {
                int ix = districts[i].x + rand() % districts[i].w;
                int iy = districts[i].y + rand() % districts[i].h;
                if (m->tiles[iy][ix].type != TILE_FLOOR) continue;
                if ((ix == ux && iy == uy) || monster_at(m, ix, iy)) continue;
                FloorItem *fi = &m->items[m->item_count++];
                memset(fi, 0, sizeof(*fi));
                fi->x = ix; fi->y = iy;
                if (rand() % 100 < 55) {
                    fi->is_gold = true;
                    fi->gold_amount = gold_value(m, 3 + floor_num / 2 + rand() % (floor_num + 5));
                } else {
                    const ConsumableTemplate *t = pick_floor_loot(floor_num);
                    fi->is_gold = false;
                    strncpy(fi->name, t->name, sizeof(fi->name) - 1);
                    fi->heal = t->heal;
                    fi->atk_buff = t->atk_buff;
                    fi->atk_buff_turns = t->atk_buff_turns;
                    fi->def_buff = t->def_buff;
                    fi->def_buff_turns = t->def_buff_turns;
                    fi->is_recall = t->is_recall;
                }
                break;
            }
        }
    }

    if (floor_num % 10 == 0 && !boss_floor && m->item_count < MAX_FLOOR_ITEMS) {
        Room lr = rooms[room_count - 1];
        FloorItem *fi = &m->items[m->item_count++];
        memset(fi, 0, sizeof(*fi));
        fi->x = lr.x;
        fi->y = lr.y;
        if (fi->x == dx && fi->y == dy) fi->x = lr.x + lr.w - 1;
        fi->is_gold = true;
        fi->gold_amount = 30 + floor_num * 2;
    }

    /* QUEST_FETCH's target, planted only on the floor the active bounty
       actually asks for. Not gated on boss_floor -- a fetch quest can
       still land on a boss floor, it just shares the room with whatever
       else is there. */
    if (p && p->quest_active && p->quest_type == QUEST_FETCH && !p->quest_item_found &&
        p->quest_floor == floor_num && room_count >= 2 && m->item_count < MAX_FLOOR_ITEMS) {
        for (int tries = 0; tries < 20; tries++) {
            Room qr = rooms[1 + rand() % (room_count - 1)];
            int qx = qr.x + rand() % qr.w, qy = qr.y + rand() % qr.h;
            if ((qx == dx && qy == dy) || (qx == ux && qy == uy)) continue;
            if (monster_at(m, qx, qy)) continue;
            FloorItem *fi = &m->items[m->item_count++];
            memset(fi, 0, sizeof(*fi));
            fi->x = qx;
            fi->y = qy;
            fi->is_quest_item = true;
            strncpy(fi->name, p->quest_monster, sizeof(fi->name) - 1);
            break;
        }
    }

    m->feature_count = 0;
    m->has_portals = false;
    m->lever_x = -1;
    m->lever_y = -1;
    m->event_name[0] = '\0';
    m->event_desc[0] = '\0';

    /* Ore veins, in the walls where a vein belongs. Scattered rather than
       districted: the material gate (items.c) means every depth band has to
       be able to supply its own tier, so this cannot be left to a district
       that only shows up on half the floors. */
    if (!boss_floor) {
        int veins = 6 + floor_num / 6;
        for (int i = 0; i < veins; i++) {
            for (int tries = 0; tries < 30; tries++) {
                int x = 1 + rand() % (MAP_W - 2), y = 1 + rand() % (MAP_H - 2);
                if (m->tiles[y][x].type != TILE_WALL) continue;
                /* Only where you could actually reach it to work it. */
                bool reachable = false;
                for (int d = 0; d < 8 && !reachable; d++) {
                    static const int vx[8] = {-1,1,0,0,-1,1,-1,1};
                    static const int vy[8] = {0,0,-1,1,-1,-1,1,1};
                    if (is_walkable_player(m, x + vx[d], y + vy[d])) reachable = true;
                }
                if (!reachable) continue;
                m->tiles[y][x].type = TILE_ORE;
                break;
            }
        }
    }

    /* An assembly line gets its console, inside the works where it belongs
       rather than wherever place_feature happens to land. */
    for (int i = 0; i < m->district_count; i++) {
        if (m->districts[i].kind != DIST_ASSEMBLY) continue;
        Room dr = { m->districts[i].x, m->districts[i].y,
                    m->districts[i].w, m->districts[i].h };
        place_feature_in(m, dr, FEATURE_CONSOLE);
    }

    /* And a watch gets the thing it is watching. Same reasoning: the whole
       district is an argument about whether the box is worth it, which does
       not work if the box is somewhere else. */
    for (int i = 0; i < m->district_count; i++) {
        if (m->districts[i].kind != DIST_WATCH) continue;
        Room dr = { m->districts[i].x, m->districts[i].y,
                    m->districts[i].w, m->districts[i].h };
        place_feature_in(m, dr, FEATURE_STRONGBOX);
    }

    /* The camp's own fittings go here, below `m->feature_count = 0`, not up
       where the palisade is built. Anything placed before that line is
       silently wiped -- the altar was lost to exactly this once already. */
    if (have_haven) {
        place_feature_in(m, haven, FEATURE_FOUNTAIN);   /* always: water is the point */
        if (rand() % 100 < 75) place_feature_in(m, haven, FEATURE_MERCHANT);
        if (rand() % 100 < 35) place_feature_in(m, haven, FEATURE_SHRINE);
    }

    if (!boss_floor) {
        maybe_place_vault(m, rooms, room_count, floor_num);
        maybe_place_lever_vault(m, rooms, room_count, floor_num);
        if (floor_num % waygate_interval() == 0) place_town_gate(m, rooms, room_count);

        int merchant_chance = (floor_num <= 85) ? 10 : 3;
        if (rand() % 100 < merchant_chance) place_feature(m, rooms, room_count, FEATURE_MERCHANT);

        if (rand() % 100 < 10) place_feature(m, rooms, room_count, FEATURE_FOUNTAIN);

        int shrine_chance = (m->biome == BIOME_RUINS || m->biome == BIOME_ABYSS) ? 12 : 5;
        if (rand() % 100 < shrine_chance) place_feature(m, rooms, room_count, FEATURE_SHRINE);

        int machine_chance = (m->biome == BIOME_INDUSTRIAL) ? 15 : 5;
        if (rand() % 100 < machine_chance) place_feature(m, rooms, room_count, FEATURE_MACHINE);

        bool elite_floor = (floor_num % 10 == 0);
        if (elite_floor || rand() % 100 < 6) place_feature(m, rooms, room_count, FEATURE_RELIC);

        /* An altar every tenth floor. Ten floors is exactly the span its
           bargain covers, so there is always another one waiting when the
           last one lapses -- the offer recurs, the boon never stacks.

           Placed here rather than with the loot: everything above this point
           runs before `m->feature_count = 0`, which is where the first
           attempt quietly went. */
        if (elite_floor) place_feature(m, rooms, room_count, FEATURE_ALTAR);

        /* The tollkeeper only bothers with floors big enough to get lost on.
           On the Shaft the stairs are never far and paying everything you
           own to be shown them would be a joke rather than a decision. */
        if (waygate_interval() == 1 && rand() % 100 < 35)
            place_feature(m, rooms, room_count, FEATURE_TOLL);

        if (room_count >= 4 && rand() % 100 < 8) {
            int ra = 1 + rand() % (room_count - 1);
            int rb;
            do { rb = 1 + rand() % (room_count - 1); } while (rb == ra);
            Room a = rooms[ra], b = rooms[rb];
            int ax = a.x + rand() % a.w, ay = a.y + rand() % a.h;
            int bx = b.x + rand() % b.w, by = b.y + rand() % b.h;
            if (m->tiles[ay][ax].type == TILE_FLOOR && m->tiles[by][bx].type == TILE_FLOOR &&
                !monster_at(m, ax, ay) && !monster_at(m, bx, by)) {
                m->tiles[ay][ax].type = TILE_PORTAL;
                m->tiles[by][bx].type = TILE_PORTAL;
                m->has_portals = true;
                m->portal_ax = ax; m->portal_ay = ay;
                m->portal_bx = bx; m->portal_by = by;
            }
        }

        apply_random_event(m, floor_num, rooms, room_count, p);
    }

    /* Last of all: guarantee the way down is actually walkable from the way
     * in.
     *
     * The room graph is connected by construction, but almost everything
     * after it moves walls -- lakes, lava, sealed vaults, floor events. A
     * corridor drowned or bricked over end to end leaves the far half of the
     * floor sealed off, and the run is finished: no stairs to find, and
     * auto-explore's "nothing left to explore, and no stairs found" was
     * telling the exact truth.
     *
     * Measured at 2 floors in 720 with no route at all, plus 4 more reachable
     * only by wading lava. Rare enough to survive a hundred playtests, common
     * enough that somebody eventually gets a run they cannot finish.
     *
     * This has to be the last thing that touches tiles, which is why it sits
     * here rather than beside the stairs it protects -- an earlier version
     * ran right after placement and was quietly undone by the vault pass. */
    if (!boss_floor && m->stairs_down_x >= 0
        && !tile_reachable(m, m->stairs_up_x, m->stairs_up_y,
                           m->stairs_down_x, m->stairs_down_y)) {
        connect_rooms(m, rooms[0], rooms[room_count - 1]);
        /* Re-stamp: the corridor may have run straight over either of them. */
        m->tiles[m->stairs_up_y][m->stairs_up_x].type = TILE_STAIRS_UP;
        m->tiles[m->stairs_down_y][m->stairs_down_x].type = TILE_STAIRS_DOWN;
    }

    /* And the camp gets the same guarantee, for the same reason.
     *
       Twice now a camp has come out unreachable, and twice the cause was a
       different pass: a gate road that dead-ended at the map border, then one
       that stopped at a wall of crystal. Both were real and both were fixed,
       but a generator with this many interacting passes does not want a proof
       that each pass is safe -- it wants an invariant checked once at the end,
       exactly like the stairs above. */
    if (have_haven && m->haven_w > 0) {
        int hx = haven.x + haven.w / 2, hy = haven.y + haven.h / 2;
        /* Aim at something inside the walls you can actually stand on. */
        for (int ry = 0; ry < haven.h && !is_walkable_player(m, hx, hy); ry++)
            for (int rx = 0; rx < haven.w && !is_walkable_player(m, hx, hy); rx++)
                { hx = haven.x + rx; hy = haven.y + ry; }

        if (is_walkable_player(m, hx, hy) &&
            !tile_reachable(m, m->stairs_up_x, m->stairs_up_y, hx, hy)) {
            Room anchor = { hx, hy, 1, 1 };
            connect_rooms(m, anchor, rooms[0]);
            m->tiles[m->stairs_up_y][m->stairs_up_x].type = TILE_STAIRS_UP;
            if (m->stairs_down_x >= 0)
                m->tiles[m->stairs_down_y][m->stairs_down_x].type = TILE_STAIRS_DOWN;
        }
    }

    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            m->tiles[y][x].visible = false;
            m->tiles[y][x].seen = false;
        }
    }
    fov_forget();   /* nothing on the old floor is lit any more */
    hero_vision_forget();   /* and nobody remembers a floor they have not been on */
}

bool line_of_sight(const Map *m, int x0, int y0, int x1, int y1) {
    /* Both endpoints are checked up front, and every stepped cell again
       below: this walks a Bresenham line straight into m->tiles[][] with
       no clamping of its own, so an out-of-range coordinate reaching it
       would be an out-of-bounds read rather than a wrong answer. */
    if (x0 < 0 || x0 >= MAP_W || y0 < 0 || y0 >= MAP_H) return false;
    if (x1 < 0 || x1 >= MAP_W || y1 < 0 || y1 >= MAP_H) return false;

    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    while (!(x == x1 && y == y1)) {
        if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return false;
        if (!(x == x0 && y == y0)) {
            TileType bt = m->tiles[y][x].type;
            if (bt == TILE_WALL || bt == TILE_THICKET) return false;
        }
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
    return true;
}

/* ---- what was lit last frame ------------------------------------------
 * compute_fov used to clear `visible` across the whole map before lighting
 * a disc of radius 7 -- O(width x height) work every single turn to undo
 * O(radius squared) of lighting. At 140x80 that is 0.005 ms and nobody
 * notices. At 1400x800 it is 0.409 ms, an 80x rise for a map only 10x wider,
 * and it becomes the single largest per-turn cost in the game.
 *
 * So remember which tiles were actually lit and clear only those. The list
 * is a render cache, not game state: it is deliberately not part of Map and
 * not saved. Anything that replaces the map wholesale calls fov_forget().
 */
#define FOV_LIT_MAX 8192
static int g_lit[FOV_LIT_MAX];
static int g_lit_count = 0;
static bool g_lit_overflow = false;   /* too many to track: clear everything */

void fov_forget(void) {
    g_lit_count = 0;
    g_lit_overflow = false;
}

/* Counts tiles the moment they are first revealed, so nothing downstream
   has to sweep the map to ask "have I uncovered anything lately". Auto-
   explore's progress check used to answer that by scanning every tile, every
   step -- 1.1 million of them at 1400x800. Deliberately not part of Map:
   it is a monotonic tick, not game state, and a save/load resetting it costs
   one spurious "something changed" and nothing else. */
static unsigned long g_reveal_ticks = 0;

unsigned long fov_reveal_ticks(void) { return g_reveal_ticks; }

/* Bumped by whoever actually lifts the fog. vision.c composites six per-body
   maps into the Tile flags and is the thing that uncovers a tile now, so the
   counter has to be reachable from there -- auto-explore's progress check
   reads it, and a counter that stopped moving would read as "stuck". */
void fov_note_reveal(void) { g_reveal_ticks++; }

static void fov_light(Map *m, int x, int y) {
    if (!m->tiles[y][x].visible) {
        if (g_lit_count < FOV_LIT_MAX) g_lit[g_lit_count++] = y * MAP_W + x;
        else g_lit_overflow = true;
    }
    if (!m->tiles[y][x].seen) g_reveal_ticks++;
    m->tiles[y][x].visible = true;
    m->tiles[y][x].seen = true;
}

static void fov_clear_previous(Map *m) {
    if (g_lit_overflow) {
        /* More lit tiles than the list can hold -- a huge party, or a future
           caller lighting half the floor. Fall back to the honest sweep. */
        for (int y = 0; y < MAP_H; y++)
            for (int x = 0; x < MAP_W; x++) m->tiles[y][x].visible = false;
    } else {
        for (int i = 0; i < g_lit_count; i++)
            m->tiles[g_lit[i] / MAP_W][g_lit[i] % MAP_W].visible = false;
    }
    g_lit_count = 0;
    g_lit_overflow = false;
}

void add_fov(Map *m, int px, int py, int radius) {
    for (int y = py - radius; y <= py + radius; y++) {
        for (int x = px - radius; x <= px + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int ddx = x - px, ddy = y - py;
            if (ddx * ddx + ddy * ddy > radius * radius) continue;
            if (line_of_sight(m, px, py, x, y)) fov_light(m, x, y);
        }
    }
    if (px >= 0 && px < MAP_W && py >= 0 && py < MAP_H) fov_light(m, px, py);
}

void compute_fov(Map *m, int px, int py, int radius) {
    fov_clear_previous(m);

    for (int y = py - radius; y <= py + radius; y++) {
        for (int x = px - radius; x <= px + radius; x++) {
            if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) continue;
            int ddx = x - px, ddy = y - py;
            if (ddx * ddx + ddy * ddy > radius * radius) continue;
            if (line_of_sight(m, px, py, x, y)) fov_light(m, x, y);
        }
    }
    if (px >= 0 && px < MAP_W && py >= 0 && py < MAP_H) fov_light(m, px, py);
}
