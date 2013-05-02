#ifndef SYNC_DATATYPE_H
#define SYNC_DATATYPE_H

#include <map>
#include <vector>
#include <boost/shared_ptr.hpp>

#include "util.h"
#include "rpc.h"

// Marshalling implementations for common datatypes:
//
// POD (struct, int, float, ...)
// Fixed size arrays
// std::vector (resizable)
// std::map
namespace synchromesh {

class Endpoint {
private:
  std::vector<int> v_;
  int tag_;
public:
  Endpoint(int f, int l, int tag) {
    for (int i = f; i <= l; ++i) {
      v_.push_back(i);
    }

    tag_ = tag;
  }

  int tag() const {
    return tag_;
  }

  int count() const {
    return v_.size();
  }

  std::vector<int>::const_iterator begin() const {
    return v_.begin();
  }

  std::vector<int>::const_iterator end() const {
    return v_.end();
  }
};

class ArrayLike {
public:
  virtual const void* data_ptr() const = 0;
  virtual void* data_ptr() = 0;

  virtual void resize(size_t) = 0;
  virtual size_t element_size() const = 0;
  virtual size_t count() const= 0;
};

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

class Marshalled;

class Comm {
protected:
  Endpoint ep_;
  RPC* rpc_;
public:
  Comm(RPC* rpc, const Endpoint& ep) :
      ep_(ep), rpc_(rpc) {
  }
  virtual Request* send_pod(const void* v, size_t len) = 0;
  virtual Request* send_array(const ArrayLike& v) {
    RequestGroup* rg = new RequestGroup;
    size_t count = v.count();
    rg->add(send_pod(&count, sizeof(count)));
    rg->add(send_pod(v.data_ptr(), v.element_size() * v.count()));
    return rg;
  }

  virtual void recv_pod(void* v, size_t len) = 0;
  virtual void recv_array(ArrayLike& v) {
    size_t count;
    recv_pod(&count, sizeof(count));
    v.resize(count);
    return recv_pod(v.data_ptr(), v.element_size() * v.count());
  }
};

class AllComm: public Comm {
public:
  AllComm(RPC* rpc, const Endpoint& ep) :
      Comm(rpc, ep) {
  }

  virtual Request* send_pod(const void* v, size_t len) {
    RequestGroup *rg = new RequestGroup();
    for (auto d : ep_) {
      if (d == rpc_->id()) {
        continue;
      }
      rg->add(rpc_->send_data(d, ep_.tag(), v, len));
    }
    return rg;
  }

  virtual void recv_pod(void* v, size_t len) {
    // Recv should either:
    //  read into a vector or perform a user reduction.
    PANIC("Not implemented yet.");
  }
};

class AnyComm: public Comm {
private:
  int tgt_;
public:
  AnyComm(RPC* rpc, const Endpoint& ep) :
      Comm(rpc, ep), tgt_(-1) {
  }
  virtual Request* send_pod(const void* v, size_t len) {
    PANIC("Not implemented.  Who do you want to send to?");
    return NULL;
  }

  virtual void recv_pod(void* v, size_t len) {
    while (tgt_ == -1) {
      for (auto proc : ep_) {
        if (rpc_->poll(proc, ep_.tag())) {
          tgt_ = proc;
          break;
        }
      }
    }
    rpc_->recv_data(tgt_, ep_.tag(), v, len);
  }
};

class OneComm: public Comm {
private:
  int dst_;
public:
  OneComm(RPC* rpc, const Endpoint& ep, int dst) :
      Comm(rpc, ep), dst_(dst) {
  }
  virtual Request* send_pod(const void* v, size_t len) {
    return rpc_->send_data(dst_, ep_.tag(), v, len);
  }

  virtual void recv_pod(void* v, size_t len) {
    rpc_->recv_data(dst_, ep_.tag(), v, len);
  }
};

// The 'sharded' comm strategy doesn't actually require the top level object
// to be marshallable.
class ShardedComm: public Comm {
public:
  ShardedComm(RPC* rpc, const Endpoint& ep) :
      Comm(rpc, ep) {
  }
  virtual Request* send_pod(const void* v, size_t len) {
    RequestGroup *rg = new RequestGroup();
    for (auto d : ep_) {
      rg->add(rpc_->send_data(d, ep_.tag(), v, len));
    }
    return rg;
  }

  virtual void recv_pod(void* v, size_t len);

  virtual Request* send_array(const ArrayLike& v);
  virtual void recv_array(ArrayLike& v);
};

template<class T>
T recv(Comm& comm) {
  T v;
  recv(comm, v);
  return v;
}

template<class T>
Request* send(Comm& comm, const T& v, typename boost::enable_if<boost::is_pod<T> >::type* = 0) {
  return comm.send_pod(&v, sizeof(v));
}

template<class T>
void recv(Comm& comm, T& v, typename boost::enable_if<boost::is_pod<T> >::type* = 0) {
  comm.recv_pod(&v, sizeof(v));
}

template<class V>
Request* send(Comm& comm, const std::vector<V>& v) {
  RequestGroup* rg = new RequestGroup;
  rg->add(send(comm, v.size()));
  if (boost::is_pod<V>::value) {
    rg->add(comm.send_pod(v.data(), v.size() * sizeof(V)));
  } else {
    for (auto i : v) {
      rg->add(send(comm, i));
    }
  }
  return rg;
}

template<class V>
void recv(Comm& comm, std::vector<V>& v) {
  size_t sz = recv<size_t>(comm);
  v.resize(sz);
  if (boost::is_pod<V>::value) {
    comm.recv_pod(&v[0], v.size() * sizeof(V));
  } else {
    for (auto i : v) {
      recv(comm, i);
    }
  }
}

// Like a vector, but should be sharded.
template<class V>
class ShardedVector: public ArrayLike {
private:
  std::vector<V> m_;
public:
  void* data_ptr() {
    return (void*) m_.data();
  }

  const void* data_ptr() const {
    return (void*) m_.data();
  }

  const V& operator[](size_t idx) const {
    return m_[idx];
  }

  V& operator[](size_t idx) {
    return m_[idx];
  }

  void resize(size_t sz) {
    m_.resize(sz);
  }

  size_t count() const {
    return m_.size();
  }

  size_t size() const {
    return m_.size();
  }

  size_t element_size() const {
    return sizeof(V);
  }
};


template<class V>
Request* send(Comm& comm, const ShardedVector<V>& v) {
  if (!boost::is_pod<V>::value) {
    PANIC("Sharding non-pod types not supported.");
  }
  return comm.send_array(v);
}

template<class V>
void recv(Comm& comm, ShardedVector<V>& v) {
  return comm.recv_array(v);
}


// Wrap a plain C pointer + len with ArrayLike.
// Ignores resize operations.
template <class V>
class FixedArray : public ArrayLike {
private:
  V* v_;
  size_t len_;
public:
  FixedArray(V* v, size_t sz) : v_(v), len_(sz) {}
  void* data_ptr() {
    return (void*) v_;
  }

  const void* data_ptr() const {
    return (void*) v_;
  }

  const V& operator[](size_t idx) const {
    return v_[idx];
  }

  V& operator[](size_t idx) {
    return v_[idx];
  }

  void resize(size_t sz) {
    ASSERT_LE(sz, len_);
  }

  size_t count() const {
    return len_;
  }

  size_t element_size() const {
    return sizeof(V);
  }
};


template<class V>
Request* send(Comm& comm, const FixedArray<V>& v) {
  if (!boost::is_pod<V>::value) {
    PANIC("Sharding non-pod types not supported.");
  }
  return comm.send_array(v);
}

template<class V>
void recv(Comm& comm, FixedArray<V>& v) {
  return comm.recv_array(v);
}


template <class V>
Request* send(Comm& comm, const V* v, size_t len) {
  return send(comm, FixedArray<V>((V*)v, len));
}

template<class V>
void recv(Comm& comm, V* v, size_t len) {
  FixedArray<V> f(v, len);
  recv(comm, f);
}


template<class K, class V>
Request* send(Comm& comm, const std::map<K, V>& v) {
  RequestGroup* rg = new RequestGroup;
  rg->add(send(comm, v.size()));
  for (auto i : v) {
    rg->add(send(comm, i.first));
    rg->add(send(comm, i.second));
  }
  return rg;
}

template<class K, class V>
void recv(Comm& comm, std::map<K, V>& m) {
  size_t sz = recv<size_t>(comm);
  for (size_t i = 0; i < sz; ++i) {
    K k;
    recv(comm, k);
    recv(comm, m[k]);
  }
}

} // namespace synchromesh
#endif /* SYNC_DATATYPE_H */
