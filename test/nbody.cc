#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "datatype.h"
#include "rpc.h"

using namespace synchromesh;

struct Point {
  double x;
  double y;
  double z;
};

static double distance(Point a, Point b) {
  return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

static inline void operator+=(Point& a, const Point& b) {
  a.x += b.x;
  a.y += b.y;
  a.z += b.z;
}

static inline Point operator-(const Point& a, const Point& b) {
  Point p = { a.x - b.x, a.y - b.y, a.z - b.z };
  return p;
}

static inline Point operator*(const Point& a, double scale) {
  Point p = { a.x * scale, a.y * scale, a.z * scale };
  return p;
}

void runner(RPC* rpc) {
  Log_Info("Running on worker: %d", rpc->id());
  Endpoint ep(rpc->first(), rpc->last(), 1987); // 1987 as tag

  //
  // PHASE 1: GENERATE AND SEND INITIAL LOCATION DATA
  //

  const int N = 1000;
  Point* pts = new Point[N];
  Point* forces = new Point[N];

  if (rpc->id() == 0) {
    // node 0: gen & send data
    srand(getpid());
    for (int i = 0; i < N; i++) {
      pts[i].x = rand() / double(RAND_MAX) * 2.0 - 1.0;
      pts[i].y = rand() / double(RAND_MAX) * 2.0 - 1.0;
      pts[i].z = rand() / double(RAND_MAX) * 2.0 - 1.0;
    }

    AllComm all(rpc, ep);
    synchromesh::send(all, pts, N);
  } else {
    // node != 0: recv data
    OneComm one(rpc, ep, 0);
    synchromesh::recv(one, pts, N);
  }

  //
  // PHASE 2: N-BODY SIMULATION
  //
  const double epsilon = 1e-9;
  const int N_ROUND = 10;
  ShardCalc shard_calc(N, sizeof(double), ep.count());
  const size_t start = shard_calc.start_elem(rpc->id());
  const size_t count = shard_calc.num_elems(rpc->id());
  for (int round = 0; round < N_ROUND; round++) {
    for (size_t i = start; i <  start + count; ++i) {
      forces[i] = { 0, 0, 0 };
    }

    for (size_t i = start; i <  start + count; ++i) {
      for (int j = 0; j < N; ++j) {
        double dist = distance(pts[i], pts[j]);
        forces[i] += (pts[i] - pts[j]);
      }
    }

    // apply forces
    for (size_t i = start; i <  start + count; ++i) {
      pts[i] += forces[i] * epsilon;
    }

    // synchronize
    AllComm all(rpc, ep);
    ShardedComm sharded(rpc, ep);

    // Send the updates for my region to everyone
    Request* req = synchromesh::send(all, pts + start, count);

    // Read in the updates for other regions
    synchromesh::recv(sharded, pts, N);

    req->wait();
  }

  delete[] pts;
}

int main(int argc, char** argv) {
  srand(getpid());

//  if (MPI::Init_thread(argc, argv, MPI_THREAD_MULTIPLE) == MPI_SUCCESS) {
//    runner(new MPIRPC);
//  } else {
    DummyRPC::run(8, &runner);
//  }
}

