#include "calendar.h"
#include "errors.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h> // strlen

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

static char* buf;
static size_t len;

const char* ctime_interval(struct tm* interval) {
	while(false == ctime_interval_r(interval, buf, len)) {
		len += 0x100;
		buf = realloc(buf,len);
	}
	return buf;
}

const char* myctime(time_t t) {
	char* ret = ctime(&t);
	ret[strlen(ret)-1] = '\0'; // stupid newline...
	return ret;
}


void advance_interval(struct tm* dest, struct tm* interval) {
	info("advancing %s by %lu %s",myctime(mktime(dest)),intervalsecs(*interval),ctime_interval(interval));
#define ONE(what,name)													\
	dest->tm_ ## what += interval->tm_ ## what;
	FOR_TM;
#undef ONE
	info("->%s",asctime(dest));
}

time_t mymktime(struct tm derp) {
	// mktime sucks
	return mktime(&derp);
}

struct tm epoch;

time_t interval_secs(struct tm interval) {
	// mktime SUCKS
	struct tm derp = epoch;
#define ONE(what,name) \
	derp.tm_ ## what += interval.tm_ ## what;
	FOR_TM;
#undef ONE
	return mktime(derp);
}

void calendar_init(void) {
	time_t sigh = 0;
	gmtime_r(&sigh,&epoch);
	buf = malloc(0x100);
	len = 0x100;
}
