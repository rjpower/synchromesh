#ifndef MPIRPC_H_
#define MPIRPC_H_

#include <mpi.h>
#include <vector>
#include <deque>
#include <map>
#include <boost/type_traits.hpp>
#include <boost/thread.hpp>

#include "fiber.h"

namespace synchromesh {

class RPC;

class Request {
public:
  virtual ~Request() {

  }
  typedef boost::shared_ptr<Request> Ptr;
  virtual bool done() = 0;
  virtual void wait() = 0;
};

// Manage a batch of requests.
class RequestGroup: public Request {
  std::vector<Request::Ptr> reqs_;
public:
  void add(Request* req) {
    add(Request::Ptr(req));
  }

  void add(Request::Ptr req) {
    reqs_.push_back(req);
  }

  void wait() {
    for (auto m : reqs_) {
      m->wait();
    }
  }

  bool done() {
    for (auto m : reqs_) {
      if (!m->done()) {
        return false;
      }
    }
    return true;
  }
};

class RPC {
public:
  static const int kAnyWorker = -1;
  static const int kAnyTag = -1;

  // Wait for outstanding requests and shutdown the network.
  virtual ~RPC() {
  }

  virtual Request* send_data(int dst, int tag, const void* ptr, int len) = 0;
  virtual void recv_data(int src, int tag, void* ptr, int len) = 0;
  virtual bool poll(int src, int tag) const = 0;

  // The first, last and current worker ids.
  virtual int first() const = 0;
  virtual int last() const = 0;
  virtual int id() const = 0;

  virtual int num_workers() const {
    return last() - first() + 1;
  }
};

template<class T>
static Request* send_pod(RPC* rpc, int dst, int tag, T t) {
  return rpc->send_data(dst, tag, &t, sizeof(t));
}

template<class T>
static void recv_pod(RPC* rpc, int src, int tag, T* t) {
  rpc->recv_data(src, tag, t, sizeof(t));
}

template<class T>
static T recv_pod(RPC* rpc, int src, int tag) {
  T v;
  recv_pod(rpc, src, tag, &v);
  return v;
}

class MPIRPC: public RPC {
private:
  MPI::Intracomm world_;
  boost::mutex mut_;

public:
  MPIRPC();
  virtual ~MPIRPC() {
    MPI::Finalize();
  }

  Request* send_data(int dst, int tag, const void* ptr, int bytes);
  void recv_data(int src, int tag, void* ptr, int bytes);
  bool poll(int src, int tag) const;

  int first() const;
  int last() const;
  int id() const;
};

// Pretend to run MPI using a bunch of threads.
// How slow can we make this go!?
class DummyRPC: public RPC {
private:
  static int num_workers_;
  static std::vector<DummyRPC*> workers_;
  static std::vector<boost::thread*> threads_;

  typedef std::string Packet;
  typedef std::deque<Packet> PacketList;
  typedef std::map<int, PacketList> TagMap;
  typedef std::map<int, TagMap> DataMap;

  // So we can use operator[]
  mutable DataMap data_;
  mutable boost::recursive_mutex mut_;

  int worker_id_;

  DummyRPC(int worker_id) {
    worker_id_ = worker_id;
  }

  bool has_data_internal(int& src, int& tag) const;

public:
  static void run(int num_workers, boost::function<void(DummyRPC*)> run_f);

  virtual ~DummyRPC();

  int first() const;
  int last() const;
  int id() const;

  Request* send_data(int dst, int tag, const void* ptr, int bytes);
  void recv_data(int src, int tag, void* ptr, int bytes);

  bool poll(int src, int tag) const;
};

} // namespace synchromesh

#endif /* MPIRPC_H_ */
