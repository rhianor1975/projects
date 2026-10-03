class_name AutoExplore
## Auto-explore -- `x` in the C game. One step per call: fight what is
## close and awake, drink when it is going badly, otherwise walk toward the
## nearest edge of what you have seen (or loot you can see), and when the
## floor is mapped, take the way down. Lava and miasma are avoided when there
## is a way round. Returns "" to keep going, or the reason it stopped.

const NO_PROGRESS_LIMIT := 60
static var _idle := 0
static var _last_seen := 0
static var _visited := {}
static var _floor := -1


static func reset() -> void:
	_idle = 0
	_last_seen = 0
	_visited = {}


static func step() -> String:
	var h := Game.hero
	var m := Game.map
	if Game.depth == 0:
		return "Auto-explore works in the temple, not the plaza."
	if Game.depth >= C.MAX_FLOOR - 1 and m.stairs_down.x >= 0 and h.pos() == m.stairs_down:
		return "The Warden's floor is next. That one is yours to walk into."
	# drink before it is too late
	if h.hp * 2 < h.maxhp:
		var best := ""
		var best_heal := 0
		for s in Game.inventory:
			var t := ItemsData.consumable_by_name(s.name)
			if not t.is_empty() and (t.heal > 0 or t.heal_pct > 0):
				var amt: int = t.heal + h.maxhp * t.heal_pct / 100
				if amt > best_heal:
					best_heal = amt
					best = s.name
		if best != "":
			Rules.use_item(best)
			return ""
		if h.hp * 4 < h.maxhp:
			return "Badly hurt, with nothing to drink."
	# fight what is awake and close
	var foe: Monster = null
	var fd := 1 << 30
	for mo in m.monsters:
		if not mo.alive or not m.is_visible(mo.x, mo.y):
			continue
		var d := maxi(absi(mo.x - h.x), absi(mo.y - h.y))
		if (mo.aggro or d <= 2) and d < fd:
			fd = d
			foe = mo
	if foe and fd <= 1:
		Rules.try_move(foe.x - h.x, foe.y - h.y)
		return ""
	if foe and fd <= 6:
		var p := _path_step(func(x, y): return x == foe.x and y == foe.y, true)
		if p != Vector2i.ZERO:
			Rules.try_move(p.x, p.y)
			return ""
	# progress: something newly seen, or ground not stood on before
	if _floor != Game.depth:
		reset()
		_floor = Game.depth
	var seen_now := m.seen.count(1)
	var here := h.y * m.w + h.x
	if seen_now > _last_seen or not _visited.has(here):
		_last_seen = seen_now
		_visited[here] = true
		_idle = 0
	else:
		_idle += 1
		if _idle > NO_PROGRESS_LIMIT:
			return "No progress for a while."
	# loot in sight, then the frontier, then the stairs
	var want_item := {}
	for it in m.items:
		if m.is_seen(it.x, it.y):
			want_item[it.y * m.w + it.x] = true
	var goal := func(x, y) -> bool:
		if want_item.has(y * m.w + x):
			return true
		if not m.is_seen(x, y):
			return false
		for d in C.DIRS8:
			if m.inb(x + d.x, y + d.y) and not m.is_seen(x + d.x, y + d.y):
				return true
		return false
	# The way down, once found, is taken: in the C game a floor boundary is a
	# doorway, not a decision. Loot close by is picked up on the way.
	if m.stairs_down.x >= 0 and m.is_seen(m.stairs_down.x, m.stairs_down.y):
		var near := {}
		for k in want_item:
			if maxi(absi(k % m.w - h.x), absi(k / m.w - h.y)) <= 10:
				near[k] = true
		if not near.is_empty():
			var si := _path_step(func(x, y): return near.has(y * m.w + x), false)
			if si != Vector2i.ZERO:
				Rules.try_move(si.x, si.y)
				return ""
		var dn := m.stairs_down
		var s2 := _path_step(func(x, y): return x == dn.x and y == dn.y, false)
		if s2 != Vector2i.ZERO:
			Rules.try_move(s2.x, s2.y)
			return ""
	var st := _path_step(goal, false)
	if st != Vector2i.ZERO:
		Rules.try_move(st.x, st.y)
		return ""
	return "Nothing left to explore that you can reach."


## BFS over seen, walkable ground to the nearest cell satisfying `goal`.
## Hazards cost a detour: they are entered only if nothing else reaches.
static func _path_step(goal: Callable, to_monster: bool) -> Vector2i:
	for avoid in [true, false]:
		var r := _bfs(goal, to_monster, avoid)
		if r != Vector2i.ZERO:
			return r
	return Vector2i.ZERO


static var _prev := PackedInt32Array()


static func _bfs(goal: Callable, to_monster: bool, avoid_hazard: bool) -> Vector2i:
	var h := Game.hero
	var m := Game.map
	if _prev.size() != m.w * m.h:
		_prev.resize(m.w * m.h)
	_prev.fill(-2)
	var start := h.y * m.w + h.x
	_prev[start] = -1
	var q := PackedInt32Array([start])
	var head := 0
	while head < q.size():
		var c := q[head]
		head += 1
		var cx := c % m.w
		var cy := c / m.w
		if c != start and goal.call(cx, cy):
			var cur := c
			while _prev[cur] != start:
				cur = _prev[cur]
			return Vector2i(cur % m.w - h.x, cur / m.w - h.y)
		for d in C.DIRS8:
			var nx: int = cx + d.x
			var ny: int = cy + d.y
			if nx < 0 or ny < 0 or nx >= m.w or ny >= m.h:
				continue
			var ni := ny * m.w + nx
			if _prev[ni] != -2:
				continue
			if not (to_monster and goal.call(nx, ny)):
				var tt := m.tiles[ni]
				if m.seen[ni] == 0:
					continue
				if GameMap.blocks_walk(tt) and not (tt == C.Tile.LOCKED_DOOR and Game.keys > 0):
					continue
				if tt == C.Tile.STAIRS_UP or tt == C.Tile.PORTAL or tt == C.Tile.SNARE:
					continue
				if tt == C.Tile.STAIRS_DOWN and not goal.call(nx, ny):
					continue
				if avoid_hazard and (tt == C.Tile.LAVA or tt == C.Tile.MIASMA):
					continue
			_prev[ni] = c
			q.append(ni)
	return Vector2i.ZERO
