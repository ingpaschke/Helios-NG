/*
 * colib_linux.c -- coroutine library for the Linux port of the Helios
 * I/O Server, replacing sun/colib.c.
 *
 * Implements the interface cofuns.c expects (InitCo, CreateCo, CallCo,
 * WaitCo, DeleteCo, CurrentCo) using glibc ucontext: getcontext, makecontext
 * and swapcontext.
 *
 * Semantics (same as the old colib.c variants):
 *   CallCo(co, arg)  switches from the current coroutine into co, which
 *                    receives arg as the return value of its WaitCo (or,
 *                    on the very first call, starts running its function;
 *                    cofuns.c's new_coroutine() ignores that argument).
 *                    Returns the argument co passes to its next WaitCo.
 *   WaitCo(arg)      switches back to whoever last CallCo'd the current
 *                    coroutine, handing it arg.
 *   DeleteCo(co)     frees a coroutine that is not running (cofuns.c calls
 *                    it from the parent after the child did
 *                    WaitCo(CoDeleted)).
 *
 * Stack size: the size passed to CreateCo comes from host.con
 * "coroutine_stack" (cofuns.c default 50000 on UNIX). glibc stdio and
 * printf-heavy server code on a 32-bit host can use a lot more than that,
 * so the stack is max(size, CO_MIN_STACK). A coroutine function that
 * returns falls into a trampoline loop that keeps reporting CoDeleted.
 */

#include "helios.h"
#include <ucontext.h>

#define CO_MIN_STACK (256 * 1024)
#define CoDeleted (123456L)

typedef struct coroutine {
    ucontext_t ctx;
    struct coroutine *parent;
    char *stack;
    void (*func)();
} coroutine;

WORD WaitCo(WORD arg);

PRIVATE coroutine *RootCo;
coroutine *CurrentCo;
PRIVATE WORD current_arg;

PRIVATE void co_trampoline(void)
{
    coroutine *self = CurrentCo;
    (*self->func)();
    for (;;) { /* function returned: behave like Seppuku() */
        WaitCo(CoDeleted);
    }
}

WORD InitCo(void)
{
    RootCo = (coroutine *)calloc(1, sizeof(coroutine));
    if (RootCo == NULL) {
        return 0L;
    }
    RootCo->parent = RootCo;
    CurrentCo = RootCo;
    return 1L;
}

ptr CreateCo(VoidFnPtr function, WORD size)
{
    coroutine *co;
    size_t stk = (size_t)size;

    if (stk < CO_MIN_STACK) {
        stk = CO_MIN_STACK;
    }

    co = (coroutine *)calloc(1, sizeof(coroutine));
    if (co == NULL) {
        return NULL;
    }
    co->stack = (char *)malloc(stk);
    if (co->stack == NULL) {
        free(co);
        return NULL;
    }
    co->func = (void (*)())function;
    co->parent = CurrentCo;

    if (getcontext(&co->ctx) != 0) {
        ServerDebug("server: getcontext failed, errno %d", errno);
        exit(10);
    }
    co->ctx.uc_stack.ss_sp = co->stack;
    co->ctx.uc_stack.ss_size = stk;
    co->ctx.uc_link = NULL;
    makecontext(&co->ctx, co_trampoline, 0);
    return (ptr)co;
}

WORD CallCo(ptr cortn, WORD arg)
{
    coroutine *co = (coroutine *)cortn;

    co->parent = CurrentCo;
    CurrentCo = co;
    current_arg = arg;
    if (swapcontext(&co->parent->ctx, &co->ctx) != 0) {
        ServerDebug("server: swapcontext failed, errno %d", errno);
        exit(10);
    }
    return current_arg;
}

WORD WaitCo(WORD arg)
{
    coroutine *co = CurrentCo;

    current_arg = arg;
    CurrentCo = co->parent;
    if (swapcontext(&co->ctx, &co->parent->ctx) != 0) {
        ServerDebug("server: swapcontext failed, errno %d", errno);
        exit(10);
    }
    return current_arg;
}

WORD DeleteCo(ptr cortn)
{
    coroutine *co = (coroutine *)cortn;

    if (co == NULL || co == RootCo || co == CurrentCo) {
        return FALSE;
    }
    free(co->stack);
    free(co);
    return TRUE;
}
