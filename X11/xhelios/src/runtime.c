/* Helios process setup and portable text RGB database lookup. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "X.h"
#include "os.h"
#include "opaque.h"

/* Retained for the upstream argument parser; Helios has no setrlimit(). */
int limitDataSpace = -1;
int limitStackSpace = -1;

void OsInit(void)
{
    setvbuf(stderr, NULL, _IONBF, 0);
    heliosTraceInit();
    HTRACE("generation", serverGeneration, 0, 0, 0);
}

int getdtablesize(void) { return 64; }
long random(void) { return rand(); }
double hypot(double x, double y)
{
    double temp;
    if (x < 0) x = -x;
    if (y < 0) y = -y;
    if (x < y) { temp = x; x = y; y = temp; }
    if (x == 0) return 0;
    y /= x;
    return x * sqrt(1 + y * y);
}

int OsLookupColor(int screen, char *name, unsigned len,
                  unsigned short *red, unsigned short *green, unsigned short *blue)
{
    FILE *file;
    char path[512], line[512], colour[256];
    unsigned r, g, b, i;
    int found = 0;
    if (len >= sizeof(colour) || strlen(rgbPath) + 5 >= sizeof(path)) return 0;
    sprintf(path, "%s.txt", rgbPath);
    file = fopen(path, "r");
    if (!file) return 0;
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "%u %u %u %255[^\r\n]", &r, &g, &b, colour) != 4)
            continue;
        if (r > 255 || g > 255 || b > 255 || strlen(colour) != len) continue;
        for (i = 0; i < len; ++i)
            if (tolower((unsigned char)name[i]) != tolower((unsigned char)colour[i])) break;
        if (i != len) continue;
        *red = r * 257;
        *green = g * 257;
        *blue = b * 257;
        found = 1;
        break;
    }
    fclose(file);
    return found;
}
