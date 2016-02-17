#include "calendar.h"
#include "errors.h"

void advance_interval(struct tm* dest, struct tm* interval) {
#define ONE(what)								\
  dest->tm_ ## what += iv->tm_ ## what;				\
  ONE(sec);
  ONE(min);
  ONE(hour);
  ONE(mday);
  ONE(mon);
  ONE(year);
}
