#ifndef MPIRPC_H_
#define MPIRPC_H_

#include <mpi.h>
#include <vector>
#include <deque>
#include <map>
#include <boost/type_traits.hpp>
#include <boost/thread.hpp>

#include "fiber.h"

class RPC;

class ProcessGroup {
public:
  int first;
  int last;

  int count() {
    return last - first + 1;
  }

  ProcessGroup(int f, int l) :
      first(f), last(l) {
  }
};

class Request {
public:
  virtual bool done() = 0;
  virtual void wait() = 0;
};

// Manage a batch of requests.
class RequestGroup: public Request {
public:
  void add(Request*);
  void wait();
  bool done();
};

class Marshalled {
protected:
  virtual Request* send_internal(RPC* rpc, int dst, int tag) const = 0;
  virtual Request* recv_internal(RPC* rpc, int src, int tag) = 0;
public:
  virtual int id() const = 0;

  Request* send(RPC* rpc, int dst, int tag) const;
  Request* recv(RPC* rpc, int src, int tag);
};

class CommStrategy {
private:
  std::vector<Marshalled>* m_;
public:
  void send(RPC* rpc, const ProcessGroup& g);
  void recv(RPC* rpc, const ProcessGroup& g);
};

CommStrategy* any(Marshalled*);
CommStrategy* all(Marshalled*);
CommStrategy* sharded(Marshalled*);

class ShardCalc {
private:
  int num_workers_;
  int num_elements_;
  int elem_size_;

public:
  ShardCalc(int num_elements, int elem_size, ProcessGroup group);
  size_t start_elem(int worker);
  size_t start_byte(int worker);

  size_t end_elem(int worker);
  size_t end_byte(int worker);

  size_t num_elems(int worker);
  size_t num_bytes(int worker);
};

class RPC {
public:
  static const int kAnyWorker = -1;
  static const int kAnyTag = -1;

  // Wait for outstanding requests and shutdown the network.
  virtual ~RPC() {
  }

  virtual Request* send_data(int dst, int tag, const void* ptr, int len) = 0;
  virtual Request* recv_data(int src, int tag, void* ptr, int len) = 0;

  virtual Request* send(int dst, int tag, const Marshalled& v) {
    return v.send(this, dst, tag);
  }

  virtual Request* recv(int src, int tag, Marshalled* v) {
    return v->recv(this, src, tag);
  }

  virtual bool has_data(int src, int tag) const = 0;

  // The first, last and current worker ids.
  virtual int first() const = 0;
  virtual int last() const = 0;
  virtual int id() const = 0;

  virtual int num_workers() const {
    return last() - first() + 1;
  }
};

template<class T>
Request* send_pod(RPC* rpc, int dst, int tag, const T& t) {
  return rpc->send_data(dst, tag, &t, sizeof(t));
}

template<class T>
Request* recv_pod(RPC* rpc, int src, int tag, T* t) {
  return rpc->recv_data(src, tag, t, sizeof(t));
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

  Request* send_data(int dst, int tag, const char* ptr, int bytes);
  Request* recv_data(int src, int tag, char* ptr, int bytes);
  bool has_data(int src, int tag) const;

  int first() const;
  int last() const;
  int id() const;

  void wait();
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

  Request* recv_data(int src, int tag, char* ptr, int bytes);
  Request* send_data(int dst, int tag, const char* ptr, int bytes);

  bool has_data(int src, int tag) const;
};

inline Request* Marshalled::send(RPC* rpc, int dst, int tag) const {
  RequestGroup* g = new RequestGroup;
  int my_id = id();
  g->add(rpc->send_data(dst, tag, &my_id, sizeof(int)));
  g->add(send_internal(rpc, dst, tag));
  return g;
}

inline Request* Marshalled::recv(RPC* rpc, int src, int tag) {
  return recv_internal(rpc, src, tag);
}

#endif /* MPIRPC_H_ */
