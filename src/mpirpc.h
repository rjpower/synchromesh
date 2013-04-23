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

  // Send data at ptr to the worker 'tgt' with tag 'tag'.
  // Returns the number of bytes actually sent.
  virtual size_t send_data(int dst, int tag, const char* ptr, int bytes) = 0;
  virtual size_t recv_data(int src, int tag, char* ptr, int bytes) = 0;

  virtual bool has_data(int src, int tag) const = 0;

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
  void send_all(int tag, const T* data, int num_elems) {
    for (int i = first(); i <= last(); ++i) {
      send_data(i, tag, (char*) data, sizeof(T) * num_elems);
    }
  }

  void send_sharded(int tag, const char* data, int elem_size, int num_elems) {
    ShardCalc calc(num_elems, elem_size, num_workers());
    for (int j = 0; j < num_workers(); ++j) {
      int dst = first() + j;
      send_data(dst, tag, data + calc.start_byte(j), calc.num_bytes(j));
    }
  }

  template<class T>
  void send_sharded(int tag, const T* data, int num_elems) {
    send_sharded(tag, (char*)data, sizeof(T), num_elems);
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
  void send_array(int dst, int tag, T* data, int num_elems) {
    ASSERT_EQ(send_data(dst, tag, (char*)data, num_elems * sizeof(T)), num_elems * sizeof(T));
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
      recv_data(src, tag, (char*)(data + calc.start_elem(j)), calc.num_bytes(j));
    }
  }

  template<class T>
  bool maybe_recv(int src, int tag, T* data, MPI::Status& st);
};

class MPIRPC: public RPC {
private:
  MPI::Intracomm world_;
  boost::mutex mut_;

public:
  MPIRPC();
  virtual ~MPIRPC() {
    MPI::Finalize();
  }

  size_t send_data(int dst, int tag, const char* ptr, int bytes);
  virtual size_t recv_data(int src, int tag, char* ptr, int bytes);
  virtual bool has_data(int src, int tag) const;

  virtual int first() const;
  virtual int last() const;
  virtual int id() const;
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

  bool has_data(int src, int tag) const {
    return has_data_internal(src, tag);
  }
};

#endif /* MPIRPC_H_ */
