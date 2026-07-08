// runtime/t3import_combinebuffers_retire_golden — 廢棄節點退場 PILOT #2 harness
// (--selftest-t3-combinebuffers-retire).
//
// The SECOND retirement pilot: prove the replace-in-place seam is NOT TransformPoints-specific. The flat
// CombineBuffers point atom (retired in this same commit) has its human-name references AUTO-TAKEN-OVER by
// the nested .t3 compound (assets/catalog_t3/CombineBuffers.t3, guid 4dd8a618…) via the SAME machinery
// TransformPoints used: graph_bridge.cpp refreshCompoundSpecs name alias + findSpec's dynamicSpecs tail.
// Four gates, each with a MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3's own Id
// guid + the .t3ui Position numbers), never sw's own output (GOLDEN_STANDARD 特徵1 / P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5) ────────────────────────────────────────────────────
//  ① TAKEOVER POLARITY: with the flat atom retired, findSpec("CombineBuffers") falls through every atom
//     sink to the compound's NAME alias → returns the COMPOUND spec (type==the .t3 guid, evaluate==nullptr)
//     and lib[guid] is a non-atomic compound with children. injectBug pushes a stand-in flat atom back into
//     a LIVE atom sink → findSpec hits it FIRST (sinks precede the dynamicSpecs tail) → returns the ATOM
//     (type=="CombineBuffers") → the compound assertion is falsified → BITE. Proves the name-fallback is
//     truly LAST: while the atom lives it wins (zero behaviour change), only its removal hands the reference
//     to the compound.
//  ② PARITY (delegate): the keystone .t3 replay golden runT3CombineBuffersParity — CombineBuffers.t3
//     import→buildEvalGraph→cookResident readback vs an INDEPENDENT concatenation oracle (bag0++bag1++bag2).
//  ③ REFERENCE REACHABILITY + COOKABILITY: the NAME reference resolves to a spec carrying the compound's
//     boundary (Input in + OutBuffer out) AND buildEvalGraph on the resolved guid flattens to a NON-EMPTY
//     resident graph (makeNode("CombineBuffers") is no longer nullptr). injectBug drops the compound's
//     registration (setDynamicSpecs({})) → findSpec is nullptr → the reference is dead → BITE.
//  ④ LAYOUT: import CombineBuffers.t3 + its sibling CombineBuffers.t3ui through the production layout seam →
//     the mapped _ExecuteCombineBuffers child + both boundary pins land on their .t3ui Position constants
//     (non-zero and mutually distinct). injectBug flips t3LayoutDisable() → every position reverts to 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1) so
// --bite's NO-BITE list catches a dead tooth instead of a false PASS.
//
// ZONE: runtime golden (shell tier — binds the importer + refreshCompoundSpecs seam + the母本 parity golden).
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"        // SymbolLibrary / Symbol / SymbolChild / SlotDef / childById
#include "runtime/graph.h"                 // findSpec / NodeSpec / PortSpec / setDynamicSpecs
#include "runtime/graph_bridge.h"          // refreshCompoundSpecs
#include "runtime/point_modify_op_registry.h"  // pointModifySpecSink (① inject the stand-in atom)
#include "runtime/resident_eval_graph.h"   // ResidentEvalGraph / buildEvalGraph (③ cookability)
#include "runtime/t3_import.h"             // importT3Symbol + runT3CombineBuffersParity + layout seam

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kCombineBuffersT3 =
#include "runtime/combinebuffers_t3_embed.inc"
;
static const char* kCombineBuffersT3ui =
#include "runtime/combinebuffers_t3ui_embed.inc"
;

// Expected constants — the .t3's OWN Id (independent of any sw output; the retirement pins the reference
// onto THIS guid via the compound's Symbol.name == "CombineBuffers").
const char* const kGuid = "4dd8a618-eb3b-40af-9851-89c50683d83e";
const char* const kName = "CombineBuffers";

// .t3ui Position constants (quoted verbatim from CombineBuffers.t3ui — never sw's own output).
const char* const kExecChildId = "147e4f54-b089-4459-8d22-30ccd3c6fdc7";  // _ExecuteCombineBuffers
const char* const kInputDefId   = "b5d25dfd-5d9f-4b5b-b3f5-36b93b13cba3"; // Input boundary
const char* const kOutputDefId  = "e113f77f-53fe-4b29-95df-2f75e36eb251"; // OutBuffer boundary
constexpr float kExecX  = 650.5037f,  kExecY  = 630.7241f;    // .t3ui SymbolChildUis _ExecuteCombineBuffers
constexpr float kInX    = 225.60156f, kInY    = 525.69403f;   // .t3ui InputUis Input
constexpr float kOutX   = 979.87415f, kOutY   = 314.08612f;   // .t3ui OutputUis OutBuffer
constexpr float kEps = 0.01f;

bool nearf(float a, float b) { return std::fabs(a - b) < kEps; }

// Import CombineBuffers.t3 and register its name alias into the LIVE dynamicSpecs (mirrors the boot path:
// catalog_boot loads the .t3 → frame_cook calls refreshCompoundSpecs). Returns true iff root == kGuid.
bool importAndRegister(SymbolLibrary& lib) {
  std::string rootId;
  std::vector<std::string> warnings;
  if (!importT3Symbol(kCombineBuffersT3, lib, &rootId, &warnings)) return false;
  refreshCompoundSpecs(lib);  // seeds guid + name("CombineBuffers") alias into the global dynamicSpecs
  return rootId == std::string(kGuid);
}

// A stand-in for the RETIRED flat atom — pushed into the live sink only for the ① injectBug, to prove an
// atom sink shadows the name alias (the retirement polarity). Ports match the old atom's I/O shape.
NodeSpec standInFlatAtomSpec() {
  NodeSpec s;
  s.type = kName;
  s.title = kName;
  s.category = "point.combine";
  s.ports = {{"input0", "input0", "Points", true}, {"out", "out", "Points", false}};
  s.evaluate = nullptr;
  return s;
}

void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0;
  for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}

// ④ LAYOUT gate — inline (the母本 runT3LayoutGolden is TransformPoints-specific). Import the .t3 + .t3ui
// through the production layout seam and assert the mapped child + both boundary pins land on their .t3ui
// constants. injectBug flips t3LayoutDisable() → the read no-ops → every position reverts to 0,0.
// Returns 0 on the expected outcome (non-bug: match; bug: all 0,0), 1 on divergence.
int runLayoutGate(bool injectBug) {
  std::string cbId;
  if (!symbolIdOfT3(kCombineBuffersT3, &cbId) || cbId != std::string(kGuid)) {
    printf("[cb-retire] ④layout FAIL: symbolIdOfT3 mismatch (got %s)\n", cbId.c_str());
    return 1;
  }
  const T3LayoutResolver layoutResolve = [&cbId](const std::string& guid, std::string& out) -> bool {
    if (guid != cbId) return false;
    out = kCombineBuffersT3ui;
    return true;
  };

  t3LayoutDisable() = injectBug;  // -bug: the read no-ops even though layoutResolve has a real hit
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  const bool ok =
      importT3Symbol(kCombineBuffersT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[cb-retire] ④layout FAIL: import bad id\n"); return 1; }

  Symbol* cb = lib.find(rootId);
  if (!cb) { printf("[cb-retire] ④layout FAIL: root symbol missing\n"); return 1; }

  // The mapped child (ComputeShader folds → only _ExecuteCombineBuffers remains).
  const SymbolChild* exec = nullptr;
  for (const SymbolChild& c : cb->children) if (c.id == 1) exec = &c;  // Children[0] → childId 1
  const SlotDef* inDef = nullptr; for (const SlotDef& d : cb->inputDefs)  if (d.id == kInputDefId)  inDef  = &d;
  const SlotDef* outDef = nullptr; for (const SlotDef& d : cb->outputDefs) if (d.id == kOutputDefId) outDef = &d;
  if (!exec || !inDef || !outDef) {
    printf("[cb-retire] ④layout FAIL: exec=%p inDef=%p outDef=%p (children=%d)\n", (void*)exec,
           (void*)inDef, (void*)outDef, (int)cb->children.size());
    return 1;
  }

  const bool execOk = injectBug ? (nearf(exec->x, 0) && nearf(exec->y, 0))
                                : (nearf(exec->x, kExecX) && nearf(exec->y, kExecY));
  const bool inOk = injectBug ? (nearf(inDef->x, 0) && nearf(inDef->y, 0))
                              : (nearf(inDef->x, kInX) && nearf(inDef->y, kInY));
  const bool outOk = injectBug ? (nearf(outDef->x, 0) && nearf(outDef->y, 0))
                               : (nearf(outDef->x, kOutX) && nearf(outDef->y, kOutY));
  // did-not-trip guard: non-bug must see non-zero, DISTINCT positions (else a never-writing importer passes
  // vacuously). All three points are distinct in the .t3ui.
  const bool distinct = !(nearf(exec->x, inDef->x) && nearf(exec->y, inDef->y)) &&
                        !(nearf(exec->x, outDef->x) && nearf(exec->y, outDef->y)) &&
                        !(nearf(inDef->x, outDef->x) && nearf(inDef->y, outDef->y));
  const bool nonZero = !(nearf(exec->x, 0) && nearf(exec->y, 0));
  printf("[cb-retire] ④layout: exec(%.4f,%.4f want %.4f,%.4f) in(%.4f,%.4f want %.4f,%.4f) "
         "out(%.4f,%.4f want %.4f,%.4f)\n",
         exec->x, exec->y, injectBug ? 0.f : kExecX, injectBug ? 0.f : kExecY, inDef->x, inDef->y,
         injectBug ? 0.f : kInX, injectBug ? 0.f : kInY, outDef->x, outDef->y, injectBug ? 0.f : kOutX,
         injectBug ? 0.f : kOutY);

  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[cb-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (execOk && inOk && outOk) ? 0 : 1;  // GREEN iff all three landed on their .t3ui constants
  }
  return (execOk && inOk && outOk) ? 0 : 1;  // -bug: 0 == everything collapsed to 0,0 (bug reproduced)
}

}  // namespace

int runT3CombineBuffersRetireGates(bool injectBug) {
  registerBuiltinPointOps();  // the compound's atom child (_ExecuteCombineBuffers is a static BufferOp) +
                              // sink population must be live for findSpec / buildEvalGraph.

  SymbolLibrary lib;
  if (!importAndRegister(lib)) {
    printf("[cb-retire] FAIL: importT3Symbol(CombineBuffers.t3) failed or wrong root id\n");
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
  printf("[cb-retire] ①takeover: findSpec(\"%s\")->type=%s (want guid %s) atomic=%d children=%d -> %s\n",
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
  printf("[cb-retire] ③reference: name resolves (in=%d out=%d ports) + buildEvalGraph nodes=%zu -> %s\n",
         g3in, g3out, g3nodes, injectBug ? (g3bit ? "BITES (name unresolved w/o alias)" : "TOOTHLESS")
                            : (g3green ? "GREEN" : "RED"));

  // ===== GATE ② PARITY (delegate to the keystone .t3 replay golden) =====
  const int g2 = runT3CombineBuffersParity(injectBug);
  const bool g2green = (g2 == 0);
  const bool g2bit = (g2 != 0);
  printf("[cb-retire] ②parity: runT3CombineBuffersParity -> %s\n",
         injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ===== GATE ④ LAYOUT (inline — .t3ui Position constants) =====
  const int g4 = runLayoutGate(injectBug);
  const bool g4green = (g4 == 0);
  const bool g4bit = (g4 != 0);
  printf("[cb-retire] ④layout: runLayoutGate -> %s\n",
         injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});  // leave no residue in the global spec table

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[cb-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n",
           green ? "PASS (retirement takeover LIVE — non-TransformPoints)" : "FAIL", g1green, g3green,
           g2green, g4green);
    return green ? 0 : 1;
  }
  // -bug leg: EVERY gate's tooth must bite. A dead tooth → return 0 so --bite's NO-BITE list surfaces it
  // (a vacuous exit-1 would let a silently-broken gate masquerade as a live one — GOLDEN_STANDARD P1).
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[cb-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
