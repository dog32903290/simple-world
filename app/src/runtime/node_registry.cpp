// runtime/node_registry — central builder: assembles all per-family NodeSpec sub-tables into
// the single registry() vector consumed by findSpec / specTypes / animGroupForSlot.
//
// ARCHITECTURE rule 7 (data-driven): adding an op = add one row to the matching family file.
// Adding a NEW FAMILY = create node_registry_<family>.{h,cpp}, include it here, add one line.
//
// Sub-table files (each < 400 lines, runtime leaf):
//   node_registry_generators.cpp    — RadialPoints, LinePoints, GridPoints, SpherePoints
//   node_registry_point_modify.cpp  — TransformPoints, OrientPoints, RandomizePoints,
//                                     SetPointAttributes, AddNoise, FilterPoints
//   node_registry_point_combine.cpp — CombineBuffers
//   node_registry_particle.cpp      — TurbulenceForce, ParticleSystem
//   node_registry_draw.cpp          — DrawPoints, DrawLines, DrawBillboards, RenderTarget
//   (image-filter ops self-register — see image_filter_op_registry.h / point_ops_<name>.cpp)
//   node_registry_math.cpp          — Time, AudioReaction, Const, Multiply, Sine,
//                                     Add, Sub, Div, Clamp, Remap, Abs, Floor, Lerp
//
// Phase B parallel lanes:
//   point-transform lane  → extend node_registry_point_modify.cpp
//   particle-force lane   → extend node_registry_particle.cpp
//
// Split from the 600-line monolith (批次16-R, law debt: ARCHITECTURE rule 4 <400 lines).
// Zero behaviour change: op names/ports/cook bindings are verbatim copies.
#include "runtime/graph.h"
#include "runtime/Particle.h"      // full EvaluationContext definition
#include "runtime/value_eval_ops.h"  // value-node evaluate fns (mechanical split, 批次12-F)

// Family sub-tables (批次16-R split)
#include "runtime/node_registry_generators.h"
#include "runtime/node_registry_point_modify.h"
#include "runtime/node_registry_point_combine.h"
#include "runtime/node_registry_particle.h"
#include "runtime/node_registry_draw.h"
#include "runtime/image_filter_op_registry.h"  // imageFilterSpecSink() — image-filter ops self-register
#include "runtime/value_op_registry.h"         // valueOpSpecSink() — stateless value ops self-register
#include "runtime/field_node_registry.h"       // fieldSpecSink() — field (SDF) ops self-register
#include "runtime/mesh_op_registry.h"          // meshSpecSink() — mesh (4th flow) ops self-register
#include "runtime/node_registry_math.h"

#include <map>
#include <string>
#include <vector>

namespace sw {
namespace {

// registry() — flat concatenation of the literal-built family sub-tables. NOTE: the image-filter
// family is NOT here — its specs come from imageFilterSpecSink(), populated by file-scope
// ImageFilterOp registrars during pre-main dynamic init. That sink CANNOT be baked into this
// cached vector: a pre-main static global (doc::g_lib, document.cpp:20) calls findSpec during its
// own initialization, which builds registry() before the image-filter registrars have run — they'd
// be missing from the snapshot. So findSpec/specTypes read the sink LIVE (below). Order is cosmetic
// only: lookups are by type name and .swproj wires are keyed by port id, not registry position.
const std::vector<NodeSpec>& registry() {
  static const std::vector<NodeSpec> specs = [] {
    std::vector<NodeSpec> v;
    auto append = [&](const std::vector<NodeSpec>& src) {
      v.insert(v.end(), src.begin(), src.end());
    };
    append(generatorSpecs());       // RadialPoints, LinePoints, GridPoints, SpherePoints
    append(pointModifySpecs());     // TransformPoints, OrientPoints, RandomizePoints,
                                    // SetPointAttributes, AddNoise, FilterPoints
    append(pointCombineSpecs());    // CombineBuffers
    append(particleSpecs());        // TurbulenceForce, ParticleSystem
    append(drawSpecs());            // DrawPoints, DrawLines, DrawBillboards, RenderTarget
    append(mathSpecs());            // Time, AudioReaction, Const, Multiply, Sine, ...
    return v;
  }();
  return specs;
}

}  // namespace

// Dynamic spec table (批次 3): NodeSpecs generated from COMPOUND symbols so the canvas /
// inspector / cook treat a compound child like any node. Rebuilt wholesale by
// refreshCompoundSpecs (graph_bridge) after lib edits; built-ins always win on id clash so
// a compound can never shadow an operator. std::map keeps pointer stability per entry
// across lookups within a frame (the table itself is only swapped between frames).
std::map<std::string, NodeSpec>& dynamicSpecs() {
  static std::map<std::string, NodeSpec> m;
  return m;
}

void setDynamicSpecs(std::map<std::string, NodeSpec> specs) { dynamicSpecs() = std::move(specs); }

const NodeSpec* findSpec(const std::string& type) {
  for (const auto& s : registry())
    if (s.type == type) return &s;
  // Image-filter family: read the self-registration sink live (see registry() note on init order).
  for (const auto& s : imageFilterSpecSink())
    if (s.type == type) return &s;
  // Value-op family: same live-read seam (mirror of image-filter; init-order safe — sink fully
  // populated by pre-main dynamic init of each value_op_<name>.cpp ValueOp registrar).
  for (const auto& s : valueOpSpecSink())
    if (s.type == type) return &s;
  // Field-op family: same live-read seam (mirror of image-filter/value-op; init-order safe — sink
  // fully populated by pre-main dynamic init of each field_ops_<name>.cpp FieldOp registrar).
  for (const auto& s : fieldSpecSink())
    if (s.type == type) return &s;
  // Mesh-op family (the 4th cook flow): same live-read seam (init-order safe — sink fully populated
  // by pre-main dynamic init of each mesh_ops_<name>.cpp MeshOp registrar).
  for (const auto& s : meshSpecSink())
    if (s.type == type) return &s;
  auto it = dynamicSpecs().find(type);
  return it != dynamicSpecs().end() ? &it->second : nullptr;
}

std::vector<std::string> specTypes() {
  std::vector<std::string> out;
  for (const auto& s : registry()) out.push_back(s.type);
  // Image-filter ops self-register into the sink — append them so the Add menu lists all of them
  // regardless of static-init order (see registry() note).
  for (const auto& s : imageFilterSpecSink()) out.push_back(s.type);
  // Value ops self-register into their own sink — append so the Add menu lists them too (same note).
  for (const auto& s : valueOpSpecSink()) out.push_back(s.type);
  // Field (SDF) ops self-register into their own sink — append so the Add menu lists them (same note).
  for (const auto& s : fieldSpecSink()) out.push_back(s.type);
  // Mesh ops self-register into their own sink — append so the Add menu lists them (same note).
  for (const auto& s : meshSpecSink()) out.push_back(s.type);
  return out;
}

// Vec group walk: POSITIONAL, the exact same consume-the-run walk the Inspector row uses
// (a head at i owns ports[i..i+N-1]) — one grouping rule, two consumers (同源, graph.h).
AnimGroup animGroupForSlot(const NodeSpec& spec, const std::string& slotId) {
  AnimGroup g{slotId, 0, 1};
  for (size_t i = 0; i < spec.ports.size(); ++i) {
    const PortSpec& p = spec.ports[i];
    if (!p.isInput) continue;
    if (p.widget == Widget::Vec && p.vecArity >= 2) {
      const int n = p.vecArity > 4 ? 4 : p.vecArity;  // same clamp as the Inspector row
      for (int k = 0; k < n && i + (size_t)k < spec.ports.size(); ++k)
        if (spec.ports[i + (size_t)k].id == slotId) return {p.id, k, n};
      i += (size_t)(n - 1);  // consume the group's component ports
      continue;
    }
    if (p.id == slotId) return g;  // scalar: its own group of 1
  }
  return g;  // unknown slot: behaves like a scalar (projection falls back to index 0)
}

}  // namespace sw
