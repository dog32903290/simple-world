#include "runtime/resident_eval_graph.h"

#include "runtime/graph.h"     // NodeSpec / findSpec / PortSpec
#include "runtime/Particle.h"  // full EvaluationContext definition (graph.h only forward-decls it)

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

float evalResidentFloat(const ResidentEvalGraph& g, const std::string& nodePath,
                        const std::string& outSlotId, const ResidentEvalCtx& ctx, int depth) {
  if (depth > 64) return 0.0f;  // cycle guard
  const ResidentNode* n = g.node(nodePath);
  if (!n) return 0.0f;
  const NodeSpec* s = findSpec(n->opType);
  if (!s || !s->evaluate) return 0.0f;

  // Gather Float input values in spec port order (mirrors flat evalFloat).
  float in[8];
  int ni = 0;
  for (size_t i = 0; i < s->ports.size() && ni < 8; ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    float v = p.def;
    if (const ResidentInput* ri = n->input(p.id)) {
      switch (ri->driver) {
        case ResidentInput::Driver::Constant:
          v = ri->constant;
          break;
        case ResidentInput::Driver::Connection:
          v = evalResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx, depth + 1);
          break;
        case ResidentInput::Driver::Automation:
          v = 0.0f;  // S3: sample def-layer curve `ri->curveRef` @ ctx.localTime. Stub for slice 1.
          break;
      }
    }
    in[ni++] = v;
  }

  // outIdx = index of the pulled OUTPUT port in spec.ports (matches flat evalFloat's outIdx).
  int outIdx = 0;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].id == outSlotId) { outIdx = (int)i; break; }

  // Reuse the SAME evaluate fns as the flat path: build a transient 16-byte ctx (time = wall
  // clock for now; automation sampling localTime arrives in S3). EvaluationContext lives in
  // Particle.h; include it in THIS .cpp's translation unit at the top (see Step 2).
  EvaluationContext ec{};
  ec.frameIndex = ctx.frameIndex;
  ec.time = ctx.localFxTime;
  ec.deltaTime = 0.0f;
  return s->evaluate(outIdx, in, ni, ec);
}

namespace {

// Resolve one connection's SOURCE side to a (residentPath, slotId) value producer, for wires
// whose source is a local atomic child (Task 3) — boundary-input + compound sources come in Task 4.
struct SrcRef { bool ok = false; std::string path, slot; };

}  // namespace

ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId) {
  ResidentEvalGraph g;
  const Symbol* root = lib.find(rootId);
  if (!root) return g;

  const std::string prefix;  // root prefix is empty; Task 4 grows it per nesting level

  // 1. Emit a resident node for each atomic child, seeding Constant drivers from effectiveInput.
  for (const SymbolChild& c : root->children) {
    const Symbol* def = lib.find(c.symbolId);
    if (!def) continue;
    // Task 4 handles compound children; until then, only atomic children are inlined.
    if (!def->atomic) continue;

    ResidentNode rn;
    rn.path = prefix + std::to_string(c.id);
    rn.opType = c.symbolId;
    for (const SlotDef& d : def->inputDefs) {
      ResidentInput in;
      in.slotId = d.id;
      in.driver = ResidentInput::Driver::Constant;
      in.constant = effectiveInput(lib, c, d.id, d.def);  // instance override else def default
      rn.inputs.push_back(in);
    }
    g.byPath[rn.path] = (int)g.nodes.size();
    g.nodes.push_back(std::move(rn));
  }

  // 2. Apply connections: a local child->child wire becomes a Connection driver on the dst input;
  //    a child->boundary-output wire records the graph's external output producer.
  for (const SymbolConnection& conn : root->connections) {
    if (sourceIsSymbolInput(conn)) continue;  // boundary-INPUT source -> Task 4 (needs parent bindings)
    std::string srcPath = prefix + std::to_string(conn.srcChild);
    if (!g.node(srcPath)) continue;           // src not (yet) inlined (e.g. compound) -> Task 4

    if (targetIsSymbolOutput(conn)) {
      g.outputs[conn.dstSlot] = {srcPath, conn.srcSlot};  // -> root external output
      continue;
    }
    std::string dstPath = prefix + std::to_string(conn.dstChild);
    auto it = g.byPath.find(dstPath);
    if (it == g.byPath.end()) continue;
    ResidentNode& dst = g.nodes[it->second];
    for (ResidentInput& in : dst.inputs)
      if (in.slotId == conn.dstSlot) {
        in.driver = ResidentInput::Driver::Connection;
        in.srcNodePath = srcPath;
        in.srcSlotId = conn.srcSlot;
      }
  }
  return g;
}

}  // namespace sw
