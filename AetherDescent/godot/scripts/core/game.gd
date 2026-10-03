extends Node
## The run: who you are, what you carry, where you stand. Autoloaded as Game.
##
## Rules (rules.gd) change this state; the view reads it and plays the `fx`
## queue -- moves, hits, bolts, numbers -- as animation. Nothing in here or in
## the rules waits on a frame, which is what lets tests drive whole floors
## headless.

signal state_changed

var rng := RandomNumberGenerator.new()
var persist := true           # tests turn this off: nothing they do reaches the save files

var hero: Hero                 # the body being driven: one of `party`
var party: Array = []          # every body: [0] the character, then the hires
var map: GameMap
var difficulty := C.Difficulty.NORMAL
var world := C.WorldSize.SHAFT
var run_seed := 1
var depth := 0
var deepest_floor := 0
var gold := 0
var gold_mult := 1
var gold_boon_until := 0
var inventory: Array = []      # [{name, count}]
var keys := 0
var turns := 0
var floor_entries := 0
var escalation_pct := 0
var steps := 0
var junk_count := 0
var junk_value := 0
var recall_countdown := 0
var rested := false            # has taken a room at the Inn: a place to wake
var quest := {}
var oracle_reading := ""       # "stairs" or "depth": spent on the next floor
var writs := 0
var tavern_seed := 1           # the Tavern's twenty, regenerated from this
var tavern_reroll: Array = []  # per seat: how many times it has been let go
var tavern_hired := 0          # bitmask of seats out with you
var tavern_fallen := 0         # bitmask of seats who died in your service
var game_over := false
var won := false
var killed_by := ""

var log_lines: Array = []      # [text, color]
var fx: Array = []             # queued animation events for the view

const LOG_CAP := 200
const SAVE_DIR := "user://"


var settings := {"art": "16bit", "sound": true}


func _ready() -> void:
	rng.randomize()
	var s := _read(SAVE_DIR + "settings.json")
	for k in s:
		settings[k] = s[k]
	Gfx.set_art(settings.art)
	Sfx.enabled = settings.sound


func save_settings() -> void:
	settings.art = Gfx.art
	settings.sound = Sfx.enabled
	_write(SAVE_DIR + "settings.json", settings)


# ---- messages and effects --------------------------------------------------------
func msg(text: String, col := Color(0.97, 0.97, 0.97)) -> void:
	log_lines.append([text, col])
	if log_lines.size() > LOG_CAP:
		log_lines.pop_front()
	fx.append({"type": "msg"})


func warn(text: String) -> void:
	msg(text, Color8(255, 168, 120))


func good(text: String) -> void:
	msg(text, Color8(160, 240, 140))


func emit_fx(e: Dictionary) -> void:
	fx.append(e)


# ---- run lifecycle ---------------------------------------------------------------
func is_swarm() -> bool:
	return difficulty == C.Difficulty.SWARM or difficulty == C.Difficulty.HARDCORE


func waygate_interval() -> int:
	return 1 if world >= C.WorldSize.DEEPS else 5


func world_dims() -> Vector2i:
	return C.WORLD_DIMS[world]


func new_run(class_id: int, name: String, diff: int, wsize: int, seed_ := 0) -> void:
	difficulty = diff
	world = wsize
	run_seed = seed_ if seed_ != 0 else (rng.randi() % 99999 + 1)
	hero = Hero.make(class_id, name)
	party = [hero]
	tavern_seed = rng.randi() % 0x7FFFFFFF + 1
	tavern_reroll = []
	tavern_reroll.resize(Party.TAVERN_ROSTER)
	tavern_reroll.fill(0)
	tavern_hired = 0
	tavern_fallen = 0
	depth = 0
	deepest_floor = 0
	gold = C.NEW_GAME_GOLD
	gold_mult = 1
	gold_boon_until = 0
	inventory = []
	keys = 0
	turns = 0
	floor_entries = 0
	steps = 0
	junk_count = 0
	junk_value = 0
	recall_countdown = 0
	rested = false
	quest = {}
	oracle_reading = ""
	writs = 0
	game_over = false
	won = false
	log_lines = []
	fx = []
	_starting_kit()
	update_escalation()
	enter_town(Town.START)
	msg("Your ether-ship came down through the canopy. Three days later the jungle let you go.")
	msg("A city on no chart. At its heart a temple sinks into the ground.", Color8(200, 200, 230))


func _starting_kit() -> void:
	var a := hero.attrs
	give("Ration Pack", 2)
	if a[C.Attr.CUNNING] >= 7 or a[C.Attr.TECH_WIT] >= 7:
		give("Recall Charm", 1)
	if a[C.Attr.FORTITUDE] >= 7 or a[C.Attr.EMPATHY] >= 7:
		give("Minor Healing Draught", 1)
	if difficulty == C.Difficulty.HARD or difficulty == C.Difficulty.SWARM:
		give("Recall Charm", 2)
	var kits := [[3, 4, 0], [3, 2, 0], [1, 1, 0], [0, 1, 0], [1, 2, 0], [1, 3, 0], [1, 2, 60]]
	var k: Array = kits[hero.archetype]
	hero.weapon_bonus += k[0]
	hero.armor_bonus += k[1]
	gold += k[2]
	match hero.archetype:
		2:   # Marksman: the bow that makes them one
			var t: Dictionary = ItemsData.RANGED[0]
			Rules.equip_ranged(hero, t)
		3:   # Arcanist: the first spell of their school
			for i in SpellBook.school_count(hero.magic_school):
				var idx := SpellBook.school_index(hero.magic_school, i)
				if SpellBook.get_spell(idx).level == 1:
					hero.known_spells.append(idx)
					hero.spell_cd.append(0)
					break
		4:
			give("Minor Healing Draught", 1)
			give("Guard Tonic (Lesser)", 1)
		5:
			give("Field Rations, Preserved", 2)


## Hand the controller to another body of the party.
func controlled_set(h: Hero) -> void:
	hero = h
	if depth > 0:
		Rules.refresh_vision()
	state_changed.emit()


func update_escalation() -> void:
	escalation_pct = Monster.escalation_for(hero.level, floor_entries)


func enter_town(at: Vector2i) -> void:
	depth = 0
	map = Town.generate()
	Party.reap_fallen()
	hero.x = at.x
	hero.y = at.y
	for h in party:
		h.stance_atk_pct = 0
		h.stance_def_pct = 0
	recall_countdown = 0
	hero.stance_atk_pct = 0
	hero.stance_def_pct = 0
	state_changed.emit()


func enter_floor(n: int, arrive_down := true) -> void:
	depth = n
	if n > deepest_floor:
		deepest_floor = n
	floor_entries += 1
	update_escalation()
	map = MapGen.generate(n, world_dims(), run_seed, difficulty)
	var at := map.stairs_up if arrive_down or map.stairs_down.x < 0 else map.stairs_down
	hero.x = at.x
	hero.y = at.y
	Party.place()
	recall_countdown = 0
	_spend_oracle()
	Rules.refresh_vision()
	msg("You descend to floor %d -- %s." % [n, C.BIOME_NAMES[map.biome]] if arrive_down
		else "You climb back to floor %d." % n, Color8(150, 200, 255))
	Quests.check_arrival()
	if map.event_name != "":
		msg("%s %s" % [map.event_name, map.event_desc], Color8(255, 220, 140))
	if map.gold_rush:
		msg("Gold rush! Coin lies everywhere on this floor.", Color8(255, 220, 96))
	if map.overrun:
		warn("This floor is overrun. It keeps producing.")
	if n % waygate_interval() == 0:
		msg("There is a waygate home somewhere near the stairs.", Color8(150, 220, 255))
	var kinds := {}
	for d in map.districts:
		kinds[d.kind] = true
	for k in kinds:
		msg("Wild ground: %s -- %s." % [C.DISTRICT_NAMES[k], C.DISTRICT_NOTES[k]], C.DISTRICT_TINTS[k].lightened(0.3))
	if Monster.is_biome_boss_floor(n):
		warn("Something enormous is waiting near the far stairs.")
	if n == C.MAX_FLOOR:
		warn("The bottom of the Deep Well. The Warden is here.")
	state_changed.emit()


func _spend_oracle() -> void:
	if oracle_reading == "":
		return
	if oracle_reading == "depth":
		map.reveal_all()
		good("The Oracle's reading holds: you know this floor before you walk it.")
	elif map.stairs_down.x >= 0:
		map.reveal_circle(map.stairs_down.x, map.stairs_down.y, 3)
		good("The Oracle's reading holds: you know where the way down is.")
	oracle_reading = ""


# ---- purse and pack ----------------------------------------------------------------
func gain_gold(amount: int) -> int:
	if amount <= 0:
		return 0
	var g := amount
	var bonus := hero.effective_stat(C.Acc.GOLD)
	if bonus > 0:
		g += g * bonus / 100
	if depth > 0 and depth <= gold_boon_until:
		g *= 2
	g *= maxi(gold_mult, 1)
	g = mini(g, 2000000000 - gold)
	gold += g
	return g


func give(name: String, qty := 1) -> bool:
	for s in inventory:
		if s.name == name:
			s.count += qty
			return true
	if ItemsData.consumable_by_name(name).is_empty():
		return false
	inventory.append({"name": name, "count": qty})
	return true


func count_item(name: String) -> int:
	for s in inventory:
		if s.name == name:
			return s.count
	return 0


func take(name: String, qty := 1) -> bool:
	for i in inventory.size():
		var s: Dictionary = inventory[i]
		if s.name == name and s.count >= qty:
			s.count -= qty
			if s.count <= 0:
				inventory.remove_at(i)
			return true
	return false


func recall_charms() -> int:
	return count_item("Recall Charm")


func discounted(price: int) -> int:
	return maxi(0, price - price * hero.shop_discount_pct / 100)


# ---- quests ----------------------------------------------------------------------
func quest_wants_fetch(floor_num: int) -> bool:
	return not quest.is_empty() and quest.get("type") == "fetch" and not quest.get("found", false) \
		and int(quest.get("depth", -1)) == floor_num


# ---- persistence -----------------------------------------------------------------
## Whose run this is: the character it started as, whoever is being driven.
func owner() -> Hero:
	return party[0] if not party.is_empty() else hero


func _slot_path(kind: String) -> String:
	var safe := owner().name.to_lower().replace(" ", "_").validate_filename()
	return SAVE_DIR + "%s_%s.json" % [kind, safe]


func snapshot() -> Dictionary:
	return {
		"version": 2, "hero": hero.to_dict(),
		"party": party.map(func(h): return h.to_dict()), "controlled": party.find(hero),
		"tavern_seed": tavern_seed, "tavern_reroll": tavern_reroll,
		"tavern_hired": tavern_hired, "tavern_fallen": tavern_fallen, "difficulty": difficulty, "world": world,
		"run_seed": run_seed, "depth": depth, "deepest_floor": deepest_floor, "gold": gold,
		"gold_mult": gold_mult, "gold_boon_until": gold_boon_until, "inventory": inventory,
		"keys": keys, "turns": turns, "floor_entries": floor_entries, "steps": steps,
		"junk_count": junk_count, "junk_value": junk_value, "rested": rested, "quest": quest,
		"oracle_reading": oracle_reading, "writs": writs,
		"map": map.to_dict() if depth > 0 else {},
	}


func restore(d: Dictionary) -> void:
	party = []
	for hd in d.get("party", [d.hero]):
		var h := Hero.from_dict(hd)
		h.known_spells = h.known_spells.map(func(v): return int(v))
		h.spell_cd = h.spell_cd.map(func(v): return int(v))
		party.append(h)
	hero = party[clampi(int(d.get("controlled", 0)), 0, party.size() - 1)]
	tavern_seed = int(d.get("tavern_seed", run_seed if d.has("run_seed") else 1))
	tavern_reroll = Array(d.get("tavern_reroll", [])).map(func(v): return int(v))
	tavern_reroll.resize(Party.TAVERN_ROSTER)
	for i in tavern_reroll.size():
		if tavern_reroll[i] == null:
			tavern_reroll[i] = 0
	tavern_hired = int(d.get("tavern_hired", 0))
	tavern_fallen = int(d.get("tavern_fallen", 0))
	difficulty = int(d.difficulty)
	world = int(d.world)
	run_seed = int(d.run_seed)
	deepest_floor = int(d.deepest_floor)
	gold = int(d.gold)
	gold_mult = int(d.gold_mult)
	gold_boon_until = int(d.gold_boon_until)
	inventory = []
	for s in d.inventory:
		inventory.append({"name": s.name, "count": int(s.count)})
	keys = int(d.keys)
	turns = int(d.turns)
	floor_entries = int(d.floor_entries)
	steps = int(d.steps)
	junk_count = int(d.junk_count)
	junk_value = int(d.junk_value)
	rested = d.rested
	quest = d.quest
	oracle_reading = d.get("oracle_reading", "")
	writs = int(d.get("writs", 0))
	game_over = false
	won = false
	fx = []
	update_escalation()
	var f := int(d.depth)
	if f > 0 and not d.map.is_empty():
		depth = f
		map = GameMap.from_dict(d.map)
		Rules.refresh_vision()
	else:
		enter_town(Vector2i(hero.x, hero.y) if f == 0 else Town.START)
	state_changed.emit()


func _write(path: String, data: Dictionary) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f:
		f.store_string(JSON.stringify(data))


func _read(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var f := FileAccess.open(path, FileAccess.READ)
	var parsed = JSON.parse_string(f.get_as_text())
	return parsed if parsed is Dictionary else {}


func save_run() -> void:
	if not persist or hero == null or game_over:
		return
	_write(_slot_path("run"), snapshot())
	var idx := _read(SAVE_DIR + "runs.json")
	idx[owner().name] = {"class": owner().class_name_str(), "level": owner().level, "deepest": deepest_floor,
		"difficulty": difficulty, "time": Time.get_unix_time_from_system()}
	_write(SAVE_DIR + "runs.json", idx)


func list_runs() -> Dictionary:
	return _read(SAVE_DIR + "runs.json")


func load_run(name: String) -> bool:
	var tmp := Hero.new()
	tmp.name = name
	var keep := party
	party = [tmp]
	var d := _read(_slot_path("run"))
	party = keep
	if d.is_empty():
		return false
	restore(d)
	log_lines = []
	msg("Welcome back, %s." % owner().name, Color8(150, 200, 255))
	return true


func delete_run() -> void:
	if hero == null:
		return
	DirAccess.remove_absolute(ProjectSettings.globalize_path(_slot_path("run")))
	DirAccess.remove_absolute(ProjectSettings.globalize_path(_slot_path("inn")))
	var idx := _read(SAVE_DIR + "runs.json")
	idx.erase(owner().name)
	_write(SAVE_DIR + "runs.json", idx)


func save_inn_snapshot() -> void:
	if not persist:
		return
	var s := snapshot()
	s.depth = 0
	s.map = {}
	_write(_slot_path("inn"), s)


func load_inn_snapshot() -> bool:
	var d := _read(_slot_path("inn"))
	if d.is_empty():
		return false
	restore(d)
	return true


func record_highscore() -> int:
	if not persist:
		return deepest_floor
	var hs := _read(SAVE_DIR + "highscores.json")
	var best := int(hs.get("best", 0))
	if deepest_floor > best:
		best = deepest_floor
		hs.best = best
		_write(SAVE_DIR + "highscores.json", hs)
	return best
