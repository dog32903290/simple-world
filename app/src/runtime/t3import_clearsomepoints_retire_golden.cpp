// runtime/t3import_clearsomepoints_retire_golden — 廢棄節點退場 harness
// (--selftest-t3-clearsomepoints-retire).
//
// Retires the flat ClearSomePoints point atom: its human-name references are AUTO-TAKEN-OVER by the
// nested .t3 compound (assets/catalog_t3/ClearSomePoints.t3, guid e570b2e6…) via the replace-in-place
// seam (graph_bridge.cpp refreshCompoundSpecs name alias + findSpec's dynamicSpecs tail). Four gates,
// each a MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3's own Id guid + the
// .t3ui Position numbers) and the mathv-verified ClearSomePoints oracle — never sw's own output
// (GOLDEN_STANDARD 特徵1 / P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: with the flat atom retired, findSpec("ClearSomePoints") falls through every
//     atom sink to the compound's NAME alias → the COMPOUND spec (type==guid, evaluate==nullptr) and
//     lib[guid] is a non-atomic compound with children. injectBug pushes a stand-in flat atom into a
//     LIVE point-modify sink → findSpec hits it FIRST → returns the ATOM → BITE.
//  ② PARITY (cook-driven, mathv oracle): import ClearSomePoints.t3 → buildEvalGraph (骨7 boundary
//     injection feeds the compound's scalar boundary params) → cookResident → read back the
//     ExecuteBufferUpdate output vs mathvRefClearSomePoints (the mathv-verified CPU oracle). ClearSomePoints
//     is a COUNT-PRESERVING KILLER: it never moves a point, it flags each with Scale := NAN when
//     hash11u(pointU) <= Ratio, so the observable is the SET OF KILLED points (Scale is NaN). This is
//     the retire §8 佈線 focus: proves FloatsToBuffer(b0 Ratio) + IntsToBuffer(b1 Seed/Repeat/Resolution)
//     assembly + boundary injection put the params in the right kernel cbuffer slots + the generic
//     ComputeShaderStage dispatches the ported kernel computeshaderstage_clearsomepoints. injectBug
//     PERTURBS the boundary-injected Ratio (the kill threshold; the kill set depends on it) while the
//     oracle keeps the clean value → the kill sets diverge → BITE. This proves the injected boundary
//     param actually reaches the kernel cbuffer (dead injection → same kills → oracle still matches → NO-BITE).
//  ③ REFERENCE REACHABILITY + COOKABILITY: the NAME reference resolves to a spec carrying the compound
//     boundary (Points in + Output out) AND buildEvalGraph flattens to a NON-EMPTY resident graph.
//     injectBug drops the compound registration (setDynamicSpecs({})) → findSpec nullptr → BITE.
//  ④ LAYOUT: import .t3 + sibling .t3ui through the production layout seam → boundary pins land on their
//     .t3ui Position constants (non-zero, distinct). injectBug flips t3LayoutDisable() → every pos 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG (deterministic, exact — branchy per-point kill, NOT transcendental) ─────────────
// The compound's scalar params are BOUNDARY inputs (no in-graph producer when replayed alone). We inject
// via the 骨7 seam. Ratio rides FloatsToBuffer → b0[0]; Seed/Repeat/Resolution ride IntsToBuffer → b1 (int
// slices, ceil-to-4-padded). We inject ALL FOUR direct-boundary scalars (a dropped scalar wire would close
// its FloatsToBuffer/IntsToBuffer gap and shift later slots). Params are chosen to AVOID the Resolution==0
// div-by-zero ambiguity class (MATH_VERIFY_WORKFLOW §7 "HLSL 本體歧義"): Resolution=1 → Mod(i,1)=0 → every
// point gets a DISTINCT block index i → a distinct hash → with Ratio=0.5 roughly half the bag is killed, a
// genuine partial split (liveness). The whole path is finite uint/float arithmetic (no eps class), so the
// GPU kill set and the CPU-oracle kill set must be BIT-IDENTICAL.
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
#include "mathv_ref_clearsomepoints.h"      // mathvRefClearSomePoints (mathv-verified CPU oracle)

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/clearsomepoints_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/clearsomepoints_t3ui_embed.inc"
;

// Expected constants — the .t3's OWN Id + the .t3ui Position numbers (never sw output).
const char* const kGuid = "e570b2e6-6e35-4a14-ade6-f377494fe96d";
const char* const kName = "ClearSomePoints";
// boundary input def guids (compound Inputs) — the 骨7 injection keys.
const char* const kInRatio      = "168cb238-fdd9-4302-ad2d-bcfe0f200525";  // b0[0] (FloatsToBuffer)
const char* const kInSeed       = "ed9306bb-5ca8-4cfc-acb9-7333821f651f";  // b1[0] (IntsToBuffer)
const char* const kInRepeat     = "68a2ea07-4ca9-4211-a8e5-e67943c7d3fa";  // b1[1]
const char* const kInResolution = "23302290-2789-49da-9b65-6d5b472c94e8";  // b1[2]
// .t3ui pins (④): Ratio input / Points input / Output — verbatim from ClearSomePoints.t3ui.
const char* const kPinRatio  = "168cb238-fdd9-4302-ad2d-bcfe0f200525";
const char* const kPinPoints = "f1662ff8-2a3c-40c4-a313-4c8b831830d7";
const char* const kPinOutput = "769cc00b-f190-4c90-ace3-3ec10cb156dd";
constexpr float kRatioX = -293.2356f, kRatioY = 985.39294f;
constexpr float kPtsX   = -379.90247f, kPtsY  = 718.65216f;
constexpr float kOutX   = 199.4545f,   kOutY  = 914.2387f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

// PARITY config values (see file header): the clean kernel params.
constexpr float kRatio = 0.5f;
constexpr int   kSeed = 0, kRepeat = 0, kResolution = 1;

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
  NodeSpec s; s.type = "t3xf_clear_input"; s.title = "t3xf_clear_input"; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_clear_input(fixtureSpec(), cookFixture);

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
NodeSpec standInFlatAtomSpec() {
  NodeSpec s; s.type = kName; s.title = kName; s.category = "point.modify";
  s.ports = {{"points", "points", "Points", true}, {"out", "out", "Points", false}};
  s.evaluate = nullptr; return s;
}
bool killed(const SwPoint& p) { return std::isnan(p.Scale.x); }

}  // namespace

// ── ② PARITY (cook-driven vs the mathv oracle) ────────────────────────────────────────────────────
int runT3ClearSomePointsParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Input bag: N points with FINITE Scale (so a kill is observable as Scale→NaN). Positions arbitrary
  // (ClearSomePoints never moves points — it only conditionally flags Scale := NAN).
  const uint32_t N = 32;
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{ (float)(std::cos(a * 6.2831853) * 1.3),
                                 (float)(std::sin(a * 6.2831853) * 0.9),
                                 (float)((a - 0.5) * 2.0) };
    in[i].Rotation = SW_FLOAT4{0, 0, 0, 1};
    in[i].FX1 = 1.0f; in[i].FX2 = 1.0f; in[i].Scale = SW_PACKED3{1, 1, 1};
  }
  g_fixture = &in;

  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[clear-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixture = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[clear-retire] ②parity FAIL: no root symbol\n"); g_fixture = nullptr; pool->release(); return 1; }

  // Repoint the Points boundary→GetBufferComponents wire at the fixture producer (the GBC that drives the
  // SRV, sw slot name "BufferWithViews"; srcChild is the boundary so this uniquely picks the Points-fed GBC,
  // not the UAV-readback GBC which is fed by StructuredBufferWithViews).
  auto childSym = [&](int id) -> std::string {
    for (const SymbolChild& c : sym->children) if (c.id == id) return c.symbolId;
    return std::string();
  };
  const int gbc = [&]{
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstSlot == "BufferWithViews" &&
          childSym(w.dstChild) == "GetBufferComponents") return w.dstChild;
    return 0; }();
  if (!gbc) { printf("[clear-retire] ②parity FAIL: no Points→GetBufferComponents wire\n"); g_fixture = nullptr; pool->release(); return 1; }
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_clear_input"; sym->children.push_back(p); }
  if (!lib.symbols.count("t3xf_clear_input"))
    if (const NodeSpec* fs = findSpec("t3xf_clear_input")) lib.symbols["t3xf_clear_input"] = atomicSymbolFromSpec(*fs);
  for (SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.dstChild == gbc && w.dstSlot == "BufferWithViews") {
      w.srcChild = fixtureId; w.srcSlot = "Buffer";
    }

  // 骨7 boundary injection: feed the scalar params. injectBug PERTURBS the injected Ratio (the kill
  // threshold — the kill SET depends on it) while the oracle keeps the clean kRatio → the readback kill set
  // must DIVERGE. This is the §8 佈線 tooth: it proves the injected boundary value actually flows through
  // FloatsToBuffer into the kernel cbuffer (dead injection → identical kills → oracle(kRatio) still matches).
  // Inject ALL FOUR direct-boundary scalars: Ratio → b0, Seed/Repeat/Resolution → b1 (int rail). Resolution=1
  // avoids the Resolution==0 div-by-zero ambiguity class (see header).
  const float ratioInjected = injectBug ? 0.05f : kRatio;
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInRatio]      = {ratioInjected};        // b0[0]
  boundaryFloatInputs[kInSeed]       = {(float)kSeed};         // b1[0]
  boundaryFloatInputs[kInRepeat]     = {(float)kRepeat};       // b1[1]
  boundaryFloatInputs[kInResolution] = {(float)kResolution};   // b1[2]

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[clear-retire] ②parity FAIL: no ExecuteBufferUpdate\n"); g_fixture = nullptr; pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[clear-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixture = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // Oracle: the mathv-verified CPU ref, fed the CLEAN params (never the perturbed Ratio).
  mathv_ref::ClearSomePointsParams prm{};
  prm.ratio = kRatio; prm.seed = kSeed; prm.repeat = kRepeat; prm.resolution = kResolution;
  std::vector<SwPoint> exp(N);
  mathv_ref::mathvRefClearSomePoints(in.data(), exp.data(), N, prm);

  // Observable = the SET of killed points (Scale NaN). Compare per-point kill flag GPU vs oracle.
  int mismatches = 0, nKilledGpu = 0, nKilledOracle = 0;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      const bool kg = killed(got[i]), ko = killed(exp[i]);
      if (kg) ++nKilledGpu;
      if (ko) ++nKilledOracle;
      if (kg != ko) ++mismatches;
    }
  printf("[clear-retire] ②parity: haveOut=%d killGpu=%d killOracle=%d mismatches=%d(need 0)\n",
         haveOut ? 1 : 0, nKilledGpu, nKilledOracle, mismatches);

  mlib->release(); q->release(); dev->release();
  g_fixture = nullptr;

  // did-not-trip guard: the clean run must produce a genuine PARTIAL split (some killed AND some alive),
  // else a vacuous all-alive/all-dead passthrough would pass hollow.
  const bool partial = haveOut && nKilledOracle > 0 && nKilledOracle < (int)N;
  const bool parityGreen = haveOut && (mismatches == 0) && partial;
  if (!injectBug) {
    printf("[clear-retire] ②parity VERDICT: %s (partialSplit=%d)\n", parityGreen ? "GREEN" : "RED", partial ? 1 : 0);
    pool->release();
    return parityGreen ? 0 : 1;
  }
  // -bug: the perturbed Ratio must change the kill set vs the clean oracle. dead tooth → 0.
  const bool bites = !parityGreen;
  printf("[clear-retire] ②parity -bug: %s\n", bites ? "BITES (Ratio injection load-bearing)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// ── ④ LAYOUT (inline — .t3ui Position constants) ──────────────────────────────────────────────────
int runClearLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[clear-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };

  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[clear-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[clear-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* rat = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinRatio) rat = &d;
  const SlotDef* pts = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPoints) pts = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput) out = &d;
  if (!rat || !pts || !out) {
    printf("[clear-retire] ④layout FAIL: rat=%p pts=%p out=%p\n", (void*)rat, (void*)pts, (void*)out);
    return 1;
  }
  const bool ratOk = injectBug ? (nearf(rat->x, 0) && nearf(rat->y, 0)) : (nearf(rat->x, kRatioX) && nearf(rat->y, kRatioY));
  const bool ptsOk = injectBug ? (nearf(pts->x, 0) && nearf(pts->y, 0)) : (nearf(pts->x, kPtsX) && nearf(pts->y, kPtsY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[clear-retire] ④layout: rat(%.3f,%.3f) pts(%.3f,%.3f) out(%.3f,%.3f)\n",
         rat->x, rat->y, pts->x, pts->y, out->x, out->y);

  const bool distinct = !(nearf(rat->x, pts->x) && nearf(rat->y, pts->y)) &&
                        !(nearf(rat->x, out->x) && nearf(rat->y, out->y)) &&
                        !(nearf(pts->x, out->x) && nearf(pts->y, out->y));
  const bool nonZero = !(nearf(rat->x, 0) && nearf(rat->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[clear-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (ratOk && ptsOk && outOk) ? 0 : 1;
  }
  const bool bites = (ratOk && ptsOk && outOk);  // under -bug all reverted to 0,0
  return bites ? 1 : 0;
}

int runT3ClearSomePointsRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[clear-retire] FAIL: import\n"); return 1;
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
  printf("[clear-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
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
  printf("[clear-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  // ② PARITY (cook-driven)
  const int g2 = runT3ClearSomePointsParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[clear-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ④ LAYOUT
  const int g4 = runClearLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[clear-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[clear-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[clear-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
