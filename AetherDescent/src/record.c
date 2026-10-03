#include "record.h"
#include "mapgen.h"
#include <stdio.h>

static FILE *g_tape = NULL;
static int   g_log_seen = 0;     /* how many log lines have been written out */
static long  g_tick = 0;

/* A character per tile type, for the map and viewport dumps.
 *
   Deliberately its own table rather than a call into render.c: the renderer's
   glyphs move around as the art changes, and a tape whose alphabet shifts
   under it is worth less the older it gets. These are for reading a file, not
   for drawing a game, so they are chosen to be told apart in a text editor and
   then left alone. Anything unmapped comes out '?', which is a finding in
   itself rather than a crash. */
static char tile_letter(TileType t) {
    switch (t) {
        case TILE_WALL:            return '#';
        case TILE_FLOOR:           return '.';
        case TILE_STAIRS_DOWN:     return '>';
        case TILE_STAIRS_UP:       return '<';
        case TILE_DECOR:           return ',';
        case TILE_WATER:           return '~';
        case TILE_BRIDGE:          return '=';
        case TILE_LAVA:            return '!';
        case TILE_MIASMA:          return '%';
        case TILE_PORTAL:          return 'O';
        case TILE_LOCKED_DOOR:     return '+';
        case TILE_SEALED_DOOR:     return 'D';
        case TILE_LEVER:           return '\\';
        case TILE_QUEST_BOARD:     return 'Q';
        case TILE_THICKET:         return 'T';
        case TILE_CRYSTAL:         return 'C';
        case TILE_ROD:             return 'i';
        case TILE_VENT:            return 'A';
        case TILE_CURRENT:         return 'c';
        case TILE_BLOODPOOL:       return 'b';
        case TILE_PRISM_RED:       return 'r';
        case TILE_PRISM_BLUE:      return 'u';
        case TILE_PRISM_GREEN:     return 'g';
        case TILE_BELT:            return 'B';
        case TILE_SNARE:           return 's';
        case TILE_PIT:             return 'o';
        case TILE_ORE:             return 'v';
        /* Town. A tape of the plaza is worth reading too -- the Tavern bug
           this file was written alongside happened entirely in town. */
        case TILE_SHOP_GENERAL:    return 'G';
        case TILE_SHOP_ARMORY:     return 'R';
        case TILE_SHOP_APOTHECARY: return 'P';
        case TILE_SHOP_ARCANIST:   return 'M';
        case TILE_TEMPLE_ENTRANCE: return 'E';
        case TILE_INN:             return 'N';
        case TILE_TAVERN:          return 'V';
        case TILE_JUNKYARD:        return 'J';
        case TILE_GLADIATOR:       return 'X';
        case TILE_BANK:            return 'K';
        case TILE_RACES:           return 'Z';
        case TILE_KITCHEN:         return 'H';
        case TILE_BLACKMARKET:     return 'W';
        case TILE_ORACLE:          return 'Y';
        case TILE_ALTAR:           return 'L';
        case TILE_BAZAAR:          return 'F';
        default:                   return '?';
    }
}

/* JSON string escaping, for log lines and names. Only the two characters that
   can actually appear here and would break the line -- a log message is plain
   ASCII from format strings this project writes. */
static void put_json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { fputc('\\', f); fputc(*s, f); }
        else if ((unsigned char)*s < 0x20)  fprintf(f, "\\u%04x", (unsigned)(unsigned char)*s);
        else fputc(*s, f);
    }
    fputc('"', f);
}

void record_open(const char *path) {
    if (!path || !*path) return;
    g_tape = fopen(path, "w");
    if (!g_tape) return;
    g_log_seen = 0;
    g_tick = 0;
    /* A header line, so a tape can be identified without guessing at it. */
    fprintf(g_tape, "{\"t\":\"open\",\"format\":1,\"map_w\":%d,\"map_h\":%d}\n",
            MAP_W, MAP_H);
    fflush(g_tape);
}

bool record_active(void) { return g_tape != NULL; }

void record_close(void) {
    if (!g_tape) return;
    fprintf(g_tape, "{\"t\":\"close\",\"ticks\":%ld}\n", g_tick);
    fclose(g_tape);
    g_tape = NULL;
}

/* Everything logged since the last call, as a JSON array.
 *
   log_reset() sets the count back to zero, so a count *lower* than last time
   means the log was cleared rather than that lines vanished -- start again
   from the top rather than emitting nothing for the rest of the run. */
static void put_new_log(FILE *f) {
    if (g_msg_log_count < g_log_seen) g_log_seen = 0;
    fputs("\"log\":[", f);
    for (int i = g_log_seen; i < g_msg_log_count; i++) {
        if (i > g_log_seen) fputc(',', f);
        put_json_string(f, g_msg_log[i]);
    }
    fputc(']', f);
    g_log_seen = g_msg_log_count;
}

void record_floor(const Map *m, const Player *p) {
    if (!g_tape || !m || !p) return;

    fprintf(g_tape, "{\"t\":\"floor\",\"tick\":%ld,\"floor\":%d,\"biome\":%d",
            g_tick, m->floor_num, (int)m->biome);
    fprintf(g_tape, ",\"stairs_down\":[%d,%d]", m->stairs_down_x, m->stairs_down_y);

    fputs(",\"districts\":[", g_tape);
    for (int i = 0; i < m->district_count; i++) {
        const MapDistrict *d = &m->districts[i];
        if (i) fputc(',', g_tape);
        fprintf(g_tape, "{\"kind\":%d,\"name\":", d->kind);
        const char *nm = wild_kind_name(d->kind);
        put_json_string(g_tape, nm ? nm : "?");
        fprintf(g_tape, ",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"state\":%d}",
                d->x, d->y, d->w, d->h, d->state);
    }
    fputc(']', g_tape);

    /* The whole grid, once. Rows as strings so a tape can be eyeballed. */
    fputs(",\"map\":[", g_tape);
    for (int y = 0; y < MAP_H; y++) {
        if (y) fputc(',', g_tape);
        fputc('"', g_tape);
        for (int x = 0; x < MAP_W; x++) fputc(tile_letter(m->tiles[y][x].type), g_tape);
        fputc('"', g_tape);
    }
    fputs("]}\n", g_tape);
    fflush(g_tape);
}

void record_tick(const Map *m, const Player *p, int key) {
    if (!g_tape || !m || !p) return;
    g_tick++;

    fprintf(g_tape, "{\"t\":\"tick\",\"tick\":%ld,\"turn\":%d,\"floor\":%d,\"key\":%d",
            g_tick, p->turns, p->floor, key);
    fprintf(g_tape, ",\"driven\":%d,\"gold\":%d", p->controlled, p->gold);

    fputs(",\"party\":[", g_tape);
    for (int i = 0; i < MAX_PARTY; i++) {
        const Hero *h = &p->party[i];
        if (i) fputc(',', g_tape);
        fprintf(g_tape, "{\"s\":%d,\"use\":%d,\"alive\":%d,\"x\":%d,\"y\":%d"
                        ",\"hp\":%d,\"max\":%d,\"lvl\":%d,\"name\":",
                i, h->in_use ? 1 : 0, h->alive ? 1 : 0, h->x, h->y,
                h->hp, h->maxhp, h->level);
        put_json_string(g_tape, h->name);
        fputc('}', g_tape);
    }
    fputc(']', g_tape);

    /* The viewport, which is the part that changes every turn. Camera matches
       render.c's: centred on the driven body, clamped to the grid. */
    int cx = party_x(p) - VIEW_W / 2, cy = party_y(p) - VIEW_H / 2;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx > MAP_W - VIEW_W) cx = MAP_W - VIEW_W;
    if (cy > MAP_H - VIEW_H) cy = MAP_H - VIEW_H;
    fprintf(g_tape, ",\"view\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d",
            cx, cy, VIEW_W, VIEW_H);

    /* Three parallel grids. `tile` is what is there, `seen` is whether the
       game thinks the player has ever been shown it, `vis` is whether it is
       lit right now. A cell that draws terrain with seen=0 is the "artefacts
       on undiscovered map" report, and these three lines are what prove it. */
    const char *keys[3] = { "tile", "seen", "vis" };
    for (int k = 0; k < 3; k++) {
        fprintf(g_tape, ",\"%s\":[", keys[k]);
        for (int vy = 0; vy < VIEW_H; vy++) {
            int wy = cy + vy;
            if (vy) fputc(',', g_tape);
            fputc('"', g_tape);
            for (int vx = 0; vx < VIEW_W; vx++) {
                int wx = cx + vx;
                if (wy < 0 || wy >= MAP_H || wx < 0 || wx >= MAP_W) { fputc(' ', g_tape); continue; }
                const Tile *t = &m->tiles[wy][wx];
                if (k == 0)      fputc(tile_letter(t->type), g_tape);
                else if (k == 1) fputc(t->seen ? '1' : '0', g_tape);
                else             fputc(t->visible ? '1' : '0', g_tape);
            }
            fputc('"', g_tape);
        }
        fputc(']', g_tape);
    }
    fputc('}', g_tape);

    /* Only the monsters on screen: the full array is up to six hundred
       thousand slots and none of the reports are about the ones you cannot
       see. */
    fputs(",\"mon\":[", g_tape);
    int shown = 0;
    for (int i = 0; i < m->monster_count; i++) {
        const Monster *mo = &m->monsters[i];
        if (!mo->alive) continue;
        if (mo->x < cx || mo->x >= cx + VIEW_W || mo->y < cy || mo->y >= cy + VIEW_H) continue;
        if (shown++) fputc(',', g_tape);
        fprintf(g_tape, "[%d,%d,%d,%d]", mo->x, mo->y, mo->hp, mo->aggro ? 1 : 0);
    }
    fputc(']', g_tape);

    fputc(',', g_tape);
    put_new_log(g_tape);
    fputs("}\n", g_tape);

    /* Flushed every tick on purpose. A tape is for a game that went wrong, and
       a game that went wrong is often a game that was killed -- buffering
       would lose exactly the last few turns that matter most. */
    fflush(g_tape);
}
