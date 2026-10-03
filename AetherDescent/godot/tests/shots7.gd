extends Node
## Seventh pass: hearing and the records.

var main: Node
var out := "user://shots"
var n := 110


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
	Game.fallen().append({"name": "Old Kael", "level": 9, "floor": 12, "gold": 300})
	Game.new_run(12, "Wren", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 31)
	main._start_game()
	Game.enter_floor(3)
	Game.hero.hearing_radius = 6
	Game.hero.maxhp = 900
	Game.hero.hp = 900
	main.view.snap()
	for i in 300:
		AutoExplore.step()
		Rules.refresh_vision()
		if Game.map.heard.size() >= 2:
			break
	main.view.snap()
	await _wait(0.6)
	await _shot("hearing")
	main._open_records()
	await _wait(0.2)
	await _shot("records")
