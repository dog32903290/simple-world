// runtime/resident_host_scalar_cook — cookHostScalarNodes: the PRODUCTION (resident-path) cook for the
// FloatList→Float BRIDGE (list-routing seam). This is the resident twin of the flat cookHostScalar
// branch (point_graph.cpp:791) — and it is the leg that actually LIVES in the running app.
//
// WHY THIS FILE EXISTS (the refuter-found self-deception it repairs):
//   The flat cookHostScalar only runs when the host-scalar op is the TERMINAL of a flat cook() (the
//   only flat caller is a golden). Production renders via PointGraph::cookResident + evalResidentFloat
//   (frame_cook.cpp) — and NOTHING on the resident path cooked host-scalar nodes. So a real graph
//   FloatsToList→FloatListLength→Multiply evaluated 0 in the running app (evalResidentFloat reads
//   ResidentNode::extOut for any !evaluate node, but extOut was never written for host-scalar nodes —
//   only AudioReaction/stateful ops had a per-frame extOut writer). The golden proved the FLAT rail,
//   which has zero production callers — the bridge was mechanism-only, not actually wired.
//
//   This file adds the missing per-frame pass: it walks the resident graph, cooks every host-scalar
//   node (FloatListLength / PickFloatFromList) by gathering its upstream FloatList inputs THROUGH THE
//   RESIDENT GRAPH (following the Connection drivers the flatten step DOES project onto FloatList
//   slots — verified: a FloatList wire becomes ResidentInput{Driver::Connection} because FloatList
//   slots, unlike String slots, get a ResidentInput in resident_eval_flatten.cpp:95-126), runs the
//   op's HostScalarCookFn, and writes the scalar into ResidentNode::extOut[outputPortIndex] — the EXACT
//   channel evalResidentFloat already reads (resident_eval_graph.cpp:68-70). Mirror of how
//   cookAudioReactionNodes writes extOut[] each frame (frame_cook.cpp:166-168).
//
// SCOPE — FloatList host-scalar ops (FloatListLength / PickFloatFromList) PLUS the StringLength leg
//   (now LIVE — the resident string-wire rail it waited on landed, task_32b5b6e5). HISTORY: this pass
//   originally SKIPPED StringLength because its String input (FloatToString.Output →
//   StringLength.InputString) is a STRING WIRE that the resident flatten DROPPED — so the resident
//   graph could not follow it, and cooking it would have read only StringLength's strDef constant
//   (the very self-deception this family exists to kill). That gate is now closed: the flatten projects
//   a ResidentInput onto every String slot (Connection when wired) and cookResidentString
//   (resident_string_cook.cpp) walks it. So StringLength's String input is gathered HERE inline via
//   cookResidentString — String-in → .size() → Float-out on extOut[0], mirror of the flat
//   cookStringLength branch (point_graph.cpp). The generic skip-on-String-input guard remains as
//   belt-and-suspenders for any FUTURE String-consuming host-scalar op with a registered cook fn whose
//   resident gather is not yet wired — StringLength is handled by its dedicated branch BEFORE the guard.
//
// PLACEMENT: runtime leaf (pure CPU; depends only on resident_eval_graph.h + graph.h + the two op
//   registries — all runtime). Called from app/frame_cook.cpp once per frame, same slot as
//   cookAudioReactionNodes / cookStatefulValueNodes (ARCHITECTURE: app owns the per-frame orchestration;
//   the compute body lives in runtime).
#include "runtime/resident_eval_graph.h"

#include <map>
#include <string>
#include <vector>

#include "runtime/dict_cook.h"                // cookResidentDict (dict-currency seam: Dict input gather)
#include "runtime/dict_op_registry.h"         // SwFloatDict
#include "runtime/eval_context.h"             // EvaluationContext (HostScalarCookCtx::ctx)
#include "runtime/floatlist_op_registry.h"    // FloatListCookFn / FloatListCookCtx / findFloatListOp
#include "runtime/graph.h"                     // NodeSpec / PortSpec / findSpec
#include "runtime/host_scalar_op_registry.h"  // HostScalarCookFn / HostScalarCookCtx / findHostScalarOp
#include "runtime/resident_value_cooks.h"     // cookResidentFloatList (extracted to resident_floatlist_cook.cpp)
#include "runtime/string_op_registry.h"       // stringInjectBug (StringLength resident leg, the teeth)

namespace sw {

// Shared parse helpers (defined in host_scalar_ops_tryparse.cpp / host_scalar_ops_tryparseint.cpp) so the
// resident TryParse/TryParseInt branches parse byte-identically to their flat cooks — a String-input
// host-scalar op the GENERIC loop below skips (it can only gather FloatList inputs on the resident graph;
// the String wire is gathered via cookResidentString in the dedicated branch, the IndexOf pattern).
bool tryParseFloat(const std::string& s, float& out);
bool tryParseInt32(const std::string& s, int& out);
// StringToDateTime's shared cook math (host_scalar_ops_stringtodatetime.cpp) — same byte-identical
// contract: the dedicated branch below and the flat cook both call this ONE helper.
float stringToDateTimeEpoch(const std::string& dateString);
float valueToRateResult(const std::string& rates, float value);  // host_scalar_ops_valuetorate.cpp

void cookHostScalarNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    // StringLength resident leg (String-in → host scalar → extOut[0]). StringLength registers ONLY its
    // type name into the host-scalar set (its NodeSpec + stub cook live on the String rail), so it has
    // NO HostScalarCookFn — the generic loop below would skip it. It is the resident twin of the flat
    // cookStringLength branch (point_graph.cpp): gather its ONE String input via cookResidentString
    // (now wireable — the resident string-wire rail), take .size(), write the count onto extOut[0] (the
    // channel evalResidentFloat reads for a downstream Float input wired to StringLength.Length). When
    // the upstream String wire is corrupted by stringInjectBug, the cooked upstream string is already
    // shorter (FloatToString drops its last char) → the count is wrong → RED carries through; we ALSO
    // clear it under injectBug to mirror the flat cookStringLength's direct host-scalar tooth.
    if (rn.opType == "StringLength") {
      const NodeSpec* s = findSpec(rn.opType);
      if (!s) continue;
      std::string in;
      for (const PortSpec& port : s->ports) {
        if (port.isInput && port.dataType == "String") {
          const ResidentInput* ri = rn.input(port.id);
          if (ri && ri->driver == ResidentInput::Driver::Connection) {
            cookResidentString(g, ri->srcNodePath, ctx, in, 0);  // WIRED upstream string
          } else {
            auto it = rn.strInputs.find(port.id);                 // UNWIRED → strDef const
            in = (it != rn.strInputs.end()) ? it->second : std::string{};
          }
          break;  // StringLength has exactly one String input ("InputString")
        }
      }
      float len = (float)in.size();
      if (stringInjectBug()) len = 0.0f;  // golden teeth (mirror flat cookStringLength's clear)
      rn.extOut[0] = len;  // Length output port index 0
      continue;
    }
    // IndexOf resident leg (TWO String inputs → host scalar → extOut[0]). IndexOf uses the full
    // HostScalarOp registry (unlike StringLength which has a legacy dedicated driver branch), so
    // findHostScalarOp("IndexOf") returns a real cook fn. However the generic loop below would skip
    // it via the String-input guard. This dedicated branch handles it FIRST:
    //   • Port 1 "OriginalString" → gathers via cookResidentString (wired) or strInputs (const)
    //   • Port 2 "SearchPattern"  → same
    // Computes first-occurrence index (C++ find → -1 on npos), writes onto extOut[0].
    // Teeth: hostScalarInjectBug() writes a sentinel; the golden LEG 25 red case fires on the
    // actual cook path (NOT by flipping expected values — mirror of StringLength / cookIndexOf flat).
    if (rn.opType == "IndexOf") {
      const NodeSpec* s = findSpec(rn.opType);
      if (!s) continue;
      // Gather the two String inputs in spec port order (port 1 = OriginalString, port 2 = SearchPattern).
      std::string strings[2];
      int strIdx = 0;
      for (const PortSpec& port : s->ports) {
        if (!port.isInput || port.dataType != "String") continue;
        if (strIdx >= 2) break;
        const ResidentInput* ri = rn.input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection) {
          cookResidentString(g, ri->srcNodePath, ctx, strings[strIdx], 0);
        } else {
          auto it = rn.strInputs.find(port.id);
          strings[strIdx] = (it != rn.strInputs.end()) ? it->second : std::string{};
        }
        ++strIdx;
      }
      const std::string& original = strings[0];
      const std::string& pattern  = strings[1];
      float idx;
      if (original.empty() || pattern.empty()) {
        idx = -1.0f;
      } else {
        auto pos = original.find(pattern);
        idx = (pos == std::string::npos) ? -1.0f : (float)(int)pos;
      }
      if (hostScalarInjectBug()) idx = -999.0f;  // golden teeth (mirror flat cookIndexOf)
      rn.extOut[0] = idx;  // Index output port index 0
      continue;
    }
    // TryParse / TryParseInt / ValueToRate resident leg (ONE String input + Float params → host scalar
    // → extOut[0]). All three use the full HostScalarOp registry (findHostScalarOp returns a real cook
    // fn), but the generic loop below would skip them via the String-input guard. This dedicated branch
    // handles them FIRST — the resident twin of the flat cookFlatHostScalar (which gathers the String
    // via gatherStringInputs):
    //   • the one String port ("String" / "Rates") → gathered via cookResidentString (wired) or
    //     strInputs (const)
    //   • Float params ("Default" / "Value")       → resolveResidentFloatInputs
    // Computes with the SHARED helpers (byte-identical to the flat cooks), writes extOut[0].
    // Teeth: hostScalarInjectBug() writes a sentinel; the golden red case fires on the actual cook path
    // (NOT by flipping expected values — mirror of StringLength / IndexOf).
    // StringToDateTime rides this SAME branch (identical shape: ONE String input → scalar via the
    // shared leaf helper; no Default param). Its "DateString" is the first String port the generic
    // gather below finds — byte-identical parse to the flat cook via stringToDateTimeEpoch.
    if (rn.opType == "TryParse" || rn.opType == "TryParseInt" || rn.opType == "StringToDateTime" ||
        rn.opType == "ValueToRate") {
      const NodeSpec* s = findSpec(rn.opType);
      if (!s) continue;
      // Gather the ONE String input (port 1 "String") — wired upstream string or strDef const.
      std::string in;
      for (const PortSpec& port : s->ports) {
        if (!(port.isInput && port.dataType == "String")) continue;
        const ResidentInput* ri = rn.input(port.id);
        if (ri && ri->driver == ResidentInput::Driver::Connection) {
          cookResidentString(g, ri->srcNodePath, ctx, in, 0);  // WIRED upstream string
        } else {
          auto it = rn.strInputs.find(port.id);                 // UNWIRED → strDef const
          in = (it != rn.strInputs.end()) ? it->second : std::string{};
        }
        break;  // exactly one String input
      }
      const std::map<std::string, float> params = resolveResidentFloatInputs(g, rn, ctx);
      auto getDef = [&](float d) {
        auto it = params.find("Default");
        return it != params.end() ? it->second : d;
      };
      float result;
      if (rn.opType == "StringToDateTime") {
        result = stringToDateTimeEpoch(in);  // parse-or-0 (fork-stringtodatetime-parsefail-zero)
      } else if (rn.opType == "TryParse") {
        const float def = getDef(0.0f);
        result = def;
        tryParseFloat(in, result);  // leaves result == def on failure
      } else if (rn.opType == "TryParseInt") {  // int dissolve to Float
        const float defF = getDef(0.0f);
        int parsed = (int)(defF >= 0.0f ? (defF + 0.5f) : (defF - 0.5f));
        tryParseInt32(in, parsed);  // leaves parsed == default on failure
        result = (float)parsed;
      } else {  // ValueToRate — pick _ratios[(int)((n-1)*clamp(Value,0,0.99)+0.5)] from the Rates lines
        auto vit = params.find("Value");
        result = valueToRateResult(in, vit != params.end() ? vit->second : 0.5f);
      }
      if (hostScalarInjectBug()) result = -999.0f;  // golden teeth (mirror flat cook)
      rn.extOut[0] = result;  // Result output port index 0
      continue;
    }
    const HostScalarCookFn* fn = findHostScalarOp(rn.opType);
    if (!fn || !*fn) continue;  // not a host-scalar op (or StringLength, handled above).
    const NodeSpec* s = findSpec(rn.opType);
    if (!s) continue;

    // SKIP host-scalar ops with a WIRED String input — the resident graph drops String Connection wires
    // (flatten:100-103), so we cannot faithfully gather a wired String source. A String input driven by a
    // CONSTANT (a resolved strInputs param — e.g. the Select*FromDict "Select" key) IS resolvable and does
    // NOT trip this skip; only a Connection-driven String input does. (Before the Dict seam this was "ANY
    // String input" — narrowed to "wired String input" so the Dict consumers, whose Select key is a
    // constant String param, run on the resident leg. StringLength stays skipped: its InputString is wired.)
    bool hasWiredStringInput = false;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "String")) continue;
      const ResidentInput* ri = rn.input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) { hasWiredStringInput = true; break; }
    }
    if (hasWiredStringInput) continue;

    // Gather FloatList inputs by following the resident Connection drivers (cookResidentFloatList),
    // in spec port order, mirroring the flat cookHostScalar's FloatList gather. An unwired FloatList
    // input contributes NO entry → empty → count/pick 0, matching TiXL null→0.
    std::vector<std::vector<float>> inputLists;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "FloatList")) continue;
      const ResidentInput* ri = rn.input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        std::vector<float> up;
        cookResidentFloatList(g, ri->srcNodePath, ctx, up, 0);
        inputLists.push_back(std::move(up));
        if (port.multiInput) {
          for (const auto& ec : ri->extraConns) {
            std::vector<float> ue;
            cookResidentFloatList(g, ec.first, ctx, ue, 0);
            inputLists.push_back(std::move(ue));
          }
        }
      }
      // (An unwired / Constant FloatList input contributes nothing → empty inputLists → 0.)
    }

    // Gather Dict inputs (dict-currency seam): one cooked SwFloatDict per "Dict" input port (single-wire —
    // Select*FromDict DictionaryInput), following the resident Connection driver via cookResidentDict.
    // Owned in dictStore; nullptr for an unwired port (TiXL null-dict → miss → 0). Mirror of the flat gather.
    std::vector<SwFloatDict> dictStore;
    std::vector<const SwFloatDict*> inputDicts;
    for (const PortSpec& port : s->ports) {
      if (!(port.isInput && port.dataType == "Dict")) continue;
      const ResidentInput* ri = rn.input(port.id);
      const SwFloatDict* cooked = nullptr;
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        dictStore.emplace_back();
        if (cookResidentDict(g, ri->srcNodePath, ctx, dictStore.back())) cooked = &dictStore.back();
      }
      inputDicts.push_back(cooked);
    }

    // Resolved Float params of THIS node (PickFloatFromList.Index rides this) — the SAME value spine
    // the flat path uses (resolveResidentFloatInputs, mirror of flat nodeParams).
    std::map<std::string, float> params = resolveResidentFloatInputs(g, rn, ctx);

    // No WIRED String inputs (guarded above); a constant String param (the Select key) rides strInputs.
    std::vector<std::string> inputStrings;

    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    EvaluationContext gpuCtx{};
    gpuCtx.frameIndex = ctx.frameIndex;
    gpuCtx.time = ctx.localFxTime;  // wall clock (host-scalar ops are time-independent today; symmetry)
    gpuCtx.deltaTime = 0.0f;
    std::map<int, float> scalarOut;  // MULTI-OUTPUT sink (AnalyzeFloatList Min/Max/AverageMean/AllValid)
    HostScalarCookCtx hc;
    hc.dev = nullptr; hc.lib = nullptr; hc.queue = nullptr;
    hc.ctx = &gpuCtx;
    hc.nodeId = 0;
    hc.inputLists = &inputLists;
    hc.inputStrings = &inputStrings;
    hc.inputDicts = &inputDicts;
    hc.params = &params;
    hc.strParams = &rn.strInputs;  // the Select* "Select" key (constant String param, survives flatten)
    hc.output = &cx; hc.outY = &cy; hc.outZ = &cz;
    hc.scalarOutputs = &scalarOut;
    (*fn)(hc);  // computes the component(s); hostScalarInjectBug() (golden teeth) corrupts them IN the cook

    // Write the component(s) onto the resident node's Float output ports (host-scalar layout: output
    // port(s) FIRST — FloatListLength.Length / SelectVec2FromDict.Result.x/.y). A scalar op sets
    // components=1 → extOut[0] only (byte-identical to before); a Vector2/Vector3 op → extOut[0..2].
    // A MULTI-OUTPUT op (AnalyzeFloatList) ALSO fills scalarOutputs[k] for its extra ports → distributed
    // onto extOut[k] (the channel evalResidentFloat reads), bound-guarded by the extOut size.
    rn.extOut[0] = cx;
    if (hc.components >= 2) rn.extOut[1] = cy;
    if (hc.components >= 3) rn.extOut[2] = cz;
    const int kExtN = (int)(sizeof(rn.extOut) / sizeof(rn.extOut[0]));
    for (const auto& kv : scalarOut)
      if (kv.first >= 0 && kv.first < kExtN) rn.extOut[kv.first] = kv.second;
  }
}

}  // namespace sw
