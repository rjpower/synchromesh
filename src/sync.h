#ifndef SYNC_H_
#define SYNC_H_

#include <string>
#include <map>
#include <vector>

#include <stddef.h>

#include <boost/noncopyable.hpp>

#include "mpirpc.h"

namespace synchromesh {

class Update;

typedef std::map<std::string, Update*> UpdateMap;
typedef void (*UpdateFunction)(const UpdateMap& update, const UpdateMap& global);

void initialize();
void register_update(const std::string& name, Update* up);

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
  virtual void send(MPIRPC&, int& idx) = 0;
  virtual void recv(MPIRPC& rpc, int src, int& idx) = 0;

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

  void send(MPIRPC& rpc, int& idx) {
    rpc.send_all(idx++, ptr_, size_);
  }

  void recv(MPIRPC& rpc, int src, int& idx) {
    rpc.recv_data(src, idx++, (char*) ptr_, size_);
  }

  Update* copy() {
    return new PODUpdate(ptr_, size_);
  }
};

template<class T>
void register_pod(const std::string& name, const T* val) {
  register_update(name, new PODUpdate(val));
}

class ArrayUpdate: public Update {
private:
  void* ptr_;
  size_t num_elems_;
  size_t elem_size_;

  ArrayUpdate(void* v, int num_elems, int elem_size) :
      ptr_(v), num_elems_(num_elems), elem_size_(elem_size) {
    ASSERT_GT(num_elems_, 0u);
  }

public:
  template<class T>
  ArrayUpdate(const T* v, int num_elems) :
      ptr_((void*) v), num_elems_(num_elems), elem_size_(sizeof(T)) {
    ASSERT_GT(num_elems_, 0u);
  }

  void* ptr() {
    return ptr_;
  }

  void send(MPIRPC& rpc, int& idx) {
    rpc.send_sharded(idx++, (char*) ptr_, elem_size_, num_elems_);
  }

  void recv(MPIRPC& rpc, int src, int& idx) {
    ShardCalc calc(num_elems_, elem_size_);
    rpc.recv_data(src, idx++, (char*) ptr_ + calc.start(src), calc.size(src));
  }

  Update* copy() {
    return new ArrayUpdate(ptr_, num_elems_, elem_size_);
  }
};

template<class T>
void register_array(const std::string& name, T* region, size_t num_elems) {
  register_update(name, new ArrayUpdate(region, num_elems));
}

namespace internal {
struct UpdateFnRegistrar {
  UpdateFnRegistrar(UpdateFunction fn);
};

void send_update(UpdateFunction fn, bool wait_for_all);
}

template<UpdateFunction Fn>
void update(bool wait_for_all) {
  static internal::UpdateFnRegistrar register_sync_fn(Fn);
  internal::send_update(Fn, wait_for_all);
}

} // namespace update

#endif /* SYNC_H_ */
