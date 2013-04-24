#include "sync.h"
#include "mpirpc.h"

#include <boost/thread.hpp>

using std::vector;
using std::string;

namespace synchromesh {

struct SyncOptions {
  bool wait_for_all;
  int update_fn_id;
  int worker_id;
};

struct InitOptions {
  int init_fn_id;
};

struct InitFinished {

};

struct Barrier {};

static void barrier(RPC* net) {
  Barrier b;
  net->send_all(kBarrier, b);
  vector<Barrier> b_fin;
  net->recv_all(kBarrier, &b_fin);
}

void Synchromesh::do_init(int id) {
  LOG("worker -> init_barrier");
  barrier(network_);
}

void Synchromesh::worker_send_update(SendRecvHelper& rpc, int update_fn_id) {
  LOG("worker -> sync");
  ASSERT_EQ(initialized_, true);
  SyncOptions opt;
  opt.wait_for_all = false;
  opt.update_fn_id = update_fn_id;
  opt.worker_id = network_->id();
  network_->send_all(kUpdateStart, opt);
  
  for (auto itr : local_) {
    Data& d = *itr.second;
    rpc.send_all(d.id());
    rpc.send_all(itr.first);
    d.send(rpc);
  }
}

void Synchromesh::worker_recv_state(SendRecvHelper& rpc) {
  LOG("worker <- sync");
  for (auto itr : local_) {
    Data& d = *itr.second;
    d.recv(rpc);
  }
}

void SyncServer::recv_update(SendRecvHelper& rpc, int src) {
  LOG("sync <- worker");

  for (auto itr : tmp_) {
    itr.second->syncer_recv(rpc, source);
  }
}

void SyncServer::send_state(SendRecvHelper& rpc, int dst) {
  LOG("sync -> worker");
  for (auto itr : global_) {
    itr.second->syncer_send(rpc, dst);
  }
}

void SyncServer::loop() {
  while (!stop_recv_loop_) {
    if (!network_->has_data(RPC::kAnyWorker, kUpdateStart)) {
      sched_yield();
      continue;
    }

    SyncOptions opt;
    network_->recv_pod(RPC::kAnyWorker, kUpdateStart, &opt);
    if (opt.wait_for_all) {
      PANIC("Not implemented.");
    } else {
      UpdateFunction* fn = UpdateFunctionRegistry::create(opt.update_fn_id);
      {
        SendRecvHelper rpc(kWorkerData, *network_);
        fn->read_values(rpc, opt.worker_id);
        recv_update(rpc, opt.worker_id);
      }
      (*fn)(tmp_, global_);
      {
        SendRecvHelper rpc(kSyncerData, *network_);
        send_state(rpc, opt.worker_id);
      }
    }
  }
}

void SyncServer::stop() {
  stop_recv_loop_ = true;
  thread_->join();
}

void SyncServer::start() {

}

Data* Synchromesh::register_update(const std::string& name, Data* data) {
  ASSERT_EQ(initialized_, false);
  local_[name] = data;
  return data;
}

Synchromesh::Synchromesh(RPC* network) {
  initialized_ = false;
  network_ = network;
  server_ = new SyncServer(network_);
}

Synchromesh::~Synchromesh() {
  barrier(network_);
  server_->stop();
  delete network_;

}

} // namespace synchromesh
