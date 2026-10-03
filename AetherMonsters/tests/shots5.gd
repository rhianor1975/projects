extends Node
## Fifth screenshot pass: the later wild districts.

var main: Node
var out := "user://shots"
var n := 90


func _ready() -> void:
	Game.persist = false
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		out = args[0]
	if "classic" in args:
		Gfx.set_art("classic")
	main = load("res://scenes/main.tscn").instantiate()
	add_child(main)
	await _run()
	get_tree().quit()


func _wait(sec: float) -> void:
	await get_tree().create_timer(sec).timeout


func _key(code: int) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.pressed = true
	Input.parse_input_event(e)
	await get_tree().process_frame
	var u := InputEventKey.new()
	u.keycode = code
	Input.parse_input_event(u)
	await get_tree().process_frame


func _shot(name: String) -> void:
	await get_tree().process_frame
	var img := get_viewport().get_texture().get_image()
	n += 1
	img.save_png("%s/%02d-%s.png" % [out, n, name])
	print("shot ", name)


func _floor_with(kind: int) -> int:
	for f in range(4, 99):
		var m: GameMap = MapGen.generate(f, C.WORLD_DIMS[C.WorldSize.HALLS], 5000 + f * 13 + kind, C.Difficulty.NORMAL)
		for i in m.districts.size():
			var d: Dictionary = m.districts[i]
			if d.kind != kind or d.w < 22:
				continue
			var c := Vector2i(d.x + d.w / 2, d.y + d.h / 2)
			for r in 6:
				for y in range(c.y - r, c.y + r + 1):
					for x in range(c.x - r, c.x + r + 1):
						if m.t(x, y) == C.Tile.FLOOR and not m.monster_at(x, y):
							Game.map = m
							Game.depth = f
							Game.hero.x = x
							Game.hero.y = y
							for mo in m.monsters:
								if absi(mo.x - x) < 9 and absi(mo.y - y) < 6:
									mo.alive = false
							m.remove_dead()
							for yy in range(d.y - 2, d.y + d.h + 2):
								for xx in range(d.x - 2, d.x + d.w + 2):
									if m.inb(xx, yy):
										m.seen[yy * m.w + xx] = 1
							Rules.refresh_vision()
							main.view.snap()
							return i
	return -1


func _run() -> void:
	Game.fallen().append({"name": "Old Kael", "floor": 12, "maxhp": 900, "atk": 9, "def": 3, "gold": 77, "look": 0})
	Game.new_run(9, "Sable", C.Difficulty.NORMAL, C.WorldSize.HALLS, 66)
	main._start_game()
	Game.hero.maxhp = 9000
	Game.hero.hp = 9000
	for kind in [C.District.GAUNTLET, C.District.MIRROR, C.District.GEOTHERMAL]:
		if _floor_with(kind) < 0:
			continue
		Game.log_lines = []
		Districts.announce(-1)
		# let it do its thing for a few turns
		for k in 4:
			Rules.wait_turn()
			main._after_action()
			await _wait(0.05)
		if kind == C.District.STORM or kind == C.District.GEOTHERMAL:
			for k in 6:
				if int(Game.map.dstate.storm_countdown) <= 2 and int(Game.map.dstate.storm_x) >= 0:
					break
				Rules.wait_turn()
				main._after_action()
			if int(Game.map.dstate.storm_x) >= 0:
				Game.hero.x = int(Game.map.dstate.storm_x) - 1
				Game.hero.y = int(Game.map.dstate.storm_y) + 2
				Rules.refresh_vision()
				main.view.snap()
		# stand where the apparition can be seen
		for mo in Game.map.monsters:
			if mo.look >= 0:
				for dd in [Vector2i(-2, 0), Vector2i(2, 0), Vector2i(0, 2), Vector2i(0, -2), Vector2i(-2, 1), Vector2i(2, 1)]:
					var p: Vector2i = mo.pos() + dd
					if Game.map.walkable_player(p.x, p.y) and not Game.map.monster_at(p.x, p.y):
						Game.hero.x = p.x
						Game.hero.y = p.y
						break
				Rules.refresh_vision()
				main.view.snap()
				break
		await _wait(0.5)
		await _shot(C.DISTRICT_NAMES[kind].to_lower().replace(" ", "-"))
