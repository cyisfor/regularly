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

bool interval_tostr_r(struct tm* interval, char* dest, size_t limit);
const char* interval_tostr(struct tm* interval);

void advance_interval(struct tm* dest, struct tm* interval);

const char* myctime(time_t t);
// mktime sucks
time_t mymktime(struct tm);

// secs if the interval is calculated starting at the epoch
time_t interval_secs(struct tm interval);
time_t interval_secs_from(struct tm base, struct tm interval);

void calendar_init(void);
void interval_between(struct timespec* dest, struct timespec a, struct timespec b);

void timespecadd(struct timespec* dest, struct timespec* a, struct timespec* b);
void timespecmul(struct timespec* src, float factor);
// a - b => dest
void timespecsub(struct timespec* dest, struct timespec* a, struct timespec* b);


