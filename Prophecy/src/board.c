#include "prophecy.h"

/* The ring is the border of a 6x6 grid, walked clockwise from the
 * top-left corner: right along the top, down, left, then back up.
 * 6*4 - 4 = 20 spaces, which is exactly the world the rules describe. */
void ring_cell(int idx, int *row, int *col)
{
    int side = GRID - 1;                 /* 5 steps per side */
    int i = ((idx % RING_N) + RING_N) % RING_N;

    if (i < side)            { *row = 0;            *col = i; }
    else if (i < 2 * side)   { *row = i - side;     *col = side; }
    else if (i < 3 * side)   { *row = side;         *col = side - (i - 2 * side); }
    else                     { *row = side - (i - 3 * side); *col = 0; }
}

int ring_step(int idx, int delta)
{
    return (((idx + delta) % RING_N) + RING_N) % RING_N;
}

/* A boat sails from a Port to the nearest Port in the chosen direction. */
int nearest_port(int from, int dir)
{
    int i, at;

    if (!ring[from].port) return -1;
    for (i = 1; i < RING_N; i++) {
        at = ring_step(from, dir * i);
        if (ring[at].port) return at;
    }
    return -1;
}

/* Each Astral Plane sits between two ring spaces and is attacked from
 * either of them.  Five planes, evenly spaced around the world. */
/* Rulebook: "to the Astral Plane closest to the Monastery, it is possible
 * to attack it from the adjacent Forest or the City" -- so a Plane lies
 * outward of a landmark and is entered from either neighbour.  Five of
 * them, every fourth space. */
static const int plane_gate[PLANES][2] = {
    {  1,  3 },   /* outward of the Magic Tower (2)  */
    {  5,  7 },   /* outward of the Monastery   (6)  */
    {  9, 11 },   /* outward of the Fortress    (10) */
    { 13, 15 },   /* outward of the Mountains   (14) */
    { 17, 19 },   /* outward of the Forest      (18) */
};
static const int plane_beyond[PLANES] = { 2, 6, 10, 14, 18 };
const char *plane_name(int p)
{
    return (p >= 0 && p < PLANES) ? ring[plane_beyond[p]].name : "?";
}

int plane_adjacent(int plane, int which)
{
    if (plane < 0 || plane >= PLANES) return -1;
    return plane_gate[plane][which & 1];
}

int plane_at(int idx)
{
    int p;

    for (p = 0; p < PLANES; p++)
        if (plane_gate[p][0] == idx || plane_gate[p][1] == idx) return p;
    return -1;
}
