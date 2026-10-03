#!/usr/bin/env python3
"""Read a --record tape and answer the questions a bug report needs.

    tools/tape.py FILE                 what is in it
    tools/tape.py FILE jumps           moves of more than one tile in a turn
    tools/tape.py FILE islands         'seen' cells with nothing seen around them
    tools/tape.py FILE tick N          one tick, drawn as the player saw it
    tools/tape.py FILE grep TEXT       ticks whose log mentions TEXT

The tape is JSON Lines, so a truncated file (the game was killed, which is
usually why there is a tape at all) still reads up to the last whole line.
"""
import json, sys, collections

def load(path):
    for line in open(path):
        try: yield json.loads(line)
        except Exception: pass          # a partial last line is expected

def summary(path):
    c = collections.Counter(); floors = []; keys = collections.Counter()
    first = last = None
    for r in load(path):
        c[r.get("t")] += 1
        if r.get("t") == "floor": floors.append(r["floor"])
        if r.get("t") == "tick":
            keys[r["key"]] += 1
            if first is None: first = r["tick"]
            last = r["tick"]
    print("records:", dict(c))
    print("floors visited:", floors)
    print("ticks:", first, "->", last)
    print("most-pressed keys:", [(k, chr(k) if 32 <= k < 127 else '?', n)
                                 for k, n in keys.most_common(6)])

def jumps(path, ignore_autoexplore=True):
    prev = {}; pf = None; out = []
    for r in load(path):
        if r.get("t") != "tick": continue
        if ignore_autoexplore and r["key"] == ord('x'):
            for h in r["party"]: prev[h["s"]] = (h["x"], h["y"])
            pf = r["floor"]; continue
        for h in r["party"]:
            if h["s"] and not h["use"]: continue
            if not h["alive"]: continue
            k = h["s"]
            if k in prev and pf == r["floor"]:
                px, py = prev[k]
                d = max(abs(h["x"] - px), abs(h["y"] - py))
                if d > 1:
                    out.append((r["tick"], k, h["name"], (px, py), (h["x"], h["y"]), d,
                                r["key"], chr(r["key"]) if 32 <= r["key"] < 127 else '?'))
            prev[k] = (h["x"], h["y"])
        pf = r["floor"]
    print("moves over one tile in a single turn (auto-explore %s):" %
          ("excluded" if ignore_autoexplore else "included"), len(out))
    for j in out[:40]:
        print("  tick %d  slot %d  %s  %s -> %s  dist %d  key %d '%s'" % j)

def islands(path):
    worst = []
    for r in load(path):
        if r.get("t") != "tick": continue
        v = r["view"]; seen = v["seen"]
        H = len(seen); W = len(seen[0]) if H else 0
        found = []
        for y in range(H):
            for x in range(W):
                if seen[y][x] != '1': continue
                if any(seen[y+dy][x+dx] == '1'
                       for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                       if (dx or dy) and 0 <= y+dy < H and 0 <= x+dx < W):
                    continue
                found.append((v["x"]+x, v["y"]+y, v["tile"][y][x]))
        if found: worst.append((len(found), r["tick"], r["floor"], found[:10]))
    worst.sort(reverse=True)
    print("ticks with remembered cells that have nothing remembered around them:", len(worst))
    for n, t, fl, where in worst[:10]:
        print("  tick %d floor %d: %d of them, e.g. %s" % (t, fl, n, where))

def show(path, want):
    for r in load(path):
        if r.get("t") != "tick" or r["tick"] != want: continue
        v = r["view"]
        print("tick %d  turn %d  floor %d  key %d  driven slot %d"
              % (r["tick"], r["turn"], r["floor"], r["key"], r["driven"]))
        for h in r["party"]:
            if h["s"] and not h["use"]: continue
            print("  slot %d %-22s (%3d,%3d) hp %d/%d %s"
                  % (h["s"], h["name"], h["x"], h["y"], h["hp"], h["max"],
                     "" if h["alive"] else "DOWN"))
        print("  view origin (%d,%d) %dx%d" % (v["x"], v["y"], v["w"], v["h"]))
        for y in range(len(v["tile"])):
            row = "".join(v["tile"][y][x] if v["seen"][y][x] == '1' else ' '
                          for x in range(len(v["tile"][y])))
            print("   |" + row + "|")
        if r["log"]:
            print("  log:")
            for l in r["log"]: print("    " + l)
        return
    print("no such tick")

def grep(path, text):
    for r in load(path):
        if r.get("t") != "tick": continue
        for l in r["log"]:
            if text.lower() in l.lower():
                print("tick %d floor %d: %s" % (r["tick"], r["floor"], l))

if __name__ == "__main__":
    if len(sys.argv) < 2: print(__doc__); sys.exit(2)
    f = sys.argv[1]
    cmd = sys.argv[2] if len(sys.argv) > 2 else "summary"
    if   cmd == "summary": summary(f)
    elif cmd == "jumps":   jumps(f, "-all" not in sys.argv)
    elif cmd == "islands": islands(f)
    elif cmd == "tick":    show(f, int(sys.argv[3]))
    elif cmd == "grep":    grep(f, sys.argv[3])
    else: print(__doc__)
