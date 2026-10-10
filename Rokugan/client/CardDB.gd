# The card pool the engine loaded, read from the same data/ERA/cards.tsv,
# and the card images from data/ootv/images.  Both are local: the text
# and images are AEG's and are never in the repository.
#
# The art for the rebuilt cards is cropped from each image when it is
# first drawn.  The images are the Oracle's primary printings, which
# come in several frame designs; the art window sits within a few
# percent of the same place on all of them, so one crop serves.
extends RefCounted

const ART := Rect2(0.095, 0.105, 0.81, 0.45)

var root := ""
var cards := {}
var _scan := {}
var _art := {}

func load_era(root_dir: String, era: String) -> String:
	root = root_dir
	cards.clear()
	var path := root + "data/" + era + "/cards.tsv"
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return "No card data at %s.\nRun  make setup  in the Rokugan folder first." % path
	var hdr := f.get_line().split("\t")
	while not f.eof_reached():
		var line := f.get_line()
		if line == "":
			continue
		var cols := line.split("\t")
		var d := {}
		for i in range(mini(hdr.size(), cols.size())):
			d[hdr[i]] = cols[i]
		cards[int(d.get("id", "0"))] = d
	return ""

func card(oid: int) -> Dictionary:
	return cards.get(oid, {})

func text(oid: int) -> String:
	var t: String = card(oid).get("text", "")
	t = t.replace(":bow:", "Bow").replace(" | ", "\n")
	return t

func _image(oid: int) -> Image:
	var p := root + "data/ootv/images/%d.jpg" % oid
	if not FileAccess.file_exists(p):
		return null
	return Image.load_from_file(p)

func scan(oid: int) -> Texture2D:
	if _scan.has(oid):
		return _scan[oid]
	var img := _image(oid)
	var tex: Texture2D = ImageTexture.create_from_image(img) if img else null
	_scan[oid] = tex
	return tex

func art(oid: int) -> Texture2D:
	if _art.has(oid):
		return _art[oid]
	var tex: Texture2D = null
	var img := _image(oid)
	if img:
		var s := img.get_size()
		var r := Rect2i(int(ART.position.x * s.x), int(ART.position.y * s.y),
				int(ART.size.x * s.x), int(ART.size.y * s.y))
		tex = ImageTexture.create_from_image(img.get_region(r))
	_art[oid] = tex
	return tex
