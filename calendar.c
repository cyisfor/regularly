#include "calendar.h"
#include "errors.h"
#include <stdio.h>
#include <stdbool.h>

#define FOR_TM									\
  ONE(sec);										\
  ONE(min);										\
  ONE(hour);									\
  ONE(mday);									\
  ONE(mon);										\
  ONE(year);

const char* ctime_interval(struct tm* interval) {
  static char buf[0x100];
  ssize_t offset = 0;
  bool first = true;
  #define ONE(what) \
	if(interval->tm_ ## what) {											\
	  if(first) {														\
		first = false;													\
	  } else {															\
		buf[offset] = ',';												\
		buf[offset+1] = ' ';										    \
		offset += 2;													\
	  }																	\
	  offset+=snprintf(buf+offset,										\
					   0x100-offset,									\
					   "%d " #what,										\
					   interval->tm_ ## what)
  FOR_TM;
#undef ONE
  return buf;
}


void advance_interval(struct tm* dest, struct tm* interval) {
#define ONE(what)								\
  dest->tm_ ## what += interval->tm_ ## what;
  FOR_TM;
#undef ONE
}
