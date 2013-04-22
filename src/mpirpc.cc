#include "mpirpc.h"

int DummyRPC::num_workers_;
std::vector<DummyRPC*> DummyRPC::workers_;

void DummyRPC::run(int num_workers, boost::function<void(DummyRPC*)> run_f) {
//  pth_init();
  num_workers_ = num_workers;
  workers_.resize(num_workers);
  std::vector<boost::thread*> threads_(num_workers_);
  for (size_t i = 0; i < workers_.size(); ++i) {
    workers_[i] = new DummyRPC(i);
  }
  for (size_t i = 0; i < workers_.size(); ++i) {
    threads_[i] = new boost::thread(boost::bind(run_f, workers_[i]));
  }
  for (size_t i = 0; i < workers_.size(); ++i) {
    threads_[i]->join();
    delete threads_[i];
  }
}

bool DummyRPC::has_data_internal(int& src, int& tag) const {
  if (src == kAnyWorker) {
    for (int i = 0; i < num_workers_; ++i) {
      if (has_data_internal(i, tag)) {
        src = i;
        return true;
      }
    }
    return false;
  }

  if (tag == kAnyTag) {
    for (int i = 0; i < kMaxTagId; ++i) {
      if (!data_[src][i].empty()) {
        tag = i;
        return true;
      }
    }
    return false;
  }

  return !data_[src][tag].empty();
}

size_t DummyRPC::recv_data(int src, int tag, char* ptr, int bytes) {
  ASSERT_GE(bytes, 0);
  if (src == kAnyWorker || tag == kAnyTag) {
    while (!has_data_internal(src, tag)) {
//      fiber::yield();
      sched_yield();
    }
  }
  while (!has_data_internal(src, tag)) {
    sched_yield();
  }
  Packet p;
  {
    boost::recursive_mutex::scoped_lock l(mut_);
    PacketList& pl = data_[src][tag];
    p = pl.front();
    pl.pop_front();
  }
  ASSERT_EQ((int)p.size(), bytes);
  memcpy(ptr, p.data(), p.size());
  return p.size();
}

size_t DummyRPC::send_data(int dst, int tag, const char* ptr, int bytes) {
  DummyRPC* dst_rpc = workers_[dst];
  {
    boost::recursive_mutex::scoped_lock l(dst_rpc->mut_);
    PacketList& pl = dst_rpc->data_[worker_id_][tag];
    pl.push_back(Packet(ptr, bytes));
  }
  return bytes;
}

size_t MPIRPC::recv_data(int src, int tag, char* ptr, int bytes) {
  ASSERT(src <= last(), "Target not a valid worker index");
  if (src == kAnyWorker) {
    src = MPI::ANY_SOURCE;
  }
  if (tag == kAnyTag) {
    tag = MPI::ANY_TAG;
  }
  LOG("Receiving from: %d %d %p %d", src, tag, ptr, bytes);
  MPI::Status status;
  while (!_world.Iprobe(src, tag, status)) {
    fiber::yield();
  }
  ASSERT_EQ(status.Get_count(MPI::CHAR), bytes);
  MPI::Request pending = _world.Irecv(ptr, bytes, MPI::CHAR, src, tag);
  while (!pending.Test(status)) {
    fiber::yield();
  }
  ASSERT_EQ(status.Get_count(MPI::CHAR), bytes);
  return bytes;
}

size_t MPIRPC::send_data(int dst, int tag, const char* ptr, int bytes) {
  ASSERT(dst <= last(), "Target not a valid worker index");
  if (dst == kAnyWorker) {
    dst = MPI::ANY_SOURCE;
  }
  if (tag == kAnyTag) {
    tag = MPI::ANY_TAG;
  }
  LOG("Sending to: %d %d %p %d", dst, tag, ptr, bytes);
  MPI::Request pending = _world.Isend(ptr, bytes, MPI::CHAR, dst, tag);
  MPI::Status status;
  while (!pending.Test(status)) {
    fiber::yield();
  }
  ASSERT_EQ(status.Get_count(MPI::CHAR), bytes);
  return bytes;
}

MPIRPC::MPIRPC() :
    _world(MPI::COMM_WORLD) {
  int is_initialized = 0;
  MPI_Initialized(&is_initialized);
  if (!is_initialized) {
    MPI::Init_thread(MPI::THREAD_SERIALIZED);
  }
  pth_init();
}

bool MPIRPC::has_data(int src, int tag) const {
  return _world.Iprobe(src, tag);
}

int MPIRPC::first() const {
  return 0;
}

int MPIRPC::last() const {
  return _world.Get_size() - 1;
}

int MPIRPC::id() const {
  return _world.Get_rank();
}

ShardCalc::ShardCalc(int num_elements, int elem_size, int num_workers) :
    num_workers_(num_workers), num_elements_(num_elements), elem_size_(elem_size) {
}

size_t ShardCalc::start(int worker) {
  int64_t elems_per_server = num_elements_ / num_workers_;
  int64_t offset = worker * elems_per_server;
  if (offset > num_elements_) {
    offset = num_elements_;
  }
  return offset * elem_size_;
}

size_t ShardCalc::end(int worker) {
  int64_t elems_per_server = num_elements_ / num_workers_;
  int64_t offset = (worker + 1) * elems_per_server;
  if (offset > num_elements_ || worker == num_workers_ - 1) {
    offset = num_elements_;
  }
  return offset * elem_size_;
}

size_t ShardCalc::size(int worker) {
  return end(worker) - start(worker);
}
