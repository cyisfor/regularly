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
		puts("junk trailer");
		return ret;
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
  // now we don't need the trailing chunk
  *space = which*sizeof(struct rules);
  ret = realloc(ret,*space);
  return ret;
}

struct {
  pthread_mutex_t lock;
  int pid;
  int status;
  bool fail;
  int input;
  int output;
  bool pending;
} sh = {
  .pid = -1,
  .lock = PTHREAD_MUTEX_INITIALIZER,
  .fail = true
};

void onchild(int signal, siginfo_t* info, void* udata) {
  assert(signal==SIGCHLD);
  int died;
  int status;
  int old_errno = errno;
  const struct timespec delay = { 0, 1000000 };
  
  for(;;) {
	errno = 0;
	died = waitpid(WAIT_ANY, &status, WNOHANG | WUNTRACED);
	if(pid > 0) {
	  pthread_mutex_lock(&sh.lock);
	  if (pid == sh.pid) {
		close(sh.input);
		close(sh.output);
		sh.status = status;
		sh.fail = true;
		pthread_mutex_unlock(&sh.lock);
		return;
	  }
	  pthread_mutex_unlock(&sh.lock);
	  continue;
	}
	if(errno != EINTR) {
	  errno = old_errno;		  
	  return;
	}
	clock_nanosleep(CLOCK_MONOTONIC, &delay, NULL);
  }
}

const char* shell = "/bin/sh";
  
void startsh(bool dolock = true) {
  if(dolock)
	pthread_mutex_lock(&sh.lock);
  assert(sh.fail == true);
  int down[2];
  int up[2];
  pipe(down);
  pipe(up);
  sh.output = down[1];
  sh.input = up[0];
  sh.pid = fork();
  if(sh.pid == 0) {
	dup2(down[0],0);
	dup2(up[1],1);
	// cloexec takes care of the extra pipe fds
	execlp(shell,shell,NULL);
	exit(23);
  }
  assert(sh.pid > 0);
  if(dolock)
	pthread_mutex_unlock(&sh.lock);
  close(down[0]);
  close(up[1]);
}

void sendsh(const char* command, size_t len) {
  pthread_mutex_lock(&sh.lock);
  if(sh.fail) {
	startsh(false);
  }
  ssize_t amt = write(sh.pid,command,len);
  assert_equal(amt, len);
  amt = write(sh.pid,"\necho _SENTINEL_THING");
  assert_equal(amt, sizeof("\necho _SENTINEL_THING")-1);
  sh.pending = true;
  pthread_mutex_unlock(&sh.lock);
}  

int main(int argc, char *argv[])
{
  struct passwd* me = getpwuid(getuid());
  chdir(me->pw_dir) && exit(1);
  chdir(".config") && exit(2);
  mkdir("regularly");
  chdir("regularly") && exit(3);
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
		  timespecsub(&left, &now, &cur->due);
		  pthread_mutex_lock(&sh.lock);
		  struct pollfd things[2] = {
			{
			  .fd = ino,
			  .events = POLLIN
			},
			{
			  .fd = sh.pid;
			  .events = POLLIN|POLLOUT
			}
		  };

		  int amt = ppoll(&things,1,&left,NULL);
		  if(amt == 0) {
			goto RUN_IT;
		  } else if(amt < 0) {
			assert(errno == EINTR);
			goto RECALCULATE;
		  }
		  if(thing[0].revents & POLLIN) {
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
			goto RECALCULATE;
		  }
		  goto RUN_IT;
		}
	} else {
	  puts("Couldn't find any rules!");

  return 0;
}
