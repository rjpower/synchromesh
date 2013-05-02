#include "rpc.h"
#include "datatype.h"

using namespace synchromesh;
using std::map;
using std::vector;

struct ABC {
  int a;
  int b;
  int c;
};

void test_1(RPC* rpc) {
  Endpoint ep(1, rpc->last(), 100);
  if (rpc->id() == 0) {
    map<int, int> m;
    for (int i = 0; i < 100; ++i) {
      m[i] = i;
    }

    ShardedComm sc(rpc, ep);
    send(&sc, m);
  } else {
    map<int, int> m;
    OneComm one(rpc, ep, 0);
    ASSERT_EQ(m.size(), 0);
    recv(&one, m);
    ASSERT_EQ(m[78], 78);
  }
}

void test_2(RPC* rpc) {
  Endpoint ep(1, rpc->last(), 100);
  if (rpc->id() == 0) {
    map<int, int> m;
    for (int i = 0; i < 100; ++i) {
      m[i] = i;
    }
    AllComm all(rpc, ep);
    send(&all, m);
  } else {
    map<int, int> m;
    OneComm one(rpc, ep, 0);
    ASSERT_EQ(m.size(), 0);
    recv(&one, m);
    ASSERT_EQ(m[78], 78);
  }
}

void test_3(RPC* rpc) {
  Endpoint ep(1, rpc->last(), 100);
  if (rpc->id() == 0) {
    Sharded<int> m;
    m.resize(1000);
    for (int i = 0; i < 1000; ++i) {
      m[i] = i;
    }
    ShardedComm sc(rpc, ep);
    send(&sc, m);
  } else {
    Sharded<int> m;
    OneComm one(rpc, ep, 0);
    ASSERT_EQ(m.size(), 0);
    recv(&one, m);
    LOG("Size of shard: %d", m.size());
  }

}
void runner(RPC* rpc) {
  LOG("Running on worker: %d", rpc->id());
  test_1(rpc);
  test_2(rpc);
  test_3(rpc);
}

int main(int argc, char** argv) {
  DummyRPC::run(8, &runner);
}
