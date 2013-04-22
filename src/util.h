#ifndef SYNCHROMESH_UTIL_H_
#define SYNCHROMESH_UTIL_H_

#define LOG(...)\
    fprintf(stderr, "%s:%d -- ", __FILE__, __LINE__);\
    fprintf(stderr, ##__VA_ARGS__);\
    fprintf(stderr, "\n");

#define PANIC(...)\
    LOG("Something bad happened:");\
    LOG(__VA_ARGS__);\
    abort();

#define ASSERT(condition, ...)\
    if (!(condition)) {\
        LOG("Assertion: " #condition " failed.");\
        LOG(__VA_ARGS__);\
        abort();\
    }

// I don't like assuming everything is of integer type.  Unfortunately
// "auto" and "decltype" are mutually exclusive, so I can't use this in
// C++-11 and older code.  Fix at some point when I determine the proper
// macros/boost library to magic this away.
#define ASSERT_COND(a, b, cond)\
    {\
    long at = (long)a;\
    long bt = (long)b;\
    if (!(at cond bt)) {\
      LOG("Assertion %s %s %s failed. %s = %ld, %s = %ld", #a, #cond, #b, #a, (long)at, #b, (long)bt);\
      abort();\
    }\
    }

#define ASSERT_EQ(a,b) ASSERT_COND(a,b,==)
#define ASSERT_GT(a,b) ASSERT_COND(a,b,>)
#define ASSERT_LT(a,b) ASSERT_COND(a,b,<)
#define ASSERT_GE(a,b) ASSERT_COND(a,b,>=)
#define ASSERT_LE(a,b) ASSERT_COND(a,b,<=)

#endif /* SYNCHROMESH_UTIL_H_ */
