class_name Menu
extends RefCounted
## A list menu in the SNES style: a title bar, a scrolling list with a
## cursor, optional tabs (Left/Right), a detail panel to the right and a
## description box underneath. Every shop, the pack, the spell list and the
## creation screens are one of these with different contents.

var title := ""
var subtitle := ""
var items: Array = []          # {label, right, color, enabled, desc}
var cursor := 0
var scroll := 0
var tabs: Array = []
var tab := 0
var rebuild: Callable          # (menu) -> refill items after a change or tab switch
var on_select: Callable        # (menu, item) -> true to close
var on_cancel: Callable        # (menu) -> true to close (default: close)
var detail: Callable           # (ci, rect, menu, item) -> draws the right panel
var footer := "Z/Enter: choose    X/Esc: back"
var list_width := 340
var rows := 11
var show_gold := true
var closed := false
var flash := ""                # a one-line result shown in the description box
var flash_t := 0.0
var data := {}


func refresh() -> void:
	if rebuild.is_valid():
		rebuild.call(self)
	cursor = clampi(cursor, 0, maxi(0, items.size() - 1))
	_keep_visible()


func current() -> Dictionary:
	return items[cursor] if cursor >= 0 and cursor < items.size() else {}


func _keep_visible() -> void:
	if cursor < scroll:
		scroll = cursor
	if cursor >= scroll + rows:
		scroll = cursor - rows + 1
	scroll = clampi(scroll, 0, maxi(0, items.size() - rows))


func say(text: String) -> void:
	flash = text
	flash_t = 3.5


## Returns true when the event was used.
func input(ev: InputEvent) -> bool:
	if not (ev is InputEventKey and ev.pressed) and not (ev is InputEventJoypadButton and ev.pressed):
		return false
	var up: bool = ev.is_action_pressed("ui_up") or (ev is InputEventKey and ev.keycode in [KEY_K, KEY_KP_8])
	var down: bool = ev.is_action_pressed("ui_down") or (ev is InputEventKey and ev.keycode in [KEY_J, KEY_KP_2])
	var left: bool = ev.is_action_pressed("ui_left") or (ev is InputEventKey and ev.keycode in [KEY_H, KEY_KP_4])
	var right: bool = ev.is_action_pressed("ui_right") or (ev is InputEventKey and ev.keycode in [KEY_L, KEY_KP_6])
	var accept: bool = ev.is_action_pressed("ui_accept") or (ev is InputEventKey and ev.keycode in [KEY_Z, KEY_ENTER, KEY_KP_ENTER])
	var cancel: bool = ev.is_action_pressed("ui_cancel") or (ev is InputEventKey and ev.keycode in [KEY_X, KEY_ESCAPE])
	if up and not items.is_empty():
		cursor = (cursor - 1 + items.size()) % items.size()
		_keep_visible()
		Sfx.play("cursor")
	elif down and not items.is_empty():
		cursor = (cursor + 1) % items.size()
		_keep_visible()
		Sfx.play("cursor")
	elif (left or right) and tabs.size() > 1:
		tab = (tab + (1 if right else -1) + tabs.size()) % tabs.size()
		cursor = 0
		scroll = 0
		refresh()
		Sfx.play("cursor")
	elif (left or right) and ev is InputEventKey and items.size() > rows:
		cursor = clampi(cursor + (rows if right else -rows), 0, items.size() - 1)
		_keep_visible()
	elif accept and not items.is_empty():
		var it := current()
		if it.get("enabled", true) == false:
			Sfx.play("buzz")
			if it.has("why"):
				say(it.why)
			return true
		if on_select.is_valid() and on_select.call(self, it):
			closed = true
		refresh()
	elif cancel:
		Sfx.play("back")
		if not on_cancel.is_valid() or on_cancel.call(self):
			closed = true
	else:
		return false
	return true


func draw(ci: CanvasItem, t: float) -> void:
	var W := 640.0
	flash_t -= ci.get_process_delta_time()
	Gfx.window(ci, Rect2(6, 6, 300, 30))
	Gfx.text(ci, Vector2(18, 13), title, Gfx.GOLD, 2)
	if show_gold:
		Gfx.window(ci, Rect2(W - 150, 6, 144, 30))
		Gfx.text(ci, Vector2(W - 138, 17), "G", Gfx.GOLD)
		Gfx.text(ci, Vector2(W - 124, 17), Gfx.comma(Game.gold), Gfx.WHITE)
	var top := 40.0
	if tabs.size() > 1:
		var x := 6.0
		for i in tabs.size():
			var w := Gfx.text_width(tabs[i]) + 16
			if i == tab:
				Gfx.window(ci, Rect2(x, top, w, 18))
			else:
				Gfx.window(ci, Rect2(x, top, w, 18), Color8(36, 40, 96), Color8(14, 14, 50))
			Gfx.text(ci, Vector2(x + 8, top + 5), tabs[i], Gfx.WHITE if i == tab else Gfx.DIM)
			x += w + 2
		top += 20
	var list_h := rows * 18 + 16
	Gfx.window(ci, Rect2(6, top, list_width, list_h))
	if items.is_empty():
		Gfx.text(ci, Vector2(28, top + 12), "Nothing here.", Gfx.DIM)
	for row in rows:
		var i := scroll + row
		if i >= items.size():
			break
		var it: Dictionary = items[i]
		var y := top + 10 + row * 18
		var col: Color = it.get("color", Gfx.WHITE)
		if it.get("enabled", true) == false:
			col = Gfx.DIM
		if i == cursor:
			Gfx.cursor(ci, Vector2(14, y), t)
			if it.get("enabled", true) != false:
				col = Gfx.GOLD if col == Gfx.WHITE else col.lightened(0.2)
		Gfx.text(ci, Vector2(30, y), it.label, col)
		if it.has("right"):
			Gfx.text_right(ci, Vector2(list_width - 6, y), str(it.right), col)
	if scroll > 0:
		Gfx.text(ci, Vector2(list_width - 10, top + 2), "▲", Gfx.GOLD, 1, false)
	if scroll + rows < items.size():
		Gfx.text(ci, Vector2(list_width - 10, top + list_h - 10), "▼", Gfx.GOLD, 1, false)
	var dr := Rect2(10 + list_width, top, W - 16 - list_width, list_h)
	Gfx.window(ci, dr)
	if detail.is_valid():
		detail.call(ci, dr, self, current())
	var by := top + list_h + 4
	Gfx.window(ci, Rect2(6, by, W - 12, 360 - by - 6))
	var desc: String = flash if flash_t > 0 and flash != "" else str(current().get("desc", ""))
	var lines := Gfx.wrap(desc, int(W - 40))
	for i in mini(lines.size(), 3):
		Gfx.text(ci, Vector2(18, by + 10 + i * 14), lines[i], Gfx.WHITE if not (flash_t > 0) else Gfx.GOLD)
	Gfx.text(ci, Vector2(18, 360 - 20), footer, Gfx.DIM)
