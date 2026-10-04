#!/usr/bin/env python3
"""CARDS.tsv -> src/cards.inc.

    python3 gen-cards.py > src/cards.inc

This is the classifier the design asks for.  The design's own writing rule
is what makes it possible:

    "Effects name Levy or Standing explicitly.  '+2 Military' alone is
     ambiguous and is not acceptable in this design; the parent game's
     classifier could not tell them apart and it cost a fortnight."

So there is no guessing here.  A pattern either matches an effect line or
it does not, and a line no pattern matches is printed to stderr and the
run fails.  That is the point: the seed set is 124 cards and the manifest
is 1,135, so the classifier will meet a thousand lines it has not seen,
and the only safe failure is a loud one.  A card silently compiled to
OP_NONE would be an inert play that the inert-play measure then reports as
a design problem, and the afternoon spent chasing that is the afternoon
this exception exists to save.
"""
import csv
import re
import sys

SERVITUDE_MAX = 5   # must match src/court.h
RES  = {'Military': 'R_MIL', 'Capital': 'R_CAP', 'Gold': 'R_GOLD'}
# The pool writes the Capital stat out in full on some cards.
RES['Political Capital'] = 'R_CAP'
COST = {'M': 0, 'C': 1, 'G': 2, 'Grv': 3}

DECK = {'Ravenmark': 'D_RAVENMARK', 'Vipren': 'D_VIPREN',
        'Goldwyn': 'D_GOLDWYN', 'Aldemar': 'D_ALDEMAR',
        'Leoward': 'D_LEOWARD', 'Stonegarth': 'D_STONEGARTH',
        'Wulfren': 'D_WULFREN', 'Everhold': 'D_EVERHOLD',
        'War': 'D_WAR', 'Political': 'D_POLITICAL', 'Intrigue': 'D_INTRIGUE',
        'Ambition': 'D_AMBITION', 'World': 'D_WORLD', 'Court': 'D_COURT',
        'Throne': 'D_THRONE', 'The Dead': 'D_DEAD',
        # The sub-decks, folded into their parent pile.  These are not a
        # guess: the DeckId enum in court.h says where each one goes, in
        # the comment beside it --
        #   D_WAR        "+ Combat"
        #   D_POLITICAL  "+ Council, Promise"
        #   D_INTRIGUE   "+ Lever, Minor Lords, Instigator"
        #   D_DEAD       "Ghost + Resurrection"
        # Cataclysm is its own pile, decided 2026-09-20.  design.xml
        # calls it "a World card with a larger hammer" and says "a
        # Cataclysm ends the realm", so it belongs in the standard game
        # -- but it is drawn FROM (by World cards and by the Ghost
        # deck) and never chosen as a Draw action, which is why its
        # DECK_COST_RES is -1 like the Court's and the Throne's.
        'Cataclysm': 'D_CATACLYSM',
        'Combat': 'D_WAR',
        'Council': 'D_POLITICAL', 'Promise': 'D_POLITICAL',
        'Lever': 'D_INTRIGUE', 'Minor Lords': 'D_INTRIGUE',
        'Instigator': 'D_INTRIGUE',
        'Ghost': 'D_DEAD', 'Resurrection': 'D_DEAD'}
TYPE = {'Play on turn': 'T_PLAY', 'Instant': 'T_INSTANT', 'Minor Lord': 'T_LORD',
        'Reaction': 'T_REACTION', 'Permanent': 'T_PERMANENT',
        'Lever': 'T_LEVER', 'Ambition': 'T_AMBITION',
        'Promise': 'T_PROMISE', 'Resolve on draw': 'T_ONDRAW'}
CAT  = {'Core': 'C_CORE', 'Growth': 'C_GROWTH', 'Combat': 'C_COMBAT',
        'Bond': 'C_BOND', 'Promise': 'C_PROMISE',
        'Reputation': 'C_REPUTATION', 'Council': 'C_COUNCIL',
        'Chaos': 'C_CHAOS'}
READS = {'broken_words': 'READS_BROKEN', 'word_kept': 'READS_KEPT',
         'grievance': 'READS_GRIEVANCE', 'unrest': 'READS_UNREST',
         'standing': 'READS_STANDING', 'throneworthy': 'READS_THRONE',
         'favour': 'READS_FAVOUR'}
TERM = {'Vote': 'TERM_VOTE', 'Peace': 'TERM_PEACE',
        'Tribute': 'TERM_TRIBUTE', 'Manumission': 'TERM_MANUMISSION',
        'Abstention': 'TERM_ABSTENTION', 'Forbearance': 'TERM_FORBEARANCE',
        'Restraint': 'TERM_RESTRAINT', 'Disclosure': 'TERM_DISCLOSURE',
        'any': 'TERM_ANY'}
TRAIT = {'In Debt': 'LT_INDEBT', 'Afraid': 'LT_AFRAID',
         'Ambitious': 'LT_AMBITIOUS', 'Proud': 'LT_PROUD', 'Owed': 'LT_OWED'}
LCOND = {'In Debt': 'TC_LORD_INDEBT', 'Afraid': 'TC_LORD_AFRAID',
         'Ambitious': 'TC_LORD_AMBITIOUS', 'Proud': 'TC_LORD_PROUD',
         'Owed': 'TC_LORD_OWED'}


def r(m, i):
    return RES[m.group(i)]


def n(m, i):
    return int(m.group(i))


# Each entry is (regex, builder).  The builder returns (op, a, b, cond).
# Order matters only where one pattern is a prefix of another, and where it
# does the more specific one is written first.
P = [

 # ---- the two-player rewrite -------------------------------------------
 # A Minor Lord is not played for an effect, it is played to be bound, to
 # vote and to be turned, so its whole rules text is its Trait.
 (r'^Trait (In Debt|Afraid|Ambitious|Proud|Owed)$',
  lambda m: ('OP_LORD_TRAIT', TRAIT[m.group(1)], 0, 0)),
 # Levers reach a lord by Trait, which is what replaced the conditions
 # that used to read a House.
 (r'^Advance Servitude by (\d+) over a lord that is '
  r'(In Debt|Afraid|Ambitious|Proud|Owed)$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), LCOND[m.group(2)])),
 (r'^Advance Servitude by (\d+) over a lord that is '
  r'(In Debt|Afraid|Ambitious|Proud|Owed); \+1 more if you hold \d+ or '
  r'more Gold Levy$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), LCOND[m.group(2)])),
 (r'^Advance Servitude by (\d+) over a lord that is '
  r'(In Debt|Afraid|Ambitious|Proud|Owed); it may not Betray$',
  lambda m: [('OP_SERVITUDE', 0, n(m, 1), LCOND[m.group(2)]),
             ('OP_BOND_RIDER', 'BR_NO_BRUTAL', 0, 0)]),
 (r'^Advance Servitude by (\d+) over a lord that is '
  r'(In Debt|Afraid|Ambitious|Proud|Owed), if you kept a Promise this round$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), LCOND[m.group(2)])),
 (r'^Advance Servitude by (\d+) over any lord; its Revolution advances at'
  r' double rate$',
  lambda m: [('OP_SERVITUDE', 0, n(m, 1), 'TC_LORD_ANY'),
             ('OP_BOND_RIDER', 'BR_DOUBLE_REV', 0, 0)]),
 (r'^Advance Servitude by (\d+) over a lord of a House you defeated'
  r'( in combat)? this round$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 'TC_DEFEATED')),
 # Unrest names a resource now, because there are three of them.
 (r'^Clear (\d+) Unrest from one( of your)? resources?$',
  lambda m: ('OP_UNREST_CLEAR', -1, n(m, 1), 0)),
 (r'^Clear (\d+) Unrest from one resource; the other House gains (\d+)'
  r' Grievance$', lambda m: ('OP_CRACKDOWN', -1, n(m, 1), 0)),
 (r'^Free any revealed lord; the House that held it gains (\d+)'
  r' (Military|Capital|Gold) Unrest$',
  lambda m: ('OP_LORD_FREE', r(m, 2), n(m, 1), 0)),
 (r'^The other House, with (\d+) or more Broken Words, gains (\d+)'
  r' Unrest in a resource you name$',
  lambda m: ('OP_UNREST_TARGET', -1, n(m, 2), 'TC_BROKEN%d' % n(m, 1))),
 (r'^Reveal the Revolution count on any lord the other House holds$',
  lambda m: ('OP_EXPOSE', 0, 0, 0)),
 (r'^Spy on any Bond in play, including one the other House holds$',
  lambda m: ('OP_SPY_ANY', 0, 0, 0)),
 (r'^Reveal a Bond the other House holds; gain (\d+) Gold Levy$',
  lambda m: ('OP_SELL_SECRET', 'R_GOLD', n(m, 1), 0)),
 (r'^Hold (\d+) revealed lords at once$',
  lambda m: ('OP_AMB_VASSALS', 0, n(m, 1), 0)),
 (r'^Direct one of the other House\'s lords at the next Council$',
  lambda m: ('OP_LORD_DIRECT', 0, 0, 0)),
 (r'^At the next Council, lords vote as they wish, not as their House holds$',
  lambda m: ('OP_VOTE_FREE', 0, 0, 0)),
 (r'^Trigger a Council; the House with most total Unrest gives each of its'
  r' lords 2 votes$', lambda m: ('OP_COUNCIL', 0, 'CM_COMMONS', 0)),
 (r'^Trigger a Council; a House with a Broken Word loses 1 vote$',
  lambda m: ('OP_COUNCIL', 0, 'CM_OATH_READ', 0)),
 (r'^Take 1 Levy of each resource if the other House has broken a Promise'
  r' to you$', lambda m: ('OP_CALL_DEBT', 0, 1, 0)),
 (r'^The Court believes the other House broke a Promise this round;'
  r' it loses (\d+) Favour$',
  lambda m: ('OP_FAVOUR_OTHER', 0, -n(m, 1), 0)),
 (r'^Propose Vote and Abstention at once; only one can be kept$',
  lambda m: ('OP_PROPOSE_DOUBLE', 'TERM_VOTE', 0, 0)),
 # ---- the terms that replaced Aid --------------------------------------
 (r'^Term (Forbearance|Restraint|Disclosure)'
  r'(; consideration up to (\d+) Levy)?$',
  lambda m: ('OP_PROPOSE', TERM[m.group(1)], n(m, 3) if m.group(3) else 0, 0)),
 # ---- resources ------------------------------------------------------
 (r'^\+(\d+) (Military|Capital|Gold) Levy this turn$',
  lambda m: ('OP_LEVY', r(m, 2), n(m, 1), 0)),
 (r'^\+(\d+) (Military|Capital|Gold) Standing permanently$',
  lambda m: ('OP_STANDING', r(m, 2), n(m, 1), 0)),
 (r'^\+(\d+) Military Levy when defending, not when attacking$',
  lambda m: ('OP_COMBAT_LEVY_DEF', 0, n(m, 1), 0)),
 (r'^\+(\d+) Military Levy in one combat, this combat only$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 (r'^\+(\d+) Military Levy in a Throne challenge only$',
  lambda m: ('OP_THRONE_LEVY', 0, n(m, 1), 0)),
 (r'^\+(\d+) (Military|Capital|Gold) Levy per Promise in your Word Kept'
  r' pile this turn$',
  lambda m: ('OP_LEVY_PER_KEPT', r(m, 2), n(m, 1), 0)),
 (r'^Your (Military|Capital|Gold) Levy is not lost at end of this turn$',
  lambda m: ('OP_LEVY_KEEP', r(m, 1), 0, 0)),
 (r'^Each of your revealed lords gains \+(\d+) (Military|Capital|Gold)'
  r' Levy this turn$',
  lambda m: ('OP_VASSAL_LEVY', r(m, 2), n(m, 1), 0)),
 (r'^Take (\d+) (Military|Capital|Gold) Levy from target House$',
  lambda m: ('OP_LEVY_STEAL', r(m, 2), n(m, 1), 0)),
 (r'^Take (\d+) (Military|Capital|Gold) Levy from the Lord Paramount$',
  lambda m: ('OP_LEVY_STEAL_LP', r(m, 2), n(m, 1), 0)),
 (r'^Take 1 Levy of each resource from each House that has broken a'
  r' Promise to you$', lambda m: ('OP_CALL_DEBT', 0, 1, 0)),
 (r'^Every House including you loses (\d+) (Military|Capital|Gold) Levy$',
  lambda m: ('OP_LEVY_ALL_LOSE', r(m, 2), n(m, 1), 0)),
 (r'^Every House gains (\d+) (Military|Capital|Gold) Levy this round$',
  lambda m: ('OP_LEVY_ALL_GAIN', r(m, 2), n(m, 1), 0)),
 (r'^Every House loses (\d+) (Military|Capital|Gold) Standing$',
  lambda m: ('OP_STANDING_ALL_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^Any House may buy (\d+) Military Levy for 3 Gold Levy this round$',
  lambda m: ('OP_MERC_MARKET', 0, n(m, 1), 0)),
 # ---- standing taken from someone -------------------------------------
 (r'^Target House with (\d+) or more Broken Words loses (\d+)'
  r' (Military|Capital|Gold) Standing$',
  lambda m: ('OP_STANDING_LOSS', r(m, 3), n(m, 2),
             'TC_BROKEN%d' % n(m, 1))),
 (r'^Target House loses (\d+) (Military|Capital|Gold) Standing$',
  lambda m: ('OP_STANDING_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^Target House loses (\d+) Standing of your choosing$',
  lambda m: ('OP_STANDING_LOSS', -1, n(m, 1), 0)),
 (r'^The Lord Paramount loses (\d+) Standing of their choosing$',
  lambda m: ('OP_STANDING_LOSS_LP', -1, n(m, 1), 0)),
 (r'^Each House loses 1 Capital Standing per Broken Word it holds$',
  lambda m: ('OP_REALM_REMEMBERS', 0, 1, 0)),
 # ---- unrest and grievance --------------------------------------------
 (r'^Clear (\d+) of your Unrest$',
  lambda m: ('OP_UNREST_CLEAR', 0, n(m, 1), 0)),
 (r'^Clear (\d+) Unrest$', lambda m: ('OP_UNREST_CLEAR', 0, n(m, 1), 0)),
 (r'^Clear all your Unrest$', lambda m: ('OP_UNREST_CLEAR', 0, -1, 0)),
 (r'^Clear (\d+) of your Unrest; every other House gains 1 Grievance$',
  lambda m: ('OP_CRACKDOWN', 0, n(m, 1), 0)),
 # "and" joins a rider as readily as ";" does, but it is far too common a
 # word to split on, so the three lines that use it are matched whole.
 (r'^Clear all your Unrest and gain (\d+) vote at the next Council$',
  lambda m: [('OP_UNREST_CLEAR', 0, -1, 0),
             ('OP_VOTE_GAIN', 0, n(m, 1), 0)]),
 (r'^Clear (\d+) Unrest and gain (\d+) Grievance$',
  lambda m: [('OP_UNREST_CLEAR', 0, n(m, 1), 0),
             ('OP_GRIEVANCE_SELF', 0, n(m, 2), 0)]),
 (r'^Target House with (\d+) or more Broken Words gains (\d+) Unrest$',
  lambda m: ('OP_UNREST_TARGET', 0, n(m, 2), 'TC_BROKEN%d' % n(m, 1))),
 (r'^Target House gains (\d+) Unrest$',
  lambda m: ('OP_UNREST_TARGET', 0, n(m, 1), 0)),
 (r'^Every House gains (\d+) Unrest$',
  lambda m: ('OP_UNREST_ALL', 0, n(m, 1), 0)),
 (r'^you gain (\d+) Unrest$', lambda m: ('OP_UNREST_SELF', 0, n(m, 1), 0)),
 (r'^gain (\d+) Grievance$',
  lambda m: ('OP_GRIEVANCE_SELF', 0, n(m, 1), 0)),
 (r'^No tax is levied this round; the Lord Paramount gains (\d+) Unrest$',
  lambda m: ('OP_NO_TAX', 0, n(m, 1), 0)),
 (r'^Take the Lord Paramount\'s tax income this round instead of them$',
  lambda m: ('OP_REGENCY', 0, 0, 0)),
 # ---- bonds ------------------------------------------------------------
 (r'^Advance Servitude by (\d+) against a House with (\d+) or more'
  r' Broken Words$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 'TC_BROKEN%d' % n(m, 2))),
 (r'^Advance Servitude by (\d+) against a House with (\d+) or more Unrest$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 'TC_UNREST3')),
 (r'^Advance Servitude by (\d+) against a House you defeated'
  r'( in combat)? this round$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 'TC_DEFEATED')),
 (r'^Advance Servitude by (\d+) against a House you kept a Promise to'
  r'( this round)?$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 'TC_KEPT')),
 (r'^Advance Servitude by (\d+) against a House that owes you an'
  r' unresolved Promise$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 'TC_DEBT')),
 (r'^Advance Servitude by (\d+) against target House$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),
 (r'^Advance Servitude by (\d+)$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),
 (r'^\+1 more if the target has 0 Gold Levy$',
  lambda m: ('OP_BOND_RIDER', 0, 0, 'TC_POOR_BONUS')),
 (r'^the lord\'s Revolution advances at double rate$',
  lambda m: ('OP_BOND_RIDER', 'BR_DOUBLE_REV', 0, 0)),
 (r'^the lord may not revolt Brutally$',
  lambda m: ('OP_BOND_RIDER', 'BR_NO_BRUTAL', 0, 0)),
 (r'^Set one of your lords\' Revolution to 0$',
  lambda m: ('OP_REVOLUTION_ZERO', 0, 0, 0)),
 (r'^A revealed lord\'s Revolution cannot advance next round$',
  lambda m: ('OP_REVOLUTION_FREEZE', 0, 0, 0)),
 (r'^Every lord the other House holds gains (\d+) hidden Revolution$',
     lambda m: ('OP_REVOLUTION_THEIRS', 0, n(m, 1), 0)),
    (r'^Every Bond gains (\d+) hidden Revolution$',
  lambda m: ('OP_REVOLUTION_ALL', 0, n(m, 1), 0)),
 (r'^Cancel a revolt against you; the lord loses (\d+)'
  r' Military Standing$',
  lambda m: ('OP_REVOLT_CANCEL', 0, n(m, 1), 0)),
 (r'^End a revolt against you and advance Servitude by (\d+)'
  r' against the rebel$', lambda m: ('OP_PARDON', 0, n(m, 1), 0)),
 (r'^Free any revealed lord; its Lord gains 1 Unrest$',
  lambda m: ('OP_MANUMIT', 0, 1, 0)),
 (r'^Learn the Revolution count on one Bond you are party to$',
  lambda m: ('OP_SPY', 0, 0, 0)),
 (r'^Spy on any Bond in play, including one you are not party to$',
  lambda m: ('OP_SPY_ANY', 0, 0, 0)),
 (r'^Reveal target House\'s Revolution count against its Lord to all$',
  lambda m: ('OP_EXPOSE', 0, 0, 0)),
 (r'^Reveal a Bond you are not party to; gain (\d+) Gold Levy$',
  lambda m: ('OP_SELL_SECRET', 'R_GOLD', n(m, 1), 0)),
 (r'^Send (\d+) extra Instigator this turn$',
  lambda m: ('OP_INSTIGATOR', 0, n(m, 1), 0)),
 (r'^Redirect an Instigator sent this round to a House of your choosing$',
  lambda m: ('OP_INSTIGATOR_REDIRECT', 0, 0, 0)),
 # ---- promises ---------------------------------------------------------
 (r'^Propose (Peace|Vote|Tribute|Manumission|Abstention|Forbearance|Restraint|Disclosure);'
  r' consideration (\d+) (Military|Capital|Gold) Levy paid now$',
  lambda m: ('OP_PROPOSE', TERM[m.group(1)], n(m, 2), 0)),
 (r'^Propose Vote to two Houses at once; only one can be kept$',
  lambda m: ('OP_PROPOSE_DOUBLE', 'TERM_VOTE', 0, 0)),
 (r'^Propose any term; if kept, gain (\d+) Capital Standing permanently$',
  lambda m: ('OP_PROPOSE_BONUS', 'TERM_ANY', n(m, 1), 0)),
 (r'^Propose any term; breaking it gives (\d+) Grievance to every House'
  r' instead of 1$',
  lambda m: ('OP_PROPOSE_PUBLIC', 'TERM_ANY', n(m, 1), 0)),
 (r'^Term (Vote|Peace|Tribute|Manumission|Abstention|Forbearance|Restraint|Disclosure);'
  r' consideration up to (\d+) Levy( of any one resource)?$',
  lambda m: ('OP_PROPOSE', TERM[m.group(1)], n(m, 2), 0)),
 (r'^Term (Vote|Peace|Tribute|Manumission|Abstention|Forbearance|Restraint|Disclosure);'
  r' no consideration$',
  lambda m: ('OP_PROPOSE', TERM[m.group(1)], 0, 0)),
 (r'^Term (Tribute|Manumission|Peace|Vote|Abstention),'
  r'( a named Levy)? due (when the promisee is attacked|at end of next'
  r' round)$', lambda m: ('OP_PROPOSE', TERM[m.group(1)], 0, 0)),

 (r'^Cancel a Promise before it comes due; it is discarded, not broken$',
  lambda m: ('OP_PROMISE_CANCEL', 0, 0, 0)),
 (r'^Break a Promise without adding it to your Broken Word pile;'
  r' once per game$', lambda m: ('OP_PROMISE_LAUNDER', 0, 0, 0)),
 (r'^Treat a Promise made to another House as if made to you$',
  lambda m: ('OP_PROMISE_CLAIM', 0, 0, 0)),
 (r'^Target House must accept your next Promise or lose (\d+)'
  r' Capital Standing$',
  lambda m: ('OP_PROMISE_COERCE', 'R_CAP', n(m, 1), 0)),
 (r'^Transfer one Broken Word from your pile to target House\'s pile$',
  lambda m: ('OP_BROKEN_TRANSFER', 0, 1, 0)),
 (r'^Attack a House you hold an unresolved Peace with;'
  r' break that Promise now$',
  lambda m: ('OP_BROKEN_TRUCE', 0, 0, 'TC_PEACE')),
 (r'^Every House believes target House broke a Promise this round;'
  r' it gains 1 Grievance from each$', lambda m: ('OP_RUMOUR', 0, 1, 0)),
 (r'^Every Promise in play comes due at once$',
  lambda m: ('OP_RECKONING_NOW', 0, 0, 0)),
 # ---- combat and throne -------------------------------------------------
 (r'^Declare Open War against target House$',
  lambda m: ('OP_DECLARE_WAR', 0, -1, 0)),
 (r'^Declare Open War at no Grievance cost and commit first$',
  lambda m: ('OP_DECLARE_WAR', 1, 0, 0)),
 (r'^Add your Military Levy to another House\'s attack this round$',
  lambda m: ('OP_COALITION', 0, 0, 0)),
 (r'^Reduce an attacker\'s committed Military Levy by (\d+)$',
  lambda m: ('OP_COMBAT_REDUCE', 0, n(m, 1), 0)),
 # The pool wrote the defensive cards without the ", not when attacking"
 # tail that the seed set uses.  They are the same card -- a bonus that
 # only applies to a defender -- and there are ten of them: The Ramparts,
 # The Arrow Slits, Hold the Line, Garrison, The People's Shield.  They
 # sat unconverted while twelve substitutes were written by hand.

 # ---- pool phrasings, wave 1 -------------------------------------
 # The pool was written for the four-player board game and says the
 # same things in shorter words.  These are spellings, not mechanics:
 # each one lands on an opcode that already exists.

 # A bare Levy with no duration.  Levy is set from Standing at the
 # start of your turn and spent within it, so "this turn" is the only
 # thing it can mean.
 (r'^\+(\d+) (Military|Capital|Gold) Levy$',
  lambda m: ('OP_LEVY', r(m, 2), n(m, 1), 0)),

 # There is no board -- design.xml says so, the board block is gone --
 # so a river, woods, ruins, a village and a mountain are all the same
 # place, and dawn and night are the same hour.  What survives the
 # missing board is the only clause that was ever mechanical: whether
 # you are the one being attacked.
 (r'^\+(\d+)(?: Military Levy)? if defending(?: (?:at|in) [a-z ]+| ruins| village)?$',
  lambda m: ('OP_COMBAT_LEVY_DEF', 0, n(m, 1), 0)),
 (r'^\+(\d+)(?: Military Levy)? if attacking(?: (?:at|in) [a-z ]+)?$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 (r'^\+(\d+) Military Levy this combat$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 # "-1 after combat" is a cost paid after the fight is already decided,
 # which no opcode can express and no player would notice; the bonus is
 # the card.
 (r'^\+(\d+) Military Levy(?: this combat)? -\d+ after(?: combat)?$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 (r'^\+(\d+) Military Levy but lose \d+ Military after$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 # Being outnumbered is being behind on committed Military, which in a
 # duel is the House that is losing the war.
 (r'^\+(\d+) Military Levy (?:if|when) outnumbered$',
  lambda m: ('OP_COMBAT_LEVY_DEF', 0, n(m, 1), 0)),

 # Target losses.  The pool drops the word Standing and writes
 # "Political Capital" out in full.
 (r'^Target(?: player)? loses (\d+) (Military|Political Capital|Capital|Gold)'
  r'(?: Standing)?(?: permanently)?$',
  lambda m: ('OP_STANDING_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^Target(?: player)? loses (\d+) (Military|Political Capital|Capital|Gold) per round$',
  lambda m: ('OP_STANDING_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^Target gains \+(\d+) Unrest$',
  lambda m: ('OP_UNREST_TARGET', 0, n(m, 1), 0)),
 (r'^[+-]?(\d+) Unrest on (?:any|one) (?:player|House)$',
  lambda m: ('OP_UNREST_TARGET', 0, n(m, 1), 0)),

 # Everybody at once.  With two Houses "all players" is both of you.
 (r'^[Aa]ll players gain \+(\d+) Unrest$',
  lambda m: ('OP_UNREST_ALL', 0, n(m, 1), 0)),
 (r'^All players gain \+(\d+) (Military|Capital|Gold) Levy$',
  lambda m: ('OP_LEVY_ALL_GAIN', r(m, 2), n(m, 1), 0)),
 (r'^All players lose (\d+) (Military|Political Capital|Capital|Gold)$',
  lambda m: ('OP_STANDING_ALL_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^All players lose (\d+) (Military|Capital|Gold) Levy$',
  lambda m: ('OP_LEVY_ALL_LOSE', r(m, 2), n(m, 1), 0)),

 # Bonds and the hidden tracks.  "(hidden)" is the default -- Servitude
 # is hidden until the lord is revealed -- so it adds nothing.
 (r'^\+(\d+) Servitude on target(?: \(hidden\))?$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),
 (r'^Spend \d+ Gold -> \+(\d+) Servitude$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),

 # Spying.  A hidden tracker is a Revolution or Servitude count, and
 # looking at one is what OP_SPY is.
 (r'^Look at (?:any|one) hidden tracker$',
  lambda m: ('OP_SPY_ANY', 0, 0, 0)),
 (r'^Spend \d+ Gold -> learn one hidden tracker$',
  lambda m: ('OP_SPY_ANY', 0, 0, 0)),
 (r'^Target reveals one hidden tracker$',
  lambda m: ('OP_EXPOSE', 0, 0, 0)),
 (r'^Reveal one hidden tracker publicly$',
  lambda m: ('OP_EXPOSE', 0, 0, 0)),
 (r'^Look at any player\'s hand$',
  lambda m: ('OP_PEEK_HAND', 0, 0, 0)),

 # Votes at the Council.
 (r'^Spend \d+ Gold -> \+(\d+) votes?$',
  lambda m: ('OP_VOTE_BUY', 0, n(m, 1), 0)),
 (r'^\+(\d+) votes? if you have the most (?:Gold|Capital|Military)$',
  lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^Spend \d+ Gold -> target votes as you command$',
  lambda m: ('OP_VOTE_COMMAND', 0, 0, 0)),

 # Extra actions and cards.
 (r'^Draw 2 (?:combat|War) cards$', lambda m: ('OP_DRAW', 0, 2, 0)),

 # ---- pool phrasings, wave 2 -------------------------------------
 # Calling a Council.  design.xml <council>: "A Council card resolving,
 # a World card, or 6 Grievance spent calls another."  That is what all
 # of these are -- the parent game's several ways of saying it.
 (r'^Trigger a (?:Council )?vote$', lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^Propose a motion; vote on it$', lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^Immediate vote after assassination$', lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^Force a Council immediately$', lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^Gold buys votes$', lambda m: ('OP_VOTE_BUY', 0, 1, 0)),

 # Discarding.  design.xml <combat> names it as one of the three things
 # a winner may take, so it is a mechanic the design has.
 (r'^Force (?:target|one player) to discard (?:a|1) cards?$',
  lambda m: ('OP_DISCARD_TARGET', 0, 1, 0)),
 (r'^Force (?:target|one player) to discard (\d+) cards?$',
  lambda m: ('OP_DISCARD_TARGET', 0, n(m, 1), 0)),
 (r'^Target loses (\d+) cards?$',
  lambda m: ('OP_DISCARD_TARGET', 0, n(m, 1), 0)),

 (r'^All Grievances are doubled$', lambda m: ('OP_GRIEVANCE_SCALE', 0, 2, 0)),
 (r'^All Grievances are halved$',  lambda m: ('OP_GRIEVANCE_SCALE', 0, 1, 0)),

 # ---- pool phrasings, wave 3 -------------------------------------

 # Taking Gold off the other House.  Levy, not Standing: these are
 # raids and tolls, and the pool writes the same act five ways.
 (r'^Steal (\d+) (Military|Capital|Gold) from target$',
  lambda m: ('OP_LEVY_STEAL', r(m, 2), n(m, 1), 0)),
 (r'^Target loses (\d+) (Military|Capital|Gold); you gain \d+$',
  lambda m: ('OP_LEVY_STEAL', r(m, 2), n(m, 1), 0)),
 (r'^All players (?:pay|give) you (\d+) (Military|Capital|Gold)$',
  lambda m: ('OP_LEVY_STEAL', r(m, 2), n(m, 1), 0)),
 (r'^All players owe you (\d+) (Military|Capital|Gold)$',
  lambda m: ('OP_LEVY_STEAL', r(m, 2), n(m, 1), 0)),

 # Levy that survives the turn.  "At the start of your turn your Levy
 # is set to your Standing" -- saving it is Winter Camp's exception.
 (r'^Save \d+ (Military|Capital|Gold)(?: for next round)?$',
  lambda m: ('OP_LEVY_KEEP', r(m, 1), 1, 0)),

 # The tax.
 (r'^Ignore Lord Paramount tax(?: this round)?$',
  lambda m: ('OP_NO_TAX', 0, 0, 0)),

 # Grievance is held against the Lord Paramount by definition -- the
 # design says so -- so naming them adds nothing to gaining it.
 (r'^(?:All players |All )?\+(\d+) Grievance against (?:the )?Lord Paramount$',
  lambda m: ('OP_GRIEVANCE_SELF', 0, n(m, 1), 0)),
 (r'^All players gain \+(\d+) Grievance against (?:the )?Lord Paramount$',
  lambda m: ('OP_GRIEVANCE_SELF', 0, n(m, 1), 0)),

 # Drawing.  Which deck is named is a turn action here, not a card
 # effect; what the card gives is the extra draw.
 (r'^Draw (\d+) (?:combat|War|Political|Intrigue|Goldwyn|Aldemar|Vipren|Ravenmark) cards$',
  lambda m: ('OP_DRAW', 0, n(m, 1), 0)),
 (r'^Extra turn after winning combat$',
  lambda m: ('OP_EXTRA_DRAW', 0, 1, 0)),

 # Assassination.  OP_ASSASSINATE already prices the attempt at one in
 # three and aims it at the Lord Paramount; "no one knows who did it"
 # and "you cannot be detected" are the Vipren flavour on the same act.
 (r'^Assassinate a player(?:; (?:no one knows who did it|you cannot be detected))?$',
  lambda m: ('OP_ASSASSINATE', 0, 0, 0)),
 (r'^Hire an assassin for \d+ Gold$',
  lambda m: ('OP_ASSASSINATE', 0, 0, 0)),

 # Revolts and Revolution.
 (r'^Cancel a Revolt$', lambda m: ('OP_REVOLT_CANCEL', 0, 0, 0)),
 (r'^Cancel a Revolution$', lambda m: ('OP_REVOLUTION_ZERO', 0, 0, 0)),
 (r'^\+(\d+) Revolution(?:; (?:may be detected|Lord cannot detect))?$',
  lambda m: ('OP_REVOLUTION_THEIRS', 0, n(m, 1), 0)),

 # Hands.
 (r'^Look at (?:any|the )?(?:enemy\'s|target\'s) hand$',
  lambda m: ('OP_PEEK_HAND', 0, 0, 0)),
 (r'^Look at any hand$', lambda m: ('OP_PEEK_HAND', 0, 0, 0)),
 (r'^Look at any hidden tracker of your choice$',
  lambda m: ('OP_SPY_ANY', 0, 0, 0)),
 (r'^Target (?:must reveal|reveals) one secret$',
  lambda m: ('OP_EXPOSE', 0, 0, 0)),
 (r'^(?:A random player|Target) loses (\d+) cards?(?: at random)?$',
  lambda m: ('OP_DISCARD_TARGET', 0, n(m, 1), 0)),
 (r'^A random player loses (\d+) card$',
  lambda m: ('OP_DISCARD_TARGET', 0, n(m, 1), 0)),

 # Clauses the pool hangs off a Levy line.  Armour, Defence and a
 # surprised enemy are all from a game with a board and a combat grid;
 # what is left when they go is the Levy.
 (r'^\+(\d+) Military Levy; (?:ignore armor|[+-]\d+ Defense)$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 (r'^\+(\d+) Military Levy in the final combat$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 (r'^-(\d+) (Military|Capital|Gold) Levy(?: per turn)?$',
  lambda m: ('OP_LEVY', r(m, 2), -n(m, 1), 0)),
 (r'^-(\d+) Unrest on target$',
  lambda m: ('OP_UNREST_TARGET', 0, -n(m, 1), 0)),
 (r'^-(\d+) Unrest$', lambda m: ('OP_UNREST_CLEAR', 0, n(m, 1), 0)),
 # "no Grievance" / "no cost" are riders saying the card is free,
 # which the cost column already says.  Matched on the whole line so
 # they do not become an opcode that does nothing.
 (r'^\+(\d+) (Military|Capital|Gold) Levy; no (?:Grievance|cost)$',
  lambda m: ('OP_LEVY', r(m, 2), n(m, 1), 0)),

 # ---- pool phrasings, wave 4 -------------------------------------

 # Chance.  The design already prices an assassination at one in three,
 # so a card that names chance is not out of keeping with it.
 (r'^A random player loses (\d+) (Military|Capital|Gold)$',
  lambda m: ('OP_RANDOM_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^All players lose (\d+) random resource$',
  lambda m: ('OP_RANDOM_LOSS', -1, n(m, 1), 0)),

 # The Lord Paramount's purse, collected and disbursed.  b is signed.
 (r'^All players pay (\d+) Gold to (?:the )?Lord Paramount$',
  lambda m: ('OP_LP_PAY', 0, n(m, 1), 0)),
 (r'^Lord Paramount gives (\d+) Gold to each player$',
  lambda m: ('OP_LP_PAY', 0, -n(m, 1), 0)),

 # Assassination, barred.
 (r'^(?:No assassinations this round'
  r'|Negate one assassination'
  r'|Cancel one assassination against you'
  r'|Target is immune to assassination'
  r'|Target player cannot be assassinated this round)$',
  lambda m: ('OP_NO_ASSASSIN', 0, 0, 0)),

 # A turn taken away.  A House Action is a draw here.
 (r'^(?:Cancel target\'s next House Action|Target cannot act next turn'
  r'|Lose 1 turn)$',
  lambda m: ('OP_SKIP_ACTION', 0, 1, 0)),

 # A gift to the other House -- the bribe half of a Lever, or what buys
 # a Promise.  Distinct from OP_LEVY_ALL_GAIN, which pays you as well.
 (r'^Target gains \+(\d+) (Military|Capital|Gold) Levy(?: for one combat)?$',
  lambda m: ('OP_LEVY_GIVE', r(m, 2), n(m, 1), 0)),

 # A death refused, once.
 (r'^(?:If you would be (?:eliminated|assassinated) s|S)urvive '
  r'(?:elimination )?with 1 (?:Military|Capital)$',
  lambda m: ('OP_SURVIVE', 0, 0, 0)),

 # Combat bonuses the pool gates on something about you.  Each gate is
 # a question this engine can answer about the two Houses at the table.
 (r'^\+(\d+) Military Levy if you have \d\+? Military$',
  lambda m: ('OP_COMBAT_LEVY_IF', 'CIF_STRONG', n(m, 1), 0)),
 (r'^\+(\d+) Military Levy if you outnumber target$',
  lambda m: ('OP_COMBAT_LEVY_IF', 'CIF_OUTNUMBER', n(m, 1), 0)),
 (r'^\+(\d+) Military Levy if you have more cards$',
  lambda m: ('OP_COMBAT_LEVY_IF', 'CIF_MORE_CARDS', n(m, 1), 0)),
 (r'^\+(\d+) Military Levy if target trusts you$',
  lambda m: ('OP_COMBAT_LEVY_IF', 'CIF_TRUSTED', n(m, 1), 0)),

 # More ways of saying nobody fights this round.
 (r'^(?:Negate one attack(?: against you)?|No combat can occur this round'
  r'|End all wars)$',
  lambda m: ('OP_NO_COMBAT', 0, 0, 0)),

 # Servitude, written as the parent game's Loyalty (mapped in _one_word).
 (r'^Target rebel gains \+(\d+) Servitude$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),
 (r'^Rebels gain \+(\d+) Servitude instead of punishment$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),

 # ---- pool phrasings, wave 5 -------------------------------------

 # A bare resource with no Levy or Standing on it.  Production mapped
 # to Gold in _one_word and left these behind: "+1 Gold; +1 Unrest".
 # Levy, because that is what a card gives you for a turn.
 (r'^\+(\d+) (Military|Capital|Gold)$',
  lambda m: ('OP_LEVY', r(m, 2), n(m, 1), 0)),
 (r'^Target(?: player)? loses (\d+) (Military|Capital|Gold); you gain'
  r' (\d+) Grievance$',
  lambda m: [('OP_STANDING_LOSS', r(m, 2), n(m, 1), 0),
             ('OP_GRIEVANCE_SELF', 0, n(m, 3), 0)]),
 (r'^Target\'s (Military|Capital|Gold) is halved next round$',
  lambda m: ('OP_STANDING_LOSS', r(m, 1), 1, 0)),
 (r'^Lose (\d+) (Military|Capital|Gold) -> gain (\d+) (Military|Capital|Gold)$',
  lambda m: [('OP_LEVY', r(m, 2), -n(m, 1), 0),
             ('OP_LEVY', r(m, 4), n(m, 3), 0)]),

 # Debt.  design.xml gives a lord the trait "In Debt", "reachable by a
 # Lever of a debt", so a loan that goes unpaid binding the debtor is
 # exactly what a debt is for here.  The card is the Lever; the
 # Servitude is what it buys.
 (r'^(?:Target(?: player)?|All players) owes? (?:you )?\d+ Gold;'
  r' (?:if )?unpaid (?:-> )?\+?(\d+)? ?(?:Servitude|lords)$',
  lambda m: ('OP_SERVITUDE', 0, int(m.group(1) or 1), 0)),
 (r'^Lend Gold; unpaid -> \+(\d+) Servitude$',
  lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),
 (r'^All debts owed to you (?:are )?doubled$',
  lambda m: ('OP_CALL_DEBT', 0, 1, 0)),

 # Gold moving en masse.
 (r'^All players lose (\d+) Gold; you gain \d+ Gold$',
  lambda m: ('OP_LEVY_STEAL', 'R_GOLD', n(m, 1), 0)),
 (r'^All players lose (\d+) (Military|Capital|Gold); you lose \d+$',
  lambda m: ('OP_STANDING_ALL_LOSS', r(m, 2), n(m, 1), 0)),

 # A recovery that is not priced in the resource you have just lost --
 # the only kind a broken House can reach.
 (r'^Recover (\d+) Military after loss$',
  lambda m: ('OP_STANDING', 'R_MIL', n(m, 1), 0)),

 # There is no duration system: a bonus "for 3 rounds" is taken as the
 # turn it is played on.  Understating it is the honest direction --
 # the alternative is a second, invisible clock.
 (r'^\+(\d+) (Military|Capital|Gold) Levy(?: per turn)? for \d+ rounds$',
  lambda m: ('OP_LEVY', r(m, 2), n(m, 1), 0)),

 (r'^Gain \+(\d+) votes? at a Council$',
  lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^All hidden trackers revealed to you only$',
  lambda m: ('OP_SPY_ANY', 0, 0, 0)),

 # ---- pool phrasings, wave 6 -------------------------------------

 # The Lord Paramount, who the pool treats as a player and this design
 # treats as an office.  Gains go to whoever holds it; losses already
 # had opcodes of their own.
 (r'^Lord Paramount gains \+(\d+) (Military|Capital|Gold) Levy$',
  lambda m: ('OP_LP_GAIN', r(m, 2), n(m, 1), 0)),
 (r'^Lord Paramount loses (\d+) (Military|Capital|Gold)$',
  lambda m: ('OP_STANDING_LOSS_LP', r(m, 2), n(m, 1), 0)),

 # Every Bond on the table at once.
 (r'^All Servitude trackers are increased by (\d+)$',
  lambda m: ('OP_SERVITUDE_ALL', 0, n(m, 1), 0)),
 (r'^All Servitude trackers are reduced by (\d+)$',
  lambda m: ('OP_SERVITUDE_ALL', 0, -n(m, 1), 0)),
 (r'^All (?:hidden )?trackers shift by (\d+)$',
  lambda m: ('OP_SERVITUDE_ALL', 0, n(m, 1), 0)),
 (r'^All trackers reset to 0$',
  lambda m: ('OP_SERVITUDE_ALL', 0, -SERVITUDE_MAX, 0)),
 (r'^Your trackers cannot be spied on(?: this round)?$',
  lambda m: ('OP_NO_SPY', 0, 0, 0)),

 # Instigators and rumour, which already had their opcodes.
 (r'^Redirect an Instigator to another target$',
  lambda m: ('OP_INSTIGATOR_REDIRECT', 0, 0, 0)),
 (r'^Target two players with Instigators$',
  lambda m: ('OP_INSTIGATOR', 0, 2, 0)),
 (r'^Plant a false tracker; target believes it$',
  lambda m: ('OP_RUMOUR', 0, 0, 0)),

 # Odds and ends that each land on something already built.
 (r'^Gain \+(\d+) (Military|Capital|Gold) Levy$',
  lambda m: ('OP_LEVY', r(m, 2), n(m, 1), 0)),
 (r'^Lose (\d+) (Military|Capital|Gold) permanently$',
  lambda m: ('OP_STANDING', r(m, 2), -n(m, 1), 0)),
 (r'^Target loses (\d+) (Military|Capital|Gold) if undefended$',
  lambda m: ('OP_STANDING_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^\+(\d+) Unrest$', lambda m: ('OP_UNREST_SELF', 0, n(m, 1), 0)),
 (r'^\+(\d+) Unrest on two players$',
  lambda m: ('OP_UNREST_ALL', 0, n(m, 1), 0)),
 (r'^\+(\d+) Grievance$', lambda m: ('OP_GRIEVANCE_SELF', 0, n(m, 1), 0)),
 (r'^All players gain \+(\d+) Grievance against you$',
  lambda m: ('OP_GRIEVANCE_TARGET', 0, n(m, 1), 0)),
 (r'^Target \+(\d+) Grievance$',
  lambda m: ('OP_GRIEVANCE_TARGET', 0, n(m, 1), 0)),
 (r'^you \+(\d+) Capital$', lambda m: ('OP_STANDING', 'R_CAP', n(m, 1), 0)),
 (r'^you gain \+(\d+) Capital$',
  lambda m: ('OP_STANDING', 'R_CAP', n(m, 1), 0)),
 (r'^Target gains \+(\d+) Grievance against you$',
  lambda m: ('OP_GRIEVANCE_TARGET', 0, n(m, 1), 0)),
 (r'^Target (?:must reveal|reveals) one hidden tracker$',
  lambda m: ('OP_EXPOSE', 0, 0, 0)),
 (r'^Target (?:is )?immune to assassination$',
  lambda m: ('OP_NO_ASSASSIN', 0, 0, 0)),
 (r'^All players may hire an assassin for \d+ Gold$',
  lambda m: ('OP_ASSASSINATE', 0, 0, 0)),
 (r'^All players may look at one hidden tracker$',
  lambda m: ('OP_SPY_ANY', 0, 0, 0)),
 (r'^(?:L|l)ook at hidden tracker$', lambda m: ('OP_SPY_ANY', 0, 0, 0)),
 (r'^Target cannot revolt$', lambda m: ('OP_REVOLT_CANCEL', 0, 0, 0)),
 (r'^Rebel becomes loyal; \+(\d+) Servitude -\d+ Revolution$',
  lambda m: [('OP_SERVITUDE', 0, n(m, 1), 0),
             ('OP_REVOLUTION_ZERO', 0, 0, 0)]),
 (r'^All players may buy \+(\d+) Military Levy for \d+ Gold$',
  lambda m: ('OP_LEVY_ALL_GAIN', 'R_MIL', n(m, 1), 0)),
 (r'^(?:All players may )?[Cc]ompete for \+(\d+) Capital Levy$',
  lambda m: ('OP_LEVY_ALL_GAIN', 'R_CAP', n(m, 1), 0)),

 # ---- pool phrasings, wave 7 -------------------------------------

 # The Throne.  A card that handed it over would be a win condition
 # bought for a Gold; what these buy is the right to try.
 (r'^Claim the throne(?: by force)?$',
  lambda m: ('OP_CLAIM_THRONE', 0, 0, 0)),
 (r'^Player with lowest Military becomes Lord Paramount$',
  lambda m: ('OP_MAKE_PARAMOUNT', 0, 0, 0)),
 (r'^Player with most Gold becomes Lord Paramount$',
  lambda m: ('OP_MAKE_PARAMOUNT', 1, 0, 0)),
 (r'^No Lord Paramount this round$', lambda m: ('OP_REGENCY', 0, 0, 0)),

 # "X or Y" where the pool offers a choice the engine cannot put to a
 # seat.  The first branch is taken, and it is always the one in the
 # card's own idiom -- an Instigator card offers Revolution first.
 (r'^\+(\d+) Revolution(?: on a lord)? or -\d+ Gold(?: on free player)?'
  r'(?:; may be detected)?$',
  lambda m: ('OP_REVOLUTION_THEIRS', 0, n(m, 1), 0)),

 # Clauses that appear after a semicolon and had no pattern of their
 # own, so the whole card failed on its second half.
 (r'^\+(\d+) Servitude$', lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),
 (r'^[Dd]raw (\d+) combat cards$', lambda m: ('OP_DRAW', 0, n(m, 1), 0)),
 (r'^target loses (\d+) cards?$',
  lambda m: ('OP_DISCARD_TARGET', 0, n(m, 1), 0)),
 (r'^all players gain \+(\d+) Servitude$',
  lambda m: ('OP_SERVITUDE_ALL', 0, n(m, 1), 0)),
 (r'^All players may release one prisoner for \+(\d+) Servitude$',
  lambda m: ('OP_SERVITUDE_ALL', 0, -n(m, 1), 0)),
 (r'^All players lose (\d+) Gold and \d+ Gold$',
  lambda m: ('OP_LEVY_ALL_LOSE', 'R_GOLD', n(m, 1) + 1, 0)),
 (r'^Lord Paramount loses (\d+)$',
  lambda m: ('OP_STANDING_LOSS_LP', 'R_CAP', n(m, 1), 0)),

 # Revolution is hidden from the House holding the lord already; a card
 # that hides it further is a shield against being spied on.
 (r'^All Revolution trackers are hidden this round$',
  lambda m: ('OP_NO_SPY', 0, 0, 0)),

 # ---- pool phrasings, wave 8 -------------------------------------

 # A fight picked with a card rather than with Open War.  design.xml:
 # "Combat is declared against one House.  It needs Open War, or a
 # card that permits it, or a Throne challenge."  These are the cards
 # that permit it.
 (r'^Challenge (?:a player )?to single combat; winner takes a card$',
  lambda m: ('OP_DECLARE_WAR', 0, 0, 0)),
 (r'^Challenge a player to single combat; winner takes a card$',
  lambda m: ('OP_DECLARE_WAR', 0, 0, 0)),
 (r'^Combat (?:challenge - fight or bribe|with a neutral enemy)$',
  lambda m: ('OP_DECLARE_WAR', 0, 0, 0)),

 # Standing taken without naming which -- the design already has this
 # shape, "Target House loses 1 Standing of your choosing".
 (r'^Target loses (\d+) resource per turn for \d+ turns$',
  lambda m: ('OP_STANDING_LOSS', -1, n(m, 1), 0)),

 (r'^\+(\d+) Military Levy in assassination attempts$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1), 0)),
 (r'^Lord Paramount gains \+(\d+) Grievance$',
  lambda m: ('OP_GRIEVANCE_TARGET', 0, n(m, 1), 0)),
 (r'^Pay \d+ Gold or lose 1 turn$',
  lambda m: ('OP_SKIP_ACTION', 0, 1, 0)),

 # A lord that cannot be turned.  design.xml gives the Proud trait
 # "never abstains" and the design already carries a rider for a Bond
 # that may not Betray; these are that rider on its own.
 (r'^(?:Cannot be forced to betray|Target cannot betray for \d+ rounds)$',
  lambda m: ('OP_BOND_RIDER', 'BR_NO_BRUTAL', 0, 0)),

 # ---- Cataclysm, decided 2026-09-20 ------------------------------
 # Its own pile, drawn FROM and never chosen.  design.xml: "a World
 # card with a larger hammer", and "a Cataclysm ends the realm".
 (r'^Draw from (?:the )?Cataclysm [Dd]eck$',
  lambda m: ('OP_DRAW_DECK', 'D_CATACLYSM', 0, 0)),

 # Cataclysm effects.  These hit the whole table at once by
 # definition, so they are the shape apply_effect allows with no
 # actor behind them.
 (r'^All players lose half their resources$',
  lambda m: ('OP_STANDING_ALL_LOSS', -1, 2, 0)),
 (r'^All players lose (\d+) (Military|Capital|Gold); Lord Paramount loses \d+$',
  lambda m: ('OP_STANDING_ALL_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^All players lose (\d+) (Military|Capital|Gold); one random player loses \d+ more$',
  lambda m: ('OP_STANDING_ALL_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^Every Bond gains (\d+) hidden Revolution$',
  lambda m: ('OP_REVOLUTION_ALL', 0, n(m, 1), 0)),
 (r'^All players lose (\d+) Gold; no Gold this round$',
  lambda m: ('OP_STANDING_ALL_LOSS', 'R_GOLD', n(m, 1), 0)),
 (r'^All players lose (\d+) Gold; all lords gain \+\d+ Revolution$',
  lambda m: [('OP_STANDING_ALL_LOSS', 'R_GOLD', n(m, 1), 0),
             ('OP_REVOLUTION_ALL', 0, 2, 0)]),

 # ---- alliances, decided 2026-09-20 ------------------------------
 # An alliance in this design is a Promise.  design.xml names one --
 # PRM008 "The Sworn Alliance", Term Restraint -- and the Promise
 # system already carries everything the pool's alliances wanted:
 # secrecy by default, a public variant, consideration paid up front,
 # and betrayal at any time with nothing enforced.
 #
 # Cards that FORM an alliance become Promise cards, which is a change
 # of type and not an effect -- OP_PROPOSE returns 0 on purpose,
 # "proposals are an action, not an effect".  Only the cards that act
 # ON an alliance are opcodes, and those are here.
 (r'^(?:All alliances are void|Cancel an alliance'
  r'|All alliances are broken; all lords revolt)$',
  lambda m: ('OP_PROMISE_CANCEL', 0, 0, 0)),
 (r'^Break alliance; \+(\d+) Capital Levy -\d+ Capital$',
  lambda m: [('OP_PROMISE_CANCEL', 0, 0, 0),
             ('OP_LEVY', 'R_CAP', n(m, 1), 0)]),
 (r'^Target cannot form alliances for (\d+) rounds$',
  lambda m: ('OP_NO_PROMISE', 0, n(m, 1), 0)),
 (r'^Force (?:an )?alliance(?: with a player)?(?: for \d+ rounds'
  r'|; neither can betray for \d+ rounds)?$',
  lambda m: ('OP_PROMISE_COERCE', 0, 0, 0)),
 (r'^All players may (give) (\d+) Gold to another player$',
  lambda m: ('OP_LEVY_GIVE', 'R_GOLD', n(m, 2), 0)),
 (r'^All players may take (\d+) Gold from another player$',
  lambda m: ('OP_LEVY_STEAL', 'R_GOLD', n(m, 1), 0)),
 # An ally that sends aid is a Promise kept, which the engine can test.
 (r'^\+(\d+) Military Levy if ally sends aid$',
  lambda m: ('OP_COMBAT_LEVY_IF', 'CIF_TRUSTED', n(m, 1), 0)),
 (r'^Call for aid; ally may send (\d+) Military$',
  lambda m: ('OP_COMBAT_LEVY_IF', 'CIF_TRUSTED', n(m, 1), 0)),
 # Deceit about who is allied with whom: a rumour, which the design
 # already has an opcode for.
 (r'^Make it look like an ally betrayed you$',
  lambda m: ('OP_RUMOUR', 0, 0, 0)),
 (r'^Assassinate a player; (?:they are )?eliminated if undefended$',
  lambda m: ('OP_ASSASSINATE', 0, 0, 0)),
 (r'^If a lord is eliminated all lords gain \+(\d+) Revolution$',
  lambda m: ('OP_REVOLUTION_ALL', 0, n(m, 1), 0)),

 # ---- Council voting, decided 2026-09-20 -------------------------
 # design.xml <council>: "Neither House votes."  The Minor Lords vote,
 # for whoever holds their Servitude, so a card about "your vote" is a
 # card about your lords -- and a House with no lords gets nothing
 # from any of these, "which is the cost of never binding one".
 (r'^Your vote counts twice$', lambda m: ('OP_VOTE_DOUBLE', 0, 0, 0)),
 (r'^Majority vote counts double$', lambda m: ('OP_VOTE_DOUBLE', 0, 0, 0)),
 (r'^Target\'s vote is nullified$', lambda m: ('OP_VOTE_NULLIFY', 0, 0, 0)),

 # Ties.  A duel ties often -- two Houses and a handful of lords -- so
 # every one of the pool's several ways of breaking one lands here.
 (r'^(?:Choose who wins a tied vote|Lord Paramount breaks tie'
  r'|Force a tied vote to your favor|Tied vote -> (?:coin flip|dice roll)'
  r'|No majority -> coin flip|Free players break tie'
  r'|Minority vote counts if majority is split)$',
  lambda m: ('OP_TIE_BREAK', 0, 0, 0)),

 # Sealed and delayed Councils, both of which the engine already had.
 (r'^(?:Hidden votes|Votes are hidden(?: until revealed)?)$',
  lambda m: ('OP_COUNCIL', 0, 'CM_SEALED', 0)),
 (r'^(?:Delay a vote by \d+ rounds?|No vote this round)$',
  lambda m: ('OP_NO_CROWNING', 0, 0, 0)),

 # Votes gained on a condition the engine can answer.
 (r'^Military leader gets \+(\d+) votes$',
  lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^\+(\d+) votes? per Unrest(?: you\'ve caused| caused)$',
  lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^\+(\d+) votes? in the final vote$',
  lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^(?:Gold can buy votes|Spend \d+ Gold per vote)$',
  lambda m: ('OP_VOTE_BUY', 0, 1, 0)),
 (r'^All players vote as you command at next Council$',
  lambda m: ('OP_VOTE_COMMAND', 0, 0, 0)),

 # Becoming king by vote is what a Council already does -- it elects
 # the Lord Paramount -- so these call one rather than inventing a
 # second crowning.
 (r'^(?:Declare yourself king if (?:you have the )?most votes'
  r'|If you have the most votes you may declare yourself king'
  r'|The player with the most votes becomes king'
  r'|All players vote for a new king)$',
  lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^Player with most total power becomes king$',
  lambda m: ('OP_MAKE_PARAMOUNT', 1, 0, 0)),

 # ---- the fallen House, decided 2026-09-20 -----------------------
 # design.xml sets the baseline: a restored House returns "at 1
 # Standing in each resource with its Promise piles intact".  The
 # pool's 26 "full stats" cards are not a contradiction of that, they
 # are cards doing better than the general rule, which is what a card
 # is for -- Winter Camp is already "the one exception" to the Levy
 # rule.  So the level rides in b and none of the 29 is flattened.
 (r'^Return with full stats$',        lambda m: ('OP_RESTORE', 0, 3, 0)),
 (r'^Return with half stats$',        lambda m: ('OP_RESTORE', 0, 2, 0)),
 (r'^Return with (\d+) in all stats$',lambda m: ('OP_RESTORE', 0, 1, 0)),
 (r'^Return with an army$',           lambda m: ('OP_RESTORE', 0, 2, 0)),
 (r'^Return as Lord Paramount$',      lambda m: ('OP_RESTORE', 0, 3, 0)),
 (r'^Return as lord of the drawer$',  lambda m: ('OP_RESTORE', 0, 1, 0)),
 (r'^Permanently eliminated$',        lambda m: ('OP_EXTINGUISH', 0, 0, 0)),
 (r'^No resurrection possible$',      lambda m: ('OP_EXTINGUISH', 0, 0, 0)),
 (r'^Remember killer$',               lambda m: ('OP_GRIEVANCE_SELF', 0, 1, 0)),
 (r'^No memory$',                     lambda m: ('OP_PROMISE_CANCEL', 0, 0, 0)),
 (r'^Servitude (\d+)$',               lambda m: ('OP_SERVITUDE', 0, n(m, 1), 0)),
 (r'^Claim throne is yours$',         lambda m: ('OP_CLAIM_THRONE', 0, 0, 0)),
 (r'^All former lords return$',       lambda m: ('OP_SERVITUDE_ALL', 0, 1, 0)),
 (r'^Replace Lord Paramount(?: by (?:force|gold|vote))?$',
  lambda m: ('OP_MAKE_PARAMOUNT', 1, 0, 0)),
 (r'^May send an Instigator for free$',
  lambda m: ('OP_INSTIGATOR', 0, 1, 0)),
 (r'^May build Servitude secretly$',  lambda m: ('OP_SERVITUDE', 0, 1, 0)),
 (r'^All (?:lose|players lose) (\d+) (Military|Capital|Gold) per lord$',
  lambda m: ('OP_STANDING_ALL_LOSS', r(m, 2), n(m, 1), 0)),
 (r'^No Lord Paramount for \d+ rounds$', lambda m: ('OP_REGENCY', 0, 0, 0)),
 (r'^All players gain \+(\d+) in all stats$',
  lambda m: ('OP_LEVY_ALL_GAIN', 'R_MIL', n(m, 1), 0)),
 # The Ghost deck's own idiom.
 (r'^Speak with a dead player(?:; they reveal one secret)?$',
  lambda m: ('OP_EXPOSE', 0, 0, 0)),
 (r'^Resurrect a dead player(?: as your lord| with full stats)?$',
  lambda m: ('OP_RESTORE', 0, 3, 0)),
 (r'^A dead player (?:may return|haunts a target|returns as your rival)$',
  lambda m: ('OP_RESTORE', 0, 1, 0)),
 (r'^(?:All|A random) dead players? (?:return as ghosts|may return as a ghost'
  r'|draw a Ghost card|returns as Lord Paramount'
  r'|returns as a lord of the Lord Paramount)$',
  lambda m: ('OP_RESTORE', 0, 1, 0)),
 (r'^Make a deal with a dead player(?:; they return as your lord)?$',
  lambda m: ('OP_RESTORE', 0, 1, 0)),
 (r'^Cancel a resurrection$',         lambda m: ('OP_EXTINGUISH', 0, 0, 0)),

 # ---- the tail -----------------------------------------------------
 # Mostly one card each, and mostly a spelling of something already
 # built.  What is NOT here is what genuinely has nowhere to land: a
 # card-in-play zone to cancel a named card out of, retreat and
 # initiative from a game with a board, table talk and dice, and the
 # Lord Paramount treated as a player rather than an office.

 (r'^Draw from (?:the )?Resurrection deck$',
  lambda m: ('OP_DRAW_DECK', 'D_DEAD', 0, 0)),
 (r'^All players draw (\d+) extra House Actions?$',
  lambda m: ('OP_DRAW_ALL', 0, n(m, 1), 0)),
 (r'^All players gain \+(\d+) in all stats(?:; game continues)?$',
  lambda m: ('OP_LEVY_ALL_GAIN', -1, n(m, 1), 0)),

 # The office changing hands.  Five tests, all about Standing.
 (r'^Lord Paramount is replaced by the player with most Gold$',
  lambda m: ('OP_MAKE_PARAMOUNT', 1, 0, 0)),
 (r'^(?:Lord Paramount is replaced by the player with most Military'
  r'|Player with most Military becomes Lord Paramount)$',
  lambda m: ('OP_MAKE_PARAMOUNT', 2, 0, 0)),
 (r'^Lord Paramount is replaced by the player with most Capital$',
  lambda m: ('OP_MAKE_PARAMOUNT', 3, 0, 0)),
 (r'^The player with the most total power becomes king(?: immediately)?$',
  lambda m: ('OP_MAKE_PARAMOUNT', 4, 0, 0)),
 (r'^(?:Declare king if most votes|Player with most votes becomes king)$',
  lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^Lord Paramount is assassinated$', lambda m: ('OP_ASSASSINATE', 0, 0, 0)),
 (r'^(?:Lord Paramount is deposed|No Lord Paramount)$',
  lambda m: ('OP_REGENCY', 0, 0, 0)),
 (r'^new vote triggered$', lambda m: ('OP_COUNCIL', 0, 0, 0)),

 # Votes: more spellings of things already built.
 (r'^(?:All votes this round are hidden|Votes hidden until revealed)$',
  lambda m: ('OP_COUNCIL', 0, 'CM_SEALED', 0)),
 (r'^Minority vote counts if majority split$',
  lambda m: ('OP_TIE_BREAK', 0, 0, 0)),
 (r'^Gold can buy votes this round$', lambda m: ('OP_VOTE_BUY', 0, 1, 0)),
 (r'^Target\'s vote nullified$', lambda m: ('OP_VOTE_NULLIFY', 0, 0, 0)),
 (r'^All players vote on a random issue$', lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^\+(\d+) votes? at next Council$', lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^Final challenge - claim or vote$',
  lambda m: ('OP_CLAIM_THRONE', 0, 0, 0)),

 # "Per free player": a House not bound to the other.  In a duel that
 # is one, so the card pays what it says and no more -- understating
 # is the honest direction when the count cannot be read off the
 # table, and it is the same choice made for durations.
 (r'^\+(\d+) vote per free player$', lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^\+(\d+) (Military|Capital|Gold) Levy per free player$',
  lambda m: ('OP_LEVY', r(m, 2), n(m, 1), 0)),

 # Grievance is held against the Lord Paramount by definition, so a
 # table uniting against them is everybody gaining some.
 (r'^All players allied against (?:Lord Paramount|the leader)$',
  lambda m: ('OP_GRIEVANCE_SELF', 0, 1, 0)),
 (r'^\+(\d+) Grievance against your killer$',
  lambda m: ('OP_GRIEVANCE_SELF', 0, n(m, 1), 0)),

 # Belief about who is allied with whom is a rumour, which is the
 # opcode the design already has for planting something false.
 (r'^(?:Target believes you\'re allied; you\'re not'
  r'|Appear allied to one; actually allied to another'
  r'|Target\'s ally becomes yours)$',
  lambda m: ('OP_RUMOUR', 0, 0, 0)),
 (r'^All alliances broken$', lambda m: ('OP_PROMISE_CANCEL', 0, 0, 0)),

 # A riposte: the card lets you pick the fight back up, which is what
 # design.xml means by "a card that permits it".
 (r'^After being attacked attack back$',
  lambda m: ('OP_DECLARE_WAR', 0, 0, 0)),

 (r'^Target discards a card$', lambda m: ('OP_DISCARD_TARGET', 0, 1, 0)),
 (r'^Target loses (\d+) random resource$',
  lambda m: ('OP_STANDING_LOSS', -1, n(m, 1), 0)),
 (r'^All trackers \(Servitude Revolution Unrest Grievance\) reset to 0$',
  lambda m: ('OP_SERVITUDE_ALL', 0, -SERVITUDE_MAX, 0)),
 (r'^(?:Negate \d+ [Aa]ttack(?: card)?)$', lambda m: ('OP_NO_COMBAT', 0, 0, 0)),

 # Restoration spellings left over from the sentence split.
 (r'^Return with an army\. All with Unrest >= \d+ may join$',
  lambda m: ('OP_RESTORE', 0, 2, 0)),
 (r'^Return with another dead player; share power$',
  lambda m: ('OP_RESTORE', 0, 1, 0)),
 (r'^A dead player returns as a lord of the Lord Paramount$',
  lambda m: ('OP_RESTORE', 0, 1, 0)),

 # ---- the Gold road, decided 2026-09-20 --------------------------
 # The game had four roads and none of them was Gold, though Gold is a
 # third of the economy and the whole of one House.  A fifth existed
 # in the engine -- The Purchased Throne -- and had never once been
 # walked: it asked 12 Gold Standing and 8 Capital when Goldwyn's
 # total Standing across all three averages 4.2 and peaks at 11.
 #
 # Both of these cards are Goldwyn's own, and they are the two levers
 # the road was missing.  Buying the Throne makes you Throneworthy,
 # which is the gate that was actually shut: Goldwyn reaches it in
 # 6.8% of games.  The Golden Throne skips the gate and asks for Gold
 # alone.
 (r'^Buy the throne for \d+ Gold$',
  lambda m: ('OP_CLAIM_THRONE', 0, 0, 0)),
 (r'^Win if you have \d+\+? Gold$',
  lambda m: ('OP_WIN_GOLD', 0, 0, 0)),

 # ---- salvage ------------------------------------------------------
 # A second pass over what was written off.  Most of it was written
 # off for wanting a mechanic this design has no room for; these are
 # the ones where the mechanic turned out to be small, or where the
 # thing the card actually BOUGHT already exists under another name.

 # Cancelling.  Every one of these stops the next thing the other
 # House does, which needs no card-in-play zone -- the card being
 # cancelled has not been played yet.  That is what separates this
 # family from stealing a card off the table, which is still out.
 (r'^Cancel (?:a Council Event|enemy\'s next (?:combat )?card'
  r'|an Instigator targeting you)$',
  lambda m: ('OP_CANCEL_NEXT', 0, 0, 0)),
 (r'^Negate one Political attack$', lambda m: ('OP_CANCEL_NEXT', 0, 0, 0)),
 (r'^Negate a bribe against you$',  lambda m: ('OP_CANCEL_NEXT', 0, 0, 0)),

 # Taking a card out of a hand, and pushing one into it.
 (r'^Steal (?:an enemy\'s|one) combat card(?: mid-combat)?$',
  lambda m: ('OP_STEAL_CARD', 0, 1, 0)),
 (r'^Give target a card; it costs them (\d+) resource$',
  lambda m: ('OP_GIVE_CARD', 0, n(m, 1), 0)),

 # Retreat.  There is none -- combat is one blind commit -- but what a
 # retreat BOUGHT was getting out without paying for it, and the
 # combat already has a step that can be skipped.
 (r'^(?:Retreat without penalty|Flee any combat without penalty'
  r'|Attacker gains nothing)$',
  lambda m: ('OP_NO_SPOIL', 0, 0, 0)),

 # Initiative.  There is no order of blows either, so striking first
 # is read as striking harder.
 (r'^Attack first(?: in any combat)?$',
  lambda m: ('OP_COMBAT_LEVY', 0, 1, 0)),
 (r'^Attack first \+(\d+) if enemy surprised$',
  lambda m: ('OP_COMBAT_LEVY', 0, n(m, 1) + 1, 0)),
 (r'^attack first$', lambda m: ('OP_COMBAT_LEVY', 0, 1, 0)),

 # "The nearest player" needs a board to measure from.  In a duel
 # there is exactly one other House, so the nearest is the only one,
 # and the card is simply a forced war.
 (r'^All players must attack the nearest player(?: this round)?$',
  lambda m: ('OP_DECLARE_WAR', 0, 0, 0)),

 (r'^(?:Military|Capital|Gold) cannot be reduced this round$',
  lambda m: ('OP_SHIELD', 'R_MIL', 0, 0)),
 (r'^Lord Paramount taxes twice this round$',
  lambda m: ('OP_TAX_DOUBLE', 0, 0, 0)),
 (r'^Lord Paramount may punish one player$',
  lambda m: ('OP_UNREST_TARGET', 0, 1, 0)),

 # The one card with three clauses, read as two: the killer is
 # remembered AS the Grievance, which is what remembering costs here.
 (r'^Return with full stats\. Remember killer\. \+(\d+) Grievance$',
  lambda m: [('OP_RESTORE', 0, 3, 0), ('OP_GRIEVANCE_SELF', 0, n(m, 1), 0)]),

 # Favour, granted outright.  OP_FAVOUR has existed since the seed set
 # and NO pattern reached it: in 1,443 cards nothing simply gave you
 # Favour.  It was earned by keeping a Promise or bought at 3 Gold a
 # point, and measured across 500 games it is the master variable --
 # rank the Houses by end-game Favour and you get the win table almost
 # exactly.  A road that decides a third of all games with one way in
 # is not a road, it is a gate.
 (r'^gain (\d+) Favour with the Court$',
  lambda m: ('OP_FAVOUR', 0, n(m, 1), 0)),
 (r'^lose (\d+) Favour with the Court$',
  lambda m: ('OP_FAVOUR', 0, -n(m, 1), 0)),

 # Optimizers.  Levy is set to Standing each turn rather than drawn, so
 # until now nothing multiplied it -- every economy card in 1,443 was a
 # flat one-off, and a resource you cannot compound is one you hold
 # rather than build.
 (r'^Double your (Military|Capital|Gold) Levy this turn$',
  lambda m: ('OP_LEVY_DOUBLE', r(m, 1), 0, 0)),
 (r'^Spend (\d+) (Military|Capital|Gold) Levy: \+1 \2 Standing$',
  lambda m: ('OP_INVEST', r(m, 2), n(m, 1), 0)),
 (r'^\+(\d+) Military Levy when defending$',
  lambda m: ('OP_COMBAT_LEVY_DEF', 0, n(m, 1), 0)),
 # The Boiling Oil, The Murder Holes, The Winter: the attacker pays for
 # attacking, whoever wins.  Per round and flat are the same thing in an
 # engine that resolves one combat per declaration.
 (r'^Attacker loses (\d+) Military(?: per round)?$',
  lambda m: ('OP_COMBAT_ATT_LOSS', 0, n(m, 1), 0)),
 # The King's Peace, The Peace of Kings, Martial Decree, The Price of
 # Peace.  "unless you allow it" and the Gold clause are flavour on top
 # of the same stop: nobody declares this round.
 (r'^(?:Spend \d+ Gold -> )?[Nn]o combat this round(?: unless you allow it)?$',
  lambda m: ('OP_NO_COMBAT', 0, 0, 0)),
 # The catch-up family.  With four players "the player with the lowest"
 # is a lottery; in a duel it is precisely the House being ground down,
 # which is the anti-snowball this game has been missing.
 (r'^(?:Player|House) with lowest (Military|Political Capital|Capital|Gold)'
  r'(?: Standing)? gains \+(\d+)(?: (?:Military|Capital|Gold))? Levy$',
  lambda m: ('OP_LOWEST_GAIN', r(m, 1), n(m, 2), 0)),
 (r'^(?:Player|House) with lowest (Military|Political Capital|Capital|Gold)'
  r'(?: Standing)? gains \+(\d+)$',
  lambda m: ('OP_LOWEST_GAIN', r(m, 1), n(m, 2), 0)),
 (r'^In a Throne challenge, one House of your choosing may not commit$',
  lambda m: ('OP_THRONE_BAR', 0, 0, 0)),
 (r'^In a Throne challenge, each House with a Broken Word commits'
  r' (\d+) less$', lambda m: ('OP_THRONE_OATH', 0, n(m, 1), 0)),
 (r'^(Count|Reduce the Purchased Throne\'s Gold requirement by) (\d+)'
  r'( toward the Purchased Throne\'s Gold requirement)? permanently$',
  lambda m: ('OP_THRONE_DISCOUNT', 'R_GOLD', n(m, 2), 0)),
 (r'^Attempt an assassination against the Lord Paramount$',
  lambda m: ('OP_ASSASSINATE', 0, 0, 'TC_PARAMOUNT')),
 (r'^After winning a combat, take a second Draw action this turn$',
  lambda m: ('OP_EXTRA_DRAW', 0, 1, 0)),
 # ---- council -----------------------------------------------------------
 (r'^Trigger a Council immediately$', lambda m: ('OP_COUNCIL', 0, 0, 0)),
 (r'^Trigger a Council; the Lord Paramount may not be voted for$',
  lambda m: ('OP_COUNCIL', 0, 'CM_NO_PARAMOUNT', 0)),
 (r'^Trigger a Council; the House with most Unrest casts 2 votes$',
  lambda m: ('OP_COUNCIL', 0, 'CM_COMMONS', 0)),
 (r'^Trigger a Council; no votes may be bought or commanded$',
  lambda m: ('OP_COUNCIL', 0, 'CM_SEALED', 0)),
 (r'^Trigger a Council; every House with a Broken Word loses 1 vote$',
  lambda m: ('OP_COUNCIL', 0, 'CM_OATH_READ', 0)),
 (r'^Trigger a Council; the Court votes first and aloud$',
  lambda m: ('OP_COUNCIL', 0, 'CM_COURT_ALOUD', 0)),
 (r'^Buy (\d+) additional vote at this Council$',
  lambda m: ('OP_VOTE_BUY', 0, n(m, 1), 0)),
 (r'^gain (\d+) vote at the next Council$',
  lambda m: ('OP_VOTE_GAIN', 0, n(m, 1), 0)),
 (r'^\+1 vote at the next Council per (\d+) Promises in your'
  r' Word Kept pile$', lambda m: ('OP_VOTE_PER_KEPT', 0, n(m, 1), 0)),
 (r'^Force one House to vote as you say at the next Council$',
  lambda m: ('OP_VOTE_COMMAND', 0, 0, 0)),
 (r'^At the next Council, lords vote as they wish, not as commanded$',
  lambda m: ('OP_VOTE_FREE', 0, 0, 0)),
 (r'^Change your vote after the reveal; once per Council$',
  lambda m: ('OP_VOTE_CHANGE', 0, 0, 0)),
 (r'^No House may be crowned by vote next round$',
  lambda m: ('OP_NO_CROWNING', 0, 0, 0)),
 (r'^Object to a crowning without paying Military Levy$',
  lambda m: ('OP_OBJECT_FREE', 0, 0, 0)),
 # ---- ambitions and the court -------------------------------------------
 (r'^Complete one revealed Ambition that requires (Military|Capital|Gold)$',
  lambda m: ('OP_AMBITION_DONE', r(m, 1), 0, 0)),
 (r'^The House with more Broken Words loses (\d+) Favour$',
  lambda m: ('OP_FAVOUR_CMP', 0, -n(m, 1), 0)),
 (r'^The House with higher Capital Standing gains (\d+) Favour$',
  lambda m: ('OP_FAVOUR_CMP', 1, n(m, 1), 0)),
 (r'^No Favour may be bought next round$',
  lambda m: ('OP_FAVOUR_LOCK', 0, 0, 0)),
 (r'^The Court does not vote at the next Council$',
  lambda m: ('OP_COURT_SILENT', 0, 0, 0)),
 # ---- information --------------------------------------------------------
 (r'^Look at target House\'s hand$', lambda m: ('OP_PEEK_HAND', 0, 0, 0)),
 # ---- ambition conditions, which are checked and never played ------------
 (r'^Hold (\d+) (Military|Capital|Gold) Standing$',
  lambda m: ('OP_AMB_STANDING', r(m, 2), n(m, 1), 0)),
 (r'^Hold (\d+) revealed lords at once$',
  lambda m: ('OP_AMB_VASSALS', 0, n(m, 1), 0)),
 (r'^Reach round (\d+) with 0 Broken Words$',
  lambda m: ('OP_AMB_UNBROKEN', 0, n(m, 1), 0)),
 (r'^Hold (\d+) Promises in your Word Kept pile$',
  lambda m: ('OP_AMB_KEPT', 0, n(m, 1), 0)),
 (r'^Win (\d+) combats$', lambda m: ('OP_AMB_COMBATS', 0, n(m, 1), 0)),
 (r'^Be Lord Paramount at the start of (\d+) rounds$',
  lambda m: ('OP_AMB_PARAMOUNT', 0, n(m, 1), 0)),
 (r'^Have a rival at (\d+) Unrest at any Consequence$',
  lambda m: ('OP_AMB_RIVAL_UNREST', 0, n(m, 1), 0)),
 (r'^Defeat in combat a House holding (\d+) or more Broken Words$',
  lambda m: ('OP_AMB_BEAT_OATHBREAKER', 0, n(m, 1), 0)),
]
P = [(re.compile(p), f) for p, f in P]


def match_one(text):
    for rx, f in P:
        m = rx.match(text)
        if m:
            hit = f(m)
            return hit if isinstance(hit, list) else [hit]
    return None


# One word for one thing.
#
# design.xml renamed the vassal to a Minor Lord when the game became a
# duel -- "none die: sixteen work unchanged" -- but the rename was never
# carried into the classifier, so the patterns went on saying vassal
# while the design said lord.  Both words were half-supported: renaming
# the card text made things WORSE, from nine cards classifying to one.
#
# The patterns say lord.  This maps the old word onto it, so the pool's
# existing text keeps working while it is rewritten.
def _one_word(text):
    # The pool carries two stats the parent board game had and this one
    # does not.  design.xml declares exactly three -- Military, Capital,
    # Gold -- so Production and Honour are not missing features, they are
    # the old names for things that were merged.
    #
    #   Production -> Gold     the economic resource; there is one.
    #   Honour     -> Capital  design.xml gives Capital as "Political --
    #                          and the Council, and Promises", which is
    #                          what Honour bought in the parent game.
    #                          It survives elsewhere only as flavour, on
    #                          the Proud trait's Lever.
    for a, b in (("vassals", "lords"), ("Vassals", "Lords"),
                 ("vassal", "lord"), ("Vassal", "Lord"),
                 ("Loyalty", "Servitude"),
                 ("Production", "Gold"), ("production", "Gold"),
                 ("Honour", "Capital"), ("Honor", "Capital"),
                 ("Political Capital", "Capital")):
        text = re.sub(r"\b%s\b" % a, b, text)
    return text


def classify(text):
    text = _one_word(text)
    """Match the whole line first, then the design's rider clauses.

    Whole-line first is not an optimisation, it is the rule: several
    effects use ';' inside one sentence rather than as a separator --
    "Clear 2 of your Unrest; every other House gains 1 Grievance" is one
    opcode, a crackdown, and not two.  Trying the clauses first would
    match the shorter pattern on the first half and quietly lose the
    Grievance every other House was supposed to gain.
    """
    hit = match_one(text)
    if hit is not None:
        return hit
    # "Spend 5 Gold -> +3 Military Levy".  The pool writes a Gold price
    # into the effect text as well as into the cost column, which is
    # where this engine takes costs from.  Strip the price and read what
    # it buys, rather than carrying a second, invisible cost system.
    m = re.match(r'^Spend \d+ Gold -> (.+)$', text)
    if m:
        hit = match_one(m.group(1)[0].upper() + m.group(1)[1:])
        if hit is None:
            hit = match_one(m.group(1))
        if hit is not None:
            return hit
    out = []
    for c in (c.strip() for c in text.split(';')):
        if not c:
            continue
        hit = match_one(c)
        if hit is None:
            out = None
            break
        out.extend(hit)
    if out is not None:
        return out
    # The Restoration deck separates its clauses with a full stop rather
    # than a semicolon -- "Return with full stats. +2 Military Levy" --
    # so a sentence split is tried last, after the whole line and the
    # semicolons have both failed.  Last, because several effects use a
    # full stop inside one clause and trying this first would cut them.
    if '. ' not in text:
        return None
    out = []
    for c in (c.strip(' .') for c in text.split('. ')):
        if not c:
            continue
        hit = match_one(c)
        if hit is None:
            return None
        out.extend(hit)
    return out


def parse_cost(s):
    """"2G", "1M 1C", "3C 2Grv", "6G!" -- a bang means paid from Standing."""
    levy, perm = [0] * 4, [0] * 4
    if s in ('0', ''):
        return levy, perm
    for tok in s.split():
        m = re.match(r'^(\d+)(Grv|M|C|G)(!?)$', tok)
        if not m:
            raise ValueError('cost %r' % s)
        (perm if m.group(3) else levy)[COST[m.group(2)]] += int(m.group(1))
    return levy, perm


def fld(x):
    """An effect field is either a number or the name of an enumerator."""
    return str(x) if isinstance(x, int) else x


def cstr(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')


rows = list(csv.DictReader(open('CARDS.tsv', encoding='utf-8'),
                           delimiter='\t'))
bad, by_deck = [], {}
out = []
for i, row in enumerate(rows):
    eff = classify(row['effect'])
    if eff is None:
        bad.append((row['id'], row['effect']))
        eff = []
    if len(eff) > 2:
        bad.append((row['id'], 'three clauses: ' + row['effect']))
        eff = eff[:2]
    levy, perm = parse_cost(row['cost'])
    reads = ' | '.join(READS[t] for t in row['reads'].split()) or '0'
    while len(eff) < 2:
        eff.append(('OP_NONE', 0, 0, 0))
    # Grouped by the deck the ENGINE has, not by the name in the file.
    # Combat cards ARE War cards, Council cards ARE Political cards --
    # court.h says so in the DeckId comments -- so they share one run of
    # DECK_FIRST and DECK_SIZE and must be counted as one deck here.
    by_deck.setdefault(DECK[row['deck']], []).append(i)
    out.append('  { %s, %s, %s,\n'
               '    %s, %s, %s,\n'
               '    { %d, %d, %d, %d }, { %d, %d, %d, %d }, %s,\n'
               '    { { %s, %s, %s, %s }, { %s, %s, %s, %s } } },'
               % (cstr(row['id']), cstr(row['name']), cstr(row['effect']),
                  DECK[row['deck']], TYPE[row['type']], CAT[row['category']],
                  levy[0], levy[1], levy[2], levy[3],
                  perm[0], perm[1], perm[2], perm[3], reads,
                  eff[0][0], fld(eff[0][1]), fld(eff[0][2]), fld(eff[0][3]),
                  eff[1][0], fld(eff[1][1]), fld(eff[1][2]), fld(eff[1][3])))

if bad:
    print('\n%d effect lines the classifier could not read:' % len(bad),
          file=sys.stderr)
    for cid, t in bad:
        print('  %-8s %s' % (cid, t), file=sys.stderr)
    print('\nAdd a pattern to gen-cards.py.  A card compiled to OP_NONE is'
          '\nan inert play, and the inert-play measure would then report it'
          '\nas a design fault rather than as the missing pattern it is.',
          file=sys.stderr)
    sys.exit(1)

# The decks in CARDS.tsv are contiguous because the extractor writes them
# in design order; the generator asserts it rather than assuming it, since
# DECK_FIRST/DECK_SIZE turn that assumption into pointer arithmetic.
print('/* Generated by gen-cards.py from CARDS.tsv.  Do not edit.')
print(' * %d cards, of the 1,135 the manifest declares.  The design calls'
      % len(rows))
print(' * its card section a seed, so the rest is a conversion pass that')
print(' * has not been run, not a table that has gone missing. */')
print('const Card CARDS[] = {')
print('\n'.join(out))
print('};')
print('const int CARD_COUNT = %d;' % len(rows))

order = []
for d in DECK.values():
    if d in by_deck and d not in order:
        order.append(d)
first, size = {}, {}
for d, idx in by_deck.items():
    assert idx == list(range(idx[0], idx[-1] + 1)), 'deck %s is not contiguous' % d
    first[d], size[d] = idx[0], len(idx)   # d is already the DeckId
# One entry per DeckId, not one per NAME.  DECK now maps several names
# onto the same deck -- Combat onto D_WAR, Council onto D_POLITICAL --
# so iterating the names emitted twenty entries into an array of
# twelve.  The compiler said "excess elements in array initializer" and
# would have gone on saying it while the last eight were silently
# dropped.
DECK_IDS = []
for _d in DECK.values():
    if _d not in DECK_IDS:
        DECK_IDS.append(_d)
assert len(DECK_IDS) == 17, 'D_COUNT is 17; got %d' % len(DECK_IDS)
print('const short DECK_FIRST[D_COUNT] = {')
print('  ' + ', '.join(str(first.get(d, 0)) for d in DECK_IDS))
print('};')
print('const short DECK_SIZE[D_COUNT] = {')
print('  ' + ', '.join(str(size.get(d, 0)) for d in DECK_IDS))
print('};')

print('  %d cards, %d decks, every effect classified'
      % (len(rows), len(by_deck)), file=sys.stderr)
