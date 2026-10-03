class_name MapGen
## Floor generation -- a port of generate_temple_floor() in src/mapgen.c.
##
## Order matters and is the C game's: lakes and lava first, then wild
## districts (the cave was there before anybody built in it), then the camp,
## then rooms packed into what is left, joined nearest-first so the floor falls
## into neighbourhoods, then stairs, hazards, monsters, loot, vaults, features
## and an event -- and finally a reachability check that repairs the one
## invariant everything else assumes: the stairs connect.

const ROOM_SPECS := [   # per 10k tiles, w_min, w_max, h_min, h_max
	[3, 16, 26, 10, 16],   # plaza
	[4, 10, 14, 10, 14],   # hub
	[10, 14, 24, 4, 6],    # hall
	[31, 7, 11, 6, 9],     # chamber
	[35, 4, 6, 4, 6],      # cell
]
const ROOM_HUB := 1
const ROOM_HALL := 2
const DIST_PER_10K := 8

static var rng := RandomNumberGenerator.new()


static func floor_seed(run_seed: int, floor_num: int) -> int:
	var h := (run_seed * 2654435761 + floor_num * 40503 + 0x9e3779b9) & 0x7fffffff
	h = ((h >> 15) ^ h) * 2246822519 & 0x7fffffff
	return h ^ (h >> 13)


static func r(n: int) -> int:
	return rng.randi() % maxi(n, 1)


static func area_scale(m: GameMap) -> float:
	return float(m.w * m.h) / 11200.0


static func scale_by_area(m: GameMap, n: int) -> int:
	return int(n * area_scale(m))


# ---- carving helpers -----------------------------------------------------------
static func carve_room(m: GameMap, rm: Rect2i) -> void:
	for y in range(rm.position.y, rm.end.y):
		for x in range(rm.position.x, rm.end.x):
			m.set_t(x, y, C.Tile.FLOOR)


static func blob(m: GameMap, cx: int, cy: int, radius: int, from: int, to: int) -> void:
	for y in range(cy - radius, cy + radius + 1):
		for x in range(cx - radius, cx + radius + 1):
			if x < 1 or y < 1 or x >= m.w - 1 or y >= m.h - 1:
				continue
			if (x - cx) * (x - cx) + (y - cy) * (y - cy) > radius * radius:
				continue
			if r(100) >= 78:
				continue
			if m.t(x, y) == from:
				m.set_t(x, y, to)


static func _strip(m: GameMap, x: int, y: int) -> void:
	if x < 1 or y < 1 or x >= m.w - 1 or y >= m.h - 1:
		return
	var tt := m.t(x, y)
	if tt == C.Tile.WALL or tt == C.Tile.THICKET or tt == C.Tile.CRYSTAL:
		m.set_t(x, y, C.Tile.FLOOR)
	elif tt == C.Tile.WATER:
		m.set_t(x, y, C.Tile.BRIDGE)


static func h_corridor(m: GameMap, x0: int, x1: int, y: int, width: int) -> void:
	var off := (width - 1) / 2
	for x in range(mini(x0, x1), maxi(x0, x1) + 1):
		for k in width:
			_strip(m, x, y - off + k)


static func v_corridor(m: GameMap, y0: int, y1: int, x: int, width: int) -> void:
	var off := (width - 1) / 2
	for y in range(mini(y0, y1), maxi(y0, y1) + 1):
		for k in width:
			_strip(m, x - off + k, y)


static func corridor_width() -> int:
	var roll := r(100)
	if roll < 8:
		return 3
	if roll < 26:
		return 2
	return 1


static func center(rm: Rect2i) -> Vector2i:
	return Vector2i(rm.position.x + rm.size.x / 2, rm.position.y + rm.size.y / 2)


static func join(m: GameMap, a: Rect2i, b: Rect2i, width := -1) -> void:
	if width < 0:
		width = corridor_width()
	var ca := center(a)
	var cb := center(b)
	if r(2) == 1:
		h_corridor(m, ca.x, cb.x, ca.y, width)
		v_corridor(m, ca.y, cb.y, cb.x, width)
	else:
		v_corridor(m, ca.y, cb.y, ca.x, width)
		h_corridor(m, ca.x, cb.x, cb.y, width)


static func nearest_room(rooms: Array, from: int, nth: int) -> int:
	var fc := center(rooms[from])
	var ds: Array = []
	for i in rooms.size():
		if i == from:
			continue
		var c := center(rooms[i])
		ds.append([(c.x - fc.x) * (c.x - fc.x) + (c.y - fc.y) * (c.y - fc.y), i])
	ds.sort_custom(func(a, b): return a[0] < b[0])
	if ds.is_empty():
		return -1
	return ds[mini(nth - 1, ds.size() - 1)][1]


# ---- wild districts --------------------------------------------------------------
static func pick_district_kind(biome: int) -> int:
	var wts: Array = C.DIST_WEIGHT[biome].duplicate()
	# a barrow with nobody in it is a room with piers in it: until a run has
	# been lost, the kind does not come up
	if Game.fallen().size() < C.GAUNTLET_MIN_GHOSTS:
		wts[C.District.GAUNTLET] = 0
	var total := 0
	for v in wts:
		total += v
	var roll := r(total)
	for i in wts.size():
		if roll < wts[i]:
			return i
		roll -= wts[i]
	return C.District.RUINS


static func _inb(m: GameMap, x: int, y: int) -> bool:
	return x >= 1 and y >= 1 and x < m.w - 1 and y < m.h - 1


## Plant `tt` on `count` random floor squares inside the district, away from its edge.
static func _plant(m: GameMap, rm: Rect2i, count: int, tt: int) -> void:
	for i in count:
		var x := rm.position.x + 2 + r(maxi(1, rm.size.x - 4))
		var y := rm.position.y + 2 + r(maxi(1, rm.size.y - 4))
		if _inb(m, x, y) and m.t(x, y) == C.Tile.FLOOR:
			m.set_t(x, y, tt)


## A walled rectangle with a floor inside -- the barrow and the proving ground.
static func _walled(m: GameMap, rm: Rect2i, rim_pct: int) -> void:
	for y in range(rm.position.y, rm.end.y):
		for x in range(rm.position.x, rm.end.x):
			if not _inb(m, x, y):
				continue
			var rim := x == rm.position.x or x == rm.end.x - 1 or y == rm.position.y or y == rm.end.y - 1
			if not rim:
				m.set_t(x, y, C.Tile.FLOOR)
			elif r(100) < rim_pct:
				m.set_t(x, y, C.Tile.WALL)


static func fill_ragged(m: GameMap, rm: Rect2i, tt: int) -> void:
	for y in range(rm.position.y, rm.end.y):
		for x in range(rm.position.x, rm.end.x):
			if x < 1 or y < 1 or x >= m.w - 1 or y >= m.h - 1:
				continue
			var rim := x == rm.position.x or x == rm.end.x - 1 or y == rm.position.y or y == rm.end.y - 1
			var near := x <= rm.position.x + 1 or x >= rm.end.x - 2 or y <= rm.position.y + 1 or y >= rm.end.y - 2
			if rim and r(100) < 72:
				continue
			if near and r(100) < 28:
				continue
			m.set_t(x, y, tt)


static func scatter(m: GameMap, rm: Rect2i, count: int, rmin: int, rspread: int, from: int, to: int) -> void:
	for i in count:
		blob(m, rm.position.x + r(rm.size.x), rm.position.y + r(rm.size.y), rmin + r(rspread), from, to)


static func carve_district(m: GameMap, rm: Rect2i, k: int) -> void:
	var area := rm.size.x * rm.size.y
	match k:
		C.District.JUNGLE:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 26, 1, 3, C.Tile.FLOOR, C.Tile.THICKET)
			scatter(m, rm, 1 + r(2), 2, 2, C.Tile.FLOOR, C.Tile.WATER)
			if r(2):
				scatter(m, rm, 1, 1, 2, C.Tile.FLOOR, C.Tile.MIASMA)
			scatter(m, rm, area / 40, 1, 2, C.Tile.FLOOR, C.Tile.GRASS)
			scatter(m, rm, area / 90, 1, 2, C.Tile.FLOOR, C.Tile.FLOWERS)
		C.District.SEA:
			fill_ragged(m, rm, C.Tile.WATER)
			for i in 2 + r(3):
				blob(m, rm.position.x + 2 + r(rm.size.x - 4), rm.position.y + 2 + r(rm.size.y - 4), 2 + r(3), C.Tile.WATER, C.Tile.FLOOR)
			var cy := rm.position.y + 1 + r(rm.size.y - 2)
			for x in range(rm.position.x, rm.end.x):
				if m.t(x, cy) == C.Tile.WATER:
					m.set_t(x, cy, C.Tile.BRIDGE)
			if r(100) < 65:
				var cx := rm.position.x + 1 + r(rm.size.x - 2)
				for y in range(rm.position.y, rm.end.y):
					if m.t(cx, y) == C.Tile.WATER:
						m.set_t(cx, y, C.Tile.BRIDGE)
			scatter(m, rm, area / 120, 1, 2, C.Tile.FLOOR, C.Tile.THICKET)
		C.District.SWAMP:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 34, 1, 3, C.Tile.FLOOR, C.Tile.WATER)
			scatter(m, rm, area / 64, 1, 2, C.Tile.FLOOR, C.Tile.MIASMA)
			scatter(m, rm, area / 46, 1, 2, C.Tile.FLOOR, C.Tile.THICKET)
			scatter(m, rm, area / 60, 1, 2, C.Tile.FLOOR, C.Tile.GRASS)
		C.District.RUINS:
			fill_ragged(m, rm, C.Tile.FLOOR)
			var built: Array = []
			var want := area / 55
			for tries in want * 8:
				if built.size() >= mini(want, 24):
					break
				var bw := 4 + r(5)
				var bh := 3 + r(4)
				if bw + 4 >= rm.size.x or bh + 4 >= rm.size.y:
					continue
				var b := Rect2i(rm.position.x + 1 + r(rm.size.x - bw - 2), rm.position.y + 1 + r(rm.size.y - bh - 2), bw, bh)
				var clash := false
				for o in built:
					if b.grow(1).intersects(o):
						clash = true
						break
				if clash:
					continue
				built.append(b)
				for y in range(b.position.y, b.end.y):
					for x in range(b.position.x, b.end.x):
						if x < 1 or y < 1 or x >= m.w - 1 or y >= m.h - 1:
							continue
						var edge := x == b.position.x or x == b.end.x - 1 or y == b.position.y or y == b.end.y - 1
						if edge:
							m.set_t(x, y, C.Tile.WALL if r(100) < 66 else C.Tile.FLOOR)
						elif r(100) < 22:
							m.set_t(x, y, C.Tile.DECOR)
			scatter(m, rm, area / 70, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
			if r(100) < 40:
				scatter(m, rm, 1, 2, 2, C.Tile.FLOOR, C.Tile.WATER)
		C.District.MYCELIUM:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 40, 1, 2, C.Tile.FLOOR, C.Tile.THICKET)
			scatter(m, rm, area / 55, 1, 3, C.Tile.FLOOR, C.Tile.GRASS)
			if r(100) < 60:
				scatter(m, rm, 1, 1, 2, C.Tile.FLOOR, C.Tile.MIASMA)
		C.District.HIVE:
			fill_ragged(m, rm, C.Tile.FLOOR)
			var cy := rm.position.y + 2
			while cy < rm.end.y - 2:
				var cx := rm.position.x + 2
				while cx < rm.end.x - 2:
					for y in range(cy, mini(cy + 3, rm.end.y - 1)):
						for x in range(cx, mini(cx + 4, rm.end.x - 1)):
							var edge := x == cx or x == cx + 3 or y == cy or y == cy + 2
							if edge and r(100) < 55 and x > 0 and y > 0 and x < m.w - 1 and y < m.h - 1:
								m.set_t(x, y, C.Tile.WALL)
					cx += 5
				cy += 4
			scatter(m, rm, area / 90, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.MIRE:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 30, 1, 3, C.Tile.FLOOR, C.Tile.WATER)
			scatter(m, rm, area / 70, 1, 2, C.Tile.FLOOR, C.Tile.MIASMA)
			scatter(m, rm, area / 50, 1, 2, C.Tile.FLOOR, C.Tile.THICKET)
		C.District.CRYSTAL:
			fill_ragged(m, rm, C.Tile.FLOOR)
			for v in 3 + r(4):
				var x := rm.position.x + 1 + r(rm.size.x - 2)
				var y := rm.position.y + 1 + r(rm.size.y - 2)
				var dx := r(3) - 1
				var dy := r(3) - 1
				if dx == 0 and dy == 0:
					dx = 1
				for i in 4 + r(10):
					if x < rm.position.x + 1 or x >= rm.end.x - 1 or y < rm.position.y + 1 or y >= rm.end.y - 1:
						break
					if m.t(x, y) == C.Tile.FLOOR and r(100) < 75:
						m.set_t(x, y, C.Tile.CRYSTAL)
					x += dx
					y += dy
			scatter(m, rm, area / 80, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.QUIET:
			fill_ragged(m, rm, C.Tile.FLOOR)
			for i in area / 70:
				var bw := 3 + r(5)
				var bh := 3 + r(4)
				if rm.size.x - bw - 2 <= 0 or rm.size.y - bh - 2 <= 0:
					continue
				var x0 := rm.position.x + 1 + r(rm.size.x - bw - 2)
				var y0 := rm.position.y + 1 + r(rm.size.y - bh - 2)
				for y in range(y0, y0 + bh):
					for x in range(x0, x0 + bw):
						if x >= 1 and y >= 1 and x < m.w - 1 and y < m.h - 1:
							m.set_t(x, y, C.Tile.WALL)
			scatter(m, rm, area / 100, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.BLOODMARSH:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 26, 2, 3, C.Tile.FLOOR, C.Tile.BLOODPOOL)
			scatter(m, rm, area / 70, 1, 2, C.Tile.FLOOR, C.Tile.THICKET)
		C.District.PRISM:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 45, 1, 3, C.Tile.FLOOR, C.Tile.PRISM_RED)
			scatter(m, rm, area / 45, 1, 3, C.Tile.FLOOR, C.Tile.PRISM_BLUE)
			scatter(m, rm, area / 60, 1, 2, C.Tile.FLOOR, C.Tile.PRISM_GREEN)
			scatter(m, rm, area / 90, 1, 2, C.Tile.FLOOR, C.Tile.CRYSTAL)
		C.District.GARDEN:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 30, 1, 2, C.Tile.FLOOR, C.Tile.SNARE)
			scatter(m, rm, area / 50, 1, 3, C.Tile.FLOOR, C.Tile.MIASMA)
			scatter(m, rm, area / 60, 1, 2, C.Tile.FLOOR, C.Tile.THICKET)
			scatter(m, rm, area / 40, 1, 2, C.Tile.FLOOR, C.Tile.FLOWERS)
		C.District.QUICKSAND:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 22, 2, 3, C.Tile.FLOOR, C.Tile.SNARE)
			scatter(m, rm, area / 90, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.STORM, C.District.GEOTHERMAL:
			# open ground: the decision is where you stand relative to the rods,
			# and you cannot make it if you cannot see them
			fill_ragged(m, rm, C.Tile.FLOOR)
			_plant(m, rm, 6 + area / 60, C.Tile.ROD if k == C.District.STORM else C.Tile.VENT)
			scatter(m, rm, area / 120, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.CHAPEL:
			fill_ragged(m, rm, C.Tile.FLOOR)
			for y in range(rm.position.y + 2, rm.end.y - 2, 3):
				for x in range(rm.position.x + 3, rm.end.x - 3, 4):
					if _inb(m, x, y) and m.t(x, y) == C.Tile.FLOOR:
						m.set_t(x, y, C.Tile.WALL)
			scatter(m, rm, area / 150, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.ARENA:
			# an amphitheatre: a ring of seating round open sand, one gate
			var c := center(rm)
			var rx := maxi(4, rm.size.x / 2 - 1)
			var ry := maxi(3, rm.size.y / 2 - 1)
			for y in range(rm.position.y, rm.end.y):
				for x in range(rm.position.x, rm.end.x):
					if not _inb(m, x, y):
						continue
					var e := (x - c.x) * (x - c.x) * ry * ry + (y - c.y) * (y - c.y) * rx * rx
					var lim := rx * rx * ry * ry
					if e <= lim:
						m.set_t(x, y, C.Tile.FLOOR)
					elif e <= lim * 9 / 5:
						m.set_t(x, y, C.Tile.WALL)
			for x in range(c.x, rm.end.x):
				if _inb(m, x, c.y):
					m.set_t(x, c.y, C.Tile.FLOOR)
		C.District.AQUEDUCT:
			fill_ragged(m, rm, C.Tile.FLOOR)
			var horizontal := rm.size.x >= rm.size.y
			for ch in 2 + r(3):
				var wide := 1 + int(r(100) < 40)
				if horizontal:
					var y := rm.position.y + 2 + r(maxi(1, rm.size.y - 4))
					for k2 in wide:
						for x in range(rm.position.x + 1, rm.end.x - 1):
							if _inb(m, x, y + k2):
								m.set_t(x, y + k2, C.Tile.CURRENT)
				else:
					var x := rm.position.x + 2 + r(maxi(1, rm.size.x - 4))
					for k2 in wide:
						for y in range(rm.position.y + 1, rm.end.y - 1):
							if _inb(m, x + k2, y):
								m.set_t(x + k2, y, C.Tile.CURRENT)
			scatter(m, rm, area / 90, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.ASSEMBLY:
			fill_ragged(m, rm, C.Tile.FLOOR)
			for y in range(rm.position.y + 2, rm.end.y - 2, 3):
				for x in range(rm.position.x + 1, rm.end.x - 1):
					if _inb(m, x, y) and m.t(x, y) == C.Tile.FLOOR:
						m.set_t(x, y, C.Tile.BELT)
			scatter(m, rm, area / 80, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.WATCH:
			# blocks in rows with lanes between: always something to put
			# between you and a warden
			fill_ragged(m, rm, C.Tile.FLOOR)
			for by in range(rm.position.y + 2, rm.end.y - 3, 4):
				for bx in range(rm.position.x + 2, rm.end.x - 3, 5):
					if r(100) < 22:
						continue
					var bw := 2 + r(2)
					var bh := 1 + r(2)
					for y in range(by, by + bh):
						for x in range(bx, bx + bw):
							if _inb(m, x, y):
								m.set_t(x, y, C.Tile.WALL)
			scatter(m, rm, area / 70, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.GAUNTLET:
			# the barrow: a walled hall with piers down the long sides, one door
			_walled(m, rm, 100)
			for x in range(rm.position.x + 3, rm.end.x - 3, 4):
				for y in [rm.position.y + 2, rm.end.y - 3]:
					if _inb(m, x, y):
						m.set_t(x, y, C.Tile.WALL)
			var gy := rm.position.y + rm.size.y / 2
			if _inb(m, rm.end.x - 1, gy):
				m.set_t(rm.end.x - 1, gy, C.Tile.FLOOR)
		C.District.EYE:
			# open ground marked into quarters by two lines of rubble, so the
			# shelter has visible edges
			fill_ragged(m, rm, C.Tile.FLOOR)
			var mc := center(rm)
			for x in range(rm.position.x + 1, rm.end.x - 1):
				if _inb(m, x, mc.y) and r(100) < 62:
					m.set_t(x, mc.y, C.Tile.DECOR)
			for y in range(rm.position.y + 1, rm.end.y - 1):
				if _inb(m, mc.x, y) and r(100) < 62:
					m.set_t(mc.x, y, C.Tile.DECOR)
		C.District.MIRROR:
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 110, 1, 2, C.Tile.FLOOR, C.Tile.CRYSTAL)
			scatter(m, rm, area / 130, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.PETRIFIED:
			# stone trees in loose stands: all blind corners, no corridors
			fill_ragged(m, rm, C.Tile.FLOOR)
			for i in area / 30:
				var cx := rm.position.x + 1 + r(rm.size.x - 2)
				var cy := rm.position.y + 1 + r(rm.size.y - 2)
				for n in 1 + r(3):
					var x := cx + r(3) - 1
					var y := cy + r(3) - 1
					if _inb(m, x, y) and m.t(x, y) == C.Tile.FLOOR:
						m.set_t(x, y, C.Tile.WALL)
			scatter(m, rm, area / 100, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.SHAFT:
			# a worked-out mine: galleries, spoil, and holes that go somewhere
			fill_ragged(m, rm, C.Tile.FLOOR)
			for y in range(rm.position.y + 2, rm.end.y - 2, 3):
				for x in range(rm.position.x + 2, rm.end.x - 2, 4):
					if _inb(m, x, y) and r(100) < 45:
						m.set_t(x, y, C.Tile.WALL)
			_plant(m, rm, 1 + area / 260, C.Tile.PIT)
			scatter(m, rm, area / 90, 1, 2, C.Tile.FLOOR, C.Tile.ORE)
			scatter(m, rm, area / 110, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
		C.District.BONEYARD:
			# half-buried machines: the rubble is the salvage
			fill_ragged(m, rm, C.Tile.FLOOR)
			scatter(m, rm, area / 24, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)
			for i in area / 70:
				var x := rm.position.x + 1 + r(rm.size.x - 2)
				var y := rm.position.y + 1 + r(rm.size.y - 2)
				if _inb(m, x, y) and m.t(x, y) == C.Tile.FLOOR:
					m.set_t(x, y, C.Tile.WALL)
		C.District.PROVING:
			# a swept floor with a fence round it, not a seal: you can always walk out
			_walled(m, rm, 62)
			scatter(m, rm, area / 120, 1, 2, C.Tile.FLOOR, C.Tile.DECOR)


static func district_anchor(m: GameMap, rm: Rect2i) -> Vector2i:
	var c := center(rm)
	for rad in rm.size.x + rm.size.y:
		for y in range(c.y - rad, c.y + rad + 1):
			for x in range(c.x - rad, c.x + rad + 1):
				if x < 1 or y < 1 or x >= m.w - 1 or y >= m.h - 1:
					continue
				var tt := m.t(x, y)
				if tt == C.Tile.FLOOR or tt == C.Tile.BRIDGE:
					return Vector2i(x, y)
	return Vector2i(-1, -1)


## Cut trails from every stranded pocket in a district back to its anchor,
## so nothing inside one is unreachable -- district_relink() in C.
static func district_relink(m: GameMap, rm: Rect2i, anchor: Vector2i) -> void:
	for pass_ in 12:
		var seen := {}
		var stack: Array = [anchor]
		seen[anchor] = 1
		while not stack.is_empty():
			var c: Vector2i = stack.pop_back()
			for d in [Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]:
				var n: Vector2i = c + d
				if not rm.has_point(n) or seen.has(n) or not m.walkable_player(n.x, n.y):
					continue
				seen[n] = 1
				stack.append(n)
		var cut := 0
		for y in range(rm.position.y, rm.end.y):
			for x in range(rm.position.x, rm.end.x):
				var p := Vector2i(x, y)
				if seen.has(p) or not m.walkable_player(x, y):
					continue
				# measure the pocket and find its point nearest the anchor
				var pocket: Array = [p]
				var near := p
				var near_d := absi(p.x - anchor.x) + absi(p.y - anchor.y)
				seen[p] = 2
				var i := 0
				while i < pocket.size():
					var c: Vector2i = pocket[i]
					i += 1
					var dd := absi(c.x - anchor.x) + absi(c.y - anchor.y)
					if dd < near_d:
						near_d = dd
						near = c
					for d in [Vector2i(1, 0), Vector2i(-1, 0), Vector2i(0, 1), Vector2i(0, -1)]:
						var n: Vector2i = c + d
						if rm.has_point(n) and not seen.has(n) and m.walkable_player(n.x, n.y):
							seen[n] = 2
							pocket.append(n)
				if pocket.size() < 2:
					continue
				var q := near
				for steps in rm.size.x + rm.size.y + 4:
					if seen.get(q, 0) == 1 and m.walkable_player(q.x, q.y):
						break
					_strip(m, q.x, q.y)
					if q == anchor:
						break
					if absi(anchor.x - q.x) >= absi(anchor.y - q.y) and q.x != anchor.x:
						q.x += 1 if anchor.x > q.x else -1
					elif q.y != anchor.y:
						q.y += 1 if anchor.y > q.y else -1
				cut += 1
		if cut == 0:
			return


# ---- the floor -------------------------------------------------------------------
static func generate(floor_num: int, size: Vector2i, run_seed: int, difficulty: int) -> GameMap:
	rng.seed = floor_seed(run_seed, floor_num)
	var m := GameMap.new()
	m.setup(size.x, size.y, C.Tile.WALL)
	m.floor_num = floor_num
	m.biome = C.biome_for_floor(floor_num)
	var boss_floor := floor_num >= C.MAX_FLOOR
	var occ := PackedByteArray()
	occ.resize(m.w * m.h)

	if not boss_floor:
		var params: Array = [[2, 4, 0, 0, 0, 1], [0, 1, 3, 6, 1, 3], [2, 4, 0, 1, 1, 2], [0, 1, 1, 2, 3, 6], [1, 2, 3, 6, 3, 6]][m.biome]
		var sc := sqrt(area_scale(m))
		for i in int((params[0] + r(params[1] - params[0] + 1)) * sc):
			blob(m, 6 + r(m.w - 12), 6 + r(m.h - 12), 3 + r(4), C.Tile.WALL, C.Tile.WATER)
		for i in int((params[2] + r(params[3] - params[2] + 1)) * sc):
			blob(m, 6 + r(m.w - 12), 6 + r(m.h - 12), 2 + r(3), C.Tile.WALL, C.Tile.LAVA)

	# wild districts, before the rooms so the rooms go around them
	var dists: Array = []
	var anchors: Array = []
	if not boss_floor:
		var mult := maxf(area_scale(m), 1.0)
		var typical := maxi(1, int(DIST_PER_10K * sqrt(mult)))
		var size_pct := maxi(100, int(100.0 * sqrt(sqrt(mult))))
		var roll := r(100)
		var want := typical / 2 if roll < 20 else (typical if roll < 80 else typical * 2)
		want = maxi(want, 2)
		var tries := 0
		while tries < 60 * want + 400 and dists.size() < want:
			tries += 1
			var dw := (20 + r(22)) * size_pct / 100
			var dh := (12 + r(15)) * size_pct / 100
			if dw >= m.w - 6 or dh >= m.h - 6:
				continue
			var d := Rect2i(2 + r(m.w - dw - 4), 2 + r(m.h - dh - 4), dw, dh)
			var clash := false
			for o in dists:
				if d.grow(1).intersects(o):
					clash = true
					break
			if clash:
				continue
			var k := pick_district_kind(m.biome)
			carve_district(m, d, k)
			var a := district_anchor(m, d)
			if a.x < 0:
				for y in range(d.position.y, d.end.y):
					for x in range(d.position.x, d.end.x):
						m.set_t(x, y, C.Tile.WALL)
				continue
			# the arena and the barrow are meant to have one way in
			if not k in [C.District.ARENA, C.District.GAUNTLET]:
				district_relink(m, d, a)
			var di := m.districts.size()
			var rec := {"x": d.position.x, "y": d.position.y, "w": d.size.x, "h": d.size.y, "kind": k, "state": 0,
				"fdx": 0, "fdy": 0, "kills": 0}
			if k == C.District.AQUEDUCT:
				# one way for the whole district: a channel that changed its mind would be noise
				var dir := 1 if r(2) else -1
				if d.size.x >= d.size.y:
					rec.fdx = dir
				else:
					rec.fdy = dir
			if k == C.District.PROVING:
				rec.state = r(3)      # the rule: a property of the place, said on arrival
			m.districts.append(rec)
			for y in range(d.position.y, d.end.y):
				for x in range(d.position.x, d.end.x):
					m.district_id[y * m.w + x] = di
			dists.append(d)
			anchors.append(a)

	# the camp: one floor in six
	if not boss_floor and r(6) == 0:
		for tries in 40:
			var hw := 14 + r(8)
			var hh := 9 + r(5)
			var hv := Rect2i(2 + r(m.w - hw - 4), 2 + r(m.h - hh - 4), hw, hh)
			var clash := false
			for o in dists:
				if hv.grow(1).intersects(o):
					clash = true
					break
			if not clash:
				m.haven = hv
				break

	# occupancy: everything rooms must avoid, dilated by one
	var mark := func(rc: Rect2i):
		for y in range(maxi(0, rc.position.y - 1), mini(m.h, rc.end.y + 1)):
			for x in range(maxi(0, rc.position.x - 1), mini(m.w, rc.end.x + 1)):
				occ[y * m.w + x] = 1
	for y in m.h:
		for x in m.w:
			var tt := m.t(x, y)
			if tt == C.Tile.WATER or tt == C.Tile.LAVA:
				occ[y * m.w + x] = 1
	for d in dists:
		mark.call(d)
	if m.haven.size.x > 0:
		mark.call(m.haven)

	var rooms: Array = []
	var hubs := {}
	var budget := mini(scale_by_area(m, 30000) / ROOM_SPECS.size(), 6000)
	for kind in ROOM_SPECS.size():
		var sp: Array = ROOM_SPECS[kind]
		var want := maxi(1, int(sp[0] * m.w * m.h / 10000))
		var placed := 0
		var attempts := 0
		while placed < want and attempts < budget:
			attempts += 1
			var rw: int = sp[1] + r(sp[2] - sp[1] + 1)
			var rh: int = sp[3] + r(sp[4] - sp[3] + 1)
			if kind == ROOM_HALL and r(2) == 1:
				var tmp := rw
				rw = rh
				rh = tmp
			if rw >= m.w - 3 or rh >= m.h - 3:
				continue
			var rx := 1 + r(m.w - rw - 2)
			var ry := 1 + r(m.h - rh - 2)
			var free := true
			for y in range(ry, ry + rh):
				for x in range(rx, rx + rw):
					if occ[y * m.w + x]:
						free = false
						break
				if not free:
					break
			if not free:
				continue
			var rc := Rect2i(rx, ry, rw, rh)
			mark.call(rc)
			if kind == ROOM_HUB:
				hubs[rooms.size()] = true
			rooms.append(rc)
			placed += 1
	if rooms.size() < 2:
		rooms = [Rect2i(2, 2, 6, 6), Rect2i(m.w - 10, m.h - 10, 6, 6)]
	for rc in rooms:
		carve_room(m, rc)

	# nearest-first spanning tree (Prim's), then a few short extra links
	var n := rooms.size()
	var joined := PackedByteArray()
	joined.resize(n)
	var best_d := PackedInt64Array()
	best_d.resize(n)
	var best_a := PackedInt32Array()
	best_a.resize(n)
	var cs: Array = []
	for rc in rooms:
		cs.append(center(rc))
	joined[0] = 1
	for i in range(1, n):
		var dd: Vector2i = cs[i] - cs[0]
		best_d[i] = dd.x * dd.x + dd.y * dd.y
	for done in range(1, n):
		var pick := -1
		for i in range(1, n):
			if not joined[i] and (pick < 0 or best_d[i] < best_d[pick]):
				pick = i
		if pick < 0:
			break
		join(m, rooms[best_a[pick]], rooms[pick])
		joined[pick] = 1
		for i in range(1, n):
			if joined[i]:
				continue
			var dd: Vector2i = cs[i] - cs[pick]
			var d2 := dd.x * dd.x + dd.y * dd.y
			if d2 < best_d[i]:
				best_d[i] = d2
				best_a[i] = pick
	for i in n / 6:
		var a := r(n)
		var b := nearest_room(rooms, a, 1 + r(4))
		if b >= 0:
			join(m, rooms[a], rooms[b])
	for a in anchors:
		for k in 2:
			join(m, Rect2i(a, Vector2i.ONE), rooms[r(n)])
	for i in hubs:
		for k in 3 + r(2):
			var b := nearest_room(rooms, i, k + 1)
			if b >= 0:
				join(m, rooms[i], rooms[b], 2 + int(r(100) < 35))

	# the camp: a palisade with gates, built after the roads so they stop at it
	if m.haven.size.x > 0:
		_carve_haven(m, m.haven)

	m.stairs_up = center(rooms[0])
	m.set_t(m.stairs_up.x, m.stairs_up.y, C.Tile.STAIRS_UP)
	var down := center(rooms[n - 1])
	if not boss_floor:
		m.stairs_down = down
		m.set_t(down.x, down.y, C.Tile.STAIRS_DOWN)

	if not boss_floor:
		var mp: Array = [[0, 1], [1, 3], [1, 2], [3, 6], [3, 6]][m.biome]
		for i in int((mp[0] + r(mp[1] - mp[0] + 1)) * sqrt(area_scale(m))):
			var rc: Rect2i = rooms[1 + r(n - 1)]
			blob(m, rc.position.x + r(rc.size.x), rc.position.y + r(rc.size.y), 1 + r(2), C.Tile.FLOOR, C.Tile.MIASMA)

	_place_monsters(m, rooms, dists, difficulty, boss_floor, down)
	_place_items(m, rooms, dists, boss_floor, down)
	if not boss_floor:
		_place_features(m, rooms, floor_num)
		_random_event(m, rooms, floor_num)

	# the invariant: the stairs connect
	if not boss_floor and not m.reachable(m.stairs_up, m.stairs_down):
		join(m, rooms[0], rooms[n - 1], 1)
		m.set_t(m.stairs_up.x, m.stairs_up.y, C.Tile.STAIRS_UP)
		m.set_t(m.stairs_down.x, m.stairs_down.y, C.Tile.STAIRS_DOWN)
	if m.haven.size.x > 0:
		var hc := center(m.haven)
		if not m.reachable(m.stairs_up, hc):
			join(m, Rect2i(hc, Vector2i.ONE), rooms[0], 1)
			m.set_t(m.stairs_up.x, m.stairs_up.y, C.Tile.STAIRS_UP)
	# nothing may be generated standing in a wall a later pass put there
	for mo in m.monsters:
		if not m.walkable_player(mo.x, mo.y):
			mo.alive = false
	m.remove_dead()
	return m


static func _carve_haven(m: GameMap, hv: Rect2i) -> void:
	for y in range(hv.position.y, hv.end.y):
		for x in range(hv.position.x, hv.end.x):
			var edge := x == hv.position.x or x == hv.end.x - 1 or y == hv.position.y or y == hv.end.y - 1
			m.set_t(x, y, C.Tile.WALL if edge else (C.Tile.GRASS if r(5) == 0 else C.Tile.FLOOR))
	var c := center(hv)
	var gates := [Vector2i(c.x, hv.position.y), Vector2i(c.x, hv.end.y - 1), Vector2i(hv.position.x, c.y), Vector2i(hv.end.x - 1, c.y)]
	var first := r(4)
	for k in 3:
		var g: Vector2i = gates[(first + k) % 4]
		m.set_t(g.x, g.y, C.Tile.FLOOR)
		# a road out of the gate until it meets open ground
		var dir := Vector2i(signi(g.x - c.x), signi(g.y - c.y))
		var p: Vector2i = g + dir
		for s in 40:
			if not m.inb(p.x, p.y) or p.x < 1 or p.y < 1 or p.x >= m.w - 1 or p.y >= m.h - 1:
				break
			if m.walkable_player(p.x, p.y):
				break
			_strip(m, p.x, p.y)
			p += dir


static func _monster_cut(mo: Monster, heavy: bool, moderate: bool, overrun: bool) -> void:
	if heavy:
		mo.xp_reward = maxi(1, mo.xp_reward / 5)
		mo.gold_reward = maxi(1, mo.gold_reward / 5)
		if overrun:
			mo.xp_reward = maxi(1, mo.xp_reward / 4)
	elif moderate:
		mo.xp_reward = maxi(1, mo.xp_reward / 2)
		mo.gold_reward = maxi(1, mo.gold_reward / 2)


static func _place_monsters(m: GameMap, rooms: Array, dists: Array, difficulty: int, boss_floor: bool, down: Vector2i) -> void:
	var floor_num := m.floor_num
	var heavy := (difficulty == C.Difficulty.SWARM or difficulty == C.Difficulty.HARDCORE) and not boss_floor
	var moderate := difficulty == C.Difficulty.HARD and not boss_floor
	m.overrun = moderate and r(8) == 0
	if m.overrun:
		heavy = true
		moderate = false
	m.gold_rush = not boss_floor and r(8) == 0
	var mult := 1
	if heavy:
		mult = C.SWARM_FLOOR1_DENSITY + floor_num * 11 / 20
		if difficulty == C.Difficulty.HARDCORE:
			mult = mult * C.HARDCORE_DENSITY_PCT / 100
		mult = clampi(mult, 1, 40) + r(3)
	elif moderate:
		mult = clampi(C.HARD_FLOOR1_DENSITY + floor_num / 20, 1, 8)
	m.spawn_density = mult
	var chance := 100 if heavy else (80 if moderate else 65)
	var budget := scale_by_area(m, 58) * mult
	var up := m.stairs_up
	var try_place := func(mx: int, my: int) -> bool:
		if Vector2i(mx, my) == up or (Vector2i(mx, my) == down and not boss_floor):
			return false
		if m.in_haven(mx, my) or m.t(mx, my) != C.Tile.FLOOR or m.monster_at(mx, my):
			return false
		var mo := Monster.for_floor(floor_num, mx, my, rng)
		_monster_cut(mo, heavy, moderate, m.overrun)
		m.add_monster(mo)
		return true
	for i in range(1, rooms.size()):
		if m.monsters.size() >= budget:
			break
		if r(100) < chance:
			var rc: Rect2i = rooms[i]
			for k in (1 + r(2)) * mult:
				if m.monsters.size() >= budget:
					break
				try_place.call(rc.position.x + r(rc.size.x), rc.position.y + r(rc.size.y))
	for p in 12:
		var before := m.monsters.size()
		for i in range(1, rooms.size()):
			if m.monsters.size() >= budget:
				break
			if r(100) >= chance:
				continue
			var rc: Rect2i = rooms[i]
			try_place.call(rc.position.x + r(rc.size.x), rc.position.y + r(rc.size.y))
		if m.monsters.size() == before or m.monsters.size() >= budget:
			break
	for d in dists:
		for k in (3 + r(4)) * mult:
			for tries in 12:
				if try_place.call(d.position.x + r(d.size.x), d.position.y + r(d.size.y)):
					break
	var last: Rect2i = rooms[rooms.size() - 1]
	if floor_num % 10 == 0 and not boss_floor:
		for tries in 20:
			var p := Vector2i(last.position.x + r(last.size.x), last.position.y + r(last.size.y))
			if p != down and not m.monster_at(p.x, p.y) and m.t(p.x, p.y) == C.Tile.FLOOR:
				m.add_monster(Monster.elite_for_floor(floor_num, p.x, p.y, rng))
				break
	if Monster.is_biome_boss_floor(floor_num):
		for tries in 20:
			var p := Vector2i(last.position.x + r(last.size.x), last.position.y + r(last.size.y))
			if p != down and not m.monster_at(p.x, p.y) and m.t(p.x, p.y) == C.Tile.FLOOR:
				m.add_monster(Monster.make_biome_boss(floor_num, p.x, p.y))
				break
	if boss_floor:
		m.add_monster(Monster.warden(down.x, down.y))


static func _gold_amount(m: GameMap) -> int:
	var f := m.floor_num
	var g := 3 + f / 2 + r(f + 5)
	return g * C.GOLD_RUSH_MULT if m.gold_rush else g


static func _loot_item(x: int, y: int, floor_num: int) -> Dictionary:
	var t := ItemsData.pick_floor_loot(rng, floor_num)
	return {"x": x, "y": y, "kind": "consumable", "name": t.name}


static func _place_items(m: GameMap, rooms: Array, dists: Array, boss_floor: bool, down: Vector2i) -> void:
	var budget := scale_by_area(m, 34)
	var gold_chance := 55
	var passes := 1
	if m.gold_rush:
		budget *= C.GOLD_RUSH_MULT
		gold_chance = 90
		passes = C.GOLD_RUSH_MULT
	budget = mini(budget, scale_by_area(m, 140))
	for p in passes:
		for i in range(1, rooms.size()):
			if m.items.size() >= budget:
				break
			if r(100) >= 55:
				continue
			var rc: Rect2i = rooms[i]
			for tries in 20:
				var x := rc.position.x + r(rc.size.x)
				var y := rc.position.y + r(rc.size.y)
				if Vector2i(x, y) == m.stairs_up or m.monster_at(x, y) or m.t(x, y) != C.Tile.FLOOR:
					continue
				if r(100) < gold_chance:
					m.items.append({"x": x, "y": y, "kind": "gold", "amount": _gold_amount(m)})
				else:
					m.items.append(_loot_item(x, y, m.floor_num))
				break
	for d in dists:
		for k in 2 + r(2):
			for tries in 20:
				var x: int = d.position.x + r(d.size.x)
				var y: int = d.position.y + r(d.size.y)
				if m.t(x, y) != C.Tile.FLOOR or Vector2i(x, y) == m.stairs_up or m.monster_at(x, y):
					continue
				if r(100) < 55:
					m.items.append({"x": x, "y": y, "kind": "gold", "amount": _gold_amount(m)})
				else:
					m.items.append(_loot_item(x, y, m.floor_num))
				break
	if m.floor_num % 10 == 0 and not boss_floor:
		var lr: Rect2i = rooms[rooms.size() - 1]
		var p := lr.position
		if p == down:
			p.x = lr.end.x - 1
		m.items.append({"x": p.x, "y": p.y, "kind": "gold", "amount": 30 + m.floor_num * 2})
	if Game.quest_wants_fetch(m.floor_num):
		for tries in 20:
			var qr: Rect2i = rooms[1 + r(rooms.size() - 1)]
			var x := qr.position.x + r(qr.size.x)
			var y := qr.position.y + r(qr.size.y)
			if Vector2i(x, y) == down or Vector2i(x, y) == m.stairs_up or m.t(x, y) != C.Tile.FLOOR:
				continue
			m.items.append({"x": x, "y": y, "kind": "quest", "name": Game.quest.get("item", "a lost ledger")})
			break


static func _place_feature(m: GameMap, rooms: Array, type: int) -> void:
	if rooms.size() < 2:
		return
	for tries in 40:
		var rc: Rect2i = rooms[1 + r(rooms.size() - 1)]
		var x := rc.position.x + r(rc.size.x)
		var y := rc.position.y + r(rc.size.y)
		if m.t(x, y) != C.Tile.FLOOR or Vector2i(x, y) == m.stairs_up or Vector2i(x, y) == m.stairs_down:
			continue
		if m.monster_at(x, y) or not m.feature_at(x, y).is_empty():
			continue
		var f := {"x": x, "y": y, "type": type, "used": false}
		if type == C.Feature.RELIC:
			f.relic_kind = r(4)
			f.relic_set = m.biome
		m.features.append(f)
		return


static func _place_in(m: GameMap, rc: Rect2i, type: int) -> void:
	for tries in 40:
		var x := rc.position.x + 1 + r(maxi(1, rc.size.x - 2))
		var y := rc.position.y + 1 + r(maxi(1, rc.size.y - 2))
		if m.t(x, y) in [C.Tile.FLOOR, C.Tile.GRASS] and m.feature_at(x, y).is_empty():
			m.features.append({"x": x, "y": y, "type": type, "used": false})
			return


static func _vault(m: GameMap, rooms: Array, floor_num: int, sealed: bool) -> void:
	for tries in 20:
		var ri := 1 + r(rooms.size() - 1)
		var rc: Rect2i = rooms[ri]
		if rc.size.x < 6 or rc.size.y < 5:
			continue
		var vx := rc.position.x + 1
		var vy := rc.position.y + 1
		if vx + 3 >= rc.end.x - 1 or vy + 3 >= rc.end.y - 1:
			continue
		if center(rc) == m.stairs_down or center(rc) == m.stairs_up:
			continue
		for yy in range(vy - 1, vy + 4):
			for xx in range(vx - 1, vx + 4):
				if yy == vy - 1 or yy == vy + 3 or xx == vx - 1 or xx == vx + 3:
					m.set_t(xx, yy, C.Tile.WALL)
		var door := Vector2i(vx + 3, vy + 1)
		m.set_t(door.x, door.y, C.Tile.SEALED_DOOR if sealed else C.Tile.LOCKED_DOOR)
		var amount := (90 + floor_num * 3 + r(70)) if sealed else (80 + floor_num * 3 + r(60))
		if m.gold_rush:
			amount *= C.GOLD_RUSH_MULT
		m.items.append({"x": vx + 1, "y": vy + 1, "kind": "gold", "amount": amount})
		var ki := ri
		while ki == ri and rooms.size() > 2:
			ki = 1 + r(rooms.size() - 1)
		var kr: Rect2i = rooms[ki]
		var kp := Vector2i(kr.position.x + r(kr.size.x), kr.position.y + r(kr.size.y))
		if sealed:
			m.set_t(kp.x, kp.y, C.Tile.LEVER)
			m.lever = kp
			m.lever_door = door
		else:
			m.items.append({"x": kp.x, "y": kp.y, "kind": "key", "name": "Brass Key"})
		return


static func _place_features(m: GameMap, rooms: Array, floor_num: int) -> void:
	# the districts' own fittings, inside them where they belong
	for d in m.districts:
		var rc := Rect2i(d.x, d.y, d.w, d.h)
		if d.kind == C.District.ASSEMBLY:
			_place_in(m, rc, C.Feature.CONSOLE)
		elif d.kind == C.District.WATCH:
			_place_in(m, rc, C.Feature.STRONGBOX)
	# ore veins, in walls you can reach: every depth has to be able to supply
	# its own tier of material, so these are not left to a district
	for i in 6 + floor_num / 6:
		for tries in 30:
			var x := 1 + r(m.w - 2)
			var y := 1 + r(m.h - 2)
			if m.t(x, y) != C.Tile.WALL or m.district_id[y * m.w + x] >= 0:
				continue
			var reach := false
			for dd in C.DIRS8:
				if m.walkable_player(x + dd.x, y + dd.y):
					reach = true
					break
			if reach:
				m.set_t(x, y, C.Tile.ORE)
				break
	if m.haven.size.x > 0:
		_place_in(m, m.haven, C.Feature.FOUNTAIN)
		if r(100) < 75:
			_place_in(m, m.haven, C.Feature.MERCHANT)
		if r(100) < 35:
			_place_in(m, m.haven, C.Feature.SHRINE)
	if r(100) < 18:
		_vault(m, rooms, floor_num, false)
	if r(100) < (35 if m.biome == C.Biome.INDUSTRIAL else 15):
		_vault(m, rooms, floor_num, true)
	if floor_num % Game.waygate_interval() == 0:
		var rc: Rect2i = rooms[0]
		for tries in 40:
			var x := rc.position.x + r(rc.size.x)
			var y := rc.position.y + r(rc.size.y)
			if m.t(x, y) == C.Tile.FLOOR and Vector2i(x, y) != m.stairs_up:
				m.features.append({"x": x, "y": y, "type": C.Feature.TOWN_GATE, "used": false})
				break
	if r(100) < (10 if floor_num <= 85 else 3):
		_place_feature(m, rooms, C.Feature.MERCHANT)
	if r(100) < 10:
		_place_feature(m, rooms, C.Feature.FOUNTAIN)
	if r(100) < (12 if m.biome == C.Biome.RUINS or m.biome == C.Biome.ABYSS else 5):
		_place_feature(m, rooms, C.Feature.SHRINE)
	if r(100) < (15 if m.biome == C.Biome.INDUSTRIAL else 5):
		_place_feature(m, rooms, C.Feature.MACHINE)
	var elite_floor := floor_num % 10 == 0
	if elite_floor or r(100) < 6:
		_place_feature(m, rooms, C.Feature.RELIC)
	if elite_floor:
		_place_feature(m, rooms, C.Feature.ALTAR)
	if Game.waygate_interval() == 1 and r(100) < 35:
		_place_feature(m, rooms, C.Feature.TOLL)
	if rooms.size() >= 4 and r(100) < 8:
		var ra := 1 + r(rooms.size() - 1)
		var rb := ra
		while rb == ra:
			rb = 1 + r(rooms.size() - 1)
		var a: Rect2i = rooms[ra]
		var b: Rect2i = rooms[rb]
		var pa := Vector2i(a.position.x + r(a.size.x), a.position.y + r(a.size.y))
		var pb := Vector2i(b.position.x + r(b.size.x), b.position.y + r(b.size.y))
		if m.t(pa.x, pa.y) == C.Tile.FLOOR and m.t(pb.x, pb.y) == C.Tile.FLOOR and not m.monster_at(pa.x, pa.y) and not m.monster_at(pb.x, pb.y):
			m.set_t(pa.x, pa.y, C.Tile.PORTAL)
			m.set_t(pb.x, pb.y, C.Tile.PORTAL)
			m.portal_a = pa
			m.portal_b = pb


static func _random_event(m: GameMap, rooms: Array, floor_num: int) -> void:
	if rooms.size() < 3 or r(100) >= 45:
		return
	var pick := r(9)
	if (pick == 0 or pick == 6) and r(100) < Game.hero.ambush_resist_pct:
		pick = 8
	var rc: Rect2i = rooms[1 + r(rooms.size() - 1)]
	match pick:
		0:
			m.event_name = "Ambush!"
			m.event_desc = "Something was waiting for you."
			for k in 3 + r(3):
				var x := rc.position.x + r(rc.size.x)
				var y := rc.position.y + r(rc.size.y)
				if m.monster_at(x, y) or m.t(x, y) != C.Tile.FLOOR:
					continue
				var mo := Monster.for_floor(floor_num, x, y, rng)
				mo.aggro = true
				m.add_monster(mo)
		1:
			m.event_name = "Trader's Camp"
			m.event_desc = "Someone set up shop down here."
			_place_feature(m, rooms, C.Feature.MERCHANT)
		2:
			m.event_name = "Old Ones Shrine"
			m.event_desc = "Something here still remembers being worshipped."
			_place_feature(m, rooms, C.Feature.SHRINE)
		3:
			m.event_name = "Flooded Passage"
			m.event_desc = "Water found a way in a long time ago."
			blob(m, 5 + r(m.w - 10), 5 + r(m.h - 10), 3 + r(3), C.Tile.WALL, C.Tile.WATER)
		4:
			m.event_name = "Toxic Vent Rupture"
			m.event_desc = "The air here tastes like a struck match."
			for i in 2:
				var rr: Rect2i = rooms[1 + r(rooms.size() - 1)]
				blob(m, rr.position.x + r(rr.size.x), rr.position.y + r(rr.size.y), 1 + r(2), C.Tile.FLOOR, C.Tile.MIASMA)
		5:
			m.event_name = "Buried Cache"
			m.event_desc = "Somebody hid something down here and never came back for it."
			_place_feature(m, rooms, C.Feature.RELIC)
		6:
			m.event_name = "Raiders' Den"
			m.event_desc = "A whole band of them, working together."
			var proto := Monster.for_floor(floor_num, rc.position.x, rc.position.y, rng)
			for k in 3 + r(2):
				var x := rc.position.x + r(rc.size.x)
				var y := rc.position.y + r(rc.size.y)
				if m.monster_at(x, y) or m.t(x, y) != C.Tile.FLOOR:
					continue
				var mo := Monster.from_dict(proto.to_dict())
				mo.x = x
				mo.y = y
				mo.aggro = true
				m.add_monster(mo)
			m.items.append({"x": rc.position.x + r(rc.size.x), "y": rc.position.y + r(rc.size.y), "kind": "gold", "amount": 20 + floor_num})
		7:
			m.event_name = "Ancient Machinery Awakens"
			m.event_desc = "Something down here is still, technically, running."
			_place_feature(m, rooms, C.Feature.MACHINE)
		_:
			m.event_name = "Eerie Calm"
			m.event_desc = "Whatever usually lives here isn't home."
			for mo in m.monsters:
				if r(100) < 40:
					mo.alive = false
			m.remove_dead()
			m.items.append(_loot_item(rc.position.x + r(rc.size.x), rc.position.y + r(rc.size.y), floor_num))
