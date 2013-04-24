#ifndef SYNCHROMESH_SYNC_H
#define SYNCHROMESH_SYNC_H

#include <string>
#include <map>
#include <vector>

#include <stddef.h>
#include <boost/noncopyable.hpp>

#include "mpirpc.h"
#include "datatype.h"

namespace synchromesh {

typedef std::map<std::string, Data*> UpdateMap;

static inline void noop_update(UpdateMap& inc, UpdateMap& global) {}

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

class UpdateFunction {
public:
  virtual void read_values(SendRecvHelper& rpc, int src) {}
  virtual void operator()(UpdateMap& a, UpdateMap& b) = 0;
  virtual int id() { return 0; }
};

typedef UpdateFunction* (*UpdateFunctionCreator)();

class UpdateFunctionRegistry {
private:
  static std::map<int, UpdateFunctionCreator> reg_;
public:
  static int register_fn(UpdateFunctionCreator*);
  static UpdateFunction* create(int id);
  
  template<class T>
  class Helper {
  private:
    int id_;
  public:
    static UpdateFunction* create() { return new T(); }
    Helper() { id_ = UpdateFunctionRegistry::register_fn(&Helper<T>::create); }
    int id() { return id_; }
  };
};

template<void (*Fn)(UpdateMap&, UpdateMap&)>
class UpdateFunction0: public UpdateFunction {
private:
public:
  virtual void read_values(SendRecvHelper& rpc, int src) {
  }

  virtual void operator()(UpdateMap& a, UpdateMap& b) {
    Fn(a, b);
  }
};


class SyncServer {
private:
  UpdateMap global_;
  UpdateMap tmp_;
  boost::thread* thread_;
  RPC* network_;
  bool stop_recv_loop_;
  
  void recv_update(SendRecvHelper& rpc, int source);
  void send_state(SendRecvHelper& rpc, int source);
  void loop();

public:
  SyncServer(RPC*);
  void start();
  void stop();
};

class Synchromesh {
private:
  UpdateMap local_;
  SyncServer *server_;
  RPC* network_;
  bool initialized_;

  void wait_for_initialization();
  void worker_send_update(SendRecvHelper& rpc, int update_fn_id);
  void worker_recv_state(SendRecvHelper& rpc);
  void do_init(int id);
public:
  Synchromesh(RPC* network);
  ~Synchromesh();

  Data* register_update(const std::string& name, Data* up);

  template<class T>
  Data* register_array(const std::string& name, T* region, size_t num_elems, bool shardable = true) {
    return register_update(name, new ArrayData<T>(region, num_elems, shardable));
  }

  template<class T>
  Data* register_pod(const std::string& name, T* val) {
    return register_update(name, new PODData<T>(val));
  }

  template<void (*Fn)(UpdateMap&, UpdateMap&)>
  class UpdateFunction0: public UpdateFunction {
  public:
    virtual void operator()(UpdateMap& up, UpdateMap& global) {
      Fn(up, global);
    }
  };

  template<void (*Fn)(UpdateMap&, UpdateMap&)>
  void update() {
    static UpdateFunctionRegistry::Helper<UpdateFunction0<Fn> > register_me;
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
