#include "sync.h"
#include "mpirpc.h"

#include <boost/thread.hpp>

using std::vector;
using std::string;

namespace update {
namespace internal {

typedef std::map<int, UpdateFunction> UpdateFunctionMap;

UpdateMap tmp_;
UpdateMap global_;
UpdateMap local_;
UpdateFunctionMap fn_map_;

int get_id(UpdateFunction fn);

const int kUpdateSendStart = 1000;
const int kUpdateSendData = 1100;
const int kUpdateRecvData = 1200;

RegisterHelper::RegisterHelper(UpdateFunction fn) {
  int sz = fn_map_.size();
  fn_map_[sz] = fn;
}

struct Options {
  bool wait_for_all;
  int update_fn_id;
};

void register_data(const std::string& name, Update* data) {
  local_[name] = data;
}

int get_id(UpdateFunction fn) {
  for (auto itr : fn_map_) {
    if (itr.second == fn) {
      return itr.first;
    }
  }
  return -1;
}

void send_update(UpdateFunction fn, bool wait_for_all) {
  MPIRPC rpc;
  Options opt;
  opt.wait_for_all = wait_for_all;
  opt.update_fn_id = get_id(fn);

  int idx = 0;

  rpc.send_all(kUpdateSendStart, &opt, sizeof(opt));

  for (auto itr : local_) {
    const Update& d = *itr.second;

    for (int w = 0; w < MPI::COMM_WORLD.Get_size(); ++w) {
      rpc.send_data(w, kUpdateSendData + idx, d.ptr(), d.size());
    }
    ++idx;
  }
}

void recv_data(MPIRPC& rpc, int source) {
  int idx = 0;
  for (auto itr : local_) {
    const string& name = itr.first;
    if (tmp_.find(name) == tmp_.end()) {
      tmp_[name] = itr.second->copy();
    }

    Update& tgt = *tmp_[name];

    if (tgt.shardable) {
      ShardCalc calc(tgt.size, tgt.elem_size);
      rpc.recv_data(source, kUpdateSendData + idx, tgt.as_array<char>() + calc.start(source), calc.size(source));
    } else {
      rpc.recv_data(source, kUpdateSendData + idx, tgt.as_array<char>(), tgt.size);
    }

    ++idx;
  }
}

void recv_updates() {
  MPIRPC rpc;
  Options opt;

  while (1) {
    for (int i = 0; i < MPI::COMM_WORLD.Get_size(); ++i) {
      rpc.recv_pod(MPI::ANY_SOURCE, kUpdateSendStart, &opt);
      if (opt.wait_for_all) {

      } else {
        recv_data(rpc, i);
        fn_map_[opt.update_fn_id](tmp_, global_);
      }
    }
  }
}

} // namespace internal
} // namespace update

namespace update {

void initialize() {
  static boost::thread update_worker(&internal::recv_updates);
}

} // namespace update
