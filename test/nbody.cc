#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "sync.h"

using namespace synchromesh;

void NbodyUpdate(const int& n, const int& my_id, UpdateMap& tmp, UpdateMap& global) {
  // TODO
  double* x = tmp["x"]->as_array<double>();
  double* y = tmp["y"]->as_array<double>();
  double* z = tmp["z"]->as_array<double>();

  double* gx = global["x"]->as_array<double>();
  double* gy = global["y"]->as_array<double>();
  double* gz = global["z"]->as_array<double>();

  for (int i = 0; i < n ; i++) {
    printf("update %d: %d: %8lf %8lf %8lf\n", my_id, i, x[i], y[i], z[i]);
  }

}

void runner(RPC* rpc) {
  LOG("Running on worker: %d", rpc->id());
  Synchromesh m(rpc);

  const int n = 10;  // n body
  double x[n], y[n], z[n];
  for (int i = 0; i < n; i++) {
    x[i] = rand() * 2.0 / RAND_MAX - 1.0;
    y[i] = rand() * 2.0 / RAND_MAX - 1.0;
    z[i] = rand() * 2.0 / RAND_MAX - 1.0;
  }

  m.register_array("x", x, n, false);
  m.register_array("y", y, n, false);
  m.register_array("z", z, n, false);

  m.init<NoOp>();

  for (int i = 0; i < n ; i++) {
    printf("worker %d: %d: %8lf %8lf %8lf\n", rpc->id(), i, x[i], y[i], z[i]);
  }

  for (int round = 0; round < 10; round++) {
    m.update<int, int, NbodyUpdate>(n, rpc->id());
  }
}

int main(int argc, char** argv) {
  srand(getpid());

  if (MPI::Init_thread(argc, argv, MPI_THREAD_MULTIPLE) == MPI_SUCCESS) {
    runner(new MPIRPC);
  } else {
    DummyRPC::run(8, &runner);
  }
}
