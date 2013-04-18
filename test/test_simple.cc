#include "sync.h"

using namespace synchromesh;

struct ABC {
  int a;
  int b;
  int c;
};

void SimpleUpdate(const UpdateMap& tmp, const UpdateMap& global) {
  for (auto v : tmp) {
    fprintf(stderr, "Processing: %s\n", v.first.c_str());
  }
}

int main() {
  float a[10] = { 0 };
  ABC abc;
  synchromesh::initialize();
  register_array("test_1", a, 10);
  register_pod("test_2", &abc);

  synchromesh::update<SimpleUpdate>(false);
}
