# The two-player recheck

The first rule is now that this is a game for two. Everything below is
`design.xml` read against that, section by section: what survives, what
stops meaning anything, and what has to be invented to replace it.

Two further decisions are taken as given: **vassals make no sense** and
**winning by vote makes no sense**.

Nothing here is applied yet. `design.xml` is the authority and it still
says `players min="2" max="4"`.

## What it costs in cards

| | |
|---|---|
| seed cards touching vassals, Bonds or revolts | 28 |
| seed cards touching votes, Councils or crowning | 24 |
| distinct seed cards affected | **51 of 124 (41%)** |
| whole decks that lose their purpose | Lever, Instigator, Council |
| manifest cards in those three decks | **90 of 1,135** |

## Survives unchanged

**Standing and Levy.** The economy is indifferent to player count. It is
the invention the design is most confident about and nothing here
touches it.

**The deck-as-board.** Choosing which deck to draw from, and paying for
it, works identically at two. Minus the Lever deck.

**Combat.** Two sides committing Military Levy secretly is *more*
natural at two, not less — there is no field, only an opponent.

**Ambitions and the Throne.** Three of six makes you Throneworthy, and a
challenge is you against the only other House. `Two Knees Bent` dies with
vassals; the other nine stand.

**Unrest.** Self-contained: it reduces your Levy and at 5 your own people
rise. It never needed a third party.

**Death, the round cap, the Final King.** Unchanged.

## Dies outright

**Bonds, Servitude, Revolution, Instigators, Levers.** With one opponent
there are two ordered pairs and "a House may be Lord to several" is
impossible. Worse, the revolt is resolved by *"every uninvolved House
chooses simultaneously: Help Lord, Help Vassal, Stay Neutral, or
Exploit"*, and there are no uninvolved Houses. Total revolt — "as Brutal,
with an ally" — is unreachable. Three hidden trackers become one.

**Acclamation, and the Council with it.** A Council existed to crown
someone. Without that it is an empty phase.

**The coalition sink.** `Join a coalition: add your Military Levy to
another House's attack` — there is no other House's attack to join.

## Breaks quietly, which is worse

**The rule that closes the pricing problem.** Breaking a promise that
carried consideration gives 1 Grievance to *every living House, not only
the promisee*, because *"taking payment and reneging is an offence
against the table."* At two players every living House **is** the
promisee. Paid and unpaid promises become identical and the pricing
problem the design solved reopens.

Only the Court still distinguishes them: −2 for a broken word, −3 if it
carried consideration. So **Court Favour stops being a third road and
becomes the trust ledger itself.** The Court is no longer a fix for the
two-player case; it is the party that makes the game work.

**Grievance loses its meaning and most of its sinks.** It is held
"against the table, not against one House" — but the table is one House.
Of five sinks, the coalition dies, forcing a Council dies, and cancelling
the tax and the assassination survive.

**Five of seven Promise terms die.** This is the serious one, because
Promises are what the game is now about.

| term | at two players |
|---|---|
| Vote | dead — no votes |
| Abstention | dead — no votes |
| Manumission | dead — no vassals |
| Aid | **dead** — if your only opponent attacks you, nobody can come |
| Alliance (Peace + Aid) | collapses to Peace |
| Peace | survives |
| Tribute | survives |

Two terms is not a trust ledger. The Promise deck needs new terms before
anything else here matters, and they have to be things one House can
promise another when there is nobody else in the room. The shape that
suggests itself is **forbearance rather than assistance** — promising not
to do something you are able to do:

* *I will not commit against your Throne challenge.*
* *I will not buy Favour this round.*
* *I will not draw from the War deck.*
* *I will reveal one Ambition, truthfully.*
* *I will not attack the Standing you are building toward an Ambition.*

The first of those is the strongest card in the idea: a promise that
decides the game at the moment it comes due, which is exactly the
promise the design says is hardest to price and most worth breaking.

## Needs a decision before anything can be written

1. Does the **Council** survive in any form — as an event that moves
   Favour rather than crowns a king — or go entirely?
2. Does **Grievance** survive, given it is defined against a table that
   no longer exists, or fold into Unrest?
3. What replaces the **Lever, Instigator and Council decks** in the
   manifest, or does the manifest shrink by 90?
4. Do the **House traits** that reference vassals and Instigators get
   replaced, or do those Houses get new weaknesses? Ravenmark cannot
   send Instigators and Vipren sends two; neither means anything now.
5. Is the **Lord Paramount** still worth having? At two Houses it is
   "whoever is ahead", it taxes one House, and the first Paramount
   already wins 66% of games.

## Proposal: unrest per resource

Instigators and revolts do not need a third party — they need a target,
and at two players the only target that exists is what the other House
*has*. Pointing both at resources gives them one.

**One Unrest track becomes three**, one per Standing. Call them what
they are: a mutiny in the Military, the court turning in Capital, the
guilds withholding in Gold.

* An **Instigator** is sent face down and resolves at Consequence,
  adding 1 Unrest to a *named* resource of the other House. The target
  learns the effect, not the sender — as now.
* At **1 to 4**, that resource's Levy is reduced by that many. This is
  the current rule with one change, and it is the change that matters:
  today Unrest is a single track reduced *"spread as you choose"*, so
  the victim picks where it hurts least. Per resource, the **attacker**
  picks where it hurts most.
* At **5**, that resource revolts: lose 2 Standing in it, reset to 0.
  A revolt is no longer a vassal going free, it is a thing you own
  turning on you — which is what the word meant before the board did.
* A **Lever** is what makes it stick. Attached to a resource, its Unrest
  cannot be cleared while the Lever holds. That is close to the Lever's
  present job — Servitude cannot advance without one — with Servitude
  swapped for the thing that replaced it.

### What it saves

| | |
|---|---|
| Instigator deck | **30 manifest cards**, purpose restored |
| Lever deck | **30 manifest cards**, purpose restored |
| seed cards revived | 3 directly, and every card naming Unrest gains a target |
| still without purpose | Council, 30 |

It also keeps the *shape* of the Bond, which is the thing worth keeping
about vassals: I build something against you over several turns, you can
see it coming but not all of it, and when it matures it bites. What it
drops is the part that needed three players — a vassal to free, a Lord
to betray, and uninvolved Houses to take sides.

And it hands Gold a sink. `<grievance>` complains that the parent game
declared five sinks and built none; clearing Unrest at 2 Gold a point,
across three tracks an opponent is actively filling, is a drain that
runs all game rather than one that exists on paper.

### What it does not solve

Servitude itself. Twenty-four seed cards name a vassal, a Lord or a
Bond, and this does not revive them — it replaces what they were for.
They need rewriting or cutting either way.

## Proposal: minor lords

Servitude does not need a second player either. It needs somebody to
bend the knee, and that can be a card.

A **Minor Lord** is a card played in front of you and staying there. You
bind it with Levers exactly as the design already describes; it serves
while its Servitude holds; and the other House works on its Revolution
until it turns. Everything the Bond was — built over several turns, half
hidden, biting when it matures — survives intact. What goes is only the
assumption that the knee belongs to a player.

This is also the conversion the rest of the game is asking for. In a
duel the thing you attack, bind, corrupt and lose has to be on the
table, not sitting in the other chair, because the other chair can
always simply decline.

### What it revives

Of the 24 seed cards that name a vassal, a Lord, Servitude or a Bond:

| | |
|---|---|
| work unchanged | **16** |
| need their target re-pointed only | 1 |
| need target *and* a new condition | 7 |
| die | **0** |

Sixteen work as written because the design already treats a vassal as a
*thing* rather than as a player — *"Each of your revealed vassals gains
+1 Military Levy"*, *"A revealed vassal's Revolution cannot advance next
round"*, *"Hold 2 revealed vassals at once"*. Those sentences were
always about a subject, not an opponent.

The seven needing more are the ones whose *condition* reads a House that
is no longer the thing being bound — `A Secret Kept` advances Servitude
"against a House with 1 or more Broken Words", `Fear` "against a House
with 3 or more Unrest". A minor lord has neither. Each needs a lord-side
equivalent, which is an opportunity rather than a cost: a lord who is
**in debt**, **afraid**, **ambitious** or **owed a favour** is a lord
with a personality, and that is what makes a deck of them worth having.

It also brings back the **Manumission** promise term, which had died
with vassals, restoring a third term to a system that was down to two.

### What this leaves

Taken with unrest-per-resource, the manifest recovers nearly everything:

| deck | status |
|---|---|
| Instigator, 30 | restored — targets resources *or* a rival's minor lords |
| Lever, 30 | restored — its original job, binding a minor lord |
| Council, 30 | **still without purpose** |
| Minor Lords | **new deck, no parent** |

Only the Council is still homeless, and only the vote is still gone.

## Proposal: the Council is the lords

A Council of two Houses is not a council, it is an argument. But a
Council of the **minor lords in play** is a real one, and every piece
needed for it is already on the table.

* Each **revealed minor lord casts one vote**, for the House that holds
  its Servitude.
* The **Court casts one**, as `<neutral_court>` already says.
* A lord whose **Revolution has passed its Servitude abstains** — or
  votes against the House that holds it. This is the good part: the
  hidden track resolves *in public*, at the worst possible moment, and
  without ever printing the number. You learn your lord has turned by
  watching it not raise its hand.

### What a Council decides

Not a king — that is gone. **The Lord Paramount.**

Accession is currently "recount Total Power; the higher is Lord
Paramount", which is the single worst-measured rule in the game: the
first Paramount wins 54% of four-player games and 66% of two-player
ones, because the title feeds the tax and the tax feeds the title.
Making it *elected rather than accrued* breaks that loop, and it turns a
bookkeeping phase into the thing the whole minor-lord layer is played
for.

It also sharpens everything else. Binding lords is how you take the
crown; Instigators are how you make a rival's lord abstain the round
before the vote; a Lever is how you stop yours doing the same.

### What it revives

Of the 24 cards touching votes, Councils or crowning:

| | |
|---|---|
| survive if a Council happens but crowns nobody | **21** |
| go with the vote win | 3 |

The three that go are `No Crown Unearned`, `Succession Crisis` and
`The Question of Succession` — all three about being crowned, which is
the one thing removed.

And **the Vote and Abstention promise terms come back**, because there
is a vote to promise again.

## Where the recheck ends up

| | at the start | with lords, resources and a lords' Council |
|---|---|---|
| seed cards dead | 51 of 124 | **3 of 124** |
| decks without purpose | Lever, Instigator, Council | **none** |
| Promise terms alive | 2 of 7 | **5 of 7** |
| new decks needed | — | Minor Lords |

Only `Aid` and the `Alliance` that contains it stay dead, for the reason
nothing can fix: when your only opponent attacks you, nobody can come.

Three things still need deciding, and none of them are rescues:

1. **Does the Lord Paramount keep the tax?** Electing the title fixes
   the feedback loop, but the tax is still the mechanic that ends games
   early.
2. **Do minor lords fight?** If a lord adds Military to a combat, the
   Military track has a body attached to it; if not, lords are purely
   political and combat stays abstract.
3. **What is a lord's own personality?** The seven cards needing a new
   condition want lords who are in debt, afraid, ambitious or owed a
   favour. That is a deck to write, not a rule to fix.

## Recommendations on the three open questions

**1. Keep the tax, and make it take Levy.**

Measured, over three ranges of 300 seeds: a Standing tax gives a 14.6x
win spread and hands the first Lord Paramount 54% of games; a Levy tax
gives 1.55x and 21–26%. That alone settles it.

But electing the title changes *why*. With Accession by Total Power the
tax was a feedback loop — the title fed the tax and the tax fed the
title. With an elected Paramount the tax becomes the **prize for winning
the Council**, and the Council needs a prize or it decides nothing. So
the tax should stay, and stop compounding. Taking Levy means the title
buys you a good turn rather than a permanent lead, which is the tempo
shape this game is converting towards.

**2. Lords do not fight.**

The game's cleanest line is that Military fights, Capital votes and Gold
buys. If lords both vote *and* fight they collapse two of those into one
card type, and the three Standings stop being three things.

Leave lords political. They vote at Councils, they pay dues in Levy, and
they can be turned against you. **Gold binds them, Capital is what they
produce, and Military never touches them.** Each resource keeps its own
layer, and combat stays what it already is — two Houses committing Levy
in the dark, which is the mechanic that works best at two players.

The design's *"fights for the Lord once"* survives as an exception a few
cards grant, not as what a lord is for.

**3. A lord's personality is a trait other cards read.**

`<card_schema>` already has `reads`, and the engine already carries
target conditions — `TC_BROKEN1`, `TC_UNREST3`, `TC_DEBT`. The seven
cards needing a new condition want the same mechanism pointed at a lord
instead of a House.

So a minor lord carries one trait: **In Debt, Afraid, Ambitious, Proud,
Owed**. `Fear` binds an Afraid lord where it used to want a House at 3
Unrest. `A Debt` binds one In Debt. `A Favour` binds one who is Owed.
Nothing new is invented — an existing mechanism gets a new subject, and
the lords become characters rather than counters because their trait is
the reason you can reach them.

### Why this direction rather than another

It recovers **121 of 124 seed cards** and leaves no deck without a
purpose, which is not a small thing: the alternative directions all
involved deleting a third of the game and writing replacements. This one
mostly re-points sentences that were already about a subject rather than
an opponent.

And it produces the Magic shape without imitating it. A duel where the
things you build, bind, corrupt and lose sit in front of you on the
table; where the resource that buys them, the resource they produce and
the resource that fights are three different resources; and where the
one thing you cannot see — whether a lord has turned — is revealed by
what it does in public rather than by a number.

## Proposal: fewer decks

Seventeen decks works on a table, where a deck is a physical pile you
point at. On a screen every deck is a menu item, and a player choosing
one Draw action a turn is reading **nine options** before they can act.

The merge follows something the design already decided: **deck access is
priced in a resource.** War costs Military, Political costs Capital,
Intrigue costs Gold. Every other drawable deck is a satellite of one of
those three, so let it be part of it.

| becomes | absorbs | cards |
|---|---|---|
| **Your House** | — | 400 |
| **War** — 1 Military | Combat | 150 |
| **Political** — 1 Capital | Council, Promise | 170 |
| **Intrigue** — 1 Gold | Lever, Minor Lords, Instigator | 220 |
| **World** — free | Cataclysm | 110 |
| Ambition — 2 Capital, *only while you hold an unfinished one* | — | 30 |
| Throne — *only while Throneworthy* | — | 15 |
| The Court — *turned by the Court, never drawn* | — | 30 |
| The Dead — *a variant; death ends the standard game* | Ghost, Resurrection | 70 |

**Nine deck slots instead of seventeen, and five draw choices instead of
nine** — because Ambition and Throne only appear in the menu when you
qualify for them, and the last two are never drawn at all.

What each choice now means is also cleaner than it was:

* **Military** buys you a fight and the cards that win one.
* **Capital** buys you the Council, and the promises that decide it.
* **Gold** buys you lords, the levers that reach them, and the whispers
  that turn them.
* **Free** buys you the World, which acts on both of you.

That is one sentence per resource, which is what a player has to hold in
their head, and it is the same division the rest of the design already
makes: *Military fights, Capital votes, Gold buys.*

### What it costs

Nothing in the card pool — every card keeps its text and its cost; only
which pile it sits in changes. It costs `<deck_access>`, `<components>`
and `<decks>` a rewrite, and the engine one table.

### What it does not solve

The four House decks are still 400 cards of the 1,195, and a House deck
is the one pile a player never chooses *between*. If the manifest is
still too large after this, that is where the next hundred come from.
