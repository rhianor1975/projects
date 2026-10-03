class_name Town
## The city over the temple: one plaza, the buildings around it, and the way
## down in the middle. The C game's town is a 58x18 character grid; this one
## is laid out for 32px tiles and a 3/4 view -- roofs, then a front wall with
## the door in it -- but holds the same buildings doing the same jobs.

const W := 40
const H := 18
const START := Vector2i(20, 10)
const TEMPLE := Vector2i(20, 8)

# id, label, x, y, w, h, roof style
const BUILDINGS := [
	["general", "General Store", 2, 2, 7, 4, "roof_red"],
	["armory", "Armory", 10, 2, 7, 4, "roof_slate"],
	["apothecary", "Apothecary", 18, 1, 5, 5, "roof_teal"],
	["arcanist", "Arcanist's Guild", 24, 2, 7, 4, "roof_teal"],
	["bank", "Bank of the Deep Well", 32, 1, 6, 5, "roof_slate"],
	["inn", "The Inn", 2, 12, 7, 4, "roof_red"],
	["gladiator", "Gladiator School", 10, 12, 7, 4, "roof_red"],
	["oracle", "The Oracle", 18, 13, 5, 3, "roof_teal"],
	["junkyard", "Junkyard", 24, 12, 7, 4, "roof_slate"],
	["blackmarket", "The Black Market", 32, 12, 6, 4, "roof_red"],
	["tavern", "The Brass Lantern", 2, 7, 6, 4, "roof_slate"],
]


static func label(id: String) -> String:
	for b in BUILDINGS:
		if b[0] == id:
			return b[1]
	return id


static func generate() -> GameMap:
	var m := GameMap.new()
	m.setup(W, H, C.Tile.TOWN_FLOOR)
	m.floor_num = 0
	m.roof_style = {}
	for y in H:
		for x in W:
			if x == 0 or y == 0 or x == W - 1 or y == H - 1:
				m.set_t(x, y, C.Tile.PLANTER)
			elif x == 1 or y == H - 2 or x == W - 2:
				m.set_t(x, y, C.Tile.TOWN_GRASS)
	for b in BUILDINGS:
		var bx: int = b[2]
		var by: int = b[3]
		var bw: int = b[4]
		var bh: int = b[5]
		for y in range(by, by + bh):
			for x in range(bx, bx + bw):
				var front := y == by + bh - 1
				m.set_t(x, y, C.Tile.HOUSE_WALL if front else C.Tile.ROOF)
				if not front:
					m.roof_style[y * W + x] = b[6]
		var door := Vector2i(bx + bw / 2, by + bh - 1)
		m.set_t(door.x, door.y, C.Tile.DOOR)
		m.doors[door.y * W + door.x] = b[0]
	# the plaza: fountains, beds, and the temple mouth in the middle
	m.set_t(TEMPLE.x, TEMPLE.y, C.Tile.TEMPLE)
	m.stairs_down = TEMPLE
	for p in [Vector2i(14, 8), Vector2i(26, 8)]:
		m.set_t(p.x, p.y, C.Tile.FOUNTAIN)
	for p in [Vector2i(9, 7), Vector2i(31, 7), Vector2i(9, 10), Vector2i(31, 10)]:
		m.set_t(p.x, p.y, C.Tile.PLANTER)
	for p in [Vector2i(34, 8), Vector2i(35, 9), Vector2i(34, 9),
			Vector2i(17, 10), Vector2i(23, 10), Vector2i(17, 6), Vector2i(23, 6)]:
		m.set_t(p.x, p.y, C.Tile.TOWN_FLOWERS)
	m.reveal_all()
	m.visible.fill(1)
	return m
