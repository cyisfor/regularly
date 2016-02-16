#include "parse.h"
#include "errors.h"
#include <ctype.h> // isspace
#include <string.h> // strcmp
#define AT_END (i == ctx->len)

bool unimportant(char c) {
  return c == ',' || isspace(c);
}

bool next_token(struct parse_ctx* ctx) {
  ssize_t i;

  for(i=ctx->start+ctx->tokenlen;i<ctx->len;++i) {
	char c = ctx->s[i];
#define ADVANCE(full) advance(full+1,sizeof(full)-2)
	bool advance(const char* full, ssize_t len) {
	  if(0!=strncasecmp(ctx->s+i,full,len))
		return false;
	  i += len;
	  if(i<ctx->len && ctx->s[i] == 's')
		++i;
	  return true;
	}
	switch(ctx->state) {
	case SEEKNUM:
	  if(unimportant(c)) continue;
	  if(isdigit(c) || c == '.') {
		ctx->gotdot = (c == '.');
		ctx->state = FINISHNUM;
		ctx->start = i;
	  }
	  continue;
	case FINISHNUM:
	  if(isdigit(c)) continue;
	  if(c == '.' && !ctx->gotdot) {
		ctx->gotdot = true;
		continue;
	  }
	  ctx->quantity = strtof(ctx->s+ctx->start,NULL);
	  ctx->state = SEEKUNIT;
	  ctx->unit = SECONDS;
	  ctx->tokenlen = i - ctx->start;
	  return true;
	case SEEKUNIT:
	  if(unimportant(c)) continue;
#define DONE ctx->state = SEEKNUM; \
	  ctx->tokenlen = i-ctx->start;		  \
		return true;

	  ctx->start = i;
	  switch(c) {
#define ONE(lower,upper,full,UNIT)							   \
		case lower:											   \
	  case upper:											   \
		if(AT_END) {												  \
		  ctx->unit = UNIT;											  \
		  DONE;															  \
		}															  \
		++i;													  \
		if(unimportant(ctx->s[i]) || ADVANCE(full)) { \
		  ctx->unit = UNIT;									   \
		  DONE;												   \
		} else {											   \
		  error("bad unit %s at %d\n",ctx->s+i,i);			   \
		}
		ONE('h','H',"hour",HOURS);
		case 'm':
		case 'M':
		  ++i;
		  if(AT_END || unimportant(ctx->s[i]) || ADVANCE("minute")) {
			ctx->unit = MINUTES;
			DONE;
		  } else if(ADVANCE("months")) {
			ctx->unit = MONTHS;
			DONE;
		  } else {
			error("bad unit %s at %d\n",ctx->s+i,i);
		  }
		ONE('s','S',"second",SECONDS);
		ONE('y','Y',"year",YEARS);
		default:
		  error("not a unit %s at %d\n",ctx->s+i,i);
	  };
	}
  }
  // at end... do we have a good enough state for one more token?
  if(ctx->state != SEEKUNIT) return false;
  ctx->tokenlen = ctx->len - ctx->start;
  return true;
}

