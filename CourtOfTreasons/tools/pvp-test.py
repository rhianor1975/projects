#!/usr/bin/env python3
"""A game between two people, and the talk that crosses while they play.

    python3 tools/pvp-test.py

Three things this proves that the single-seat tests cannot:

  N1 still holds with two seats on the wire.  The authority now sends a
  View to the seat it is NOT asking, so that player can watch the table
  move; that is a new path, and a new path is where a leak gets in.

  The per-seat Remote.  It was one static, which was correct for exactly
  as long as only one seat could be remote.  Two seats sharing one would
  send both players the same question.

  N6.  Talk arrives when the sender chooses, not when they are asked.
  This test only ever speaks from the seat that is NOT being asked --
  which is the case a loop that reads only the channel it is waiting on
  gets wrong, and gets wrong silently: the line is not lost, it is just
  not delivered until that player's turn comes round.
"""
import json, socket, subprocess, sys, threading, time

PORT = 9319
srv = subprocess.Popen(['./court', '--serve', '0', '--duel', '--houses', '2',
                        '--port', str(PORT)],
                       stderr=subprocess.DEVNULL, text=True,
                       env={'COURT_SEED': '77', 'PATH': '/usr/bin:/bin'})

def connect():
    for _ in range(80):
        try:
            return socket.create_connection(('127.0.0.1', PORT), timeout=10)
        except OSError:
            time.sleep(0.1)
    return None

res = {}
def play(name, f, speak, spaced=False):
    views = acts = leaks = said = 0
    heard = []            # chat this seat received
    waiting = False
    me = None
    while True:
        line = f.readline()
        if not line:
            break
        m = json.loads(line)
        if m.get('waiting'):
            waiting = True
            continue
        if m.get('chat'):
            heard.append((m['seat'], m['text']))
            continue
        if m.get('over'):
            res[name] = dict(views=views, acts=acts, leaks=leaks,
                             heard=heard, waiting=waiting, me=me,
                             winner=m['winner'])
            return
        if 'me' in m:
            # A View with no question: not our turn.  This is the only
            # moment this seat speaks, which is the whole point.
            views += 1
            me = m['me']
            for l in m['lords']:
                if l['holder'] != m['me'] and l['revolution'] != -1:
                    leaks += 1
            # Counted on what this seat has SAID.  Guarding on what it
            # had heard made A fall silent before it ever got a turn off,
            # because B had already filled its quota -- and the test
            # then blamed the relay for a fault in itself.
            if speak and said < 3:
                # One seat speaks in spaced JSON and the other compact,
                # so both spellings of the same message stay covered.
                # The relay accepted only the compact one until this
                # test sent the other by accident.
                f.write(json.dumps({"say": "%s speaks out of turn" % name},
                                   separators=None if spaced else (',', ':'))
                        + '\n')
                said += 1
            continue
        if m.get('ask'):
            view = json.loads(f.readline()); views += 1
            me = view['me']
            for l in view['lords']:
                if l['holder'] != view['me'] and l['revolution'] != -1:
                    leaks += 1
            if m['ask'] == 'choose':
                legal = json.loads(f.readline())['legal']
                pick = max(legal, key=lambda a: (a['kind'] != 13, a['kind'] == 1))
                f.write(json.dumps(pick) + '\n'); acts += 1
            else:
                f.write('{"answer":0}\n')

a = connect()
if not a:
    print('  could not reach the authority'); sys.exit(1)
fa = a.makefile('rw', buffering=1, newline='\n')
fa.write('{"join":"pvp"}\n')
time.sleep(0.4)                     # so A is demonstrably the one waiting
b = connect()
fb = b.makefile('rw', buffering=1, newline='\n')
fb.write('{"join":"pvp"}\n')

ta = threading.Thread(target=play, args=('A', fa, True, True))
tb = threading.Thread(target=play, args=('B', fb, True, False))
ta.start(); tb.start()
ta.join(120); tb.join(120)
srv.terminate()

if len(res) != 2:
    print('  a seat never finished: %s' % sorted(res)); sys.exit(1)
A, B = res['A'], res['B']
seats = {A['me'], B['me']}
leaks = A['leaks'] + B['leaks']
# Each seat must have heard the other, not merely its own echo.
from_other = [t for s, t in B['heard'] if s == A['me']]
to_other   = [t for s, t in A['heard'] if s == B['me']]
print('  two seats: %s, %d views, %d actions, %d leaks'
      % (sorted(seats), A['views'] + B['views'], A['acts'] + B['acts'], leaks))
print('  A waited for B: %s' % ('yes' if A['waiting'] else 'NO'))
print('  talk out of turn: B heard %d from A, A heard %d from B'
      % (len(from_other), len(to_other)))
print('  both agree on the winner: %s'
      % ('yes' if A['winner'] == B['winner'] else 'NO'))
ok = (seats == {0, 1} and leaks == 0 and A['waiting']
      and from_other and to_other and A['winner'] == B['winner'])
print('  ' + ('all clear' if ok else 'FAILED'))
sys.exit(0 if ok else 1)
