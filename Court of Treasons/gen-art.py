#!/usr/bin/env python3
"""Card art, from CARDS.tsv to art/, via ImageLab on the Mac Pro.

    python3 gen-art.py --deck Ravenmark      one deck
    python3 gen-art.py --id RAV001,VIP002    named cards
    python3 gen-art.py --all                 everything still missing
    python3 gen-art.py --dry-run --deck War  print prompts, queue nothing

Resumable: a card whose file already exists in art/ is skipped, so the
run can be stopped and started again, which matters because the server
renders one image at a time and 124 of them is most of a working day.

The server is shared -- other people's jobs sit in the same queue -- so
this keeps at most a few of its own in flight and waits rather than
flooding it.
"""
import argparse
import csv
import json
import os
import sys
import time
import urllib.error
import urllib.request

HOST  = os.environ.get('IMAGELAB', 'http://192.168.0.50:8095')
# Every job this program has ever queued, as card id and job id.
#
# /api/jobs returns only the last 120 jobs -- a display limit in the
# server ([-120:] at line 651), not a retention one: it keeps and renders
# everything.  Polling that list for a job's status therefore reports any
# job pushed past the window as missing, and the old code read missing as
# lost and gave up on it.  Queueing a large run made most of it "lost"
# while the server rendered it anyway.
#
# So the job list is not used to find finished work.  This file is, and
# /out/<jobid>.png is asked for directly.  It also survives a restart,
# which the in-memory map did not.
JOBMAP = os.path.join('logs', 'jobs.tsv')
# Card art is sdxl-turbo and backs are sd-turbo, which is a division of
# labour and not a preference.  A card face is a scene with people in it
# and wants the bigger UNet; a back is one device on one shield, where
# sd-turbo is faster and, on the evidence of Aldemar's stag, at least as
# good at a simple heraldic subject.  See tools/gen-backs.py.
MODEL = 'sdxl-turbo'
ART   = 'art'

# The style, fixed for every card, so that 124 images look like one deck
# rather than 124 separate commissions.
# Measured, not guessed, and the measurement is narrower than it first
# looked.  One word of steampunk against a scene drew the scene: figures,
# a banner, a mustering host.  Giving steampunk concrete nouns instead
# (gears, copper pipes, smokestacks) drew a beautiful machine shop with
# nobody in it, for a card whose scene is a line of guns levelled at
# kneeling prisoners -- but the same heavy style drew Poison very well, a
# gloved hand and a green glass and a figure in goggles.
#
# So it is not that the heavy style is worse.  It is that this model
# draws nouns and glosses adjectives, so style nouns compete with subject
# nouns, and a scene that needs a crowd loses that competition while a
# scene that needs one object on a table wins it.  This deck is mostly
# crowds and figures, so steampunk stays one word and the palette carries
# the rest.
STYLE = ('steampunk, oil painting, thick impasto brushwork, dramatic '
         'chiaroscuro, muted ochre and verdigris palette, brass and '
         'soot, painted card illustration')
NEGATIVE = ('text, words, letters, typography, watermark, signature, '
            'border, frame, logo, ui, blurry, deformed hands, extra fingers')

# One visual register per deck.  The decks are the game's factions and its
# tones; a Vipren card and a Goldwyn card should not be able to be swapped
# without anyone noticing.
REGISTER = {
    'Ravenmark':  'iron, storm light, riveted plate, black banners, rain',
    'Vipren':     'shadow, green glass, lamplight through smoke, masks, '
                  'whispering figures',
    'Goldwyn':    'brass, stacked coin, ledgers, warm gaslight, polished '
                  'counting-house wood',
    'Aldemar':    'pale marble, gilt seals, cold daylight, robed assembly, '
                  'carved law',
    'War':        'smoke, artillery brass, trenchworks, a red horizon',
    'Political':  'council chamber, tall windows, oratory, folded hands',
    'Intrigue':   'a lit doorway in a dark street, gloved hands, poison '
                  'glass, cipher',
    'Promise':    'two hands over a signed page, a wax seal, candlelight',
    'Lever':      'chain, key, iron collar, a held document',
    'Ambition':   'a distant crown seen through machinery, ascent, '
                  'far horizon',
    'Council':    'a full chamber mid-vote, raised hands, a speaker standing',
    'World':      'a wide landscape under weather, the realm entire',
    'Court':      'an empty throne room, an impartial clerk, a long ledger',
    'Throne':     'a brass and iron throne, a contested crown, banners',
    # These five had no register and fell through to World, which is a
    # wide landscape under weather -- the worst possible palette for a
    # deck about a knife in a corridor.  A silent fallback that produces
    # a plausible picture is worse than one that fails.
    'Combat':     'close quarters, mud and smoke, locked shields, bayonet '
                  'steel, no horizon',
    'Ghost':      'cold blue-grey light, mist, guttering candles, frost on '
                  'glass, figures half there',
    'Resurrection': 'torn banners raised again, dawn on ruin, a returned '
                    'claimant, worn regalia',
    'Instigator': 'unmarked coin, a foreign hand, a crowd being turned, '
                  'lamplight on wet stone',
    'Cataclysm':  'red sky, falling ash, distant fires, a realm coming '
                  'apart',
    # The four Houses added 2026-09-20.  Each register is built away
    # from the first four's: Ravenmark already owns iron and storm,
    # Vipren shadow and green glass, Goldwyn brass, Aldemar marble.
    # A House is learned by its look before its rules, and eight that
    # blur into each other is a worse failure than one that is ugly.
    'Leoward':       'gold leaf, sunlit stone, a sworn oath on vellum, red '
                  'and amber heraldry, open hands',
    'Stonegarth':         'ochre earth, worked timber, yoke and harness, patient '
                  'beasts, deep-cut furrows, dust',
    'Wulfren':       'slate grey, bare winter branches, breath on cold air, '
                  'lean figures, old scars, snow light',
    'Everhold':       'dark blood red, bracken and mire, tusks and bristle, '
                  'low thicket, torchlight in rain',
}


SCENES = {}
try:
    for _line in open('scenes.tsv', encoding='utf-8'):
        if '\t' in _line:
            _k, _v = _line.rstrip('\n').split('\t', 1)
            SCENES[_k] = _v
except FileNotFoundError:
    pass


def scene(row):
    """A prompt from the card: a hand-written scene, the deck's palette,
    and the constant style.

    The card's effect text is deliberately left out.  It is written for a
    classifier -- "+3 Military Levy this turn" -- and a classifier is the
    only reader it suits; fed to an image model it contributes numbers and
    game nouns and no picture at all.  The server's own note on this model
    says it "drew 1 of 9 scene elements" against FLUX's 8, so the prompt
    that works is short, concrete and visual, which is what scenes.tsv is.
    """
    reg = REGISTER.get(row['deck'], REGISTER['World'])
    sc = SCENES.get(row['id'])
    if sc is None:
        raise KeyError(row['id'])
    return '%s. %s. %s' % (sc, reg, STYLE)


def post_retry(path, payload, tries=4):
    """A three-hour run meets the odd dropped connection; one of those
    should cost a card's turn in the queue, not the rest of the run."""
    for i in range(tries):
        try:
            return post(path, payload)
        except OSError as e:
            if i == tries - 1:
                raise
            print('    retrying after %s' % e, file=sys.stderr)
            time.sleep(5 * (i + 1))


def post(path, payload):
    req = urllib.request.Request(
        HOST + path, data=json.dumps(payload).encode(),
        headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.load(r)


def get(path):
    with urllib.request.urlopen(HOST + path, timeout=30) as r:
        return json.load(r)


def remember(card, jid):
    try:
        os.makedirs('logs', exist_ok=True)
        with open(JOBMAP, 'a', encoding='utf-8') as fh:
            fh.write('%s\t%s\n' % (card, jid))
    except OSError:
        pass                      # a lost note costs a re-queue, not a card


def recall():
    """Card id -> every job id ever queued for it, newest last."""
    out = {}
    try:
        fh = open(JOBMAP, encoding='utf-8')
    except (OSError, IOError):
        return out
    with fh:
        for line in fh:
            if '\t' in line:
                c, j = line.rstrip('\n').split('\t', 1)
                out.setdefault(c, []).append(j)
    return out


def ready(jid):
    """The rendered image, or None if it is not done yet.

    Asked of /out directly.  A job outside the /api/jobs window is not a
    job that failed, and this cannot tell the difference -- so it does
    not try to; it asks whether the picture exists."""
    try:
        with urllib.request.urlopen(HOST + '/out/' + jid + '.png',
                                    timeout=60) as r:
            if r.status == 200:
                return r.read()
    except urllib.error.HTTPError:
        return None               # 404: still rendering
    except OSError:
        return None
    return None


def fetch(output, dest):
    with urllib.request.urlopen(HOST + '/out/' + output, timeout=120) as r:
        data = r.read()
    with open(dest, 'wb') as fh:
        fh.write(data)
    return len(data)


def _pool():
    """CARDS.tsv and POOL.tsv, in that order, deduplicated by id.

    CARDS.tsv wins where both have a card, because it is the live one --
    and it holds 34 cards written after the pool was converted, which
    exist nowhere else."""
    seen, rows = set(), []
    for path in ('CARDS.tsv', 'POOL.tsv'):
        try:
            fh = open(path, encoding='utf-8')
        except FileNotFoundError:
            continue
        with fh:
            for r in csv.DictReader(fh, delimiter='\t'):
                if r['id'] in seen:
                    continue
                seen.add(r['id'])
                rows.append(r)
    return rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--deck', help='one deck name, as CARDS.tsv spells it')
    ap.add_argument('--id', help='comma-separated card ids')
    ap.add_argument('--all', action='store_true')
    ap.add_argument('--dry-run', action='store_true')
    ap.add_argument('--model', default=MODEL)
    ap.add_argument('--size', type=int, default=512)
    ap.add_argument('--inflight', type=int, default=500,
                    help='own jobs allowed in the queue at once; the '
                         'default queues the whole run')
    a = ap.parse_args()

    # Every card there is, not only the ones the engine currently deals.
    #
    # CARDS.tsv is the live set and POOL.tsv is the whole manifest; the
    # art is generated against the union because an image outlives the
    # rules text on top of it.  A card whose effect is still to be
    # rewritten still gets the same picture when it is, and the art run
    # is the slow half of this -- 1,100 images at a minute each is days,
    # and none of that time should wait on wording.
    rows = _pool()
    if a.id:
        want = set(x.strip().upper() for x in a.id.split(','))
        rows = [r for r in rows if r['id'].upper() in want]
    elif a.deck:
        rows = [r for r in rows if r['deck'].lower() == a.deck.lower()]
    elif not a.all:
        ap.error('pick --deck, --id or --all')

    missing = [r['id'] for r in rows if r['id'] not in SCENES]
    if missing:
        print('no scene written for: %s' % ', '.join(missing), file=sys.stderr)
        print('Add a line to scenes.tsv.  A card drawn from its name alone'
              '\nis worse than a card not drawn.', file=sys.stderr)
        return 1

    os.makedirs(ART, exist_ok=True)
    todo = [r for r in rows
            if not os.path.exists(os.path.join(ART, r['id'] + '.png'))]
    print('%d cards selected, %d already drawn, %d to do'
          % (len(rows), len(rows) - len(todo), len(todo)), file=sys.stderr)

    if a.dry_run:
        for r in todo:
            print('%s  %s' % (r['id'], scene(r)))
        return 0

    # Adopt every job already queued for a card we still need.
    #
    # The map on disk, not the server's job list: the list shows only the
    # last 120 and would hide most of a large run.  A card with several
    # attempts takes the newest.
    pending = {}            # job id -> card id
    known = recall()
    queue, adopted = [], 0
    # Adopt only a job the SERVER still has.
    #
    # logs/jobs.tsv remembers every job ever queued, and a job id in it
    # was taken as proof the render was on its way: "a job is never
    # given up on".  That is true while the server is up and false the
    # moment it is restarted or its queue is cleared, and then the
    # adopted ids are polled forever -- misses, sleep 20, misses --
    # while the cards behind them are never queued at all.  384 to do,
    # 372 adopted, and only the 12 genuinely new ones ever rendered.
    live = set()
    try:
        _r = get('/api/jobs')
        _rows = _r.get('active', []) if isinstance(_r, dict) else _r
        live = set(j['id'] for j in _rows)
    except Exception:
        live = set()
    stale = 0
    for r in todo:
        js = known.get(r['id'])
        if js and js[-1] in live:
            pending[js[-1]] = r['id']
            adopted += 1
        else:
            if js:
                stale += 1
            queue.append(r)
    if stale:
        print('%d remembered jobs are gone from the server, re-queued'
              % stale, file=sys.stderr)
    if adopted:
        print('%d already queued earlier, adopted rather than re-queued'
              % adopted, file=sys.stderr)
    # The order jobs were queued in, which is the order they finish in.
    order = list(pending)
    done = failed = 0
    while queue or pending:
        # The whole run goes into the queue at once.  Three at a time was
        # politeness on a shared server, and it bought nothing: the server
        # renders serially either way, so a small window only adds a poll
        # gap between every card and makes the run depend on this process
        # staying alive.  Queued, the server works through them whether or
        # not anything is watching, and a restart picks up the downloads.
        while queue and len(pending) < a.inflight:
            r = queue.pop(0)
            body = {'prompt': scene(r), 'negative': NEGATIVE,
                    'model': a.model, 'width': a.size, 'height': a.size,
                    'steps': 4, 'count': 1, 'seed': -1,
                    'hires': 0, 'upscale': False}
            try:
                res = post_retry('/api/jobs', body)
            except OSError as e:
                print('  queue failed for %s: %s' % (r['id'], e),
                      file=sys.stderr)
                failed += 1
                continue
            for jid in res.get('queued', []):
                pending[jid] = r['id']
                order.append(jid)
                remember(r['id'], jid)
                print('  queued %s -> %s' % (r['id'], jid), file=sys.stderr)

        if not pending:
            break
        got = False
        # In queue order, and stop after a few that are not ready.
        #
        # The server renders roughly first-in-first-out, so once a job is
        # still running everything queued behind it is too.  Asking about
        # all of them every pass was several hundred requests a minute at
        # a machine whose whole job is to be rendering instead.
        misses = 0
        for jid in list(order):
            if jid not in pending:
                continue
            data = ready(jid)
            if data is None:
                misses += 1
                if misses >= 3:
                    break
                continue
            misses = 0
            dest = os.path.join(ART, pending[jid] + '.png')
            try:
                with open(dest, 'wb') as fh:
                    fh.write(data)
                print('  %s  %d bytes' % (pending[jid], len(data)),
                      file=sys.stderr)
                done += 1
                got = True
            except OSError as e:
                print('  write failed %s: %s' % (pending[jid], e),
                      file=sys.stderr)
                failed += 1
            del pending[jid]
        # Nothing finished this pass, so wait before asking again.  A job
        # is never given up on: the server keeps and renders everything
        # it accepted, and the only way to find out is to keep asking.
        if not got:
            time.sleep(20)

    print('%d drawn, %d failed, %d left in art/'
          % (done, failed, len(os.listdir(ART))), file=sys.stderr)
    return 1 if failed and not done else 0


if __name__ == '__main__':
    sys.exit(main())
