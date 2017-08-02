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

bool interval_tostr_r(const struct tm* interval, char* buf, size_t len) {
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

const char* interval_tostr(const struct tm* interval) {
	while(false == interval_tostr_r(interval, buf, len)) {
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


void advance_interval(struct tm* dest, const struct tm* interval) {
	info("advancing %s by %lu %s",myctime(mktime(dest)),interval_secs(*interval),interval_tostr(interval));
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

time_t interval_secs(const struct tm interval) {
	struct timespec base = {};
	return interval_secs_from(base, interval);
}

time_t interval_secs_from(const struct timespec base, const struct tm interval) {
	struct tm now;
	gmtime_r(&base.tv_sec,&now);
#define ONE(what,name) \
	now.tm_ ## what += interval.tm_ ## what;
	FOR_TM;
#undef ONE
	return mktime(&now);
}

void calendar_init(void) {
	buf = malloc(0x100);
	len = 0x100;
}

void interval_between(struct tm* dest, const struct tm* a, const struct tm* b) {
	#define ONE(what,name) dest->tm_ ## what = (a->tm_ ## what + b->tm_ ## what) / 2
	FOR_TM;
	#undef ONE
}

void interval_mul(struct tm* dest, const struct tm* a, const float factor) {
	#define ONE(what,name) dest->tm_ ## what = a->tm_ ## what * factor;
	FOR_TM;
	#undef
}

void timespecadd(struct timespec* dest, const struct timespec* a, const struct timespec* b) {
  dest->tv_sec += a->tv_sec + b->tv_sec;
  dest->tv_nsec = a->tv_nsec + b->tv_nsec;
  if(dest->tv_nsec > 1000000000) {
		dest->tv_sec += dest->tv_nsec / 1000000000;
		dest->tv_nsec %= 1000000000;
  }
}

// only works for unsigned numbers...
#define MAXOF(a) (unsigned long long int) (-1 | (a))

void timespecmul(struct timespec* src, const float factor) {
  float nsecs = src->tv_nsec * factor;
  float secs = src->tv_sec * factor;
  if(secs > MAXOF(src->tv_sec)) {
		src->tv_sec = MAXOF(src->tv_sec);
		return;
  }
  while(nsecs > MAXOF(src->tv_nsec)) {
		if(src->tv_sec == MAXOF(src->tv_sec)) {
			src->tv_sec = MAXOF(src->tv_sec);
			return;
		}
		++src->tv_sec;
		nsecs -= 1.0e9;
  }
  src->tv_nsec = nsecs;
}

void timespecsub(struct timespec* dest, const struct timespec* a, const struct timespec* b) {
  bool needborrow = a->tv_nsec < b->tv_nsec;
  dest->tv_sec = a->tv_sec - (needborrow ? 1 : 0) - b->tv_sec;
  dest->tv_nsec = a->tv_nsec + (needborrow ? 1000000000 : 0) - b->tv_nsec;
}

