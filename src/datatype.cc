#include "datatype.h"

namespace synchromesh {

int DataRegistry::creator_counter_ = 1000;
std::map<int, DataRegistry::CreatorFn> DataRegistry::creators_;

int DataRegistry::register_type(CreatorFn fn) {
  int id = creator_counter_++;
  creators_.insert(std::make_pair(id, fn));
  return id;
}

class AllStrategy : public CommStrategy {
  virtual Request* send(RPC* rpc, const ProcessGroup& g, int tag) {
    RequestGroup *rg = new RequestGroup();
    for (auto proc : g) {
      rg->add(m_->send(rpc, proc, tag));
    }
    return rg;
  }

  virtual void recv(RPC* rpc, const ProcessGroup& g, int tag) {
    // Recv should either:
    //  read into a vector or perform a user reduction.
    PANIC("Not implemented yet.");
  }
};

class AnyStrategy : public CommStrategy {
  virtual Request* send(RPC* rpc, const ProcessGroup& g, int tag) {
    PANIC("Not implemented.  Who do you want to send to?");
    return NULL;
  }

  virtual void recv(RPC* rpc, const ProcessGroup& g, int tag) {
    while (true) {
      for (auto proc : g) {
        if (rpc->poll(proc, tag)) {
         m_->recv(rpc, proc, tag);
         return;
        }
      }
    }
  }
};

class OneStrategy : public CommStrategy {
  virtual Request* send(RPC* rpc, int dest, int tag) {
    return this->m_->send(rpc, dest, tag);
  }

  virtual void recv(RPC* rpc, int src, int tag) {
    this->m_->recv(rpc, src, tag);
  }
};


class ShardedStrategy : public CommStrategy {
  virtual Request* send(RPC* rpc, const ProcessGroup& g, int tag) {
    RequestGroup *rg = new RequestGroup();
    for (auto proc : g) {
      rg->add(m_->send(rpc, proc, tag));
    }
    return rg;
  }

  virtual void recv(RPC* rpc, const ProcessGroup& g, int tag) {
    // FIXME how can I get shard information
  }
};

} // namespace synchromesh
