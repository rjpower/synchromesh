#ifndef SYNCHROMESH_UTIL_H_
#define SYNCHROMESH_UTIL_H_

#include <boost/thread.hpp>

extern boost::recursive_mutex log_mutex;

#define DO_LOG(...)\
    {\
    boost::recursive_mutex::scoped_lock log_lock_(log_mutex);\
    fprintf(stderr, "%s:%d -- ", __FILE__, __LINE__);\
    fprintf(stderr, ##__VA_ARGS__);\
    fprintf(stderr, "\n");\
    }

enum LogLevel {
  kDebug = 0,
  kInfo = 1,
  kWarn = 2,
  kError = 3,
};

extern LogLevel log_level;

#define Log_Debug(...) if (log_level <= kDebug) { DO_LOG(__VA_ARGS__); }
#define Log_Info(...) if (log_level <= kInfo) { DO_LOG(__VA_ARGS__); }
#define Log_Warn(...) if (log_level <= kWarn) { DO_LOG(__VA_ARGS__); }
#define Log_Error(...) if (log_level <= kError) { DO_LOG(__VA_ARGS__); }

#define PANIC(...)\
    DO_LOG("Something bad happened:");\
    DO_LOG(__VA_ARGS__);\
    abort();

#define ASSERT(condition, ...)\
    if (!(condition)) {\
        DO_LOG("Assertion: " #condition " failed.");\
        DO_LOG(__VA_ARGS__);\
        abort();\
    }


// I don't like assuming everything is of integer type.  Unfortunately
// "auto" and "decltype" are mutually exclusive, so I can't use this in
// C++-11 and older code.  Fix at some point when I determine the proper
// macros/boost library to magic this away.
#define ASSERT_COND(a, b, cond)\
    {\
    auto at = a;\
    auto bt = b;\
    if (!(at cond bt)) {\
      DO_LOG("Assertion %s %s %s failed. %s = %ld, %s = %ld", #a, #cond, #b, #a, (long)at, #b, (long)bt);\
      abort();\
    }\
    }

#define ASSERT_EQ(a,b) ASSERT_COND(a,b,==)
#define ASSERT_GT(a,b) ASSERT_COND(a,b,>)
#define ASSERT_LT(a,b) ASSERT_COND(a,b,<)
#define ASSERT_GE(a,b) ASSERT_COND(a,b,>=)
#define ASSERT_LE(a,b) ASSERT_COND(a,b,<=)

#endif /* SYNCHROMESH_UTIL_H_ */
