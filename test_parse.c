#include "parse.h"
#include "calendar.h"
#include <stdio.h>

int main(int argc, char *argv[])
{
  const char s[] = "10 minutes, 2 hours, 3y, 4months 42m";
  struct parser ctx = {
	.s = s,
	.len = sizeof(s)-1
  };
  while(next_token(&ctx)) {
	fputs("token: ",stdout);
	fwrite(ctx.s+ctx.start,ctx.tokenlen,1,stdout);
	printf("| state: %d interval %s\n",ctx.state, ctime_interval(&ctx.interval));
  }
  return 0;
}
