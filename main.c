#define _GNU_SOURCE
#include "errors.h"
#include "parse.h" // next_token
#include <time.h>
#include <string.h> // memcpy
#include <fcntl.h> // open, O_RDONLY
#include <sys/stat.h>
#include <sys/mman.h>
#include <pwd.h>
#include <unistd.h> // getuid
#include <sys/inotify.h>
#include <poll.h>
#include <errno.h>

void timespecadd(struct timespec* dest, struct timespec* a, struct timespec* b) {
  dest->tv_sec += a->tv_sec + b->tv_sec;
  dest->tv_nsec = a->tv_nsec + b->tv_nsec;
  if(dest->tv_nsec > 1000000000) {
	dest->tv_sec += dest->tv_nsec / 1000000000;
	dest->tv_nsec %= 1000000000;
  }
}

void timespecsub(struct timespec* dest, struct timespec* a, struct timespec* b) {
  bool needborrow = a->tv_nsec < b->tv_nsec;
  dest->tv_sec = a->tv_sec - (needborrow ? 1 : 0) - b->tv_sec;
  dest->tv_nsec = a->tv_nsec + (needborrow ? 1000000000 : 0) - b->tv_nsec; 
}


void parse_interval(struct tm* dest,
					const char* s,
					ssize_t len) {
  struct parser ctx = {
	.s = s,
	.len = len,
  };
  memset(dest,0,sizeof(struct tm));
  while(next_token(&ctx)) {
	if(ctx.state == SEEKNUM) {
	  advance_interval(dest,&ctx.interval);
	}
  }
}

struct rule {
  struct tm interval;
  struct timespec due;
  char* command;
  ssize_t command_length;
  uint8_t retries;
  bool disabled;
};

void update_due(struct rule* r, struct timespec* base) {
  // TODO: specify the base from which intervals are calculated
  struct tm date;
  localtime_r(&base->tv_sec,&date);
  advance_interval(&date,&r->interval);
  r->due.tv_sec = mktime(&date);
}

struct rule* parse(struct rule* ret, size_t* space) {
  int fd = open("rules", O_RDONLY);
  if(fd < 0) return NULL;
  struct stat file_info;
  fstat(fd,&file_info);
  const char* s = mmap(NULL, file_info.st_size,PROT_READ,MAP_PRIVATE,fd,0);
  assert(s);
  int which = 0;
  struct timespec now;
  clock_gettime(CLOCK_REALTIME,&now);
  size_t i = 0;
  while(i<file_info.st_size) {
	if(which%(1<<8)==0) {
	  /* faster to allocate in chunks */
	  size_t old = *space;
	  *space += ((which>>8)+1)<<8;
	  ret = realloc(ret,*space*sizeof(struct rule));
	  memset(ret+old,0,(*space-old)*sizeof(struct rule));
	}
	// parse interval
	size_t start = i;
	for(;;) {
	  if(s[i] == '\n')
		break;
	  if(++i == file_info.st_size) {
		warn("junk trailer %s\n",s+start);
		goto DONE;
	  }
	}
	parse_interval(&ret[which].interval,
				   s+start,i-start);
	update_due(&ret[which],&now);
	++i;
	start = i;
	for(;i<file_info.st_size;++i) {
	  if(s[i] == '\n')
		break;
	}
	ret[which].command = realloc(ret[which].command,i-start+1);
	memcpy(ret[which].command,s+start,i-start);
	if(i-start>1)
	  assert(ret[which].command[i-start-1] != '\n');
	ret[which].command[i-start] = '\0';
	++which;
  }
 DONE:
  munmap((void*)s,file_info.st_size);
  // now we don't need the trailing chunk
  *space = which;
  ret = realloc(ret,*space*sizeof(struct rule));
  return ret;
}

void onchild(int signal) {
  return;
}

struct rule* find_next(struct rule* first, ssize_t num) {
  ssize_t i;
  struct rule* soonest = first;
  for(i=1;i<num;++i) {
	if(first[i].disabled == true) continue;
	if(first[i].due.tv_sec > soonest->due.tv_sec)
	  continue;
	if(first[i].due.tv_sec == soonest->due.tv_sec &&
	   first[i].due.tv_nsec > soonest->due.tv_nsec)
	   continue;
	soonest = first + i;
  }
  return soonest;
}	

int main(int argc, char *argv[])
{
  struct passwd* me = getpwuid(getuid());
  assert_zero(chdir(me->pw_dir));
  assert_zero(chdir(".config"));
  mkdir("regularly",0700);
  assert_zero(chdir("regularly"));
  int ino = inotify_init();
  inotify_add_watch(ino,".",IN_MOVED_TO|IN_CLOSE_WRITE);
  struct rule* r = NULL;
  size_t space = 0;
  struct timespec now,left;
  
  for(;;) {
  REPARSE:
	r = parse(r,&space);
	if(r) {
	  for(;;) {
		struct rule* cur = find_next(r,space);
		if(cur->disabled) {
		  warn("All rules disabled");
		  left.tv_sec = 10;
		  left.tv_nsec = 0;
		  goto WAIT_AGAIN;
		}
		RECALCULATE:
		clock_gettime(CLOCK_REALTIME,&now);
		if(cur->due.tv_sec <= now.tv_sec &&
		   cur->due.tv_nsec <= now.tv_nsec) {
		  int res;
		RUN_IT:
		  res = system(cur->command);
		  if(!WIFEXITED(res) || 0 != WEXITSTATUS(res)) {
			warn("%s exited with %d\n",cur->command,res);
			if(cur->retries == 0) {
			  warn("disabling");
			  cur->disabled = true;
			} else {
			  --cur->retries;
			  goto RUN_IT;
			}
		  } else {
		  	update_due(cur,&now);
		  }
		} else {
		  struct pollfd things[1] = {
			{
			  .fd = ino,
			  .events = POLLIN
			},
		  };
		  int amt;
		  timespecsub(&left, &cur->due, &now);
		  WAIT_AGAIN:
		  amt = ppoll(things,1,&left,NULL);
		  if(amt == 0) {
			goto RUN_IT;
		  } else if(amt < 0) {
			assert(errno == EINTR);
			goto RECALCULATE;
		  }
		  if(things[0].revents & POLLIN) {
			static char buf[0x1000]
			  __attribute__ ((aligned(__alignof__(struct inotify_event))));
			ssize_t len = read(ino,buf, sizeof(buf));
			assert(len > 0);
			struct inotify_event* event = (struct inotify_event*)buf;
			int i;
			for(i=0;i<len/sizeof(struct inotify_event);++i) {
			  if(strcmp("rules",event[i].name)) {
				// config changed, reparse
				goto REPARSE;
			  }
			}
			// nope, some other file changed
			if(cur)
			  goto RECALCULATE;
			// we're actually waiting for rules to exist, oops...
			goto WAIT_AGAIN;
		  }
		}
	  }
	} else {
	  warn("Couldn't find any rules!");
	  left.tv_sec = 10;
	  left.tv_nsec = 0;
	  goto WAIT_AGAIN;
	}
  }
  return 0;
}
