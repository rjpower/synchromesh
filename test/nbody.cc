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

static const int N = 1000;
static Point* pts;
static Point* forces;

void runner(RPC* rpc) {
  Log_Info("Running on worker: %d", rpc->id());
  Endpoint ep(rpc->first(), rpc->last(), 1987); // 1987 as tag

  //
  // PHASE 1: GENERATE AND SEND INITIAL LOCATION DATA
  //

  if (rpc->id() == 0) {
    // node 0: gen & send data
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
}

int main(int argc, char** argv) {
  pts = new Point[N];
  forces = new Point[N];

  // Run with 1 worker to get a reference.
  Log_Info("Running with 1 worker.");
  srand(getpid());
  DummyRPC::run(1, &runner);
  static Point* reference = new Point[N];
  memcpy(reference, pts, sizeof(Point) * N);

  Log_Info("done.");

  for (int num_workers = 2; num_workers < 16; num_workers *= 2) {
    srand(getpid());
    DummyRPC::run(num_workers, &runner);
    for (size_t i = 0; i < N; ++i) {
      ASSERT_EQ(pts[i].x, reference[i].x);
      ASSERT_EQ(pts[i].y, reference[i].y);
      ASSERT_EQ(pts[i].z, reference[i].z);
    }
    ASSERT_EQ(memcmp(reference, pts, sizeof(Point) * N), 0);
  }
}

