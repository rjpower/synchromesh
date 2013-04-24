#include "sync.h"

using namespace synchromesh;

struct ABC {
  int a;
  int b;
  int c;
};

void test_register(RPC* rpc) {
  float* a = 0;
  ABC abc;

  Synchromesh s(rpc);
  s.register_array("test_1", a, 1);
  s.register_pod("test_2", &abc);
}

int main() {
  DummyRPC::run(1, &test_register);
}
