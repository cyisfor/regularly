#include <stdint.h>
#include <time.h>
enum time_unit { SECONDS, HOURS, MINUTES, DAYS, WEEKS, MONTHS, YEARS };

struct interval {
  enum time_unit unit;
  float amount;
};

void advance_interval(struct tm* dest, struct interval* iv);
