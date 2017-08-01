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
#include <ctype.h> // isspace

#include <poll.h>
#include <errno.h>
#include <stdio.h>

// sigh
#define WRITE(s,n) fwrite(s,n,1,stderr)
#define WRITELIT(l) WRITE(l,sizeof(l)-1)
#define NL() fputc('\n',stderr);

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
	struct tm failing;
  uint8_t retries;
	uint8_t retried;
  struct timespec due;
  char* command;
  ssize_t command_length;
  bool disabled;
	char* name;
};

struct rule default_rule = {
	.interval = { .tm_hour = 1 },
	.failing = { .tm_hour = 2 },
	.retries = 0
};

void update_due(struct rule* r, struct timespec* base) {
  // TODO: specify the base from which intervals are calculated
  struct tm date;
  gmtime_r(&base->tv_sec,&date);
  advance_interval(&date,&r->interval);
  r->due.tv_sec = mktime(&date) + 1;
  r->due.tv_nsec = 0;
	// have it happen at the BEGINNING of the second, or the END of the second?
	// or at base->tv_nsec nanoseconds after the second?
	// end is prettier
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
		// parse name=value pairs, committing with a command.
		size_t start = i;
		size_t eq = start;
		bool goteq = false;
		for(;;) {
			if(s[i] == '\n') break;
			if(s[i] == '=') {
				eq = i;
				goteq = true;
			}
			if(++i == file_info.st_size) {
				info("doesn't end in a newline");
				break;
			}
		}
		size_t sname = start;
		size_t ename = eq;
		size_t sval = eq+1;
		size_t eval = i;
	
		bool is_a_command(void) {
			if(!goteq) {
				--sval; // eq is start, so eq+1 is BAD
				return true; // it's a command
			}
			for(;;) {
				// strip trailing spaces from name
				if(ename == start) {
					// empty name means command
					return true;
				}
				if(isspace(s[ename-1])) {
					--ename;
				} else {
					break;
				}
			}
			for(;;) {
				// strip leading spaces from name
				if(sname == ename) {
					// empty name means command
					return true;
				}
				if(isspace(s[sname])) {
					++sname;
				} else {
					break;
				}
			}
		
			for(;;) {
				// strip trailing spaces from value
				if(eval == sval) {
					WRITELIT("warning: empty value for ");
					WRITE(s+sname,ename-sname);
					NL();
					return false; // still not a command
				}
				if(isspace(s[eval-1])) {
					--eval;
				} else {
					break;
				}
			}
		
			for(;;) {
				// strip leading spaces from value
				if(sval == eval) {
					WRITELIT("warning: empty value for ");
					WRITE(s+sname,ename-sname);
					NL();
					return false; // still not a command
				}
				if(isspace(s[sval])) {
					++sval;
				} else {
					break;
				}
			}

			// not a command, but needs handling

#define NAME_IS(N) (ename-sname == sizeof(N)-1 && 0==memcmp(s+sname,N,sizeof(N)-1))
			if(NAME_IS("name")) {
				default_rule.name = realloc(default_rule.name,eval-sval+1);
				memcpy(default_rule.name,s+sval,eval-sval);
				default_rule.name[eval-sval] = '\0';
			} else if(NAME_IS("wait") || NAME_IS("interval")) {
				parse_interval(&default_rule.interval,s+sval,eval-sval);
				return false;
			} else if(NAME_IS("retries")) {
				// we know the end already is eval.
				char* enumber = NULL;
				// base == 0 allows for 0xFF and 0755 syntax
				size_t retries = strtol(s+sval,&enumber,0);
				if(enumber == s + eval) {
					default_rule.retries = retries;
				} else {
					WRITELIT("warning: ignoring retries because not a number: ");
					WRITE(s+sval,eval-sval);
					NL();
				}
				return false;
			} else if(NAME_IS("failing")) {
				parse_interval(&default_rule.failing,s+sval,eval-sval);
				return false;
			}
			// assume the command contains an '=' sign and this line isn't a n=v pair
			return true;
		}

		if(is_a_command()) {
			// use sval and eval because might be command= or just a leading =

			if(which%(1<<8)==0) {
				/* faster to allocate in chunks */
				size_t old = *space;
				*space += ((which>>8)+1)<<8;
				ret = realloc(ret,*space*sizeof(struct rule));
			}

			{
				struct tm tm;
				// mktime sucks
				memcpy(&tm, &default_rule.interval,sizeof(default_rule.interval));
				time_t a = mktime(&tm);
				memcpy(&tm, &default_rule.interval,sizeof(default_rule.interval));
				time_t b = mktime(&tm);
				// sanity check
				if(b < a) {
					char normal[0x100];
					char failing[0x100];
					ctime_interval_r(&default_rule.failing, failing, 0x100);
					ctime_interval_r(&default_rule.interval, normal, 0x100);
					warn("failing set to lower than normal wait time... '%s' < '%s' adjusting.",
							 failing,
							 normal);
					a = a << 1;
					gmtime_r(&a,&default_rule.failing);
				}
			}
			
			// any n=v pairs now committed to the current rule.
			// further rules will use the same values unless specified
			memcpy(ret+which,&default_rule,sizeof(struct rule));
			// be sure to transfer ownership of the name pointer. (move semantics)
			default_rule.name = NULL;
			ret[which].command = realloc(ret[which].command,eval-sval+1);
			memcpy(ret[which].command,s+sval,eval-sval);
			ret[which].command[eval-sval] = '\0';
			// we're not gonna mess with shell parsing... just pass to the shell.
			if(getenv("nowait")) {
				// just make everything due on startup
				memcpy(&ret[which].due,&now,sizeof(now));
			} else {
				update_due(&ret[which],&now);
			}
			++which;
		}
		++i;
  }
DONE:
  munmap((void*)s,file_info.st_size);
  // now we don't need the trailing chunk
  *space = which;
  ret = realloc(ret,which*sizeof(struct rule));
  return ret;
}

void onchild(int signal) {
  return;
}

const char* shell = NULL;

int logfd = -1;

int mysystem(const char* command) {
	// TODO: have a shell process running, and feed it these as lines.
  int pid = fork();
  if(pid == 0) {
    /* TODO: put this in... limits.conf file? idk */
		dup2(logfd,1);
		dup2(logfd,2);
/*     struct rlimit lim = {
			 .rlim_cur = 0x100,
			 .rlim_max = 0x100
			 };
			 setrlimit(RLIMIT_NPROC,&lim);
			 lim.rlim_cur = 200;
			 lim.rlim_max = 300;
			 setrlimit(RLIMIT_CPU,&lim); */
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
	info("Rules found:");
	struct timespec now;
	clock_gettime(CLOCK_REALTIME,&now);
  for(i=1;i<num;++i) {
		struct timespec interval;
		struct tm intervalderp;
		timespecsub(&interval,&first[i].due,&now);
		gmtime_r(&interval->tv_sec,&intervalderp);
		info("%s: %s (%s)",
				 first[i].name,
				 ctime_interval(&intervalderp),
				 ctime(&first[i].due));
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

	logfd = open("log",O_APPEND|O_WRONLY|O_CREAT,0644);

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
	unsetenv("nowait");
  
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
			warn("running command: %s",cur->command);
		RETRY_RULE:    
			res = mysystem(cur->command);
			fflush(stdout);
			if(WIFSIGNALED(res)) {
				warn("died with %hhd (%s)",WTERMSIG(res),strsignal(WTERMSIG(res)));
			} else if(WIFEXITED(res)) {
				if (0 == WEXITSTATUS(res)) {
					// okay, it exited fine, update due and run the next rule
					clock_gettime(CLOCK_REALTIME,&now);
					update_due(cur,&now);
					goto RUN_RULE;
				} else {
					warn("exited with %hhd",cur->command,WEXITSTATUS(res));
				}
			} else {
				error("command neither exited or died? WTF??? %d",res);
			}
			clock_gettime(CLOCK_REALTIME,&now);
			if(cur->retried == 0) {
				time_t a = mktime(&cur->interval);
				time_t b = mktime(&cur->failing);
				a = (a+b)>>1; // average leads toward failing w/ every iteration
				gmtime_r(&a, &cur->interval); 
				warn("slowing down to %s",ctime_interval(&cur->interval));
				cur->retried = cur->retries;
				update_due(cur,&now);
				goto RUN_RULE;
			} else {
				--cur->retried;
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
			warn("delay is %s? waiting %d",ctime_interval(&cur->interval),
					 left.tv_sec);
			goto WAIT_FOR_CONFIG; 
		}  
		error("should never get here!");
  }
  return 0;
}
