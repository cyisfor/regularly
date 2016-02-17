#include <stdint.h>
enum time_unit { SECONDS, HOURS, MINUTES, DAYS, WEEKS, MONTHS, YEARS };

struct interval {
  enum unit unit;
  uint32_t amount;
};


void advance_time(struct timespec* base, struct interval* iv);
