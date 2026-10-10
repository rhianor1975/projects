# One card on the table.
#
# Laid out once at a fixed size and scaled to whatever slot it sits in, so
# every size has the same layout and a card moving between zones only has
# to tween its position and scale.
#
# Classic, after the second Classic mockup: one gilded frame for every
# card, a name bar between two clubs, the art, and the stats as a list --
# Force, Chi, Cost, Personal Honor, Honor Requirement.  Holdings are their
# art.  In the hand, a card is its text: name, type line, rules.  Modern
# cards are portrait, after the Modern mockup.
#
# The frame is a 9-patch over the art and the parchment; the stats are
# drawn; an overlay child draws what must sit on top of everything (the
# glow when the card has something to do, the Province's Strength, the
# 手 mark on a card played by hand, the battle tag, a pile's count).
extends Control

const SQUARE := Vector2(150, 168)
const TEXT := Vector2(176, 132)
const MODERN := Vector2(150, 210)

static func base_for(skin: String, variant := "") -> Vector2:
	if skin == "modern":
		return MODERN
	return TEXT if variant == "text" else SQUARE

var base := SQUARE
var variant := ""
var main
var inst := -1
var oid := -1
var c := {}              # the card as the View sent it; empty for a back
var back := ""           # "fate", "dynasty", "province" when face down
var zone := ""
var mine := false
var lit := false
var strength := -1       # a Province's Strength, shown as a badge
var tag := ""
var pile := ""           # "7 Holdings · 9 gold ready" on a pile
var bowed := false

var _frame: NinePatchRect
var _art: TextureRect
var _over: Control
var _tween: Tween

func setup(m) -> void:
	main = m
	mouse_filter = Control.MOUSE_FILTER_STOP
	tooltip_text = " "
	_art = TextureRect.new()
	_art.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_art.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_art)
	_frame = NinePatchRect.new()
	_frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_frame)
	_over = Control.new()
	_over.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_over.draw.connect(_draw_over)
	add_child(_over)
	mouse_entered.connect(_hover.bind(true))
	mouse_exited.connect(_hover.bind(false))
	set_variant("")

func set_variant(v: String) -> void:
	variant = v
	base = base_for(main.skin, v)
	size = base
	pivot_offset = base * 0.5
	_frame.size = base
	_over.size = base

# A face-up card from the View, or a back.
func show_card(card: Dictionary, back_kind := "") -> void:
	c = card
	back = back_kind if card.is_empty() else ""
	inst = int(card.get("i", -1))
	oid = int(card.get("o", -1))
	_style()
	set_bowed(int(card.get("b", 0)) == 1 and pile == "")
	queue_redraw()
	_over.queue_redraw()

func set_marks(is_lit: bool, str_badge: int, battle_tag: String) -> void:
	lit = is_lit
	strength = str_badge
	tag = battle_tag
	_over.queue_redraw()

func set_bowed(b: bool) -> void:
	if b == bowed:
		return
	bowed = b
	if _tween:
		_tween.kill()
	_tween = create_tween()
	_tween.tween_property(self, "rotation", PI * 0.5 if b else 0.0, 0.35).set_trans(Tween.TRANS_BACK)

func modern() -> bool:
	return main.skin == "modern"

func ctype() -> String:
	return c.get("t", main.db.card(oid).get("type", ""))

func _art_only() -> bool:
	return ctype() in ["Holding", "Stronghold", "Region", "Event", "Ring"]

func _style() -> void:
	_frame.texture = main.tex("frame_modern" if modern() else "frame_classic")
	var m := 10 if modern() else 14
	_frame.patch_margin_left = m
	_frame.patch_margin_right = m
	_frame.patch_margin_top = m
	_frame.patch_margin_bottom = m
	_frame.draw_center = false
	_frame.visible = back == ""
	_art.visible = true
	if back != "":
		var t := "back_modern"
		if not modern():
			t = "back_lattice" if back == "province" else "back_" + back
		_art.texture = main.tex(t)
		_art.position = Vector2.ZERO
		_art.size = base
		_art.stretch_mode = TextureRect.STRETCH_SCALE
		return
	_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_art.texture = main.db.art(oid)
	if modern():
		_art.position = Vector2(8, 8)
		_art.size = Vector2(134, 124)
	elif variant == "text":
		_art.visible = false
	elif _art_only():
		_art.position = Vector2(11, 31)
		_art.size = Vector2(base.x - 22, base.y - 42)
	else:
		_art.position = Vector2(11, 31)
		_art.size = Vector2(base.x - 22, 64)

func _hover(on: bool) -> void:
	if zone == "hand":
		z_index = 20 if on else 0
		var t := create_tween()
		t.tween_property(self, "position:y", position.y + (-18 if on else 18), 0.12)
	main.hovered(self, on)

func _gui_input(e: InputEvent) -> void:
	if e is InputEventMouseButton and e.pressed:
		main.card_clicked(self, e.button_index)
		accept_event()

# The printed card, as the hover tooltip; a pile lists what is in it.
func _make_custom_tooltip(_t: String) -> Object:
	if pile != "":
		var pl := Label.new()
		pl.text = main.pile_tooltip(self)
		return pl
	if oid < 0:
		return null
	var box := VBoxContainer.new()
	var scan = main.db.scan(oid)
	if scan:
		var tr := TextureRect.new()
		tr.texture = scan
		tr.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
		tr.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT
		tr.custom_minimum_size = Vector2(286, 400)
		box.add_child(tr)
	var row: Dictionary = main.db.card(oid)
	var l := Label.new()
	l.text = "%s\n%s" % [row.get("name", ""), main.db.text(oid)]
	l.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	l.custom_minimum_size = Vector2(286, 0)
	if int(c.get("auto", 1)) == 0:
		l.text += "\n\n手  The engine does not run this card: play it, then carry it out with the right-click table commands."
	box.add_child(l)
	return box

# ------------------------------------------------------------- drawing
func _n(x) -> String:
	return str(int(float(x)))

func _row() -> Dictionary:
	return main.db.card(oid)

func _draw() -> void:
	if pile != "":
		# the cards beneath, peeking out
		for k in [2, 1]:
			var off := Vector2(5 * k, -5 * k)
			draw_rect(Rect2(off, base), Color(0.30, 0.20, 0.10))
			draw_rect(Rect2(off, base), Color(0.75, 0.58, 0.28), false, 2)
	if back != "":
		return
	if modern():
		_draw_modern()
	else:
		_draw_classic()

func _txt(p: Vector2, s: String, sz: int, col: Color, f: Font, w := -1.0, align := HORIZONTAL_ALIGNMENT_CENTER) -> void:
	draw_string(f, p, s, align, w, sz, col)

func _fit(s: String, f: Font, sz: int, w: float) -> int:
	while sz > 6 and f.get_string_size(s, HORIZONTAL_ALIGNMENT_LEFT, -1, sz).x > w:
		sz -= 1
	return sz

func _type_line(row: Dictionary) -> String:
	var kw: String = row.get("keywords", "")
	var t := ctype()
	return t + (" • " + kw.replace(",", " • ") if kw != "" else "")

func _draw_classic() -> void:
	var row := _row()
	var t := ctype()
	var ink := Color(0.17, 0.10, 0.04)
	var soft := Color(0.42, 0.26, 0.10)
	draw_texture_rect(main.tex("parchment"), Rect2(4, 4, base.x - 8, base.y - 8), true)
	# the name bar, between two clubs
	var title := Rect2(10, 9, base.x - 20, 20)
	draw_rect(title, Color(0.91, 0.84, 0.66))
	draw_rect(title, Color(0.55, 0.40, 0.20), false, 1)
	_txt(Vector2(title.position.x + 3, title.position.y + 15), "♣", 12, ink, main.f_ui, 14, HORIZONTAL_ALIGNMENT_LEFT)
	_txt(Vector2(title.end.x - 15, title.position.y + 15), "♣", 12, ink, main.f_ui, 14, HORIZONTAL_ALIGNMENT_LEFT)
	var name: String = c.get("n", "")
	var tw := title.size.x - 30
	_txt(Vector2(title.position.x + 15, title.position.y + 15), name, _fit(name, main.f_serif, 13, tw), ink, main.f_serif, tw)
	if variant == "text":
		var y := 44.0
		_txt(Vector2(12, y), _type_line(row), _fit(_type_line(row), main.f_bold, 11, base.x - 24), soft, main.f_bold,
				base.x - 24, HORIZONTAL_ALIGNMENT_LEFT)
		# as many lines as fit above the footer, so the text never runs into it
		var lines := int((base.y - 18 - (y + 16)) / 11.5) + 1
		draw_multiline_string(main.f_ui, Vector2(12, y + 16), main.db.text(oid), HORIZONTAL_ALIGNMENT_LEFT,
				base.x - 24, 10, lines, ink)
		var foot := "Focus %s" % _n(row.get("focus", "0"))
		if row.get("cost", "0") != "0":
			foot += "   ·   %s gold" % _n(row.get("cost", "0"))
		_txt(Vector2(12, base.y - 12), foot, 10, soft, main.f_bold, base.x - 24, HORIZONTAL_ALIGNMENT_RIGHT)
		return
	if _art_only():
		return
	draw_rect(Rect2(11, 31, base.x - 22, 64), Color(0.45, 0.30, 0.15), false, 1)
	# the stats as a list
	var rows: Array = []
	if t == "Personality":
		rows = [["Force", _n(c.get("f", row.get("force", "0")))], ["Chi", _n(c.get("c", row.get("chi", "0")))],
				["Cost", _n(row.get("cost", "0"))], ["Personal Honor", _n(row.get("ph", "0"))],
				["Honor Req.", str(row.get("hreq", "-"))]]
	elif t == "Follower" or t == "Item":
		rows = [["Force", "%+d" % int(row.get("force", "0"))], ["Chi", ("%+d" % int(row.get("chi", "0"))) if int(row.get("chi", "0")) else "–"],
				["Cost", _n(row.get("cost", "0"))], ["Personal Honor", "–"]]
	else:
		rows = [["Focus", _n(row.get("focus", "0"))], ["Cost", _n(row.get("cost", "0"))]]
	var y0 := 98.0
	var rh := minf(13.0, (base.y - 10 - y0) / rows.size())
	for i in rows.size():
		var y := y0 + (i + 1) * rh - 2
		_txt(Vector2(14, y), rows[i][0], 11, ink, main.f_ui, base.x - 28, HORIZONTAL_ALIGNMENT_LEFT)
		_txt(Vector2(14, y), rows[i][1], 12, ink, main.f_bold, base.x - 28, HORIZONTAL_ALIGNMENT_RIGHT)

func _draw_modern() -> void:
	var row := _row()
	var t := ctype()
	var ink := Color(0.16, 0.10, 0.05)
	draw_rect(Rect2(4, 4, base.x - 8, base.y - 8), Color(0.08, 0.06, 0.05))
	var box := Rect2(8, 134, base.x - 16, base.y - 142)
	draw_texture_rect(main.tex("parchment"), box, true)
	var head := t
	match t:
		"Personality": head = "Personality  ·  HR %s  ·  Cost %s" % [row.get("hreq", "0"), _n(row.get("cost", "0"))]
		"Holding", "Stronghold": head = "%s · %s gold" % [t, _n(row.get("gold", "0"))]
		"Follower", "Item": head = "%s: %+dF" % [t, int(row.get("force", "0"))]
	_txt(Vector2(12, box.position.y + 13), head, 9, Color(0.45, 0.20, 0.06), main.f_bold, box.size.x - 8, HORIZONTAL_ALIGNMENT_LEFT)
	draw_multiline_string(main.f_ui, Vector2(12, box.position.y + 25), main.db.text(oid), HORIZONTAL_ALIGNMENT_LEFT,
			box.size.x - 28, 8, 5, ink)
	draw_texture_rect(main.mon_tex(row.get("clan", "")), Rect2(box.end.x - 22, box.end.y - 22, 20, 20), false)

func _draw_over() -> void:
	if back == "":
		var row := _row()
		var t := ctype()
		if modern():
			_over.draw_rect(Rect2(8, 8, 134, 20), Color(0.95, 0.90, 0.78, 0.93))
			var name: String = c.get("n", "")
			_over.draw_string(main.f_serif, Vector2(13, 23), name, HORIZONTAL_ALIGNMENT_LEFT, 124,
					_fit(name, main.f_serif, 13, 124), Color(0.15, 0.1, 0.05))
			if t == "Personality":
				var vals := [_n(c.get("f", row.get("force", "0"))), _n(c.get("c", row.get("chi", "0"))), _n(row.get("ph", "0"))]
				for i in 3:
					var b := Rect2(116, 34 + i * 29, 24, 26)
					_over.draw_rect(b, Color(0.95, 0.90, 0.78))
					_over.draw_rect(b, Color(0.3, 0.2, 0.1), false, 1)
					_over.draw_string(main.f_bold, Vector2(b.position.x, b.end.y - 6), vals[i],
							HORIZONTAL_ALIGNMENT_CENTER, b.size.x, 16, Color(0.15, 0.1, 0.05))
		elif _art_only() and variant != "text":
			# the clan's mon in the corner, and the gold a Holding makes
			if t == "Holding" or t == "Stronghold":
				_over.draw_texture_rect(main.tex("coin_gold"), Rect2(base.x - 36, base.y - 36, 28, 28), false)
				_over.draw_string(main.f_bold, Vector2(base.x - 36, base.y - 16), _n(row.get("gold", "0")),
						HORIZONTAL_ALIGNMENT_CENTER, 28, 13, Color(0.25, 0.12, 0.02))
		if int(c.get("auto", 1)) == 0 and t != "Personality" and t != "Holding":
			_over.draw_texture_rect(main.tex("hand"), Rect2(base.x - 34, 32, 22, 22), false)
	if pile != "":
		_over.draw_rect(Rect2(6, base.y - 30, base.x - 12, 24), Color(0.12, 0.07, 0.03, 0.88))
		_over.draw_string(main.f_bold, Vector2(6, base.y - 13), pile, HORIZONTAL_ALIGNMENT_CENTER, base.x - 12,
				_fit(pile, main.f_bold, 12, base.x - 16), Color(1, 0.92, 0.7))
	if strength >= 0:
		var p := Vector2(6, 6)
		_over.draw_circle(p, 16, Color(0.35, 0.05, 0.03))
		_over.draw_circle(p, 13, Color(0.75, 0.16, 0.08))
		_over.draw_string(main.f_bold, Vector2(p.x - 16, p.y + 6), str(strength), HORIZONTAL_ALIGNMENT_CENTER, 32, 17, Color.WHITE)
	if tag != "":
		_over.draw_rect(Rect2(0, base.y - 24, base.x, 24), Color(0.55, 0.04, 0.04, 0.92))
		_over.draw_string(main.f_bold, Vector2(0, base.y - 7), tag, HORIZONTAL_ALIGNMENT_CENTER, base.x, 14, Color.WHITE)
	if lit:
		_over.draw_rect(Rect2(-3, -3, base.x + 6, base.y + 6), Color(1.0, 0.82, 0.25, 0.95), false, 4)
