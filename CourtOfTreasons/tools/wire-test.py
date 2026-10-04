#!/usr/bin/env python3
"""A client, to prove the wire works and that N1 holds on it.

    python3 tools/wire-test.py [seeds]

Forty lines, in a language the engine is not written in, which is the
point: if a client this small can play a game through the protocol then
the protocol is a protocol and not an internal detail with quotes round
it.

What it actually checks is requirement N1 -- "no client ever receives
hidden state it is not entitled to see" -- from outside.  Every View the
authority sends is inspected for a Revolution count on a lord this seat
does not hold.  court_view is written so there is nothing to leak; this
is the test that says so from the far side of a pipe, which is the only
side that counts.
"""
import json, subprocess, sys
SEEDS = [int(x) for x in sys.argv[1:]] or [7, 11, 23, 41, 97]
total_v = total_a = total_leak = total_told = 0
for seed in SEEDS:
 p = subprocess.Popen(['./court', '--serve', '0', '--houses', '2'],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True,
                     bufsize=1, env={'COURT_SEED': str(seed), 'PATH': '/usr/bin:/bin'})
 turns = leaks = views = told = 0
 while True:
     line = p.stdout.readline()
     if not line: break
     m = json.loads(line)
     # A seat is now told what the other one did, unprompted, so the
     # wire is message-driven and not a fixed ask/view/legal sequence.
     # Anything that reads it in lockstep has to skip what it did not
     # ask for -- this loop read a 'did' where it expected a View.
     if 'did' in m:
         told += 1
         # N1 again: what he is told must be public.  A Bond advanced or
         # an Instigator sent are neither, and must never appear here.
         if int(m['did']['kind']) in (2, 3):
             leaks += 1
         continue
     if m.get('over'):
         print('  game over: winner seat %d by %s' % (m['winner'], m['reason'])); break
     ask = m.get('ask')
     view = json.loads(p.stdout.readline()); views += 1
     # N1: nothing in a View may be hidden state this seat is not entitled to.
     me = view['me']
     for l in view['lords']:
         if l['holder'] != me and l['revolution'] != -1: leaks += 1
     if 'their_hand' in view or len(view.get('hand', [])) > 14: leaks += 1
     if ask == 'choose':
         legal = json.loads(p.stdout.readline())['legal']
         pick = max(legal, key=lambda a: (a['kind'] != 13, a['kind'] == 1))
         p.stdin.write(json.dumps(pick) + '\n'); turns += 1
     else:
         p.stdin.write('{"answer":%d}\n' % (1 if ask == 'accept_promise' else 0))
 total_v += views; total_a += turns; total_leak += leaks; total_told += told
 p.stdin.close(); p.wait()

print('  %d games over the wire: %d views, %d actions, %d told, %d leaks'
      % (len(SEEDS), total_v, total_a, total_told, total_leak))
sys.exit(1 if total_leak else 0)
