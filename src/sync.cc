#include "sync.h"
#include "mpirpc.h"

#include <boost/thread.hpp>

using std::vector;
using std::string;

namespace synchromesh {

enum MessageTag {
  kUpdateInitialize = 1000,
  kUpdateSendStart = 1001,
  kUpdateSendData = 1100,
  kUpdateRecvData = 1200
};

UpdateFunctionMap Synchromesh::fn_map_;

Synchromesh::UpdateFnRegistrar::UpdateFnRegistrar(UpdateFunction fn) {
  int sz = fn_map_.size();
  fn_map_[sz] = fn;
}

struct SyncOptions {
  bool wait_for_all;
  int update_fn_id;
  int worker_id;
};

struct InitOptions {

};

int Synchromesh::get_id(UpdateFunction fn) {
  for (auto itr : fn_map_) {
    if (itr.second == fn) {
      return itr.first;
    }
  }
  return -1;
}

void Synchromesh::recv_data(RPC& rpc, int source) {
  int idx = kUpdateSendData;
  for (auto itr : local_) {
    const string& name = itr.first;
    if (tmp_.find(name) == tmp_.end()) {
      tmp_[name] = itr.second->copy();
    }
    Update& tgt = *tmp_[name];
    tgt.syncer_recv(rpc, source, idx);
  }
}

void Synchromesh::send_update(UpdateFunction fn, bool wait_for_all) {
  if (first_update_) {
    InitOptions init_opt;
    network_->send_all(kUpdateInitialize, (char*)&init_opt, sizeof(init_opt));
    vector<InitOptions> res;
    network_->recv_all(kUpdateInitialize, &res);
    first_update_ = false;
  }

  SyncOptions opt;
  opt.wait_for_all = wait_for_all;
  opt.update_fn_id = get_id(fn);
  opt.worker_id = network_->id();
  network_->send_all(kUpdateSendStart, opt);
  int idx = kUpdateSendData;
  for (auto itr : local_) {
    Update& d = *itr.second;
    d.worker_send(*network_, idx);
  }
}

void Synchromesh::recv_updates() {
  SyncOptions opt;
  while (!stop_recv_loop_) {
    network_->recv_pod(RPC::kAnyWorker, kUpdateSendStart, &opt);
    if (opt.wait_for_all) {
      PANIC("Not implemented.");
    } else {
      recv_data(*network_, opt.worker_id);
      fn_map_[opt.update_fn_id](tmp_, global_);
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
  update_worker_ = new boost::thread(&Synchromesh::recv_updates, this);
}

Synchromesh::~Synchromesh() {
  stop_recv_loop_ = true;
  update_worker_->join();
  delete network_;
}

} // namespace synchromesh
