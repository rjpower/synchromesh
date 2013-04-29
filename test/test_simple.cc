#include "rpc.h"
#include "datatype.h"

using namespace synchromesh;

struct ABC {
  int a;
  int b;
  int c;
};

void runner(RPC* rpc) {
  LOG("Running on worker: %d", rpc->id());
  ABC local;
  ProcessGroup g(rpc->first(), rpc->last());
  Request* req = NULL;
  if (rpc->id() == 0) {
    CommStrategy::Ptr c = all(pod(&local));
    req = c->send(rpc, g, 100);
  }

  CommStrategy::Ptr c = one(pod(&local), 0);
  c->recv(rpc, g, 100);
  if (req) {
    req->wait();
    delete req;
  }
}

int main(int argc, char** argv) {
  DummyRPC::run(8, &runner);
}
