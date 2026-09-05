#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "talisman.h"

CellRef gridmap[MAX_BOARDS][GRID][GRID];

/* Anything left on a space -- an enemy that beat you, a Place card, the
 * gold and Talisman of someone who died there -- stays until dealt with. */
int res_card[NSPACES][MAX_RES];
int res_n[NSPACES];
int res_gold[NSPACES];
int res_tal[NSPACES];

static const RegionDef *reg(int region)
{
    if (region < 0 || region >= region_count) region = REG_CROWN;
    return &region_tbl[region];
}

int space_id(int region, int idx)
{
    const RegionDef *g = reg(region);
    return g->base + ((idx % g->n) + g->n) % g->n;
}

int region_board(int region) { return reg(region)->board; }
int topology(int region)     { return reg(region)->topo;  }

int res_add(int sid, int card)
{
    if (res_n[sid] >= MAX_RES) return 0;
    res_card[sid][res_n[sid]++] = card;
    return 1;
}

void res_remove(int sid, int slot)
{
    int i;
    for (i = slot; i < res_n[sid] - 1; i++)
        res_card[sid][i] = res_card[sid][i + 1];
    if (res_n[sid] > 0) res_n[sid]--;
}

int ring_len(int region) { return reg(region)->n; }

const Space *space_at(int region, int idx)
{
    const RegionDef *g = reg(region);
    return &g->spaces[((idx % g->n) + g->n) % g->n];
}

/* Walk the perimeter of the (7-2*region) square, clockwise from its
 * top-left corner: right along the top, down, left, then back up. */
void ring_cell(int region, int idx, int *row, int *col)
{
    int o    = region;
    int side = (GRID - 2 * region) - 1;      /* 6, 4, 2 */
    int i;

    if (region >= REG_CROWN) { *row = *col = GRID / 2; return; }

    i = ((idx % (4 * side)) + 4 * side) % (4 * side);
    if (i < side)            { *row = o;                      *col = o + i; }
    else if (i < 2 * side)   { *row = o + (i - side);         *col = o + side; }
    else if (i < 3 * side)   { *row = o + side;               *col = o + side - (i - 2 * side); }
    else                     { *row = o + side - (i - 3 * side); *col = o; }
}

/* The Dungeon spiral drawn on its own 7x7: the first 16 spaces are the
 * perimeter of the 5x5, the next 8 the perimeter of the 3x3, and the
 * Treasure Chamber sits in the middle.  Those are the same rings the main
 * board's Middle and Inner Regions use, so ring_cell already knows how to
 * walk them -- a spiral is nested squares you do not stop between. */
void dungeon_cell(int idx, int *row, int *col)
{
    if (idx < 16)      ring_cell(1, idx,      row, col);
    else if (idx < 24) ring_cell(2, idx - 16, row, col);
    else               { *row = GRID / 2; *col = GRID / 2; }
}

/* ------------------------------------------------- distance to the Crown --
 * The AI used to judge progress by ring index, which stops meaning anything
 * the moment a second board exists: a character in the Dungeon cannot tell
 * whether she is closer to winning than before.  So the board is walked once
 * at startup as a graph and every space is given its distance, in turns, from
 * the Crown of Command.
 *
 * A turn moves you 1-6 spaces, so every space reaches its six neighbours at a
 * cost of one turn.  The crossings are edges too.  The Treasure Chamber's way
 * out is the odd one: rolling a 6 there does land you on the Crown, but it is
 * one roll in six and you do not get to try again, so that edge is weighted 6
 * rather than 1 -- otherwise the Dungeon reads as a motorway to the Crown. */
int dist_crown[2][NSPACES];   /* [0] without a Talisman, [1] with one */

#define MAXE 16
static int  adj_n[NSPACES];
static int  adj_to[NSPACES][MAXE];
static int  adj_w[NSPACES][MAXE];

static void edge(int from, int to, int w)
{
    if (from < 0 || to < 0 || adj_n[from] >= MAXE) return;
    adj_to[from][adj_n[from]] = to;
    adj_w [from][adj_n[from]] = w;
    adj_n[from]++;
}

/* Twice over: the Portal of Power "needs a Talisman to pass", so a character
 * without one has a different board in front of her -- for her the only road
 * to the Crown is the Dungeon.  A single table would tell her the surface
 * route is short when in truth it is shut. */
static void dist_pass(int with_talisman)
{
    int g, i, k, n, done[NSPACES];
    int *dist = dist_crown[with_talisman];

    for (i = 0; i < NSPACES; i++) { adj_n[i] = 0; dist[i] = 9999; done[i] = 0; }

    for (g = 0; g < region_count; g++) {
        const RegionDef *rg = &region_tbl[g];
        for (i = 0; i < rg->n; i++) {
            int from = space_id(g, i);
            for (k = 1; k <= 6; k++) {
                if (rg->topo == TOPO_RING) {
                    edge(from, space_id(g, i + k), 1);
                    edge(from, space_id(g, i - k), 1);
                } else if (i + k < rg->n) {
                    edge(from, space_id(g, i + k), 1);   /* a PATH runs one way */
                }
            }
        }
    }
    /* the crossings, each of them a turn */
    edge(space_id(REG_OUTER,  SENTINEL_IDX), space_id(REG_MIDDLE, MID_ENTRY_IDX),   1);
    if (with_talisman)
        edge(space_id(REG_MIDDLE, PORTAL_IDX), space_id(REG_INNER, INNER_ENTRY_IDX), 1);
    edge(space_id(REG_INNER,  GATE_IDX),     space_id(REG_CROWN,  0),               1);
    /* and the Treasure Chamber's lottery */
    edge(space_id(REG_DUNGEON, DUNGEON_N - 1), space_id(REG_CROWN, 0), 6);
    /* The City Gates and the main board's City space are the same place, so
     * the two boards join there; the Wharf is a second way out.  Without
     * these the whole City reads as having no road to the Crown, which
     * would be a lie the AI acts on. */
    {
        int city_sp = -1, i2;
        for (i2 = 0; i2 < OUTER_N; i2++)
            if (!strcmp(outer_ring[i2].name, "City")) city_sp = i2;
        /* The Vortex is the road home: five of its six exits put you back
         * on the main board, so the Timescape joins the map there. */
        {
            int t;
            for (t = 0; t < TIME_N; t++)
                if (time_scape[t].kind == SP_T_VORTEX) {
                    int reg2, id2;
                    if (space_by_name("Village", &reg2, &id2))
                        edge(space_id(REG_TIME, t), space_id(reg2, id2), 1);
                }
        }
        if (city_sp >= 0) {
            edge(space_id(REG_OUTER, city_sp), space_id(REG_CITY, 0), 1);
            edge(space_id(REG_CITY, 0), space_id(REG_OUTER, city_sp), 1);
            for (i2 = 0; i2 < CITY_N; i2++)
                if (city_ring[i2].kind == SP_C_WHARF)
                    edge(space_id(REG_CITY, i2), space_id(REG_OUTER, city_sp), 1);
        }
    }

    /* Dijkstra outwards from the Crown over the reversed graph. */
    dist[space_id(REG_CROWN, 0)] = 0;
    for (n = 0; n < NSPACES; n++) {
        int best = -1, bv = 9999;
        for (i = 0; i < NSPACES; i++)
            if (!done[i] && dist[i] < bv) { bv = dist[i]; best = i; }
        if (best < 0) break;
        done[best] = 1;
        for (i = 0; i < NSPACES; i++)              /* who can reach `best`? */
            for (k = 0; k < adj_n[i]; k++)
                if (adj_to[i][k] == best && dist[i] > bv + adj_w[i][k])
                    dist[i] = bv + adj_w[i][k];
    }
}

static void dist_init(void) { dist_pass(0); dist_pass(1); }

/* TALISMAN_DIST=1 prints the table, because a distance map that is quietly
 * wrong would steer the AI wrongly without ever looking wrong. */
static void dist_dump(void)
{
    int g, i;
    if (!getenv("TALISMAN_DIST")) return;
    for (i = 0; i < 2; i++) {
        fprintf(stderr, "--- %s a Talisman ---\n", i ? "with" : "without");
        for (g = 0; g < region_count; g++) {
            int j;
            fprintf(stderr, "%-8s:", region_tbl[g].name);
            for (j = 0; j < region_tbl[g].n; j++) {
                int d = dist_crown[i][space_id(g, j)];
                fprintf(stderr, " %s=%d", region_tbl[g].spaces[j].name, d >= 9999 ? -1 : d);
            }
            fprintf(stderr, "\n");
        }
    }
}

int turns_to_crown(int region, int idx, int with_talisman)
{
    int d = dist_crown[with_talisman ? 1 : 0][space_id(region, idx)];
    return d >= 9999 ? -1 : d;         /* -1: no road from here at all */
}

void board_init(void)
{
    int region, idx, r, c;

    {   /* The region table owns the space_id range; if it ever outgrows the
         * resident arrays, say so here rather than corrupt them silently. */
        int total = 0;
        for (region = 0; region < region_count; region++) {
            const RegionDef *g = &region_tbl[region];
            if (g->base + g->n > total) total = g->base + g->n;
        }
        if (total > NSPACES) {
            fprintf(stderr, "board: %d spaces declared but NSPACES is %d\n",
                    total, NSPACES);
            exit(1);
        }
    }

    for (r = 0; r < MAX_BOARDS; r++)
        for (idx = 0; idx < GRID; idx++)
            for (c = 0; c < GRID; c++) {
                gridmap[r][idx][c].region = -1;   /* no space here */
                gridmap[r][idx][c].idx    = 0;
            }

    for (region = REG_OUTER; region <= REG_INNER; region++)
        for (idx = 0; idx < ring_len(region); idx++) {
            ring_cell(region, idx, &r, &c);
            gridmap[region_board(region)][r][c].region = region;
            gridmap[region_board(region)][r][c].idx    = idx;
        }
    gridmap[region_board(REG_CROWN)][GRID / 2][GRID / 2].region = REG_CROWN;
    gridmap[region_board(REG_CROWN)][GRID / 2][GRID / 2].idx    = 0;

    dist_init();
    dist_dump();

    for (idx = 0; idx < DUNGEON_N; idx++) {
        dungeon_cell(idx, &r, &c);
        gridmap[region_board(REG_DUNGEON)][r][c].region = REG_DUNGEON;
        gridmap[region_board(REG_DUNGEON)][r][c].idx    = idx;
    }

    /* the City is a 24-space ring, which is exactly the 7x7 perimeter */
    for (idx = 0; idx < CITY_N; idx++) {
        ring_cell(REG_OUTER, idx, &r, &c);
        gridmap[region_board(REG_CITY)][r][c].region = REG_CITY;
        gridmap[region_board(REG_CITY)][r][c].idx    = idx;
    }
    gridmap[region_board(REG_DONJON)][GRID / 2][GRID / 2].region = REG_DONJON;
    gridmap[region_board(REG_DONJON)][GRID / 2][GRID / 2].idx    = 0;

    /* The Timescape is a graph, not a ring, so its 16 realities are simply
     * laid out where they will fit and read; the warp lines, not the
     * geometry, say what is next to what. */
    for (idx = 0; idx < TIME_N; idx++) {
        /* A 4x4 of realities on alternate cells: rows and columns 0,2,4,6.
         * Starting at 1 put the last row at 7 and wrote off the end of the
         * grid -- UBSan caught it, and the four realities it lost were
         * never drawn at all. */
        r = (idx / 4) * 2;
        c = (idx % 4) * 2;
        if (r >= GRID || c >= GRID) continue;
        gridmap[region_board(REG_TIME)][r][c].region = REG_TIME;
        gridmap[region_board(REG_TIME)][r][c].idx    = idx;
    }
}

/* Rulebook: a Raft carries you "to any space of his choice directly
 * opposite the one he is in".  On this board the Middle Region is the
 * next square in, so directly opposite is the same grid cell pulled one
 * step inside the ring. */
int across_river(int outer_idx)
{
    int r, c;

    ring_cell(REG_OUTER, outer_idx, &r, &c);
    if (r < 1) r = 1;  if (r > GRID - 2) r = GRID - 2;
    if (c < 1) c = 1;  if (c > GRID - 2) c = GRID - 2;
    return (gridmap[0][r][c].region == REG_MIDDLE) ? gridmap[0][r][c].idx : -1;
}

/* Rule 11:18 puts no direction on the crossing -- "A Character with a Raft
 * may cross the river to any Space of their choice directly opposite the one
 * they are in" -- so the Raft goes back out as readily as it comes in.  The
 * Outer ring is the longer one, so several of its spaces face the same
 * Middle space; step outwards until the grid stops being Middle. */
int back_across_river(int middle_idx)
{
    int r, c, dr, dc, k;
    int mid = (GRID - 1) / 2;

    ring_cell(REG_MIDDLE, middle_idx, &r, &c);
    dr = (r < mid) ? -1 : (r > mid) ? 1 : 0;
    dc = (c < mid) ? -1 : (c > mid) ? 1 : 0;
    if (!dr && !dc) return -1;
    for (k = 1; k <= 2; k++) {
        int rr = r + dr * k, cc = c + dc * k;
        if (rr < 0 || rr >= GRID || cc < 0 || cc >= GRID) break;
        if (gridmap[0][rr][cc].region == REG_OUTER) return gridmap[0][rr][cc].idx;
    }
    return -1;
}

/* Characters do not start where the table happens to seat them: each 2e
 * card names the space its owner begins on.  Returns -1 if the board has
 * no such space, so a mistyped name shows up rather than silently
 * dropping someone at the Village. */
int outer_by_name(const char *name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < OUTER_N; i++)
        if (!strcmp(outer_ring[i].name, name)) return i;
    return -1;
}

/* Find a space anywhere on the board by name -- the Treasure Chamber's
 * exit table names its destinations, and they are spread across Regions.
 * Returns 0 and leaves the outputs alone if the name is not on the board. */
int space_by_name(const char *name, int *region, int *idx)
{
    int g, i;
    if (!name) return 0;
    for (g = 0; g < region_count; g++)
        for (i = 0; i < region_tbl[g].n; i++)
            if (!strcmp(region_tbl[g].spaces[i].name, name)) {
                *region = g; *idx = i; return 1;
            }
    return 0;
}
