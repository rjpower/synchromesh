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

typedef void (*UpdateFunction)(UpdateMap&, UpdateMap&);
typedef std::map<int, UpdateFunction> UpdateFunctionMap;

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
  virtual void worker_send(RPC&, int& idx) = 0;
  virtual void worker_recv(RPC&, int& idx) = 0;

  virtual void syncer_recv(RPC& rpc, int src, int& idx) = 0;
  virtual void syncer_send(RPC& rpc, int src, int& idx) = 0;

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

  void worker_send(RPC& rpc, int& idx) {
    rpc.send_all(idx++, (char*) ptr_, size_);
  }

  void worker_recv(RPC& rpc, int& idx) {
    // Which sync server should I read from?
    // For now, just assume server 0 is the canonical.
//    rpc.recv_data(0, idx++, (char*) ptr_, size_);
  }

  void syncer_send(RPC& rpc, int dst, int& idx) {
  }
  void syncer_recv(RPC& rpc, int src, int& idx) {
    rpc.recv_data(src, idx++, (char*) ptr_, size_);
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

  void worker_send(RPC& rpc, int& idx) {
    if (shardable_) {
      rpc.send_sharded(idx++, (char*) ptr_, elem_size_, num_elems_);
    } else {
      rpc.send_all(idx++, (char*) ptr_, elem_size_ * num_elems_);
    }
  }

  void worker_recv(RPC& rpc, int& idx) {

  }

  void syncer_recv(RPC& rpc, int src, int& idx) {
    if (shardable_) {
      ShardCalc calc(num_elems_, elem_size_, rpc.num_workers());
      rpc.recv_data(src, idx++, (char*) ptr_ + calc.start(src), calc.size(src));
    } else {
      rpc.recv_data(src, idx++, (char*) ptr_, num_elems_ * elem_size_);
    }
  }

  void syncer_send(RPC& rpc, int dst, int& idx) {

  }

  Update* copy() {
    return new ArrayUpdate(malloc(num_elems_ * elem_size_), num_elems_, elem_size_, shardable_);
  }
};

class Synchromesh {
private:
  struct UpdateFnRegistrar {
    UpdateFnRegistrar(UpdateFunction fn);
  };

  UpdateMap tmp_;
  UpdateMap global_;
  UpdateMap local_;

  static UpdateFunctionMap fn_map_;

  boost::thread* update_worker_;
  RPC* network_;

  bool stop_recv_loop_;

  // Initially true; reset after the first call to update.
  //
  // After initialization no more register_* methods can be called.  This
  // forces a synchronization point amongst all of the workers, ensuring
  // all register_* calls have completed before any sync/updates begin.
  bool first_update_;

  int get_id(UpdateFunction fn);
  void send_update(UpdateFunction fn, bool wait_for_all);
  void recv_updates();
  void recv_data(RPC& rpc, int source);

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

  template<UpdateFunction Fn>
  void update(bool wait_for_all) {
    static UpdateFnRegistrar register_sync_fn(Fn);
    send_update(Fn, wait_for_all);
  }
};

} // namespace update

#endif /* SYNCHROMESH_SYNC_H */
