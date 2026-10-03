extends Node2D
## The screens and the input: title, character creation, the game, and the
## modal menus over it -- shops, pack, spells, the character sheet, the
## temple's floor choice, death and victory.

const W := 640.0
const H := 360.0

var view: MapView
var ui: Node2D
var mode := "title"          # title, create, game
var modal: Menu = null
var dialog := {}             # a simple message box: {title, lines, on_close}
var t := 0.0
var auto := false
var hold_dir := Vector2i.ZERO
var working := false         # a `g` job is running: one turn per step until done
var hold_t := 0.0

# creation state
var c_step := "difficulty"
var c_diff := 0
var c_world := 0
var c_arch := 0
var c_class := 0
var c_name := ""
var title_menu: Menu
var title_bg: Texture2D


func _ready() -> void:
	texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST
	view = MapView.new()
	add_child(view)
	view.visible = false
	ui = Node2D.new()
	add_child(ui)
	ui.draw.connect(_draw_ui)
	if ResourceLoader.exists("res://assets/title_bg.png"):
		title_bg = load("res://assets/title_bg.png")
	_open_title()


func _process(delta: float) -> void:
	t += delta
	ui.queue_redraw()
	# a big floor: show the card for a frame, then build it
	if not Game.pending_floor.is_empty():
		auto = false
		hold_dir = Vector2i.ZERO
		if Game.pending_floor.shown:
			Game.finish_pending_floor()
			view.snap()
			_after_action()
		else:
			Game.pending_floor.shown = true
		return
	if mode != "game" or modal != null or not dialog.is_empty():
		auto = false
		return
	if view.busy():
		return
	if working:
		if Game.hero.work_left > 0 and Game.depth > 0:
			_do(Districts.work_step)
		else:
			working = false
		return
	# auto-explore runs one step per settle
	if auto:
		if view.hero_settled():
			var why := AutoExplore.step()
			_after_action()
			if why != "":
				auto = false
				Game.msg(why, Gfx.GREY)
		return
	# held movement repeats once the last step has landed
	if hold_dir != Vector2i.ZERO:
		hold_t += delta
		if hold_t > 0.18 and view.hero_settled():
			_do(Rules.try_move.bind(hold_dir.x, hold_dir.y))


# ---- title ------------------------------------------------------------------------
func _open_title() -> void:
	mode = "title"
	view.visible = false
	title_menu = Menu.new()
	var runs := Game.list_runs()
	title_menu.items = [{"label": "New Game", "id": "new"}]
	for name in runs:
		var r: Dictionary = runs[name]
		title_menu.items.append({"label": "Continue: %s" % name, "id": "load", "name": name,
			"desc": "%s, level %d, deepest floor %d (%s)" % [r.get("class", "?"), int(r.get("level", 1)),
				int(r.get("deepest", 0)), C.DIFFICULTY_NAMES[int(r.get("difficulty", 0))]]})
	title_menu.items.append({"label": _art_label(), "id": "art",
		"desc": "16-bit: shaded anime sprites.  Classic: the flat look of the first sketches."})
	title_menu.items.append({"label": "Quit", "id": "quit"})


func _guardian_label() -> String:
	return "Guardian angel: %s" % ("on" if Game.settings.get("guardian", false) else "off")


func _art_label() -> String:
	return "Art: %s" % ("16-bit" if Gfx.art == "16bit" else "Classic")


func _title_input(ev: InputEvent) -> void:
	if ev is InputEventJoypadButton and ev.pressed:
		var k := InputEventKey.new()
		k.pressed = true
		k.keycode = {JOY_BUTTON_DPAD_UP: KEY_UP, JOY_BUTTON_DPAD_DOWN: KEY_DOWN, JOY_BUTTON_A: KEY_ENTER}.get(ev.button_index, KEY_NONE)
		ev = k
	if not (ev is InputEventKey and ev.pressed):
		return
	var m := title_menu
	if ev.keycode in [KEY_UP, KEY_K, KEY_W]:
		m.cursor = (m.cursor - 1 + m.items.size()) % m.items.size()
		Sfx.play("cursor")
	elif ev.keycode in [KEY_DOWN, KEY_J, KEY_S]:
		m.cursor = (m.cursor + 1) % m.items.size()
		Sfx.play("cursor")
	elif ev.keycode in [KEY_ENTER, KEY_Z, KEY_SPACE, KEY_KP_ENTER]:
		Sfx.play("confirm")
		var it := m.current()
		match it.id:
			"new":
				_open_create()
			"load":
				if Game.load_run(it.name):
					_start_game()
			"art":
				Gfx.set_art("classic" if Gfx.art == "16bit" else "16bit")
				Game.save_settings()
				it.label = _art_label()
			"quit":
				get_tree().quit()


func _draw_title(ci: CanvasItem) -> void:
	if title_bg:
		ci.draw_texture(title_bg, Vector2.ZERO)
	else:
		ci.draw_rect(Rect2(0, 0, W, H), Color8(20, 14, 56))
	var gold := [Color8(255, 250, 210), Color8(255, 232, 140), Color8(250, 196, 72), Color8(224, 148, 40),
		Color8(184, 104, 32), Color8(152, 72, 24), Color8(112, 48, 24)]
	var title := "AETHER DESCENT"
	var tw := Gfx.text_width(title, 5)
	Gfx.text(ci, Vector2(roundi((W - tw) / 2), 44 + roundi(sin(t * 1.5) * 2)), title, Color8(255, 210, 100), 5, false, Gfx.INK)
	Gfx.text_center(ci, W / 2, 98, "One hundred floors down.", Color8(232, 216, 248), 2)
	var mh := title_menu.items.size() * 16 + 16
	var mw := 220.0
	Gfx.window(ci, Rect2(W - mw - 20, 190, mw, mh))
	for i in title_menu.items.size():
		var it: Dictionary = title_menu.items[i]
		var y := 198 + i * 16
		if i == title_menu.cursor:
			Gfx.cursor(ci, Vector2(W - mw - 10, y), t)
		Gfx.text(ci, Vector2(W - mw + 8, y), it.label, Gfx.WHITE if i == title_menu.cursor else Gfx.GREY)
	var cur := title_menu.current()
	if cur.has("desc"):
		Gfx.text_right(ci, Vector2(W - 20, 194 + mh), cur.desc, Gfx.GREY)
	# the party, walking toward the temple
	for i in 3:
		var look: int = [0, 6, 2][i]
		var frame := "walk%d" % (int(t * 6 + i) % 4)
		var x := fmod(t * 18.0 + i * 40.0, W + 120) - 60
		ci.draw_texture_rect_region(Gfx.heroes, Rect2(x, H - 82, 48, 60), Gfx.hero_src(look, "right", frame))
	Gfx.text_right(ci, Vector2(W - 8, H - 12), "v0.1  Godot 4", Color8(120, 160, 128))
	Gfx.text(ci, Vector2(8, H - 12), "Arrows: move   Z/Enter: confirm   X/Esc: back", Color8(120, 160, 128))


# ---- character creation -----------------------------------------------------------
const NAMES := ["Kael", "Mira", "Rook", "Sable", "Vesper", "Ivy", "Orin", "Tamsin", "Bram", "Lio", "Wren",
	"Cass", "Dax", "Esme", "Fen", "Halo", "Juno", "Kit", "Lark", "Nyx", "Quill", "Rhea", "Soren", "Thea"]


func _open_create() -> void:
	mode = "create"
	c_step = "difficulty"
	c_name = NAMES[randi() % NAMES.size()]
	modal = Menu.new()
	modal.title = "New Character"
	modal.show_gold = false
	modal.list_width = 220
	modal.rows = 12
	_create_step(modal)


func _create_step(m: Menu) -> void:
	m.tabs = []
	m.cursor = 0
	m.scroll = 0
	match c_step:
		"difficulty":
			m.subtitle = "Choose a difficulty. It never changes."
			m.items = []
			for i in 4:
				m.items.append({"label": C.DIFFICULTY_NAMES[i], "desc": C.DIFFICULTY_BLURBS[i], "v": i})
			m.cursor = c_diff
			m.detail = _detail_text.bind("Difficulty", "Chosen once, at creation. Hardcore death is final; on every other mode you wake at the Inn.")
		"world":
			m.items = []
			for i in C.WORLD_NAMES.size():
				m.items.append({"label": C.WORLD_NAMES[i], "desc": C.WORLD_BLURBS[i], "v": i})
			m.cursor = c_world
			m.detail = _detail_text.bind("World size", "How big every floor of the run is. Waygates home come every fifth floor on the Shaft and the Halls, and on every floor of the Deeps.")
		"class":
			m.tabs = C.ARCHETYPE_NAMES
			m.tab = c_arch
			m.rebuild = func(mm: Menu):
				c_arch = mm.tab
				mm.items = []
				for i in ClassesData.CLASSES.size():
					var cs: Dictionary = ClassesData.CLASSES[i]
					if cs.archetype == mm.tab:
						mm.items.append({"label": cs.name, "desc": cs.tagline, "v": i})
			m.refresh()
			m.detail = _detail_class
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		Sfx.play("confirm")
		match c_step:
			"difficulty":
				c_diff = it.v
				c_step = "world"
			"world":
				c_world = it.v
				c_step = "class"
			"class":
				c_class = it.v
				c_step = "name"
				return false
		_create_step(mm)
		return false
	m.on_cancel = func(mm: Menu) -> bool:
		match c_step:
			"difficulty":
				modal = null
				_open_title()
				return true
			"world":
				c_step = "difficulty"
			"class":
				c_step = "world"
		_create_step(mm)
		return false
	m.footer = "Z/Enter: choose    X/Esc: back" + ("    Left/Right: archetype" if c_step == "class" else "")


func _detail_text(ci: CanvasItem, r: Rect2, _m: Menu, it: Dictionary, head: String, body: String) -> void:
	Gfx.text(ci, r.position + Vector2(14, 12), head, Gfx.GOLD, 2)
	var y := r.position.y + 40
	for line in Gfx.wrap(body, int(r.size.x - 28)):
		Gfx.text(ci, Vector2(r.position.x + 14, y), line, Gfx.GREY)
		y += 14
	if it.has("desc"):
		y += 8
		Gfx.text(ci, Vector2(r.position.x + 14, y), it.label, Gfx.WHITE, 2)
		y += 22
		for line in Gfx.wrap(it.desc, int(r.size.x - 28)):
			Gfx.text(ci, Vector2(r.position.x + 14, y), line, Gfx.WHITE)
			y += 14


func _detail_class(ci: CanvasItem, r: Rect2, _m: Menu, it: Dictionary) -> void:
	if it.is_empty():
		return
	var h := Hero.make(it.v, c_name)
	var p := r.position
	Gfx.portrait(ci, p + Vector2(14, 14), h.look, "neutral")
	Gfx.text(ci, p + Vector2(92, 14), ClassesData.CLASSES[it.v].name, Gfx.GOLD, 2)
	var y := p.y + 38
	for line in Gfx.wrap(ClassesData.CLASSES[it.v].tagline, int(r.size.x - 100)):
		Gfx.text(ci, Vector2(p.x + 92, y), line, Gfx.WHITE)
		y += 13
	var stats := [["HP", h.maxhp], ["ATK", h.base_atk + h.weapon_bonus], ["DEF", h.base_def + h.armor_bonus],
		["AE", h.aether_max], ["Evasion", "%d%%" % h.evasion_pct], ["Crit", "%d%%" % h.crit_pct],
		["Ward", "%d%%" % h.ward_pct], ["Vision", h.fov_radius]]
	for i in stats.size():
		var x := p.x + 14 + (i % 2) * 120
		var yy := p.y + 98 + (i / 2) * 15
		Gfx.text(ci, Vector2(x, yy), stats[i][0], Gfx.CYAN)
		Gfx.text_right(ci, Vector2(x + 100, yy), str(stats[i][1]), Gfx.WHITE)
	var leads: Array = []
	var order := range(30)
	order.sort_custom(func(a, b): return h.attrs[a] > h.attrs[b])
	for k in 2:
		leads.append("%s %d" % [C.ATTR_NAMES[order[k]].to_upper(), h.attrs[order[k]]])
	var rows := [["School", C.SCHOOL_NAMES[h.magic_school]], ["Ability", C.ABILITY_NAMES[h.ability_id]],
		["Weapon", ClassesData.CLASSES[it.v].weapon], ["Leads", "  ".join(leads)]]
	for i in rows.size():
		Gfx.text(ci, Vector2(p.x + 14, p.y + 164 + i * 15), rows[i][0], Gfx.CYAN)
		Gfx.text(ci, Vector2(p.x + 76, p.y + 164 + i * 15), rows[i][1], Gfx.GREEN if i == 3 else Gfx.WHITE)
	# turning to show itself off
	var facings := ["down", "left", "up", "right"]
	var f: String = facings[int(t * 0.8) % 4]
	ci.draw_texture_rect_region(Gfx.heroes, Rect2(p + Vector2(r.size.x - 64, 92), Vector2(48, 60)),
		Gfx.hero_src(h.look, f, "walk%d" % (int(t * 6) % 4)))


func _draw_name_entry(ci: CanvasItem) -> void:
	Gfx.window(ci, Rect2(120, 120, 400, 110))
	Gfx.text(ci, Vector2(136, 132), "What are you called?", Gfx.GOLD, 2)
	Gfx.window(ci, Rect2(136, 162, 368, 26), Color8(20, 20, 60), Color8(8, 8, 30))
	Gfx.text(ci, Vector2(146, 169), c_name + ("_" if int(t * 2) % 2 == 0 else " "), Gfx.WHITE, 1)
	Gfx.text(ci, Vector2(136, 200), "Type a name.  Enter: begin   Tab: another   Esc: back", Gfx.GREY)


func _name_input(ev: InputEvent) -> void:
	if not (ev is InputEventKey and ev.pressed):
		return
	match ev.keycode:
		KEY_ENTER, KEY_KP_ENTER:
			if c_name.strip_edges() == "":
				return
			Sfx.play("confirm")
			Game.new_run(c_class, c_name.strip_edges(), c_diff, c_world)
			if c_diff != C.Difficulty.HARDCORE:
				Game.save_inn_snapshot()
			Game.save_run()
			modal = null
			_start_game()
		KEY_ESCAPE:
			c_step = "class"
			_create_step(modal)
		KEY_BACKSPACE:
			c_name = c_name.substr(0, c_name.length() - 1)
		KEY_TAB:
			c_name = NAMES[randi() % NAMES.size()]
		_:
			if ev.unicode >= 32 and ev.unicode < 127 and c_name.length() < 16:
				c_name += char(ev.unicode)


func _start_game() -> void:
	Game.defer_floors = true
	mode = "game"
	view.visible = true
	view.snap()
	auto = false


# ---- the game -------------------------------------------------------------------
const MOVE_KEYS := {
	KEY_LEFT: Vector2i(-1, 0), KEY_RIGHT: Vector2i(1, 0), KEY_UP: Vector2i(0, -1), KEY_DOWN: Vector2i(0, 1),
	KEY_H: Vector2i(-1, 0), KEY_L: Vector2i(1, 0), KEY_K: Vector2i(0, -1), KEY_J: Vector2i(0, 1),
	KEY_Y: Vector2i(-1, -1), KEY_U: Vector2i(1, -1), KEY_B: Vector2i(-1, 1), KEY_N: Vector2i(1, 1),
	KEY_KP_4: Vector2i(-1, 0), KEY_KP_6: Vector2i(1, 0), KEY_KP_8: Vector2i(0, -1), KEY_KP_2: Vector2i(0, 1),
	KEY_KP_7: Vector2i(-1, -1), KEY_KP_9: Vector2i(1, -1), KEY_KP_1: Vector2i(-1, 1), KEY_KP_3: Vector2i(1, 1),
}


func _do(action: Callable) -> void:
	var before := Game.depth
	action.call()
	_after_action()
	if Game.depth != before:
		Sfx.play("stairs")


func _after_action() -> void:
	_play_fx_sounds()
	var req := Rules.request
	Rules.request = {}
	if req.is_empty():
		return
	auto = false
	hold_dir = Vector2i.ZERO
	match req.open:
		"shop":
			_open_building(req.id)
		"temple":
			_open_temple()
		"board":
			_open_board()
		"merchant":
			_open_merchant()
		"gameover":
			_open_death()
		"win":
			_open_win()


func _play_fx_sounds() -> void:
	for e in Game.fx:
		match e.type:
			"attack":
				if e.who is Hero:
					Sfx.play("crit" if e.get("crit", false) else "hit")
			"hurt":
				if e.who is Hero:
					Sfx.play("hurt")
			"die": Sfx.play("die")
			"miss": Sfx.play("miss")
			"heal": Sfx.play("heal")
			"bolt", "cast": Sfx.play("spell")
			"shot": Sfx.play("shot")
			"levelup": Sfx.play("levelup")
			"pickup": Sfx.play("coin" if str(e.text).begins_with("+") else "pickup")


const PAD_MOVE := {
	JOY_BUTTON_DPAD_LEFT: Vector2i(-1, 0), JOY_BUTTON_DPAD_RIGHT: Vector2i(1, 0),
	JOY_BUTTON_DPAD_UP: Vector2i(0, -1), JOY_BUTTON_DPAD_DOWN: Vector2i(0, 1),
}


func _pad_input(ev: InputEventJoypadButton) -> void:
	if not ev.pressed:
		if PAD_MOVE.has(ev.button_index) and PAD_MOVE[ev.button_index] == hold_dir:
			hold_dir = Vector2i.ZERO
		return
	if auto:
		auto = false
		return
	if PAD_MOVE.has(ev.button_index):
		hold_dir = PAD_MOVE[ev.button_index]
		hold_t = 0.0
		if not view.busy():
			_do(Rules.try_move.bind(hold_dir.x, hold_dir.y))
		return
	if view.busy():
		return
	match ev.button_index:
		JOY_BUTTON_A:
			if Game.depth > 0:
				_do(Rules.wait_turn)
		JOY_BUTTON_X:
			_do(Rules.use_ability)
		JOY_BUTTON_Y:
			_do(Rules.fire_ranged)
		JOY_BUTTON_LEFT_SHOULDER:
			_open_spells()
		JOY_BUTTON_RIGHT_SHOULDER:
			_open_inventory()
		JOY_BUTTON_BACK:
			view.show_full_map = not view.show_full_map
		JOY_BUTTON_LEFT_STICK, JOY_BUTTON_RIGHT_STICK:
			if Party.switch_next():
				Game.msg("You are %s now." % Game.hero.name, Party.color_of(Game.hero))
				view.snap_camera()
		JOY_BUTTON_START, JOY_BUTTON_B:
			_open_pause()


## A big floor is being built: say where you are going while the frame waits.
func _draw_descent(ci: CanvasItem) -> void:
	var p := Game.pending_floor
	var n := int(p.n)
	ci.draw_rect(Rect2(0, 0, W, H), Color8(6, 6, 18))
	var b := C.biome_for_floor(n)
	Gfx.text_center(ci, W / 2, H / 2 - 40, "Floor %d" % n, Gfx.GOLD, 3)
	Gfx.text_center(ci, W / 2, H / 2 - 4, C.BIOME_NAMES[b], Gfx.WHITE, 2)
	Gfx.text_center(ci, W / 2, H / 2 + 30, ("The stairs go a long way down..." if bool(p.down) else "The long climb back up...")
		+ "  (%s floors take a moment to build)" % C.WORLD_NAMES[Game.world], Gfx.GREY)


func _game_input(ev: InputEvent) -> void:
	if not Game.pending_floor.is_empty():
		return
	if ev is InputEventJoypadButton:
		_pad_input(ev)
		return
	if ev is InputEventKey and not ev.pressed:
		if MOVE_KEYS.has(ev.keycode) and MOVE_KEYS[ev.keycode] == hold_dir:
			hold_dir = Vector2i.ZERO
		return
	if not (ev is InputEventKey and ev.pressed) or ev.echo:
		return
	if auto:
		auto = false
		Game.msg("You take the reins back.", Gfx.GREY)
		return
	if working:
		working = false
		Districts.work_abandon("You straighten up and leave it.")
		return
	if view.show_full_map and ev.keycode != KEY_TAB:
		view.show_full_map = false
		return
	var k: int = ev.keycode
	if MOVE_KEYS.has(k):
		var dir: Vector2i = MOVE_KEYS[k]
		if ev.shift_pressed:
			pass
		hold_dir = dir
		hold_t = 0.0
		if not view.busy():
			_do(Rules.try_move.bind(dir.x, dir.y))
		return
	if view.busy():
		return
	match k:
		KEY_PERIOD, KEY_KP_5, KEY_SPACE:
			if Game.depth > 0:
				_do(Rules.wait_turn)
		KEY_X:
			if Game.depth > 0:
				auto = true
				AutoExplore.reset()
			else:
				Game.msg("Auto-explore works in the temple. The way down is in the middle of the plaza.", Gfx.GREY)
		KEY_M:
			if ev.shift_pressed:
				view.show_full_map = not view.show_full_map
			else:
				_open_spells()
		KEY_TAB:
			view.show_full_map = not view.show_full_map
		KEY_G:
			if Game.depth > 0 and Districts.work_begin():
				working = true
		KEY_P:
			if Party.switch_next():
				Sfx.play("confirm")
				Game.msg("You are %s now." % Game.hero.name, Party.color_of(Game.hero))
				view.snap_camera()
			elif Party.count() == 0:
				Game.msg("Nobody to hand the lantern to. The Brass Lantern hires, west of the plaza.", Gfx.GREY)
			else:
				Game.msg("There is nobody else standing.", Gfx.GREY)
		KEY_S:
			var slot := Game.hero.last_spell_slot
			if slot >= 0:
				_do(Rules.cast_spell.bind(slot))
			else:
				_open_spells()
		KEY_F:
			_do(Rules.fire_ranged)
		KEY_A:
			_do(Rules.use_ability)
		KEY_I:
			_open_inventory()
		KEY_C:
			_open_character()
		KEY_R:
			_do(Rules.start_recall)
		KEY_SLASH, KEY_F1:
			_open_help()
		KEY_ESCAPE:
			_open_pause()


func _unhandled_input(ev: InputEvent) -> void:
	if not dialog.is_empty():
		var pad: bool = ev is InputEventJoypadButton and ev.pressed and ev.button_index in [JOY_BUTTON_A, JOY_BUTTON_B]
		if pad or (ev is InputEventKey and ev.pressed and not ev.echo and ev.keycode in [KEY_ENTER, KEY_Z, KEY_X, KEY_ESCAPE, KEY_SPACE, KEY_KP_ENTER]):
			var cb = dialog.get("on_close")
			dialog = {}
			Sfx.play("confirm")
			if cb is Callable and cb.is_valid():
				cb.call()
		return
	if mode == "title":
		_title_input(ev)
		return
	if mode == "create":
		if c_step == "name":
			_name_input(ev)
		elif modal:
			var m := modal
			if m.input(ev) and m.closed and modal == m:
				modal = null
		return
	if modal:
		var m := modal
		if m.input(ev) and m.closed:
			if modal == m:
				modal = null
			if Game.depth == 0:
				Game.save_run()
		return
	if mode == "game":
		_game_input(ev)


func _show(title: String, lines: Array, on_close := Callable()) -> void:
	dialog = {"title": title, "lines": lines, "on_close": on_close}


# ---- menus over the game ------------------------------------------------------------
func _menu(title: String) -> Menu:
	var m := Menu.new()
	m.title = title
	modal = m
	return m


func _open_inventory() -> void:
	var m := _menu("Pack")
	m.rebuild = func(mm: Menu):
		mm.items = []
		for s in Game.inventory:
			var tpl := ItemsData.consumable_by_name(s.name)
			mm.items.append({"label": s.name, "right": "x%d" % s.count, "name": s.name, "desc": _consumable_desc(tpl)})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		_do(Rules.use_item.bind(it.name))
		return true
	m.detail = _detail_hero
	m.footer = "Z/Enter: use    X/Esc: back"
	m.refresh()


func _consumable_desc(tpl: Dictionary) -> String:
	if tpl.is_empty():
		return ""
	var parts: Array = []
	if tpl.recall:
		parts.append("Cracks open a way home to the city after a few steady turns.")
	if tpl.heal > 0 or tpl.heal_pct > 0:
		parts.append("Heals %d HP plus %d%% of your maximum, and cures poison." % [tpl.heal, tpl.heal_pct])
	if tpl.atk_buff > 0:
		parts.append("+%d attack for %d turns." % [tpl.atk_buff, tpl.atk_turns])
	if tpl.def_buff > 0:
		parts.append("+%d defence for %d turns." % [tpl.def_buff, tpl.def_turns])
	if tpl.perm_maxhp > 0:
		parts.append("+%d maximum HP, permanently." % tpl.perm_maxhp)
	return " ".join(parts)


func _detail_hero(ci: CanvasItem, r: Rect2, _m: Menu, _it: Dictionary) -> void:
	var h := Game.hero
	var p := r.position
	Gfx.portrait(ci, p + Vector2(14, 14), h.look, "neutral")
	Gfx.text(ci, p + Vector2(92, 14), h.name, Gfx.WHITE, 2)
	Gfx.text(ci, p + Vector2(92, 36), "%s  Lv %d" % [h.class_name_str(), h.level], Gfx.GREY)
	Gfx.text(ci, p + Vector2(92, 52), "HP %d/%d   AE %d/%d" % [h.hp, h.maxhp, h.aether, h.aether_max], Gfx.WHITE)
	var rows := [["ATK", h.eff_atk()], ["DEF", h.eff_def()], ["Crit", "%d%%" % h.effective_stat(C.Acc.CRIT)],
		["Evasion", "%d%%" % h.effective_stat(C.Acc.EVASION)]]
	for i in rows.size():
		var x := p.x + 14 + (i % 2) * 120
		var y := p.y + 96 + (i / 2) * 15
		Gfx.text(ci, Vector2(x, y), rows[i][0], Gfx.CYAN)
		Gfx.text_right(ci, Vector2(x + 100, y), str(rows[i][1]), Gfx.WHITE)
	Gfx.text(ci, Vector2(p.x + 14, p.y + 136), "Weapon", Gfx.CYAN)
	Gfx.text(ci, Vector2(p.x + 70, p.y + 136), "%s +%d" % [h.weapon_name, h.weapon_plus] if h.weapon_plus else h.weapon_name, Gfx.WHITE)
	Gfx.text(ci, Vector2(p.x + 14, p.y + 151), "Armour", Gfx.CYAN)
	Gfx.text(ci, Vector2(p.x + 70, p.y + 151), "%s +%d" % [h.armor_name, h.armor_plus] if h.armor_plus else h.armor_name, Gfx.WHITE)
	if h.ranged_type != C.Ranged.NONE:
		Gfx.text(ci, Vector2(p.x + 14, p.y + 166), "Ranged", Gfx.CYAN)
		Gfx.text(ci, Vector2(p.x + 70, p.y + 166), "%s (%d/%d)" % [h.ranged_name, h.ranged_ammo, h.ranged_ammo_max], Gfx.WHITE)


func _open_spells() -> void:
	var h := Game.hero
	var m := _menu("Spells")
	m.rebuild = func(mm: Menu):
		mm.items = []
		for i in h.known_spells.size():
			var s := SpellBook.get_spell(h.known_spells[i])
			var ready: bool = h.spell_cd[i] == 0 and h.aether >= s.cost
			mm.items.append({"label": s.name, "right": ("AE %d" % s.cost) if h.spell_cd[i] == 0 else ("cd %d" % h.spell_cd[i]),
				"slot": i, "enabled": ready, "color": C.SCHOOL_COLORS[s.school].lightened(0.3),
				"desc": SpellBook.describe(s), "why": "Not ready: recharging or short of aether."})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		modal = null
		_do(Rules.cast_spell.bind(it.slot))
		return true
	m.detail = _detail_hero
	m.footer = "Z/Enter: cast    X/Esc: back    (s recasts the last one)"
	m.refresh()
	if m.items.is_empty():
		modal = null
		Game.msg("You know no spells. The Arcanist's Guild in the plaza teaches %s." % C.SCHOOL_NAMES[h.magic_school], Gfx.GREY)


func _open_character() -> void:
	var h := Game.hero
	var m := _menu(h.name)
	m.show_gold = true
	m.list_width = 250
	m.rows = 12
	m.rebuild = func(mm: Menu):
		mm.items = []
		for i in 30:
			mm.items.append({"label": C.ATTR_NAMES[i], "right": h.attrs[i], "desc": _attr_desc(i)})
	m.detail = func(ci: CanvasItem, r: Rect2, _mm: Menu, _it: Dictionary):
		var p := r.position
		Gfx.portrait(ci, p + Vector2(14, 14), h.look, "happy")
		Gfx.text(ci, p + Vector2(92, 14), "%s  Lv %d" % [h.class_name_str(), h.level], Gfx.GOLD)
		Gfx.text(ci, p + Vector2(92, 30), "%s  /  %s" % [C.ARCHETYPE_NAMES[h.archetype], C.DIFFICULTY_NAMES[Game.difficulty]], Gfx.GREY)
		Gfx.text(ci, p + Vector2(92, 46), "XP %d/%d   seed %d" % [h.xp, h.xp_next, Game.run_seed], Gfx.GREY)
		Gfx.text(ci, p + Vector2(92, 62), "Deepest floor %d    %d scrap  %d plat  %d diam" % [Game.deepest_floor,
			int(Game.mats[0]), int(Game.mats[1]), int(Game.mats[2])], Gfx.GREY)
		var stats := [["ATK", h.eff_atk()], ["DEF", h.eff_def()], ["HP", "%d/%d" % [h.hp, h.maxhp]],
			["AE", "%d/%d" % [h.aether, h.aether_max]], ["Crit", "%d%%" % h.effective_stat(C.Acc.CRIT)],
			["Evasion", "%d%%" % h.effective_stat(C.Acc.EVASION)], ["Ward", "%d%%" % h.effective_stat(C.Acc.WARD)],
			["Vision", h.fov_radius], ["Gold find", "+%d%%" % h.effective_stat(C.Acc.GOLD)],
			["XP find", "+%d%%" % h.effective_stat(C.Acc.XP)], ["Discount", "%d%%" % h.shop_discount_pct],
			["Hazard res", "%d%%" % (h.hazard_resist_pct + h.set_bonus_hazard)]]
		for i in stats.size():
			var x := p.x + 14 + (i % 2) * 180
			var y := p.y + 92 + (i / 2) * 14
			Gfx.text(ci, Vector2(x, y), stats[i][0], Gfx.CYAN)
			Gfx.text_right(ci, Vector2(x + 160, y), str(stats[i][1]), Gfx.WHITE)
		var y2 := p.y + 182
		var gear := [["Weapon", h.weapon_name + (" +%d" % h.weapon_plus if h.weapon_plus else "")],
			["Armour", h.armor_name + (" +%d" % h.armor_plus if h.armor_plus else "")],
			["Ring", h.ring_name if h.ring_name != "" else "-"], ["Trinket", h.trinket_name if h.trinket_name != "" else "-"],
			["Ability", "%s (%s)" % [C.ABILITY_NAMES[h.ability_id], "ready" if h.ability_cd == 0 else "%d" % h.ability_cd]],
			["School", C.SCHOOL_NAMES[h.magic_school]]]
		for i in gear.size():
			Gfx.text(ci, Vector2(p.x + 14, y2 + i * 14), gear[i][0], Gfx.CYAN)
			Gfx.text(ci, Vector2(p.x + 80, y2 + i * 14), gear[i][1], Gfx.WHITE)
	m.footer = "X/Esc: back"
	m.refresh()


func _attr_desc(i: int) -> String:
	var d := {
		C.Attr.MIGHT: "Raises base attack.", C.Attr.BRAWN: "Raises attack and maximum HP.",
		C.Attr.AGILITY: "Evasion, with Reflexes and Balance.", C.Attr.REFLEXES: "Evasion.", C.Attr.PRECISION: "Crit chance, ranged damage and ammunition.",
		C.Attr.GRIT: "Raises base defence.", C.Attr.FORTITUDE: "Defence and hazard resistance.", C.Attr.STAMINA: "Maximum HP; quicker recall.",
		C.Attr.BALANCE: "Evasion.", C.Attr.VISION: "How far you see.", C.Attr.HEARING: "Hearing through walls.",
		C.Attr.INTELLECT: "More experience per kill.", C.Attr.CUNNING: "More gold per find.", C.Attr.MEMORY: "Shop discount.",
		C.Attr.RESOLVE: "Hazard resistance.", C.Attr.INSTINCT: "Talks you out of ambushes.", C.Attr.CHARM: "Shop discount.",
		C.Attr.GUILE: "Ambush resistance.", C.Attr.PRESENCE: "Monsters notice you closer in.", C.Attr.EMPATHY: "Luck at fountains.",
		C.Attr.TECH_WIT: "Luck with ancient machines.", C.Attr.CRAFTING: "Better bonuses from gear.", C.Attr.CHEMISTRY: "Stronger potions.",
		C.Attr.SCRIBING: "Scribing school.", C.Attr.AETHER_SENSE: "Aether-Sense school.", C.Attr.CONDUIT: "Aether pool, longer buffs.",
		C.Attr.RESONANCE: "Spell power and relic quality.", C.Attr.WARDING: "Ward: blows that never land.",
		C.Attr.FORTUNE: "Crit, gold and luck.", C.Attr.JINX: "Curses the things you hit.",
	}
	return "%s  Train it at the Gladiator School." % d.get(i, "")


func _open_help() -> void:
	_show("Keys", [
		"Move: arrows, hjkl/yubn, or the number pad. Walk into a monster to attack.",
		"Walk into a door in the city to go inside. The temple stairs are in the middle.",
		". or Space: wait     x: auto-explore     Tab or M: the whole floor",
		"m: cast a spell     s: cast the last one again     f: fire     a: class ability",
		"i: pack     c: character sheet     r: recall charm     Esc: menu",
		"p: take over the next of your party -- hires fight on their own until you do.",
		"g: work what is beside you -- an ore vein, a rod, a vent, a wreck -- for materials.",
	])


func _open_pause() -> void:
	var m := _menu("Menu")
	m.items = [{"label": "Resume", "id": "resume"}, {"label": "Keys", "id": "help"},
		{"label": "Sound: %s" % ("on" if Sfx.enabled else "off"), "id": "sound"},
		{"label": _art_label(), "id": "art", "desc": "16-bit: shaded anime sprites.  Classic: the flat look of the first sketches."},
		{"label": _guardian_label(), "id": "guardian",
			"desc": "An assist, not a difficulty. The dice lean your way when you are in trouble, one killing blow a floor is turned aside above floor 50 -- and a run that is coasting gets leaned on."},
		{"label": "Save and quit to title", "id": "quit"}]
	m.detail = _detail_hero
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		match it.id:
			"help":
				modal = null
				_open_help()
				return true
			"sound":
				Sfx.enabled = not Sfx.enabled
				it.label = "Sound: %s" % ("on" if Sfx.enabled else "off")
				Game.save_settings()
				return false
			"art":
				Gfx.set_art("classic" if Gfx.art == "16bit" else "16bit")
				Game.save_settings()
				it.label = _art_label()
				return false
			"guardian":
				Game.settings.guardian = not Game.settings.get("guardian", false)
				Game.save_settings()
				it.label = _guardian_label()
				Game.update_escalation()
				return false
			"quit":
				Game.save_run()
				modal = null
				_open_title()
				return true
		return true


# ---- the city ------------------------------------------------------------------------
func _buy(mm: Menu, price: int) -> bool:
	if Game.gold < price:
		Sfx.play("buzz")
		mm.say("You can't afford that.")
		return false
	Game.gold -= price
	Sfx.play("buy")
	return true


func _open_building(id: String) -> void:
	Sfx.play("door")
	match id:
		"general": _open_store("General Store", ItemsData.GENERAL, false)
		"apothecary": _open_store("Apothecary", ItemsData.APOTHECARY, true)
		"armory": _open_armory()
		"arcanist": _open_arcanist()
		"bank": _open_bank()
		"inn": _open_inn()
		"gladiator": _open_gladiator()
		"junkyard": _open_junkyard()
		"blackmarket": _open_blackmarket()
		"oracle": _open_oracle()
		"tavern": _open_tavern()
		"kitchen": _open_kitchen()
		"races": _open_races()
		"bazaar": _open_bazaar()
		"altar": _open_altar()


func _open_store(title: String, stock: Array, hp_upgrade: bool) -> void:
	var h := Game.hero
	var m := _menu(title)
	m.rebuild = func(mm: Menu):
		mm.items = []
		for tpl in stock:
			var price := Game.discounted(tpl.price)
			mm.items.append({"label": tpl.name, "right": price, "tpl": tpl, "price": price,
				"desc": _consumable_desc(tpl) + "  (You carry %d.)" % Game.count_item(tpl.name)})
		if hp_upgrade:
			var p := Game.discounted(ItemsData.upgrade_price(h.hp_plus))
			mm.items.append({"label": "Constitution +%d" % (h.hp_plus + 1), "right": p, "price": p, "hp": true,
				"color": Gfx.GREEN, "desc": "Something bitter, and your body takes it: +%d maximum HP, permanently." % C.UPGRADE_HP_STEP})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if not _buy(mm, it.price):
			return false
		if it.get("hp", false):
			h.hp_plus += 1
			h.maxhp += C.UPGRADE_HP_STEP
			h.hp += C.UPGRADE_HP_STEP
			mm.say("+%d maximum HP. You are now +%d." % [C.UPGRADE_HP_STEP, h.hp_plus])
		else:
			Game.give(it.tpl.name)
			mm.say("You buy a %s." % it.tpl.name)
		return false
	m.detail = _detail_hero
	m.footer = "Z/Enter: buy    X/Esc: leave"
	m.refresh()


func _open_armory() -> void:
	var h := Game.hero
	var m := _menu("Armory")
	m.tabs = ["Weapons", "Armour", "Ranged", "Accessories", "Smithing"]
	m.rebuild = func(mm: Menu):
		mm.items = []
		match mm.tab:
			0:
				for g in ItemsData.WEAPONS:
					var p := Game.discounted(g.price)
					var bonus: int = g.bonus + g.bonus * h.gear_bonus_pct / 100
					mm.items.append({"label": g.name, "right": p, "price": p, "kind": "weapon", "g": g, "bonus": bonus,
						"desc": "Attack +%d. You carry %s (+%d). The smith's work stays with the old blade." % [bonus, h.weapon_name, h.weapon_bonus]})
			1:
				for g in ItemsData.ARMORS:
					var p := Game.discounted(g.price)
					var bonus: int = g.bonus + g.bonus * h.gear_bonus_pct / 100
					mm.items.append({"label": g.name, "right": p, "price": p, "kind": "armor", "g": g, "bonus": bonus,
						"desc": "Defence +%d. You wear %s (+%d)." % [bonus, h.armor_name, h.armor_bonus]})
			2:
				for g in ItemsData.RANGED:
					var p := Game.discounted(g.price)
					mm.items.append({"label": g.name, "right": p, "price": p, "kind": "ranged", "g": g,
						"desc": "%s. %d damage, %d ammo a shot, %d turns to reset, reach %d." % [C.RANGED_NAMES[g.type], g.bonus, g.ammo, g.cd, C.RANGED_REACH[g.type]]})
			3:
				for g in ItemsData.ACCESSORIES:
					var p := Game.discounted(g.price)
					mm.items.append({"label": g.name, "right": p, "price": p, "kind": "acc", "g": g,
						"desc": "+%d %s. Fills your ring slot, then your trinket slot." % [g.bonus, C.ACC_NAMES[g.stat]]})
			4:
				for spec in [["weapon", "Weapon", h.weapon_plus], ["armor", "Armour", h.armor_plus]]:
					var p := Game.discounted(ItemsData.upgrade_price(spec[2]))
					var mt := ItemsData.upgrade_material_tier(spec[2])
					var mc := ItemsData.upgrade_material_cost(spec[2])
					var need := (" and %d %s (you have %d)" % [mc, C.MAT_NAMES[mt], int(Game.mats[mt])]) if mc > 0 else ""
					mm.items.append({"label": "%s +%d" % [spec[1], spec[2] + 1], "right": p, "price": p, "kind": "plus_" + spec[0],
						"mat": mt, "mat_cost": mc, "enabled": int(Game.mats[mt]) >= mc,
						"why": "\"Past this I need %d %s, and coin won't stand in for it.\" It comes up from the deep floors." % [mc, C.MAT_NAMES[mt]],
						"desc": "+%d %s on whatever you carry now, for %s gold%s. Buying a new one starts it again at +0." % [C.UPGRADE_STEP, "attack" if spec[0] == "weapon" else "defence", Gfx.comma(p), need]})
				var ap := Game.discounted(ItemsData.upgrade_price(h.ammo_plus))
				mm.items.append({"label": "Bandolier +%d" % (h.ammo_plus + 1), "right": ap, "price": ap, "kind": "plus_ammo",
					"enabled": h.ranged_type != C.Ranged.NONE, "why": "Buy something to load first.",
					"desc": "+%d shots carried, for good." % C.UPGRADE_AMMO_STEP})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if not _buy(mm, it.price):
			return false
		var g = it.get("g")
		match it.kind:
			"weapon":
				h.weapon_name = g.name
				h.weapon_bonus = it.bonus
				h.weapon_plus = 0
				h.weapon_set = -1
				h.recompute_set_bonus()
				mm.say("You arm yourself with a %s." % g.name)
			"armor":
				h.armor_name = g.name
				h.armor_bonus = it.bonus
				h.armor_plus = 0
				h.armor_set = -1
				h.recompute_set_bonus()
				mm.say("You don the %s." % g.name)
			"ranged":
				Rules.equip_ranged(h, g)
				mm.say("You arm yourself with a %s." % g.name)
			"acc":
				if h.ring_stat == C.Acc.NONE or h.trinket_stat != C.Acc.NONE:
					h.ring_name = g.name
					h.ring_stat = g.stat
					h.ring_bonus = g.bonus
					h.ring_set = -1
				else:
					h.trinket_name = g.name
					h.trinket_stat = g.stat
					h.trinket_bonus = g.bonus
					h.trinket_set = -1
				h.recompute_set_bonus()
				mm.say("You put on the %s." % g.name)
			"plus_weapon":
				Game.mats[it.mat] = int(Game.mats[it.mat]) - int(it.mat_cost)
				h.weapon_plus += 1
				mm.say("The smith works your %s up to +%d." % [h.weapon_name, h.weapon_plus])
			"plus_armor":
				Game.mats[it.mat] = int(Game.mats[it.mat]) - int(it.mat_cost)
				h.armor_plus += 1
				mm.say("The smith works your %s up to +%d." % [h.armor_name, h.armor_plus])
			"plus_ammo":
				h.ammo_plus += 1
				h.ranged_ammo_max += C.UPGRADE_AMMO_STEP
				h.ranged_ammo = h.ranged_ammo_max
				mm.say("A deeper magazine: +%d shots." % C.UPGRADE_AMMO_STEP)
		return false
	m.detail = _detail_hero
	m.footer = "Z/Enter: buy    Left/Right: counter    X/Esc: leave"
	m.refresh()


func _open_arcanist() -> void:
	var h := Game.hero
	var m := _menu("Arcanist's Guild")
	m.rebuild = func(mm: Menu):
		mm.items = []
		for n in 30:
			var idx := SpellBook.school_index(h.magic_school, n)
			var s := SpellBook.get_spell(idx)
			var known: bool = idx in h.known_spells
			var p := Game.discounted(s.price)
			mm.items.append({"label": s.name, "right": "known" if known else str(p), "price": p, "idx": idx,
				"enabled": not known, "why": "You know this one.", "color": C.SCHOOL_COLORS[s.school].lightened(0.35),
				"desc": SpellBook.describe(s)})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if not _buy(mm, it.price):
			return false
		Rules.learn_spell(it.idx)
		mm.say("You learn %s." % SpellBook.get_spell(it.idx).name)
		return false
	m.detail = func(ci: CanvasItem, r: Rect2, _mm: Menu, it: Dictionary):
		Gfx.text(ci, r.position + Vector2(14, 12), C.SCHOOL_NAMES[h.magic_school], C.SCHOOL_COLORS[h.magic_school], 2)
		Gfx.text(ci, r.position + Vector2(14, 36), "Your school: the only one the guild will", Gfx.GREY)
		Gfx.text(ci, r.position + Vector2(14, 50), "teach you. Spells cost aether, which", Gfx.GREY)
		Gfx.text(ci, r.position + Vector2(14, 64), "regrows a point a turn as you walk.", Gfx.GREY)
		Gfx.text(ci, r.position + Vector2(14, 90), "Known: %d   Aether %d" % [h.known_spells.size(), h.aether_max], Gfx.WHITE)
		if not it.is_empty():
			var s := SpellBook.get_spell(it.idx)
			Gfx.text(ci, r.position + Vector2(14, 118), s.name, Gfx.GOLD, 2)
			Gfx.text(ci, r.position + Vector2(14, 142), SpellBook.describe(s), Gfx.WHITE)
	m.footer = "Z/Enter: learn    X/Esc: leave"
	m.refresh()


func _open_bank() -> void:
	var m := _menu("Bank of the Deep Well")
	m.rebuild = func(mm: Menu):
		var price := ItemsData.bank_price(Game.gold_mult)
		mm.items = [{"label": "Buy a share: x%d" % (Game.gold_mult + 1), "right": Gfx.comma(price) if price > 0 else "-",
			"price": price, "enabled": price > 0, "why": "There is nothing left to sell you.",
			"desc": "Every coin you earn from now on is multiplied -- kills, piles, bounties, scrap. Priced as the cube of what you hold."}]
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if not _buy(mm, it.price):
			return false
		Game.gold_mult += 1
		mm.say("The clerk stamps your ledger. Everything you earn is now x%d." % Game.gold_mult)
		return false
	m.detail = _detail_text.bind("Shares", "The bank sells one thing: a share of everything you will ever earn. It does not lend and it does not buy back.")
	m.refresh()


func _open_inn() -> void:
	var fee := Game.gold / 5
	var hardcore := Game.difficulty == C.Difficulty.HARDCORE
	var m := _menu("The Inn")
	m.items = [{"label": "Take a room", "right": fee,
		"desc": "A fifth of what you carry. You wake whole%s." % ("" if hardcore else ", and this is where you wake if the temple takes you")},
		{"label": "Leave", "id": "leave"}]
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if it.get("id", "") == "leave":
			return true
		Game.gold -= fee
		for b in Game.party:
			if b.alive:
				b.hp = Party.max_hp(b)
				b.aether = b.aether_max
				b.ranged_ammo = b.ranged_ammo_max
				b.poison_turns = 0
		Game.rested = true
		Sfx.play("heal")
		if hardcore:
			Game.good("You take a room at the Inn. %d gold, and you sleep like the dead." % fee)
		else:
			Game.save_inn_snapshot()
			Game.good("You take a room at the Inn. %d gold, a hot meal, and a bed." % fee)
			Game.msg("If the temple takes you, this is where you'll wake up.", Gfx.GREY)
		Game.save_run()
		return true
	m.detail = _detail_hero


# ---- the Ashfall Kitchen --------------------------------------------------------------
## A service night: seven covers, one at a time, and the only resource is what
## you carried up. Nothing to lose -- the worst night still pays -- but a night
## run badly costs reputation, and reputation is the thing that compounds.
func _open_kitchen() -> void:
	if Game.kitchen_rep <= 0 and Game.kitchen_earned == 0:
		Game.kitchen_rep = Kitchen.REP_START
	var m := _menu("The Ashfall Kitchen")
	m.list_width = 260
	var closed := ""
	if Kitchen.meat_total() == 0:
		closed = "\"Come back with something,\" the cook says, \"and I'll cook it.\" (Meat comes off your own blade, underground.)"
	elif Game.kitchen_nights >= Kitchen.NIGHTS_PER_VISIT:
		closed = "The chairs are up. You cooked tonight; there isn't another one until you have been down again."
	m.data = {"patrons": [] if closed != "" else Kitchen.roll_patrons(), "i": 0, "served": 0, "taken": 0, "closed": closed}
	m.rebuild = func(mm: Menu):
		mm.items = []
		var d: Dictionary = mm.data
		if d.closed != "" or d.i >= d.patrons.size():
			mm.items.append({"label": "Leave", "id": "leave", "desc": d.closed})
			return
		for g in Kitchen.GRADES:
			mm.items.append({"label": "Serve %s" % Kitchen.MEAT_NAMES[g], "right": "x%d" % int(Game.meat[g]), "g": g,
				"enabled": int(Game.meat[g]) > 0, "why": "There is no %s left." % Kitchen.MEAT_NAMES[g],
				"color": [Gfx.GREY, Gfx.WHITE, Gfx.CYAN, Gfx.GOLD][g],
				"desc": "Pays about %s." % Gfx.comma(Kitchen.payout(d.patrons[d.i], g))})
		mm.items.append({"label": "Turn them away", "g": -1, "color": Gfx.RED.lightened(0.2), "desc": "Costs the house a little standing."})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		var d: Dictionary = mm.data
		if it.get("id", "") == "leave":
			return true
		var pat: Dictionary = d.patrons[d.i]
		var g: int = it.g
		Game.kitchen_rep = clampi(Game.kitchen_rep + Kitchen.rep_delta(pat, g), 0, Kitchen.REP_MAX)
		if g < 0:
			mm.say("%s is turned away, and says so on the way out." % pat.name)
			Sfx.play("back")
		else:
			Game.meat[g] = int(Game.meat[g]) - 1
			var paid := Game.gain_gold(Kitchen.payout(pat, g))
			d.taken += paid
			Game.kitchen_earned += paid
			var happy := g >= int(pat.wants)
			mm.say("%s %s. (+%s gold)" % [pat.name, "eats well" if happy else "pushes the plate back", Gfx.comma(paid)])
			Sfx.play("coin" if happy else "buzz")
		d.served += 1
		d.i += 1
		if d.i == 1:
			Game.kitchen_nights += 1     # the night is spent only once you have cooked
		if d.i < d.patrons.size() and Kitchen.meat_total() == 0:
			d.i = d.patrons.size()
			d.closed = "The larder is bare. The cook calls last orders. %s gold on the night." % Gfx.comma(d.taken)
		elif d.i >= d.patrons.size():
			d.closed = "Service is over. %s gold on the night, and the house is %s." % [Gfx.comma(d.taken), Kitchen.rep_title(Game.kitchen_rep)]
		if d.closed != "":
			Game.good(d.closed)
			Game.save_run()
		mm.cursor = 0
		return false
	m.detail = func(ci: CanvasItem, r: Rect2, mm: Menu, _it: Dictionary):
		var d: Dictionary = mm.data
		var p := r.position
		Gfx.text(ci, p + Vector2(14, 12), "The pass", Gfx.GOLD, 2)
		Gfx.text(ci, p + Vector2(14, 36), "The house: %s (%d)" % [Kitchen.rep_title(Game.kitchen_rep), Game.kitchen_rep], Gfx.GREY)
		var y := p.y + 58
		if d.closed == "" and d.i < d.patrons.size():
			var pat: Dictionary = d.patrons[d.i]
			Gfx.text(ci, Vector2(p.x + 14, y), "Cover %d of %d: %s" % [d.i + 1, d.patrons.size(), pat.name], Gfx.WHITE)
			y += 16
			for line in Gfx.wrap(pat.line, int(r.size.x - 28)):
				Gfx.text(ci, Vector2(p.x + 14, y), line, Gfx.CYAN)
				y += 14
			Gfx.text(ci, Vector2(p.x + 14, y + 4), "Wants %s.  Purse %s." % [Kitchen.MEAT_NAMES[pat.wants], Gfx.comma(pat.purse)], Gfx.GREY)
			y += 30
		Gfx.text(ci, Vector2(p.x + 14, y), "The larder", Gfx.CYAN)
		y += 16
		for g in Kitchen.GRADES:
			Gfx.text(ci, Vector2(p.x + 24, y), Kitchen.MEAT_NAMES[g], Gfx.WHITE)
			Gfx.text_right(ci, Vector2(p.x + 150, y), "%d / %d" % [int(Game.meat[g]), Kitchen.larder_cap()], Gfx.WHITE)
			y += 14
		if d.taken > 0:
			Gfx.text(ci, Vector2(p.x + 14, y + 8), "Tonight: %s gold" % Gfx.comma(d.taken), Gfx.GOLD)
	m.footer = "Z/Enter: serve    X/Esc: leave"
	m.refresh()


# ---- the lizard track ----------------------------------------------------------------
## Six runners and a bookmaker. The first three races of a visit are priced
## generously; after that he shortens the book.
func _open_races() -> void:
	var m := _menu("The Lizard Track")
	m.list_width = 220
	m.data = {"stake": 50, "race": {}}
	m.rebuild = func(mm: Menu):
		var o := Kitchen.odds()
		mm.items = []
		for i in Kitchen.RUNNERS.size():
			mm.items.append({"label": Kitchen.RUNNERS[i], "right": "%d-1" % o[i], "i": i,
				"desc": "Stake %s on %s at %d-1: pays %s if it comes in." % [Gfx.comma(mm.data.stake), Kitchen.RUNNERS[i], o[i], Gfx.comma(mm.data.stake * (o[i] + 1))]})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		var d: Dictionary = mm.data
		if not d.race.is_empty() and t - d.race.t0 < 3.4:
			return false
		if Game.gold < d.stake:
			Sfx.play("buzz")
			mm.say("\"Coin first,\" says the bookmaker, without looking up.")
			return false
		var o := Kitchen.odds()
		Game.gold -= d.stake
		var w := Kitchen.race_winner(o)
		Game.races_this_visit += 1
		var line := ""
		if w == it.i:
			# credited flat: the stake was already yours, and the bank's share
			# on winnings would make the book a printing press
			var paid: int = mini(d.stake * (o[it.i] + 1), 2000000000 - Game.gold)
			Game.gold += paid
			line = "%s comes in! The bookmaker counts out %s." % [Kitchen.RUNNERS[w], Gfx.comma(paid)]
		else:
			line = "%s takes it. Your %s stays on the table." % [Kitchen.RUNNERS[w], Gfx.comma(d.stake)]
		if Game.races_this_visit == Kitchen.GENEROUS_RACES:
			line += " The bookmaker shortens his prices."
		d.race = {"winner": w, "pick": it.i, "t0": t, "line": line, "said": false,
			"pace": range(6).map(func(_i): return randf_range(0.82, 0.97))}
		Sfx.play("confirm")
		Game.save_run()
		return false
	m.on_key = func(mm: Menu, key: int) -> bool:
		var d: Dictionary = mm.data
		match key:
			KEY_LEFT, KEY_H: d.stake = d.stake - 50 if d.stake > 50 else 10
			KEY_RIGHT, KEY_L: d.stake = mini(d.stake + 50, Kitchen.STAKE_MAX)
			KEY_EQUAL, KEY_PLUS, KEY_KP_ADD: d.stake = mini(d.stake * 10, Kitchen.STAKE_MAX)
			KEY_MINUS, KEY_KP_SUBTRACT: d.stake = maxi(d.stake / 10, 10)
			_: return false
		Sfx.play("cursor")
		return true
	m.detail = func(ci: CanvasItem, r: Rect2, mm: Menu, _it: Dictionary):
		var d: Dictionary = mm.data
		var p := r.position
		Gfx.text(ci, p + Vector2(14, 12), "Stake  %s" % Gfx.comma(d.stake), Gfx.GOLD, 2)
		Gfx.text(ci, p + Vector2(14, 36), "Left/Right: 50 at a time    +/-: ten times", Gfx.GREY)
		var x0 := p.x + 108
		var lane_w := r.size.x - 108 - 44
		var race: Dictionary = d.race
		var el := t - float(race.get("t0", -99.0))
		var kind := ArtLayout.MON_KINDS.find("serpent")
		for i in Kitchen.RUNNERS.size():
			var y := p.y + 60 + i * 24
			ci.draw_rect(Rect2(x0, y + 18, lane_w + 30, 1), Color8(90, 90, 150))
			var k := 0.0
			if not race.is_empty():
				# the race is decided; this is the show, staged to end that way
				var finish: float = 2.6 if i == race.winner else 2.6 / race.pace[i]
				k = clampf(el / finish, 0.0, 1.0)
				if i != race.winner:
					k = minf(k, 0.97)
			var x := x0 + k * lane_w
			var frame := int(el * 8 + i) % 2 if k > 0.0 and k < 1.0 else 0
			# the sheet faces left; a negative width turns them toward the post
			ci.draw_texture_rect_region(Gfx.monsters, Rect2(x + 26, y - 8, -26, 26), Gfx.monster_src(0, kind, frame))
			var col := Gfx.GOLD if not race.is_empty() and el > 2.7 and i == race.winner else (Gfx.CYAN if i == mm.cursor else Gfx.GREY)
			Gfx.text(ci, Vector2(p.x + 14, y + 4), Kitchen.RUNNERS[i], col)
		ci.draw_rect(Rect2(x0 + lane_w + 28, p.y + 56, 2, 6 * 24 + 4), Gfx.WHITE)
		if not race.is_empty() and el > 2.8 and not race.said:
			race.said = true
			mm.say(race.line)
			Sfx.play("coin" if race.winner == race.pick else "miss")
	m.footer = "Z/Enter: bet on the runner    X/Esc: leave"
	m.refresh()


# ---- the Barter Bazaar -----------------------------------------------------------------
## The Apothecary's bottles, at today's price -- which moves between trips.
func _open_bazaar() -> void:
	var m := _menu("Barter Bazaar")
	m.rebuild = func(mm: Menu):
		mm.items = []
		for i in ItemsData.APOTHECARY.size():
			var tpl: Dictionary = ItemsData.APOTHECARY[i]
			var pct := Kitchen.bazaar_pct(i)
			var price := Game.discounted(tpl.price * pct / 100)
			mm.items.append({"label": tpl.name, "right": price, "tpl": tpl, "price": price,
				"color": Gfx.GREEN if pct < 85 else (Gfx.RED.lightened(0.2) if pct > 120 else Gfx.WHITE),
				"desc": "%d%% of the Apothecary's price today.  %s" % [pct, _consumable_desc(tpl)]})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if not _buy(mm, it.price):
			return false
		Game.give(it.tpl.name)
		mm.say("You buy a %s for %s." % [it.tpl.name, Gfx.comma(it.price)])
		return false
	m.detail = _detail_hero
	m.refresh()
	Game.msg("The same bottles as the Apothecary, at whatever the stalls say today.", Gfx.GREY)


# ---- the Altar --------------------------------------------------------------------------
## Trade one point of an attribute for one of another, for half what the
## Gladiator School would charge to train the second one up.
func _open_altar() -> void:
	var h := Game.hero
	var m := _menu("The Altar")
	m.list_width = 260
	m.rows = 11
	m.data = {"give": -1}
	m.rebuild = func(mm: Menu):
		mm.items = []
		var give: int = mm.data.give
		for i in 30:
			var it := {"label": "%s  %d" % [C.ATTR_NAMES[i], h.attrs[i]], "a": i}
			if give < 0:
				it.enabled = h.attrs[i] > Kitchen.ALTAR_MIN_ATTR
				it.why = "\"There is nothing there to take.\""
				it.desc = "Give up a point of %s." % C.ATTR_NAMES[i]
			else:
				var price := _altar_price(i)
				it.right = Gfx.comma(price)
				it.enabled = i != give
				it.why = "\"That is not a trade.\""
				it.desc = "%s for %s, %s gold." % [C.ATTR_NAMES[give], C.ATTR_NAMES[i], Gfx.comma(price)]
				if i == give:
					it.color = Gfx.RED.lightened(0.2)
			mm.items.append(it)
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		var give: int = mm.data.give
		if give < 0:
			mm.data.give = it.a
			mm.title = "Give %s for..." % C.ATTR_NAMES[it.a]
			Sfx.play("confirm")
			return false
		var price := _altar_price(it.a)
		if not _buy(mm, price):
			return false
		h.shift_attribute(give, it.a)
		Sfx.play("levelup")
		mm.say("%s for %s. It is done, and it cost %s." % [C.ATTR_NAMES[give], C.ATTR_NAMES[it.a], Gfx.comma(price)])
		mm.data.give = -1
		mm.title = "The Altar"
		Game.save_run()
		return false
	m.on_cancel = func(mm: Menu) -> bool:
		if mm.data.give >= 0:
			mm.data.give = -1
			mm.title = "The Altar"
			mm.refresh()
			return false
		return true
	m.detail = _detail_hero
	m.footer = "Z/Enter: choose    X/Esc: back"
	m.refresh()


func _altar_price(attr: int) -> int:
	return Game.discounted(ItemsData.training_price(Game.hero.attrs[attr]) / 2)


# ---- the bounty board ---------------------------------------------------------------
## One notice at a time. Take it, carry it out, come back -- or tear it down.
func _open_board() -> void:
	Sfx.play("door")
	if Quests.turn_in():
		Game.save_run()
	var m := _menu("Bounty Board")
	m.list_width = 260
	m.rebuild = func(mm: Menu):
		if Quests.active():
			mm.items = [{"label": "Tear down the notice", "id": "abandon",
				"desc": "Gives the bounty up%s. Nothing is paid." % (", and your client goes home" if Quests.client() else "")}]
		else:
			mm.items = [{"label": "Take a bounty", "id": "take",
				"desc": "Posts a new notice for somewhere around the deepest floor you have reached."}]
		mm.items.append({"label": "Leave", "id": "leave"})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		match it.id:
			"take":
				Sfx.play("confirm")
				mm.say(Quests.offer())
				Game.save_run()
			"abandon":
				Sfx.play("back")
				Quests.abandon()
				mm.say("The notice comes down.")
				Game.save_run()
			"leave":
				return true
		return false
	m.detail = func(ci: CanvasItem, r: Rect2, _mm: Menu, _it: Dictionary):
		var p := r.position
		Gfx.text(ci, p + Vector2(14, 12), "The notice", Gfx.GOLD, 2)
		var y := p.y + 40
		if not Quests.active():
			for line in Gfx.wrap("Nothing pinned up. Six kinds of work come through here: a kill, a thing to fetch, a floor to thin out, a floor to reach against the clock, a floor to reach without a charm, and somebody to get there alive.", int(r.size.x - 28)):
				Gfx.text(ci, Vector2(p.x + 14, y), line, Gfx.GREY)
				y += 14
			return
		var q := Game.quest
		var kind: String = {"kill": "Hunt", "fetch": "Recovery", "clear": "Clearance", "timed": "Against the clock",
			"norecall": "No charms", "escort": "Escort"}[q.type]
		Gfx.text(ci, Vector2(p.x + 14, y), kind, Gfx.CYAN)
		y += 18
		for line in Gfx.wrap(Quests.summary(), int(r.size.x - 28)):
			Gfx.text(ci, Vector2(p.x + 14, y), line, Gfx.WHITE)
			y += 14
		y += 10
		Gfx.text(ci, Vector2(p.x + 14, y), "Pays", Gfx.CYAN)
		Gfx.text(ci, Vector2(p.x + 70, y), "%s gold, %d xp" % [Gfx.comma(int(q.gold)), int(q.xp)], Gfx.GOLD)
		var c := Quests.client()
		if c:
			Gfx.portrait(ci, Vector2(p.x + 14, y + 26), c.look, "neutral")
			Gfx.text(ci, Vector2(p.x + 92, y + 26), c.name, Party.color_of(c))
			Gfx.text(ci, Vector2(p.x + 92, y + 40), ClassesData.CLASSES[c.class_id].name, Gfx.GREY)
			Gfx.text(ci, Vector2(p.x + 92, y + 54), "Fights for themselves. Keep them alive.", Gfx.GREY)
	m.footer = "Z/Enter: choose    X/Esc: leave"
	m.refresh()


# ---- the Tavern ---------------------------------------------------------------------
## The Brass Lantern: heroes for hire, coin up front. Twenty on offer, each
## ten times the price of the last one you took; they fight on their own down
## there -- you pay them, you don't command them.
func _open_tavern() -> void:
	var m := _menu("The Brass Lantern")
	m.list_width = 300
	m.rows = 11
	m.data = {"confirm": -1}
	m.rebuild = func(mm: Menu):
		mm.items = []
		var filled := Party.count()
		mm.footer = "Z/Enter: hire    R: let go    X/Esc: leave    Party %d of %d" % [filled, Party.MAX_COMPANIONS]
		for i in Party.TAVERN_ROSTER:
			var live := Party.by_roster(i)
			var c: Hero = live if live else Party.candidate(i)
			var right := ""
			var col := Gfx.WHITE
			if Party.is_hired(i):
				right = "with you"
				col = Party.color_of(c)
			elif Party.has_fallen(i):
				right = Gfx.comma(Party.rehire_cost(filled))
				col = Gfx.RED.lightened(0.3)
			else:
				right = Gfx.comma(Party.hire_cost(filled))
			mm.items.append({"label": c.name, "right": right, "i": i, "color": col,
				"desc": "%s, %s.  %s" % [ClassesData.CLASSES[c.class_id].name, Party.PERSONALITY_NAMES[c.personality],
					ClassesData.CLASSES[c.class_id].tagline]})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if Party.is_hired(it.i):
			Sfx.play("buzz")
			mm.say("They already drink on your coin. R lets them go.")
			return false
		var before := Party.count()
		var line := Party.hire(it.i)
		if Party.count() > before:
			Sfx.play("buy")
			Game.save_run()
		else:
			Sfx.play("buzz")
		mm.say(line)
		return false
	m.on_key = func(mm: Menu, key: int) -> bool:
		if key != KEY_R:
			return false
		var i: int = mm.current().get("i", -1)
		if i < 0 or not Party.can_release(i):
			Sfx.play("buzz")
			mm.say("They don't work for you. There's nothing to end.")
			return true
		if mm.data.confirm != i:
			mm.data.confirm = i
			var refund := " Half the fee comes back." if Party.is_hired(i) else ""
			mm.say("Let %s go for good?%s Press R again." % [Party.candidate(i).name, refund])
			return true
		mm.data.confirm = -1
		Sfx.play("coin")
		mm.say(Party.release(i))
		Game.save_run()
		return true
	m.detail = _detail_hire
	m.refresh()
	Game.msg("They fight on their own down there -- you pay them, you don't command them.", Gfx.GREY)


func _detail_hire(ci: CanvasItem, r: Rect2, _m: Menu, it: Dictionary) -> void:
	if it.is_empty():
		return
	var live := Party.by_roster(it.i)
	var c: Hero = live if live else Party.candidate(it.i)
	var p := r.position
	Gfx.portrait(ci, p + Vector2(14, 14), c.look, "happy" if live else "neutral")
	var cs: Dictionary = ClassesData.CLASSES[c.class_id]
	Gfx.text(ci, p + Vector2(92, 12), cs.name, Party.color_of(c) if live else Gfx.GOLD, 2)
	Gfx.text(ci, p + Vector2(92, 34), "%s  /  %s" % [C.ARCHETYPE_NAMES[c.archetype], Party.PERSONALITY_NAMES[c.personality]], Gfx.GREY)
	Gfx.text(ci, p + Vector2(92, 48), ("Lv %d   %d kills" % [c.level, c.kills]) if live else "Lv %d" % c.level, Gfx.GREY)
	var stats := [["HP", ("%d/%d" % [c.hp, Party.max_hp(c)]) if live else str(c.maxhp)], ["ATK", c.eff_atk()],
		["DEF", c.eff_def()], ["Reach", Party.reach(c)]]
	for i in stats.size():
		var x := p.x + 14 + (i % 2) * 140
		var y := p.y + 90 + (i / 2) * 15
		Gfx.text(ci, Vector2(x, y), stats[i][0], Gfx.CYAN)
		Gfx.text_right(ci, Vector2(x + 120, y), str(stats[i][1]), Gfx.WHITE)
	var y2 := p.y + 128
	for line in Gfx.wrap(Party.weapon_line(c), int(r.size.x - 28)):
		Gfx.text(ci, Vector2(p.x + 14, y2), line, Gfx.WHITE)
		y2 += 13
	y2 += 4
	Gfx.text(ci, Vector2(p.x + 14, y2), Party.ABILITY_NAMES[c.archetype], Gfx.GOLD)
	y2 += 13
	for line in Gfx.wrap(Party.ABILITY_DESCS[c.archetype], int(r.size.x - 28)):
		Gfx.text(ci, Vector2(p.x + 14, y2), line, Gfx.GREY)
		y2 += 13
	for g in c.gear:
		Gfx.text(ci, Vector2(p.x + 14, y2), "%s  +%d/+%d/+%d" % [g.name, g.atk, g.def, g.hp], Gfx.GREEN)
		y2 += 13
	if Party.has_fallen(it.i):
		Gfx.text(ci, Vector2(p.x + 14, y2 + 2), "Died in your service. A quarter to take them back.", Gfx.RED)


func _open_gladiator() -> void:
	var h := Game.hero
	var m := _menu("Gladiator School")
	m.list_width = 260
	m.rows = 11
	m.rebuild = func(mm: Menu):
		mm.items = []
		for i in 30:
			var p := ItemsData.training_price(h.attrs[i])
			mm.items.append({"label": "%s  %d" % [C.ATTR_NAMES[i], h.attrs[i]], "right": Gfx.comma(p), "price": p, "a": i,
				"desc": _attr_desc(i).replace("  Train it at the Gladiator School.", "") + "  One point, %s gold%s." % [Gfx.comma(p), " (or a writ)" if Game.writs > 0 else ""]})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if Game.writs > 0:
			Game.writs -= 1
		elif not _buy(mm, it.price):
			return false
		h.shift_attribute(-1, it.a)
		Sfx.play("levelup")
		mm.say("The drill-master works you until you drop. %s is now %d." % [C.ATTR_NAMES[it.a], h.attrs[it.a]])
		return false
	m.detail = _detail_hero
	m.footer = "Z/Enter: train    X/Esc: leave"
	m.refresh()


func _open_junkyard() -> void:
	var m := _menu("Junkyard")
	m.rebuild = func(mm: Menu):
		mm.items = []
		for tier in 3:
			var n := int(Game.mats[tier])
			mm.items.append({"label": "Sell the %s (%d)" % [C.MAT_NAMES[tier], n], "right": Gfx.comma(int(Game.mat_value[tier])),
				"tier": tier, "enabled": n > 0, "why": "You have no %s." % C.MAT_NAMES[tier],
				"color": [Gfx.WHITE, Gfx.CYAN, Gfx.GOLD][tier],
				"desc": ("Scrap comes off the walls as you walk -- a piece every hundred steps -- and out of wrecks." if tier == 0
					else "The smith wants %s for the rungs past +%d. Selling it is a choice you can regret." % [C.MAT_NAMES[tier], C.UPGRADE_PLATINUM_FROM if tier == 1 else C.UPGRADE_DIAMOND_FROM])})
		mm.items.append({"label": "Leave", "tier": -1})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if it.tier < 0:
			return true
		var g := Game.gain_gold(int(Game.mat_value[it.tier]))
		mm.say("The scales creak. %d %s, %s gold." % [int(Game.mats[it.tier]), C.MAT_NAMES[it.tier], Gfx.comma(g)])
		Game.mats[it.tier] = 0
		Game.mat_value[it.tier] = 0
		Sfx.play("coin")
		Game.save_run()
		return false
	m.detail = _detail_hero
	m.refresh()


func _open_blackmarket() -> void:
	var h := Game.hero
	var m := _menu("The Black Market")
	m.rebuild = func(mm: Menu):
		var p := ItemsData.level_sale_price(h.level)
		mm.items = [{"label": "Sell a level (%d -> %d)" % [h.level, h.level - 1], "right": Gfx.comma(p), "price": p,
			"enabled": h.level > 1, "why": "There is nothing underneath level 1.",
			"desc": "You lose 8 max HP, a point of attack, a point of defence on an even level, and progress to the next. You walk out with coin."}]
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if Rules.sell_level():
			Game.gold += it.price
			Sfx.play("coin")
			mm.say("A cold hand on your wrist, and the feeling of something leaving. +%s gold." % Gfx.comma(it.price))
		return false
	m.detail = _detail_hero


func _open_oracle() -> void:
	var m := _menu("The Oracle")
	m.rebuild = func(mm: Menu):
		mm.items = [
			{"label": "Where the way down is", "right": C.ORACLE_STAIRS_PRICE, "price": Game.discounted(C.ORACLE_STAIRS_PRICE), "r": "stairs",
				"desc": "On the next floor you enter, you will know where the stairs are."},
			{"label": "The whole of the next floor", "right": C.ORACLE_FLOOR_PRICE, "price": Game.discounted(C.ORACLE_FLOOR_PRICE), "r": "depth",
				"desc": "The next floor you enter, mapped before you walk it."},
		]
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if Game.oracle_reading != "":
			mm.say("You are already carrying a reading.")
			return false
		if not _buy(mm, it.price):
			return false
		Game.oracle_reading = it.r
		mm.say("The Oracle tells you, and you remember it.")
		return false
	m.detail = _detail_text.bind("The Oracle", "Sells a true answer about what is below. Spent on the next floor you enter.")
	m.refresh()


func _open_temple() -> void:
	var m := _menu("Temple of the Deep Well")
	m.show_gold = false
	m.rebuild = func(mm: Menu):
		mm.items = []
		var top := maxi(1, Game.deepest_floor)
		for f in range(top, 0, -1):
			var b := C.biome_for_floor(f)
			mm.items.append({"label": "Floor %d" % f, "right": C.BIOME_SHORT[b], "f": f,
				"desc": "Descend to floor %d, in %s." % [f, C.BIOME_NAMES[b]]})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		modal = null
		Game.save_run()
		Game.go_to_floor(it.f, true, func(): view.snap())
		Sfx.play("stairs")
		return true
	m.detail = _detail_text.bind("The way down", "You can go straight down to any floor you have already reached.")
	m.refresh()


func _open_merchant() -> void:
	var m := _menu("Merchant")
	m.rebuild = func(mm: Menu):
		mm.items = []
		for tpl in ItemsData.MERCHANT:
			var p := Game.discounted(tpl.price)
			mm.items.append({"label": tpl.name, "right": p, "price": p, "tpl": tpl, "desc": _consumable_desc(tpl)})
	m.on_select = func(mm: Menu, it: Dictionary) -> bool:
		if _buy(mm, it.price):
			Game.give(it.tpl.name)
			mm.say("You buy a %s." % it.tpl.name)
		return false
	m.detail = _detail_text.bind("A trader", "Somebody came down and stayed. The prices show it.")
	m.refresh()


# ---- the end ---------------------------------------------------------------------
func _open_death() -> void:
	var hardcore := Game.difficulty == C.Difficulty.HARDCORE
	var best := Game.record_highscore()
	Game.record_fallen()      # onto the roster the barrows are raised from
	var died_on := Game.depth
	var lines := ["Slain by %s on floor %d." % [Game.killed_by, died_on],
		"Level %d. Deepest floor %d. Best ever: %d." % [Game.hero.level, Game.deepest_floor, best]]
	if hardcore:
		lines.append("Hardcore: nobody is coming to fetch you back.")
		_show("You have died", lines, func():
			Game.delete_run()
			_open_title())
	else:
		lines.append("You wake in your room at the Inn, whole, and some hours older.")
		_show("You have died", lines, func():
			if Game.load_inn_snapshot():
				Game.log_lines = []
				Game.msg("You wake in your room at the Inn, whole, and some hours older.", Color8(150, 200, 255))
				Game.msg("Whatever happened on floor %d, you are not carrying it." % died_on, Gfx.GREY)
				for b in Game.party:
					if b.alive:
						b.hp = Party.max_hp(b)
				Game.enter_town(Vector2i(5, 16))
				Game.save_run()
				view.snap()
			else:
				Game.delete_run()
				_open_title())


func _open_win() -> void:
	Game.deepest_floor = C.MAX_FLOOR
	Game.record_highscore()
	_show("The Warden falls", ["At the bottom of the Deep Well, nothing is left standing but you.",
		"%s the %s, level %d. One hundred floors down." % [Game.hero.name, Game.hero.class_name_str(), Game.hero.level]],
		func():
			Game.delete_run()
			_open_title())


# ---- drawing -----------------------------------------------------------------------
func _draw_ui() -> void:
	var ci := ui
	match mode:
		"title":
			_draw_title(ci)
		"create":
			ci.draw_rect(Rect2(0, 0, W, H), Color8(8, 8, 24))
			if modal:
				modal.draw(ci, t)
			if c_step == "name":
				ci.draw_rect(Rect2(0, 0, W, H), Color(0, 0, 0.1, 0.5))
				_draw_name_entry(ci)
			var labels := {"difficulty": "1/4  Difficulty", "world": "2/4  World size", "class": "3/4  Class", "name": "4/4  Name"}
			Gfx.text_right(ci, Vector2(W - 16, 17), labels[c_step], Gfx.CYAN)
		"game":
			if not Game.pending_floor.is_empty():
				_draw_descent(ci)
				return
			if modal:
				ci.draw_rect(Rect2(0, 0, W, H), Color(0.02, 0.02, 0.12, 0.6))
				modal.draw(ci, t)
			if auto:
				Gfx.window(ci, Rect2(W / 2 - 70, 92, 140, 18), Color8(40, 90, 60), Color8(10, 40, 20))
				Gfx.text_center(ci, W / 2, 97, "Auto-exploring...", Gfx.WHITE)
			if Game.depth == 0 and modal == null:
				_draw_door_labels(ci)
	if not dialog.is_empty():
		ci.draw_rect(Rect2(0, 0, W, H), Color(0, 0, 0.1, 0.45))
		var lines: Array = []
		for l in dialog.lines:
			lines.append_array(Gfx.wrap(l, 440))
		var hh := 54 + lines.size() * 15
		var r := Rect2(90, (H - hh) / 2, 460, hh)
		Gfx.window(ci, r)
		Gfx.text(ci, r.position + Vector2(14, 10), dialog.title, Gfx.GOLD, 2)
		for i in lines.size():
			Gfx.text(ci, r.position + Vector2(14, 36 + i * 15), lines[i], Gfx.WHITE)
		Gfx.text(ci, r.position + Vector2(r.size.x - 22, r.size.y - 14), "▼", Gfx.GOLD, 1, false)


func _draw_door_labels(ci: CanvasItem) -> void:
	var h := Game.hero
	var m := Game.map
	for k in m.doors:
		var x: int = k % m.w
		var y: int = k / m.w
		if absi(x - h.x) > 4 or absi(y - h.y) > 4:
			continue
		var label := Town.label(m.doors[k])
		var w := Gfx.text_width(label) + 14
		var sp := Vector2(x * 32 + 16, y * 32) + view.world.position
		var r := Rect2(roundi(sp.x - w / 2.0), sp.y - 46, w, 17)
		Gfx.window(ci, r)
		Gfx.text(ci, r.position + Vector2(7, 5), label, Gfx.WHITE)
	if absi(Town.BOARD.x - h.x) <= 3 and absi(Town.BOARD.y - h.y) <= 3:
		var sp := Vector2(Town.BOARD.x * 32 + 16, Town.BOARD.y * 32) + view.world.position
		var label := "Bounty Board" + (" (1 notice)" if Quests.active() else "")
		var w := Gfx.text_width(label) + 14
		var r := Rect2(roundi(sp.x - w / 2.0), sp.y - 22, w, 17)
		Gfx.window(ci, r)
		Gfx.text(ci, r.position + Vector2(7, 5), label, Gfx.GOLD)
	if absi(Town.TEMPLE.x - h.x) <= 3 and absi(Town.TEMPLE.y - h.y) <= 3:
		var sp := Vector2(Town.TEMPLE.x * 32 + 16, Town.TEMPLE.y * 32) + view.world.position
		var label := "Temple of the Deep Well"
		var w := Gfx.text_width(label) + 14
		var r := Rect2(roundi(sp.x - w / 2.0), sp.y - 22, w, 17)
		Gfx.window(ci, r)
		Gfx.text(ci, r.position + Vector2(7, 5), label, Gfx.GOLD)
