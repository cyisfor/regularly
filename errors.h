void error(const char* s, ...);
void warn(const char* s, ...);

#ifdef SILENT_INFO
#define info(...)
#else
void info(const char* s, ...);
#endif

#define assert_equal(a,b) if((a) != (b)) { error(#a " != " #b " %d %d\n",(a),(b)); exit(1); }
#define assert_zero(a) assert_equal(a,0);

#define assert(a) if(!(a)) { error("assert failed: " #a); }
