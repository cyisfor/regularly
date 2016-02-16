#include "errors.h"
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>

void error(const char* s, ...) {
  va_list args;
  fputs("ERROR: ",stderr);
  va_start(args,s);
  vfprintf(stderr,s, args);
  va_end(args);
  fputc('\n',stderr);
  if(errno) {
	perror("Errno:");
  }
  exit(23);
}

void warn(const char* s, ...) {
  va_list args;
  fputs("WARNING: ",stderr);
  va_start(args,s);
  vfprintf(stderr,s, args);
  va_end(args);
  putchar('\n');
}

