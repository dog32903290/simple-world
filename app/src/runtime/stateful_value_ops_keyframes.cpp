// runtime/stateful_value_ops_keyframes — FindKeyframes / SetKeyframes (anim/utils family):
// REFLECT into the keyframe CURVES of a CONNECTED UPSTREAM operator and read (FindKeyframes) or
// write (SetKeyframes) them. Unlike EaseKeys — which reads its OWN Value input's Automation curves —
// these ops reach ACROSS the AnimatedOp connection into the upstream node's Animator store.
//
// TiXL authority:
//   Operators/Lib/numbers/anim/utils/FindKeyframes.cs
//   Operators/Lib/numbers/anim/utils/SetKeyframes.cs
//   Core/DataTypes/Curve.cs (GetVDefinitions cs:16-19, TryGetPreviousKey cs:80-91,
//                            TryGetNextKey cs:93-105, GetSampledValue, AddOrUpdateV, RemoveKeyframeAt)
//   Core/Utils/MathUtils.cs WasTriggered (cs:531-538 — edge detect: fires only on state change)
//
// THE SEAM (why this is not a plain step fn, and why it is DIFFERENT from EaseKeys):
//   TiXL reads `slot.UpdateAction.Target.Parent.Symbol.Animator` — a C# delegate reflecting into the
//   Instance the AnimatedOp MultiInput slot is wired to, then that instance's PARENT symbol's Animator.
//   sw has no delegate reflection; instead we FOLLOW THE sw GRAPH CONNECTION: the FindKeyframes/
//   SetKeyframes ResidentNode's "AnimatedOp" input is a Connection (or MultiInput of Connections)
//   whose srcNodePath names the upstream ResidentNode. That upstream node's inputs each already carry
//   their own Automation driver (animSymbolId + curveRef, projected at flatten time exactly as
//   sampleAutomation resolves — resident_eval_flatten.cpp:129-132). So "the upstream op's animated
//   curves" = iterate the upstream ResidentNode's inputs, keep the Automation-driven ones, resolve
//   each curve through ctx.lib->find(animSymbolId)->animator.resolveRef(curveRef), flat-indexed in
//   input-declaration order — the exact 1:1 of TiXL's `foreach(p in target.Inputs) if IsAnimated ...`
//   (FindKeyframes.cs:119-138). A VecN animated input is N sw input rows (.x/.y/.z), each its own
//   Automation driver → the SAME flat-index sequence as TiXL's TryGetCurvesForInputSlot returning N.
//
// MUTABILITY (SetKeyframes write): the def-layer store IS mutable — Curve::addOrUpdate / removeAt and
//   Animator::curvesFor(non-const) exist, and the app owns a mutable SymbolLibrary (doc::g_lib()). The
//   runtime leaf cannot include app/document.h (dependency direction), so frame_cook passes a mutable
//   SymbolLibrary* into cookSetKeyframesNode; the write bumps libRevision app-side (a definition edit,
//   = TiXL SetKeyframes editing the Animator on the parent Symbol, broadcast to instances next rebuild).
//
// TIME: FindKeyframes SampleAndDistance samples @ IndexOrTime (a probe arg, not the clock).
//   SetKeyframes writes @ context.LocalFxTime (the wall clock, SetKeyframes.cs:52) — ctx.localFxTime.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <iterator>  // std::prev / std::advance
#include <limits>    // std::numeric_limits (Nearest/SampleAndDistance ±inf sentinels)
#include <string>
#include <vector>

#include "runtime/curve.h"                        // Curve / VDefinition
#include "runtime/curve_animator.h"               // Animator::resolveRef / curvesFor / parseRef
#include "runtime/resident_eval_graph.h"          // ResidentNode / ResidentEvalGraph / ResidentEvalCtx
#include "runtime/stateful_value_op_registry.h"   // StatefulOpReg
#include "runtime/stateful_value_ops.h"           // StatefulValueState / cook hook decls
#include "runtime/stateful_value_ops_internal.h"  // getIn / enumOf

namespace sw {
namespace {

// TEETH hook (file-local; 0 = production, set by --selftest-keyframes only via setKeyframesBug):
//   1 = SEVER the cross-connection curve query (curveForUpstream returns nullptr): FindKeyframes
//       collapses to count=0 / Time=0 / Value=0 and SetKeyframes writes nothing → the golden's fixed
//       expected values (a real read/write result) go RED. Proves the reflection-through-connection
//       seam is load-bearing (not a self-sourced constant).
//   2 = DROP the value read (FindKeyframes returns the keyframe TIME where it should return the VALUE,
//       i.e. Value := (float)vDef.U): the Value-probe assertions go RED while the Time probes stay
//       green — isolates the bite to the value-vs-time channel selection (FindKeyframes.cs:74-75).
int g_keyframesBug = 0;

// Resolve the requestCurveIndex'th ANIMATED curve on the upstream node, following the AnimatedOp
// connection. = TiXL TryFindCurveWithIndex (FindKeyframes.cs:98-141 / SetKeyframes.cs:69-113): iterate
// the upstream node's inputs, keep Automation-driven ones (= animator.IsAnimated), flat-index the
// curves. Returns nullptr when: no connection, OpIndex out of range, upstream not animated, or the
// requested flat index overshoots. `outCurveRef`/`outSymId` are filled with the resolved curve's
// def-layer identity so SetKeyframes can re-fetch a MUTABLE handle to the same curve.
const Curve* curveForUpstream(const ResidentEvalGraph& g, const ResidentNode& rn,
                              const ResidentEvalCtx& ctx, int opIndex, int requestCurveIndex,
                              std::string* outCurveRef, std::string* outSymId) {
  if (g_keyframesBug == 1) return nullptr;  // bug 1: SEVER the cross-connection query
  const ResidentInput* ao = rn.input("AnimatedOp");
  if (!ao || ao->driver != ResidentInput::Driver::Connection || !ctx.lib) return nullptr;

  // Collect the connected upstream node paths in wire-declaration order (primary + extraConns), then
  // pick opIndex (= TiXL AnimatedOp.CollectedInputs[opIndex], clamped). CollectedInputs are the
  // connected slots; an unconnected AnimatedOp has no Connection driver → handled above.
  std::vector<std::string> srcPaths;
  srcPaths.push_back(ao->srcNodePath);
  for (const auto& ec : ao->extraConns) srcPaths.push_back(ec.first);
  if (srcPaths.empty()) return nullptr;
  if (opIndex < 0) opIndex = 0;
  if (opIndex >= (int)srcPaths.size()) opIndex = (int)srcPaths.size() - 1;  // Clamp(0, count) — TiXL cs:42

  const ResidentNode* up = g.node(srcPaths[opIndex]);
  if (!up) return nullptr;

  int curveIndex = 0;
  for (const ResidentInput& ui : up->inputs) {
    if (ui.driver != ResidentInput::Driver::Automation) continue;  // = !IsAnimated → skip (cs:121)
    const Symbol* sym = ctx.lib->find(ui.animSymbolId);
    if (!sym) continue;
    const Curve* c = sym->animator.resolveRef(ui.curveRef);
    if (!c) continue;
    if (curveIndex == requestCurveIndex) {  // cs:130-133
      if (outCurveRef) *outCurveRef = ui.curveRef;
      if (outSymId) *outSymId = ui.animSymbolId;
      return c;
    }
    ++curveIndex;
  }
  return nullptr;  // requested index overshoots the animated-curve count (cs:140)
}

// TiXL FindKeyframes Modes (cs:181-186). Values pinned to the enum ordinal.
enum Modes { kIndex = 0, kNearest = 1, kSampleAndDistance = 2 };

}  // namespace

void setKeyframesBug(int mode) { g_keyframesBug = mode; }

// FindKeyframes production cook (read-only; const lib). Outputs (extOut order = registry port order):
//   [0]=Time, [1]=Value, [2]=KeyframeCount. Returns false for any non-FindKeyframes op.
bool cookFindKeyframesNode(const ResidentEvalGraph& g, ResidentNode& rn, const ResidentEvalCtx& ctx,
                           const std::map<std::string, float>& in) {
  if (rn.opType != "FindKeyframes") return false;

  const int opIndex = enumOf(in, "OpIndex");
  const int curveIndex = enumOf(in, "CurveIndex");
  const bool wrapIndex = getIn(in, "WrapIndex", 0.0f) >= 0.5f;
  const int mode = enumOf(in, "Mode");
  const float indexOrTime = getIn(in, "IndexOrTime", 0.0f);

  const Curve* curve = curveForUpstream(g, rn, ctx, opIndex, curveIndex, nullptr, nullptr);
  if (!curve) {  // TiXL cs:52-58 (no curve): count=0, Time=0, Value=0
    rn.extOut[0] = 0.0f;
    rn.extOut[1] = 0.0f;
    rn.extOut[2] = 0.0f;
    return true;
  }

  const auto& tbl = curve->table();  // sorted time -> VDefinition (= GetVDefinitions order, cs:49)
  const int count = (int)tbl.size();
  rn.extOut[2] = (float)count;
  if (count == 0) {  // cs:61-64
    rn.extOut[0] = 0.0f;
    rn.extOut[1] = 0.0f;
    return true;
  }

  float outTime = 0.0f, outValue = 0.0f;
  switch (mode) {
    case kIndex: {  // cs:69-77
      const int raw = (int)indexOrTime;
      int idx = wrapIndex ? (raw % count) : (std::abs(raw) % count);
      if (idx < 0) idx += count;  // C# % keeps sign of dividend; normalize the wrap case
      auto it = tbl.begin();
      std::advance(it, idx);
      outTime = (float)it->second.u;
      outValue = (float)it->second.value;
      break;
    }
    case kNearest: {  // cs:78-85 (TryFindClosestKey by |t - keyU|)
      const auto next = tbl.lower_bound(Curve::roundTime(indexOrTime));
      const VDefinition* prevKey = (next == tbl.begin()) ? nullptr : &std::prev(next)->second;
      const VDefinition* nextKey = (next == tbl.end()) ? nullptr : &next->second;
      const double prevT = prevKey ? prevKey->u : -std::numeric_limits<double>::infinity();
      const double nextT = nextKey ? nextKey->u : std::numeric_limits<double>::infinity();
      const VDefinition* closest =
          (std::abs(indexOrTime - prevT) < std::abs(indexOrTime - nextT)) ? prevKey : nextKey;
      if (closest) {
        outTime = (float)closest->u;
        outValue = (float)closest->value;
      }
      break;
    }
    case kSampleAndDistance: {  // cs:87-94: Time = closestKey.U - t; Value = GetSampledValue(t)
      const auto next = tbl.lower_bound(Curve::roundTime(indexOrTime));
      const VDefinition* prevKey = (next == tbl.begin()) ? nullptr : &std::prev(next)->second;
      const VDefinition* nextKey = (next == tbl.end()) ? nullptr : &next->second;
      const double prevT = prevKey ? prevKey->u : -std::numeric_limits<double>::infinity();
      const double nextT = nextKey ? nextKey->u : std::numeric_limits<double>::infinity();
      const VDefinition* closest =
          (std::abs(indexOrTime - prevT) < std::abs(indexOrTime - nextT)) ? prevKey : nextKey;
      if (closest) {
        outTime = (float)closest->u - indexOrTime;
        outValue = (float)curve->sample(indexOrTime);
      }
      break;
    }
    default:
      break;
  }
  rn.extOut[0] = outTime;
  rn.extOut[1] = (g_keyframesBug == 2) ? outTime : outValue;  // bug 2: DROP value read (returns time)
  return true;
}

// SetKeyframes production cook (WRITE; MUTABLE lib). `mutableLib` is doc::g_lib() (app-owned), passed
// so the runtime leaf can re-fetch a mutable Curve on the SAME (childId,inputId) the read resolved.
// `state` carries the per-op edge-latch (WasTriggered's `ref current`). Returns false for non-SetKeyframes.
// Output [0]=CurrentValue. `dirtied` set true iff the store was mutated (caller bumps libRevision).
bool cookSetKeyframesNode(const ResidentEvalGraph& g, ResidentNode& rn, const ResidentEvalCtx& ctx,
                          const std::map<std::string, float>& in, SymbolLibrary* mutableLib,
                          StatefulValueState& state, bool* dirtied) {
  if (rn.opType != "SetKeyframes") return false;
  if (dirtied) *dirtied = false;

  const int opIndex = enumOf(in, "OpIndex");
  const int curveIndex = enumOf(in, "CurveIndex");
  const float value = getIn(in, "Value", 0.0f);
  const bool triggerSetNow = getIn(in, "TriggerSet", 0.0f) >= 0.5f;
  const bool triggerClearNow = getIn(in, "TriggerClear", 0.0f) >= 0.5f;

  // WasTriggered edge detect (MathUtils.cs:531-538): fire on state CHANGE, latch the new state. Two
  // latches live in the op's StatefulValueState scratch (s[0]=TriggerSet prev, s[1]=TriggerClear prev),
  // 0 = default (false) — per-op, keyed by rn.path in frame_cook's state map.
  const bool prevSet = state.s[0] >= 0.5f;
  const bool prevClear = state.s[1] >= 0.5f;
  const bool triggeredSet = (triggerSetNow != prevSet) && triggerSetNow;
  const bool triggeredClear = (triggerClearNow != prevClear) && triggerClearNow;
  state.s[0] = triggerSetNow ? 1.0f : 0.0f;
  state.s[1] = triggerClearNow ? 1.0f : 0.0f;

  rn.extOut[0] = value;  // CurrentValue mirrors the Value input (cs:53 sets it only on set, but the
                         // slot's last value persists; mirroring keeps the output live-readable).

  // Resolve which curve to edit (its def-layer identity), via the SAME cross-connection path as read.
  std::string curveRef, symId;
  const Curve* found =
      curveForUpstream(g, rn, ctx, opIndex, curveIndex, &curveRef, &symId);
  if (!found || (!triggeredSet && !triggeredClear)) return true;  // no edge / no target → no write

  // Re-fetch a MUTABLE handle to the same curve. resolveRef is const; go through the non-const
  // Animator::curvesFor by parsing the curveRef we recorded (childId, inputId, index).
  if (!mutableLib) return true;  // no mutable authority (flat/selftest without lib) → honest no-op
  Symbol* sym = mutableLib->find(symId);
  if (!sym) return true;
  int childId = 0, chIdx = 0;
  std::string inputId;
  if (!Animator::parseRef(curveRef, childId, inputId, chIdx)) return true;
  Animator::CurveArray* arr = sym->animator.curvesFor(childId, inputId);
  if (!arr || chIdx < 0 || chIdx >= (int)arr->size()) return true;
  Curve& curve = (*arr)[chIdx];

  if (triggeredSet) {  // cs:50-54: AddOrUpdateV(LocalFxTime, {Value})
    VDefinition v;
    v.value = value;
    curve.addOrUpdate((double)ctx.localFxTime, v);
    rn.extOut[0] = value;
    if (dirtied) *dirtied = true;
  }
  if (triggeredClear) {  // cs:56-65: remove every keyframe
    // Snapshot the times first (removeAt mutates the table; can't erase while iterating it).
    std::vector<double> times;
    times.reserve(curve.table().size());
    for (const auto& kv : curve.table()) times.push_back(kv.first);
    for (double u : times) curve.removeAt(u);
    if (dirtied) *dirtied = true;
  }
  return true;
}

// ONE dispatcher for frame_cook's cook loop: routes FindKeyframes (read, const path) / SetKeyframes
// (write, mutable path) to the right cook, returns true when handled (extOut written). Keeps the
// per-op cooks above individually callable by the goldens. `libDirtied` is OR-ed on a real store write.
bool cookKeyframeReflectionNode(const ResidentEvalGraph& g, ResidentNode& rn,
                                const ResidentEvalCtx& ctx, const std::map<std::string, float>& in,
                                SymbolLibrary* mutableLib, StatefulValueState& state, bool* libDirtied) {
  if (cookFindKeyframesNode(g, rn, ctx, in)) return true;
  if (rn.opType == "SetKeyframes") {
    bool d = false;
    cookSetKeyframesNode(g, rn, ctx, in, mutableLib, state, &d);
    if (d && libDirtied) *libDirtied = true;
    return true;
  }
  return false;
}

namespace {

// Registered step fns = the NOT-connected honest fallback (= TiXL's "No animated operator connected"
// no-op path, FindKeyframes.cs:27-32 / SetKeyframes.cs:20-25). The production cooks above intercept
// with graph+lib context and never reach these when an AnimatedOp connection exists. A caller without
// the resident/graph context (flat rail, generic selftests) sees the passthrough: outputs stay 0.
void stepFindKeyframes(const std::map<std::string, float>&, float, float, StatefulValueState&,
                       float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
}
void stepSetKeyframes(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                      float out[3], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  out[0] = getIn(in, "Value", 0.0f);  // CurrentValue mirrors Value even with nothing connected
}

}  // namespace

static const StatefulOpReg _reg_FindKeyframes{"FindKeyframes", stepFindKeyframes};
static const StatefulOpReg _reg_SetKeyframes{"SetKeyframes", stepSetKeyframes};

}  // namespace sw
