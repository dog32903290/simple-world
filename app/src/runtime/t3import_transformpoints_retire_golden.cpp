// runtime/t3import_transformpoints_retire_golden — 廢棄節點退場 PILOT harness (--selftest-t3-transformpoints-retire).
//
// The keystone pilot for the retirement battle: prove that a flat atom with live references
// ("TransformPoints") can be RETIRED and have its references AUTO-TAKEN-OVER by the nested .t3
// compound, with the replace-in-place seam (graph_bridge.cpp refreshCompoundSpecs name alias +
// findSpec's dynamicSpecs tail). Four gates, each with a MEASURED RED→GREEN tooth. Expected values are
// TiXL constants (the .t3's own Id guid + the .t3ui Position numbers + the TransformMatrix host matrix),
// never sw's own output (GOLDEN_STANDARD 特徵1 / P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5) ────────────────────────────────────────────────────
//  ① TAKEOVER POLARITY (the NEW retirement tooth): with the flat atom retired, findSpec("TransformPoints")
//     falls through every atom sink to the compound's NAME alias → returns the COMPOUND spec (type==the
//     .t3 guid, evaluate==nullptr) and lib[guid] is a non-atomic compound with children. injectBug pushes
//     a stand-in flat atom back into the LIVE point-modify sink → findSpec hits it FIRST (sinks precede
//     the dynamicSpecs tail) → returns the ATOM (type=="TransformPoints") → the compound assertion is
//     falsified → BITE. This proves the name-fallback is truly LAST: while the atom lives it wins (zero
//     behaviour change), only its removal hands the reference to the compound.
//  ② PARITY (delegate): the keystone .t3 replay golden runT3TransformPointsParity — TransformPoints.t3
//     import→buildEvalGraph→cookResident readback vs the焊死 host-matrix oracle (independent .cs math).
//  ③ REFERENCE REACHABILITY + COOKABILITY (the NEW retirement tooth): the NAME reference resolves to a
//     spec carrying the compound's boundary (Points in + Points out) AND buildEvalGraph on the resolved
//     guid flattens to a NON-EMPTY resident graph (the reference reaches a real, buildable compound —
//     makeNode("TransformPoints") is no longer nullptr). injectBug drops the compound's registration
//     (setDynamicSpecs({})) → findSpec is nullptr → the reference is dead → BITE.
//  ④ LAYOUT (delegate): the .t3ui layout golden runT3LayoutGolden — imported children/pins land on their
//     .t3ui Position constants; injectBug reverts all to 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1) so
// --bite's NO-BITE list catches a dead tooth instead of a false PASS.
//
// ZONE: runtime golden (shell tier — binds the importer + refreshCompoundSpecs seam + the two母本 goldens).
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"        // SymbolLibrary / Symbol / SymbolChild
#include "runtime/graph.h"                 // findSpec / NodeSpec / PortSpec / setDynamicSpecs / registerBuiltinPointOps
#include "runtime/graph_bridge.h"          // refreshCompoundSpecs
#include "runtime/point_modify_op_registry.h"  // pointModifySpecSink (① inject the stand-in atom)
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / buildEvalGraph (③ cookability)
#include "runtime/t3_import.h"             // importT3Symbol + runT3TransformPointsParity + runT3LayoutGolden

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kTransformPointsT3 =
#include "runtime/transformpoints_t3_embed.inc"
;

// Expected constants — the .t3's OWN Id (independent of any sw output; the retirement pins the reference
// onto THIS guid via the compound's Symbol.name == "TransformPoints").
const char* const kGuid = "7f6c64fe-ca2e-445e-a9b4-c70291ce354e";
const char* const kName = "TransformPoints";

// Import TransformPoints.t3 and register its name alias into the LIVE dynamicSpecs (mirrors the boot
// path: catalog_boot loads the .t3 → frame_cook calls refreshCompoundSpecs). Returns true iff the root
// symbol is the expected guid.
bool importAndRegister(SymbolLibrary& lib) {
  std::string rootId;
  std::vector<std::string> warnings;
  if (!importT3Symbol(kTransformPointsT3, lib, &rootId, &warnings)) return false;
  refreshCompoundSpecs(lib);  // seeds guid + name("TransformPoints") alias into the global dynamicSpecs
  return rootId == std::string(kGuid);
}

// A stand-in for the RETIRED flat atom — pushed into the live sink only for the ① injectBug, to prove
// an atom sink shadows the name alias (the retirement polarity). Ports match the old atom's I/O shape.
NodeSpec standInFlatAtomSpec() {
  NodeSpec s;
  s.type = kName;
  s.title = kName;
  s.category = "point.transform";
  s.ports = {{"points", "points", "Points", true}, {"out", "out", "Points", false}};
  s.evaluate = nullptr;
  return s;
}

void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0;
  for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}

}  // namespace

int runT3TransformPointsRetireGates(bool injectBug) {
  registerBuiltinPointOps();  // the compound's atom children (ComputeShaderStage etc.) must be findSpec-able

  SymbolLibrary lib;
  if (!importAndRegister(lib)) {
    printf("[tp-retire] FAIL: importT3Symbol(TransformPoints.t3) failed or wrong root id\n");
    return 1;
  }

  // ===== GATE ① TAKEOVER POLARITY =====
  const NodeSpec* g1spec = findSpec(kName);
  const Symbol* csym = lib.find(kGuid);
  const bool g1green = g1spec && g1spec->type == std::string(kGuid) && g1spec->evaluate == nullptr &&
                       csym && !csym->atomic && !csym->children.empty();
  bool g1bit = false;
  if (injectBug) {
    pointModifySpecSink().push_back(standInFlatAtomSpec());
    const NodeSpec* shadowed = findSpec(kName);
    g1bit = shadowed && shadowed->type == std::string(kName);  // the atom masked the compound alias
    pointModifySpecSink().pop_back();
  }
  printf("[tp-retire] ①takeover: findSpec(\"%s\")->type=%s (want guid %s) atomic=%d children=%d -> %s\n",
         kName, g1spec ? g1spec->type.c_str() : "<null>", kGuid, csym ? (int)csym->atomic : -1,
         csym ? (int)csym->children.size() : -1,
         injectBug ? (g1bit ? "BITES (atom shadows compound)" : "TOOTHLESS") : (g1green ? "GREEN" : "RED"));

  // ===== GATE ③ REFERENCE REACHABILITY + COOKABILITY =====
  bool g3green = false;
  size_t g3nodes = 0;
  int g3in = 0, g3out = 0;
  if (g1spec) {
    countPorts(*g1spec, g3in, g3out);                          // the compound's boundary I/O shape
    ResidentEvalGraph eg = buildEvalGraph(lib, g1spec->type);  // name → guid → flatten
    g3nodes = eg.nodes.size();
    g3green = g3in > 0 && g3out > 0 && g3nodes > 0;             // resolves to a wired-in/out, buildable compound
  }
  bool g3bit = false;
  if (injectBug) {
    setDynamicSpecs({});  // strip the compound registration → the name alias is gone
    g3bit = (findSpec(kName) == nullptr);
    refreshCompoundSpecs(lib);  // restore for cleanliness
  }
  printf("[tp-retire] ③reference: name resolves (in=%d out=%d ports) + buildEvalGraph nodes=%zu -> %s\n",
         g3in, g3out, g3nodes, injectBug ? (g3bit ? "BITES (name unresolved w/o alias)" : "TOOTHLESS")
                            : (g3green ? "GREEN" : "RED"));

  // ===== GATE ② PARITY (delegate to the keystone .t3 replay golden) =====
  const int g2 = runT3TransformPointsParity(injectBug);
  const bool g2green = (g2 == 0);
  const bool g2bit = (g2 != 0);
  printf("[tp-retire] ②parity: runT3TransformPointsParity -> %s\n",
         injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ===== GATE ④ LAYOUT (delegate to the .t3ui layout golden) =====
  const int g4 = runT3LayoutGolden(injectBug);
  const bool g4green = (g4 == 0);
  const bool g4bit = (g4 != 0);
  printf("[tp-retire] ④layout: runT3LayoutGolden -> %s\n",
         injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});  // leave no residue in the global spec table

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[tp-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  // -bug leg: EVERY gate's tooth must bite. A dead tooth → return 0 so --bite's NO-BITE list surfaces it
  // (a vacuous exit-1 would let a silently-broken gate masquerade as a live one — GOLDEN_STANDARD P1).
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[tp-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
