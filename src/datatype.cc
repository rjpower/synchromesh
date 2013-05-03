#include "datatype.h"

using std::vector;
using std::map;

namespace synchromesh {

ShardCalc::ShardCalc(int num_elements, int elem_size, int num_workers) :
    num_workers_(num_workers), num_elements_(num_elements), elem_size_(elem_size) {
}

size_t ShardCalc::start_elem(int worker) {
  ASSERT_LT(worker, num_workers_);
  int64_t elems_per_server = num_elements_ / num_workers_;
  int64_t offset = worker * elems_per_server;
  if (offset > num_elements_) {
    offset = num_elements_;
  }
  return offset;
}

size_t ShardCalc::start_byte(int worker) {
  return start_elem(worker) * elem_size_;
}

size_t ShardCalc::end_elem(int worker) {
  ASSERT_LT(worker, num_workers_);
  int64_t elems_per_server = num_elements_ / num_workers_;
  int64_t offset = (worker + 1) * elems_per_server;
  if (offset > num_elements_ || worker == num_workers_ - 1) {
    offset = num_elements_;
  }
  return offset;
}

size_t ShardCalc::end_byte(int worker) {
  return end_elem(worker) * elem_size_;
}

size_t ShardCalc::num_bytes(int worker) {
  return end_byte(worker) - start_byte(worker);
}

size_t ShardCalc::num_elems(int worker) {
  return end_elem(worker) - start_elem(worker);
}


Request* ShardedComm::send_array(const ArrayLike& v) {
  RequestGroup* rg = new RequestGroup;
  Log_Info("send_array: %d %d", v.count(), ep_.count());
  ShardCalc sc(v.count(), v.element_size(), ep_.count());
  const char* cv = (const char*) (v.data_ptr());
  for (int i = 0; i < ep_.count(); ++i) {
    int dst = *(ep_.begin() + i);
    // skip self.
//    if (dst == rpc_->id()) {
//      continue;
//    }

    size_t sz = sc.num_elems(i);
    Log_Debug("Sending %d entries to %d", sz, dst);
    rg->add(rpc_->send_data(dst, ep_.tag(), &sz, sizeof(size_t)));
    rg->add(rpc_->send_data(dst, ep_.tag(), cv + sc.start_byte(i), sc.num_bytes(i)));
  }
  return rg;
}

void ShardedComm::recv_array(ArrayLike& v) {
  size_t pos = 0;
  for (int i = 0; i < ep_.count(); ++i) {
    int src = *(ep_.begin() + i);

    // skip self.
//    if (src == rpc_->id()) {
//      continue;
//    }

    size_t sz;
    rpc_->recv_data(src, ep_.tag(), &sz, sizeof(size_t));
    v.resize(pos + sz);
    char* cv = (char*) v.data_ptr();
    cv = cv + pos * v.element_size();
    rpc_->recv_data(src, ep_.tag(), cv, sz * v.element_size());
    Log_Info("%d: %d entries from %d; %d -> %d", rpc_->id(), sz, src, pos, pos + sz);
    pos += sz;
  }
}

void ShardedComm::recv_pod(void* v, size_t len) {
  PANIC("Not implemented.");
  // rpc_->recv_data(dst_, ep_.tag(), v, len);
}


Request* AllComm::send_pod(const void* v, size_t len) {
  RequestGroup* rg = new RequestGroup();
  for (auto d : ep_) {
    Log_Info("%d: sending %d bytes to %d", rpc_->id(), len, d);
    rg->add(rpc_->send_data(d, ep_.tag(), v, len));
  }
  return rg;
}

void AllComm::recv_pod(void* v, size_t len) {
  // Recv should either:
  //  read into a vector or perform a user reduction.
  PANIC("Not implemented yet.");
}

void AnyComm::recv_pod(void* v, size_t len) {
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



} // namespace synchromesh
