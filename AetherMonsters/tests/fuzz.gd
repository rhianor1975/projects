extends Node
## Random input through the real game: title, creation, the plaza, every
## building, the dungeon, menus, the pad. Watches for nothing itself -- the
## run's log is read for script errors afterwards.
##   godot --headless --path . tests/fuzz.tscn -- [presses] [seed]

var main: Node
var presses := 6000
const KEYS := [KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_H, KEY_J, KEY_K, KEY_L, KEY_Y, KEY_U, KEY_B, KEY_N, KEY_ENTER, KEY_ENTER, KEY_Z, KEY_X, KEY_ESCAPE,
	KEY_PERIOD, KEY_SPACE, KEY_M, KEY_S, KEY_F, KEY_A, KEY_I, KEY_C, KEY_R, KEY_G, KEY_P, KEY_TAB,
	KEY_SLASH, KEY_KP_5, KEY_EQUAL, KEY_MINUS, KEY_BACKSPACE]
const PAD := [JOY_BUTTON_A, JOY_BUTTON_B, JOY_BUTTON_X, JOY_BUTTON_Y, JOY_BUTTON_LEFT_SHOULDER,
	JOY_BUTTON_RIGHT_SHOULDER, JOY_BUTTON_START, JOY_BUTTON_BACK, JOY_BUTTON_LEFT_STICK,
	JOY_BUTTON_DPAD_UP, JOY_BUTTON_DPAD_DOWN, JOY_BUTTON_DPAD_LEFT, JOY_BUTTON_DPAD_RIGHT]


func _ready() -> void:
	Game.persist = false
	var args := OS.get_cmdline_user_args()
	if args.size() > 0:
		presses = int(args[0])
	seed(int(args[1]) if args.size() > 1 else 1)
	main = load("res://scenes/main.tscn").instantiate()
	add_child(main)
	await _run()
	print("fuzz done: %d presses, mode %s, depth %d, party %d" % [presses, main.mode, Game.depth, Game.party.size()])
	print("fuzz stats: ", stats)
	get_tree().quit()


func _send(ev: InputEvent) -> void:
	Input.parse_input_event(ev)
	await get_tree().process_frame


func _key(code: int) -> void:
	var k := InputEventKey.new()
	k.keycode = code
	k.pressed = true
	await _send(k)
	var u := InputEventKey.new()
	u.keycode = code
	await _send(u)


## Through character creation the way a player does: New Game, a difficulty,
## a world, a class (a random tab and row), a name.
func _create() -> void:
	main.title_menu.cursor = 0
	await _key(KEY_ENTER)
	for step in 3:
		for k in randi() % 3:
			await _key(KEY_DOWN if step != 1 else KEY_UP)
		if step == 2:
			for k in randi() % 7:
				await _key(KEY_RIGHT)
		await _key(KEY_ENTER)
	await _key(KEY_ENTER)
	mortal = randf() < 0.5
	if randf() < 0.5:
		Game.gold = 2000000
		for k in 1 + randi() % 3:
			Party.hire(randi() % Party.TAVERN_ROSTER)


var stats := {}
var mortal := false       # this run may die


func _run() -> void:
	await _create()
	var runs := 1
	var deepest := 0
	var deaths := 0
	var in_dungeon := 0
	for i in presses:
		deepest = maxi(deepest, Game.depth)
		if Game.depth > 0:
			in_dungeon += 1
		if Game.game_over:
			deaths += 1
		# most of a run is underground: go back down when stuck in the plaza
		if main.mode == "game" and main.modal == null and main.dialog.is_empty() and Game.depth == 0 and randf() < 0.01:
			Game.go_to_floor(1 + randi() % maxi(1, Game.deepest_floor + 3))
			main.view.snap()
		stats = {"runs": runs, "deepest": deepest, "turns underground": in_dungeon, "death frames": deaths,
			"most party": maxi(stats.get("most party", 0), Game.party.size())}
		if main.mode == "title":
			# never pick Quit: start another run instead
			runs += 1
			await _create()
			continue
		if main.mode == "create":
			await _key(KEY_ENTER)
			continue
		# keep the run going: a fuzzer that dies on floor 1 tests floor 1
		if not mortal and Game.hero != null and Game.hero.alive and Game.hero.hp < Game.hero.maxhp / 3:
			Game.hero.hp = Game.hero.maxhp
		if Game.gold < 5000 and Game.hero != null:
			Game.gold = 200000
		if i % 700 == 699 and main.mode == "game" and main.modal == null and Game.depth == 0:
			# walk into a building now and then, so the shops get their share
			main._open_building(["tavern", "kitchen", "races", "bazaar", "altar", "armory", "arcanist",
				"junkyard", "inn", "gladiator", "oracle", "blackmarket", "general", "apothecary", "bank"][randi() % 15])
		if i % 900 == 450 and main.mode == "game" and main.modal == null and Game.depth == 0:
			main._open_board()
		if i % 1200 == 600 and main.mode == "game" and main.modal == null and Game.depth == 0:
			main._open_temple()
		var r := randf()
		if r < 0.12:
			var e := InputEventJoypadButton.new()
			e.button_index = PAD[randi() % PAD.size()]
			e.pressed = true
			await _send(e)
			var u := InputEventJoypadButton.new()
			u.button_index = e.button_index
			u.pressed = false
			await _send(u)
		elif r < 0.14:
			var mo := InputEventJoypadMotion.new()
			mo.axis = JOY_AXIS_TRIGGER_LEFT if randi() % 2 else JOY_AXIS_TRIGGER_RIGHT
			mo.axis_value = 1.0
			await _send(mo)
			mo = mo.duplicate()
			mo.axis_value = 0.0
			await _send(mo)
		else:
			var k := InputEventKey.new()
			k.keycode = KEYS[randi() % KEYS.size()]
			k.pressed = true
			k.shift_pressed = randf() < 0.1
			k.unicode = 97 + randi() % 26 if randf() < 0.2 else 0
			await _send(k)
			var u := InputEventKey.new()
			u.keycode = k.keycode
			await _send(u)
		# let animations land sometimes, so the view's own code runs too
		if randf() < 0.05:
			for f in 8:
				await get_tree().process_frame
