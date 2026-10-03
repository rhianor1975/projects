extends Node
## Third screenshot pass: the Tavern, and a hired party at work below.

var main: Node
var out := "user://shots"
var n := 40


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


func _run() -> void:
	Game.new_run(12, "Kael", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 77)
	Game.gold = 1200000
	main._start_game()
	# walk to the Tavern door: west of the plaza
	Game.hero.x = 5
	Game.hero.y = 11
	main.view.snap()
	await _wait(0.4)
	await _shot("tavern-door")
	main._open_building("tavern")
	await _wait(0.2)
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	await _shot("tavern")
	await _key(KEY_ENTER)
	await _wait(0.1)
	await _key(KEY_DOWN)
	await _key(KEY_ENTER)
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	await _key(KEY_ENTER)
	await _key(KEY_UP)
	await _key(KEY_UP)
	await _wait(0.2)
	await _shot("tavern-hired")
	await _key(KEY_ESCAPE)
	for b in Game.party:
		b.maxhp += 300
		b.hp = b.maxhp
	Game.enter_floor(2)
	main.view.snap()
	await _wait(0.5)
	await _shot("party-arrives")
	# walk until the party is in a fight
	var shots := 0
	for i in 600:
		AutoExplore.step()
		main._after_action()
		await get_tree().process_frame
		while main.view.busy():
			await get_tree().process_frame
		var near := 0
		for mo in Game.map.monsters:
			if mo.alive and Game.map.is_visible(mo.x, mo.y) and maxi(absi(mo.x - Game.hero.x), absi(mo.y - Game.hero.y)) <= 6:
				near += 1
		var vis := Party.others().filter(func(b): return Game.map.is_visible(b.x, b.y) and maxi(absi(b.x - Game.hero.x), absi(b.y - Game.hero.y)) <= 7).size()
		if near >= 2 and vis >= 2 and i > 30:
			await _wait(0.05)
			await _shot("party-fight%d" % shots)
			shots += 1
			if shots >= 3:
				break
			for k in 25:
				AutoExplore.step()
				main._after_action()
				await get_tree().process_frame
	# take over somebody else
	await _key(KEY_P)
	await _wait(0.4)
	await _shot("switched")
