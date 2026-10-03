extends Node
## Fourth screenshot pass: the east side of the plaza.

var main: Node
var out := "user://shots"
var n := 60


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
	Game.new_run(30, "Rook", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 91)
	Game.gold = 40000
	Game.deepest_floor = 30
	Game.meat = [5, 3, 2, 1]
	main._start_game()
	Game.hero.x = 43
	Game.hero.y = 10
	main.view.snap()
	await _wait(0.4)
	await _shot("plaza-east")
	main._open_building("kitchen")
	await _wait(0.1)
	await _key(KEY_DOWN)
	await _key(KEY_ENTER)
	await _wait(0.2)
	await _shot("kitchen")
	await _key(KEY_ESCAPE)
	main._open_building("races")
	await _wait(0.1)
	await _key(KEY_RIGHT)
	await _key(KEY_RIGHT)
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	await _key(KEY_ENTER)
	await _wait(1.6)
	await _shot("races-running")
	await _wait(1.6)
	await _shot("races-result")
	await _key(KEY_ESCAPE)
	main._open_building("bazaar")
	await _wait(0.2)
	await _shot("bazaar")
	await _key(KEY_ESCAPE)
	main._open_building("altar")
	await _wait(0.1)
	await _key(KEY_ENTER)
	await _key(KEY_DOWN)
	await _key(KEY_DOWN)
	await _wait(0.2)
	await _shot("altar")
