/* The machine seat: the AI wearing the Seat interface.
 *
 * Nothing here decides anything.  Every function hands the question
 * straight to ai.c, and exists so that the engine can ask a seat rather
 * than asking the AI -- which is the whole difference between a game
 * that plays itself and a game a person can sit down at.
 *
 * A human seat replaces these five function pointers and nothing else.
 * A remote seat replaces them with something that writes to a socket and
 * waits.  The engine cannot tell, because every one of them takes a View
 * and a View is all any seat is entitled to.
 */
#include "court.h"

static unsigned long seat_rng = 0x2545F4914F6CDD1DUL;

static Action m_choose(Seat *s, const View *v, const Action *opts, int n)
{
    (void)s;
    return ai_choose(v, opts, n, &seat_rng);
}
static int m_commit(Seat *s, const View *v, int max, int defending)
{
    (void)s;
    return ai_commit(v, max, defending);
}
static int m_keep(Seat *s, const View *v, int i)
{
    (void)s;
    return ai_keep_promise(v, i);
}
static int m_accept(Seat *s, const View *v, int promiser, int term,
                    int cons_res, int cons_amt)
{
    (void)s;
    return ai_accept_promise(v, promiser, term, cons_res, cons_amt);
}
static int m_lord(Seat *s, const View *v, int holder, int lord_idx)
{
    (void)s;
    return ai_side(v, holder, lord_idx);
}

void court_seat_ai(Seat *s)
{
    s->ctx            = 0;
    s->choose         = m_choose;
    s->commit         = m_commit;
    s->keep_promise   = m_keep;
    s->accept_promise = m_accept;
    s->lord_turns     = m_lord;
}
