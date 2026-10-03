/* Per-tick position trace for the player and every hire.
 *
   Writes one CSV row per actor per tick: tick,who,x,y,hp,alive. Everything
   about "are they actually moving" becomes a question about data rather than
   an argument about what a screenshot looked like.
 *
   Built because summary metrics kept lying. A "dance step" counter that
   looked for A->B->A reported a clean 0% while one hire was running a
   ten-tile loop for hundreds of ticks and two others sat on a single square
   for 775. Positions per tick show all three instantly; no aggregate does.
 *
   WARNING, and the reason this file carries a warning at all: the simulated
   turn below must match what the game actually does per turn, or it measures
   its own bugs. The first version lit only the player's field of view and
   left out companions_reveal(), so hires walked to a frontier that could
   never be revealed and thrashed against it forever -- which looked exactly
   like a movement bug and was not one. If you add a per-turn step to the
   game, add it here.
 *
   Usage:  ./bin/trace [HIRES] [TICKS] [WORLD] [OUT.csv]
   Then:   awk -F, '$2=="hire0"' out.csv | ...  */
#include "../src/common.h"
#include "../src/mapgen.h"
#include "../src/companions.h"
#include "../src/combat.h"
#include "../src/path.h"
#include "../src/vision.h"
#include <stdio.h>

int main(int argc, char **argv) {
    int hires  = argc > 1 ? atoi(argv[1]) : 5;
    int ticks  = argc > 2 ? atoi(argv[2]) : 800;
    int world  = argc > 3 ? atoi(argv[3]) : WORLD_SHAFT;
    const char *out = argc > 4 ? argv[4] : "trace.csv";

    world_size_apply(world);
    static Map m; static Player p;
    memset(&p, 0, sizeof p);
    p.run_seed = 7; p.party[0].level = 30; p.party[0].hp = p.party[0].maxhp = 40000;
    p.party[0].base_atk = 4000; p.party[0].base_def = 4000; p.gold = 100000000;
    for (int i = 0; i < ATTR_COUNT; i++) p.party[0].attrs[i] = 5;
    p.party[0].fov_radius = 8;
    for (int i = 0; i < hires; i++) companion_hire(&p, i);

    int ux, uy;
    generate_temple_floor(&m, 20, &ux, &uy, &p);
    p.party[0].x = ux; p.party[0].y = uy;
    companions_place(&p, &m);
    compute_fov(&m, p.party[0].x, p.party[0].y, p.party[0].fov_radius);

    FILE *f = fopen(out, "w");
    if (!f) { perror("open"); return 1; }
    fprintf(f, "tick,who,x,y,hp,alive\n");

    bool died = false;
    for (int t = 0; t < ticks; t++) {
        int dx, dy;
        if (path_frontier_step(&m, p.party[0].x, p.party[0].y, true, &dx, &dy)
            && is_walkable_player(&m, p.party[0].x + dx, p.party[0].y + dy)) {
            p.party[0].x += dx; p.party[0].y += dy;
        }
        /* The real game's refresh_vision() does BOTH of these every turn.
           Lighting only the player left hires standing on a frontier that
           could never be revealed, so they thrashed against it forever --
           a harness bug that looked exactly like a game bug. */
        hero_vision_update(&m, &p, p.party[0].fov_radius);
        companions_take_turn(&p, &m);
        /* Signature is (Map*, Player*, bool*). Getting this backwards
           compiled without complaint under -w and quietly fed the monster
           loop garbage -- the third harness bug of the session, and the
           reason this file is built with the project's real warning flags. */
        process_monster_turns(&m, &p, &died);

        fprintf(f, "%d,player,%d,%d,%d,1\n", t, p.party[0].x, p.party[0].y, p.party[0].hp);
        for (int i = 0; i < MAX_COMPANIONS; i++) {
            const Hero *c = &p.party[(i) + 1];
            if (!c->in_use) continue;
            fprintf(f, "%d,hire%d,%d,%d,%d,%d\n", t, i, c->x, c->y, c->hp, c->alive ? 1 : 0);
        }
    }
    fclose(f);
    printf("wrote %s: %d ticks, %d hires\n", out, ticks, hires);
    return 0;
}
