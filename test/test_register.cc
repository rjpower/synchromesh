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
  Update* t1 = s.register_array("test_1", a, 1);
  Update* t2 = s.register_pod("test_2", &abc);
  t1->copy();
  t2->copy();

  s.init<NoOp>();
}

int main() {
  DummyRPC::run(1, &test_register);
}
