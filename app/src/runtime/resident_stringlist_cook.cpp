// runtime/resident_stringlist_cook — cookResidentStringList: the PRODUCTION (resident-path) gather for
// the STRINGLIST currency (host List<string> cook flow = TiXL Slot<List<string>>). Sub-seam A.
//
// The string-rail twin of cookResidentColorList (resident_colorlist_cook.cpp) over std::string. It cooks
// ONE upstream StringList-producing resident node (SplitString, …) by gathering its inputs THROUGH the
// resident Connection drivers (the SAME walk the flat stringlist cook does over flat connections), runs
// the op's StringListCookFn, and returns the host string list. There is NO per-frame cookStringListNodes
// pass: a StringList is only ever CONSUMED by a String producer (JoinStringList), whose cook
// (cookResidentString) recurses into THIS fn on demand — exactly how cookResidentColorList's future
// combiner recursion works. So the StringList is gathered inline, frame_cook.cpp stays untouched.
//
// WHY R-2 SOUND (no flat-only self-deception): the ONLY consumer (JoinStringList) is itself a String
// producer that the EXISTING cookStringNodes per-frame pass drives in production — and that pass now
// recurses into cookResidentString → cookResidentStringList for the StringList input. So a wired
// SplitString→JoinStringList graph evaluates the REAL split list on the production resident path, not a
// flat-only golden artefact (the golden LEG 36 drives exactly this production chain).
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h + the stringlist &
//   string registries — all runtime).
#include "runtime/resident_eval_graph.h"

#include <map>
#include <string>
#include <vector>

#include "runtime/eval_context.h"             // EvaluationContext (StringListCookCtx::ctx)
#include "runtime/graph.h"                     // NodeSpec / PortSpec / findSpec
#include "runtime/stringlist_op_registry.h"  // StringListCookFn / StringListCookCtx / findStringListOp

namespace sw {

namespace {

constexpr int kResidentStringListDepthCap = 64;  // same cycle guard as evalResidentFloat / cookResident

}  // namespace

// Cook ONE upstream StringList-producing resident node into `out` (host string list), gathering its inputs
// THROUGH the resident graph. Mirror of cookResidentColorList but walking std::string:
//   • a "StringList" input port → follow each Connection driver (primary + extraConns, wire order) and
//     recurse this same fn into a gathered list per wire (a future StringList combiner; SplitString has
//     none — it produces FROM a String input, gathered via cookResidentString below);
//   • each "String" input port → follow the Connection driver (wired upstream string via cookResidentString)
//     or fall back to n->strInputs[port.id] (the wire-OR-const dual identity, byte-identical to the flat
//     gather) — SplitString's String + Split inputs ride here, in spec port order.
// Returns false if `path` is not a StringList producer / unknown (caller treats as an empty list).
bool cookResidentStringList(const ResidentEvalGraph& g, const std::string& path,
                            const ResidentEvalCtx& ctx, std::vector<std::string>& out, int depth) {
  out.clear();
  if (depth > kResidentStringListDepthCap) return false;
  const ResidentNode* n = g.node(path);
  if (!n) return false;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return false;
  const StringListCookFn* fn = findStringListOp(n->opType);
  if (!fn || !*fn) return false;

  std::vector<std::string> inputStrings;             // upstream String inputs (SplitString.String / .Split)
  std::vector<std::vector<std::string>> inputLists;  // upstream StringList inputs (future combiner)
  for (const PortSpec& port : s->ports) {
    if (!port.isInput) continue;
    const ResidentInput* ri = n->input(port.id);
    if (port.dataType == "String") {
      // Wire-OR-const, exactly as cookResidentString gathers a String input port (one entry per port; a
      // MultiInput String port would yield one per wire, but SplitString's two String inputs are single).
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::string up;
        cookResidentString(g, ri->srcNodePath, ctx, up, depth + 1);  // wired upstream string (port-0 only)
        inputStrings.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::string ue;
            cookResidentString(g, ec.first, ctx, ue, depth + 1);
            inputStrings.push_back(std::move(ue));
          }
        }
      } else if (!port.multiInput) {
        auto it = n->strInputs.find(port.id);
        inputStrings.push_back(it != n->strInputs.end() ? it->second : std::string{});
      }
    } else if (port.dataType == "StringList") {
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<std::string> up;
        cookResidentStringList(g, ri->srcNodePath, ctx, up, depth + 1);
        inputLists.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::vector<std::string> ue;
            cookResidentStringList(g, ec.first, ctx, ue, depth + 1);
            inputLists.push_back(std::move(ue));
          }
        }
      }
    }
  }

  // RESOLVED Float params of THIS node — the SAME value spine the flat path supplies (mirror of the
  // sibling resident cooks). SplitString reads none (empty map → byte-identical).
  std::map<std::string, float> params = resolveResidentFloatInputs(g, *n, ctx);

  EvaluationContext gpuCtx{};
  gpuCtx.frameIndex = ctx.frameIndex;
  gpuCtx.time = ctx.localFxTime;  // symmetry with the sibling cooks (stringlist ops are time-independent)
  gpuCtx.deltaTime = 0.0f;

  StringListCookCtx sc;
  sc.dev = nullptr; sc.lib = nullptr; sc.queue = nullptr;  // host-only stringlist ops ignore these
  sc.ctx = &gpuCtx;
  sc.nodeId = 0;
  sc.inputStrings = &inputStrings;
  sc.inputLists = &inputLists;
  sc.output = &out;
  sc.params = &params;
  (*fn)(sc);  // computes the list; stringListInjectBug() (golden teeth) corrupts it IN the cook
  return true;
}

}  // namespace sw
