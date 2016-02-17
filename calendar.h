#include <stdint.h>
#include <time.h>
enum time_unit { SECONDS, HOURS, MINUTES, DAYS, WEEKS, MONTHS, YEARS };

struct interval {
  enum time_unit unit;
  float amount;
};


void advance_time(struct timespec* base, struct interval* iv);
