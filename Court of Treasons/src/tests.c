/* What can be checked without playing a game: the tables, the classifier's
 * output, and the two structural rules the design turns on.
 *
 * `make check` plays 200 games with the invariants armed.  This is the
 * part that fails faster than that.
 */
#include "court.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define T(cond, ...) do { if (!(cond)) { \
        printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } \
    } while (0)

int main(void)
{
    int i, j, d, n;

    /* The opcode name table is indexed by the enum and the compiler will
     * not check that it stays aligned.  A hole prints the wrong effect in
     * every log line after it, which survives a long time unnoticed. */
    for (i = 0; i < OP_COUNT; i++)
        T(OP_NAME[i] && OP_NAME[i][0], "OP_NAME has a hole at %d", i);

    /* Deck bounds: DECK_FIRST and DECK_SIZE are pointer arithmetic, and
     * gen-cards.py asserts the decks are contiguous when it writes them. */
    for (d = 0, n = 0; d < D_COUNT; d++) {
        T(DECK_FIRST[d] >= 0 && DECK_FIRST[d] + DECK_SIZE[d] <= CARD_COUNT,
          "deck %d runs off the end of the table", d);
        for (i = 0; i < DECK_SIZE[d]; i++)
            T(CARDS[DECK_FIRST[d] + i].deck == d,
              "card %s is in deck %d but filed under %d",
              CARDS[DECK_FIRST[d] + i].id, CARDS[DECK_FIRST[d] + i].deck, d);
        n += DECK_SIZE[d];
    }
    T(n == CARD_COUNT, "decks hold %d cards, the table has %d", n, CARD_COUNT);

    /* "A card that cannot be carried out in the current state is inert."
     * A card with no effect at all is inert in every state, which means
     * the classifier missed it -- and an OP_NONE card would be counted
     * against the inert-play measure as if the design were at fault. */
    for (i = 0; i < CARD_COUNT; i++)
        T(CARDS[i].eff[0].op != OP_NONE,
          "%s (%s) compiled to no effect at all", CARDS[i].id, CARDS[i].name);

    /* Every card must be payable by somebody at some point: a cost above
     * the Standing ceiling can never be met. */
    for (i = 0; i < CARD_COUNT; i++)
        for (j = 0; j < RC_COUNT; j++) {
            if (j == RC_GRV)
                T(CARDS[i].cost[j] <= GRIEVANCE_MAX,
                  "%s costs more Grievance than can be held", CARDS[i].id);
            else
                T(CARDS[i].cost[j] <= STANDING_MAX,
                  "%s costs more Levy than Standing can ever grant",
                  CARDS[i].id);
        }

    /* Ambition cards are checked, never played, so none may carry a cost
     * or sit in a deck a House can draw into its hand for playing. */
    for (i = DECK_FIRST[D_AMBITION];
         i < DECK_FIRST[D_AMBITION] + DECK_SIZE[D_AMBITION]; i++) {
        T(CARDS[i].type == T_AMBITION, "%s is in the Ambition deck but is"
          " type %d", CARDS[i].id, CARDS[i].type);
        T(CARDS[i].eff[0].op >= OP_AMB_STANDING,
          "%s is an Ambition with a playable effect", CARDS[i].id);
    }

    /* The View is the whole of the hidden-information defence, so it gets
     * a test rather than a comment.  Build a game, put a Bond on the
     * table that nobody has revealed, and check that the House it is
     * aimed at cannot see the Servitude and the Lord cannot see the
     * Revolution. */
    {
        static Game g;
        View v;
        court_seed(&g, 12345);
        court_setup(&g, 2, 0);
        T(g.lord_n > 0, "setup dealt no Minor Lords");
        g.lord[0].holder = 0;
        g.lord[0].servitude = 3;
        g.lord[0].revolution = 4;

        court_view(&g, 0, &v);          /* the House holding it */
        T(v.lord[0].servitude == 3, "a House cannot see the Bond it holds");
        T(v.lord[0].revolution == -1,
          "a House can see a Revolution count it has not spied");

        court_view(&g, 1, &v);          /* the other House */
        T(v.lord[0].servitude == -1,
          "the other House sees a Servitude count that is still hidden");
        T(v.lord[0].revolution == -1,
          "the other House sees a Revolution count it has no right to");
        T(v.hand_n == g.h[1].hand_n, "a seat cannot see its own hand");

        /* And once the House has spied, and only then. */
        g.lord[0].lord_knows = 1;
        court_view(&g, 0, &v);
        T(v.lord[0].revolution == 4, "spying told the House nothing");

        /* Trust is derived from the piles and nothing else, and zero is
         * ignorance rather than suspicion. */
        T(court_trust(&g, 0, 1) == 0, "a House that has never dealt with"
          " you is not at zero trust");
    }

    /* The seat interface is what a UI will hold, so it gets a test: a
     * fresh game must have both seats answerable, and replacing one must
     * not disturb the other.  A null callback falls back to the machine,
     * which is what makes a one-human game a one-field change. */
    {
        static Game g;
        int i;
        court_seed(&g, 99);
        court_setup(&g, 2, 0);
        for (i = 0; i < 2; i++) {
            T(g.seat[i].choose && g.seat[i].commit && g.seat[i].keep_promise
              && g.seat[i].accept_promise && g.seat[i].lord_turns,
              "seat %d was not filled with the machine's answers", i);
        }
        g.seat[0].choose = 0;           /* a seat nobody has taken yet   */
        T(g.seat[1].choose != 0, "clearing one seat disturbed the other");
        while (!court_round(&g))
            ;
        T(g.winner >= 0, "a game with an unanswered seat did not finish");
    }

    if (fails) printf("%d failures\n", fails);
    else       printf("tables, classifier output and the View: all clear\n");
    return fails != 0;
}
