// runtime/resident_eval_graph — the EVAL/RESOLVE half of the resident engine (evalResidentFloat /
// resolveResidentFloatInputs / sampleAutomation). The FLATTENER half (inlineSymbol +
// buildEvalGraph) lives in resident_eval_flatten.cpp (批次9 split along the two-job seam,
// ARCHITECTURE rule 4); the version-chasing cache lives in resident_eval_cache.cpp.
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

}  // namespace sw
