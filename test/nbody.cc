#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "datatype.h"
#include "rpc.h"

using namespace synchromesh;


void runner(RPC* rpc) {
  LOG("Running on worker: %d", rpc->id());
}

int main(int argc, char** argv) {
  srand(getpid());

//  if (MPI::Init_thread(argc, argv, MPI_THREAD_MULTIPLE) == MPI_SUCCESS) {
//    runner(new MPIRPC);
//  } else {
    DummyRPC::run(8, &runner);
//  }
}
