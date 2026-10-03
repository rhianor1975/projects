extends Node

func _ready() -> void:
	Game.persist = false
	Game.new_run(5, "Dbg", C.Difficulty.NORMAL, C.WorldSize.SHAFT, 4247)
	Game.hero.maxhp *= 20
	Game.hero.hp = Game.hero.maxhp
	Game.enter_floor(1)
	AutoExplore.reset()
	var last := Vector2i.ZERO
	for i in 2500:
		var why := AutoExplore.step()
		if i % 250 == 0 or why != "":
			var m: GameMap = Game.map
			var ag := 0
			for mo in m.monsters:
				if mo.aggro: ag += 1
			print("%d pos %s stairs %s seen-stairs %s seen %d mons %d aggro %d floor %d hp %d log: %s %s" % [i, Game.hero.pos(), m.stairs_down, m.is_seen(m.stairs_down.x, m.stairs_down.y), m.seen.count(1), m.monsters.size(), ag, Game.depth, Game.hero.hp, Game.log_lines[-1][0] if Game.log_lines.size() else "", why])
		if why != "":
			var m2: GameMap = Game.map
			var h = Game.hero
			for yy in range(h.y - 6, h.y + 7):
				var row := ""
				for xx in range(h.x - 12, h.x + 13):
					var c := "?"
					if not m2.inb(xx, yy): c = " "
					elif Vector2i(xx, yy) == h.pos(): c = "@"
					elif m2.monster_at(xx, yy): c = "M"
					elif not m2.items_at(xx, yy).is_empty(): c = "$"
					elif not m2.is_seen(xx, yy): c = " "
					else: c = ["#", ".", ">", "<", ",", "~", "=", "L", "%", "O", "+", "D", "/", "T", "*", "b", "r", "u", "g", "&", "\"", "f"][m2.t(xx, yy)] if m2.t(xx, yy) < 22 else "?"
					row += c
				print(row)
			for l in Game.log_lines.slice(-6): print("  > ", l[0])
			break
	get_tree().quit()
