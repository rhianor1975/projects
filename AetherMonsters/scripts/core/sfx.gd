extends Node
## Chiptune sound effects, synthesised at start-up -- square and triangle
## voices with pitch sweeps and a little noise, the SNES-era palette. No
## audio files to ship or license. Autoloaded as Sfx.

const RATE := 22050
var _streams := {}
var _players: Array = []
var enabled := true


func _ready() -> void:
	for i in 8:
		var p := AudioStreamPlayer.new()
		p.volume_db = -10
		add_child(p)
		_players.append(p)
	_streams["cursor"] = _tone([[1320, 1320, 0.035]], "square", 0.25)
	_streams["confirm"] = _tone([[880, 880, 0.05], [1320, 1320, 0.07]], "square", 0.28)
	_streams["back"] = _tone([[660, 440, 0.07]], "square", 0.25)
	_streams["buzz"] = _tone([[140, 120, 0.14]], "square", 0.3)
	_streams["step"] = _noise(0.03, 0.12, 4000)
	_streams["hit"] = _mix(_noise(0.09, 0.5, 2200), _tone([[220, 90, 0.09]], "square", 0.35))
	_streams["crit"] = _mix(_noise(0.14, 0.6, 3200), _tone([[440, 110, 0.14]], "square", 0.4))
	_streams["hurt"] = _tone([[300, 120, 0.12]], "square", 0.4)
	_streams["miss"] = _tone([[900, 1400, 0.08]], "triangle", 0.3)
	_streams["spell"] = _tone([[520, 1560, 0.18]], "triangle", 0.35)
	_streams["shot"] = _mix(_noise(0.05, 0.4, 6000), _tone([[1200, 600, 0.06]], "square", 0.2))
	_streams["coin"] = _tone([[1568, 1568, 0.05], [2093, 2093, 0.12]], "square", 0.25)
	_streams["pickup"] = _tone([[784, 784, 0.04], [1175, 1175, 0.06]], "triangle", 0.3)
	_streams["die"] = _mix(_noise(0.25, 0.35, 1200), _tone([[400, 60, 0.25]], "square", 0.3))
	_streams["levelup"] = _tone([[523, 523, 0.08], [659, 659, 0.08], [784, 784, 0.08], [1047, 1047, 0.22]], "square", 0.3)
	_streams["stairs"] = _tone([[660, 660, 0.06], [494, 494, 0.06], [392, 392, 0.06], [330, 330, 0.12]], "triangle", 0.35)
	_streams["heal"] = _tone([[660, 990, 0.1], [990, 1320, 0.12]], "triangle", 0.3)
	_streams["door"] = _noise(0.12, 0.3, 900)
	_streams["buy"] = _tone([[1047, 1047, 0.05], [1319, 1319, 0.05], [1568, 1568, 0.1]], "square", 0.25)


func play(name: String) -> void:
	if not enabled or not _streams.has(name):
		return
	for p in _players:
		if not p.playing:
			p.stream = _streams[name]
			p.play()
			return


func _tone(segs: Array, wave: String, vol: float) -> AudioStreamWAV:
	var data := PackedByteArray()
	var phase := 0.0
	for seg in segs:
		var f0: float = seg[0]
		var f1: float = seg[1]
		var n := int(seg[2] * RATE)
		for i in n:
			var k := float(i) / n
			var f := lerpf(f0, f1, k)
			phase += f / RATE
			var v := 0.0
			if wave == "square":
				v = 1.0 if fmod(phase, 1.0) < 0.5 else -1.0
			else:
				v = 4.0 * absf(fmod(phase, 1.0) - 0.5) - 1.0
			var env := minf(1.0, k * 20.0) * (1.0 - k * 0.7)
			_push(data, v * vol * env)
	return _wav(data)


func _noise(dur: float, vol: float, cutoff: float) -> AudioStreamWAV:
	var data := PackedByteArray()
	var n := int(dur * RATE)
	var last := 0.0
	var a := clampf(cutoff / RATE, 0.02, 1.0)
	for i in n:
		var k := float(i) / n
		last = lerpf(last, randf() * 2.0 - 1.0, a)
		_push(data, last * vol * (1.0 - k))
	return _wav(data)


func _mix(a: AudioStreamWAV, b: AudioStreamWAV) -> AudioStreamWAV:
	var da := a.data
	var db := b.data
	var out := PackedByteArray()
	var n := maxi(da.size(), db.size()) / 2
	for i in n:
		var va := da.decode_s16(i * 2) if i * 2 < da.size() else 0
		var vb := db.decode_s16(i * 2) if i * 2 < db.size() else 0
		_push(out, clampf((va + vb) / 32767.0, -1.0, 1.0))
	return _wav(out)


func _push(data: PackedByteArray, v: float) -> void:
	var s := int(clampf(v, -1.0, 1.0) * 32000.0)
	data.append(s & 0xff)
	data.append((s >> 8) & 0xff)


func _wav(data: PackedByteArray) -> AudioStreamWAV:
	var w := AudioStreamWAV.new()
	w.format = AudioStreamWAV.FORMAT_16_BITS
	w.mix_rate = RATE
	w.stereo = false
	w.data = data
	return w
