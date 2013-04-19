#ifndef MPIRPC_H_
#define MPIRPC_H_

#include <mpi.h>
#include <vector>
#include <deque>
#include <map>
#include <boost/thread.hpp>

#include "fiber.h"

class RPC;

class ShardCalc {
private:
  int num_workers_;
  int num_elements_;
  int elem_size_;

public:
  ShardCalc(int num_elements, int elem_size, int num_workers);
  size_t start(int worker);
  size_t end(int worker);
  size_t size(int worker);
};

class RPC {
public:
  static const int kAnyWorker = -1;
  static const int kAnyTag = -1;

  // Send data at ptr to the worker 'tgt' with tag 'tag'.
  // Returns the number of bytes actually sent.
  virtual size_t send_data(int dst, int tag, const char* ptr, int bytes) = 0;
  virtual size_t recv_data(int src, int tag, char* ptr, int bytes) = 0;

  // The first, last and current worker ids.
  virtual int first() const = 0;
  virtual int last() const = 0;
  virtual int id() const = 0;

  virtual int num_workers() const {
    return last() - first() + 1;
  }

  // Wait for outstanding requests and shutdown the network.
  virtual ~RPC() {

  }

  template<class T>
  void send_all(int tag, const T* data, int num_bytes) {
    for (int i = first(); i <= last(); ++i) {
      send_data(i, tag, (char*) data, sizeof(T) * num_bytes);
    }
  }

  void send_sharded(int tag, const char* data, int elem_size, int num_elems) {
    ShardCalc calc(num_elems, elem_size, num_workers());
    for (int j = 0; j < num_workers(); ++j) {
      int dst = first() + j;
      send_data(dst, tag, data + calc.start(j), calc.size(j));
    }
  }

  template<class T>
  void send_sharded(int tag, const T* data, int num_elems) {
    send_sharded(tag, data, sizeof(T), num_elems);
  }

  template<class T>
  void send_pod(int dst, int tag, const T& data) {
    send_data(dst, tag, (char*) ((&data)), sizeof(T));
  }

  template<class T>
  void send_all(int tag, const T& data) {
    for (int i = first(); i <= last(); ++i) {
      send_pod(i, tag, data);
    }
  }

  template<class T>
  void recv_array(int src, int tag, T* data, int num_elems) {
    ASSERT_EQ(recv_data(src, tag, (char*)data, num_elems * sizeof(T)), num_elems * sizeof(T));
  }

  template<class T>
  void recv_pod(int src, int tag, T* val) {
    recv_data(src, tag, (char*) ((val)), sizeof(T));
  }

  template<class T>
  void recv_all(int tag, std::vector<T>* res) {
    res->resize(last() - first() + 1);
    for (int i = first(); i <= last(); ++i) {
      recv_pod(i, tag, &res->at(i - first()));
    }
  }

  template<class T>
  void recv_sharded(int tag, T* data, int num_elems) {
    int64_t num_servers = (last() - first() + 1);
    ShardCalc calc(num_elems, sizeof(T), num_servers);
    for (int j = 0; j < num_servers; ++j) {
      int src = first() + j;
      recv_data(src, tag, data + calc.start(j), calc.size(j));
    }
  }

  template<class T>
  bool maybe_recv(int src, int tag, T* data, MPI::Status& st);
};

class MPIRPC: public RPC {
private:
  MPI::Intracomm _world;

public:
  MPIRPC();
  virtual ~MPIRPC() {
    MPI::Finalize();
  }

  size_t send_data(int dst, int tag, const char* ptr, int bytes);
  virtual size_t recv_data(int src, int tag, char* ptr, int bytes);

  virtual int first() const {
    return 0;
  }

  virtual int last() const {
    return _world.Get_size() - 1;
  }

  virtual int id() const {
    return _world.Get_rank();
  }
};

// Pretend to run MPI using a bunch of threads.
// How slow can we make this go!?
class DummyRPC: public RPC {
private:
  static int num_workers_;
  static std::vector<DummyRPC*> workers_;

  static const int kMaxTagId = 4096;

  boost::recursive_mutex mut_;

  typedef std::string Packet;
  typedef std::deque<Packet> PacketList;
  typedef std::vector<PacketList> TagMap;
  typedef std::vector<TagMap> DataMap;

  DataMap data_;
  int worker_id_;

  DummyRPC(int worker_id) {
    worker_id_ = worker_id;
    data_.resize(num_workers_);
    for (size_t i = 0; i < data_.size(); ++i) {
      data_[i].resize(kMaxTagId);
    }
  }

  size_t has_data(int& src, int& tag);
public:
  static void run(int num_workers, boost::function<void(DummyRPC*)> run_f);

  virtual ~DummyRPC() {

  }

  int first() const {
    return 0;
  }

  int last() const {
    return num_workers_ - 1;
  }

  int id() const {
    return worker_id_;
  }

  size_t recv_data(int src, int tag, char* ptr, int bytes);
  size_t send_data(int dst, int tag, const char* ptr, int bytes);
};


#endif /* MPIRPC_H_ */
