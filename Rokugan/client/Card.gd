# One card on the table.
#
# Laid out once at a fixed size and scaled to whatever slot it sits in: a
# Province, the Home Zone, the hand, the opponent's band.  So every size
# has the same layout, and a card moving between zones only has to tween
# its position and scale.
#
# The size depends on the skin.  Classic cards are nearly square, as in
# the first mockup -- a name banner, the art, and a stat block of
# Force | Chi | Cost over the clan mon | Honor Requirement | Personal
# Honor.  Modern cards are portrait, as in the second.
#
# The frame is a 9-patch over the art and the parchment; the stats are
# drawn; an overlay child draws what must sit on top of everything (the
# glow when the card has something to do, the Province's Strength, the
# 手 mark on a card played by hand, the battle tag).
extends Control

const CLASSIC := Vector2(170, 196)
const MODERN := Vector2(150, 210)

static func base_for(skin: String) -> Vector2:
	return MODERN if skin == "modern" else CLASSIC

var base := CLASSIC
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
var bowed := false

var _frame: NinePatchRect
var _art: TextureRect
var _over: Control
var _tween: Tween

func setup(m) -> void:
	main = m
	base = base_for(m.skin)
	size = base
	pivot_offset = base * 0.5
	mouse_filter = Control.MOUSE_FILTER_STOP
	tooltip_text = " "
	_art = TextureRect.new()
	_art.expand_mode = TextureRect.EXPAND_IGNORE_SIZE
	_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_art.mouse_filter = Control.MOUSE_FILTER_IGNORE
	add_child(_art)
	_frame = NinePatchRect.new()
	_frame.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_frame.size = base
	add_child(_frame)
	_over = Control.new()
	_over.size = base
	_over.mouse_filter = Control.MOUSE_FILTER_IGNORE
	_over.draw.connect(_draw_over)
	add_child(_over)
	mouse_entered.connect(_hover.bind(true))
	mouse_exited.connect(_hover.bind(false))

# A face-up card from the View, or a back.
func show_card(card: Dictionary, back_kind := "") -> void:
	c = card
	back = back_kind if card.is_empty() else ""
	inst = int(card.get("i", -1))
	oid = int(card.get("o", -1))
	_style()
	set_bowed(int(card.get("b", 0)) == 1)
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

func _style() -> void:
	_frame.texture = main.tex("frame_modern" if modern() else "frame_classic")
	var m := 10 if modern() else 14
	_frame.patch_margin_left = m
	_frame.patch_margin_right = m
	_frame.patch_margin_top = m
	_frame.patch_margin_bottom = m
	_frame.draw_center = false
	_frame.visible = back == ""
	if back != "":
		# the backs are portrait; a square card shows the middle of one
		_art.texture = main.tex("back_modern" if modern() else "back_" + back)
		_art.position = Vector2.ZERO
		_art.size = base
		_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
		return
	_art.stretch_mode = TextureRect.STRETCH_KEEP_ASPECT_COVERED
	_art.texture = main.db.art(oid)
	if modern():
		_art.position = Vector2(8, 8)
		_art.size = Vector2(134, 124)
	else:
		_art.position = Vector2(12, 34)
		_art.size = Vector2(146, 80)

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

# The printed card, as the hover tooltip.
func _make_custom_tooltip(_t: String) -> Object:
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

func _cell(r: Rect2, s: String, sz: int, ink: Color, f: Font = null) -> void:
	var font: Font = f if f else main.f_bold
	var z := _fit(s, font, sz, r.size.x - 4)
	_txt(Vector2(r.position.x, r.position.y + r.size.y * 0.5 + z * 0.36), s, z, ink, font, r.size.x)

# The first mockup's card, square.
func _draw_classic() -> void:
	var row := _row()
	var t: String = c.get("t", row.get("type", ""))
	var ink := Color(0.17, 0.10, 0.04)
	var line := Color(0.55, 0.40, 0.20)
	var paper := Color(0.97, 0.93, 0.82)
	draw_texture_rect(main.tex("parchment"), Rect2(4, 4, base.x - 8, base.y - 8), true)
	# the name banner, with the cost coin on the right where there is one
	var title := Rect2(12, 10, base.x - 24, 22)
	draw_rect(title, Color(0.90, 0.82, 0.62))
	draw_rect(title, line, false, 1)
	var coin: bool = t in ["Holding", "Follower", "Item", "Spell", "Strategy"] and row.get("cost", "0") != "0"
	var tw := title.size.x - (26 if coin else 6)
	var name: String = c.get("n", "")
	_txt(Vector2(title.position.x + 3, title.position.y + 16), name, _fit(name, main.f_serif, 14, tw), ink, main.f_serif, tw)
	if coin:
		var cc := Vector2(title.end.x - 11, title.position.y + 11)
		draw_circle(cc, 11, Color(0.62, 0.44, 0.12))
		draw_circle(cc, 9, Color(0.96, 0.80, 0.34))
		_txt(Vector2(cc.x - 11, cc.y + 5), _n(row.get("cost", "0")), 13, ink, main.f_bold, 22)
	draw_rect(Rect2(12, 34, base.x - 24, 80), line, false, 1)
	var box := Rect2(12, 117, base.x - 24, base.y - 117 - 9)
	if t == "Personality":
		# Force | Chi | Cost, then mon | Hon. Req | Personal Honor
		var cw := box.size.x / 3.0
		var hh := 20.0
		for i in 3:
			var col := Rect2(box.position.x + cw * i + 1, box.position.y, cw - 2, box.size.y)
			draw_rect(col, paper)
			draw_rect(col, line, false, 1)
			draw_line(Vector2(col.position.x, col.position.y + hh), Vector2(col.end.x, col.position.y + hh), line, 1)
		var low := box.size.y - hh
		_cell(Rect2(box.position.x, box.position.y, cw, hh), "Force " + _n(c.get("f", row.get("force", "0"))), 12, ink)
		_cell(Rect2(box.position.x + cw, box.position.y, cw, hh), "Chi", 12, ink)
		_cell(Rect2(box.position.x + cw * 2, box.position.y, cw, hh), "Cost " + _n(row.get("cost", "0")), 12, ink)
		var ms := minf(cw - 10, low - 10)
		draw_texture_rect(main.mon_tex(row.get("clan", "")),
				Rect2(box.position.x + (cw - ms) * 0.5, box.position.y + hh + (low - ms) * 0.5, ms, ms), false)
		# the middle box holds two numbers, as in the mockup: Chi, and
		# under a line the Honor Requirement, named
		var mx := box.position.x + cw
		var y0 := box.position.y + hh
		_cell(Rect2(mx, y0, cw, low * 0.5), _n(c.get("c", row.get("chi", "0"))), 18, ink, main.f_serif)
		draw_line(Vector2(mx + 4, y0 + low * 0.5), Vector2(mx + cw - 4, y0 + low * 0.5), line, 1)
		_txt(Vector2(mx, y0 + low * 0.5 + 9), "Hon. Req", 7, Color(0.45, 0.28, 0.1), main.f_ui, cw)
		_cell(Rect2(mx, y0 + low * 0.5 + 7, cw, low * 0.5 - 7), str(row.get("hreq", "0")), 15, ink, main.f_serif)
		var px := box.position.x + cw * 2
		_txt(Vector2(px, y0 + 12), "Personal", 8, Color(0.45, 0.28, 0.1), main.f_ui, cw)
		_txt(Vector2(px, y0 + 21), "Honor", 8, Color(0.45, 0.28, 0.1), main.f_ui, cw)
		_cell(Rect2(px, y0 + 22, cw, low - 22), _n(row.get("ph", "0")), 20, ink, main.f_serif)
		return
	draw_rect(box, paper)
	draw_rect(box, line, false, 1)
	var head := t
	match t:
		"Holding", "Stronghold": head = "%s · %s gold" % [t, _n(row.get("gold", "0"))]
		"Follower", "Item": head = "%s · %+dF%s" % [t, int(row.get("force", "0")),
				(" %+dC" % int(row.get("chi", "0"))) if int(row.get("chi", "0")) else ""]
		"Strategy", "Spell": head = "%s · focus %s" % [t, _n(row.get("focus", "0"))]
	_txt(Vector2(box.position.x + 4, box.position.y + 12), head, 10, Color(0.45, 0.22, 0.06), main.f_bold,
			box.size.x - 8, HORIZONTAL_ALIGNMENT_LEFT)
	var gold := t == "Holding" or t == "Stronghold"
	draw_multiline_string(main.f_ui, Vector2(box.position.x + 4, box.position.y + 24), main.db.text(oid),
			HORIZONTAL_ALIGNMENT_LEFT, box.size.x - (34 if gold else 8), 9, 5, ink)
	if gold:
		draw_texture_rect(main.tex("coin_gold"), Rect2(box.end.x - 28, box.end.y - 28, 26, 26), false)

func _draw_modern() -> void:
	var row := _row()
	var t: String = c.get("t", row.get("type", ""))
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
		var t: String = c.get("t", row.get("type", ""))
		if modern():
			# the name plate over the art, the stat column down its edge
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
		if int(c.get("auto", 1)) == 0 and t != "Personality" and t != "Holding":
			_over.draw_texture_rect(main.tex("hand"), Rect2(14, 36 if not modern() else 32, 22, 22), false)
	if strength >= 0:
		# top left, half off the card, so it covers nothing printed
		var p := Vector2(6, 6)
		_over.draw_circle(p, 18, Color(0.35, 0.05, 0.03))
		_over.draw_circle(p, 15, Color(0.75, 0.16, 0.08))
		_over.draw_string(main.f_bold, Vector2(p.x - 18, p.y + 7), str(strength), HORIZONTAL_ALIGNMENT_CENTER, 36, 20, Color.WHITE)
	if tag != "":
		_over.draw_rect(Rect2(0, base.y - 26, base.x, 26), Color(0.55, 0.04, 0.04, 0.92))
		_over.draw_string(main.f_bold, Vector2(0, base.y - 8), tag, HORIZONTAL_ALIGNMENT_CENTER, base.x, 15, Color.WHITE)
	if lit:
		_over.draw_rect(Rect2(-3, -3, base.x + 6, base.y + 6), Color(1.0, 0.82, 0.25, 0.95), false, 4)
