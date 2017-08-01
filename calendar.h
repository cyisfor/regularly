#include <time.h>
#include <stdbool.h>
#include <stdlib.h> // size_t

bool ctime_interval_r(struct tm* interval, char* dest, size_t limit);
const char* ctime_interval(struct tm* interval);

void advance_interval(struct tm* dest, struct tm* interval);
