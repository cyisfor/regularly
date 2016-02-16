#define assert_equal(a,b) if((a) != (b)) { error(#a " != " #b " %d %d\n",(a),(b)); exit(1); }
#define assert_zero(a) assert_equal(a,0);
