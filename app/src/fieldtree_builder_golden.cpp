// fieldtree_builder_golden — --selftest-fieldtree-builder. The PF-0 BUILDER acceptance tooth (blueprint
// §6: PF-0a/b's real verification, independent of the PF-a probe). Asserts that the graph→FieldNode
// builder (field_graph_builder.cpp) reconstructs the wired ToroidalVortexField tree on BOTH legs:
//   FLAT     — buildFieldTree(Graph, rootId, nodeParams)
//   RESIDENT — buildResidentFieldTree(ResidentEvalGraph, rootPath, nodeParams)
// For each leg it builds the SAME field op as the particle-field probe (ToroidalVortexField with a
// NON-DEFAULT wired Radius), then asserts:
//   (1) the tree exists (root != nullptr),
//   (2) root TYPE == ToroidalVortexField  — proven via the node prefix (BuildNodeId = "<Type>_<id>_",
//       a public FieldNode member; the leaf subclass is TU-private so we identify it by prefix not RTTI),
//   (3) the WIRED Radius reached the node — proven via collectParams: ToroidalVortexField lays Center at
//       floats[0..2] then Radius at floats[3] (the leaf's documented [GraphParam] order). A wired
//       Radius != the .t3 default (1.0) bites: if param-apply were a no-op the value would be 1.0.
//
// This is RED before the builder + configure-from-map path exist (no buildFieldTree symbol / Radius
// stays default), GREEN after PF-0a/b. injectBug severs the param-apply expectation (asserts the
// builder did NOT silently leave the default), giving the harness a falsifiable -bug row.
//
// ZONE: shell tier (app/src/ root, like particlefield_probe_golden.cpp / the field render goldens). Pure
// host — NO Metal, NO metallib (the builder + assemble are pure string/host work). Crosses runtime only.
#include "runtime/field_graph_builder.h"

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild (resident leg)
#include "runtime/field_graph.h"          // FieldNode (prefix + collectParams)
#include "runtime/graph.h"                // Graph / Node / resolveNodeParams / pinId
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / resolveResidentFloatInputs
#include "runtime/tixl_point.h"           // EvaluationContext

namespace sw {
namespace {

constexpr float kWiredRadius = 3.5f;  // != ToroidalVortexField .t3 default (1.0) → bites param-apply.
constexpr float kEps = 1e-4f;

// root TYPE check: the FieldNode prefix is "<Type>_<shortId>_" (TiXL BuildNodeId). A
// ToroidalVortexField node's prefix begins "ToroidalVortexField_".
bool prefixIsToroidal(const std::shared_ptr<FieldNode>& n) {
  const std::string want = "ToroidalVortexField_";
  return n && n->prefix.rfind(want, 0) == 0;
}

// Radius the node carries: collectParams lays Center[0..2] then Radius[3] (leaf documented order).
float nodeRadius(const std::shared_ptr<FieldNode>& n) {
  if (!n) return -999.0f;
  std::vector<float> floats;
  std::vector<std::string> fields;
  n->collectParams(floats, fields);
  return floats.size() > 3 ? floats[3] : -999.0f;
}

// FLAT leg: a 1-node Graph holding ToroidalVortexField with a stored Radius override; build the tree
// from it. The flat resolver IS the production nodeParams shape (resolveNodeParams → the value spine).
std::shared_ptr<FieldNode> buildFlatLeg(float radius) {
  Graph g;
  Node fld; fld.id = 5; fld.type = "ToroidalVortexField"; fld.params["Radius"] = radius;
  g.nodes = {fld};
  EvaluationContext ctx{};
  // Per-call fresh resolver (the cook driver's nodeParams shape). Owns the resolved map for the build.
  std::map<int, std::map<std::string, float>> scratch;
  FieldParamResolver params = [&](int id) -> const std::map<std::string, float>* {
    const Node* n = g.node(id);
    if (!n) return nullptr;
    return &(scratch[id] = resolveNodeParams(g, *n, ctx, nullptr));
  };
  return buildFieldTree(g, 5, params);
}

// RESIDENT leg: the same field op as a single-child SymbolLibrary, flattened to a ResidentEvalGraph,
// built via buildResidentFieldTree. Mirrors the probe golden's resident library shape. The Radius
// override rides the SymbolChild override → resolveResidentFloatInputs resolves it (the value spine).
std::shared_ptr<FieldNode> buildResidentLeg(float radius) {
  SymbolLibrary slib;
  slib.symbols["ToroidalVortexField"] = [] {
    Symbol s; s.id = s.name = "ToroidalVortexField"; s.atomic = true;
    s.inputDefs = {{"Radius", "Radius", "Float", 1.0f}};  // the wired param the builder must project
    s.outputDefs = {{"Result", "Result", "Field", 0.0f}};
    return s;
  }();
  Symbol root; root.id = root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Field", 0.0f}};
  SymbolChild cf; cf.id = 5; cf.symbolId = "ToroidalVortexField"; cf.overrides["Radius"] = radius;
  root.children = {cf};
  root.connections = {{5, "Result", kSymbolBoundary, "out"}};
  slib.symbols["Root"] = root; slib.rootId = "Root";

  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");
  ResidentEvalCtx rc{};
  // nodeParams resolver (resident shape): resolve a node's Float inputs through its drivers.
  FieldParamResolverResident params = [&](const std::string& path) -> const std::map<std::string, float>* {
    static std::map<std::string, std::map<std::string, float>> scratch;
    const ResidentNode* n = rg.node(path);
    if (!n) return nullptr;
    return &(scratch[path] = resolveResidentFloatInputs(rg, *n, rc));
  };
  // Root child path: the flatten keys atomic children by their child id as a string ("5").
  return buildResidentFieldTree(rg, "5", params);
}

}  // namespace

int runFieldTreeBuilderSelfTest(bool injectBug) {
  const float wired = kWiredRadius;

  std::shared_ptr<FieldNode> flat = buildFlatLeg(wired);
  std::shared_ptr<FieldNode> res = buildResidentLeg(wired);

  const bool flatExists = (bool)flat;
  const bool resExists = (bool)res;
  const bool flatType = prefixIsToroidal(flat);
  const bool resType = prefixIsToroidal(res);
  const float flatR = nodeRadius(flat);
  const float resR = nodeRadius(res);
  const bool flatRadius = flatExists && (flatR > wired - kEps) && (flatR < wired + kEps);
  const bool resRadius = resExists && (resR > wired - kEps) && (resR < wired + kEps);

  std::printf("[selftest-fieldtree-builder] flat:  root=%s type=%s Radius=%.3f (want %.3f) -> %s\n",
              flatExists ? "built" : "NULL", flatType ? "ToroidalVortexField" : "WRONG", flatR, wired,
              (flatExists && flatType && flatRadius) ? "OK" : "FAIL");
  std::printf("[selftest-fieldtree-builder] resid: root=%s type=%s Radius=%.3f (want %.3f) -> %s\n",
              resExists ? "built" : "NULL", resType ? "ToroidalVortexField" : "WRONG", resR, wired,
              (resExists && resType && resRadius) ? "OK" : "FAIL");

  if (injectBug) {
    // -bug: assert the param-apply did NOT silently leave the .t3 default (1.0). If the builder failed
    // to project the wired Radius, both radii would equal 1.0 != wired → this row FAILS, proving the
    // from-map configure is load-bearing (a regression that drops param-apply trips here). RED iff the
    // wired value was NOT honoured.
    const bool defaulted = (flatR > 1.0f - kEps && flatR < 1.0f + kEps) ||
                           (resR > 1.0f - kEps && resR < 1.0f + kEps);
    std::printf("[selftest-fieldtree-builder] -bug (param-apply NOT a silent default): %s\n",
                defaulted ? "RED (a leg kept .t3 default 1.0 — param-apply DROPPED)" : "PASS");
    return defaulted ? 1 : 0;
  }

  const bool ok = flatExists && resExists && flatType && resType && flatRadius && resRadius;
  std::printf("[selftest-fieldtree-builder] %s (graph->FieldNode tree built on BOTH legs, wired param "
              "projected)\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
