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
      // PickObject (TiXL Lib.data.object.PickObject): pick ONE of N wired inputs by Index.Mod(count)
      // (PickObject.cs:23; Mod = MathUtils.cs:273-284 — negatives WRAP, unlike Switch's -1=none/-2=all).
      // The pick lives in both cook drivers' MultiInput Command collector branch (the Switch cook-core
      // precedent); the op cook just forwards the picked chain (point_ops_pickobject.cpp). NAMED FORK
      // (currency): TiXL's MultiInputSlot<object> takes ANY object; sw types the pick on the Command rail
      // until an Object rail exists (the data registry's command-rail posture). Ports keep TiXL's names
      // (Input / Index / Selected). .t3: Index default 0.
      {"PickObject", "PickObject",
       {{"Input", "Input", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"Selected", "Selected", "Command", false},
        {"Index", "Index", "Float", true, 0.0f, -1000.0f, 1000.0f}},
       nullptr,
       "data.object"},
  };
  return specs;
}

}  // namespace sw
