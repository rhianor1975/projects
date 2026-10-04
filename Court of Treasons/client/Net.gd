# The wire, from the client side.
#
# Line-delimited JSON over TCP: the authority sends a message per line,
# and this turns each line into a Dictionary and emits it.  Nothing here
# interprets the game -- that is Main.gd's job -- and nothing here keeps
# state, because the client has none to keep.
extends RefCounted

signal message(m: Dictionary)
signal closed(reason: String)

var _peer := StreamPeerTCP.new()
var _buf := ""
var _live := false

# ms is how long this will block the main thread waiting for the
# handshake.  It blocks on purpose -- the client has nothing to draw
# until it is connected -- but the caller has to know the cost, because
# a caller that retries turns two seconds into two minutes.  One did.
func connect_to(host: String, port: int, ms := 2000) -> String:
	# A StreamPeerTCP that has already been given a host refuses a second
	# one with ERR_ALREADY_IN_USE, and stays refusing it.  The retry loop
	# in _play_here therefore failed every attempt after the first and
	# only ever connected by accident -- four of those errors on every
	# single launch.  Reset it before asking again.
	if _peer.get_status() != StreamPeerTCP.STATUS_NONE:
		_peer.disconnect_from_host()
		_peer.poll()
	_live = false
	var err := _peer.connect_to_host(host, port)
	if err != OK:
		return "cannot reach %s:%d" % [host, port]
	# connect_to_host returns before the handshake finishes, so the
	# status has to be polled rather than trusted.
	for _i in range(maxi(1, ms / 10)):
		_peer.poll()
		var st := _peer.get_status()
		if st == StreamPeerTCP.STATUS_CONNECTED:
			_peer.set_no_delay(true)
			_live = true
			return ""
		if st == StreamPeerTCP.STATUS_ERROR:
			break
		OS.delay_msec(10)
	return "no answer from %s:%d" % [host, port]

func poll() -> void:
	if not _live:
		return
	_peer.poll()
	if _peer.get_status() != StreamPeerTCP.STATUS_CONNECTED:
		_live = false
		closed.emit("the authority has gone")
		return
	var n := _peer.get_available_bytes()
	if n > 0:
		# A socket splits and joins messages as it likes, so the buffer
		# is kept until a newline arrives rather than assuming one read
		# is one message.  That assumption survives a loopback test and
		# fails on a real link, which is the one that matters.
		_buf += _peer.get_utf8_string(n)
	while true:
		var nl := _buf.find("\n")
		if nl < 0:
			break
		var line := _buf.substr(0, nl)
		_buf = _buf.substr(nl + 1)
		if line.strip_edges() == "":
			continue
		var j = JSON.parse_string(line)
		if j is Dictionary:
			message.emit(j)

func send(d: Dictionary) -> void:
	if _live:
		_peer.put_data((JSON.stringify(d) + "\n").to_utf8_buffer())

func close() -> void:
	_live = false
	_peer.disconnect_from_host()
