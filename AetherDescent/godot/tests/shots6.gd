extends Node
## Sixth pass: the Well, and the card while its floor is built.

var main: Node
var out := "user://shots"
var n := 100


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
	Game.new_run(3, "Juno", C.Difficulty.NORMAL, C.WorldSize.WELL, 8)
	main._start_game()
	await _wait(0.3)
	Game.hero.x = Town.TEMPLE.x
	Game.hero.y = Town.TEMPLE.y + 1
	main.view.snap()
	await _key(KEY_UP)
	await _wait(0.2)
	await _key(KEY_ENTER)
	await get_tree().process_frame
	await _shot("descent-card")
	var t0 := Time.get_ticks_msec()
	while not Game.pending_floor.is_empty():
		await get_tree().process_frame
	print("built in %d ms" % (Time.get_ticks_msec() - t0))
	await _wait(0.6)
	await _shot("well-floor-1")
