#include "calendar.h"
#include "errors.h"
#include <stdio.h>
#include <stdbool.h>

#define FOR_TM																	\
	ONE(sec,"second");														\
	ONE(min,"minute");														\
	ONE(hour,"hour");															\
	ONE(mday,"day");															\
	ONE(mon,"month");															\
	ONE(year,"year");

bool ctime_interval_r(struct tm* interval, char* buf, size_t len) {
	ssize_t offset = 0;
	bool first = true;
#define ONE(what,name)													\
	if(interval->tm_ ## what) {										\
		if(first) {																	\
			first = false;														\
		} else {																		\
			if(offset+2>=len) return false;						\
			buf[offset] = ',';												\
			buf[offset+1] = ' ';											\
			offset += 2;															\
		}																						\
		offset+=snprintf(buf+offset,								\
										 len-offset,								\
										 "%d " name,								\
										 interval->tm_ ## what);		\
		if(offset >= len) return false;							\
		if(interval->tm_ ## what > 1) {							\
			if(offset == len) return false;						\
			buf[offset] = 's';												\
			++offset;																	\
		}																						\
	}
	FOR_TM;
#undef ONE
	if(offset != len) 
		buf[offset] = '\0';
	return true;
}

const char* ctime_interval(struct tm* interval) {
	static char* buf = NULL;
	static size_t len = 0;
	if(buf == NULL) {
		buf = malloc(0x100);
		len = 0x100;
	}

	while(false == ctime_interval_r(interval, buf, len)) {
		len += 0x100;
		buf = realloc(buf,len);
	}
	return buf;
}

void advance_interval(struct tm* dest, struct tm* interval) {
#define ONE(what,name)													\
	dest->tm_ ## what += interval->tm_ ## what;
	FOR_TM;
#undef ONE
}
