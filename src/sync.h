#ifndef SYNC_H_
#define SYNC_H_

#include <string>
#include <map>
#include <vector>

#include <stddef.h>

#include <boost/noncopyable.hpp>

namespace update {

struct Update: private boost::noncopyable {
  template<class T>
  T* as_array() {
    return (T*) ptr();
  }

  template<class T>
  const T& as() {
    return *((const T*) ptr());
  }

  virtual bool shardable() {
    return false;
  }

  virtual size_t element_size() {
    return -1;
  }

  virtual void* ptr() = 0;
  virtual size_t size() = 0;
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

  size_t size() {
    return size_;
  }

  Update* copy() {
    return new PODUpdate(ptr_, size_);
  }
};

struct ArrayUpdate: public Update {
private:
  void* ptr_;
  size_t size_;
  size_t elem_size_;

  ArrayUpdate(void* v, int size, int elem_size) :
      ptr_(v), size_(size), elem_size_(elem_size) {
  }

public:
  template<class T>
  ArrayUpdate(const T* v, int num_elems) :
      ptr_((void*) v), size_(sizeof(T) * num_elems), elem_size_(sizeof(T)) {
  }

  size_t size() {
    return size_;
  }

  void* ptr() {
    return ptr_;
  }

  Update* copy() {
    return new ArrayUpdate(ptr_, size_, elem_size_);
  }

  bool shardable() {
    return true;
  }

  size_t element_size() {
    return elem_size_;
  }
};

typedef std::map<std::string, Update*> UpdateMap;
typedef void (*UpdateFunction)(const UpdateMap& update, const UpdateMap& global);

namespace internal {
struct RegisterHelper {
  RegisterHelper(UpdateFunction fn);
};

void send_update(UpdateFunction fn, bool wait_for_all);
void register_data(const std::string& name, Update*);
}

void register_update(const std::string& name, Update* up);

template<class T>
void register_array(const std::string& name, T* region, size_t size) {
  register_update(name, new ArrayUpdate(region, size));
}

template<class T>
void register_struct(const std::string& name, const T* val) {
  register_update(name, new PODUpdate(val));
}

template<UpdateFunction Fn>
void update(bool wait_for_all) {
  static internal::RegisterHelper register_sync_fn(Fn);
  internal::send_update(Fn, wait_for_all);
}

} // namespace update

#endif /* SYNC_H_ */
