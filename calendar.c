#include "calendar.h"
#include <stdbool.h>
#include <stdarg.h>

uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static void clock_localtime_r(struct timespec* time, struct tm* dest) {
  localtime_r(base.tv_sec,&dest);
}
static struct tm* clock_localtime(struct timespec* base) {
  static tm mytm; // so we can pass it to localtime_r elsewhere
  clock_localtime_r(base,&mytm);
  return &mytm;
}

static void inc_year(struct tm* base) {
  base->tm_year += 1;
}

#define INCTHING(what,higher,low,high) \
  static void inc_ ## what(struct tm* base) {	\
	if(base->tm_ ## what == limit) {			\
	  inc_ ## higher(base);						\
	  base->tm_ ## what = low;					\
	} else {									\
	  ++base->tm ## what;						\
	}											\
  }
INCTHING(month,year,0,11);
INCTHING(mday,month,1,mdays[base->tm_mon]);
INCTHING(hour,mday,0,23);
INCTHING(min,hour,0,59);
INCTHING(sec,min,0,59);


static void inc_week(struct tm* base) {
  uint8_t i;
  for(i=0;i<7;++i)
	inc_day(base);
}

void advance_time(struct timespec* dest, enum time_unit unit, uint32_t increment) {
  struct tm* base = clock_localtime(dest);
  uint32_t i;
  for(i=0;i<increment;++i) {
	switch(unit) {
#define ONE(what,how)							\
	  case what:								\
		inc_ ## how(base);						\
		continue
	  ONE(SECONDS,sec);
	  ONE(MINUTES,min);
	  ONE(HOURS,hour);
	  ONE(DAYS,mday);
	  ONE(WEEKS,week);
	  ONE(MONTHS,month);
	  ONE(YEARS,year);
	  default:
	  float seconds;
	  switch(ctx.unit) {
	  case SECONDS:
		seconds = ctx.quantity;
		continue;
	  case MINUTES:
		seconds = ctx.quantity * 60;
		continue;
	  case HOURS:
		seconds = ctx.quantity * 60 * 60;
		continue;
	  case DAYS:
		seconds = ctx.quantity * 60 * 60 * 24;
		continue;
	  case MONTHS:
		{

		}
		continue;
	  case YEARS:
		// TODO: if tm_year is a leap year, 366
		// TODO: if tm_year has leap seconds, add them too
		seconds += 60 * 60 * 24 * 365;
		continue;
	  default:
		error("whoops the programmer forgot to account for a unit %d",unit);
	};
  }
  dest->tv_sec = mktime(base);
}
