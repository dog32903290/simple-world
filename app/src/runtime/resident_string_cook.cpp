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

#include "runtime/eval_context.h"         // EvaluationContext (StringCookCtx::ctx)
#include "runtime/graph.h"                // NodeSpec / PortSpec / findSpec
#include "runtime/string_op_registry.h"  // StringCookFn / StringCookCtx / findStringOp

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
bool cookResidentString(const ResidentEvalGraph& g, const std::string& path,
                        const ResidentEvalCtx& ctx, std::string& out, int depth) {
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
      cookResidentString(g, ri->srcNodePath, ctx, up, depth + 1);
      inputStrings.push_back(std::move(up));
      if (port.multiInput) {
        for (const auto& ec : ri->extraConns) {
          std::string ue;
          cookResidentString(g, ec.first, ctx, ue, depth + 1);
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

  // RESOLVED Float params of THIS node — the SAME value spine the flat path's nodeParams supplies
  // (mirror of resident_host_scalar_cook.cpp). FloatToString.Value / IntToString.Value /
  // Vec3ToString.Vector.x/.y/.z ride this; CombineStrings reads none (empty map → byte-identical).
  std::map<std::string, float> params = resolveResidentFloatInputs(g, *n, ctx);

  // Build the transient GPU EvaluationContext for symmetry with the flat cook (string ops ignore it —
  // host currency, no GPU EvaluationContext touched — but the ctx ref rides along like the siblings).
  EvaluationContext gpuCtx{};
  gpuCtx.frameIndex = ctx.frameIndex;
  gpuCtx.time = ctx.localFxTime;  // wall clock (string ops are time-independent today; symmetry)
  gpuCtx.deltaTime = 0.0f;

  StringCookCtx sc;
  sc.dev = nullptr; sc.lib = nullptr; sc.queue = nullptr;  // host-only string ops ignore these
  sc.ctx = &gpuCtx;
  sc.nodeId = 0;
  sc.inputStrings = &inputStrings;
  sc.output = &out;
  sc.params = &params;  // FloatToString/IntToString/Vec3ToString read Value/Vector.* from here
  (*fn)(sc);  // computes the string; stringInjectBug() (golden teeth) corrupts it IN the cook
  return true;
}

void cookStringNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    const StringCookFn* fn = findStringOp(rn.opType);
    if (!fn || !*fn) continue;  // not a string op
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // Find this op's MAIN String OUTPUT port (the channel a downstream consumer / the golden reads).
    // StringLength is registered as a StringOp (its stub) but has NO String output (its output is the
    // Float "Length"), so outPortIdx stays -1 → it is SKIPPED here and cooked instead by the host-scalar
    // pass (cookHostScalarNodes / its inline cookResidentString) — exactly as the flat path treats it
    // (cookStringLength, not cookStringNode). A real String producer has exactly one String output.
    int outPortIdx = -1;
    for (size_t i = 0; i < s->ports.size(); ++i) {
      if (!s->ports[i].isInput && s->ports[i].dataType == "String") { outPortIdx = (int)i; break; }
    }
    if (outPortIdx < 0) continue;

    // Cook the host string through the resident graph (cookResidentString gathers the resident
    // Connection drivers — the SAME walk the flat cookStringNode does over flat connections).
    std::string str;
    cookResidentString(g, rn.path, ctx, str, 0);
    rn.extStrOut[outPortIdx] = std::move(str);
  }
}

}  // namespace sw
