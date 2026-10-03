class_name Kitchen
## The Ashfall Kitchen -- a port of src/kitchen.c -- and the other two ways
## the plaza takes your money for fun: the lizard track and the Bazaar's
## moving prices.
##
## Meat comes off a blade and nothing else. Burn a thing to death and there is
## nothing on it; put an arrow through it and the cut is ruined; let a hire
## tear it apart and you have a mess. That one restriction is what makes the
## kitchen a build decision rather than a passive drop.

enum { STRINGY, FAIR, PRIME, MYTHIC }
const GRADES := 4
const MEAT_NAMES := ["stringy", "fair", "prime", "mythic"]
const MEAT_BASE := [40, 140, 460, 1600]
const DROP_PCT := 12
const LARDER_BASE := 12
const LARDER_PER_LEVEL := 2
const LARDER_CEIL := 99
const PATRONS := 7
const NIGHTS_PER_VISIT := 1
const REP_MAX := 100
const REP_START := 10

const PATRON_NAMES := ["a canal pilot", "the assayer's widow", "two off-shift miners", "a tally clerk",
	"the harbourmaster", "a lamplighter", "someone in good boots", "a votary of the Rill",
	"the night foreman", "a lizard-track tout", "an off-duty gatekeep", "a woman with a ledger"]
const PATRON_LINES := ["\"Something hot. Don't care what.\"", "\"Whatever's honest, and a lot of it.\"",
	"\"I've had a week. Bring the good cut.\"", "\"I heard what you brought up. That.\""]

# the lizard track
const RUNNERS := ["Ashken's Ruin", "Tin Whistle", "The Baroness", "Sixpenny Grin", "Marrow", "Quick Lily"]
const ODDS_GENEROUS := [3, 4, 6, 8, 10, 12]
const ODDS_SOUR := [2, 3, 4, 5, 6, 7]
const GENEROUS_RACES := 3        # how many before the book turns
const STAKE_MAX := 1000000

# the Bazaar and the Altar
const BAZAAR_MIN_PCT := 55
const BAZAAR_MAX_PCT := 150
const ALTAR_MIN_ATTR := 1


static func rnd(n: int) -> int:
	return Game.rng.randi() % maxi(n, 1)


# ---- meat ----------------------------------------------------------------------
## What a body is worth to a cook: depth sets the floor, a heavy thing for its
## depth is a better cut, and a boss is always the best in the house.
static func grade_for_kill(floor_num: int, maxhp: int, boss: bool) -> int:
	if boss:
		return MYTHIC
	var g := STRINGY
	if floor_num >= 25:
		g = FAIR
	if floor_num >= 60:
		g = PRIME
	if maxhp > (12 + floor_num * 3) * 2 and g < PRIME:
		g += 1
	return g


static func larder_cap() -> int:
	return mini(LARDER_BASE + (maxi(Game.hero.level, 1) - 1) * LARDER_PER_LEVEL, LARDER_CEIL)


static func meat_total() -> int:
	var n := 0
	for g in GRADES:
		n += int(Game.meat[g])
	return n


## Called when your own blade lands the killing blow. Returns the grade taken, or -1.
static func on_clean_kill(mo: Monster) -> int:
	if not mo.is_boss and rnd(100) >= DROP_PCT:
		return -1
	var g := grade_for_kill(Game.depth, mo.maxhp, mo.is_boss)
	if int(Game.meat[g]) >= larder_cap():
		return -1
	Game.meat[g] = int(Game.meat[g]) + 1
	return g


# ---- a service night ---------------------------------------------------------------
static func rep_permille(rep: int) -> int:
	return 500 + clampi(rep, 0, REP_MAX) * 25


static func rep_title(rep: int) -> String:
	if rep >= 90: return "the best table in the city"
	if rep >= 70: return "a name people say"
	if rep >= 45: return "well spoken of"
	if rep >= 25: return "getting known"
	if rep >= 10: return "a room above a bar"
	return "nobody's been in"


## Seven covers. Deeper players and a better name draw a better room.
static func roll_patrons() -> Array:
	var deck := range(PATRON_NAMES.size())
	for i in deck.size() - 1:
		var j: int = i + rnd(deck.size() - i)
		var t = deck[i]
		deck[i] = deck[j]
		deck[j] = t
	var reach := Game.deepest_floor / 20 + Game.kitchen_rep / 30
	var out: Array = []
	for i in PATRONS:
		var want := mini(rnd(2 + mini(reach, 2)), GRADES - 1)
		var purse: int = MEAT_BASE[want] * (10 + Game.deepest_floor) / 10
		purse += rnd(purse / 2 + 1)
		out.append({"wants": want, "purse": mini(purse, 1000000), "name": PATRON_NAMES[deck[i % deck.size()]],
			"line": PATRON_LINES[want]})
	return out


## What serving `grade` pays. Better than asked pays a little over; worse pays
## steeply less, so fobbing a prime-wanting patron off with stringy is not a plan.
static func payout(pat: Dictionary, grade: int) -> int:
	var gap: int = grade - int(pat.wants)
	var pay: int = pat.purse
	if gap > 0:
		pay = pat.purse + pat.purse * 15 / 100
	elif gap < 0:
		for i in range(gap, 0):
			pay = pay * 30 / 100
	pay = pay * rep_permille(Game.kitchen_rep) / 1000
	return mini(pay, 2000000)


static func rep_delta(pat: Dictionary, grade: int) -> int:
	if grade < 0:
		return -3          # turned away
	var gap: int = grade - int(pat.wants)
	if gap == 0: return 3
	if gap > 0: return 4   # delighted, and they tell people
	if gap == -1: return -2
	return -5


# ---- the lizard track ---------------------------------------------------------------
static func odds() -> Array:
	return ODDS_SOUR if Game.races_this_visit >= GENEROUS_RACES else ODDS_GENEROUS


## Chance of winning is the inverse of the odds, roughly. Weights, not a
## simulation of lizards: the animation is the show, the weights are the truth.
static func race_winner(o: Array) -> int:
	var w: Array = o.map(func(v): return maxi(1, 120 / (v + 1)))
	var total := 0
	for v in w:
		total += v
	var roll := rnd(total)
	for i in w.size():
		if roll < w[i]:
			return i
		roll -= w[i]
	return 0


# ---- the Bazaar ----------------------------------------------------------------------
## Today's price, as a percentage of the Apothecary's: a pure function of the
## trip and the stall, so the screen and the till cannot disagree.
static func bazaar_pct(stall: int) -> int:
	var h := (Game.bazaar_day * 2654435761 + stall * 40503) & 0xFFFFFFFF
	h ^= h >> 13
	h = (h * 1274126177) & 0xFFFFFFFF
	h ^= h >> 16
	return BAZAAR_MIN_PCT + h % (BAZAAR_MAX_PCT - BAZAAR_MIN_PCT + 1)
