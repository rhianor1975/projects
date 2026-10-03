class_name GameMap
extends RefCounted
## One floor (or the town): tiles, what you have seen of it, and everything
## standing on it. Field of view and line of sight are the C game's own --
## a Bresenham line per tile in the radius, walls and thicket blocking.

var w := 0
var h := 0
var floor_num := 0
var biome := 0
var tiles := PackedByteArray()
var seen := PackedByteArray()
var visible := PackedByteArray()
var district_id := PackedInt32Array()     # -1 outside any district
var districts: Array = []                 # {x,y,w,h,kind}
var monsters: Array = []                  # Array[Monster]
var mgrid := {}                           # cell index -> Monster
var items: Array = []                     # {x,y,kind,...}
var features: Array = []                  # {x,y,type,used,relic_kind}
var doors := {}                           # town: cell index -> building id
var roof_style := {}                      # town: cell index -> roof tile kind
var stairs_up := Vector2i(-1, -1)
var stairs_down := Vector2i(-1, -1)
var lever := Vector2i(-1, -1)
var lever_door := Vector2i(-1, -1)
var portal_a := Vector2i(-1, -1)
var portal_b := Vector2i(-1, -1)
var haven := Rect2i()
var event_name := ""
var event_desc := ""
var overrun := false
var gold_rush := false
var turns_on_floor := 0
var infest_target := 0
var spawn_density := 1


func setup(width: int, height: int, fill: int) -> void:
	w = width
	h = height
	tiles = PackedByteArray()
	tiles.resize(w * h)
	tiles.fill(fill)
	seen = PackedByteArray()
	seen.resize(w * h)
	visible = PackedByteArray()
	visible.resize(w * h)
	district_id = PackedInt32Array()
	district_id.resize(w * h)
	district_id.fill(-1)


func inb(x: int, y: int) -> bool:
	return x >= 0 and y >= 0 and x < w and y < h


func idx(x: int, y: int) -> int:
	return y * w + x


func t(x: int, y: int) -> int:
	if x < 0 or y < 0 or x >= w or y >= h:
		return C.Tile.WALL
	return tiles[y * w + x]


func set_t(x: int, y: int, v: int) -> void:
	if x >= 0 and y >= 0 and x < w and y < h:
		tiles[y * w + x] = v


static func blocks_walk(tt: int) -> bool:
	return tt == C.Tile.WALL or tt == C.Tile.WATER or tt == C.Tile.LOCKED_DOOR \
		or tt == C.Tile.SEALED_DOOR or tt == C.Tile.THICKET or tt == C.Tile.CRYSTAL \
		or tt == C.Tile.ROOF or tt == C.Tile.HOUSE_WALL or tt == C.Tile.DOOR \
		or tt == C.Tile.FOUNTAIN or tt == C.Tile.PLANTER


func walkable_player(x: int, y: int) -> bool:
	return inb(x, y) and not blocks_walk(tiles[y * w + x])


func walkable_monster(x: int, y: int) -> bool:
	if not inb(x, y):
		return false
	var tt := tiles[y * w + x]
	if blocks_walk(tt):
		return false
	return tt != C.Tile.LAVA and tt != C.Tile.MIASMA and tt != C.Tile.SNARE


func blocks_sight(tt: int) -> bool:
	return tt == C.Tile.WALL or tt == C.Tile.THICKET or tt == C.Tile.ROOF or tt == C.Tile.HOUSE_WALL


func district_at(x: int, y: int) -> int:
	if not inb(x, y):
		return -1
	var d := district_id[y * w + x]
	return -1 if d < 0 else districts[d].kind


func in_haven(x: int, y: int) -> bool:
	return haven.size.x > 0 and haven.has_point(Vector2i(x, y))


# ---- monsters ------------------------------------------------------------------
func add_monster(m: Monster) -> void:
	monsters.append(m)
	mgrid[m.y * w + m.x] = m


func monster_at(x: int, y: int) -> Monster:
	var m = mgrid.get(y * w + x)
	if m != null and m.alive:
		return m
	return null


func move_monster(m: Monster, nx: int, ny: int) -> void:
	if mgrid.get(m.y * w + m.x) == m:
		mgrid.erase(m.y * w + m.x)
	if nx < m.x:
		m.facing_left = true
	elif nx > m.x:
		m.facing_left = false
	m.x = nx
	m.y = ny
	mgrid[ny * w + nx] = m


func remove_dead() -> void:
	var live: Array = []
	for m in monsters:
		if m.alive:
			live.append(m)
		elif mgrid.get(m.y * w + m.x) == m:
			mgrid.erase(m.y * w + m.x)
	monsters = live


func rebuild_mgrid() -> void:
	mgrid.clear()
	for m in monsters:
		if m.alive:
			mgrid[m.y * w + m.x] = m


func items_at(x: int, y: int) -> Array:
	var out: Array = []
	for it in items:
		if it.x == x and it.y == y:
			out.append(it)
	return out


func feature_at(x: int, y: int) -> Dictionary:
	for f in features:
		if f.x == x and f.y == y:
			return f
	return {}


# ---- sight ---------------------------------------------------------------------
func line_of_sight(x0: int, y0: int, x1: int, y1: int) -> bool:
	if not inb(x0, y0) or not inb(x1, y1):
		return false
	var dx := absi(x1 - x0)
	var dy := absi(y1 - y0)
	var sx := 1 if x0 < x1 else -1
	var sy := 1 if y0 < y1 else -1
	var err := dx - dy
	var x := x0
	var y := y0
	while not (x == x1 and y == y1):
		if not (x == x0 and y == y0):
			if blocks_sight(tiles[y * w + x]):
				return false
		var e2 := 2 * err
		if e2 > -dy:
			err -= dy
			x += sx
		if e2 < dx:
			err += dx
			y += sy
	return true


var _lit: PackedInt32Array = PackedInt32Array()


## Light what (px,py) can see. `fresh` clears the last pass first; the
## party's own eyes add to the driver's with fresh = false.
func compute_fov(px: int, py: int, radius: int, fresh := true) -> void:
	if fresh:
		for i in _lit:
			visible[i] = 0
		_lit.clear()
	var r2 := radius * radius
	for y in range(py - radius, py + radius + 1):
		if y < 0 or y >= h:
			continue
		for x in range(px - radius, px + radius + 1):
			if x < 0 or x >= w:
				continue
			var dx := x - px
			var dy := y - py
			if dx * dx + dy * dy > r2:
				continue
			if line_of_sight(px, py, x, y):
				var i := y * w + x
				if visible[i] == 1:
					continue
				visible[i] = 1
				seen[i] = 1
				_lit.append(i)


func reveal_all() -> void:
	seen.fill(1)


func reveal_circle(cx: int, cy: int, radius: int) -> void:
	for y in range(cy - radius, cy + radius + 1):
		for x in range(cx - radius, cx + radius + 1):
			if inb(x, y) and (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius:
				seen[y * w + x] = 1


func is_visible(x: int, y: int) -> bool:
	return inb(x, y) and visible[y * w + x] == 1


func is_seen(x: int, y: int) -> bool:
	return inb(x, y) and seen[y * w + x] == 1


# ---- reachability --------------------------------------------------------------
## Breadth-first distances from (sx, sy) over ground you can walk, ignoring
## monsters. Hazards count as passable, the way tile_reachable() treats them.
func flood(sx: int, sy: int, limit := 1 << 30) -> PackedInt32Array:
	var dist := PackedInt32Array()
	dist.resize(w * h)
	dist.fill(-1)
	if not inb(sx, sy):
		return dist
	var q := PackedInt32Array([sy * w + sx])
	dist[sy * w + sx] = 0
	var head := 0
	while head < q.size():
		var c := q[head]
		head += 1
		var cx := c % w
		var cy := c / w
		var d := dist[c]
		if d >= limit:
			continue
		for dir in C.DIRS8:
			var nx: int = cx + dir.x
			var ny: int = cy + dir.y
			if nx < 0 or ny < 0 or nx >= w or ny >= h:
				continue
			var ni := ny * w + nx
			if dist[ni] >= 0 or blocks_walk(tiles[ni]):
				continue
			dist[ni] = d + 1
			q.append(ni)
	return dist


func reachable(a: Vector2i, b: Vector2i) -> bool:
	return flood(a.x, a.y)[b.y * w + b.x] >= 0


# ---- persistence ---------------------------------------------------------------
func to_dict() -> Dictionary:
	var ms: Array = []
	for m in monsters:
		if m.alive:
			ms.append(m.to_dict())
	return {
		"w": w, "h": h, "floor_num": floor_num, "biome": biome,
		"tiles": Marshalls.raw_to_base64(tiles.compress(FileAccess.COMPRESSION_DEFLATE)),
		"seen": Marshalls.raw_to_base64(seen.compress(FileAccess.COMPRESSION_DEFLATE)),
		"district_id": Marshalls.raw_to_base64(district_id.to_byte_array().compress(FileAccess.COMPRESSION_DEFLATE)),
		"districts": districts, "monsters": ms, "items": items, "features": features,
		"doors": doors, "stairs_up": [stairs_up.x, stairs_up.y], "stairs_down": [stairs_down.x, stairs_down.y],
		"lever": [lever.x, lever.y], "lever_door": [lever_door.x, lever_door.y],
		"portal_a": [portal_a.x, portal_a.y], "portal_b": [portal_b.x, portal_b.y],
		"haven": [haven.position.x, haven.position.y, haven.size.x, haven.size.y],
		"event_name": event_name, "event_desc": event_desc, "overrun": overrun, "gold_rush": gold_rush,
		"turns_on_floor": turns_on_floor, "infest_target": infest_target,
	}


static func _v(a) -> Vector2i:
	return Vector2i(int(a[0]), int(a[1]))


static func from_dict(d: Dictionary) -> GameMap:
	var m := GameMap.new()
	m.w = int(d.w)
	m.h = int(d.h)
	m.floor_num = int(d.floor_num)
	m.biome = int(d.biome)
	m.tiles = Marshalls.base64_to_raw(d.tiles).decompress(m.w * m.h, FileAccess.COMPRESSION_DEFLATE)
	m.seen = Marshalls.base64_to_raw(d.seen).decompress(m.w * m.h, FileAccess.COMPRESSION_DEFLATE)
	m.visible = PackedByteArray()
	m.visible.resize(m.w * m.h)
	m.district_id = Marshalls.base64_to_raw(d.district_id).decompress(m.w * m.h * 4, FileAccess.COMPRESSION_DEFLATE).to_int32_array()
	m.districts = []
	for ds in d.districts:
		var dd := {}
		for k in ds:
			dd[k] = int(ds[k])
		m.districts.append(dd)
	for md in d.monsters:
		m.monsters.append(Monster.from_dict(md))
	m.rebuild_mgrid()
	m.items = []
	for it in d.items:
		var c: Dictionary = it.duplicate()
		c.x = int(c.x)
		c.y = int(c.y)
		m.items.append(c)
	m.features = []
	for f in d.features:
		var c: Dictionary = f.duplicate()
		c.x = int(c.x)
		c.y = int(c.y)
		c.type = int(c.type)
		m.features.append(c)
	for k in d.doors:
		m.doors[int(k)] = d.doors[k]
	m.stairs_up = _v(d.stairs_up)
	m.stairs_down = _v(d.stairs_down)
	m.lever = _v(d.lever)
	m.lever_door = _v(d.lever_door)
	m.portal_a = _v(d.portal_a)
	m.portal_b = _v(d.portal_b)
	m.haven = Rect2i(int(d.haven[0]), int(d.haven[1]), int(d.haven[2]), int(d.haven[3]))
	m.event_name = d.event_name
	m.event_desc = d.event_desc
	m.overrun = d.overrun
	m.gold_rush = d.gold_rush
	m.turns_on_floor = int(d.turns_on_floor)
	m.infest_target = int(d.infest_target)
	return m
