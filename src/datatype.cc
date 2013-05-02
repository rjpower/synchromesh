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
  ShardCalc sc(v.count(), v.element_size(), ep_.count());
  const char* cv = (const char*) (v.data_ptr());
  for (int i = 0; i < ep_.count(); ++i) {
    int dst = *(ep_.begin() + i);
    // skip self.
    if (dst == rpc_->id()) {
      continue;
    }
    size_t sz = sc.num_elems(i);
    rg->add(rpc_->send_data(dst, ep_.tag(), &sz, sizeof(size_t)));
    rg->add(rpc_->send_data(dst, ep_.tag(), cv + sc.start_byte(i), sc.num_bytes(i)));
  }
  return rg;
}

void ShardedComm::recv_array(ArrayLike& v) {
  size_t pos = 0;
  for (int i = 0; i < ep_.count(); ++i) {
    int src = *(ep_.begin() + i);

    Log_Debug("Reading... %d %d %d", i, src, rpc_->id());
    // skip self.
    if (src == rpc_->id()) {
      continue;
    }

    size_t sz;
    rpc_->recv_data(src, ep_.tag(), &sz, sizeof(size_t));
    v.resize(pos + sz);
    Log_Debug("Reading %d entries from %d (%d); %d -> %d", sz, i, src, pos, pos + sz);
    char* cv = (char*) v.data_ptr();
    cv = cv + pos * v.element_size();
    rpc_->recv_data(src, ep_.tag(), cv, sz * v.element_size());
    pos += sz;
  }
}

void ShardedComm::recv_pod(void* v, size_t len) {
  PANIC("Not implemented.");
  // rpc_->recv_data(dst_, ep_.tag(), v, len);
}


} // namespace synchromesh
