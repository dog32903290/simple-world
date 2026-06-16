// runtime/value_op_registry — the two sinks + the ValueOp registrar ctor.
// Mirror of point_ops_image_filter_registry.cpp, minus all GPU/asset/compute/size/mip machinery
// (a value op has no cook). See value_op_registry.h for the full self-registration contract.
#include "runtime/value_op_registry.h"

#include <utility>

namespace sw {

// Meyers singletons: function-local statics, constructed on first call (i.e. inside the first
// ValueOp registrar ctor that runs during pre-main dynamic init). Safe across TUs — see header.
std::vector<NodeSpec>& valueOpSpecSink() {
  static std::vector<NodeSpec> sink;
  return sink;
}

std::vector<std::pair<const char*, int (*)(bool)>>& valueOpSelfTests() {
  static std::vector<std::pair<const char*, int (*)(bool)>> tests;
  return tests;
}

ValueOp::ValueOp(NodeSpec spec, const char* selftestName, int (*selftest)(bool)) {
  valueOpSpecSink().push_back(std::move(spec));
  if (selftestName && selftest) valueOpSelfTests().push_back({selftestName, selftest});
}

}  // namespace sw
