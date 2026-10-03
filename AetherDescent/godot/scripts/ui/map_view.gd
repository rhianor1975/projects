class_name MapView
extends Node2D
## Draws the floor and everything on it, and turns the rules' fx queue into
## animation. The rules resolve a turn instantly; this catches up visually --
## a step slides 32px, a swing plays its three frames, a hit flashes and
## throws a number -- and reports `busy` while something the player should
## see is still playing.

const TS := 32
const STEP_TIME := 0.11
const SCREEN := Vector2(640, 360)

var world: Node2D
var hud: Node2D
var cam := Vector2.ZERO
var t := 0.0
var hud_visible := true
var show_full_map := false

var hero_vis := Vector2.ZERO    # where the driven body is drawn: the camera follows it
var body_vis := {}         # instance id -> {h, pos, steps, attack_t, lunge, cast_t, cast_col, flash_t, flash_col}
var fallen: Array = []     # party members going down: {h, pos, t}
var mon_vis := {}          # instance id -> {pos, flash_t, flash_col, lunge, lunge_t, phase}
var dying: Array = []      # {m, pos, t}
var floaters: Array = []   # {text, pos, t, col, scale}
var projectiles: Array = []
var bursts: Array = []
var screen_flash := 0.0
var busy_until := 0.0
var _map_ref: GameMap
var _mini: Image
var _mini_tex: ImageTexture
var _log_seen := 0
var _log_time := -99.0


func _ready() -> void:
	world = Node2D.new()
	add_child(world)
	world.draw.connect(_draw_world)
	hud = Node2D.new()
	add_child(hud)
	hud.draw.connect(_draw_hud)
	texture_filter = CanvasItem.TEXTURE_FILTER_NEAREST


func busy() -> bool:
	return t < busy_until


func hero_settled() -> bool:
	return hero_vis.distance_to(Vector2(Game.hero.pos()) * TS) < 6.0


func snap() -> void:
	hero_vis = Vector2(Game.hero.pos()) * TS
	body_vis.clear()
	fallen.clear()
	mon_vis.clear()
	dying.clear()
	projectiles.clear()
	bursts.clear()
	floaters.clear()
	_map_ref = null


## The controller has moved to another body: the camera goes with it.
func snap_camera() -> void:
	hero_vis = _bv(Game.hero).pos


func _bv(h: Hero) -> Dictionary:
	var k := h.get_instance_id()
	if not body_vis.has(k):
		body_vis[k] = {"h": h, "pos": Vector2(h.pos()) * TS, "steps": 0, "attack_t": 99.0, "lunge": Vector2.ZERO,
			"cast_t": 99.0, "cast_col": Color.WHITE, "flash_t": 99.0, "flash_col": Color.WHITE}
	return body_vis[k]


func _process(delta: float) -> void:
	t += delta
	if Game.hero == null or Game.map == null:
		return
	if _map_ref != Game.map:
		_map_ref = Game.map
		body_vis.clear()
		fallen.clear()
		mon_vis.clear()
		dying.clear()
		_rebuild_minimap()
	_consume_fx()
	var speed := TS / STEP_TIME * delta
	for k in body_vis.keys():
		var v: Dictionary = body_vis[k]
		var b: Hero = v.h
		if not b in Game.party:
			body_vis.erase(k)
			continue
		var bt := Vector2(b.pos()) * TS
		v.pos = bt if v.pos.distance_to(bt) > TS * 3 else v.pos.move_toward(bt, speed)
		v.attack_t += delta
		v.cast_t += delta
		v.flash_t += delta
	for b in Game.party:
		_bv(b)
	hero_vis = _bv(Game.hero).pos
	for f in fallen:
		f.t += delta
	fallen = fallen.filter(func(f): return f.t < 0.8)
	for k in mon_vis.keys():
		var v: Dictionary = mon_vis[k]
		var mo: Monster = v.m
		if not mo.alive:
			mon_vis.erase(k)
			continue
		var mt := Vector2(mo.pos()) * TS
		v.pos = mt if v.pos.distance_to(mt) > TS * 3 else v.pos.move_toward(mt, speed)
	for d in dying:
		d.t += delta
	dying = dying.filter(func(d): return d.t < 0.45)
	for f in floaters:
		f.t += delta
	floaters = floaters.filter(func(f): return f.t < f.get("life", 0.9))
	for p in projectiles:
		p.t += delta
	projectiles = projectiles.filter(func(p): return p.t < p.dur + 0.12)
	for b in bursts:
		b.t += delta
	bursts = bursts.filter(func(b): return b.t < 0.4)
	screen_flash = maxf(0.0, screen_flash - delta * 3.0)
	# the camera: centred on the hero, clamped to the map
	var m := Game.map
	var want := hero_vis + Vector2(TS / 2.0, TS / 2.0) - SCREEN / 2.0
	var max_c := Vector2(m.w * TS, m.h * TS) - SCREEN
	cam = Vector2(clampf(want.x, 0, maxf(0, max_c.x)), clampf(want.y, -8, maxf(-8, max_c.y + 40)))
	if m.w * TS < SCREEN.x:
		cam.x = (m.w * TS - SCREEN.x) / 2.0
	world.position = -cam.round()
	if Game.log_lines.size() != _log_seen:
		_log_seen = Game.log_lines.size()
		_log_time = t
	_update_minimap_near()
	world.queue_redraw()
	hud.queue_redraw()


func _vis_for(mo: Monster) -> Dictionary:
	var k := mo.get_instance_id()
	if not mon_vis.has(k):
		mon_vis[k] = {"m": mo, "pos": Vector2(mo.pos()) * TS, "flash_t": 99.0, "flash_col": Color.WHITE,
			"lunge": Vector2.ZERO, "lunge_t": 99.0, "phase": randf() * 3.0}
	return mon_vis[k]


func _block(seconds: float) -> void:
	busy_until = maxf(busy_until, t + seconds)


func _consume_fx() -> void:
	if Game.fx.is_empty():
		return
	var h := Game.hero
	var delay := 0.0
	for e in Game.fx:
		match e.type:
			"move":
				if e.who is Hero:
					_bv(e.who).steps += 1
			"attack":
				if e.who is Hero:
					var bv := _bv(e.who)
					bv.attack_t = 0.0
					bv.lunge = (Vector2(e.target.pos()) - Vector2(e.who.pos())).normalized()
					if e.who == h:
						_block(0.2)
				else:
					var v := _vis_for(e.who)
					v.lunge = (Vector2(e.target.pos()) - Vector2(e.who.pos())).normalized()
					v.lunge_t = 0.0
					if e.target == h:
						_block(0.12)
			"hurt":
				var who = e.who
				if who is Hero:
					var bv := _bv(who)
					bv.flash_t = 0.0
					bv.flash_col = Color8(255, 80, 80)
					_float(bv.pos + Vector2(16, -30), str(e.amount), Color8(255, 196, 196) if who == h else Color8(255, 220, 200),
						2 if who == h else 1, delay)
				else:
					var v := _vis_for(who)
					v.flash_t = -delay
					v.flash_col = Color.WHITE
					_float(v.pos + Vector2(16, -26), str(e.amount), Color.WHITE, 2, delay)
				delay += 0.05
			"die":
				var v := _vis_for(e.who)
				dying.append({"m": e.who, "pos": v.pos, "t": -delay})
				_block(0.15)
			"miss":
				var bp: Vector2 = _bv(e.who).pos if e.get("who") is Hero else hero_vis
				_float(bp + Vector2(16, -30), "Miss", Gfx.CYAN, 1, delay)
			"heal":
				var bp: Vector2 = _bv(e.who).pos if e.get("who") is Hero else hero_vis
				_float(bp + Vector2(16, -30), "+%d" % e.amount, Gfx.GREEN, 2, delay)
				bursts.append({"at": bp + Vector2(16, 16), "radius": 1, "t": 0.0, "col": Gfx.GREEN})
			"fall":
				fallen.append({"h": e.who, "pos": _bv(e.who).pos, "t": -delay})
				_block(0.3)
			"bolt", "shot":
				var dist := Vector2(e.from).distance_to(Vector2(e.to))
				var dur := clampf(dist * 0.03, 0.08, 0.25)
				projectiles.append({"from": Vector2(e.from) * TS + Vector2(16, 8), "to": Vector2(e.to) * TS + Vector2(16, 8),
					"t": -delay, "dur": dur, "col": e.color, "shot": e.type == "shot"})
				delay += dur
				_block(delay + 0.1)
			"burst":
				bursts.append({"at": Vector2(e.at) * TS + Vector2(16, 16), "radius": e.radius, "t": -delay, "col": e.color})
				_block(0.25)
			"cast":
				var bv := _bv(e.get("who") if e.get("who") is Hero else h)
				bv.cast_t = 0.0
				bv.cast_col = e.color
				if bv.h == h:
					_block(0.18)
			"levelup":
				var lh: Hero = e.get("who") if e.get("who") is Hero else h
				_float(_bv(lh).pos + Vector2(16, -40), "LEVEL UP!" if lh.xp < 3 else "GEAR!", Gfx.GOLD, 1, delay + 0.2, 1.4)
			"pickup":
				_float(Vector2(e.at) * TS + Vector2(16, -24), e.text, e.color, 1, delay, 1.0)
			"warp", "teleport":
				screen_flash = 1.0
				for b in Game.party:
					_bv(b).pos = Vector2(b.pos()) * TS
				hero_vis = Vector2(h.pos()) * TS
			"sense":
				screen_flash = 0.5
	Game.fx.clear()


func _float(pos: Vector2, text: String, col: Color, scale: int, delay := 0.0, life := 0.9) -> void:
	floaters.append({"text": text, "pos": pos, "t": -delay, "col": col, "scale": scale, "life": life})


# ---- the world --------------------------------------------------------------------
static func _hash(x: int, y: int) -> int:
	var h := (x * 73856093) ^ (y * 19349663)
	return (h ^ (h >> 13)) & 0x7fffffff


func _tile_light(m: GameMap, x: int, y: int) -> Color:
	if Game.depth == 0:
		return Color.WHITE
	if not m.is_seen(x, y):
		return Color(0, 0, 0, 0)
	if not m.is_visible(x, y):
		return Color(0.30, 0.32, 0.48)
	var h := Game.hero
	var r := float(h.fov_radius)
	var d := Vector2(x - h.x, y - h.y).length() / maxf(r, 1.0)
	var v := 1.0 if d < 0.45 else (0.86 if d < 0.75 else 0.72)
	return Color(v, v * 0.97, v * 0.92)


func _src_for(m: GameMap, x: int, y: int, tt: int) -> Rect2:
	var row := m.biome
	var hsh := _hash(x, y)
	var frame := int(t * 2.0 + (x + y) * 0.25) % 2
	if Game.depth == 0:
		var tr := ArtLayout.TOWN_ROW
		match tt:
			C.Tile.TOWN_FLOOR: return Gfx.tile_src(tr, hsh % 4)
			C.Tile.ROOF: return Gfx.tile_src(tr, Gfx.town_col(m.roof_style.get(y * m.w + x, "roof_red")))
			C.Tile.HOUSE_WALL: return Gfx.tile_src(tr, Gfx.town_col("house_wall"))
			C.Tile.DOOR: return Gfx.tile_src(tr, Gfx.town_col("door_" + m.doors.get(y * m.w + x, "general")))
			C.Tile.TOWN_GRASS: return Gfx.tile_src(tr, Gfx.town_col("grass"))
			C.Tile.TOWN_FLOWERS: return Gfx.tile_src(tr, Gfx.town_col("flowers"))
			C.Tile.PLANTER: return Gfx.tile_src(tr, Gfx.town_col("planter"))
			C.Tile.FOUNTAIN: return Gfx.tile_src(tr, Gfx.town_col("fountain"))
			C.Tile.TEMPLE: return Gfx.tile_src(tr, Gfx.town_col("temple"))
		return Gfx.tile_src(tr, 0)
	var k := "floor0"
	match tt:
		C.Tile.WALL:
			var below := m.t(x, y + 1)
			if below != C.Tile.WALL:
				k = "face%d" % (hsh % 3)
			else:
				k = "top%d" % (hsh % 2)
		C.Tile.FLOOR:
			var grown := m.district_at(x, y) >= 0
			var g := (hsh % 100) < (55 if grown else 18)
			k = ("grown%d" % (hsh % 2)) if g else ("floor%d" % (hsh % 4))
		C.Tile.DECOR: k = "rubble"
		C.Tile.GRASS: k = "grass%d" % (hsh % 2)
		C.Tile.FLOWERS: k = "flowers"
		C.Tile.THICKET: k = "thicket%d" % (hsh % 2)
		C.Tile.WATER: k = "water%d" % frame
		C.Tile.LAVA: k = "lava%d" % frame
		C.Tile.STAIRS_DOWN: k = "stairs_down"
		C.Tile.STAIRS_UP: k = "stairs_up"
		C.Tile.BRIDGE: k = "bridge"
		C.Tile.MIASMA: k = "miasma"
		C.Tile.PORTAL: k = "portal"
		C.Tile.LOCKED_DOOR: k = "locked_door"
		C.Tile.SEALED_DOOR: k = "sealed_door"
		C.Tile.LEVER: k = "lever"
		C.Tile.CRYSTAL: k = "crystal"
		C.Tile.BLOODPOOL: k = "bloodpool"
		C.Tile.PRISM_RED: k = "prism_red"
		C.Tile.PRISM_BLUE: k = "prism_blue"
		C.Tile.PRISM_GREEN: k = "prism_green"
		C.Tile.SNARE: k = "snare"
	return Gfx.tile_src(row, Gfx.kind_col(k))


func _draw_world() -> void:
	var m := Game.map
	if m == null:
		return
	var ci := world
	var x0 := maxi(0, int(cam.x / TS) - 1)
	var y0 := maxi(0, int(cam.y / TS) - 1)
	var x1 := mini(m.w - 1, int((cam.x + SCREEN.x) / TS) + 1)
	var y1 := mini(m.h - 1, int((cam.y + SCREEN.y) / TS) + 2)
	var torches: Array = []
	for y in range(y0, y1 + 1):
		for x in range(x0, x1 + 1):
			var tt := m.tiles[y * m.w + x]
			var light := _tile_light(m, x, y)
			if light.a == 0:
				continue
			var r := Rect2(x * TS, y * TS, TS, TS)
			if Game.depth > 0 and tt != C.Tile.WALL and tt != C.Tile.WATER:
				pass
			var mod := light
			if Game.depth > 0 and tt == C.Tile.FLOOR and m.district_at(x, y) >= 0:
				mod = mod * C.DISTRICT_TINTS[m.district_at(x, y)].lerp(Color.WHITE, 0.72)
			ci.draw_texture_rect_region(Gfx.tiles, r, _src_for(m, x, y, tt), mod)
			if Game.depth == 0:
				continue
			# depth cues: walls shade the floor below them, shores foam
			var above := m.t(x, y - 1)
			if tt != C.Tile.WALL and tt != C.Tile.WATER and above == C.Tile.WALL:
				for i in 6:
					ci.draw_rect(Rect2(x * TS, y * TS + i, TS, 1), Color(0.02, 0.0, 0.08, 0.45 - i * 0.07))
			if tt == C.Tile.WATER and above != C.Tile.WATER and above != C.Tile.BRIDGE:
				ci.draw_rect(Rect2(x * TS, y * TS, TS, 1), Color(0.9, 0.96, 1.0, 0.85 * light.r))
				ci.draw_rect(Rect2(x * TS, y * TS + 1, TS, 2), Color(0.6, 0.8, 1.0, 0.45 * light.r))
			if tt == C.Tile.WALL and m.t(x, y + 1) != C.Tile.WALL and _hash(x, y) % 11 == 0:
				var flick := 0.85 + 0.15 * sin(t * 9.0 + x)
				ci.draw_texture_rect(Gfx.torch, Rect2(x * TS, y * TS - 4, TS, TS), false, light * Color(1, 1, 1, flick))
				if m.is_visible(x, y):
					torches.append(Vector2(x * TS + 16, y * TS + 8))
	# torch glow: additive-looking warm pools
	for p in torches:
		for i in 4:
			ci.draw_circle(p, 26.0 + i * 14.0, Color(1.0, 0.6, 0.25, 0.05))
	# items and features you have seen
	for f in m.features:
		if f.x < x0 or f.x > x1 or f.y < y0 or f.y > y1 or not m.is_seen(f.x, f.y):
			continue
		var prop: String = C.FEATURE_PROPS[f.type]
		var mod := _tile_light(m, f.x, f.y)
		if f.get("used", false) and f.type in [C.Feature.SHRINE, C.Feature.RELIC, C.Feature.MACHINE, C.Feature.ALTAR]:
			mod = mod * Color(0.55, 0.55, 0.6)
		ci.draw_texture_rect_region(Gfx.props, Rect2(f.x * TS, f.y * TS - 4, TS, TS), Gfx.prop_src(prop), mod)
	for it in m.items:
		if it.x < x0 or it.x > x1 or it.y < y0 or it.y > y1 or not m.is_seen(it.x, it.y):
			continue
		var prop := "gold"
		match it.kind:
			"key": prop = "key"
			"quest": prop = "quest"
			"consumable": prop = ItemsData.prop_for_consumable(ItemsData.consumable_by_name(it.name))
		var bob := sin(t * 3.0 + it.x) * 1.5 if it.kind != "gold" else 0.0
		ci.draw_texture_rect_region(Gfx.props, Rect2(it.x * TS, it.y * TS - 2 + bob, TS, TS), Gfx.prop_src(prop), _tile_light(m, it.x, it.y))
	# actors, back to front
	var actors: Array = []
	if Game.depth > 0:
		for mo in m.monsters:
			if not mo.alive or mo.x < x0 - 1 or mo.x > x1 + 1 or mo.y < y0 - 1 or mo.y > y1 + 1:
				continue
			if not m.is_visible(mo.x, mo.y):
				continue
			var v := _vis_for(mo)
			actors.append([v.pos.y, 0, mo, v])
		for d in dying:
			actors.append([d.pos.y, 2, d])
		for f in fallen:
			actors.append([f.pos.y, 3, f])
	# the party: in town only whoever you are driving walks the streets
	for b in Game.party:
		if not Party.is_up(b) or (Game.depth == 0 and b != Game.hero):
			continue
		if Game.depth > 0 and b != Game.hero and not m.is_visible(b.x, b.y):
			continue
		var bv := _bv(b)
		actors.append([bv.pos.y + (0.5 if b == Game.hero else 0.4), 1, b, bv])
	actors.sort_custom(func(a, b): return a[0] < b[0])
	for a in actors:
		match a[1]:
			0: _draw_monster(ci, a[2], a[3])
			1: _draw_hero(ci, a[2], a[3])
			2: _draw_dying(ci, a[2])
			3: _draw_fallen(ci, a[2])
	# effects
	for p in projectiles:
		if p.t < 0:
			continue
		var k := clampf(p.t / p.dur, 0, 1)
		var pos: Vector2 = p.from.lerp(p.to, k)
		if p.t <= p.dur:
			var tail: Vector2 = p.from.lerp(p.to, maxf(0, k - 0.25))
			ci.draw_line(tail, pos, Color(p.col, 0.6), 3.0 if not p.shot else 2.0)
			ci.draw_circle(pos, 4.0 if not p.shot else 2.5, p.col)
			ci.draw_circle(pos, 2.0 if not p.shot else 1.2, Color.WHITE)
		else:
			var f: float = (p.t - p.dur) / 0.12
			for i in 8:
				var ang := i * PI / 4
				ci.draw_line(p.to + Vector2.from_angle(ang) * 3 * (1 + f * 3), p.to + Vector2.from_angle(ang) * 6 * (1 + f * 3), Color(p.col.lightened(0.4), 1 - f), 2.0)
	for b in bursts:
		if b.t < 0:
			continue
		var f: float = b.t / 0.4
		var rad: float = (b.radius + 0.5) * TS * (0.3 + f * 0.7)
		ci.draw_arc(b.at, rad, 0, TAU, 48, Color(b.col, 1.0 - f), 3.0)
		ci.draw_arc(b.at, rad * 0.7, 0, TAU, 48, Color(b.col.lightened(0.5), (1.0 - f) * 0.6), 2.0)
	for f in floaters:
		if f.t < 0:
			continue
		var rise: float = minf(f.t, 0.35) * 40.0
		var alpha: float = 1.0 if f.t < f.life * 0.7 else 1.0 - (f.t - f.life * 0.7) / (f.life * 0.3)
		var col: Color = f.col
		col.a = alpha
		Gfx.text_center(ci, f.pos.x, f.pos.y - rise, f.text, col, f.scale, Color(Gfx.INK, alpha))


func _draw_hero(ci: CanvasItem, h: Hero, v: Dictionary) -> void:
	var frame := "walk0"
	var target := Vector2(h.pos()) * TS
	var at: Vector2 = v.pos
	var moving := at.distance_to(target) > 0.5
	if v.attack_t < 0.22:
		frame = "atk0" if v.attack_t < 0.06 else ("atk1" if v.attack_t < 0.14 else "atk2")
	elif v.cast_t < 0.3:
		frame = "cast"
	elif moving:
		frame = "walk1" if v.steps % 2 == 0 else "walk3"
		if at.distance_to(target) < TS * 0.35:
			frame = "walk2" if v.steps % 2 == 0 else "walk0"
	var lunge := Vector2.ZERO
	if v.attack_t < 0.22:
		lunge = v.lunge * 6.0 * sin(clampf(v.attack_t / 0.22, 0, 1) * PI)
	var cell: Vector2i = ArtLayout.HERO_CELL
	var feet: Vector2 = at + Vector2(TS / 2.0, TS - 3) + lunge
	var mod := _tile_light(Game.map, h.x, h.y) if Game.depth > 0 else Color.WHITE
	mod.a = 1
	if v.flash_t < 0.16 and int(v.flash_t * 30) % 2 == 0:
		mod = v.flash_col
	_shadow(ci, feet, 22)
	if v.cast_t < 0.35:
		var f: float = v.cast_t / 0.35
		ci.draw_arc(feet - Vector2(0, 20), 10 + f * 18, 0, TAU, 32, Color(v.cast_col, 1 - f), 2.0)
	if h.guard_turns > 0:
		ci.draw_arc(feet - Vector2(0, 18), 17, 0, TAU, 24, Color(0.8, 0.88, 1.0, 0.55), 2.0)
	var pos := (feet - Vector2(cell.x / 2.0, ArtLayout.HERO_FEET_Y)).round()
	ci.draw_texture_rect_region(Gfx.heroes, Rect2(pos, Vector2(cell)), Gfx.hero_src(h.look, h.facing, frame), mod)
	if h == Game.hero:
		if Game.recall_countdown > 0:
			ci.draw_arc(feet - Vector2(0, 22), 18 + sin(t * 6) * 2, 0, TAU, 32, Color(0.5, 0.95, 1.0, 0.8), 2.0)
		return
	# one of yours: a marker in their colour, and their health once it is hurt
	var col := Party.color_of(h)
	var top := feet - Vector2(0, ArtLayout.HERO_FEET_Y + 2 + sin(t * 3 + h.roster_idx) * 1.0)
	ci.draw_colored_polygon(PackedVector2Array([top + Vector2(-4, -5), top + Vector2(4, -5), top]), Gfx.INK)
	ci.draw_colored_polygon(PackedVector2Array([top + Vector2(-3, -4), top + Vector2(3, -4), top + Vector2(0, -1)]), col)
	var mx := Party.max_hp(h)
	if h.hp < mx:
		Gfx.bar(ci, feet + Vector2(-11, 2), 22, float(h.hp) / mx, Color8(88, 216, 96) if h.hp * 2 > mx else Color8(240, 200, 64), 2)


func _draw_fallen(ci: CanvasItem, f: Dictionary) -> void:
	if f.t < 0:
		return
	var h: Hero = f.h
	var k: float = f.t / 0.8
	if int(f.t * 20) % 2 == 0 and k < 0.5:
		return
	var cell: Vector2i = ArtLayout.HERO_CELL
	var feet: Vector2 = f.pos + Vector2(TS / 2.0, TS - 3)
	var pos := (feet - Vector2(cell.x / 2.0, ArtLayout.HERO_FEET_Y - k * 12)).round()
	ci.draw_texture_rect_region(Gfx.heroes, Rect2(pos, Vector2(cell)), Gfx.hero_src(h.look, "down", "atk2"), Color(1, 0.5, 0.5, 1 - k))


func _draw_monster(ci: CanvasItem, mo: Monster, v: Dictionary) -> void:
	var m := Game.map
	var c := ArtLayout.MON_CELL
	var frame := int(t * 2.5 + v.phase) % 2
	var lunge := Vector2.ZERO
	if v.lunge_t < 0.18:
		frame = 2
		lunge = v.lunge * 8.0 * sin(clampf(v.lunge_t / 0.18, 0, 1) * PI)
	v.lunge_t += get_process_delta_time()
	v.flash_t += get_process_delta_time()
	var feet: Vector2 = v.pos + Vector2(TS / 2.0, TS - 3) + lunge
	var mod := _tile_light(m, mo.x, mo.y)
	mod.a = 1
	if v.flash_t >= 0 and v.flash_t < 0.14:
		mod = Color(3, 3, 3)
	var big := mo.is_boss
	var size := ArtLayout.BOSS_CELL if big else c
	_shadow(ci, feet, 34 if big else 26)
	var pos := (feet - Vector2(size / 2.0, size - 3)).round()
	var dst := Rect2(pos, Vector2(size, size))
	var flip := not mo.facing_left
	if flip:
		dst = Rect2(pos + Vector2(size, 0), Vector2(-size, size))
	if big:
		ci.draw_texture_rect_region(Gfx.warden, dst, Gfx.warden_src(frame), mod)
	else:
		var src := Gfx.monster_src(Monster.family_row(mo), mo.kind, frame)
		if mo.biome_boss or mo.is_elite:
			# elites and biome bosses: drawn bigger, with a glow
			var grow := 1.35 if mo.biome_boss else 1.15
			var s2 := c * grow
			pos = (feet - Vector2(s2 / 2.0, s2 - 3)).round()
			dst = Rect2(pos, Vector2(s2, s2)) if not flip else Rect2(pos + Vector2(s2, 0), Vector2(-s2, s2))
			ci.draw_circle(feet - Vector2(0, s2 * 0.45), s2 * 0.42, Color(1.0, 0.85, 0.3, 0.10 + 0.05 * sin(t * 4)))
		ci.draw_texture_rect_region(Gfx.monsters, dst, src, mod)
	# health bar once it has been hurt
	if mo.hp < mo.maxhp:
		var w := 22
		Gfx.bar(ci, feet + Vector2(-w / 2.0, -(size if big else c) - 2), w, float(mo.hp) / mo.maxhp,
			Color8(232, 72, 72) if not (mo.is_elite or mo.is_boss) else Color8(255, 176, 48), 2)
	if mo.stun_turns > 0:
		Gfx.text_center(ci, feet.x, feet.y - c - 10, "*", Gfx.GOLD)


func _draw_dying(ci: CanvasItem, d: Dictionary) -> void:
	if d.t < 0:
		return
	var mo: Monster = d.m
	var f: float = d.t / 0.45
	var c := ArtLayout.MON_CELL
	var feet: Vector2 = d.pos + Vector2(TS / 2.0, TS - 3)
	if int(d.t * 24) % 2 == 0:
		return
	var src := Gfx.monster_src(Monster.family_row(mo), mo.kind, 2)
	var size := c
	if mo.is_boss:
		size = ArtLayout.BOSS_CELL
	var pos := (feet - Vector2(size / 2.0, size - 3 - f * 10)).round()
	if mo.is_boss:
		ci.draw_texture_rect_region(Gfx.warden, Rect2(pos, Vector2(size, size)), Gfx.warden_src(2), Color(1, 0.6, 0.6, 1 - f))
	else:
		ci.draw_texture_rect_region(Gfx.monsters, Rect2(pos, Vector2(size, size)), src, Color(1, 0.6, 0.6, 1 - f))


func _shadow(ci: CanvasItem, feet: Vector2, w: float) -> void:
	var pts := PackedVector2Array()
	for i in 16:
		var a := i * TAU / 16
		pts.append(feet + Vector2(cos(a) * w / 2.0, sin(a) * 3.5 - 1))
	ci.draw_colored_polygon(pts, Color(0, 0, 0, 0.32))


# ---- minimap ---------------------------------------------------------------------
func _mini_color(m: GameMap, x: int, y: int) -> Color:
	var i := y * m.w + x
	if m.seen[i] == 0:
		return Color(0, 0, 0, 0)
	var tt := m.tiles[i]
	match tt:
		C.Tile.WALL, C.Tile.ROOF, C.Tile.HOUSE_WALL: return Color8(40, 40, 72)
		C.Tile.WATER: return Color8(64, 112, 200)
		C.Tile.LAVA: return Color8(240, 96, 32)
		C.Tile.STAIRS_DOWN, C.Tile.TEMPLE: return Color8(255, 224, 96)
		C.Tile.STAIRS_UP: return Color8(200, 200, 255)
		C.Tile.THICKET, C.Tile.GRASS, C.Tile.FLOWERS: return Color8(72, 140, 72)
		C.Tile.DOOR, C.Tile.LOCKED_DOOR, C.Tile.SEALED_DOOR: return Color8(220, 150, 80)
	var d := m.district_at(x, y)
	if d >= 0:
		return C.DISTRICT_TINTS[d].darkened(0.25)
	return Color8(120, 128, 140)


func _rebuild_minimap() -> void:
	var m := Game.map
	_mini = Image.create(m.w, m.h, false, Image.FORMAT_RGBA8)
	for y in m.h:
		for x in m.w:
			if m.seen[y * m.w + x]:
				_mini.set_pixel(x, y, _mini_color(m, x, y))
	_mini_tex = ImageTexture.create_from_image(_mini)


var _mini_tick := 0


func _update_minimap_near() -> void:
	if _mini == null or Game.map == null:
		return
	_mini_tick += 1
	if _mini_tick % 6 != 0:
		return
	var m := Game.map
	var h := Game.hero
	var r := h.fov_radius + 12
	for y in range(maxi(0, h.y - r), mini(m.h, h.y + r + 1)):
		for x in range(maxi(0, h.x - r), mini(m.w, h.x + r + 1)):
			if m.seen[y * m.w + x]:
				_mini.set_pixel(x, y, _mini_color(m, x, y))
	_mini_tex.update(_mini)


# ---- HUD -------------------------------------------------------------------------
func _draw_hud() -> void:
	if Game.hero == null or not hud_visible:
		return
	var ci := hud
	var h := Game.hero
	if screen_flash > 0:
		ci.draw_rect(Rect2(Vector2.ZERO, SCREEN), Color(1, 1, 1, screen_flash * 0.6))
	# the party panel
	Gfx.window(ci, Rect2(4, 4, 236, 64))
	var expr := "hurt" if h.hp * 3 < h.maxhp else ("angry" if _bv(h).attack_t < 0.5 else "neutral")
	ci.draw_texture_rect_region(Gfx.portraits, Rect2(10, 12, 44, 44), Gfx.portrait_crop(h.look, expr, Vector2(10, 8), Vector2(44, 44)))
	var lv := "Lv %d" % h.level
	var cls := h.class_name_str()
	var nm := h.name
	if 60 + Gfx.text_width(nm) + Gfx.text_width(lv) + Gfx.text_width(cls) + 16 > 232:
		nm = Party.short_name(h)
	Gfx.text(ci, Vector2(60, 12), nm, Party.color_of(h) if h.is_hire else Gfx.WHITE)
	Gfx.text(ci, Vector2(60 + Gfx.text_width(nm) + 8, 12), lv, Gfx.GREY)
	if 60 + Gfx.text_width(nm) + Gfx.text_width(lv) + Gfx.text_width(cls) + 16 <= 232:
		Gfx.text_right(ci, Vector2(232, 12), cls, Gfx.DIM)
	Gfx.text(ci, Vector2(60, 27), "HP", Gfx.CYAN)
	var hp_col := Color8(88, 216, 96) if h.hp * 2 > h.maxhp else (Color8(240, 200, 64) if h.hp * 4 > h.maxhp else Color8(240, 72, 72))
	Gfx.bar(ci, Vector2(78, 28), 96, float(h.hp) / h.maxhp, hp_col)
	Gfx.text(ci, Vector2(180, 27), "%d/%d" % [h.hp, h.maxhp], Gfx.WHITE)
	Gfx.text(ci, Vector2(60, 41), "AE", Gfx.CYAN)
	Gfx.bar(ci, Vector2(78, 42), 96, float(h.aether) / maxi(h.aether_max, 1), Color8(96, 160, 248))
	Gfx.text(ci, Vector2(180, 41), "%d/%d" % [h.aether, h.aether_max], Gfx.WHITE)
	var xp_frac := float(h.xp) / maxi(h.xp_next, 1)
	ci.draw_rect(Rect2(60, 55, 172, 2), Color8(24, 24, 56))
	ci.draw_rect(Rect2(60, 55, 172 * xp_frac, 2), Color8(220, 180, 255))
	# status chips
	var chips: Array = []
	if h.ranged_type != C.Ranged.NONE:
		chips.append(["AM %d" % h.ranged_ammo, Gfx.WHITE])
	if h.atk_buff_turns > 0: chips.append(["ATK+", Gfx.GOLD])
	if h.def_buff_turns > 0: chips.append(["DEF+", Gfx.CYAN])
	if h.poison_turns > 0: chips.append(["PSN", Gfx.GREEN])
	if h.stun_turns > 0: chips.append(["STUN", Gfx.GOLD])
	if h.slow_turns > 0: chips.append(["SLOW", Gfx.CYAN])
	if h.held_turns > 0: chips.append(["HELD", Gfx.RED])
	if h.stance_atk_pct > 0: chips.append(["RED", Gfx.RED])
	if h.stance_def_pct > 0: chips.append(["BLUE", Gfx.CYAN])
	if Game.recall_countdown > 0: chips.append(["RECALL %d" % Game.recall_countdown, Gfx.CYAN])
	var cx := 8.0
	for chip in chips:
		var w := Gfx.text_width(chip[0]) + 8
		Gfx.window(ci, Rect2(cx, 70, w, 14), Color8(30, 30, 80), Color8(12, 12, 40))
		Gfx.text(ci, Vector2(cx + 4, 73), chip[0], chip[1], 1, false)
		cx += w + 2
	# the rest of the party: a face, a name and a bar each
	var py := 88.0 if not chips.is_empty() else 72.0
	for b in Game.party:
		if b == h:
			continue
		var up := Party.is_up(b)
		Gfx.window(ci, Rect2(4, py, 132, 22), Color8(30, 30, 80), Color8(12, 12, 40))
		ci.draw_texture_rect_region(Gfx.portraits, Rect2(8, py + 3, 16, 16),
			Gfx.portrait_crop(b.look, "hurt" if not up or b.hp * 3 < Party.max_hp(b) else "neutral", Vector2(14, 10), Vector2(36, 36)),
			Color.WHITE if up else Color(0.4, 0.4, 0.45))
		Gfx.text(ci, Vector2(28, py + 3), Party.short_name(b), Party.color_of(b) if up else Gfx.DIM, 1, false)
		if up:
			Gfx.bar(ci, Vector2(28, py + 14), 72, float(b.hp) / Party.max_hp(b), Color8(88, 216, 96) if b.hp * 2 > Party.max_hp(b) else Color8(240, 200, 64), 3)
			Gfx.text_right(ci, Vector2(132, py + 3), "Lv%d" % b.level, Gfx.GREY)
		else:
			Gfx.text(ci, Vector2(28, py + 12), "fallen", Gfx.DIM, 1, false)
		py += 24
	# where you are, and the purse
	Gfx.window(ci, Rect2(SCREEN.x - 168, 4, 164, 40))
	if Game.depth == 0:
		Gfx.text(ci, Vector2(SCREEN.x - 158, 12), "The City", Gfx.GOLD)
		Gfx.text(ci, Vector2(SCREEN.x - 108, 12), "deepest B%d" % Game.deepest_floor, Gfx.GREY)
	else:
		Gfx.text(ci, Vector2(SCREEN.x - 158, 12), "B%d" % Game.depth, Gfx.GOLD)
		Gfx.text(ci, Vector2(SCREEN.x - 132, 12), C.BIOME_SHORT[Game.map.biome], Gfx.WHITE)
	Gfx.text(ci, Vector2(SCREEN.x - 158, 27), "G", Gfx.GOLD)
	Gfx.text(ci, Vector2(SCREEN.x - 146, 27), Gfx.comma(Game.gold), Gfx.WHITE)
	if Game.keys > 0:
		Gfx.text_right(ci, Vector2(SCREEN.x - 12, 27), "Keys %d" % Game.keys, Gfx.GREY)
	elif Game.gold_mult > 1:
		Gfx.text_right(ci, Vector2(SCREEN.x - 12, 27), "x%d" % Game.gold_mult, Gfx.GOLD)
	# minimap
	if Game.depth > 0 and _mini_tex:
		var m := Game.map
		var box := Vector2(120, 72)
		var r := Rect2(SCREEN.x - box.x - 8, 48, box.x + 4, box.y + 4)
		Gfx.window(ci, r, Color8(24, 24, 64), Color8(8, 8, 32))
		# a window onto the floor around you, 1px a tile on the small worlds
		# a window onto the floor around you, two pixels a tile
		var span := Vector2(minf(m.w, (box.x - 8) / 2), minf(m.h, (box.y - 8) / 2))
		var origin := Vector2(clampf(h.x - span.x / 2, 0, m.w - span.x), clampf(h.y - span.y / 2, 0, m.h - span.y)).floor()
		var dst := Rect2(r.position + Vector2(6, 6) + (Vector2(box.x - 8, box.y - 8) - span * 2) / 2, span * 2)
		ci.draw_texture_rect_region(_mini_tex, dst, Rect2(origin, span))
		for b in Party.others():
			var bp := (Vector2(b.pos()) - origin) * 2
			if bp.x >= 0 and bp.y >= 0 and bp.x < span.x * 2 and bp.y < span.y * 2:
				ci.draw_rect(Rect2(dst.position + bp - Vector2(1, 1), Vector2(2, 2)), Party.color_of(b))
		var hp := dst.position + (Vector2(h.pos()) - origin) * 2
		if int(t * 4) % 2 == 0:
			ci.draw_rect(Rect2(hp - Vector2(1, 1), Vector2(3, 3)), Color.WHITE)
	# messages
	var recent := t - _log_time < 6.0
	if Game.depth > 0 or recent:
		var n := Game.log_lines.size()
		Gfx.window(ci, Rect2(4, SCREEN.y - 50, SCREEN.x - 8, 46))
		for i in 2:
			var li := n - 2 + i
			if li < 0:
				continue
			var line: Array = Game.log_lines[li]
			var col: Color = line[1]
			if i == 0:
				col = col.darkened(0.25)
			Gfx.text(ci, Vector2(14, SCREEN.y - 42 + i * 15), line[0], col)
	if show_full_map and Game.depth > 0:
		_draw_full_map(ci)


func _draw_full_map(ci: CanvasItem) -> void:
	var m := Game.map
	ci.draw_rect(Rect2(Vector2.ZERO, SCREEN), Color(0, 0, 0.08, 0.85))
	var scale := minf((SCREEN.x - 40) / m.w, (SCREEN.y - 60) / m.h)
	var size := Vector2(m.w, m.h) * scale
	var pos := ((SCREEN - size) / 2).round() + Vector2(0, 8)
	Gfx.window(ci, Rect2(pos - Vector2(8, 8), size + Vector2(16, 16)), Color8(24, 24, 64), Color8(8, 8, 32))
	ci.draw_texture_rect(_mini_tex, Rect2(pos, size), false)
	for b in Party.others():
		ci.draw_circle(pos + Vector2(b.pos()) * scale, 2, Party.color_of(b))
	var hp := pos + Vector2(Game.hero.pos()) * scale
	ci.draw_circle(hp, 3, Color.WHITE if int(t * 4) % 2 == 0 else Gfx.GOLD)
	Gfx.text_center(ci, SCREEN.x / 2, 6, "Floor %d -- %s" % [Game.depth, C.BIOME_NAMES[m.biome]], Gfx.GOLD)
