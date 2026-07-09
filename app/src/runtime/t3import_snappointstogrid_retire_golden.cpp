// runtime/t3import_snappointstogrid_retire_golden — 廢棄節點退場 harness
// (--selftest-t3-snappointstogrid-retire).
//
// Retires the flat SnapPointsToGrid point atom: its human-name references are AUTO-TAKEN-OVER by the
// nested .t3 compound (assets/catalog_t3/SnapPointsToGrid.t3, guid bc88304a…) via the replace-in-place
// seam (graph_bridge.cpp refreshCompoundSpecs name alias + findSpec's dynamicSpecs tail). Four gates,
// each a MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3's own Id guid + the
// .t3ui Position numbers) and the mathv-verified SnapPointsToGrid oracle — never sw's own output
// (GOLDEN_STANDARD 特徵1 / P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: with the flat atom retired, findSpec("SnapPointsToGrid") falls through every
//     atom sink to the compound's NAME alias → the COMPOUND spec (type==guid, evaluate==nullptr) and
//     lib[guid] is a non-atomic compound with children. injectBug pushes a stand-in flat atom into a
//     LIVE point-modify sink → findSpec hits it FIRST → returns the ATOM → BITE.
//  ② PARITY (cook-driven, mathv oracle): import SnapPointsToGrid.t3 → buildEvalGraph (骨7 boundary
//     injection feeds the compound's scalar boundary params) → cookResident → read back the
//     ExecuteBufferUpdate output vs mathvRefSnapPointsToGrid (the mathv-verified CPU oracle). This is
//     the retire §8 佈線 focus: proves FloatsToBuffer assembly + boundary injection put the params in
//     the right kernel cbuffer slots + the generic ComputeShaderStage dispatches the ported kernel
//     computeshaderstage_snaptogrid. injectBug PERTURBS the boundary-injected Amount (output depends on
//     it continuously) while the oracle keeps the clean value → readback diverges → BITE. This proves the
//     injected boundary param actually reaches the kernel cbuffer (dead injection → output unchanged → NO-BITE).
//  ③ REFERENCE REACHABILITY + COOKABILITY: the NAME reference resolves to a spec carrying the compound
//     boundary (Points in + Output out) AND buildEvalGraph flattens to a NON-EMPTY resident graph.
//     injectBug drops the compound registration (setDynamicSpecs({})) → findSpec nullptr → BITE.
//  ④ LAYOUT: import .t3 + sibling .t3ui through the production layout seam → boundary pins land on their
//     .t3ui Position constants (non-zero, distinct). injectBug flips t3LayoutDisable() → every pos 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG (deterministic, exact — no transcendental) ─────────────────────────────────────
// The compound's scalar params are BOUNDARY inputs (no in-graph producer when replayed alone). We inject
// via the 骨7 seam. Vector inputs (GridStretch/GridOffset/GainAndBias) wire a SINGLE boundary value onto
// their Vector*Components '.x' head (fork-t3-vec3-wire-lands-on-head), so an injected value V yields
// (V,0,0)/(V,0); the oracle is fed the SAME resolved vectors. With GainAndBias=(0,0) and generic (never
// cell-aligned) fixture positions, ApplyGainAndBias's snapAmount∈(0,1) → biasedSnap==0 EXACTLY on both
// CPU and GPU (getBias(0,x)=x/inf=0 for x<1; the SnapPointsToGrid.hlsl ApplyGainAndBias fork — scalar
// clamped early-out vs float4 unclamped — only diverges at snapAmount==1, which generic points avoid).
// So the whole path is exact float arithmetic (floor/mix), matchable to <1e-3.
//
// ZONE: runtime golden (shell tier — binds the importer + refreshCompoundSpecs seam + the mathv oracle).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"     // BufferOp / BufferCookCtx (fixture producer)
#include "runtime/compound_graph.h"         // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/graph.h"                  // findSpec / NodeSpec / PortSpec / setDynamicSpecs
#include "runtime/graph_bridge.h"           // refreshCompoundSpecs / atomicSymbolFromSpec
#include "runtime/point_modify_op_registry.h"  // pointModifySpecSink (① inject the stand-in atom)
#include "runtime/point_graph.h"            // PointGraph / residentSwBufferFor
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph / initResidentCache / ctx
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/t3_import.h"              // importT3Symbol / t3ImportInjectBug / t3LayoutDisable / symbolIdOfT3
#include "runtime/tixl_point.h"             // SwPoint (64B)
#include "mathv_ref_snappointstogrid.h"     // mathvRefSnapPointsToGrid (mathv-verified CPU oracle)

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/snappointstogrid_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/snappointstogrid_t3ui_embed.inc"
;

// Expected constants — the .t3's OWN Id + the .t3ui Position numbers (never sw output).
const char* const kGuid = "bc88304a-a2c7-4df9-93d8-b7dfecbce3de";
const char* const kName = "SnapPointsToGrid";
// boundary input def guids (compound Inputs) — the 骨7 injection keys.
const char* const kInAmount      = "3e1dbc97-76d9-47da-a12e-1aefa384cf81";
const char* const kInGridScale   = "4d7f1f34-ca1b-43ee-803f-cbc14bcc8679";
const char* const kInScatter     = "a7d80f3e-298d-4eb5-9751-ec432cda4065";  // direct scalar — inject for alignment
const char* const kInGridStretch = "eacc6bf8-1e12-44fb-8541-91ac4a557745";  // vec3 → Value.x head
// .t3ui pins (④): Amount input / Points input / Output — verbatim from SnapPointsToGrid.t3ui.
const char* const kPinAmount = "3e1dbc97-76d9-47da-a12e-1aefa384cf81";
const char* const kPinPoints = "953a95d0-5226-46bb-80c3-f20b27a32064";
const char* const kPinOutput = "b7bc82a2-f095-490a-91e3-276431d5eb87";  // Output pin (.t3ui OutputUis)
constexpr float kAmtX = 6.076416f,    kAmtY = 990.98047f;
constexpr float kPtsX = -263.2265f,   kPtsY = 583.24f;
constexpr float kOutX = 708.4847f,    kOutY = 676.4649f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

// PARITY config values (see file header): resolved kernel params under the vec-wire-lands-on-head fork.
constexpr float kAmount = 0.75f, kGridScale = 0.5f, kGridStretchX = 3.0f;

// ── Test-fixture Buffer producer (emits a fixed N-point bag as the compound's Points input) ────────
std::vector<SwPoint>* g_fixture = nullptr;
void cookFixture(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes || !g_fixture) return;
  const uint32_t n = (uint32_t)g_fixture->size();
  if (n == 0) return;
  void* dst = c.requestBytes(n * (uint32_t)sizeof(SwPoint));
  if (!dst) return;
  std::memcpy(dst, g_fixture->data(), n * sizeof(SwPoint));
  c.output->elementStride = (uint32_t)sizeof(SwPoint);
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}
NodeSpec fixtureSpec() {
  NodeSpec s; s.type = "t3xf_snap_input"; s.title = "t3xf_snap_input"; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_snap_input(fixtureSpec(), cookFixture);

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
NodeSpec standInFlatAtomSpec() {
  NodeSpec s; s.type = kName; s.title = kName; s.category = "point.transform";
  s.ports = {{"points", "points", "Points", true}, {"out", "out", "Points", false}};
  s.evaluate = nullptr; return s;
}

}  // namespace

// ── ② PARITY (cook-driven vs the mathv oracle) ────────────────────────────────────────────────────
int runT3SnapPointsToGridParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Input bag: N points on a scaled circle (generic positions, never grid-cell-aligned → snapAmount∈(0,1)).
  const uint32_t N = 24;
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{ (float)(std::cos(a * 6.2831853) * 1.37 + 0.13),
                                 (float)(std::sin(a * 6.2831853) * 0.83 - 0.07),
                                 (float)((a - 0.5) * 1.9 + 0.11) };
    in[i].Rotation = SW_FLOAT4{0, 0, 0, 1};
    in[i].FX1 = 1.0f; in[i].FX2 = 1.0f; in[i].Scale = SW_PACKED3{1, 1, 1};
  }
  g_fixture = &in;

  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[snap-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixture = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[snap-retire] ②parity FAIL: no root symbol\n"); g_fixture = nullptr; pool->release(); return 1; }

  // Repoint the Points boundary→GetBufferComponents wire at the fixture producer. Require the dst child to
  // be a GetBufferComponents (BOTH GBC.BufferWithViews AND ExecuteBufferUpdate.BufferToUpdate map to the sw
  // slot name "BufferWithViews" — the fixture must feed the GBC that drives the SRV, not ExecuteBufferUpdate).
  auto childSym = [&](int id) -> std::string {
    for (const SymbolChild& c : sym->children) if (c.id == id) return c.symbolId;
    return std::string();
  };
  const int gbc = [&]{
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstSlot == "BufferWithViews" &&
          childSym(w.dstChild) == "GetBufferComponents") return w.dstChild;
    return 0; }();
  if (!gbc) { printf("[snap-retire] ②parity FAIL: no Points→GetBufferComponents wire\n"); g_fixture = nullptr; pool->release(); return 1; }
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_snap_input"; sym->children.push_back(p); }
  if (!lib.symbols.count("t3xf_snap_input"))
    if (const NodeSpec* fs = findSpec("t3xf_snap_input")) lib.symbols["t3xf_snap_input"] = atomicSymbolFromSpec(*fs);
  for (SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.dstChild == gbc && w.dstSlot == "BufferWithViews") {
      w.srcChild = fixtureId; w.srcSlot = "Buffer";
    }

  // 骨7 boundary injection: feed the scalar params. Vector inputs land a single value on the .x head.
  // injectBug PERTURBS the injected Amount (a param the output depends on CONTINUOUSLY) while the oracle
  // keeps the clean kAmount → the readback must DIVERGE. This is the §8 佈線 tooth: it proves the injected
  // boundary value actually flows through FloatsToBuffer into the kernel cbuffer (if injection were dead,
  // changing Amount would leave the output unchanged → oracle(kAmount) would still match → NO-BITE).
  // Inject ALL direct-boundary scalars up to GridScale, INCLUDING Scatter=0: a direct scalar we skip has its
  // FloatsToBuffer wire drop, closing the gap and shifting every LATER float (Mode, GainAndBias) to the
  // wrong cbuffer slot. GridStretch/GridOffset/GainAndBias ride Vector*Components (X/Y/Z→FloatsToBuffer wires
  // always present → their slots stay filled at 0). b0 = [GridStretch.xyz, Amount, GridOffset.xyz, GridScale,
  // Scatter, Mode, GainAndBias.xy] (SnapPointsToGrid.hlsl :9-17).
  const float amtInjected = injectBug ? 0.20f : kAmount;
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInAmount]      = {amtInjected};    // b0[3]
  boundaryFloatInputs[kInGridScale]   = {kGridScale};     // b0[7]
  boundaryFloatInputs[kInScatter]     = {0.0f};           // b0[8] — inject to keep Mode/GainAndBias aligned
  boundaryFloatInputs[kInGridStretch] = {kGridStretchX};  // b0[0] (Vector3Components.x)

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[snap-retire] ②parity FAIL: no ExecuteBufferUpdate\n"); g_fixture = nullptr; pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[snap-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixture = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // Oracle: the mathv-verified CPU ref, fed the SAME resolved params (vec .x head → (V,0,0)/(V,0)).
  mathv_ref::SnapPointsToGridParams prm{};
  prm.gridStretchX = kGridStretchX; prm.gridStretchY = 0.0f; prm.gridStretchZ = 0.0f;
  prm.amount = kAmount;
  prm.gridOffsetX = prm.gridOffsetY = prm.gridOffsetZ = 0.0f;
  prm.gridScale = kGridScale;
  prm.scatter = 0.0f; prm.mode = 0.0f;
  prm.gainAndBiasX = 0.0f; prm.gainAndBiasY = 0.0f;
  prm.strengthFactor = 0;
  std::vector<SwPoint> exp(N);
  mathv_ref::mathvRefSnapPointsToGrid(in.data(), exp.data(), N, prm);

  double maxErr = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      float dx = exp[i].Position.x - got[i].Position.x;
      float dy = exp[i].Position.y - got[i].Position.y;
      float dz = exp[i].Position.z - got[i].Position.z;
      double e = std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
      if (e > maxErr) { maxErr = e; worstI = (int)i; }
    }
  printf("[snap-retire] ②parity: haveOut=%d maxPosErr=%.6f(need<1e-3) worstI=%d\n", haveOut ? 1 : 0, maxErr, worstI);

  // did-not-trip guard: non-bug must see a NON-IDENTITY snap (else a vacuous passthrough passes hollow).
  double maxMove = 0.0;
  if (haveOut) for (uint32_t i = 0; i < N; ++i) {
    float dx = in[i].Position.x - got[i].Position.x, dy = in[i].Position.y - got[i].Position.y,
          dz = in[i].Position.z - got[i].Position.z;
    maxMove = std::max(maxMove, std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz));
  }

  mlib->release(); q->release(); dev->release();
  g_fixture = nullptr;

  const bool parityGreen = haveOut && (maxErr < 1e-3) && (maxMove > 1e-3);
  if (!injectBug) {
    printf("[snap-retire] ②parity VERDICT: %s (maxMove=%.4f)\n", parityGreen ? "GREEN" : "RED", maxMove);
    pool->release();
    return parityGreen ? 0 : 1;
  }
  // -bug: the cbuffer-order scramble must break parity. dead tooth → 0 (NO-BITE list catches it).
  const bool bites = !parityGreen;
  printf("[snap-retire] ②parity -bug: %s\n", bites ? "BITES (cbuffer order load-bearing)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// ── ④ LAYOUT (inline — .t3ui Position constants) ──────────────────────────────────────────────────
int runSnapLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[snap-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };

  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[snap-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[snap-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* amt = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinAmount) amt = &d;
  const SlotDef* pts = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPoints) pts = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput) out = &d;
  if (!amt || !pts || !out) {
    printf("[snap-retire] ④layout FAIL: amt=%p pts=%p out=%p\n", (void*)amt, (void*)pts, (void*)out);
    return 1;
  }
  const bool amtOk = injectBug ? (nearf(amt->x, 0) && nearf(amt->y, 0)) : (nearf(amt->x, kAmtX) && nearf(amt->y, kAmtY));
  const bool ptsOk = injectBug ? (nearf(pts->x, 0) && nearf(pts->y, 0)) : (nearf(pts->x, kPtsX) && nearf(pts->y, kPtsY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[snap-retire] ④layout: amt(%.3f,%.3f) pts(%.3f,%.3f) out(%.3f,%.3f)\n",
         amt->x, amt->y, pts->x, pts->y, out->x, out->y);

  const bool distinct = !(nearf(amt->x, pts->x) && nearf(amt->y, pts->y)) &&
                        !(nearf(amt->x, out->x) && nearf(amt->y, out->y)) &&
                        !(nearf(pts->x, out->x) && nearf(pts->y, out->y));
  const bool nonZero = !(nearf(amt->x, 0) && nearf(amt->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[snap-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (amtOk && ptsOk && outOk) ? 0 : 1;
  }
  const bool bites = (amtOk && ptsOk && outOk);  // under -bug all reverted to 0,0
  return bites ? 1 : 0;
}

int runT3SnapPointsToGridRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[snap-retire] FAIL: import\n"); return 1;
  }
  refreshCompoundSpecs(lib);

  // ① TAKEOVER POLARITY
  const NodeSpec* g1spec = findSpec(kName);
  const Symbol* csym = lib.find(kGuid);
  const bool g1green = g1spec && g1spec->type == std::string(kGuid) && g1spec->evaluate == nullptr &&
                       csym && !csym->atomic && !csym->children.empty();
  bool g1bit = false;
  if (injectBug) {
    pointModifySpecSink().push_back(standInFlatAtomSpec());
    const NodeSpec* shadowed = findSpec(kName);
    g1bit = shadowed && shadowed->type == std::string(kName);
    pointModifySpecSink().pop_back();
  }
  printf("[snap-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
         kName, g1spec ? g1spec->type.c_str() : "<null>", csym ? (int)csym->atomic : -1,
         csym ? (int)csym->children.size() : -1,
         injectBug ? (g1bit ? "BITES" : "TOOTHLESS") : (g1green ? "GREEN" : "RED"));

  // ③ REFERENCE REACHABILITY + COOKABILITY
  bool g3green = false; size_t g3nodes = 0; int g3in = 0, g3out = 0;
  if (g1spec) {
    countPorts(*g1spec, g3in, g3out);
    ResidentEvalGraph eg = buildEvalGraph(lib, g1spec->type);
    g3nodes = eg.nodes.size();
    g3green = g3in > 0 && g3out > 0 && g3nodes > 0;
  }
  bool g3bit = false;
  if (injectBug) {
    setDynamicSpecs({});
    g3bit = (findSpec(kName) == nullptr);
    refreshCompoundSpecs(lib);
  }
  printf("[snap-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  // ② PARITY (cook-driven)
  const int g2 = runT3SnapPointsToGridParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[snap-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ④ LAYOUT
  const int g4 = runSnapLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[snap-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[snap-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[snap-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
