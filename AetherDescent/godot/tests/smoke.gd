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
	# generation sweep: every floor is connected stairs-to-stairs
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
	print("spells in pool: ", SpellBook.count())
	print("done in %d ms, %d failures" % [Time.get_ticks_msec() - t0, failures])
	get_tree().quit(1 if failures else 0)
