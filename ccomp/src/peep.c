/*
 * Stage 3: peephole optimizer. Holds a small window of machine ops and
 * passes them on to the emitter. Milestone 1 has no patterns yet, so
 * code passes through unchanged; the window and the barrier rule are
 * in place for later.
 */
#include "ccish.h"

#define WINDOW 4

static Mop win[WINDOW];
static int nwin;

static void pass_oldest(void)
{
    int i;
    emit_put(&win[0]);
    for (i = 1; i < nwin; i++)
        win[i - 1] = win[i];
    nwin--;
}

void peep_flush(void)
{
    while (nwin > 0)
        pass_oldest();
}

void peep_put(Mop *m)
{
    /* Pseudo-ops (and, later, labels) are barriers: never optimize
     * across them. */
    if (m->op >= OP_FUNC_BEGIN) {
        peep_flush();
        emit_put(m);
        return;
    }
    if (nwin == WINDOW)
        pass_oldest();
    win[nwin++] = *m;
    /* Patterns that rewrite the end of the window go here. */
}
