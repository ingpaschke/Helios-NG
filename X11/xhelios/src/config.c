/* newXrc lookup: case-insensitive names, first occurrence wins, quoted and
 * escaped tokens, # comments, NAME / !NAME booleans. No input is modified. */
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include "config.h"

static void skipSpace(const char **p)
{
    for (;;) {
        while (**p && isspace((unsigned char)**p)) ++*p;
        if (**p != '#') break;
        while (**p && **p != '\n') ++*p;
    }
}

static int token(const char **input, char *out, unsigned capacity)
{
    const char *p = *input;
    unsigned used = 0;
    int quoted = 0, saw = 0;
    while (*p) {
        unsigned char c = *p;
        if (!quoted && (isspace(c) || c == '#' || c == '=')) break;
        ++p;
        if (c == '"') { quoted = !quoted; saw = 1; continue; }
        if (c == '\\') {
            if (!*p) return 0;
            c = *p++;
        }
        if (used + 1 >= capacity) return 0;
        out[used++] = c;
        saw = 1;
    }
    if (quoted || !saw) return 0;
    out[used] = 0;
    *input = p;
    return 1;
}

static int equalName(const char *a, const char *b)
{
    while (*a && *b && toupper((unsigned char)*a) == toupper((unsigned char)*b)) {
        ++a; ++b;
    }
    return !*a && !*b;
}

const char *heliosConfigValue(const char *buffer, const char *name)
{
    static char value[256];
    char key[256];
    const char *p = buffer;
    int negative;
    if (!buffer || !name || !*name) return NULL;
    while (*p) {
        skipSpace(&p);
        if (!*p || (unsigned char)*p == 26) break;
        negative = *p == '!';
        if (negative) ++p;
        if (!token(&p, key, sizeof(key))) return NULL;
        /* Keep the next line separate from a boolean setting. */
        while (*p == ' ' || *p == '\t' || *p == '\r') ++p;
        if (*p == '=') {
            ++p;
            while (*p == ' ' || *p == '\t') ++p;
            if (!token(&p, value, sizeof(value))) return NULL;
        } else {
            strcpy(value, negative ? "FALSE" : "TRUE");
        }
        if (equalName(key, name)) return value;
        /* Extra words (e.g. AUTO_REPEAT_FILTER's list) belong to this line. */
        while (*p && *p != '\n') ++p;
    }
    return NULL;
}
