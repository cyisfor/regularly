#include "parse.h"
#include <stdio.h>
#include <stdarg.h>
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
	time_t derp = mktime(&ctx.interval);
	printf("| state: %d interval %s",ctime(&derp));
  }
  return 0;
}
