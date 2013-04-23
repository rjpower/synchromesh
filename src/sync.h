#ifndef SYNCHROMESH_SYNC_H
#define SYNCHROMESH_SYNC_H

#include <string>
#include <map>
#include <vector>

#include <stddef.h>
#include <boost/noncopyable.hpp>

#include "mpirpc.h"

namespace synchromesh {

class Update;
typedef std::map<std::string, Update*> UpdateMap;

enum MessageTag {
  kInitBarrier = 1000,
  kInitStart = 1001,
  kInitDone = 1002,
  kInitData = 1003,
  kUpdateStart = 1100,
  kWorkerData = 1101,
  kSyncerData = 1200,
  kBarrier = 1300
};

// Helper for performing symmetric send/receives.
// Increments 'tag' for each RPC call.
class SendRecvHelper : private boost::noncopyable {
private:
  RPC& net_;
  int tag_;

public:
  int id() {
    return net_.id();
  }

  RPC& network() {
      return net_;
  }

  SendRecvHelper(int tag, RPC& rpc) :
      net_(rpc), tag_(tag) {
  }

  int num_workers() const {
    return net_.num_workers();
  }

  template<class T>
  void send_all(const T& v) {
    net_.send_all(tag_++, v);
  }

  template<class T>
  void send_all(const T* ptr, int num_elems) {
    net_.send_all(tag_++, ptr, num_elems);
  }

  template<class T>
  void send_sharded(const T* v, int num_elems) {
    net_.send_sharded(tag_++, v, num_elems);
  }

  void send_sharded(const char* v, int elem_size, int num_elems) {
    net_.send_sharded(tag_++, v, elem_size, num_elems);
  }

  template <class T>
  void send_array(int dst, const T* ptr, int num_elems) {
    net_.send_array(dst, tag_++, ptr, num_elems);
  }

  template<class T>
  void recv_pod(int src, T* v) {
    net_.recv_pod(src, tag_++, v);
  }

  template<class T>
  T recv_pod(int src) {
    T v;
    net_.recv_pod(src, tag_++, v);
    return v;
  }

  template<class T>
  void recv_array(int src, T* v, int num_elems) {
    net_.recv_array(src, tag_++, v, num_elems);
  }

  template <class T>
  void recv_sharded(T* v, int num_elems) {
    net_.recv_sharded(tag_++, v, num_elems);
  }

  template <class T>
  void recv_all(std::vector<T>* vals) {
    net_.recv_all(tag_++, vals);
  }
};

class Update: private boost::noncopyable {
public:
  template<class T>
  T* as_array() {
    return (T*) ptr();
  }

  template<class T>
  const T& as() {
    return *((const T*) ptr());
  }

  virtual void* ptr() = 0;

  virtual void worker_init(SendRecvHelper& rpc) {
//    worker_send(rpc);
  }

  virtual void syncer_init(SendRecvHelper&) {
  }

  virtual void worker_send(SendRecvHelper&) = 0;
  virtual void worker_recv(SendRecvHelper&) = 0;

  virtual void syncer_recv(SendRecvHelper&, int src) = 0;
  virtual void syncer_send(SendRecvHelper&, int dst) = 0;

  virtual Update* copy() = 0;
};

class PODUpdate: public Update {
private:
  void* ptr_;
  size_t size_;
  PODUpdate(void* v, int sz) :
      ptr_(v), size_(sz) {
  }

public:
  template<class T>
  PODUpdate(const T* v) :
      ptr_((void*) v), size_(sizeof(T)) {
  }

  void* ptr() {
    return ptr_;
  }

  void worker_send(SendRecvHelper& rpc) {
    rpc.send_all((char*) ptr_, size_);
  }

  void worker_recv(SendRecvHelper& rpc) {
  }

  void syncer_send(SendRecvHelper& rpc, int dst) {
  }

  void syncer_recv(SendRecvHelper& rpc, int src) {
    rpc.recv_array(src, (char*) ptr_, size_);
  }

  Update* copy() {
    return new PODUpdate(malloc(size_), size_);
  }
};

class ArrayUpdate: public Update {
private:
  void* ptr_;
  size_t num_elems_;
  size_t elem_size_;
  bool shardable_;

  ArrayUpdate(void* v, int num_elems, int elem_size, bool shardable) :
      ptr_(v), num_elems_(num_elems), elem_size_(elem_size), shardable_(shardable) {
    ASSERT_GT(num_elems_, 0u);
  }

public:
  template<class T>
  ArrayUpdate(const T* v, int num_elems, bool shardable) :
      ptr_((void*) v), num_elems_(num_elems), elem_size_(sizeof(T)), shardable_(shardable) {
    ASSERT_GT(num_elems_, 0u);
  }

  void* ptr() {
    return ptr_;
  }

  void worker_send(SendRecvHelper& rpc) {
    if (shardable_) {
      rpc.send_sharded((char*) ptr_, elem_size_, num_elems_);
    } else {
      rpc.send_all((char*) ptr_, elem_size_ * num_elems_);
    }
  }

  void worker_recv(SendRecvHelper& rpc) {

  }

  void syncer_recv(SendRecvHelper& rpc, int src) {
    if (shardable_) {
      ShardCalc calc(num_elems_, elem_size_, rpc.num_workers());
      rpc.recv_array(src, (char*) ptr_ + calc.start_byte(src), calc.num_bytes(src));
    } else {
      rpc.recv_array(src, (char*) ptr_, num_elems_ * elem_size_);
    }
  }

  void syncer_send(SendRecvHelper& rpc, int dst) {

  }

  Update* copy() {
    return new ArrayUpdate(malloc(num_elems_ * elem_size_), num_elems_, elem_size_, shardable_);
  }
};

class UpdateFunctionBase {
public:
  virtual void read_values(SendRecvHelper& rpc, int src) {
  }

  virtual void operator()(UpdateMap& a, UpdateMap& b) = 0;

  virtual int id() {
    return 0;
  }
};

template<void (*Fn)(UpdateMap&, UpdateMap&)>
class UpdateFunction0: public UpdateFunctionBase {
private:
public:
  virtual void read_values(SendRecvHelper& rpc, int src) {
  }

  virtual void operator()(UpdateMap& a, UpdateMap& b) {
    Fn(a, b);
  }
};

class UpdateFunctionCreator {
public:
  virtual UpdateFunctionBase* create() = 0;
};

static inline void NoOp(UpdateMap& inc, UpdateMap& global) {}

class Synchromesh {
private:
  UpdateMap tmp_;
  UpdateMap global_;
  UpdateMap local_;

  static int fn_max_;
  static UpdateFunctionCreator* fn_map_[16];

  template<class T>
  class RegisterHelper: public UpdateFunctionCreator {
  private:
    int id_;
  public:
    RegisterHelper() {
      id_ = fn_max_++;
      fn_map_[id_] = this;
    }

    UpdateFunctionBase* create() {
      return new T();
    }

    int id() {
      return id_;
    }
  };

  boost::thread* update_worker_;
  RPC* network_;

  bool stop_recv_loop_;
  bool initialized_;

  void wait_for_initialization();
  void worker_send_update(SendRecvHelper& rpc, int update_fn_id);
  void worker_recv_state(SendRecvHelper& rpc);

  void syncer_recv_update(SendRecvHelper& rpc, int source);
  void syncer_send_state(SendRecvHelper& rpc, int source);

  void syncer_loop();

  void do_init(int id);

public:
  Synchromesh(RPC* network);
  ~Synchromesh();

  Update* register_update(const std::string& name, Update* up);

  template<class T>
  Update* register_array(const std::string& name, T* region, size_t num_elems, bool shardable = true) {
    return register_update(name, new ArrayUpdate(region, num_elems, shardable));
  }

  template<class T>
  Update* register_pod(const std::string& name, const T* val) {
    return register_update(name, new PODUpdate(val));
  }

  template<void (*Fn)(UpdateMap&, UpdateMap&)>
  class UpdateFunction0: public UpdateFunctionBase {
  public:
    virtual void operator()(UpdateMap& up, UpdateMap& global) {
      Fn(up, global);
    }
  };

  template<void (*Fn)(UpdateMap&, UpdateMap&)>
  void update() {
    static RegisterHelper<UpdateFunction0<Fn> > register_me;
    {
      SendRecvHelper send(kWorkerData, *network_);
      worker_send_update(send, register_me.id());
    }

    {
      SendRecvHelper recv(kSyncerData, *network_);
      worker_recv_state(recv);
    }
  }

  template<void (*Fn)(UpdateMap&, UpdateMap&)>
  void init() {
    static RegisterHelper<UpdateFunction0<Fn> > register_me;
    do_init(register_me.id());
  }

#include "update_gen.h"

};

} // namespace update

#endif /* SYNCHROMESH_SYNC_H */
