#!/usr/bin/env python3
"""The help text, with its facts taken from design.xml.

    python3 tools/mkhelp.py            writes client/assets/help.tsv

The prose is written here, for a person who has never seen the game.
The FACTS are read out of design.xml -- the phases and their order, the
names of the win conditions, the Court's threshold, the trackers and
their maxima -- and the script fails if any of them has moved.

That is the whole point.  Help that is typed out separately drifts from
the rules the moment the rules change, and help that is wrong is worse
than none: it teaches a game nobody is playing.
"""
import os, re, sys, xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
d = ET.parse(os.path.join(ROOT, 'design.xml')).getroot()

def fail(m):
    print('mkhelp: %s' % m, file=sys.stderr)
    sys.exit(1)

# ---- facts, read rather than remembered
phases = [(int(p.get('order')), p.get('name'), (p.text or '').strip())
          for p in d.iter('phase')]
phases.sort()
if [n for _, n, _ in phases] != ['Accession', 'Tax', 'Turns', 'Reckoning',
                                 'Consequence', 'World']:
    fail('the round has changed shape: %s' % [n for _, n, _ in phases])

wins = [(c.get('name'), [s.text.strip() for s in c.findall('step')])
        for c in d.iter('condition')]
# Six since 2026-09-20: The Golden Throne was added and The Purchased
# Throne became reachable.  The count is asserted rather than counted
# because a road appearing or vanishing is exactly the kind of fact the
# help text is generated from, and it should not move quietly.
if len(wins) != 6:
    fail('expected six roads to victory, found %d' % len(wins))

trackers = {t.get('name'): t.get('max') for t in d.iter('tracker')}
for t in ('Servitude', 'Revolution', 'Unrest', 'Grievance'):
    if t not in trackers:
        fail('tracker %s is gone from design.xml' % t)

cap = d.find('.//round_cap')
cap = cap.text.strip() if cap is not None else '?'

terms = [(t.get('name'), t.get('due'), (t.text or '').strip())
         for t in d.iter('term')]
if not terms:
    fail('no promise terms')

# ---- the turn actions, as design.xml lists them
acts = {a.get('name'): ((a.text or '').strip(), a.get('limit') or '')
        for a in d.iter('action')}
for need in ('Draw', 'Play', 'Build Bond', 'Send Instigator', 'Propose',
             'Declare'):
    if need not in acts:
        fail('turn action %s is gone from design.xml' % need)

# ---- the client's action labels, against the engine's enum
#
# The label table was written against a shorter enum once and everything
# from kind 9 on was out by one: the panel offered "buy Favour" for an
# action that spends Grievance, and printed "action 13" for Pass.  The
# engine was never wrong -- the client sends the whole action back, so
# the right thing happened and the player was told it was something
# else.  Nothing caught it because nothing was comparing the two.
def check_action_labels():
    h = open(os.path.join(ROOT, 'src', 'court.h'), encoding='utf-8').read()
    m = re.search(r'\n\s*A_DRAW,(.*?)A_COUNT', h, re.S)
    if not m:
        fail('cannot find the Action enum in court.h')
    kinds = ['A_DRAW'] + [k.strip() for k in
                          re.split(r'[,\s]+', m.group(1)) if k.strip()]
    gd = open(os.path.join(ROOT, 'client', 'Main.gd'), encoding='utf-8').read()
    blk = re.search(r'func _action_label.*?\n\treturn "action', gd, re.S)
    if not blk:
        fail('cannot find _action_label in Main.gd')
    labelled = set(int(x) for x in re.findall(r'^\t\t(\d+):', blk.group(0), re.M))
    missing = [ (i, k) for i, k in enumerate(kinds)
                if i not in labelled and i <= kinds.index('A_PASS') ]
    if missing:
        fail('the client has no label for: %s'
             % ', '.join('%d (%s)' % (i, k) for i, k in missing))
    return kinds

KINDS = check_action_labels()

rows = []
def add(key, title, body):
    # Newlines survive as a literal backslash-n: the file is one entry
    # per line, so a real newline would end the entry.  The client turns
    # them back.  Lists that were run together as a paragraph read as a
    # wall and nobody finished them.
    body = '\n'.join(' '.join(p.split()) for p in body.split('\n'))
    rows.append((key, title, body.replace('\n', '\\n')))

# ---- the glossary a new player actually needs
add('standing', 'Standing',
    "What your House is: permanent, public, and slow to move.  You have "
    "three -- Military, Capital and Gold.  Standing is not spent by most "
    "cards; it is what your Levy is refilled from.")
add('levy', 'Levy',
    "What you can spend this turn.  It is refilled from your Standing at "
    "the start of every turn and it is gone at the end of it, so Levy you "
    "do not use is Levy you wasted.  A card that says spend takes Levy; a "
    "card that says permanently takes or gives Standing.")
add('military', 'Military',
    "Standing in arms.  It is what a Throne challenge is decided on: you "
    "win the challenge by committing more Military than the other House.")
add('capital', 'Capital',
    "Standing at court -- influence and position.  Eight of it, with "
    "twelve Gold, buys the Throne outright.")
add('gold', 'Gold',
    "Standing in money.  The Lord Paramount's tax takes Levy from you and "
    "turns it into their Gold.")
add('unrest', 'Unrest',
    "Trouble at home, one track for each resource, to %s.  At %s that "
    "resource revolts.  It is three tracks and not one on purpose: the "
    "House causing the trouble chooses where it falls, not the House "
    "suffering it." % (trackers['Unrest'], trackers['Unrest']))
add('grievance', 'Grievance',
    "What you are owed by the Lord Paramount, to %s.  You gain one every "
    "time they tax you.  It is spent, not kept." % trackers['Grievance'])
add('kept', 'Word Kept',
    "Promises you have kept.  The Court is watching and it remembers.")
add('broken', 'Broken Words',
    "Promises you have broken.  It costs Favour with the Court, and it "
    "breaks ties against you -- for the Paramountcy and at the final "
    "reckoning.")
add('court', 'The Court',
    "A third party that neither House controls, and at two players the "
    "whole game leans on it.  It watches promises.  Reach +%s Favour and "
    "you win outright, without a fight and without the Throne."
    % re.sub(r'\D', '', next(s for n, ss in wins if n == 'Favour of the Court'
                             for s in ss)) )
add('paramount', 'Lord Paramount',
    "Elected by the Council each round, not accrued.  The title lets you "
    "take 1 Levy of your choosing from the other House and turn it into "
    "Gold -- and gives them a Grievance for it.  It buys a good turn, not "
    "a permanent lead.")
add('lords', 'Minor Lords',
    "The Council.  You bind a lord with a hidden Bond of Servitude, to %s, "
    "and every revealed lord votes for the House that holds it.  Meanwhile "
    "Revolution builds in it, to %s, hidden -- and a lord whose Revolution "
    "has passed its Servitude stops voting for you.  At %s it turns."
    % (trackers['Servitude'], trackers['Revolution'], trackers['Revolution']))
add('servitude', 'Servitude',
    "How firmly a lord is bound to a House, to %s.  Hidden until the lord "
    "is revealed." % trackers['Servitude'])
add('revolution', 'Revolution',
    "How close a lord is to turning on the House that holds it, to %s.  "
    "Hidden -- and hidden from you as well, for a lord you do not hold.  A "
    "question mark there is not the client keeping a secret; it is the "
    "honest answer, because the authority never told it."
    % trackers['Revolution'])
add('promise', 'Promises',
    "A promise is an object, not a conversation.  It is recorded, it comes "
    "due, and at the Reckoning it is kept or broken -- both Houses "
    "deciding before either is shown.  Consideration is what you pay now "
    "to make it worth believing; breaking a promise that carried "
    "consideration costs more.")
add('ambition', 'Ambitions',
    "Secret goals.  Complete three and you become Throneworthy, which is "
    "what lets you challenge for the Throne at all.")
add('unbound', 'Unbound lords',
    "Lords nobody holds yet.  Bind one and it votes for you at the next "
    "Council -- and starts quietly resenting you.")

add('actions', 'Your turn',
    "Everything you may legally do, right now -- and not everything you "
    "could ever do.  An entry marked with a small bar down its right edge "
    "comes from a card in your hand: playing it, binding a lord with it, "
    "or making the promise it carries.  Hovering the card lights the "
    "line, and hovering the line lifts the card.\n"
    "The unmarked ones need no card: drawing from a deck, whispering to "
    "a lord or to the Court, declaring war, challenging for the Throne.\n"
    "A card you are holding that is not listed is one you cannot play "
    "this turn -- almost always because you have not the Levy for it.  "
    "Click a card in your hand to play it; the list on the right is "
    "there for everything that is not a card, and for the few cards "
    "that can do more than one thing.  "
    "Those cards go grey in your hand while you are being asked, and the "
    "ones you can play are lit at the edge.  Nothing is hidden and "
    "nothing moves: the hand stays in the order you know it in.")

# ---- one entry per thing the panel can offer, keyed by the engine's
# action kind.  The client looks these up by number, so a kind that
# gains a meaning gets an explanation here and nowhere else.
def act(kind, title, body, design_key=None):
    if design_key:
        txt, lim = acts[design_key]
        body = '%s  %s  (%s.)' % (body, txt, lim.lower())
    add('act.%d' % kind, title, body)

act(0, 'Draw a card',
    "Take a card from that deck into your hand.  Your own House deck is "
    "yours alone; the others are shared.  Drawing is once a turn, so the "
    "deck you pick is the turn's first real decision.\n"
    "Drawing deep costs 3 of that deck's resource instead of 1, and "
    "draws two so you keep the cheaper of them -- more choice for three "
    "times the price.", 'Draw')
act(1, 'Play a card',
    "Put this card into effect now, paying its cost in Levy.", 'Play')
act(2, 'Bind a lord',
    "Spend this card to advance your hidden Servitude over a Minor Lord.  "
    "A bound lord votes for you at the Council -- and begins, quietly, to "
    "resent you.", 'Build Bond')
act(3, 'Work against them',
    "Add hidden Revolution to a lord they hold, or Unrest to one of their "
    "resources.  Neither is visible to them as it happens.",
    'Send Instigator')
act(4, 'Offer a promise',
    "Name a term and offer it.  They may refuse; it costs neither of you "
    "anything if they do.  If they accept it is recorded, and at the "
    "Reckoning you will keep it or break it in front of the Court.",
    'Propose')
act(5, 'Answer their promise',
    "They have proposed something and it is your turn to say yes or no.")
act(9, 'Let a resource revolt',
    "Unrest has reached its limit in one of your resources.  It revolts: "
    "you lose 2 Standing in it and the track resets to nothing.")
act(12, 'Discard a card',
    "Put a card out of your hand without playing it.")
act(6, 'Declare Open War',
    "Say so openly.  What follows is fought with committed Military Levy.",
    'Declare')
act(7, 'Challenge for the Throne',
    "Only if you are Throneworthy -- three Ambitions completed.  Both "
    "Houses commit Military Levy at once and the larger commitment takes "
    "the Throne and the game.", 'Declare')
act(8, 'Reveal a Bond',
    "Show a Servitude you had hidden.  A revealed lord can vote for you; "
    "an unrevealed one cannot.", 'Declare')
act(10, 'Spend Grievance',
    "Cash in what the Lord Paramount owes you.  Grievance is spent, not "
    "kept, and it is worth nothing at the end of the game.")
act(11, 'Buy Favour',
    "Pay for the Court's good opinion.  Favour is a road to winning on "
    "its own: reach +8 and the game is yours without a fight.")
act(13, 'Pass',
    "Do nothing more this turn.  Any Levy you have not spent is lost when "
    "the turn ends.")

# ---- and one per question the authority can put to you
add('ask.commit', 'Committing Military',
    "Both Houses choose at the same time and neither sees the other's "
    "number first -- that is the whole reason this is a separate moment.  "
    "The larger commitment wins.  What you commit is spent whether you "
    "win or lose, so the question is not only how badly you want this, it "
    "is how much you can afford to lose.")
add('ask.break_promise', 'Keeping or breaking your word',
    "A promise you made has come due.  Keeping it costs you whatever you "
    "promised.  Breaking it costs Favour with the Court -- more if they "
    "paid you consideration for it -- and hands them a Grievance either "
    "way.  Both Houses decide before either decision is shown, so you "
    "cannot wait to see theirs.")
add('ask.accept_promise', 'Accepting a promise',
    "They are offering their word, not asking for yours.  Accepting costs "
    "you nothing and obliges you to nothing; refusing costs nothing "
    "either.  What it changes is that a promise accepted is recorded, and "
    "the Court will see whether they keep it.")
add('ask.lord_turns', 'Turning a lord against them',
    "You have been building Revolution in a lord the other House holds, "
    "and there is now enough of it.  Say yes and the lord leaves them "
    "now.  Say no and the Revolution stays where it is, to be spent "
    "later or lost if they shore the lord up.")

# ---- when each term falls due, in the engine's TERM_ order
#
# A promise whose condition is an event carries due_round = -1, which the
# panel was printing raw as "due round -1".  That is correct data and a
# meaningless thing to show a person, so the client shows the condition
# instead -- and takes the words from design.xml rather than keeping a
# second copy of them.
_h = open(os.path.join(ROOT, 'src', 'court.h'), encoding='utf-8').read()
_m = re.search(r'TERM_VOTE,(.*?)TERM_COUNT', _h, re.S)
if not _m:
    fail('cannot find the Term enum in court.h')
_order = ['TERM_VOTE'] + [t.strip() for t in re.split(r'[,\s]+', _m.group(1))
                          if t.strip()]
_due = {n.upper(): due for n, due, _ in terms}
for _i, _t in enumerate(_order):
    _name = _t[5:]
    if _name == 'ANY':
        add('due.%d' % _i, 'any term', 'when its own condition is met')
        continue
    if _name not in _due:
        fail('term %s is in court.h but not in design.xml' % _name)
    add('due.%d' % _i, _name.capitalize(), _due[_name])

# ---- the per-deck frame colours, read out of tools/card.sh
#
# The composed card takes its frame from ACCENT there, and the client
# draws a small version of the same card.  Two copies of a colour table
# drift; this is the one copy, and if a deck is recoloured the client
# follows without anybody remembering to.
_sh = open(os.path.join(ROOT, 'tools', 'card.sh'), encoding='utf-8').read()
_acc = re.findall(r"^\s*([A-Za-z\\ ]+?)\)\s+ACCENT='(#[0-9a-fA-F]{6})';"
                  r"\s*DARK='(#[0-9a-fA-F]{6})'", _sh, re.M)
if len(_acc) < 10:
    fail('cannot read the deck accents out of card.sh (found %d)' % len(_acc))
for _d, _a, _k in _acc:
    add('accent.%s' % _d.replace('\\ ', ' ').strip(), _a, _k)

# ---- the panel colour, also out of card.sh
#
# PARCH with a radial gradient multiplied over it, from near-white at
# the middle to a darker tan at the edges.  The client cannot run
# ImageMagick, but it can read the three numbers and approximate the
# falloff, which is the difference between a mini card that looks like
# its own card and one that looks like a grey slab.
_pa = re.search(r"^PARCH='(#[0-9a-fA-F]{6})'", _sh, re.M)
_gr = re.search(r"radial-gradient:'(#[0-9a-fA-F]{6})'-'(#[0-9a-fA-F]{6})'", _sh)
if not _pa or not _gr:
    fail('cannot read the panel colours out of card.sh')
add('parch', _pa.group(1), '%s %s' % (_gr.group(1), _gr.group(2)))

add('house', 'A House',
    "Everything one House has and is owed, on one line: its name and "
    "its word, its three Standings with the Levy each will refill to, "
    "what it is owed by the Lord Paramount, how many promises it has "
    "kept and broken, and how much trouble is brewing at home.  Yours "
    "is the lower bar; theirs is the upper one.")
add('log', 'The log',
    "What has happened, most recent last.  It is here so you can see "
    "what the other House did rather than infer it from a number that "
    "changed while you were looking elsewhere.")
add('decks', 'The decks',
    "One shield for every deck you may draw from this turn, in that "
    "deck's own colours.  Click one to draw.  Only the decks you can "
    "afford appear, and drawing is once a turn -- so which shield you "
    "click is the turn's first real decision.")
# ---- the Houses, read out of src/data.c
#
# The client kept its own list of four.  Adding a fifth House meant
# remembering to add it in two places, and the door's House picker was
# laid out as five fixed boxes that a sixth would have run straight off
# the end of.  One list, emitted here, and a picker that does not care
# how long it is.
_dc = open(os.path.join(ROOT, 'src', 'data.c'), encoding='utf-8').read()
_hm = re.search(r'HOUSE_NAME\[H_COUNT\]\s*=\s*\{(.*?)\}', _dc, re.S)
if not _hm:
    fail('cannot read HOUSE_NAME out of data.c')
_houses = re.findall(r'"([^"]+)"', _hm.group(1))
if not _houses:
    fail('no House names found')
add('houses', str(len(_houses)), ' | '.join(_houses))

# One entry per phase, so the tracker's icons can name themselves on
# hover.  Same source as the summary below: the phase's own text.
for _o, _n, _t in phases:
    add('phase.%d' % (_o - 1), _n, _t)

# And one per step of a turn.  The tracker draws these, so the name and
# the limit a player reads on hover are design.xml's own words.
_steps = sorted((int(x.get('order')), x.get('name'), x.get('limit'),
                 (x.text or '').strip())
                for sq in d.iter('sequence') for x in sq)
if [n for _, n, _, _ in _steps] != ['Levy', 'Draw', 'First Court',
                                    'Declare', 'Second Court']:
    fail('the turn has changed shape: %s' % [n for _, n, _, _ in _steps])
for _o, _n, _l, _t in _steps:
    add('step.%d' % (_o - 1), _n, '%s\n%s' % (_t, _l.lower()))

add('round', 'A round, in order',
    '\n'.join('%d.  %s -- %s' % (o, n, re.split(r'(?<=[.])\s', t)[0])
              for o, n, t in phases))
add('winning', 'The roads to winning',
    '\n'.join('%s -- %s' % (n, ' '.join(ss)) for n, ss in wins))
# Five, not all of them.  The overlay is two columns and the eighth term
# ran off the bottom of the second -- and every term is shown in full at
# the moment you are actually offered it, so the reference does not have
# to carry the lot.
_SHOW = 5
add('terms', 'What you can promise',
    '\n'.join('%s, due %s\n    "%s"' % (n, (due or '?').lower(), txt)
              for n, due, txt in terms[:_SHOW])
    + ('\nand %d more, each shown in full when it is offered.'
       % (len(terms) - _SHOW) if len(terms) > _SHOW else ''))

out = os.path.join(ROOT, 'client', 'assets', 'help.tsv')
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w', encoding='utf-8') as fh:
    for k, t, b in rows:
        fh.write('%s\t%s\t%s\n' % (k, t, b))
print('%d entries -> %s' % (len(rows), out))
print('  checked against design.xml: %d phases, %d roads, %d terms'
      % (len(phases), len(wins), len(terms)))
