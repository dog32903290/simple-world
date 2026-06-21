// runtime/resident_eval_graph — the EVAL/RESOLVE half of the resident engine (evalResidentFloat /
// resolveResidentFloatInputs / sampleAutomation). The FLATTENER half (inlineSymbol +
// buildEvalGraph) lives in resident_eval_flatten.cpp (批次9 split along the two-job seam,
// ARCHITECTURE rule 4); the version-chasing cache lives in resident_eval_cache.cpp.
#include "runtime/resident_eval_graph.h"

#include "runtime/graph.h"     // NodeSpec / findSpec / PortSpec
#include "runtime/Particle.h"  // full EvaluationContext definition (graph.h only forward-decls it)

#include <cmath>   // NAN sentinel — the over-cap guard returns NaN (mirror of flat graph.cpp)
#include <cstdio>  // std::fprintf — the LOUD over-cap diagnostic (mirror of flat graph.cpp)

namespace sw {

// ★ MUST STAY IN SYNC with graph.cpp's evalFloat kMaxFloatIn (currently 32). The flat gather
// (graph.cpp::evalFloat) and this PRODUCTION resident gather (evalResidentFloat) MUST cap Float
// inputs identically + guard identically, or a >cap op NaN-bites the flat golden while silently
// truncating in production (an inverse-R-2 trap). graph.cpp keeps its own function-local
// constexpr (it cannot include this header without a dependency cycle); when one bumps, bump both.
constexpr int kMaxFloatIn = 32;

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
    // Stateful externally-cooked value nodes (AudioReaction) + host-scalar consumers (FloatListLength/
    // PickFloatFromList/StringLength — the FloatList→Float bridge, list-routing seam): mirror flat
    // evalFloat's outCache read. This branch is ALREADY GENERIC (any !evaluate node reads extOut),
    // which is the union the flat path's predicate was widened to (AudioReaction || isHostScalarOp) —
    // so the flat (Node::outCache) and resident (ResidentNode::extOut) READ sides are now ALIGNED, not
    // drifting (blueprint R-5).
    // WRITE side: extOut is filled per-frame by the app's cookers — AudioReaction/stateful by
    // cookAudioReactionNodes/cookStatefulValueNodes, and the FloatList host-scalar ops (FloatListLength/
    // PickFloatFromList) by cookHostScalarNodes (resident_host_scalar_cook.cpp). So a FloatList
    // host-scalar op reached via this resident path now reads the REAL cooked scalar (the production
    // bridge is live, not flat-only). EXCEPTION: StringLength — its String wire is dropped by the
    // resident flatten, so cookHostScalarNodes skips it and its extOut stays 0 here (the resident
    // string-wire rail is a separate, not-yet-built seam). index = output port index (leading ports).
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (!s->ports[i].isInput && s->ports[i].id == outSlotId)
        return i < 8 ? n->extOut[i] : 0.0f;
    return 0.0f;
  }

  // Gather Float input values in spec port order (mirrors flat evalFloat). in[kMaxFloatIn]: a
  // MultiInput port (批次25 seam) expands its primary + every extraConn into consecutive in[]
  // slots, so a reducer (Sum) can take many sources — kMaxFloatIn caps it.
  //
  // ★LOUD GUARD (mirror of graph.cpp::evalFloat — flat and production must fail IDENTICALLY).
  // Count this spec's Float inputs UP FRONT; if it exceeds the cap, do NOT quietly gather a prefix
  // and hand a too-short in[]/ni to evaluate() (the old behaviour silently truncated beyond the
  // cap — self-deception). Emit the SAME error + return the SAME NaN sentinel as the flat path:
  // NaN != any finite want, so EVERY golden on this op flips RED (NaN-aware: std::fabs(NaN - want)
  // is NaN, never < eps). The fix on tripping this is to raise kMaxFloatIn in BOTH places.
  // NOTE: this counts spec Float INPUT PORTS; a MultiInput port that expands past the cap at
  // RUNTIME is still bounded by the `ni < kMaxFloatIn` loop guard below (same as flat), but the
  // common >cap case (a wide op like PerlinNoise3's 19 inputs) is caught loudly here.
  {
    int floatIn = 0;
    for (const PortSpec& p : s->ports)
      if (p.isInput && p.dataType == "Float") ++floatIn;
    if (floatIn > kMaxFloatIn) {
      std::fprintf(stderr,
                   "[evalResidentFloat] FATAL: node type '%s' has %d Float inputs, exceeds gather "
                   "cap %d — raise kMaxFloatIn (graph.cpp + resident_eval_graph.cpp, kept in sync). "
                   "Returning NaN (was silent truncation).\n",
                   n->opType.c_str(), floatIn, kMaxFloatIn);
      return NAN;  // bites every golden; never silently truncate to a wrong value again
    }
  }
  float in[kMaxFloatIn];
  int ni = 0;
  for (size_t i = 0; i < s->ports.size() && ni < kMaxFloatIn; ++i) {
    const PortSpec& p = s->ports[i];
    if (!(p.isInput && p.dataType == "Float")) continue;
    const ResidentInput* ri = n->input(p.id);
    float v = p.def;
    if (ri) {
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
    // MultiInput: append the extra wired sources (always Connection producers) after the primary.
    if (p.multiInput && ri) {
      for (const auto& ec : ri->extraConns) {
        if (ni >= kMaxFloatIn) break;
        in[ni++] = evalResidentFloat(g, ec.first, ec.second, ctx, depth + 1);
      }
    }
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
  // LocalFxTime seam (additive): expose TiXL's LocalFxTime (BARS) to value ops that read it
  // (PerlinNoise2's OverrideTime-unwired path). ec.time stays as-is (the existing readers only
  // touch .time/.frameIndex/.deltaTime); this only POPULATES the formerly-dead offset-12 slot.
  ec.localFxTime = ctx.localFxTime;
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
