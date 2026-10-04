# Court of Treasons

A card game of ambition, betrayal and deceit for two to four Houses. No
board and no movement. `design.xml` is the design and it is the authority;
there is no engine yet.

Descended from `treasons-c` next door. Almost nothing worth keeping there
was the board: of 1,035 cards, 19 mentioned movement, and the board named
which deck you drew from and nothing else. What replaces it is a choice of
deck, which is a decision rather than a die roll.

Three things are new rather than ported, and each is marked `<invention>`
in the design with the reason it exists:

* **Standing and Levy.** The parent had stats that only accumulated —
  nothing spent Military, Capital starved, and there was no curve because
  there was no cost. Standing is what you are; Levy is what you can spend
  this turn, set from Standing and gone at the end of it.
* **Promises as objects.** An Offer was engine state and trust was computed
  from it. Here a Promise is a card, kept or broken in the open, and the
  piles are public and permanent — so other cards can read a reputation.
* **The Neutral Court.** The trust ledger needs three. At two players the
  Court is the third party: it does not take turns, but it votes, it
  remembers, and it can be bought as far as +3.

Grievance has its sinks built first this time, rather than declared and
left unbuilt, because next door it sat pinned at its maximum in every
single game.

## Taking a seat

The engine asks a seat five questions and only five:

```c
typedef struct Seat {
    void *ctx;
    Action (*choose)(Seat *, const View *, const Action *, int n);
    int (*commit)(Seat *, const View *, int max, int defending);
    int (*keep_promise)(Seat *, const View *, int promise_idx);
    int (*accept_promise)(Seat *, const View *, int promiser, int term,
                          int cons_res, int cons_amt);
    int (*lord_turns)(Seat *, const View *, int holder, int lord_idx);
} Seat;
```

A machine seat answers from `ai.c`. A human seat answers from a UI. A
remote seat answers over a wire. The engine cannot tell which, because
every one of them takes a `View` and a `View` is all any seat is
entitled to — the rule the AI already obeyed, made into the interface
instead of a habit.

`court_setup` fills both seats with the machine, so a game plays itself
until something replaces one. **A null callback falls back to the
machine**, so a game with one human is one field, not a fork.

This is also networking requirement N4 — *"a client proposes; it never
asserts"* — with the client in the same process. Moving it to another
process later changes what is behind the five pointers and nothing else.

## Over a wire

All three networking options are wanted, so all three are built — and
two of them are one implementation. A central server and a trusted host
are the same authority binary run by different people; the difference is
who is trusted and what the interface says about it. Only commit-reveal
is a new mechanism rather than a new address, and it is a layer over the
same protocol.

    ./court --serve 0        hand seat 0 to stdin, speaking the wire

One format: a client sends an Action, the authority replies with that
seat's View. Both are plain data — an Action is seven small integers, a
View has no pointers — so a client can be written in any language
without linking to this one. Line-delimited JSON, because a turn is one
message and a game is a few hundred, so compactness is not the
constraint and being able to watch the traffic go past is.

**The authority never sends a Game.** It sends a `View`, per seat, built
by `court_view`. That is requirement N1 satisfied by construction rather
than by filtering: there is nothing in a View to leak, so there is no
filtering step to get wrong.

`tools/wire-test.py` is a forty-line client in a language the engine is
not written in, which is the point — if a client that small can play a
game through the protocol then the protocol is a protocol. It also
checks N1 **from outside**: every View is inspected for a Revolution
count on a lord that seat does not hold. Five games, 262 views, 215
actions, **0 leaks**.

The local build speaks it too, so the protocol is exercised by the
single-player game rather than only by the networked one, and cannot
quietly rot between them.

### A server that stays up

    tools/deploy.sh macpro 9017

Ships source, not a binary. The engine is ISO C99 and the one file that
knows about sockets is the only one with an `#ifdef` in it, so it builds
wherever it lands — and building it there is what caught a repeated
`typedef` that this compiler allowed and macOS 12's did not. A shipped
binary would have hidden that until the next platform.

The server takes a player, plays a game, records who won, and goes back
to waiting. One at a time, which is the honest shape for a duel: no
lobby, no matchmaking, only the next person to connect. Each game gets a
fresh `Game` and a fresh seed, because a server reusing either would
deal the same cards to everyone who ever connected.

Running on the Mac Pro at `192.168.0.50:9017` under launchd. A full game
from another machine: **79 views, 74 actions, 0 leaks, 0.4 seconds.**

What it is not, yet: one game at a time rather than many, no
authentication, and reachable on the LAN only. None of those are
architecture — they are things to add to a thing that works.

### Over a socket

    ./court --serve 0 --port 9017      be the authority
    ./court --join otherbox:9017       take the other seat

`src/net.c` is **the only file in this program that knows what an
operating system is** — Winsock on Windows, Berkeley sockets elsewhere,
behind one `Chan`. Everything else deals in a channel and never in a
socket, which is what keeps a Mac playing a Linux box from putting
`#ifdef` through the middle of the protocol.

A host started with `--port` **prints that it is a trusted host** before
the first card is dealt, because it is: the other seat's hidden Bonds
and Ambitions are on that machine. The design says to label it rather
than quietly assume it — *"A player who does not mind is entitled to not
mind; a player who has not been told is not."*

`tools/net-test.py` plays a full game over a real socket and checks both
things. That is a different test from the pipe one and worth having:
a wire format can be right while a socket splits one message across two
reads or joins two into one, and a reader that assumed one message per
read would only discover it on a slow link — which is the case this
exists for. **86 views, 65 actions, 0 leaks, host announced.**

## What is built

`make` builds a headless engine; `make check` plays 200 games with the
rules invariants armed; `tools/soak.sh N FIRST HOUSES` plays a batch and
prints the five measures `<engine_contract>` names, each beside the
threshold the design sets for it.

    extract-cards.py   design.xml -> CARDS.tsv, HOUSES.tsv
    gen-cards.py       CARDS.tsv  -> src/cards.inc, and fails loudly on
                       any effect line it cannot classify
    src/court.h        state, the View a seat is entitled to, the Action
    src/rules.c        the effect interpreter, one arm per opcode
    src/phases.c       round, turn, combat, Council, revolt, the Throne
    src/ai.c           the machine seats; takes a View, never a Game

All 124 seed effects classify. Two structural rules from the design hold
everywhere, because two of the three networking options die without them:
every seat decides from a `View` that simply has no field to read another
House's hidden Bond from, and every simultaneous decision is collected
from all seats before any is revealed.

## What the soak says

400 games per player count, seeds 1..400, with the Levy tax of Q5.

| | 4 Houses | 3 Houses | 2 Houses | design says |
|---|---|---|---|---|
| inert plays | 23.8% | 26.1% | 29.3% | next door it was 53% |
| promises broken | 8.5% | 7.5% | 0.4% | near 10% |
| promises made | 14.0 | 8.3 | 1.8 | — |
| grievance, sampled every round | 1.4 | 1.2 | 0.8 | must not pin at 10 |
| levy unspent | 54.0% | 51.5% | 36.5% | low means no curve |
| win spread | 1.19x | 2.07x | 2.06x | about 1.33x |
| first Paramount wins | 24.5% | 48.2% | 66.2% | chance: 25 / 33 / 50% |
| rounds | 22.3 | 15.6 | 3.6 | — |

At four Houses every measure the design names is met or close, and all
five roads to the crown are walked: Claim by Force 27%, The Final King
38%, Acclamation 19%, The Last House 16%, The Purchased Throne 0.2%.
Grievance moves — it does not sit pinned at 10 in any game at any player
count, which was the parent's standing defect and is the one thing the
sinks were built first to prevent.

Two engine bugs found by measurement rather than by reading, both of
which had been quietly deciding every game:

**A defender could not fight back.** Levy is set from Standing at the
start of your turn and lost at the end of it, so outside your own turn a
House holds nothing. Every defender in every combat committed zero, and
a Throne challenge at Consequence was zero against zero — which does not
exceed zero. `Claim by Force`, the win condition the design describes
first, won 0.8% of games, and 55% of games ended with the realm dead
because nobody could ever defend. A House called to fight outside its
own turn now musters from its Military Standing, before either side is
asked what it commits. `Claim by Force` went to 27%, the realm-dead rate
to 16%, and the win spread from 7.34x to 1.19x. `<turn_structure>` and
`<combat>` do not say what a defender commits with; this is an engine
decision standing in for a design one.

**Cards were offered that could not be paid for.** `court_actions`
listed every card in hand and `card_play` then refused the unaffordable
ones, so a House picked its best card, had it rejected, and ended its
turn — which read in the log as a House with a full hand and a full
purse doing nothing at all. The cost rule lived in two places.

Four things the measurements found that the design has to answer.

**Q5 is answered, and the answer is Levy.** The tax as written takes
Standing, and it compounds: across three ranges of 300 seeds, the House
that led after the first Accession won 60%, 64% and 60% of games against
a 25% chance, and that was the same number as the Aldemar win rate, so
it was not uneven Houses. With a Levy tax it is 21–26% and the win
spread falls to about 1.2x. The code still does what the design says by
default, because the design is the authority; `COURT_TAX=levy` and
`tools/ab-tax.sh` run the pair.

**Two decks have no way into a hand.** `<deck_access>` names House, the
three shared, World, Ambition, Throne and Ghost. It does not name
Promise or Lever — 70 of the 100 cards the design says are new, with the
Promise deck's own note saying its cards "are drawn like anything else".
Before access was added the engine made 2.96 promises a game, all from
the five Promise cards that happen to sit in House decks. The engine now
prices Promise at 1 Capital and Lever at 1 Gold; that belongs in
`<deck_access>`.

**The two-player game is over in 3.6 rounds.** At two living Houses a
crowning needs 2 votes, neither House will ever vote for the other, and
the Court holds the third — so every Council crowns whoever the Court
favours. The Court deck turns a card every round and one of its five is
*A Judgement*, which triggers a Council. 88% of two-player games end by
Acclamation, and the Favour track that `<invention name="The Neutral
Court">` calls "a third road, and the only one a House can walk while
losing every fight" wins 6%. The Court works; it just decides too early.
Promises never come due in 3.6 rounds either, which is why the
two-player broken-promise rate is 0.4% and means nothing.

**Levy outruns cards.** 54% of granted Levy is never spent. The design
reads that as costs being too low, and some of it is — but a House gets
one Draw a turn and a Levy set from Standing every turn, so past about 6
Standing in a resource the money arrives faster than anything to spend
it on can. Pricing alone will not close that.

## Card art

`gen-art.py` renders the deck through ImageLab on the Mac Pro
(`192.168.0.50:8095`), model `sdxl-turbo`, steampunk oil painting.

    python3 gen-art.py --dry-run --deck Ravenmark   print prompts only
    python3 gen-art.py --deck Vipren                one deck
    python3 gen-art.py --all                        everything missing

Resumable: a card already in `art/` is skipped, so the run can be stopped
and restarted. The server renders one image at a time at about three
minutes each, so the full 124 is roughly six hours; it is also shared, so
the tool keeps only three of its own jobs in the queue.

The prompt for each card is a hand-written scene from `scenes.tsv`, the
deck's own visual register, and one constant style string. The card's
effect text is deliberately not used: it is written for a classifier —
`+3 Military Levy this turn` — and fed to an image model it contributes
numbers and game nouns and no picture. A card with no line in
`scenes.tsv` stops the run rather than being drawn from its name alone.

## The conversion pass

`convert-parent.py` runs `<conversion>` over the 1,035 cards in
`treasons-c/design.xml` and writes `CONVERTED.tsv` and `CUT.tsv`;
`merge-pool.py` joins those to the seed as `POOL.tsv`, 1,066 cards in
deck order. Five decks the seed had no cards for — Combat, Ghost,
Cataclysm, Resurrection, Instigator — now exist in the engine.

The design says four passes, of which only the third needs judgement.
Having run them, three things are different from that.

**Removing the board costs three times what the design says.**
`<conversion>` counts 19 cards naming movement, "under 2% of the pool
and the whole price of removing the board". Movement alone measures 17,
so that number is right. But `<ancestry>` also removes castles and
sieges (36 cards) and `<ambitions>` removes the gauntlet (11 more), and
neither is counted in the 19. The real cut is **64 cards, 6.2%**.

**There is a fifth pass.** The parent's `type` is mostly the name of the
deck a card sits in — `Political`, `Intrigue`, `Ghost` — and it carries
36 categories against the 8 in `<card_schema>`. Neither vocabulary maps
itself. Both are now mapped in `convert-parent.py` with the reasoning
beside each group.

**Pass 2 is not mechanical, and it is the real work.** Disambiguating
Levy from Standing is mechanical: 150 effects named a stat, 27 said a
duration, 3 said "permanently", and 120 said nothing and defaulted to
Levy because Levy is the reading that does not compound. But running the
classifier over the merged pool leaves **930 of 1,066 effects
unreadable**, and they do not collapse:

| | |
|---|---|
| unreadable effect lines | 930 |
| ...using only concepts this game has | 836 |
| ...naming something it does not | 47 |
| distinct shapes among the 836 | 517 |
| shapes appearing exactly once | 353 |
| lines covered by the 20 commonest shapes | 16% |

There is no small set of patterns that unlocks the pool. Two thirds of
the shapes are singletons. The honest reading is that these 942 cards
need their effects **rewritten onto this game's vocabulary** — the 85
opcodes the engine already has — rather than parsed in the vocabulary
they arrived in. That is writing, not tooling, and it is the pass the
design describes as mechanical.

The 47 that name something else need decisions first, not sentences:
draws from decks that no longer exist (29), a `hidden tracker` the
trackers here do not match (19), `Loyalty` (18), being `king` outside
the four win conditions (15), and `Production`, a parent stat with no
equivalent here (13).

## What the two-player engine measures

400 games, seeds 1..400.

| | | design asks |
|---|---|---|
| rounds | 12.4 | — |
| **promises broken** | **10.1%** | near 10% |
| promises made | 7.4 per game | — |
| inert plays | 31.8% | beat 53% |
| grievance, sampled every round | 0.3 | never pinned |
| levy unspent | 39.3% | low means no curve |
| holding the Paramountcy longer won | 49.0% | 50% is chance |

Four roads, all walked: **Claim by Force 11%, Favour of the Court 50%,
The Last House 28%, The Final King 10%.** Houses win between 4% and 37%,
which is not level and is no longer the point — expanding this game adds
Houses rather than chairs.

### How breaking got to 10%

**Breaking is priced in Favour, and the scales have to meet.** At two
players there is no table for an offence to be against: every living
House is the promisee, so the Grievance clause cannot tell a paid
betrayal from an unpaid one, and only the Court can. A point of Favour
is worth about three, and consideration adds to it — which is what stops
a bought promise being worth exactly as much as a free one.

The version this replaced cost **15 to 29 against a gain of 4 to 8**, so
breaking was never close and 400 games produced not one broken promise.
It also weighed keeping against what a term is worth in the abstract,
which never changes. What a promise is worth breaking is what it is
worth *now*, and the only thing that makes now different is how close
the House you promised is to winning.

An earlier commit claimed this was already done. It was not: the edit
was applied with a string replace that matched nothing, the failure was
silent, and the claim reached a commit message and this file while the
old pricing stayed in the source. Every measurement taken between those
two points was of code that did not exist. Edits assert their match
before writing now.

### The Houses play differently

The House traits in `design.xml` are all permissions and prices —
Ravenmark declares war free, Vipren sends two Instigators, Goldwyn buys
votes. None of that makes a machine House *behave* like its House: an AI
handed Ravenmark's free war will still sit and count coin if counting
coin scores higher.

So style is a table in `ai.c`, one row per House, added to the score of
whole kinds of action. A row and not a branch: the engine already
carries 22 hardcoded `id == H_RAVENMARK` tests and those are what would
make a fifth House a treasure hunt. Here a fifth House is six numbers.

| per game played | wars | bonds | whispers | promises | breaks |
|---|---|---|---|---|---|
| **Ravenmark** | **6.17** | 0.76 | 0.00 | 0.05 | 0.02 |
| **Vipren** | 0.00 | 1.37 | **34.96** | 2.02 | 0.12 |
| **Goldwyn** | 0.00 | **2.01** | 16.91 | 1.11 | 0.05 |
| **Aldemar** | 0.04 | 1.10 | 16.93 | **4.11** | **0.60** |

Ravenmark fights six times a game and promises once in twenty. Vipren
whispers exactly twice as often as anyone else, which is its rule
showing through its style. Goldwyn binds the most lords. Aldemar makes
four promises a game and breaks the most of anyone — not because it is
faithless but because it is the only House that promises enough for
breaking to come up, which is also why its "loses 2 Capital the first
time it breaks a Promise" weakness has something to bite.

One thing this exposed: Aldemar bound **0.12** lords a game at first,
because its style pushed it away from Gold and the merge had moved the
Minor Lords into the Intrigue deck. A House whose entire style is the
Council could not reach the only things that vote in it. The Intrigue
draw now takes the larger of a House's intrigue and council leanings,
and Aldemar binds 1.10.

### Nine decks

Seventeen decks worked on a table, where a deck is a pile you point at.
On a screen each one is a menu item, and a single Draw action a turn
meant reading **nine options** before acting. The satellites are folded
into the three decks whose resource already paid for them: War takes
Combat, Political takes Council and Promise, Intrigue takes Lever, Minor
Lords and Instigator, World takes Cataclysm, and Ghost and Resurrection
become one variant deck.

**Nine slots, five draw choices**, since Ambition and Throne only enter
the menu when you qualify and the Court deck is never drawn. Every card
kept its text and its cost; only its pile moved, and the manifest still
closes at 1,195.

Two things it changed that were not the point:

**Merging dilutes.** Promises made fell from 3.4 a game to 1.5. A
dedicated Promise deck guaranteed a Promise card; forty of them inside a
170-card Political deck gives one about a quarter of the time. The menu
is shorter and you can no longer *choose* to go looking for a promise —
which matters, because promises are what this game is about. Writing 24
more Promise cards took the pool from 8 of 40 to 32 and brought it back
to 7.4 a game, so the dilution is answered by filling the deck rather
than by un-merging it.

**Holding the Paramountcy longer moved from 40% to 68% and back to
49%**, against a 50% chance. Something about what the merged
decks offer has made the title worth holding again. That is not
necessarily wrong — a title nobody wants is as broken as one that runs
away — but it moved a long way on a change that was supposed to be about
menus, and it is worth knowing which of the two numbers is the accident.
