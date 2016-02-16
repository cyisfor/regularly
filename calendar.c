#include "calendar.h"
#include <stdbool.h>
#include <stdarg.h>

/*  http://www.rosettacode.org/wiki/Leap_year#C */
bool is_leap_year(int year)
{
  year += 1900;
  return (!(year % 4) && year % 100 || !(year % 400)) ? 1 : 0;
}

uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

void clock_localtime_r(struct timespec* time, struct tm* dest) {
  localtime_r(base.tv_sec,&dest);
}
struct tm* clock_localtime(struct timespec* base) {
  static tm mytm; // so we can pass it to localtime_r elsewhere
  clock_localtime_r(base,&mytm);
  return &mytm;  
}

static void incmonth(struct tm* base) {
	if(base->tm_mon == 11) {
	  base->tm_year += 1;
	  base->tm_mon = 0;
	} else {
	  ++base->tm_mon;
	}
}
void advance_months(struct tm* base, uint32_t months) {
  uint32_t i;
  for(i=0;i<months;++i) {
	incmonth(base);
  }
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

static 
void advance_days(struct tm* base, uint32_t days) {
  uint32_t i;
  for(i=0;i<days;++i) {
	if(base->tm_mday == mdays[base.tm_mon]) {
	  if(base->tm_mon==11) {
		base->tm_year += 1;
		base->tm_mon = 0;
	  } else {
		++base->tm_mon;
	  }
	}
	  base->tm_mon = 0;
	} else {
	  ++base->tm_mon;
	}
	// this adjusts for leap years, leap seconds, etc
	localtime_r(mktime(base),base);
  }
