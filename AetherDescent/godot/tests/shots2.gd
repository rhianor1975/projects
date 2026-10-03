extends Node
## Second screenshot pass: the town, the temple, a spell, a shot, a death.

var main: Node
var out := "user://shots"
var n := 20


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
	Game.new_run(41, "Mira", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 1234)
	Game.gold = 5000
	main._start_game()
	await _wait(0.5)
	await _shot("town")
	# learn a spell, buy a bow
	main._open_building("arcanist")
	await _wait(0.2)
	await _key(KEY_ENTER)
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	await _key(KEY_ENTER)
	await _wait(0.2)
	await _shot("arcanist")
	await _key(KEY_ESCAPE)
	main._open_building("armory")
	await _key(KEY_RIGHT)
	await _key(KEY_RIGHT)
	await _key(KEY_ENTER)
	await _wait(0.2)
	await _shot("armory-ranged")
	await _key(KEY_ESCAPE)
	# walk up to the temple: it is two tiles north of the start
	await _key(KEY_UP)
	await _wait(0.3)
	await _key(KEY_UP)
	await _wait(0.3)
	await _shot("temple-menu")
	await _key(KEY_ENTER)
	await _wait(0.6)
	Game.hero.maxhp += 200
	Game.hero.hp = Game.hero.maxhp
	# find something to cast at
	for i in 400:
		var tg := Rules.find_target(5)
		if tg:
			break
		AutoExplore.step()
		main._after_action()
		await get_tree().process_frame
	await _wait(0.4)
	Rules.cast_spell(0)
	main._after_action()
	await _wait(0.06)
	await _shot("spell")
	await _wait(0.5)
	Game.hero.ranged_cooldown = 0
	if Rules.find_target(6):
		Rules.fire_ranged()
		main._after_action()
		await _wait(0.05)
		await _shot("arrow")
	await _wait(0.5)
	# and die, to see the Inn take you back
	Rules.hero_take_damage(99999, "a test")
	main._after_action()
	await _wait(0.3)
	await _shot("death")
	await _key(KEY_ENTER)
	await _wait(0.6)
	await _shot("woke-at-inn")
