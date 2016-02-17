#include "calendar.h"
#include "errors.h"

void advance_interval(struct tm* dest, struct interval* iv) {
#define ONE(UNIT,what)							\
  case UNIT:									\
	dest->tm_ ## what += iv->interval;			\
	return

  ONE(SECONDS,sec);
  ONE(MINUTES,min);
  ONE(HOURS,hour);
  ONE(DAYS,day);
  case WEEKS:
	dest->tm_day += 7 * iv->amount;
	return;
  ONE(MONTHS,mon);
  ONE(YEARS,year);
  default:
	error("whoops the programmer forgot to account for a unit %d",iv->unit);
  };
}
