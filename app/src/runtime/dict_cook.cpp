// runtime/dict_cook — impl of the FLAT + RESIDENT Dict<float> PRODUCER cooks (dict-currency seam).
// See dict_cook.h for the contract. Mirrors cookResidentFloatList's gather discipline (spec port order;
// Float MultiInput → wire-declaration order; other dataTypes read as resolved constants).
#include "runtime/dict_cook.h"

#include <map>
#include <string>
#include <vector>

#include "runtime/dict_op_registry.h"     // DictCookCtx / DictCookFn / findDictOp
#include "runtime/eval_context.h"         // EvaluationContext
#include "runtime/graph.h"                // Graph/Node/NodeSpec/PortSpec/Connection/pinId/pinNode/findSpec/evalFloat
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph/ResidentNode/ResidentInput/evalResidentFloat/resolveResidentFloatInputs

namespace sw {

bool cookFlatDict(const Graph& g, const EvaluationContext& ctx, int id, SwFloatDict& out) {
  out.entries.clear();
  const Node* n = g.node(id);
  if (!n) return false;
  const NodeSpec* s = findSpec(n->type);
  if (!s) return false;
  const DictCookFn* fn = findDictOp(n->type);
  if (!fn || !*fn) return false;

  // Gather inputs in spec port order: "String" ports → wire-OR-const (here: const from strParams/strDef,
  // resident-parity — a Dict producer's keys are static labels); "Float" MultiInput ports → all wired
  // scalar sources aggregated into ONE list via evalFloat, wire-declaration order (mirror of the FloatList
  // gather in point_graph_hostvalue_cook.cpp / cookResidentFloatList).
  std::vector<std::string> inputStrings;
  std::vector<std::vector<float>> inputLists;
  for (size_t i = 0; i < s->ports.size(); ++i) {
    const PortSpec& port = s->ports[i];
    if (!port.isInput) continue;
    const int inPin = pinId(id, (int)i);
    if (port.dataType == "String") {
      // Wire-OR-const: keep the resident-safe constant path (String Connection wires do not survive the
      // resident flatten, so a wired Keys would be flat-only). Read the stored strParams override else the
      // spec strDef — identical to how gatherStringInputs resolves an unwired single String port.
      std::string val = port.strDef;
      auto it = n->strParams.find(port.id);
      if (it != n->strParams.end()) val = it->second;
      inputStrings.push_back(val);
    } else if (port.dataType == "Float" && port.multiInput) {
      std::vector<float> scalars;
      for (const Connection& c : g.connections)
        if (c.toPin == inPin) scalars.push_back(evalFloat(g, c.fromPin, ctx));
      inputLists.push_back(std::move(scalars));
    }
  }

  DictCookCtx dc;
  dc.ctx = &ctx;
  dc.nodeId = id;
  dc.inputStrings = &inputStrings;
  dc.inputLists = &inputLists;
  dc.params = nullptr;
  dc.output = &out;
  (*fn)(dc);
  return true;
}

bool cookResidentDict(const ResidentEvalGraph& g, const std::string& path,
                      const ResidentEvalCtx& ctx, SwFloatDict& out) {
  out.entries.clear();
  const ResidentNode* n = g.node(path);
  if (!n) return false;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return false;
  const DictCookFn* fn = findDictOp(n->opType);
  if (!fn || !*fn) return false;

  // Gather inputs in spec port order (mirror cookResidentFloatList): "String" → resolved constant from
  // the resident node's strInputs (String wires do not survive flatten); "Float" MultiInput → aggregate
  // all wired scalar sources via evalResidentFloat, primary then extraConns (wire-declaration order).
  std::vector<std::string> inputStrings;
  std::vector<std::vector<float>> inputLists;
  for (const PortSpec& port : s->ports) {
    if (!port.isInput) continue;
    if (port.dataType == "String") {
      std::string val = port.strDef;
      auto it = n->strInputs.find(port.id);
      if (it != n->strInputs.end()) val = it->second;
      inputStrings.push_back(val);
    } else if (port.dataType == "Float" && port.multiInput) {
      std::vector<float> scalars;
      const ResidentInput* ri = n->input(port.id);
      if (ri && ri->driver == ResidentInput::Driver::Connection) {
        scalars.push_back(evalResidentFloat(g, ri->srcNodePath, ri->srcSlotId, ctx));
        for (const auto& ec : ri->extraConns)
          scalars.push_back(evalResidentFloat(g, ec.first, ec.second, ctx));
      }
      inputLists.push_back(std::move(scalars));
    }
  }

  EvaluationContext ec{};
  ec.frameIndex = ctx.frameIndex;
  ec.time = ctx.localFxTime;
  ec.deltaTime = 0.0f;
  ec.localFxTime = ctx.localFxTime;
  std::map<std::string, float> params = resolveResidentFloatInputs(g, *n, ctx);

  DictCookCtx dc;
  dc.ctx = &ec;
  dc.nodeId = 0;
  dc.inputStrings = &inputStrings;
  dc.inputLists = &inputLists;
  dc.params = &params;
  dc.output = &out;
  (*fn)(dc);
  return true;
}

}  // namespace sw
