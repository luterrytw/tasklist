#ifndef __SYS_TIME_H__
#define __SYS_TIME_H__

#ifdef __cplusplus
extern "C" {
#endif

#define CLOCK_REALTIME	0

struct timezone {
   int tz_minuteswest;     /* minutes west of Greenwich */
   int tz_dsttime;         /* type of DST correction */
};
#if _MSC_VER < 1900
struct timespec {
	time_t   tv_sec;        /* seconds */
	long     tv_nsec;       /* nanoseconds */
};
#else
#include <time.h>
#endif
int gettimeofday(struct timeval *tv, struct timezone *tz);
int clock_gettime(int X, struct timespec *ts);
struct tm* localtime_r(const time_t *time, struct tm *tm);

#ifdef __cplusplus
}
#endif

#endif //__SYS_TIME_H__