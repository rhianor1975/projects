# Rokugan -- the table.
#
# Two skins over one game, built from Control nodes.  Classic is the first
# mockup: a 1997 Windows window, real menus and grey bevelled buttons, the
# honor track, FATE and GOLD (KOKU), Provinces with their buttons, the
# Home Zone between the Fate and Dynasty decks.  Modern is the second:
# lacquer and gold, painted backdrop, portraits, ring tokens, the phase
# list and the log down the right.
#
# The shell (panels, labels, buttons) is rebuilt when the skin changes.
# The cards live in their own layer as Card nodes keyed by instance, so a
# card the View moves -- recruited from a Province, attached, drawn --
# glides there rather than blinking out and in.
extends Control

const PROC := preload("res://Proc.gd")
const DB := preload("res://CardDB.gd")
const CARD := preload("res://Card.gd")

const W := 1600.0
const H := 1000.0

const CLAN_COLOR := {
	"Crab": Color(0.27, 0.36, 0.46), "Crane": Color(0.42, 0.66, 0.84),
	"Dragon": Color(0.20, 0.50, 0.32), "Lion": Color(0.80, 0.63, 0.16),
	"Phoenix": Color(0.86, 0.45, 0.16), "Scorpion": Color(0.66, 0.13, 0.13),
	"Unicorn": Color(0.45, 0.27, 0.64), "Mantis": Color(0.13, 0.55, 0.45),
	"Spider": Color(0.22, 0.20, 0.22), "Unaligned": Color(0.45, 0.40, 0.34),
}
const ERAS := [["gold", "Gold (Four Winds)"], ["celestial", "Celestial (Destroyer War)"], ["ivory", "Ivory (A Brother's Destiny)"]]
const PHASES := ["Straighten", "Action", "Attack", "Dynasty", "End"]
const RINGS := [["earth", "Earth"], ["water", "Water"], ["fire", "Fire"], ["air", "Air"], ["void", "Void"]]

enum { A_PASS, A_BUY, A_CYCLE, A_PLAY, A_USE, A_ATTACK, A_NOATTACK, A_ASSIGN, A_DONE, A_DISCARD }

var root := ""
var proc
var db
var v := {}
var skin := "classic"
var era := "gold"
var my_clan := "Crab"
var opp_clan := "Random"

var f_ui: SystemFont
var f_bold: SystemFont
var f_serif: SystemFont
var _tex := {}

var shell: Control
var board: Control
var top: Control
var cards := {}
var piles := {}
var ui := {}
var L := {}
var popup: PopupMenu
var popup_cmds: Array = []
var zoom: Control

var shot := ""
var shot_frames := 30
var autoplay := 0

# ================================================================ start
func _ready() -> void:
	root = ProjectSettings.globalize_path("res://").get_base_dir().get_base_dir() + "/"
	f_ui = _font(["Tahoma", "Microsoft Sans Serif", "Liberation Sans", "DejaVu Sans", "Arial"], 400)
	f_bold = _font(["Tahoma", "Liberation Sans", "DejaVu Sans", "Arial"], 700)
	f_serif = _font(["Georgia", "Palatino Linotype", "Book Antiqua", "DejaVu Serif", "Liberation Serif"], 700)
	db = DB.new()
	proc = PROC.new()
	popup = PopupMenu.new()
	add_child(popup)
	popup.id_pressed.connect(_on_popup)
	_load_settings()
	_parse_args()
	_build()
	_new_game(int(Time.get_unix_time_from_system()) % 100000)

func _font(names: Array, weight: int) -> SystemFont:
	var f := SystemFont.new()
	f.font_names = PackedStringArray(names)
	f.font_weight = weight
	return f

func tex(name: String) -> Texture2D:
	if not _tex.has(name):
		_tex[name] = load("res://assets/%s.png" % name)
	return _tex[name]

func mon_tex(clan: String) -> Texture2D:
	return tex("mon_" + (clan if CLAN_COLOR.has(clan) else "Unaligned"))

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
	var err: String = db.load_era(root, era)
	if err != "":
		_message(err)
		return
	var clans := clans_for(era)
	if clans.is_empty():
		_message("No decks for this era in decks/%s/." % era)
		return
	if not my_clan in clans:
		my_clan = clans[0]
	var opp: String = opp_clan
	if not opp in clans:
		opp = clans[seed % clans.size()]
		if opp == my_clan and clans.size() > 1:
			opp = clans[(seed + 1) % clans.size()]
	var r: Dictionary
	if proc.running():
		r = proc.send("new %s %s %s %d" % [era, my_clan, opp, seed])
	else:
		var e: String = proc.start(root + "rokugan", PackedStringArray(["--serve", "--home", root,
				"--era", era, "--clan0", my_clan, "--clan1", opp, "--seed", str(seed)]))
		if e != "":
			_message(e)
			return
		r = proc.read_view()
	_save_settings()
	_take(r)

func _exit_tree() -> void:
	if proc:
		proc.stop()

func _process(_d: float) -> void:
	if shot == "":
		return
	if autoplay > 0 and not v.is_empty():
		_autoplay_step()
		autoplay -= 1
		return
	shot_frames -= 1
	if shot_frames == 0:
		get_viewport().get_texture().get_image().save_png(shot)
		get_tree().quit()

func _autoplay_step() -> void:
	var acts: Array = v.get("actions", [])
	if acts.is_empty() or int(v.get("winner", -1)) >= 0:
		autoplay = 0
		return
	var pick := 0
	for i in acts.size():
		var k: int = int(acts[i]["k"])
		if k == A_BUY or (k == A_PLAY and int(acts[i]["p"]) >= 0) or k == A_ATTACK or k == A_ASSIGN:
			pick = i
			break
	_act(pick)

# ============================================================== the wire
func _act(i: int) -> void:
	_send("act %d" % i)

func _send(cmd: String) -> void:
	_take(proc.send(cmd))

func _take(r: Dictionary) -> void:
	if r.has("error"):
		_message(r["error"])
		return
	v = r
	_sync()

func me() -> Dictionary:
	return v["players"][int(v["seat"])]

func opp() -> Dictionary:
	return v["players"][1 - int(v["seat"])]

func my_turn() -> bool:
	return not v.is_empty() and int(v["active"]) == int(v["seat"])

func my_say() -> bool:
	return not v.is_empty() and int(v["decider"]) == int(v["seat"])

func actions_for(inst: int) -> Array:
	var out: Array = []
	var acts: Array = v.get("actions", [])
	for i in acts.size():
		if int(acts[i]["s"]) == inst:
			out.append(i)
	return out

func actions_for_prov(k: int) -> Array:
	var out: Array = []
	var acts: Array = v.get("actions", [])
	for i in acts.size():
		var kind := int(acts[i]["k"])
		if int(acts[i]["v"]) == k and (kind == A_BUY or kind == A_CYCLE):
			out.append(i)
	return out

func global_actions() -> Array:
	var out: Array = []
	var acts: Array = v.get("actions", [])
	for i in acts.size():
		if int(acts[i]["s"]) < 0:
			out.append(i)
	return out

func phase_label() -> String:
	var ph: String = v.get("phase", "")
	match ph:
		"Assign", "Defend", "Battle": return "Attack"
		"Discard": return "End"
	return ph

# The card's laid-out size in this skin, and its height over its width.
func cbase() -> Vector2:
	return CARD.base_for(skin)

func cr() -> float:
	return cbase().y / cbase().x

func _n(x) -> String:
	return str(int(float(x)))

# ================================================================ themes
func _sb_tex(name: String, m: int, content := 6) -> StyleBoxTexture:
	var s := StyleBoxTexture.new()
	s.texture = tex(name)
	s.texture_margin_left = m
	s.texture_margin_right = m
	s.texture_margin_top = m
	s.texture_margin_bottom = m
	s.content_margin_left = content + 2
	s.content_margin_right = content + 2
	s.content_margin_top = content * 0.6
	s.content_margin_bottom = content * 0.6
	return s

func _sb_flat(col: Color, border := Color.TRANSPARENT, bw := 0) -> StyleBoxFlat:
	var s := StyleBoxFlat.new()
	s.bg_color = col
	s.border_color = border
	s.set_border_width_all(bw)
	s.content_margin_left = 8
	s.content_margin_right = 8
	s.content_margin_top = 4
	s.content_margin_bottom = 4
	return s

func _theme() -> Theme:
	var t := Theme.new()
	if skin == "modern":
		t.default_font = f_serif
		t.default_font_size = 17
		var up := _sb_tex("gold_button", 8, 8)
		var lit := _sb_tex("gold_button_lit", 8, 8)
		for cls in ["Button", "OptionButton", "MenuButton"]:
			t.set_stylebox("normal", cls, up)
			t.set_stylebox("hover", cls, lit)
			t.set_stylebox("pressed", cls, lit)
			t.set_stylebox("disabled", cls, up)
			t.set_stylebox("focus", cls, StyleBoxEmpty.new())
			t.set_color("font_color", cls, Color(0.98, 0.90, 0.70))
			t.set_color("font_hover_color", cls, Color(1, 0.96, 0.8))
			t.set_color("font_pressed_color", cls, Color(1, 0.96, 0.8))
			t.set_color("font_disabled_color", cls, Color(0.55, 0.48, 0.38))
		t.set_stylebox("panel", "Panel", _sb_tex("panel_modern", 14, 10))
		t.set_stylebox("panel", "PanelContainer", _sb_tex("panel_modern", 14, 12))
		t.set_color("font_color", "Label", Color(0.95, 0.88, 0.72))
		t.set_stylebox("panel", "PopupMenu", _sb_flat(Color(0.08, 0.06, 0.05), Color(0.75, 0.58, 0.30), 2))
		t.set_stylebox("hover", "PopupMenu", _sb_flat(Color(0.40, 0.29, 0.12)))
		t.set_color("font_color", "PopupMenu", Color(0.95, 0.88, 0.72))
		t.set_color("font_hover_color", "PopupMenu", Color(1, 0.95, 0.8))
		t.set_color("default_color", "RichTextLabel", Color(0.20, 0.13, 0.08))
	else:
		t.default_font = f_ui
		t.default_font_size = 15
		var up := _sb_tex("bevel_up", 3, 6)
		var down := _sb_tex("bevel_down", 3, 6)
		for cls in ["Button", "OptionButton", "MenuButton"]:
			t.set_stylebox("normal", cls, up)
			t.set_stylebox("hover", cls, up)
			t.set_stylebox("pressed", cls, down)
			t.set_stylebox("disabled", cls, up)
			t.set_stylebox("focus", cls, StyleBoxEmpty.new())
			t.set_color("font_color", cls, Color.BLACK)
			t.set_color("font_hover_color", cls, Color.BLACK)
			t.set_color("font_pressed_color", cls, Color.BLACK)
			t.set_color("font_disabled_color", cls, Color(0.45, 0.45, 0.45))
		t.set_stylebox("panel", "Panel", up)
		t.set_stylebox("panel", "PanelContainer", up)
		t.set_color("font_color", "Label", Color.BLACK)
		t.set_stylebox("panel", "PopupMenu", _sb_tex("bevel_up", 3, 4))
		t.set_stylebox("hover", "PopupMenu", _sb_flat(Color(0.0, 0.0, 0.5)))
		t.set_color("font_color", "PopupMenu", Color.BLACK)
		t.set_color("font_hover_color", "PopupMenu", Color.WHITE)
		t.set_stylebox("normal", "MenuBar", StyleBoxEmpty.new())
		t.set_stylebox("hover", "MenuBar", _sb_flat(Color(0.0, 0.0, 0.5)))
		t.set_stylebox("pressed", "MenuBar", _sb_flat(Color(0.0, 0.0, 0.5)))
		t.set_color("font_color", "MenuBar", Color.BLACK)
		t.set_color("font_hover_color", "MenuBar", Color.WHITE)
		t.set_color("font_pressed_color", "MenuBar", Color.WHITE)
		t.set_color("default_color", "RichTextLabel", Color.BLACK)
	return t

# ============================================================ the shell
func _build() -> void:
	if shell:
		shell.queue_free()
	if board:
		board.queue_free()
	if top:
		top.queue_free()
	cards.clear()
	ui.clear()
	var th := _theme()
	theme = th
	popup.theme = th
	shell = Control.new()
	shell.size = Vector2(W, H)
	shell.mouse_filter = Control.MOUSE_FILTER_PASS
	add_child(shell)
	board = Control.new()
	board.size = Vector2(W, H)
	board.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(board)
	top = Control.new()
	top.size = Vector2(W, H)
	top.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(top)
	if skin == "modern":
		_build_modern()
	else:
		_build_classic()
	_build_dialogs()
	if not v.is_empty():
		_sync()

# small builders
func _rect(parent: Control, r: Rect2, col: Color) -> ColorRect:
	var c := ColorRect.new()
	c.position = r.position
	c.size = r.size
	c.color = col
	c.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(c)
	return c

func _texr(parent: Control, r: Rect2, t: Texture2D, mode := TextureRect.STRETCH_SCALE) -> TextureRect:
	var x := TextureRect.new()
	x.position = r.position
	x.size = r.size
	x.texture = t
	x.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	x.stretch_mode = mode
	x.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(x)
	return x

func _label(parent: Control, r: Rect2, s: String, size := 15, col := Color(-1, 0, 0), font: Font = null,
		align := HORIZONTAL_ALIGNMENT_LEFT, valign := VERTICAL_ALIGNMENT_CENTER) -> Label:
	var l := Label.new()
	l.position = r.position
	l.size = r.size
	l.text = s
	l.horizontal_alignment = align
	l.vertical_alignment = valign
	l.add_theme_font_size_override("font_size", size)
	if col.r >= 0:
		l.add_theme_color_override("font_color", col)
	if font:
		l.add_theme_font_override("font", font)
	l.clip_text = true
	l.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(l)
	return l

func _panel(parent: Control, r: Rect2, style: StyleBox = null) -> Panel:
	var p := Panel.new()
	p.position = r.position
	p.size = r.size
	if style:
		p.add_theme_stylebox_override("panel", style)
	p.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(p)
	return p

func _button(parent: Control, r: Rect2, s: String, cb: Callable, size := 15) -> Button:
	var b := Button.new()
	b.position = r.position
	b.size = r.size
	b.text = s
	b.clip_text = true
	b.add_theme_font_size_override("font_size", size)
	b.pressed.connect(cb)
	b.focus_mode = Control.FOCUS_NONE
	parent.add_child(b)
	return b

func _nine(parent: Control, r: Rect2, t: Texture2D, m: int) -> NinePatchRect:
	var n := NinePatchRect.new()
	n.position = r.position
	n.size = r.size
	n.texture = t
	n.patch_margin_left = m
	n.patch_margin_right = m
	n.patch_margin_top = m
	n.patch_margin_bottom = m
	n.draw_center = false
	n.mouse_filter = Control.MOUSE_FILTER_IGNORE
	parent.add_child(n)
	return n

func _portrait(parent: Control, r: Rect2, key: String) -> void:
	# the champion's art, framed; filled in by _sync
	_rect(parent, r, Color(0.12, 0.08, 0.05))
	var art := _texr(parent, r.grow(-8), null, TextureRect.STRETCH_KEEP_ASPECT_COVERED)
	_nine(parent, r, tex("frame_modern" if skin == "modern" else "frame_classic"), 10 if skin == "modern" else 22)
	ui[key] = art

# ------------------------------------------------------------- classic
func _build_classic() -> void:
	_rect(shell, Rect2(0, 0, W, H), Color(0.75, 0.75, 0.75))
	# title bar, with the clan's mon as its icon
	var g := Gradient.new()
	g.set_color(0, Color(0.0, 0.0, 0.5))
	g.set_color(1, Color(0.06, 0.52, 0.82))
	var gt := GradientTexture2D.new()
	gt.gradient = g
	gt.width = 256
	gt.height = 4
	_texr(shell, Rect2(3, 3, W - 6, 30), gt)
	ui["icon"] = _texr(shell, Rect2(8, 7, 22, 22), mon_tex(my_clan))
	_label(shell, Rect2(36, 3, 800, 30), "Legend of the Five Rings (L5R)", 18, Color.WHITE, f_bold)
	var bx := W - 90
	for gl in ["_", "□", "X"]:
		var cb := (func(): get_tree().quit()) if gl == "X" else (func(): pass)
		_button(shell, Rect2(bx, 7, 26, 22), gl, cb, 13)
		bx += 28
	var mb := MenuBar.new()
	mb.position = Vector2(8, 34)
	mb.size = Vector2(400, 26)
	mb.flat = true
	mb.add_theme_font_size_override("font_size", 17)
	shell.add_child(mb)
	for m in [["File", [["New Game...", "new"], ["Quit", "quit"]]],
			["Options", [["Classic skin (1997)", "skin classic"], ["Modern skin", "skin modern"]]],
			["Help", [["How to play", "rules"], ["About", "about"]]]]:
		var pm := PopupMenu.new()
		pm.name = m[0]
		var cmds: Array = []
		for it in m[1]:
			pm.add_item(it[0], cmds.size())
			cmds.append(it[1])
		pm.id_pressed.connect(func(id): _menu_command(cmds[id]))
		mb.add_child(pm)
	# the table, with a darker strip under each zone
	_panel(shell, Rect2(4, 62, W - 8, 872), _sb_tex("bevel_down", 3))
	_texr(shell, Rect2(6, 64, W - 12, 868), tex("wood"), TextureRect.STRETCH_TILE)
	var cx := 192.0
	var cw := W - 384.0
	for band in [Rect2(cx, 66, cw, 210), Rect2(cx, 474, cw, 214)]:
		_rect(shell, band, Color(0.08, 0.04, 0.01, 0.30))
		_rect(shell, Rect2(band.position, Vector2(band.size.x, 2)), Color(0.05, 0.02, 0.0, 0.5))
		_rect(shell, Rect2(band.position.x, band.end.y - 2, band.size.x, 2), Color(0.05, 0.02, 0.0, 0.5))
	# left: the honor track, my clan
	_panel(shell, Rect2(8, 66, 176, 632))
	_label(shell, Rect2(8, 70, 176, 28), "HONOR", 19, Color.BLACK, f_bold, HORIZONTAL_ALIGNMENT_CENTER)
	var track := Control.new()
	track.position = Vector2(14, 102)
	track.size = Vector2(164, 236)
	track.draw.connect(_draw_track.bind(track))
	shell.add_child(track)
	ui["track"] = track
	ui["honor_legend"] = _label(shell, Rect2(8, 342, 176, 24), "", 16, Color.BLACK, f_ui, HORIZONTAL_ALIGNMENT_CENTER)
	ui["my_banner"] = _clan_head(Rect2(14, 372, 164, 32))
	_portrait(shell, Rect2(14, 406, 164, 284), "my_face")
	ui["my_mon"] = _texr(shell, Rect2(132, 642, 42, 42), null)
	# right: FATE, GOLD (KOKU), the opponent and the table's numbers
	var x := W - 184
	_panel(shell, Rect2(x, 66, 176, 70))
	_label(shell, Rect2(x, 68, 176, 24), "FATE", 17, Color.BLACK, f_bold, HORIZONTAL_ALIGNMENT_CENTER)
	_texr(shell, Rect2(x + 14, 94, 36, 36), tex("coin_fate"))
	ui["fate"] = _counter(Rect2(x + 64, 94, 98, 34))
	_panel(shell, Rect2(x, 142, 176, 74))
	_label(shell, Rect2(x, 144, 176, 24), "GOLD (KOKU)", 17, Color.BLACK, f_bold, HORIZONTAL_ALIGNMENT_CENTER)
	_texr(shell, Rect2(x + 10, 168, 44, 44), tex("ingot"))
	ui["gold"] = _counter(Rect2(x + 64, 172, 98, 34))
	_panel(shell, Rect2(x, 222, 176, 270))
	ui["opp_banner"] = _clan_head(Rect2(x + 6, 228, 164, 32))
	_portrait(shell, Rect2(x + 6, 262, 164, 222), "opp_face")
	ui["opp_mon"] = _texr(shell, Rect2(x + 124, 436, 42, 42), null)
	_panel(shell, Rect2(x, 498, 176, 236), _sb_tex("bevel_down", 3))
	ui["info"] = []
	for i in 8:
		var y := 506 + i * 27
		var l := _label(shell, Rect2(x + 10, y, 90, 24), "", 15, Color.BLACK, f_ui)
		var r := _label(shell, Rect2(x + 76, y, 92, 24), "", 15, Color.BLACK, f_ui, HORIZONTAL_ALIGNMENT_RIGHT)
		ui["info"].append([l, r])
	# the opponent's row
	ui["opp_title"] = _label(shell, Rect2(cx, 68, cw, 28), "", 21, Color(1, 0.94, 0.80), f_serif, HORIZONTAL_ALIGNMENT_CENTER)
	L["sq"] = 140.0
	L["opp_row"] = Rect2(cx + 16, 100, cw - 32, 172)
	# my Provinces, each with its label -- a button only when it has something to do
	L["my_prov"] = []
	ui["prov_btn"] = []
	var pw: float = L["sq"]
	var gap := 40.0
	var px := cx + (cw - (pw * 4 + gap * 3)) * 0.5
	for k in 4:
		var r := Rect2(px + k * (pw + gap), 284, pw, pw * cr())
		L["my_prov"].append(r)
		var b := _button(shell, Rect2(r.position.x - 22, r.end.y + 3, pw + 44, 24), "", _prov_pressed.bind(k), 13)
		b.add_theme_color_override("font_disabled_color", Color(1, 0.94, 0.80))
		b.add_theme_font_override("font", f_serif)
		ui["prov_btn"].append(b)
	# the Home Zone, between the decks
	var tagr := Rect2(cx + cw * 0.5 - 80, 478, 160, 26)
	_panel(shell, tagr, _sb_flat(Color(0.20, 0.12, 0.05, 0.9), Color(0.80, 0.62, 0.30), 2))
	_label(shell, tagr, "Home Zone:", 17, Color(1, 0.94, 0.80), f_serif, HORIZONTAL_ALIGNMENT_CENTER)
	L["fate_deck"] = Rect2(cx + 16, 512, 118, 118 * cr())
	L["dyn_deck"] = Rect2(cx + cw - 134, 512, 118, 118 * cr())
	_deck_stack(L["fate_deck"], "back_fate")
	_deck_stack(L["dyn_deck"], "back_dynasty")
	ui["fate_deck_lbl"] = _deck_label(L["fate_deck"])
	ui["dyn_deck_lbl"] = _deck_label(L["dyn_deck"])
	ui["dyn_mon"] = _texr(shell, Rect2(L["dyn_deck"].get_center() - Vector2(28, 14), Vector2(56, 56)), null)
	L["home_row"] = Rect2(L["fate_deck"].end.x + 26, 506, L["dyn_deck"].position.x - L["fate_deck"].end.x - 52, 178)
	# the hand
	L["hand"] = Rect2(cx + 10, 700, cw - 20, 156)
	L["hand_w"] = CARD.TEXT.x
	# the status bar
	_panel(shell, Rect2(4, H - 62, W - 8, 58))
	_panel(shell, Rect2(12, H - 54, 340, 44), _sb_tex("bevel_down", 3))
	ui["phase"] = _label(shell, Rect2(22, H - 54, 280, 44), "", 20, Color.BLACK, f_bold)
	var bulb := Control.new()
	bulb.position = Vector2(306, H - 52)
	bulb.size = Vector2(40, 44)
	bulb.draw.connect(_draw_bulb.bind(bulb))
	shell.add_child(bulb)
	ui["bulb"] = bulb
	_panel(shell, Rect2(362, H - 54, 620, 44), _sb_tex("bevel_down", 3))
	ui["ticker"] = _label(shell, Rect2(374, H - 54, 600, 44), "", 17, Color.BLACK, f_ui)
	var acts := HBoxContainer.new()
	acts.position = Vector2(990, H - 55)
	acts.size = Vector2(486, 46)
	acts.alignment = BoxContainer.ALIGNMENT_END
	acts.add_theme_constant_override("separation", 8)
	shell.add_child(acts)
	ui["acts"] = acts
	_button(shell, Rect2(W - 116, H - 55, 104, 46), "Log", _toggle_log, 19)
	# the log, as a window over the table
	var lw := Panel.new()
	lw.position = Vector2(W * 0.5 - 380, 140)
	lw.size = Vector2(760, 600)
	lw.visible = false
	top.add_child(lw)
	_texr(lw, Rect2(3, 3, 754, 28), gt)
	_label(lw, Rect2(10, 3, 600, 28), "Log", 16, Color.WHITE, f_bold)
	_button(lw, Rect2(728, 6, 24, 22), "X", _toggle_log, 12)
	ui["log"] = _rich(lw, Rect2(14, 40, 732, 548), 15)
	ui["log_win"] = lw

# A clan's name in a navy bar, over its portrait.
func _clan_head(r: Rect2) -> Label:
	var p := _panel(shell, r, _sb_flat(Color(0.05, 0.10, 0.42), Color(0.85, 0.70, 0.35), 2))
	return _label(p, Rect2(Vector2.ZERO, r.size), "", 19, Color.WHITE, f_serif, HORIZONTAL_ALIGNMENT_CENTER)

# A number in a white sunken box, as the mockup counts Fate and Gold.
func _counter(r: Rect2) -> Label:
	_panel(shell, r, _sb_tex("bevel_down", 3))
	_rect(shell, r.grow(-3), Color.WHITE)
	return _label(shell, r, "", 26, Color.BLACK, f_bold, HORIZONTAL_ALIGNMENT_CENTER)

func _deck_label(r: Rect2) -> Label:
	var l := _label(shell, Rect2(r.position.x, r.position.y + 10, r.size.x, 52), "", 15, Color(1, 0.95, 0.82),
			f_serif, HORIZONTAL_ALIGNMENT_CENTER, VERTICAL_ALIGNMENT_TOP)
	l.add_theme_color_override("font_outline_color", Color(0, 0, 0))
	l.add_theme_constant_override("outline_size", 4)
	return l

func _deck_stack(r: Rect2, t: String) -> void:
	for i in 3:
		_texr(shell, Rect2(r.position + Vector2(6 - i * 3, 6 - i * 3), r.size), tex(t))

func _rich(parent: Control, r: Rect2, size: int) -> RichTextLabel:
	var t := RichTextLabel.new()
	t.position = r.position
	t.size = r.size
	t.bbcode_enabled = true
	t.scroll_following = true
	t.add_theme_font_size_override("normal_font_size", size)
	t.add_theme_font_size_override("bold_font_size", size)
	t.add_theme_font_override("normal_font", f_ui)
	t.add_theme_font_override("bold_font", f_bold)
	t.mouse_filter = Control.MOUSE_FILTER_PASS
	parent.add_child(t)
	return t

# The honor track as a graph: a line every 10, from 40 (win) to -20
# (lose), and a marker for each player, tagged at the right.
func _draw_track(c: Control) -> void:
	c.draw_rect(Rect2(Vector2.ZERO, c.size), Color(0.97, 0.97, 0.95))
	var top := 12.0
	var bot := c.size.y - 12
	var lx := 34.0
	var rx := c.size.x - 8
	for val in range(40, -21, -10):
		var y := top + (40 - val) / 60.0 * (bot - top)
		c.draw_line(Vector2(lx, y), Vector2(rx, y), Color(0.55, 0.55, 0.55) if val != 0 else Color(0.2, 0.2, 0.2), 1)
		c.draw_string(f_ui, Vector2(0, y + 6), str(val), HORIZONTAL_ALIGNMENT_RIGHT, lx - 6, 15, Color(0.1, 0.1, 0.1))
	if v.is_empty():
		return
	var ys: Array = []
	for who in [[me(), Color(0.12, 0.30, 0.85), "You"], [opp(), Color(0.78, 0.10, 0.08), "Opp"]]:
		var hv := clampf(float(who[0]["honor"]), -20, 40)
		var y := top + (40 - hv) / 60.0 * (bot - top)
		# a second tag that would sit on the first steps left of it
		var tx := rx - 42 - (46 if ys.size() and absf(ys[0] - y) < 20 else 0)
		ys.append(y)
		c.draw_line(Vector2(lx, y), Vector2(tx, y), who[1], 4)
		var tag := Rect2(tx, y - 10, 42, 20)
		c.draw_rect(tag, who[1])
		c.draw_string(f_serif, Vector2(tag.position.x, tag.end.y - 5), who[2], HORIZONTAL_ALIGNMENT_CENTER, tag.size.x, 13, Color.WHITE)

func _draw_bulb(c: Control) -> void:
	var on := my_say()
	if on:
		c.draw_circle(Vector2(20, 18), 17, Color(1, 0.9, 0.3, 0.25))
	c.draw_circle(Vector2(20, 18), 12, Color(1.0, 0.88, 0.25) if on else Color(0.6, 0.6, 0.56))
	c.draw_rect(Rect2(14, 29, 12, 9), Color(0.45, 0.45, 0.45))
	c.draw_line(Vector2(14, 32), Vector2(26, 32), Color(0.3, 0.3, 0.3), 1)

# -------------------------------------------------------------- modern
func _build_modern() -> void:
	ui["bg"] = _texr(shell, Rect2(0, 0, W, H), tex("dusk"), TextureRect.STRETCH_KEEP_ASPECT_COVERED)
	_rect(shell, Rect2(0, 0, W, H), Color(0.03, 0.02, 0.02, 0.45))
	# left: the five rings, the title, the decks, my portrait
	_panel(shell, Rect2(8, 8, 226, 166))
	for i in 5:
		var a := TAU * i / 5.0 - PI * 0.5
		var p := Vector2(121, 62) + Vector2(cos(a), sin(a)) * 34
		_texr(shell, Rect2(p - Vector2(15, 15), Vector2(30, 30)), tex("ring_" + RINGS[i][0]))
	_label(shell, Rect2(8, 104, 226, 26), "Legend of the", 17, Color(0.93, 0.78, 0.44), f_serif, HORIZONTAL_ALIGNMENT_CENTER)
	_label(shell, Rect2(8, 128, 226, 34), "Five Rings", 27, Color(0.96, 0.80, 0.44), f_serif, HORIZONTAL_ALIGNMENT_CENTER)
	_panel(shell, Rect2(8, 182, 226, 318))
	_label(shell, Rect2(8, 190, 226, 26), "Rings in play", 17, Color(0.95, 0.85, 0.6), f_serif, HORIZONTAL_ALIGNMENT_CENTER)
	ui["rings"] = []
	for i in 5:
		var y := 224 + i * 54
		var t := _texr(shell, Rect2(26, y, 44, 44), tex("ring_%s_off" % RINGS[i][0]))
		var l := _label(shell, Rect2(84, y, 140, 44), RINGS[i][1], 20, Color(0.6, 0.55, 0.48), f_serif)
		ui["rings"].append([t, l])
	_panel(shell, Rect2(8, 508, 226, 228))
	_label(shell, Rect2(22, 516, 200, 24), "Draw Deck", 17, Color(0.92, 0.85, 0.7), f_serif)
	L["fate_deck"] = Rect2(26, 544, 54, 76)
	_texr(shell, L["fate_deck"], tex("back_modern"))
	ui["fate_n"] = _label(shell, Rect2(92, 560, 120, 40), "", 26, Color(1, 0.9, 0.6), f_bold)
	_label(shell, Rect2(22, 626, 200, 24), "Discard Pile", 17, Color(0.92, 0.85, 0.7), f_serif)
	_texr(shell, Rect2(26, 652, 54, 76), tex("back_modern")).modulate = Color(0.6, 0.6, 0.6)
	ui["fdisc_n"] = _label(shell, Rect2(92, 668, 120, 40), "", 26, Color(1, 0.9, 0.6), f_bold)
	_portrait_modern(Rect2(8, 744, 226, 248), "my_face", "my_name", "my_honor")
	# top: the opponent
	var cx := 242.0
	var cw := W - 242 - 252
	_portrait_modern(Rect2(cx, 8, 168, 180), "opp_face", "opp_name", "opp_honor")
	_panel(shell, Rect2(cx + 176, 8, cw - 176, 180))
	var hold := Rect2(cx + cw - 236, 18, 226, 160)
	_texr(shell, hold, tex("parchment"), TextureRect.STRETCH_TILE).modulate = Color(0.85, 0.8, 0.72)
	ui["opp_holds"] = _rich(shell, hold.grow(-6), 12)
	L["opp_prov"] = []
	for k in 4:
		L["opp_prov"].append(Rect2(cx + 190 + k * 82, 26, 76, 106))
	L["opp_units"] = Rect2(cx + 190 + 4 * 82 + 10, 26, hold.position.x - (cx + 190 + 4 * 82 + 10) - 10, 106)
	ui["opp_hand"] = _label(shell, Rect2(cx + 190, 150, 320, 30), "", 15, Color(0.92, 0.85, 0.7), f_serif)
	# my provinces, with the Dynasty deck beside them as in the mockup
	_rect(shell, Rect2(cx, 196, cw, 2), Color(0.70, 0.54, 0.28, 0.8))
	L["my_prov"] = []
	ui["prov_btn"] = []
	var pw := 122.0
	var gap := (cw - 130 - pw * 4) / 5.0
	for k in 4:
		var r := Rect2(cx + gap + k * (pw + gap), 208, pw, pw * cr())
		L["my_prov"].append(r)
		ui["prov_btn"].append(_button(shell, Rect2(r.position.x - 6, r.end.y + 6, pw + 12, 34), "Province",
				_prov_pressed.bind(k), 15))
	L["dyn_deck"] = Rect2(cx + cw - 118, 222, 104, 146)
	_texr(shell, L["dyn_deck"], tex("back_modern"))
	ui["dyn_n"] = _label(shell, Rect2(L["dyn_deck"].position.x, L["dyn_deck"].end.y + 4, 104, 26), "", 17,
			Color(1, 0.9, 0.6), f_bold, HORIZONTAL_ALIGNMENT_CENTER)
	# the Home Zone
	_rect(shell, Rect2(cx, 428, cw, 2), Color(0.70, 0.54, 0.28, 0.8))
	var home := Rect2(cx, 434, cw, 300)
	_label(shell, Rect2(home.position.x, home.position.y, home.size.x, 26), "Home Zone", 18, Color(0.95, 0.85, 0.6),
			f_serif, HORIZONTAL_ALIGNMENT_CENTER)
	L["home_holds"] = Rect2(home.position.x + 6, home.position.y + 28, 5 * 66, home.size.y - 32)
	L["home_units"] = Rect2(L["home_holds"].end.x + 14, home.position.y + 28, home.end.x - L["home_holds"].end.x - 20, home.size.y - 32)
	L["unit_w"] = 122.0
	_rect(shell, Rect2(cx, 740, cw, 2), Color(0.70, 0.54, 0.28, 0.8))
	_panel(shell, Rect2(cx, 746, cw, 246))
	L["hand"] = Rect2(cx + 10, 756, cw - 20, 230)
	L["hand_w"] = 136.0
	# right: the banner, the phases, what you can do, the log
	var rx := W - 244
	_panel(shell, Rect2(rx, 8, 236, 300))
	_texr(shell, Rect2(rx + 12, 20, 70, 276), tex("banner"))
	ui["scene"] = _texr(shell, Rect2(rx + 88, 20, 136, 276), tex("dusk"), TextureRect.STRETCH_KEEP_ASPECT_COVERED)
	ui["phases"] = []
	var py := 318.0
	for ph in PHASES:
		var b := _button(shell, Rect2(rx + 8, py, 220, 38), ph, func(): pass, 18)
		b.mouse_filter = Control.MOUSE_FILTER_IGNORE
		ui["phases"].append(b)
		py += 42
	ui["turn"] = _label(shell, Rect2(rx, py + 2, 236, 26), "", 15, Color(1, 0.9, 0.6), f_serif, HORIZONTAL_ALIGNMENT_CENTER)
	var acts := VBoxContainer.new()
	acts.position = Vector2(rx + 8, py + 32)
	acts.size = Vector2(220, 100)
	acts.add_theme_constant_override("separation", 6)
	shell.add_child(acts)
	ui["acts"] = acts
	_button(shell, Rect2(rx + 8, 642, 106, 32), "New Game", func(): _show_new(true), 13)
	_button(shell, Rect2(rx + 122, 642, 106, 32), "Classic", func(): _menu_command("skin classic"), 15)
	var logr := Rect2(rx, 682, 236, 310)
	_texr(shell, logr, tex("parchment"), TextureRect.STRETCH_TILE)
	_panel(shell, logr, _sb_flat(Color.TRANSPARENT, Color(0.5, 0.35, 0.18), 3))
	ui["log"] = _rich(shell, logr.grow(-12), 13)

func _portrait_modern(r: Rect2, face: String, name: String, honor: String) -> void:
	_panel(shell, r)
	var pic := Rect2(r.position + Vector2(8, 8), r.size - Vector2(16, 48))
	_rect(shell, pic, Color(0.1, 0.07, 0.05))
	ui[face] = _texr(shell, pic, null, TextureRect.STRETCH_KEEP_ASPECT_COVERED)
	ui[name] = _label(shell, Rect2(r.position.x + 12, r.end.y - 38, r.size.x - 70, 32), "", 18,
			Color(0.95, 0.88, 0.72), f_serif)
	_panel(shell, Rect2(r.end.x - 52, r.end.y - 46, 44, 40), _sb_flat(Color(0.1, 0.07, 0.05), Color(0.78, 0.62, 0.32), 2))
	ui[honor] = _label(shell, Rect2(r.end.x - 52, r.end.y - 46, 44, 40), "", 21, Color(1, 0.9, 0.6), f_bold,
			HORIZONTAL_ALIGNMENT_CENTER)

# ---------------------------------------------------------- dialogs
func _build_dialogs() -> void:
	# New Game
	var d := PanelContainer.new()
	d.position = Vector2(W * 0.5 - 330, 200)
	d.size = Vector2(660, 0)
	d.visible = false
	top.add_child(d)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 14)
	d.add_child(box)
	var t := Label.new()
	t.text = "New Game"
	t.add_theme_font_size_override("font_size", 26)
	t.add_theme_font_override("font", f_serif)
	t.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	box.add_child(t)
	var grid := GridContainer.new()
	grid.columns = 2
	grid.add_theme_constant_override("h_separation", 18)
	grid.add_theme_constant_override("v_separation", 12)
	box.add_child(grid)
	ui["ng_era"] = _option(grid, "Era")
	ui["ng_clan"] = _option(grid, "Your clan")
	ui["ng_opp"] = _option(grid, "Opponent")
	ui["ng_skin"] = _option(grid, "Skin")
	ui["ng_era"].item_selected.connect(func(_i): _fill_new(ERAS[ui["ng_era"].selected][0]))
	var row := HBoxContainer.new()
	row.alignment = BoxContainer.ALIGNMENT_END
	row.add_theme_constant_override("separation", 10)
	box.add_child(row)
	var cancel := Button.new()
	cancel.text = "Cancel"
	cancel.custom_minimum_size = Vector2(120, 38)
	cancel.pressed.connect(func(): _show_new(false))
	row.add_child(cancel)
	var go := Button.new()
	go.text = "Start"
	go.custom_minimum_size = Vector2(120, 38)
	go.pressed.connect(_start_from_dialog)
	row.add_child(go)
	ui["new"] = d
	# a message
	var m := PanelContainer.new()
	m.position = Vector2(W * 0.5 - 380, 260)
	m.size = Vector2(760, 0)
	m.visible = false
	top.add_child(m)
	var mb := VBoxContainer.new()
	mb.add_theme_constant_override("separation", 16)
	m.add_child(mb)
	var ml := Label.new()
	ml.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ml.custom_minimum_size = Vector2(720, 0)
	mb.add_child(ml)
	var ok := Button.new()
	ok.text = "OK"
	ok.custom_minimum_size = Vector2(120, 36)
	ok.size_flags_horizontal = Control.SIZE_SHRINK_END
	ok.pressed.connect(func(): m.visible = false)
	mb.add_child(ok)
	ui["msg"] = m
	ui["msg_label"] = ml
	# the card, large
	zoom = Control.new()
	zoom.size = Vector2(W, H)
	zoom.visible = false
	zoom.mouse_filter = Control.MOUSE_FILTER_STOP
	zoom.gui_input.connect(func(e): if e is InputEventMouseButton and e.pressed: zoom.visible = false)
	top.add_child(zoom)
	_rect(zoom, Rect2(0, 0, W, H), Color(0, 0, 0, 0.7))
	ui["zoom_tex"] = _texr(zoom, Rect2(W * 0.5 - 40, 80, 600, 840), null, TextureRect.STRETCH_KEEP_ASPECT)
	ui["zoom_text"] = _label(zoom, Rect2(80, 100, W * 0.5 - 160, 800), "", 18, Color(1, 0.95, 0.85), f_ui,
			HORIZONTAL_ALIGNMENT_LEFT, VERTICAL_ALIGNMENT_TOP)
	ui["zoom_text"].autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	ui["zoom_text"].clip_text = false

func _option(grid: GridContainer, label: String) -> OptionButton:
	var l := Label.new()
	l.text = label
	l.add_theme_font_size_override("font_size", 18)
	grid.add_child(l)
	var o := OptionButton.new()
	o.custom_minimum_size = Vector2(420, 36)
	grid.add_child(o)
	return o

func _fill_new(e: String) -> void:
	var o: OptionButton = ui["ng_clan"]
	var p: OptionButton = ui["ng_opp"]
	o.clear()
	p.clear()
	var clans := clans_for(e)
	for c in clans:
		o.add_item(c)
		p.add_item(c)
	p.add_item("Random")
	o.select(maxi(0, clans.find(my_clan)))
	p.select(clans.find(opp_clan) if opp_clan in clans else clans.size())

func _show_new(on: bool) -> void:
	if on:
		var eo: OptionButton = ui["ng_era"]
		eo.clear()
		for e in ERAS:
			eo.add_item(e[1])
		eo.select(maxi(0, ERAS.map(func(x): return x[0]).find(era)))
		_fill_new(era)
		var so: OptionButton = ui["ng_skin"]
		so.clear()
		so.add_item("Classic (1997)")
		so.add_item("Modern")
		so.select(1 if skin == "modern" else 0)
	ui["new"].visible = on

func _start_from_dialog() -> void:
	era = ERAS[ui["ng_era"].selected][0]
	my_clan = ui["ng_clan"].get_item_text(ui["ng_clan"].selected)
	opp_clan = ui["ng_opp"].get_item_text(ui["ng_opp"].selected)
	var new_skin := "modern" if ui["ng_skin"].selected == 1 else "classic"
	_show_new(false)
	if new_skin != skin:
		skin = new_skin
		_build()
	_new_game(int(Time.get_unix_time_from_system()) % 100000)

func _message(s: String) -> void:
	if not ui.has("msg"):
		return
	ui["msg_label"].text = s
	ui["msg"].visible = true

func _toggle_log() -> void:
	if ui.has("log_win"):
		ui["log_win"].visible = not ui["log_win"].visible

func _menu_command(c: String) -> void:
	match c:
		"new": _show_new(true)
		"quit": get_tree().quit()
		"skin classic", "skin modern":
			skin = c.substr(5)
			_save_settings()
			_build()
		"rules": _message(RULES)
		"about": _message(ABOUT)

const RULES := """Win with 40 Family Honor at the start of your turn, by destroying all four enemy Provinces, or with five Rings in play. Fall to -20 and you lose.

Your turn: your cards straighten and your Provinces turn face up. Players then take Open actions in turn (and you Limited ones) until both pass. You may attack: send units at enemy Provinces, the defender meets them, and each battle is fought with Battle actions and then resolved -- the lower-Force army is destroyed, and the Province falls if the attacker beats the defenders by more than its Strength (the red number on the card). Then recruit from your Provinces with gold from your Holdings, and draw a Fate card.

Left-click a card for what it can do. Hover for the printed card. Right-click for the table commands -- bow, destroy, +Force, honor -- that carry out a card marked 手, which the engine plays by hand."""

const ABOUT := """Rokugan -- the Legend of the Five Rings CCG, 1995-2015.
Card data and images: the Oracle of the Void (oracleofthevoid.com), imported locally.
Legend of the Five Rings is (c) AEG / Fantasy Flight Games."""

# ============================================================ syncing
func _sync() -> void:
	if v.is_empty():
		return
	var m := me()
	var o := opp()
	# portraits and backgrounds, from the art
	if ui.has("my_face"):
		ui["my_face"].texture = db.art(int(m.get("face", -1)))
		ui["opp_face"].texture = db.art(int(o.get("face", -1)))
	if ui.has("bg"):
		var sh = db.art(int(m.get("stronghold", -1)))
		ui["bg"].texture = sh if sh else tex("dusk")
		var osh = db.art(int(o.get("stronghold", -1)))
		ui["scene"].texture = osh if osh else tex("dusk")
	if skin == "modern":
		_sync_modern(m, o)
	else:
		_sync_classic(m, o)
	_sync_log()
	_sync_buttons()
	_sync_cards(m, o)
	if v.get("phase", "") == "Over":
		var won := int(v["winner"]) == int(v["seat"])
		_message("%s\n\n%s" % ["Victory!" if won else ("Defeat." if int(v["winner"]) >= 0 else "A draw."), v.get("how", "")])

func _sync_classic(m: Dictionary, o: Dictionary) -> void:
	ui["track"].queue_redraw()
	ui["bulb"].queue_redraw()
	ui["icon"].texture = mon_tex(m["clan"])
	ui["honor_legend"].text = "You %s     Opp %s" % [_n(m["honor"]), _n(o["honor"])]
	for pair in [[ui["my_banner"], m], [ui["opp_banner"], o]]:
		var t := "%s CLAN" % str(pair[1]["clan"]).to_upper()
		pair[0].text = t
		# "SCORPION CLAN" in 19pt is wider than the bar
		pair[0].add_theme_font_size_override("font_size", 19 if t.length() <= 11 else 15)
	ui["my_mon"].texture = mon_tex(m["clan"])
	ui["opp_mon"].texture = mon_tex(o["clan"])
	ui["dyn_mon"].texture = mon_tex(m["clan"])
	ui["fate"].text = _n(m["hand"])
	ui["gold"].text = _n(m["gold"])
	var rows := [["Turn:", _n(v["turn"])], ["Clan:", "%s" % o["clan"]], ["Honor:", _n(o["honor"])],
			["Hand:", _n(o["hand"])], ["Provinces:", str(_alive(o))], ["Fate Deck:", _n(o["fate"])],
			["Dynasty:", _n(o["dynasty"])], ["Rings:", _n(o["rings"])]]
	for i in rows.size():
		ui["info"][i][0].text = rows[i][0]
		ui["info"][i][1].text = rows[i][1]
	ui["opp_title"].text = "%s Clan — opponent" % o["clan"]
	ui["fate_deck_lbl"].text = "Fate Deck\n%s" % _n(m["fate"])
	ui["dyn_deck_lbl"].text = "Dynasty Deck\n%s" % _n(m["dynasty"])
	var who := "Your Turn" if my_turn() else "%s's Turn" % o["clan"]
	ui["phase"].text = "%s - Phase: %s" % [who, phase_label()]
	var logs: Array = v.get("log", [])
	var last := ""
	for i in range(logs.size() - 1, -1, -1):
		var s := str(logs[i])
		if "battle" in s or "wins" in s or "holds" in s:
			last = "Battle Result: " + s.strip_edges()
			break
	ui["ticker"].text = last if last != "" else (str(logs[-1]).strip_edges() if logs.size() else "")

func _sync_modern(m: Dictionary, o: Dictionary) -> void:
	var have := int(m["rings"])
	for i in 5:
		ui["rings"][i][0].texture = tex("ring_%s%s" % [RINGS[i][0], "" if i < have else "_off"])
		ui["rings"][i][1].add_theme_color_override("font_color", Color(0.98, 0.9, 0.75) if i < have else Color(0.55, 0.5, 0.44))
	ui["fate_n"].text = _n(m["fate"])
	ui["fdisc_n"].text = _n(m["fdisc"])
	ui["dyn_n"].text = "%s left" % _n(m["dynasty"])
	ui["my_name"].text = "%s · %s gold" % [m["clan"], _n(m["gold"])]
	ui["my_honor"].text = _n(m["honor"])
	ui["opp_name"].text = str(o["clan"])
	ui["opp_honor"].text = _n(o["honor"])
	ui["opp_hand"].text = "Hand %s  ·  Fate deck %s  ·  Rings %s" % [_n(o["hand"]), _n(o["fate"]), _n(o["rings"])]
	var cur := phase_label()
	for i in PHASES.size():
		var b: Button = ui["phases"][i]
		var on: bool = PHASES[i] == cur
		b.add_theme_stylebox_override("normal", _sb_tex("gold_button_lit" if on else "gold_button", 8, 8))
		b.add_theme_color_override("font_color", Color(1, 0.95, 0.75) if on else Color(0.6, 0.53, 0.42))
	ui["turn"].text = ("Your turn" if my_turn() else "%s's turn" % o["clan"]) + ("  ·  you act" if my_say() else "")

func _alive(p: Dictionary) -> int:
	var n := 0
	for k in p["provinces"]:
		n += int(k["alive"])
	return n

const LOG_WORDS := {"destroyed": "#d84a3a", "wins": "#e8b84a", "holds": "#e8b84a", "attacks": "#d86a3a",
		"recruits": "#6aa86a", "(by hand)": "#9a7aa8"}

func _sync_log() -> void:
	var rich: RichTextLabel = ui["log"]
	var lines: PackedStringArray = []
	for s in v.get("log", []):
		var t := str(s).replace("[", "[lb]")
		for clan in CLAN_COLOR:
			var col: Color = CLAN_COLOR[clan]
			if skin == "classic":
				col = col.darkened(0.25)
			t = t.replace(clan, "[b][color=#%s]%s[/color][/b]" % [col.to_html(false), clan])
		for w in LOG_WORDS:
			t = t.replace(w, "[color=%s]%s[/color]" % [LOG_WORDS[w], w])
		lines.append(t)
	rich.text = "\n".join(lines)

func _sync_buttons() -> void:
	var box: Container = ui["acts"]
	for ch in box.get_children():
		ch.queue_free()
	if my_say():
		for i in global_actions():
			var b := Button.new()
			b.text = v["actions"][i]["l"]
			b.focus_mode = Control.FOCUS_NONE
			b.add_theme_font_size_override("font_size", 19 if skin == "classic" else 17)
			b.custom_minimum_size = Vector2(150 if skin == "classic" else 220, 46 if skin == "classic" else 40)
			b.pressed.connect(_act.bind(i))
			box.add_child(b)
	for k in 4:
		var b: Button = ui["prov_btn"][k]
		var p: Dictionary = me()["provinces"][k]
		var acts := actions_for_prov(k)
		var label := ""
		for i in acts:
			if int(v["actions"][i]["k"]) == A_BUY:
				label = "Recruit · " + str(v["actions"][i]["l"]).get_slice("(", 1).trim_suffix(")")
		if acts.size() and label == "":
			label = "Discard"
		if label == "":
			# not a button now, only the Province's name, small, as in the mockup
			if not int(p["alive"]):
				label = "Destroyed"
			elif p["card"] == null:
				label = "Province: face down" if skin == "classic" else "Face down"
			else:
				label = ("Province: %s" if skin == "classic" else "%s") % p["card"]["n"]
		b.text = label
		b.add_theme_font_size_override("font_size", 13 if label.length() <= 24 else 11)
		b.disabled = acts.is_empty()
		b.flat = skin == "classic" and acts.is_empty()

func _prov_pressed(k: int) -> void:
	var acts := actions_for_prov(k)
	if acts.size() == 1:
		_act(acts[0])
	elif acts.size() > 1:
		_menu_of(acts, -1)

# ----------------------------------------------------------- the cards
func _sync_cards(m: Dictionary, o: Dictionary) -> void:
	var placed: Array = []
	piles.clear()
	if skin == "classic":
		_cards_classic(placed, m, o)
	else:
		_cards_modern(placed, m, o)
	var want := {}
	for p in placed:
		want[p] = true
	for key in cards.keys():
		if not want.has(key):
			var n: Control = cards[key]
			cards.erase(key)
			var t := n.create_tween()
			t.tween_property(n, "modulate:a", 0.0, 0.25)
			t.tween_callback(n.queue_free)

func _units(p: Dictionary) -> Array:
	return (p["play"] as Array).filter(func(c): return c["t"] == "Personality" or c["t"] == "Ring")

func _holdings(p: Dictionary) -> Array:
	return (p["play"] as Array).filter(func(c): return c["t"] != "Personality" and c["t"] != "Ring")

# Classic, after the second mockup: one card size everywhere; the
# opponent's Provinces, Holdings and units along the top; my Provinces;
# the Home Zone between the decks; text cards in the hand.
func _cards_classic(placed: Array, m: Dictionary, o: Dictionary) -> void:
	var w: float = L["sq"]
	var h := w * cr()
	var row: Rect2 = L["opp_row"]
	var x := row.position.x
	var y := row.end.y - h
	for k in 4:
		var p: Dictionary = o["provinces"][k]
		if int(p["alive"]):
			placed.append(_place("o%d" % k, p["card"], "province", Rect2(x, y, w, h), "opp", false, int(p["str"])))
		x += w + 10
	x += 8
	var oh := _holdings(o)
	if oh.size():
		placed.append(_place_pile("pile_o", oh, Rect2(x + 6, y, w, h), false))
		x += w + 22
	_spread(placed, _units(o), Rect2(x, row.position.y, row.end.x - x, row.size.y), w, "opp", false)
	for k in 4:
		var p: Dictionary = m["provinces"][k]
		if int(p["alive"]):
			placed.append(_place("m%d" % k, p["card"], "province", L["my_prov"][k], "province", true, int(p["str"])))
	var home: Rect2 = L["home_row"]
	var hx := home.position.x
	var mh := _holdings(m)
	if mh.size():
		placed.append(_place_pile("pile_m", mh, Rect2(hx + 6, home.end.y - h - 4, w, h), true))
		hx += w + 24
	_spread(placed, _units(m), Rect2(hx, home.position.y, home.end.x - hx, home.size.y - 4), w, "play", true)
	_spread(placed, me().get("handcards", []), L["hand"], L["hand_w"], "hand", true, "text")

func _cards_modern(placed: Array, m: Dictionary, o: Dictionary) -> void:
	for k in 4:
		var p: Dictionary = o["provinces"][k]
		if int(p["alive"]):
			placed.append(_place("o%d" % k, p["card"], "province", L["opp_prov"][k], "opp", false, int(p["str"])))
	_spread(placed, _units(o), L["opp_units"], L["opp_prov"][0].size.x, "opp", false)
	ui["opp_holds"].text = _holdings_text(o)
	for k in 4:
		var p: Dictionary = m["provinces"][k]
		if int(p["alive"]):
			placed.append(_place("m%d" % k, p["card"], "province", L["my_prov"][k], "province", true, int(p["str"])))
	var mh := _holdings(m)
	var hr: Rect2 = L["home_holds"]
	var th := 60.0 * cr()
	var per := maxi(1, ceili(mh.size() / 5.0))
	var vstep := minf(th + 8, (hr.size.y - th - 2) / maxf(per - 1, 1))
	for j in mh.size():
		placed.append(_place(str(int(mh[j]["i"])), mh[j], "", Rect2(hr.position.x + (j % 5) * 66, hr.position.y + (j / 5) * vstep, 60, th), "play", true))
	_spread(placed, _units(m), L["home_units"], L["unit_w"], "play", true)
	_spread(placed, m.get("handcards", []), L["hand"], L["hand_w"], "hand", true)

# Lay cards out in a row inside r, overlapping if they must, bottom
# aligned; a unit's attachments peek out above the Personality, so his
# stats stay readable.
func _spread(placed: Array, list: Array, r: Rect2, w: float, zone: String, mine: bool, variant := "") -> void:
	var h := w * CARD.base_for(skin, variant).y / CARD.base_for(skin, variant).x
	var n := list.size()
	var step := minf(w + 12, (r.size.x - w) / maxf(n - 1, 1))
	var x := r.position.x + maxf(0, (r.size.x - (step * (n - 1) + w)) * 0.5)
	for c in list:
		var att: Array = c.get("att", [])
		var ah := minf(16.0, (r.size.y - h - 2) / maxf(att.size(), 1)) if att.size() else 0.0
		var y := r.end.y - h
		for j in att.size():
			var a: Dictionary = att[j]
			placed.append(_place(str(int(a["i"])), a, "", Rect2(x + 4, y - (att.size() - j) * ah, w - 8, (w - 8) * h / w),
					zone, mine, -1, variant))
		placed.append(_place(str(int(c["i"])), c, "", Rect2(x, y, w, h), zone, mine, -1, variant))
		x += step

func _place(key: String, c, back_kind: String, r: Rect2, zone: String, mine: bool, strength := -1, variant := "") -> String:
	var face: Dictionary = c if c is Dictionary else {}
	var k := key if face.is_empty() else str(int(face["i"]))
	var node: Control = cards.get(k)
	var fresh := node == null
	if fresh:
		node = CARD.new()
		node.setup(self)
		board.add_child(node)
		cards[k] = node
	node.pile = ""
	if node.variant != variant:
		node.set_variant(variant)
	var s: float = r.size.x / node.base.x
	var target_scale := Vector2(s, s)
	if fresh:
		node.scale = target_scale
		# drawn cards fly in from the deck they came from
		if zone == "hand" and L.has("fate_deck"):
			node.position = _at(L["fate_deck"].position, target_scale, node.base)
		elif zone == "province" and mine and L.has("dyn_deck"):
			node.position = _at(L["dyn_deck"].position, target_scale, node.base)
		else:
			node.position = _at(r.position, target_scale, node.base)
			node.modulate.a = 0.0
	node.zone = zone
	node.mine = mine
	node.show_card(face, back_kind if face.is_empty() else "")
	var tag := ""
	if not face.is_empty() and int(face.get("at", -1)) >= 0:
		tag = "⚔ Province %d" % (int(face["at"]) + 1)
	var badge := strength if (zone == "province" or zone == "opp") else -1
	node.set_marks(not face.is_empty() and my_say() and actions_for(int(face["i"])).size() > 0, badge, tag)
	board.move_child(node, -1)
	var t := node.create_tween().set_parallel(true)
	t.tween_property(node, "position", _at(r.position, target_scale, node.base), 0.35).set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	t.tween_property(node, "scale", target_scale, 0.35)
	t.tween_property(node, "modulate:a", 1.0, 0.3)
	return k

# A player's Holdings and Stronghold as one pile: the top card is one
# that can still be bowed, the count says how many and what they make.
func _place_pile(key: String, holds: Array, r: Rect2, mine: bool) -> String:
	var top: Dictionary = holds[0]
	for c in holds:
		if int(c.get("b", 0)) == 0 and c["t"] != "Stronghold":
			top = c
			break
	var ready := 0
	var lit := false
	for c in holds:
		if int(c.get("b", 0)) == 0:
			ready += int(db.card(int(c["o"])).get("gold", "0"))
		if mine and actions_for(int(c["i"])).size():
			lit = true
	var node: Control = cards.get(key)
	if node == null:
		node = CARD.new()
		node.setup(self)
		board.add_child(node)
		cards[key] = node
		node.modulate.a = 0.0
	var s: float = r.size.x / node.base.x
	node.pile = "%d Holding%s · %d gold ready" % [holds.size(), "" if holds.size() == 1 else "s", ready]
	node.zone = "pile"
	node.mine = mine
	node.show_card(top, "")
	node.inst = -2
	node.set_marks(lit and my_say(), -1, "")
	board.move_child(node, -1)
	piles[key] = holds
	node.set_meta("pile", key)
	var t := node.create_tween().set_parallel(true)
	t.tween_property(node, "position", _at(r.position, Vector2(s, s), node.base), 0.35)
	t.tween_property(node, "scale", Vector2(s, s), 0.35)
	t.tween_property(node, "modulate:a", 1.0, 0.3)
	return key

func _holdings_text(p: Dictionary) -> String:
	var ready := 0
	var lines: PackedStringArray = []
	for c in _holdings(p):
		var row: Dictionary = db.card(int(c["o"]))
		var bowed := int(c.get("b", 0)) == 1
		if not bowed:
			ready += int(row.get("gold", "0"))
		var s := "%s (%s)" % [str(c["n"]).replace("[", "[lb]"), "SH" if c["t"] == "Stronghold" else row.get("gold", "0") + "g"]
		lines.append(("[color=#8a7a66]⤵ %s[/color]" % s) if bowed else s)
	return "[color=#2a1a0a][b]Holdings · %d gold ready[/b]\n%s[/color]" % [ready, "\n".join(lines)]

func pile_tooltip(node) -> String:
	var holds: Array = piles.get(node.get_meta("pile", ""), [])
	var lines: PackedStringArray = []
	for c in holds:
		var row: Dictionary = db.card(int(c["o"]))
		lines.append("%s%s  (%s gold)" % ["⤵ " if int(c.get("b", 0)) else "", c["n"], row.get("gold", "0")])
	return "\n".join(lines)

# A Card scales and turns about its centre, so the top-left a scaled card
# is drawn at is not its position: shift it back by what the scale took.
func _at(p: Vector2, s: Vector2, base: Vector2) -> Vector2:
	return p - base * 0.5 * (Vector2.ONE - s)

# ------------------------------------------------------------ clicking
func hovered(_node, _on) -> void:
	pass

func card_clicked(node, button: int) -> void:
	if v.is_empty():
		return
	if node.pile != "":
		# a pile: each Holding in it, with what it can do
		popup.clear()
		popup_cmds.clear()
		for c in piles.get(node.get_meta("pile", ""), []):
			var acts := actions_for(int(c["i"]))
			for i in acts:
				_pop(v["actions"][i]["l"], "act %d" % i)
			if node.mine and button == MOUSE_BUTTON_RIGHT:
				_pop("Bow %s   (by hand)" % c["n"], "manual bow %d" % int(c["i"]))
			_pop("View %s%s" % [c["n"], "  (bowed)" if int(c.get("b", 0)) else ""], "zoom %d" % int(c["o"]))
		_show_popup()
		return
	if button == MOUSE_BUTTON_LEFT:
		var acts := actions_for(node.inst) if node.inst >= 0 else []
		if acts.is_empty():
			if node.oid >= 0:
				_zoom(node.oid)
			return
		_menu_of(acts, node.oid)
	elif button == MOUSE_BUTTON_RIGHT:
		popup.clear()
		popup_cmds.clear()
		if node.mine and node.zone == "play":
			for mm in [["bow", "Bow"], ["straighten", "Straighten"], ["destroy", "Destroy"],
					["home", "Send home"], ["force+", "+1 Force"], ["force-", "-1 Force"]]:
				_pop(mm[1] + "   (by hand)", "manual %s %d" % [mm[0], node.inst])
		elif node.mine and node.zone == "hand":
			_pop("Discard   (by hand)", "manual discard %d" % node.inst)
		if node.mine:
			popup.add_separator()
			for mm in [["honor+", "Gain 1 honor"], ["honor-", "Lose 1 honor"], ["draw", "Draw a card"]]:
				_pop(mm[1] + "   (by hand)", "manual %s -1" % mm[0])
		if node.oid >= 0:
			popup.add_separator()
			_pop("View card", "zoom %d" % node.oid)
		_show_popup()

func _pop(label: String, cmd: String) -> void:
	popup.add_item(label, popup_cmds.size())
	popup_cmds.append(cmd)

func _menu_of(acts: Array, oid: int) -> void:
	popup.clear()
	popup_cmds.clear()
	for i in acts:
		_pop(v["actions"][i]["l"], "act %d" % i)
	if oid >= 0:
		popup.add_separator()
		_pop("View card", "zoom %d" % oid)
	_show_popup()

func _show_popup() -> void:
	if popup_cmds.is_empty():
		return
	popup.reset_size()
	popup.position = DisplayServer.mouse_get_position()
	popup.popup()

func _on_popup(id: int) -> void:
	var cmd: String = popup_cmds[id]
	if cmd.begins_with("zoom "):
		_zoom(int(cmd.substr(5)))
	else:
		_send(cmd)

func _zoom(oid: int) -> void:
	ui["zoom_tex"].texture = db.scan(oid)
	var row: Dictionary = db.card(oid)
	ui["zoom_text"].text = "%s\n%s\n\n%s" % [row.get("name", ""), row.get("type", ""), db.text(oid)]
	zoom.visible = true
