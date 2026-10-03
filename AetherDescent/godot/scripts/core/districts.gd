class_name Districts
## What the later wild districts do while you stand in them -- a port of
## districts_tick() in src/combat.c and its neighbours: the arena's waves, the
## barrow's ghosts, the eye's moving shelter, the mirror, the belts and the
## channels, the rods and the vents, the watch, the proving ground, the pits,
## and the multi-turn work (`g`) that turns rods, vents, ore and wrecks into
## materials.

static func rnd(n: int) -> int:
	return Game.rng.randi() % maxi(n, 1)


static func M() -> GameMap:
	return Game.map


static func _d(i: int) -> Dictionary:
	return M().districts[i]


static func _inside(d: Dictionary, x: int, y: int) -> bool:
	return x >= d.x and x < d.x + d.w and y >= d.y and y < d.y + d.h


static func _live_in(d: Dictionary) -> int:
	var n := 0
	for mo in M().monsters:
		if mo.alive and _inside(d, mo.x, mo.y):
			n += 1
	return n


static func _hazard(h: Hero, dmg: int) -> int:
	return maxi(1, dmg - dmg * (h.hazard_resist_pct + h.set_bonus_hazard) / 100)


# ---- the proving ground ----------------------------------------------------------
## What the ground under (x,y) forbids. True when `act` (C.Act) is allowed.
static func allows(x: int, y: int, act: int) -> bool:
	var i := M().district_index(x, y)
	if i < 0 or _d(i).kind != C.District.PROVING:
		return true
	match int(_d(i).state):
		C.Prove.MELEE: return act == C.Act.MELEE
		C.Prove.ARCANE: return act == C.Act.ARCANE
	return act != C.Act.RANGED       # unarmed: close in, but your weapon counts for nothing


static func unarmed_at(x: int, y: int) -> bool:
	var i := M().district_index(x, y)
	return i >= 0 and _d(i).kind == C.District.PROVING and int(_d(i).state) == C.Prove.UNARMED


## A body's attack with the proving ground's third skin applied.
static func eff_atk(h: Hero) -> int:
	if Game.depth == 0 or not unarmed_at(h.x, h.y):
		return h.eff_atk()
	var atk := h.base_atk + h.atk_buff + h.set_bonus_atk
	atk += atk * h.stance_atk_pct / 100
	return maxi(atk, 1)


## Kills made on the marked ground count toward its writ.
static func note_kill(x: int, y: int) -> void:
	var i := M().district_index(x, y)
	if i < 0 or _d(i).kind != C.District.PROVING:
		return
	var d := _d(i)
	if d.get("paid", false):
		return
	d.kills = int(d.get("kills", 0)) + 1
	if d.kills < C.PROVE_KILLS_WANTED:
		return
	d.paid = true
	Game.writs += 1
	var purse := Game.gain_gold(250 + Game.depth * 40)
	Game.good("That is the last of them, on their terms and not yours.")
	Game.good("A writ of training, and %d gold. The Gladiator School honours these." % purse)


# ---- the watch -------------------------------------------------------------------
## Anybody's blow inside a watch breaks it -- a hire's arrow, a spell that
## clipped a warden on its way past. The district asks you to start nothing.
static func note_hurt(mo: Monster) -> void:
	var i := M().district_index(mo.x, mo.y)
	if i < 0 or _d(i).kind != C.District.WATCH or int(_d(i).state) != C.WATCH_KEEPING:
		return
	_d(i).state = C.WATCH_BROKEN
	var woke := 0
	for o in M().monsters:
		if o.alive and not o.aggro and M().district_index(o.x, o.y) == i:
			o.aggro = true
			woke += 1
	Game.warn("A shout goes up, and it is taken up all the way down the line.")
	if woke > 0:
		Game.warn("The watch is broken. %d of them are coming." % woke)


static func strongbox(f: Dictionary) -> void:
	var i := M().district_index(f.x, f.y)
	if i < 0 or _d(i).kind != C.District.WATCH:
		return
	if int(_d(i).state) == C.WATCH_BROKEN:
		Game.msg("The box is open and there is nothing in it worth the noise you made.")
		return
	if int(_d(i).state) == C.WATCH_TAKEN:
		return
	var h := Game.hero
	var before := h.gold_bonus_pct
	h.gold_bonus_pct = mini(h.gold_bonus_pct + C.WATCH_FIND_PCT, C.WATCH_FIND_CAP)
	_d(i).state = C.WATCH_TAKEN
	f.used = true
	if h.gold_bonus_pct > before:
		Game.good("Ledgers, routes, and where the good floors are. You are +%d%% on coin found," % (h.gold_bonus_pct - before))
		Game.good("and nobody has looked up. Walk out the way you came.")
	else:
		Game.msg("More of what you already know. You take it anyway, quietly.")


## How far a monster at (x,y) notices you, after the district has its say.
static func aggro_radius(x: int, y: int, r: int) -> int:
	var i := M().district_index(x, y)
	if i < 0:
		return r
	var d := _d(i)
	if d.kind == C.District.WATCH and int(d.state) != C.WATCH_BROKEN:
		return mini(r, C.WATCH_AGGRO_RADIUS)
	if d.kind == C.District.CHAPEL:
		return mini(r, C.CHAPEL_AGGRO_RADIUS)
	return r


## The stone wood: what you cannot see gets the first blow, and gets it twice.
static func ambush(mo: Monster) -> bool:
	return M().district_at(mo.x, mo.y) == C.District.PETRIFIED and not M().is_visible(mo.x, mo.y)


# ---- the assembly line ---------------------------------------------------------
static func console() -> void:
	if Game.keys < C.CONSOLE_KEY_COST:
		Game.msg("The console wants %d keys turned at once. You have %d." % [C.CONSOLE_KEY_COST, Game.keys])
		return
	if not Party.grant_golem(Game.depth):
		return
	Game.keys -= C.CONSOLE_KEY_COST
	Game.good("Three keys turn together and the line runs for the first time in an age.")


static func _belt_flow(x: int, y: int) -> Vector2i:
	if M().t(x, y) != C.Tile.BELT:
		return Vector2i.ZERO
	var i := M().district_index(x, y)
	if i < 0 or _d(i).kind != C.District.ASSEMBLY:
		return Vector2i.ZERO
	return Vector2i(1 if ((y - int(_d(i).y)) / 3) % 2 == 0 else -1, 0)


static func _current_flow(x: int, y: int) -> Vector2i:
	if M().t(x, y) != C.Tile.CURRENT:
		return Vector2i.ZERO
	var i := M().district_index(x, y)
	if i < 0 or _d(i).kind != C.District.AQUEDUCT:
		return Vector2i.ZERO
	return Vector2i(int(_d(i).fdx), int(_d(i).fdy))


## Carry something up to `dist` squares; never onto stairs, down a hole, or
## into anybody. Returns how far it went.
static func _carry(pos: Vector2i, dir: Vector2i, dist: int, self_obj) -> Vector2i:
	var m := M()
	var p := pos
	for i in dist:
		var n := p + dir
		if not m.walkable_player(n.x, n.y):
			break
		if m.t(n.x, n.y) in [C.Tile.STAIRS_DOWN, C.Tile.STAIRS_UP, C.Tile.PIT]:
			break
		var occ := m.monster_at(n.x, n.y)
		if occ and occ != self_obj:
			break
		var b := Party.body_at(n.x, n.y)
		if b and b != self_obj:
			break
		p = n
	return p


static func _convey() -> void:
	var m := M()
	var moving := false
	for d in m.districts:
		if d.kind == C.District.ASSEMBLY or d.kind == C.District.AQUEDUCT:
			moving = true
			break
	if not moving:
		return
	for h in Game.party:
		if not Party.is_up(h):
			continue
		var belt := _belt_flow(h.x, h.y)
		var cur := _current_flow(h.x, h.y)
		for spec in [[belt, C.BELT_PUSH, "The belt grinds you against the stop."],
				[cur, C.CURRENT_PUSH, "The current shoves you against the channel wall."]]:
			if spec[0] == Vector2i.ZERO:
				continue
			var from: Vector2i = h.pos()
			var to := _carry(from, spec[0], spec[1], h)
			if to != from:
				h.x = to.x
				h.y = to.y
				Game.emit_fx({"type": "move", "who": h, "from": from})
				if h == Game.hero and spec[1] == C.CURRENT_PUSH:
					Game.msg("The current takes you %d tiles." % maxi(absi(to.x - from.x), absi(to.y - from.y)), Color8(150, 200, 255))
			elif h == Game.hero:
				Game.msg(spec[2], Color8(180, 180, 210))
	for mo in m.monsters.duplicate():
		if not mo.alive:
			continue
		var under := m.tiles[mo.y * m.w + mo.x]
		if under != C.Tile.BELT and under != C.Tile.CURRENT:
			continue
		for dir in [_belt_flow(mo.x, mo.y), _current_flow(mo.x, mo.y)]:
			if dir == Vector2i.ZERO:
				continue
			var push := C.BELT_PUSH if m.t(mo.x, mo.y) == C.Tile.BELT else C.CURRENT_PUSH
			var to := _carry(mo.pos(), dir, push, mo)
			if to != mo.pos():
				m.move_monster(mo, to.x, to.y)


# ---- the arena -------------------------------------------------------------------
## Step onto the sand and it starts: ten waves, and after the fifth the gate
## opens again, so pressing on for the prize is a decision made five times.
static func _arena_seal(d: Dictionary, shut: bool) -> void:
	var m := M()
	var cy: int = d.y + d.h / 2
	for x in range(d.x, d.x + d.w):
		if not m.inb(x, cy):
			continue
		var tt := m.t(x, cy)
		if shut and tt == C.Tile.FLOOR and (x < d.x + 2 or x > d.x + d.w - 3) and not Party.body_at(x, cy):
			m.set_t(x, cy, C.Tile.SEALED_DOOR)
		elif not shut and tt == C.Tile.SEALED_DOOR:
			m.set_t(x, cy, C.Tile.FLOOR)


static func _spawn_in(d: Dictionary, mo_for: Callable) -> Monster:
	var m := M()
	for tries in 60:
		var x: int = d.x + 1 + rnd(d.w - 2)
		var y: int = d.y + 1 + rnd(d.h - 2)
		if m.t(x, y) != C.Tile.FLOOR or m.monster_at(x, y) or Party.body_at(x, y):
			continue
		var mo: Monster = mo_for.call(x, y)
		mo.aggro = true
		m.add_monster(mo)
		return mo
	return null


static func _arena_wave(d: Dictionary, wave: int) -> void:
	for k in C.ARENA_BASE_ENEMIES + wave:
		# each wave fights as if the floor were deeper than it is
		_spawn_in(d, func(x, y): return Monster.for_floor(mini(Game.depth + wave * 2, C.MAX_FLOOR - 1), x, y, Game.rng))


static func _arena_tick() -> void:
	var m := M()
	var s := m.dstate
	var here := m.district_index(Game.hero.x, Game.hero.y)
	if int(s.arena_wave) == 0:
		if here < 0 or _d(here).kind != C.District.ARENA or s.arena_paid:
			return
		s.arena_district = here
		s.arena_wave = 1
		_arena_seal(_d(here), true)
		_arena_wave(_d(here), 1)
		Game.warn("The gates come down. Something is let in at the far end.")
		Game.msg("Wave 1 of %d. They open again after %d." % [C.ARENA_WAVES, C.ARENA_FREE_AFTER], Gfx.GOLD)
		return
	if int(s.arena_district) < 0 or int(s.arena_wave) > C.ARENA_WAVES:
		return
	var d := _d(int(s.arena_district))
	if _live_in(d) > 0:
		return
	if int(s.arena_wave) >= C.ARENA_WAVES:
		_arena_seal(d, false)
		s.arena_wave = C.ARENA_WAVES + 1
		if not s.arena_paid:
			s.arena_paid = true
			Rules.grant_random_set_piece(m.biome)
			var purse := Game.gain_gold(400 + Game.depth * 60)
			Game.writs += 1
			Game.good("The last of them goes down. The gates grind open.")
			Game.good("A chest comes up through the sand: set gear, %d gold, and a writ of training." % purse)
		return
	s.arena_wave = int(s.arena_wave) + 1
	if int(s.arena_wave) > C.ARENA_FREE_AFTER:
		_arena_seal(d, false)
	_arena_wave(d, int(s.arena_wave))
	if int(s.arena_wave) == C.ARENA_FREE_AFTER + 1:
		Game.msg("The gates lift. You may walk out -- or take wave %d." % int(s.arena_wave), Gfx.GOLD)
	else:
		Game.msg("Wave %d of %d." % [int(s.arena_wave), C.ARENA_WAVES], Gfx.GOLD)


# ---- the barrow ------------------------------------------------------------------
## The arena's shape, a different question: the enemies are the runs you have
## already lost, exactly as strong as they were.
static func _barrow_seal(d: Dictionary, shut: bool) -> void:
	var m := M()
	for y in range(d.y, d.y + d.h):
		for x in range(d.x, d.x + d.w):
			if not (x == d.x or x == d.x + d.w - 1 or y == d.y or y == d.y + d.h - 1):
				continue
			if not m.inb(x, y) or Party.body_at(x, y):
				continue
			var tt := m.t(x, y)
			if shut and (tt == C.Tile.FLOOR or tt == C.Tile.BRIDGE):
				m.set_t(x, y, C.Tile.SEALED_DOOR)
			elif not shut and tt == C.Tile.SEALED_DOOR:
				m.set_t(x, y, C.Tile.FLOOR)


static func _raise(d: Dictionary, wave: int) -> void:
	var ring := Game.fallen()
	if wave < 1 or wave > ring.size():
		return
	var fr: Dictionary = ring[wave - 1]
	var mo := _spawn_in(d, func(x, y):
		var g := Monster.new()
		g.name = str(fr.get("name", "the nameless"))
		g.x = x
		g.y = y
		g.maxhp = maxi(int(fr.get("maxhp", 10)), 10)
		g.hp = g.maxhp
		g.atk = maxi(int(fr.get("atk", 1)), 1)
		g.def = maxi(int(fr.get("def", 0)), 0)
		g.is_elite = true
		g.xp_reward = 0          # killing what you used to be is not training
		g.gold_reward = int(fr.get("gold", 0))
		g.apparition = 1
		g.look = int(fr.get("look", 0))
		g.family = C.tier_for_floor(Game.depth)
		return g)
	if mo:
		Game.warn("%s, who got as far as floor %d, stands up." % [mo.name, int(fr.get("floor", 1))])


static func _barrow_tick() -> void:
	var m := M()
	var s := m.dstate
	var here := m.district_index(Game.hero.x, Game.hero.y)
	if int(s.gauntlet_wave) == 0:
		if here < 0 or _d(here).kind != C.District.GAUNTLET or s.gauntlet_paid:
			return
		if Game.fallen().size() < C.GAUNTLET_MIN_GHOSTS:
			return
		s.gauntlet_district = here
		s.gauntlet_wave = 1
		_barrow_seal(_d(here), true)
		_raise(_d(here), 1)
		Game.warn("The door goes down behind you. This is not a tomb, it is a queue.")
		return
	if int(s.gauntlet_district) < 0:
		return
	var d := _d(int(s.gauntlet_district))
	if _live_in(d) > 0:
		return
	var limit := mini(C.GAUNTLET_MAX_GHOSTS, Game.fallen().size())
	if int(s.gauntlet_wave) >= limit:
		if int(s.gauntlet_wave) == limit:
			_barrow_seal(d, false)
			s.gauntlet_wave = limit + 1
			if not s.gauntlet_paid:
				s.gauntlet_paid = true
				Game.good("The last of them lies down again. The door grinds up.")
				Game.msg("You are the only one of you still walking.", Gfx.GREY)
		return
	s.gauntlet_wave = int(s.gauntlet_wave) + 1
	_raise(d, int(s.gauntlet_wave))


# ---- the eye -----------------------------------------------------------------------
## One quarter is safe and the rest is not, and the shelter moves on a rhythm
## you can learn: derived from the floor's turn count, not stored.
static func eye_safe_quarter() -> int:
	return (M().turns_on_floor / C.EYE_ROTATE_EVERY) % 4


static func eye_sheltered(d: Dictionary, x: int, y: int) -> bool:
	var q := (2 if y >= d.y + d.h / 2 else 0) + (1 if x >= d.x + d.w / 2 else 0)
	return q == eye_safe_quarter()


static func _eye_tick() -> void:
	var m := M()
	if m.turns_on_floor % C.EYE_STRIKE_EVERY != 0:
		return
	for i in m.districts.size():
		var d := _d(i)
		if d.kind != C.District.EYE:
			continue
		for h in Game.party.duplicate():
			if not Party.is_up(h) or m.district_index(h.x, h.y) != i or eye_sheltered(d, h.x, h.y):
				continue
			var dmg := _hazard(h, C.EYE_DAMAGE)
			if h == Game.hero:
				Game.warn("The ceiling turns over you -- %d damage. The quiet is elsewhere." % dmg)
			Rules.body_take_damage(h, dmg, "the eye of the storm")
		for mo in m.monsters.duplicate():
			if mo.alive and _inside(d, mo.x, mo.y) and m.district_index(mo.x, mo.y) == i and not eye_sheltered(d, mo.x, mo.y):
				Rules.monster_take_damage(mo, C.EYE_DAMAGE)


# ---- the mirror ----------------------------------------------------------------------
static func _mirror_tick() -> void:
	var m := M()
	var h := Game.hero
	var i := m.district_index(h.x, h.y)
	if i < 0 or _d(i).kind != C.District.MIRROR or int(_d(i).state) != 0:
		return
	var mo := _spawn_in(_d(i), func(x, y):
		var g := Monster.new()
		g.name = "%s in the glass" % h.name
		g.x = x
		g.y = y
		g.maxhp = maxi(1, h.maxhp * C.MIRROR_HP_PCT / 100)
		g.hp = g.maxhp
		g.atk = maxi(1, h.eff_atk() * C.MIRROR_DAMAGE_PCT / 100)
		g.def = h.eff_def()
		g.is_elite = true
		g.xp_reward = 0          # levelling off your own reflection is a loop
		g.gold_reward = 0
		g.apparition = 2
		g.look = h.look
		g.family = C.tier_for_floor(Game.depth)
		return g)
	if mo:
		_d(i).state = 1
		Game.warn("The glass keeps pace, and then steps out of it.")


# ---- the rods and the vents ------------------------------------------------------------
## Telegraphed, timed area damage on a tile you can see. Anything touching a
## struck rod is simply gone: lure something onto one and the floor does the work.
static func _storm_tick() -> void:
	var m := M()
	var s := m.dstate
	var any := false
	for d in m.districts:
		if d.kind == C.District.STORM or d.kind == C.District.GEOTHERMAL:
			any = true
			break
	if not any:
		return
	if int(s.storm_countdown) > 0:
		s.storm_countdown = int(s.storm_countdown) - 1
	if int(s.storm_countdown) == 0 and int(s.storm_x) >= 0:
		var at := Vector2i(int(s.storm_x), int(s.storm_y))
		var vent: bool = s.storm_is_vent
		s.storm_x = -1
		s.storm_y = -1
		if m.is_visible(at.x, at.y):
			Game.emit_fx({"type": "burst", "at": at, "radius": C.STORM_BLAST_RADIUS,
				"color": Color8(255, 150, 60) if vent else Color8(170, 210, 255)})
			Sfx.play("crit")
		for h in Game.party.duplicate():
			if not Party.is_up(h):
				continue
			if (h.x - at.x) * (h.x - at.x) + (h.y - at.y) * (h.y - at.y) <= C.STORM_BLAST_RADIUS * C.STORM_BLAST_RADIUS:
				var dmg := _hazard(h, C.STORM_DAMAGE)
				if h == Game.hero:
					Game.warn(("The vent lets go and you are far too close -- %d damage." if vent
						else "The rod takes the bolt and you are far too close -- %d damage.") % dmg)
				Rules.body_take_damage(h, dmg, "the vent" if vent else "the lightning")
		var killed := 0
		for mo in m.monsters.duplicate():
			if mo.alive and (mo.x - at.x) * (mo.x - at.x) + (mo.y - at.y) * (mo.y - at.y) <= 2:
				Rules.monster_take_damage(mo, mo.hp)
				killed += 1
		if killed > 0 and m.is_visible(at.x, at.y):
			Game.good(("The blast takes %d of them with it." if vent else "The arc earths itself through %d of them.") % killed)
	if int(s.storm_x) < 0:
		for tries in 60:
			var d: Dictionary = m.districts[rnd(m.districts.size())]
			var vent: bool = d.kind == C.District.GEOTHERMAL
			if d.kind != C.District.STORM and not vent:
				continue
			var x: int = d.x + rnd(d.w)
			var y: int = d.y + rnd(d.h)
			if m.t(x, y) != (C.Tile.VENT if vent else C.Tile.ROD):
				continue
			s.storm_x = x
			s.storm_y = y
			s.storm_is_vent = vent
			s.storm_countdown = C.STORM_STRIKE_EVERY
			break
	elif int(s.storm_countdown) == 1 and m.is_visible(int(s.storm_x), int(s.storm_y)):
		Game.warn("A vent starts to draw. Whatever is beside it has one turn." if s.storm_is_vent
			else "A rod begins to sing. Whatever is beside it has one turn.")


## Once a turn, underground, after the monsters have moved.
static func tick() -> void:
	if Game.depth <= 0 or Game.game_over:
		return
	_arena_tick()
	_barrow_tick()
	_eye_tick()
	_mirror_tick()
	_convey()
	_storm_tick()


## Crossing into a district says what it is called -- and for the proving
## ground, what the rule is.
static func announce(prev: int) -> void:
	var m := M()
	var i := m.district_index(Game.hero.x, Game.hero.y)
	if i == prev or i < 0:
		return
	var d := _d(i)
	var line: String = "You cross into the %s." % C.DISTRICT_NAMES[d.kind].to_lower()
	if d.kind == C.District.PROVING:
		line = "You step onto the proving ground: %s." % C.PROVE_RULES[int(d.state)]
	elif d.kind == C.District.WATCH and int(d.state) == C.WATCH_KEEPING:
		line = "You cross into the watch. They are awake. Start nothing."
	Game.msg(line, C.DISTRICT_TINTS[d.kind].lightened(0.3))


# ---- pits --------------------------------------------------------------------------
## A hole in the mine floor: a floor skipped, paid for in blood.
static func fall() -> void:
	var h := Game.hero
	var dmg := _hazard(h, maxi(h.maxhp * C.PIT_FALL_HP_PCT / 100, C.PIT_MIN_DAMAGE))
	Game.warn("The floor is not a floor. You go down hard -- %d damage." % dmg)
	Rules.hero_take_damage(dmg, "a fall")
	if Game.game_over:
		return
	Game.save_run()
	Game.go_to_floor(mini(Game.depth + 1, C.MAX_FLOOR), true, func():
		Game.warn("You land on floor %d, in the dark, somewhere you did not choose." % Game.depth)
		Game.emit_fx({"type": "warp"}))


# ---- work ----------------------------------------------------------------------------
static func material_tier(floor_num: int) -> int:
	if floor_num >= 86:
		return C.Mat.DIAMOND       # the Abyss
	if floor_num >= 36:
		return C.Mat.PLATINUM      # the Ruins and the Wastes
	return C.Mat.SCRAP


static func gain_material(tier: int, count: int, value_each: int) -> void:
	Game.mats[tier] = int(Game.mats[tier]) + count
	Game.mat_value[tier] = int(Game.mat_value[tier]) + value_each * count \
		* (2 if Game.depth > 0 and Game.depth <= Game.gold_boon_until else 1)


static func work_at(x: int, y: int) -> int:
	var m := M()
	if Game.depth <= 0 or not m.inb(x, y):
		return C.Work.NONE
	match m.t(x, y):
		C.Tile.ORE: return C.Work.MINE
		C.Tile.ROD: return C.Work.HARVEST_ROD
		C.Tile.VENT: return C.Work.HARVEST_VENT
		C.Tile.DECOR:
			# rubble in an old quarter is a machine worth stripping; in a
			# boneyard, a dead golem worth opening; anywhere else, rubble
			match m.district_at(x, y):
				C.District.RUINS: return C.Work.SALVAGE
				C.District.BONEYARD: return C.Work.CORE
	return C.Work.NONE


## Start a job on your square or any of the eight around it.
static func work_begin() -> bool:
	var h := Game.hero
	for d in [Vector2i.ZERO] + C.DIRS8:
		var x: int = h.x + d.x
		var y: int = h.y + d.y
		var kind := work_at(x, y)
		if kind == C.Work.NONE:
			continue
		h.work_kind = kind
		h.work_left = C.WORK_TURNS[kind]
		h.work_x = x
		h.work_y = y
		Game.msg("You set to %s -- %d turns, and you cannot answer for any of them." % [C.WORK_VERBS[kind], h.work_left], Color8(220, 200, 150))
		return true
	Game.msg("Nothing here worth the time. (Ore veins, rods, vents, and wrecks in the old quarters and boneyards.)", Gfx.GREY)
	return false


static func work_abandon(why: String) -> void:
	var h := Game.hero
	if h.work_left <= 0:
		return
	h.work_left = 0
	h.work_kind = C.Work.NONE
	Game.warn(why)


## One turn of the job. Anything that hurts you ruins it: you have to have
## made the ground safe before committing to ten turns.
static func work_step() -> void:
	var h := Game.hero
	var hp_before := h.hp
	h.work_left -= 1
	Rules.end_turn()
	if Game.game_over or Game.hero != h or Game.depth == 0:
		h.work_left = 0
		h.work_kind = C.Work.NONE
		return
	if h.hp < hp_before:
		work_abandon("Something gets a hand on you. The work is ruined.")
	elif h.work_left <= 0:
		_work_finish()


## Paid on completion, never in instalments.
static func _work_finish() -> void:
	var h := Game.hero
	var m := M()
	var kind := h.work_kind
	var at := Vector2i(h.work_x, h.work_y)
	h.work_kind = C.Work.NONE
	h.work_left = 0
	var tier := material_tier(Game.depth)
	var worth := ItemsData.junk_worth(Game.depth)
	Sfx.play("coin")
	match kind:
		C.Work.HARVEST_ROD, C.Work.HARVEST_VENT:
			# the rod is consumed, and stops being a lightning target too
			m.set_t(at.x, at.y, C.Tile.FLOOR)
			if int(m.dstate.storm_x) == at.x and int(m.dstate.storm_y) == at.y:
				m.dstate.storm_x = -1
				m.dstate.storm_y = -1
			var paid := Game.gain_gold(60 + Game.depth * 12)
			Game.good(("The crust comes away in sheets. (+%d gold)" if kind == C.Work.HARVEST_VENT
				else "The ore comes free. (+%d gold)") % paid)
		C.Work.MINE:
			m.set_t(at.x, at.y, C.Tile.WALL)
			var got := 4 + rnd(5)
			gain_material(tier, got, worth)
			Game.good("The vein gives up %d %s." % [got, C.MAT_NAMES[tier]])
		C.Work.SALVAGE:
			m.set_t(at.x, at.y, C.Tile.FLOOR)
			gain_material(tier, 6, worth)
			var paid := Game.gain_gold(40 + Game.depth * 8)
			Game.good("You strip the wreck out: 6 %s, and %d gold." % [C.MAT_NAMES[tier], paid])
		C.Work.CORE:
			m.set_t(at.x, at.y, C.Tile.FLOOR)
			var got := 10 + rnd(7)
			gain_material(tier, got, worth)
			var paid := Game.gain_gold(90 + Game.depth * 14)
			Game.good("The core comes out whole: %d %s, and %d gold." % [got, C.MAT_NAMES[tier], paid])
	Game.emit_fx({"type": "pickup", "at": h.pos(), "text": "+" + C.MAT_NAMES[tier], "color": Gfx.CYAN})
