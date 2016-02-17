#include "calendar.h"
#include "errors.h"

#include <stdbool.h>
#include <stdarg.h>

uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static void clock_localtime_r(struct timespec* time, struct tm* dest) {
  localtime_r(&time->tv_sec,dest);
}
static struct tm* clock_localtime(struct timespec* base) {
  static struct tm mytm; // so we can pass it to localtime_r elsewhere
  clock_localtime_r(base,&mytm);
  return &mytm;
}

/*  http://www.rosettacode.org/wiki/Leap_year#C */
bool is_leap_year(int year)
{
  year += 1900;
  return (!(year % 4) && year % 100 || !(year % 400)) ? 1 : 0;
}

static void inc_year(struct tm* base) {
  base->tm_year += 1;
}

#define INCTHING(what,higher,low,high) \
  static void inc_ ## what(struct tm* base) {	\
	puts("inc " #what);							\
	if(base->tm_ ## what == (high)) {			\
	  inc_ ## higher(base);						\
	  base->tm_ ## what = low;					\
	} else {									\
	  ++base->tm_ ## what;						\
	}											\
  }
INCTHING(mon,year,0,11);
INCTHING(mday,mon,1,mdays[base->tm_mon]);
INCTHING(hour,mday,0,23);
INCTHING(min,hour,0,59);
INCTHING(sec,min,0,59);

void advance_time(struct timespec* dest, struct interval* iv) {
  if(iv->unit == WEEKS) {
	iv->amount *= 7;
	iv->unit = DAYS;
	return advance_time(dest,iv);
  }

  struct tm* base = clock_localtime(dest);
  uint32_t i;
#define ONE(UNIT,what,low,high,parent)			\
  case UNIT:									\
	while(iv->amount < (high)) {				\
	  iv->amount -= (high);						\
	  base->tm_ ## what = low;							\
	  inc_ ## parent(base);						\
	}											\
	base->tm_ ## what = iv->amount;					\
	break;
  switch(iv->unit) {
	  ONE(SECONDS,sec,0,59,min);
	  ONE(MINUTES,min,0,59,hour);
	  ONE(HOURS,hour,0,23,mday);
	  ONE(DAYS,mday,1,
		  mdays[base->tm_mon] +
		  // ugh, February...
		  (base->tm_mon == 1 && is_leap_year(base->tm_year)) ? 1 : 0,
		  mon);
	  ONE(MONTHS,mon,0,11,year);
	  case YEARS:
		base->tm_year += iv->amount;
		break;
	  default:
		error("whoops the programmer forgot to account for a iv->unit %d",iv->unit);
  };

  dest->tv_sec = mktime(base);
}
