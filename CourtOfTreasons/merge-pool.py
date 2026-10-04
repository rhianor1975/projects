#!/usr/bin/env python3
"""CARDS.tsv + CONVERTED.tsv -> POOL.tsv, in deck order.

    python3 merge-pool.py

gen-cards.py asserts that each deck's rows are contiguous, because
DECK_FIRST and DECK_SIZE turn that into pointer arithmetic.  The seed and
the converted pool are each ordered by their own deck sequence, so
concatenating them interleaves Ravenmark with Ravenmark and the assert
fires.  This sorts by the deck order the enum uses and nothing else --
within a deck the seed comes first, because those are the cards that were
written for this game rather than converted into it.
"""
import csv
import sys

ORDER = ['Ravenmark', 'Vipren', 'Goldwyn', 'Aldemar', 'War', 'Political',
         'Intrigue', 'Promise', 'Lever', 'Ambition', 'Council', 'World',
         'Court', 'Throne', 'Combat', 'Ghost', 'Cataclysm', 'Resurrection',
         'Instigator']
HDR = ('deck', 'id', 'name', 'type', 'category', 'cost', 'reads', 'effect')

rows, seen_ids = [], {}
for src, origin in (('CARDS.tsv', 'seed'), ('CONVERTED.tsv', 'converted')):
    try:
        fh = open(src, encoding='utf-8')
    except FileNotFoundError:
        continue
    for r in csv.DictReader(fh, delimiter='\t'):
        # The seed and the parent both number from 001 in every deck, so a
        # converted card can collide with a seed card that is not it.  The
        # converted one is renumbered rather than dropped: it is a
        # different card that happens to share a number.
        key = (r['deck'], r['id'])
        if key in seen_ids:
            n = 1
            while (r['deck'], '%s-%d' % (r['id'], n)) in seen_ids:
                n += 1
            r['id'] = '%s-%d' % (r['id'], n)
        seen_ids[(r['deck'], r['id'])] = origin
        r['_origin'] = origin
        rows.append(r)

rows.sort(key=lambda r: (ORDER.index(r['deck']) if r['deck'] in ORDER else 99,
                         0 if r['_origin'] == 'seed' else 1))

with open('POOL.tsv', 'w', encoding='utf-8') as fh:
    fh.write('\t'.join(HDR) + '\n')
    for r in rows:
        fh.write('\t'.join(str(r.get(k, '')).replace('\t', ' ')
                           for k in HDR) + '\n')

import collections
by = collections.Counter((r['deck'], r['_origin']) for r in rows)
print('  POOL.tsv  %d cards\n' % len(rows), file=sys.stderr)
print('  deck            seed  converted   total', file=sys.stderr)
for d in ORDER:
    s, c = by[(d, 'seed')], by[(d, 'converted')]
    if s or c:
        print('  %-14s %5d %10d %7d' % (d, s, c, s + c), file=sys.stderr)
