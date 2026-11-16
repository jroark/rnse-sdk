#ifndef RNSE_DOOM_TIME_H
#define RNSE_DOOM_TIME_H
#include <sys/types.h>
time_t time(time_t *t);
/* The calendar's fields, as the standard has them (for a program that
 * keeps its own clock: nothing here fills them in). */
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
};
#endif
