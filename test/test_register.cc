#include "sync.h"

using namespace update;

struct ABC {
  int a;
  int b;
  int c;
};

int main() {
  float* a = 0;
  ABC abc;
  register_array("test_1", a, 1);
  register_struct("test_2", &abc);
}
