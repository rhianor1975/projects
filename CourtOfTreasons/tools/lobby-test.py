#!/usr/bin/env python3
"""The room: logging in, talking, and starting a game from it.

    python3 tools/lobby-test.py

What this has to prove beyond the pvp test is that the lobby holds
several people at once without forking -- so it checks that a third
person sees the first two arrive and sees what they say, and that a
wrong password is refused while a right one is not.
"""
import json, socket, subprocess, sys, threading, time

PORT = 9331
USERS = '/tmp/court-lobby-test-users.tsv'
import os
if os.path.exists(USERS): os.unlink(USERS)

srv = subprocess.Popen(['./court', '--serve', '0', '--houses', '2',
                        '--port', str(PORT)],
                       stderr=subprocess.PIPE, text=True,
                       env={'COURT_SEED': '5', 'COURT_USERS': USERS,
                            'PATH': '/usr/bin:/bin'})

def conn():
    for _ in range(80):
        try:
            s = socket.create_connection(('127.0.0.1', PORT), timeout=20)
            return s.makefile('rw', buffering=1, newline='\n'), s
        except OSError:
            time.sleep(0.1)
    return None, None

def send(f, d):
    """Write a message and push it out.

    buffering=1 on a socket makefile did not reliably flush here, so a
    line that was never sent looked exactly like a server that never
    answered -- and cost most of an afternoon."""
    f.write(json.dumps(d) + '\n')
    f.flush()


def login(name, pw):
    f, s = conn()
    send(f, {"hello": name, "pass": pw})
    return f, s

def until(f, pred, secs=8):
    """The next message that matters, skipping the room's other traffic.

    The lobby broadcasts joins, departures and the user list to
    everyone, so a test that reads exactly one line reads whichever
    happened to arrive first."""
    end = time.time() + secs
    while time.time() < end:
        line = f.readline()
        if not line:
            return None
        m = json.loads(line)
        if pred(m):
            return m
    return None

fails = []
def check(what, got, want):
    ok = got == want
    print('  %-34s %s' % (what, 'ok' if ok else 'FAILED (%r != %r)' % (got, want)))
    if not ok: fails.append(what)

# --- a name is created on first use, and the password then holds
fa, sa = login('alice', 'correct horse')
m = json.loads(fa.readline())
check('first use creates the name', m.get('lobby'), 1)

fx, sx = login('alice', 'wrong')
m = json.loads(fx.readline())
check('wrong password is refused', m.get('denied'), 'wrong password')

fy, sy = login('alice', 'correct horse')
m = json.loads(fy.readline())
check('same name twice is refused', m.get('denied'), 'already in the room')

fb, sb = login('bob', 'hunter two')
m = json.loads(fb.readline())
check('a second person can log in', m.get('lobby'), 1)

# --- alice should have been told bob arrived, and see him in the list
sa.settimeout(8)
m = until(fa, lambda m: 'users' in m
          and any(u['name'] == 'bob' for u in m['users']))
check('alice sees bob in the room',
      sorted(u['name'] for u in (m or {}).get('users', [])), ['alice', 'bob'])

# --- talking reaches the other person
send(fb, {"say": "well met"})
m = until(fa, lambda m: m.get('chat'))
check('alice hears bob speak',
      (m['from'], m['text']) if m else None, ('bob', 'well met'))

# --- bob offers a game, alice sits down, and a real game starts
send(fb, {"seek": 1})
m = until(fa, lambda m: 'users' in m
          and any(u['name'] == 'bob' and u['seek'] for u in m['users']))
bob_i = next((u['i'] for u in (m or {}).get('users', [])
              if u['name'] == 'bob'), None)
check('alice sees bob is looking', bob_i is not None, True)

send(fa, {"sit": bob_i})

def play(f, name, out):
    views = acts = leaks = 0
    started = False
    while True:
        line = f.readline()
        if not line: break
        m = json.loads(line)
        if m.get('start'): started = True; continue
        if m.get('over'):
            out[name] = (started, views, acts, leaks, m['winner']); return
        if 'me' in m:
            views += 1
            for l in m['lords']:
                if l['holder'] != m['me'] and l['revolution'] != -1: leaks += 1
            continue
        if m.get('ask'):
            v = json.loads(f.readline()); views += 1
            for l in v['lords']:
                if l['holder'] != v['me'] and l['revolution'] != -1: leaks += 1
            if m['ask'] == 'choose':
                legal = json.loads(f.readline())['legal']
                f.write(json.dumps(max(legal, key=lambda a: a['kind'] == 1)) + '\n'); f.flush()
                acts += 1
            else:
                f.write('{"answer":0}\n'); f.flush()

out = {}
ta = threading.Thread(target=play, args=(fa, 'alice', out))
tb = threading.Thread(target=play, args=(fb, 'bob', out))
ta.start(); tb.start(); ta.join(180); tb.join(180)
srv.terminate()

check('both seats finished a game', sorted(out), ['alice', 'bob'])
if len(out) == 2:
    check('both were told the game started',
          (out['alice'][0], out['bob'][0]), (True, True))
    check('no hidden state leaked', out['alice'][3] + out['bob'][3], 0)
    check('both agree on the winner', out['alice'][4], out['bob'][4])
    print('  %d views, %d actions over the lobby'
          % (out['alice'][1] + out['bob'][1], out['alice'][2] + out['bob'][2]))
print('  ' + ('all clear' if not fails else 'FAILED: ' + ', '.join(fails)))
sys.exit(1 if fails else 0)
