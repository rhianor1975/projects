#!/usr/bin/env python3
"""Card backs: one heraldic shield per deck.

    python3 tools/gen-backs.py              render any that are missing
    python3 tools/gen-backs.py --dry-run    print the prompts

sd-turbo, not the sdxl-turbo the card faces use.  A face is a scene with
people in it and wants the bigger UNet; a back is one device on one
shield, where sd-turbo is faster and, on the evidence of Aldemar's stag,
at least as good at a simple heraldic subject.

A back is not a scene.  It wants to be centred, symmetrical and the same
every time, so these prompts ask for a shield on an ornamented ground and
say so several ways -- this model glosses adjectives and draws nouns, so
"centred" has to arrive as "a single shield" rather than as a composition
note.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.request

HOST = os.environ.get('IMAGELAB', 'http://192.168.0.50:8095')
ART = 'art/backs'
STYLE = ('steampunk, oil painting, thick impasto brushwork, dramatic '
         'chiaroscuro, brass and soot, heraldic, symmetrical, centred, '
         'painted card back')
NEG = ('text, words, letters, typography, watermark, signature, people, '
       'faces, hands, landscape, scene, blurry, asymmetric, cropped')

# The four Houses and the realm were written in the elaborate register
# the card art uses.  The rest are written the way Aldemar's came out --
# a device, "in a heraldic nordic medieval shield", and "oil painted".
#
# That is not a stylistic preference, it is the result.  Three long
# prompts of mine came back as roundels for the stag; four plain words on
# sd-turbo got the shield and got picked.  A long prompt gives this model
# more nouns to prefer over the one that mattered.
BACKS = {
    'ravenmark': 'a single heraldic shield bearing one black raven with '
                 'spread wings, riveted iron, dark red and gunmetal, '
                 'storm light behind it',
    'vipren':    'a single heraldic shield bearing one coiled serpent, '
                 'deep green and verdigris, green glass and lamplight, '
                 'smoke behind it',
    # Goldwyn's spider stays as first drawn.  I read it as too small and
    # too dark to carry and asked for a bigger one; the answer was that it
    # was fine.  A gold spider that has to be looked for on a banker's
    # arms is arguably the joke working, and either way it was not mine
    # to change.
    'goldwyn':   'a single heraldic shield bearing one spider on its web, '
                 'brass and gold leaf, warm gaslight, stacked coins behind it',
    # Not used: Aldemar's accepted back is in BACKS.tsv and came from
    # elsewhere.  Kept so --reproduce is not the only way to get one.
    'aldemar':   'stag in a heraldic nordic medieval shield, oil painted',
    'realm':     'a single heraldic shield bearing a crown above crossed '
                 'keys and a brass gear, ochre and soot, for the shared '
                 'decks of the realm',

    # the eight piles a player chooses between, plus the two that resolve
    # as they are drawn.  Device, shield, oil paint, and the deck's own
    # colour from tools/card.sh.
    'war':       'crossed cannons in a heraldic nordic medieval shield, '
                 'oil painted, dark red and soot',
    'political':  'a crossed mace and scroll in a heraldic nordic medieval '
                 'shield, oil painted, slate blue and pewter',
    'intrigue':  'a dagger and a key crossed in a heraldic nordic medieval '
                 'shield, oil painted, brass and shadow',
    'world':     'a sun over bare hills in a heraldic nordic medieval '
                 'shield, oil painted, weathered brown and bone',
    'ambition':  'a crown on a high step in a heraldic nordic medieval '
                 'shield, oil painted, dull plum and old gold',
    'throne':    'a tall iron throne in a heraldic nordic medieval shield, '
                 'oil painted, dark red brown and gilt',
    'promise':   'two clasped hands over a wax seal in a heraldic nordic '
                 'medieval shield, oil painted, deep green and parchment',
    'lever':     'a chain and an iron key in a heraldic nordic medieval '
                 'shield, oil painted, moss green and iron',
    'council':   'a ring of empty chairs in a heraldic nordic medieval '
                 'shield, oil painted, cold slate and tin',
    'court':     'an open ledger and a quill in a heraldic nordic medieval '
                 'shield, oil painted, pale stone and grey',
    # The four Houses added 2026-09-20.  One charge each, drawn to read
    # at the size a shield is actually seen.
    'leoward':   'a single heraldic shield bearing one lion\'s head facing '
                 'forward with a full mane, gold leaf and amber, sunlit '
                 'stone behind it',
    'stonegarth':'a single heraldic shield bearing one ox head with wide '
                 'horns and a yoke across them, ochre and worked timber, '
                 'deep-cut furrows behind it',
    'wulfren':   'a single heraldic shield bearing one wolf head in '
                 'profile with a long muzzle, slate grey and iron, bare '
                 'winter branches behind it',
    # The realm's last deck had no back at all: BACK_FILE in the client
    # lists seventeen and this held sixteen of them, so the Cataclysm
    # pile drew as a blank shield.
    'cataclysm': 'a single heraldic shield bearing a cracked crown over '
                 'a broken hourglass, ash grey and ember red, a burning '
                 'sky behind it',
    'everhold':  'a single heraldic shield bearing one boar head lowered '
                 'to charge with an upward tusk, dark blood red, bracken '
                 'and mire behind it',
}


def post(path, payload):
    req = urllib.request.Request(
        HOST + path, data=json.dumps(payload).encode(),
        headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def get(path):
    with urllib.request.urlopen(HOST + path, timeout=30) as r:
        return json.load(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--reproduce', action='store_true',
                    help='re-render the accepted backs from BACKS.tsv, '
                         'seeds and all')
    ap.add_argument('--only', help='comma-separated back names')
    ap.add_argument('--model', default='sd-turbo')
    a = ap.parse_args()

    # art/ is not in the repository -- it is generated, and generated
    # things are regenerated rather than stored.  That only holds if what
    # generated them is written down, and for a back it is not enough to
    # keep the prompt: these were chosen one out of four, so the seed is
    # half of what made them.  BACKS.tsv holds the model, the seed and the
    # prompt of every accepted back.
    #
    # Aldemar's is not in the dictionary below and never was.  It came
    # from four words on a different model -- 'stag in a heraldic nordic
    # medieval shield, oil painted', sd-turbo, no negative prompt -- after
    # three of my increasingly elaborate attempts came back as roundels.
    if a.reproduce:
        import csv
        rows = list(csv.DictReader(open('BACKS.tsv', encoding='utf-8'),
                                   delimiter='\t'))
        os.makedirs(ART, exist_ok=True)
        pending = {}
        for r in rows:
            if a.only and r['back'] not in a.only.split(','):
                continue
            body = {'prompt': r['prompt'], 'negative': r['negative'],
                    'model': r['model'], 'width': int(r['w']),
                    'height': int(r['h']), 'steps': int(r['steps']),
                    'count': 1, 'seed': int(r['seed']),
                    'hires': 0, 'upscale': False}
            for jid in post('/api/jobs', body).get('queued', []):
                pending[jid] = r['back']
                print('  %s -> %s (seed %s)' % (r['back'], jid, r['seed']),
                      file=sys.stderr)
        while pending:
            time.sleep(15)
            try:
                # /api/jobs returns a dict now -- {active, rows, page} -- and used
                # to return a bare list, so iterating it gave the dict's KEYS and
                # j['id'] raised TypeError.
                _r = get('/api/jobs')
                _rows = _r.get('active', []) if isinstance(_r, dict) else _r
                jobs = {j['id']: j for j in _rows}
            except urllib.error.URLError:
                continue
            for jid in list(pending):
                j = jobs.get(jid)
                if j is None:
                    del pending[jid]; continue
                if j.get('status') == 'done' and j.get('output'):
                    dst = '%s/%s.png' % (ART, pending[jid])
                    with urllib.request.urlopen(
                            HOST + '/out/' + j['output'], timeout=120) as r2:
                        open(dst, 'wb').write(r2.read())
                    print('  %s' % dst, file=sys.stderr)
                    del pending[jid]
                elif j.get('status') in ('failed', 'error'):
                    del pending[jid]
        return 0

    want = BACKS
    if a.only:
        keys = [k.strip() for k in a.only.split(',')]
        want = {k: BACKS[k] for k in keys if k in BACKS}

    if a.dry_run:
        for k, v in want.items():
            print('%-10s %s. %s' % (k, v, STYLE))
        return 0

    os.makedirs(ART, exist_ok=True)
    todo = [k for k in want if not os.path.exists('%s/%s.png' % (ART, k))]
    print('%d backs, %d to render' % (len(want), len(todo)), file=sys.stderr)

    pending = {}
    for k in todo:
        body = {'prompt': '%s. %s' % (want[k], STYLE), 'negative': NEG,
                'model': a.model, 'width': 512, 'height': 512, 'steps': 4,
                'count': 1, 'seed': -1, 'hires': 0, 'upscale': False}
        res = post('/api/jobs', body)
        for jid in res.get('queued', []):
            pending[jid] = k
            print('  queued %s -> %s' % (k, jid), file=sys.stderr)

    while pending:
        time.sleep(15)
        try:
            # /api/jobs returns a dict now -- {active, rows, page} -- and used
            # to return a bare list, so iterating it gave the dict's KEYS and
            # j['id'] raised TypeError.
            _r = get('/api/jobs')
            _rows = _r.get('active', []) if isinstance(_r, dict) else _r
            jobs = {j['id']: j for j in _rows}
        except urllib.error.URLError:
            continue
        for jid in list(pending):
            j = jobs.get(jid)
            if j is None:
                del pending[jid]
                continue
            if j.get('status') == 'done' and j.get('output'):
                dst = '%s/%s.png' % (ART, pending[jid])
                with urllib.request.urlopen(HOST + '/out/' + j['output'],
                                            timeout=120) as r:
                    open(dst, 'wb').write(r.read())
                print('  %s' % dst, file=sys.stderr)
                del pending[jid]
            elif j.get('status') in ('failed', 'error'):
                print('  failed %s' % pending[jid], file=sys.stderr)
                del pending[jid]
    return 0


if __name__ == '__main__':
    sys.exit(main())
