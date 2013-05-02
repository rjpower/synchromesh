#include "datatype.h"

using std::vector;
using std::map;

namespace synchromesh {

ShardCalc::ShardCalc(int num_elements, int elem_size, int num_workers) :
    num_workers_(num_workers), num_elements_(num_elements), elem_size_(elem_size) {
}

size_t ShardCalc::start_elem(int worker) {
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

} // namespace synchromesh
