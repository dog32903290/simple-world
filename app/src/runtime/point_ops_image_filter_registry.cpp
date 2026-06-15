// runtime/point_ops_image_filter_registry — the two sinks + the ImageFilterOp registrar ctor.
// Named point_ops_*.cpp so the CMake glob (SW_POINT_OP_SRCS = src/runtime/point_ops*.cpp) picks it
// up with zero CMake edit. See image_filter_op_registry.h for the full self-registration contract.
#include "runtime/image_filter_op_registry.h"

#include <map>
#include <set>
#include <string>
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

// Compute-leaf sinks — same Meyers-singleton lifetime as the two above (first touched inside the
// first ImageFilterComputeOp ctor that runs during pre-main dynamic init; consumed only after main).
std::set<std::string>& imageFilterComputeTypes() {
  static std::set<std::string> types;
  return types;
}

std::map<std::string, ImageFilterSizeFn>& imageFilterSizeFns() {
  static std::map<std::string, ImageFilterSizeFn> fns;
  return fns;
}

// Mipped-output sink — same Meyers-singleton lifetime as the sets above (first touched inside the
// first registrar ctor with mippedOutput=true during pre-main dynamic init; consumed only after main).
std::set<std::string>& imageFilterMippedOutputTypes() {
  static std::set<std::string> types;
  return types;
}

ImageFilterOp::ImageFilterOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                             const char* selftestName, int (*selftest)(bool), bool mippedOutput) {
  registerTexOp(cookType, cook);
  imageFilterSpecSink().push_back(std::move(spec));
  if (mippedOutput) imageFilterMippedOutputTypes().insert(cookType);
  if (selftestName && selftest) imageFilterSelfTests().push_back({selftestName, selftest});
}

ImageFilterComputeOp::ImageFilterComputeOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                                           ImageFilterSizeFn sizeFn, const char* selftestName,
                                           int (*selftest)(bool), bool mippedOutput) {
  registerTexOp(cookType, cook);
  imageFilterSpecSink().push_back(std::move(spec));
  imageFilterComputeTypes().insert(cookType);
  if (sizeFn) imageFilterSizeFns()[cookType] = sizeFn;
  if (mippedOutput) imageFilterMippedOutputTypes().insert(cookType);
  if (selftestName && selftest) imageFilterSelfTests().push_back({selftestName, selftest});
}

}  // namespace sw
