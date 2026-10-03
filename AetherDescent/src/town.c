#include "town.h"
#include "vision.h"
#include "mapgen.h"

static void fill_rect(Map *m, int x0, int y0, int x1, int y1, TileType t) {
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x >= 0 && x < MAP_W && y >= 0 && y < MAP_H) {
                m->tiles[y][x].type = t;
            }
        }
    }
}

void generate_town_map(Map *m) {
    memset(m, 0, sizeof(*m));
    m->floor_num = 0;

    /* TOWN_MAP_W x TOWN_MAP_H, not VIEW_W x VIEW_H -- see the note in common.h. The
       plaza is the same size regardless of what the window or the render mode
       can show of it; everything outside it, up to the much bigger dungeon
       grid, stays TILE_WALL from the memset above. */
    fill_rect(m, 0, 0, TOWN_MAP_W - 1, TOWN_MAP_H - 1, TILE_WALL);
    fill_rect(m, 1, 1, TOWN_MAP_W - 2, TOWN_MAP_H - 2, TILE_FLOOR);

    /* General Store (top-left), door faces south into the plaza */
    fill_rect(m, 5, 2, 9, 5, TILE_WALL);
    m->tiles[5][7].type = TILE_SHOP_GENERAL;

    /* Armory (top-right), door faces south */
    fill_rect(m, 48, 2, 52, 5, TILE_WALL);
    m->tiles[5][50].type = TILE_SHOP_ARMORY;

    /* The Ashfall Kitchen (top, between the store and the guild), door faces
       south. You bring the meat; the room upstairs does the rest. */
    fill_rect(m, 11, 2, 15, 5, TILE_WALL);
    m->tiles[5][13].type = TILE_KITCHEN;

    /* Apothecary (bottom-left), door faces north */
    fill_rect(m, 5, 12, 9, 15, TILE_WALL);
    m->tiles[12][7].type = TILE_SHOP_APOTHECARY;

    /* Arcanist's Guild (top, west-of-centre), door faces south */
    fill_rect(m, 18, 2, 22, 5, TILE_WALL);
    m->tiles[5][20].type = TILE_SHOP_ARCANIST;

    /* Temple of the Deep Well (bottom-centre), door faces north */
    fill_rect(m, 25, 13, 32, 16, TILE_WALL);
    m->tiles[TEMPLE_DOOR_Y][TEMPLE_DOOR_X].type = TILE_TEMPLE_ENTRANCE;

    /* The Inn (bottom-right), door faces north. Walking in rests, banks a
       snapshot to come back to, and charges for it -- there is no menu. */
    fill_rect(m, 44, 12, 49, 15, TILE_WALL);
    m->tiles[12][46].type = TILE_INN;

    /* The Tavern (bottom-left of centre), door faces north. Hire heroes. */
    fill_rect(m, 34, 12, 39, 15, TILE_WALL);
    m->tiles[12][36].type = TILE_TAVERN;

    /* The Junkyard (mid-left), door faces north. Walk in and the whole
       armful is weighed at once -- no menu, like the Inn. */
    fill_rect(m, 12, 12, 17, 15, TILE_WALL);
    m->tiles[12][14].type = TILE_JUNKYARD;

    /* The black market (bottom, between the junkyard and the temple), door
       faces north. Buys levels. Asks nothing. */
    fill_rect(m, 19, 12, 23, 15, TILE_WALL);
    m->tiles[12][21].type = TILE_BLACKMARKET;

    /* The Gladiator School (top-right of centre), door faces south. Buys a
       point in one of the thirty -- the only way an attribute moves after
       character creation. */
    fill_rect(m, 36, 2, 41, 5, TILE_WALL);
    m->tiles[5][38].type = TILE_GLADIATOR;

    /* The Bank of the Deep Well (top-centre), door faces south. Sells a
       standing multiplier on every coin you earn from here on. */
    fill_rect(m, 25, 2, 30, 5, TILE_WALL);
    m->tiles[5][27].type = TILE_BANK;

    /* The lizard track (mid-right), door faces north. Venusian racing
       lizards, and a bookmaker who has seen everything. */
    fill_rect(m, 52, 12, 56, 15, TILE_WALL);
    m->tiles[12][54].type = TILE_RACES;

    /* The Oracle's Sanctuary (top-right of centre, between the School and the
       Armory), door faces south. Sells a true answer about what is below. */
    fill_rect(m, 43, 2, 46, 5, TILE_WALL);
    m->tiles[5][44].type = TILE_ORACLE;

    /* The Altar of Sacrifice (top-centre, between the Bank and the School),
       door faces south. Deliberately next door to the Gladiator School: it is
       the same curve and the opposite verb. */
    fill_rect(m, 32, 2, 34, 5, TILE_WALL);
    m->tiles[5][33].type = TILE_ALTAR;

    /* The Barter Bazaar (bottom, between the Tavern and the Inn), door faces
       north. Out in the middle of things on purpose -- you have to walk past
       today's prices on the way to everything else. */
    fill_rect(m, 40, 12, 43, 15, TILE_WALL);
    m->tiles[12][41].type = TILE_BAZAAR;

    /* Quest board, standing alone in the open plaza (a few steps off the
       player's spawn point, not on top of it) */
    m->tiles[8][34].type = TILE_QUEST_BOARD;

    /* a couple of decorative overgrown-machinery clumps in the open plaza */
    fill_rect(m, 15, 8, 16, 9, TILE_WALL);
    fill_rect(m, 40, 8, 41, 9, TILE_WALL);

    /* scatter some cosmetic jungle-reclaimed patches (still walkable) */
    unsigned seed_positions[][2] = {
        {20, 4}, {35, 4}, {22, 11}, {37, 11}, {12, 8}, {45, 8},
        {28, 5}, {28, 10}, {18, 14}, {39, 14}, {14, 3}, {44, 15}
    };
    for (size_t i = 0; i < sizeof(seed_positions) / sizeof(seed_positions[0]); i++) {
        int x = (int)seed_positions[i][0];
        int y = (int)seed_positions[i][1];
        if (m->tiles[y][x].type == TILE_FLOOR) {
            m->tiles[y][x].type = TILE_DECOR;
        }
    }

    /* Nobody remembers a floor they are no longer standing on.
     *
       mapgen.c says it as an invariant -- "anything that replaces the map
       wholesale calls fov_forget()" -- and generate_temple_floor() does. This
       did not, and it is the other half of the same job: the *map's* flags are
       set below, but hero_vision_update() paints the **driven body's** memory
       over them every frame, and that memory was still the dungeon floor you
       had just climbed out of.

       What it looked like: the plaza lit exactly where the last floor had been
       explored and pitch black where it had not -- a dungeon's fog of war,
       cut out of a town, in every render mode. Reported from play with a
       screenshot of a black hole in the middle of the city.

       Before the flags below, not after: forgetting clears the per-body maps,
       and the composite is rebuilt from them on the next update. */
    fov_forget();
    hero_vision_forget();

    /* town is always fully lit/explored, within its own footprint */
    for (int y = 0; y < TOWN_MAP_H; y++) {
        for (int x = 0; x < TOWN_MAP_W; x++) {
            m->tiles[y][x].visible = true;
            m->tiles[y][x].seen = true;
        }
    }

    m->monster_count = 0;
    m->item_count = 0;
    m->stairs_down_x = TEMPLE_DOOR_X;
    m->stairs_down_y = TEMPLE_DOOR_Y;
}
