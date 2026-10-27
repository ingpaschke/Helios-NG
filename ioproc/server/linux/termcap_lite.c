/*
 * termcap_lite.c -- minimal built-in termcap for the Linux port of the
 * Helios I/O Server. Implements the capabilities used by the host console
 * without a separate termcap library: tgetent, tgetstr, tgetnum, tgetflag,
 * tgoto and tputs. ANSI-compatible terminals include xterm, vt100, linux,
 * screen and tmux.
 *
 * Key capabilities are the sequences a modern terminal emulator sends in
 * normal (non-application) cursor mode; "ks"/"ke" are deliberately absent
 * so the terminal is never switched into application keypad mode.
 * Lines/columns come from TIOCGWINSZ on stdout, else $LINES/$COLUMNS,
 * else 24x80.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct tc_str {
    const char *id;
    const char *val;
};

static const struct tc_str tc_strings[] = {
    {"cl", "\033[H\033[2J"}, {"ce", "\033[K"},   {"cd", "\033[J"},
    {"cm", "\033[%i%d;%dH"}, {"bl", "\007"},     {"mr", "\033[7m"},
    {"me", "\033[m"},        {"so", "\033[7m"},  {"se", "\033[m"},
    {"ku", "\033[A"},        {"kd", "\033[B"},   {"kr", "\033[C"},
    {"kl", "\033[D"},        {"kh", "\033[H"},   {"@7", "\033[F"},
    {"kI", "\033[2~"},       {"kP", "\033[5~"},  {"kN", "\033[6~"},
    {"k1", "\033OP"},        {"k2", "\033OQ"},   {"k3", "\033OR"},
    {"k4", "\033OS"},        {"k5", "\033[15~"}, {"k6", "\033[17~"},
    {"k7", "\033[18~"},      {"k8", "\033[19~"}, {"k9", "\033[20~"},
    {"k;", "\033[21~"},      {NULL, NULL}};

int tgetent(char *bp, const char *name)
{
    (void)name;
    if (bp) {
        bp[0] = '\0';
    }
    return 1; /* every terminal is "ANSI" */
}

char *tgetstr(const char *id, char **area)
{
    const struct tc_str *t;
    for (t = tc_strings; t->id; t++) {
        if (t->id[0] == id[0] && t->id[1] == id[1]) {
            char *dst;
            size_t n = strlen(t->val) + 1;
            if (area && *area) {
                dst = *area;
                *area += n;
            } else {
                dst = (char *)malloc(n);
            }
            if (!dst) {
                return NULL;
            }
            memcpy(dst, t->val, n);
            return dst;
        }
    }
    return NULL;
}

int tgetnum(const char *id)
{
    struct winsize ws;
    const char *env;

    if (id[0] == 'l' && id[1] == 'i') {
        if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) {
            return ws.ws_row;
        }
        if ((env = getenv("LINES")) != NULL && atoi(env) > 0) {
            return atoi(env);
        }
        return 24;
    }
    if (id[0] == 'c' && id[1] == 'o') {
        if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            return ws.ws_col;
        }
        if ((env = getenv("COLUMNS")) != NULL && atoi(env) > 0) {
            return atoi(env);
        }
        return 80;
    }
    return -1;
}

int tgetflag(const char *id)
{
    return (id[0] == 'a' && id[1] == 'm'); /* auto margins */
}

/* Only the "cm" form \E[%i%d;%dH is ever produced here. */
char *tgoto(const char *cap, int col, int row)
{
    static char buf[32];
    (void)cap;
    snprintf(buf, sizeof buf, "\033[%d;%dH", row + 1, col + 1);
    return buf;
}

int tputs(const char *str, int affcnt, int (*putc_fn)(int))
{
    (void)affcnt;
    if (str == NULL) {
        return -1;
    }
    while (*str >= '0' && *str <= '9') {
        str++; /* strip padding */
    }
    while (*str) {
        (*putc_fn)((unsigned char)*str++);
    }
    return 0;
}
