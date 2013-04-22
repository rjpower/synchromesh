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
  kUpdateInitialize = 1000,
  kUpdateSendStart = 1001,
  kWorkerData = 1100,
  kSyncerData = 1200
};

// Helper for performing symmetric send/receives.
// Increments 'tag' for each RPC call.
class SendRecvHelper {
  RPC& rpc_;
  int tag_;

private:

public:
  SendRecvHelper(int tag, RPC& rpc) :
      rpc_(rpc), tag_(tag) {
  }

  int num_workers() const {
    return rpc_.num_workers();
  }

  template<class T>
  void send_all(const T& v) {
    rpc_.send_all(tag_++, v);
  }

  template<class T>
  void send_all(const T* ptr, int num_elems) {
    rpc_.send_all(tag_++, ptr, num_elems);
  }

  template<class T>
  void send_sharded(const T* v, int num_elems) {
    rpc_.send_sharded(tag_++, v, num_elems);
  }

  void send_sharded(const char* v, int elem_size, int num_elems) {
    rpc_.send_sharded(tag_++, v, elem_size, num_elems);
  }

  template<class T>
  void recv_pod(int src, T* v) {
    rpc_.recv_pod(src, tag_++, v);
  }

  template<class T>
  T recv_pod(int src) {
    T v;
    rpc_.recv_pod(src, tag_++, v);
    return v;
  }

  template<class T>
  void recv_array(int src, T* v, int num_elems) {
    rpc_.recv_array(src, tag_++, v, num_elems);
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
  virtual void worker_send(SendRecvHelper&) = 0;
  virtual void worker_recv(SendRecvHelper&) = 0;

  virtual void syncer_recv(SendRecvHelper&, int src) = 0;
  virtual void syncer_send(SendRecvHelper&, int src) = 0;

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
      rpc.recv_array(src, (char*) ptr_ + calc.start(src), calc.size(src));
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

class Synchromesh {
private:
  UpdateMap tmp_;
  UpdateMap global_;
  UpdateMap local_;

  typedef std::map<int, UpdateFunctionCreator*> UpdateFunctionMap;
  static UpdateFunctionMap fn_map_;

  template<class T>
  class RegisterHelper: public UpdateFunctionCreator {
  private:
    int id_;
  public:
    RegisterHelper() {
      id_ = fn_map_.size();
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

// Initially true; reset after the first call to update.
//
// After initialization no more register_* methods can be called.  This
// forces a synchronization point amongst all of the workers, ensuring
// all register_* calls have completed before any sync/updates begin.
  bool first_update_;

  void wait_for_initialization();
  void worker_send_update(SendRecvHelper& rpc, int update_fn_id);
  void worker_recv_state(SendRecvHelper& rpc);

  void syncer_recv_update(SendRecvHelper& rpc, int source);
  void syncer_send_state(SendRecvHelper& rpc, int source);

  void syncer_loop();

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

#include "update_gen.h"

};

} // namespace update

#endif /* SYNCHROMESH_SYNC_H */
