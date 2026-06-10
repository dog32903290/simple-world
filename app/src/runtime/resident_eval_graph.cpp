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

// Inline one symbol's subgraph at `prefix`, given how its OWN input defs are driven from the
// outer graph (`inBindings`: inputDefId -> the resolved driver to copy onto inner consumers).
// Appends resident nodes to g, returns this symbol's output producers (outputDefId ->
// (residentPath, slotId)). `onPath` carries the symbol ids active on the current path for the
// self-nesting guard (TiXL does NOT guard this — contract S14). depth bounds runaway recursion.
std::map<std::string, std::pair<std::string, std::string>> inlineSymbol(
    const SymbolLibrary& lib, const Symbol& sym, const std::string& prefix,
    const std::map<std::string, ResidentInput>& inBindings, ResidentEvalGraph& g,
    std::vector<std::string>& onPath, int depth) {

  std::map<std::string, std::pair<std::string, std::string>> outProducers;
  if (depth > 64) return outProducers;

  // Per-child output producers for COMPOUND children (filled by recursion), keyed by child id.
  std::map<int, std::map<std::string, std::pair<std::string, std::string>>> childOuts;

  // 1. Recurse compound children first (their outputs are needed to resolve wires reading them);
  //    emit atomic children as resident nodes with Constant drivers.
  for (const SymbolChild& c : sym.children) {
    const Symbol* def = lib.find(c.symbolId);
    if (!def) continue;

    if (def->atomic) {
      ResidentNode rn;
      rn.path = prefix + std::to_string(c.id);
      rn.opType = c.symbolId;
      for (const SlotDef& d : def->inputDefs) {
        ResidentInput in;
        in.slotId = d.id;
        in.driver = ResidentInput::Driver::Constant;
        in.constant = effectiveInput(lib, c, d.id, d.def);
        rn.inputs.push_back(in);
      }
      g.byPath[rn.path] = (int)g.nodes.size();
      g.nodes.push_back(std::move(rn));
    } else {
      // self-nesting / cycle guard: skip if this symbol id is already active on the path.
      bool nested = false;
      for (const std::string& id : onPath)
        if (id == c.symbolId) { nested = true; break; }
      if (nested) continue;

      // Gather how THIS compound child's input defs are driven by the current symbol's wires.
      // Seed every compound input def with its effective Constant driver (override else def),
      // so boundary-INPUT consumers that are NOT wire-driven still receive the value. The wire
      // loop below overwrites the slots that ARE wired. (Mirrors the atomic branch's effectiveInput
      // seeding; refuter Finding 1: without this, override/default-driven compound inputs are dropped.)
      std::map<std::string, ResidentInput> childIn;
      for (const SlotDef& d : def->inputDefs) {
        ResidentInput ci;
        ci.slotId = d.id;
        ci.driver = ResidentInput::Driver::Constant;
        ci.constant = effectiveInput(lib, c, d.id, d.def);
        childIn[d.id] = ci;
      }
      for (const SymbolConnection& w : sym.connections) {
        if (w.dstChild != c.id) continue;  // wires feeding this compound child's inputs
        ResidentInput in;
        in.slotId = w.dstSlot;
        if (sourceIsSymbolInput(w)) {
          // source = parent's own input def -> copy the driver the outer graph gave us.
          auto bit = inBindings.find(w.srcSlot);
          if (bit != inBindings.end()) { in = bit->second; in.slotId = w.dstSlot; }
        } else {
          const Symbol* sdef = lib.find(/*src child's symbol*/ "");  // resolved below
          (void)sdef;
          // source is a sibling child: resolve to its resident producer.
          // atomic sibling -> (prefix+srcChild, srcSlot); compound sibling -> its childOuts.
          auto sibling = [&](int childId, const std::string& slot) -> std::pair<std::string,std::string> {
            auto cit = childOuts.find(childId);
            if (cit != childOuts.end()) {
              auto pit = cit->second.find(slot);
              if (pit != cit->second.end()) return pit->second;
            }
            return {prefix + std::to_string(childId), slot};
          };
          auto pr = sibling(w.srcChild, w.srcSlot);
          in.driver = ResidentInput::Driver::Connection;
          in.srcNodePath = pr.first;
          in.srcSlotId = pr.second;
        }
        childIn[w.dstSlot] = in;
      }

      onPath.push_back(c.symbolId);
      childOuts[c.id] = inlineSymbol(lib, *def, prefix + std::to_string(c.id) + "/", childIn, g,
                                     onPath, depth + 1);
      onPath.pop_back();
    }
  }

  // 2. Resolve this symbol's wires onto atomic dst inputs + collect this symbol's output producers.
  auto resolveSrc = [&](const SymbolConnection& w) -> std::pair<std::string, std::string> {
    if (sourceIsSymbolInput(w)) {  // shouldn't reach here for value resolution; handled via inBindings
      return {"", ""};
    }
    auto cit = childOuts.find(w.srcChild);  // compound sibling output
    if (cit != childOuts.end()) {
      auto pit = cit->second.find(w.srcSlot);
      if (pit != cit->second.end()) return pit->second;
    }
    return {prefix + std::to_string(w.srcChild), w.srcSlot};  // atomic sibling
  };

  for (const SymbolConnection& w : sym.connections) {
    if (targetIsSymbolOutput(w)) {                 // child -> this symbol's external output
      if (sourceIsSymbolInput(w)) continue;        // pass-through input->output (rare); skip in slice 1
      outProducers[w.dstSlot] = resolveSrc(w);
      continue;
    }
    if (sourceIsSymbolInput(w)) continue;          // boundary-input already applied via childIn above
    // child -> child: only ATOMIC dst gets a resident input here (compound dst handled via childIn).
    std::string dstPath = prefix + std::to_string(w.dstChild);
    auto it = g.byPath.find(dstPath);
    if (it == g.byPath.end()) continue;            // dst is compound (driven via childIn) -> skip
    auto pr = resolveSrc(w);
    for (ResidentInput& in : g.nodes[it->second].inputs)
      if (in.slotId == w.dstSlot) {
        in.driver = ResidentInput::Driver::Connection;
        in.srcNodePath = pr.first;
        in.srcSlotId = pr.second;
      }
  }

  // 3. Apply boundary-INPUT bindings onto atomic children that read this symbol's input defs
  //    (a wire srcChild==boundary, dstChild==atomic): copy the outer driver onto the inner input.
  for (const SymbolConnection& w : sym.connections) {
    if (!sourceIsSymbolInput(w) || targetIsSymbolOutput(w)) continue;
    std::string dstPath = prefix + std::to_string(w.dstChild);
    auto it = g.byPath.find(dstPath);
    if (it == g.byPath.end()) continue;            // dst compound: bound via childIn already
    auto bit = inBindings.find(w.srcSlot);
    if (bit == inBindings.end()) continue;
    for (ResidentInput& in : g.nodes[it->second].inputs)
      if (in.slotId == w.dstSlot) {
        ResidentInput b = bit->second;
        b.slotId = w.dstSlot;
        in = b;
      }
  }

  return outProducers;
}

}  // namespace

ResidentEvalGraph buildEvalGraph(const SymbolLibrary& lib, const std::string& rootId) {
  ResidentEvalGraph g;
  const Symbol* root = lib.find(rootId);
  if (!root) return g;
  std::vector<std::string> onPath = {rootId};
  std::map<std::string, ResidentInput> noBindings;  // root has no outer driver
  g.outputs = inlineSymbol(lib, *root, "", noBindings, g, onPath, 0);
  return g;
}

}  // namespace sw
