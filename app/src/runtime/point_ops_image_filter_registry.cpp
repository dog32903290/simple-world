// runtime/point_ops_image_filter_registry — the two sinks + the ImageFilterOp registrar ctor.
// Named point_ops_*.cpp so the CMake glob (SW_POINT_OP_SRCS = src/runtime/point_ops*.cpp) picks it
// up with zero CMake edit. See image_filter_op_registry.h for the full self-registration contract.
#include "runtime/image_filter_op_registry.h"

#include <utility>

#include "runtime/point_graph.h"  // registerTexOp

namespace sw {

// Meyers singletons: function-local statics, constructed on first call (i.e. inside the first
// registrar ctor that runs during pre-main dynamic init). Safe across TUs — see header.
std::vector<NodeSpec>& imageFilterSpecSink() {
  static std::vector<NodeSpec> sink;
  return sink;
}

std::vector<std::pair<const char*, int (*)(bool)>>& imageFilterSelfTests() {
  static std::vector<std::pair<const char*, int (*)(bool)>> tests;
  return tests;
}

ImageFilterOp::ImageFilterOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                             const char* selftestName, int (*selftest)(bool)) {
  registerTexOp(cookType, cook);
  imageFilterSpecSink().push_back(std::move(spec));
  if (selftestName && selftest) imageFilterSelfTests().push_back({selftestName, selftest});
}

}  // namespace sw
