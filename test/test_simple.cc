#include "sync.h"

using namespace synchromesh;

struct ABC {
  int a;
  int b;
  int c;
};

void SimpleUpdate(UpdateMap& tmp, UpdateMap& global) {
  LOG("Update function running...");
  float* f = tmp["test_1"]->as_array<float>();
  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(f[i], i);
  }
  const ABC& abc = tmp["test_2"]->as<ABC>();
  ASSERT_EQ(abc.a, 1);
  ASSERT_EQ(abc.b, 3);
  ASSERT_EQ(abc.c, 5);
}

void SimpleUpdate2(const int& a, UpdateMap& tmp, UpdateMap& global) {
  SimpleUpdate(tmp, global);
  ASSERT_EQ(a, 100);
}

void runner(RPC* rpc) {
  LOG("Running on worker: %d", rpc->id());
  Synchromesh m(rpc);
  float a[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  ABC abc = { 1, 3, 5 };
  m.register_array("test_1", a, 10, false /* not sharded */);
  m.register_pod("test_2", &abc);

  m.update<SimpleUpdate>();
  m.update<int, SimpleUpdate2>(100);
}

int main() {
  DummyRPC::run(8, &runner);
}
