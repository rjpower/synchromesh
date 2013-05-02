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

static const int kDefaultTag = 1;

#define RUN_TEST(expr)\
  Log_Info("Running %s", #expr);\
  DummyRPC::run(8, &expr);;\
  Log_Info("Done.");

void test_sharded_map_to_one(RPC* rpc) {
  Endpoint ep(1, rpc->last(), kDefaultTag);
  if (rpc->id() == 0) {
    map<int, int> m;
    for (int i = 0; i < 100; ++i) {
      m[i] = i;
    }

    ShardedComm sc(rpc, ep);
    send(sc, m)->wait();
  } else {
    map<int, int> m;
    OneComm one(rpc, ep, 0);
    ASSERT_EQ(m.size(), 0);
    recv(one, m);
    ASSERT_EQ(m[78], 78);
  }
}

void test_all_to_one(RPC* rpc) {
  Endpoint ep(1, rpc->last(), kDefaultTag);
  if (rpc->id() == 0) {
    map<int, int> m;
    for (int i = 0; i < 100; ++i) {
      m[i] = i;
    }
    AllComm all(rpc, ep);
    send(all, m)->wait();
  } else {
    map<int, int> m;
    OneComm one(rpc, ep, 0);
    ASSERT_EQ(m.size(), 0);
    recv(one, m);
    ASSERT_EQ(m[78], 78);
  }
}

void test_sharded_send(RPC* rpc) {
  Endpoint ep(rpc->first(), rpc->last(), kDefaultTag);
  if (rpc->id() == 0) {
    ShardedVector<int> m;
    m.resize(100);
    for (int i = 0; i < 100; ++i) {
      m[i] = i;
    }
    ShardedComm sc(rpc, ep);
    send(sc, m)->wait();
  } else {
    ShardedVector<int> m;
    OneComm one(rpc, ep, 0);
    recv(one, m);

    ShardCalc sc(100, sizeof(int), ep.count());
    Log_Info("Size of shard: %zd", m.size());
    ASSERT_EQ(sc.num_elems(rpc->id()), m.size());
    for (size_t i = 0; i < m.size(); ++i) {
      ASSERT_EQ(m[i], sc.start_elem(rpc->id()) + i);
    }
  }
}

void test_sharded_recv(RPC* rpc) {
  Endpoint ep(1, rpc->last(), kDefaultTag);
  ShardCalc sc(100, sizeof(int), ep.count());

  if (rpc->id() == 0) {
    ShardedVector<int> m;
    ShardedComm c(rpc, ep);
    recv(c, m);
    ASSERT_EQ(m.size(), 100);
    for (int i = 0; i < 100; ++i) {
      ASSERT_EQ(m[i], i);
    }
  } else {
    ShardedVector<int> m;
    m.resize(sc.num_elems(rpc->id() - 1));
    for (size_t i = 0; i < m.size(); ++i) {
      m[i] = sc.start_elem(rpc->id() - 1) + i;
    }

    Log_Info("%d sending %d items", rpc->id(), m.size());

    OneComm one(rpc, ep, 0);
    send(one, m)->wait();
  }
}

int main(int argc, char** argv) {
  RUN_TEST(test_sharded_map_to_one);
  RUN_TEST(test_all_to_one);
  RUN_TEST(test_sharded_send)
  RUN_TEST(test_sharded_recv);
}
