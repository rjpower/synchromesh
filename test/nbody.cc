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

static double d_squared(Point a, Point b) {
  return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
}

static inline void operator+=(Point& a, const Point& b) {
  a.x += b.x;
  a.y += b.y;
  a.z += b.z;
}

static inline Point operator+(const Point& a, const Point& b) {
  Point p = { a.x + b.x, a.y + b.y, a.z + b.z };
  return p;
}

static inline Point operator-(const Point& a, const Point& b) {
  Point p = { a.x - b.x, a.y - b.y, a.z - b.z };
  return p;
}

static inline Point operator*(const Point& a, double scale) {
  Point p = { a.x * scale, a.y * scale, a.z * scale };
  return p;
}

static inline Point operator/(const Point& a, double scale) {
  Point p = { a.x / scale, a.y / scale, a.z / scale };
  return p;
}

static inline double uniform() {
  return rand() / double(RAND_MAX) * 2.0 - 1.0;
}

// return unit length vector
static inline Point normalize(const Point& v) {
    double vnorm = sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return v / vnorm;
}


static const double kTimestep = 1e-3;
static const int kNumRounds = 10;
static const int kNumPoints = 1000;
static const int kTag = 1987;

static Point global_pts[kNumPoints];

void runner(RPC* rpc) {
  Point* pts = new Point[kNumPoints];
  Point* accel = new Point[kNumPoints];
  Point* velocity = new Point[kNumPoints];

  Log_Info("Running on worker: %d", rpc->id());
  Endpoint everyone(rpc->first(), rpc->last(), kTag);

  //
  // PHASE 1: GENERATE AND SEND INITIAL LOCATION DATA
  //

  if (rpc->id() == 0) {
    // node 0: gen & send data
    for (int i = 0; i < kNumPoints; i++) {
      pts[i] = { uniform(), uniform(), uniform() };
    }

    Endpoint ep(1, rpc->last(), kTag);
    AllComm all(rpc, ep);
    synchromesh::send(all, pts, kNumPoints);
  } else {
    // node != 0: recv data
    OneComm one(rpc, everyone, 0);
    synchromesh::recv(one, pts, kNumPoints);
  }

  //
  // PHASE 2: N-BODY SIMULATION
  //
  ShardCalc shard_calc(kNumPoints, sizeof(double), everyone.count());
  const size_t start = shard_calc.start_elem(rpc->id());
  const size_t count = shard_calc.num_elems(rpc->id());
  for (int i = start; i < start + count; ++i) {
      velocity[i] = {0, 0, 0};
  }
  for (int round = 0; round < kNumRounds; round++) {
    for (size_t i = start; i < start + count; ++i) {
      accel[i] = {0, 0, 0};
    }

    for (size_t i = start; i < start + count; ++i) {
      for (int j = 0; j < kNumPoints; ++j) {
        if (i == j) {
          continue;
        }
        // simplified model, assume (gravity factor * mass) gives us 1.0
        accel[i] = normalize(pts[i] - pts[j]) / d_squared(pts[i], pts[j]) * 1.0;
      }
    }

    // update
    for (size_t i = start; i < start + count; ++i) {
      pts[i] += velocity[i] * kTimestep + accel[i] * 0.5 * kTimestep * kTimestep;
      velocity[i] += accel[i] * kTimestep;
    }

    // synchronize
//    Log_Info("My id: %d, start: %d, count: %d", rpc->id(), start, count);

    // Send the updates for my region to everyone
    AllComm all(rpc, everyone);
    Request* req = synchromesh::send(all, pts + start, count);

    // Read in the updates for other regions
    ShardedComm sharded(rpc, everyone);
    synchromesh::recv(sharded, pts, kNumPoints);

    req->wait();
  }

  // Copy back to the global array for testing.
  if (rpc->id() == 0) {
    memcpy(global_pts, pts, sizeof(Point) * kNumPoints);
  }

  delete[] pts;
  delete[] accel;
  delete[] velocity;
}

int main(int argc, char** argv) {
  // Run with 1 worker to get a reference.
  Log_Info("Running with 1 worker.");
  srand(getpid());
  DummyRPC::run(1, &runner);
  static Point* reference = new Point[kNumPoints];
  memcpy(reference, global_pts, sizeof(Point) * kNumPoints);
  Log_Info("done.");

  for (int num_workers = 2; num_workers < 16; num_workers *= 2) {
    Log_Info("Running with %d workers.", num_workers);
    srand(getpid());
    DummyRPC::run(num_workers, &runner);
    for (size_t i = 0; i < kNumPoints; ++i) {
      Point diff = global_pts[i] - reference[i];
      ASSERT_LT(fabs(diff.x), 1e-9);
      ASSERT_LT(fabs(diff.y), 1e-9);
      ASSERT_LT(fabs(diff.z), 1e-9);
    }
  }
}

