#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <pth.h>

// Some trivial wrappers around GNU pth using boost::bind and boost::function.
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

#define ASSERT_COND(a, b, cond)\
    {\
    auto at = a;\
    auto bt = b;\
    if (!(at cond bt)) {\
      LOG("Assertion %s %s %s failed. %s = %d, %s = %d", #a, #cond, #b, #a, (int)at, #b, (int)bt);\
      abort();\
    }\
    }

#define ASSERT_EQ(a,b) ASSERT_COND(a,b,==)
#define ASSERT_GT(a,b) ASSERT_COND(a,b,>)
#define ASSERT_LT(a,b) ASSERT_COND(a,b,<)
#define ASSERT_GE(a,b) ASSERT_COND(a,b,>=)
#define ASSERT_LE(a,b) ASSERT_COND(a,b,<=)

typedef boost::function<void(void)> VoidFn;

namespace fiber {
static inline void* _boost_helper(void* bound_fn) {
  VoidFn* boost_fn = (VoidFn*) bound_fn;
  (*boost_fn)();
  delete boost_fn;
  return NULL;
}

// Run this function in a while(1) loop.
static inline void _forever(VoidFn f) {
  while (1) {
    f();
    pth_yield(NULL);
  }
}

static inline pth_t run(VoidFn f) {
  void* heap_fn = new VoidFn(f);
  pth_attr_t t_attr = pth_attr_new();
  pth_attr_init(t_attr);
  return pth_spawn(t_attr, _boost_helper, heap_fn);
}

static inline void run_forever(VoidFn f) {
  run(boost::bind(&_forever, f));
}

static inline void wait(std::vector<pth_t>& fibers) {
  for (size_t i = 0; i < fibers.size(); ++i) {
    ASSERT(pth_join(fibers[i], NULL), "Failed to join fibers.");
  }

  fibers.clear();
}

static inline void yield() {
  pth_yield(NULL);
}
}
