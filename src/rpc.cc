#include "rpc.h"

namespace synchromesh {

int DummyRPC::num_workers_;
std::vector<DummyRPC*> DummyRPC::workers_;
std::vector<boost::thread*> DummyRPC::threads_;

static const int kMPIBufferBytes = 1 << 28;

// DummyRPC requests always complete immediately.
class DummyRequest: public Request {
public:
  bool done() {
    return true;
  }
  void wait() {
  }
};

class MPIRequest: public Request {
private:
  MPI::Request req_;
public:
  MPIRequest(MPI::Request r) :
      req_(r) {
  }

  bool done() {
    return req_.Test();
  }
  void wait() {
    return req_.Wait();
  }
};

void DummyRPC::run(int num_workers, boost::function<void(DummyRPC*)> run_f) {
//  pth_init();
  num_workers_ = num_workers;
  workers_.resize(num_workers);
  threads_.resize(num_workers_);
  for (size_t i = 0; i < workers_.size(); ++i) {
    workers_[i] = new DummyRPC(i);
  }

  for (size_t i = 0; i < workers_.size(); ++i) {
    threads_[i] = new boost::thread(boost::bind(run_f, workers_[i]));
  }

  for (size_t i = 0; i < workers_.size(); ++i) {
    threads_[i]->join();
  }
}

DummyRPC::~DummyRPC() {
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

  boost::recursive_mutex::scoped_lock l(mut_);
  if (tag == kAnyTag) {
    for (auto t : data_[src]) {
      if (!t.second.empty()) {
        tag = t.first;
        return true;
      }
    }
    return false;
  }

  return !data_[src][tag].empty();
}

void DummyRPC::recv_data(int src, int tag, void* ptr, int bytes) {
  LOG("Receiving... %d %d %d", src, tag, bytes);
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
    PacketList& pl = data_[src][tag];
    p = pl.front();
    pl.pop_front();
  }
  ASSERT_EQ((int) p.size(), bytes);
  memcpy(ptr, p.data(), p.size());
}

Request* DummyRPC::send_data(int dst, int tag, const void* ptr, int bytes) {
  LOG("Sending... %d %d %d", dst, tag, bytes);
  DummyRPC* dst_rpc = workers_[dst];
  {
    boost::recursive_mutex::scoped_lock l(dst_rpc->mut_);
    PacketList& pl = dst_rpc->data_[worker_id_][tag];
    pl.push_back(Packet((char *) ptr, bytes));
  }

  return new DummyRequest();
}

int DummyRPC::first() const {
  return 0;
}

int DummyRPC::last() const {
  return num_workers_ - 1;
}

int DummyRPC::id() const {
  return worker_id_;
}

bool DummyRPC::poll(int src, int tag) const {
  return has_data_internal(src, tag);
}

void MPIRPC::recv_data(int src, int tag, void* ptr, int bytes) {

  ASSERT(src <= last(), "Target not a valid worker index");
  if (src == kAnyWorker) {
    src = MPI::ANY_SOURCE;
  }
  if (tag == kAnyTag) {
    tag = MPI::ANY_TAG;
  }
  LOG("Receiving from: %d %d %p %d", src, tag, ptr, bytes);

  world_.Recv(ptr, bytes, MPI::CHAR, src, tag);
  LOG("Recv DONE: %d %d %p %d", src, tag, ptr, bytes);
}

Request* MPIRPC::send_data(int dst, int tag, const void* ptr, int bytes) {
//  boost::mutex::scoped_lock lock(mut_);
  ASSERT(dst <= last(), "Target not a valid worker index");
  if (dst == kAnyWorker) {
    dst = MPI::ANY_SOURCE;
  }
  if (tag == kAnyTag) {
    tag = MPI::ANY_TAG;
  }

  MPI::Request req = world_.Ibsend(ptr, bytes, MPI::CHAR, dst, tag);
  LOG("Send done to: %d %d %p %d", dst, tag, ptr, bytes);

  return new MPIRequest(req);
}

MPIRPC::MPIRPC() :
    world_(MPI::COMM_WORLD) {
  int is_initialized = 0;
  MPI_Initialized(&is_initialized);
  if (!is_initialized) {
    MPI::Init_thread(MPI::THREAD_SERIALIZED);

    void* mpi_buffer = malloc(kMPIBufferBytes);
    MPI::Attach_buffer(mpi_buffer, kMPIBufferBytes);
  }
//  fiber::init();
}

bool MPIRPC::poll(int src, int tag) const {
  return world_.Iprobe(src, tag);
}

int MPIRPC::first() const {
  return 0;
}

int MPIRPC::last() const {
  return world_.Get_size() - 1;
}

int MPIRPC::id() const {
  return world_.Get_rank();
}


} // namespace synchromesh
