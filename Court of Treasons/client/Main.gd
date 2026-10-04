# The table.
#
# Everything drawn here came out of a View the authority sent.  There is
# no Game in this file and no copy of one: if a thing is not in the View
# it cannot be drawn, which is how requirement N1 survives contact with
# a UI.  A client that filtered hidden state itself would be a client
# that had been told it.
#
# The layout is the one in TABLE.png, which was drawn before any of this
# existed so that the screen could be argued about before a toolkit was
# chosen.  The vertical budget is declared once, because the first still
# was laid out by eye and the hand ended up drawn through the status bar.
extends Control

const HOST_DEFAULT := "192.168.0.50"
const PORT_DEFAULT := 9017

const W := 1920.0             # the size the layout was designed at
const PANEL_W := 470.0        # the right-hand panel, which never stretches

# What the window actually is, in canvas units, refreshed every frame.
#
# The project stretches canvas_items and expands the aspect, so at 16:9
# these are exactly 1920x1080 whatever the monitor is.  At any other
# shape the viewport grows in one axis -- and everything here used to be
# drawn against the constants, so an ultrawide got a dead unpainted band
# down the right where the panel and the table both stopped short.
#
# Extra width becomes more table, which is where the room is wanted;
# extra height opens the middle, between the far lords and your own.
# With stretch aspect "keep" these are always 1920x1080 whatever the
# window is -- Godot scales the whole canvas uniformly and puts bars on
# anything that is not 16:9, so nothing here has to think about it.
#
# They are read from the viewport rather than assumed because the aspect
# is one line in project.godot: set it back to "expand" and the layout
# uses the extra room instead of being bordered by it -- the panel stays
# on the right edge, the table takes the width, the middle takes the
# height.  Both work; "keep" is the one chosen, because a card layout
# scaled whole is the same layout, and a reflowed one is a second layout
# to look after.
var vw := 1920.0
var vh := 1080.0
var right := 1450.0

# The layout, measured each frame instead of pinned to 1080.
#
# Scaling the canvas up makes it SMALLER in layout units -- 1080/1.4 is
# 771 -- so a layout authored at 1080 runs out of room exactly when a
# person asks for bigger type.  Clamping the rows only moved the
# collision about.  These are computed: the cards and bars shrink with
# the canvas, the rows are laid out from the top down and the bottom up,
# and the middle is whatever is left between them.
var bar_h := 112.0        # a House's bar
var lord_w := 96.0
var lord_h := 134.0
var row_h := 178.0        # a lord card plus its two tracker bars
var hand_w := 140.0
var hand_h := 196.0
var y_their_lords := 138.0
var y_table := 330.0
var y_your_lords := 580.0
var y_your_bar := 772.0
var y_hand := 876.0
var tight := false        # too little room for the decks on the table
var stack := false        # lords as chips rather than as a gallery

func _measure() -> void:
	# Two rearrangements, not a shrink.  When the canvas is short the
	# lords become chips and the decks move into the panel; the cards in
	# hand and the type stay the size they were drawn at, which is the
	# entire point of doing it this way.
	var k: float = clampf(vh / 1080.0, 0.55, 1.25)
	bar_h  = 92.0 * clampf(k, 0.82, 1.0)
	hand_w = 140.0 * clampf(k, 0.88, 1.0)
	hand_h = hand_w * 717.0 / 512.0
	lord_w = 96.0
	lord_h = lord_w * 717.0 / 512.0
	# The lower bar carries a levy line and an unrest row the upper one
	# does not, so it needs its content height and not its box height.
	# The player's own bar carries a levy line under each plaque, so it
	# needs 96 where the opponent's needs none of it.
	var bar_full: float = maxf(bar_h, 96.0 * clampf(k, 0.88, 1.0))

	# Gallery first; chips only if the gallery will not fit.
	stack = false
	row_h = lord_h + 42.0
	for _pass in range(2):
		var top: float = bar_h + 22.0 + row_h
		var bot: float = row_h + 12.0 + bar_full + 6.0 + hand_h
		if vh - top - bot >= lord_h + 70.0:
			break
		stack = true
		row_h = CHIP_H + 10.0

	# The door's field slots, worked out here rather than while drawing
	# it.  _place_fields runs before _draw_login, so computing them in
	# the draw left the widgets a frame behind -- and with nothing to
	# force a second redraw they simply stayed there, sitting on top of
	# the PLAY button.
	var dcw: float = minf(940.0, vw - 180.0)
	field_x = vw * 0.5 - dcw * 0.5 + 250.0
	# The MULTIPLAYER screen only: title, the screen heading, the line
	# under it, then the first slot.  Kept as a sum of the same steps
	# _draw_login walks so the two cannot drift -- they did once when
	# the boxes climbed into the text, and again when PLAY grew a screen
	# of its own and this was still counting the House picker that had
	# moved off the front page.
	field_y = vh * 0.5 - 330.0 + 54.0 + 46.0 + 84.0 + 2.0
	field_w = dcw - 250.0 - 40.0

	y_their_lords = bar_h + 22.0
	y_table       = y_their_lords + row_h + 14.0
	y_hand        = vh - hand_h - 6.0
	y_your_bar    = y_hand - bar_full - 4.0
	y_your_lords  = y_your_bar - row_h - 12.0
	tight = (y_your_lords - y_table) < 230.0 or (right - PROM_X) < 700.0

# RIGHT was a constant; `right` is now computed from the window each
# frame.  The constant is kept only as the design width it was chosen at.
const RIGHT := 1450.0
# Slate, not leather.  Brown type on brown ground is the whole reason
# this was hard to read; a neutral dark ground lets the brass, the cream
# plates and the three resource colours all sit at full contrast against
# it.  His second mockup is right about this.
const GROUND := Color("23262a")
const BRASS := Color("9c7434")
const BRASS_LIT := Color("d8a552")
const PLATE := Color("efe0bd")      # cream, for text that must be read
const PLATE_INK := Color("221a10")
const PANEL := Color("1c1812")
const INK := Color("e8dab8")
const DIM := Color("8a7a5c")
const PALE := Color("f4ead2")
const WARN := Color("c98a72")

const Y_THEIR_BAR := 0.0
const Y_THEIR_LORDS := 138.0
const Y_TABLE := 330.0
const UP_YOUR_LORDS := 500.0   # from the bottom
const UP_YOUR_BAR := 308.0
const UP_HAND := 204.0
const LORD_W := 96.0
const HAND_W := 140.0
# Where the promises column starts.  A constant because it was 266, which
# was clear of two unbound lords and not of four.
const PROM_X := 560.0

# EIGHT Houses.  These three were four-entry tables and the engine deals
# eight, so _house_bar indexed them with 6 and 7 and threw on every frame
# a new House was seated -- and a _draw that throws stops drawing, so a
# player seated as one of them met a half-drawn screen.  Found by taking
# a screenshot; no C test can see it.
# The five steps of a turn, in the order design.xml's <sequence> gives
# them.  The engine walks these; view.step is which one is open, or 5
# when no turn is running.
const STEP_NAME := ["Levy", "Draw", "First Court", "Declare",
	"Second Court"]

const HOUSE_NAMES := ["RAVENMARK", "VIPREN", "GOLDWYN", "ALDEMAR",
	"LEOWARD", "STONEGARTH", "WULFREN", "EVERHOLD"]
const HOUSE_WORDS := ["By Sword and Storm", "What is Whispered is Owned",
	"Every Throne Has a Price", "The Realm Remembers",
	"A Lion's Word is a Lion's Claw", "What Is Bound Stays Bound",
	"The Pack Remembers", "It Does Not Stop Coming"]
const HOUSE_TINT := [Color("e8b4a0"), Color("9fe0bb"), Color("e8cf92"), Color("d8d2bc"),
	Color("f0c884"), Color("cfc48a"), Color("a8bcd0"), Color("e0a0a4")]
const RES_NAME := ["MILITARY", "CAPITAL", "GOLD"]
# Saturated on purpose.  Three desaturated browns are three browns; the
# whole point of colour here is that a resource is identifiable without
# reading its label, which matters most to the person who finds the
# labels hard to read.
const RES_COL := [Color("d4552f"), Color("3f8fd0"), Color("e8b02c")]
const TERMS := ["Vote", "Peace", "Tribute", "Manumission", "Abstention",
	"Forbearance", "Restraint", "Disclosure", "any term"]
const TRAITS := ["In Debt", "Afraid", "Ambitious", "Proud", "Owed"]

# Preloaded rather than relying on class_name: the global class cache is
# built when a project is opened, and a client that only ever runs from
# the command line may never have been.
const NetScript = preload("res://Net.gd")
var net = null
var view: Dictionary = {}
var legal: Array = []
var asking := ""
var ask_a := 0
var ask_b := 0
var status := "connecting"
var log_lines: Array[String] = []
var cards: Dictionary = {}               # id -> Texture2D
var tex_table: Texture2D
var tex_panel: Texture2D
var tex_bar: Texture2D
var tex_vig: Texture2D
var bg_names: PackedStringArray = []   # "" is the painted table
var bg_pick := 0
var bg_force := -1
# Random legal play, driven through the client rather than the wire,
# so a UI fault has the same chance of firing as an engine one.
var chaos := 0
var chaos_rng := RandomNumberGenerator.new()
var bg_tex: Texture2D
var tex_shadow: Texture2D
var f_serif: Font
var f_bold: Font
var f_ital: Font
var card_meta: Dictionary = {}           # index -> {id, name}
var hover := -1
var buttons: Array = []                  # {rect, action} for the click test
# Every card drawn this frame, so the mouse can find one.  Rebuilt each
# _draw and hit-tested against the frame before, which is what an
# immediate-mode surface has instead of a widget tree.
var card_rects: Array = []               # {rect, idx}
var hover_card := -1                     # the card id under the mouse
# The hovered card is held back and drawn after everything else.  Drawn
# in place it grew into its right-hand neighbour, which was then drawn
# over the top of it, and a card that grows and is immediately clipped
# looks like a fault rather than a response.
var top_card: Dictionary = {}

# Talk, which exists only when there is somebody to talk to.  Against the
# machine the box would be a lie, so it is not drawn.
const CHAT_X := 780.0
const UP_CHAT := 208.0
const CHAT_W := 650.0
const CHAT_H := 200.0
var pvp := false
var chat_lines: Array[String] = []
var chat_input: LineEdit = null

# Where we are: the door, the room, or a game.  One screen at a time,
# because a lobby behind a live board is a lobby nobody looks at.
var screen := "login"          # login | lobby | game
var me_name := ""
var lobby_users: Array = []    # [{i, name, seek}]
var seeking := false
var denied := ""
var fld_host: LineEdit = null
var fld_name: LineEdit = null
var fld_pass: LineEdit = null
var lobby_rows: Array = []     # {rect, i} for the click test
var hover_row := -1
var last_host := ""            # to offer the same door again
var last_port := 0
var outcome := ""              # how the last game ended
# An engine started by this client, for a game that needs nobody else.
# The server is an option, not a requirement: the same binary serves a
# room on a machine down the hall or one game on this one.
var local_pid := -1
# Help, generated from design.xml by tools/mkhelp.py so it cannot drift
# from the rules it explains.  Hover anything on the table for a line;
# press ? for the whole thing.
var help: Dictionary = {}          # key -> {title, body}
var help_spots: Array = []         # {rect, key} hover regions
var help_open := false
var hover_help := ""
# The card backs.  Fifteen of them were generated, copied into the
# client and imported by Godot, and drawn nowhere at all -- while the
# only sign that a deck existed was a line of text saying "draw from
# War".  A deck you draw from should be an object on the table.
#
# Indexed by the engine's DeckId: the four Houses, then War, Political,
# Intrigue, Ambition, World, Court, Throne, Dead.
const BACK_FILE := ["ravenmark", "vipren", "goldwyn", "aldemar",
	"leoward", "stonegarth", "wulfren", "everhold",
	"war", "political", "intrigue", "ambition", "world", "court",
	"throne", "realm", "cataclysm"]
var backs: Dictionary = {}     # deck id -> Texture2D
var cards_full: Dictionary = {}  # idx -> the 512-wide face, for reading
var dec_budget := 0            # decodes left this frame
var dec_more := false          # something was put off until the next one
var deck_spots: Array = []     # {rect, action} so a deck can be clicked
var hover_deck := -1
# How large everything is drawn.
#
# The layout is authored at 1920x1080 and Godot scales the whole canvas,
# so raising this makes every glyph and every card bigger together --
# there is no separate "font size" to get out of step with the boxes
# around it.  The cost is that less fits: at 1.4 the canvas is 1371
# units wide, and the layout follows the viewport so it keeps up.
const SCALE_MIN := 1.0
# The layout rearranges now instead of only shrinking, so this can go
# further than the 1.10 the fixed layout could take.  1.60 is where the
# hand stops fitting five cards beside the talk box.
const SCALE_MAX := 1.60
const SCALE_FILE := "user://scale.txt"
const BG_FILE := "user://background.txt"
# Where a person can drop one.  Scanned in this order; the first that
# exists is used, so a folder beside the game beats the hidden one.
const BG_DIRS := ["res://backgrounds", "user://backgrounds"]
var ui_scale := 1.0
var scale_shown := 0.0
var bg_shown := 0.0         # seconds left showing the readout

func _ready() -> void:
	set_process(true)
	# A window that cannot be made big is a window that gets complained
	# about.  F11 or alt+enter, and escape leaves fullscreen only -- it
	# must not be a way to quit a game by accident.
	get_window().unresizable = false
	# --scale= on the command line wins over the kept value, so a
	# screenshot can be taken at a size nobody has chosen.
	var forced := 0.0
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--scale="):
			forced = a.substr(8).to_float()
		# Read in THIS pass, not the later one: _load_background runs
		# a few lines below and the second pass has not happened yet.
		if a.begins_with("--bg="):
			bg_force = int(a.substr(5))
		# This pass, not the later one: _load_skin runs a few lines down.
		if a.begins_with("--pick="):
			want_house = int(a.substr(7))
		if a.begins_with("--menu="):
			menu_force = a.substr(7)
		if a.begins_with("--panel-tex="):
			panel_tex_path = a.substr(12)
	if forced > 0.0:
		fk = clampf(forced, 0.85, 1.35)
		_apply_scale()
	else:
		_load_scale()
	_load_background()
	_load_cards()
	_load_skin()
	_load_help()
	_load_houses()
	_load_backs()
	net = NetScript.new()
	net.message.connect(_on_message)
	net.closed.connect(_on_closed)
	var host := HOST_DEFAULT
	var port := PORT_DEFAULT
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--ask="):
			auto_ask = a.substr(6)
		if a == "--hover-playable":
			auto_playable = true
		if a == "--help-screen":
			auto_help = true
		if a.begins_with("--hover-help="):
			auto_hover = a.substr(13)
		if a.begins_with("--scale="):
			fk = clampf(a.substr(8).to_float(), 0.85, 1.35)
		if a.begins_with("--step="):
			mock_step = int(a.substr(7))
		if a.begins_with("--chaos="):
			chaos = int(a.substr(8))
			chaos_rng.seed = chaos
		if a == "--times":
			times = true
		if a == "--walk":
			walk = true
		if a == "--menu-test":
			menu_test = true
		if a == "--art-check":
			art_check = true
		if a == "--hit-test":
			hit_test = true
		if a == "--panel-right":
			panel_left = false
		if a.begins_with("--stall="):
			stall = int(a.substr(8))
		if a == "--step-theirs":
			mock_theirs = true
		if a == "--here":
			auto_here = true
		if a == "--alone":
			auto_alone = true
		if a == "--auto":
			auto_play = true
		if a.begins_with("--hover-btn="):
			hover_btn = int(a.substr(12))
		if a == "--click-card":
			auto_click = true
		if a == "--audit":
			audit = true
		if a == "--sit":
			auto_sit = true
		if a == "--seek":
			auto_seek = true
		if a.begins_with("--login="):
			auto_login = a.substr(8)
		if a.begins_with("--hover="):
			hover_probe = int(a.substr(8))
		if a.begins_with("--shot="):
			shot_path = a.substr(7)
			shot_after = 6.0
		# Six seconds is longer than a whole --auto game, so a shot
		# meant to catch the table caught the login screen after it.
		if a.begins_with("--shot-at="):
			shot_after = float(a.substr(10))
		if a == "--pvp":
			pvp = true
		if a.begins_with("--host="):
			var hp := a.substr(7).split(":")
			host = hp[0]
			if hp.size() > 1:
				port = int(hp[1])
	# Use the whole screen, now that the arguments have been read.
	# Scaling the canvas to a 2K display is a third larger for free, and
	# larger for free beats larger for a layout that no longer fits.
	# Not while taking a picture: a maximised window would photograph at
	# whatever this monitor happens to be.
	if shot_path == "":
		get_window().mode = Window.MODE_MAXIMIZED
	if not pvp:
		# The door.  Nothing is sent, and no connection is made, until
		# there is a name and a password to send with it.
		_build_login(host, port)
		if auto_here:
			_play_here()
			return
		if auto_login != "":
			var np := auto_login.split(":")
			fld_name.text = np[0]
			fld_pass.text = np[1] if np.size() > 1 else "x"
			_do_login()
		return
	var err: String = net.connect_to(host, port)
	status = err if err != "" else "waiting for the authority"
	_say("joined %s:%d" % [host, port] if err == "" else err)
	if pvp and err == "":
		# A client that wants a person says so on joining; one that says
		# nothing gets the machine.
		net.send({"join": "pvp"})
		status = "waiting for another player"
		_build_chat()

func _field(y: float, label: String, text: String, secret := false) -> LineEdit:
	var e := LineEdit.new()
	e.text = text
	e.placeholder_text = label
	e.secret = secret
	e.max_length = 60
	e.position = Vector2(760, y)
	e.size = Vector2(400, 40)
	e.add_theme_font_override("font", _f())
	e.add_theme_font_size_override("font_size", 20)
	e.add_theme_color_override("font_color", PALE)
	e.add_theme_color_override("font_placeholder_color", Color("6a5c46"))
	e.add_theme_color_override("caret_color", Color("c9a86a"))
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.06, 0.05, 0.04, 0.9)
	sb.border_color = Color(0.45, 0.38, 0.26)
	sb.set_border_width_all(1)
	sb.content_margin_left = 10
	e.add_theme_stylebox_override("normal", sb)
	e.add_theme_stylebox_override("focus", sb)
	add_child(e)
	return e

# The game is over when the authority's child exits, which closes the
# socket -- so a dropped connection during a game is the end of that
# game, and the way back to the room is through the door again.
#
# The password is not kept to make that automatic.  It is cleared when it
# is sent and it stays cleared; typing it once more after a whole game is
# a smaller cost than a client that holds a secret all evening.
func _on_closed(reason: String) -> void:
	# The result screen outlives the connection that produced it.  The
	# engine exits the moment it has announced a winner, so this arrives
	# immediately afterwards and used to throw the result away.
	if screen == "over":
		_stop_local()
		return
	if screen == "game":
		if outcome == "":
			outcome = "the game ended: %s" % reason
		denied = outcome
		screen = "login"
		view = {}
		legal = []
		asking = ""
		chat_lines.clear()
		if chat_input: chat_input.queue_free()
		chat_input = null
		lobby_users = []
		seeking = false
		_stop_local()
		_build_login(last_host, last_port)
	else:
		status = reason
	queue_redraw()

# Entering the login screen no longer builds a form, because the form
# is no longer the front page: it is behind MULTIPLAYER.  The fields are
# real Control nodes, so they are made when that door is opened and
# freed when it is closed -- otherwise they hang over the main menu,
# invisible and still taking the keyboard.
func _build_login(host: String, port: int) -> void:
	menu = menu_force if menu_force != "" else "main"
	last_host = host
	last_port = port
	_free_fields()
	if menu == "multiplayer":
		call_deferred("_build_fields", host, port)

func _free_fields() -> void:
	for e in [fld_host, fld_name, fld_pass]:
		if e:
			e.queue_free()
	fld_host = null
	fld_name = null
	fld_pass = null

func _build_fields(host: String, port: int) -> void:
	_free_fields()
	fld_host = _field(430, "server", "%s:%d" % [host, port])
	# The name comes back with you; only the password does not.
	fld_name = _field(500, "name", me_name)
	# secret, so a password is not readable over the shoulder of whoever
	# is playing.  It is still sent in the clear -- the screen says so.
	fld_pass = _field(570, "password", "", true)
	for e in [fld_host, fld_name, fld_pass]:
		# A refusal that outlives the thing it refused is a lie about the
		# current state of the form.
		e.text_changed.connect(func(_t):
			if denied != "":
				denied = ""
				queue_redraw())
	fld_pass.text_submitted.connect(func(_t): _do_login())
	fld_name.text_submitted.connect(func(_t): fld_pass.grab_focus())
	if me_name == "":
		fld_name.grab_focus()
	else:
		fld_pass.grab_focus()

# The engine, wherever it is next to us.  A build that ships would put it
# beside the executable; a checkout has it one level up from the project.
func _engine_path() -> String:
	var here := OS.get_executable_path().get_base_dir()
	for p in [ProjectSettings.globalize_path("res://../court"),
			here.path_join("court"),
			ProjectSettings.globalize_path("res://court")]:
		if FileAccess.file_exists(p):
			return p
	return ""

func _play_here() -> void:
	if times: print("  PLAY clicked      %d ms" % Time.get_ticks_msec())
	var exe := _engine_path()
	if exe == "":
		denied = "cannot find the engine next to this client"
		queue_redraw()
		return
	# A port nobody else is on.  If the guess collides the connect fails
	# and says so, which is better than silently joining someone else.
	var port := 20000 + (randi() % 20000)
	# --solo, not --duel: this engine belongs to this client and must not
	# pair whoever else finds the port.
	var args := ["--serve", "0", "--solo", "--houses", "2",
		"--port", str(port)]
	if want_house >= 0:
		args.append("--house")
		args.append(str(want_house))
	if times: print("  engine found      %d ms" % Time.get_ticks_msec())
	local_pid = OS.create_process(exe, args)
	if local_pid <= 0:
		denied = "could not start the engine"
		queue_redraw()
		return
	# It has to finish binding before anything can connect, so this
	# retries -- but each attempt has to be SHORT.
	#
	# It used to call connect_to sixty times, and connect_to blocks for
	# two seconds before giving up: two minutes of a window that does
	# not repaint and macOS calling it Not Responding.  Fifteen attempts
	# of 120ms is under two seconds in the worst case, and the engine we
	# just started binds in the first one or two.
	var err := "not started"
	for _i in range(15):
		err = net.connect_to("127.0.0.1", port, 120)
		if err == "":
			break
		OS.delay_msec(20)
	if err != "":
		denied = err
		_stop_local()
		queue_redraw()
		return
	if times: print("  connected         %d ms" % Time.get_ticks_msec())
	me_name = "you"
	denied = ""
	outcome = ""
	screen = "game"
	pvp = false
	status = "your move"
	for e in [fld_host, fld_name, fld_pass]:
		if e: e.queue_free()
	fld_host = null; fld_name = null; fld_pass = null
	queue_redraw()

func _stop_local() -> void:
	if local_pid > 0:
		OS.kill(local_pid)
		local_pid = -1

func _notification(what: int) -> void:
	# The engine we started is ours to end.  Leaving it running would put
	# a stray listener on this machine every time the client is closed.
	if what == NOTIFICATION_WM_CLOSE_REQUEST \
			or what == NOTIFICATION_PREDELETE \
			or what == NOTIFICATION_EXIT_TREE:
		_stop_local()

func _do_login() -> void:
	var hp: PackedStringArray = fld_host.text.strip_edges().split(":")
	var host := hp[0]
	var port := PORT_DEFAULT
	if hp.size() > 1:
		port = int(hp[1])
	if fld_name.text.strip_edges() == "" or fld_pass.text == "":
		denied = "a name and a password are needed"
		queue_redraw()
		return
	denied = ""
	last_host = host
	last_port = port
	var err: String = net.connect_to(host, port)
	if err != "":
		denied = err
		queue_redraw()
		return
	net.send({"hello": fld_name.text.strip_edges(), "pass": fld_pass.text})
	# Not kept a moment longer than it takes to send.
	fld_pass.text = ""
	status = "knocking"
	queue_redraw()

func _enter_lobby() -> void:
	screen = "lobby"
	status = "in the antechamber"
	denied = ""
	for e in [fld_host, fld_name, fld_pass]:
		if e: e.queue_free()
	fld_host = null; fld_name = null; fld_pass = null
	_build_chat()

# Nodes are placed in pixels, so they do not move when the window does.
# Put them where they belong every frame instead: it is a handful of
# assignments and it removes a whole class of "only right at 1920".
func _place_fields() -> void:
	if chat_input:
		if screen == "lobby":
			chat_input.position = Vector2(60, vh - 100.0)
			chat_input.size = Vector2(minf(880.0, vw - 120.0), 36)
		else:
			chat_input.position = Vector2(CHAT_X + 12 + (PANEL_W if panel_left else 0.0),
				vh - UP_CHAT + CHAT_H - 44)
			chat_input.size = Vector2(right - CHAT_X - 44.0, 34)
	# Straight onto the slots _draw_login measured, so the caption and
	# the box it names cannot drift apart.
	for i in range(3):
		var e: LineEdit = [fld_host, fld_name, fld_pass][i]
		if e:
			e.position = Vector2(field_x, field_y + float(i) * 62.0)
			e.size = Vector2(field_w, 44)

func _build_chat() -> void:
	var e := LineEdit.new()
	e.placeholder_text = "say something"
	e.max_length = 180
	# Where it goes is _place_fields()'s business, every frame.
	e.add_theme_font_override("font", _f())
	e.add_theme_font_size_override("font_size", 17)
	e.add_theme_color_override("font_color", PALE)
	e.add_theme_color_override("font_placeholder_color", Color("6a5c46"))
	e.add_theme_color_override("caret_color", Color("c9a86a"))
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.06, 0.05, 0.04, 0.85)
	sb.border_color = Color(0.45, 0.38, 0.26)
	sb.set_border_width_all(1)
	sb.content_margin_left = 8
	e.add_theme_stylebox_override("normal", sb)
	e.add_theme_stylebox_override("focus", sb)
	e.text_submitted.connect(_send_chat)
	add_child(e)
	chat_input = e

func _send_chat(text: String) -> void:
	var t := text.strip_edges()
	if t == "":
		return
	net.send({"say": t})
	chat_input.text = ""
	# Not echoed locally: the authority sends it back, and a line that
	# appears before it has been relayed would tell the player it went
	# out when it may not have.

# --shot=FILE writes a frame and quits.  A client is the one part of this
# that cannot be checked by running it headless and reading the output,
# so it takes its own picture rather than something taking a picture of
# the whole screen.
var shot_path := ""
var shot_after := 0.0
# --hover=N puts the mouse on the Nth card for a picture.  The hover is
# the one thing on this screen that cannot be seen in a screenshot taken
# without one.
var hover_probe := -1
# --login=name:password fills the door and walks through it, so the room
# can be photographed without a pair of hands.
var auto_login := ""
# --sit takes the first seat on offer, --seek offers one.  Test hooks, so
# the whole path from the door to a dealt hand can be walked without a
# pair of hands.
var auto_sit := false
var auto_seek := false
# --auto answers for the person at the keyboard, so the whole loop --
# door, room, a played game, and the door again -- can be walked without
# one.  It plays badly on purpose: it is a test hook, not an opponent.
var auto_play := false
var auto_alone := false
var panel_left := true
var hit_test := false
var art_check := false
var menu_test := false
# A watchable run-through: hover a button, pause so it lights, click it,
# pause again.  Real InputEventMouse through _gui_input, so it exercises
# exactly the path a hand does -- including the remap that was sending
# every menu click hundreds of pixels wide of its target.
var walk := false
var walk_step := 0
var walk_t := 0.0
var walk_phase := 0
const WALK := ["menuHouse", "houseNext", "houseNext", "houseNext", "here"]
# --times: where the wait between clicking PLAY and seeing a table
# actually goes.  Five lines, printed once, because "it takes ten
# seconds" and "it takes three hundred milliseconds" were measured on
# two different machines and only one of them is the one being played.
var menu := "main"
var end_view: Dictionary = {}
var end_why := ""
var end_won := false
var end_winner := -1
var menu_force := ""
var panel_tex_path := ""
var times := false
var t_draws := 0
var t_decodes := 0
var t_decode_us := 0
var t_last_draw := 0
var t_frame_n := 0
var t_frame_us := 0
var stall := -1
var mock_step := -1
var mock_theirs := false
var auto_here := false
var auto_help := false
var auto_playable := false
var hover_btn := -1
var auto_click := false
# Which House to ask for, or -1 for whatever the deal gives.
#
# It was a colour until today: every House held twelve cards.  Ravenmark
# holds sixty-one now, so which one you are is a real choice and the
# game has to let you make it.
var want_house := -1
# The Houses the engine actually has, read from help.tsv, which reads
# them from src/data.c.  The client used to keep its own list of four.
var houses: PackedStringArray = PackedStringArray(
	["RAVENMARK", "VIPREN", "GOLDWYN", "ALDEMAR"])

func _load_houses() -> void:
	if not help.has("houses"):
		return
	var out := PackedStringArray()
	for h in String(help["houses"].body).split("|"):
		out.append(h.strip_edges().to_upper())
	if out.size() > 0:
		houses = out
# Where _draw_login put the three fields this frame; _place_fields moves
# the real LineEdit nodes onto them.
var field_x := 760.0
var field_y := 400.0
var field_w := 400.0
var auto_ask := ""
var auto_hover := ""

func _process(dt: float) -> void:
	if net:
		net.poll()
	if auto_ask != "" and (asking != auto_ask or not legal.is_empty()):
		# Pretend the authority asked, so the explanation under a question
		# can be photographed without waiting for the game to reach one.
		asking = auto_ask
		legal = []
		ask_a = 3
		ask_b = 1
		queue_redraw()
	if auto_click and not legal.is_empty():
		for L in legal:
			var cd2: int = int((L as Dictionary).get("card", -1))
			if cd2 >= 0:
				_click_card(cd2)
				break
	if auto_playable and not legal.is_empty():
		# Hover the first card in hand that the panel offers to play, so
		# the two-way highlight can be photographed instead of hoped for.
		for L in legal:
			var cd: int = int((L as Dictionary).get("card", -1))
			if cd >= 0:
				if hover_card != cd:
					hover_card = cd
					queue_redraw()
				break
	if hover_btn >= 0 and hover != hover_btn and hover_btn < buttons.size():
		hover = hover_btn
		queue_redraw()
	if auto_help and not help_open:
		help_open = true
		queue_redraw()
	if auto_hover != "" and hover_help != auto_hover:
		hover_help = auto_hover
		queue_redraw()
	if hover_probe >= 0 and not card_rects.is_empty():
		var i: int = mini(hover_probe, card_rects.size() - 1)
		var want := int(card_rects[i].idx)
		if want != hover_card:
			hover_card = want
			queue_redraw()
	# Does every card the engine knows actually decode?  File present is
	# not the same as file loadable, and the whole point of the last fix
	# was that 1300 files were present and none of them loaded.
	if art_check:
		art_check = false
		var miss: Array = []
		var ok := 0
		# Decoded and dropped, not cached.  Holding all 1455 at once is
		# the two gigabytes the lazy loader exists to avoid, and the
		# first version of this check ran out of memory and reported two
		# perfectly good cards as broken.
		for k in card_meta:
			var t := _tex_file("res://assets/cards/%s.png" % card_meta[k].id)
			if t == null:
				miss.append(String(card_meta[k].id))
			else:
				ok += 1
			t = null
		var bad := 0
		for i in range(BACK_FILE.size()):
			if _back_tex(i) == null:
				bad += 1
				print("  back missing: ", BACK_FILE[i])
		print("art-check: %d of %d cards decoded, %d backs missing of %d"
			% [ok, card_meta.size(), bad, BACK_FILE.size()])
		if not miss.is_empty():
			print("  first failures: ", ", ".join(miss.slice(0, 12)))
		get_tree().quit()
	# Does a menu button take the click aimed at it?
	#
	# --hit-test only ever walked the game screen, so the remap that
	# carries a click across the swapped panel was never checked
	# anywhere else -- and it ran on every screen, which put every menu
	# click hundreds of pixels from where it was aimed.
	if menu_test and screen == "login" and not buttons.is_empty():
		menu_test = false
		var bad := 0
		for m in ["main", "house", "settings"]:
			menu = m
			queue_redraw()
			await RenderingServer.frame_post_draw
			for i in range(buttons.size()):
				var c: Vector2 = (buttons[i].rect as Rect2).get_center()
				var ev := InputEventMouseMotion.new()
				ev.position = c
				_gui_input(ev)
				if hover != i:
					bad += 1
					print("  MISS %s '%s' at %d,%d -> %d"
						% [m, buttons[i].action, int(c.x), int(c.y), hover])
			print("  %-10s %d buttons" % [m, buttons.size()])
		print("menu-test: %d wrong, panel_left=%s" % [bad, panel_left])
		get_tree().quit()
	if walk and screen == "login" and walk_step < WALK.size():
		walk_t -= dt
		if walk_t <= 0.0:
			var want: String = WALK[walk_step]
			var at := -1
			for i in range(buttons.size()):
				if String(buttons[i].action) == want:
					at = i
					break
			if at < 0:
				print("  walk: no button '%s' on this screen" % want)
				walk_step += 1
				walk_t = 0.6
			elif walk_phase == 0:
				var c: Vector2 = (buttons[at].rect as Rect2).get_center()
				var mv := InputEventMouseMotion.new()
				mv.position = c
				_gui_input(mv)
				print("  walk: pointing at '%s' (%d,%d)"
					% [want, int(c.x), int(c.y)])
				walk_phase = 1
				walk_t = 0.9
			else:
				var c2: Vector2 = (buttons[at].rect as Rect2).get_center()
				var cl := InputEventMouseButton.new()
				cl.position = c2
				cl.button_index = MOUSE_BUTTON_LEFT
				cl.pressed = true
				_gui_input(cl)
				print("  walk: clicked '%s'" % want)
				walk_phase = 0
				walk_step += 1
				walk_t = 1.1
	if dec_more:
		dec_more = false
		queue_redraw()
	if hit_test and not card_rects.is_empty():
		hit_test = false
		_hit_test()
		get_tree().quit()
	if scale_shown > 0.0:
		scale_shown -= dt
		if scale_shown <= 0.0:
			queue_redraw()
	if bg_shown > 0.0:
		bg_shown -= dt
		if bg_shown <= 0.0:
			queue_redraw()
	if shot_path != "":
		shot_after -= dt
		if shot_after <= 0.0:
			# Taken and cleared BEFORE the await.  frame_post_draw
			# yields, _process runs again on the next frame with
			# shot_path still set and shot_after still past zero, and
			# the shot fired a second time -- after _stop_local had
			# killed the engine and the client had fallen back to the
			# login screen, which then overwrote the good capture.
			# Every screenshot that "landed on the login screen" was
			# this, and was re-timed instead of being fixed.
			var dst := shot_path
			shot_path = ""
			await RenderingServer.frame_post_draw
			var img := get_viewport().get_texture().get_image()
			img.save_png(dst)
			print("wrote ", dst)
			# Before quitting, not after: get_tree().quit() does not
			# raise WM_CLOSE_REQUEST, so the engine this client started
			# outlived it every time a screenshot was taken.  That is
			# how the stray processes got onto his machine.
			_stop_local()
			get_tree().quit()

# ---------------------------------------------------------------- assets
func _load_scale() -> void:
	var f := FileAccess.open(SCALE_FILE, FileAccess.READ)
	if f:
		fk = clampf(f.get_line().to_float(), 0.85, 1.35)
	_apply_scale()

func _apply_scale() -> void:
	get_window().content_scale_factor = ui_scale
	queue_redraw()

func _set_scale(v: float) -> void:
	# Type only.  The canvas stays 1:1 with the window, so nothing loses
	# room; the art on a card and the height of a row give way instead.
	fk = clampf(snappedf(v, 0.05), 0.85, 1.35)
	ui_scale = 1.0
	_apply_scale()
	scale_shown = 1.6
	# Kept, because being asked to resize the text every time you open a
	# game is its own kind of unreadable.
	var f := FileAccess.open(SCALE_FILE, FileAccess.WRITE)
	if f:
		f.store_line(str(fk))

func _load_backs() -> void:
	# Nothing here.  Seventeen backs at 700KB each were decoded before
	# the menu could be drawn, and on a machine where a decode is slow
	# that is the whole of the wait before anything appears.  They are
	# fetched the first time one is drawn, like the card faces.
	pass

func _load_help() -> void:
	var f := FileAccess.open("res://assets/help.tsv", FileAccess.READ)
	if not f:
		return
	while not f.eof_reached():
		var line := f.get_line()
		var p := line.split("\t")
		if p.size() >= 3:
			# The file stores a newline as two characters, because it is
			# one entry per line and a real one would end the entry.
			help[p[0]] = {"title": p[1], "body": p[2].replace("\\n", "\n")}

func _load_skin() -> void:
	# Surfaces and faces.  They are generated and copied rather than
	# committed, so a missing one has to be survivable: every draw falls
	# back to flat colour, which is what this looked like before and is
	# ugly rather than broken.
	tex_table = _tex("res://assets/ui/table.png")
	tex_panel = _tex_file(panel_tex_path) if panel_tex_path != "" \
		else _tex("res://assets/ui/panel.png")
	tex_bar = _tex("res://assets/ui/bar.png")
	tex_vig = _tex("res://assets/ui/vignette.png")
	tex_shadow = _tex("res://assets/ui/shadow.png")
	f_serif = _fnt("res://assets/ui/serif.ttf")
	f_bold = _fnt("res://assets/ui/serif-bold.ttf")
	f_ital = _fnt("res://assets/ui/serif-italic.ttf")

# Backgrounds a person can add without a rebuild.
#
# Not load(): a res:// resource has to be imported by the editor first,
# so a jpg dropped into a folder would not exist as far as load() is
# concerned.  The bytes are read and decoded here instead, which is
# what makes "drop a file in and it is there" true rather than "drop a
# file in, reopen the editor, reimport, export again".
#
# jpg because the painted table is a 7.7MB png of a photograph, which
# is the worst possible use of png.
func _scan_backgrounds() -> void:
	bg_names = PackedStringArray([""])      # the painted table, always first
	for d in BG_DIRS:
		var dir := DirAccess.open(d)
		if dir == null:
			continue
		dir.list_dir_begin()
		var fn := dir.get_next()
		while fn != "":
			var e := fn.get_extension().to_lower()
			if not dir.current_is_dir() and e in ["jpg", "jpeg", "png", "webp"]:
				bg_names.append(d + "/" + fn)
			fn = dir.get_next()
		dir.list_dir_end()
	# Stable, so the same key press gives the same picture next time.
	var rest := Array(bg_names.slice(1))
	rest.sort()
	bg_names = PackedStringArray([""] + rest)

# Every generated image goes through here, not through load().
#
# load() only sees what the editor has imported, and everything under
# assets/cards and assets/backs is generated by tools/mkassets.sh after
# the editor last looked: 1455 faces on disk against 155 .import files.
# ResourceLoader.exists() said no to the other 1300 and the loop
# skipped them in silence, so all of the art rendered this week drew as
# named empty frames.  The same trap the backgrounds were written to
# avoid, one folder over.
func _tex_file(path: String) -> Texture2D:
	if path == "":
		return null
	var _t0 := Time.get_ticks_usec()
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return null
	var img := Image.new()
	var buf := f.get_buffer(f.get_length())
	var err := ERR_FILE_UNRECOGNIZED
	match path.get_extension().to_lower():
		"jpg", "jpeg": err = img.load_jpg_from_buffer(buf)
		"png":         err = img.load_png_from_buffer(buf)
		"webp":        err = img.load_webp_from_buffer(buf)
	if err != OK:
		return null
	var _tx := ImageTexture.create_from_image(img)
	if times:
		t_decodes += 1
		t_decode_us += Time.get_ticks_usec() - _t0
	return _tx

func _bg_load(path: String) -> Texture2D:
	return _tex_file(path)

func _set_background(i: int) -> void:
	if bg_names.is_empty():
		return
	bg_pick = ((i % bg_names.size()) + bg_names.size()) % bg_names.size()
	bg_tex = _bg_load(bg_names[bg_pick])
	# A file that will not decode must not leave a blank table: fall
	# back to the painted one and say so, rather than showing nothing.
	if bg_tex == null and bg_names[bg_pick] != "":
		_say("that background would not load: " + bg_names[bg_pick].get_file())
		bg_pick = 0
	var f := FileAccess.open(BG_FILE, FileAccess.WRITE)
	if f:
		f.store_line(bg_names[bg_pick])
	bg_shown = 1.6
	queue_redraw()

func _load_background() -> void:
	_scan_backgrounds()
	var want := ""
	var f := FileAccess.open(BG_FILE, FileAccess.READ)
	if f:
		want = f.get_line()
	if bg_force >= 0:
		_set_background(bg_force)
		return
	var at := bg_names.find(want)
	_set_background(at if at >= 0 else 0)

func _tex(path: String) -> Texture2D:
	# From the file, not through the import pipeline -- the same reason
	# _fnt below gives for the faces, and it applies here too.
	#
	# A .import points at res://.godot/imported/<name>.ctex, and
	# client/.godot is not in the repository, so load() fails on a fresh
	# clone and every surface fell back to flat colour.  The table was
	# grey in a game that carries a painted one.
	var t := _tex_file(path)
	if t:
		return t
	return load(path) if ResourceLoader.exists(path) else null

func _fnt(path: String) -> Font:
	# Loaded from the file rather than through the import pipeline.
	#
	# These faces are copied in by tools/mktextures.sh and are not in the
	# repository, so requiring an editor pass before the client would
	# show its own typeface made a generated asset depend on a step
	# nobody runs -- and when the pass quietly produced no .import for a
	# .ttf, the client fell back to a sans face and said nothing about
	# it.  load_dynamic_font reads the bytes and needs no import.
	if not FileAccess.file_exists(path):
		push_warning("no font at %s -- run tools/mktextures.sh" % path)
		return null
	var f := FontFile.new()
	if f.load_dynamic_font(path) != OK:
		push_warning("could not read font %s" % path)
		return null
	return f

# ---------------------------------------------------------------- assets
func _load_cards() -> void:
	# The card table is generated, so the client reads the same CARDS.tsv
	# the engine compiled from.  A card the engine knows and the client
	# does not would otherwise be a blank rectangle with no explanation.
	var f := FileAccess.open("res://assets/cards.tsv", FileAccess.READ)
	if f:
		var i := 0
		while not f.eof_reached():
			var line := f.get_line()
			if line.strip_edges() == "":
				continue
			var p := line.split("\t")
			if p.size() < 3 or p[1] == "id":
				continue
			card_meta[i] = {"id": p[1], "name": p[2], "deck": p[0]}
			i += 1

# Decoded on demand and kept.  Eagerly decoding every face is about two
# gigabytes of RGBA for a game that shows a few dozen cards, so the
# work is done the first time a card is actually drawn.  A miss is
# cached as null too, or a card with no art is re-read every frame.
func _back_tex(i: int) -> Texture2D:
	if backs.has(i):
		return backs[i]
	if i < 0 or i >= BACK_FILE.size():
		return null
	backs[i] = _tex_file("res://assets/backs/%s.png" % BACK_FILE[i])
	return backs[i]

# The small face, for the hand and the lords, which are drawn at about
# 150 pixels.  A 512x717 source for a 150-pixel card is four times the
# pixels needed and all of them have to be decoded first.
func _card_tex(idx: int) -> Texture2D:
	if cards.has(idx):
		return cards[idx]
	if not card_meta.has(idx):
		cards[idx] = null
		return null
	# Budgeted.  Thirteen faces on the first frame of a game is thirteen
	# decodes before anything appears, and on a slow machine that is a
	# frozen window for seconds.  Two per frame, the rest next frame:
	# the table is up immediately and dresses itself.
	if dec_budget <= 0:
		dec_more = true
		return null
	dec_budget -= 1
	var t := _tex_file("res://assets/thumbs/%s.png" % card_meta[idx].id)
	if t == null:
		t = _tex_file("res://assets/cards/%s.png" % card_meta[idx].id)
	cards[idx] = t
	return t

# The full face, for the one card being read.  Only ever one at a time,
# so it is worth its decode.
func _card_full(idx: int) -> Texture2D:
	if cards_full.has(idx):
		return cards_full[idx]
	if not card_meta.has(idx):
		cards_full[idx] = null
		return null
	cards_full[idx] = _tex_file("res://assets/cards/%s.png"
		% card_meta[idx].id)
	return cards_full[idx]

func _card_name(idx: int) -> String:
	return card_meta.get(idx, {}).get("name", "card %d" % idx)

# --------------------------------------------------------------- messages
func _on_message(m: Dictionary) -> void:
	if m.has("over"):
		var w := int(m.get("winner", -1))
		var why := str(m.get("reason", "?")).replace("_", " ")
		outcome = ("you won by %s" % why) if w == int(view.get("me", -1)) \
			else ("you lost by %s" % why)
		status = outcome
		_say(status)
		# The table is about to go: the engine closes as soon as it has
		# said this.  Keep the last View, because the end screen is the
		# only place the final standings can still be read.
		end_view = view.duplicate(true)
		end_why = why
		end_won = w == int(view.get("me", -1))
		end_winner = w
		screen = "over"
		queue_redraw()
		return
	if m.has("denied"):
		denied = str(m.denied)
		screen = "login"
		queue_redraw()
		return
	if m.has("lobby"):
		me_name = str(m.get("name", ""))
		_enter_lobby()
		queue_redraw()
		return
	if m.has("users"):
		lobby_users = m.users
		if auto_alone:
			auto_alone = false
			net.send({"alone": 1})
			return
		if auto_seek and not seeking:
			seeking = true
			net.send({"seek": 1})
		if auto_sit and screen == "lobby":
			for u in lobby_users:
				if bool(u.get("seek", 0)) and str(u.get("name","")) != me_name:
					auto_sit = false
					net.send({"sit": int(u.get("i", -1))})
					break
		for u in lobby_users:
			if str(u.get("name", "")) == me_name:
				seeking = bool(u.get("seek", 0))
		queue_redraw()
		return
	if m.has("note"):
		chat_lines.append("- %s" % str(m.note))
		if chat_lines.size() > 12: chat_lines.pop_front()
		queue_redraw()
		return
	if m.has("start"):
		# The room hands us to a game and stops talking; everything after
		# this line is the game protocol.
		screen = "game"
		chat_lines.clear()
		if chat_input: chat_input.queue_free()
		chat_input = null
		# Only when the other seat is a person.
		pvp = int(m.get("human", 1)) == 1
		if pvp:
			_build_chat()
		queue_redraw()
		return
	if m.has("waiting"):
		status = "waiting for another player"
		queue_redraw()
		return
	if m.has("chat"):
		var who: String = str(m.from) if m.has("from") \
			else _seat_name(int(m.get("seat", -1)))
		chat_lines.append("%s: %s" % [who, str(m.get("text", ""))])
		if chat_lines.size() > (12 if screen == "lobby" else 6):
			chat_lines.pop_front()
		queue_redraw()
		return
	if m.has("ask"):
		asking = m.ask
		status = "your move"
		if auto_play and m.ask != "choose":
			var lo := int(m.get("a", 0))
			var hi := int(m.get("b", 0))
			if lo > hi:
				var t := lo; lo = hi; hi = t
			net.send({"answer": chaos_rng.randi_range(lo, hi) if chaos > 0 else 0})
			asking = ""
			return
		ask_a = int(m.get("a", 0))
		ask_b = int(m.get("b", 0))
		return
	if m.has("legal"):
		legal = m.legal
		if stall == 0:
			auto_play = false
		if stall > 0:
			stall -= 1
		if auto_play and legal.size() > 0:
			var pick: Dictionary = legal[0]
			if chaos > 0:
				pick = legal[chaos_rng.randi_range(0, legal.size() - 1)]
			else:
				for L in legal:
					if int(L.kind) == 1: pick = L; break
			net.send(pick)
			asking = ""
			legal = []
			queue_redraw()
			return
		queue_redraw()
		return
	# What the other seat just did.  The engine sends only what this seat
	# is entitled to see -- no Bonds, no Instigators -- and the wording
	# happens here, where the deck and House names already are.
	if m.has("did"):
		_say(_did_label(m.did, int(m.get("house", -1))))
		queue_redraw()
		return
	if m.has("me"):
		view = m
		if asking != "choose":
			legal = []
		if asking == "":
			status = "waiting on them"
		queue_redraw()


func _did_label(a: Dictionary, house: int) -> String:
	var who: String = "they"
	if house >= 0 and house < HOUSE_NAMES.size():
		who = String(HOUSE_NAMES[house]).capitalize()
	var card: int = int(a.get("card", -1))
	match int(a.kind):
		0:
			var d := int(a.get("a", 0))
			return "%s draws %s from %s" % [who,
				"deep" if int(a.get("b", 0)) == 1 else "a card",
				_deck_short(d)]
		1:  return "%s plays %s" % [who, _card_name(card)]
		4:  return "%s offers a Promise" % who
		6:  return "%s declares Open War" % who
		7:  return "%s challenges for the Throne" % who
		8:  return "%s reveals a Bond" % who
		10: return "%s spends Grievance" % who
		11: return "%s buys Favour at the Court" % who
	return "%s acts" % who

func _say(s: String) -> void:
	log_lines.append(s)
	if log_lines.size() > 9:
		log_lines.pop_front()

# ----------------------------------------------------------------- input
func _unhandled_key_input(e: InputEvent) -> void:
	if not (e is InputEventKey and e.pressed):
		return
	var k := e as InputEventKey
	var toggle: bool = k.keycode == KEY_F11 \
		or (k.keycode == KEY_ENTER and k.alt_pressed) \
		or (k.keycode == KEY_F and (k.meta_pressed or k.ctrl_pressed) and k.shift_pressed)
	if toggle:
		_set_fullscreen(not _is_fullscreen())
		get_viewport().set_input_as_handled()
	elif k.keycode == KEY_B:
		# Shift steps back, so a folder of thirty is not a one-way trip.
		_set_background(bg_pick + (-1 if k.shift_pressed else 1))
		get_viewport().set_input_as_handled()
	elif k.keycode == KEY_EQUAL or k.keycode == KEY_PLUS \
			or k.keycode == KEY_KP_ADD:
		_set_scale(fk + 0.1)
		get_viewport().set_input_as_handled()
	elif k.keycode == KEY_MINUS or k.keycode == KEY_KP_SUBTRACT:
		_set_scale(fk - 0.1)
		get_viewport().set_input_as_handled()
	elif k.keycode == KEY_0 or k.keycode == KEY_KP_0:
		_set_scale(1.0)
		get_viewport().set_input_as_handled()
	elif k.keycode == KEY_QUESTION or k.keycode == KEY_SLASH \
			or k.keycode == KEY_H or k.keycode == KEY_F1:
		if screen == "game" or screen == "lobby":
			help_open = not help_open
			queue_redraw()
			get_viewport().set_input_as_handled()
	elif k.keycode == KEY_ESCAPE and help_open:
		help_open = false
		queue_redraw()
		get_viewport().set_input_as_handled()
	elif k.keycode == KEY_ESCAPE and _is_fullscreen():
		_set_fullscreen(false)
		get_viewport().set_input_as_handled()

func _is_fullscreen() -> bool:
	var m := DisplayServer.window_get_mode()
	return m == DisplayServer.WINDOW_MODE_FULLSCREEN \
		or m == DisplayServer.WINDOW_MODE_EXCLUSIVE_FULLSCREEN

func _set_fullscreen(on: bool) -> void:
	DisplayServer.window_set_mode(
		DisplayServer.WINDOW_MODE_FULLSCREEN if on
		else DisplayServer.WINDOW_MODE_WINDOWED)
	queue_redraw()

# The panel on the left.  Nothing in the layout moves -- the table is
# still measured from 0 to `right` and the panel from `right` -- the two
# are simply painted in the other order.  That keeps every rect, clamp
# and hit test exactly as it was, and costs one translate each way.
func _pt(p: Vector2) -> Vector2:
	# Only the table swaps sides.  The menu, the House screen, settings
	# and the lobby are painted where they are measured, so carrying a
	# click back across the panel there sent it hundreds of pixels from
	# where it was aimed: on the main menu only the right-hand third of
	# PLAY happened to map back inside itself, and the House picker's
	# small arrows almost never did.
	if not panel_left or screen != "game":
		return p
	if p.x < PANEL_W:
		return Vector2(p.x + right, p.y)
	return Vector2(p.x - PANEL_W, p.y)

# Layout coordinates back out to the screen -- the inverse of _pt, and
# the only way to ask "where is this painted?"
func _inv(p: Vector2) -> Vector2:
	if not panel_left or screen != "game":
		return p
	if p.x >= right:
		return Vector2(p.x - right, p.y)
	return Vector2(p.x + PANEL_W, p.y)

# Does a click land on the thing it is pointing at?  Reasoning about the
# remap is not evidence.  This walks every hit rect the last frame
# registered, works out where on the SCREEN it was painted, pushes a
# real mouse event at that point through _gui_input, and checks what
# came back is the same element.
func _hit_test() -> void:
	var bad := 0
	var n := 0
	for hs in help_spots:
		var c: Vector2 = (hs.rect as Rect2).get_center()
		var ev := InputEventMouseMotion.new()
		ev.position = _inv(c)
		_gui_input(ev)
		n += 1
		if hover_help != String(hs.key):
			bad += 1
			print("  MISS help '%s' layout %d,%d screen %d,%d -> '%s'" % [
				hs.key, int(c.x), int(c.y),
				int(ev.position.x), int(ev.position.y), hover_help])
	for cr in card_rects:
		var c2: Vector2 = (cr.rect as Rect2).get_center()
		var ev2 := InputEventMouseMotion.new()
		ev2.position = _inv(c2)
		_gui_input(ev2)
		n += 1
		if hover_card != int(cr.idx):
			bad += 1
			print("  MISS card %d at screen %d,%d -> %d" % [
				int(cr.idx), int(ev2.position.x), int(ev2.position.y),
				hover_card])
	print("hit-test: %d probes, %d wrong, panel_left=%s" % [n, bad, panel_left])

func _gui_input(e: InputEvent) -> void:
	if panel_left and e is InputEventMouse:
		e = e.duplicate()
		e.position = _pt(e.position)
	if e is InputEventMouseMotion:
		var h := _hit(e.position)
		var c := _hit_card(e.position)
		# The other direction: point at "play Mercenaries" and Mercenaries
		# lifts in your hand.
		if h >= 0 and h < buttons.size() and buttons[h].action is Dictionary:
			var ca: int = int((buttons[h].action as Dictionary).get("card", -1))
			if ca >= 0:
				c = ca
		var hd := -1
		for ds in deck_spots:
			if (ds.rect as Rect2).has_point(e.position):
				hd = int(ds.deck)
		if hd != hover_deck:
			hover_deck = hd
			queue_redraw()
		# The cursor is the cheapest feedback there is and it works
		# before you have looked at anything: a hand over what can be
		# clicked, an arrow over what cannot.
		var over: bool = h >= 0 or hd >= 0
		if not over:
			for row in lobby_rows:
				if (row.rect as Rect2).has_point(e.position):
					over = true
		if not over and screen == "game":
			for cr in card_rects:
				if (cr.rect as Rect2).has_point(e.position) \
						and _playable().has(int(cr.idx)):
					over = true
		Input.set_default_cursor_shape(
			Input.CURSOR_POINTING_HAND if over else Input.CURSOR_ARROW)
		var hh := _hit_help(e.position)
		if hh != hover_help:
			hover_help = hh
			queue_redraw()
		var r := -1
		for row in lobby_rows:
			if (row.rect as Rect2).has_point(e.position):
				r = int(row.i)
		if h != hover or c != hover_card or r != hover_row:
			hover = h
			hover_card = c
			hover_row = r
			queue_redraw()
	elif e is InputEventMouseButton and e.pressed and e.button_index == MOUSE_BUTTON_LEFT:
		var h := _hit(e.position)
		if h >= 0 and h < buttons.size():
			_do(buttons[h].action)
			return
		# A card in hand is a button.  Going to the right-hand list to
		# play the card you are already looking at is a step that exists
		# only because the list came first.
		var cd := _hit_card(e.position)
		if cd >= 0 and _click_card(cd):
			return
		for ds in deck_spots:
			if (ds.rect as Rect2).has_point(e.position):
				_do(ds.action)
				return
		for row in lobby_rows:
			if (row.rect as Rect2).has_point(e.position):
				net.send({"sit": int(row.i)})
				status = "sitting down..."
				queue_redraw()
				return

# Last drawn wins: cards drawn later sit on top, so the mouse should
# find the one the eye finds.
# Mark a patch of the table as explainable.  Cheap enough to do while
# drawing, which keeps the region and the thing it describes together --
# a tooltip that has to be positioned separately is a tooltip that ends
# up over the wrong label.
func _spot(r: Rect2, key: String) -> void:
	if help.has(key):
		help_spots.append({"rect": r, "key": key})

func _hit_help(p: Vector2) -> String:
	for i in range(help_spots.size() - 1, -1, -1):
		if (help_spots[i].rect as Rect2).has_point(p):
			return String(help_spots[i].key)
	return ""

# Clicking a card in hand plays it.  Returns true if the click was
# taken, so the caller stops looking for something else under it.
#
# Going to the right-hand list to play the card you are already looking
# at is a step that exists only because the list came first.
func _click_card(cd: int) -> bool:
	var forit: Array = []
	for a in legal:
		if int((a as Dictionary).get("card", -1)) == cd:
			forit.append(a)
	if forit.size() == 1:
		if audit:
			print("CLICK %s -> %s" % [_card_name(cd), _action_label(forit[0])])
		_do(forit[0])
		return true
	if forit.size() > 1:
		# More than one thing to do with it -- play it, bind a lord with
		# it, offer the promise it carries.  The card cannot say which,
		# so the list still answers that, and hovering here has already
		# lit those choices over there.
		status = "that card can do more than one thing -- pick on the right"
		queue_redraw()
		return true
	return false

func _hit_card(p: Vector2) -> int:
	for i in range(card_rects.size() - 1, -1, -1):
		if (card_rects[i].rect as Rect2).has_point(p):
			return int(card_rects[i].idx)
	return -1

func _hit(p: Vector2) -> int:
	for i in range(buttons.size()):
		if (buttons[i].rect as Rect2).has_point(p):
			return i
	return -1

func _do(a) -> void:
	if a is String:
		match a:
			"login": _do_login()
			"here": _play_here()
			"again":
				outcome = ""
				denied = ""
				end_view = {}
				log_lines.clear()
				screen = "login"
				menu = "main"
				_play_here()
			"menuHouse":
				menu = "house"
				_free_fields()
				queue_redraw()
			"menuMulti":
				menu = "multiplayer"
				_build_fields(last_host, last_port)
				queue_redraw()
			"menuSettings":
				menu = "settings"
				_free_fields()
				queue_redraw()
			"menuQuit":
				_stop_local()
				get_tree().quit()
			"menuBack":
				screen = "login"
				outcome = ""
				end_view = {}
				log_lines.clear()
				menu = "main"
				denied = ""
				_free_fields()
				queue_redraw()
			"sizeUp": _set_scale(fk + 0.05)
			"sizeDown": _set_scale(fk - 0.05)
			"bgPrev": _set_background(bg_pick - 1)
			"bgNext": _set_background(bg_pick + 1)
			"housePrev", "houseNext":
				# ANY is index 0 and the Houses follow it, so there are
				# size()+1 positions and want_house is the index minus
				# one.  Spelled out because the clever one-liner sent
				# "next" from ANY to the last House.
				var slots: int = houses.size() + 1
				var idx: int = want_house + 1
				idx = (idx + (1 if a == "houseNext" else slots - 1)) % slots
				want_house = idx - 1
				queue_redraw()
			"seek":
				seeking = not seeking
				net.send({"seek": 1 if seeking else 0})
				queue_redraw()
			"alone":
				net.send({"alone": 1, "house": want_house})
				status = "dealing..."
				queue_redraw()
		return
	if a is Dictionary and a.has("kind"):
		net.send(a)
		_say("you: %s" % _action_label(a))
	else:
		net.send({"answer": int(a)})
		_say("you: %s" % ("yes" if int(a) else "no"))
	asking = ""
	legal = []
	buttons = []
	hover = -1
	queue_redraw()

# A legal action, said in English.  The engine's Action is seven small
# integers; a person needs the sentence.
func _action_label(a: Dictionary) -> String:
	# The numbers are the engine's Action kinds, in the order court.h
	# declares them:
	#
	#   0 DRAW  1 PLAY  2 BOND  3 INSTIGATE  4 PROPOSE  5 RESPOND
	#   6 DECLARE_WAR  7 CHALLENGE  8 REVEAL_BOND  9 REVOLT
	#   10 SPEND_GRIEVANCE  11 BUY_FAVOUR  12 DISCARD  13 PASS
	#
	# This table was written against a shorter enum and everything from 9
	# on was out by one, so the panel offered "buy Favour" for an action
	# that spends Grievance and printed "action 13" for Pass.  The engine
	# was never wrong -- the client sends the whole action back, so the
	# right thing happened and the player was told it was something else,
	# which is the worse kind of wrong.
	var k := int(a.kind)
	match k:
		0:
			# a.b is the deep draw: pay 3 of the deck's resource instead
			# of 1, draw two and keep the cheaper.  Both spellings read
			# as "draw from Political" until now, so the list offered the
			# same words twice and one of them cost three times as much.
			if int(a.get("b", 0)) == 1:
				return "draw deep from %s  (3, keep best of 2)" % _deck_name(int(a.a))
			return "draw from %s" % _deck_name(int(a.a))
		1: return "play %s" % _card_name(int(a.card))
		2: return "bind a lord with %s" % _card_name(int(a.card))
		3:
			# a.a says which kind of instigator, and for unrest which
			# resource: the engine pushes 3 + r for each of Military,
			# Capital and Gold.  All three used to come back as the same
			# three words, so the panel offered "stir unrest" three times
			# over and gave no way to tell them apart.
			if int(a.a) == 1:
				return "whisper to %s" % _lord_name(int(a.target))
			if int(a.a) == 2:
				return "whisper to the Court"
			var r := int(a.a) - 3
			if r >= 0 and r < RES_NAME.size():
				return "stir unrest in their %s" % RES_NAME[r].to_lower()
			return "stir unrest"
		4:
			# Two Promise cards offering the same term read identically
			# unless the card is named.  Only one target at two players,
			# so the target is not in the label; it would have to be at
			# three.
			return "promise %s  (%s)" % [
				TERMS[clamp(int(a.a), 0, TERMS.size() - 1)],
				_card_name(int(a.card))]
		5: return "answer their promise"
		6:
			var wc := int(a.a)
			return "declare Open War" if wc <= 0 \
				else "declare Open War  (%d Grievance)" % wc
		7: return "challenge for the Throne"
		8:
			# One action per lord, every one of them reading the same
			# four words until the lord was named.
			return "reveal your Bond on %s" % _lord_name(int(a.target))
		9: return "let a resource revolt"
		10:
			# a is both the amount and which sink it is, and the three
			# sinks are entirely different acts: 5 is an attempt on the
			# Lord Paramount, 6 forces a Council, 4 buys a Rebellion out
			# of a discard pile.  "spend 5 Grievance" said none of that.
			match int(a.a):
				4: return "spend 4 Grievance: take %s from the discard" \
					% _card_name(int(a.card))
				5: return "spend 5 Grievance: send a knife to the Lord Paramount"
				6: return "spend 6 Grievance: force a Council"
			return "spend %d Grievance" % int(a.a)
		11: return "buy Favour"
		12: return "discard %s" % _card_name(int(a.card))
		13:
			# "pass" meant one thing when a turn was one open loop.  It
			# now advances a step, and a player who is never told that
			# plays the whole hand in First Court and never learns that
			# holding something back to answer a war is a move.
			#
			# It names what it LEAVES, not where it lands: the next step
			# is skipped when it has nothing in it, so "pass -> Declare"
			# would be a promise the engine does not always keep.
			match int(view.get("step", 5)):
				1: return "draw nothing"
				2: return "leave First Court"
				3: return "declare nothing"
				4: return "end turn"
				_: return "pass"
	return "action %d" % k

# A lord by name rather than by index.  "whisper to a lord" was the same
# line however many lords were on the table.
func _lord_name(idx: int) -> String:
	var ls: Array = view.get("lords", [])
	if idx >= 0 and idx < ls.size():
		return _card_name(int(ls[idx].card))
	return "a lord"

# Under a shield there is room for one word, and the heraldry has
# already said which deck it is -- so "your House" and "the World" were
# being clipped to "your Hou" and "the Worl" to no purpose.  The list
# keeps the long form, where it reads as a sentence.
func _deck_short(d: int) -> String:
	# Eight House decks, then the shared piles.  Twelve entries against a
	# D_COUNT of seventeen labelled the new Houses' own decks War,
	# Political, Intrigue and Ambition.
	var n := ["House", "House", "House", "House",
		"House", "House", "House", "House",
		"War", "Political", "Intrigue", "Ambition", "World",
		"Court", "Throne", "Dead", "Cataclysm"]
	return n[d] if d >= 0 and d < n.size() else "?"

func _deck_name(d: int) -> String:
	# One entry per DeckId, and the House decks come first.  There are
	# EIGHT of them since the Lion, Ox, Wolf and Boar were added; with
	# four here the Lion's deck was being labelled "War" and the Ox's
	# "Political", which is wrong in the quietest possible way -- a
	# real deck, a real shield, the wrong word under it.
	var n := ["your House", "your House", "your House", "your House",
		"your House", "your House", "your House", "your House",
		"War", "Political", "Intrigue", "Ambition", "the World",
		"the Court", "the Throne", "the Dead", "the Cataclysm"]
	return n[d] if d >= 0 and d < n.size() else "?"

# ---------------------------------------------------------------- drawing
func _f() -> Font:
	return f_serif if f_serif else ThemeDB.fallback_font

func _fb() -> Font:
	return f_bold if f_bold else _f()

# Type size, on its own, apart from the layout.
#
# The canvas zoom made everything bigger by making the canvas smaller,
# so asking for larger type cost you the room to put it in -- which is
# why it had to stop at 110%.  This multiplies the type and nothing
# else.  What has to give instead is the art on a card and the height
# of a row, both of which are drawn from the type size rather than
# fixed, so they follow rather than collide.
var fk := 1.0

func _fs(size: int) -> int:
	return int(round(float(size) * fk))

# --audit measures text against the box it is drawn in and prints what
# does not fit.
#
# "Check the text doesn't break the boxes" is not a thing to do by
# reading screenshots: it depends on the House, the card names dealt,
# the type size and the window, so the combination that breaks is
# rarely the one photographed.  This asks the font, at draw time, in
# whatever state the game happens to be in.
var audit := false
var audit_seen := {}

# A label too long for its button was hard-clipped by draw_string, so
# "whisper to The Frightened Castellan" became "whisper to The Frightened
# Castell" with nothing to say it had been cut.  Trimmed with an ellipsis
# instead, which at least admits it.
func _clip_to(text: String, size: int, maxw: float, bold := false) -> String:
	var fnt: Font = _fb() if bold else _f()
	if fnt.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1,
			_fs(size)).x <= maxw:
		return text
	var t := text
	while t.length() > 1:
		t = t.substr(0, t.length() - 1)
		if fnt.get_string_size(t + "\u2026", HORIZONTAL_ALIGNMENT_LEFT, -1,
				_fs(size)).x <= maxw:
			return t.strip_edges() + "\u2026"
	return t

func _fit(what: String, text: String, size: int, maxw: float,
		bold := false) -> void:
	if not audit or text == "":
		return
	var fnt: Font = _fb() if bold else _f()
	var wide: float = fnt.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(size)).x
	if wide > maxw + 0.5:
		var k := "%s|%s" % [what, text]
		if not audit_seen.has(k):
			audit_seen[k] = true
			print("OVERFLOW  %-14s %5.0f > %5.0f  %s"
				% [what, wide, maxw, text])

func _t(pos: Vector2, s: String, size: int = 20, col: Color = INK) -> void:
	draw_string(_f(), pos, s, HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(size), col)

func _tb(pos: Vector2, s: String, size: int = 20, col: Color = INK) -> void:
	# Bold text gets a dark rim rather than a shadow.  On a textured
	# ground a plain glyph loses its edge where the grain runs light, and
	# the eyesight this is built for cannot afford that.
	draw_string(_fb(), pos + Vector2(1, 1), s, HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(size), Color(0, 0, 0, 0.55))
	draw_string(_fb(), pos, s, HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(size), col)

func _box(r: Rect2, fill: Color, stroke = null, w: float = 2.0) -> void:
	draw_rect(r, fill, true)
	if stroke != null:
		draw_rect(r, stroke, false, w)

# A surface: a texture tiled under a tint, or a flat colour where the
# texture has not been generated.  The fallback is its own colour and not
# the tint: a tint of white over a missing texture painted the whole
# table white, which is a worse failure than the flat brown it replaced.
func _surface(r: Rect2, tex: Texture2D, tint: Color, flat: Color) -> void:
	if tex:
		# Stretched, not tiled.  Tiling a 1024 square put a hard seam
		# down the middle of the table at x=1024; the textures are cut
		# to the size they cover now.
		draw_texture_rect(tex, r, false, tint)
	else:
		draw_rect(r, flat, true)

# A hairline in brass, which is what separates one part of this table
# from another.  Two lines, dark over light, because that is what an
# engraved edge is and one flat line reads as a crack.
func _rule(x: float, y: float, w: float, alpha := 1.0) -> void:
	draw_rect(Rect2(x, y, w, 1), Color(0.24, 0.20, 0.14, alpha), true)
	draw_rect(Rect2(x, y + 1, w, 1), Color(0.62, 0.52, 0.34, alpha * 0.8), true)

func _shadow(at: Vector2, size: Vector2, lift := 1.0) -> void:
	# A card lifted off the table throws a bigger, softer, further shadow.
	# lift 1.0 is resting; the hovered card passes more.
	if tex_shadow:
		var sp: float = 6.0 * lift
		var drop: float = 4.0 * lift
		draw_texture_rect(tex_shadow,
			Rect2(at - Vector2(sp, drop - 3.0 * (lift - 1.0)),
				size + Vector2(sp * 2.0, sp * 2.0 + 6.0)), false,
			Color(1, 1, 1, min(0.85, 0.72 + 0.10 * lift)))

func _bar(x: float, y: float, w: float, n: int, of_: int, col: Color,
		label: String, value: String) -> void:
	_t(Vector2(x, y + 12), label, 15, DIM)
	_tb(Vector2(x + w - 26, y + 22), value, 26, PALE)
	_box(Rect2(x, y + 28, w, 10), Color(0.10, 0.08, 0.06, 0.85))
	if of_ > 0:
		var fw: float = w * min(n, of_) / float(of_)
		_box(Rect2(x, y + 28, fw, 10), col)
		# a lit top edge, so the fill reads as something in a channel
		draw_rect(Rect2(x, y + 28, fw, 2), col.lightened(0.3), true)
	draw_rect(Rect2(x, y + 28, w, 10), Color(0.62, 0.52, 0.34, 0.45), false, 1)

# The art window inside a composed face, from tools/card.sh: AX/AY 26,
# AW 460, AH 345 of a 512x717 card.
const ART_REGION := Rect2(26, 26, 460, 345)

# A card small enough that its rules text cannot be read, drawn as art
# plus its name at a size that can be.
#
# This is his design, and it is better than what was here: at 140 units
# wide the composed face is a beautiful illegible smear, so showing the
# whole of it is showing nothing.  The name is what you pick a card by;
# the rules are what the reading copy is for.
# A glow, rather than a hairline.
#
# A one pixel outline says "this one" quietly; several rings of falling
# alpha say it at a distance, which is the whole point for somebody who
# is finding the screen hard to read.  Four rings is enough -- more is
# just a blurrier rectangle.
# A brass frame with rivets, which is the mockup's unit of structure.
# Everything that is a thing sits in one: a plaque, a button, a region.
func _frame(r: Rect2, lit := false, rivets := true) -> void:
	var c: Color = BRASS_LIT if lit else BRASS
	draw_rect(r, Color(c.r * 0.45, c.g * 0.42, c.b * 0.38, 0.9), false, 5)
	draw_rect(r, c, false, 2)
	if not rivets or r.size.x < 46.0:
		return
	for p in [Vector2(r.position.x + 9, r.position.y + 9),
			Vector2(r.position.x + r.size.x - 9, r.position.y + 9),
			Vector2(r.position.x + 9, r.position.y + r.size.y - 9),
			Vector2(r.position.x + r.size.x - 9, r.position.y + r.size.y - 9)]:
		draw_circle(p, 3.0, Color(c.r * 1.25, c.g * 1.2, c.b * 1.1))
		draw_circle(p - Vector2(0.8, 0.8), 1.2, Color(1, 0.95, 0.82, 0.75))

# A cream plaque with dark text: the highest contrast pairing available
# and the reason his mockup reads at a glance where mine did not.
func _plaque(r: Rect2, text: String, size: int, lit := false,
		ink := PLATE_INK) -> void:
	# Solid cream FIRST, then the leather over it at low alpha as tooth.
	# Tinting the dark bar texture up by 1.9 did not make it cream, it
	# made it brown, and the dark ink on it disappeared entirely -- the
	# card names went invisible.
	draw_rect(r, PLATE.lightened(0.10) if lit else PLATE, true)
	if tex_bar:
		draw_texture_rect(tex_bar, r, false, Color(1, 1, 1, 0.16))
	_frame(r, lit)
	draw_multiline_string(_fb(), Vector2(r.position.x + 8,
		r.position.y + size + (r.size.y - size * 2.0) * 0.5),
		text, HORIZONTAL_ALIGNMENT_CENTER, r.size.x - 16, size, 2, ink)

# A brass band around the whole screen, with rivets along it and a
# heavier block at each corner.  It is what makes his mockup read as a
# machine rather than a web form, and it costs four rectangles.
func _bezel(r: Rect2) -> void:
	var t := 26.0
	for band in [Rect2(r.position, Vector2(r.size.x, t)),
			Rect2(Vector2(r.position.x, r.end.y - t), Vector2(r.size.x, t)),
			Rect2(r.position, Vector2(t, r.size.y)),
			Rect2(Vector2(r.end.x - t, r.position.y), Vector2(t, r.size.y))]:
		# Solid brass first, texture over it.  Tinting the dark panel
		# texture up does not make brass, it makes dark brass -- the
		# same mistake as the cream plaques, made again.
		draw_rect(band, Color("8a6a2e"), true)
		if tex_panel:
			draw_texture_rect(tex_panel, band, false, Color(1, 1, 1, 0.30))
		draw_rect(band, Color(0.16, 0.11, 0.04, 0.9), false, 2)
	# rivets along the top and bottom, and the corner blocks
	var step := 96.0
	var x := r.position.x + t + 30.0
	while x < r.end.x - t - 20.0:
		for yy in [r.position.y + t * 0.5, r.end.y - t * 0.5]:
			draw_circle(Vector2(x, yy), 4.0, Color("c79a4e"))
			draw_circle(Vector2(x - 1.0, yy - 1.0), 1.6, Color(1, 0.96, 0.84, 0.8))
		x += step
	for c in [r.position, Vector2(r.end.x - 58.0, r.position.y),
			Vector2(r.position.x, r.end.y - 58.0),
			Vector2(r.end.x - 58.0, r.end.y - 58.0)]:
		var cr := Rect2(c, Vector2(58, 58))
		draw_rect(cr, Color("a07c36"), true)
		if tex_panel:
			draw_texture_rect(tex_panel, cr, false, Color(1, 1, 1, 0.28))
		draw_rect(cr, Color(0.16, 0.11, 0.04, 0.9), false, 2)
		draw_circle(cr.get_center(), 6.0, Color("d8ab5c"))
		draw_circle(cr.get_center() - Vector2(1.5, 1.5), 2.4,
			Color(1, 0.97, 0.86, 0.85))

# A field box: dark inside, brass outside, rivets at the ends.  The
# LineEdit sits inside it and draws only the text.
func _slot(r: Rect2) -> void:
	draw_rect(r, Color("14120e"), true)
	_frame(r, false)

func _glow(r: Rect2, col: Color, rings := 4) -> void:
	for i in range(rings, 0, -1):
		var g := float(i)
		draw_rect(r.grow(g * 2.0),
			Color(col.r, col.g, col.b, 0.30 / g), false, 2.0)
	draw_rect(r, col, false, 2)

func _card_plate(idx: int, at: Vector2, w: float, h: float, dim: bool,
		tint: Color, label := "") -> void:
	# Larger type takes room from the picture rather than running out of
	# the plate.  At the largest setting a small card is mostly its name,
	# which is the right trade for somebody who cannot read the name.
	var art_h: float = h * clampf(0.56 - (fk - 1.0) * 0.55, 0.22, 0.56)
	var tex := _card_tex(idx)
	if tex:
		# ART_REGION is in pixels of the 512-wide composed face; a
		# smaller source needs the same region scaled to it.
		var k: float = tex.get_size().x / 512.0
		draw_texture_rect_region(tex, Rect2(at, Vector2(w, art_h)),
			Rect2(ART_REGION.position * k, ART_REGION.size * k), tint)
	else:
		_box(Rect2(at, Vector2(w, art_h)), Color("241f18"), Color("4a3f2e"))
	var pr := Rect2(at.x, at.y + art_h, w, h - art_h)
	var fs: int = int(clampf(w * 0.125, 13.0, 21.0))
	# A single word cannot wrap, so if the longest one does not fit the
	# type comes down until it does.  "Ambitious" on a lord at the
	# largest setting was being clipped to "Ambitiou".
	var words: PackedStringArray = (_card_name(idx) if label == "" \
		else label).split(" ")
	for _guard in range(8):
		var widest := 0.0
		for wd in words:
			widest = maxf(widest, _fb().get_string_size(wd,
				HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(fs)).x)
		if widest <= w - 12.0 or fs <= 9:
			break
		fs -= 1
	# The composed card's own panel colour, not a deck tint.
	#
	# Tinting the plate towards the deck turned Aldemar's grey-olive
	# into a dull slab; on the real card the panel is the SAME warm
	# gold on every card and only the frame carries the deck.  PARCH
	# with a radial gradient multiplied over it, near-white in the
	# middle and darker at the edges -- approximated here by three
	# nested rects, which at this size reads as the same falloff.
	var acc := _deck_accent(idx)
	var base := Color("d2bc8e")
	var hi := Color("fffaf0")
	var lo := Color("b8a074")
	if help.has("parch"):
		base = Color(String(help["parch"].title))
		var g: PackedStringArray = String(help["parch"].body).split(" ")
		if g.size() == 2:
			hi = Color(g[0]); lo = Color(g[1])
	var edge: Color = Color(base.r * lo.r, base.g * lo.g, base.b * lo.b)
	var mid: Color = Color(base.r * hi.r, base.g * hi.g, base.b * hi.b)
	# card.sh finishes the panel with -modulate 101,110,101: a tenth
	# more saturation and a touch more light.  Without it the multiply
	# alone comes out grey, which is exactly what he was pointing at --
	# two different yellows, and mine was the dull one.
	edge.s = minf(1.0, edge.s * 1.10); edge.v = minf(1.0, edge.v * 1.01)
	mid.s = minf(1.0, mid.s * 1.10); mid.v = minf(1.0, mid.v * 1.01)
	if dim:
		edge = edge.darkened(0.42); mid = mid.darkened(0.42)
	draw_rect(pr, edge, true)
	draw_rect(pr.grow(-3.0), edge.lerp(mid, 0.55), true)
	draw_rect(pr.grow(-9.0), mid, true)
	if tex_bar:
		draw_texture_rect(tex_bar, pr, false, Color(1, 1, 1, 0.13))
	_fit("card name", _card_name(idx) if label == "" else label, fs,
		(w - 12.0) * 3.0, true)
	draw_multiline_string(_fb(), Vector2(pr.position.x + 7,
		pr.position.y + float(_fs(fs)) + 6.0),
		_card_name(idx) if label == "" else label,
		HORIZONTAL_ALIGNMENT_CENTER, w - 12, _fs(fs), 3,
		PLATE_INK if not dim else Color("2b2119"))
	# The card's own colour outside, a thin gold rule inside: the same
	# two-line edge the composed card has.
	var full := Rect2(at, Vector2(w, h))
	draw_rect(full, _deck_dark(idx), false, 6)
	draw_rect(full, acc if not dim else acc.darkened(0.45), false, 4)
	draw_rect(full.grow(-4.0), Color(0.80, 0.68, 0.42, 0.55 if not dim else 0.2),
		false, 1)
	draw_rect(Rect2(pr.position, Vector2(pr.size.x, 1.5)),
		Color(0.62, 0.50, 0.30, 0.8), true)

# A mini card should look like its own card, not like every other one.
#
# Every small card carried the same gold frame and the same pale plate,
# so five cards from three different decks were five identical yellow
# rectangles.  The composed face takes its frame from ACCENT in
# tools/card.sh; these are the same numbers, generated from that file by
# mkhelp so the two cannot drift apart.
func _deck_accent(idx: int) -> Color:
	var d: String = String(card_meta.get(idx, {}).get("deck", ""))
	var k := "accent.%s" % d
	if help.has(k):
		return Color(String(help[k].title))
	return BRASS

func _deck_dark(idx: int) -> Color:
	var d: String = String(card_meta.get(idx, {}).get("deck", ""))
	var k := "accent.%s" % d
	if help.has(k):
		return Color(String(help[k].body))
	return Color("241f18")

# A lord's Trait is the whole of its rules text -- design.xml says so in
# as many words -- so on a lord the large word is the Trait and not the
# name.  His mockup had that right and I had it the other way round.
var plate_label := ""

func _card(idx: int, at: Vector2, w: float, dim := false) -> void:
	var tex := _card_tex(idx)
	var h := w * 717.0 / 512.0
	# The hit region is always the resting rect, never the grown one.
	# Registering the grown rect would make the card lose the mouse the
	# moment it grew past it, and it would flicker at the edges.
	card_rects.append({"rect": Rect2(at, Vector2(w, h)), "idx": idx})
	var r := Rect2(at, Vector2(w, h))
	if idx == hover_card and idx >= 0 and top_card.is_empty():
		# Grown about its own centre, so it does not shove its
		# neighbours or crawl towards a corner.
		top_card = {"r": r.grow(w * 0.06), "idx": idx, "dim": dim}
		return
	_shadow(r.position, r.size)
	if w < 200.0:
		# Too small for its own text.  Art and a legible name instead.
		_card_plate(idx, r.position, r.size.x, r.size.y, dim,
			Color(0.52, 0.50, 0.48, 0.9) if dim else Color.WHITE,
			plate_label)
		plate_label = ""
		return
	if tex:
		# Dimmed AND cooled: alpha alone still read as a bright card at a
		# glance, because the art is high-contrast oil paint.
		draw_texture_rect(tex, r, false,
			Color(0.42, 0.40, 0.38, 0.85) if dim else Color.WHITE)
	else:
		# A card with no art yet still has to occupy its place, or the
		# hand silently shrinks and nobody knows a card is missing.
		_box(r, Color("241f18"), Color("4a3f2e"))
		# Wrapped to the frame, because a name wider than the card ran
		# into the card beside it and read as one long nonsense.
		draw_string(_f(), at + Vector2(5, 18), _card_name(idx),
			HORIZONTAL_ALIGNMENT_LEFT, w - 10, 13, DIM)
		_t(at + Vector2(5, h - 8), "no art yet", 11, Color("5a4e3a"))

# One repaint, timed.  The menu only redraws on demand, so frame-to-frame
# says nothing; what matters is how long a single repaint costs, because
# that is the lag between a click and seeing it.
func _draw() -> void:
	if not times:
		_draw_all()
		return
	var t0 := Time.get_ticks_usec()
	_draw_all()
	var ms := float(Time.get_ticks_usec() - t0) / 1000.0
	t_frame_n += 1
	if ms > 40.0 or t_frame_n <= 4:
		print("  repaint %-14s %.0f ms" %
			[screen if screen != "login" else "menu/" + menu, ms])

func _draw_all() -> void:
	dec_budget = 2
	dec_more = false
	vw = maxf(size.x, 1280.0)
	vh = maxf(size.y, 720.0)
	right = vw - PANEL_W
	_measure()
	_place_fields()
	buttons = []
	card_rects = []
	top_card = {}
	lobby_rows = []
	help_spots = []
	deck_spots = []
	if screen == "login":
		_draw_login()
		return
	if screen == "lobby":
		_draw_lobby()
		return
	if screen == "over":
		_draw_over()
		return
	draw_rect(Rect2(0, 0, vw, vh), GROUND, true)
	if bg_tex:
		# Cover, not stretch.  A square photograph in a 16:10 window was
		# squashed a third narrower than it was painted, and a face is
		# the one thing that cannot survive that.  The larger scale of
		# the two wins and the overflow is cropped off the centre.
		var ts := bg_tex.get_size()
		if ts.x > 0.0 and ts.y > 0.0:
			var sc: float = maxf(vw / ts.x, vh / ts.y)
			var sw: float = vw / sc
			var sh: float = vh / sc
			draw_texture_rect_region(bg_tex, Rect2(0, 0, vw, vh),
				Rect2((ts.x - sw) * 0.5, (ts.y - sh) * 0.5, sw, sh))
		else:
			draw_texture_rect(bg_tex, Rect2(0, 0, vw, vh), false)
		# Held well back.  This is a table to read cards on, not a
		# wallpaper: at 0.42 the lord names underneath were competing
		# with a lit brass pipe and losing.
		draw_rect(Rect2(0, 0, vw, vh), Color(0.02, 0.02, 0.03, 0.66), true)
	elif tex_table:
		# The leather stays, but as a tooth over slate rather than as the
		# colour of everything.
		draw_texture_rect(tex_table, Rect2(0, 0, vw, vh), false,
			Color(0.42, 0.40, 0.38, 0.30))
	if tex_vig:
		draw_texture_rect(tex_vig, Rect2(0, 0, vw, vh), false)
	if view.is_empty():
		_tb(Vector2(40, 60), "COURT OF TREASONS", 40, PALE)
		_t(Vector2(40, 100), status, 20, DIM)
		return
	if times and t_draws < 3:
		t_draws += 1
		print("  table frame %d     %d ms" % [t_draws, Time.get_ticks_msec()])
	var me := int(view.me)
	var them := 1 - me
	var houses: Array = view.get("houses", [])
	if houses.size() < 2:
		return
	if panel_left:
		draw_set_transform(Vector2(PANEL_W, 0))
	var _ts := Time.get_ticks_usec()
	_house_bar(houses[them], them, 0.0, bar_h, false)
	_lords(them, y_their_lords, "THEIR LORDS")
	_middle()
	# Straight off the bottom, with no floor.
	#
	# A floor was added to stop the rows inverting at large zoom and it
	# made things worse at small zoom: it pushed the house bar DOWN into
	# the hand at 110%, which is the only zoom anybody will use.  The
	# scale is capped instead, where the arithmetic actually is.
	if times and t_draws == 1:
		print("    lords+middle   %d ms, %d decodes %d ms"
			% [(Time.get_ticks_usec() - _ts) / 1000, t_decodes,
			   t_decode_us / 1000])
		_ts = Time.get_ticks_usec()
	_lords(me, y_your_lords, "YOUR LORDS")
	_house_bar(houses[me], me, y_your_bar, bar_h, true)
	if times and t_draws == 1:
		print("    your lords     %d ms, %d decodes %d ms"
			% [(Time.get_ticks_usec() - _ts) / 1000, t_decodes,
			   t_decode_us / 1000])
		_ts = Time.get_ticks_usec()
	_hand()
	if times and t_draws == 1:
		print("    hand           %d ms, %d decodes %d ms"
			% [(Time.get_ticks_usec() - _ts) / 1000, t_decodes,
			   t_decode_us / 1000])
		_ts = Time.get_ticks_usec()
	# Three groups, and every overlay belongs to exactly one of them.
	# The panel group is measured from `right`; the table group from 0;
	# the screen group is positioned off the raw cursor and is painted
	# where it is.  _reading_copy reads card_rects and _top_card reads a
	# rect captured during the table draw, so both are table.
	if panel_left:
		draw_set_transform(Vector2(-right, 0))
	_panel()
	_hint()
	if panel_left:
		draw_set_transform(Vector2(PANEL_W, 0))
	_chat()
	_top_card()
	_reading_copy()
	if panel_left:
		draw_set_transform(Vector2.ZERO)
	_tooltip()
	_help_screen()
	_scale_readout()
	_bg_readout()

func _bg_readout() -> void:
	if bg_shown <= 0.0:
		return
	var nm: String = bg_names[bg_pick] if bg_pick < bg_names.size() else ""
	var txt := "the painted table" if nm == "" else nm.get_file().get_basename()
	txt = "%s   (%d of %d)" % [txt, bg_pick + 1, bg_names.size()]
	var w := maxf(300.0, _f().get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(20)).x + 48.0)
	var at := Vector2(vw * 0.5 - w * 0.5, 40.0)
	_surface(Rect2(at, Vector2(w, 54)), tex_bar, Color(1.1, 1.05, 0.95),
		Color("241d14"))
	draw_rect(Rect2(at, Vector2(w, 54)), Color(0.62, 0.52, 0.34, 0.9), false, 2)
	_tb(at + Vector2(24, 35), txt, 20, PALE)

func _scale_readout() -> void:
	if scale_shown <= 0.0:
		return
	var txt := "text size  %d%%" % int(round(fk * 100.0))
	var w := 260.0
	var at := Vector2(vw * 0.5 - w * 0.5, 40.0)
	_surface(Rect2(at, Vector2(w, 54)), tex_bar, Color(1.1, 1.05, 0.95),
		Color("241d14"))
	draw_rect(Rect2(at, Vector2(w, 54)), Color(0.62, 0.52, 0.34, 0.9), false, 2)
	_tb(at + Vector2(24, 35), txt, 22, PALE)

# A line saying help exists, because help nobody finds is help nobody has.
func _hint() -> void:
	if help_open:
		return
	# In the panel, not along the bottom of the table: the hand is drawn
	# over the bottom edge and the line was hidden behind the cards.
	if screen == "game":
		_t(Vector2(right + 30.0, vh - 46.0), "hover anything to learn what it is",
			15, Color("6a5c46"))
		_t(Vector2(right + 30.0, vh - 24.0),
			"?  the rules      + -  text size      b  background", 15, Color("9b8a6a"))
	else:
		_t(Vector2(60, vh - 46.0),
			"hover anything to learn what it is     ?  the rules     + -  text size",
			15, Color("6a5c46"))

func _wrapped(at: Vector2, w: float, text: String, sz: int, col: Color,
		bold := false) -> float:
	var fnt: Font = _fb() if bold else _f()
	var used := fnt.get_multiline_string_size(text, HORIZONTAL_ALIGNMENT_LEFT,
		w, _fs(sz))
	draw_multiline_string(fnt, at, text, HORIZONTAL_ALIGNMENT_LEFT, w,
		_fs(sz), -1, col)
	return used.y

func _tooltip() -> void:
	if help_open or hover_help == "" or not help.has(hover_help):
		return
	# When the reading copy is already open, the card says more about
	# what this does than "Play a card" ever will, and two panels fought
	# each other for the same corner of the screen.  The card wins.
	if hover_card >= 0 and hover_help.begins_with("act."):
		return
	var e: Dictionary = help[hover_help]
	# Big, because this is the one thing on screen with no layout to
	# fight: an overlay can be as large as it needs to be, and if the
	# complaint is font size then the explanation is the worst possible
	# place to economise.
	var ts: int = 27
	var bs: int = 21
	var w := 620.0
	# The raw cursor, not the mapped one: this is painted in screen
	# space, so it has to be placed in screen space too.
	var m := get_local_mouse_position()
	# Placed off the cursor, and pushed back inside the screen rather than
	# being allowed to hang off the edge where it cannot be read.
	# Measured at the size it is DRAWN at.  _wrapped multiplies by the
	# type setting and this did not, so above 100% the box was sized for
	# smaller text than it then received and the last lines came out
	# through the bottom of the frame.  That is the breaking he saw.
	var body_h: float = _f().get_multiline_string_size(
		String(e.body), HORIZONTAL_ALIGNMENT_LEFT, w - 36.0, _fs(bs)).y
	var h := body_h + float(_fs(ts)) + 46.0
	# To the left of the cursor when the cursor is in the panel, so the
	# explanation does not sit on top of the buttons it is explaining.
	var at := Vector2(m.x + 24.0, m.y + 20.0)
	# Never over the panel.  Everything in there is a control, and an
	# explanation that hides the buttons it is explaining is worse than
	# no explanation -- both of his screenshots are of exactly that.
	var lo: float = (PANEL_W + 12.0) if panel_left else 12.0
	var hi: float = (vw - w - 12.0) if panel_left else (right - w - 16.0)
	if at.x > hi:
		at.x = m.x - w - 24.0
	at.x = clampf(at.x, lo, maxf(lo, hi))
	at.y = clampf(at.y, 12.0, vh - h - 12.0)
	_shadow(at, Vector2(w, h), 2.0)
	_surface(Rect2(at, Vector2(w, h)), tex_bar, Color(1.1, 1.04, 0.92),
		Color("241d14"))
	draw_rect(Rect2(at, Vector2(w, h)), Color(0.62, 0.52, 0.34, 0.85), false, 2)
	_tb(at + Vector2(18, float(_fs(ts)) + 12.0), String(e.title), ts,
		Color("f6e6c2"))
	_rule(at.x + 18, at.y + float(_fs(ts)) + 20.0, w - 36, 0.4)
	_wrapped(at + Vector2(18, float(_fs(ts)) + 46.0), w - 36.0,
		String(e.body), bs, Color("e2d2ae"))

# Everything at once, for somebody who has just sat down.
const HELP_ORDER := ["round", "winning", "standing", "levy", "unrest",
	"grievance", "court", "paramount", "lords", "promise", "ambition",
	"terms"]

func _help_screen() -> void:
	if not help_open:
		return
	draw_rect(Rect2(0, 0, vw, vh), Color(0.04, 0.03, 0.02, 0.93), true)
	_tb(Vector2(60, 76), "HOW THIS IS PLAYED", 42, PALE)
	_rule(60, 98, vw - 120.0, 0.6)
	_t(Vector2(60, 128), "press ? or escape to go back", 16, DIM)
	var colw := (vw - 180.0) * 0.5
	var y := 176.0
	var x := 60.0
	for k in HELP_ORDER:
		if not help.has(k):
			continue
		var e: Dictionary = help[k]
		var bh: float = _f().get_multiline_string_size(String(e.body),
			HORIZONTAL_ALIGNMENT_LEFT, colw, _fs(20)).y
		if y + bh + 54.0 > vh - 40.0 and x < 60.0 + colw:
			x += colw + 60.0
			y = 176.0
		_tb(Vector2(x, y), String(e.title), 24, Color("e0bd80"))
		y += 10.0
		y += _wrapped(Vector2(x, y + 20.0), colw, String(e.body), 20,
			Color("ded0ae")) + 42.0

func _seat_name(seat: int) -> String:
	if seat < 0:
		return "?"
	if seat == int(view.get("me", -1)):
		return "you"
	var hs: Array = view.get("houses", [])
	if seat < hs.size():
		return HOUSE_NAMES[int(hs[seat].get("house", 0))].capitalize()
	return "seat %d" % seat

func _chat() -> void:
	if not pvp:
		return
	var r := Rect2(CHAT_X, vh - UP_CHAT, right - CHAT_X - 20.0, CHAT_H)
	_surface(r, tex_bar, Color(0.9, 0.86, 0.78), Color("1d1810"))
	draw_rect(r, Color(0.45, 0.38, 0.26, 0.8), false, 2)
	_tb(Vector2(CHAT_X + 12, r.position.y + 24), "TALK", 15, Color("a8956f"))
	_rule(CHAT_X + 12, r.position.y + 30, 120, 0.45)
	# Oldest at the top, newest just above the box, so the eye lands on
	# the newest line on its way down to the thing it types into.
	for i in range(chat_lines.size()):
		_t(Vector2(CHAT_X + 12, r.position.y + 58 + i * 24), chat_lines[i], 17,
			Color("cbb894"))

func _top_card() -> void:
	if top_card.is_empty():
		return
	var r: Rect2 = top_card.r
	var tex := _card_tex(int(top_card.idx))
	_shadow(r.position, r.size, 1.7)
	if r.size.x < 200.0:
		_card_plate(int(top_card.idx), r.position, r.size.x, r.size.y,
			false, Color.WHITE)
		return
	if tex:
		draw_texture_rect(tex, r, false,
			Color(1, 1, 1, 0.55) if top_card.dim else Color.WHITE)
	else:
		_box(r, Color("2c261d"), Color("6a5a3a"))
		draw_string(_f(), r.position + Vector2(5, 18), _card_name(int(top_card.idx)),
			HORIZONTAL_ALIGNMENT_LEFT, r.size.x - 10, 13, INK)

# A card is 140 pixels wide in the hand and its rules text is a smear at
# that size.  Growing it in place is the feedback; this is the part that
# can actually be read.  Drawn last, so it is over everything.
func _reading_copy() -> void:
	if hover_card < 0:
		return
	var tex := _card_full(hover_card)
	# Bigger, and bigger again with the type setting.  This is the only
	# place the rules text is meant to be read, so it is the last place
	# to be modest about size.
	var w: float = clampf(520.0 * fk, 380.0, minf(660.0, vh * 0.62))
	var h := w * 717.0 / 512.0
	var at := Vector2(0, 0)
	# Beside the card, on whichever side has the room, and never off the
	# bottom or under the panel.
	var src := Rect2(0, 0, 0, 0)
	for c in card_rects:
		if int(c.idx) == hover_card:
			src = c.rect
			break
	at.x = src.position.x + src.size.x + 18.0
	if at.x + w > right - 12.0:
		at.x = src.position.x - w - 18.0
	at.x = clamp(at.x, 12.0, right - w - 12.0)
	at.y = clamp(src.position.y + src.size.y * 0.5 - h * 0.5, 12.0, vh - h - 12.0)
	_shadow(at, Vector2(w, h), 2.6)
	if tex:
		draw_texture_rect(tex, Rect2(at, Vector2(w, h)), false)
	else:
		_box(Rect2(at, Vector2(w, h)), Color("241f18"), Color("6a5a3a"))
		draw_string(_fb(), at + Vector2(16, 40), _card_name(hover_card),
			HORIZONTAL_ALIGNMENT_LEFT, w - 32, 24, PALE)
		_t(at + Vector2(16, 76), "no art yet", 15, DIM)
	draw_rect(Rect2(at, Vector2(w, h)), Color(0.62, 0.52, 0.34, 0.55), false, 2)


# What happened, before the table is taken away.
#
# The result used to be one line at the bottom of the main menu, after
# the screen had already changed: you won or lost and had no idea how,
# or against what.
func _draw_over() -> void:
	draw_rect(Rect2(0, 0, vw, vh), Color("23262a"), true)
	if tex_table:
		draw_texture_rect(tex_table, Rect2(0, 0, vw, vh), false,
			Color(0.40, 0.40, 0.42, 0.35))
	if tex_vig:
		draw_texture_rect(tex_vig, Rect2(0, 0, vw, vh), false)
	_bezel(Rect2(0, 0, vw, vh))

	var cw: float = minf(940.0, vw - 180.0)
	var cx := vw * 0.5 - cw * 0.5
	var y := vh * 0.5 - 300.0

	var head: String = "YOU WIN" if end_won else "YOU LOSE"
	var col: Color = Color("e2b45c") if end_won else Color("c07a62")
	var hw: float = _fb().get_string_size(head, HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(64)).x
	draw_string(_fb(), Vector2(cx + cw * 0.5 - hw * 0.5 + 3, y + 4), head,
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(64), Color(0, 0, 0, 0.75))
	draw_string(_fb(), Vector2(cx + cw * 0.5 - hw * 0.5, y), head,
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(64), col)
	y += 30.0
	var ww: float = _f().get_string_size("by " + end_why,
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(26)).x
	_t(Vector2(cx + cw * 0.5 - ww * 0.5, y + 30.0), "by " + end_why, 26,
		Color("cbb894"))
	y += 62.0
	_rule(cx + 60.0, y, cw - 120.0, 0.6)
	y += 26.0

	# Both Houses, side by side, so a loss can be read rather than
	# guessed at: what each held when it ended.
	var hs: Array = end_view.get("houses", [])
	var me := int(end_view.get("me", 0))
	if hs.size() >= 2:
		var colw := (cw - 160.0) * 0.5
		for k in range(2):
			var seat: int = me if k == 0 else 1 - me
			var h: Dictionary = hs[seat]
			var hid := int(h.get("house", 0))
			var bx := cx + 80.0 + float(k) * (colw + 0.0)
			var tint: Color = HOUSE_TINT[clampi(hid, 0, HOUSE_TINT.size() - 1)]
			var lbl: String = "YOU" if k == 0 else "THEM"
			_t(Vector2(bx, y + 18.0), lbl, 15,
				Color("c98a52") if k == 0 else Color("4aa37a"))
			_tb(Vector2(bx, y + 48.0),
				String(HOUSE_NAMES[clampi(hid, 0, HOUSE_NAMES.size() - 1)]),
				28, tint)
			if seat == end_winner:
				var cr: float = _fb().get_string_size("CROWNED",
					HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(15)).x
				_tb(Vector2(bx + colw - cr - 20.0, y + 48.0), "CROWNED", 15,
					Color("e2b45c"))
			var st: Array = h.get("standing", [0, 0, 0])
			var rows := [
				["MILITARY", str(int(st[0]))],
				["CAPITAL", str(int(st[1]))],
				["GOLD", str(int(st[2]))],
				["COURT FAVOUR", str(int(h.get("favour", 0)))],
				["WORD KEPT", str(int(h.get("kept", 0)))],
				["BROKEN", str(int(h.get("broken", 0)))],
			]
			var ry := y + 78.0
			for r in rows:
				_t(Vector2(bx, ry + 22.0), String(r[0]), 17, Color("8a7a5c"))
				var vwd: float = _fb().get_string_size(String(r[1]),
					HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(19)).x
				_tb(Vector2(bx + colw - vwd - 40.0, ry + 22.0), String(r[1]),
					19, INK)
				ry += 28.0
			if not bool(h.get("alive", 1)):
				_tb(Vector2(bx, ry + 24.0), "FALLEN", 17, WARN)
		y += 78.0 + 6.0 * 28.0 + 40.0

	for it in [["PLAY AGAIN", "again"], ["MAIN MENU", "menuBack"]]:
		var br := Rect2(cx + cw * 0.5 - 200.0, y, 400.0, 64.0)
		var bhot: bool = hover >= 0 and hover < buttons.size() \
			and buttons[hover].action == it[1]
		if bhot:
			_glow(br, BRASS_LIT, 4)
		_plaque(br, String(it[0]), 26, bhot)
		buttons.append({"rect": br, "action": it[1]})
		y += 78.0

func _draw_login() -> void:
	draw_rect(Rect2(0, 0, vw, vh), Color("23262a"), true)
	if tex_table:
		draw_texture_rect(tex_table, Rect2(0, 0, vw, vh), false,
			Color(0.40, 0.40, 0.42, 0.35))
	if tex_vig:
		draw_texture_rect(tex_vig, Rect2(0, 0, vw, vh), false)
	# His mockup, as close as immediate mode gets: a brass band round the
	# whole screen, big gold labels beside their boxes, and PLAY as the
	# one bright object on it.
	_bezel(Rect2(0, 0, vw, vh))

	var cw: float = minf(940.0, vw - 180.0)
	var cx := vw * 0.5 - cw * 0.5
	var y := vh * 0.5 - 330.0

	var tsz := 66
	var tw: float = _fb().get_string_size("COURT OF TREASONS",
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(tsz)).x
	while tw > cw and tsz > 30:
		tsz -= 2
		tw = _fb().get_string_size("COURT OF TREASONS",
			HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(tsz)).x
	var tx := vw * 0.5 - tw * 0.5
	# Heavier than _tb: a dark relief under it and a pale highlight over
	# the top, which is the nearest thing to his gradient without a
	# shader.
	draw_string(_fb(), Vector2(tx + 3, y + 4), "COURT OF TREASONS",
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(tsz), Color(0, 0, 0, 0.75))
	draw_string(_fb(), Vector2(tx, y), "COURT OF TREASONS",
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(tsz), Color("e2b45c"))
	draw_string(_fb(), Vector2(tx, y - 2), "COURT OF TREASONS",
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(tsz), Color(1, 0.94, 0.78, 0.38))
	y += 54.0

	# Four doors, not a form.
	#
	# The join-a-server fields were the front page, with PLAY above them,
	# which is the wrong weight: almost every game is one person against
	# the machine on this computer.  Multiplayer is a door you open when
	# you want it.
	if menu == "main":
		var items := [["PLAY", "menuHouse"], ["MULTIPLAYER", "menuMulti"],
			["SETTINGS", "menuSettings"], ["QUIT", "menuQuit"]]
		var first := true
		for it in items:
			var h: float = 96.0 if first else 66.0
			var br := Rect2(cx + 90.0, y, cw - 180.0, h)
			var bhot: bool = hover >= 0 and hover < buttons.size() \
				and buttons[hover].action == it[1]
			if bhot:
				_glow(br, BRASS_LIT, 6 if first else 4)
			_plaque(br, String(it[0]), 44 if first else 28, bhot)
			buttons.append({"rect": br, "action": it[1]})
			y += h + 18.0
			first = false
		return

	# Choosing a House is a decision, not a dropdown on the way past.
	# It gets its own screen: the shield, the name, the words the House
	# is known by, and one door out of it.
	if menu == "house":
		_menu_title(cx, y, cw, "CHOOSE YOUR HOUSE")
		y += 58.0
		var hid: int = want_house
		var sz: float = minf(230.0, vh * 0.26)
		var shield := Rect2(cx + cw * 0.5 - sz * 0.5, y, sz, sz)
		var shield_tex := _back_tex(hid)
		if hid >= 0 and shield_tex:
			draw_texture_rect(shield_tex, shield, false)
			draw_rect(shield, Color(0.62, 0.52, 0.34, 0.75), false, 2)
		else:
			draw_rect(shield, Color("1b1811"), true)
			_frame(shield, false, false)
			var qw: float = _fb().get_string_size("?",
				HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(64)).x
			_tb(Vector2(shield.get_center().x - qw * 0.5,
				shield.get_center().y + 22.0), "?", 64, Color("4a3f2e"))
		y += sz + 22.0
		var nm: String = "ANY HOUSE" if hid < 0 \
			else String(HOUSE_NAMES[clampi(hid, 0, HOUSE_NAMES.size() - 1)])
		var tint: Color = Color("cbb894") if hid < 0 \
			else HOUSE_TINT[clampi(hid, 0, HOUSE_TINT.size() - 1)]
		var nw: float = _fb().get_string_size(nm, HORIZONTAL_ALIGNMENT_LEFT,
			-1, _fs(40)).x
		_tb(Vector2(cx + cw * 0.5 - nw * 0.5, y + 40.0), nm, 40, tint)
		y += 52.0
		var word: String = "the Court deals you one" if hid < 0 \
			else String(HOUSE_WORDS[clampi(hid, 0, HOUSE_WORDS.size() - 1)])
		var ww: float = _f().get_string_size(word, HORIZONTAL_ALIGNMENT_LEFT,
			-1, _fs(22)).x
		_t(Vector2(cx + cw * 0.5 - ww * 0.5, y + 24.0), word, 22,
			Color("9b8a6a"))
		y += 44.0
		y += _house_picker(cx, y, cw) + 20.0
		var sr := Rect2(cx + cw * 0.5 - 200.0, y, 400.0, 74.0)
		var shot2: bool = hover >= 0 and hover < buttons.size() \
			and buttons[hover].action == "here"
		if shot2:
			_glow(sr, BRASS_LIT, 5)
		_plaque(sr, "TAKE THE SEAT", 30, shot2)
		buttons.append({"rect": sr, "action": "here"})
		y += 74.0 + 14.0
		_back_button(cx, y, cw)
		return

	if menu == "settings":
		_menu_title(cx, y, cw, "SETTINGS")
		y += 62.0
		y += _setting_row(cx, y, cw, "TEXT SIZE",
			"%d%%" % int(round(fk * 100.0)), "sizeDown", "sizeUp")
		var nm: String = bg_names[bg_pick] if bg_pick < bg_names.size() else ""
		y += _setting_row(cx, y, cw, "BACKGROUND",
			"the painted table" if nm == "" else nm.get_file().get_basename(),
			"bgPrev", "bgNext")
		y += 10.0
		_back_button(cx, y, cw)
		return

	# menu == "multiplayer"
	_menu_title(cx, y, cw, "PLAY A PERSON")
	y += 46.0
	var s2: float = _f().get_string_size(
		"to play a person, or on a machine that is always on",
		HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(18)).x
	_t(Vector2(vw * 0.5 - s2 * 0.5, y + 54.0),
		"to play a person, or on a machine that is always on", 18,
		Color("a99a7c"))
	y += 84.0

	# Labels left, boxes right, both off the same two numbers.
	var lab := ["SERVER", "NAME", "PASSWORD"]
	for i in range(3):
		var ly := y + float(i) * 62.0
		var lw: float = _fb().get_string_size(lab[i],
			HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(24)).x
		_tb(Vector2(field_x - 26.0 - lw, ly + 36.0), lab[i], 24,
			Color("e6c079"))
		_slot(Rect2(field_x - 6.0, ly + 2.0, field_w + 12.0, 48.0))
	y += 3.0 * 62.0 + 16.0

	var r := Rect2(field_x, y, field_w, 56)
	var hot: bool = hover >= 0 and hover < buttons.size() \
		and buttons[hover].action == "login"
	if hot:
		_glow(r, BRASS_LIT, 4)
	draw_rect(r, Color("9a7530") if hot else Color("6f5423"), true)
	if tex_panel:
		draw_texture_rect(tex_panel, r, false, Color(1, 1, 1, 0.28))
	_frame(r, hot)
	var ew: float = _fb().get_string_size("ENTER", HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(28)).x
	_tb(Vector2(r.position.x + (r.size.x - ew) * 0.5, r.position.y + 39.0),
		"ENTER", 28, Color("fff4d6") if hot else Color("e8cf92"))
	buttons.append({"rect": r, "action": "login"})
	y += 84.0

	if denied != "":
		var good: bool = denied.begins_with("you won") or denied.begins_with("you lost")
		var dw: float = _fb().get_string_size(denied,
			HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(22)).x
		_tb(Vector2(vw * 0.5 - dw * 0.5, y + 24.0), denied, 22,
			Color("e8cf92") if good else WARN)
	y += 54.0
	for i in range(2):
		var line: String = [
			"A name nobody has used is yours.  The password is stored hashed,",
			"but it is sent to the server unencrypted -- use a network you trust."][i]
		var lw2: float = _f().get_string_size(line, HORIZONTAL_ALIGNMENT_LEFT,
			-1, _fs(17)).x
		_t(Vector2(vw * 0.5 - lw2 * 0.5, y + 20.0 + float(i) * 24.0), line,
			17, Color("7d6e55"))
	# Clear of the two warning lines above, which are drawn at y+20 and
	# y+44 -- BACK was landing on top of them.
	y += 78.0
	_back_button(cx, y, cw)


# The House picker: one at a time with a nudge either way, rather than a
# box per House.  A row of five boxes was fine for four Houses and would
# have run off the end at eight; this is the same size for four or forty.
func _house_picker(cx: float, y: float, cw: float) -> float:
	var n_h: int = houses.size()
	var pick: String = "ANY HOUSE" if want_house < 0 \
		else String(houses[clampi(want_house, 0, maxi(0, n_h - 1))])
	var ptint: Color = Color("cbb894") if want_house < 0 \
		else HOUSE_TINT[clampi(want_house, 0, HOUSE_TINT.size() - 1)]
	var sel := Rect2(cx + 90.0, y, cw - 180.0, 44)
	draw_rect(sel, Color("1b1811"), true)
	_frame(sel, false, false)
	var lr := Rect2(sel.position, Vector2(52, 44))
	var rr := Rect2(Vector2(sel.end.x - 52, sel.position.y), Vector2(52, 44))
	for side in [["<", lr, "housePrev"], [">", rr, "houseNext"]]:
		var br2: Rect2 = side[1]
		var bh2: bool = hover >= 0 and hover < buttons.size() \
			and buttons[hover].action == side[2]
		if bh2:
			_glow(br2, BRASS_LIT, 2)
		draw_rect(br2, Color("33291a") if bh2 else Color("241f14"), true)
		_frame(br2, bh2, false)
		var aw: float = _fb().get_string_size(String(side[0]),
			HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(20)).x
		_tb(Vector2(br2.position.x + (52.0 - aw) * 0.5,
			br2.position.y + 30.0), String(side[0]), 20,
			Color("f4e6c4") if bh2 else Color("9b8a6a"))
		buttons.append({"rect": br2, "action": side[2]})
	var pw3: float = _fb().get_string_size(pick, HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(20)).x
	_tb(Vector2(sel.get_center().x - pw3 * 0.5, sel.position.y + 30.0),
		pick, 20, ptint)
	_t(Vector2(sel.position.x + 60.0, sel.position.y + 30.0),
		"%d of %d" % [want_house + 2, n_h + 1], 13, Color("6a5c46"))
	return 44.0

func _menu_title(cx: float, y: float, cw: float, txt: String) -> void:
	var w: float = _fb().get_string_size(txt, HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(26)).x
	_tb(Vector2(cx + cw * 0.5 - w * 0.5, y + 26.0), txt, 26, Color("e0a94e"))

func _back_button(cx: float, y: float, cw: float) -> void:
	var br := Rect2(cx + cw * 0.5 - 130.0, y, 260.0, 56.0)
	var bhot: bool = hover >= 0 and hover < buttons.size() \
		and buttons[hover].action == "menuBack"
	if bhot:
		_glow(br, BRASS_LIT, 3)
	_plaque(br, "BACK", 24, bhot)
	buttons.append({"rect": br, "action": "menuBack"})

# label on the left, value between two nudges: the same shape the House
# picker already uses, so settings do not invent a second idiom.
func _setting_row(cx: float, y: float, cw: float, label: String,
		value: String, prev: String, next: String) -> float:
	_tb(Vector2(cx + 90.0, y + 30.0), label, 20, Color("cbb894"))
	var sel := Rect2(cx + cw - 90.0 - 420.0, y, 420.0, 44.0)
	draw_rect(sel, Color("1b1811"), true)
	_frame(sel, false, false)
	var lr := Rect2(sel.position, Vector2(52, 44))
	var rr := Rect2(Vector2(sel.end.x - 52, sel.position.y), Vector2(52, 44))
	for side in [["<", lr, prev], [">", rr, next]]:
		var br2: Rect2 = side[1]
		var bh2: bool = hover >= 0 and hover < buttons.size() \
			and buttons[hover].action == side[2]
		if bh2:
			_glow(br2, BRASS_LIT, 2)
		draw_rect(br2, Color("33291a") if bh2 else Color("241f14"), true)
		_frame(br2, bh2, false)
		var aw: float = _fb().get_string_size(String(side[0]),
			HORIZONTAL_ALIGNMENT_LEFT, -1, _fs(20)).x
		_tb(Vector2(br2.position.x + (52.0 - aw) * 0.5,
			br2.position.y + 30.0), String(side[0]), 20,
			Color("f4e6c4") if bh2 else Color("9b8a6a"))
		buttons.append({"rect": br2, "action": side[2]})
	var vwid: float = _fb().get_string_size(value, HORIZONTAL_ALIGNMENT_LEFT,
		-1, _fs(20)).x
	_tb(Vector2(sel.get_center().x - vwid * 0.5, sel.position.y + 30.0),
		value, 20, Color("e8cf92"))
	return 44.0 + 22.0

func _draw_lobby() -> void:
	_surface(Rect2(0, 0, vw, vh), tex_table, Color(1, 1, 1), GROUND)
	if tex_vig:
		draw_texture_rect(tex_vig, Rect2(0, 0, vw, vh), false)
	_tb(Vector2(60, 84), "THE ANTECHAMBER", 40, PALE)
	_rule(60, 104, minf(880.0, vw - 980.0), 0.7)
	_t(Vector2(60, 134), "you are %s" % me_name, 18, DIM)

	# Who is here.  Somebody looking for a game is a row you can click.
	# The roster keeps to the right-hand side of whatever window it is in.
	var ux := vw - 910.0
	_tb(Vector2(ux, 84), "WHO IS HERE", 18, Color("a8956f"))
	_rule(ux, 102, 500, 0.5)
	var y := 140.0
	for u in lobby_users:
		var nm: String = str(u.get("name", "?"))
		var mine: bool = nm == me_name
		var looking: bool = bool(u.get("seek", 0))
		var r := Rect2(ux, y, 500, 44)
		var hot: bool = false
		for b in lobby_rows:
			if b.rect == r and hover_row == int(u.get("i", -1)): hot = true
		if looking and not mine:
			if hot:
				_glow(r, BRASS_LIT, 3)
			_surface(r, tex_bar, Color(1.5, 1.4, 1.16) if hot else Color(1, 1, 1),
				Color("2a2318"))
			_frame(r, hot)
			lobby_rows.append({"rect": r, "i": int(u.get("i", -1))})
		_tb(Vector2(ux + 14, y + 29), nm, 21, PALE if not mine else Color("d8b070"))
		if looking:
			_t(Vector2(ux + 290, y + 28),
				"waiting" if mine else "sit down", 17, Color("c9a86a"))
		y += 52.0

	# Offer a game, or stop offering.
	var br := Rect2(60, 180, 360, 52)
	var bhot: bool = hover >= 0 and hover < buttons.size() \
		and buttons[hover].action == "seek"
	if bhot:
		_glow(br, BRASS_LIT, 3)
	_surface(br, tex_bar, Color(1.5, 1.4, 1.16) if bhot else Color(1, 1, 1),
		Color("2a2318"))
	_frame(br, bhot)
	_tb(Vector2(br.position.x + 22, br.position.y + 34),
		"STOP LOOKING" if seeking else "LOOK FOR A GAME", 22, PALE)
	buttons.append({"rect": br, "action": "seek"})

	# A room with one person in it should still be a game.
	var ar := Rect2(440, 180, 340, 52)
	var ahot: bool = hover >= 0 and hover < buttons.size() \
		and buttons[hover].action == "alone"
	if ahot:
		_glow(ar, BRASS_LIT, 3)
	_surface(ar, tex_bar, Color(1.5, 1.4, 1.16) if ahot else Color(1, 1, 1),
		Color("2a2318"))
	_frame(ar, ahot)
	_tb(Vector2(ar.position.x + 22, ar.position.y + 34),
		"PLAY THE MACHINE", 22, PALE)
	buttons.append({"rect": ar, "action": "alone"})

	# The room talking.
	_tb(Vector2(60, 300), "TALK", 18, Color("a8956f"))
	_rule(60, 318, minf(880.0, vw - 980.0), 0.5)
	var ty := 352.0
	for line in chat_lines:
		_t(Vector2(60, ty), line, 19,
			Color("8a7a5c") if line.begins_with("- ") else Color("cbb894"))
		ty += 28.0
	_t(Vector2(60, vh - 130.0), status, 16, DIM)

func _house_bar(h: Dictionary, seat: int, y: float, ht: float, mine: bool) -> void:
	_surface(Rect2(0, y, right, ht), tex_bar, Color(1, 1, 1), Color("20160f"))
	_rule(0, y + ht - 2, right, 0.7)
	if y > 0:
		_rule(0, y, right, 0.7)
	var hid := int(h.get("house", 0))
	_spot(Rect2(20, y + 8, 340, 62), "house")
	_tb(Vector2(28, y + 36), HOUSE_NAMES[hid], 33, HOUSE_TINT[hid])
	_t(Vector2(28, y + 62), HOUSE_WORDS[hid], 18, DIM)
	if int(view.get("paramount", -1)) == seat:
		_t(Vector2(28, y + 84), "LORD PARAMOUNT", 15, Color("d8b070"))
		_spot(Rect2(24, y + 70, 220, 22), "paramount")
	var st: Array = h.get("standing", [0, 0, 0])
	var un: Array = h.get("unrest", [0, 0, 0])
	# Proportional to the table.  At the design width these land on the
	# old 400 and 138; narrower, they close up instead of the stats
	# climbing over the GOLD bar.
	var rx0: float = right * 0.276
	var rdx: float = right * 0.0952
	for i in range(3):
		var x := rx0 + i * rdx
		# His design: the label in the resource's own colour and the
		# number large beneath it.  A thin progress bar next to a small
		# numeral was two ways of saying one thing, and the bar was the
		# one nobody could read.
		_fit("res label", RES_NAME[i], 16, rdx - 12.0, true)
		_tb(Vector2(x, y + 22), RES_NAME[i], 16, RES_COL[i].lightened(0.45))
		var vr := Rect2(x, y + 30, minf(104.0, rdx - 24.0), 40)
		_surface(vr, tex_bar, Color(0.9, 0.86, 0.8), Color("15171a"))
		draw_rect(vr, Color(RES_COL[i].r, RES_COL[i].g, RES_COL[i].b, 0.35),
			false, 5)
		draw_rect(vr, RES_COL[i].lightened(0.2), false, 2)
		_tb(Vector2(vr.position.x + vr.size.x * 0.5 - 9.0, vr.position.y + 31),
			str(int(st[i])), 30, RES_COL[i].lightened(0.55))
		_spot(Rect2(x, y + 6, rdx - 16.0, 62), ["military", "capital", "gold"][i])
		if mine:
			var lv: Array = view.get("levy", [0, 0, 0])
			# Below the plaque, not through it.  The plaque ends at y+70
			# and this was drawn at y+72, straight across its bottom rail.
			_t(Vector2(x + 2, y + 88), "levy %d" % int(lv[i]), 15,
				Color("c3b08a"))
			_spot(Rect2(x, y + 74, 110, 20), "levy")
	# Unrest as three small bars rather than "M0 C0 G0": this face sets
	# old-style figures, so a zero sits low and "M0" reads as "Mo".  The
	# bars are also the thing the numbers were standing in for.
	# Unrest beside the resources rather than under them, so a House is
	# one line: its name, what it has, what is going wrong with it, and
	# what it is owed.  It also takes about thirty units off the height
	# of every bar, which the rest of the layout wants back.
	# Out to the right of the counters, not between them and the
	# resources.  It is the smallest thing on the bar -- three short
	# bars and no numeral -- so putting it in the middle broke the run
	# of big numbers for something that reads as a gap.  Small thing,
	# far end.
	# Far right, but never closer to BROKEN than the counters' own
	# spacing -- at large type they were touching by luck rather than
	# by arithmetic.
	var sdx0: float = maxf(right * 0.0779, 124.0 * fk)
	# ...and never off the right edge either.  Pushing it clear of
	# BROKEN without a ceiling just moved the collision to the panel.
	var unx: float = clampf(
		maxf(right - 168.0,
			rx0 + rdx * 3.0 + 34.0 + sdx0 * 2.0 + 116.0 * fk),
		rx0 + rdx * 3.0 + 34.0, right - 136.0)
	_t(Vector2(unx, y + 22), "UNREST", 14, DIM)
	_spot(Rect2(unx - 4, y + 16, 146, 44), "unrest")
	for i in range(3):
		var ux := unx + i * 42.0
		var uy := y + 38.0
		_box(Rect2(ux, uy, 34, 16), Color(0.08, 0.07, 0.06, 0.9))
		if int(un[i]) > 0:
			_box(Rect2(ux, uy, 34.0 * min(int(un[i]), 5) / 5.0, 16),
				RES_COL[i].lightened(0.30))
		draw_rect(Rect2(ux, uy, 34, 16),
			Color(RES_COL[i].r, RES_COL[i].g, RES_COL[i].b, 0.8), false, 1)
	var stats := [["GRIEVANCE", h.get("grievance", 0)], ["WORD KEPT", h.get("kept", 0)],
		["BROKEN", h.get("broken", 0)]]
	# Beside the resources when there is width for them, and on one
	# compact line under the House name when there is not: three ten
	# letter headings do not fit in 70 units and printed over each other
	# as "GRIEVANC WORD KEE BROKEN".
	var narrow: bool = right < 1150.0
	if narrow:
		for i in range(stats.size()):
			var sx: float = 28.0 + i * 96.0
			var lbl: String = ["grievance", "kept", "broken"][i]
			_t(Vector2(sx, y + 86), lbl, 13, DIM)
			_tb(Vector2(sx + 68, y + 87), str(int(stats[i][1])), 19,
				WARN if i == 2 and int(stats[i][1]) > 0 else PALE)
			_spot(Rect2(sx - 4, y + 72, 92, 22), lbl)
	for i in range(stats.size()):
		if narrow:
			break
		# Spacing follows the type: at 150% the three headings grew into
		# each other because the gap between them did not.
		var sdx: float = maxf(right * 0.0779, 124.0 * fk)
		var x := rx0 + rdx * 3.0 + 34.0 + i * sdx
		_spot(Rect2(x - 4, y + 12, right * 0.0779 - 8.0, 62),
			["grievance", "kept", "broken"][i])
		_fit("counter", str(stats[i][0]), 15, sdx - 10.0, true)
		_tb(Vector2(x, y + 24), str(stats[i][0]), 15, Color("a99a7c"))
		_tb(Vector2(x, y + 62), str(int(stats[i][1])), 32,
			WARN if stats[i][0] == "BROKEN" and int(stats[i][1]) > 0 else PALE)
	if h.get("throneworthy", 0):
		_t(Vector2(28, y + 84), "THRONEWORTHY", 16, Color("d8b070"))
		_spot(Rect2(24, y + 70, 220, 22), "ambition")

func _lords(seat: int, y: float, label: String) -> void:
	_tb(Vector2(24, y - 8), label, 16, Color("a8956f"))
	_spot(Rect2(20, y - 22, 220, 26), "lords")
	_rule(24, y - 4, 200, 0.45)
	if stack:
		_lords_chips(seat, y)
		return
	var i := 0
	for l in view.get("lords", []):
		if int(l.get("holder", -1)) != seat:
			continue
		var x := 24.0 + i * (lord_w + 12)
		plate_label = TRAITS[clamp(int(l.get("trait", 0)), 0, 4)]
		_card(int(l.card), Vector2(x, y), lord_w)
		var yy := y + lord_h + 4.0
		var sv := int(l.get("servitude", -1))
		var rv := int(l.get("revolution", -1))
		_box(Rect2(x, yy, lord_w, 18), Color("2a241b"))
		if sv >= 0:
			_box(Rect2(x, yy, lord_w * sv / 5.0, 18), Color("6a5a3a"))
		_t(Vector2(x + 4, yy + 14), "SERVITUDE %s" % ("?" if sv < 0 else str(sv)), 12, PALE)
		_spot(Rect2(x, yy, lord_w, 18), "servitude")
		_box(Rect2(x, yy + 21, lord_w, 18), Color("2a241b"))
		if rv >= 0:
			_box(Rect2(x, yy + 21, lord_w * rv / 5.0, 18), Color("7d3b4a"))
		# A question mark is the honest answer, and the only one the View
		# allows: a Revolution count in a lord you do not hold is not
		# absent from the screen because it is hidden, it is absent
		# because it was never sent.
		_t(Vector2(x + 4, yy + 35), "REVOLUTION %s" % ("?" if rv < 0 else str(rv)),
			12, PALE if rv >= 0 else Color("7a6a52"))
		_spot(Rect2(x, yy + 21, lord_w, 18), "revolution")
		if not l.get("revealed", 0):
			_t(Vector2(x + 4, y + 6), "unrevealed", 12, Color("9b8a6a"))
		i += 1
	if i == 0:
		_t(Vector2(24, y + 30), "none sworn", 19, Color("6a5c46"))

# The lords as a strip rather than as a gallery.
#
# A lord shown as a full card costs 180 units of height for a portrait
# nobody reads twice; what is actually watched is the name and the two
# hidden counts.  A chip is 58 tall and carries all three, so two bands
# of lords cost 116 instead of 400 -- which is where the room for large
# type comes from.  This is the rearrangement; shrinking the cards was
# not.
#
# The portrait survives as a thumbnail, and hovering a chip still opens
# the full card at reading size, so nothing is actually lost.
const CHIP_W := 232.0
const CHIP_H := 58.0

func _lords_chips(seat: int, y: float) -> void:
	var i := 0
	for l in view.get("lords", []):
		if int(l.get("holder", -1)) != seat:
			continue
		var per: int = maxi(1, int((right - 48.0) / (CHIP_W + 10.0)))
		var x := 24.0 + float(i % per) * (CHIP_W + 10.0)
		var cy := y + float(i / per) * (CHIP_H + 8.0)
		var idx := int(l.card)
		var r := Rect2(x, cy, CHIP_W, CHIP_H)
		var hot: bool = idx == hover_card
		_surface(r, tex_bar, Color(1.2, 1.14, 1.0) if hot else Color(1, 1, 1),
			Color("241f18"))
		draw_rect(r, Color("c9a86a") if hot else Color(0.40, 0.34, 0.24), false, 2)
		# The portrait, small but present, and registered so hovering it
		# opens the card at a size that can be read.
		var tw := CHIP_H - 10.0
		var th := CHIP_H - 10.0
		var tex := _card_tex(idx)
		if tex:
			draw_texture_rect_region(tex, Rect2(x + 5, cy + 5, tw, th),
				Rect2(40, 30, 432, 432))
		else:
			_box(Rect2(x + 5, cy + 5, tw, th), Color("2c261d"), Color("4a3f2e"))
		card_rects.append({"rect": r, "idx": idx})
		var nx := x + tw + 14.0
		_fit("chip name", _card_name(idx), 16, CHIP_W - tw - 34.0, true)
		_tb(Vector2(nx, cy + 20), _card_name(idx), 16, PALE)
		if not l.get("revealed", 0):
			_t(Vector2(x + CHIP_W - 82.0, cy + 20), "unrevealed", 12,
				Color("9b8a6a"))
		# Two gauges side by side: bound to you, and turning against you.
		var sv := int(l.get("servitude", -1))
		var rv := int(l.get("revolution", -1))
		var gw := (CHIP_W - tw - 34.0) * 0.5
		_gauge(nx, cy + 34.0, gw, sv, Color("6a5a3a"), "S", "servitude")
		_gauge(nx + gw + 10.0, cy + 34.0, gw, rv, Color("7d3b4a"), "R",
			"revolution")
		i += 1
	if i == 0:
		_t(Vector2(24, y + 22), "none sworn", 19, Color("6a5c46"))

func _gauge(x: float, y: float, w: float, v: int, col: Color, tag: String,
		key: String) -> void:
	_t(Vector2(x, y + 13), tag, 12, Color("9b8a6a"))
	var bx := x + 14.0
	var bw := w - 14.0
	_box(Rect2(bx, y + 3, bw, 12), Color(0.10, 0.08, 0.06, 0.9))
	if v >= 0:
		if v > 0:
			_box(Rect2(bx, y + 3, bw * minf(v, 5) / 5.0, 12), col)
		_tb(Vector2(bx + bw + 6.0, y + 14), str(v), 14, PALE)
	else:
		# Never told, rather than hidden by the client.
		_t(Vector2(bx + bw + 6.0, y + 14), "?", 14, Color("7a6a52"))
	draw_rect(Rect2(bx, y + 3, bw, 12), Color(0.55, 0.46, 0.30, 0.5), false, 1)
	_spot(Rect2(x, y, w + 18.0, 18), key)

func _middle() -> void:
	_rule(0, y_table - 10, right, 0.7)
	_tb(Vector2(24, y_table + 8), "UNBOUND", 16, Color("a8956f"))
	_spot(Rect2(20, y_table - 4, 200, 26), "unbound")
	_rule(24, y_table + 12, 200, 0.45)
	var i := 0
	for l in view.get("lords", []):
		if int(l.get("holder", -1)) != -1:
			continue
		plate_label = TRAITS[clamp(int(l.get("trait", 0)), 0, 4)]
		_card(int(l.card), Vector2(24 + i * (lord_w + 12), y_table + 24), lord_w)
		# Clipped to its own card: two long lord names side by side ran
		# together into one unreadable string.
		# Wrapped to two lines, not clipped to one.  "The Mortgaged
		# Marches" is 175 units of text in a 96 unit box, so a single
		# clipped line cut it mid-word -- which is what he saw breaking.
		# Baseline below the card, measured from the type rather than a
		# fixed 32 -- at larger type the first line rode up under the
		# card's bottom edge.
		draw_multiline_string(_f(),
			Vector2(24 + i * (lord_w + 12),
				y_table + 24.0 + lord_h + float(_fs(12)) + 5.0),
			_card_name(int(l.card)), HORIZONTAL_ALIGNMENT_LEFT, lord_w,
			_fs(12), 2, Color("8a7a5c"))
		i += 1
	_decks()
	_tb(Vector2(PROM_X, y_table + 8), "PROMISES IN PLAY", 16, Color("a8956f"))
	_spot(Rect2(PROM_X - 4, y_table - 4, 260, 26), "promise")
	var pw: float = _decks_x() - PROM_X - 24.0
	_rule(PROM_X, y_table + 12, pw, 0.45)
	var n := 0
	for p in view.get("promises", []):
		if int(p.get("state", 0)) != 1:          # P_LIVE
			continue
		if n >= 2:
			break
		var y := y_table + 24 + n * 92
		_surface(Rect2(PROM_X, y, pw, 80), tex_bar, Color(1.15, 1.08, 0.95), Color("221c14"))
		draw_rect(Rect2(PROM_X, y, pw, 80), Color(0.45, 0.38, 0.26), false, 2)
		_spot(Rect2(PROM_X, y, pw, 80), "promise")
		_fit("promise", TERMS[clamp(int(p.term), 0, TERMS.size() - 1)], 22,
			pw - 180.0, true)
		_tb(Vector2(PROM_X + 16, y + 28), TERMS[clamp(int(p.term), 0, TERMS.size() - 1)], 22, PALE)
		var cons := int(p.get("cons_amt", 0))
		_t(Vector2(PROM_X + 16, y + 52), "consideration: %s" % ("none" if cons <= 0 else str(cons)),
			16, Color("c9a86a") if cons > 0 else DIM)
		# due_round is -1 when the condition is an event rather than a
		# round, which the panel printed raw as "due round -1".  Correct
		# data, meaningless on screen.
		var dr: int = int(p.get("due_round", -1))
		var tk: String = "due.%d" % int(p.term)
		var due: String = ("due round %d" % dr) if dr >= 0 			else (String(help[tk].body).to_lower() if help.has(tk) else "due on its condition")
		_t(Vector2(PROM_X + 16, y + 72), due, 15, DIM)
		_t(Vector2(PROM_X + pw - 136.0, y + 28),
			"YOU -> THEM" if int(p.promiser) == int(view.me) else "THEM -> YOU", 15, DIM)
		n += 1
	if n == 0:
		_t(Vector2(PROM_X + 16, y_table + 54), "none", 18, DIM)

# Which cards in hand the authority is actually offering this turn.
#
# Empty while it is not your move: dimming the whole hand because no
# question is open would say "you can play nothing", when what is true is
# "you are not being asked yet".
func _playable() -> Dictionary:
	var out := {}
	for L in legal:
		var cd: int = int((L as Dictionary).get("card", -1))
		if cd >= 0:
			out[cd] = true
	return out

# The decks, as objects rather than as sentences.
#
# Only the ones you may actually draw from this turn, because a deck you
# cannot reach is not a decision -- and drawing is once a turn, so which
# deck is the turn's first real choice and deserves to be looked at
# rather than read.
# Where the deck shields start, and therefore where the promises column
# has to stop.  One number, so the two cannot be laid out over the top of
# each other -- which is exactly what they did at 140%.
func _decks_x() -> float:
	return maxf(PROM_X + 300.0, right - 350.0)

# The shields, wherever they are living.  Returns the height used.
#
# On the table they are a block to the right of the promises; in the
# panel they are a row above the turn list, and they take the place of
# the "draw from" lines rather than sitting beside them -- which is the
# point, because eight text lines is most of a short panel.
func _deck_row(x: float, y: float, w: float, cols: int) -> float:
	var offer := {}
	for a in legal:
		var ad: Dictionary = a
		if int(ad.kind) != 0:
			continue
		var d := int(ad.a)
		if int(ad.get("b", 0)) == 0 or not offer.has(d):
			offer[d] = ad
	if offer.is_empty():
		return 0.0
	var gap := 14.0
	var cw: float = (w - gap * (cols - 1)) / float(cols)
	var bw: float = minf(cw, 72.0)
	var bh: float = bw * 717.0 / 512.0
	var i := 0
	for d in offer.keys():
		var at := Vector2(x + (i % cols) * (cw + gap),
			y + float(i / cols) * (bh + 28.0))
		var r := Rect2(at, Vector2(bw, bh))
		var hot: bool = hover_deck == int(d)
		_shadow(at, Vector2(bw, bh), 1.4 if hot else 1.0)
		# Asked for, not looked up: backs are fetched on first draw now,
		# so backs.has() is false until something has drawn one and this
		# guard would have shown every deck as an empty box.
		var bt := _back_tex(int(d))
		if bt:
			draw_texture_rect(bt, r, false,
				Color(1.25, 1.2, 1.1) if hot else Color.WHITE)
		else:
			_box(r, Color("241f18"), Color("4a3f2e"))
		if hot:
			_glow(r, Color("e8cf92"))
		# Clipped to the shield: "your House" and "War" grew into one
		# another at large type.
		# The label may use the gap beside it: it sits under the shield
		# where nothing else is drawn, and "Ambition" is wider than a
		# 72 unit shield at large type.
		_fit("deck label", _deck_short(int(d)), 14, cw + gap - 2.0)
		draw_string(_f(), Vector2(at.x, at.y + bh + 15.0), _deck_short(int(d)),
			HORIZONTAL_ALIGNMENT_LEFT, cw + gap - 2.0, _fs(14),
			PALE if hot else Color("a8977a"))
		_spot(r, "act.0")
		deck_spots.append({"rect": r, "action": offer[d], "deck": int(d)})
		i += 1
	var rows: int = int((i + cols - 1) / cols)
	return float(rows) * (bh + 28.0)

func _decks() -> void:
	if tight:
		return                       # they are in the panel instead
	deck_spots = []
	var x0 := _decks_x()
	if _deck_row(x0, y_table + 26.0, 330.0, 4) <= 0.0:
		return
	_spot(Rect2(x0 - 4, y_table - 4, 200, 26), "decks")
	_tb(Vector2(x0, y_table + 8), "THE DECKS", 16, Color("a8956f"))
	_rule(x0, y_table + 12, 330, 0.45)

func _hand() -> void:
	var hand: Array = view.get("hand", [])
	var can := _playable()
	var asking_now: bool = not legal.is_empty()
	for i in range(hand.size()):
		var idx := int(hand[i])
		var at := Vector2(20 + i * (hand_w + 6), y_hand)
		# A card you cannot play this turn recedes; one you can is lit at
		# the edge.  Nothing is hidden and nothing moves -- the hand stays
		# in the order you know it in.
		_card(idx, at, hand_w, asking_now and not can.has(idx))
		if asking_now and can.has(idx) and idx != hover_card:
			_glow(Rect2(at, Vector2(hand_w, hand_h)), Color("e8c06a"))

# The five steps of a turn, as a column of icons down the right edge.
#
# One column, not two: a turn belongs to one seat at a time, so a second
# strip would be dark all game.  The colour says whose turn it is --
# brass for yours, the other House's tint for theirs -- which is the
# thing worth knowing a beat before a war lands.
func _step_icon(r: Rect2, n: int, c: Color) -> void:
	var o := r.position
	var w := 3.0
	match n:
		0:  # Levy: Standing becomes coin
			draw_arc(o + Vector2(21, 31), 10, 0, TAU, 22, c, w)
			draw_arc(o + Vector2(32, 21), 10, 0, TAU, 22, c, w)
		1:  # Draw: one card off the deck
			draw_rect(Rect2(o + Vector2(13, 15), Vector2(15, 22)), c, false, w)
			draw_rect(Rect2(o + Vector2(24, 18), Vector2(15, 22)), c, false, w)
		2:  # First Court: the doors open
			draw_line(o + Vector2(15, 39), o + Vector2(15, 26), c, w)
			draw_line(o + Vector2(37, 39), o + Vector2(37, 26), c, w)
			draw_arc(o + Vector2(26, 26), 11, PI, TAU, 18, c, w)
			draw_line(o + Vector2(11, 40), o + Vector2(41, 40), c, w)
		3:  # Declare: war, a challenge, or a Bond shown
			draw_line(o + Vector2(14, 39), o + Vector2(38, 14), c, w)
			draw_line(o + Vector2(38, 39), o + Vector2(14, 14), c, w)
			draw_line(o + Vector2(12, 30), o + Vector2(22, 40), c, w)
			draw_line(o + Vector2(40, 30), o + Vector2(30, 40), c, w)
		4:  # Second Court: the same doors, barred behind you
			draw_line(o + Vector2(15, 39), o + Vector2(15, 26), c, w)
			draw_line(o + Vector2(37, 39), o + Vector2(37, 26), c, w)
			draw_arc(o + Vector2(26, 26), 11, PI, TAU, 18, c, w)
			draw_line(o + Vector2(11, 40), o + Vector2(41, 40), c, w)
			draw_line(o + Vector2(15, 31), o + Vector2(37, 31), c, w)
			draw_line(o + Vector2(15, 23), o + Vector2(37, 23), c, w)

func _steps_row(x: float, y: float) -> float:
	# mock_step is a rendering override only -- the live value is the
	# engine's, straight off the wire.
	var now: int = mock_step if mock_step >= 0 else int(view.get("step", -1))
	if now < 0 or now > STEP_NAME.size():
		return 0.0
	var lit := BRASS_LIT
	var fill := Color("5c4d2c")
	if mock_theirs:
		lit = Color("8fb0c8")
		fill = Color("2b3945")
	var tile := 52.0
	var gap := 5.0
	for i in range(STEP_NAME.size()):
		var r := Rect2(x + i * (tile + gap), y, tile, tile)
		var on: bool = i == now
		var done: bool = i < now
		draw_rect(r, fill if on else Color(0.13, 0.11, 0.08, 0.95), true)
		draw_rect(r, lit if on else (Color(0.42, 0.36, 0.25) if done
			else Color(0.26, 0.22, 0.16)), false, 2 if on else 1)
		_step_icon(r, i, PALE if on else (Color(0.62, 0.54, 0.39) if done
			else Color(0.40, 0.35, 0.26)))
		_spot(r, "step.%d" % i)
	return tile

func _panel() -> void:
	_surface(Rect2(right, 0, PANEL_W, vh), tex_panel, Color(1, 1, 1), PANEL)
	draw_rect(Rect2(right, 0, 2, vh), Color(0.10, 0.08, 0.06), true)
	draw_rect(Rect2(right + 2, 0, 2, vh), Color(0.62, 0.52, 0.34, 0.7), true)
	var x := right + 30
	_spot(Rect2(x - 6, 16, 300, 66), "round")
	_tb(Vector2(x, 44), "ROUND %d" % int(view.get("round", 0)), 28, PALE)
	# Not the round number again: the heading above already says it.
	_t(Vector2(x, 74), status, 17, DIM)

	# The Court, which at two players is the third party the whole game
	# is built on, so it gets the top of the panel.
	_spot(Rect2(x - 6, 116, 420, 44), "court")
	var crown := int(view.get("crown", 8))
	_rule(x, 100, 410, 0.6)
	_tb(Vector2(x, 138), "THE COURT", 24, Color("d8c49a"))
	_t(Vector2(x, 158), "crowns at +%d" % crown, 16, DIM)
	var houses: Array = view.get("houses", [])
	var me := int(view.me)
	for row in range(2):
		var seat := me if row == 0 else 1 - me
		var y := 190.0 + row * 76
		var fav := int(houses[seat].get("favour", 0))
		_tb(Vector2(x, y + 12), "YOU" if row == 0 else "THEM", 16,
			Color("c98a52") if row == 0 else Color("4aa37a"))
		for i in range(crown + 1):
			var on := i <= fav
			var c := Color("e07b32") if row == 0 else Color("2fbe6a")
			var pr := Rect2(x + i * 40, y + 20, 32, 28)
			_box(pr, c if on else Color(0.12, 0.10, 0.07, 0.9))
			if on:
				draw_rect(Rect2(pr.position, Vector2(pr.size.x, 3)),
					c.lightened(0.35), true)
			# the crowning step is marked, so the end of the road is
			# visible from the start of it
			draw_rect(pr, Color("c9a86a") if i == crown else Color(0.38, 0.32, 0.22),
				false, 2 if i == crown else 1)
		_tb(Vector2(x + 400, y + 44), str(fav), 30,
			Color("e07b32") if row == 0 else Color("2fbe6a"))

	# What you may do, which is the only part of the panel that is a
	# control rather than a report.
	var y := 350.0
	# The steps sit under the heading and push everything after them
	# down.  `y` is carried all the way to the log, so advancing it here
	# is what stops the row and the line below it stacking.
	# One heading, whether or not there is anything to choose: the row
	# of steps belongs under it either way.  It was written in both
	# branches AND again above the action list, so a real prompt --
	# which nothing could photograph until the screenshot bug was
	# fixed -- printed YOUR TURN twice with the tracker between them.
	var my_turn: bool = asking == "choose" and legal.size() > 0
	_tb(Vector2(x, y), "YOUR TURN", 22, Color("e8cf92"))
	y += 30.0
	y += _steps_row(x, y) + 20.0
	if my_turn:
		_spot(Rect2(x - 6, y - 20, 300, 30), "actions")
		if tight:
			# No room on the table, so the decks live here.
			deck_spots = []
			y += _deck_row(x, y + 6.0, PANEL_W - 60.0, 4) + 14.0
		var shown := 0
		# How many fit, rather than a fixed eleven: zoomed in there is
		# less panel and the list ran off the bottom of the screen.
		# The rows close up when there are many, instead of a fixed 50
		# hiding a third of them.  A turn can offer nineteen things and
		# a choice you cannot see is a choice you do not have.
		#
		# 132 is kept back for the note, the log and the two hint lines,
		# which the list used to be drawn straight through.
		# Count what the list will actually carry.  The deck draws are
		# drawn as shields and skipped here, and counting them as list
		# entries made the rows shorter than they needed to be AND made
		# the note below claim six things were hidden when every one of
		# them was on screen as a shield.
		var listed := 0
		for a in legal:
			if not (int(a.kind) == 0 and int(a.get("b", 0)) == 0):
				listed += 1
		var avail: float = vh - y - 132.0
		# The floor is a flat 31 rather than something that grows with
		# the type: a row has to stay tall enough to read, but it does
		# not have to stay proportional, and the alternative to a
		# shorter row is a choice the player cannot see at all.
		#
		# Measured over 112 turns of real play the list carries a median
		# of 8 and never more than 14, and 14 fits at 31 even in a 16:9
		# window at the largest type -- so overflow is not rare now, it
		# is gone, and no scrolling is needed to reach anything.
		var rh: float = clampf(avail / maxf(1.0, float(listed)),
			31.0, 30.0 + 22.0 * fk)
		var cap: int = maxi(3, int(avail / rh))
		for a in legal:
			# The shields ARE the draws, wherever they are drawn -- on the
			# table or up here.  Listing them again as text as well cost
			# eight of the eleven lines the panel has, which pushed real
			# choices off the bottom: nearly half of what a player could
			# do was invisible unless they made the type smaller, which
			# is precisely backwards.
			#
			# The deep draw is not a shield and stays a line, because it
			# is a different price for a different thing.
			if int(a.kind) == 0 and int(a.get("b", 0)) == 0:
				continue
			if shown >= cap:
				break
			var r := Rect2(x, y, 410, rh - 6.0)
			# Lit either by the mouse being on it, or by the mouse being
			# on the card in your hand that it would play.  The list and
			# the hand are the same objects and nothing said so.
			var card_of: int = int(a.get("card", -1))
			var hot: bool = hover == buttons.size() \
				or (card_of >= 0 and card_of == hover_card)
			_surface(r, tex_bar, Color(1.7, 1.6, 1.38) if hot else Color(1.05, 1.0, 0.9),
				Color("2c2419"))
			if hot:
				_glow(r, BRASS_LIT, 3)
			_frame(r, hot)
			# Clipped to the button, so a long label runs out of room
			# rather than out of the panel.
			var asz: int = 17 if rh < 44.0 else 18
			var alab := _clip_to(_action_label(a), asz, 384.0, true)
			_fit("action", alab, asz, 384.0, true)
			var ac: Color = Color("fff6e2") if hot else Color("ecdcb6")
			draw_string(_fb(), Vector2(x + 18, y + rh * 0.5 + 5.0),
				alab, HORIZONTAL_ALIGNMENT_LEFT, 384.0, _fs(asz), ac)
			if card_of >= 0:
				# A small mark, so it is visible before you hover that this
				# entry is a card you are holding rather than a deck or a
				# promise.
				draw_rect(Rect2(r.position.x + r.size.x - 14.0,
					r.position.y + r.size.y * 0.5 - 7.0, 6, 14),
					Color("c9a86a") if hot else Color(0.45, 0.38, 0.26), true)
			_spot(r, "act.%d" % int(a.kind))
			buttons.append({"rect": r, "action": a})
			y += rh
			shown += 1
		# On its own line after the list, not crammed into the six pixel
		# gap under the last button -- where it was drawn over the LOG
		# heading and read as a cut-off sentence.
		var hidden: int = listed - shown
		if hidden > 0:
			_t(Vector2(x + 4, y + 18),
				"+%d more, not shown -- lower the text size with -" % hidden,
				14, Color("b09a72"))
			y += 30
	elif asking != "":
		_tb(Vector2(x, y), "THE AUTHORITY ASKS", 20, Color("e8cf92"))
		y += 30
		var q := asking
		if asking == "break_promise":
			q = "Break your word?"
		elif asking == "accept_promise":
			q = "Accept a promise of %s?" % TERMS[clamp(ask_b, 0, TERMS.size() - 1)]
		elif asking == "commit":
			q = "Commit Military Levy (up to %d)?" % ask_a
		elif asking == "lord_turns":
			# Asked of the House working ON the lord, not the one holding
			# it -- the old wording made it sound like something being
			# reported rather than something being decided.
			q = "Turn their lord against them now?"
		_t(Vector2(x, y + 4), q, 19, INK)
		_spot(Rect2(x - 6, y - 26, 420, 50), "ask." + asking)
		y += 30
		# The question in plain words, under the question.  This is the
		# moment a new player is most lost, and a tooltip they have to
		# discover is no use to somebody who does not know there is
		# anything to discover.
		if help.has("ask." + asking):
			y += _wrapped(Vector2(x, y), 404.0,
				String(help["ask." + asking].body), 17,
				Color("b3a17c")) + 16.0
		var opts := [["no", 0], ["yes", 1]]
		if asking == "lord_turns":
			# The engine turns the lord only on 2 -- ai_side() returns 2
			# or 0.  "yes" sent 1, which reads as no, so a person could
			# never make a lord turn at all while the machine could.
			opts = [["no", 0], ["yes, turn it", 2]]
		if asking == "commit":
			opts = [["none", 0], ["half", int(ask_a / 2)], ["all %d" % ask_a, ask_a]]
		for o in opts:
			var r := Rect2(x, y, 410, 44)
			var hot := hover == buttons.size()
			_surface(r, tex_bar, Color(1.5, 1.42, 1.25) if hot else Color(0.95, 0.92, 0.85),
				Color("2c2419") if hot else Color("241f18"))
			draw_rect(r, Color("c9a86a") if hot else Color(0.38, 0.32, 0.22), false, 2)
			if hot:
				draw_rect(Rect2(r.position, Vector2(4, r.size.y)), Color("c9a86a"), true)
			_t(Vector2(x + 16, y + 29), str(o[0]), 18, PALE if hot else INK)
			buttons.append({"rect": r, "action": o[1]})
			y += 50
	else:
		_t(Vector2(x, y), "waiting", 20, DIM)

	# The log, so a player can see what the other seat did rather than
	# inferring it from numbers that changed.
	var ly := maxf(y + 20.0, 900.0)
	_spot(Rect2(x - 6, ly - 16, 300, 30), "log")
	_t(Vector2(x, ly), "LOG", 15, DIM)
	for i in range(log_lines.size()):
		_t(Vector2(x, ly + 28 + i * 26), log_lines[i], 18, Color("a8977a"))
