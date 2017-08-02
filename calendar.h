#include <time.h>
#include <stdbool.h>
#include <stdlib.h> // size_t

/* an interval MUST be a struct tm or analogue
	 because an interval of "1 month" in rules could be 30 or 31 days depending on when
	 the rule was last run. If intervals are uint64_t or struct timespec, you could lose
	 leap-seconds, leap-years, months, etc.

	 struct tm intervals are NOT valid offsets from the epoch. Usually they're invalid times
	 in general. Only by adding the interval's members to a valid struct tm can you get how
	 long it is in seconds, because intervals are longer or shorter depending on what time
	 they start at.
*/

bool interval_tostr_r(const struct tm* interval, char* dest, size_t limit);
const char* interval_tostr(const struct tm* interval);

void advance_interval(struct tm* dest, const struct tm* interval);

const char* myctime(time_t t);
// mktime sucks
time_t mymktime(struct tm);

// secs if the interval is calculated starting at the epoch
time_t interval_secs(const struct tm* interval);
time_t interval_secs_from(const struct timespec* base, const struct tm* interval);

void calendar_init(void);
void interval_between(struct tm* dest, const struct tm* a, const struct tm* b);
void interval_mul(struct tm* dest, const struct tm* a, const float factor);

void timespecadd(struct timespec* dest, const struct timespec* a, const struct timespec* b);
// a - b => dest
void timespecsub(struct timespec* dest, const struct timespec* a, const struct timespec* b);
void timespecmul(struct timespec* src, const float factor);

bool timespecbefore(const struct timespec* before,const struct timespec* after);
								 
#define timespecsecs(t) ((t).tv_sec + (t).tv_nsec / 1000000000.0)
