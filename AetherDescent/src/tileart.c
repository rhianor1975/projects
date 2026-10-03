#include "tileart.h"
#include "common.h"

#include <string.h>

/* Sprites are 8 wide x 16 tall and written out as literal pictures. That is
   a lot of lines, but it is the only form in which "does this look like a
   fountain?" is answerable by reading the source -- and the whole point of
   the table is that adding a tile means adding a picture, not writing code.
   tests/tileart.c enforces the shape (16 rows, 8 columns, palette chars
   only) so a typo here fails the build rather than the frame. */

#define RGB(r_, g_, b_) { (unsigned char)(r_), (unsigned char)(g_), (unsigned char)(b_) }

static const ArtDef ART[ART_COUNT] = {

/* ---- terrain ------------------------------------------------------ */

[ART_WALL] = { "wall", 0x2588,
    RGB(148, 140, 128), RGB(52, 48, 44), RGB(186, 178, 164), true, {
    "oooooooo",
    "++o+++o+",
    "##o###o#",
    "--o---o-",
    "oooooooo",
    "o+++o+++",
    "o###o###",
    "o---o---",
    "oooooooo",
    "++o+++o+",
    "##o###o#",
    "--o---o-",
    "oooooooo",
    "o+++o+++",
    "o###o###",
    "o---o---",
}},

/* Floor carries a flagstone grid rather than a near-black field with a speck
   in it. In text mode a '.' on black is enough, but at pixel size an unlit
   floor makes a room read as a hole -- the grout lines are what tell you the
   room has a surface. */
[ART_FLOOR] = { "floor", 0x00B7,
    RGB(58, 55, 52), RGB(28, 27, 26), RGB(78, 74, 70), true, {
    "........",
    ".#######",
    ".###-###",
    ".#######",
    ".#######",
    ".##---##",
    ".#######",
    ".#######",
    "........",
    ".#######",
    ".#-#####",
    ".#######",
    ".####-##",
    ".#######",
    ".##---##",
    ".#######",
}},

/* Rubble/undergrowth: the same flagstone base so it reads as floor with
   something on it, not as a separate kind of ground. */
[ART_DECOR] = { "rubble", 0x25AA,
    RGB(104, 158, 86), RGB(28, 27, 26), RGB(52, 50, 47), true, {
    "........",
    ".::::::.",
    "...#....",
    "..###...",
    ".:#####:",
    "...###.:",
    "....#.#.",
    ".::::::.",
    "........",
    "..#...:.",
    ".###....",
    "..#..#..",
    ".::::###",
    "......#.",
    ".::::::.",
    "........",
}},

[ART_THICKET] = { "thicket", 0x2592,
    RGB(74, 142, 62), RGB(16, 38, 20), RGB(142, 196, 96), true, {
    "..#..#..",
    ".###.##.",
    "#####:##",
    ".:###::#",
    "#.:#:.#:",
    "##:##.##",
    ":#####:.",
    "#.:###:#",
    "##..#:##",
    ".####.#.",
    "#::###:#",
    "##.#:###",
    ".:####..",
    "#:.###:#",
    "###.:###",
    ".#:.#.:.",
}},
/* A question mark, and deliberately nothing more. Hearing reports a position,
   not an identity, so this must never look like any particular monster --
   the moment it does, the sense is a free reveal wearing a mark's clothing.
   `solid` is false so the biome tint leaves it alone: it is not part of the
   floor and must read the same in every biome. */
[ART_HEARD] = { "heard", '?',
    RGB(236, 208, 96), RGB(20, 18, 14), RGB(255, 236, 150), false, {
    "        ",
    "        ",
    "  ####  ",
    " ##  ## ",
    " ##  ## ",
    "     ## ",
    "    ##  ",
    "   ##   ",
    "   ##   ",
    "   ##   ",
    "        ",
    "        ",
    "   ##   ",
    "   ##   ",
    "        ",
    "        ",
}},


[ART_WATER] = { "water", 0x2248,
    RGB(108, 168, 232), RGB(20, 42, 78), RGB(180, 220, 255), true, {
    "..##....",
    ".#..#..#",
    "#....##.",
    "........",
    "..--....",
    ".-..-..-",
    "-....--.",
    "........",
    "..++....",
    ".+..+..+",
    "+....++.",
    "........",
    "..--....",
    ".-..-..-",
    "-....--.",
    "........",
}},

[ART_BRIDGE] = { "bridge", 0x2550,
    RGB(150, 110, 68), RGB(20, 42, 78), RGB(190, 150, 100), true, {
    "########",
    "#+####+#",
    "########",
    "oooooooo",
    "########",
    "#+####+#",
    "########",
    "oooooooo",
    "########",
    "#+####+#",
    "########",
    "oooooooo",
    "########",
    "#+####+#",
    "########",
    "oooooooo",
}},

[ART_LAVA] = { "lava", 0x2593,
    RGB(232, 108, 32), RGB(78, 18, 10), RGB(255, 226, 120), true, {
    "#-######",
    "##*#-###",
    "#**#####",
    "-#*###**",
    "####-###",
    "#-###**#",
    "###-####",
    "##**####",
    "#*##*#-#",
    "####-###",
    "#-##**##",
    "###**###",
    "##-#####",
    "#**###-#",
    "####-###",
    "#-###*##",
}},

[ART_MIASMA] = { "miasma", 0x2591,
    RGB(122, 210, 112), RGB(22, 44, 24), RGB(170, 240, 160), true, {
    "..--....",
    ".-##-...",
    "..-#--..",
    "...--#-.",
    "..-..--.",
    ".--...-.",
    "-##-....",
    "-#--....",
    "..--.-..",
    "...-##-.",
    "..--#--.",
    ".-#--...",
    "--..-...",
    ".-...--.",
    "..-.-##-",
    "....-#--",
}},

[ART_STAIRS_DOWN] = { "stairs down", 0x25BC,
    RGB(226, 186, 70), RGB(26, 24, 20), RGB(255, 232, 150), true, {
    "oooooooo",
    "+++++++o",
    "#######o",
    "-------o",
    "o++++++o",
    "o######o",
    "o------o",
    "oo+++++o",
    "oo#####o",
    "oo-----o",
    "ooo++++o",
    "ooo####o",
    "ooo----o",
    "oooo+++o",
    "oooo###o",
    "oooooooo",
}},

[ART_STAIRS_UP] = { "stairs up", 0x25B2,
    RGB(226, 186, 70), RGB(26, 24, 20), RGB(255, 232, 150), true, {
    "oooooooo",
    "o+++oooo",
    "o###oooo",
    "o---oooo",
    "o++++ooo",
    "o####ooo",
    "o----ooo",
    "o+++++oo",
    "o#####oo",
    "o-----oo",
    "o++++++o",
    "o######o",
    "o------o",
    "o+++++++",
    "o#######",
    "oooooooo",
}},

[ART_PORTAL] = { "portal", 0x25C9,
    RGB(206, 118, 228), RGB(28, 18, 42), RGB(255, 216, 255), true, {
    "..o##o..",
    ".o#++#o.",
    "o#+**+#o",
    "o#*::*#o",
    "#+*::*+#",
    "#+*::*+#",
    "#+*::*+#",
    "#+*::*+#",
    "#+*::*+#",
    "#+*::*+#",
    "o#*::*#o",
    "o#+**+#o",
    ".o#++#o.",
    "..o##o..",
    "...oo...",
    "........",
}},

[ART_LOCKED_DOOR] = { "locked door", 0x256C,
    RGB(198, 158, 68), RGB(38, 28, 16), RGB(248, 224, 128), true, {
    "oooooooo",
    "o######o",
    "o#+##+#o",
    "o######o",
    "o##oo##o",
    "o#o**o#o",
    "o#o**o#o",
    "o##oo##o",
    "o######o",
    "o#+##+#o",
    "o######o",
    "o######o",
    "o######o",
    "o#+##+#o",
    "o######o",
    "oooooooo",
}},

[ART_SEALED_DOOR] = { "sealed door", 0x2593,
    RGB(150, 120, 152), RGB(32, 26, 36), RGB(196, 168, 200), true, {
    "oooooooo",
    "o##--##o",
    "o##--##o",
    "oooooooo",
    "o##--##o",
    "o##--##o",
    "oooooooo",
    "o##--##o",
    "o##--##o",
    "oooooooo",
    "o##--##o",
    "o##--##o",
    "oooooooo",
    "o##--##o",
    "o##--##o",
    "oooooooo",
}},

[ART_LEVER] = { "lever", 0x21A5,
    RGB(108, 198, 108), RGB(26, 26, 26), RGB(232, 232, 120), true, {
    "........",
    "......*.",
    ".....**.",
    "....**..",
    "....*...",
    "...*....",
    "...*....",
    "..*.....",
    "..*.....",
    "..*.....",
    "........",
    ".oooooo.",
    ".o####o.",
    ".o#--#o.",
    ".o####o.",
    ".oooooo.",
}},

[ART_QUEST_BOARD] = { "quest board", 0x25A4,
    RGB(146, 96, 56), RGB(33, 32, 30), RGB(244, 238, 214), true, {
    "oooooooo",
    "o######o",
    "o#*::*#o",
    "o#*::*#o",
    "o#*::*#o",
    "o##--##o",
    "o#**#*#o",
    "o#::#:#o",
    "o#::#:#o",
    "o##--##o",
    "o######o",
    "oooooooo",
    "...##...",
    "...##...",
    "...##...",
    "...--...",
}},

[ART_TEMPLE_ENTRANCE] = { "temple", 0x2302,
    RGB(238, 208, 118), RGB(34, 28, 18), RGB(255, 244, 190), true, {
    "...oo...",
    "..o##o..",
    ".o#++#o.",
    "o#+##+#o",
    "o#+##+#o",
    "o#+##+#o",
    "o#+##+#o",
    "o#+##+#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "oooooooo",
}},

[ART_SHOP_GENERAL] = { "general store", 0x25A5,
    RGB(206, 118, 216), RGB(34, 26, 38), RGB(250, 200, 255), true, {
    "oooooooo",
    "o******o",
    "o:*:*:*o",
    "o******o",
    "oooooooo",
    "o######o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o######o",
    "oooooooo",
}},

[ART_SHOP_ARMORY] = { "armory", 0x25A8,
    RGB(198, 198, 214), RGB(34, 32, 38), RGB(248, 248, 255), true, {
    "oooooooo",
    "o******o",
    "o::**::o",
    "o******o",
    "oooooooo",
    "o######o",
    "o#o**o#o",
    "o#o**o#o",
    "o#o**o#o",
    "o#*::*#o",
    "o#o**o#o",
    "o#o**o#o",
    "o#o**o#o",
    "o#o..o#o",
    "o######o",
    "oooooooo",
}},

[ART_SHOP_APOTHECARY] = { "apothecary", 0x25A7,
    RGB(108, 214, 196), RGB(24, 40, 38), RGB(200, 255, 244), true, {
    "oooooooo",
    "o******o",
    "o*::::*o",
    "o******o",
    "oooooooo",
    "o######o",
    "o#o..o#o",
    "o#o**o#o",
    "o#o**o#o",
    "o#*::*#o",
    "o#*::*#o",
    "o#*::*#o",
    "o#o**o#o",
    "o#o..o#o",
    "o######o",
    "oooooooo",
}},

[ART_SHOP_ARCANIST] = { "arcanist's guild", 0x25A9,
    RGB(118, 148, 238), RGB(22, 28, 52), RGB(200, 216, 255), true, {
    "oooooooo",
    "o******o",
    "o:*::*:o",
    "o******o",
    "oooooooo",
    "o######o",
    "o#o**o#o",
    "o#*::*#o",
    "o#o**o#o",
    "o#o.*o#o",
    "o#o.*o#o",
    "o#o.*o#o",
    "o#o.*o#o",
    "o#o..o#o",
    "o######o",
    "oooooooo",
}},

[ART_INN] = { "inn", 0x2302,
    RGB(226, 176, 96), RGB(38, 30, 22), RGB(255, 236, 170), true, {
    "...oo...",
    "..o##o..",
    ".o#++#o.",
    "o#+##+#o",
    "oooooooo",
    "o######o",
    "o#*::*#o",
    "o#*::*#o",
    "o##--##o",
    "o######o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "oooooooo",
}},

[ART_TAVERN] = { "tavern", 0x2302,
    RGB(214, 150, 78), RGB(40, 28, 18), RGB(255, 216, 140), true, {
    "...oo...",
    "..o##o..",
    ".o#++#o.",
    "o#+##+#o",
    "oooooooo",
    "o######o",
    "o#*##*#o",
    "o#::::#o",
    "o#*##*#o",
    "o######o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "o#o..o#o",
    "oooooooo",
}},

/* ---- map features -------------------------------------------------- */

[ART_SHRINE] = { "shrine", 0x25C7,
    RGB(128, 218, 224), RGB(24, 24, 24), RGB(210, 255, 255), false, {
    "        ",
    "   **   ",
    "  *::*  ",
    "  *::*  ",
    "   **   ",
    "        ",
    "  oooo  ",
    "  o##o  ",
    "  o##o  ",
    "  o##o  ",
    " oo##oo ",
    " o####o ",
    "o######o",
    "o#+##+#o",
    "o######o",
    "oooooooo",
}},

[ART_FOUNTAIN] = { "fountain", 0x25CB,
    RGB(118, 178, 238), RGB(24, 24, 24), RGB(214, 240, 255), false, {
    "        ",
    "   *    ",
    "  * *   ",
    "  * *   ",
    "   *    ",
    "  ***   ",
    " oooooo ",
    "o######o",
    "o#::::#o",
    "o#::::#o",
    "o#::::#o",
    "o######o",
    " o####o ",
    "  oooo  ",
    "        ",
    "        ",
}},

[ART_MERCHANT] = { "merchant", 0x25CF,
    RGB(234, 188, 88), RGB(24, 24, 24), RGB(255, 230, 160), false, {
    "        ",
    "   oo   ",
    "  o##o  ",
    "  o##o  ",
    "   oo   ",
    "  ####  ",
    " #####* ",
    "#######*",
    " #####* ",
    "  ####  ",
    "  #..#  ",
    "  #..#  ",
    "  #..#  ",
    "  #..#  ",
    " o#..#o ",
    "        ",
}},

[ART_MACHINE] = { "machine", 0x25A6,
    RGB(198, 198, 204), RGB(24, 24, 24), RGB(120, 220, 255), false, {
    "oooooooo",
    "o######o",
    "o#-**-#o",
    "o#-**-#o",
    "o######o",
    "o#o..o#o",
    "o#o..o#o",
    "o######o",
    "o#-##-#o",
    "o######o",
    "o##--##o",
    "o######o",
    "o#-**-#o",
    "o######o",
    "oooooooo",
    "..o..o..",
}},

[ART_RELIC] = { "relic", 0x25C6,
    RGB(244, 204, 88), RGB(24, 24, 24), RGB(255, 250, 205), false, {
    "        ",
    "   oo   ",
    "  o**o  ",
    " o*::*o ",
    "o*::::*o",
    "o*::::*o",
    " o*::*o ",
    "  o**o  ",
    "   oo   ",
    "        ",
    "  #  #  ",
    "   ##   ",
    "        ",
    "        ",
    "        ",
    "        ",
}},

[ART_TOWN_GATE] = { "waygate", 0x2229,
    RGB(108, 218, 218), RGB(24, 24, 24), RGB(206, 255, 255), false, {
    "oooooooo",
    "o######o",
    "o#+##+#o",
    "o######o",
    "oo####oo",
    " o****o ",
    " o*::*o ",
    " o*::*o ",
    " o*::*o ",
    " o*::*o ",
    " o*::*o ",
    " o*::*o ",
    " o*::*o ",
    " o*::*o ",
    " oooooo ",
    "        ",
}},

/* ---- floor items --------------------------------------------------- */

[ART_GOLD] = { "gold", '$',
    RGB(244, 198, 68), RGB(24, 24, 24), RGB(255, 246, 180), false, {
    "        ",
    "        ",
    "        ",
    "        ",
    "  oooo  ",
    " o++++o ",
    "o+#**#+o",
    " o+##+o ",
    "  oooo  ",
    " oooooo ",
    "o++++++o",
    "o+#**#+o",
    "o+####+o",
    "o++++++o",
    " oooooo ",
    "        ",
}},

[ART_KEY] = { "key", '/',
    RGB(234, 204, 108), RGB(24, 24, 24), RGB(255, 240, 190), false, {
    "        ",
    "        ",
    "   oo   ",
    "  o##o  ",
    " o#++#o ",
    " o#..#o ",
    " o#++#o ",
    "  o##o  ",
    "   ##   ",
    "   ##   ",
    "   ##   ",
    "   ###  ",
    "   ##   ",
    "   ###  ",
    "   oo   ",
    "        ",
}},

[ART_POTION] = { "potion", '!',
    RGB(118, 212, 224), RGB(24, 24, 24), RGB(226, 255, 255), false, {
    "        ",
    "   oo   ",
    "   ##   ",
    "   ##   ",
    "  o##o  ",
    "  o##o  ",
    " o#**#o ",
    " o****o ",
    "o**::**o",
    "o*::::*o",
    "o*::::*o",
    "o**::**o",
    " o****o ",
    "  oooo  ",
    "        ",
    "        ",
}},

/* ---- actors -------------------------------------------------------- */

[ART_PLAYER] = { "player", '@',
    RGB(118, 232, 232), RGB(24, 24, 24), RGB(255, 255, 255), false, {
    "        ",
    "   oo   ",
    "  o##o  ",
    "  o**o  ",
    "  o##o  ",
    "   oo   ",
    "  ####  ",
    " o####o ",
    "#o####o#",
    "#o####o#",
    " o####o ",
    "  o##o  ",
    "  #  #  ",
    "  #  #  ",
    " o#  #o ",
    "        ",
}},

[ART_MON_VERMIN] = { "vermin", 'r',
    RGB(128, 198, 88), RGB(24, 24, 24), RGB(226, 92, 92), false, {
    "        ",
    "        ",
    "        ",
    "        ",
    "  oooo  ",
    " o####o ",
    "o#*##*#o",
    "o######o",
    "o######o",
    " o####o ",
    "# o##o #",
    "## oo ##",
    "#  ##  #",
    "   ##   ",
    "        ",
    "        ",
}},

[ART_MON_CLOCKWORK] = { "clockwork", 'c',
    RGB(224, 188, 78), RGB(24, 24, 24), RGB(255, 122, 60), false, {
    "        ",
    "  o##o  ",
    "  #**#  ",
    "  o##o  ",
    "   ##   ",
    " oo##oo ",
    "o######o",
    "o#-##-#o",
    "o######o",
    "o######o",
    " oo##oo ",
    "   ##   ",
    "  o##o  ",
    "  #  #  ",
    " o#  #o ",
    "        ",
}},

[ART_MON_RUINS] = { "ruin-walker", 'w',
    RGB(204, 122, 224), RGB(24, 24, 24), RGB(255, 226, 255), false, {
    "   oo   ",
    "  o##o  ",
    " o#**#o ",
    " o####o ",
    "  o##o  ",
    "  ####  ",
    " o####o ",
    "o######o",
    "o######o",
    "o######o",
    " o####o ",
    " o-##-o ",
    "  o--o  ",
    "  .--.  ",
    "   ..   ",
    "        ",
}},

[ART_MON_OUTRIDER] = { "outrider", 'o',
    RGB(108, 154, 238), RGB(24, 24, 24), RGB(226, 240, 255), false, {
    "        ",
    "   oo   ",
    "  o##o  ",
    "  #**#  ",
    "  o##o  ",
    " o####o ",
    "o##--##o",
    "o##--##o",
    "o######o",
    " o####o ",
    " o#  #o ",
    "  #  #  ",
    "  #  #  ",
    "  #  #  ",
    " o#  #o ",
    "        ",
}},

[ART_MON_ABYSSAL] = { "abyssal", 'a',
    RGB(228, 88, 78), RGB(24, 24, 24), RGB(255, 216, 120), false, {
    "        ",
    "  oooo  ",
    " o####o ",
    "o##**##o",
    "o#*::*#o",
    "o##**##o",
    "o######o",
    "o#*##*#o",
    "o######o",
    " o####o ",
    "o-o##o-o",
    "-  ##  -",
    "   --   ",
    "  -  -  ",
    "        ",
    "        ",
}},

[ART_MON_BOSS] = { "boss", 'B',
    RGB(255, 68, 58), RGB(24, 24, 24), RGB(255, 220, 120), false, {
    "o      o",
    "#o    o#",
    "#*o  o*#",
    " o####o ",
    "o##**##o",
    "o#*::*#o",
    "o##**##o",
    "o######o",
    "#o####o#",
    "#o####o#",
    "#o####o#",
    " o####o ",
    " o#  #o ",
    " o#  #o ",
    "o##  ##o",
    "        ",
}},

[ART_UNKNOWN] = { "unknown", '?',
    RGB(230, 60, 230), RGB(48, 0, 48), RGB(255, 255, 0), true, {
    "oooooooo",
    "o.####.o",
    "o##**##o",
    "o#*..*#o",
    "o#*..*#o",
    "o...**#o",
    "o...**.o",
    "o..**..o",
    "o.**...o",
    "o.**...o",
    "o......o",
    "o..**..o",
    "o..**..o",
    "o......o",
    "o......o",
    "oooooooo",
}},

};

const ArtDef *art_def(ArtId id) {
    int i = (int)id;
    if (i < 0 || i >= ART_COUNT) return &ART[ART_UNKNOWN];
    /* A hole in the designated-initialiser table would show up as a NULL
       name; report it as unknown rather than dereferencing empty rows. */
    if (!ART[i].name) return &ART[ART_UNKNOWN];
    return &ART[i];
}

ArtId art_for_tile(int tile_type) {
    switch (tile_type) {
        case TILE_WALL:            return ART_WALL;
        case TILE_FLOOR:           return ART_FLOOR;
        case TILE_DECOR:           return ART_DECOR;
        case TILE_THICKET:         return ART_THICKET;
        case TILE_WATER:           return ART_WATER;
        case TILE_BRIDGE:          return ART_BRIDGE;
        case TILE_LAVA:            return ART_LAVA;
        case TILE_MIASMA:          return ART_MIASMA;
        case TILE_STAIRS_DOWN:     return ART_STAIRS_DOWN;
        case TILE_STAIRS_UP:       return ART_STAIRS_UP;
        case TILE_PORTAL:          return ART_PORTAL;
        case TILE_LOCKED_DOOR:     return ART_LOCKED_DOOR;
        case TILE_SEALED_DOOR:     return ART_SEALED_DOOR;
        case TILE_LEVER:           return ART_LEVER;
        case TILE_QUEST_BOARD:     return ART_QUEST_BOARD;
        case TILE_TEMPLE_ENTRANCE: return ART_TEMPLE_ENTRANCE;
        case TILE_SHOP_GENERAL:    return ART_SHOP_GENERAL;
        case TILE_SHOP_ARMORY:     return ART_SHOP_ARMORY;
        case TILE_SHOP_APOTHECARY: return ART_SHOP_APOTHECARY;
        case TILE_SHOP_ARCANIST:   return ART_SHOP_ARCANIST;
        case TILE_INN:             return ART_INN;
        case TILE_TAVERN:          return ART_TAVERN;
        case TILE_JUNKYARD:        return ART_MACHINE;
        case TILE_GLADIATOR:       return ART_TAVERN;
        case TILE_BANK:            return ART_MACHINE;
        case TILE_ORACLE:          return ART_TAVERN;
        case TILE_ALTAR:           return ART_TAVERN;
        case TILE_BAZAAR:          return ART_TAVERN;
        case TILE_CRYSTAL:         return ART_RELIC;
        case TILE_ROD:             return ART_MACHINE;
        case TILE_VENT:            return ART_MACHINE;
        case TILE_CURRENT:         return ART_WATER;
        case TILE_BLOODPOOL:       return ART_WATER;
        case TILE_PRISM_RED:
        case TILE_PRISM_BLUE:
        case TILE_PRISM_GREEN:     return ART_DECOR;
        case TILE_BELT:            return ART_BRIDGE;
        case TILE_SNARE:           return ART_DECOR;
        case TILE_PIT:             return ART_DECOR;
        case TILE_ORE:             return ART_RELIC;
        case TILE_RACES:           return ART_TAVERN;
        case TILE_KITCHEN:         return ART_TAVERN;
        case TILE_BLACKMARKET:     return ART_MACHINE;
        default:                   return ART_UNKNOWN;
    }
}

ArtId art_for_feature(int feature_type) {
    switch (feature_type) {
        case FEATURE_SHRINE:    return ART_SHRINE;
        case FEATURE_FOUNTAIN:  return ART_FOUNTAIN;
        case FEATURE_MERCHANT:  return ART_MERCHANT;
        case FEATURE_MACHINE:   return ART_MACHINE;
        case FEATURE_RELIC:     return ART_RELIC;
        case FEATURE_TOWN_GATE: return ART_TOWN_GATE;
        case FEATURE_TOLL:      return ART_MERCHANT;
        case FEATURE_CONSOLE:   return ART_MACHINE;
        case FEATURE_STRONGBOX: return ART_RELIC;
        default:                return ART_UNKNOWN;
    }
}

/* Monsters carry a colour pair rather than a species tag, and that pair is
   already assigned by faction in monsters.c -- so it is the faction key,
   without needing a second field on Monster that could drift out of step. */
ArtId art_for_monster(int color_pair, bool is_boss) {
    if (is_boss) return ART_MON_BOSS;
    switch (color_pair) {
        case CP_MON_VERMIN:    return ART_MON_VERMIN;
        case CP_MON_CLOCKWORK: return ART_MON_CLOCKWORK;
        case CP_MON_RUINS:     return ART_MON_RUINS;
        case CP_MON_OUTRIDER:  return ART_MON_OUTRIDER;
        case CP_MON_ABYSSAL:   return ART_MON_ABYSSAL;
        case CP_MON_BOSS:      return ART_MON_BOSS;
        default:               return ART_MON_VERMIN;
    }
}

/* Remembered-but-not-visible tiles. One factor, applied identically by the
   block and tile renderers, so the two modes agree about what "out of sight"
   looks like. */
#define ART_DIM_NUM 42
#define ART_DIM_DEN 100

static unsigned char scale8(unsigned char v, int num, int den) {
    int r = (int)v * num / den;
    return (unsigned char)(r > 255 ? 255 : r);
}

/* ---- biome tinting ----------------------------------------------------
   Per-channel colour direction for each biome, in percent, indexed by Biome.
   100 is neutral; above lifts a channel, below pulls it down.

   These were "deliberately gentle" and the gentleness was the bug. Measured
   across the whole rendered scene, wastes and city landed 3.8 apart out of
   255 and the *widest* pair in the game was 28.9 -- reported from play as
   "on the tileset all biomes look the same". Two things were wrong.

   The rows themselves were near-neutral: every channel sat within a quarter
   of 100, so the biggest hue any of them could ask for was a faint wash.

   And the shift was purely multiplicative, which is the wrong operator for
   this art. Dungeon terrain is dark by design -- floor's foreground is
   RGB(58,55,52) -- and multiplying a channel at 55 by even 130% moves it
   sixteen levels. The tint was strongest exactly where the art was already
   bright enough not to need it, and vanished in the dark where the floor is.

   So: real hue separation in the rows, and an additive term alongside the
   multiply so that dark terrain shifts by an absolute amount rather than a
   proportional one. BIOME_LIFT is what carries the dark end. */
typedef struct { int r, g, b; } BiomeTint;
static const BiomeTint BIOME_TINT[] = {
    { 68, 132,  74 },   /* BIOME_JUNGLE     -- wet green, everything overgrown */
    { 140, 100,  60 },  /* BIOME_INDUSTRIAL -- brass, soot and furnace light */
    { 76,  96, 142 },   /* BIOME_RUINS      -- cold stone, old and blue-grey */
    { 146, 132,  74 },  /* BIOME_WASTES     -- bleached, salt-dried, sunstruck */
    { 98,  66, 148 },   /* BIOME_ABYSS      -- violet dark, lit by nothing good */
    /* ART_TINT_CITY -- the surface, and the only place in the game with a sky.
       Warm and a shade brighter than anything below it: every dungeon tint
       above pulls at least one channel down, so daylight is the one that pulls
       two of them up. The plaza had no tint at all before, which left the one
       place a run begins and ends looking like the untinted default -- flat
       warm grey, buildings barely separable from the ground. Reported as "the
       city on mode 2 is a bit meh", which it was. */
    { 138, 122,  94 },  /* ART_TINT_CITY    -- sunlit stone and dust */
};
#define BIOME_TINT_N ((int)(sizeof(BIOME_TINT) / sizeof(BIOME_TINT[0])))

/* -1 means "a caller that has not said": no tint either way. The surface has
   its own entry now -- see ART_TINT_CITY -- because "no tint" and "daylight"
   are different answers and town was getting the first by default. */
static int g_biome = -1;

void art_set_biome(int biome) {
    g_biome = (biome >= 0 && biome < BIOME_TINT_N) ? biome : -1;
}

int art_get_biome(void) { return g_biome; }

/* How much of the tint arrives as a flat offset rather than a multiplier.
   This is the part that survives into the dark end of the palette. 40 was
   picked by rendering all six scenes and measuring how far apart they land:
   below about 30 the closest pair stays under ten levels and still reads as
   one place, and above about 55 unlit floor starts to glow. */
#define BIOME_LIFT 40

static unsigned char shift_channel(unsigned char v, int pct) {
    int r = (int)v * pct / 100 + (pct - 100) * BIOME_LIFT / 100;
    if (r < 0) r = 0;
    return (unsigned char)(r > 255 ? 255 : r);
}

static TuiRGB biome_shift(TuiRGB c) {
    if (g_biome < 0) return c;
    const BiomeTint *t = &BIOME_TINT[g_biome];
    TuiRGB out;
    out.r = shift_channel(c.r, t->r);
    out.g = shift_channel(c.g, t->g);
    out.b = shift_channel(c.b, t->b);
    return out;
}

static TuiRGB tint(TuiRGB c, int num, int den, bool dim) {
    TuiRGB out;
    out.r = scale8(c.r, num, den);
    out.g = scale8(c.g, num, den);
    out.b = scale8(c.b, num, den);
    if (dim) {
        out.r = scale8(out.r, ART_DIM_NUM, ART_DIM_DEN);
        out.g = scale8(out.g, ART_DIM_NUM, ART_DIM_DEN);
        out.b = scale8(out.b, ART_DIM_NUM, ART_DIM_DEN);
    }
    return out;
}

void art_block_colors(ArtId id, bool dim, TuiRGB *fg, TuiRGB *bg) {
    const ArtDef *d = art_def(id);
    if (fg) *fg = tint(d->solid ? biome_shift(d->fg) : d->fg, 1, 1, dim);
    if (bg) *bg = tint(d->solid ? biome_shift(d->bg) : d->bg, 1, 1, dim);
}

/* Resolve one palette character to a colour. Returns false for transparent,
   which the caller must leave untouched. */
static bool palette_rgb(const ArtDef *d, char c, bool dim,
                        const TuiRGB *fg_override, TuiRGB *out) {
    /* Terrain takes the biome's colour; anything standing on it does not. */
    TuiRGB fg = d->solid ? biome_shift(d->fg) : d->fg;
    /* ...and a body takes the colour the *game* gave it. The party are all one
       sprite and told apart by colour alone, so a sprite-coloured '@' makes
       five hires identical -- which is what tile and block mode did while text
       mode gave each of them their own. Only the figure is overridden; the
       accent is left, so whatever the sprite uses '*' for still reads. */
    if (fg_override) fg = *fg_override;
    TuiRGB bg = d->solid ? biome_shift(d->bg) : d->bg;
    TuiRGB ac = d->solid ? biome_shift(d->accent) : d->accent;

    switch (c) {
        case ' ': return false;
        case '.': *out = tint(bg,   1,   1, dim); return true;
        case 'o': *out = tint(fg,  25, 100, dim); return true;
        case '-': *out = tint(fg,  55, 100, dim); return true;
        case '#': *out = tint(fg,   1,   1, dim); return true;
        case '+': *out = tint(fg, 145, 100, dim); return true;
        case '*': *out = tint(ac,   1,   1, dim); return true;
        case ':': *out = tint(ac,  55, 100, dim); return true;
        default:  *out = tint(fg,   1,   1, dim); return true;
    }
}

void art_sprite_rgba(ArtId id, bool dim, bool opaque_bg,
                     unsigned char *rgba, int w, int h, int stride) {
    art_sprite_rgba_tinted(id, dim, opaque_bg, NULL, rgba, w, h, stride);
}

void art_sprite_rgba_tinted(ArtId id, bool dim, bool opaque_bg,
                            const TuiRGB *fg_override,
                            unsigned char *rgba, int w, int h, int stride) {
    const ArtDef *d = art_def(id);
    if (!rgba || w <= 0 || h <= 0) return;

    for (int py = 0; py < h; py++) {
        /* Nearest neighbour. Sprites are authored at the text-cell aspect
           (8x16), so on a typical cell this is close to 1:1 and never
           stretches enough to blur the shapes. */
        int sy = py * ART_SPRITE_H / h;
        const char *row = d->sprite[sy];
        /* A short or missing row is a table typo, caught by
           tests/tileart.c. Treat it as transparent here so a bad edit can
           never read past the literal at runtime. */
        int rowlen = row ? (int)strlen(row) : 0;
        unsigned char *dst = rgba + (size_t)py * (size_t)stride;

        for (int px = 0; px < w; px++) {
            int sx = px * ART_SPRITE_W / w;
            TuiRGB c;
            bool solid = palette_rgb(d, sx < rowlen ? row[sx] : ' ', dim, fg_override, &c);
            unsigned char *p = dst + (size_t)px * 4;
            if (!solid) {
                if (!opaque_bg) continue;      /* leave the terrain showing */
                c = tint(d->solid ? biome_shift(d->bg) : d->bg, 1, 1, dim);
            }
            p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
        }
    }
}
