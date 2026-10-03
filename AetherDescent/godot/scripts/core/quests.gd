class_name Quests
## The bounty board -- a port of src/quests.c. Six kinds:
##
##   kill / fetch / clear   slay one, bring one back, thin a floor out
##   timed                  be on floor N before the clock runs out
##   norecall               get there without cracking a charm
##   escort                 get somebody else there alive
##
## The escort's client is not a special kind of thing: they are a body in the
## party like any hire, built the same way, and they walk, fight and die under
## the same rules. Everything that makes them a client is in the bounty.
##
## Game.quest holds the notice: {type, depth, gold, xp, ...}; empty is none.

const KINDS := ["kill", "fetch", "clear", "timed", "norecall", "escort"]
const ITEM_NAMES := ["a Sealed Dispatch Case", "a Cracked Pressure Gauge", "a Waterlogged Ledger",
	"a Company Signet", "a Corroded Aether Vial", "a Torn Survey Map", "a Rusted Union Badge",
	"a Sealed Specimen Jar"]


static func rnd(n: int) -> int:
	return Game.rng.randi() % maxi(n, 1)


static func active() -> bool:
	return not Game.quest.is_empty()


## The client walking with you, if this is an escort and they are still in the party.
static func client() -> Hero:
	for h in Game.party:
		if h.is_client:
			return h
	return null


## Lets the client go, whatever became of them. Every way out of an escort --
## paid, failed or abandoned -- comes through here.
static func _release_client() -> void:
	var c := client()
	if c == null:
		return
	if c == Game.hero:
		for h in Game.party:
			if h != c and Party.is_up(h):
				Game.controlled_set(h)
				break
	Game.party.erase(c)


static func _take_on_client() -> Hero:
	if Game.party.size() >= Party.MAX_COMPANIONS + 1:
		return null
	var c := Party.candidate(rnd(Party.TAVERN_ROSTER))
	c.roster_idx = -1          # not of the Tavern: nobody to rehire them from
	c.is_client = true
	c.x = Game.hero.x
	c.y = Game.hero.y
	Game.party.append(c)
	return c


## Posts a new bounty, replacing whatever was there. Returns the notice's line.
## `kind` picks one instead of the board's roll (the tests use it).
static func offer(kind := "") -> String:
	_release_client()
	var base := maxi(Game.deepest_floor, 1)
	var hi := mini(base + 3, C.MAX_FLOOR)
	var lo := mini(maxi(base - 5, 1), hi)
	var depth := lo + rnd(hi - lo + 1)
	var q := {"depth": depth, "gold": 40 + depth * 4 + rnd(40), "xp": 15 + depth * 3 + rnd(15)}
	q.type = kind if kind in KINDS else KINDS[rnd(KINDS.size())]
	var line := ""
	match q.type:
		"fetch":
			q.item = ITEM_NAMES[rnd(ITEM_NAMES.size())]
			q.found = false
			line = "Recover %s from floor %d and bring it back here." % [q.item, depth]
		"clear":
			q.needed = 5 + depth / 10 + rnd(4)
			q.done = 0
			q.gold = q.gold * 3 / 2
			q.xp = q.xp * 3 / 2
			line = "Clear %d foes from floor %d." % [q.needed, depth]
		"timed":
			# enough turns to walk it at a fair pace, not to clear every floor on the way
			var budget := 400 + depth * 90
			q.deadline = Game.turns + budget
			q.gold *= 2
			q.xp *= 2
			line = "Be standing on floor %d within %d turns." % [depth, budget]
		"norecall":
			q.broken = false
			q.gold *= 2
			q.xp *= 2
			line = "Reach floor %d without cracking a recall charm." % depth
		"escort":
			var c := _take_on_client()
			if c == null:
				# nowhere to put them: a dull bounty beats one that cannot start
				q.type = "kill"
			else:
				q.client = c.name
				q.gold *= 3
				q.xp *= 2
				line = "%s wants to reach floor %d. Keep them breathing." % [c.name, depth]
	if q.type == "kill":
		q.monster = Monster.quest_name(depth, Game.rng)
		line = "Slay a %s on floor %d." % [q.monster, depth]
	Game.quest = q
	Game.msg("New bounty: " + line, Color8(255, 220, 140))
	return line


static func _pay(why: String) -> void:
	var q := Game.quest
	var g := Game.gain_gold(int(q.gold))
	Rules.grant_xp(int(q.xp))
	Game.good("%s (+%d gold, +%d xp)" % [why, g, int(q.xp)])
	Sfx.play("levelup")
	_release_client()
	Game.quest = {}


## The three bounties decided by arriving somewhere settle here, on every
## floor arrival.
static func check_arrival() -> void:
	var q := Game.quest
	if q.is_empty() or not q.type in ["timed", "norecall", "escort"] or Game.depth != int(q.depth):
		return
	var why := ""
	if q.type == "timed" and Game.turns > int(q.deadline):
		why = "The bounty was on the clock, and the clock ran out."
	if q.type == "norecall" and q.get("broken", false):
		why = "The bounty said no charms. The charm is spent."
	if q.type == "escort" and not Party.is_up(client()):
		why = "You arrive without your client. Nobody is paying for that."
	if why != "":
		Game.warn(why)
		_release_client()
		Game.quest = {}
		return
	if q.type == "escort":
		_pay("%s pays out and goes their own way." % client().name)
	else:
		_pay("Bounty complete! Floor %d, as asked." % int(q.depth))


## A recall charm has been spent: that breaks a no-charm bounty.
static func note_recall() -> void:
	if Game.quest.get("type", "") == "norecall":
		Game.quest.broken = true


## At the board: hand over a fetched item.
static func turn_in() -> bool:
	var q := Game.quest
	if q.get("type", "") == "fetch" and q.get("found", false):
		_pay("Bounty complete! You hand over %s." % q.item)
		return true
	return false


## Tears the notice down, releasing the client if there is one.
static func abandon() -> void:
	if not active():
		return
	Game.msg("You tear down the notice: %s." % summary())
	_release_client()
	Game.quest = {}


static func summary() -> String:
	var q := Game.quest
	if q.is_empty():
		return "no bounty"
	var d := int(q.depth)
	match q.type:
		"fetch":
			return "recover %s from floor %d%s" % [q.item, d, " (found -- bring it back)" if q.get("found", false) else ""]
		"clear":
			return "clear %d foes from floor %d (%d done)" % [int(q.needed), d, int(q.get("done", 0))]
		"timed":
			return "reach floor %d within %d turns" % [d, maxi(0, int(q.deadline) - Game.turns)]
		"norecall":
			return "reach floor %d without a charm%s" % [d, " (broken)" if q.get("broken", false) else ""]
		"escort":
			var c := client()
			return "get %s to floor %d%s" % [c.name if Party.is_up(c) else "your client", d, "" if Party.is_up(c) else " (dead)"]
	return "slay a %s on floor %d" % [q.get("monster", "?"), d]
