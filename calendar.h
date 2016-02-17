#include <time.h>

const char* ctime_interval(struct tm* interval);

void advance_interval(struct tm* dest, struct tm* interval);
