#include "calendar.h"
#include "errors.h"

void advance_interval(struct tm* dest, struct interval* iv) {
#define ONE(UNIT,what)							\
  case UNIT:									\
	dest->tm_ ## what += iv->amount;			\
	return
  switch(iv->unit) {
	ONE(SECONDS,sec);
	ONE(MINUTES,min);
	ONE(HOURS,hour);
	ONE(DAYS,mday);
  case WEEKS:
	dest->tm_mday += 7 * iv->amount;
	return;
	ONE(MONTHS,mon);
	ONE(YEARS,year);
  default:
	error("whoops the programmer forgot to account for a unit %d",iv->unit);
  };
}
