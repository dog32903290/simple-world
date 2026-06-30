// Shared eval helper for the math-ops selftests, split across math_ops_selftest.cpp (scalar +
// vec3) and math_ops_selftest_extra.cpp (vec2 + logic + misc-vec). Lifted out of the per-TU
// lambda so both halves call ONE definition (was an identical [&] lambda in each).
//
// Zone: runtime (pure value eval, no GPU, no platform deps). Leaf. Header-only inline.
#pragma once

#include <initializer_list>
#include <utility>

#include "runtime/graph.h"     // Graph, Node, NodeSpec, findSpec, evalFloat, pinId
#include "runtime/Particle.h"  // EvaluationContext

namespace sw {

// Build a one-node graph of `type`, set the named Float input params, evaluate the named out pin.
// Supports any op whose output port name is passed as outPortId (e.g. "Result", "Result.x", "X").
inline float evalOpParams(const char* type,
                          std::initializer_list<std::pair<const char*, float>> params,
                          const char* outPortId) {
  const NodeSpec* spec = findSpec(type);
  if (!spec) return -999.0f;
  Graph g;
  Node nd; nd.id = g.nextId++;
  nd.type = type;
  for (const auto& p : spec->ports)
    if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
  g.nodes.push_back(nd);
  int nid = g.nodes.back().id;
  for (auto& kv : params) g.node(nid)->params[kv.first] = kv.second;
  int outIdx = -1;
  for (size_t i = 0; i < spec->ports.size(); ++i)
    if (spec->ports[i].id == outPortId) { outIdx = (int)i; break; }
  EvaluationContext ctx{}; ctx.time = 0.0f;
  return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
}

}  // namespace sw
