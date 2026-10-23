/* Private interface between time conversion and locale formatting. */
#ifndef HELIOS_TIMEZONE_H
#define HELIOS_TIMEZONE_H
#include <time.h>
extern void _tzinfo(const struct tm *, long *, char *, unsigned);
#endif
