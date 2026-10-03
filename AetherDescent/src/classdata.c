#include "classes.h"

/* 100 classes. Every attribute not listed here defaults to 3 (average).
   Values run roughly 1 (crippled) to 9 (best in the city). */

static const AttrOverride OV001[] = { {ATTR_MIGHT,9},{ATTR_GRIT,9},{ATTR_FORTITUDE,7},{ATTR_BRAWN,7},{ATTR_STAMINA,6},{ATTR_AGILITY,1},{ATTR_REFLEXES,2}, OV_END };
static const AttrOverride OV002[] = { {ATTR_AGILITY,9},{ATTR_REFLEXES,9},{ATTR_PRECISION,7},{ATTR_CUNNING,7},{ATTR_GRIT,2},{ATTR_BRAWN,2}, OV_END };
static const AttrOverride OV003[] = { {ATTR_INTELLECT,9},{ATTR_CONDUIT,8},{ATTR_TECH_WIT,7},{ATTR_AETHER_SENSE,7},{ATTR_RESOLVE,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV004[] = { {ATTR_MIGHT,7},{ATTR_PRESENCE,7},{ATTR_RESOLVE,7},{ATTR_CHARM,6},{ATTR_GUILE,2},{ATTR_EMPATHY,2}, OV_END };
static const AttrOverride OV005[] = { {ATTR_PRECISION,8},{ATTR_CHEMISTRY,7},{ATTR_RESOLVE,8},{ATTR_EMPATHY,1},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV006[] = { {ATTR_PRECISION,9},{ATTR_VISION,9},{ATTR_RESOLVE,7},{ATTR_BALANCE,6},{ATTR_AGILITY,2}, OV_END };
static const AttrOverride OV007[] = { {ATTR_CUNNING,8},{ATTR_INSTINCT,7},{ATTR_FORTUNE,6},{ATTR_GRIT,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV008[] = { {ATTR_AETHER_SENSE,9},{ATTR_RESOLVE,8},{ATTR_BALANCE,7},{ATTR_EMPATHY,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV009[] = { {ATTR_CHARM,8},{ATTR_GUILE,8},{ATTR_MEMORY,7},{ATTR_RESOLVE,7},{ATTR_BRAWN,2}, OV_END };
static const AttrOverride OV010[] = { {ATTR_MIGHT,7},{ATTR_STAMINA,8},{ATTR_FORTITUDE,6},{ATTR_RESOLVE,1},{ATTR_GUILE,1}, OV_END };

static const AttrOverride OV011[] = { {ATTR_BRAWN,9},{ATTR_STAMINA,8},{ATTR_FORTITUDE,7},{ATTR_CHARM,2},{ATTR_PRECISION,2}, OV_END };
static const AttrOverride OV012[] = { {ATTR_MIGHT,7},{ATTR_GRIT,7},{ATTR_RESOLVE,7},{ATTR_PRESENCE,6},{ATTR_GUILE,2}, OV_END };
static const AttrOverride OV013[] = { {ATTR_FORTITUDE,8},{ATTR_STAMINA,7},{ATTR_RESOLVE,7},{ATTR_HEARING,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV014[] = { {ATTR_PRECISION,8},{ATTR_CRAFTING,9},{ATTR_TECH_WIT,7},{ATTR_MIGHT,2},{ATTR_PRESENCE,2}, OV_END };
static const AttrOverride OV015[] = { {ATTR_RESOLVE,8},{ATTR_INSTINCT,7},{ATTR_CHARM,6},{ATTR_SCRIBING,6},{ATTR_MIGHT,2}, OV_END };
static const AttrOverride OV016[] = { {ATTR_AGILITY,8},{ATTR_STAMINA,8},{ATTR_VISION,7},{ATTR_PRESENCE,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV017[] = { {ATTR_JINX,8},{ATTR_AETHER_SENSE,7},{ATTR_RESONANCE,7},{ATTR_CHARM,1},{ATTR_BRAWN,2}, OV_END };
static const AttrOverride OV018[] = { {ATTR_BALANCE,8},{ATTR_VISION,7},{ATTR_RESOLVE,6},{ATTR_AETHER_SENSE,6},{ATTR_MIGHT,2}, OV_END };
static const AttrOverride OV019[] = { {ATTR_MIGHT,8},{ATTR_BRAWN,7},{ATTR_GRIT,6},{ATTR_PRESENCE,6},{ATTR_AGILITY,2}, OV_END };
static const AttrOverride OV020[] = { {ATTR_CHEMISTRY,8},{ATTR_EMPATHY,7},{ATTR_PRECISION,6},{ATTR_CUNNING,6},{ATTR_PRESENCE,2}, OV_END };

static const AttrOverride OV021[] = { {ATTR_AGILITY,8},{ATTR_GUILE,8},{ATTR_HEARING,6},{ATTR_PRESENCE,1},{ATTR_BRAWN,2}, OV_END };
static const AttrOverride OV022[] = { {ATTR_CRAFTING,8},{ATTR_CHEMISTRY,7},{ATTR_MIGHT,6},{ATTR_RESOLVE,2},{ATTR_GUILE,2}, OV_END };
static const AttrOverride OV023[] = { {ATTR_EMPATHY,7},{ATTR_RESOLVE,7},{ATTR_INSTINCT,7},{ATTR_GRIT,6},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV024[] = { {ATTR_PRECISION,9},{ATTR_AGILITY,7},{ATTR_CHARM,6},{ATTR_BRAWN,2},{ATTR_FORTITUDE,2}, OV_END };
static const AttrOverride OV025[] = { {ATTR_CUNNING,8},{ATTR_FORTUNE,7},{ATTR_CHARM,6},{ATTR_GRIT,2},{ATTR_VISION,2}, OV_END };
static const AttrOverride OV026[] = { {ATTR_CHEMISTRY,9},{ATTR_PRECISION,6},{ATTR_REFLEXES,6},{ATTR_RESOLVE,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV027[] = { {ATTR_RESOLVE,7},{ATTR_GRIT,7},{ATTR_PRESENCE,6},{ATTR_STAMINA,6},{ATTR_GUILE,2}, OV_END };
static const AttrOverride OV028[] = { {ATTR_REFLEXES,8},{ATTR_AGILITY,7},{ATTR_BALANCE,6},{ATTR_RESOLVE,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV029[] = { {ATTR_AGILITY,8},{ATTR_BALANCE,8},{ATTR_STAMINA,6},{ATTR_GRIT,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV030[] = { {ATTR_RESOLVE,8},{ATTR_RESONANCE,7},{ATTR_AETHER_SENSE,6},{ATTR_CHARM,2},{ATTR_AGILITY,2}, OV_END };

static const AttrOverride OV031[] = { {ATTR_BRAWN,9},{ATTR_MIGHT,7},{ATTR_GRIT,6},{ATTR_AGILITY,2},{ATTR_VISION,2}, OV_END };
static const AttrOverride OV032[] = { {ATTR_PRECISION,7},{ATTR_RESOLVE,6},{ATTR_AETHER_SENSE,6},{ATTR_INTELLECT,6},{ATTR_BRAWN,2}, OV_END };
static const AttrOverride OV033[] = { {ATTR_PRECISION,8},{ATTR_MIGHT,6},{ATTR_GRIT,6},{ATTR_CHARM,2},{ATTR_EMPATHY,2}, OV_END };
static const AttrOverride OV034[] = { {ATTR_PRESENCE,9},{ATTR_STAMINA,7},{ATTR_RESOLVE,6},{ATTR_GUILE,2},{ATTR_PRECISION,2}, OV_END };
static const AttrOverride OV035[] = { {ATTR_VISION,9},{ATTR_REFLEXES,6},{ATTR_INSTINCT,6},{ATTR_GRIT,2},{ATTR_BRAWN,2}, OV_END };
static const AttrOverride OV036[] = { {ATTR_AGILITY,8},{ATTR_STAMINA,7},{ATTR_BALANCE,6},{ATTR_CHARM,2},{ATTR_VISION,2}, OV_END };
static const AttrOverride OV037[] = { {ATTR_EMPATHY,8},{ATTR_TECH_WIT,7},{ATTR_RESONANCE,6},{ATTR_MIGHT,2},{ATTR_PRESENCE,2}, OV_END };
static const AttrOverride OV038[] = { {ATTR_CHEMISTRY,7},{ATTR_CRAFTING,6},{ATTR_FORTITUDE,6},{ATTR_EMPATHY,6},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV039[] = { {ATTR_GRIT,8},{ATTR_FORTITUDE,7},{ATTR_MIGHT,6},{ATTR_GUILE,2},{ATTR_RESOLVE,2}, OV_END };
static const AttrOverride OV040[] = { {ATTR_AGILITY,8},{ATTR_HEARING,6},{ATTR_GUILE,6},{ATTR_MIGHT,2},{ATTR_CHARM,2}, OV_END };

static const AttrOverride OV041[] = { {ATTR_TECH_WIT,8},{ATTR_INTELLECT,6},{ATTR_RESONANCE,6},{ATTR_GRIT,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV042[] = { {ATTR_JINX,8},{ATTR_PRECISION,7},{ATTR_RESONANCE,6},{ATTR_EMPATHY,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV043[] = { {ATTR_CHARM,7},{ATTR_INSTINCT,6},{ATTR_AETHER_SENSE,6},{ATTR_SCRIBING,6},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV044[] = { {ATTR_AGILITY,8},{ATTR_BALANCE,8},{ATTR_REFLEXES,7},{ATTR_INTELLECT,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV045[] = { {ATTR_PRECISION,7},{ATTR_CRAFTING,7},{ATTR_RESOLVE,7},{ATTR_EMPATHY,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV046[] = { {ATTR_VISION,7},{ATTR_BALANCE,7},{ATTR_AETHER_SENSE,6},{ATTR_MIGHT,1},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV047[] = { {ATTR_CHEMISTRY,9},{ATTR_PRECISION,6},{ATTR_JINX,6},{ATTR_FORTITUDE,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV048[] = { {ATTR_MIGHT,8},{ATTR_PRESENCE,8},{ATTR_FORTITUDE,7},{ATTR_EMPATHY,1},{ATTR_AGILITY,2}, OV_END };
static const AttrOverride OV049[] = { {ATTR_MIGHT,9},{ATTR_GRIT,7},{ATTR_STAMINA,6},{ATTR_PRECISION,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV050[] = { {ATTR_AETHER_SENSE,7},{ATTR_MEMORY,7},{ATTR_INSTINCT,6},{ATTR_CHARM,2},{ATTR_MIGHT,2}, OV_END };

static const AttrOverride OV051[] = { {ATTR_FORTUNE,8},{ATTR_CUNNING,6},{ATTR_STAMINA,6},{ATTR_CHARM,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV052[] = { {ATTR_HEARING,8},{ATTR_PRESENCE,6},{ATTR_VISION,6},{ATTR_GUILE,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV053[] = { {ATTR_CRAFTING,8},{ATTR_REFLEXES,6},{ATTR_CHEMISTRY,6},{ATTR_CHARM,2},{ATTR_MIGHT,2}, OV_END };
static const AttrOverride OV054[] = { {ATTR_RESOLVE,7},{ATTR_STAMINA,7},{ATTR_GRIT,6},{ATTR_VISION,6},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV055[] = { {ATTR_PRESENCE,8},{ATTR_RESOLVE,8},{ATTR_MIGHT,6},{ATTR_EMPATHY,1},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV056[] = { {ATTR_CHEMISTRY,7},{ATTR_PRECISION,6},{ATTR_GUILE,6},{ATTR_RESOLVE,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV057[] = { {ATTR_PRECISION,7},{ATTR_CRAFTING,6},{ATTR_TECH_WIT,6},{ATTR_MIGHT,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV058[] = { {ATTR_MIGHT,8},{ATTR_BRAWN,8},{ATTR_GRIT,6},{ATTR_AGILITY,1},{ATTR_VISION,2}, OV_END };
static const AttrOverride OV059[] = { {ATTR_INTELLECT,9},{ATTR_TECH_WIT,7},{ATTR_MEMORY,6},{ATTR_PRESENCE,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV060[] = { {ATTR_GUILE,7},{ATTR_PRECISION,6},{ATTR_AGILITY,6},{ATTR_CHARM,2},{ATTR_GRIT,2}, OV_END };

static const AttrOverride OV061[] = { {ATTR_MIGHT,8},{ATTR_PRECISION,7},{ATTR_BRAWN,6},{ATTR_CHARM,2},{ATTR_AGILITY,2}, OV_END };
static const AttrOverride OV062[] = { {ATTR_RESOLVE,7},{ATTR_BALANCE,7},{ATTR_AGILITY,6},{ATTR_GRIT,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV063[] = { {ATTR_RESOLVE,8},{ATTR_FORTITUDE,7},{ATTR_RESONANCE,6},{ATTR_CHARM,1},{ATTR_AGILITY,2}, OV_END };
static const AttrOverride OV064[] = { {ATTR_PRECISION,9},{ATTR_INTELLECT,6},{ATTR_RESOLVE,6},{ATTR_EMPATHY,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV065[] = { {ATTR_FORTITUDE,7},{ATTR_STAMINA,7},{ATTR_RESOLVE,6},{ATTR_CHARM,2},{ATTR_PRECISION,2}, OV_END };
static const AttrOverride OV066[] = { {ATTR_CUNNING,7},{ATTR_INSTINCT,6},{ATTR_STAMINA,6},{ATTR_CHARM,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV067[] = { {ATTR_CHARM,8},{ATTR_GUILE,7},{ATTR_FORTUNE,6},{ATTR_GRIT,2},{ATTR_MIGHT,2}, OV_END };
static const AttrOverride OV068[] = { {ATTR_REFLEXES,9},{ATTR_AGILITY,7},{ATTR_RESOLVE,2},{ATTR_GRIT,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV069[] = { {ATTR_GRIT,7},{ATTR_BRAWN,7},{ATTR_FORTITUDE,6},{ATTR_CHARM,2},{ATTR_VISION,2}, OV_END };
static const AttrOverride OV070[] = { {ATTR_PRECISION,8},{ATTR_AGILITY,6},{ATTR_REFLEXES,6},{ATTR_CHARM,2},{ATTR_GRIT,2}, OV_END };

static const AttrOverride OV071[] = { {ATTR_PRESENCE,8},{ATTR_RESOLVE,7},{ATTR_INSTINCT,6},{ATTR_EMPATHY,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV072[] = { {ATTR_VISION,8},{ATTR_HEARING,6},{ATTR_INSTINCT,6},{ATTR_CHARM,2},{ATTR_PRESENCE,2}, OV_END };
static const AttrOverride OV073[] = { {ATTR_AGILITY,7},{ATTR_PRECISION,5},{ATTR_BRAWN,2},{ATTR_MIGHT,2},{ATTR_RESOLVE,2}, OV_END };
static const AttrOverride OV074[] = { {ATTR_PRESENCE,7},{ATTR_STAMINA,6},{ATTR_CHARM,6},{ATTR_GRIT,2},{ATTR_PRECISION,2}, OV_END };
static const AttrOverride OV075[] = { {ATTR_CHEMISTRY,8},{ATTR_EMPATHY,6},{ATTR_FORTITUDE,6},{ATTR_MIGHT,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV076[] = { {ATTR_MIGHT,7},{ATTR_AGILITY,6},{ATTR_GRIT,6},{ATTR_CHARM,1},{ATTR_INTELLECT,2}, OV_END };
static const AttrOverride OV077[] = { {ATTR_RESOLVE,7},{ATTR_PRESENCE,6},{ATTR_AGILITY,6},{ATTR_GUILE,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV078[] = { {ATTR_INSTINCT,8},{ATTR_AETHER_SENSE,6},{ATTR_RESOLVE,6},{ATTR_CHARM,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV079[] = { {ATTR_AGILITY,9},{ATTR_BALANCE,8},{ATTR_REFLEXES,6},{ATTR_INTELLECT,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV080[] = { {ATTR_TECH_WIT,7},{ATTR_PRECISION,7},{ATTR_RESOLVE,7},{ATTR_GUILE,2},{ATTR_MIGHT,2}, OV_END };

static const AttrOverride OV081[] = { {ATTR_GRIT,7},{ATTR_MIGHT,6},{ATTR_RESOLVE,6},{ATTR_EMPATHY,6},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV082[] = { {ATTR_PRESENCE,8},{ATTR_CHARM,6},{ATTR_RESOLVE,6},{ATTR_GUILE,2},{ATTR_AGILITY,2}, OV_END };
static const AttrOverride OV083[] = { {ATTR_VISION,7},{ATTR_RESOLVE,7},{ATTR_GRIT,6},{ATTR_CHARM,2},{ATTR_AGILITY,2}, OV_END };
static const AttrOverride OV084[] = { {ATTR_CRAFTING,8},{ATTR_MIGHT,7},{ATTR_BRAWN,6},{ATTR_AGILITY,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV085[] = { {ATTR_AGILITY,7},{ATTR_HEARING,6},{ATTR_CUNNING,6},{ATTR_MIGHT,1},{ATTR_PRESENCE,1}, OV_END };
static const AttrOverride OV086[] = { {ATTR_AETHER_SENSE,8},{ATTR_CONDUIT,7},{ATTR_PRECISION,6},{ATTR_GRIT,2},{ATTR_MIGHT,2}, OV_END };
static const AttrOverride OV087[] = { {ATTR_FORTUNE,6},{ATTR_INSTINCT,6},{ATTR_FORTITUDE,6},{ATTR_CUNNING,6},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV088[] = { {ATTR_RESOLVE,8},{ATTR_JINX,7},{ATTR_FORTITUDE,6},{ATTR_CHARM,1},{ATTR_VISION,2}, OV_END };
static const AttrOverride OV089[] = { {ATTR_CRAFTING,8},{ATTR_INTELLECT,6},{ATTR_EMPATHY,6},{ATTR_MIGHT,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV090[] = { {ATTR_VISION,7},{ATTR_RESOLVE,7},{ATTR_BALANCE,6},{ATTR_CHARM,2},{ATTR_PRESENCE,2}, OV_END };

static const AttrOverride OV091[] = { {ATTR_AGILITY,7},{ATTR_STAMINA,7},{ATTR_RESOLVE,6},{ATTR_CHARM,2},{ATTR_MIGHT,2}, OV_END };
static const AttrOverride OV092[] = { {ATTR_REFLEXES,7},{ATTR_CHEMISTRY,6},{ATTR_MIGHT,6},{ATTR_RESOLVE,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV093[] = { {ATTR_VISION,8},{ATTR_AETHER_SENSE,7},{ATTR_INSTINCT,6},{ATTR_CHARM,1},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV094[] = { {ATTR_CHARM,7},{ATTR_RESOLVE,6},{ATTR_PRESENCE,6},{ATTR_GRIT,2},{ATTR_MIGHT,2}, OV_END };
static const AttrOverride OV095[] = { {ATTR_WARDING,8},{ATTR_EMPATHY,6},{ATTR_RESOLVE,6},{ATTR_MIGHT,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV096[] = { {ATTR_INSTINCT,7},{ATTR_PRESENCE,6},{ATTR_INTELLECT,6},{ATTR_GUILE,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV097[] = { {ATTR_MIGHT,8},{ATTR_BRAWN,7},{ATTR_GRIT,6},{ATTR_AGILITY,2},{ATTR_CHARM,2}, OV_END };
static const AttrOverride OV098[] = { {ATTR_AGILITY,8},{ATTR_FORTITUDE,7},{ATTR_BALANCE,6},{ATTR_CHARM,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV099[] = { {ATTR_CUNNING,8},{ATTR_INSTINCT,6},{ATTR_FORTUNE,6},{ATTR_MIGHT,2},{ATTR_GRIT,2}, OV_END };
static const AttrOverride OV100[] = { {ATTR_CONDUIT,8},{ATTR_FORTITUDE,7},{ATTR_RESOLVE,6},{ATTR_GUILE,2},{ATTR_CHARM,2}, OV_END };

const ClassSeed CLASS_TABLE[NUM_CLASSES] = {
    { "Gear-Knight", "Armored steam-suit soldier. Slow, unstoppable, runs hot and heavy.", OV001, "Piston Fist", "Sealed Steam-Plate", ARCH_VANGUARD },
    { "Chrono-Thief", "Pocket-watch saboteur. Fast, slippery, steals seconds before strikes.", OV002, "Watch-Spring Blade", "Slip-Silk Vest", ARCH_SKIRMISHER },
    { "Arcanist-Engineer", "Aetheric-circuit caster. Clever, volatile, spells backfire in steam.", OV003, "Circuit Rod", "Fuse-Lined Coat", ARCH_ARCANIST },
    { "Sky-Pirate", "Cloud-cutting raider. Loud, fearless, leads with a lightning-cutlass.", OV004, "Lightning Cutlass", "Storm-Slick Coat", ARCH_ENVOY },
    { "Ferro-Vivisectionist", "Surgical steel-stitcher. Cold, necessary, fixes flesh with rivets.", OV005, "Rivet Scalpel", "Blood-Stained Apron", ARCH_ARTIFICER },
    { "Zeppelin-Sniper", "High-altitude marksman. Still, patient, fires phosphorus from above.", OV006, "Phosphorus Rifle", "Ghillie Wrap", ARCH_MARKSMAN },
    { "Gutter-Savant", "Under-city scrounger. Vicious, resourceful, knows every drain and den.", OV007, "Sharpened Pipe", "Layered Rags", ARCH_SURVIVOR },
    { "Aether-Sailor", "Void-current navigator. Eerie, unshaken, bends gravity with a scope.", OV008, "Gravity Scope-Lance", "Void-Weave Coat", ARCH_ARCANIST },
    { "Thespian Automaton", "Escaped clockwork performer. Mimic, wanderer, hides brass behind porcelain.", OV009, "Hidden Cane-Blade", "Porcelain Mask-Plate", ARCH_ENVOY },
    { "Pressure Junkie", "Boiler-backed daredevil. Reckless, blazing, redlines until explosion.", OV010, "Overclocked Knuckles", "Redline Harness", ARCH_VANGUARD },

    { "Coal-Stoker", "Below-deck engine feeder. Grimy, strong, keeps the whole ship breathing.", OV011, "Coal Shovel", "Soot-Caked Overalls", ARCH_VANGUARD },
    { "Brass-Collar", "Ex-military enforcer. Rigid, disciplined, cracks skulls by the book.", OV012, "Regulation Truncheon", "Brass-Collar Uniform", ARCH_VANGUARD },
    { "Steam-Diver", "Pressurized-suit deep diver. Silent, patient, hunts in murk and wreckage.", OV013, "Diving Pick", "Pressurized Dive-Suit", ARCH_SURVIVOR },
    { "Clockmaker", "Precision-gear artisan. Deft, obsessive, builds traps into trinkets.", OV014, "Trap-Loaded Cane", "Tool-Belt Coat", ARCH_ARTIFICER },
    { "Oil-Poet", "Doomsday prophet with a pipe. Grim, eloquent, reads smoke like scripture.", OV015, "Weighted Pipe", "Oil-Stained Cassock", ARCH_ENVOY },
    { "Rail-Scout", "Frontier-track surveyor. Lone, eagle-eyed, outruns cavalry on foot.", OV016, "Surveyor's Spike", "Track-Runner Coat", ARCH_SKIRMISHER },
    { "Ash-Witch", "Burned-out factory mystic. Pale, cursed, whispers to dying machines.", OV017, "Cursed Distaff", "Ash-Grey Shroud", ARCH_ARCANIST },
    { "Balloonist", "High-atmosphere drifter. Calm, untethered, navigates by star-glint and wind-teeth.", OV018, "Ballast Hook", "Windproof Coat", ARCH_SKIRMISHER },
    { "Rivet-Legionnaire", "Shock-troop piston-jumper. Brutal, loud, lands like a falling anvil.", OV019, "Piston Warhammer", "Drop-Trooper Plate", ARCH_VANGUARD },
    { "Soot-Doctor", "Back-alley lung-healer. Ragged, quick-handed, trades cures for secrets.", OV020, "Bone Syringe", "Patchwork Coat", ARCH_ARTIFICER },

    { "Cog-Fox", "Silent vent-shaft spy. Sly, nimble, leaves no oil-smudge behind.", OV021, "Vent-Slit Knife", "Oil-Slick Wrap", ARCH_SKIRMISHER },
    { "Blast-Forger", "Powder-headed sapper. Loud, cheerful, counts down in heartbeats.", OV022, "Charge-Set Hammer", "Blast-Scarred Vest", ARCH_ARTIFICER },
    { "Tame-Master", "Whistle-taming beast-handler. Scarred, patient, rides clockwork-hounds into battle.", OV023, "Taming Whistle-Lash", "Scarred Hide Coat", ARCH_SURVIVOR },
    { "Gilt-Rapier", "Brass-silk noble duelist. Poised, deadly, lunges with pressurised precision.", OV024, "Pressurised Rapier", "Brass-Silk Doublet", ARCH_SKIRMISHER },
    { "Junk-King", "Scrap-throne salvage lord. Ragged, shrewd, trades in forgotten teeth and gears.", OV025, "Throne-Leg Cudgel", "Scrap-Plate Mantle", ARCH_SURVIVOR },
    { "Fume-Alchemist", "Gas-mask brew-mixer. Twitchy, corrosive, dissolves locks in acid-vapour.", OV026, "Acid-Vial Sprayer", "Gas-Mask Coat", ARCH_ARTIFICER },
    { "Iron-Constable", "Steam-badge lawkeeper. Stern, tireless, follows oil-trails to justice.", OV027, "Steam-Badge Baton", "Constable's Plate", ARCH_VANGUARD },
    { "Cloud-Scorcher", "Hydro-jet racing pilot. Reckless, grinning, leaves vapor-trails like signatures.", OV028, "Jet-Spur Blade", "Vapor-Trail Suit", ARCH_SKIRMISHER },
    { "Rig-Jockey", "Mast-climbing aerial rigger. Wiry, fearless, patches leaks mid-freefall.", OV029, "Rigging Hook", "Climber's Harness", ARCH_SKIRMISHER },
    { "Gear-Priest", "Cog-wheel cultist zealot. Hollow-eyed, chanting, prays to the great engine below.", OV030, "Cog-Wheel Censer", "Zealot's Vestment", ARCH_ARCANIST },

    { "Anchor-Wright", "Dockside chain-forger. Burly, sooty, anchors airships with bare hands.", OV031, "Anchor-Chain Flail", "Dockside Apron", ARCH_VANGUARD },
    { "Sun-Dialer", "Solar-panel chronomancer. Squinting, patient, burns enemies with focused light.", OV032, "Focusing Lens-Staff", "Panel-Plated Robe", ARCH_ARCANIST },
    { "Rust-Cutter", "Scrapyard acetylene-wielder. Sharp-eyed, steady, carves through hulls like butter.", OV033, "Acetylene Torch-Blade", "Scrapyard Apron", ARCH_MARKSMAN },
    { "Bellow-Breath", "Bellows-lunged shout-captain. Deafening, commanding, shouts orders through cannon-roar.", OV034, "Bellows-Horn Mace", "Captain's Chestplate", ARCH_ENVOY },
    { "Glim-Hawk", "Beacon-lamp spotter. Keen-sighted, twitchy, signals fleets from crow's nests.", OV035, "Signal-Lamp Pole", "Crow's Nest Wrap", ARCH_MARKSMAN },
    { "Pitch-Runner", "Tar-covered courier. Blackened, swift, leaves sticky footprints on rooftops.", OV036, "Tar-Slick Cudgel", "Pitch-Coated Wrap", ARCH_SKIRMISHER },
    { "Cog-Whisperer", "Machine-empath greaser. Gentle, strange, calms raging boilers with a touch.", OV037, "Greaser's Wrench", "Oil-Soft Apron", ARCH_ARTIFICER },
    { "Steam-Chef", "Pressurized-kitchen field cook. Grumpy, inventive, feeds armies on scrap and spice.", OV038, "Pressure Cleaver", "Scalded Apron", ARCH_ARTIFICER },
    { "Bolt-Swallower", "Ammo-guzzling heavy gunner. Reckless, grinning, eats bullets for breakfast.", OV039, "Bolt-Fed Cannon", "Ammo-Belt Plate", ARCH_VANGUARD },
    { "Chimney-Sweep", "Rooftop soot-stalker. Silent, wiry, drops from flues like a shadow.", OV040, "Sweep-Hook Rod", "Soot-Black Wrap", ARCH_SKIRMISHER },

    { "Magnet-Master", "Electromagnetic manipulator. Focused, crackling, rips weapons from enemy hands.", OV041, "Polarity Gauntlet", "Coil-Wound Vest", ARCH_ARTIFICER },
    { "Rivet-Witch", "Cursed rivet-gun shaman. Hexed, humming, drives nails that never miss.", OV042, "Hexed Rivet-Gun", "Humming Shroud", ARCH_ARCANIST },
    { "Barometer-Bard", "Weather-singing troubadour. Melodic, eerie, predicts storms in four-part harmony.", OV043, "Barometer-Staff", "Troubadour's Coat", ARCH_ENVOY },
    { "Gyro-Striker", "Spinning-top martial artist. Whirling, dizzying, deflects bullets with centrifuge-shield.", OV044, "Gyro Discs", "Centrifuge Guard", ARCH_SKIRMISHER },
    { "Clank-Surgeon", "Rusty-prosthetic fixer. Steady-handed, grim, replaces lost limbs with salvage.", OV045, "Salvage Bonesaw", "Prosthetic-Fitted Coat", ARCH_ARTIFICER },
    { "Lighter-Than-Air", "Helium-bag drifter. Floaty, dreamy, scouts valleys from silent heights.", OV046, "Drift-Pole", "Helium-Weave Vest", ARCH_MARKSMAN },
    { "Quicksilver-Dipper", "Mercury-bath alchemist. Toxic, shimmering, poisons blades with liquid metal.", OV047, "Mercury-Dipped Kris", "Toxin-Sealed Apron", ARCH_ARTIFICER },
    { "Furnace-King", "Underground boiler-tyrant. Overbearing, radiant, rules the heat-pits with an iron stoker.", OV048, "Iron Stoker-Rod", "Furnace-Forged Plate", ARCH_VANGUARD },
    { "Piston-Pugilist", "Steam-fist bare-knuckle fighter. Bruised, relentless, punches through brick walls.", OV049, "Piston Knuckles", "Bruiser's Wraps", ARCH_VANGUARD },
    { "Scrimshaw-Sailor", "Bone-carving void-mystic. Tattooed, whispering, reads futures in whale-ivory.", OV050, "Scrimshaw Harpoon", "Tattooed Oilskins", ARCH_ARCANIST },

    { "Slag-Sifter", "Tailings-pile prospector. Dusty, hopeful, finds treasure in other men's waste.", OV051, "Sifting Rake", "Dust-Caked Coat", ARCH_SURVIVOR },
    { "Horn-Blower", "Signal-horn sentinel. Alert, loud, warns of raiders from the mist-banks.", OV052, "Signal-Horn Club", "Sentinel's Cloak", ARCH_ENVOY },
    { "Glue-Stitcher", "Adhesive-patch field-repairer. Quick, messy, holds airships together with resin.", OV053, "Resin-Gun Spike", "Patch-Sealed Vest", ARCH_ARTIFICER },
    { "Wheel-Warden", "Rail-line track-guardian. Lone, disciplined, patrols the endless iron roads.", OV054, "Track-Warden Pike", "Rail-Plate Coat", ARCH_SURVIVOR },
    { "Ash-Captain", "Burned-out shell-commander. Hollow, tactical, leads from the front in silent fury.", OV055, "Command Saber", "Ash-Scarred Plate", ARCH_ENVOY },
    { "Flask-Thrower", "Acid-vial grenadier. Wild, unpredictable, shatters glass and armour alike.", OV056, "Vial-Sling", "Splash-Guard Coat", ARCH_MARKSMAN },
    { "Loom-Weaver", "Silk-and-brass net-caster. Deft, trapping, ensnares foes in conductive threads.", OV057, "Conductive Net-Rod", "Silk-Brass Wrap", ARCH_MARKSMAN },
    { "Ballast-Breaker", "Keel-weight demolisher. Heavy, crushing, drops from above like a meteor.", OV058, "Ballast Maul", "Keel-Weighted Plate", ARCH_VANGUARD },
    { "Sprocket-Sage", "Gear-philosophy thinker. Distracted, brilliant, solves problems by dismantling them.", OV059, "Dismantling Rod", "Scholar's Coat", ARCH_ARTIFICER },
    { "Tar-Pitcher", "Roof-top ambush-thrower. Sly, sticky, slows cavalry to a crawl.", OV060, "Tar-Pot Sling", "Rooftop Wrap", ARCH_SKIRMISHER },

    { "Harpooner", "Deck-mounted whale-spearer. Mighty, accurate, pins enemies to the mast.", OV061, "Deck Harpoon", "Whaler's Oilskin", ARCH_MARKSMAN },
    { "Cloud-Diver", "Parachute-suit free-faller. Brave, wind-rushed, jumps from airships for fun.", OV062, "Dive-Spike", "Parachute Rig", ARCH_SKIRMISHER },
    { "Rust-Priest", "Corrosion-worshipping ascetic. Quiet, decaying, believes entropy is holy.", OV063, "Corroded Crozier", "Rust-Fused Robe", ARCH_SURVIVOR },
    { "Caliper-Man", "Measuring-instrument marksman. Precise, cold, aims for the one-millimetre gap.", OV064, "Caliper Crossbow", "Measured Vest", ARCH_MARKSMAN },
    { "Stoker-Acolyte", "Fire-temple apprentice. Devoted, sweating, tends the sacred furnace.", OV065, "Acolyte's Poker", "Sweat-Soaked Robe", ARCH_SURVIVOR },
    { "Tramp-Steamer", "River-barge vagabond. Drifting, cunning, knows every waterway backwater.", OV066, "Barge-Pole", "Vagabond's Coat", ARCH_SURVIVOR },
    { "Gypsy-Gear", "Wandering clockwork-merchant. Charming, elusive, sells stolen springs and smiles.", OV067, "Merchant's Cudgel", "Traveling Cloak", ARCH_ENVOY },
    { "Nitro-Jockey", "Explosive-fuel speed-demon. Crazy, fast, outruns his own shockwave.", OV068, "Nitro-Spike", "Blast-Wind Suit", ARCH_SKIRMISHER },
    { "Hull-Scrapper", "Underbelly rust-scourer. Tough, filthy, keeps the copper clean.", OV069, "Scraper Blade", "Bilge-Worn Coat", ARCH_SURVIVOR },
    { "Pendulum-Sword", "Swinging-blade fencer. Rhythmic, fatal, strikes on the backswing.", OV070, "Pendulum Blade", "Fencer's Jacket", ARCH_SKIRMISHER },

    { "Coal-Priestess", "Black-goddess cult-leader. Fierce, prophetic, burns heretics in her own fire.", OV071, "Black Censer-Blade", "Cult-Priestess Robe", ARCH_ENVOY },
    { "Visor-Scout", "Lens-mask forward-recon. Quiet, observant, sees heat-signatures through fog.", OV072, "Recon Spike", "Visor-Mask Wrap", ARCH_MARKSMAN },
    { "Rivet-Boy", "Apprentice gear-puller. Eager, small, squeezes into tight places.", OV073, "Apprentice Wrench", "Patched Overalls", ARCH_SKIRMISHER },
    { "Steam-Drummer", "Percussion-battle musician. Rhythmic, motivating, beats time for the charge.", OV074, "Drum-Mallet", "Battle-Drummer's Vest", ARCH_ENVOY },
    { "Flask-Brewer", "Medicinal-syrup distiller. Careful, herbal, cures coughs with whiskey and steam.", OV075, "Brewer's Ladle", "Distiller's Apron", ARCH_ARTIFICER },
    { "Iron-Mongrel", "Half-clockwork war-hound. Loyal, savage, tears out throats on command.", OV076, "Clockwork Fangs", "Half-Plate Hide", ARCH_VANGUARD },
    { "Sky-Warden", "Aerial law-enforcement rider. Stern, soaring, arrests pirates mid-flight.", OV077, "Warden's Lance", "Aerial Rider's Plate", ARCH_ENVOY },
    { "Soot-Prophet", "Grimy future-teller. Murmuring, dreaded, reads ashes like tea-leaves.", OV078, "Ash-Reading Staff", "Grimy Shawl", ARCH_ARCANIST },
    { "Gear-Dancer", "Clockwork-ballet acrobat. Graceful, lethal, kicks with spring-loaded heels.", OV079, "Spring-Loaded Greaves", "Ballet-Wire Corset", ARCH_SKIRMISHER },
    { "Pressure-Tamer", "Regulator-valve technician. Calm, exact, never lets the needle hit red.", OV080, "Valve-Key Wrench", "Regulator's Vest", ARCH_ARTIFICER },

    { "Scrap-Knight", "Patchwork-armour crusader. Noble, mismatched, fights for the broken.", OV081, "Patchwork Blade", "Mismatched Plate", ARCH_VANGUARD },
    { "Whistle-Captain", "Train-conductor warlord. Authoritative, booming, commands with a brass whistle.", OV082, "Conductor's Cane", "Warlord's Coat", ARCH_ENVOY },
    { "Fog-Cutter", "Searchlight-beam operator. Blinding, unyielding, cuts through the murk.", OV083, "Searchlight Lance", "Fog-Cutter's Coat", ARCH_MARKSMAN },
    { "Anvil-Shaper", "Blacksmith-munitions crafter. Heavy-swinging, sparky, forges bullets on the move.", OV084, "Anvil-Head Hammer", "Forge-Scarred Apron", ARCH_ARTIFICER },
    { "Keel-Boy", "Bilge-pump child-runner. Scurrying, overlooked, knows every leaky seam.", OV085, "Bilge-Pump Rod", "Ragged Overalls", ARCH_SKIRMISHER },
    { "Ether-Spinner", "Thread-of-reality weaver. Delicate, dangerous, stitches tears in the sky.", OV086, "Reality-Thread Needle", "Woven Ether-Cloak", ARCH_ARCANIST },
    { "Crater-Crawler", "Bomb-site scavenger. Cautious, hungry, digs through rubble for loot.", OV087, "Rubble Pick", "Blast-Site Wrap", ARCH_SURVIVOR },
    { "Boiler-Wraith", "Overheated-engine ghost. Translucent, vengeful, haunts the engine-room.", OV088, "Wraith-Steam Claw", "Translucent Shroud", ARCH_ARCANIST },
    { "Tinker-Dame", "Scrap-gadget inventor. Clever, matronly, builds wonders from bent spoons.", OV089, "Spoon-Bent Gadget", "Inventor's Apron", ARCH_ARTIFICER },
    { "Drift-Warden", "Anchor-less sky-guardian. Watchful, solitary, drifts above borders.", OV090, "Drift-Warden Glaive", "Anchorless Cloak", ARCH_MARKSMAN },

    { "Rivet-Runner", "Messenger of the gear-cults. Fleet, pious, carries scripture in oil-cans.", OV091, "Messenger's Rivet-Rod", "Cult-Runner's Wrap", ARCH_SKIRMISHER },
    { "Flask-Lighter", "Fire-oil splash-thrower. Quick, fiery, ignites everything he touches.", OV092, "Fire-Oil Flask", "Scorch-Proof Coat", ARCH_MARKSMAN },
    { "Marble-Eye", "Glass-lens augur. Unblinking, unsettling, sees what shouldn't be seen.", OV093, "Augur's Lens-Rod", "Unblinking Shroud", ARCH_ARCANIST },
    { "Chain-Singer", "Anchor-chain musician. Rhythmic, mournful, plays the hull like a harp.", OV094, "Chain-Harp Lash", "Mournful Cloak", ARCH_ENVOY },
    { "Hull-Blesser", "Protective-oil anointed. Slick, blessed, deflects bullets with sacred grease.", OV095, "Blessing Oil-Rod", "Anointed Slick-Coat", ARCH_ARCANIST },
    { "Gyro-Captain", "Spinning-compass navigator. Confident, magnetic, always knows true north.", OV096, "Compass-Blade", "Captain's Gyro-Coat", ARCH_ENVOY },
    { "Piston-Breaker", "Hydraulic-jack demolisher. Relentless, shuddering, cracks vaults with pressure.", OV097, "Hydraulic Jack-Ram", "Demolisher's Plate", ARCH_VANGUARD },
    { "Smog-Dancer", "Gas-mask acrobat. Twisting, breathless, fights in poison clouds.", OV098, "Smog-Twist Blades", "Gas-Mask Wraps", ARCH_SKIRMISHER },
    { "Cog-Crow", "Scavenging bird-tamer. Cunning, feathered, sends clockwork-crows for loot.", OV099, "Crow-Talon Blade", "Feathered Wrap", ARCH_SURVIVOR },
    { "Spark-Catcher", "Lightning-rod survivor. Scarred, jittery, channels storms into his core.", OV100, "Lightning-Rod Spear", "Storm-Scarred Plate", ARCH_ARCANIST },
};
