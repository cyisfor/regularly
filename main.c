#include "error.h"
#include "parse.h" // next_token

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

short mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

void parse_interval(struct timespec* dest,
					struct timespec* base,
					const char* s,
					ssize_t len) {
  struct parser ctx = {
	.s = s,
	.len = len,
  };
  struct timespec nextint;
  while(next_token(&ctx)) {
	if(ctx->state == SEEKNUM) {
	  float seconds;
	  switch(ctx->unit) {
	  case SECONDS:
		seconds = ctx->quantity;
		continue;
	  case MINUTES:
		seconds = ctx->quantity * 60;
		continue;
	  case HOURS:
		seconds = ctx->quantity * 60 * 60;
		continue;
	  case DAYS:
		seconds = ctx->quantity * 60 * 60 * 24;
		continue;
	  case MONTHS:
		{
		  int curmonth = localtime(&base->tv_sec).tm_mon;
		  // TODO: if tm_year is a leap year, mdays[1] = 29
		  // TODO: if tm_year has leap seconds, add them on rollover 11-0
		  while(ctx->quantity >= 1.0) {
			seconds += 60 * 60 * 24 * mdays[curmonth];
			curmonth = (curmonth + 1) % 12;
		  }
		  // final bit of the last month
		  seconds += 60 * 60 * 24 * mdays[curmonth] * ctx->quantity;
		}
		continue;
	  case YEARS:
		// TODO: if tm_year is a leap year, 366
		// TODO: if tm_year has leap seconds, add them too
		seconds += 60 * 60 * 24 * 365;
		continue;
	  default:
		error("whoops the programmer forgot to account for a unit %d",ctx->unit);		
	  };	  
	}
  }

  struct timespec interval = {
	.tv_sec = seconds,
	.tv_nsec = 1000000000*(seconds-((int)seconds));
  }
  timespecadd(dest,dest,&interval);
}
		
		
struct rules* parse(struct rules* ret, size_t* space) {
  int fd = open("rules", O_RDONLY);
  if(fd < 0) exit(4);
  struct stat buf;
  fstat(fd,&buf);
  const char* s = mmap(NULL, buf.st_size,PROT_READ,MAP_PRIVATE,fd,0);
  assert(s);
  int which = 0;
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC,&now);
  size_t i = 0;
  while(i<buf.st_size) {
	if(which%(1<<8)==0) {
	  /* faster to allocate in chunks */
	  size_t old = *space;
	  *space += (((which>>8+1)<<8)*sizeof(struct rules));
	  ret = realloc(ret,*space);
	  memset(ret+old,0,*space-old);
	}
	// parse interval
	size_t start = i;
	for(;;) {
	  if(s[i] == '\n')
		break;
	  if(++i == buf.st_size) {
		warn("junk trailer %s\n",s+start);
		goto DONE;
	  }
	}	  
	parse_interval(s+start,i-start,&ret[which].interval);
	timespecadd(&ret[which].due,&ret[which].interval,&now);
	++i;
	start = i;
	for(;i<buf.st_size;++i) {
	  if(s[i] == '\n')
		break;
	}
	ret[which].command = realloc(ret[which].command,i-start);
	memcpy(ret[which].command,s+start,i-start);
	++which;
  }
 DONE:
  // now we don't need the trailing chunk
  *space = which*sizeof(struct rules);
  ret = realloc(ret,*space);
  return ret;
}

void onchild(int signal) {
  return;
}


int main(int argc, char *argv[])
{
  struct passwd* me = getpwuid(getuid());
  assert_zero(chdir(me->pw_dir));
  assert_zero(chdir(".config"));
  mkdir("regularly");
  assert_zero(chdir("regularly"));
  int ino = inotify_init();
  inotify_add_watch(ino,".",IN_MOVED_TO|IN_CLOSE_WRITE);
  struct rules* r = NULL;
  size_t space = 0;
  struct timespec now;
  for(;;) {
  REPARSE:
	r = parse(r,&space);
	if(r) {
	  for(;;) {
		struct rules* cur = find_next(r);
		RECALCULATE:
		clock_gettime(CLOCK_MONOTONIC,&now);
		if(cur->due.tv_sec <= now.tv_sec &&
		   cur->due.tv_nsec <= now.tv_nsec) {
		  int res;
		RUN_IT:
		  res = system(cur->command);
		  if(!WIFEXITED(res) || 0 != WEXITSTATUS(res)) {
			printf("%s exited with %d\n",cur->command,res);
			if(--cur->retries == 0) {
			  puts("disabling\n");
			  cur->disabled = true;
			}
		  }
		  timespecadd(&cur->due,&cur->interval,&now);
		} else {
		  struct timespec left;
		  struct pollfd things[1] = {
			{
			  .fd = ino,
			  .events = POLLIN
			},
		  };
		  int amt;
		  timespecsub(&left, &now, &cur->due);
		  WAIT_AGAIN:
		  amt = ppoll(&things,1,&left,NULL);
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
			struct inotify_event* event = buf;
			for(i=0;i<len/sizeof(struct inotify_event);++i) {
			  if(strcmcp("rules",event[i]->name)) {
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
	  puts("Couldn't find any rules!");
	  left.tv_sec = 10;
	  left.tv_nsec = 0;
	  goto WAIT_AGAIN;
	}
  }
  return 0;
}
