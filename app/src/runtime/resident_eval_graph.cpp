#include "runtime/resident_eval_graph.h"

#include "runtime/graph.h"  // NodeSpec / findSpec / PortSpec

namespace sw {

const ResidentInput* ResidentNode::input(const std::string& slotId) const {
  for (const ResidentInput& i : inputs)
    if (i.slotId == slotId) return &i;
  return nullptr;
}
const ResidentNode* ResidentEvalGraph::node(const std::string& path) const {
  auto it = byPath.find(path);
  return it != byPath.end() ? &nodes[it->second] : nullptr;
}

// STUB (Task 3/4 implement). Returns empty so the selftest is RED until then.
ResidentEvalGraph buildEvalGraph(const SymbolLibrary&, const std::string&) { return {}; }

// STUB (Task 3 implements).
float evalResidentFloat(const ResidentEvalGraph&, const std::string&, const std::string&,
                        const ResidentEvalCtx&, int) {
  return 0.0f;
}

}  // namespace sw
