#include "path.h"

const int PATH_DX[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
const int PATH_DY[8] = { 0, 0,-1, 1, -1,-1,  1, 1};

/* See path.h: never route through the way back up. */
static bool path_blocked(const Map *m, int x, int y) {
    return m->tiles[y][x].type == TILE_STAIRS_UP;
}

/* ---- visited marking --------------------------------------------------
 * Both searches used to memset a width-by-height `visited` array and a
 * matching parent array on every call. That is O(map) work to answer a
 * question that usually touches a few hundred squares, and it is called
 * several times a turn -- once for the player, once per companion.
 *
 * A generation stamp replaces the clear entirely: bump a counter, and a
 * square counts as visited only if its stamp matches the current one. The
 * wrap case is the only subtlety, and it is handled by doing the sweep once,
 * every four billion calls.
 */
static unsigned int g_gen = 0;
static unsigned int g_stamp[MAP_H_MAX][MAP_W_MAX];

static void path_begin(void) {
    if (++g_gen == 0) {                 /* wrapped: retire every old stamp */
        memset(g_stamp, 0, sizeof(g_stamp));
        g_gen = 1;
    }
}
static bool path_seen(int x, int y)  { return g_stamp[y][x] == g_gen; }
static void path_mark(int x, int y)  { g_stamp[y][x] = g_gen; }

/* A* rather than breadth-first.
 *
   Both find a shortest route -- every step costs one, diagonals included --
   but breadth-first has no idea where it is going, so it floods every tile
   between you and the target before arriving. Measured on the Well, walking
   to a known staircase: 13.2 ms a call, every turn auto-explore takes, and
   the companions pay it too.

   A* with Chebyshev distance is admissible here precisely *because* the cost
   is uniform and diagonal: max(|dx|,|dy|) is the exact number of steps across
   open ground, so it never overestimates and the route stays shortest.

   The frontier search below stays breadth-first on purpose -- it has no
   single target to aim at, and it already costs nothing because it usually
   answers from the tile you are standing on. */
static int g_gscore[MAP_H_MAX][MAP_W_MAX];
/* Stamped alongside g_gscore: without it, a g-score left over from the last
   search reads as a real one, and a *worse* route silently overwrites a
   better one. That turns A* into breadth-first with a heap bolted on -- which
   is measurably slower than plain breadth-first, and was. */
static unsigned int g_gstamp[MAP_H_MAX][MAP_W_MAX];

/* Binary min-heap over f-score. Small and local: a bucket queue would be
   faster still, and this is already three orders off the thing it replaced. */
typedef struct { int f, x, y; } PathNode;
static PathNode g_heap[MAP_W_MAX * MAP_H_MAX];
static int g_heap_n;

static void heap_push(int f, int x, int y) {
    int i = g_heap_n++;
    g_heap[i].f = f; g_heap[i].x = x; g_heap[i].y = y;
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (g_heap[parent].f <= g_heap[i].f) break;
        PathNode t = g_heap[parent]; g_heap[parent] = g_heap[i]; g_heap[i] = t;
        i = parent;
    }
}

static PathNode heap_pop(void) {
    PathNode top = g_heap[0];
    g_heap[0] = g_heap[--g_heap_n];
    int i = 0;
    for (;;) {
        int l = 2 * i + 1, r = l + 1, best = i;
        if (l < g_heap_n && g_heap[l].f < g_heap[best].f) best = l;
        if (r < g_heap_n && g_heap[r].f < g_heap[best].f) best = r;
        if (best == i) break;
        PathNode t = g_heap[best]; g_heap[best] = g_heap[i]; g_heap[i] = t;
        i = best;
    }
    return top;
}

static int chebyshev(int ax, int ay, int bx, int by) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

bool path_next_step(const Map *m, int sx, int sy, int tx, int ty,
                    bool avoid_hazards, bool require_seen,
                    int *out_dx, int *out_dy) {
    if (sx == tx && sy == ty) return false;
    if (tx < 0 || tx >= MAP_W || ty < 0 || ty >= MAP_H) return false;

    static signed char parent_dir[MAP_H_MAX][MAP_W_MAX];

    path_begin();
    g_heap_n = 0;

    g_gscore[sy][sx] = 0;
    g_gstamp[sy][sx] = g_gen;
    parent_dir[sy][sx] = -1;
    heap_push(chebyshev(sx, sy, tx, ty), sx, sy);

    bool found = false;
    while (g_heap_n > 0) {
        PathNode cur = heap_pop();
        int cx = cur.x, cy = cur.y;

        if (path_seen(cx, cy)) continue;   /* a better route already closed it */
        path_mark(cx, cy);

        if (cx == tx && cy == ty) { found = true; break; }

        for (int d = 0; d < 8; d++) {
            int nx = cx + PATH_DX[d], ny = cy + PATH_DY[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (path_seen(nx, ny)) continue;
            if (require_seen && !m->tiles[ny][nx].seen) continue;
            if (path_blocked(m, nx, ny)) continue;
            bool ok = avoid_hazards ? is_walkable_delegated(m, nx, ny)
                                    : is_walkable_player(m, nx, ny);
            if (!ok) continue;

            int g = g_gscore[cy][cx] + 1;
            /* Only relax if this really is a better way in. */
            if (g_gstamp[ny][nx] == g_gen && g_gscore[ny][nx] <= g) continue;
            g_gscore[ny][nx] = g;
            g_gstamp[ny][nx] = g_gen;
            parent_dir[ny][nx] = (signed char)d;
            heap_push(g + chebyshev(nx, ny, tx, ty), nx, ny);
        }
    }
    if (!found) return false;

    /* Walk the parent chain back from the target; the last square before the
       start is the step to take. */
    int cx = tx, cy = ty;
    int step_x = tx, step_y = ty;
    while (!(cx == sx && cy == sy)) {
        int d = parent_dir[cy][cx];
        if (d < 0) return false;          /* chain broken -- refuse rather than guess */
        step_x = cx; step_y = cy;
        cx -= PATH_DX[d];
        cy -= PATH_DY[d];
    }
    *out_dx = step_x - sx;
    *out_dy = step_y - sy;
    return true;
}

bool path_frontier_target(const Map *m, int sx, int sy, bool avoid_hazards,
                          int *out_x, int *out_y) {
    int dx, dy;
    return path_frontier_step_ex(m, sx, sy, avoid_hazards, &dx, &dy, out_x, out_y);
}

bool path_frontier_step(const Map *m, int sx, int sy, bool avoid_hazards,
                        int *out_dx, int *out_dy) {
    int tx, ty;
    return path_frontier_step_ex(m, sx, sy, avoid_hazards, out_dx, out_dy, &tx, &ty);
}

/* The search, reporting both the first step and the frontier tile it is
   heading for. Callers that only want to move take the step; callers that
   need to keep heading for the same place across turns take the target. */
bool path_frontier_step_ex(const Map *m, int sx, int sy, bool avoid_hazards,
                           int *out_dx, int *out_dy, int *out_tx, int *out_ty) {
    /* Already standing at the fog's edge: just step into it -- but only a
       direction you can tell (by standing next to it) is actually open
       ground, never a blind guess into a tile that could be wall forever. */
    for (int d = 0; d < 8; d++) {
        int nx = sx + PATH_DX[d], ny = sy + PATH_DY[d];
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
        if (m->tiles[ny][nx].seen) continue;
        if (path_blocked(m, nx, ny)) continue;
        bool ok = avoid_hazards ? is_walkable_delegated(m, nx, ny)
                                : is_walkable_player(m, nx, ny);
        if (!ok) continue;
        *out_dx = PATH_DX[d];
        *out_dy = PATH_DY[d];
        *out_tx = nx; *out_ty = ny;
        return true;
    }

    static signed char parent_dir[MAP_H_MAX][MAP_W_MAX];
    static int qx[MAP_W_MAX * MAP_H_MAX], qy[MAP_W_MAX * MAP_H_MAX];

    path_begin();

    int qh = 0, qt = 0;
    qx[qt] = sx; qy[qt] = sy; qt++;
    path_mark(sx, sy);
    parent_dir[sy][sx] = -1;
    int found_x = -1, found_y = -1;

    while (qh < qt) {
        int cx = qx[qh], cy = qy[qh]; qh++;
        for (int d = 0; d < 8; d++) {
            int nx = cx + PATH_DX[d], ny = cy + PATH_DY[d];
            if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
            if (path_seen(nx, ny)) continue;
            if (!m->tiles[ny][nx].seen) continue;
            if (path_blocked(m, nx, ny)) continue;
            bool ok = avoid_hazards ? is_walkable_delegated(m, nx, ny)
                                    : is_walkable_player(m, nx, ny);
            if (!ok) continue;
            path_mark(nx, ny);
            parent_dir[ny][nx] = (signed char)d;
            qx[qt] = nx; qy[qt] = ny; qt++;

            /* Does this newly-reached known tile itself border the unknown? */
            for (int d2 = 0; d2 < 8; d2++) {
                int fx = nx + PATH_DX[d2], fy = ny + PATH_DY[d2];
                if (fx < 0 || fx >= MAP_W || fy < 0 || fy >= MAP_H) continue;
                if (!m->tiles[fy][fx].seen) { found_x = nx; found_y = ny; break; }
            }
        }
        if (found_x >= 0) break;
    }
    if (found_x < 0) return false;

    int cx = found_x, cy = found_y;
    int step_x = found_x, step_y = found_y;
    while (!(cx == sx && cy == sy)) {
        int d = parent_dir[cy][cx];
        if (d < 0) return false;
        step_x = cx; step_y = cy;
        cx -= PATH_DX[d];
        cy -= PATH_DY[d];
    }
    *out_dx = step_x - sx;
    *out_dy = step_y - sy;
    *out_tx = found_x; *out_ty = found_y;
    return true;
}
