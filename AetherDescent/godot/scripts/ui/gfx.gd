class_name Gfx
## Drawing helpers shared by every screen: the blue SNES window, the bitmap
## font, bars, and lookups into the generated sprite sheets.

const WHITE := Color(0.97, 0.97, 0.97)
const GREY := Color8(168, 176, 200)
const DIM := Color8(110, 116, 150)
const GOLD := Color8(248, 216, 96)
const CYAN := Color8(128, 224, 248)
const GREEN := Color8(136, 232, 120)
const RED := Color8(255, 120, 110)
const INK := Color8(24, 16, 40)
const SHADOW := Color8(16, 16, 48)
const WIN_TOP := Color8(64, 80, 200)
const WIN_BOT := Color8(16, 16, 96)

static var tiles: Texture2D = preload("res://assets/tiles.png")
static var heroes: Texture2D = preload("res://assets/heroes.png")
static var portraits: Texture2D = preload("res://assets/portraits.png")
static var monsters: Texture2D = preload("res://assets/monsters.png")
static var warden: Texture2D = preload("res://assets/warden.png")
static var props: Texture2D = preload("res://assets/props.png")
static var font: Texture2D = preload("res://assets/font.png")
static var torch: Texture2D = preload("res://assets/torch.png")
static var _glyph := {}


static func _glyphs() -> Dictionary:
	if _glyph.is_empty():
		for i in ArtLayout.FONT_CHARS.size():
			_glyph[ArtLayout.FONT_CHARS[i]] = i
	return _glyph


# ---- text ------------------------------------------------------------------------
static func text_width(s: String, scale := 1) -> int:
	var g := _glyphs()
	var w := 0
	for ch in s:
		var i: int = g.get(ch, g["?"])
		w += ArtLayout.FONT_WIDTHS[i] + 1
	return maxi(0, w - 1) * scale


## Draw text with a drop shadow (or an outline). Returns the width drawn.
static func text(ci: CanvasItem, pos: Vector2, s: String, col := WHITE, scale := 1,
		shadow := true, outline := Color(0, 0, 0, 0)) -> int:
	var g := _glyphs()
	var cell: Vector2i = ArtLayout.FONT_CELL
	var x := 0
	for ch in s:
		var i: int = g.get(ch, g["?"])
		var src := Rect2(Vector2((i % 16) * cell.x, (i / 16) * cell.y), Vector2(cell))
		var dst_size := Vector2(cell) * scale
		var p := pos + Vector2(x * scale, 0)
		if outline.a > 0:
			for o in [Vector2(-1, 0), Vector2(1, 0), Vector2(0, -1), Vector2(0, 1), Vector2(-1, -1), Vector2(1, 1), Vector2(-1, 1), Vector2(1, -1)]:
				ci.draw_texture_rect_region(font, Rect2(p + o * scale, dst_size), src, outline)
		elif shadow:
			ci.draw_texture_rect_region(font, Rect2(p + Vector2(scale, scale), dst_size), src, SHADOW)
		ci.draw_texture_rect_region(font, Rect2(p, dst_size), src, col)
		x += ArtLayout.FONT_WIDTHS[i] + 1
	return x * scale


static func text_right(ci: CanvasItem, right: Vector2, s: String, col := WHITE, scale := 1) -> void:
	text(ci, right - Vector2(text_width(s, scale), 0), s, col, scale)


static func text_center(ci: CanvasItem, center_x: float, y: float, s: String, col := WHITE, scale := 1,
		outline := Color(0, 0, 0, 0)) -> void:
	text(ci, Vector2(roundi(center_x - text_width(s, scale) / 2.0), y), s, col, scale, true, outline)


## Word-wrap to a pixel width.
static func wrap(s: String, width: int) -> Array:
	var lines: Array = []
	var cur := ""
	for word in s.split(" "):
		var trial := word if cur == "" else cur + " " + word
		if text_width(trial) > width and cur != "":
			lines.append(cur)
			cur = word
		else:
			cur = trial
	if cur != "":
		lines.append(cur)
	return lines


# ---- the window ------------------------------------------------------------------
static func window(ci: CanvasItem, r: Rect2, top := WIN_TOP, bottom := WIN_BOT, alpha := 1.0) -> void:
	var p := r.position
	var s := r.size
	var t := Color(top, alpha)
	var b := Color(bottom, alpha)
	ci.draw_polygon(PackedVector2Array([p, p + Vector2(s.x, 0), p + s, p + Vector2(0, s.y)]),
		PackedColorArray([t, t, b, b]))
	var dark := Color8(40, 40, 72)
	var light := Color8(232, 232, 248)
	var mid := Color8(168, 168, 200)
	# bevelled silver frame, three pixels deep
	ci.draw_rect(Rect2(p, Vector2(s.x, 1)), dark)
	ci.draw_rect(Rect2(p + Vector2(0, s.y - 1), Vector2(s.x, 1)), dark)
	ci.draw_rect(Rect2(p, Vector2(1, s.y)), dark)
	ci.draw_rect(Rect2(p + Vector2(s.x - 1, 0), Vector2(1, s.y)), dark)
	ci.draw_rect(Rect2(p + Vector2(1, 1), Vector2(s.x - 2, 1)), light)
	ci.draw_rect(Rect2(p + Vector2(1, 1), Vector2(1, s.y - 2)), light)
	ci.draw_rect(Rect2(p + Vector2(2, 2), Vector2(s.x - 4, 1)), mid)
	ci.draw_rect(Rect2(p + Vector2(2, 2), Vector2(1, s.y - 4)), mid)
	ci.draw_rect(Rect2(p + Vector2(1, s.y - 2), Vector2(s.x - 2, 1)), mid)
	ci.draw_rect(Rect2(p + Vector2(s.x - 2, 1), Vector2(1, s.y - 2)), mid)
	ci.draw_rect(Rect2(p + Vector2(2, s.y - 3), Vector2(s.x - 4, 1)), dark)
	ci.draw_rect(Rect2(p + Vector2(s.x - 3, 2), Vector2(1, s.y - 4)), dark)


static func bar(ci: CanvasItem, pos: Vector2, w: int, frac: float, col: Color, h := 5) -> void:
	ci.draw_rect(Rect2(pos - Vector2.ONE, Vector2(w + 2, h + 2)), Color8(8, 8, 24))
	ci.draw_rect(Rect2(pos, Vector2(w, h)), Color8(24, 24, 56))
	var fill := int(w * clampf(frac, 0, 1))
	if fill > 0:
		ci.draw_rect(Rect2(pos, Vector2(fill, h)), col)
		ci.draw_rect(Rect2(pos, Vector2(fill, 1)), col.lightened(0.4))
		ci.draw_rect(Rect2(pos + Vector2(0, h - 2), Vector2(fill, 2)), col.darkened(0.3))


static func cursor(ci: CanvasItem, pos: Vector2, t: float) -> void:
	text(ci, pos + Vector2(roundi(sin(t * 8.0) * 1.5), 0), "▶", GOLD)


# ---- sprite lookups --------------------------------------------------------------
static func tile_src(row: int, col: int) -> Rect2:
	var ts := ArtLayout.TILE
	return Rect2(col * ts, row * ts, ts, ts)


static func kind_col(kind: String) -> int:
	return ArtLayout.TILE_KINDS.find(kind)


static func town_col(kind: String) -> int:
	return ArtLayout.TOWN_KINDS.find(kind)


static func hero_src(look: int, facing: String, frame: String) -> Rect2:
	var cell: Vector2i = ArtLayout.HERO_CELL
	var row := look * 4 + ArtLayout.HERO_FACINGS.find(facing)
	var col := ArtLayout.HERO_FRAMES.find(frame)
	return Rect2(col * cell.x, row * cell.y, cell.x, cell.y)


static func portrait_src(look: int, expr := "neutral") -> Rect2:
	var p := ArtLayout.PORTRAIT
	return Rect2(ArtLayout.EXPRESSIONS.find(expr) * p, look * p, p, p)


static func monster_src(family_row: int, kind: int, frame: int) -> Rect2:
	var c := ArtLayout.MON_CELL
	return Rect2((kind * 3 + frame) * c, family_row * c, c, c)


static func prop_src(kind: String) -> Rect2:
	var ts := ArtLayout.TILE
	return Rect2(ArtLayout.PROP_KINDS.find(kind) * ts, 0, ts, ts)


static func portrait(ci: CanvasItem, pos: Vector2, look: int, expr := "neutral", framed := true) -> void:
	if framed:
		window(ci, Rect2(pos - Vector2(5, 5), Vector2(74, 74)), Color8(40, 40, 104), Color8(16, 16, 56))
	ci.draw_texture_rect_region(portraits, Rect2(pos, Vector2(64, 64)), portrait_src(look, expr))


static func comma(n: int) -> String:
	var s := str(absi(n))
	var out := ""
	while s.length() > 3:
		out = "," + s.substr(s.length() - 3) + out
		s = s.substr(0, s.length() - 3)
	return ("-" if n < 0 else "") + s + out
