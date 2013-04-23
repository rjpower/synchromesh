#include "sync.h"
#include "mpirpc.h"

#include <boost/thread.hpp>

using std::vector;
using std::string;

namespace synchromesh {

int Synchromesh::fn_max_ = 0;
UpdateFunctionCreator* Synchromesh::fn_map_[16];

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
  LOG("worker -> init");
  // Barrier; wait for all register_ calls to complete.
  InitOptions init_opt = { id };
  network_->send_all(kInitBarrier, (char*) &init_opt, sizeof(init_opt));

  {
    vector<InitOptions> res;
    network_->recv_all(kInitBarrier, &res);
  }

  // Send initial state to sync servers.
  SendRecvHelper rpc(kInitData, *network_);
  rpc.send_all(init_opt);
  for (auto itr : local_) {
    Update& d = *itr.second;
    d.worker_init(rpc);
  }

  // Wait for initialization to finish.
  {
    vector<InitFinished> res;
    network_->recv_all(kInitDone, &res);
  }

  initialized_ = true;
}

void Synchromesh::syncer_recv_update(SendRecvHelper& rpc, int source) {
  LOG("sync <- worker");
  for (auto itr : tmp_) {
    itr.second->syncer_recv(rpc, source);
  }
}

void Synchromesh::syncer_send_state(SendRecvHelper& rpc, int dst) {
  LOG("sync -> worker");
  for (auto itr : global_) {
    itr.second->syncer_send(rpc, dst);
  }
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
    Update& d = *itr.second;
    d.worker_send(rpc);
  }
}

void Synchromesh::worker_recv_state(SendRecvHelper& rpc) {
  LOG("worker <- sync");
  for (auto itr : local_) {
    Update& d = *itr.second;
    d.worker_recv(rpc);
  }
}

void Synchromesh::syncer_loop() {
  // Wait for initialization messages from each worker
  {
    SendRecvHelper rpc(kInitData, *network_);
    vector<InitOptions> opt;
    rpc.recv_all(&opt);

    for (auto itr : local_) {
      const string& name = itr.first;
      tmp_[name] = itr.second->copy();
      tmp_[name]->syncer_init(rpc);
      global_[name] = tmp_[name]->copy();
    }

    UpdateFunctionBase* f = fn_map_[opt[0].init_fn_id]->create();
    (*f)(tmp_, global_);

    InitFinished init_f;
    network_->send_all(kInitDone, init_f);
  }

  LOG("Initialization finished.");

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
      UpdateFunctionCreator* creator = fn_map_[opt.update_fn_id];
      UpdateFunctionBase* fn = creator->create();
      {
        SendRecvHelper rpc(kWorkerData, *network_);
        fn->read_values(rpc, opt.worker_id);
        syncer_recv_update(rpc, opt.worker_id);
      }
      (*fn)(tmp_, global_);
      {
        SendRecvHelper rpc(kSyncerData, *network_);
        syncer_send_state(rpc, opt.worker_id);
      }
    }
  }
}

Update* Synchromesh::register_update(const std::string& name, Update* data) {
  ASSERT_EQ(initialized_, false);
  local_[name] = data;
  return data;
}

Synchromesh::Synchromesh(RPC* network) {
  initialized_ = false;
  network_ = network;
  stop_recv_loop_ = false;
  update_worker_ = new boost::thread(&Synchromesh::syncer_loop, this);
}

Synchromesh::~Synchromesh() {
  stop_recv_loop_ = true;
  update_worker_->join();
  barrier(network_);
  delete network_;
}

} // namespace synchromesh
