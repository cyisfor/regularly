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
#include <sys/time.h> // setrlimit
#include <sys/resource.h> // setrlimit
#include <sys/wait.h> // waitpid

#include <poll.h>
#include <errno.h>
#include <stdio.h>

void timespecadd(struct timespec* dest, struct timespec* a, struct timespec* b) {
  dest->tv_sec += a->tv_sec + b->tv_sec;
  dest->tv_nsec = a->tv_nsec + b->tv_nsec;
  if(dest->tv_nsec > 1000000000) {
	dest->tv_sec += dest->tv_nsec / 1000000000;
	dest->tv_nsec %= 1000000000;
  }
}

// only works for unsigned numbers...
#define MAXOF(a) (unsigned long long int) (-1 | (a))

void timespecmul(struct timespec* src, float factor) {
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
  uint8_t slowdown;
  bool disabled;
};

void update_due(struct rule* r, struct timespec* base) {
  // TODO: specify the base from which intervals are calculated
  struct tm date;
  localtime_r(&base->tv_sec,&date);
  advance_interval(&date,&r->interval);
  r->due.tv_sec = mktime(&date) / r->slowdown;
  r->due.tv_nsec = 0; // eh
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
	ret[which].retries = 3;
	ret[which].slowdown = 1;
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
	if(getenv("nowait")) {
	  // just make everything due on startup
	  memcpy(&ret[which].due,&now,sizeof(now));
	} else {
	  update_due(&ret[which],&now);
	}
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
	++i;
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

const char* shell = NULL;

int mysystem(const char* command) {
  int pid = fork();
  if(pid == 0) {
    /* TODO: put this in... limits.conf file? idk */
    struct rlimit lim = {
      .rlim_cur = 0x100,
      .rlim_max = 0x100
    };
    //setrlimit(RLIMIT_NPROC,&lim);
    lim.rlim_cur = 200;
    lim.rlim_max = 300;
    //setrlimit(RLIMIT_CPU,&lim);
    // no other limits can really be guessed at...
    execlp(shell,shell,"-c",command,NULL);
  }
  assert(pid > 0);
  int status = 0;
  assert(pid == waitpid(pid,&status,0));
  return status;
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
  if(soonest->disabled) return NULL;
  return soonest;
}

int main(int argc, char *argv[])
{

  struct passwd* me = NULL;
  int ino = -1;
  struct rule* r = NULL;
  size_t space = 0;
  struct timespec now,left;
  struct pollfd things[1] = { {
	.fd = -1,
	.events = POLLIN
  } };
  ssize_t amt;
  
  me = getpwuid(getuid());
  assert_zero(chdir(me->pw_dir));
  assert_zero(chdir(".config"));
  mkdir("regularly",0700);
  assert_zero(chdir("regularly"));

  ino = inotify_init();
  inotify_add_watch(ino,".",IN_MOVED_TO|IN_CLOSE_WRITE);

  things[0].fd = ino;

  /*shell = me->pw_shell;
  if(shell == NULL) {
    shell = getenv("SHELL");
    if(shell == NULL) {
      shell = "sh";
    }
  }*/
  // better to have a standard behavior not based on your login shell.
  shell = "sh";

REPARSE:
  r = parse(r,&space);
  
MAYBE_RUN_RULE:
  if(r) {
	goto RUN_RULE;
  } else {
	warn("Couldn't find any rules!");
	left.tv_sec = 10;
	left.tv_nsec = 0;
  }
WAIT_FOR_CONFIG:
  amt = ppoll(things,1,&left,NULL);
  if(amt == 0) {
	// no things (no config updates) so we're golden.
	goto MAYBE_RUN_RULE;
  }
  if(amt < 0) {
	assert(errno == EINTR);
	goto WAIT_FOR_CONFIG;
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
  }
  goto WAIT_FOR_CONFIG;
RUN_RULE:
  { struct rule* cur = find_next(r,space);
	if(cur == NULL) {
	  warn("All rules disabled");
	  left.tv_sec = 10;
	  left.tv_nsec = 0;
	  goto WAIT_FOR_CONFIG;
	}
	clock_gettime(CLOCK_REALTIME,&now);
	//warn("cur %d now %d",cur->due.tv_sec,now.tv_sec);
	if(cur->due.tv_sec <= now.tv_sec ||
       cur->due.tv_sec == now.tv_sec &&
	   cur->due.tv_nsec <= now.tv_nsec) {
	  int res;
	  if(cur->disabled)
		goto RUN_RULE;
	  info("running command: %s",cur->command);
	RETRY_RULE:    
	  res = mysystem(cur->command);
	  fflush(stdout);
	  if(!WIFEXITED(res) || 0 != WEXITSTATUS(res)) {
		warn("%s exited with %d\n",cur->command,res);
		clock_gettime(CLOCK_REALTIME,&now);
		if(cur->retries == 0) {
		  warn("slowing down");
		  ++cur->slowdown;
		  cur->retries = 3;
		  update_due(cur,&now);
		  goto RUN_RULE;
		} else {
		  --cur->retries;
		  update_due(cur,&now);
		  goto RUN_RULE;
		}
		
	  } else {
		clock_gettime(CLOCK_REALTIME,&now);
		update_due(cur,&now);
		goto RUN_RULE;
	  }
	} else {
	  int amt;
	  timespecsub(&left, &cur->due, &now);
	  if(left.tv_sec <= 0) {
      // no waiting less than a second, please
		left.tv_sec = 1;
		left.tv_nsec = 0;
	  } 
	  info("delay is %s? waiting %d %d",ctime_interval(&cur->interval),
		   left.tv_sec,left.tv_nsec);
	  goto WAIT_FOR_CONFIG; 
	}  
	error("should never get here!");
  }
  return 0;
}
