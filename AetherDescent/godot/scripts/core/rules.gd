class_name Rules
## Everything that happens in a turn -- a port of src/combat.c, spells.c,
## ranged.c, abilities.c, features.c and the turn handling in main.c.
##
## Actions return true when they spent the turn. Anything the UI must open
## (a shop, the temple's floor choice, the game-over screen) is left in
## Game-side `request` for the view to pick up after the action.

static var request := {}
static var _blade_clean := false   # the blow landing now came off your own blade


static func H() -> Hero:
	return Game.hero


static func M() -> GameMap:
	return Game.map


static func rnd(n: int) -> int:
	return Game.rng.randi() % maxi(n, 1)


# ---- vision ----------------------------------------------------------------------
static func refresh_vision() -> void:
	var h := H()
	var m := M()
	if Game.depth == 0:
		return
	var radius := h.fov_radius
	if m.district_at(h.x, h.y) == C.District.MIRE:
		radius = mini(radius, C.MIRE_FOV_RADIUS)
	radius = Dda.sight(h, radius)
	m.compute_fov(h.x, h.y, radius)
	Party.reveal()
	_hear(m)


## The other sense: a monster nobody can see, inside somebody's hearing
## radius, is heard -- through walls, which is the point. It reports a
## position, not an identity. Standing in the chapel costs you it.
static func _hear(m: GameMap) -> void:
	m.heard = []
	var ears: Array = []
	for b in Game.party:
		if Party.is_up(b) and b.hearing_radius > 0 and m.district_at(b.x, b.y) != C.District.CHAPEL:
			ears.append([b.x, b.y, b.hearing_radius * b.hearing_radius])
	if ears.is_empty():
		return
	for mo in m.monsters:
		if m.heard.size() >= 64:
			break
		if not mo.alive or m.is_visible(mo.x, mo.y):
			continue
		for e in ears:
			if (mo.x - e[0]) * (mo.x - e[0]) + (mo.y - e[1]) * (mo.y - e[1]) <= e[2]:
				m.heard.append(mo.pos())
				break


# ---- the damage model --------------------------------------------------------------
static func damage_after_defence(attack: int, defence: int, variance: int) -> int:
	if attack < 1:
		return 1
	defence = maxi(defence, 0)
	return maxi(1, attack * attack / (attack + defence) + variance)


## Experience goes to the body that earned it -- grant_xp() in combat.c.
static func grant_xp(xp: int, who: Hero = null) -> void:
	var h := who if who != null else H()
	if Game.is_swarm():
		xp = maxi(1, xp / C.SWARM_MONSTER_FRACTION)
	var bonus := h.effective_stat(C.Acc.XP)
	if bonus > 0:
		xp += xp * bonus / 100
	h.xp += xp
	while h.xp >= h.xp_next:
		h.xp -= h.xp_next
		h.level += 1
		h.maxhp += 8
		h.hp = h.maxhp
		h.base_atk += 1
		if h.level % 2 == 0:
			h.base_def += 1
		h.xp_next = 20 + (h.level - 1) * 15
		if h == H():
			Game.good("You reach level %d! Max HP now %d." % [h.level, h.maxhp])
		else:
			Game.good("%s reaches level %d." % [h.name, h.level])
		Game.emit_fx({"type": "levelup", "at": h.pos(), "who": h})
	Game.update_escalation()


static func sell_level() -> bool:
	var h := H()
	if h.level <= 1:
		return false
	if h.level % 2 == 0:
		h.base_def -= 1
	h.base_atk -= 1
	h.maxhp = maxi(1, h.maxhp - 8)
	h.level -= 1
	h.xp = 0
	h.xp_next = 20 + (h.level - 1) * 15
	h.base_atk = maxi(1, h.base_atk)
	h.base_def = maxi(0, h.base_def)
	h.hp = mini(h.hp, h.maxhp)
	Game.update_escalation()
	return true


## Every blow anybody lands goes through here, so a kill by a hire pays the
## party's gold, counts for the bounty, and ends the run if it was the Warden.
static func monster_take_damage(mo: Monster, dmg: int, killer: Hero = null) -> void:
	var m := M()
	if not mo.alive:
		return
	if dmg > 0:
		var dk := m.district_at(mo.x, mo.y)
		if dk == C.District.HIVE:
			var woke := 0
			for o in m.monsters:
				if o.alive and o != mo and not o.aggro and absi(o.x - mo.x) <= C.HIVE_ALARM_RADIUS and absi(o.y - mo.y) <= C.HIVE_ALARM_RADIUS:
					o.aggro = true
					woke += 1
			if woke > 0:
				Game.warn("The comb shrieks. %d of them know where you are." % woke)
		elif dk == C.District.QUIET:
			var di := m.district_id[mo.y * m.w + mo.x]
			var woke := 0
			for o in m.monsters:
				if o.alive and not o.aggro and m.district_id[o.y * m.w + o.x] == di:
					o.aggro = true
					woke += 1
			if woke > 0:
				Game.warn("The quiet goes out of the place. All %d of them are up." % woke)
	if dmg > 0:
		Districts.note_hurt(mo)
	mo.aggro = true
	mo.hp -= dmg
	Game.emit_fx({"type": "hurt", "who": mo, "amount": dmg})
	if mo.hp > 0:
		return
	mo.hp = 0
	mo.alive = false
	Game.emit_fx({"type": "die", "who": mo})
	Districts.note_kill(mo.x, mo.y)
	var gold := Game.gain_gold(mo.gold_reward)
	Game.msg("The %s falls. (+%d gold)" % [mo.name, gold], Color8(255, 232, 150))
	# only a blade leaves anything worth carrying up
	if _blade_clean and killer == null:
		var cut := Kitchen.on_clean_kill(mo)
		if cut >= 0:
			Game.msg("You take a %s cut off the %s." % [Kitchen.MEAT_NAMES[cut], mo.name], Color8(240, 180, 160))
	grant_xp(mo.xp_reward, killer)
	var q := Game.quest
	if not q.is_empty() and int(q.get("depth", -1)) == Game.depth:
		if q.type == "kill" and q.monster == mo.name:
			Quests._pay("Bounty complete! You've slain the %s." % mo.name)
		elif q.type == "clear":
			q.done = int(q.get("done", 0)) + 1
			if q.done >= int(q.needed):
				Quests._pay("Bounty complete! Floor %d is clear enough now." % Game.depth)
			else:
				Game.msg("Bounty progress: %d/%d." % [q.done, q.needed])
	if mo.is_boss:
		if rnd(100) < 60:
			grant_random_set_piece(m.biome)
		Game.won = true
		request = {"open": "win"}
	elif mo.is_elite:
		if rnd(100) < (60 if mo.biome_boss else 12):
			grant_random_set_piece(m.biome)


static func find_target(radius: int, exclude: Monster = null) -> Monster:
	var h := H()
	var m := M()
	var best: Monster = null
	var best_d := 1 << 30
	for mo in m.monsters:
		if not mo.alive or mo == exclude:
			continue
		var dx: int = mo.x - h.x
		var dy: int = mo.y - h.y
		var d := dx * dx + dy * dy
		if d > radius * radius or d >= best_d:
			continue
		if not m.is_visible(mo.x, mo.y) or not m.line_of_sight(h.x, h.y, mo.x, mo.y):
			continue
		best = mo
		best_d = d
	return best


static func find_target_from(at: Vector2i, radius: int, exclude: Array) -> Monster:
	var m := M()
	var best: Monster = null
	var best_d := 1 << 30
	for mo in m.monsters:
		if not mo.alive or mo in exclude:
			continue
		var d: int = (mo.x - at.x) * (mo.x - at.x) + (mo.y - at.y) * (mo.y - at.y)
		if d > radius * radius or d >= best_d or not m.is_visible(mo.x, mo.y):
			continue
		best = mo
		best_d = d
	return best


static func damage_all_in_reach(radius: int, dmg: int) -> int:
	var h := H()
	var hits := 0
	for mo in M().monsters.duplicate():
		if not mo.alive:
			continue
		var dx: int = mo.x - h.x
		var dy: int = mo.y - h.y
		if dx * dx + dy * dy > radius * radius or not M().is_visible(mo.x, mo.y):
			continue
		monster_take_damage(mo, dmg)
		hits += 1
	return hits


static func hero_attack(mo: Monster) -> void:
	var h := H()
	if not Districts.allows(h.x, h.y, C.Act.MELEE):
		Game.msg("Not here. This ground was marked out for %s." % C.PROVE_RULES[C.Prove.ARCANE], Gfx.GREY)
		return
	# the Ruins' old wards did not all fail: a warded blow costs the turn and nothing else
	var mward := Dda.monster_ward_pct()
	if mward > 0 and rnd(100) < mward:
		_face(h, mo.x - h.x, mo.y - h.y)
		Game.emit_fx({"type": "attack", "who": h, "target": mo})
		Game.msg("An old ward flares around the %s and your blow slides off." % mo.name, Color8(170, 190, 230))
		return
	var dmg := damage_after_defence(Districts.eff_atk(h), mo.def, rnd(4) - 1)
	var crit_chance := h.effective_stat(C.Acc.CRIT) + Dda.grace()
	var crit := crit_chance > 0 and rnd(100) < crit_chance
	if crit:
		dmg *= 2
	_face(h, mo.x - h.x, mo.y - h.y)
	Game.emit_fx({"type": "attack", "who": h, "target": mo, "crit": crit})
	Game.msg(("A vicious opening! You strike the %s for %d." if crit else "You strike the %s for %d.") % [mo.name, dmg])
	if h.jinx_pct > 0 and rnd(100) < h.jinx_pct:
		mo.jinx_pen = 3 + rnd(3)
		mo.jinx_turns = 4
		Game.msg("A flicker of bad luck settles over the %s." % mo.name, Color8(190, 140, 255))
	if h.set_complete >= 0 and rnd(100) < C.SET_PROC_PCT:
		_set_proc(mo)
	_blade_clean = true
	monster_take_damage(mo, dmg)
	_blade_clean = false


static func _set_proc(mo: Monster) -> void:
	match H().set_complete:
		C.Biome.JUNGLE:
			mo.jinx_pen += 2
			mo.jinx_turns = maxi(mo.jinx_turns, 6)
			Game.msg("Your root-drowned gear weeps venom into the %s." % mo.name, Color8(140, 230, 120))
		C.Biome.INDUSTRIAL:
			mo.stun_turns += 1
			Game.msg("Your Company plate drives the %s off its footing." % mo.name, Color8(240, 200, 120))
		_:
			mo.slow_turns += 2
			Game.msg("Old power in your gear drags at the %s." % mo.name, Color8(160, 210, 255))


static func hero_take_damage(dmg: int, source: String) -> void:
	var h := H()
	if h.guard_turns > 0:
		dmg = (dmg + 1) / 2
	h.hp -= dmg
	Game.emit_fx({"type": "hurt", "who": h, "amount": dmg})
	if h.hp <= 0:
		h.hp = 0
		Game.killed_by = source
		_die()


static func _die() -> void:
	var h := H()
	if Dda.guardian_catch():
		return
	# The party will not let you die with a charm in your pack...
	if Party.attempt_rescue():
		return
	h.alive = false
	Game.emit_fx({"type": "fall", "who": h})
	# ...and if they cannot save you, one of them carries on.
	if Party.pass_the_torch():
		return
	Game.game_over = true
	request = {"open": "gameover"}


## Damage landing on any body. The driven one goes through hero_take_damage;
## anybody else takes it here -- the Vanguard's guard halves it -- and a death
## puts them back on the Tavern's books.
static func body_take_damage(c: Hero, dmg: int, source: String) -> void:
	if c == H():
		hero_take_damage(dmg, source)
		return
	if not Party.is_up(c):
		return
	if c.guard_turns > 0:
		dmg = (dmg + 1) / 2
	dmg = maxi(dmg, 1)
	c.hp -= dmg
	Game.emit_fx({"type": "hurt", "who": c, "amount": dmg})
	if c.hp > 0:
		return
	c.hp = 0
	c.alive = false
	Game.emit_fx({"type": "fall", "who": c})
	Game.warn("%s goes down and does not get up." % c.name)


static func monster_hit_hero(mo: Monster, target: Hero = null) -> void:
	var h := target if target != null else H()
	var you := h == H()
	mo.facing_left = h.x < mo.x
	Game.emit_fx({"type": "attack", "who": mo, "target": h})
	if rnd(100) < (Dda.softened_crit(C.MONSTER_CRIT_PCT) if you else C.MONSTER_CRIT_PCT):
		var dmg := maxi(1, Party.max_hp(h) * (4 + rnd(4)) / 100)
		if you:
			Game.warn("A critical blow from the %s finds a gap in your guard! (-%d)" % [mo.name, dmg])
		body_take_damage(h, dmg, mo.name)
		_reflect(mo, dmg, h)
		_inflict_status(mo, h)
		return
	var ev := h.effective_stat(C.Acc.EVASION) + h.evasion_buff + (Dda.grace() if you else 0)
	if ev > 0 and rnd(100) < ev:
		if you:
			Game.msg("You dodge the %s's attack." % mo.name, Color8(150, 220, 255))
		Game.emit_fx({"type": "miss", "who": h})
		return
	var wd := h.effective_stat(C.Acc.WARD) + Dda.ward_bonus()
	if wd > 0 and rnd(100) < wd:
		if you:
			Game.msg("Something wards off the %s's blow entirely." % mo.name, Color8(150, 220, 255))
		Game.emit_fx({"type": "miss", "who": h})
		return
	var atk := mo.atk
	if mo.jinx_turns > 0:
		atk -= mo.jinx_pen
	var dmg := damage_after_defence(maxi(atk, 0), h.eff_def(), rnd(4) - 1)
	if you:
		Game.msg("The %s hits you for %d." % [mo.name, dmg], Color8(255, 190, 170))
	body_take_damage(h, dmg, mo.name)
	_reflect(mo, dmg, h)
	_inflict_status(mo, h)


static func _reflect(mo: Monster, dmg: int, h: Hero) -> void:
	if not mo.alive or h.reflect_turns <= 0 or h.reflect_pct <= 0 or not h.alive:
		return
	var back := dmg * h.reflect_pct / 100
	if back >= 1:
		if h == H():
			Game.msg("The ward turns %d of it back into the %s." % [back, mo.name])
		monster_take_damage(mo, back, h)


static func _inflict_status(mo: Monster, h: Hero) -> void:
	if not h.alive:
		return
	var you := h == H()
	var proc := maxi(3, 15 - (h.hazard_resist_pct + h.set_bonus_hazard) / 4)
	if rnd(100) >= proc:
		return
	if mo.is_boss or mo.biome_boss or mo.family == 1 or mo.family == 4:
		h.stun_turns += 1
		if you:
			Game.warn("The %s's blow leaves you reeling, stunned!" % mo.name)
	elif mo.family == 0 or mo.family == 3:
		h.poison_turns = 4 + rnd(3)
		h.poison_dmg = 2 + rnd(3)
		if you:
			Game.warn("The %s's bite leaves you poisoned!" % mo.name)
	else:
		h.slow_turns = 3 + rnd(3)
		if you:
			Game.warn("Something about the %s's touch slows your blood." % mo.name)


# ---- monster turns ---------------------------------------------------------------
static func monster_turn(mo: Monster) -> void:
	var h := H()
	var m := M()
	if not mo.alive:
		return
	if mo.jinx_turns > 0:
		mo.jinx_turns -= 1
	if m.t(mo.x, mo.y) == C.Tile.BLOODPOOL and mo.hp < mo.maxhp:
		mo.hp = mini(mo.maxhp, mo.hp + C.BLOODPOOL_REGEN)
	if mo.stun_turns > 0:
		mo.stun_turns -= 1
		return
	if mo.slow_turns > 0:
		mo.slow_turns -= 1
		if rnd(2) == 0:
			return
	# A hired hero next to a monster gets hit like anyone else -- checked
	# before the player, so a monster boxed in by the party fights the party.
	for d in C.DIRS8:
		var c := Party.other_at(mo.x + d.x, mo.y + d.y)
		if c:
			monster_hit_hero(mo, c)
			return
	var dx := h.x - mo.x
	var dy := h.y - mo.y
	if absi(dx) <= 1 and absi(dy) <= 1:
		monster_hit_hero(mo)
		# the stone wood: what you cannot see gets the first blow, and gets it twice
		if mo.alive and H().alive and Districts.ambush(mo):
			for i in C.AMBUSH_EXTRA_BLOWS:
				monster_hit_hero(mo)
			Game.warn("It was behind the stone. You never saw it move.")
		return
	var aggro_r := Districts.aggro_radius(mo.x, mo.y,
		maxi(2, 8 - h.aggro_reduction - Dda.aggro_reduction() - Dda.growth_cover()))
	var dk := m.district_at(mo.x, mo.y)
	if dk == C.District.QUIET:
		aggro_r = mini(aggro_r, C.QUIET_AGGRO_RADIUS)
	if dk == C.District.MYCELIUM:
		if h.still_turns >= C.MYCELIUM_FORGET_TURNS:
			mo.aggro = false
		elif not mo.aggro and dx * dx + dy * dy <= C.MYCELIUM_HEAR_RADIUS * C.MYCELIUM_HEAR_RADIUS:
			mo.aggro = true
	if not mo.aggro and dx * dx + dy * dy <= aggro_r * aggro_r and m.line_of_sight(mo.x, mo.y, h.x, h.y):
		mo.aggro = true
	if not mo.aggro:
		if rnd(4) == 0:
			var nx := mo.x + rnd(3) - 1
			var ny := mo.y + rnd(3) - 1
			if m.walkable_monster(nx, ny) and not m.monster_at(nx, ny) and not Party.body_at(nx, ny):
				m.move_monster(mo, nx, ny)
		return
	# chase: take the axis step that closes the most, else any step that helps
	var sx := signi(dx)
	var sy := signi(dy)
	var tries: Array = []
	if absi(dx) >= absi(dy):
		tries = [Vector2i(sx, sy), Vector2i(sx, 0), Vector2i(0, sy)]
	else:
		tries = [Vector2i(sx, sy), Vector2i(0, sy), Vector2i(sx, 0)]
	for st in tries:
		if st == Vector2i.ZERO:
			continue
		var nx: int = mo.x + st.x
		var ny: int = mo.y + st.y
		if m.walkable_monster(nx, ny) and not m.monster_at(nx, ny) and not Party.body_at(nx, ny):
			m.move_monster(mo, nx, ny)
			return
	var best := Vector2i(-1, -1)
	var best_d := (dx * dx + dy * dy)
	for d in C.DIRS8:
		var nx: int = mo.x + d.x
		var ny: int = mo.y + d.y
		if not m.walkable_monster(nx, ny) or m.monster_at(nx, ny) or Party.body_at(nx, ny):
			continue
		var dd: int = (h.x - nx) * (h.x - nx) + (h.y - ny) * (h.y - ny)
		if dd <= best_d:
			best_d = dd
			best = Vector2i(nx, ny)
	if best.x >= 0:
		m.move_monster(mo, best.x, best.y)


static func process_monsters() -> void:
	var m := M()
	var bodies: Array = Game.party.filter(func(b): return Party.is_up(b))
	# one box round the whole party first: most of a Well floor is outside it
	var lo := Vector2i(1 << 30, 1 << 30)
	var hi := Vector2i(-(1 << 30), -(1 << 30))
	for b in bodies:
		lo = Vector2i(mini(lo.x, b.x), mini(lo.y, b.y))
		hi = Vector2i(maxi(hi.x, b.x), maxi(hi.y, b.y))
	lo -= Vector2i(C.ACTIVE_RADIUS, C.ACTIVE_RADIUS)
	hi += Vector2i(C.ACTIVE_RADIUS, C.ACTIVE_RADIUS)
	for mo in m.monsters.duplicate():
		if not H().alive or Game.won or Game.game_over:
			break
		if not mo.alive or mo.x < lo.x or mo.y < lo.y or mo.x > hi.x or mo.y > hi.y:
			continue
		# a monster acts if any of the party is near enough to matter
		var near := false
		for b in bodies:
			if absi(mo.x - b.x) <= C.ACTIVE_RADIUS and absi(mo.y - b.y) <= C.ACTIVE_RADIUS:
				near = true
				break
		if not near:
			continue
		monster_turn(mo)
	m.remove_dead()


static func maybe_respawn() -> void:
	var m := M()
	var h := H()
	if Game.depth <= 0:
		return
	m.turns_on_floor += 1
	var area := MapGen.area_scale(m)
	var respawn_interval := maxi(1, int(25 * area))
	var doubling := maxi(1, int(2500 * area))
	var spawn := func(want: int) -> int:
		var placed := 0
		for tries in want * 25:
			if placed >= want:
				break
			var x := rnd(m.w)
			var y := rnd(m.h)
			if m.t(x, y) != C.Tile.FLOOR or m.in_haven(x, y) or m.monster_at(x, y) or Party.body_at(x, y):
				continue
			if (x - h.x) * (x - h.x) + (y - h.y) * (y - h.y) < C.RESPAWN_MIN_DIST * C.RESPAWN_MIN_DIST:
				continue
			m.add_monster(Monster.for_floor(m.floor_num, x, y, Game.rng))
			placed += 1
		return placed
	if m.turns_on_floor % doubling == 0:
		var live := m.monsters.size()
		if spawn.call(maxi(live, C.DOUBLING_MIN_WAVE)) > 0:
			Game.warn("The floor answers your loitering. There are twice as many now.")
	var infested := Game.is_swarm()
	var interval := respawn_interval / 3 if m.overrun else respawn_interval
	if infested:
		interval = respawn_interval / 4
	interval = maxi(interval, 1)
	if m.turns_on_floor % interval != 0:
		return
	var per_wave := 5 if m.overrun else 2
	if infested:
		var alive := m.monsters.size()
		if m.infest_target <= 0:
			m.infest_target = alive
		if alive >= m.infest_target:
			return
		per_wave = clampi((m.infest_target - alive) / 8, 2, 40)
	if spawn.call(per_wave) > 0:
		Game.msg("You hear something stir, back the way you came." if not m.overrun else "More of them. The floor keeps producing.",
			Color8(200, 190, 210))


# ---- the end of a turn ------------------------------------------------------------
static func end_turn() -> void:
	var h := H()
	var m := M()
	Game.turns += 1
	if h.atk_buff_turns > 0:
		h.atk_buff_turns -= 1
		if h.atk_buff_turns == 0:
			h.atk_buff = 0
			Game.msg("Your attack buff wears off.", Color8(180, 180, 200))
	if h.def_buff_turns > 0:
		h.def_buff_turns -= 1
		if h.def_buff_turns == 0:
			h.def_buff = 0
			Game.msg("Your guard settles back to normal.", Color8(180, 180, 200))
	if h.evasion_buff_turns > 0:
		h.evasion_buff_turns -= 1
		if h.evasion_buff_turns == 0:
			h.evasion_buff = 0
	if h.reflect_turns > 0:
		h.reflect_turns -= 1
	for i in h.spell_cd.size():
		if h.spell_cd[i] > 0:
			h.spell_cd[i] -= 1
	if h.aether < h.aether_max:
		h.aether = mini(h.aether_max, h.aether + 1 + Dda.recharge_bonus())
	if h.ability_cd > 0:
		h.ability_cd -= 1
	if h.ranged_cooldown > 0:
		h.ranged_cooldown -= 1
	if Game.turns % C.RANGED_AMMO_REGEN_TURNS == 0 and h.ranged_ammo < h.ranged_ammo_max:
		h.ranged_ammo = mini(h.ranged_ammo_max, h.ranged_ammo + 1 + Dda.recharge_bonus())
	var regen := h.effective_stat(C.Acc.REGEN) + Dda.regen_bonus()
	if regen > 0 and h.poison_turns == 0 and h.hp < h.maxhp:
		h.hp = mini(h.maxhp, h.hp + regen)
	# the ground you stand on
	var here := m.t(h.x, h.y)
	h.stance_atk_pct = 0
	h.stance_def_pct = 0
	if here == C.Tile.PRISM_RED:
		h.stance_atk_pct = C.PRISM_SWING
		h.stance_def_pct = -C.PRISM_SWING
	elif here == C.Tile.PRISM_BLUE:
		h.stance_atk_pct = -C.PRISM_SWING
		h.stance_def_pct = C.PRISM_SWING
	elif here == C.Tile.PRISM_GREEN and h.hp < h.maxhp:
		h.hp = mini(h.maxhp, h.hp + C.PRISM_REGEN)
	if here == C.Tile.LAVA or here == C.Tile.MIASMA:
		var raw := ((12 + rnd(9)) if here == C.Tile.LAVA else (2 + rnd(4))) + Dda.hazard_extra()
		var dmg := maxi(1, raw - raw * (h.hazard_resist_pct + h.set_bonus_hazard) / 100)
		Game.warn(("The lava sears you for %d damage!" if here == C.Tile.LAVA else "The miasma burns in your lungs for %d damage.") % dmg)
		hero_take_damage(dmg, "the lava" if here == C.Tile.LAVA else "the miasma")
	if h.poison_turns > 0 and h.alive:
		h.poison_turns -= 1
		Game.warn("The poison burns for %d damage." % h.poison_dmg)
		hero_take_damage(h.poison_dmg, "poison")
		if h.poison_turns == 0 and h.alive:
			Game.msg("The poison finally fades from your veins.")
	if Game.game_over:
		return
	if Game.depth > 0:
		Party.take_turn()
		if Game.won:
			return
		process_monsters()
		if Game.game_over or Game.won:
			return
		Districts.tick()
		M().remove_dead()
		if Game.game_over or Game.won or Game.depth == 0:
			return
		maybe_respawn()
		refresh_vision()
		if Game.recall_countdown > 0:
			Game.recall_countdown -= 1
			if Game.recall_countdown == 0:
				Game.save_run()
				Game.enter_town(Town.START)
				Game.good("The charm cracks and the world folds. You are standing in the plaza.")
				Game.emit_fx({"type": "warp"})


static func _face(h: Hero, dx: int, dy: int) -> void:
	if absi(dx) > absi(dy):
		h.facing = "left" if dx < 0 else "right"
	elif dy != 0:
		h.facing = "up" if dy < 0 else "down"
	elif dx != 0:
		h.facing = "left" if dx < 0 else "right"


static func _hindered() -> bool:
	var h := H()
	if h.stun_turns > 0:
		h.stun_turns -= 1
		Game.warn("You are stunned and can't act!")
		end_turn()
		return true
	if h.held_turns > 0:
		h.held_turns -= 1
		Game.warn("You are held fast -- you struggle and get nowhere.")
		end_turn()
		return true
	if h.slow_turns > 0:
		h.slow_turns -= 1
		if rnd(2) == 0:
			Game.msg("Your limbs drag. You lose the moment.", Color8(180, 180, 210))
			end_turn()
			return true
	return false


# ---- moving -----------------------------------------------------------------------
static func try_move(dx: int, dy: int) -> bool:
	var h := H()
	var m := M()
	request = {}
	if not h.alive:
		return false
	_face(h, dx, dy)
	var nx := h.x + dx
	var ny := h.y + dy
	if not m.inb(nx, ny):
		return false
	var tt := m.t(nx, ny)
	if Game.depth == 0:
		if tt == C.Tile.DOOR:
			request = {"open": "shop", "id": m.doors.get(ny * m.w + nx, "")}
			return false
		if tt == C.Tile.TEMPLE:
			request = {"open": "temple"}
			return false
		if tt == C.Tile.QUEST_BOARD:
			request = {"open": "board"}
			return false
		if not m.walkable_player(nx, ny):
			return false
		var from := h.pos()
		h.x = nx
		h.y = ny
		Game.emit_fx({"type": "move", "who": h, "from": from})
		return true
	var mo := m.monster_at(nx, ny)
	if mo:
		if not Districts.allows(h.x, h.y, C.Act.MELEE):
			Game.msg("Not here. This ground was marked out for %s." % C.PROVE_RULES[C.Prove.ARCANE], Gfx.GREY)
			return false
		if _hindered():
			return true
		hero_attack(mo)
		h.still_turns = 0
		end_turn()
		return true
	if Party.other_at(nx, ny):
		if _hindered():
			return true
		Party.displace_into(h, nx, ny)
		h.still_turns = 0
		_pickup()
		end_turn()
		return true
	if tt == C.Tile.LOCKED_DOOR:
		if Game.keys > 0:
			Game.keys -= 1
			m.set_t(nx, ny, C.Tile.FLOOR)
			Game.good("The brass key turns. The door swings open.")
			end_turn()
			return true
		Game.msg("Locked. A brass key would open it.")
		return false
	if tt == C.Tile.SEALED_DOOR:
		Game.msg("Sealed. There is a lever somewhere on this floor.")
		return false
	if not m.walkable_player(nx, ny):
		return false
	if _hindered():
		return true
	var from := h.pos()
	var was_in := m.district_index(h.x, h.y)
	h.x = nx
	h.y = ny
	h.still_turns = 0
	Game.emit_fx({"type": "move", "who": h, "from": from})
	Districts.announce(was_in)
	Game.steps += 1
	if Game.steps % C.JUNK_STEPS_PER_PIECE == 0:
		Districts.gain_material(Districts.material_tier(Game.depth), 1, ItemsData.junk_worth(Game.depth))
	_pickup()
	if _landed(tt):
		return true
	var f := m.feature_at(nx, ny)
	if not f.is_empty():
		trigger_feature(f)
	if Game.depth > 0 and Game.map == m:
		end_turn()
	return true


static func wait_turn() -> bool:
	request = {}
	if _hindered():
		return true
	H().still_turns += 1
	end_turn()
	return true


static func _landed(tt: int) -> bool:
	var h := H()
	var m := M()
	match tt:
		C.Tile.STAIRS_DOWN:
			Game.save_run()
			Game.go_to_floor(Game.depth + 1, true, func(): Game.emit_fx({"type": "warp"}))
			return true
		C.Tile.STAIRS_UP:
			if Game.depth <= 1:
				Game.enter_town(Town.TEMPLE + Vector2i(0, 1))
				Game.msg("You climb back out into the plaza.", Color8(150, 200, 255))
				Game.emit_fx({"type": "warp"})
			else:
				Game.go_to_floor(Game.depth - 1, false, func(): Game.emit_fx({"type": "warp"}))
			return true
		C.Tile.PORTAL:
			var to := m.portal_b if h.pos() == m.portal_a else m.portal_a
			if to.x >= 0:
				h.x = to.x
				h.y = to.y
				Game.msg("The portal folds you across the floor.", Color8(200, 160, 255))
				Game.emit_fx({"type": "teleport", "who": h})
		C.Tile.LEVER:
			if m.lever_door.x >= 0 and m.t(m.lever_door.x, m.lever_door.y) == C.Tile.SEALED_DOOR:
				m.set_t(m.lever_door.x, m.lever_door.y, C.Tile.FLOOR)
				Game.good("You haul the lever. Somewhere, stone grinds open.")
		C.Tile.PIT:
			Districts.fall()
			return true
		C.Tile.SNARE:
			var sand := m.district_at(h.x, h.y) == C.District.QUICKSAND
			var dmg := C.SNARE_SAND_DAMAGE if sand else C.SNARE_GARDEN_DAMAGE
			h.held_turns = C.SNARE_SAND_HOLD if sand else C.SNARE_GARDEN_HOLD
			Game.warn(("You are caught in the sand -- %d damage, held for %d." if sand
				else "It snaps shut on you -- %d damage, held for %d.") % [dmg, h.held_turns])
			hero_take_damage(dmg, "the snare")
	return false


static func _pickup() -> void:
	var h := H()
	var m := M()
	for it in m.items_at(h.x, h.y):
		m.take_item(it)
		match it.kind:
			"gold":
				var g := Game.gain_gold(int(it.amount))
				Game.msg("You find %d gold." % g, Color8(255, 220, 96))
				Game.emit_fx({"type": "pickup", "at": h.pos(), "text": "+%dG" % g, "color": Color8(255, 220, 96)})
			"key":
				Game.keys += 1
				Game.good("You find a brass key.")
			"quest":
				Game.quest.found = true
				Game.good("You recover %s! Bring it back to the quest board in town." % it.name)
			_:
				Game.give(it.name)
				Game.msg("You pick up a %s." % it.name, Color8(200, 230, 255))
				Game.emit_fx({"type": "pickup", "at": h.pos(), "text": it.name, "color": Color8(200, 230, 255)})


# ---- features ---------------------------------------------------------------------
static func trigger_feature(f: Dictionary) -> void:
	var h := H()
	var m := M()
	match f.type:
		C.Feature.MERCHANT:
			request = {"open": "merchant"}
			return
		C.Feature.CONSOLE:
			Districts.console()
			return
		C.Feature.STRONGBOX:
			if not f.used:
				Districts.strongbox(f)
			return
		C.Feature.TOWN_GATE:
			Game.save_run()
			Game.enter_town(Town.START)
			Game.good("The waygate hums, and you step out into the plaza.")
			Game.emit_fx({"type": "warp"})
			return
	if f.used:
		return
	match f.type:
		C.Feature.SHRINE:
			match rnd(4):
				0:
					h.maxhp += 5
					h.hp += 5
					Game.good("The shrine remembers you passing. +5 max HP, permanently.")
				1:
					h.base_atk += 1
					Game.good("The shrine remembers you passing. +1 attack, permanently.")
				2:
					h.base_def += 1
					Game.good("The shrine remembers you passing. +1 defence, permanently.")
				_:
					h.hp = h.maxhp
					Game.good("The shrine remembers you passing. You are fully healed.")
		C.Feature.FOUNTAIN:
			var roll := rnd(100)
			var bad := maxi(0, 5 - h.fortune_luck_pct / 6)
			if roll < 45:
				var heal := 20 + rnd(20)
				h.hp = mini(h.maxhp, h.hp + heal)
				Game.good("The water is cold and clean. +%d HP." % heal)
			elif roll < 75:
				h.atk_buff = 5
				h.atk_buff_turns = 25
				Game.good("The water tingles like static. +5 attack for 25 turns.")
			elif roll < 100 - bad:
				h.def_buff = 5
				h.def_buff_turns = 25
				Game.good("The water settles your nerves. +5 defence for 25 turns.")
			else:
				var dmg := 6 + rnd(8)
				Game.warn("The water was not water. It costs you %d HP." % dmg)
				hero_take_damage(dmg, "a bad fountain")
		C.Feature.MACHINE:
			var roll := rnd(100)
			var ambush := 60 + maxi(5, 25 - h.machine_luck_pct)
			if roll < 35:
				var g := Game.gain_gold(15 + Game.depth + rnd(30))
				Game.good("Ancient machinery disgorges a handful of coin -- +%d gold." % g)
			elif roll < 60:
				m.reveal_circle(h.x, h.y, 12)
				Game.good("The machine's lamps flare -- the passage ahead is briefly lit.")
			elif roll < ambush:
				for tries in 20:
					var nx := h.x + rnd(7) - 3
					var ny := h.y + rnd(7) - 3
					if not Party.body_at(nx, ny) and m.walkable_monster(nx, ny) and not m.monster_at(nx, ny):
						var mo := Monster.for_floor(Game.depth, nx, ny, Game.rng)
						mo.aggro = true
						m.add_monster(mo)
						Game.warn("Gears grind and something wakes nearby. That was a mistake.")
						break
			else:
				Game.msg("The machine hisses steam and does nothing else.")
		C.Feature.RELIC:
			apply_set_gear(int(f.relic_set), int(f.relic_kind))
		C.Feature.ALTAR:
			if h.hp <= 1:
				Game.msg("The altar wants blood you do not have.")
				return
			var cost := h.hp / 2
			h.hp = maxi(1, h.hp - cost)
			Game.gold_boon_until = Game.depth + 10
			Game.warn("You open a vein across the basin. It costs you %d." % cost)
			Game.good("Everything down here glitters twice as bright, until floor %d." % Game.gold_boon_until)
		C.Feature.TOLL:
			if Game.gold <= 0 or m.stairs_down.x < 0:
				Game.msg("The tollkeeper looks at your empty hands and goes back to sleep.")
				return
			var paid := Game.gold
			Game.gold = 0
			h.x = m.stairs_down.x
			h.y = m.stairs_down.y - 1 if m.walkable_player(m.stairs_down.x, m.stairs_down.y - 1) else m.stairs_down.y
			Game.msg("You tip out %d gold. They walk you to the stairs without a word." % paid)
			Game.emit_fx({"type": "teleport", "who": h})
	f.used = true


static func apply_set_gear(biome: int, slot: int) -> void:
	var h := H()
	var g: Array = ItemsData.SET_GEAR[biome][slot]
	var bonus: int = g[1] + h.relic_quality_bonus
	match slot:
		C.Relic.WEAPON:
			h.weapon_bonus = bonus
			h.weapon_name = g[0]
			h.weapon_set = biome
			h.weapon_plus = 0
		C.Relic.ARMOR:
			h.armor_bonus = bonus
			h.armor_name = g[0]
			h.armor_set = biome
			h.armor_plus = 0
		C.Relic.RING:
			h.ring_name = g[0]
			h.ring_stat = g[2]
			h.ring_bonus = bonus
			h.ring_set = biome
		C.Relic.TRINKET:
			h.trinket_name = g[0]
			h.trinket_stat = g[2]
			h.trinket_bonus = bonus
			h.trinket_set = biome
	h.recompute_set_bonus()
	Game.good("You cannot resist taking up the %s." % g[0])
	Game.emit_fx({"type": "levelup", "at": h.pos()})


static func grant_random_set_piece(biome: int) -> void:
	apply_set_gear(biome, rnd(4))


# ---- items ------------------------------------------------------------------------
static func use_item(name: String) -> bool:
	var h := H()
	var t := ItemsData.consumable_by_name(name)
	if t.is_empty() or Game.count_item(name) <= 0:
		return false
	if t.recall:
		return start_recall()
	var potency := 1.0 + h.potion_potency_pct / 100.0
	var duration := 1.0 + h.buff_duration_pct / 100.0
	if t.heal > 0 or t.heal_pct > 0:
		var amount := int((t.heal + h.maxhp * t.heal_pct / 100) * potency)
		h.hp = mini(h.maxhp, h.hp + amount)
		Game.good("You drink the %s, healing %d HP." % [name, amount])
		Game.emit_fx({"type": "heal", "who": h, "amount": amount})
		Game.dda_heals += 1
		if h.poison_turns > 0:
			h.poison_turns = 0
			Game.msg("The draught cleanses the poison from your blood.")
	if t.atk_buff > 0:
		h.atk_buff = int(t.atk_buff * potency)
		h.atk_buff_turns = int(t.atk_turns * duration)
		Game.good("Ether courses through you: +%d attack for %d turns." % [h.atk_buff, h.atk_buff_turns])
	if t.def_buff > 0:
		h.def_buff = int(t.def_buff * potency)
		h.def_buff_turns = int(t.def_turns * duration)
		Game.good("Your guard steadies: +%d defence for %d turns." % [h.def_buff, h.def_buff_turns])
	if t.perm_maxhp > 0:
		var amount := int(t.perm_maxhp * potency)
		h.maxhp += amount
		h.hp += amount
		Game.good("You feel permanently sturdier: +%d max HP." % amount)
	if not Dda.supply_saved():       # the Wastes' salt keeps it, sometimes
		Game.take(name)
	if Game.depth > 0:
		end_turn()
	return true


static func start_recall() -> bool:
	if Game.difficulty == C.Difficulty.HARDCORE:
		Game.warn("Hardcore: the charms are dead things here. Nobody is coming to fetch you.")
		return false
	if Game.depth == 0:
		Game.msg("You are already in the city.")
		return false
	if Game.recall_countdown > 0:
		Game.msg("The charm is already working.")
		return false
	if not Game.take("Recall Charm"):
		Game.msg("You have no recall charm.")
		return false
	Game.recall_countdown = H().recall_turns
	Quests.note_recall()
	Game.dda_retreated = true
	Game.msg("You crack a recall charm. Hold steady -- %d turns." % Game.recall_countdown, Color8(150, 230, 255))
	Game.emit_fx({"type": "burst", "at": H().pos(), "radius": 2, "color": Color8(120, 230, 255)})
	end_turn()
	return true


static func equip_ranged(h: Hero, t: Dictionary) -> void:
	h.ranged_name = t.name
	h.ranged_type = t.type
	h.ranged_bonus = t.bonus + t.bonus * h.gear_bonus_pct / 100
	h.ranged_ammo_cost = t.ammo
	h.ranged_cooldown_max = t.cd
	h.ranged_cooldown = 0
	h.ranged_ammo_max = 6 + h.attrs[C.Attr.PRECISION] * 2 + h.ammo_plus * C.UPGRADE_AMMO_STEP
	h.ranged_ammo = h.ranged_ammo_max


static func crystal_ricochet(magnitude: int, what: String) -> bool:
	var h := H()
	if M().district_at(h.x, h.y) != C.District.CRYSTAL or rnd(100) >= C.CRYSTAL_RICOCHET_PCT:
		return false
	var back := maxi(1, magnitude / 2)
	Game.warn("The crystal catches the %s and throws it back at you! (-%d)" % [what, back])
	Game.emit_fx({"type": "burst", "at": h.pos(), "radius": 1, "color": Color8(150, 220, 255)})
	hero_take_damage(back, "a ricochet")
	return true


# ---- spells -----------------------------------------------------------------------
static func learn_spell(idx: int) -> void:
	H().known_spells.append(idx)
	H().spell_cd.append(0)


static func cast_spell(slot: int) -> bool:
	var h := H()
	var m := M()
	request = {}
	if Game.depth == 0:
		Game.msg("Not in the plaza. Somebody would call the watch.")
		return false
	if slot < 0 or slot >= h.known_spells.size():
		return false
	if not Districts.allows(h.x, h.y, C.Act.ARCANE):
		Game.msg("Not here. This ground was marked out for blades.", Gfx.GREY)
		return false
	var s := SpellBook.get_spell(h.known_spells[slot])
	if h.spell_cd[slot] > 0:
		Game.msg("%s is still recharging (%d turns)." % [s.name, h.spell_cd[slot]])
		return false
	if h.aether < s.cost:
		Game.msg("Not enough aether charge to cast %s." % s.name)
		return false
	if _hindered():
		return true
	var mag: int = s.magnitude + s.magnitude * h.spell_power_pct / 100
	var col: Color = C.SCHOOL_COLORS[s.school]
	var spent := func():
		h.aether -= s.cost
		h.spell_cd[slot] = s.cooldown
		h.last_spell_slot = slot
	if crystal_ricochet(mag, s.name):
		spent.call()
		end_turn()
		return true
	var dur: int = s.duration + s.duration * h.buff_duration_pct / 100
	var lv: int = s.level
	var reach: int = SpellBook.LEVEL_REACH[lv]
	var aoe: int = SpellBook.LEVEL_AOE[lv]
	Game.emit_fx({"type": "cast", "who": h, "color": col})
	match s.effect:
		C.Effect.DAMAGE, C.Effect.LIFE_DRAIN, C.Effect.DOT_BURN, C.Effect.CURSE, C.Effect.STUN, C.Effect.SLOW:
			var tg := find_target(reach)
			if tg == null:
				Game.msg("There's nothing in sight to aim %s at." % s.name)
				return false
			_face(h, tg.x - h.x, tg.y - h.y)
			Game.emit_fx({"type": "bolt", "from": h.pos(), "to": tg.pos(), "color": col})
			match s.effect:
				C.Effect.DAMAGE:
					Game.msg("%s tears into the %s for %d." % [s.name, tg.name, mag], col.lightened(0.4))
					monster_take_damage(tg, mag)
				C.Effect.LIFE_DRAIN:
					var heal := mag / 2
					Game.msg("%s drains the %s for %d, healing you for %d." % [s.name, tg.name, mag, heal], col.lightened(0.4))
					monster_take_damage(tg, mag)
					h.hp = mini(h.maxhp, h.hp + heal)
				C.Effect.DOT_BURN:
					Game.msg("%s festers in the %s for %d, and its strength fails." % [s.name, tg.name, mag], col.lightened(0.4))
					tg.jinx_pen = mag / 3
					tg.jinx_turns = dur if dur > 0 else 4
					monster_take_damage(tg, mag)
				C.Effect.CURSE:
					tg.jinx_pen = mag
					tg.jinx_turns = dur if dur > 0 else 4
					tg.aggro = true
					Game.msg("%s settles over the %s -- its attacks falter." % [s.name, tg.name], col.lightened(0.4))
				C.Effect.STUN:
					tg.stun_turns = dur if dur > 0 else 2
					tg.aggro = true
					Game.msg("%s locks the %s in place." % [s.name, tg.name], col.lightened(0.4))
				C.Effect.SLOW:
					tg.slow_turns = dur if dur > 0 else 3
					tg.aggro = true
					Game.msg("%s weighs down the %s." % [s.name, tg.name], col.lightened(0.4))
		C.Effect.CHAIN:
			var tg := find_target(reach)
			if tg == null:
				Game.msg("There's nothing in sight for %s to earth through." % s.name)
				return false
			Game.emit_fx({"type": "bolt", "from": h.pos(), "to": tg.pos(), "color": col})
			Game.msg("%s earths through the %s for %d." % [s.name, tg.name, mag], col.lightened(0.4))
			monster_take_damage(tg, mag)
			var hit: Array = [tg]
			var at := tg.pos()
			var arc := maxi(1, mag / 2)
			for j in 2:
				var nx := find_target_from(at, reach, hit)
				if nx == null:
					break
				Game.emit_fx({"type": "bolt", "from": at, "to": nx.pos(), "color": col})
				monster_take_damage(nx, arc)
				Game.msg("The arc jumps to the %s for %d." % [nx.name, arc], col.lightened(0.4))
				hit.append(nx)
				at = nx.pos()
				arc /= 2
				if arc < 1:
					break
		C.Effect.DAMAGE_NOVA:
			var hits := damage_all_in_reach(aoe + 2, mag)
			if hits == 0:
				Game.msg("%s finds nothing nearby to strike." % s.name)
				return false
			Game.emit_fx({"type": "burst", "at": h.pos(), "radius": aoe + 2, "color": col})
			Game.msg("%s erupts, striking %d foe%s for %d each." % [s.name, hits, "" if hits == 1 else "s", mag], col.lightened(0.4))
		C.Effect.SCORCH:
			for y in range(h.y - aoe, h.y + aoe + 1):
				for x in range(h.x - aoe, h.x + aoe + 1):
					if Vector2i(x, y) != h.pos() and m.t(x, y) == C.Tile.FLOOR and (x - h.x) * (x - h.x) + (y - h.y) * (y - h.y) <= aoe * aoe:
						m.set_t(x, y, C.Tile.MIASMA)
			Game.emit_fx({"type": "burst", "at": h.pos(), "radius": aoe, "color": col})
			Game.msg("%s sears the ground around you." % s.name, col.lightened(0.4))
		C.Effect.BUFF_ATK:
			h.atk_buff = mag
			h.atk_buff_turns = dur
			Game.msg("%s courses through you. +%d attack for %d turns." % [s.name, mag, dur], col.lightened(0.4))
		C.Effect.HEAL_SELF:
			h.hp = mini(h.maxhp, h.hp + mag)
			Game.emit_fx({"type": "heal", "who": h, "amount": mag})
			Game.msg("%s knits you back together for %d HP." % [s.name, mag], col.lightened(0.4))
		C.Effect.PARTY_HEAL:
			# everybody standing within reach of the song, you included
			var hurt: Array = Game.party.filter(func(b): return Party.is_up(b) and b.hp < Party.max_hp(b) \
				and (b.x - h.x) * (b.x - h.x) + (b.y - h.y) * (b.y - h.y) <= 36)
			if hurt.is_empty():
				Game.msg("%s finds nobody who needs it." % s.name)
				return false
			for b in hurt:
				b.hp = mini(Party.max_hp(b), b.hp + mag)
				Game.emit_fx({"type": "heal", "who": b, "amount": mag})
			Game.msg("%s washes over the party for %d HP." % [s.name, mag] if hurt.size() > 1
				else "%s knits you back together for %d HP." % [s.name, mag], col.lightened(0.4))
		C.Effect.WARD_SHIELD:
			h.def_buff = mag
			h.def_buff_turns = dur
			Game.msg("%s settles over you. +%d defence for %d turns." % [s.name, mag, dur], col.lightened(0.4))
		C.Effect.BARRIER:
			var dirs := [Vector2i(-1, 0), Vector2i(1, 0), Vector2i(0, -1), Vector2i(0, 1)]
			dirs.shuffle()
			var done := false
			for d in dirs:
				var p: Vector2i = h.pos() + d
				if m.walkable_player(p.x, p.y) and not m.monster_at(p.x, p.y) and m.t(p.x, p.y) == C.Tile.FLOOR:
					m.set_t(p.x, p.y, C.Tile.WALL)
					done = true
					break
			if not done:
				Game.msg("%s finds nowhere clear to seal." % s.name)
				return false
			Game.msg("%s grinds shut beside you." % s.name, col.lightened(0.4))
		C.Effect.PURGE:
			var n := 0
			for y in range(h.y - aoe - 1, h.y + aoe + 2):
				for x in range(h.x - aoe - 1, h.x + aoe + 2):
					if m.t(x, y) == C.Tile.LAVA or m.t(x, y) == C.Tile.MIASMA:
						m.set_t(x, y, C.Tile.FLOOR)
						n += 1
			if n == 0:
				Game.msg("%s finds nothing here to cleanse." % s.name)
				return false
			Game.emit_fx({"type": "burst", "at": h.pos(), "radius": aoe + 1, "color": col})
			Game.msg("%s clears the hazard from around you." % s.name, col.lightened(0.4))
		C.Effect.BLINK:
			var ok := false
			for tries in 30:
				var nx := h.x + rnd(mag * 2 + 1) - mag
				var ny := h.y + rnd(mag * 2 + 1) - mag
				if not Party.body_at(nx, ny) and m.walkable_player(nx, ny) and not m.monster_at(nx, ny) and m.t(nx, ny) == C.Tile.FLOOR:
					h.x = nx
					h.y = ny
					ok = true
					break
			if not ok:
				Game.msg("%s won't fold -- nowhere clear to land." % s.name)
				return false
			Game.emit_fx({"type": "teleport", "who": h})
			Game.msg("%s, and you're somewhere else." % s.name, col.lightened(0.4))
		C.Effect.REVEAL:
			m.reveal_circle(h.x, h.y, mag)
			Game.emit_fx({"type": "burst", "at": h.pos(), "radius": mini(mag, 8), "color": col})
			Game.msg("%s lights up the passage around you." % s.name, col.lightened(0.4))
		C.Effect.BRIDGE:
			var n := 0
			for y in range(h.y - aoe - 1, h.y + aoe + 2):
				for x in range(h.x - aoe - 1, h.x + aoe + 2):
					if m.t(x, y) == C.Tile.WATER:
						m.set_t(x, y, C.Tile.BRIDGE)
						n += 1
			if n == 0:
				Game.msg("%s finds no water nearby to span." % s.name)
				return false
			Game.msg("%s spans the water around you." % s.name, col.lightened(0.4))
		C.Effect.UNBIND:
			var best := Vector2i(-1, -1)
			var bd := reach * reach + 1
			for y in range(h.y - reach, h.y + reach + 1):
				for x in range(h.x - reach, h.x + reach + 1):
					if m.t(x, y) == C.Tile.LOCKED_DOOR or m.t(x, y) == C.Tile.SEALED_DOOR:
						var d := (x - h.x) * (x - h.x) + (y - h.y) * (y - h.y)
						if d < bd:
							bd = d
							best = Vector2i(x, y)
			if best.x < 0:
				Game.msg("%s finds no locked door nearby." % s.name)
				return false
			m.set_t(best.x, best.y, C.Tile.FLOOR)
			Game.msg("%s unwrites the lock -- the door swings open." % s.name, col.lightened(0.4))
		C.Effect.HASTE:
			var cleared := 0
			for k in h.spell_cd.size():
				if k != slot and h.spell_cd[k] > 0:
					h.spell_cd[k] = 0
					cleared += 1
			if h.ability_cd > 0:
				h.ability_cd = 0
				cleared += 1
			if h.ranged_cooldown > 0:
				h.ranged_cooldown = 0
				cleared += 1
			if cleared == 0:
				Game.msg("%s finds nothing waiting on you." % s.name)
				return false
			Game.msg("%s runs ahead of the beat -- %d things are ready again." % [s.name, cleared], col.lightened(0.4))
		C.Effect.REFLECT:
			h.reflect_pct = mini(60, 20 + lv * 8)
			h.reflect_turns = dur if dur > 0 else 5
			Game.msg("%s settles over you: %d%% of what lands comes back, %d turns." % [s.name, h.reflect_pct, h.reflect_turns], col.lightened(0.4))
		C.Effect.SENSE_LIFE:
			var found := 0
			for mo in m.monsters:
				if mo.alive:
					m.seen[mo.y * m.w + mo.x] = 1
					found += 1
			Game.emit_fx({"type": "sense"})
			Game.msg("%s finds them: %d things are moving on this floor." % [s.name, found], col.lightened(0.4))
		C.Effect.MASS_SLOW:
			var caught := 0
			for mo in m.monsters:
				if mo.alive and (mo.x - h.x) * (mo.x - h.x) + (mo.y - h.y) * (mo.y - h.y) <= (aoe + 2) * (aoe + 2):
					mo.slow_turns += dur if dur > 0 else 4
					caught += 1
			if caught == 0:
				Game.msg("%s is written out over nothing in particular." % s.name)
				return false
			Game.emit_fx({"type": "burst", "at": h.pos(), "radius": aoe + 2, "color": col})
			Game.msg("%s settles over %d of them. Everything here is slower now." % [s.name, caught], col.lightened(0.4))
	spent.call()
	h.still_turns = 0
	end_turn()
	return true


# ---- ranged -----------------------------------------------------------------------
static func fire_ranged() -> bool:
	var h := H()
	request = {}
	if h.ranged_type == C.Ranged.NONE:
		Game.msg("You have no ranged weapon equipped. The Armory sells them.")
		return false
	if Game.depth == 0:
		return false
	if not Districts.allows(h.x, h.y, C.Act.RANGED):
		Game.msg("Not here. Whatever this ground is for, it is not shooting.", Gfx.GREY)
		return false
	if h.ranged_cooldown > 0:
		Game.msg("%s needs a moment to reset. (%d)" % [h.ranged_name, h.ranged_cooldown])
		return false
	if h.ranged_ammo < h.ranged_ammo_cost:
		Game.msg("Not enough ammo left for %s." % h.ranged_name)
		return false
	if _hindered():
		return true
	var reach: int = C.RANGED_REACH[h.ranged_type]
	var dmg := h.ranged_bonus + h.ranged_bonus * h.ranged_dmg_bonus_pct / 100
	var col: Color = C.RANGED_COLORS[h.ranged_type]
	if crystal_ricochet(dmg, h.ranged_name):
		h.ranged_ammo -= h.ranged_ammo_cost
		h.ranged_cooldown = h.ranged_cooldown_max
		end_turn()
		return true
	if h.ranged_type == C.Ranged.GRENADE:
		var hits := damage_all_in_reach(2, dmg)
		if hits == 0:
			Game.msg("%s finds nothing nearby to hit." % h.ranged_name)
			return false
		Game.emit_fx({"type": "burst", "at": h.pos(), "radius": 2, "color": col})
		Game.msg("%s explodes, striking %d foe%s for %d each." % [h.ranged_name, hits, "" if hits == 1 else "s", dmg])
	else:
		var tg := find_target(reach)
		if tg == null:
			Game.msg("Nothing in sight for %s to hit." % h.ranged_name)
			return false
		_face(h, tg.x - h.x, tg.y - h.y)
		Game.emit_fx({"type": "shot", "from": h.pos(), "to": tg.pos(), "color": col, "kind": h.ranged_type})
		match h.ranged_type:
			C.Ranged.LASER:
				Game.msg("%s lances through the %s for %d." % [h.ranged_name, tg.name, dmg])
				monster_take_damage(tg, dmg)
				var second := find_target(reach, tg)
				if second:
					Game.emit_fx({"type": "shot", "from": tg.pos(), "to": second.pos(), "color": col, "kind": h.ranged_type})
					Game.msg("The beam pierces on into the %s for %d." % [second.name, dmg])
					monster_take_damage(second, dmg)
			C.Ranged.BLOWGUN:
				Game.msg("%s needles the %s for %d -- its attacks falter." % [h.ranged_name, tg.name, dmg])
				tg.jinx_pen = dmg / 3 + 1
				tg.jinx_turns = 4
				monster_take_damage(tg, dmg)
			C.Ranged.THROWN:
				var crit := h.crit_pct + h.set_bonus_crit > 0 and rnd(100) < h.crit_pct + h.set_bonus_crit
				var fd := dmg * 2 if crit else dmg
				Game.msg(("A perfect throw! %s finds the %s for %d." if crit else "%s strikes the %s for %d.") % [h.ranged_name, tg.name, fd])
				monster_take_damage(tg, fd)
			_:
				Game.msg("%s strikes the %s for %d." % [h.ranged_name, tg.name, dmg])
				monster_take_damage(tg, dmg)
	h.ranged_ammo -= h.ranged_ammo_cost
	h.ranged_cooldown = h.ranged_cooldown_max
	h.still_turns = 0
	end_turn()
	return true


# ---- class abilities -----------------------------------------------------------------
static func use_ability() -> bool:
	var h := H()
	var m := M()
	request = {}
	if Game.depth == 0:
		return false
	var nm: String = C.ABILITY_NAMES[h.ability_id]
	if h.ability_cd > 0:
		Game.msg("%s isn't ready yet (%d turns)." % [nm, h.ability_cd])
		return false
	if _hindered():
		return true
	var a := h.attrs
	var col := Color8(255, 236, 160)
	match h.ability_id:
		C.Ability.CRUSHING_BLOW, C.Ability.CALLED_SHOT:
			var crushing := h.ability_id == C.Ability.CRUSHING_BLOW
			var tg := find_target(2 if crushing else 6)
			if tg == null:
				Game.msg("There's nothing in reach for %s." % nm)
				return false
			var dmg := (25 + a[C.Attr.MIGHT] * 3) if crushing else (25 + a[C.Attr.PRECISION] * 4)
			_face(h, tg.x - h.x, tg.y - h.y)
			if crushing:
				Game.emit_fx({"type": "attack", "who": h, "target": tg, "crit": true})
			else:
				Game.emit_fx({"type": "shot", "from": h.pos(), "to": tg.pos(), "color": col, "kind": C.Ranged.BOW})
			Game.msg("%s crashes into the %s for %d." % [nm, tg.name, dmg], col)
			monster_take_damage(tg, dmg)
		C.Ability.ADRENALINE, C.Ability.IRON_RESOLVE, C.Ability.SECOND_WIND:
			var heal := 20 + a[C.Attr.BRAWN] * 4
			if h.ability_id == C.Ability.IRON_RESOLVE:
				heal = 25 + a[C.Attr.FORTITUDE] * 4
			elif h.ability_id == C.Ability.SECOND_WIND:
				heal = 15 + a[C.Attr.STAMINA] * 3
				h.atk_buff = 8
				h.atk_buff_turns = 8
			h.hp = mini(h.maxhp, h.hp + heal)
			Game.emit_fx({"type": "heal", "who": h, "amount": heal})
			Game.msg("%s surges through you, healing %d HP." % [nm, heal], col)
		C.Ability.EVASIVE_ROLL:
			h.evasion_buff = 15 + a[C.Attr.AGILITY] * 2
			h.evasion_buff_turns = 8
			Game.msg("%s -- +%d%% evasion for 8 turns." % [nm, h.evasion_buff], col)
		C.Ability.RIPOSTE:
			h.atk_buff = 10 + a[C.Attr.REFLEXES] * 2
			h.atk_buff_turns = 8
			Game.msg("%s -- +%d attack for 8 turns." % [nm, h.atk_buff], col)
		C.Ability.UNBREAKABLE:
			h.def_buff = 12 + a[C.Attr.GRIT] * 2
			h.def_buff_turns = 10
			Game.msg("%s -- +%d defence for 10 turns." % [nm, h.def_buff], col)
		C.Ability.STEADY_FOOTING:
			h.evasion_buff = 10 + a[C.Attr.BALANCE] * 2
			h.evasion_buff_turns = 8
			h.def_buff = 8
			h.def_buff_turns = 8
			Game.msg("%s -- +%d%% evasion and +8 defence for 8 turns." % [nm, h.evasion_buff], col)
		C.Ability.PREDATORS_SENSE:
			var tg := find_target(6)
			m.reveal_circle(h.x, h.y, 6)
			Game.emit_fx({"type": "burst", "at": h.pos(), "radius": 6, "color": col})
			if tg:
				tg.stun_turns = 3 + a[C.Attr.INSTINCT] / 3
				Game.msg("%s locks the %s in place and lights up your surroundings." % [nm, tg.name], col)
			else:
				Game.msg("%s lights up your surroundings." % nm, col)
	Game.emit_fx({"type": "cast", "who": h, "color": col})
	h.ability_cd = C.ABILITY_COOLDOWNS[h.ability_id]
	h.still_turns = 0
	end_turn()
	return true
