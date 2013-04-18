#include "sync.h"

using namespace synchromesh;

struct ABC {
  int a;
  int b;
  int c;
};

int main() {
  float* a = 0;
  ABC abc;
  register_array("test_1", a, 1);
  register_pod("test_2", &abc);
}
