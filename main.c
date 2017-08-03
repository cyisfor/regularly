#define _GNU_SOURCE
#include "errors.h"
#include "parse.h" // next_token
#include <time.h>
#include <string.h> // memcpy, memmove
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
#include <libgen.h> // dirname
#include <poll.h>
#include <errno.h>
#include <stdio.h>

// sigh
#define WRITE(s,n) fwrite(s,n,1,stderr)
#define WRITELIT(l) WRITE(l,sizeof(l)-1)
#define NL() fputc('\n',stderr);

void parse_interval(struct tm* dest,
										const char* s,
										ssize_t len) {
  struct parser ctx = {
		.s = s,
		.len = len,
  };

	// intervals are NOT valid times, gmtime_r(0,&this) makes a different result
	memset(dest,0,sizeof(struct tm));
  while(next_token(&ctx)) {
		if(ctx.state == SEEKNUM) {
			memcpy(dest,&ctx.interval,sizeof(*dest));
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

/* sorting strategy:
	 first, sort according to soonest due (0 elements)
	 when we insert, search for insertion point, then (maybe) realloc, shift up, and insert.
	 
	 Then, soonest will always be element #1 since until due is updated, all
	 countdown by same time.
*/

/* if a,b,c are before but not d, e, f, insert before d
	 if re-inserting d, a,b,c are before, but not d, but d is the current one,
	 so check e,f,etc. not e, so insert before e (i.e. do nothing)
*/
static size_t find_point(struct rule* r, size_t num, struct timespec due) {
	printf("find point %d\n",num);
	if(num == 0) return 0;
	if(num < 4) {
		int i;
		// binary search doesn't work, or isn't cheaper than linear search
		for(i=0;i<num;++i) {
			if(timespecbefore(&due,&r[i].due)) return i;
		}
		// add it onto the end?
		return num;
	}

	size_t lo, hi;
	lo = 0;
	hi = num-1;

	for(;;) {
		size_t i = (lo+hi)/2;
		if(timespecbefore(&r[i].due,&due)) {
			info("point ↓ %d→%d",lo,i);
			lo = i;
			if(lo + 1 == hi) break;
		} else if(timespecequal(&r[i].due,&due)) {
			lo = i;
			break;
		} else {
			info("point ↑ %d→%d",hi,i);
			hi = i;
			if(lo + 1 == hi) break;
		}
	}
	return lo;
}
static size_t sort_insert(struct rule* r, size_t num, struct timespec due) {
	size_t i = find_point(r,num,due);
	// found insertion point. now shift old ones up

	if(i < num) {
		// assume we've pre-allocated enough space to shift up
		memmove(r+i+1,r+i,sizeof(*r) * (num-i));
	}

	// now r[i] is duplicate of r[i+1] so scrap r[i]
	r[i].due = due;
	return i; // still needs initialization!
}

static void show_rules(struct rule* r, size_t num) {
	int i;
	puts("Rules:");
	for(i=0;i<num;++i) {
		printf("  %d: %s (%s) %s\n",
					 i,
					 r[i].name,
					 interval_tostr(&r[i].interval),
					 myctime(r[i].due.tv_sec));
	}
	puts("-----");
}

static size_t sort_adjust(struct rule* r, size_t num, size_t which) {
	// "due" changed on r+which so find its new spot, and shift accordingly
	int i = find_point(r,num,r[which].due);
	if(i == which) return i;
	struct rule T = r[which];
	if(i < which) {
		/* 0 1 2 i 4 5 6 which 7 8
			 save which into T
			 shift i 4 5 6 up into which
			 restore T into i
		*/
		memmove(r+i+1,r+i,sizeof(*r) * (which-i));
	} else {
		/* 0 1 2 which 4 5 6 i 7 8
			 save which into T
			 shift 4 5 6 i down into which
			 restore T into i
		*/
		memmove(r+which,r+which+1,sizeof(*r) * (i-which));
	}
	r[i] = T;
	show_rules(r,num);
	return i;
}

struct rule default_rule = {
	.interval = { .tm_hour = 1 },
	.failing = { .tm_hour = 2 },
	.retries = 0
};

static void later_time(struct timespec* dest,
											 const struct tm* interval,
											 const struct timespec* base) {
	struct tm date;
  gmtime_r(&base->tv_sec,&date);
  advance_interval(&date,interval);
	dest->tv_sec = mymktime(date) + 1;
	dest->tv_nsec = 0;
	// have it happen at the BEGINNING of the second, or the END of the second?
	// or at base->tv_nsec nanoseconds after the second?
	// end is prettier
}

void update_due_adjust(struct rule* r, size_t num, ssize_t which,
												 const struct timespec* base) {
	/* TODO: specify the base from which intervals are calculated */
	later_time(&r[which].due, &r[which].interval, base);
	sort_adjust(r,num,which);
}

const char* rules_override = NULL;

struct rule* parse(struct rule* ret, size_t* space) {
  int fd;
	if(rules_override==NULL) {
		fd = open("rules", O_RDONLY);
	} else {
		fd = open(rules_override, O_RDONLY);
	}
  if(fd < 0) return NULL;
  struct stat file_info;
  fstat(fd,&file_info);
  const char* s = mmap(NULL, file_info.st_size,PROT_READ,MAP_PRIVATE,fd,0);
  assert(s);
  int num = 0;
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
				//info("doesn't end in a newline");
				break;
			}
		}
		size_t sname = start;
		size_t ename = eq;
		size_t sval = eq+1;
		size_t eval = i;
	
		bool is_a_command(void) {
			if(sval >= eval) {
				// newline
				//info("newline");
				return false;
			}
			//info("nanewline %d %d",sval,eval);
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
				info("found name %s",default_rule.name);
				return false;
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
			
			{
				
				time_t a = interval_secs_from(&now, &default_rule.interval);
				time_t b = interval_secs_from(&now, &default_rule.failing);
				// sanity check
				if(b < a) {
					char normal[0x100];
					char failing[0x100];
					interval_tostr_r(&default_rule.failing, failing, 0x100);
					interval_tostr_r(&default_rule.interval, normal, 0x100);
					warn("failing set to lower than normal wait time... %d '%s' < %d '%s' adjusting.",
							 b,
							 failing,
							 a,
							 normal);
					interval_mul(&default_rule.failing, &default_rule.interval, 2);
				}
			}

			default_rule.command = malloc(eval-sval+1);
			memcpy(default_rule.command,s+sval,eval-sval);
			default_rule.command[eval-sval] = '\0';
			// we're not gonna mess with shell parsing... just pass to the shell.
			
			if(num%(1<<8)==0) {
				/* faster to allocate in chunks */
				size_t old = *space;
				*space += ((num>>8)+1)<<8;
				ret = realloc(ret,*space*sizeof(struct rule));
			}
			
			size_t which;
			if(getenv("nowait")) {
				which = num; // all have same sorting key
				// just make everything due on startup
				memcpy(&default_rule.due,&now,sizeof(now));
			} else {
				later_time(&default_rule.due,&default_rule.interval,&now);
				which = sort_insert(ret,num,default_rule.due);
				// eh, copies due twice
			}
			memcpy(ret+which,&default_rule,sizeof(struct rule));


			// any n=v pairs now committed to the current rule.
			// further rules will use the same values unless specified

			//info("%d name should have copied %s == %s",which, default_rule.name, ret[which].name);
			// be sure to transfer ownership of the name pointer. (move semantics)
			default_rule.name = NULL;
			default_rule.command = NULL;
			++num;
			show_rules(ret,num);
		}
		++i;
  }
DONE:
  munmap((void*)s,file_info.st_size);
  // now we don't need the trailing chunk
  *space = num;
  ret = realloc(ret,num*sizeof(struct rule));
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

int main(int argc, char *argv[])
{

	calendar_init();
	
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

  ino = inotify_init();

	rules_override = getenv("rules");
  
	if(NULL==rules_override) {
		me = getpwuid(getuid());
		assert_zero(chdir(me->pw_dir));
		assert_zero(chdir(".config"));
		mkdir("regularly",0700);
		assert_zero(chdir("regularly"));
		logfd = open("log",O_APPEND|O_WRONLY|O_CREAT,0644);
		inotify_add_watch(ino,".",IN_MOVED_TO|IN_CLOSE_WRITE);
	} else {
		logfd = STDERR_FILENO;
		char* csux = strdup(rules_override);
		inotify_add_watch(ino,dirname(csux),IN_MOVED_TO|IN_CLOSE_WRITE);
		free(csux);
	}

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
	if(r) {
		if(left.tv_sec == 0 && left.tv_nsec == 0) goto MAYBE_RUN_RULE;
		amt = ppoll(things,1,&left,NULL);
	} else {
		amt = ppoll(things,1,NULL,NULL);
	}
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
  { if(space == 0) {
			warn("All rules disabled");
			left.tv_sec = 10;
			left.tv_nsec = 0;
			goto WAIT_FOR_CONFIG;
		}
		
		clock_gettime(CLOCK_REALTIME,&now);
		//warn("cur %d now %d",r[cur].due.tv_sec,now.tv_sec);
		if(r[0].due.tv_sec <= now.tv_sec ||
       r[0].due.tv_sec == now.tv_sec &&
			 r[0].due.tv_nsec <= now.tv_nsec) {
			int res;
			if(r[0].disabled)
				goto RUN_RULE;
			warn("running command: %s",r[0].name);
		RETRY_RULE:    
			res = mysystem(r[0].command);
			fflush(stdout);
			if(WIFSIGNALED(res)) {
				warn("died with %hhd (%s)",WTERMSIG(res),strsignal(WTERMSIG(res)));
			} else if(WIFEXITED(res)) {
				if (0 == WEXITSTATUS(res)) {
					// okay, it exited fine, update due and run the next rule
					clock_gettime(CLOCK_REALTIME,&now);
					update_due_adjust(r,space,0,&now);
					goto RUN_RULE;
				} else {
					warn("exited with %hhd",r[0].command,WEXITSTATUS(res));
				}
			} else {
				error("command neither exited or died? WTF??? %d",res);
			}
			clock_gettime(CLOCK_REALTIME,&now);
			if(r[0].retried == 0) {
				interval_between(&r[0].interval,&r[0].interval,&r[0].failing);
				warn("slowing down to %d %s",
						 interval_secs_from(&now,&r[0].interval),
						 interval_tostr(&r[0].interval));
				r[0].retried = r[0].retries;
				update_due_adjust(r,space,0,&now);
				goto RUN_RULE;
			} else {
				--r[0].retried;
				update_due_adjust(r,space,0,&now);
				goto RUN_RULE;
			}
		} else {
			int amt;
			timespecsub(&left, &r[0].due, &now);
			if(left.tv_sec <= 0) {
				// no time travel, please
				// less than a second is ok because several may come due at once.
				left.tv_sec = 1;
				left.tv_nsec = 0;
			} 
			warn("delay is %s? waiting %d",interval_tostr(&r[0].interval),
					 left.tv_sec);
			goto WAIT_FOR_CONFIG; 
		}  
		error("should never get here!");
  }
  return 0;
}
