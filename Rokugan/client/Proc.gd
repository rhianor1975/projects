# The engine, as a child process speaking one line each way.
#
# Strictly request and reply: the engine writes one View when it starts
# and exactly one after every line it reads, so a read never waits for a
# message that is not coming.  The machine opponent plays inside the
# engine before it replies; a reply is a few milliseconds, which is why
# the read blocks rather than polls.
extends RefCounted

var _io: FileAccess
var _err: FileAccess
var _pid := -1

func start(bin: String, args: PackedStringArray) -> String:
	if not FileAccess.file_exists(bin):
		return "No engine at %s -- run make in the Rokugan folder." % bin
	var r := OS.execute_with_pipe(bin, args)
	if r.is_empty():
		return "Could not start %s." % bin
	_io = r["stdio"]
	_err = r["stderr"]
	_pid = r["pid"]
	return ""

func running() -> bool:
	return _pid > 0 and OS.is_process_running(_pid)

func read_view() -> Dictionary:
	if _io == null:
		return {"error": "The engine is not running."}
	var line := _io.get_line()
	if line.strip_edges() == "":
		var why := _err.get_as_text() if _err else ""
		return {"error": "The engine stopped. " + why}
	var j = JSON.parse_string(line)
	if j is Dictionary:
		return j
	return {"error": "The engine said something unreadable."}

func send(cmd: String) -> Dictionary:
	if not running():
		return {"error": "The engine is not running."}
	_io.store_string(cmd + "\n")
	_io.flush()
	return read_view()

func stop() -> void:
	if running():
		_io.store_string("quit\n")
		_io.flush()
		OS.kill(_pid)
	_pid = -1
