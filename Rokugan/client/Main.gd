# Rokugan -- the table.
#
# Two skins over one board.  Classic is the 1997 Windows window from the
# first mockup: grey bevels, a wooden table, rebuilt parchment cards with
# the stat block Force | Chi | Cost over Honor Req. | Personal Honor.
# Modern is the dark lacquer one from the second mockup: painted panels,
# portraits, ring tokens, the phase list and the log down the right.
#
# Everything is drawn here, immediate-mode, and every drawn thing that
# can be clicked leaves a hit rectangle behind it.  That is what lets the
# two skins share one input path: they differ in how they paint, not in
# what a click means.
extends Control

const PROC := preload("res://Proc.gd")
const DB := preload("res://CardDB.gd")

const W := 1600.0
const H := 1000.0

const CLAN_COLOR := {
	"Crab": Color(0.27, 0.36, 0.46), "Crane": Color(0.42, 0.66, 0.84),
	"Dragon": Color(0.20, 0.50, 0.32), "Lion": Color(0.80, 0.63, 0.16),
	"Phoenix": Color(0.86, 0.45, 0.16), "Scorpion": Color(0.66, 0.13, 0.13),
	"Unicorn": Color(0.45, 0.27, 0.64), "Mantis": Color(0.13, 0.55, 0.45),
	"Spider": Color(0.22, 0.20, 0.22), "Unaligned": Color(0.45, 0.40, 0.34),
}
const CLAN_MON := {
	"Crab": "蟹", "Crane": "鶴", "Dragon": "龍", "Lion": "獅", "Phoenix": "鳳",
	"Scorpion": "蠍", "Unicorn": "麒", "Mantis": "蟷", "Spider": "蜘",
	"Unaligned": "浪",
}
const RINGS := [["Earth", "地"], ["Water", "水"], ["Fire", "火"], ["Air", "風"], ["Void", "空"]]
const ERAS := [["gold", "Gold (Four Winds)"], ["celestial", "Celestial"], ["ivory", "Ivory"]]
const PHASES := ["Straighten", "Action", "Attack", "Dynasty", "End"]

# A_ kinds, as the engine numbers them
enum { A_PASS, A_BUY, A_CYCLE, A_PLAY, A_USE, A_ATTACK, A_NOATTACK, A_ASSIGN, A_DONE, A_DISCARD }

var root := ""
var proc
var db
var v := {}
var skin := "classic"
var era := "gold"
var my_clan := "Crab"
var opp_clan := "Random"
var message := ""

var hits: Array = []
var hover := {}
var mouse := Vector2.ZERO
var zoom_oid := -1
var show_log := false
var show_new := false
var popup: PopupMenu
var popup_cmds: Array = []

var f_ui: SystemFont
var f_bold: SystemFont
var f_serif: SystemFont
var f_cjk: SystemFont

# test hooks: godot --path client -- --shot=out.png --autoplay=40
var shot := ""
var shot_frames := 20
var autoplay := 0

# --------------------------------------------------------------- startup
func _ready() -> void:
	root = ProjectSettings.globalize_path("res://").get_base_dir().get_base_dir() + "/"
	f_cjk = _font(["Noto Serif CJK JP", "Noto Sans CJK JP", "IPAGothic", "IPAPGothic",
			"WenQuanYi Zen Hei", "Yu Mincho", "MS Mincho", "Hiragino Mincho ProN"], 400)
	f_ui = _font(["Tahoma", "Microsoft Sans Serif", "Liberation Sans", "DejaVu Sans", "Arial"], 400)
	f_bold = _font(["Tahoma", "Liberation Sans", "DejaVu Sans", "Arial"], 700)
	f_serif = _font(["Georgia", "Palatino Linotype", "DejaVu Serif", "Liberation Serif", "Times New Roman"], 700)
	popup = PopupMenu.new()
	add_child(popup)
	popup.id_pressed.connect(_on_popup)
	db = DB.new()
	proc = PROC.new()
	_load_settings()
	_parse_args()
	_new_game(int(Time.get_unix_time_from_system()) % 100000)
	if shot == "" and v.is_empty():
		show_new = true

func _font(names: Array, weight: int) -> SystemFont:
	var f := SystemFont.new()
	f.font_names = PackedStringArray(names)
	f.font_weight = weight
	if f_cjk and weight >= 0:
		f.fallbacks = [f_cjk]
	return f

func _parse_args() -> void:
	for a in OS.get_cmdline_user_args():
		var kv: PackedStringArray = a.trim_prefix("--").split("=", true, 1)
		var val := kv[1] if kv.size() > 1 else ""
		match kv[0]:
			"shot": shot = val
			"frames": shot_frames = int(val)
			"autoplay": autoplay = int(val)
			"skin": skin = val
			"era": era = val
			"clan0": my_clan = val
			"clan1": opp_clan = val

func _load_settings() -> void:
	var c := ConfigFile.new()
	if c.load("user://settings.cfg") == OK:
		skin = c.get_value("ui", "skin", skin)
		era = c.get_value("game", "era", era)
		my_clan = c.get_value("game", "clan", my_clan)
		opp_clan = c.get_value("game", "opponent", opp_clan)

func _save_settings() -> void:
	var c := ConfigFile.new()
	c.set_value("ui", "skin", skin)
	c.set_value("game", "era", era)
	c.set_value("game", "clan", my_clan)
	c.set_value("game", "opponent", opp_clan)
	c.save("user://settings.cfg")

func clans_for(e: String) -> Array:
	var out: Array = []
	for f in DirAccess.get_files_at(root + "decks/" + e):
		if f.ends_with(".txt"):
			out.append(f.get_basename())
	out.sort()
	return out

func _new_game(seed: int) -> void:
	message = db.load_era(root, era)
	if message != "":
		v = {}
		queue_redraw()
		return
	var clans := clans_for(era)
	if clans.is_empty():
		message = "No decks for this era in decks/%s/." % era
		return
	if not my_clan in clans:
		my_clan = clans[0]
	var opp: String = opp_clan
	if not opp in clans:
		opp = clans[seed % clans.size()]
	if proc.running():
		v = proc.send("new %s %s %s %d" % [era, my_clan, opp, seed])
	else:
		message = proc.start(root + "rokugan", PackedStringArray(["--serve", "--home", root,
				"--era", era, "--clan0", my_clan, "--clan1", opp, "--seed", str(seed)]))
		if message == "":
			v = proc.read_view()
	if v.has("error"):
		message = v["error"]
		v = {}
	_save_settings()
	queue_redraw()

func _exit_tree() -> void:
	if proc:
		proc.stop()

func _process(_d: float) -> void:
	if shot == "":
		return
	if autoplay > 0 and not v.is_empty():
		_autoplay_step()
		autoplay -= 1
		queue_redraw()
		return
	shot_frames -= 1
	if shot_frames == 0:
		var img := get_viewport().get_texture().get_image()
		img.save_png(shot)
		get_tree().quit()

# Prefer doing something over passing, so a screenshot shows a game.
func _autoplay_step() -> void:
	var acts: Array = v.get("actions", [])
	if acts.is_empty() or v.get("winner", -1) >= 0:
		autoplay = 0
		return
	var pick := 0
	for i in acts.size():
		var k: int = acts[i]["k"]
		if k == A_BUY or (k == A_PLAY and acts[i]["p"] >= 0) or k == A_ATTACK or k == A_USE:
			pick = i
			break
		if k == A_ASSIGN:
			pick = i
			break
	_act(pick)

# ------------------------------------------------------------- the wire
func _act(i: int) -> void:
	_send("act %d" % i)

func _send(cmd: String) -> void:
	var r: Dictionary = proc.send(cmd)
	if r.has("error"):
		message = r["error"]
	else:
		v = r
	queue_redraw()

func me() -> Dictionary:
	return v["players"][v["seat"]]

func opp() -> Dictionary:
	return v["players"][1 - int(v["seat"])]

func actions_for(inst: int) -> Array:
	var out: Array = []
	var acts: Array = v.get("actions", [])
	for i in acts.size():
		if acts[i]["s"] == inst:
			out.append(i)
	return out

func actions_for_prov(k: int) -> Array:
	var out: Array = []
	var acts: Array = v.get("actions", [])
	for i in acts.size():
		var kind: int = acts[i]["k"]
		if acts[i]["v"] == k and (kind == A_BUY or kind == A_CYCLE):
			out.append(i)
	return out

func global_actions() -> Array:
	var out: Array = []
	var acts: Array = v.get("actions", [])
	for i in acts.size():
		if acts[i]["s"] < 0:
			out.append(i)
	return out

func my_turn() -> bool:
	return not v.is_empty() and int(v["active"]) == int(v["seat"])

func my_say() -> bool:
	return not v.is_empty() and int(v["decider"]) == int(v["seat"])

func phase_label() -> String:
	var ph: String = v.get("phase", "")
	match ph:
		"Assign", "Defend", "Battle": return "Attack"
		"Discard": return "End"
	return ph

# ---------------------------------------------------------------- input
func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseMotion:
		mouse = e.position
		var h := _hit_at(mouse)
		if h.get("oid", -1) != hover.get("oid", -1) or h.get("inst", -1) != hover.get("inst", -1):
			hover = h
			queue_redraw()
	elif e is InputEventMouseButton and e.pressed:
		var h := _hit_at(e.position)
		if zoom_oid >= 0:
			zoom_oid = -1
			queue_redraw()
			return
		if e.button_index == MOUSE_BUTTON_LEFT:
			_click(h)
		elif e.button_index == MOUSE_BUTTON_RIGHT:
			_right_click(h)

func _hit_at(p: Vector2) -> Dictionary:
	for i in range(hits.size() - 1, -1, -1):
		if hits[i]["r"].has_point(p):
			return hits[i]
	return {}

func _hit(r: Rect2, kind: String, extra := {}) -> void:
	var h := {"r": r, "kind": kind}
	h.merge(extra)
	hits.append(h)

func _click(h: Dictionary) -> void:
	if h.is_empty():
		return
	match h["kind"]:
		"act":
			_act(h["idx"])
		"menu":
			_open_menu(h["name"], h["r"])
		"quit":
			get_tree().quit()
		"ng":
			_new_game_click(h)
		"log":
			show_log = not show_log
			queue_redraw()
		"close":
			show_log = false
			show_new = false
			queue_redraw()
		"ok", "ok_any":
			message = ""
			queue_redraw()
		"prov":
			var acts := actions_for_prov(h["prov"])
			if acts.size() == 1:
				_act(acts[0])
			elif acts.size() > 1:
				_menu_of(acts, h["r"])
		"card":
			var acts := actions_for(h["inst"])
			if acts.is_empty():
				if h.get("oid", -1) >= 0:
					zoom_oid = h["oid"]
					queue_redraw()
				return
			_menu_of(acts, h["r"], h.get("oid", -1))

func _menu_of(acts: Array, r: Rect2, oid := -1) -> void:
	popup.clear()
	popup_cmds.clear()
	for i in acts:
		popup.add_item(v["actions"][i]["l"], popup_cmds.size())
		popup_cmds.append("act %d" % i)
	if oid >= 0:
		popup.add_separator()
		popup.add_item("View card", popup_cmds.size())
		popup_cmds.append("zoom %d" % oid)
	_show_popup(r)

func _show_popup(_r: Rect2) -> void:
	popup.reset_size()
	popup.position = DisplayServer.mouse_get_position()
	popup.popup()

func _right_click(h: Dictionary) -> void:
	if h.is_empty() or v.is_empty():
		return
	popup.clear()
	popup_cmds.clear()
	if h["kind"] == "card":
		var inst: int = h["inst"]
		var mine: bool = h.get("mine", false)
		var zone: String = h.get("zone", "")
		if mine and zone == "play":
			for m in [["bow", "Bow"], ["straighten", "Straighten"], ["destroy", "Destroy"],
					["home", "Send home"], ["force+", "+1 Force"], ["force-", "-1 Force"]]:
				popup.add_item(m[1] + "  (by hand)", popup_cmds.size())
				popup_cmds.append("manual %s %d" % [m[0], inst])
		elif mine and zone == "hand":
			popup.add_item("Discard  (by hand)", popup_cmds.size())
			popup_cmds.append("manual discard %d" % inst)
		if h.get("oid", -1) >= 0:
			popup.add_item("View card", popup_cmds.size())
			popup_cmds.append("zoom %d" % h["oid"])
	elif h["kind"] == "plaque":
		for m in [["honor+", "Gain 1 honor"], ["honor-", "Lose 1 honor"], ["draw", "Draw a card"]]:
			popup.add_item(m[1] + "  (by hand)", popup_cmds.size())
			popup_cmds.append("manual %s -1" % m[0])
	if popup_cmds.size():
		_show_popup(h["r"])

func _on_popup(id: int) -> void:
	var cmd: String = popup_cmds[id]
	if cmd.begins_with("zoom "):
		zoom_oid = int(cmd.substr(5))
		queue_redraw()
	elif cmd.begins_with("menu:"):
		_menu_command(cmd.substr(5))
	else:
		_send(cmd)

func _open_menu(name: String, r: Rect2) -> void:
	popup.clear()
	popup_cmds.clear()
	var items: Array = []
	match name:
		"File": items = [["New Game...", "new"], ["Quit", "quit"]]
		"Options": items = [["Classic skin (1997)", "skin classic"], ["Modern skin", "skin modern"]]
		"Help": items = [["How to play", "rules"], ["About", "about"]]
	for it in items:
		popup.add_item(it[0], popup_cmds.size())
		popup_cmds.append("menu:" + it[1])
	_show_popup(Rect2(r.position + Vector2(0, r.size.y * 0.5), Vector2(0, r.size.y)))

func _menu_command(c: String) -> void:
	match c:
		"new": show_new = true
		"quit": get_tree().quit()
		"skin classic": skin = "classic"
		"skin modern": skin = "modern"
		"rules": message = RULES
		"about": message = ABOUT
	_save_settings()
	queue_redraw()

func _new_game_click(h: Dictionary) -> void:
	match h["what"]:
		"open":
			show_new = true
		"era":
			era = h["val"]
		"clan":
			my_clan = h["val"]
		"opp":
			opp_clan = h["val"]
		"skin":
			skin = h["val"]
		"start":
			show_new = false
			_new_game(int(Time.get_unix_time_from_system()) % 100000)
	queue_redraw()

const RULES := """Win with 40 Family Honor at the start of your turn, by destroying all four
enemy Provinces, or with five Rings in play.  Fall to -20 and you lose.

Your turn: your cards straighten and your Provinces turn face up.  Then
players take Open actions in turn (and you Limited ones) until both pass.
You may attack: send units at enemy Provinces, the defender meets them,
and each battle is fought with Battle actions, then resolved -- the lower
Force army is destroyed, and the Province falls if the attacker beats the
defenders by more than its Strength.  Then recruit from your Provinces
with gold from bowing Holdings, and draw a Fate card.

Left-click a card for what it can do.  Right-click for the table commands
-- bow, destroy, +Force, honor -- to carry out a card the engine plays
by hand (marked 手)."""

const ABOUT := """Rokugan -- the Legend of the Five Rings CCG (1995-2015).
Card data and images: the Oracle of the Void (oracleofthevoid.com),
imported locally.  Legend of the Five Rings is (c) AEG / Fantasy Flight."""

# ============================================================== drawing
func _draw() -> void:
	hits.clear()
	if skin == "modern":
		_draw_modern()
	else:
		_draw_classic()
	_draw_overlays()

# ---------------------------------------------------------- primitives
func _text(p: Vector2, s: String, size: int, col: Color, font: Font = null, width := -1.0,
		align := HORIZONTAL_ALIGNMENT_LEFT) -> void:
	draw_string(font if font else f_ui, p, s, align, width, size, col)

func _fit(s: String, font: Font, size: int, width: float) -> int:
	var sz := size
	while sz > 7 and font.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, sz).x > width:
		sz -= 1
	return sz

func _bevel(r: Rect2, raised := true, fill := Color(0.75, 0.75, 0.75)) -> void:
	draw_rect(r, fill)
	var lt := Color.WHITE if raised else Color(0.25, 0.25, 0.25)
	var rb := Color(0.25, 0.25, 0.25) if raised else Color.WHITE
	draw_line(r.position, Vector2(r.end.x, r.position.y), lt, 2)
	draw_line(r.position, Vector2(r.position.x, r.end.y), lt, 2)
	draw_line(Vector2(r.position.x, r.end.y), r.end, rb, 2)
	draw_line(Vector2(r.end.x, r.position.y), r.end, rb, 2)

func _button95(r: Rect2, label: String, enabled := true, size := 14) -> void:
	_bevel(r, true)
	var col := Color.BLACK if enabled else Color(0.5, 0.5, 0.5)
	var sz := _fit(label, f_ui, size, r.size.x - 8)
	_text(Vector2(r.position.x, r.position.y + r.size.y * 0.5 + sz * 0.36), label, sz, col, f_ui,
			r.size.x, HORIZONTAL_ALIGNMENT_CENTER)

func _lacquer(r: Rect2, alpha := 0.82) -> void:
	draw_rect(r, Color(0.07, 0.05, 0.04, alpha))
	draw_rect(r, Color(0.70, 0.54, 0.28), false, 2)
	draw_rect(r.grow(-4), Color(0.70, 0.54, 0.28, 0.35), false, 1)

func _gold_button(r: Rect2, label: String, lit := false, enabled := true) -> void:
	var fill := Color(0.30, 0.22, 0.10) if lit else Color(0.12, 0.10, 0.08)
	draw_rect(r, fill)
	draw_rect(r, Color(0.78, 0.62, 0.32) if enabled else Color(0.4, 0.35, 0.3), false, 2)
	var col := Color(0.98, 0.90, 0.70) if enabled else Color(0.5, 0.45, 0.4)
	var sz := _fit(label, f_serif, mini(19, int(r.size.y * 0.55)), r.size.x - 12)
	_text(Vector2(r.position.x, r.position.y + r.size.y * 0.5 + sz * 0.36), label, sz, col, f_serif,
			r.size.x, HORIZONTAL_ALIGNMENT_CENTER)

func _mon(c: Vector2, rad: float, clan: String, ring := Color(0.85, 0.68, 0.30)) -> void:
	var col: Color = CLAN_COLOR.get(clan, CLAN_COLOR["Unaligned"])
	draw_circle(c, rad, ring)
	draw_circle(c, rad * 0.86, col.darkened(0.25))
	var k: String = CLAN_MON.get(clan, "浪")
	var sz := int(rad * 1.1)
	_text(Vector2(c.x - rad, c.y + sz * 0.38), k, sz, Color(1, 0.95, 0.85), f_cjk, rad * 2,
			HORIZONTAL_ALIGNMENT_CENTER)

func _coin(c: Vector2, rad: float, k: String) -> void:
	draw_circle(c, rad, Color(0.55, 0.40, 0.10))
	draw_circle(c, rad * 0.88, Color(0.93, 0.76, 0.30))
	draw_arc(c, rad * 0.72, 0, TAU, 32, Color(0.55, 0.40, 0.10), 2)
	var sz := int(rad * 1.0)
	_text(Vector2(c.x - rad, c.y + sz * 0.38), k, sz, Color(0.25, 0.15, 0.05), f_cjk, rad * 2,
			HORIZONTAL_ALIGNMENT_CENTER)

func _wood(r: Rect2) -> void:
	draw_rect(r, Color(0.36, 0.23, 0.13))
	var y := r.position.y
	var i := 0
	while y < r.end.y:
		var shade := 0.03 * sin(i * 1.7) + 0.02 * sin(i * 0.37)
		draw_line(Vector2(r.position.x, y), Vector2(r.end.x, y), Color(0.30 + shade, 0.19 + shade, 0.10), 1)
		y += 3 + (i % 3)
		i += 1

# ---------------------------------------------------------------- cards
func _row(oid: int) -> Dictionary:
	return db.card(oid)

# JSON numbers arrive as floats; a card never says "5.0".
func _n(x) -> String:
	return str(int(float(x)))

func _int(d: Dictionary, k: String) -> String:
	return str(d.get(k, "0"))

func _clan_of(row: Dictionary) -> String:
	var c: String = row.get("clan", "Unaligned")
	return c if CLAN_COLOR.has(c) else "Unaligned"

# A card from a View: draws face or back, upright or bowed, and leaves a
# hit rectangle for clicks and hover.
func card(r: Rect2, c, opts := {}) -> void:
	var bowed: bool = c is Dictionary and c.get("b", 0) == 1
	var face := r
	if bowed:
		# Bowed: turned a quarter, and shrunk to stay inside its slot.
		var s := r.size.x / r.size.y
		var ctr := r.get_center()
		draw_set_transform(ctr, PI * 0.5, Vector2(s, s))
		face = Rect2(-r.size * 0.5, r.size)
	if c == null or not (c is Dictionary):
		_back(face, opts.get("back", "province"))
	elif skin == "modern":
		_face_modern(face, c, opts)
	else:
		_face_classic(face, c, opts)
	if bowed:
		draw_set_transform(Vector2.ZERO, 0, Vector2.ONE)
	if c is Dictionary:
		var acts := actions_for(int(c["i"]))
		if acts.size() and my_say():
			draw_rect(r.grow(3), Color(1.0, 0.82, 0.25, 0.95), false, 3)
		if int(c.get("at", -1)) >= 0:
			var tag := Rect2(r.position.x, r.end.y - 18, r.size.x, 18)
			draw_rect(tag, Color(0.6, 0.05, 0.05, 0.9))
			_text(Vector2(tag.position.x, tag.end.y - 4), "⚔ Province %d" % (int(c["at"]) + 1), 12,
					Color.WHITE, f_bold, tag.size.x, HORIZONTAL_ALIGNMENT_CENTER)
		_hit(r, "card", {"inst": int(c["i"]), "oid": int(c["o"]), "mine": opts.get("mine", false),
				"zone": opts.get("zone", "")})

func _back(r: Rect2, kind: String) -> void:
	if skin == "modern":
		draw_rect(r, Color(0.10, 0.07, 0.06))
		draw_rect(r, Color(0.66, 0.50, 0.26), false, 2)
		var red := Color(0.55, 0.10, 0.08) if kind != "fate" else Color(0.16, 0.20, 0.36)
		draw_circle(r.get_center(), r.size.x * 0.28, red)
		_swirl(r.get_center(), r.size.x * 0.22, Color(0.85, 0.66, 0.30))
		return
	match kind:
		"fate":
			draw_rect(r, Color(0.36, 0.24, 0.12))
			draw_rect(r.grow(-5), Color(0.10, 0.20, 0.45))
			draw_rect(r.grow(-5), Color(0.80, 0.62, 0.25), false, 2)
			draw_circle(r.get_center(), r.size.x * 0.30, Color(0.80, 0.62, 0.25))
			draw_circle(r.get_center(), r.size.x * 0.26, Color(0.10, 0.20, 0.45))
			_swirl(r.get_center(), r.size.x * 0.22, Color(0.80, 0.62, 0.25))
		"dynasty":
			draw_rect(r, Color(0.36, 0.24, 0.12))
			draw_rect(r.grow(-5), Color(0.30, 0.18, 0.08))
			draw_rect(r.grow(-5), Color(0.80, 0.62, 0.25), false, 2)
			draw_circle(r.get_center(), r.size.x * 0.30, Color(0.80, 0.62, 0.25))
			draw_circle(r.get_center(), r.size.x * 0.26, Color(0.30, 0.18, 0.08))
			_swirl(r.get_center(), r.size.x * 0.22, Color(0.80, 0.62, 0.25))
		_:
			# A face-down Province: the sun of Amaterasu, as in the mockup
			draw_rect(r, Color(0.33, 0.21, 0.10))
			draw_rect(r.grow(-4), Color(0.55, 0.38, 0.16))
			draw_rect(r.grow(-4), Color(0.88, 0.70, 0.30), false, 2)
			var c := r.get_center()
			var rad := r.size.x * 0.28
			for i in 16:
				var a := TAU * i / 16.0
				var pts := PackedVector2Array([c + Vector2(cos(a - 0.12), sin(a - 0.12)) * rad * 0.9,
						c + Vector2(cos(a), sin(a)) * rad * 1.45, c + Vector2(cos(a + 0.12), sin(a + 0.12)) * rad * 0.9])
				draw_colored_polygon(pts, Color(0.95, 0.78, 0.30))
			draw_circle(c, rad, Color(0.95, 0.78, 0.30))
			draw_circle(c, rad * 0.86, Color(0.98, 0.92, 0.70))
			var sz := int(rad * 0.62)
			_text(Vector2(c.x - rad, c.y - 2), "天", sz, Color(0.2, 0.1, 0.05), f_cjk, rad * 2, HORIZONTAL_ALIGNMENT_CENTER)
			_text(Vector2(c.x - rad, c.y + sz * 0.95), "照", sz, Color(0.2, 0.1, 0.05), f_cjk, rad * 2, HORIZONTAL_ALIGNMENT_CENTER)

func _swirl(c: Vector2, rad: float, col: Color) -> void:
	# a three-armed tomoe, near enough
	for k in 3:
		var a0 := TAU * k / 3.0
		draw_arc(c, rad * 0.55, a0, a0 + 2.2, 16, col, rad * 0.22)
		draw_circle(c + Vector2(cos(a0), sin(a0)) * rad * 0.55, rad * 0.16, col)

func _art(r: Rect2, oid: int, clan: String) -> void:
	var tex = db.art(oid)
	if tex:
		draw_texture_rect(tex, r, false)
	else:
		var col: Color = CLAN_COLOR.get(clan, CLAN_COLOR["Unaligned"])
		draw_rect(r, col.darkened(0.35))
		_mon(r.get_center(), minf(r.size.x, r.size.y) * 0.32, clan)

# The stat block of the first mockup, with the Chi box's second number
# named: it is the Honor Requirement.
func _face_classic(r: Rect2, c: Dictionary, opts: Dictionary) -> void:
	var row := _row(int(c["o"]))
	var t: String = c.get("t", row.get("type", ""))
	var small: bool = opts.get("small", false)
	var ink := Color(0.16, 0.10, 0.05)
	draw_rect(r, Color(0.42, 0.28, 0.14))
	draw_rect(r.grow(-3), Color(0.91, 0.85, 0.70))
	draw_rect(r.grow(-3), Color(0.55, 0.40, 0.20), false, 1)
	var pad := r.size.x * 0.05
	var title := Rect2(r.position.x + pad, r.position.y + pad, r.size.x - pad * 2, r.size.y * 0.11)
	draw_rect(title, Color(0.86, 0.77, 0.56))
	draw_rect(title, Color(0.55, 0.40, 0.20), false, 1)
	var name: String = c.get("n", "")
	var tsz := _fit(name, f_serif, int(r.size.y * 0.062), title.size.x - 6)
	_text(Vector2(title.position.x, title.position.y + title.size.y * 0.5 + tsz * 0.36), name, tsz, ink,
			f_serif, title.size.x, HORIZONTAL_ALIGNMENT_CENTER)
	var art := Rect2(title.position.x, title.end.y + pad * 0.6, title.size.x, r.size.y * 0.44)
	_art(art, int(c["o"]), _clan_of(row))
	draw_rect(art, Color(0.45, 0.30, 0.15), false, 1)
	if int(c.get("auto", 1)) == 0 and t != "Personality" and t != "Holding":
		draw_circle(art.position + Vector2(10, 10), 9, Color(0.6, 0.1, 0.1))
		_text(art.position + Vector2(1, 15), "手", 13, Color.WHITE, f_cjk, 18, HORIZONTAL_ALIGNMENT_CENTER)
	var box := Rect2(art.position.x, art.end.y + pad * 0.6, art.size.x, r.end.y - art.end.y - pad * 1.6)
	if small:
		_small_stats(box, c, row, t, ink)
		return
	var fs := int(r.size.y * 0.055)
	match t:
		"Personality":
			var cw := box.size.x / 3.0
			var hh := box.size.y * 0.30
			for i in 3:
				var col := Rect2(box.position.x + cw * i, box.position.y, cw - 2, box.size.y)
				draw_rect(col, Color(0.95, 0.90, 0.78))
				draw_rect(col, Color(0.55, 0.40, 0.20), false, 1)
				draw_line(Vector2(col.position.x, col.position.y + hh), Vector2(col.end.x, col.position.y + hh),
						Color(0.55, 0.40, 0.20), 1)
			var f: String = _n(c.get("f", row.get("force", "0")))
			_cell(Rect2(box.position.x, box.position.y, cw - 2, hh), "Force " + f, fs, ink)
			_cell(Rect2(box.position.x + cw, box.position.y, cw - 2, hh), "Chi " + _n(c.get("c", row.get("chi", "0"))), fs, ink)
			_cell(Rect2(box.position.x + cw * 2, box.position.y, cw - 2, hh), "Cost " + _int(row, "cost"), fs, ink)
			var low := Rect2(box.position.x, box.position.y + hh, cw - 2, box.size.y - hh)
			_mon(low.get_center(), minf(low.size.x, low.size.y) * 0.36, _clan_of(row))
			var hr := _int(row, "hreq")
			_cell2(Rect2(box.position.x + cw, box.position.y + hh, cw - 2, box.size.y - hh), "Honor Req.", hr, fs, ink)
			_cell2(Rect2(box.position.x + cw * 2, box.position.y + hh, cw - 2, box.size.y - hh), "Personal Honor", _int(row, "ph"), fs, ink)
		_:
			var txt: String = db.text(int(c["o"]))
			draw_rect(box, Color(0.95, 0.90, 0.78))
			draw_rect(box, Color(0.55, 0.40, 0.20), false, 1)
			var head := t
			if t == "Holding" or t == "Stronghold":
				head = "%s  ·  %s gold" % [t, _int(row, "gold")]
			elif t == "Follower" or t == "Item":
				head = "%s  ·  %+dF%s" % [t, int(row.get("force", "0")),
						("  %+dC" % int(row.get("chi", "0"))) if int(row.get("chi", "0")) else ""]
			_text(box.position + Vector2(4, fs + 1), head, int(fs * 0.85), Color(0.45, 0.25, 0.08), f_bold, box.size.x - 8)
			draw_multiline_string(f_ui, box.position + Vector2(4, fs * 2 + 2), txt, HORIZONTAL_ALIGNMENT_LEFT,
					box.size.x - 8, int(fs * 0.78), int((box.size.y - fs * 1.6) / (fs * 0.92)), ink)
			if row.get("cost", "0") != "0" or t in ["Holding", "Follower", "Item"]:
				_coin(Vector2(box.end.x - 13, box.end.y - 13), 11, _int(row, "cost"))

func _cell(r: Rect2, s: String, size: int, ink: Color) -> void:
	var sz := _fit(s, f_bold, size, r.size.x - 4)
	_text(Vector2(r.position.x, r.position.y + r.size.y * 0.5 + sz * 0.36), s, sz, ink, f_bold, r.size.x,
			HORIZONTAL_ALIGNMENT_CENTER)

func _cell2(r: Rect2, label: String, val: String, size: int, ink: Color) -> void:
	var lsz := _fit(label, f_ui, int(size * 0.75), r.size.x - 4)
	_text(Vector2(r.position.x, r.position.y + lsz + 2), label, lsz, Color(0.40, 0.25, 0.10), f_ui, r.size.x,
			HORIZONTAL_ALIGNMENT_CENTER)
	_text(Vector2(r.position.x, r.end.y - 4), val, int(size * 1.4), ink, f_serif, r.size.x, HORIZONTAL_ALIGNMENT_CENTER)

func _small_stats(box: Rect2, c: Dictionary, row: Dictionary, t: String, ink: Color) -> void:
	draw_rect(box, Color(0.95, 0.90, 0.78))
	var s := t
	match t:
		"Personality": s = "%sF  %sC" % [_n(c.get("f", row.get("force", "0"))), _n(c.get("c", row.get("chi", "0")))]
		"Holding", "Stronghold": s = "%s gold" % _int(row, "gold")
		"Follower", "Item": s = "+%sF" % _int(row, "force")
	_cell(box, s, int(box.size.y * 0.55), ink)

# The second mockup's card: art on top, a stat column on the art's right
# edge, a dark name plate, the text below.
func _face_modern(r: Rect2, c: Dictionary, opts: Dictionary) -> void:
	var row := _row(int(c["o"]))
	var t: String = c.get("t", row.get("type", ""))
	var clan := _clan_of(row)
	var small: bool = opts.get("small", false)
	draw_rect(r, Color(0.09, 0.07, 0.06))
	var art := Rect2(r.position + Vector2(4, 4), Vector2(r.size.x - 8, r.size.y * (0.72 if small else 0.58)))
	_art(art, int(c["o"]), clan)
	var plate := Rect2(art.position, Vector2(art.size.x, r.size.y * 0.10))
	draw_rect(plate, Color(0.95, 0.90, 0.78, 0.92))
	var name: String = c.get("n", "")
	var tsz := _fit(name, f_serif, int(r.size.y * 0.058), plate.size.x - 8)
	_text(Vector2(plate.position.x + 4, plate.position.y + plate.size.y * 0.5 + tsz * 0.36), name, tsz,
			Color(0.15, 0.10, 0.06), f_serif, plate.size.x - 8)
	if t == "Personality":
		var vals := [_n(c.get("f", row.get("force", "0"))), _n(c.get("c", row.get("chi", "0"))), _int(row, "ph")]
		var bw := r.size.x * 0.17
		for i in 3:
			var b := Rect2(art.end.x - bw - 2, plate.end.y + 4 + i * (bw + 3), bw, bw)
			draw_rect(b, Color(0.95, 0.90, 0.78))
			draw_rect(b, Color(0.30, 0.20, 0.10), false, 1)
			_cell(b, vals[i], int(bw * 0.7), Color(0.15, 0.10, 0.06))
	draw_rect(art, Color(0.70, 0.54, 0.28), false, 1)
	if int(c.get("auto", 1)) == 0 and t != "Personality" and t != "Holding":
		draw_circle(Vector2(art.position.x + 10, plate.end.y + 12), 9, Color(0.6, 0.1, 0.1))
		_text(Vector2(art.position.x + 1, plate.end.y + 17), "手", 13, Color.WHITE, f_cjk, 18, HORIZONTAL_ALIGNMENT_CENTER)
	var box := Rect2(r.position.x + 4, art.end.y + 3, r.size.x - 8, r.end.y - art.end.y - 7)
	draw_rect(box, Color(0.93, 0.87, 0.74))
	var fs := int(r.size.y * 0.05)
	var line := ""
	match t:
		"Personality": line = "Personality (%s/%s/%s)  HR %s  cost %s" % [_n(c.get("f", row.get("force", "0"))),
				_n(c.get("c", row.get("chi", "0"))), _int(row, "ph"), _int(row, "hreq"), _int(row, "cost")]
		"Holding", "Stronghold": line = "%s · %s gold" % [t, _int(row, "gold")]
		"Follower", "Item": line = "%s: +%sF" % [t, _int(row, "force")]
		_: line = t
	if small:
		_cell(box, line if t != "Personality" else "%sF %sC" % [_n(c.get("f", row.get("force", "0"))),
				_n(c.get("c", row.get("chi", "0")))], int(box.size.y * 0.5), Color(0.15, 0.10, 0.06))
		return
	_text(box.position + Vector2(4, fs + 1), line, int(fs * 0.85), Color(0.45, 0.20, 0.08), f_bold, box.size.x - 8)
	draw_multiline_string(f_ui, box.position + Vector2(4, fs * 2 + 2), db.text(int(c["o"])), HORIZONTAL_ALIGNMENT_LEFT,
			box.size.x - 8, int(fs * 0.78), int((box.size.y - fs * 1.6) / (fs * 0.92)), Color(0.15, 0.10, 0.06))
	draw_circle(Vector2(box.end.x - 12, box.end.y - 12), 10, (CLAN_COLOR.get(clan, Color.GRAY) as Color))

# ------------------------------------------------------------ the board
# Shared by both skins: where things go.  Each skin paints the frames.
func _board(center: Rect2, opp_h: float, prov_h: float, home_h: float) -> void:
	if v.is_empty():
		return
	var o := opp()
	var m := me()
	var y := center.position.y
	# --- the opponent: provinces, then what they have in play
	var orow := Rect2(center.position.x, y, center.size.x, opp_h)
	var cw := 84.0
	var ch := cw * 1.4
	var x := orow.position.x + 10
	for k in 4:
		var p: Dictionary = o["provinces"][k]
		var r := Rect2(x, orow.position.y + 22, cw, ch)
		if int(p["alive"]):
			card(r, p["card"] if p["card"] != null else null, {"small": true, "back": "province"})
		else:
			_ruin(r)
		_caption(Rect2(x, r.end.y + 2, cw, 16), "Str %d" % int(p["str"]))
		x += cw + 6
	x += 16
	var units: Array = (o["play"] as Array).filter(func(c): return c["t"] == "Personality" or c["t"] == "Ring")
	var holds: Array = (o["play"] as Array).filter(func(c): return c["t"] != "Personality" and c["t"] != "Ring")
	var lbox := Rect2(orow.end.x - 236, orow.position.y + 22, 226, ch + 18)
	var avail := lbox.position.x - x - 10
	var step := minf(cw + 6, (avail - cw) / maxf(units.size() - 1, 1))
	for c in units:
		card(Rect2(x, orow.position.y + 22, cw, ch), c, {"small": true, "zone": "opp"})
		x += step
	_holdings_list(lbox, holds)
	y += opp_h + 6
	# --- my provinces, with the Recruit button under each
	var pw := 132.0
	var ph := pw * 1.4
	var gap := (center.size.x - pw * 4) / 5.0
	for k in 4:
		var p: Dictionary = m["provinces"][k]
		var r := Rect2(center.position.x + gap + k * (pw + gap), y + 4, pw, ph)
		if int(p["alive"]):
			card(r, p["card"] if p["card"] != null else null, {"back": "province", "zone": "province", "mine": true})
		else:
			_ruin(r)
		var br := Rect2(r.position.x - 6, r.end.y + 4, pw + 12, 24)
		var acts := actions_for_prov(k)
		var label := "Province  ·  Str %d" % int(p["str"])
		for i in acts:
			if v["actions"][i]["k"] == A_BUY:
				label = "Recruit (%s)" % v["actions"][i]["l"].get_slice("(", 1).trim_suffix(")")
		_province_button(br, label, acts.size() > 0)
		if acts.size():
			_hit(br, "prov", {"prov": k})
		if p["region"] != null:
			_caption(Rect2(r.position.x, r.position.y - 16, pw, 14), "Region: " + str(p["region"]["n"]))
	y += prov_h
	# --- the Home Zone
	var home := Rect2(center.position.x + 8, y, center.size.x - 16, home_h)
	_zone_frame(home, "Home Zone")
	var dw := 96.0
	var dh := dw * 1.4
	var fd := Rect2(home.position.x + 12, home.position.y + 26, dw, dh)
	_back(fd, "fate")
	_deck_label(Rect2(fd.position.x - 4, fd.end.y + 4, dw + 8, 22), "Fate Deck  %d" % int(m["fate"]))
	var dd := Rect2(home.end.x - dw - 12, home.position.y + 26, dw, dh)
	_back(dd, "dynasty")
	_deck_label(Rect2(dd.position.x - 4, dd.end.y + 4, dw + 8, 22), "Dynasty Deck  %d" % int(m["dynasty"]))
	var inplay: Array = (m["play"] as Array).filter(func(c): return c["t"] == "Personality" or c["t"] == "Ring")
	var mine_h: Array = (m["play"] as Array).filter(func(c): return c["t"] != "Personality" and c["t"] != "Ring")
	var full := Rect2(fd.end.x + 16, home.position.y + 24, dd.position.x - fd.end.x - 32, home.size.y - 30)
	var hcols := 5
	var mw := 62.0
	var mh := mw * 1.4
	var harea := Rect2(full.position, Vector2(hcols * (mw + 4), full.size.y))
	var per_col := maxi(1, int(ceil(mine_h.size() / float(hcols))))
	var vstep := minf(mh + 4, (harea.size.y - mh) / maxf(per_col - 1, 1))
	for j in mine_h.size():
		var hr := Rect2(harea.position.x + (j % hcols) * (mw + 4), harea.position.y + 2 + (j / hcols) * vstep, mw, mh)
		card(hr, mine_h[j], {"small": true, "mine": true, "zone": "play"})
	var area := Rect2(harea.end.x + 12, full.position.y, full.end.x - harea.end.x - 12, full.size.y)
	var hw := 104.0
	var hh := hw * 1.4
	var n := inplay.size()
	var hstep := minf(hw + 8, (area.size.x - hw) / maxf(n - 1, 1))
	var hx := area.position.x + maxf(0, (area.size.x - (hstep * (n - 1) + hw)) * 0.5)
	for c in inplay:
		var r := Rect2(hx, area.position.y + 2, hw, hh)
		card(r, c, {"mine": true, "zone": "play"})
		var ay := r.end.y + 2
		for a in c.get("att", []):
			var ar := Rect2(r.position.x, ay, hw, 15)
			draw_rect(ar, Color(0.95, 0.88, 0.70, 0.95) if skin == "classic" else Color(0.2, 0.15, 0.1, 0.95))
			_text(Vector2(ar.position.x + 3, ar.end.y - 3), "+ %s" % a["n"], _fit("+ " + str(a["n"]), f_ui, 11, hw - 6),
					Color(0.15, 0.1, 0.05) if skin == "classic" else Color(0.95, 0.88, 0.7), f_ui, hw - 6)
			_hit(ar, "card", {"inst": int(a["i"]), "oid": int(a["o"]), "mine": true, "zone": "play"})
			ay += 16
		hx += hstep

func _hand(r: Rect2) -> void:
	if v.is_empty():
		return
	var cards_: Array = me().get("handcards", [])
	var cw := 112.0
	var ch := cw * 1.4
	var n := cards_.size()
	var step := minf(cw + 8, (r.size.x - cw) / maxf(n - 1, 1))
	var x := r.position.x + maxf(0, (r.size.x - (step * (n - 1) + cw)) * 0.5)
	for c in cards_:
		var lift := 10.0 if hover.get("inst", -1) == int(c["i"]) else 0.0
		card(Rect2(x, r.position.y + 6 - lift, cw, ch), c, {"mine": true, "zone": "hand"})
		x += step

func _holdings_list(r: Rect2, holds: Array) -> void:
	if skin == "modern":
		_lacquer(r, 0.7)
	else:
		draw_rect(r, Color(0.92, 0.86, 0.70, 0.92))
		draw_rect(r, Color(0.45, 0.30, 0.15), false, 2)
	var ink := Color(0.15, 0.10, 0.05) if skin == "classic" else Color(0.95, 0.88, 0.72)
	var total := 0
	for c in holds:
		if c["t"] != "Ring" and int(c.get("b", 0)) == 0:
			total += int(_row(int(c["o"])).get("gold", "0"))
	_text(r.position + Vector2(8, 18), "Holdings  ·  %d gold ready" % total, 13, ink, f_bold, r.size.x - 16)
	var y := r.position.y + 36
	for c in holds:
		if y > r.end.y - 6:
			_text(Vector2(r.position.x + 8, y), "...", 12, ink)
			break
		var row := _row(int(c["o"]))
		var bowed := int(c.get("b", 0)) == 1
		var s := "%s  (%s)" % [str(c["n"]), _int(row, "gold") + "g" if c["t"] != "Stronghold" else "SH"]
		var col := ink if not bowed else Color(ink, 0.45)
		var lr := Rect2(r.position.x + 8, y - 13, r.size.x - 16, 16)
		_text(Vector2(lr.position.x, y), ("⤵ " if bowed else "") + s, _fit(s, f_ui, 12, lr.size.x - 14), col, f_ui, lr.size.x)
		_hit(lr, "card", {"inst": int(c["i"]), "oid": int(c["o"]), "zone": "opp"})
		y += 16

func _ruin(r: Rect2) -> void:
	draw_rect(r, Color(0.15, 0.10, 0.07, 0.8))
	draw_line(r.position, r.end, Color(0.6, 0.1, 0.1), 3)
	draw_line(Vector2(r.end.x, r.position.y), Vector2(r.position.x, r.end.y), Color(0.6, 0.1, 0.1), 3)

func _caption(r: Rect2, s: String) -> void:
	var col := Color(0.95, 0.88, 0.70)
	_text(Vector2(r.position.x, r.end.y - 3), s, _fit(s, f_ui, 12, r.size.x), col, f_ui, r.size.x, HORIZONTAL_ALIGNMENT_CENTER)

func _province_button(r: Rect2, label: String, live: bool) -> void:
	if skin == "modern":
		_gold_button(r, label, live, true)
	else:
		_button95(r, label, true, 14)
		if live:
			draw_rect(r.grow(1), Color(0.0, 0.0, 0.5), false, 1)

func _deck_label(r: Rect2, s: String) -> void:
	if skin == "modern":
		_caption(r, s)
	else:
		_button95(r, s, true, 13)

func _zone_frame(r: Rect2, title: String) -> void:
	if skin == "modern":
		_lacquer(r, 0.55)
		_text(Vector2(r.position.x, r.position.y + 18), title, 16, Color(0.95, 0.85, 0.60), f_serif, r.size.x,
				HORIZONTAL_ALIGNMENT_CENTER)
	else:
		draw_rect(r, Color(0.25, 0.15, 0.08, 0.35))
		draw_rect(r, Color(0.55, 0.40, 0.22), false, 2)
		_text(Vector2(r.position.x, r.position.y + 17), title, 15, Color(0.98, 0.92, 0.80), f_bold, r.size.x,
				HORIZONTAL_ALIGNMENT_CENTER)

# ------------------------------------------------------------- classic
func _draw_classic() -> void:
	draw_rect(Rect2(0, 0, W, H), Color(0.75, 0.75, 0.75))
	# title bar
	var tb := Rect2(2, 2, W - 4, 28)
	draw_rect(tb, Color(0.0, 0.0, 0.5))
	draw_rect(Rect2(tb.position.x + tb.size.x * 0.5, tb.position.y, tb.size.x * 0.5, tb.size.y), Color(0.06, 0.32, 0.72))
	_text(Vector2(12, 22), "Legend of the Five Rings (L5R)", 17, Color.WHITE, f_bold)
	var bx := W - 84
	for g in ["_", "□", "X"]:
		var b := Rect2(bx, 6, 24, 20)
		_button95(b, g, true, 13)
		if g == "X":
			_hit(b, "quit")
		bx += 26
	# menu bar
	var mx := 10.0
	for name in ["File", "Options", "Help"]:
		var w := f_ui.get_string_size(name, HORIZONTAL_ALIGNMENT_LEFT, -1, 16).x + 20
		var r := Rect2(mx, 32, w, 24)
		_text(Vector2(mx + 10, 50), name, 16, Color.BLACK)
		draw_line(Vector2(mx + 10, 52), Vector2(mx + 10 + f_ui.get_string_size(name.substr(0, 1), HORIZONTAL_ALIGNMENT_LEFT, -1, 16).x, 52), Color.BLACK, 1)
		_hit(r, "menu", {"name": name})
		mx += w + 6
	# the table
	var table := Rect2(4, 60, W - 8, 876)
	_bevel(table, false)
	_wood(table.grow(-2))
	if v.is_empty():
		_draw_status_classic()
		return
	_classic_left(Rect2(10, 66, 200, 864))
	_classic_right(Rect2(W - 210, 66, 200, 864))
	var center := Rect2(218, 66, W - 436, 864)
	draw_rect(Rect2(center.position.x, center.position.y, center.size.x, 162), Color(0, 0, 0, 0.18))
	_text(Vector2(center.position.x + 10, center.position.y + 16), "%s Clan — opponent" % opp()["clan"], 14,
			Color(0.98, 0.9, 0.75), f_bold)
	_board(center, 168, 240, 262)
	_hand(Rect2(center.position.x, center.position.y + 168 + 240 + 262 + 8, center.size.x, 190))
	_draw_status_classic()

func _classic_left(r: Rect2) -> void:
	_bevel(r, true)
	var hdr := Rect2(r.position.x + 8, r.position.y + 8, r.size.x - 16, 30)
	_button95(hdr, "HONOR", true, 17)
	var track := Rect2(r.position.x + 8, hdr.end.y + 6, r.size.x - 16, 520)
	draw_rect(track, Color(0.05, 0.05, 0.05))
	var steps := 13
	var rh := track.size.y / steps
	for i in steps:
		var val := 40 - i * 5
		var rr := Rect2(track.position.x + 2, track.position.y + i * rh + 1, track.size.x - 4, rh - 2)
		draw_rect(rr, Color(0.12, 0.12, 0.12))
		_text(Vector2(rr.position.x, rr.end.y - rh * 0.25), str(val), 18, Color(0.85, 0.85, 0.8), f_bold, rr.size.x,
				HORIZONTAL_ALIGNMENT_CENTER)
	for who in [[me(), Color(0.8, 0.1, 0.1)], [opp(), Color(0.2, 0.4, 0.95)]]:
		var hv := clampi(int(who[0]["honor"]), -20, 40)
		var yy := track.position.y + (40.0 - hv) / 5.0 * rh + rh * 0.5
		var mark := Rect2(track.position.x + 2, yy - rh * 0.5 + 1, track.size.x - 4, rh - 2)
		draw_rect(mark, (who[1] as Color) * Color(1, 1, 1, 0.75))
		_text(Vector2(mark.position.x, mark.end.y - rh * 0.25), _n(who[0]["honor"]), 18, Color.WHITE, f_bold, mark.size.x,
				HORIZONTAL_ALIGNMENT_CENTER)
	_text(Vector2(track.position.x, track.end.y + 18), "■ You %d" % int(me()["honor"]), 14, Color(0.7, 0.05, 0.05), f_bold)
	_text(Vector2(track.position.x + 92, track.end.y + 18), "■ Opp %d" % int(opp()["honor"]), 14, Color(0.1, 0.2, 0.75), f_bold)
	var plq := Rect2(r.position.x + 8, track.end.y + 30, r.size.x - 16, r.end.y - track.end.y - 38)
	_plaque_classic(plq, str(me()["clan"]), true)

func _plaque_classic(r: Rect2, clan: String, mine: bool) -> void:
	var col: Color = CLAN_COLOR.get(clan, CLAN_COLOR["Unaligned"])
	var pic := Rect2(r.position.x, r.position.y, r.size.x, r.size.y - 52)
	draw_rect(pic, Color(0.55, 0.42, 0.25))
	draw_rect(pic.grow(-5), col.darkened(0.45))
	_mon(pic.get_center(), minf(pic.size.x, pic.size.y) * 0.34, clan)
	var lab := Rect2(r.position.x, pic.end.y + 4, r.size.x, 44)
	draw_rect(lab, col.darkened(0.15))
	draw_rect(lab, Color(0.9, 0.75, 0.4), false, 2)
	_text(Vector2(lab.position.x, lab.position.y + 29), "%s CLAN" % clan.to_upper(), 19, Color.WHITE, f_bold, lab.size.x,
			HORIZONTAL_ALIGNMENT_CENTER)
	if mine:
		_hit(r, "plaque")

func _classic_right(r: Rect2) -> void:
	_bevel(r, true)
	var x := r.position.x + 8
	var w := r.size.x - 16
	var fate := Rect2(x, r.position.y + 8, w, 104)
	_bevel(fate, false, Color(0.80, 0.80, 0.80))
	_button95(Rect2(x + 4, fate.position.y + 4, w - 8, 26), "FATE", true, 16)
	_coin(Vector2(x + 44, fate.position.y + 66), 28, "運")
	_text(Vector2(x + 84, fate.position.y + 86), _n(me()["hand"]), 46, Color.BLACK, f_bold)
	var gold := Rect2(x, fate.end.y + 8, w, 104)
	_bevel(gold, false, Color(0.80, 0.80, 0.80))
	_button95(Rect2(x + 4, gold.position.y + 4, w - 8, 26), "GOLD (KOKU)", true, 15)
	_coin(Vector2(x + 44, gold.position.y + 66), 28, "金")
	_text(Vector2(x + 84, gold.position.y + 86), _n(me()["gold"]), 46, Color.BLACK, f_bold)
	if int(me()["pool"]) > 0:
		_text(Vector2(x + 130, gold.position.y + 86), "pool %d" % int(me()["pool"]), 12, Color(0.3, 0.3, 0.3))
	var plq := Rect2(x, gold.end.y + 10, w, 300)
	_plaque_classic(plq, str(opp()["clan"]), false)
	var info := Rect2(x, plq.end.y + 10, w, r.end.y - plq.end.y - 18)
	_bevel(info, false, Color(0.80, 0.80, 0.80))
	var lines := [
		"Turn %d" % int(v["turn"]),
		"Opponent honor  %d" % int(opp()["honor"]),
		"Opponent hand  %d" % int(opp()["hand"]),
		"Rings  you %d · opp %d" % [int(me()["rings"]), int(opp()["rings"])],
		"Fate discard  %d" % int(me()["fdisc"]),
		"Dynasty discard  %d" % int(me()["ddisc"]),
	]
	var ly := info.position.y + 22
	for l in lines:
		_text(Vector2(info.position.x + 8, ly), l, 14, Color.BLACK)
		ly += 22

func _draw_status_classic() -> void:
	var sb := Rect2(4, H - 60, W - 8, 56)
	_bevel(sb, true)
	if v.is_empty():
		_text(Vector2(16, H - 26), "No game -- File > New Game", 18, Color.BLACK, f_bold)
		return
	var who := "Your Turn" if my_turn() else "%s's Turn" % opp()["clan"]
	_text(Vector2(18, H - 24), "%s - Phase: %s" % [who, phase_label()], 20, Color.BLACK, f_bold)
	var bulb := Vector2(18 + f_bold.get_string_size("%s - Phase: %s" % [who, phase_label()],
			HORIZONTAL_ALIGNMENT_LEFT, -1, 20).x + 26, H - 32)
	draw_circle(bulb, 11, Color(1.0, 0.9, 0.3) if my_say() else Color(0.55, 0.55, 0.5))
	draw_rect(Rect2(bulb.x - 5, bulb.y + 9, 10, 8), Color(0.4, 0.4, 0.4))
	var logs: Array = v.get("log", [])
	if logs.size():
		_text(Vector2(560, H - 26), str(logs[-1]), 15, Color(0.15, 0.15, 0.15), f_ui, 560)
	_status_buttons(Rect2(W - 470, H - 54, 460, 44))

func _status_buttons(r: Rect2) -> void:
	var bx := r.end.x
	var lw := 70.0
	var lr := Rect2(bx - lw, r.position.y, lw, r.size.y)
	if skin == "modern":
		_gold_button(lr, "Log", show_log)
	else:
		_button95(lr, "Log", true, 15)
	_hit(lr, "log")
	bx -= lw + 8
	if not my_say():
		return
	var g := global_actions()
	g.reverse()
	for i in g:
		var label: String = v["actions"][i]["l"]
		var w := maxf(120.0, f_bold.get_string_size(label, HORIZONTAL_ALIGNMENT_LEFT, -1, 17).x + 30)
		var b := Rect2(bx - w, r.position.y, w, r.size.y)
		if skin == "modern":
			_gold_button(b, label, true)
		else:
			_button95(b, label, true, 17)
		_hit(b, "act", {"idx": i})
		bx -= w + 8

# -------------------------------------------------------------- modern
func _draw_modern() -> void:
	# a painted dusk: gradient, mountains, a pagoda, blossom
	for i in 40:
		var t := i / 40.0
		draw_rect(Rect2(0, H * t, W, H / 40.0 + 1), Color(0.16, 0.18, 0.22).lerp(Color(0.08, 0.06, 0.05), t))
	var hills := PackedVector2Array([Vector2(0, 520), Vector2(260, 360), Vector2(520, 470), Vector2(800, 330),
			Vector2(1100, 450), Vector2(1350, 340), Vector2(1600, 430), Vector2(1600, 1000), Vector2(0, 1000)])
	draw_colored_polygon(hills, Color(0.12, 0.12, 0.14, 0.8))
	for i in 60:
		var p := Vector2(fmod(i * 173.3, 300.0) + (0 if i % 2 else 1300), fmod(i * 97.1, 900.0) + 60)
		draw_circle(p, 2.0 + (i % 3), Color(0.85, 0.55, 0.65, 0.35))
	# left column
	var left := Rect2(8, 8, 222, H - 16)
	_lacquer(Rect2(left.position, Vector2(left.size.x, 150)))
	for i in 5:
		var a := TAU * i / 5.0 - PI * 0.5
		_ring_token(Vector2(119, 62) + Vector2(cos(a), sin(a)) * 30, 13, RINGS[i][1], true)
	_text(Vector2(left.position.x, 128), "Legend of the", 16, Color(0.92, 0.78, 0.45), f_serif, left.size.x, HORIZONTAL_ALIGNMENT_CENTER)
	_text(Vector2(left.position.x, 150), "Five Rings", 24, Color(0.95, 0.80, 0.45), f_serif, left.size.x, HORIZONTAL_ALIGNMENT_CENTER)
	if v.is_empty():
		_modern_right()
		return
	var rr := Rect2(left.position.x, 168, left.size.x, 330)
	_lacquer(rr)
	_text(Vector2(rr.position.x, rr.position.y + 22), "Rings in play", 16, Color(0.95, 0.85, 0.6), f_serif, rr.size.x,
			HORIZONTAL_ALIGNMENT_CENTER)
	var have: int = int(me()["rings"])
	for i in 5:
		var c := Vector2(rr.position.x + 46, rr.position.y + 62 + i * 56)
		_ring_token(c, 22, RINGS[i][1], i < have)
		_text(Vector2(c.x + 34, c.y + 7), RINGS[i][0], 18, Color(0.95, 0.9, 0.8) if i < have else Color(0.55, 0.5, 0.45), f_serif)
	var decks := Rect2(left.position.x, rr.end.y + 8, left.size.x, 240)
	_lacquer(decks)
	var ly := decks.position.y + 30
	for l in [["Fate Deck", me()["fate"]], ["Fate Discard", me()["fdisc"]], ["Dynasty Deck", me()["dynasty"]],
			["Dynasty Discard", me()["ddisc"]], ["Gold (koku)", me()["gold"]], ["Turn", v["turn"]]]:
		_text(Vector2(decks.position.x + 16, ly), str(l[0]), 17, Color(0.92, 0.85, 0.7), f_serif)
		_text(Vector2(decks.end.x - 60, ly), _n(l[1]), 19, Color(1, 0.9, 0.6), f_bold, 44, HORIZONTAL_ALIGNMENT_RIGHT)
		ly += 36
	_portrait(Rect2(left.position.x, H - 244, left.size.x, 236), str(me()["clan"]), int(me()["honor"]), true)
	# the board
	var center := Rect2(240, 8, W - 240 - 250, H - 16)
	_portrait(Rect2(center.position.x, center.position.y, 150, 168), str(opp()["clan"]), int(opp()["honor"]), false)
	var oc := Rect2(center.position.x + 160, center.position.y, center.size.x - 160, 168)
	_lacquer(oc, 0.6)
	_board(Rect2(oc.position.x - 4, oc.position.y - 14, oc.size.x, center.size.y), 176, 240, 262)
	_hand(Rect2(center.position.x, H - 214, center.size.x, 206))
	_modern_right()

func _ring_token(c: Vector2, rad: float, k: String, lit: bool) -> void:
	draw_circle(c, rad, Color(0.75, 0.58, 0.28) if lit else Color(0.30, 0.26, 0.22))
	draw_circle(c, rad * 0.84, Color(0.18, 0.12, 0.08) if lit else Color(0.12, 0.10, 0.09))
	_text(Vector2(c.x - rad, c.y + rad * 0.38), k, int(rad * 1.05), Color(1, 0.9, 0.6) if lit else Color(0.45, 0.4, 0.35),
			f_cjk, rad * 2, HORIZONTAL_ALIGNMENT_CENTER)

func _portrait(r: Rect2, clan: String, honor: int, mine: bool) -> void:
	_lacquer(r)
	var col: Color = CLAN_COLOR.get(clan, CLAN_COLOR["Unaligned"])
	var pic := r.grow(-8)
	pic.size.y -= 34
	draw_rect(pic, col.darkened(0.55))
	_mon(pic.get_center(), minf(pic.size.x, pic.size.y) * 0.34, clan)
	var plate := Rect2(r.position.x + 8, r.end.y - 38, r.size.x - 56, 30)
	draw_rect(plate, Color(0.12, 0.09, 0.07))
	_text(Vector2(plate.position.x + 8, plate.end.y - 8), clan, 18, Color(0.95, 0.88, 0.72), f_serif)
	var hb := Rect2(r.end.x - 46, r.end.y - 44, 40, 38)
	draw_rect(hb, Color(0.12, 0.09, 0.07))
	draw_rect(hb, Color(0.78, 0.62, 0.32), false, 2)
	_text(Vector2(hb.position.x, hb.end.y - 10), str(honor), 20, Color(1, 0.9, 0.6), f_bold, hb.size.x, HORIZONTAL_ALIGNMENT_CENTER)
	if mine:
		_hit(r, "plaque")

func _modern_right() -> void:
	var r := Rect2(W - 242, 8, 234, H - 16)
	var top := Rect2(r.position.x, r.position.y, r.size.x, 230)
	_lacquer(top)
	var ban := Rect2(top.position.x + 10, top.position.y + 10, 64, top.size.y - 20)
	draw_rect(ban, Color(0.10, 0.07, 0.05))
	var ky := ban.position.y + 44
	for k in ["五", "輪", "の", "書"]:
		_text(Vector2(ban.position.x, ky), k, 40, Color(0.92, 0.74, 0.38), f_cjk, ban.size.x, HORIZONTAL_ALIGNMENT_CENTER)
		ky += 50
	var sky := Rect2(ban.end.x + 8, ban.position.y, top.end.x - ban.end.x - 18, ban.size.y)
	for i in 20:
		var t := i / 20.0
		draw_rect(Rect2(sky.position.x, sky.position.y + sky.size.y * t, sky.size.x, sky.size.y / 20.0 + 1),
				Color(0.22, 0.30, 0.45).lerp(Color(0.55, 0.40, 0.35), t))
	draw_circle(sky.position + Vector2(sky.size.x * 0.7, 40), 16, Color(0.98, 0.85, 0.6))
	var torii := sky.position + Vector2(sky.size.x * 0.5, sky.size.y - 30)
	draw_rect(Rect2(torii.x - 40, torii.y - 60, 80, 8), Color(0.65, 0.15, 0.1))
	draw_rect(Rect2(torii.x - 34, torii.y - 46, 68, 5), Color(0.65, 0.15, 0.1))
	draw_rect(Rect2(torii.x - 28, torii.y - 56, 7, 60), Color(0.65, 0.15, 0.1))
	draw_rect(Rect2(torii.x + 21, torii.y - 56, 7, 60), Color(0.65, 0.15, 0.1))
	# the phases, the current one lit
	var py := top.end.y + 10
	var cur := phase_label()
	for ph in PHASES:
		var b := Rect2(r.position.x + 10, py, r.size.x - 20, 38)
		_gold_button(b, ph, ph == cur, true)
		py += 44
	if not v.is_empty():
		var who := "Your turn" if my_turn() else "%s's turn" % opp()["clan"]
		_text(Vector2(r.position.x, py + 18), who + ("  ·  you act" if my_say() else ""), 15,
				Color(1, 0.9, 0.6) if my_say() else Color(0.7, 0.65, 0.55), f_serif, r.size.x, HORIZONTAL_ALIGNMENT_CENTER)
		py += 30
		if my_say():
			for i in global_actions():
				var b := Rect2(r.position.x + 10, py, r.size.x - 20, 40)
				_gold_button(b, v["actions"][i]["l"], true)
				_hit(b, "act", {"idx": i})
				py += 46
	var gear := Rect2(r.position.x + 10, py + 4, (r.size.x - 30) * 0.5, 34)
	_gold_button(gear, "New Game", false)
	_hit(gear, "ng", {"what": "open"})
	var sk := Rect2(gear.end.x + 10, py + 4, (r.size.x - 30) * 0.5, 34)
	_gold_button(sk, "Classic", false)
	_hit(sk, "ng", {"what": "skin", "val": "classic"})
	py += 46
	var logr := Rect2(r.position.x, py, r.size.x, r.end.y - py)
	draw_rect(logr, Color(0.86, 0.79, 0.64))
	draw_rect(logr, Color(0.55, 0.40, 0.20), false, 3)
	if not v.is_empty():
		var logs: Array = v.get("log", [])
		var ly := logr.end.y - 12
		for i in range(logs.size() - 1, -1, -1):
			var s: String = logs[i]
			var lines := ceili(f_ui.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, 14).x / (logr.size.x - 24))
			ly -= 18 * maxi(lines, 1)
			if ly < logr.position.y + 6:
				break
			draw_multiline_string(f_ui, Vector2(logr.position.x + 12, ly + 14), s, HORIZONTAL_ALIGNMENT_LEFT,
					logr.size.x - 24, 14, -1, Color(0.2, 0.13, 0.08))

# ------------------------------------------------------------- overlays
func _draw_overlays() -> void:
	# hover: the original card, as printed
	if hover.get("oid", -1) >= 0 and zoom_oid < 0 and not show_new:
		var tex = db.scan(int(hover["oid"]))
		var sz := Vector2(258, 360)
		var p := mouse + Vector2(24, -sz.y * 0.5)
		if p.x + sz.x > W - 4:
			p.x = mouse.x - sz.x - 24
		p.y = clampf(p.y, 4, H - sz.y - 4)
		var rr := Rect2(p, sz)
		if tex:
			draw_rect(rr.grow(3), Color(0, 0, 0, 0.8))
			draw_texture_rect(tex, rr, false)
		else:
			var row := _row(int(hover["oid"]))
			draw_rect(rr, Color(0.95, 0.90, 0.78))
			draw_rect(rr, Color(0.4, 0.3, 0.15), false, 2)
			_text(rr.position + Vector2(10, 26), str(row.get("name", "")), 18, Color(0.15, 0.1, 0.05), f_serif, sz.x - 20)
			draw_multiline_string(f_ui, rr.position + Vector2(10, 52), db.text(int(hover["oid"])), HORIZONTAL_ALIGNMENT_LEFT,
					sz.x - 20, 13, -1, Color(0.15, 0.1, 0.05))
	if zoom_oid >= 0:
		draw_rect(Rect2(0, 0, W, H), Color(0, 0, 0, 0.6))
		var tex = db.scan(zoom_oid)
		var zr := Rect2(W * 0.5 - 300, H * 0.5 - 420, 600, 840)
		if tex:
			draw_texture_rect(tex, zr, false)
		draw_multiline_string(f_ui, Vector2(40, 60), db.text(zoom_oid), HORIZONTAL_ALIGNMENT_LEFT, W * 0.5 - 340, 16, -1,
				Color(1, 0.95, 0.85))
	if show_log and not v.is_empty():
		var lr := Rect2(W * 0.5 - 380, 120, 760, 620)
		_panel(lr, "Log")
		var ly := lr.position.y + 60
		for s in v.get("log", []):
			_text(Vector2(lr.position.x + 18, ly), str(s), 15, _ink(), f_ui, lr.size.x - 36)
			ly += 22
	var over: int = int(v.get("winner", -1)) if not v.is_empty() else -1
	if not v.is_empty() and v.get("phase", "") == "Over":
		var orr := Rect2(W * 0.5 - 300, H * 0.5 - 120, 600, 240)
		_panel(orr, "The game is over")
		var won := over == int(v["seat"])
		_text(Vector2(orr.position.x, orr.position.y + 110), ("Victory" if won else "Defeat" if over >= 0 else "Draw"),
				40, _ink(), f_serif, orr.size.x, HORIZONTAL_ALIGNMENT_CENTER)
		_text(Vector2(orr.position.x, orr.position.y + 150), str(v.get("how", "")), 20, _ink(), f_ui, orr.size.x,
				HORIZONTAL_ALIGNMENT_CENTER)
		var b := Rect2(orr.position.x + 200, orr.end.y - 56, 200, 40)
		_btn(b, "New Game", true)
		_hit(b, "ng", {"what": "open"})
	if show_new:
		_draw_new_game()
	if message != "":
		var mr := Rect2(W * 0.5 - 420, H * 0.5 - 200, 840, 400)
		_panel(mr, "Rokugan")
		draw_multiline_string(f_ui, mr.position + Vector2(24, 70), message, HORIZONTAL_ALIGNMENT_LEFT, mr.size.x - 48, 16,
				-1, _ink())
		var b := Rect2(mr.end.x - 140, mr.end.y - 52, 120, 36)
		_btn(b, "OK", true)
		_hit(b, "ok")
		hits.append({"r": Rect2(0, 0, W, H), "kind": "ok_any"})

func _ink() -> Color:
	return Color.BLACK if skin == "classic" else Color(0.95, 0.88, 0.72)

func _btn(r: Rect2, label: String, lit: bool) -> void:
	if skin == "modern":
		_gold_button(r, label, lit)
	else:
		_button95(r, label, true, 16)

func _panel(r: Rect2, title: String) -> void:
	if skin == "modern":
		_lacquer(r, 0.95)
		_text(Vector2(r.position.x, r.position.y + 34), title, 22, Color(0.95, 0.82, 0.5), f_serif, r.size.x,
				HORIZONTAL_ALIGNMENT_CENTER)
	else:
		_bevel(r, true)
		var tb := Rect2(r.position.x + 3, r.position.y + 3, r.size.x - 6, 26)
		draw_rect(tb, Color(0, 0, 0.5))
		_text(Vector2(tb.position.x + 8, tb.end.y - 7), title, 16, Color.WHITE, f_bold)
		var x := Rect2(tb.end.x - 24, tb.position.y + 3, 20, 20)
		_button95(x, "X", true, 12)
		_hit(x, "close")

func _draw_new_game() -> void:
	var r := Rect2(W * 0.5 - 430, 140, 860, 640)
	_panel(r, "New Game")
	var y := r.position.y + 70
	_text(Vector2(r.position.x + 30, y + 22), "Era", 18, _ink(), f_bold)
	var x := r.position.x + 230
	for e in ERAS:
		var b := Rect2(x, y, 190, 34)
		_chip(b, e[1], era == e[0])
		_hit(b, "ng", {"what": "era", "val": e[0]})
		x += 200
	y += 60
	var clans := clans_for(era)
	for who in [["Your clan", "clan", my_clan], ["Opponent", "opp", opp_clan]]:
		_text(Vector2(r.position.x + 30, y + 22), who[0], 18, _ink(), f_bold)
		var opts: Array = clans.duplicate()
		if who[1] == "opp":
			opts.append("Random")
		var i := 0
		for c in opts:
			var b := Rect2(r.position.x + 230 + (i % 5) * 120, y + (i / 5) * 42, 112, 34)
			_chip(b, c, who[2] == c)
			_hit(b, "ng", {"what": who[1], "val": c})
			i += 1
		y += 42 * ceili(opts.size() / 5.0) + 22
	_text(Vector2(r.position.x + 30, y + 22), "Skin", 18, _ink(), f_bold)
	var sx := r.position.x + 230
	for s in [["classic", "Classic (1997)"], ["modern", "Modern"]]:
		var b := Rect2(sx, y, 190, 34)
		_chip(b, s[1], skin == s[0])
		_hit(b, "ng", {"what": "skin", "val": s[0]})
		sx += 200
	var go := Rect2(r.end.x - 220, r.end.y - 64, 190, 44)
	_btn(go, "Start", true)
	_hit(go, "ng", {"what": "start"})
	hits.push_front({"r": Rect2(0, 0, W, H), "kind": "none"})

func _chip(r: Rect2, label: String, on: bool) -> void:
	if skin == "modern":
		_gold_button(r, label, on)
	else:
		_bevel(r, not on, Color(0.85, 0.85, 0.85) if not on else Color(0.70, 0.70, 0.78))
		_text(Vector2(r.position.x, r.position.y + r.size.y * 0.5 + 6), label, _fit(label, f_ui, 16, r.size.x - 8),
				Color.BLACK, f_ui, r.size.x, HORIZONTAL_ALIGNMENT_CENTER)
