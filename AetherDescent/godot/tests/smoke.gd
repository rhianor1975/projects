extends Node
## Headless smoke run: make a character, walk down several floors on
## auto-explore, and check the invariants the rules depend on.
##   godot --headless --path . tests/smoke.tscn

func _ready() -> void:
	Game.persist = false
	var game = Game
	var failures := 0
	var t0 := Time.get_ticks_msec()
	for cls in [0, 2, 5, 13, 40]:
		game.new_run(cls, "Tester%d" % cls, C.Difficulty.NORMAL, C.WorldSize.SHAFT, 4242 + cls)
		game.hero.maxhp *= 20      # survive long enough to see several floors
		game.hero.hp = game.hero.maxhp
		game.enter_floor(1)
		AutoExplore.reset()
		var steps := 0
		var floors := 1
		while steps < 3000 and not game.game_over and game.depth > 0:
			var before: int = game.depth
			var why := AutoExplore.step()
			steps += 1
			if game.depth != before:
				floors += 1
				AutoExplore.reset()
			if why != "":
				print("  stopped: ", why)
				break
			# invariants
			var m: GameMap = game.map
			if game.depth > 0:
				if not m.walkable_player(game.hero.x, game.hero.y):
					print("FAIL hero in a wall at ", game.hero.pos()); failures += 1; break
				var occupied := {}
				for mo in m.monsters:
					if mo.alive:
						var k: int = mo.y * m.w + mo.x
						if occupied.has(k):
							print("FAIL two monsters on one tile"); failures += 1
						occupied[k] = 1
						if mo.pos() == game.hero.pos():
							print("FAIL monster on the hero"); failures += 1
		print("class %d (%s): %d steps, reached floor %d, level %d, gold %d, hp %d/%d" % [cls,
			game.hero.class_name_str(), steps, game.depth, game.hero.level, game.gold, game.hero.hp, game.hero.maxhp])
	# a hired party: everyone takes turns, nobody shares a tile, kills pay
	game.new_run(7, "Captain", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 5150)
	game.gold = 20000000
	var names := {}
	for i in Party.TAVERN_ROSTER:
		var nm: String = Party.candidate(i).name
		if names.has(nm):
			print("FAIL two candidates called ", nm); failures += 1
		names[nm] = 1
	for i in [0, 3, 6, 9, 12, 15]:
		Party.hire(i)
	if Party.count() != Party.MAX_COMPANIONS:
		print("FAIL party of %d after six hires" % Party.count()); failures += 1
	var paid: int = 20000000 - game.gold
	if paid != 1000 + 10000 + 100000 + 1000000 + 10000000:
		print("FAIL hire prices came to ", paid); failures += 1
	Party.release(12)
	if Party.count() != 4 or Party.candidate(12).name == Party.by_roster(0).name:
		print("FAIL release"); failures += 1
	for b in game.party:       # everybody lives long enough to be watched
		b.maxhp *= 20
		b.hp = b.maxhp
	game.enter_floor(1)
	AutoExplore.reset()
	var hire_kills := 0
	var psteps := 0
	var switched := false
	while psteps < 2500 and not game.game_over and game.depth > 0 and game.depth < 5:
		var before: int = game.depth
		var why := AutoExplore.step()
		psteps += 1
		if game.depth != before:
			AutoExplore.reset()
		if why != "":
			print("  party stopped: ", why)
			break
		if psteps == 400 and not switched:
			switched = Party.switch_next()
			AutoExplore.reset()
		var m: GameMap = game.map
		var taken := {}
		for b in game.party:
			if not Party.is_up(b):
				continue
			var k: int = b.y * m.w + b.x
			if taken.has(k):
				print("FAIL two of the party on one tile at ", b.pos()); failures += 1
			taken[k] = 1
			if not m.walkable_player(b.x, b.y):
				print("FAIL %s in a wall" % b.name); failures += 1
			var mo := m.monster_at(b.x, b.y)
			if mo:
				print("FAIL monster on %s" % b.name); failures += 1
		if failures > 0:
			break
	for b in game.party:
		hire_kills += b.kills
	print("party: %d steps, floor %d, %d standing, hires' kills %d, driving %s, gold %d" % [psteps, game.depth,
		game.party.filter(func(b): return Party.is_up(b)).size(), hire_kills, game.hero.name, game.gold])
	if hire_kills == 0:
		print("FAIL the hires never killed anything"); failures += 1
	# every body keeps its own map: switching shows theirs, and back shows yours
	var mine: int = game.map.seen.count(1)
	var me: Hero = game.hero
	var other: Hero = Party.others()[0] if not Party.others().is_empty() else null
	if other:
		var theirs: int = game.map.memory(other.uid).count(1)
		Party.switch_next()
		while game.hero != other:
			Party.switch_next()
		if theirs == 0 or game.map.seen.count(1) < theirs:
			print("FAIL switching did not bring their map (%d, knew %d)" % [game.map.seen.count(1), theirs]); failures += 1
		while game.hero != me:
			Party.switch_next()
		if game.map.seen.count(1) < mine:
			print("FAIL switching back lost your map (%d < %d)" % [game.map.seen.count(1), mine]); failures += 1
		print("maps: you knew %d tiles, %s knew %d" % [mine, other.name, theirs])
	var psnap: Dictionary = JSON.parse_string(JSON.stringify(game.snapshot()))
	var driving: String = game.hero.name
	game.restore(psnap)
	if game.party.size() != psnap.party.size() or game.hero.name != driving:
		print("FAIL party save round trip"); failures += 1
	# the bounty board: every kind pays when it should and fails when it should
	game.new_run(20, "Bounty", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 31)
	game.deepest_floor = 3
	game.give("Recall Charm", 3)
	var g0: int = game.gold
	Quests.offer("timed")
	var qd := int(game.quest.depth)
	game.enter_floor(qd)
	if Quests.active() or game.gold <= g0:
		print("FAIL timed bounty did not pay on arrival"); failures += 1
	game.enter_town(Town.START)
	Quests.offer("timed")
	game.turns += 100000
	game.enter_floor(int(game.quest.depth))
	if Quests.active():
		print("FAIL expired timed bounty still open"); failures += 1
	game.enter_town(Town.START)
	Quests.offer("norecall")
	qd = int(game.quest.depth)
	game.enter_floor(maxi(1, qd - 1) if qd > 1 else 2)
	Rules.start_recall()
	if not game.quest.get("broken", false):
		print("FAIL a charm did not break the no-recall bounty"); failures += 1
	game.enter_floor(qd)
	if Quests.active():
		print("FAIL broken no-recall bounty still open"); failures += 1
	game.enter_town(Town.START)
	Quests.offer("escort")
	var cl := Quests.client()
	if cl == null or not cl in game.party:
		print("FAIL escort client did not join the party"); failures += 1
	g0 = game.gold
	game.enter_floor(int(game.quest.depth))
	if Quests.active() or Quests.client() != null or game.gold <= g0:
		print("FAIL escort did not pay and release the client"); failures += 1
	game.enter_town(Town.START)
	Quests.offer("escort")
	Quests.client().hp = 0
	Quests.client().alive = false
	Party.reap_fallen()
	game.enter_floor(int(game.quest.depth))
	if Quests.active():
		print("FAIL escort with a dead client still open"); failures += 1
	game.enter_town(Town.START)
	Quests.offer("fetch")
	game.enter_floor(int(game.quest.depth))
	var qi: Array = game.map.items.filter(func(it): return it.kind == "quest")
	if qi.is_empty():
		print("FAIL no fetch item on the bounty floor"); failures += 1
	else:
		game.hero.x = qi[0].x
		game.hero.y = qi[0].y
		Rules._pickup()
		game.enter_town(Town.START)
		g0 = game.gold
		if not Quests.turn_in() or game.gold <= g0:
			print("FAIL fetch bounty did not pay at the board"); failures += 1
	Quests.offer("kill")
	var want: String = game.quest.monster
	game.enter_floor(int(game.quest.depth))
	var mo := Monster.for_floor(game.depth, game.hero.x + 1, game.hero.y, game.rng)
	mo.name = want
	Rules.monster_take_damage(mo, 999999)
	if Quests.active():
		print("FAIL kill bounty did not pay"); failures += 1
	print("bounties checked")
	# the Kitchen, the track and the Bazaar
	game.new_run(0, "Cook", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 8)
	game.enter_floor(3)
	game.hero.maxhp = 99999
	game.hero.hp = 99999
	var cuts := 0
	for i in 300:
		var victim := Monster.for_floor(3, game.hero.x + 1, game.hero.y, game.rng)
		victim.hp = 1
		Rules.hero_attack(victim)
	cuts = Kitchen.meat_total()
	if cuts == 0 or cuts > 120:
		print("FAIL %d cuts from 300 blade kills" % cuts); failures += 1
	var before_cuts := Kitchen.meat_total()
	for i in 200:
		var shot_at := Monster.for_floor(3, game.hero.x + 1, game.hero.y, game.rng)
		Rules.monster_take_damage(shot_at, 99999)      # a spell, a shot: no blade
	if Kitchen.meat_total() != before_cuts:
		print("FAIL meat came off a kill that was not a blade"); failures += 1
	var pat := {"wants": 1, "purse": 1000}
	if not (Kitchen.payout(pat, 2) > Kitchen.payout(pat, 1) and Kitchen.payout(pat, 1) > Kitchen.payout(pat, 0)):
		print("FAIL kitchen payouts out of order"); failures += 1
	game.races_this_visit = 0
	var generous := Kitchen.odds().duplicate()
	game.races_this_visit = 3
	if Kitchen.odds()[5] >= generous[5]:
		print("FAIL the book never soured"); failures += 1
	var pct := Kitchen.bazaar_pct(2)
	if pct != Kitchen.bazaar_pct(2) or pct < Kitchen.BAZAAR_MIN_PCT or pct > Kitchen.BAZAAR_MAX_PCT:
		print("FAIL bazaar price unstable or out of range"); failures += 1
	print("kitchen: %d cuts from 300 blade kills" % cuts)
	failures += _districts(game)
	failures += _dda(game)
	# generation sweep: every floor is connected stairs-to-stairs
	var tw := Time.get_ticks_msec()
	var well: GameMap = MapGen.generate(30, C.WORLD_DIMS[C.WorldSize.WELL], 4321, C.Difficulty.NORMAL)
	if not well.reachable(well.stairs_up, well.stairs_down):
		print("FAIL a Well floor's stairs do not connect"); failures += 1
	print("well floor 30: %d monsters, %d districts, %d ms" % [well.monsters.size(), well.districts.size(), Time.get_ticks_msec() - tw])
	for f in [1, 7, 15, 22, 35, 48, 60, 77, 85, 99, 100]:
		for ws in [C.WorldSize.SHAFT, C.WorldSize.HALLS]:
			var tg := Time.get_ticks_msec()
			var m: GameMap = MapGen.generate(f, C.WORLD_DIMS[ws], 777 + f, C.Difficulty.NORMAL)
			var ok: bool = f == 100 or m.reachable(m.stairs_up, m.stairs_down)
			if not ok:
				print("FAIL floor %d (%s): stairs not connected" % [f, C.WORLD_NAMES[ws]]); failures += 1
			if f == 1 or f == 99:
				print("floor %d %s: %d monsters, %d items, %d features, %d districts, %d ms" % [f, C.WORLD_NAMES[ws],
					m.monsters.size(), m.items.size(), m.features.size(), m.districts.size(), Time.get_ticks_msec() - tg])
	# save/load round trip
	game.new_run(3, "Roundtrip", C.Difficulty.HARD, C.WorldSize.SHAFT, 99)
	game.enter_floor(2)
	var snap: Dictionary = game.snapshot()
	var json := JSON.stringify(snap)
	game.restore(JSON.parse_string(json))
	if game.depth != 2 or game.map.monsters.size() != snap.map.monsters.size():
		print("FAIL save round trip"); failures += 1
	# every door in town opens onto something: a shop with nothing in its list
	# is a screen that forgot to fill itself (the black market once did)
	game.new_run(0, "Shopper", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 5)
	game.hero.level = 12
	var ui = load("res://scenes/main.tscn").instantiate()
	add_child(ui)
	ui._start_game()
	for b in Town.BUILDINGS:
		ui.modal = null
		ui._open_building(b[0])
		if ui.modal is Menu and (ui.modal as Menu).items.is_empty():
			print("FAIL %s opens empty" % b[1]); failures += 1
	ui.modal = null
	ui.queue_free()
	print("spells in pool: ", SpellBook.count())
	print("done in %d ms, %d failures" % [Time.get_ticks_msec() - t0, failures])
	get_tree().quit(1 if failures else 0)



## A floor holding a district of `kind`, made current, with the hero standing
## on an open square inside it. Returns the district's index, or -1.
func _floor_with(game, kind: int) -> int:
	for f in range(1, 99):
		var m: GameMap = MapGen.generate(f, C.WORLD_DIMS[C.WorldSize.HALLS], 3000 + f * 7 + kind, C.Difficulty.NORMAL)
		for i in m.districts.size():
			var d: Dictionary = m.districts[i]
			if d.kind != kind:
				continue
			for y in range(d.y + 1, d.y + d.h - 1):
				for x in range(d.x + 1, d.x + d.w - 1):
					if m.t(x, y) == C.Tile.FLOOR and not m.monster_at(x, y):
						game.map = m
						game.depth = f
						game.hero.x = x
						game.hero.y = y
						# a quiet floor: only what the district itself brings
						for mo in m.monsters:
							mo.alive = false
						m.remove_dead()
						Rules.refresh_vision()
						return i
	return -1


func _districts(game) -> int:
	var fails := 0
	game.fallen().append({"name": "Old Kael", "floor": 12, "maxhp": 60, "atk": 9, "def": 3, "gold": 77, "look": 0})
	game.new_run(0, "Wanderer", C.Difficulty.NORMAL, C.WorldSize.HALLS, 12)
	game.hero.maxhp = 50000
	game.hero.hp = 50000
	# the arena: the gates shut, ten waves, a writ
	var i := _floor_with(game, C.District.ARENA)
	if i < 0:
		print("FAIL no arena generated"); return 1
	Districts.tick()
	if int(game.map.dstate.arena_wave) != 1 or game.map.monsters.is_empty():
		print("FAIL the arena did not start"); fails += 1
	var writs: int = game.writs
	for k in 40:
		for mo in game.map.monsters.duplicate():
			if mo.alive:
				Rules.monster_take_damage(mo, 999999)
		game.map.remove_dead()
		Districts.tick()
	if game.writs != writs + 1 or not game.map.dstate.arena_paid:
		print("FAIL the arena never paid (wave %d)" % int(game.map.dstate.arena_wave)); fails += 1
	# the barrow raises the fallen
	i = _floor_with(game, C.District.GAUNTLET)
	if i >= 0:
		Districts.tick()
		var ghosts: Array = game.map.monsters.filter(func(mo): return mo.apparition == 1)
		if ghosts.is_empty() or ghosts[0].name != "Old Kael":
			print("FAIL the barrow raised nobody"); fails += 1
	else:
		print("FAIL no barrow generated"); fails += 1
	# the mirror
	i = _floor_with(game, C.District.MIRROR)
	Districts.tick()
	if game.map.monsters.filter(func(mo): return mo.apparition == 2).size() != 1:
		print("FAIL the mirror made no copy"); fails += 1
	# the eye strikes whoever is out of the quiet quarter
	i = _floor_with(game, C.District.EYE)
	var d: Dictionary = game.map.districts[i]
	var hurt := false
	for turn in 30:
		game.map.turns_on_floor = turn
		if not Districts.eye_sheltered(d, game.hero.x, game.hero.y):
			var before: int = game.hero.hp
			if turn % C.EYE_STRIKE_EVERY == 0:
				Districts.tick()
				hurt = hurt or game.hero.hp < before
	if not hurt:
		print("FAIL the eye never struck"); fails += 1
	# the watch: the strongbox pays while it holds; a blow breaks it
	i = _floor_with(game, C.District.WATCH)
	var box: Array = game.map.features.filter(func(f): return f.type == C.Feature.STRONGBOX)
	if box.is_empty():
		print("FAIL a watch with no strongbox"); fails += 1
	else:
		var g0: int = game.hero.gold_bonus_pct
		Districts.strongbox(box[0])
		if game.hero.gold_bonus_pct != g0 + C.WATCH_FIND_PCT:
			print("FAIL the strongbox paid nothing"); fails += 1
	# the proving ground's rules
	i = _floor_with(game, C.District.PROVING)
	var rule: int = game.map.districts[i].state
	var melee_ok := Districts.allows(game.hero.x, game.hero.y, C.Act.MELEE)
	var shoot_ok := Districts.allows(game.hero.x, game.hero.y, C.Act.RANGED)
	if shoot_ok or melee_ok != (rule != C.Prove.ARCANE):
		print("FAIL proving ground rule %d not applied" % rule); fails += 1
	# the rods: a strike comes round
	i = _floor_with(game, C.District.STORM)
	var struck := false
	for k in 20:
		Districts.tick()
		struck = struck or int(game.map.dstate.storm_x) >= 0
	if not struck:
		print("FAIL the rods never sang"); fails += 1
	# the aqueduct carries you
	i = _floor_with(game, C.District.AQUEDUCT)
	d = game.map.districts[i]
	var carried := false
	for y in range(d.y, d.y + d.h):
		for x in range(d.x, d.x + d.w):
			if carried or game.map.t(x, y) != C.Tile.CURRENT:
				continue
			game.hero.x = x
			game.hero.y = y
			var before: Vector2i = game.hero.pos()
			Districts.tick()
			carried = game.hero.pos() != before
	if not carried:
		print("FAIL the current carried nobody"); fails += 1
	# the workings: a pit drops you a floor; ore pays materials
	i = _floor_with(game, C.District.SHAFT)
	d = game.map.districts[i]
	var dropped := false
	for y in range(d.y, d.y + d.h):
		for x in range(d.x, d.x + d.w):
			if dropped or game.map.t(x, y) != C.Tile.PIT:
				continue
			var f0: int = game.depth
			game.hero.x = x - 1
			game.hero.y = y
			if game.map.walkable_player(x - 1, y):
				Rules.try_move(1, 0)
				dropped = game.depth == f0 + 1
	if not dropped:
		print("FAIL no fall through a pit"); fails += 1
	var vein := Vector2i(-1, -1)
	for y in game.map.h:
		for x in game.map.w:
			if vein.x < 0 and game.map.t(x, y) == C.Tile.ORE:
				for dd in C.DIRS8:
					if game.map.walkable_player(x + dd.x, y + dd.y) and not game.map.monster_at(x + dd.x, y + dd.y):
						vein = Vector2i(x, y)
						game.hero.x = x + dd.x
						game.hero.y = y + dd.y
						break
	if vein.x < 0:
		print("FAIL no ore vein"); fails += 1
	else:
		for mo in game.map.monsters:
			mo.alive = false
		game.map.remove_dead()
		var before_mats: int = game.mats[0] + game.mats[1] + game.mats[2]
		Districts.work_begin()
		for k in 12:
			if game.hero.work_left > 0:
				Districts.work_step()
		if game.mats[0] + game.mats[1] + game.mats[2] <= before_mats:
			print("FAIL mining yielded nothing"); fails += 1
	if ItemsData.upgrade_material_cost(19) != 0 or ItemsData.upgrade_material_cost(20) != 2 \
			or ItemsData.upgrade_material_tier(55) != C.Mat.DIAMOND:
		print("FAIL the smith's material rungs"); fails += 1
	print("districts checked")
	return fails



func _dda(game) -> int:
	var fails := 0
	game.new_run(0, "Pressed", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 3)
	game.enter_floor(2)       # the Roots
	if Dda.band_help() != 0 or Dda.regen_bonus() != 0:
		print("FAIL the band helped a run that is fine"); fails += 1
	game.hero.hp = game.hero.maxhp / 10
	if Dda.pressure() >= 0 or Dda.band_help() < 1 or Dda.regen_bonus() < 1:
		print("FAIL the Roots did nothing for a run that is drowning"); fails += 1
	game.enter_floor(3)       # leaving a floor at 10% is a rout
	if game.dda_pressure != Dda.ROUTED:
		print("FAIL a mauled floor did not move pressure (%d)" % game.dda_pressure); fails += 1
	game.hero.hp = game.hero.maxhp
	for i in 12:
		game.enter_floor(4 + i)  # cruising
	if game.dda_pressure <= 0:
		print("FAIL cruising never recovered the pressure"); fails += 1
	# the guardian: off by default, on when asked
	game.hero.hp = 0
	if Dda.guardian_catch():
		print("FAIL the guardian acted without being asked"); fails += 1
	game.settings.guardian = true
	game.enter_floor(10)
	game.hero.hp = 0
	if not Dda.guardian_catch() or game.hero.hp != 1:
		print("FAIL the guardian did not catch the blow"); fails += 1
	game.hero.hp = 0
	if Dda.guardian_catch():
		print("FAIL the guardian caught twice on one floor"); fails += 1
	game.settings.guardian = false
	# the Abyss is dark, and Aether-Sense answers it
	game.enter_floor(90)
	if Dda.sight(game.hero, 8) != 5:
		print("FAIL the Abyss took no sight"); fails += 1
	# hearing: what is in range and out of sight is marked, nothing else
	game.enter_floor(4)
	game.hero.hearing_radius = 6
	var due := 0
	for k in 40:
		Rules.refresh_vision()
		due = 0
		for mo in game.map.monsters:
			if mo.alive and not game.map.is_visible(mo.x, mo.y) \
					and (mo.x - game.hero.x) * (mo.x - game.hero.x) + (mo.y - game.hero.y) * (mo.y - game.hero.y) <= 36:
				due += 1
		if due > 0:
			break
		AutoExplore.step()
	if due > 0 and game.map.heard.size() != mini(due, 64):
		print("FAIL heard %d of %d" % [game.map.heard.size(), due]); fails += 1
	for hp in game.map.heard:
		if game.map.is_visible(hp.x, hp.y):
			print("FAIL a seen monster was 'heard'"); fails += 1
			break
	print("dda checked")
	return fails
