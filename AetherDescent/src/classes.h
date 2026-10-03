#ifndef CLASSES_H
#define CLASSES_H

#include "common.h"

typedef struct {
    Attribute attr;
    int value;
} AttrOverride;

/* Sentinel marking the end of an overrides list. */
#define OV_END { ATTR_COUNT, 0 }

/* Broad role, used to split the 100 classes into pickable groups. Choosing
   from a hundred names in one flat list is not a choice, it's a scroll --
   the archetype is what the player actually decides first.

   Assigned per class by hand from its lead attributes, with the name
   breaking ties: a precision-led net-caster is a Marksman, a precision-led
   surgeon is an Artificer. Deriving it from the numbers alone got those
   backwards often enough to be worth the table. */
typedef enum {
    ARCH_VANGUARD = 0,  /* might, brawn, grit -- stands in front */
    ARCH_SKIRMISHER,    /* agility, reflexes, balance -- fast and slippery */
    ARCH_MARKSMAN,      /* precision, vision -- kills at a distance */
    ARCH_ARCANIST,      /* the six aether attributes -- casters */
    ARCH_ARTIFICER,     /* tech-wit, crafting, chemistry -- builders */
    ARCH_SURVIVOR,      /* fortitude, stamina, instinct, fortune -- endures */
    ARCH_ENVOY,         /* charm, guile, presence -- leads and talks */
    ARCHETYPE_COUNT
} Archetype;

const char *archetype_name(int a);
const char *archetype_blurb(int a);

/* The class list viewed one archetype at a time, which is how character
   creation browses it. archetype_class_index maps the n-th class of `a` to
   its CLASS_TABLE index, so everything downstream still stores class ids and
   nothing else changes. Same shape as the per-school spell view. */
int archetype_class_count(int a);
int archetype_class_index(int a, int n);

typedef struct {
    const char *name;
    const char *tagline;
    const AttrOverride *overrides; /* everything not listed defaults to 3 */
    const char *start_weapon_name;
    const char *start_armor_name;
    int archetype;                 /* Archetype */
} ClassSeed;

extern const ClassSeed CLASS_TABLE[NUM_CLASSES];

/* ---- the one constructor ---------------------------------------------
   Builds a well-formed body of `class_id` into `h`: identity, attrs[30],
   every stat derived from them, the class's starting weapon and armour, and
   (with starting_kit) the archetype's kit into the party's pack and purse.

   Everything in the game that makes a body goes through here -- the character
   at new-game, a hire at the Tavern, a golem off the assembly line. Two
   constructors producing two shapes is what made a hire a lesser thing than a
   character for as long as there were two structs to make, and it is the root
   the Hero refactor was aimed at rather than the structs themselves.

   `p` may be NULL when starting_kit is false: a candidate being rolled for
   display touches no shared state. */
void hero_create(Player *p, Hero *h, int class_id, bool starting_kit);

/* The character at new-game: slot 0 of the party, with the full kit. */
void apply_class_to_player(Player *p, int class_id);

/* Raises one of the thirty by a point and re-derives everything that hangs
   off it. Used by the gladiator school, which is the only way an attribute
   ever changes after character creation.

   Deliberately not a second call to apply_class_to_player: that rebuilds the
   attribute array from the class seed (undoing the training), resets HP to a
   freshly-derived maximum (throwing away every Constitution Draught the run
   has paid for), and re-issues the starting weapon over whatever is
   equipped. This preserves all three.

   Takes the body rather than the party because the School trains whoever
   walked in, and whoever walked in is whoever holds the role. */
void train_attribute(Hero *h, int attr);

/* The Altar's trade: one point off `give`, one onto `take`, with every derived
   stat recomputed once from the finished sheet rather than twice from two
   halves of the move. False (and nothing changed) if the trade is not legal --
   same attribute both ways, or `give` already at the floor. */
bool trade_attribute(Hero *h, int give, int take);

#endif
