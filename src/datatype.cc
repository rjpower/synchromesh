#include "datatype.h"

using std::vector;
using std::map;

namespace synchromesh {

int DataRegistry::creator_counter_ = 1000;
DataRegistry::Map* DataRegistry::creators_ = NULL;

int DataRegistry::register_type(CreatorFn fn) {
  if (creators_ == NULL) {
    creators_ = new DataRegistry::Map();
  }
  int id = creator_counter_++;
  creators_->insert(std::make_pair(id, fn));
  return id;
}

class AllStrategy: public CommStrategy {
public:
  AllStrategy(Marshalled* m) :
      CommStrategy(m) {
  }
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

class AnyStrategy: public CommStrategy {
public:
  AnyStrategy(Marshalled* m) :
      CommStrategy(m) {
  }
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

class OneStrategy: public CommStrategy {
private:
  int dst_;
public:
  OneStrategy(Marshalled* m, int dst) :
      CommStrategy(m), dst_(dst) {
  }
  virtual Request* send(RPC* rpc, const ProcessGroup& g, int tag) {
    return this->m_->send(rpc, dst_, tag);
  }

  virtual void recv(RPC* rpc, const ProcessGroup& g, int tag) {
    this->m_->recv(rpc, dst_, tag);
  }
};

// The 'sharded' comm strategy doesn't actually require the top level object
// to be marshallable.
class ShardedStrategy: public CommStrategy {
public:
  ShardedStrategy(Shardable* m) :
      CommStrategy(reinterpret_cast<Marshalled*>(m)) {
  }

  virtual Request* send(RPC* rpc, const ProcessGroup& g, int tag) {
    vector<Marshalled::Ptr> slices = reinterpret_cast<Shardable*>(m_)->slice(g.count());
    RequestGroup *rg = new RequestGroup();
    for (int i = 0; i < g.count(); ++i) {
      rg->add(slices[i]->send(rpc, (*g.begin() + i), tag));
    }

    return rg;
  }

  virtual void recv(RPC* rpc, const ProcessGroup& g, int tag) {
    vector<Marshalled::Ptr> slices = reinterpret_cast<Shardable*>(m_)->slice(g.count());
    for (int i = 0; i < g.count(); ++i) {
      slices[i]->recv(rpc, (*g.begin() + i), tag);
    }
  }
};

CommStrategy::Ptr all(Marshalled* m) {
  return CommStrategy::Ptr(new AllStrategy(m));
}

CommStrategy::Ptr any(Marshalled* m) {
  return CommStrategy::Ptr(new AnyStrategy(m));
}

CommStrategy::Ptr one(Marshalled* m, int tgt) {
  return CommStrategy::Ptr(new OneStrategy(m, tgt));
}

CommStrategy::Ptr sharded(Shardable* m) {
  return CommStrategy::Ptr(new ShardedStrategy(m));
}

} // namespace synchromesh
