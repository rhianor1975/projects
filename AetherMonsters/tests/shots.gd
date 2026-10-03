extends Node
## Drives the real game with key presses and saves screenshots.
##   xvfb-run godot --path . --rendering-driver opengl3 tests/shots.tscn -- OUTDIR

var main: Node
var out := "user://shots"
var n := 0


func _ready() -> void:
	Game.persist = false
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		out = args[0]
	DirAccess.make_dir_recursive_absolute(out)
	main = load("res://scenes/main.tscn").instantiate()
	add_child(main)
	await _run()
	get_tree().quit()


func _wait(sec: float) -> void:
	await get_tree().create_timer(sec).timeout


func _key(code: int, shift := false) -> void:
	var e := InputEventKey.new()
	e.keycode = code
	e.pressed = true
	e.shift_pressed = shift
	Input.parse_input_event(e)
	await get_tree().process_frame
	var u := InputEventKey.new()
	u.keycode = code
	u.pressed = false
	Input.parse_input_event(u)
	await get_tree().process_frame


func _shot(name: String) -> void:
	await get_tree().process_frame
	await get_tree().process_frame
	var img := get_viewport().get_texture().get_image()
	n += 1
	img.save_png("%s/%02d-%s.png" % [out, n, name])
	print("shot ", name, " ", img.get_size())


func _run() -> void:
	await _wait(0.6)
	await _shot("title")
	await _key(KEY_ENTER)              # New Game
	await _wait(0.2)
	await _shot("difficulty")
	await _key(KEY_ENTER)
	await _key(KEY_ENTER)              # world size
	for i in 3:
		await _key(KEY_RIGHT)          # to Arcanist
	await _key(KEY_DOWN)
	await _wait(0.3)
	await _shot("class")
	await _key(KEY_ENTER)
	await _wait(0.2)
	await _shot("name")
	await _key(KEY_ENTER)
	await _wait(0.8)
	await _shot("town")
	main._open_building("armory")
	await _wait(0.3)
	await _shot("armory")
	await _key(KEY_ESCAPE)
	# go down
	Game.hero.maxhp += 300
	Game.hero.hp = Game.hero.maxhp
	Game.enter_floor(1)
	main.view.snap()
	await _wait(0.8)
	await _shot("floor1")
	# explore until something is close, then fight it
	for i in 400:
		var close := false
		for mo in Game.map.monsters:
			if mo.alive and Game.map.is_visible(mo.x, mo.y) and maxi(absi(mo.x - Game.hero.x), absi(mo.y - Game.hero.y)) <= 3:
				close = true
		if close:
			break
		AutoExplore.step()
		main._after_action()
		await get_tree().process_frame
	await _wait(0.5)
	await _shot("near-monster")
	for i in 40:
		var foe: Monster = null
		for mo in Game.map.monsters:
			if mo.alive and Game.map.is_visible(mo.x, mo.y) and maxi(absi(mo.x - Game.hero.x), absi(mo.y - Game.hero.y)) <= 1:
				foe = mo
		if foe == null:
			AutoExplore.step()
			main._after_action()
			await _wait(0.12)
			continue
		Rules.try_move(foe.x - Game.hero.x, foe.y - Game.hero.y)
		main._after_action()
		await _wait(0.09)
		await _shot("attack-%d" % i)
		await _wait(0.25)
		if i > 6:
			break
	main._open_inventory()
	await _wait(0.2)
	await _shot("pack")
	await _key(KEY_ESCAPE)
	main._open_character()
	await _wait(0.2)
	await _shot("character")
	await _key(KEY_ESCAPE)
	main.view.show_full_map = true
	await _wait(0.3)
	await _shot("fullmap")
