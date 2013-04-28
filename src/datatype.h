#ifndef SYNC_DATATYPE_H
#define SYNC_DATATYPE_H

#include <map>

#include "util.h"
#include "rpc.h"

// Marshalling implementations for common datatypes:
//
// POD (struct, int, float, ...)
// Fixed size arrays
// Vectors (resizable)
// Dictionaries
namespace synchromesh {

class Marshalled {
public:
  virtual ~Marshalled() {
  }
  virtual int id() const = 0;

  virtual Request* send(RPC* rpc, int dst, int tag) const = 0;
  virtual void recv(RPC* rpc, int src, int tag) = 0;
};

class DataRegistry {
public:
  typedef Marshalled* (*CreatorFn)();
  static inline Marshalled* create(int id) {
    return creators_[id]();
  }
  static int register_type(CreatorFn);
private:
  static int creator_counter_;
  static std::map<int, CreatorFn> creators_;
};

static inline Request* send(RPC* rpc, int dst, int tag, const Marshalled& v) {
  send_pod(rpc, dst, tag, v.id());
  return v.send(rpc, dst, tag);
}

static inline void recv(RPC* rpc, int src, int tag, Marshalled* v) {
  int id = recv_pod<int>(rpc, src, tag);
  ASSERT_EQ(id, v->id());
  v->recv(rpc, src, tag);
}

static inline Marshalled* recv(RPC* rpc, int src, int tag) {
  int id = recv_pod<int>(rpc, src, tag);
  Marshalled* m = DataRegistry::create(id);
  m->recv(rpc, src, tag);
  return m;
}

class Shardable {
public:
  virtual ~Shardable() {
  }
  virtual void* data() = 0;
  virtual size_t element_size() = 0;
  virtual size_t len() = 0;
};

class CommStrategy {
protected:
  Marshalled* m_;
public:
  virtual ~CommStrategy() {
  }

  // Should this return a Request* or block?
  //
  //
  // User code:???
  // auto r = send(msg1);
  // recv(msg2);
  // r.wait();
  //
  virtual Request* send(RPC* rpc, const ProcessGroup& g, int tag) = 0;
  virtual void recv(RPC* rpc, const ProcessGroup& g, int tag) = 0;
};

CommStrategy* any(Marshalled*);
CommStrategy* all(Marshalled*);
CommStrategy* sharded(Shardable*);
CommStrategy* one(Marshalled*, int tgt);

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

  Request* send(RPC* rpc, int dst, int tag) {
    return rpc->send_data(dst, tag, ptr_, sizeof(T));
  }

  void recv(RPC* rpc, int src, int tag) {
    rpc->recv_data(src, tag, ptr_, sizeof(T));
  }
};

template<class T>
static inline PODData<T>* pod(T* v) {
  return new PODData<T>(v);
}

template<class T>
class Slice: public Marshalled, public Shardable {
private:
  T* ptr_;
  size_t num_elems_;
  bool shardable_;
public:
  Slice() :
      ptr_(NULL), num_elems_(-1), shardable_(false) {

  }

  Slice(T* v, int num_elems, bool shardable) :
      ptr_(v), num_elems_(num_elems), shardable_(shardable) {
    ASSERT_GT(num_elems_, 0u);
  }

  static Marshalled* create() {
    return new Slice<T> ;
  }

  int id() const {
    return RegHelper<Slice<T> >::id();
  }

  Request* send(RPC* rpc, int dst, int tag) {
    RequestGroup *g = new RequestGroup;
    g->add(send_pod(rpc, dst, tag, num_elems_));
    g->add(rpc->send_data(dst, tag, ptr_, sizeof(T) * num_elems_));
    return g;
  }

  void recv(RPC* rpc, int src, int tag) {
    size_t count = recv_pod<size_t>(rpc, src, tag);
    ASSERT_EQ(count, num_elems_);
    rpc->recv_data(src, tag, ptr_, count * num_elems_);
  }
};

template<class T>
static inline Slice<T>* array(T* v, size_t len) {
  return new Slice<T>(v, len);
}

template<class T>
class VectorData: public Marshalled, public Shardable {
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

  Request* send(RPC* rpc, int dst, int tag) const {
    RequestGroup *g = new RequestGroup();
    g->add(send_pod(rpc, dst, tag, data_->size()));
    g->add(rpc->send_data(dst, tag, data_->data(), data_->size() * sizeof(T)));
    return g;
  }

  void recv(RPC* rpc, int src, int tag) {
    size_t len = recv_pod<size_t>(rpc, src, tag);
    data_->resize(len);
    rpc->recv_data(src, tag, &data_[0], data_->size());
  }
};

class SeqMarshalled : public Marshalled {
private:
  std::vector<Marshalled*> data_;
public:
  SeqMarshalled() {
    PANIC("Not implemented.");
  }

  SeqMarshalled(std::vector<Marshalled*>& v) :
      data_(v) {

  }
  int id() const {
    return RegHelper<SeqMarshalled>::id();
  }

  Request* send(RPC* rpc, int dst, int tag) const {
    RequestGroup *g = new RequestGroup();
    g->add(send_pod(rpc, dst, tag, data_.size()));

    for (auto m : data_) {
      g->add(m->send(rpc, dst, tag));
    }
    return g;
  }

  void recv(RPC* rpc, int src, int tag) {
    size_t len = recv_pod<size_t>(rpc, src, tag);
    ASSERT_EQ(len, data_.size());
    for (auto m : data_) {
      int id = recv_pod<int>(rpc, src, tag);
      ASSERT_EQ(id, m->id());
      m->recv(rpc, src, tag);
    }
  }
};

static inline SeqMarshalled* seq(Marshalled* a) {
  std::vector<Marshalled*> v;
  v.push_back(a);
  return new SeqMarshalled(v);
}

} // namespace synchromesh
#endif /* SYNC_DATATYPE_H */
