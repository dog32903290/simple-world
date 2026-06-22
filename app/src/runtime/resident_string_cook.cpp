// runtime/resident_string_cook — cookStringNodes: the PRODUCTION (resident-path) cook for the STRING
// currency (host std::string cook flow = TiXL Slot<string>). The resident twin of the flat
// cookStringNode branch (point_graph.cpp) — and the leg that actually LIVES in the running app.
//
// WHY THIS FILE EXISTS (the R-2 self-deception it repairs — task_32b5b6e5, the 柏為-approved P0 gate;
// see resident_host_scalar_cook.cpp:1-12 / resident_colorlist_cook.cpp:1-22 for the canonical trap):
//   The flat string-rail (b247602) is FLAT-ONLY: cookStringNode only runs when a String op is the
//   TERMINAL of a flat cook() (the only flat caller is a golden). Production renders via
//   PointGraph::cookResident + the resident eval graph (frame_cook.cpp). And the resident FLATTEN
//   originally DROPPED every String wire (resident_eval_flatten.cpp: String slots resolved a const
//   into strInputs and `continue`d, never projecting a driver) — so NOTHING on the resident path
//   could follow FloatToString.Output → CombineStrings.Input. A String golden that proved ONLY the
//   flat rail (zero production callers) was fake-green: a real graph's String wire evaluated to the
//   downstream's strDef const, never the wired upstream string. resident_host_scalar_cook.cpp even
//   SKIPS StringLength for exactly this reason ("the resident graph drops String wires").
//
//   This file (with the flatten String-driver projection it depends on) closes the gate. The flatten
//   now projects a ResidentInput onto every String slot (Constant when unwired, upgraded to
//   Connection when a String wire feeds it — incl. extraConns for a MultiInput like CombineStrings.
//   Input). This pass walks the resident graph, cooks every String-producer op by gathering its String
//   inputs THROUGH those resident Connection drivers (the SAME walk the flat cookStringNode does over
//   flat connections), runs the op's StringCookFn, and writes the host string onto
//   ResidentNode::extStrOut[outputPortIndex] (a SEPARATE channel — a string is not a float, cannot
//   ride extOut[]). The string-rail golden's resident LEG drives THIS pass (buildEvalGraph →
//   cookStringNodes → read extStrOut), proving the PRODUCTION resident path, not flat-only.
//
// SIMPLER THAN COLORLIST: String ops are STATELESS (no KeepColors-style cross-frame accumulator), so
//   there is no `state` map — each cook is a pure function of its inputs this frame.
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h + the string
//   registry — all runtime). Called from app/frame_cook.cpp once per frame, BEFORE cookHostScalarNodes
//   (producers settle before consumers; StringLength's resident leg recurses this fn inline anyway).
#include "runtime/resident_eval_graph.h"

#include <map>
#include <string>
#include <vector>

#include "runtime/eval_context.h"            // EvaluationContext (StringCookCtx::ctx)
#include "runtime/graph.h"                   // NodeSpec / PortSpec / findSpec
#include "runtime/host_scalar_op_registry.h" // isHostScalarOp (StringLength skip discriminator)
#include "runtime/string_op_registry.h"      // StringCookFn / StringCookCtx / StringState / findStringOp
// Sub-seam A: the string ctx now carries FloatList + StringList inputs. cookResidentFloatList /
// cookResidentStringList are declared in resident_value_cooks.h (included via resident_eval_graph.h).

namespace sw {

namespace {

constexpr int kResidentStringDepthCap = 64;  // same cycle guard as evalResidentFloat / cookResident

}  // namespace

// Cook ONE upstream String-producing resident node into `out` (host string), gathering its inputs
// THROUGH the resident graph. Mirror of the flat cookStringNode but walking ResidentInput drivers
// instead of flat Graph connections:
//   • each "String" input port → follow the Connection driver (primary + extraConns, WIRE order) and
//     recurse this same fn (a MultiInput like CombineStrings.Input yields one gathered string per wire,
//     in wire-declaration order — the load-bearing gather contract); an UNWIRED port falls back to
//     n->strInputs[port.id] (the wire-OR-const dual identity, byte-identical to the flat gather);
//   • the op's Float params (FloatToString.Value, …) ride the resolved Float-param spine.
// Returns false if `path` is not a String producer / unknown (caller treats as an empty string).
//
// MULTI-OUTPUT (Sub-seam B): the optional sinks are the op's extra String / scalar outputs. They are
// nullptr for the RECURSIVE gather (a downstream String input only ever wants this producer's MAIN
// port-0 string — byte-identical to before), and are passed ONLY by the top-level cookStringNodes
// producer loop, which then fans the captured extras onto the right resident channels. Cooking happens
// ONCE; the op fills *output (port 0) + (when present) the sinks in a single StringCookFn call.
bool cookResidentString(const ResidentEvalGraph& g, const std::string& path,
                        const ResidentEvalCtx& ctx, std::string& out, int depth,
                        std::map<int, std::string>* extraStrOut, std::map<int, float>* scalarOut,
                        StringState* state) {
  out.clear();
  if (depth > kResidentStringDepthCap) return false;
  const ResidentNode* n = g.node(path);
  if (!n) return false;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return false;
  const StringCookFn* fn = findStringOp(n->opType);
  if (!fn || !*fn) return false;

  // Gather String inputs in spec port order (mirror cookStringNode's gatherStringInputs loop). Each
  // String input port yields exactly one entry — OR, for a MultiInput String port, one entry PER WIRE
  // (wire-declaration order) and NOTHING when unwired (faithful to GetCollectedTypedInputs — the same
  // dual identity the flat gather has). A single String port wired → the upstream cooked string;
  // unwired → the strInputs const fallback.
  std::vector<std::string> inputStrings;
  for (const PortSpec& port : s->ports) {
    if (!(port.isInput && port.dataType == "String")) continue;
    const ResidentInput* ri = n->input(port.id);
    if (ri && ri->driver == ResidentInput::Driver::Connection) {
      // WIRED: primary Connection first, then extraConns (wire-declaration order). Recurse the upstream
      // String producer per wire (a non-String / unknown upstream → empty string, faithful to the flat
      // `up ? *up : ""`).
      std::string up;
      cookResidentString(g, ri->srcNodePath, ctx, up, depth + 1);  // gather wants port-0 only (no sinks)
      inputStrings.push_back(std::move(up));
      if (port.multiInput) {
        for (const auto& ec : ri->extraConns) {
          std::string ue;
          cookResidentString(g, ec.first, ctx, ue, depth + 1);  // gather wants port-0 only (no sinks)
          inputStrings.push_back(std::move(ue));
        }
      }
    } else if (!port.multiInput) {
      // UNWIRED single String port → the strDef const (resolved into strInputs at flatten time, =
      // override-else-strDef). A MultiInput String port that is unwired contributes NOTHING (faithful
      // to GetCollectedTypedInputs — CombineStrings with no Input wires joins nothing).
      auto it = n->strInputs.find(port.id);
      inputStrings.push_back(it != n->strInputs.end() ? it->second : std::string{});
    }
  }

  // Sub-seam A: gather FloatList + StringList inputs THROUGH the resident drivers (the bridge + the new
  // StringList currency). FloatListToString.Value rides inputFloatLists; JoinStringList.Input rides
  // inputStringLists. Both follow the EXISTING per-type resident gathers (cookResidentFloatList already
  // lives on the production path; cookResidentStringList is the new string-rail twin). An UNWIRED list
  // port contributes no entry (→ empty list → "" per each op's empty guard). Spec port order; a
  // MultiInput list port yields one entry per wire (wire-declaration order).
  std::vector<std::vector<float>> inputFloatLists;        // FloatList currency (the bridge)
  std::vector<std::vector<std::string>> inputStringLists; // StringList currency (the new channel)
  for (const PortSpec& port : s->ports) {
    if (!port.isInput) continue;
    const ResidentInput* ri = n->input(port.id);
    if (port.dataType == "FloatList") {
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<float> up;
        cookResidentFloatList(g, ri->srcNodePath, ctx, up, depth + 1);
        inputFloatLists.push_back(std::move(up));
        if (port.multiInput)
          for (const auto& ec : ri->extraConns) {
            std::vector<float> ue;
            cookResidentFloatList(g, ec.first, ctx, ue, depth + 1);
            inputFloatLists.push_back(std::move(ue));
          }
      }
    } else if (port.dataType == "StringList") {
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<std::string> up;
        cookResidentStringList(g, ri->srcNodePath, ctx, up, depth + 1);
        inputStringLists.push_back(std::move(up));
        if (port.multiInput)
          for (const auto& ec : ri->extraConns) {
            std::vector<std::string> ue;
            cookResidentStringList(g, ec.first, ctx, ue, depth + 1);
            inputStringLists.push_back(std::move(ue));
          }
      }
    }
  }

  // RESOLVED Float params of THIS node — the SAME value spine the flat path's nodeParams supplies
  // (mirror of resident_host_scalar_cook.cpp). FloatToString.Value / IntToString.Value /
  // Vec3ToString.Vector.x/.y/.z ride this; CombineStrings reads none (empty map → byte-identical).
  std::map<std::string, float> params = resolveResidentFloatInputs(g, *n, ctx);

  // Build the transient GPU EvaluationContext for symmetry with the flat cook (string ops ignore it —
  // host currency, no GPU EvaluationContext touched — but the ctx ref rides along like the siblings).
  EvaluationContext gpuCtx{};
  gpuCtx.frameIndex = ctx.frameIndex;
  gpuCtx.time = ctx.localFxTime;  // wall clock (most string ops are time-independent; symmetry)
  gpuCtx.deltaTime = 0.0f;
  // LocalFxTime (BARS) — populated so a TIME-DEPENDENT string op (BuildRandomString's debounce reads
  // `_lastUpdateTime` vs LocalFxTime) sees the real production clock on the resident path. Was left 0
  // before (no string op read it); now mirrors the flat path, which already passes the real ctx. A
  // time-independent string op ignores this field (byte-identical for every incumbent).
  gpuCtx.localFxTime = ctx.localFxTime;

  StringCookCtx sc;
  sc.dev = nullptr; sc.lib = nullptr; sc.queue = nullptr;  // host-only string ops ignore these
  sc.ctx = &gpuCtx;
  sc.nodeId = 0;
  sc.inputStrings = &inputStrings;
  sc.inputFloatLists = &inputFloatLists;    // Sub-seam A: FloatListToString.Value (the bridge)
  sc.inputStringLists = &inputStringLists;  // Sub-seam A: JoinStringList.Input (the StringList currency)
  sc.output = &out;
  sc.params = &params;  // FloatToString/IntToString/Vec3ToString read Value/Vector.* from here
  sc.extraStrOutputs = extraStrOut;  // nullptr for the recursive gather; the producer loop's sinks else
  sc.scalarOutputs = scalarOut;
  // Per-node CROSS-FRAME slot (HasStringChanged's `_lastString`): nullptr for the RECURSIVE upstream
  // gather (those calls omit it → default nullptr; upstream String producers are stateless), supplied
  // ONLY by the cookStringNodes producer loop for the top node it cooks. A stateless op ignores it.
  sc.state = state;
  (*fn)(sc);  // computes the string(s); stringInjectBug() (golden teeth) corrupts it IN the cook
  return true;
}

void cookStringNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx,
                     std::map<std::string, StringState>* state) {
  for (ResidentNode& rn : g.nodes) {
    const StringCookFn* fn = findStringOp(rn.opType);
    if (!fn || !*fn) continue;  // not a string op
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // Find this op's MAIN String OUTPUT port (the channel a downstream consumer / the golden reads).
    // A real String producer has its MAIN String output at the FIRST String output port (port 0 for
    // every ported op: SubString.Result, PickStringPart.Fragments, FilePathParts.Directory). MULTI-OUTPUT
    // (Sub-seam B): we no longer `break` on the first String port — we cook ONCE and FAN every output.
    int mainPortIdx = -1;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      if (!s->ports[i].isInput && s->ports[i].dataType == "String") { mainPortIdx = (int)i; break; }
    }

    // No-String-output StringOps split TWO ways (both have only a scalar/Float output):
    //   • HOST-SCALAR ops (StringLength: registered via registerHostScalarType, STATELESS) are SKIPPED
    //     here and cooked by the host-scalar pass (cookHostScalarNodes / its inline cookResidentString) —
    //     exactly as the flat path treats StringLength (cookStringLength, not cookStringNode).
    //   • STATEFUL scalar-only String ops (HasStringChanged: a StringOp NOT in the host-scalar set, with
    //     a per-node `_lastString`) MUST be cooked HERE — this is the ONLY pass that threads the
    //     cross-frame s_stringState store; the host-scalar pass has no state. We cook it through the SAME
    //     cookResidentString (with the per-path state slot) and fan its bool→Float scalar onto extOut.
    const bool scalarOnly = (mainPortIdx < 0);
    if (scalarOnly && isHostScalarOp(rn.opType)) continue;  // StringLength → host-scalar pass

    // Per-node CROSS-FRAME state slot (HasStringChanged's `_lastString`): supplied ONLY here, ONLY for the
    // top node this loop cooks (recursive upstream gathers inside cookResidentString pass nullptr). Keyed
    // by resident path in the caller-owned s_stringState store; operator[] default-creates an empty
    // StringState (lastString="") on first cook. nullptr store (a stateless / single-frame golden) → no
    // slot threaded (a stateful op then sees frame-0 persistence only). Mirror of cookColorListNodes.
    StringState* st = state ? &(*state)[rn.path] : nullptr;

    // Cook the host string(s) through the resident graph ONCE. cookResidentString gathers the resident
    // Connection drivers (the SAME walk the flat cookStringNode does), runs the op's StringCookFn, and —
    // because we hand it the multi-output sinks + the state slot — captures the op's EXTRA String / scalar
    // outputs (and mutates its cross-frame state) in the SAME single cook. A single-output stateless op
    // leaves the sinks empty and ignores the state slot.
    std::string str;
    std::map<int, std::string> extraStr;
    std::map<int, float> scalarOut;
    cookResidentString(g, rn.path, ctx, str, 0, &extraStr, &scalarOut, st);

    // FAN the outputs onto the right resident channels (keyed by the op's own spec output-port index):
    //   • MAIN String → extStrOut[mainPortIdx] (the downstream-readable / golden channel) — ONLY when the
    //     op HAS a String output (a scalarOnly op like HasStringChanged has none → skip this write).
    //   • EXTRA Strings → extStrOut[portIdx] (FilePathParts' Filename/Extension at ports 1/2).
    //   • SCALAR (Int/bool dissolved to float) → extOut[portIdx] (PickStringPart.TotalCount,
    //     FilePathParts.FileExists, HasStringChanged.HasChanged) — the SAME float channel
    //     cookHostScalarNodes writes, so a downstream Float input reads it the established way. Guard [8].
    if (mainPortIdx >= 0) rn.extStrOut[mainPortIdx] = std::move(str);
    for (auto& kv : extraStr) rn.extStrOut[kv.first] = std::move(kv.second);
    for (auto& kv : scalarOut) {
      if (kv.first >= 0 && kv.first < (int)(sizeof(rn.extOut) / sizeof(rn.extOut[0])))
        rn.extOut[kv.first] = kv.second;
    }
  }
}

}  // namespace sw
