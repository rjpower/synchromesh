#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "datatype.h"
#include "rpc.h"

using namespace synchromesh;

void runner(RPC* rpc) {
  Log_Info("Running on worker: %d", rpc->id());
  Endpoint ep(1, rpc->last(), 1987); // 1987 as tag

  //
  // PHASE 1: GENERATE AND SEND INITIAL LOCATION DATA
  //

  const int N = 1000;
  double* xarr = new double[N];
  double* yarr = new double[N];
  double* zarr = new double[N];
  for (int i = 0; i < N; i++) {
    xarr[i] = yarr[i] = zarr[i] = 0.0;
  }

  if (rpc->id() == 0) {
    // node 0: gen & send data
    srand(getpid());
    for (int i = 0; i < N; i++) {
      xarr[i] = rand() / double(RAND_MAX) * 2.0 - 1.0;
      yarr[i] = rand() / double(RAND_MAX) * 2.0 - 1.0;
      zarr[i] = rand() / double(RAND_MAX) * 2.0 - 1.0;
    }
    AllComm all(rpc, ep);
    all.send_array(xarr, N, sizeof(double));
    all.send_array(yarr, N, sizeof(double));
    all.send_array(zarr, N, sizeof(double));
  } else {
    // node != 0: recv data
    OneComm one(rpc, ep, 0);
    one.recv_array(xarr, N, sizeof(double));
    one.recv_array(yarr, N, sizeof(double));
    one.recv_array(zarr, N, sizeof(double));
  }

  //
  // PHASE 2: WRAP DATA INTO SHARDED TYPE
  //

  const int N_ROUND = 10;
  Sharded<double> x;
  Sharded<double> y;
  Sharded<double> z;
  x.resize(N);
  y.resize(N);
  z.resize(N);
  for (int i = 0; i < N; i++) {
    x[i] = xarr[i];
    y[i] = yarr[i];
    z[i] = zarr[i];
  }

  //
  // PHASE 3: N-BODY SIMULATION
  //

  ShardCalc shard_calc(N, sizeof(double), rpc->num_workers());
  for (int round = 0; round < N_ROUND; round++) {
    // TODO xfer data
  }

  delete[] xarr;
  delete[] yarr;
  delete[] zarr;
}

int main(int argc, char** argv) {
  srand(getpid());

  if (MPI::Init_thread(argc, argv, MPI_THREAD_MULTIPLE) == MPI_SUCCESS) {
    runner(new MPIRPC);
  } else {
    DummyRPC::run(8, &runner);
  }
}


