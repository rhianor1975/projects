#!/usr/bin/env python3
"""Four games at once against one server.

    ./court --serve 0 --duel --houses 2 --port 9412 --central &
    python3 tools/concurrent-test.py

A server that plays one game at a time looks identical to a server that
plays four, right up until the second player connects.  This is the test
that tells them apart: four clients at once, four different games, four
different outcomes.

Each game must also be its own.  A server reusing a seed would deal the
same cards to everyone who ever connected, and the tell is four
identical transcripts -- so the action counts are printed, and four
games that agree on them are a bug rather than a coincidence.
"""
import json, socket, sys, threading, time
def play(n, out):
    try:
        s = socket.create_connection(('127.0.0.1', 9412), timeout=10)
        f = s.makefile('rw', buffering=1, newline='\n')
        t0 = time.time(); acts = 0
        while True:
            line = f.readline()
            if not line: break
            m = json.loads(line)
            if m.get('over'):
                out[n] = 'game %d: seat %d by %s in %.1fs, %d actions' % (
                    n, m['winner'], m['reason'], time.time()-t0, acts); break
            f.readline()
            if m.get('ask') == 'choose':
                legal = json.loads(f.readline())['legal']
                f.write(json.dumps(max(legal, key=lambda a:(a['kind']!=13, a['kind']==1)))+'\n')
                acts += 1
            else:
                f.write('{"answer":0}\n')
    except Exception as ex:
        out[n] = 'game %d failed: %s' % (n, ex)
out = {}
done = {}
ts = [threading.Thread(target=play, args=(i, out)) for i in range(4)]
t0 = time.time()
for t in ts: t.start()
for t in ts: t.join()
wall = time.time() - t0
for i in sorted(out): print('  ' + out[i])
print('  four at once in %.1fs total' % wall)

# A game that never connected is not a pass.  This printed
# "Connection refused" four times, then "four at once in 0.0s", and
# exited 0 -- because four failure strings are still four results and
# no two of them are equal.  Nothing was being tested and the test
# said so in green.
fails = [o for o in out.values() if 'failed' in o]
if fails or len(out) < 4:
    print('  %d of 4 games did not play -- is the server up on 9412?'
          % (len(fails) + 4 - len(out)))
    sys.exit(1)

acts = [o.rsplit(', ', 1)[-1] for o in out.values()]
if len(set(acts)) == 1:
    print('  all four games identical -- the seed is being reused'); sys.exit(1)

# And the thing this test is named for.  Four games that finish in a
# neat staircase, each about one game-length after the last, are four
# games played one at a time however concurrent the server claims to
# be.  Compared against the slowest single game, not against a clock:
# a machine under load is slow, a serialised server is CUMULATIVE.
secs = sorted(float(o.split(' in ')[1].split('s,')[0]) for o in out.values())
if secs[-1] > secs[0] * 2.0 and secs[-1] > 1.0 and wall > secs[0] * 3.0:
    print('  finishing times %s' % [round(x, 2) for x in secs])
    print('  that is a staircase, not four at once: the server is '
          'playing them one at a time')
    sys.exit(1)
sys.exit(0)
