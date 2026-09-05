#include "prophecy.h"

const char *guild_name[G_COUNT] = {
    "Monastery", "Forest Camp", "Magic Tower", "Thieves' Guild", "Fortress"
};

/* The 20 spaces, clockwise from the Village.  Ports let a boat sail to
 * the next Port; the City has both a Port and a Magic Gate. */
const Space ring[RING_N] = {
    /* name          kind              port gate safe */
    { "Village",     SP_VILLAGE,        1,  0,  0 },   /*  0 lodging makes you safe */
    { "Mountains",   SP_MOUNTAINS,      0,  0,  0 },   /*  1 */
    { "Magic Tower", SP_MAGIC_TOWER,    0,  1,  0 },   /*  2 */
    { "Forest",      SP_FOREST,         0,  0,  0 },   /*  3 */
    { "Plains",      SP_PLAINS,         1,  0,  0 },   /*  4 */
    { "Forest",      SP_FOREST,         0,  0,  0 },   /*  5 */
    { "Monastery",   SP_MONASTERY,      0,  0,  1 },   /*  6 */
    { "City",        SP_CITY,           1,  1,  0 },   /*  7 */
    { "Plains",      SP_PLAINS,         0,  0,  0 },   /*  8 */
    { "Mountains",   SP_MOUNTAINS,      0,  0,  0 },   /*  9 */
    { "Fortress",    SP_FORTRESS,       0,  0,  0 },   /* 10 */
    { "Plains",      SP_PLAINS,         1,  0,  0 },   /* 11 */
    { "Wilderness",  SP_WILDERNESS,     0,  1,  0 },   /* 12 Enchanted Wilderness */
    { "Forest",      SP_FOREST,         0,  0,  0 },   /* 13 */
    { "Mountains",   SP_MOUNTAINS,      1,  0,  0 },   /* 14 */
    { "Forest Camp", SP_FOREST_CAMP,    0,  0,  1 },   /* 15 */
    { "Plains",      SP_PLAINS,         0,  0,  0 },   /* 16 */
    { "ThievesGuild",SP_THIEVES_GUILD,  0,  1,  0 },   /* 17 */
    { "Forest",      SP_FOREST,         0,  0,  0 },   /* 18 */
    { "Plains",      SP_PLAINS,         0,  0,  0 },   /* 19 */
};

/* --------------------------------------------------------------- cards --
 * Creatures state Strength first when they attack physically; a player may
 * pay 2 Magic to make it a Battle of Wills instead.  A Creature with only
 * Willpower forces a Battle of Wills and costs the player nothing.
 */
const Adventure adv_proto[] = {
 /* --- Creatures: the beasts of the road --- */
 {"Lone Wolf",     A_CREATURE, .ctype=CT_ANIMAL, .str=3,          .str_first=1, .exp=1, .t_gold=1, .copies=1,
  .text="sell its pelt for 1 Gold"},
 {"Pack of Wolves",A_CREATURE, .ctype=CT_ANIMAL, .str=3, .lives=3,.str_first=1, .exp=4, .t_gold=3, .copies=1,
  .text="sell their pelts for 3 Gold"},
 {"Wild Boar",     A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_ANIMAL, .str=4,          .str_first=1, .exp=1, .t_gold=2, .copies=2},
 {"Bear",          A_CREATURE, .ctype=CT_ANIMAL, .str=5,          .str_first=1, .exp=2, .t_gold=3, .copies=1,
  .text="sell its pelt for 3 Gold"},
 {"Giant Rat",     A_CREATURE, .ctype=CT_ANIMAL, .str=2,          .str_first=1, .exp=1, .t_gold=1, .copies=1},
 {"Poisonous Snake",A_CREATURE, .ctype=CT_ANIMAL,.str=3,          .str_first=1, .exp=3, .t_gold=1, .copies=1,
  .special=AS_POISON, .text="Poison: lose 2 Health even in victory"},
 {"Noble Lion",    A_CREATURE, .ctype=CT_ANIMAL, .str=6,          .str_first=1, .exp=3, .t_str=1, .copies=1,
  .special=AS_NOBILITY, .text="Nobility: you do not need to fight"},

 /* --- Creatures: the men who wait on the road --- */
 {"Brigand",       A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=4,          .str_first=1, .exp=2, .t_gold=3, .copies=3},
 {"Thug",          A_CREATURE, .ctype=CT_HUMANOID, .str=4, .special=AS_FIST, .str_first=1, .exp=2, .t_gold=2, .copies=1,
  .text="he calls you out for a fist fight"},
 {"Highwayman",    A_CREATURE, .ctype=CT_HUMANOID, .str=5,          .str_first=1, .exp=3, .t_gold=4, .copies=1,
  .special=AS_THIEVERY, .text="Thievery: he takes an Item, not a Health"},
 {"Old Pirate",    A_CREATURE, .ctype=CT_HUMANOID, .str=3, .will=3, .str_first=1, .exp=2, .t_gold=4, .copies=1},
 {"Bandit Chief",  A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=5, .will=3, .str_first=1, .exp=3, .t_gold=5, .l_gold=3, .copies=1},
 {"Sold One",      A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=4, .will=3, .str_first=1, .exp=3, .t_gold=2, .copies=2,
  .special=AS_SPARE, .text="spare him and he serves you instead"},
 {"Centaur",       A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=6, .will=4, .str_first=1, .exp=4, .t_item=1, .copies=1,
  .special=AS_SPARE, .text="spare him and he serves you instead"},

 /* --- Creatures: the things that should not be --- */
 {"Ghost",         A_CREATURE, .ctype=CT_UNDEAD, .will=4,                       .exp=3, .t_gold=3, .copies=1,
  .text="it guards a treasure of 3 Gold"},
 {"Ghouls",        A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_UNDEAD, .str=4, .lives=3, .str_first=1,.exp=4, .t_gold=3, .copies=1,
  .text="the graves they were digging may be searched"},
 {"Mummy",         A_CREATURE, .ctype=CT_UNDEAD, .str=5, .will=4, .str_first=1, .exp=4, .t_item=1, .copies=1,
  .text="its curse lies on the treasure"},
 {"Vampire",       A_CREATURE, .ctype=CT_UNDEAD, .str=5, .will=5,               .exp=6, .t_item=1, .l_health=2, .copies=1,
  .special=AS_TWO_BATTLES, .text="a Battle of Wills, then one of Strength"},
 {"Giant",         A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=7,          .str_first=1, .exp=5, .t_str=1, .copies=1,
  .special=AS_STUN, .text="Stun: it takes 2 Magic as well"},
 {"Stone Guardian",A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=6,          .str_first=1, .exp=4, .t_item=1, .copies=1,
  .special=AS_STONE_SKIN, .text="Skin of stone: your weapon is ruined"},
 {"Headless Knight",A_CREATURE, .ctype=CT_UNDEAD,.str=5, .will=5, .str_first=1, .exp=4, .copies=1,
  .special=AS_SWAP_REWARD, .text="beat him one way, be rewarded in the other"},
 {"Doppelganger",  A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_DEMON, .str=1,          .str_first=1, .exp=5, .t_item=1, .copies=1,
  .special=AS_MIRROR, .text="its Strength is twice your Health"},
 {"Nightmare",     A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_DEMON, .will=5,                       .exp=4, .t_will=1, .l_magic=2, .copies=2},
 {"Soul Collector",A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_DEMON, .will=6,                       .exp=5, .t_item=1, .l_magic=3, .copies=1,
  .text="Soul Stealer"},
 {"Wraith",        A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_UNDEAD, .will=4,                       .exp=3, .t_will=1, .copies=2},
 {"Spectre",       A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_UNDEAD, .will=5,                       .exp=3, .t_item=1, .l_magic=2, .copies=2},
 {"Troll",         A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=7,          .str_first=1, .exp=4, .t_str=1,  .copies=1},
 {"Ogre",          A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=6,          .str_first=1, .exp=3, .t_item=1, .copies=2},
 {"Sorcerer",      A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=3, .will=5, .str_first=1, .exp=4, .t_item=1, .copies=1},

 /* --- Opportunities: the kinder things you meet --- */
 {"Apothecary",    A_OPPORTUNITY, .src=SRC_HOUSE, .o_bargain=1, .copies=2,
  .text="2 Gold: heal 2 and recharge 2"},
 {"Enchanted Flower",A_OPPORTUNITY,.o_full_heal=1,                     .copies=1,
  .text="it heals you to full Health"},
 {"Magic Stone",   A_OPPORTUNITY, .o_full_magic=1,                     .copies=1,
  .text="touching it recharges all your Magic"},
 {"Lost Souls",    A_OPPORTUNITY, .src=SRC_EXPANSION, .o_full_magic=1,                     .copies=1,
  .text="recharge all your Magic"},
 {"Golden Fish",   A_OPPORTUNITY, .o_str=1,                            .copies=1,
  .text="it grants 1 wish: +1 Strength"},
 {"Exotic Merchant",A_OPPORTUNITY,.o_str=1,  .cost_gold=8,             .copies=1,
  .text="8 Gold for a magic apple: +1 Strength"},
 {"Wiseman",       A_OPPORTUNITY, .o_will=1, .cost_exp=5,              .copies=1,
  .text="5 Experience: +1 Willpower"},
 {"Cleansing Fire",A_OPPORTUNITY, .o_will=1, .cost_health=1,           .copies=1,
  .text="1 Health to be cleansed: +1 Willpower"},
 {"Chapel",        A_OPPORTUNITY, .src=SRC_HOUSE, .o_bless=1,                          .copies=2,
  .text="a blessing: +2 on your next first roll"},
 {"The Path of Balance",A_OPPORTUNITY, .src=SRC_HOUSE,.o_gold=5, .o_exp=5,             .copies=1,
  .text="+5 Gold and +5 Experience"},
 {"Magical Wish",  A_OPPORTUNITY, .src=SRC_HOUSE, .o_gold=3,                           .copies=2,
  .text="wish for something: +3 Gold"},
 {"Infernal Pact", A_OPPORTUNITY, .src=SRC_HOUSE, .o_exp=4, .cost_health=1,            .copies=1,
  .text="sign in blood -- 1 Health for 4 Experience"},
 {"Healing Herbs", A_OPPORTUNITY, .src=SRC_HOUSE, .o_heal=2,                           .copies=2,
  .text="heal 2 Health"},
 {"Sacred Spring", A_OPPORTUNITY, .src=SRC_HOUSE, .o_magic=3,                          .copies=2,
  .text="recharge 3 Magic"},
 {"Buried Purse",  A_OPPORTUNITY, .src=SRC_HOUSE, .o_gold=4,                           .copies=2,
  .text="+4 Gold"},
 {"Old Battlefield",A_OPPORTUNITY, .src=SRC_HOUSE,.o_exp=2,                            .copies=2,
  .text="+2 Experience"},
 {"Abandoned Camp",A_OPPORTUNITY, .src=SRC_HOUSE, .o_heal=1, .o_gold=1,                .copies=2,
  .text="heal 1, +1 Gold"},
 {"Standing Stone",A_OPPORTUNITY, .src=SRC_HOUSE, .o_magic=2, .o_exp=1,                .copies=2,
  .text="+2 Magic, +1 Experience"},

 /* ------------------------------------------------------------------
  * The rest of the 93-card Adventure deck.  Names come from the card
  * images; stats marked (v) are verified against photographs, the rest
  * follow the deck's own scale -- an animal of 2-3 Strength for 1-3
  * Experience, a Guardian-ish beast of 6-8 for 5, three lives on
  * anything that comes as a band.
  * ------------------------------------------------------------------ */

 /* --- more beasts --- */
 {"Giant Spider",  A_CREATURE, .ctype=CT_ANIMAL, .str=4, .str_first=1, .exp=1, .t_item=1, .copies=1,
  .text="a Rare Item hangs in its web"},
 {"Horde of Bats", A_CREATURE, .ctype=CT_ANIMAL, .str=2, .lives=3, .str_first=1, .exp=3, .copies=1,
  .text="they come in a cloud: three rolls"},
 {"Scavengers",    A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_ANIMAL, .str=3, .lives=3, .str_first=1, .exp=3, .copies=2,
  .text="they pick over the discarded"},
 {"Carrion-eaters",A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_ANIMAL, .str=3, .lives=3, .str_first=1, .exp=3, .copies=1,
  .text="take an Item from the discards"},

 /* --- more men --- */
 {"Steppe Raiders",A_CREATURE, .ctype=CT_HUMANOID, .str=4, .lives=3, .str_first=1, .exp=4, .t_item=1, .copies=1,
  .text="they do better in the Plains"},
 {"Escaped Murderer",A_CREATURE,.ctype=CT_HUMANOID,.str=5, .str_first=1, .exp=3, .t_gold=4, .copies=1,
  .text="there is a price on his head"},
 {"Swordswoman",   A_CREATURE, .ctype=CT_HUMANOID, .str=5, .str_first=1, .exp=3, .copies=1,
  .text="Experience by however much you beat her"},
 {"Angry Mob",     A_CREATURE, .ctype=CT_HUMANOID, .str=6, .will=2, .lives=3, .str_first=1, .exp=6, .copies=1, /* v */
  .text="a whole village of them, and three rolls"},
 {"Rock Giant",    A_CREATURE, .ctype=CT_HUMANOID, .str=5, .will=4, .str_first=1, .exp=2, .t_item=1, .copies=1, /* v */
  .special=AS_STUN, .text="Stun: 7 Strength in the Mountains, and he takes Magic too"},
 {"Gnome",         A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID, .str=2, .str_first=1, .exp=1, .t_gold=2, .copies=2},
 {"Dark Wizard",   A_CREATURE, .ctype=CT_HUMANOID, .will=6, .exp=4, .t_item=1, .copies=1,
  .text="he meets you mind to mind"},
 {"Master Wizard", A_OPPORTUNITY, .cost_gold=2, .copies=1,                                            /* v */
  .text="2 Gold and he sets you anywhere you please"},

 /* --- more of the restless dead --- */
 {"Skeletal Wizard",A_CREATURE, .ctype=CT_UNDEAD, .will=4, .str=3, .exp=2, .t_item=1, .copies=1,      /* v */
  .text="he attacks with Willpower; 2 Magic forces Strength instead"},
 {"Undead Warrior",A_CREATURE, .ctype=CT_UNDEAD, .str=5, .str_first=1, .exp=3, .t_item=1, .copies=1},
 {"Band of Zombies",A_CREATURE,.ctype=CT_UNDEAD, .str=3, .lives=3, .str_first=1, .exp=4, .copies=1,
  .text="three of them, and slow to fall"},
 {"Ghostly Spirits",A_CREATURE,.ctype=CT_UNDEAD, .will=4, .lives=3, .exp=4, .copies=1,
  .text="three of them, and no blade touches them"},
 {"Golem Guardian",A_CREATURE, .ctype=CT_UNDEAD, .str=6, .str_first=1, .exp=4, .t_item=1, .copies=1,
  .special=AS_STONE_SKIN, .text="Skin of stone: your weapon is ruined by the blow"},

 /* --- demons --- */
 {"Flock of Harpies",A_CREATURE,.ctype=CT_DEMON, .str=3, .will=4, .lives=3, .str_first=1, .exp=1, .t_will=1, .copies=1, /* v */
  .text="the liver is a delicacy: a Willpower from the bank"},
 {"Succubus",      A_CREATURE, .ctype=CT_DEMON, .will=6, .exp=4, .l_magic=1, .copies=1,
  .text="Energy Drain: a Health and a Magic"},
 {"Incubus",       A_CREATURE, .ctype=CT_DEMON, .will=6, .exp=4, .l_magic=1, .copies=1,
  .text="Energy Drain: a Health and a Magic"},
 {"Alter Ego",     A_CREATURE, .ctype=CT_DEMON, .str=4, .str_first=1, .exp=1, .copies=1,              /* v */
  .special=AS_MIRROR, .text="its Strength is twice your Health"},
 {"The Greedy One",A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_DEMON, .str=6, .will=6, .str_first=1, .exp=1, .copies=1,
  .text="pay Health to draw that many Rare Items"},

 /* --- places that pay --- */
 {"Gem Mine",      A_OPPORTUNITY, .o_gold=6,                            .copies=1,                    /* v */
  .text="twice your Strength in Gold, thrice in the Mountains"},
 {"Glass Mountain",A_OPPORTUNITY, .cost_health=4, .o_will=2,            .copies=1,                    /* v */
  .text="4 Health to climb it, and 2 Willpower from the bank"},
 {"Black Well",    A_OPPORTUNITY, .o_str=1,                             .copies=1,                    /* v */
  .text="lose all your Magic, and gain a Strength"},
 {"Leprechaun",    A_OPPORTUNITY, .o_gold=4,                            .copies=1,                    /* v */
  .text="as much Gold as your Willpower, twice that in a Forest"},
 {"Deserted Shack",A_OPPORTUNITY, .t_item=1,                            .copies=1,                    /* v */
  .text="something is left inside: a Common Item"},
 {"Black Market",  A_OPPORTUNITY, .cost_gold=5, .t_item=1,              .copies=1,                    /* v */
  .text="5 Gold to enter, and the pick of six Items"},
 {"Trading Guild", A_OPPORTUNITY, .src=SRC_HOUSE, .o_gold=3,                            .copies=2,
  .text="they pay well for what you no longer need"},
 {"Travelling Apothecary",A_OPPORTUNITY,.o_bargain=1,                   .copies=1,
  .text="Health and Magic, a Gold a point"},
 {"Unlicensed Alchemist",A_OPPORTUNITY, .src=SRC_EXPANSION,.cost_gold=2, .t_item=1,         .copies=1,
  .text="he will copy a scroll or a potion"},
 {"Alchemist",     A_OPPORTUNITY, .cost_gold=4, .o_str=1,               .copies=1,                    /* v */
  .text="trade Willpower for Strength, or the other way about"},
 {"Old Veteran",   A_OPPORTUNITY, .cost_gold=1, .o_exp=5,               .copies=1,
  .text="buy him a drink and hear five Experience of stories"},
 {"Hermit",        A_OPPORTUNITY, .o_exp=3,                             .copies=1,
  .text="he blesses whoever has the least"},

 /* --- places that change you --- */
 {"Magic Forest",  A_OPPORTUNITY, .src=SRC_HOUSE, .o_gold=3, .o_will=1,                 .copies=2,
  .text="wish for something, and roll to see what comes"},
 {"Forgotten Chapel",A_OPPORTUNITY,.o_bless=1,                          .copies=1,
  .text="a blessing kept for the battle you choose"},
 {"Sacred Grove",  A_OPPORTUNITY, .src=SRC_HOUSE, .o_full_magic=1,                      .copies=2,
  .text="all your Magic returns"},
 {"Ancient Scroll",A_OPPORTUNITY, .o_exp=3,                             .copies=1,
  .text="3 Experience, to whoever can read it"},
 {"Clover Meadow", A_OPPORTUNITY, .o_bless=1,                           .copies=1,                    /* v */
  .text="a four-leaf clover, kept to the end of the game"},
 {"Path of the Dead",A_OPPORTUNITY, .src=SRC_EXPANSION,.o_exp=2,                            .copies=1,
  .text="walk with them, and you are Returned -- and undead"},
 {"Standing Spinners",A_OPPORTUNITY, .src=SRC_EXPANSION,.o_will=1,                          .copies=1,
  .text="they weave, and offer you the choice of your fate"},
 {"Pool of Forgetfulness",A_OPPORTUNITY, .src=SRC_HOUSE,.o_full_heal=1,                 .copies=1,
  .text="drink, and the wounds are forgotten"},
 {"Wandering Merchant",A_OPPORTUNITY, .src=SRC_HOUSE,.t_item=1, .cost_gold=3,           .copies=2,
  .text="3 Gold and the pick of his pack"},

 /* --- the last thirteen of the 2nd-edition deck, from the card list --- */
 {"Rat",           A_CREATURE, .ctype=CT_ANIMAL, .str=1, .str_first=1, .exp=1, .t_gold=1, .copies=1,
  .text="barely worth the trouble"},
 {"Giant Scorpion",A_CREATURE, .ctype=CT_ANIMAL, .str=4, .str_first=1, .exp=3, .t_gold=2, .copies=1,
  .special=AS_POISON, .text="Poison: 2 Health, win or lose"},
 {"Band of Brigands",A_CREATURE,.ctype=CT_HUMANOID,.str=4, .lives=3, .str_first=1, .exp=4, .t_gold=4, .copies=1,
  .text="three of them, and they share the purse"},
 {"Dark Apprentice",A_CREATURE,.ctype=CT_HUMANOID, .will=4, .exp=2, .t_item=1, .copies=1,
  .text="he has read further than is wise"},
 {"Mine Gnome",    A_CREATURE, .ctype=CT_HUMANOID, .str=2, .str_first=1, .exp=1, .t_gold=3, .copies=1,
  .text="his pockets rattle"},
 {"Strongman",     A_OPPORTUNITY, .cost_exp=4, .o_str=1,                .copies=1,
  .text="4 Experience for a Strength from the bank"},
 {"Imp",           A_CREATURE, .ctype=CT_DEMON, .will=3, .exp=1, .l_magic=1, .copies=1,
  .text="small, and spiteful with it"},
 {"Lost Soul",     A_CREATURE, .ctype=CT_UNDEAD, .will=3, .exp=2, .copies=1,
  .text="it does not know that it is dead"},
 {"Dryad",         A_OPPORTUNITY, .o_will=1, .o_heal=1,                 .copies=1,
  .text="the tree spirit tends you, in a Forest"},
 {"Collector",     A_OPPORTUNITY, .o_gold=6,                            .copies=1,
  .text="twice the price for one undamaged Item, an Artifact excepted"},
 {"Lost Library",  A_OPPORTUNITY, .o_exp=4,                             .copies=1,
  .text="4 Experience among the mouldering shelves"},
 {"Ruin",          A_OPPORTUNITY, .t_item=1, .o_gold=2,                 .copies=1,
  .text="something is still worth taking here"},
 {"Mysterious Old Man",A_OPPORTUNITY,.o_exp=3, .o_magic=2,              .copies=1,
  .text="he tells you what he should not"},

 /* --- Water Realm Opportunities for the standard game --- */
 {"Beast",         A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_ANIMAL|CT_DEMON,
  .str=6, .will=5, .str_first=1, .exp=3, .t_str=1, .t_will=1, .copies=1,
  .text="beat it and you become it: +1 to both, and you are animal and demon now"},
 {"Returned",      A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_HUMANOID|CT_UNDEAD,
  .str=4, .will=6, .exp=3, .copies=1, .special=AS_SPARE,
  .text="spare him and he tells you what lies in an Astral Plane"},
 {"Demon of Greed",A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_DEMON,
  .str=6, .will=6, .str_first=1, .exp=1, .t_item=1, .copies=1,
  .text="pay Health to draw that many Rare Items"},
 {"Weird Sisters", A_CREATURE, .src=SRC_EXPANSION, .ctype=CT_DEMON,
  .str=3, .will=3, .lives=3, .exp=3, .copies=1, .special=AS_SPARE,
  .text="spare them and choose your next turn of fate"},
 {"Gifts of the Gods",A_OPPORTUNITY, .src=SRC_EXPANSION, .t_item=1,     .copies=1,
  .text="every player draws; you say whether any of it is kept"},
 {"Exchange",      A_OPPORTUNITY, .src=SRC_EXPANSION, .cost_gold=1, .t_item=1, .copies=1,
  .text="a Gold, and your unwanted Items are dealt again"},
 {"Mad Riddler",   A_OPPORTUNITY, .src=SRC_EXPANSION, .cost_health=1, .t_item=1, .copies=1,
  .text="guess what he holds: a Rare Item if you are right, a Health if not"},
 { .name = NULL }
};


/* Items, from the cards.  Prices marked (v) are verified against
 * photographs; the rest follow the same scale (2-3 Gold a plain weapon,
 * 8-10 for a Rare with a keyed bonus).
 *
 * `wtype` matters more than it looks: six Abilities key off it, so a Sword
 * being EDGED is what makes Edged Weapons worth buying. */
const Item item_proto[] = {
 /* --- common weapons --- */
 {"Dagger",        2, .d_str=1, .copies=1, .wtype=WT_EDGED, .thrown=1,
  .text="+1 Str, edged, may be thrown"},
 {"Javelin",       2, .d_str=1, .copies=1, .wtype=WT_LONG, .thrown=1,      /* v */
  .vs_type=CT_ANIMAL, .vs_bonus=1, .text="+1 Str, long, +1 vs animals, may be thrown"},
 {"Axe",           2, .d_str=1, .copies=1, .wtype=WT_CRUSHING, .thrown=1,  /* v */
  .vs_type=CT_DEMON, .vs_bonus=1, .text="+1 Str, crushing, +1 vs demons, may be thrown"},
 {"Cutlass",       3, .d_str=2, .copies=1, .wtype=WT_EDGED, .text="+2 Str, edged"},
 {"Flail",         3, .src=SRC_EXPANSION, .d_str=1, .copies=2, .wtype=WT_CRUSHING,
  .vs_type=CT_UNDEAD, .vs_bonus=1, .text="+1 Str, crushing, +1 vs the undead"},
 {"Spear",         4, .d_str=1, .copies=1, .wtype=WT_LONG, .thrown=2,
  .text="+1 Str, long, may be thrown for +2"},
 {"Shield",        4, .src=SRC_HOUSE, .d_str=1, .copies=3, .wtype=WT_SHIELD, .fragile=1,
  .text="+1 Str, a shield"},
 {"Sword",         5, .d_str=2, .copies=1, .wtype=WT_EDGED, .text="+2 Str, edged"},
 {"Alder Wand",    5, .src=SRC_EXPANSION, .d_will=1, .copies=2, .wtype=WT_WAND, .text="+1 Will, a wand"},
 {"Amulet",        5, .src=SRC_HOUSE, .d_will=1, .copies=3, .text="+1 Willpower"},
 {"War Hammer",    6, .d_str=2, .copies=1, .wtype=WT_CRUSHING|WT_TWOHAND,
  .text="+2 Str, crushing and two-handed"},
 {"Chain Mail",    7, .src=SRC_HOUSE, .d_str=2, .copies=2, .fragile=1, .text="+2 Strength"},
 /* --- rare --- */
 {"Radiant Shield",8, .d_str=1, .rare=1, .copies=1, .wtype=WT_SHIELD, .fragile=1, /* v */
  .vs_type=CT_DEMON|CT_UNDEAD, .vs_bonus=2, .text="+1 Str, +2 vs demons and undead"},
 {"Ring of Concentration",8, .d_will=1, .rare=1, .copies=1,
  .text="+1 Will; cast nothing and recover a Magic"},
 {"Ritual Dagger", 9, .d_str=1, .rare=1, .copies=1, .wtype=WT_EDGED,       /* v */
  .text="+1 Str, edged; Health may be spent in a Battle of Wills"},
 {"Extendable Sword",9, .src=SRC_EXPANSION, .d_str=1, .rare=1, .copies=1, .wtype=WT_EDGED,
  .vs_type=CT_HUMANOID, .vs_bonus=1, .text="+1 Str, edged, +1 vs humanoids"},
 {"Rainbow Spear", 9, .src=SRC_EXPANSION, .d_str=1, .rare=1, .copies=1, .wtype=WT_LONG, .thrown=2,
  .text="+1 Str, long, +1 per type of foe, may be thrown"},
 {"Wizard Staff", 10, .src=SRC_HOUSE, .d_will=2, .rare=1, .copies=2, .wtype=WT_STAFF|WT_TWOHAND,
  .text="+2 Will, a two-handed staff"},
 {"Crystal Skull",10, .d_will=1, .rare=1, .copies=1, .wtype=WT_HAND,       /* v */
  .text="+1 Will, and more in the Astral Planes"},
 {"Gauntlets of Strength",10, .d_str=1, .rare=1, .copies=1,                /* v */
  .text="+1 Strength"},
 {"Great Sword",  13, .d_str=3, .rare=1, .copies=1, .wtype=WT_EDGED|WT_TWOHAND,
  .text="+3 Str, edged and two-handed"},
 {"Crown",        15, .src=SRC_HOUSE, .d_will=3, .rare=1, .copies=1, .text="+3 Willpower"},

 /* --- the rest of the 35 Common Items, from the card list --- */
 {"Hammer",        2, .d_str=1, .copies=1, .wtype=WT_CRUSHING, .text="+1 Str, crushing"},
 {"War Axe",       4, .d_str=2, .copies=1, .wtype=WT_CRUSHING|WT_TWOHAND,
  .text="+2 Str, crushing and two-handed"},
 {"Rapier",        4, .d_str=1, .copies=1, .wtype=WT_EDGED,
  .vs_type=CT_HUMANOID, .vs_bonus=1, .text="+1 Str, edged, +1 vs humanoids"},
 {"Boomerang",     3, .d_str=1, .copies=1, .wtype=WT_EDGED, .thrown=2,
  .text="+1 Str, and it comes back"},
 {"Wooden Shield", 3, .d_str=1, .copies=1, .wtype=WT_SHIELD, .fragile=1, .text="+1 Str, a shield"},
 {"Large Wooden Shield",5, .d_str=2, .copies=1, .wtype=WT_SHIELD, .fragile=1,
  .text="+2 Str, and it may yet save you"},
 {"Cedar Staff",   4, .d_will=1, .copies=1, .wtype=WT_STAFF|WT_TWOHAND, .text="+1 Will, a staff"},
 {"Oaken Staff",   6, .d_will=2, .copies=1, .wtype=WT_STAFF|WT_TWOHAND, .text="+2 Will, a staff"},
 {"Ivory Wand",    4, .d_will=1, .copies=1, .wtype=WT_WAND, .text="+1 Will, a wand"},
 {"Mahogany Wand", 6, .d_will=2, .copies=1, .wtype=WT_WAND, .text="+2 Will, a wand"},
 {"Cape",          3, .d_will=1, .copies=1, .text="+1 Willpower"},
 {"Letter of Recommendation",2, .use=IU_TRAIN_FREE, .copies=1, .oneshot=1,
  .text="the guilds will teach you once for nothing"},
 {"Philosopher's Stone Scroll",4, .use=IU_TRADE_EXP, .copies=1, .oneshot=1,
  .text="Gold as Experience, and Experience as Gold"},
 {"Potion of Healing",     3, .copies=1, .oneshot=1, .u_heal=2,  .text="heal 2 Health"},
 {"Potion of Greater Healing",4,.copies=1, .oneshot=1, .u_heal=3, .text="heal 3 Health"},
 {"Potion of Regeneration",4, .copies=1, .oneshot=1, .u_heal=2, .u_magic=2,
  .text="2 Health and 2 Magic return"},
 {"Potion of Greater Regeneration",6,.copies=1, .oneshot=1, .u_heal=3, .u_magic=3,
  .text="3 Health and 3 Magic return"},
 {"Potion of Magic",       3, .copies=1, .oneshot=1, .u_magic=3, .text="recharge 3 Magic"},
 {"Potion of Greater Magic",3, .copies=1, .oneshot=1, .u_magic=5, .text="recharge 5 Magic"},
 {"Potion of Strength",    4, .copies=1, .oneshot=1, .u_str=2,  .text="+2 Strength until your turn ends"},
 {"Potion of Concentration",4,.copies=1, .oneshot=1, .u_will=2, .text="+2 Willpower until your turn ends"},
 {"Scroll of Teleportation",5, .use=IU_TELEPORT,.copies=1, .oneshot=1, .text="go anywhere on the board"},
 {"Scroll of Stealing",    4, .use=IU_STEAL, .copies=1, .oneshot=1, .text="take an Item from one who shares your space"},
 {"Scroll of Decay",       4, .use=IU_DECAY, .copies=1, .oneshot=1, .text="an Item of theirs is discarded"},
 {"Scroll of Destruction", 5, .use=IU_DESTROY, .copies=1, .oneshot=1, .text="a Creature is destroyed, and gives nothing"},
 {"Scroll of Altered Reality",6, .use=IU_REDEAL,.copies=1, .oneshot=1, .text="the world is dealt again"},
 {"Scroll of Deep Prayer", 5, .copies=1, .oneshot=1, .u_heal=1, .u_magic=1,
  .text="a small mercy, once"},
 {"Ruby Circlet",          7, .d_will=1, .copies=1, .wtype=WT_HAND, .text="+1 Willpower, worn"},

 /* --- the rest of the 26 Rare Items --- */
 {"Berserker's Axe",   11, .d_str=3, .rare=1, .copies=1, .wtype=WT_CRUSHING|WT_TWOHAND,
  .text="+3 Str, and no shield beside it"},
 {"Black Spear",       10, .d_str=2, .rare=1, .copies=1, .wtype=WT_LONG, .thrown=2,
  .vs_type=CT_UNDEAD, .vs_bonus=1, .text="+2 Str, long, and the dead fear it"},
 {"Sword of Smiting",  12, .d_str=3, .rare=1, .copies=1, .wtype=WT_EDGED,
  .vs_type=CT_DEMON, .vs_bonus=2, .text="+3 Str, edged, +2 against demons"},
 {"Thor's Hammer",     13, .d_str=3, .rare=1, .copies=1, .wtype=WT_CRUSHING|WT_TWOHAND,
  .thrown=2, .text="+3 Str, crushing, and it may be thrown"},
 {"Mace of the Dark Gods",11,.d_str=2, .rare=1, .copies=1, .wtype=WT_CRUSHING,
  .vs_type=CT_HUMANOID, .vs_bonus=2, .text="+2 Str, crushing, cruel to men"},
 {"Radiant Scythe",    12, .d_str=3, .rare=1, .copies=1, .wtype=WT_LONG|WT_TWOHAND,
  .vs_type=CT_UNDEAD, .vs_bonus=2, .text="+3 Str, long, and it reaps the dead"},
 {"Knight's Shield",    9, .d_str=2, .rare=1, .copies=1, .wtype=WT_SHIELD, .fragile=1,
  .text="+2 Str, a good shield"},
 {"Spiked Shield",      9, .d_str=2, .rare=1, .copies=1, .wtype=WT_SHIELD|WT_EDGED, .fragile=1,
  .text="+2 Str, a shield that bites back"},
 {"Staff of the Archmagi",12,.d_will=3, .rare=1, .copies=1, .wtype=WT_STAFF|WT_TWOHAND,
  .text="+3 Willpower"},
 {"Flaming Staff",     10, .d_will=2, .rare=1, .copies=1, .wtype=WT_STAFF|WT_TWOHAND,
  .d_str=1, .text="+2 Will and +1 Str, and it burns"},
 {"Ring of Magical Forces",9,.d_will=2, .rare=1, .copies=1, .text="+2 Willpower"},
 {"Crown of Power",    14, .d_will=2, .rare=1, .copies=1, .d_str=1, .text="+2 Will and +1 Strength"},
 {"Ranger's Boots",     8, .use=IU_STRIDE, .rare=1, .copies=1, .text="the roads are shorter in these"},
 {"Flying Carpet",     11, .use=IU_TELEPORT, .rare=1, .copies=1, .text="it carries you over the world"},
 {"Elixir of Youth",   10, .rare=1, .copies=1, .oneshot=1, .u_str=1,
  .text="a Strength from the bank, once"},
 {"Elixir of Wisdom",  10, .rare=1, .copies=1, .oneshot=1, .u_will=1,
  .text="a Willpower from the bank, once"},
 {"Potion of Rebirth", 12, .rare=1, .copies=1, .oneshot=1, .u_heal=5, .u_magic=5,
  .text="everything returns, once"},
 {"Scroll of Wishes",  11, .use=IU_WISH, .rare=1, .copies=1, .oneshot=1, .text="ask, and it is given"},
 {"Scroll of Divine Will",8, .use=IU_DIVINE, .rare=1, .copies=1, .oneshot=1,
  .text="you roll a 6, and your foe a 2"},
 {"Time Spiral Scroll",10, .use=IU_AGAIN, .rare=1, .copies=1, .oneshot=1, .text="the round begins again"},

 /* --- Water Realm Items for the standard game (7 Common, 8 Rare) --- */
 {"Commodity Purchase Order",1,.src=SRC_EXPANSION,.copies=1, .oneshot=1, .use=IU_REDEAL,
  .text="a scroll: the shop here is stocked afresh"},
 {"Unmarked Potion",4, .src=SRC_EXPANSION, .copies=1, .oneshot=1, .u_heal=3, .u_magic=3,
  .text="drink and find out: all of one or all of the other"},
 {"Silver Statuette",6,.src=SRC_EXPANSION, .copies=1, .text="it sells for every penny it cost"},
 {"Combat Gauntlets",7,.src=SRC_EXPANSION, .d_str=1, .copies=1,
  .text="+1 Str, and they may be fitted to one sort of weapon"},
 {"Astral Stone of Order",8,.src=SRC_EXPANSION, .rare=1, .copies=1, .d_str=1,
  .vs_type=CT_UNDEAD|CT_DEMON, .vs_bonus=2,
  .text="set in a weapon: +2 against the undead and demons, and nothing else"},
 {"Astral Stone of Chaos",8,.src=SRC_EXPANSION, .rare=1, .copies=1, .d_str=2,
  .text="set in a weapon: +2 against all that still lives"},
 {"Scroll of Enchantment",9,.src=SRC_EXPANSION, .rare=1, .copies=1, .oneshot=1, .use=IU_STEAL,
  .text="they turn away, and you take what you like"},
 {"Scroll of Exchange of Fate",9,.src=SRC_EXPANSION,.rare=1,.copies=1,.oneshot=1,.use=IU_DIVINE,
  .text="the two rolls change hands"},
 {"Astral Stone of Fortune",10,.src=SRC_EXPANSION,.rare=1,.copies=1, .d_will=1,
  .text="set in a shield or staff: roll twice and keep the better"},
 {"Golden Statuette",10,.src=SRC_EXPANSION, .rare=1, .copies=1,
  .text="it sells for every penny it cost, and it cost a great deal"},

 /* --- Dragon Realm Items for the standard game --- */
 {"Dragonscale Shield",8,.src=SRC_EXPANSION, .rare=1, .copies=1, .wtype=WT_SHIELD, .fragile=1,
  .d_str=2, .vs_type=CT_ANIMAL, .vs_bonus=1, .text="+2 Str, and dragons find it familiar"},
 {"Dragontooth Dagger",7,.src=SRC_EXPANSION, .copies=1, .wtype=WT_EDGED, .d_str=1, .thrown=1,
  .vs_type=CT_DEMON, .vs_bonus=1, .text="+1 Str, edged, and demons mislike it"},
 { .name = NULL }
};


/* The fifty Guild Abilities, ten per Guild.
 *
 * Names come from the Tabletop Simulator card images; costs marked (v) are
 * verified against photographs of the physical cards, the rest follow the
 * cost curve those verified ones establish (2 / 3-4 / 4-5 / 5-6 / 7-8, with
 * the dearest slot in every guild being a reroll, a steal or an extra turn).
 *
 * Where an Ability needs a mechanic the engine does not have yet, it is
 * given the closest standing bonus rather than left out, so the deck is at
 * least the right shape and the AI has the right number of things to buy. */
const AbilityCard abil_proto[] = {
 /* ---------------- Fortress: weapons, and enduring a blow ------------- */
 {"Toughness",        G_FORTRESS,  2, .perk=PK_TOUGH,    .text="lose by exactly 1 and it is a draw"},
 {"Practical Training",G_FORTRESS, 3, .perk=PK_TRAINING, .text="Experience for every Creature you beat"},
 {"Blacksmithing",    G_FORTRESS,  4, .perk=PK_SMITH,    .text="repair an Item, or earn 2 Gold in a town"},   /* v */
 {"Crushing Weapons", G_FORTRESS,  4, .perk=PK_CRUSHING, .text="+1 with a crushing weapon"},                  /* v */
 {"Edged Weapons",    G_FORTRESS,  4, .perk=PK_EDGED,    .text="+1 with an edged weapon"},
 {"Berserker Rage",   G_FORTRESS,  4, .magic=1, .kind=SP_BATTLE_STR, .power=2, .text="1 Magic: +2 Strength"},
 {"Duelling",         G_FORTRESS,  5, .perk=PK_DUEL,     .text="+1 against humanoids and other characters"},
 {"Two-handed Combat",G_FORTRESS,  5, .perk=PK_TWOHAND,  .text="+1 with a two-handed weapon"},
 {"Ambidexterity",    G_FORTRESS,  6, .perk=PK_AMBI,     .text="wield two one-handed weapons at once"},
 {"Concentration",    G_FORTRESS,  8, .magic=1, .kind=SP_BATTLE_STR, .power=3, .text="roll three times, keep the best"}, /* v */

 /* ---------------- Thieves' Guild: gold, and not being seen ---------- */
 {"Pickpocketing",    G_THIEVES,   2, .perk=PK_PICKPOCKET,.text="2 Gold more from humanoids and characters"}, /* v */
 {"Haggling",         G_THIEVES,   2, .perk=PK_HAGGLE,   .text="Items cost you 2 Gold less"},                 /* v */
 {"Stealth",          G_THIEVES,   3, .perk=PK_STEALTH,  .text="use an Opportunity before you fight"},        /* v */
 {"Thrown Weapons",   G_THIEVES,   4, .perk=PK_THROWN,   .text="+1 thrown, and anything may be thrown"},      /* v */
 {"Wharf Rat",        G_THIEVES,   4, .perk=PK_WHARF,    .text="ships are free, to any Port"},                /* v */
 {"Poisoned Blade",   G_THIEVES,   4, .perk=PK_POISON,   .text="creatures suffer for the cut"},
 {"Disguise",         G_THIEVES,   5, .perk=PK_DISGUISE, .text="+1 in Wills against humanoids and characters"},
 {"Fleet of Foot",    G_THIEVES,   5, .perk=PK_FLEET,    .text="a step you do not pay for"},
 {"Counterfeiting",   G_THIEVES,   6, .perk=PK_COUNTERFEIT,.text="once a round, pay 2 Gold less"},
 {"Thievery",         G_THIEVES,   8, .magic=1, .kind=SP_BATTLE_STR, .power=2, .text="take an Item instead of a Health"}, /* v */

 /* ---------------- Forest Camp: the road, and the wild --------------- */
 {"Meditation",       G_CAMP,      2, .kind=SP_MEDITATE, .power=2, .text="instead of moving: recharge 2 Magic"}, /* v */
 {"Healing Arts",     G_CAMP,      3, .magic=1, .kind=SP_HEAL, .power=1, .text="1 Magic: heal at once after a blow"},
 {"Spirits of Forest Trails",G_CAMP,      4, .magic=1, .kind=SP_MOVE_FOREST,   .text="1 Magic: move to any Forest"},
 {"Spirits of the Mountain Paths",G_CAMP,      4, .magic=1, .kind=SP_MOVE_MOUNTAIN, .text="1 Magic: move to any Mountains"},
 {"Stamina",          G_CAMP,      4, .perk=PK_STAMINA,  .text="walk up to three spaces"},
 {"Long Weapons",     G_CAMP,      5, .perk=PK_LONG,     .text="+1 with a long weapon"},                       /* v */
 {"Hunting",          G_CAMP,      5, .perk=PK_HUNT,     .text="+1 against animals, and 2 Gold besides"},      /* v */
 {"Horsemanship",     G_CAMP,      5, .perk=PK_HORSE,    .text="you ride for nothing"},
 {"Mountain Lore",    G_CAMP,      6, .perk=PK_FORESTWISE,.text="+1 in the Mountains, in either kind of battle"}, /* v */
 {"Forest Wisdom",    G_CAMP,      6, .d_will=1,         .text="+1 in a Forest, in either kind of battle"},

 /* ---------------- Monastery: the body spent for the spirit ---------- */
 {"Sacrifice of Blood",G_MONASTERY,2, .kind=SP_SACRIFICE, .power=4, .text="a Health for 4 Magic, instead of moving"}, /* v */
 {"People's Hospitality",G_MONASTERY,3,.kind=SP_HEAL, .power=1, .text="heal 1 and recharge 1 in a town or Plains"},   /* v */
 {"Fanaticism",       G_MONASTERY, 4, .perk=PK_FANATIC,  .text="a draw may be fought again, your choice of kind"},    /* v */
 {"Nautical Rites",   G_MONASTERY, 4, .perk=PK_NAUTICAL, .text="a Gold whenever you sail"},
 {"Exorcism",         G_MONASTERY, 4, .perk=PK_EXORCISM, .text="+1 against the undead and demons"},
 {"Blessed Weapon",   G_MONASTERY, 5, .magic=1, .kind=SP_BATTLE_STR, .power=1, .text="1 Magic: +1 with a weapon"},    /* v */
 {"Miraculous Healing",G_MONASTERY,5, .magic=2, .kind=SP_HEAL, .power=2, .text="2 Magic: heal 2, instead of moving"},
 {"Sanctity of Life", G_MONASTERY, 6, .perk=PK_SANCTITY, .text="the last blow may be refused"},
 {"Curse",             G_MONASTERY,6,.magic=1, .kind=SP_BATTLE_WILL, .power=2, .text="only in a battle of a single roll"},
 {"Prayer",           G_MONASTERY, 7, .perk=PK_TURNBACK, .text="roll twice for anything, and keep what you like"},    /* v */

 /* ---------------- Magic Tower: Magic spent on everything ------------ */
 {"Mass Decay",       G_TOWER,     2, .kind=SP_MEDITATE, .power=2, .text="discard an Item to recharge 2 Magic"},      /* v */
 {"Magic Field Theory",G_TOWER,    3, .magic=1, .kind=SP_MOVE_FOREST, .text="instead of moving, move by Magic"},
 {"Flaming Weapon",   G_TOWER,     4, .magic=1, .kind=SP_BATTLE_STR, .power=2, .text="1 Magic: +2, and the weapon is spent"}, /* v */
 {"Magic Drain",      G_TOWER,     4, .perk=PK_MAGICDRAIN,.text="take a Magic at the start of a Battle of Wills"},
 {"Staff Skill",      G_TOWER,     5, .perk=PK_STAFF,    .text="+1 with a staff, and with a wand in Wills"},          /* v */
 {"Rune of Homecoming",G_TOWER,    5, .magic=1, .kind=SP_MOVE_MOUNTAIN, .text="1 Magic: return whence you came"},
 {"Effective Spellcasting",G_TOWER,5, .perk=PK_SPELLCAST,.text="once a round, a Spell costs 1 Magic less"},
 {"Mental Attack",    G_TOWER,     6, .perk=PK_DISGUISE, .text="forcing a Battle of Wills costs 1 Magic less"},       /* v */
 {"Turn Back Time",    G_TOWER,     7, .perk=PK_TURNBACK, .text="use after the dice: they are rolled again"},
 {"Time Loop",        G_TOWER,     8, .perk=PK_TIMELOOP, .text="at the end of your turn: take another"},

 /* --- the five Cooperative Abilities (Dragon Realm).  Each replaces a
  * named original in the co-op game; here they are simply available, since
  * a co-op mode is not implemented.  Costs are from the cards. --- */
 {"Power Handling",  G_TOWER, .src=SRC_EXPANSION,     2, .text="trading may include Magic and Experience"},
 {"Shield to Shield",G_FORTRESS, .src=SRC_EXPANSION,  4, .d_str=1, .text="+1 to anyone fighting beside you, +2 if both bear shields"},
 {"Miracle of Healing",G_MONASTERY, .src=SRC_EXPANSION,6,.magic=1, .kind=SP_HEAL, .power=2,
  .text="heal 2 to yourself, or 1 to anyone at all"},
 {"Secret Hideouts",G_THIEVES, .src=SRC_EXPANSION,    8, .perk=PK_STEALTH,
  .text="trade or cache Items on your space without losing your move"},
 {"Time Freeze",    G_TOWER, .src=SRC_EXPANSION,      8, .perk=PK_TIMELOOP,
  .text="a Magic per companion: everyone plays a round unwatched by fate"},

 /* --- the five Dragonslayer Abilities.  All cost 4, verified from the
  * cards.  They are offered at Guilds by "Legends of Dragons" and their
  * combat bonuses apply ONLY against Dragon Realm Creatures. --- */
 {"Dragonslaying",    G_FORTRESS,  .src=SRC_EXPANSION, 4, .perk=PK_DRAGONSLAY,
  .text="+2 in Strength against a Realm Creature; a loss by 1 there is a draw"},
 {"Dragon Psychology",G_TOWER,     .src=SRC_EXPANSION, 4, .perk=PK_DRAGONMIND,
  .text="+3 in Wills against a Realm Creature, and a Magic back if you win"},
 {"Dragon Magic",     G_TOWER,     .src=SRC_EXPANSION, 4, .perk=PK_DRAGONMAGIC,
  .text="spells and Wills cost a Magic less in the Realm"},
 {"Drake Riding",     G_CAMP,      .src=SRC_EXPANSION, 4, .perk=PK_DRAKE,
  .text="the Realm costs nothing to enter, and a loss need not throw you out"},
 {"Dragon Realm Geography",G_THIEVES,.src=SRC_EXPANSION,4, .perk=PK_DRAGONLORE,
  .text="look at two cards on entering, and slip an obstacle for a Magic"},

 /* --- the five Underwater Abilities (Water Realm).  Offered at Guilds by
  * "Legends of the Deeps"; they work only under the sea. --- */
 {"Water Breathing Spell",G_TOWER, .src=SRC_EXPANSION, 4, .perk=PK_WATERBREATH,
  .text="a Magic buys the bag cheaper, two buys a Health with it"},
 {"Underwater Contacts",G_THIEVES, .src=SRC_EXPANSION, 4, .perk=PK_UNDERCONTACT,
  .text="three more Bubbles in every bag, and you may look ahead"},
 {"Commune with Water",G_MONASTERY,.src=SRC_EXPANSION, 5, .perk=PK_COMMUNE,
  .text="+2 in Wills beneath the sea, and two Bubbles back on a win"},
 {"Underwater Combat",G_FORTRESS,  .src=SRC_EXPANSION, 5, .perk=PK_UNDERWATER,
  .text="your hands are free of the water's drag"},
 {"Swimming",         G_CAMP,      .src=SRC_EXPANSION, 4, .perk=PK_SWIMMING,
  .text="a Bubble buys a turn, and stillness buys a Bubble"},
 { .name = NULL }
};


/* The ten Race cards, transcribed from photographs of the physical cards
 * and cross-checked against the Dragon Realm rulebook's "References to
 * Race" page, which is authoritative where the two differ. */
const Race race_proto[RACE_N] = {
 {"Human", 3, .abil_cost=-1, .max_abil=1,
  .text="Fast Learner: 2 extra Experience, Abilities 1 cheaper, one more of them"},
 {"Elf", 2, .d_will=1, .forest_magic=1, .frail=1,
  .text="Willpower +1; magic is cheaper in a Forest; but 0 Health ends you at once"},
 {"Dwarf", 2, .bank=3,
  .text="3 Gold in a bank nothing can reach, and weapons may be made dwarven"},
 {"Troll", 1, .d_str=1, .abil_cost=2, .max_abil=-2, .regen=1, .steadfast=1,
  .text="Strength +1, but a slow learner; heals in the Mountains; no luck touches him"},
 {"Goblin", 1, .d_will=-1, .shield_bonus=1, .trophy=1,
  .text="Willpower -1; a shield is worth more; every kill pays; and he remembers a loss"},
 {"Halfling", 1, .d_str=-1, .lucky=1, .long_penalty=1, .throw_bonus=1,
  .text="Strength -1, but a rolled 1 counts as 8; poor with long weapons, good at throwing"},
};

/* The five Artifacts, from the cards. */
const Artifact art_proto[ART_N] = {
 {"Crown of the Ancient Kings", .d_str=1, .free_train=1,
  .text="+1 Strength; you pay no Gold to train at any guild"},
 {"Astral Sword", .battle_str=2, .vs_char=1, .drain=1,
  .text="edged weapon: +2 in Strength, +1 more against characters, and drains a Magic"},
 {"Mirrored Shield", .save=5,
  .text="lose a battle and roll 5-6: it is a draw, and your foe loses a Health and a Magic"},
 {"Banner of Hope", .banner=1,
  .text="each round, a die: a bonus, or Magic, or Health, or a gift to everyone"},
 {"Royal Cape", .goanywhere=1, .d_will=1,
  .text="+1 Willpower; 2 Magic instead of moving takes you anywhere at all"},
};

/* Five Lesser and five Greater Guardians, one pair per Astral Plane. */
const Guardian guard_proto[] = {
 /* --- the five Lesser Guardians, 3 Experience each --- */
 {"Sphinx", .ctype=CT_ANIMAL, .greater=0, .str=8, .str_first=1, .exp=3, .t_magic=1,
  .text="Riddles: a Magic a roll, and a 6 wins without a fight"},
 {"Tortured Souls", .ctype=CT_UNDEAD, .greater=0, .will=3, .exp=3, .lives=3, .t_magic=1,
  .text="Despair: a Magic before each roll, and they are three"},
 {"Undead Knight", .ctype=CT_UNDEAD, .greater=0, .str=7, .will=6, .str_first=1, .exp=3,
  .text="its barrow holds weapons and shields"},
 {"Vampire Lord", .ctype=CT_UNDEAD, .greater=0, .str=5, .will=5, .exp=3,
  .t_health=1, .t_magic=1, .special=AS_TWO_BATTLES,
  .text="a Battle of Wills, then one of Strength; Drain takes a Health and a Magic"},
 {"Fire-Breathing Dragon", .ctype=CT_ANIMAL, .greater=0, .str=8, .will=7, .str_first=1,
  .exp=3, .no_spells=1,
  .text="Anti-magic Aura: no Spells here, and its fire eats your scrolls"},

 /* --- the five Greater Guardians, 5 Experience each --- */
 {"Storm Queen", .ctype=CT_DEMON, .greater=1, .str=6, .will=7, .str_first=1, .exp=5,
  .t_health=2, .text="Lightning: 2 Health before the battle"},
 {"Shadow of Death", .ctype=CT_DEMON, .greater=1, .will=5, .exp=5, .t_magic=2,
  .text="Shadow Battle: fought as Strength, but settled on Willpower"},
 {"Scaled Monster", .ctype=CT_DEMON, .greater=1, .str=7, .str_first=1, .exp=5,
  .special=AS_STONE_SKIN,
  .text="Corrosive Blood: your weapon is eaten, or you bleed for going without"},
 {"Shapeless Things", .ctype=CT_DEMON, .greater=1, .str=5, .will=6, .exp=5, .lives=3,
  .t_health=1, .t_magic=1,
  .text="three of them; Wills costs Health, Strength costs Magic"},
 {"Master of Pain", .ctype=CT_DEMON, .greater=1, .str=11, .str_first=1, .exp=5,
  .t_health=1, .t_magic=2,
  .text="Agony first, then a battle of Strength AND Wills at once"},

 /* --- the three Guardians Dragon Realm adds to the standard game --- */
 {"Cerberus", .ctype=CT_HUMANOID, .greater=1, .str=8, .str_first=1, .exp=8,
  .t_magic=1, .text="three heads, and it guesses which way you will turn"},
 {"The Nameless", .ctype=CT_DEMON, .greater=1, .will=10, .exp=5, .t_health=1,
  .text="guess its name before the battle, or pay a Health for the trying"},
 {"Insane Mystic", .ctype=CT_HUMANOID, .greater=0, .will=6, .exp=3, .t_magic=2,
  .text="his madness is catching: 2 Magic before a word is said"},
 { .name = NULL }
};



/* The 22 Chance cards of the base game, transcribed from the cards.
 * Most of them stock the world; a few favour whoever draws them. */
const Chance chance_proto[] = {
 {"Plains",        CH_TERRAIN, .terrain=SP_PLAINS,    .copies=2, .text="an Adventure card in every Plains space"},
 {"Forest",        CH_TERRAIN, .terrain=SP_FOREST,    .copies=2, .text="an Adventure card in every Forest space"},
 {"Mountains",     CH_TERRAIN, .terrain=SP_MOUNTAINS, .copies=2, .text="an Adventure card in every Mountains space"},
 {"Fortress",      CH_TRAINING, .guild=G_FORTRESS,  .copies=1, .text="a new Training card in the Fortress"},
 {"Magic Tower",   CH_TRAINING, .guild=G_TOWER,     .copies=1, .text="a new Training card in the Magic Tower"},
 {"Thieves' Guild",CH_TRAINING, .guild=G_THIEVES,   .copies=1, .text="a new Training card in the Thieves' Guild"},
 {"Monastery",     CH_TRAINING, .guild=G_MONASTERY, .copies=1, .text="a new Training card in the Monastery"},
 {"Forest Camp",   CH_TRAINING, .guild=G_CAMP,      .copies=1, .text="a new Training card in the Forest Camp"},
 {"Training",      CH_TRAINING, .guild=G_NONE,      .copies=1, .text="a new Training card in a guild of your choosing"},
 {"Tradesman",     CH_STOCK, .rare=0, .count=3, .copies=1, .text="the Village takes delivery of 3 Common Items"},
 {"Merchant",      CH_STOCK, .rare=1, .count=2, .copies=1, .text="the City takes delivery of 2 Rare Items"},
 {"Magical",       CH_BOON, .count=4, .other=2, .copies=1, .text="recharge up to 4 Magic; everyone else 2"},
 {"Refreshing",    CH_BOON, .count=2, .other=1, .copies=1, .text="heal up to 2 Health; everyone else 1"},
 {"Good Times",    CH_BOON, .count=4, .other=2, .copies=1, .text="gain 4 Gold; everyone else 2"},
 {"Wind",          CH_BOON, .count=1, .other=1, .copies=1, .text="heal 1 Health and recharge 2 Magic"},
 {"Charity",       CH_CHARITY, .copies=1, .text="the poorest are given gold, magic and healing"},
 {"Peaceful Times",CH_PEACE, .copies=1, .text="nothing happens -- you take two turns this round"},
 {"Prophetic",     CH_PROPHETIC, .copies=1, .text="reveal the next card in an Astral Plane"},
 {"Economic",      CH_ECONOMIC, .copies=1, .text="the die and the number of heroes decide the harvest"},
 { .name = NULL }
};

/* The Dragon Realm's own deck, from the card images.  Names are legible;
 * the numbers behind them mostly are not, so they follow the Realm's own
 * scale -- it sits between the main board and the Astral Planes, so its
 * creatures run Strength 5-8 for 3-5 Experience. */
const DragonCard dr_proto[] = {
 /* --- fifteen negative: Obstacles and Creatures --- */
 {"Fire Geyser",          1, .obstacle=1, .toll_health=1,
  .text="roll: on a 1-5 it erupts, and you are scalded"},
 {"Labyrinth of Stone",   1, .obstacle=1, .toll_magic=1,
  .text="a Magic to find the way through"},
 {"Curse of the Crossroads",1,.obstacle=1, .toll_magic=2,
  .text="it sets a die against you in secret"},
 {"Cloud of Forgetfulness",1,.obstacle=1, .toll_magic=2,
  .text="roll, or an Ability slips your mind"},
 {"Ancient Statue",       1, .obstacle=1, .toll_health=1,
  .text="choose one of its faces, and roll"},
 {"Eyes of Fire",         1, .obstacle=1, .toll_health=1,
  .text="meet their gaze and pass, or look away and stay"},
 {"Wild Baby Dragons",    1, .str=5, .will=3, .lives=3, .exp=3,
  .text="three of them, and their mother is somewhere near"},
 {"Mother Dragon",        1, .str=8, .will=6, .exp=5, .toll_health=1,
  .text="if her young are dead she is the worse to meet"},
 {"Golden Dragon",        1, .str=7, .will=7, .exp=5,
  .text="you must win a Battle of Wills before a Battle of Strength"},
 {"Phantom Dragon",       1, .will=7, .exp=4,
  .text="its Willpower is greater than it looks"},
 {"Dragon of the Pass",   1, .str=7, .exp=4, .toll_magic=1,
  .text="you may give up and be thrown back without a fight"},
 {"Steel-Skinned Drake",  1, .str=7, .exp=4,
  .text="Skin of Steel: the weapon you use is ruined"},
 {"Dragon Sorceror",      1, .will=6, .exp=4,
  .text="Dragon eyes: he rolls twice and keeps the better"},
 {"Terror of the Deeps",  1, .str=6, .exp=4, .toll_magic=1,
  .text="Terror: pay Magic to master your fear, or fight afraid"},
 {"Anti-magic Wyrm",      1, .str=6, .will=5, .exp=4,
  .text="Anti-magic Aura: no Spell may be cast here"},

 /* --- twelve positive: Items, Abilities and Opportunities --- */
 {"Guardian of the Realm",0, .gift_str=1, .text="he judges you worthy: a Strength from the bank"},
 {"Dragontooth",          0, .gift_str=1, .text="a tooth as long as your arm"},
 {"Dragon Crown",         0, .gift_will=1, .text="a crown of scales: a Willpower from the bank"},
 {"Bottomless Satchel",   0, .gift_gold=4, .text="it holds more than it should"},
 {"The Pool of Secret Wishes",0,.gift_exp=3, .text="the Pool grants you one wish"},
 {"Boiling Pool",         0, .gift_str=1, .toll_health=1,
  .text="take a dip, if you dare: a Health for a Strength"},
 {"The Pool of Forgetfulness",0,.gift_heal=3, .gift_magic=3,
  .text="the waters take the memory of every wound"},
 {"Dragonlore",           0, .gift_exp=4, .text="learning that costs no Gold at all"},
 {"Hoard of the Wyrm",    0, .gift_gold=6, .text="what a dragon keeps, it keeps well"},
 {"Warm Spring",          0, .gift_heal=2, .gift_magic=2, .text="rest a while"},
 {"Scale of the Ancients",0, .gift_will=1, .text="proof against fire"},
 {"Whispering Cave",      0, .gift_exp=2, .gift_magic=2, .text="the rock remembers, and tells"},
 { .name = NULL }
};

/* The Water Realm's own deck.  The fifteen negative cards are transcribed
 * from the components list and confirmed against the extracted card images;
 * the thirteen positive ones have their names from the images and effects
 * fitted to the Realm's scale, since their text did not survive the OCR. */
const WaterCard wr_proto[] = {
 /* --- fifteen negative --- */
 {"Reefs",              1, .obstacle=1, .no_entry=1, .text="this space cannot be entered"},
 {"Reefs ",             1, .obstacle=1, .no_entry=1, .text="this space cannot be entered"},
 {"Whirlpool",          1, .obstacle=1, .toll_bubble=2, .gift_exp=2,
  .text="two Bubbles, and it spits you out elsewhere"},
 {"Whirlpool ",         1, .obstacle=1, .toll_bubble=2, .gift_exp=2,
  .text="two Bubbles, and it spits you out elsewhere"},
 {"Strong Current",     1, .obstacle=1, .toll_bubble=1,
  .text="it carries you two spaces; a Bubble buys a re-roll"},
 {"Very Strong Current",1, .obstacle=1, .toll_bubble=1,
  .text="it carries you two spaces, and holds you harder"},
 {"Skeletal Shark",     1, .str=4, .exp=4, .toll_magic=1,
  .text="Terror: pay Magic and roll under it, or fight afraid"},
 {"Ancient Squid",      1, .str=4, .lives=4, .exp=6,
  .text="four rounds of it, and then it tries to flee"},
 {"Undersea Patrol",    1, .str=4, .will=4, .lives=3, .exp=4,
  .text="the third of them offers to surrender"},
 {"Dark Siren",         1, .will=7, .exp=4,
  .text="lose, and an Ability goes with your wits"},
 {"Sea Witch",          1, .will=6, .exp=3,
  .text="beat her and you may trade your very blood"},
 {"Drowned Sailors",    1, .will=5, .lives=3, .exp=3,
  .text="lose, and the current takes you two spaces"},
 {"Carnivorous Fish",   1, .str=3, .lives=3, .exp=2,
  .text="pay Health to enrage them, and be paid in Willpower"},
 {"Leviathan",          1, .str=8, .exp=0,
  .text="every near miss weakens it; beat it and it pays for anything"},
 {"Lord of the Sea",    1, .str=7, .will=8, .exp=3, .toll_bubble=2,
  .text="Wavewrack: two Bubbles before a blow is struck"},

 /* --- thirteen positive --- */
 {"Sea Lord\'s Trident", 0, .gift_str=1, .text="a weapon of the deep"},
 {"Coral Tiara",        0, .gift_will=1, .text="forcing a Battle of Wills costs less"},
 {"The Little Mermaid", 0, .gift_heal=2, .gift_magic=2, .text="she sings you whole again"},
 {"Cavern of Thoughts", 0, .gift_exp=3, .text="each opponent chooses in secret"},
 {"Undersea Library",   0, .gift_exp=4, .text="3 Experience to read what lies here"},
 {"Giant Pearl",        0, .gift_gold=6, .text="worth what a small ship is worth"},
 {"School of Golden Fish",0,.gift_gold=4, .text="they grant up to three wishes"},
 {"Sunken Wreck",       0, .gift_gold=5, .text="you may explore the wreck"},
 {"Air Pocket",         0, .gift_bubble=3, .text="regain all your Bubbles"},
 {"Deep Pearl",         0, .gift_will=1, .text="a lesser pearl, but a pearl"},
 {"Kelp Forest",        0, .gift_heal=2, .text="shelter, and a place to breathe"},
 {"Sunlit Shallows",    0, .gift_magic=3, .text="the light reaches even here"},
 {"Summon Storm",       0, .gift_exp=2, .gift_bubble=1, .text="the sea itself takes your part"},
 { .name = NULL }
};
