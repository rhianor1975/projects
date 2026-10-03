import csv, sys, collections
path = sys.argv[1]
rows = list(csv.DictReader(open(path)))
by = collections.defaultdict(list)
for r in rows:
    by[r['who']].append((int(r['tick']), int(r['x']), int(r['y']), int(r['hp']), int(r['alive'])))

print(f"  {'actor':8} {'ticks':>6} {'moved':>6} {'idle%':>6} {'2-cycle':>8} {'distinct':>9} {'longest stall':>14} {'net drift':>10}")
for who, seq in by.items():
    seq.sort()
    moves = 0; osc = 0; stall = 0; worst = 0
    seen = set()
    prev = prev2 = None
    for (t,x,y,hp,alive) in seq:
        seen.add((x,y))
        if prev is not None:
            if (x,y) == prev:
                stall += 1
                worst = max(worst, stall)
            else:
                moves += 1; stall = 0
                if prev2 is not None and (x,y) == prev2:
                    osc += 1
        prev2 = prev; prev = (x,y)
    n = len(seq)
    x0,y0 = seq[0][1], seq[0][2]
    x1,y1 = seq[-1][1], seq[-1][2]
    drift = ((x1-x0)**2 + (y1-y0)**2) ** .5
    idle = 100*(n-moves)//n
    print(f"  {who:8} {n:6} {moves:6} {idle:5}% {osc:8} {len(seen):9} {worst:14} {drift:10.0f}")
