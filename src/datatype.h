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
  Comm(RPC* rpc, const Endpoint& ep) : ep_(ep), rpc_(rpc) {}
  virtual Request* send_pod(const void* v, size_t len) = 0;
  virtual Request* send_array(const void* v, size_t nelems, size_t elem_size) {
    return send_pod(v, nelems * elem_size);
  }

  virtual void recv_pod(void* v, size_t len) = 0;
  virtual void recv_array(void* v, size_t nelems, size_t elem_size) {
    return recv_pod(v, nelems * elem_size);
  }
};


class AllComm: public Comm {
public:
  AllComm(RPC* rpc, const Endpoint& ep) : Comm(rpc, ep) {}

  virtual Request* send_pod(const void* v, size_t len) {
    RequestGroup *rg = new RequestGroup();
    for (auto d : ep_) { rg->add(rpc_->send_data(d, ep_.tag(), v, len)); }
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
  AnyComm(RPC* rpc, const Endpoint& ep) : Comm(rpc, ep) {}
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
  OneComm(RPC* rpc, const Endpoint& ep, int dst) : Comm(rpc, ep), dst_(dst) {}
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
  ShardedComm(RPC* rpc, const Endpoint& ep) : Comm(rpc, ep) {}
  virtual Request* send_pod(const void* v, size_t len) {
    RequestGroup *rg = new RequestGroup();
    for (auto d : ep_) {
      rg->add(rpc_->send_data(d, ep_.tag(), v, len));
    }
    return rg;
  }

  virtual void recv_pod(void* v, size_t len) {
    PANIC("Not implemented.");
    // rpc_->recv_data(dst_, ep_.tag(), v, len);
  }

  virtual Request* send_array(const void* v, size_t nelems, size_t elem_size) {
    RequestGroup* rg = new RequestGroup;
    ShardCalc sc(nelems, elem_size, ep_.count());
    const char* cv = (char*)v;
    for (int i = 0; i < ep_.count(); ++i) {
      int dst = *(ep_.begin() + i);
      rg->add(rpc_->send_data(dst, ep_.tag(), cv + sc.start_byte(i), sc.num_elems(i)));
    }
    return rg;
  }

  virtual void recv_array(void* v, size_t nelems, size_t elem_size) {
    ShardCalc sc(nelems, elem_size, ep_.count());
    char* cv = (char*)v;
    for (int i = 0; i < ep_.count(); ++i) {
      int src = *(ep_.begin() + i);
      rpc_->recv_data(src, ep_.tag(), cv + sc.start_byte(i), sc.num_elems(i));
    }
  }
};

template <class T>
T recv(Comm* comm) {
  T v;
  recv(comm, v);
  return v;
}

template <class T>
void send(Comm* comm, const T& v,
          typename boost::enable_if<boost::is_pod<T> >::type* = 0) {
  comm->send_pod(&v, sizeof(v));
}

template <class T>
void recv(Comm* comm, T& v,
          typename boost::enable_if<boost::is_pod<T> >::type* = 0) {
  comm->recv_pod(&v, sizeof(v));
}

template <class V>
void send(Comm* comm, const std::vector<V>& v) {
  send(comm, v.size());
  if (boost::is_pod<V>::value) {
    comm->send_pod(v.data(), v.size() * sizeof(V));
  } else {
    for (auto i : v) {
      send(comm, i);
    }
  }
}

template <class V>
void recv(Comm* comm, std::vector<V>& v) {
  size_t sz = recv<size_t>(comm);
  v.resize(sz);
  if (boost::is_pod<V>::value) {
    comm->recv_pod(&v[0], v.size() * sizeof(V));
  } else {
    for (auto i : v) {
      recv(comm, i);
    }
  }
}

// Like a vector, but should be sharded.
// TODO - different behavior for shardcomm vs others?
template <class V>
class Sharded : public std::vector<V> {
};

template <class V>
void send(Comm* comm, const Sharded<V>& v) {
  if (!boost::is_pod<V>::value) {
    PANIC("Sharding non-pod types not supported.");
  }
  send(comm, v.size());
  comm->send_array(v.data(), v.size(), sizeof(V));
}

template <class V>
void recv(Comm* comm, Sharded<V>& v) {
  size_t sz = recv<size_t>(comm);
  v.resize(sz);
  if (!boost::is_pod<V>::value) {
    PANIC("Sharding non-pod types not supported.");
  }
  comm->recv_array(v.data(), v.size(), sizeof(V));
}

template <class K, class V>
void send(Comm* comm, const std::map<K, V>& v) {
  send(comm, v.size());
  for (auto i : v) {
    send(comm, i.first);
    send(comm, i.second);
  }
}

template <class K, class V>
void recv(Comm* comm, std::map<K, V>& m) {
  size_t sz = recv<size_t>(comm);
  for (size_t i = 0; i < sz; ++i) {
    K k;
    recv(comm, k);
    recv(comm, m[k]);
  }
}


} // namespace synchromesh
#endif /* SYNC_DATATYPE_H */
