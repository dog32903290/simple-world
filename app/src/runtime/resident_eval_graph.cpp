#include "runtime/resident_eval_graph.h"

#include "runtime/graph.h"     // NodeSpec / findSpec / PortSpec
#include "runtime/Particle.h"  // full EvaluationContext definition (graph.h only forward-decls it)

namespace sw {

const ResidentInput* ResidentNode::input(const std::string& slotId) const {
  for (const ResidentInput& i : inputs)
    if (i.slotId == slotId) return &i;
  return nullptr;
}

float sampleAutomation(const ResidentEvalCtx& ctx, const ResidentInput& ri) {
  // Resolve the curve through the def-layer Animator on ri.animSymbolId, sample @ localTime (the
  // playhead). No lib / no symbol / no curve -> the projected constant (flat-parity fallback).
  if (ctx.lib) {
    const Symbol* sym = ctx.lib->find(ri.animSymbolId);
    if (sym) {
      const Curve* c = sym->animator.resolveRef(ri.curveRef);
      if (c && !c->empty()) return (float)c->sample((double)ctx.localTime);
    }
  }
  return ri.constant;
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

  // S2 BYPASS (= TiXL Slot.ByPassUpdate): the no-cache twin of pullResidentFloat's redirect. A
  // bypassed node's MAIN output returns its MAIN input's upstream value. (isDisabled is a CACHE
  // semantic — freeze the last computed value — so it has no meaning on this stateless path, which
  // recomputes from scratch each call; the cache path pullResidentFloat owns disable.)
  if (n->bypassed && outSlotId == n->bypassOutSlot) {
    const ResidentInput* ri = n->input(n->bypassInSlot);
    if (!ri) return 0.0f;
    switch (ri->driver) {
      case ResidentInput::Driver::Constant:   return ri->constant;
      case ResidentInput::Driver::Automation: return sampleAutomation(ctx, *ri);
      case ResidentInput::Driver::Connection:
        return evalResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx, depth + 1);
    }
  }

  const NodeSpec* s = findSpec(n->opType);
  if (!s) return 0.0f;
  if (!s->evaluate) {
    // Stateful externally-cooked value nodes (AudioReaction): mirror flat evalFloat's
    // outCache read — the app's per-frame cooker wrote extOut; index = output port index
    // (same numbering as flat: outputs are the leading ports).
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (!s->ports[i].isInput && s->ports[i].id == outSlotId)
        return i < 3 ? n->extOut[i] : 0.0f;
    return 0.0f;
  }

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
          // S3: sample the def-layer curve `ri->curveRef` @ ctx.localTime (the playhead). Falls
          // back to the projected CONSTANT when unresolvable — the same fallback flat
          // resolvePortValue uses, so the two paths can't diverge (refuter-2b analysis finding 1).
          v = sampleAutomation(ctx, *ri);
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

std::map<std::string, float> resolveResidentFloatInputs(const ResidentEvalGraph& g,
                                                        const ResidentNode& n,
                                                        const ResidentEvalCtx& ctx) {
  std::map<std::string, float> out;
  const NodeSpec* s = findSpec(n.opType);
  if (!s) return out;
  for (const PortSpec& p : s->ports) {
    if (!(p.isInput && p.dataType == "Float")) continue;
    float v = p.def;
    if (const ResidentInput* ri = n.input(p.id)) {
      switch (ri->driver) {
        case ResidentInput::Driver::Constant:   v = ri->constant; break;
        case ResidentInput::Driver::Connection:
          v = evalResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx);
          break;
        case ResidentInput::Driver::Automation: v = sampleAutomation(ctx, *ri); break;  // S3 sample @ localTime
      }
    }
    out[p.id] = v;
  }
  return out;
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
      // S2 bypass: an atomic child whose MAIN I/O type is bypassable (childIsBypassable) and that is
      // marked isBypassed has its MAIN output redirected to its MAIN input's upstream at eval time
      // (= TiXL ByPassUpdate). Record the main-slot ids now; the redirect itself lives in pull/eval.
      // Whitelist/refusal mirrors SetBypassed — a non-bypassable type leaves bypassed=false (silent
      // no-op, like TiXL's IsBypassable() guard). Output-connectedness is enforced by the COMMAND
      // layer (SetBypassChildCommand) before the flag is ever set, so the builder trusts the flag.
      if (c.isBypassed && childIsBypassable(lib, c)) {
        rn.bypassed = true;
        rn.bypassInSlot = def->inputDefs[0].id;
        rn.bypassOutSlot = def->outputDefs[0].id;
      }
      // S2 per-output isDisabled / triggerOverride: project the child's sparse maps onto the resident
      // node (the cache reads them in initResidentNodeCache). Only outputs the referenced symbol
      // actually defines are carried (a stale slot id would be ignored downstream, but filter here
      // for cleanliness). Only Always matters for the LIVE-source derivation; None/Animated leave the
      // default chase (Animated arrives via the Automation driver path, not a per-output trigger here).
      for (const auto& kv : c.disabledOutputs)
        if (kv.second) rn.disabledOut[kv.first] = true;
      for (const auto& kv : c.triggerOverrides)
        if (kv.second == TriggerOverride::Always) rn.triggerAlwaysOut[kv.first] = true;
      const NodeSpec* spec = findSpec(c.symbolId);  // anim-group lookup (Vec multi-channel)
      for (const SlotDef& d : def->inputDefs) {
        ResidentInput in;
        in.slotId = d.id;
        // S3: an Automation driver projects from the PARENT symbol's Animator (the def owning this
        // child). The constant is still the KEPT fallback a future un-animate restores (= TiXL reads
        // the default live; same kept-fallback rule as patchLibSetDefault). If not animated, plain
        // Constant. A wire feeding this slot overwrites the driver below (loop 2/3) — automation and
        // a connection don't coexist on one input (single-cardinality), connection wins on a clash.
        //
        // Vec multi-channel (批次8): the Animator keys a vector param's curves under the group
        // HEAD's slot id (= TiXL one inputId -> Curve[N]); each component slot resolves its OWN
        // channel index. animGroupForSlot is the SAME grouping the Inspector gesture uses (同源,
        // graph.h) — a scalar resolves to {d.id, 0, 1}, identical to the pre-Vec projection.
        in.constant = effectiveInput(lib, c, d.id, d.def);
        const AnimGroup ag = spec ? animGroupForSlot(*spec, d.id) : AnimGroup{d.id, 0, 1};
        if (sym.animator.isAnimated(c.id, ag.headId)) {
          in.driver = ResidentInput::Driver::Automation;
          in.curveRef = Animator::makeRef(c.id, ag.headId, ag.channel);
          in.animSymbolId = sym.id;
        } else {
          in.driver = ResidentInput::Driver::Constant;
        }
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
