#ifndef SYNC_DATATYPE_H
#define SYNC_DATATYPE_H

#include "util.h"
#include "rpc.h"

// Marshalling implementations for common datatypes:
//
// POD (struct, int, float, ...)
// Fixed size arrays
// Vectors (resizable)
// Dictionaries
namespace synchromesh {

class DataRegistry {
public:
  typedef Marshalled* (*CreatorFn)();

  static Marshalled* create(int id);
  static int register_type(CreatorFn);
};

template<class T>
class RegHelper {
private:
  static int id_;
public:
  static int id() {
    return id_;
  }

  static Marshalled* create() {
    return new T;
  }
};

template<class T>
int RegHelper<T>::id_ = DataRegistry::register_type(&RegHelper<T>::create);

template<class T>
class PODData: public Marshalled {
private:
  T* ptr_;
public:
  int id() const {
    return RegHelper<PODData<T> >::id();
  }

  PODData() :
      ptr_(new T) {
  }

  PODData(T* v) :
      ptr_(v) {
  }

  Request* send_internal(RPC* rpc, int dst, int tag) {
    return rpc->send_data(dst, tag, ptr_, sizeof(T));
  }

  Request* recv_internal(RPC* rpc, int src, int tag) {
    return rpc->recv_data(src, tag, ptr_, sizeof(T));
  }
};

template<class T>
static inline Marshalled* pod(T* v) {
  return new PODData<T>(v);
}

template<class T>
class ArrayData: public Marshalled {
private:
  T* ptr_;
  size_t num_elems_;
  bool shardable_;
public:
  ArrayData() :
      ptr_(NULL), num_elems_(-1), shardable_(false) {

  }

  ArrayData(T* v, int num_elems, bool shardable) :
      ptr_(v), num_elems_(num_elems), shardable_(shardable) {
    ASSERT_GT(num_elems_, 0u);
  }

  static Marshalled* create() {
    return new ArrayData<T> ;
  }

  int id() const {
    return RegHelper<ArrayData<T> >::id();
  }

  Request* send_internal(RPC* rpc, int dst, int tag) {
    RequestGroup *g = new RequestGroup;
    g->add(send_pod(rpc, dst, tag, num_elems_));
    g->add(rpc->send_data(dst, tag, ptr_, sizeof(T) * num_elems_));
    return g;
  }

  Request* recv_internal(RPC* rpc, int src, int tag) {
    size_t count;
    Request* r = recv_pod(rpc, src, tag, &count);

    // TODO(rjp) -- use callbacks to do this properly?
    r->wait();

    ASSERT_EQ(count, num_elems_);
    return rpc->recv_data(src, tag, ptr_, tag);
  }
};

template<class T>
static inline Marshalled* array(T* v, size_t len) {
  return new ArrayData<T>(v, len);
}

template<class T>
class VectorData: public Marshalled {
private:
  std::vector<T>* data_;
  bool own_;
public:
  VectorData() :
      data_(new std::vector<T>), own_(true) {

  }

  VectorData(std::vector<T>* d) :
      data_(d), own_(false) {

  }

  virtual ~VectorData() {
    delete data_;
  }

  int id() const {
    return RegHelper<VectorData<T> >::id();
  }

  Request* send_internal(RPC* rpc, int dst, int tag) const {
    RequestGroup *g = new RequestGroup();
    g->add(send_pod(rpc, dst, tag, data_->size()));
    g->add(rpc->send_data(dst, tag, data_->data(), data_->size() * sizeof(T)));
    return g;
  }

  Request* recv_internal(RPC* rpc, int src, int tag) {
    size_t len;
    Request* r = recv_pod(rpc, src, tag, &len);
    r->wait();
    data_->resize(len);
    return rpc->recv_data(src, tag, &data_[0], data_->size());
  }
};

} // namespace synchromesh
#endif /* SYNC_DATATYPE_H */
