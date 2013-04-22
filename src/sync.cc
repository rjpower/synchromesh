#include "sync.h"
#include "mpirpc.h"

#include <boost/thread.hpp>

using std::vector;
using std::string;

namespace synchromesh {

Synchromesh::UpdateFunctionMap Synchromesh::fn_map_;

struct SyncOptions {
  bool wait_for_all;
  int update_fn_id;
  int worker_id;
};

struct InitOptions {

};

void Synchromesh::syncer_recv_update(SendRecvHelper& rpc, int source) {
  for (auto itr : local_) {
    const string& name = itr.first;
    if (tmp_.find(name) == tmp_.end()) {
      tmp_[name] = itr.second->copy();
    }
    Update& tgt = *tmp_[name];
    tgt.syncer_recv(rpc, source);
  }
}

void Synchromesh::wait_for_initialization() {
  if (first_update_) {
    InitOptions init_opt;
    network_->send_all(kUpdateInitialize, (char*) &init_opt, sizeof(init_opt));
    vector<InitOptions> res;
    network_->recv_all(kUpdateInitialize, &res);
    first_update_ = false;
  }
}

void Synchromesh::worker_send_update(SendRecvHelper& rpc, int update_fn_id) {
  wait_for_initialization();

  SyncOptions opt;
  opt.wait_for_all = false;
  opt.update_fn_id = update_fn_id;
  opt.worker_id = network_->id();
  network_->send_all(kUpdateSendStart, opt);
  for (auto itr : local_) {
    Update& d = *itr.second;
    d.worker_send(rpc);
  }
}

void Synchromesh::worker_recv_state(SendRecvHelper& rpc) {
  for (auto itr : local_) {
    Update& d = *itr.second;
    d.worker_recv(rpc);
  }
}

void Synchromesh::syncer_loop() {
  SyncOptions opt;
  while (!stop_recv_loop_) {
    if (!network_->has_data(RPC::kAnyWorker, kUpdateSendStart)) {
      sched_yield();
      continue;
    }

    network_->recv_pod(RPC::kAnyWorker, kUpdateSendStart, &opt);
    if (opt.wait_for_all) {
      PANIC("Not implemented.");
    } else {
      SendRecvHelper rpc(kWorkerData, *network_);
      UpdateFunctionCreator* creator = fn_map_[opt.update_fn_id];
      UpdateFunctionBase* fn = creator->create();
      fn->read_values(rpc, opt.worker_id);
      syncer_recv_update(rpc, opt.worker_id);
      (*fn)(tmp_, global_);
    }
  }
}

Update* Synchromesh::register_update(const std::string& name, Update* data) {
  ASSERT_EQ(first_update_, true);
  local_[name] = data;
  return data;
}

Synchromesh::Synchromesh(RPC* network) {
  first_update_ = true;
  network_ = network;
  stop_recv_loop_ = false;
  update_worker_ = new boost::thread(&Synchromesh::syncer_loop, this);
}

Synchromesh::~Synchromesh() {
  stop_recv_loop_ = true;
  update_worker_->join();
  delete network_;
}

} // namespace synchromesh
