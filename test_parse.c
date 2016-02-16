#include "parse.h"
#include <stdio.h>
#include <stdarg.h>
void error(const char* s, ...) {
  va_list args;
  va_start(args,s);
  vfprintf(stdout,s, args);
  va_end(args);
  putchar('\n');
  exit(23);
}

int main(int argc, char *argv[])
{
  const char s[] = "10 minutes, 2 hours, 3y, 4months 42m";
  struct parse_ctx ctx = {
	.s = s,
	.len = sizeof(s)-1
  };
  while(next_token(&ctx)) {
	fputs("token: ",stdout);
	fwrite(ctx.s+ctx.start,ctx.tokenlen,1,stdout);
	printf("| state: %d quantity: %f unit %d\n",ctx.state,ctx.quantity,ctx.unit);
  }
  return 0;
}
