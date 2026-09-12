#include <stddef.h>
#include "djarhun.h"

/* The board, transcribed from _board_main.jpg and _board_urthe.jpg in
 * Djarhun_Renegade_Version.zip.  It is four concentric rings with the
 * Abyss at the middle, plus Urthe on its own sheet:
 *
 *      Frostburn   the frozen outer edge
 *      Tar'ri      the ocean; you need a ship to cross it
 *      Durach      the heartland, where Elidor and the Gypsy Camp are
 *      Aldun       the desert
 *      The Abyss   a one-way spiral from the Lake of Tears to Gharad
 *
 * Space rules come off the board itself where they are legible; the ones
 * that just say "Draw N Cards" are exact.
 */

/* ------------------------------------------------------------ Frostburn --
 * 38 spaces.  "This frozen land is on the far reaches of Djarhun." */
static const Space frost_ring[] = {
    {"Cylmar Strg", SP_MARKET, 0, -1, -1},
    {"Woods Mudon", SP_DRAW,   1, -1, -1},
    {"Aldaren Rns", SP_DRAW,   1, -1, -1},
    {"Chilltall",   SP_DRAW,   1, -1, -1},
    {"Narisa Wds",  SP_DRAW,   1, -1, -1},
    {"Demonblood",  SP_DRAW,   1, -1, -1},
    {"Wds Dukran",  SP_DRAW,   1, -1, -1},
    {"Snowdrop Mt", SP_DRAW,   1, -1, -1},
    {"Ugmar Alchm", SP_MARKET, 0, -1, -1},
    {"The Jun Wds", SP_DRAW,   1, -1, -1},
    {"Sidara Frts", SP_DRAW,   1, -1, -1},
    {"Icewind Lake",SP_DRAW,   1, -1, -1},
    {"Icewyrm Mts", SP_DRAW,   1, -1, -1},
    {"Owlbear Frs", SP_DRAW,   1, -1, -1},
    {"Seryth Tund", SP_DRAW,   1, -1, -1},
    {"Kimlar Wds",  SP_DRAW,   1, -1, -1},
    {"Technomancr", SP_MARKET, 0, -1, -1},
    {"Frostpeak",   SP_DRAW,   1, -1, -1},
    {"Lake Marfan", SP_DRAW,   1, -1, -1},
    {"Castle Ice",  SP_DRAW,   1, -1, -1},
    {"Crystal Wd",  SP_DRAW,   1, -1, -1},
    {"Orc Camp",    SP_DRAW,   2, -1, -1},
    {"Lake Mirror", SP_DRAW,   1, -1, -1},
    {"Wiskchill",   SP_DRAW,   1, -1, -1},
    {"Jarin Crypt", SP_DRAW,   1, -1, -1},
    {"Icemorn Wds", SP_DRAW,   1, -1, -1},
    {"Hailstorm",   SP_DRAW,   1, -1, -1},
    {"Dungn Harak", SP_DRAW,   2, -1, -1},
    {"Woods Kila",  SP_DRAW,   1, -1, -1},
    /* "To travel between Frostburn and Durach, you need to visit the Mage
     * in Springvale or the Sorcerer in Glacial Hills." */
    {"Glacial Hlls",SP_CROSSING, 0, LAND_DURACH, 0},
    {"Snowdrift",   SP_DRAW,   1, -1, -1},
    {"Frost Giant", SP_DRAW,   1, -1, -1},
    {"Pirog Tundra",SP_DRAW,   1, -1, -1},
    {"Diamondglde", SP_DRAW,   1, -1, -1},
    {"Shrine Wizd", SP_DRAW,   1, -1, -1},
    {"Glitterfall", SP_DRAW,   1, -1, -1},
    {"Cylmar Lake", SP_DRAW,   1, -1, -1},
    {"Stones Peril",SP_DRAW,   1, -1, -1},
};

/* ----------------------------------------------------------- Tar'ri Sea --
 * 40 spaces.  "To travel the high seas, you need a ship... you may only
 * leave your boat if you dock it at Elidor or Glacial Hills." */
static const Space tarri_ring[] = {
    {"Stones Peril",SP_DRAW,  1, -1, -1},
    {"Orius Waters",SP_DRAW,  1, -1, -1},
    {"Str Gartuga", SP_DRAW,  1, -1, -1},
    {"Siren Rocks", SP_DRAW,  1, -1, -1},
    {"Southwind",   SP_DRAW,  2, -1, -1},
    {"Sharkbite",   SP_DRAW,  1, -1, -1},
    {"Pirate Cove", SP_DRAW,  1, -1, -1},
    {"Waves Jupeti",SP_DRAW,  1, -1, -1},
    {"Dravburn Td", SP_DRAW,  1, -1, -1},
    {"Anchorcatch", SP_DRAW,  1, -1, -1},
    {"Buccaneers",  SP_DRAW,  1, -1, -1},
    {"Lake Marfan", SP_DRAW,  1, -1, -1},
    {"Eelbraid Cr", SP_DRAW,  1, -1, -1},
    {"Serpentine",  SP_DRAW,  1, -1, -1},
    {"Waters Vorn", SP_DRAW,  1, -1, -1},
    {"Deep Tar'ri", SP_DRAW,  1, -1, -1},
    {"Pegleg Rocks",SP_DRAW,  1, -1, -1},
    {"Swifttide",   SP_DRAW,  1, -1, -1},
    {"Waves Elidor",SP_CROSSING, 0, LAND_DURACH, 9},
    {"Lighthouse",  SP_DRAW,  1, -1, -1},
    {"Hullbreak",   SP_DRAW,  1, -1, -1},
    {"Str Rapture", SP_DRAW,  1, -1, -1},
    {"Highwall Wv", SP_DRAW,  1, -1, -1},
    {"Mermaid Rks", SP_DRAW,  1, -1, -1},
    {"Plankwalk",   SP_DRAW,  1, -1, -1},
    {"Shipwreck",   SP_DRAW,  1, -1, -1},
    {"Calm Tar'ri", SP_DRAW,  1, -1, -1},
    {"Northwind",   SP_DRAW,  1, -1, -1},
    {"Blowhole Wt", SP_DRAW,  1, -1, -1},
    {"Whirlpool",   SP_DRAW,  1, -1, -1},
    {"Ackle Lake",  SP_DRAW,  2, -1, -1},
    {"Wetdeck Cur", SP_DRAW,  1, -1, -1},
    {"Traitor Str", SP_DRAW,  1, -1, -1},
    {"Tornsail Wv", SP_DRAW,  1, -1, -1},
    {"Westwind",    SP_DRAW,  2, -1, -1},
    {"Plunderers",  SP_DRAW,  1, -1, -1},
    {"Tides Redbrd",SP_DRAW,  1, -1, -1},
    {"Gartugas Clm",SP_DRAW,  1, -1, -1},
    {"Kraken Str",  SP_DRAW,  1, -1, -1},
    {"Seaweed Wtrs",SP_DRAW,  1, -1, -1},
};

/* -------------------------------------------------------------- Durach --
 * 32 spaces, the heartland.  Elidor and the Gypsy Camp both sit here, so
 * whichever way a hero's Morality points, the Book comes back to Durach. */
static const Space durach_ring[] = {
    /* "you need to visit the Mage in Springvale" to reach Frostburn */
    {"Springvale",  SP_CROSSING, 0, LAND_FROST, 29},
    {"Kalar Prarie",SP_DRAW,  1, -1, -1},
    {"Raen Field",  SP_DRAW,  1, -1, -1},
    {"Dragus Swamp",SP_DRAW,  2, -1, -1},
    {"CentaurSprng",SP_DRAW,  0, -1, -1},
    {"Odimus Mts",  SP_DRAW,  1, -1, -1},
    {"Drake Hills", SP_DRAW,  1, -1, -1},
    {"Tabor Forest",SP_DRAW,  1, -1, -1},
    {"Gypsy Camp",  SP_GYPSY, 0, -1, -1},
    {"City Elidor", SP_ELIDOR,0, -1, -1},
    {"CermorMeadow",SP_DRAW,  1, -1, -1},
    {"Felladin Pl", SP_DRAW,  1, -1, -1},
    {"Wyvern Mts",  SP_DRAW,  1, -1, -1},
    /* "you can see the adjacent spaces between Durach and Urthe (Church of
     * Gedwin <> Gedwin Springs ... for examples)".  The rulebook leaves the
     * way INTO Urthe open -- "Travel to Urthe has many possible forms" --
     * and names no card that does it, so this is a house rule built on the
     * one pairing the rulebook does name.  Without it Urthe is 32 spaces
     * and 126 cards that only a Hero who began there will ever see. */
    {"Chrch Gedwin",SP_CROSSING, 0, LAND_URTHE, 4},
    {"OgrespitSwmp",SP_DRAW,  2, -1, -1},
    {"Sylvan Frst", SP_DRAW,  1, -1, -1},
    {"Odar Plains", SP_DRAW,  1, -1, -1},
    {"EnchantdGrve",SP_DRAW,  1, -1, -1},
    {"Upal Pasture",SP_DRAW,  1, -1, -1},
    {"RockwallMdw", SP_DRAW,  1, -1, -1},
    {"Firestorm",   SP_DRAW,  1, -1, -1},
    {"DariensCemt", SP_DRAW,  1, -1, -1},
    {"Valgar Mts",  SP_DRAW,  1, -1, -1},
    /* "To travel between Aldun and Durach, you need to travel between the
     * Wolfbane Hills and the Nesta Badlands." */
    {"WolfbaneHlls",SP_CROSSING, 0, LAND_ALDUN, 12},
    {"Truant Frst", SP_DRAW,  1, -1, -1},
    {"Fort Tymar",  SP_DRAW,  1, -1, -1},
    {"Witchcall",   SP_DRAW,  1, -1, -1},
    {"AncientObel", SP_DRAW,  1, -1, -1},
    {"Dragontear",  SP_DRAW,  2, -1, -1},
    {"Eastwind",    SP_DRAW,  1, -1, -1},
    {"NerimGrssld", SP_DRAW,  1, -1, -1},
    {"FieldsRathgr",SP_DRAW,  1, -1, -1},
};

/* --------------------------------------------------------------- Aldun --
 * 13 spaces.  "The treacherous desert land on the surface of Djarhun." */
static const Space aldun_ring[] = {
    {"City Aldun",  SP_MARKET, 0, -1, -1},
    {"Jahrec Dunes",SP_DRAW,  1, -1, -1},
    {"WateringSnds",SP_DRAW,  1, -1, -1},
    {"Taryn Sands", SP_DRAW,  1, -1, -1},
    {"Sharra Ruins",SP_DRAW,  0, -1, -1},
    {"Frigglant",   SP_DRAW,  1, -1, -1},
    {"Fort Tymar",  SP_DRAW,  1, -1, -1},
    {"OldacWastlnd",SP_DRAW,  1, -1, -1},
    {"Shady Sands", SP_DRAW,  1, -1, -1},
    {"Mirror Dunes",SP_DRAW,  1, -1, -1},
    /* "Getting to the Lake of Tears... the most common is by doing
     * something particular at the Oasis of Ezrabar." */
    {"OasisEzrabar",SP_ABYSS_GATE, 0, LAND_ABYSS, 0},
    {"Diamond Snds",SP_DRAW,  1, -1, -1},
    {"Tomb Dyvin",  SP_DRAW,  1, -1, -1},
    {"NestaBadlnds",SP_CROSSING, 0, LAND_DURACH, 23},
};

/* --------------------------------------------------------------- Abyss --
 * "Heroes always start at the Lake of Tears.  They move one space per turn
 * in a spiral... until they reach Gharad's tower." */
static const Space abyss_path[] = {
    {"Lake Tears",  SP_LAKE,  0, -1, -1},
    {"Lava Flows",  SP_DRAW,  0, -1, -1},
    {"Demon Tower", SP_DRAW,  0, -1, -1},
    {"Catacombs",   SP_DRAW,  0, -1, -1},
    {"Harpy Nest",  SP_DRAW,  0, -1, -1},
    {"Darkmore Hld",SP_DRAW,  0, -1, -1},
    {"Warlock",     SP_DRAW,  0, -1, -1},
    {"Gharad Dngn", SP_DRAW,  0, -1, -1},
    {"GharadsTower",SP_GHARAD,0, -1, -1},
};

/* --------------------------------------------------------------- Urthe --
 * "Urthe represents Durach 1,000 years in the future", reached only by
 * time travel and left by bringing Propha some Ancient Bones. */
static const Space urthe_ring[] = {
    {"Prophas Keep",SP_CROSSING, 0, LAND_DURACH, 0},
    {"Odar Peaks",  SP_DRAW,  1, -1, -1},
    {"Jardoc Wstl", SP_DRAW,  1, -1, -1},
    {"OgresandTmbr",SP_DRAW,  2, -1, -1},
    {"GedwinSprngs",SP_CROSSING, 0, LAND_DURACH, 15},
    {"WyvernBarrns",SP_DRAW,  1, -1, -1},
    {"Darklimb",    SP_DRAW,  2, -1, -1},
    {"DarktailPks", SP_DRAW,  1, -1, -1},
    {"CastleBlckcd",SP_DRAW,  0, -1, -1},
    {"BlackwoodGrw",SP_DRAW,  2, -1, -1},
    {"RockfallWsts",SP_DRAW,  1, -1, -1},
    {"Lornin Range",SP_DRAW,  1, -1, -1},
    {"TempleSrpnt", SP_DRAW,  0, -1, -1},
    {"ParithWstlnd",SP_DRAW,  1, -1, -1},
    {"NerimTmbrlnd",SP_DRAW,  2, -1, -1},
    {"RathgarMassf",SP_DRAW,  1, -1, -1},
    {"Rogue Camp",  SP_DRAW,  0, -1, -1},
    {"Amnoc Growth",SP_DRAW,  2, -1, -1},
    {"Drakebone",   SP_DRAW,  1, -1, -1},
    {"Lagor Range", SP_DRAW,  1, -1, -1},
    {"ChapelTarri", SP_DRAW,  0, -1, -1},
    {"Gharad Peaks",SP_DRAW,  1, -1, -1},
    {"Ashmorrow",   SP_DRAW,  2, -1, -1},
    {"Oknak Barrns",SP_DRAW,  1, -1, -1},
    {"Darkvale",    SP_MARKET,0, -1, -1},
    {"MassifKirana",SP_DRAW,  1, -1, -1},
    {"WolfbaneBrsh",SP_DRAW,  2, -1, -1},
    {"ValgarWstlnd",SP_DRAW,  1, -1, -1},
    {"VardahGrvyrd",SP_DRAW,  0, -1, -1},
    {"FirestormPss",SP_DRAW,  1, -1, -1},
    {"MaplevineGrw",SP_DRAW,  2, -1, -1},
    {"Upal Plains", SP_DRAW,  1, -1, -1},
};

#define NELEM(a) ((int)(sizeof (a) / sizeof (a)[0]))

/* Indexed by the Land enum -- the order of these rows is the order of
 * LAND_FROST, LAND_TARRI, LAND_DURACH, LAND_ALDUN, LAND_ABYSS, LAND_URTHE
 * and must stay that way. */
/* Hull, Speed and value as printed; the names are the legible ones from the
 * same sheets.  See the note in djarhun.h -- the ship cards are sliced
 * across two columns and this is a reconstruction from the numbers. */
const ShipDef ship_tbl[] = {
    {"Carved Log",           10,  3, 3},
    {"Windless Sail",        10,  3, 3},
    {"The Seaside Humility", 15,  4, 5},
    {"Mermaid's Sail",       20,  6, 7},
    {"The Ocean Watcher",    20,  7, 7},
    {"Savage Dragon",        25,  9, 9},
};
const int ship_count = (int)(sizeof ship_tbl / sizeof ship_tbl[0]);

const LandDef land_tbl[LAND_COUNT] = {
    /*  name            spaces        n      topology     base  min level */
    {"Frostburn",   frost_ring,  NELEM(frost_ring),  TOPO_RING,   0,  2},
    {"Tar'ri Ocean",tarri_ring,  NELEM(tarri_ring),  TOPO_RING,  38,  3},
    {"Durach",      durach_ring, NELEM(durach_ring), TOPO_RING,  78,  0},
    {"Desert Aldun",aldun_ring,  NELEM(aldun_ring),  TOPO_RING, 110,  4},
    {"The Abyss",   abyss_path,  NELEM(abyss_path),  TOPO_SPIRAL,124, 8},
    {"Urthe",       urthe_ring,  NELEM(urthe_ring),  TOPO_RING, 133,  6},
};

/* The Heroes, read off hero_a.jpg .. hero_l.jpg by extract-heroes.py
 * and parse-heroes.py -- the full table is in HEROES.tsv.  Name,
 * Race, Home, Morality and the three statistics are the printed
 * values.  The skill line is the first of each card's numbered
 * Skills, kept as flavour: none of them are implemented yet. */
const HeroTemplate hero_tbl[] = {
#include "heroes.inc"
};

const int hero_count = NELEM(hero_tbl);

/* --------------------------------------------------------------- cards --
 * Placeholders, pending the deck sheets (frost_*.jpg, durach_*.jpg,
 * aldun_*.jpg, tarri_*.jpg, urthe_*.jpg, spell_*.jpg, treasure_*.jpg).
 * A Foe's highest statistic decides which kind of Battle is fought, so
 * the numbers below already carry that. */
/* Djarhun's decks, read off the 79 print-and-play sheets by
 * extract-decks.py and parse-decks.py -- the full table, all 1,563
 * faces of it, is in CARDS-djarhun.tsv.  Names, types and the Foes'
 * printed Strength, Speed and Sorcery are real.  `order` is the roman
 * numeral: when several cards share a space they are dealt with
 * lowest first. */
const Card card_proto[] = {
#include "cards.inc"
};

int card_count(void)
{
    int i = 0;
    while (card_proto[i].name) i++;
    return i;
}
