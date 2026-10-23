
/* time.c: ANSI draft (X3J11 Oct 86) section 4.12 code */
/* Copyright (C) A.C. Norman and A. Mycroft */
/* version 0.02a */
/* $Id: time.c,v 1.2 1992/08/25 17:15:58 craig Exp $ */

#include <time.h>
#include <stdio.h>
#include <string.h>

/* NB strftime() is in locale.c since it seems VERY locale-dependent      */

/* In NorCroft C time() yields the unix result of an unsigned int holding */
/* seconds since 1 Jan 1970.                                              */
/* clock() returns an unsigned int with ticks of cpu time.                */

/* N.B. clock() and time() are defined in opsys.c                         */

/* get past Helios/ARM compiler bug - no statics in fns */
static char _timebuf[26+(8+3*9+7)];  /* slop in case illegal args */

char *asctime(const struct tm *timeptr)
{
    sprintf(_timebuf, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n",
       "SunMonTueWedThuFriSat" + (timeptr -> tm_wday)*3,
       "JanFebMarAprMayJunJulAugSepOctNovDecBad" + (timeptr -> tm_mon)*3,
       timeptr -> tm_mday,
       timeptr -> tm_hour, timeptr -> tm_min, timeptr -> tm_sec,
       timeptr -> tm_year + 1900);
    return _timebuf;
}

char *ctime(const time_t *timer)
{   return asctime(localtime(timer));
}

/* Calendar and timezone conversion are in timezone.c. */
