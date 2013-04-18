#include "sync.h"
#include "mpirpc.h"

#include <boost/thread.hpp>

using std::vector;
using std::string;

namespace synchromesh {
namespace internal {

typedef std::map<int, UpdateFunction> UpdateFunctionMap;

static UpdateMap tmp_;
static UpdateMap global_;
static UpdateMap local_;
static UpdateFunctionMap fn_map_;

int get_id(UpdateFunction fn);

const int kUpdateSendStart = 1000;
const int kUpdateSendData = 1100;
const int kUpdateRecvData = 1200;

UpdateFnRegistrar::UpdateFnRegistrar(UpdateFunction fn) {
  int sz = fn_map_.size();
  fn_map_[sz] = fn;
}

struct Options {
  bool wait_for_all;
  int update_fn_id;
};

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

  rpc.send_all(kUpdateSendStart, &opt, sizeof(opt));

  int idx = kUpdateSendData;
  for (auto itr : local_) {
    Update& d = *itr.second;
    d.send(rpc, idx);
  }
}

void recv_data(MPIRPC& rpc, int source) {
  int idx = kUpdateSendData;
  for (auto itr : local_) {
    const string& name = itr.first;
    if (tmp_.find(name) == tmp_.end()) {
      tmp_[name] = itr.second->copy();
    }

    Update& tgt = *tmp_[name];
    tgt.recv(rpc, source, idx);
  }
}

static boost::mutex recv_mutex;
static boost::condition_variable recv_cv;
static bool recv_active = true;

void recv_updates() {
  MPIRPC rpc;
  Options opt;

  while (recv_active) {
    for (int i = 0; i < MPI::COMM_WORLD.Get_size(); ++i) {
      rpc.recv_pod(MPI::ANY_SOURCE, kUpdateSendStart, &opt);
      if (opt.wait_for_all) {

      } else {
        recv_data(rpc, i);
        fn_map_[opt.update_fn_id](tmp_, global_);
//        send_data(rpc, i);
      }
    }
  }

  recv_cv.notify_all();
}

void shutdown() {
  boost::unique_lock<boost::mutex> lock(recv_mutex);
  recv_active = false;
  recv_cv.wait(lock);

  MPI::Finalize();
}

} // namespace internal
} // namespace synchromesh

namespace synchromesh {

void register_update(const std::string& name, Update* data) {
  internal::local_[name] = data;
}

void initialize() {
  int is_initialized = 0;
  MPI_Initialized(&is_initialized);

  if (!is_initialized) {
    MPI::Init_thread(MPI::THREAD_SERIALIZED);
  }

  pth_init();

  static boost::thread update_worker(&internal::recv_updates);
  atexit(&internal::shutdown);
}

} // namespace synchromesh
