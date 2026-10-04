#!/usr/bin/env python3
"""Play a game over a real socket, and check N1 from the far end.

    python3 tools/net-test.py

The pipe test proves the protocol; this proves the transport.  They are
different failures: a wire format can be right while a socket silently
splits a message across two reads, or joins two into one, and a reader
that assumed one message per read would only find out on a slow link
between a Mac and a Linux box -- which is the case this exists for.
"""
import json, socket, subprocess, sys, time

PORT = 9317
srv = subprocess.Popen(['./court', '--serve', '0', '--duel', '--houses', '2',
                        '--port', str(PORT)],
                       stderr=subprocess.PIPE, text=True,
                       env={'COURT_SEED': '31', 'PATH': '/usr/bin:/bin'})
# wait for the listener rather than guessing at a sleep
s = None
for _ in range(60):
    try:
        s = socket.create_connection(('127.0.0.1', PORT), timeout=2); break
    except OSError:
        time.sleep(0.1)
if not s:
    print('  could not reach the authority'); sys.exit(1)

f = s.makefile('rw', buffering=1, newline='\n')
views = acts = leaks = 0
while True:
    line = f.readline()
    if not line: break
    m = json.loads(line)
    if m.get('over'):
        print('  over a socket: winner seat %d by %s' % (m['winner'], m['reason']))
        break
    view = json.loads(f.readline()); views += 1
    me = view['me']
    for l in view['lords']:
        if l['holder'] != me and l['revolution'] != -1: leaks += 1
    if m.get('ask') == 'choose':
        legal = json.loads(f.readline())['legal']
        pick = max(legal, key=lambda a: (a['kind'] != 13, a['kind'] == 1))
        f.write(json.dumps(pick) + '\n'); acts += 1
    else:
        f.write('{"answer":0}\n')
# Stop the server before reading what it said.
#
# This used to read stderr to end-of-file while the server was still
# running, and it worked -- because writing to the socket this client had
# just closed killed the server with SIGPIPE.  The test was passing on
# the strength of a bug, and when the server was fixed to survive a
# disconnect the test hung instead of the server dying.  A server that
# outlives its clients is the correct behaviour and the test has to be
# written for it.
srv.terminate()
try:
    out = srv.stderr.read() or ''
except Exception:
    out = ''
trust = 'TRUSTED HOST' in out
try:
    srv.wait(timeout=5)
except Exception:
    srv.kill()
print('  %d views, %d actions, %d leaks' % (views, acts, leaks))
print('  the host announced itself as a trusted host: %s' % ('yes' if trust else 'NO'))
sys.exit(1 if leaks or not trust else 0)
