#!/usr/bin/env python3
"""No two choices offered at once may look the same.

    python3 tools/label-test.py [games]

Five separate bugs in this client were one bug: the engine varies a
field, the label ignores it, and the panel offers the same words twice
for two different things.  "stir unrest" three times over.  "draw from
Political" twice, one of them at triple the price.  "reveal a Bond" once
per lord.  Kinds 9 to 13 all off by one.

Each was found by a person reading the screen and asking.  This finds
the rest without one.

It plays real games over the wire, takes every set of legal actions the
authority offers, and checks that no two DIFFERENT actions in the same
set would render to the same text.  What the client reads per kind is
declared below and has to match _action_label in Main.gd -- that table
is the thing a reviewer should check, and it is eight lines rather than
a hundred.
"""
import json, socket, subprocess, sys, time

# Per kind: the Action fields the client's label actually consults.
# If the engine ever varies a field that is not listed here, two
# different actions collapse to one line and this test fails.
USES = {
    0:  ('a', 'b'),          # draw: which deck, and deep or not
    1:  ('card',),           # play
    2:  ('card',),           # bind with which lever
    3:  ('a', 'target'),     # instigate: kind, and which lord
    4:  ('a', 'card'),       # promise: term, and which card carries it
    5:  (),                  # respond
    6:  ('a',),              # declare war: the Grievance it costs
    7:  (),                  # challenge
    8:  ('target',),         # reveal a Bond on which lord
    9:  (),                  # revolt
    10: ('a', 'card'),       # spend grievance: which sink
    11: (),                  # buy favour
    12: ('card',),           # discard
    13: (),                  # pass
}

PORT = 9421
games = int(sys.argv[1]) if len(sys.argv) > 1 else 6
bad, sets, acts = [], 0, 0

for g in range(games):
    srv = subprocess.Popen(['./court', '--serve', '0', '--solo', '--houses', '2',
                            '--port', str(PORT + g)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           env={'COURT_SEED': str(17 + g * 101),
                                'PATH': '/usr/bin:/bin'})
    s = None
    for _ in range(80):
        try:
            s = socket.create_connection(('127.0.0.1', PORT + g), timeout=10)
            break
        except OSError:
            time.sleep(0.1)
    if not s:
        print('  could not reach the authority'); sys.exit(1)
    f = s.makefile('rw', buffering=1, newline='\n')
    while True:
        line = f.readline()
        if not line:
            break
        m = json.loads(line)
        if m.get('over'):
            break
        if not m.get('ask'):
            continue
        f.readline()                      # the View
        if m['ask'] != 'choose':
            f.write('{"answer":0}\n'); f.flush(); continue
        legal = json.loads(f.readline())['legal']
        sets += 1
        acts += len(legal)
        seen = {}
        for a in legal:
            k = int(a['kind'])
            if k not in USES:
                bad.append('kind %d is offered and has no entry in USES' % k)
                continue
            key = (k,) + tuple(a.get(x) for x in USES[k])
            full = tuple(sorted(a.items()))
            if key in seen and seen[key] != full:
                bad.append('kind %d: two different actions read alike\n'
                           '      %s\n      %s'
                           % (k, dict(seen[key]), dict(full)))
            seen[key] = full
        f.write(json.dumps(max(legal, key=lambda a: a['kind'] == 1)) + '\n')
        f.flush()
    srv.terminate()

print('  %d games, %d choices offered, %d actions' % (games, sets, acts))
if bad:
    uniq = sorted(set(bad))
    for b in uniq[:6]:
        print('  COLLISION  ' + b)
    print('  %d distinct collisions' % len(uniq))
    sys.exit(1)
print('  no two actions offered together read the same: all clear')
