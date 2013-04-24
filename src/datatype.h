#ifndef SYNC_DATATYPE_H
#define SYNC_DATATYPE_H

#include "mpirpc.h"

namespace synchromesh {

class Data {
public:
  virtual int id() const = 0;
  virtual void send(SendRecvHelper&, int dst) = 0;
  virtual void recv(SendRecvHelper&, int src) = 0;
};

typedef Data* (*DataCreator)();

class DataRegistry {
public:
  static Data* create(int id);
  static int register_type(DataCreator);
  class Helper {
  private:
    int id_;
  public:
    Helper(DataCreator fn) {
      id_ = DataRegistry::register_type(fn);
    }

    int id() const {
      return id_;
    }
  };
};

template<class T>
class PODData: public Data {
private:
  T* ptr_;

  static DataRegistry::Helper register_me_;

public:
  PODData() :
      ptr_(new T) {
  }

  PODData(T* v) :
      ptr_(v) {
  }

  static Data* create() {
    return new PODData<T>();
  }

  int id() const {
    return register_me_.id();
  }

  void send(SendRecvHelper& rpc, int dst) {
    rpc.send_array(dst, (char*) ptr_, sizeof(T));
  }

  void recv(SendRecvHelper& rpc, int src) {
    rpc.recv_array(src, (char*) ptr_, sizeof(T));
  }
};

template<class T>
DataRegistry::Helper PODData<T>::register_me_(&PODData<T>::create);

template<class T>
class ArrayData: public Data {
private:
  T* ptr_;
  size_t num_elems_;
  bool shardable_;
  static DataRegistry::Helper register_me_;
public:
  ArrayData() :
      ptr_(NULL), num_elems_(-1), shardable_(false) {

  }

  ArrayData(T* v, int num_elems, bool shardable) :
      ptr_(v), num_elems_(num_elems), shardable_(shardable) {
    ASSERT_GT(num_elems_, 0u);
  }

  static Data* create() {
    return new ArrayData<T> ;
  }

  int id() const {
    return register_me_.id();
  }

  void send(SendRecvHelper& rpc) {
    if (shardable_) {
      rpc.send_sharded(ptr_, num_elems_);
    } else {
      rpc.send_all(ptr_, num_elems_);
    }
  }

  void recv(SendRecvHelper& rpc, int src) {
    if (shardable_) {
      ShardCalc calc(num_elems_, sizeof(T), rpc.num_workers());
      rpc.recv_array(src, (char*) ptr_ + calc.start_byte(src), calc.num_bytes(src));
    } else {
      rpc.recv_array(src, (char*) ptr_, num_elems_ * sizeof(T));
    }
  }
};

template<class T>
DataRegistry::Helper ArrayData<T>::register_me_(&ArrayData<T>::create);

} // namespace synchromesh
#endif /* SYNC_DATATYPE_H */
