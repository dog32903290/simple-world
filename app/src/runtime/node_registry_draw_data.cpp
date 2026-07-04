// runtime/node_registry_draw_data — NodeSpec rows for the DATA family (data.* command-rail ops).
// Peeled out of node_registry_draw.cpp as a dedicated lane file so the data lane can add its nodes
// here without touching drawSpecs()'s shared table (parallel-lane peel — no merge conflict with
// render/camera/flow lanes). Starts empty: no data.* op has landed yet, this is the lane hook.
// Add a data op = add ONE NodeSpec row to the vector below; drawSpecs() appends it in source order.
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawDataSpecs() {
  static const std::vector<NodeSpec> specs = {
      // (data lane appends rows here)
  };
  return specs;
}

}  // namespace sw
