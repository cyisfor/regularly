#include <stdlib.h>
#include <stdbool.h>

#include "calendar.h"

struct parser {
  struct interval interval;
  enum { SEEKNUM, FINISHNUM, SEEKUNIT, FINISHUNIT } state;
  const char* s;
  ssize_t start;
  ssize_t tokenlen;
  ssize_t len;
  bool gotdot;
};

bool next_token(struct parser* ctx);

