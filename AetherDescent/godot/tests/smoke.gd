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
