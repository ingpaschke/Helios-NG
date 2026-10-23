/* Timezone conversion for the unsigned, 32-bit Helios time_t ABI.
 * Timestamps stay in UTC. TZ selects a POSIX rule or an installed TZif file;
 * /helios/etc/timezone supplies the default when TZ is absent.
 * All arithmetic uses 32-bit days and seconds, including TZif v2-v4 input.
 */
#include "timezone.h"
#include <errno.h>
#include <limits.h>
#include <sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef TZDIR
#define TZDIR "/helios/etc/zoneinfo/"
#endif
#ifndef TZDEFAULT
#define TZDEFAULT "/helios/etc/timezone"
#endif
#define TZ_PATH 512
#define TZ_NAME 64
#define TZ_FILE_MAX 131072
#define DAY_SECONDS 86400

struct date {
    int day, sec;
};
struct rule {
    int kind, month, week, day, sec;
};
struct zone {
    long offset;
    int dst;
    const char *name;
};
static Semaphore tzlock = {1, NULL, (struct Id *)&tzlock};
static unsigned char *data;
static unsigned count, type_count, char_count, width;
static const unsigned char *trans, *indices, *types, *names;
static int rules;
static struct rule start_rule, end_rule;
static char standard[TZ_NAME] = "UTC", summer[TZ_NAME] = "UTC";
static long std_offset, dst_offset;
static char previous[TZ_PATH], default_name[TZ_PATH];
static int have_previous, have_default;
static struct tm local_tm, utc_tm;
/* Public data slots are allocated by clibstar.a. */

static int leap(int y) { return !(y % 4) && (y % 100 || !(y % 400)); }
static int month_days(int m, int y)
{
    static const unsigned char lengths[] = {31, 28, 31, 30, 31, 30,
                                            31, 31, 30, 31, 30, 31};
    return lengths[m] + (m == 1 && leap(y));
}
static int year_day(int y)
{
    int p = y - 1;
    return 365 * (y - 1970) + p / 4 - p / 100 + p / 400 - 477;
}
static int month_day(int y, int m)
{
    int d = year_day(y), i;
    for (i = 0; i < m; i++)
        d += month_days(i, y);
    return d;
}
static struct date add_seconds(struct date d, long n)
{
    d.day += (int)(n / DAY_SECONDS);
    d.sec += (int)(n % DAY_SECONDS);
    if (d.sec < 0) {
        d.sec += DAY_SECONDS;
        d.day--;
    }
    if (d.sec >= DAY_SECONDS) {
        d.sec -= DAY_SECONDS;
        d.day++;
    }
    return d;
}
static int compare(struct date a, struct date b)
{
    if (a.day != b.day)
        return a.day < b.day ? -1 : 1;
    return (a.sec > b.sec) - (a.sec < b.sec);
}
static int utc_date(const time_t *t, struct date *d)
{
    unsigned value = (unsigned)*t;
    if ((time_t)value != *t) {
        errno = ERANGE;
        return 0;
    }
    d->day = value / DAY_SECONDS;
    d->sec = value % DAY_SECONDS;
    return 1;
}
static int timestamp(struct date d, time_t *t)
{
    unsigned max = (unsigned)-1;
    if (d.day < 0 || (unsigned)d.day > max / DAY_SECONDS ||
        ((unsigned)d.day == max / DAY_SECONDS && (unsigned)d.sec > max % DAY_SECONDS))
        return 0;
    *t = (time_t)((unsigned)d.day * DAY_SECONDS + (unsigned)d.sec);
    return 1;
}
static void breakdown(struct date d, int dst, struct tm *tm)
{
    int y = 1970 + d.day / 366, first;
    while (year_day(y) > d.day)
        y--;
    while (year_day(y + 1) <= d.day)
        y++;
    first = year_day(y);
    tm->tm_year = y - 1900;
    tm->tm_yday = d.day - first;
    tm->tm_wday = (d.day + 4) % 7;
    if (tm->tm_wday < 0)
        tm->tm_wday += 7;
    d.day -= first;
    tm->tm_mon = 0;
    while (d.day >= month_days(tm->tm_mon, y))
        d.day -= month_days(tm->tm_mon++, y);
    tm->tm_mday = d.day + 1;
    tm->tm_hour = d.sec / 3600;
    tm->tm_min = d.sec / 60 % 60;
    tm->tm_sec = d.sec % 60;
    tm->tm_isdst = dst;
}
static int add_day(int *a, int b)
{
    if ((b > 0 && *a > INT_MAX - b) || (b < 0 && *a < INT_MIN - b))
        return 0;
    *a += b;
    return 1;
}
static int normalize(const struct tm *tm, struct date *d)
{
    int y = tm->tm_year, m = tm->tm_mon, adj = m / 12, n;
    m %= 12;
    if (m < 0) {
        m += 12;
        adj--;
    }
    if (!add_day(&y, adj) || y < -1899 || y > 8099)
        return 0;
    y += 1900;
    d->day = month_day(y, m);
    d->sec = 0;
    if (!add_day(&d->day, tm->tm_mday) || !add_day(&d->day, -1))
        return 0;
    n = tm->tm_hour / 24;
    if (!add_day(&d->day, n))
        return 0;
    d->sec = (tm->tm_hour % 24) * 3600;
    n = tm->tm_min / 1440;
    if (!add_day(&d->day, n))
        return 0;
    d->sec += (tm->tm_min % 1440) * 60;
    n = tm->tm_sec / DAY_SECONDS;
    if (!add_day(&d->day, n))
        return 0;
    d->sec += tm->tm_sec % DAY_SECONDS;
    n = d->sec / DAY_SECONDS;
    d->sec %= DAY_SECONDS;
    if (d->sec < 0) {
        d->sec += DAY_SECONDS;
        n--;
    }
    if (!add_day(&d->day, n))
        return 0;
    /* No supported offset can bring a more distant date into time_t. */
    if (d->day < -7 || d->day > (int)((unsigned)-1 / DAY_SECONDS) + 7)
        return 0;
    return 1;
}

static unsigned read32(const unsigned char *p)
{
    return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3];
}
static int signed32(const unsigned char *p)
{
    unsigned n = read32(p);
    return n & 0x80000000U ? -1 - (int)(~n) : (int)n;
}
static struct date transition(unsigned i)
{
    const unsigned char *p = trans + i * width;
    int high = width == 8 ? signed32(p) : (p[0] & 128 ? -1 : 0);
    unsigned low = read32(p + width - 4);
    struct date d;
    if (high) {
        d.day = high < 0 ? INT_MIN / 2 : INT_MAX / 2;
        d.sec = 0;
    } else {
        d.day = low / DAY_SECONDS;
        d.sec = low % DAY_SECONDS;
    }
    return d;
}
static int block_size(const unsigned char *h, unsigned avail, unsigned scale,
                      unsigned *size)
{
    unsigned g = read32(h + 20), s = read32(h + 24), l = read32(h + 28);
    unsigned t = read32(h + 32), n = read32(h + 36), c = read32(h + 40);
    if (g > 256 || s > 256 || l > 4096 || t > 8192 || !n || n > 256 || !c || c > 4096)
        return 0;
    if ((g && g != n) || (s && s != n))
        return 0;
    *size = t * (scale + 1) + 6 * n + c + l * (scale + 4) + g + s;
    return *size <= avail;
}
static unsigned char *read_file(const char *path, unsigned *length)
{
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *p = NULL;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 44 && size <= TZ_FILE_MAX &&
        fseek(f, 0, SEEK_SET) == 0) {
        p = malloc((unsigned)size + 1);
        if (p && fread(p, 1, (unsigned)size, f) != (unsigned)size) {
            free(p);
            p = NULL;
        }
        if (p) {
            p[size] = 0;
            *length = (unsigned)size;
        }
    }
    fclose(f);
    return p;
}
static void copy_name(char *dest, const char *src)
{
    strncpy(dest, src, TZ_NAME - 1);
    dest[TZ_NAME - 1] = 0;
}
static int number(const char **p, int max, int *result)
{
    int n = 0, digits = 0;
    while (**p >= '0' && **p <= '9') {
        int d = *(*p)++ - '0';
        if (n > (max - d) / 10)
            return 0;
        n = n * 10 + d;
        digits++;
    }
    *result = n;
    return digits && n <= max;
}
static int name(const char **p, char *out)
{
    int n = 0, bracket = (**p == '<');
    if (bracket)
        (*p)++;
    while (**p &&
           (bracket ? **p != '>'
                    : ((**p >= 'A' && **p <= 'Z') || (**p >= 'a' && **p <= 'z')))) {
        if (n >= TZ_NAME - 1)
            return 0;
        out[n++] = *(*p)++;
    }
    out[n] = 0;
    if (bracket && *(*p)++ != '>')
        return 0;
    return n >= 3;
}
static int offset(const char **p, int maxhour, long *out)
{
    int sign = 1, h, m = 0, s = 0;
    if (**p == '-' || **p == '+') {
        if (**p == '-')
            sign = -1;
        (*p)++;
    }
    if (!number(p, maxhour, &h))
        return 0;
    if (**p == ':') {
        (*p)++;
        if (!number(p, 59, &m))
            return 0;
    }
    if (**p == ':') {
        (*p)++;
        if (!number(p, 59, &s))
            return 0;
    }
    *out = sign * (h * 3600L + m * 60L + s);
    return 1;
}
static int rule(const char **p, struct rule *r)
{
    long sec = 7200;
    memset(r, 0, sizeof(*r));
    if (**p == 'M') {
        r->kind = 'M';
        (*p)++;
        if (!number(p, 12, &r->month) || !r->month || *(*p)++ != '.' ||
            !number(p, 5, &r->week) || !r->week || *(*p)++ != '.' ||
            !number(p, 6, &r->day))
            return 0;
    } else {
        r->kind = **p == 'J' ? 'J' : 'D';
        if (r->kind == 'J')
            (*p)++;
        if (!number(p, r->kind == 'J' ? 365 : 365, &r->day) ||
            (r->kind == 'J' && !r->day))
            return 0;
    }
    if (**p == '/') {
        (*p)++;
        if (!offset(p, 167, &sec))
            return 0;
    }
    r->sec = (int)sec;
    return 1;
}
static int posix_rule(const char *p)
{
    char a[TZ_NAME], b[TZ_NAME];
    long west = 0, dstwest;
    struct rule first, last;
    if (!name(&p, a))
        return 0;
    if (*p && !offset(&p, 24, &west))
        return 0;
    dstwest = west;
    b[0] = 0;
    if (*p) {
        if (!name(&p, b))
            return 0;
        dstwest = west - 3600;
        if (*p && *p != ',' && !offset(&p, 24, &dstwest))
            return 0;
        if (!*p)
            p = ",M3.2.0,M11.1.0";
        if (*p++ != ',' || !rule(&p, &first) || *p++ != ',' || !rule(&p, &last) || *p)
            return 0;
    }
    copy_name(standard, a);
    copy_name(summer, b[0] ? b : a);
    timezone = west;
    std_offset = -west;
    dst_offset = -dstwest;
    daylight = b[0] != 0;
    rules = 1;
    if (daylight) {
        start_rule = first;
        end_rule = last;
    }
    return 1;
}
static int tzfile(const char *path)
{
    unsigned length, size, base = 0, i, tail, scale = 4;
    unsigned char *p = read_file(path, &length);
    const unsigned char *h;
    char footer[256];
    if (!p)
        return 0;
    if (memcmp(p, "TZif", 4))
        goto bad;
    if (p[4] && p[4] != '2' && p[4] != '3' && p[4] != '4')
        goto bad;
    if (!block_size(p, length - 44, 4, &size))
        goto bad;
    if (p[4]) {
        base = 44 + size;
        if (length - base < 44 || memcmp(p + base, "TZif", 4))
            goto bad;
        scale = 8;
        if (!block_size(p + base, length - base - 44, 8, &size))
            goto bad;
    }
    h = p + base;
    count = read32(h + 32);
    type_count = read32(h + 36);
    char_count = read32(h + 40);
    trans = h + 44;
    width = scale;
    indices = trans + count * scale;
    types = indices + count;
    names = types + 6 * type_count;
    for (i = 0; i < type_count; i++) {
        const unsigned char *t = types + 6 * i;
        int off = signed32(t);
        if (t[4] > 1 || t[5] >= char_count || off < -604800 || off > 604800 ||
            !memchr(names + t[5], 0, char_count - t[5]))
            goto bad;
    }
    for (i = 0; i < count; i++) {
        if (indices[i] >= type_count)
            goto bad;
        if (i) {
            const unsigned char *a = trans + (i - 1) * scale, *b = trans + i * scale;
            int ah = scale == 8 ? signed32(a) : (a[0] & 128 ? -1 : 0);
            int bh = scale == 8 ? signed32(b) : (b[0] & 128 ? -1 : 0);
            if (ah > bh || (ah == bh && read32(a + scale - 4) >= read32(b + scale - 4)))
                goto bad;
        }
    }
    data = p;
    daylight = 0;
    for (i = 0; i < type_count; i++) {
        const unsigned char *t = types + 6 * i;
        if (t[4]) {
            dst_offset = signed32(t);
            copy_name(summer, (const char *)names + t[5]);
            daylight = 1;
        } else {
            std_offset = signed32(t);
            copy_name(standard, (const char *)names + t[5]);
        }
    }
    if (!daylight) {
        dst_offset = std_offset;
        copy_name(summer, standard);
    }
    timezone = -std_offset;
    tail = base + 44 + size;
    if (scale == 8 && tail < length && p[tail] == '\n' && p[length - 1] == '\n' &&
        length - tail - 2 < sizeof(footer)) {
        unsigned n = length - tail - 2;
        memcpy(footer, p + tail + 1, n);
        footer[n] = 0;
        if (n)
            posix_rule(footer);
    }
    return 1;
bad:
    free(p);
    count = type_count = 0;
    trans = indices = types = names = NULL;
    return 0;
}
static void load_zone(void)
{
    const char *setting = getenv("TZ"), *p;
    char path[TZ_PATH];
    int saved = errno;
    tzname[0] = standard;
    tzname[1] = summer;
    if (!setting) {
        if (!have_default) {
            FILE *f = fopen(TZDEFAULT, "r");
            strcpy(default_name, "UTC0");
            if (f) {
                if (fgets(default_name, sizeof(default_name), f))
                    default_name[strcspn(default_name, "\r\n\t ")] = 0;
                fclose(f);
            }
            have_default = 1;
        }
        setting = default_name;
    }
    if (strlen(setting) >= sizeof(previous))
        setting = "UTC0";
    if (have_previous && !strcmp(previous, setting)) {
        errno = saved;
        return;
    }
    strcpy(previous, setting);
    have_previous = 1;
    free(data);
    data = NULL;
    count = type_count = 0;
    rules = 0;
    strcpy(standard, "UTC");
    strcpy(summer, "UTC");
    timezone = daylight = std_offset = dst_offset = 0;
    if (*setting) {
        p = setting;
        if (*p == ':')
            p++;
        if (*p == '/') {
            tzfile(p);
        } else if (strlen(p) + sizeof(TZDIR) <= sizeof(path)) {
            strcpy(path, TZDIR);
            strcat(path, p);
            if (!tzfile(path) && *setting != ':')
                posix_rule(setting);
        }
    }
    errno = saved;
}
static struct date rule_date(const struct rule *r, int year, long off)
{
    struct date d;
    int w, delta;
    d.day = year_day(year);
    d.sec = 0;
    if (r->kind == 'M') {
        d.day = month_day(year, r->month - 1);
        w = (d.day + 4) % 7;
        if (w < 0)
            w += 7;
        delta = (r->day - w + 7) % 7 + 7 * (r->week - 1);
        if (delta >= month_days(r->month - 1, year))
            delta -= 7;
        d.day += delta;
    } else
        d.day += r->kind == 'J' ? r->day - 1 + (leap(year) && r->day >= 60) : r->day;
    return add_seconds(d, r->sec - off);
}
static struct zone zone_at(struct date d)
{
    struct zone z;
    if (data && (!rules || (count && compare(d, transition(count - 1)) <= 0))) {
        unsigned a = 0, b = count, t = 0;
        while (a < b) {
            unsigned m = a + (b - a) / 2;
            if (compare(transition(m), d) <= 0)
                a = m + 1;
            else
                b = m;
        }
        if (a)
            t = indices[a - 1];
        else
            while (t + 1 < type_count && types[6 * t + 4])
                t++;
        z.offset = signed32(types + 6 * t);
        z.dst = types[6 * t + 4];
        z.name = (const char *)names + types[6 * t + 5];
        return z;
    }
    z.offset = std_offset;
    z.dst = 0;
    z.name = standard;
    if (daylight && rules) {
        struct tm tm;
        struct date begin, end;
        breakdown(add_seconds(d, std_offset), 0, &tm);
        begin = rule_date(&start_rule, tm.tm_year + 1900, std_offset);
        end = rule_date(&end_rule, tm.tm_year + 1900, dst_offset);
        if (compare(begin, end) < 0 ? (compare(d, begin) >= 0 && compare(d, end) < 0)
                                    : (compare(d, begin) >= 0 || compare(d, end) < 0)) {
            z.offset = dst_offset;
            z.dst = 1;
            z.name = summer;
        }
    }
    return z;
}
static int make_time(struct tm *tm, time_t *result)
{
    struct date wall, utc, back;
    struct zone z;
    long offsets[258];
    unsigned n = 0, i, j;
    int wanted = tm->tm_isdst;
    if (!normalize(tm, &wall))
        return 0;
    offsets[n++] = std_offset;
    offsets[n++] = dst_offset;
    for (i = 0; i < type_count; i++) {
        long off = signed32(types + 6 * i);
        for (j = 0; j < n && offsets[j] != off; j++)
            ;
        if (j == n)
            offsets[n++] = off;
    }
    for (i = 0; i < n; i++) {
        utc = add_seconds(wall, -offsets[i]);
        if (!timestamp(utc, result))
            continue;
        z = zone_at(utc);
        back = add_seconds(utc, z.offset);
        if (!compare(back, wall) && (wanted < 0 || z.dst == (wanted > 0))) {
            breakdown(back, z.dst, tm);
            return 1;
        }
    }
    /* A nonexistent wall time, or an explicitly forced DST choice, is
     * normalized by interpreting it with the requested standard/DST offset. */
    utc = add_seconds(wall, -(wanted > 0 ? dst_offset : std_offset));
    if (!timestamp(utc, result))
        return 0;
    z = zone_at(utc);
    breakdown(add_seconds(utc, z.offset), z.dst, tm);
    return 1;
}
void tzset(void)
{
    Wait(&tzlock);
    have_previous = have_default = 0;
    load_zone();
    Signal(&tzlock);
}
struct tm *gmtime_r(const time_t *timer, struct tm *tm)
{
    struct date d;
    if (!timer || !tm) {
        errno = EINVAL;
        return NULL;
    }
    if (!utc_date(timer, &d))
        return NULL;
    breakdown(d, 0, tm);
    return tm;
}
struct tm *gmtime(const time_t *timer) { return gmtime_r(timer, &utc_tm); }
struct tm *localtime_r(const time_t *timer, struct tm *tm)
{
    struct date d;
    struct zone z;
    if (!timer || !tm) {
        errno = EINVAL;
        return NULL;
    }
    if (!utc_date(timer, &d))
        return NULL;
    Wait(&tzlock);
    load_zone();
    z = zone_at(d);
    breakdown(add_seconds(d, z.offset), z.dst, tm);
    Signal(&tzlock);
    return tm;
}
struct tm *localtime(const time_t *timer) { return localtime_r(timer, &local_tm); }
time_t mktime(struct tm *tm)
{
    time_t result;
    int ok;
    if (!tm) {
        errno = EINVAL;
        return (time_t)-1;
    }
    Wait(&tzlock);
    load_zone();
    ok = make_time(tm, &result);
    Signal(&tzlock);
    if (!ok) {
        errno = ERANGE;
        return (time_t)-1;
    }
    return result;
}
void _tzinfo(const struct tm *tm, long *off, char *name, unsigned capacity)
{
    struct tm copy = *tm;
    struct date d;
    struct zone z;
    time_t t;
    Wait(&tzlock);
    load_zone();
    if (tm == &utc_tm) {
        z.offset = 0;
        z.name = "UTC";
    } else if (make_time(&copy, &t) && utc_date(&t, &d))
        z = zone_at(d);
    else {
        z.offset = tm->tm_isdst > 0 ? dst_offset : std_offset;
        z.name = tzname[tm->tm_isdst > 0];
    }
    *off = z.offset;
    if (capacity) {
        strncpy(name, z.name, capacity - 1);
        name[capacity - 1] = 0;
    }
    Signal(&tzlock);
}
