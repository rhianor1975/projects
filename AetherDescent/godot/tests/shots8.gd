extends Node
## Eighth pass: the black market.

var main: Node
var out := "user://shots"
var n := 120


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
	Game.new_run(12, "Wren", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 31)
	main._start_game()
	for i in 23:
		Rules.grant_xp(Game.hero.xp_next, Game.hero)
	Game.hero.xp = 40
	Game.hero.hp = Game.hero.maxhp
	main.view.snap()
	await _wait(0.4)
	main._open_building("blackmarket")
	await _wait(0.3)
	await _shot("blackmarket")
	for k in [KEY_RIGHT, KEY_RIGHT, KEY_EQUAL]:
		await _key(k)
	await _wait(0.2)
	await _shot("blackmarket-ten")
	await _key(KEY_ENTER)
	await _wait(0.3)
	await _shot("blackmarket-sold")
	await _key(KEY_DOWN)
	await _wait(0.2)
	await _shot("blackmarket-all")
	await _key(KEY_ENTER)
	await _wait(0.3)
	await _shot("blackmarket-empty")
