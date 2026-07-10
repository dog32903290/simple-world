// runtime/t3import_blendpoints_retire_golden — 廢棄節點退場 harness (--selftest-t3-blendpoints-retire).
//
// Retires the flat BlendPoints point atom (a COOK-ONLY op — no NodeSpec; the flat cook was only ever
// exercised by hand-built graphs + mathv). Its human-name references are AUTO-TAKEN-OVER by the nested .t3
// compound (assets/catalog_t3/BlendPoints.t3, guid 2dc5c9d1…) via the replace-in-place seam
// (graph_bridge.cpp refreshCompoundSpecs name alias + findSpec's dynamicSpecs tail). Four gates, each a
// MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3's own Id guid + the .t3ui Position
// numbers) and the mathv-verified BlendPoints oracle — never sw's own output (GOLDEN_STANDARD 特徵1 /
// P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: findSpec("BlendPoints") resolves through the compound's NAME alias to the COMPOUND
//     spec (type==guid, evaluate==nullptr); lib[guid] is a non-atomic compound with children. injectBug
//     pushes a stand-in flat atom into a LIVE sink → findSpec hits it FIRST → returns the ATOM → BITE.
//  ② PARITY (cook-driven, mathv oracle): import BlendPoints.t3 → buildEvalGraph (骨7 boundary injection) →
//     cookResident → read back the ExecuteBufferUpdate output vs mathvRefBlendPoints. BlendPoints is a
//     DUAL-SRV index-paired blend: each output point lerps PointsA[i] toward PointsB[i] by BlendMode-selected
//     f; the observable is the written Position AND FX1 (=f). This is the retire §8 佈線 focus: proves
//     FloatsToBuffer(b0 [BlendFactor,BlendMode,Pairing,Width,Scatter]) assembly + the TWO SRV wires (PointsA
//     t0 / PointsB t1) + the generic-seam per-SRV countB AUX reach the ported kernel
//     computeshaderstage_blendpoints. injectBug PERTURBS the boundary-injected BlendFactor (in Mix mode
//     f==BlendFactor, so the output depends on it continuously) while the oracle keeps the clean value →
//     the readback diverges → BITE. This proves the injected boundary param reaches the kernel cbuffer.
//  ③ REFERENCE REACHABILITY + COOKABILITY: the NAME reference resolves to a spec carrying the compound
//     boundary (PointsA/PointsB in + Output out) AND buildEvalGraph flattens to a NON-EMPTY resident graph.
//     injectBug drops the compound registration (setDynamicSpecs({})) → findSpec nullptr → BITE.
//  ④ LAYOUT: import .t3 + sibling .t3ui through the production layout seam → boundary pins land on their
//     .t3ui Position constants (non-zero, distinct). injectBug flips t3LayoutDisable() → every pos 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG (deterministic, exact — Mix mode, Scatter=0) ─────────────────────────────────────
// Two equal-length boundary Points inputs (PointsA_/PointsB_) each drive a GetBufferComponents SRV; we
// repoint BOTH at fixture producers (distinguished by the boundary wire's srcSlot == input def guid). The
// b0 scalars are injected via the 骨7 seam. The 3 DIRECT scalars (BlendFactor/RangeWidth/Scatter) MUST all
// be injected (a dropped direct wire closes its FloatsToBuffer gap and shifts later b0 slots); BlendMode /
// Pairing ride IntToFloat nodes (their FloatsToBuffer wire is always present) but are injected too for
// determinism. Mode=Mix(0)+Scatter=0 → f==BlendFactor for every point (no hash jitter, no smoothstep) →
// exact float lerp (Scale finite → noBlend=false), matchable to <1e-3. Only Position and FX1 are compared.
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
#include "runtime/t3_import.h"              // importT3Symbol / t3LayoutDisable / symbolIdOfT3
#include "runtime/tixl_point.h"             // SwPoint (64B)
#include "mathv_ref_blendpoints.h"          // mathvRefBlendPoints (mathv-verified CPU oracle)

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/blendpoints_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/blendpoints_t3ui_embed.inc"
;

// Expected constants — the .t3's OWN Id + the .t3ui Position numbers (never sw output).
const char* const kGuid = "2dc5c9d1-ea93-4597-a4d9-7b610aad603a";
const char* const kName = "BlendPoints";
// boundary input def guids (compound Inputs) — the 骨7 injection / SRV-repoint keys (LOWERCASE).
const char* const kInPointsA     = "97904d2e-ae67-4ab4-9201-7902a85d12f3";  // PointsA (t0)
const char* const kInPointsB     = "91b903a2-5127-431b-ab66-d5a38ce1693c";  // PointsB (t1)
const char* const kInBlendFactor = "ba7ffda2-f9f6-440d-a174-7339844835fa";  // b0[0]
const char* const kInBlendMode   = "c5480ce5-a8ba-4a26-8cee-c28e442020b7";  // b0[1] (via IntToFloat)
const char* const kInPairing     = "acef877d-214d-4ca0-ac11-95fa59d1f6fc";  // b0[2] (via IntToFloat)
const char* const kInRangeWidth  = "bdb712a8-3dbc-458a-887a-5add51813196";  // b0[3] (Width)
const char* const kInScatter     = "ee8e9e15-ce18-4034-abc6-dd56108c8a02";  // b0[4]
// .t3ui pins (④): PointsA input / BlendFactor input / Output — verbatim from BlendPoints.t3ui.
const char* const kPinPointsA = "97904d2e-ae67-4ab4-9201-7902a85d12f3";
const char* const kPinBlend   = "ba7ffda2-f9f6-440d-a174-7339844835fa";
const char* const kPinOutput  = "660013c7-8f6b-458a-bb86-61e5a85692a4";
constexpr float kPaX = -539.26337f, kPaY = 958.8297f;
constexpr float kBlX = -275.56927f, kBlY = 1362.6199f;
constexpr float kOuX = 792.6956f,   kOuY = 659.3562f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

// PARITY config values (see header): the clean kernel params (Mix mode, no scatter).
constexpr float kBlendFactor = 0.35f, kBlendMode = 0.0f, kPairing = 0.0f, kWidth = 0.5f, kScatter = 0.0f;

// ── Two test-fixture Buffer producers (PointsA + PointsB) ──────────────────────────────────────────
std::vector<SwPoint>* g_fixtureA = nullptr;
std::vector<SwPoint>* g_fixtureB = nullptr;
void cookFixtureFrom(BufferCookCtx& c, std::vector<SwPoint>* bag) {
  if (!c.output || !c.requestBytes || !bag) return;
  const uint32_t n = (uint32_t)bag->size();
  if (n == 0) return;
  void* dst = c.requestBytes(n * (uint32_t)sizeof(SwPoint));
  if (!dst) return;
  std::memcpy(dst, bag->data(), n * sizeof(SwPoint));
  c.output->elementStride = (uint32_t)sizeof(SwPoint);
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}
void cookFixtureA(BufferCookCtx& c) { cookFixtureFrom(c, g_fixtureA); }
void cookFixtureB(BufferCookCtx& c) { cookFixtureFrom(c, g_fixtureB); }
NodeSpec fixtureSpec(const char* type) {
  NodeSpec s; s.type = type; s.title = type; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_blend_a(fixtureSpec("t3xf_blend_a"), cookFixtureA);
const BufferOp _reg_t3xf_blend_b(fixtureSpec("t3xf_blend_b"), cookFixtureB);

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
NodeSpec standInFlatAtomSpec() {
  NodeSpec s; s.type = kName; s.title = kName; s.category = "point.combine";
  s.ports = {{"PointsA", "PointsA", "Points", true}, {"PointsB", "PointsB", "Points", true},
             {"out", "out", "Points", false}};
  s.evaluate = nullptr; return s;
}

bool repointSrvAtFixture(Symbol* sym, SymbolLibrary& lib, const char* inputGuid, const char* fixtureType) {
  auto childSym = [&](int id) -> std::string {
    for (const SymbolChild& c : sym->children) if (c.id == id) return c.symbolId;
    return std::string();
  };
  const int gbc = [&]{
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.srcSlot == std::string(inputGuid) &&
          w.dstSlot == "BufferWithViews" && childSym(w.dstChild) == "GetBufferComponents")
        return w.dstChild;
    return 0; }();
  if (!gbc) return false;
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = fixtureType; sym->children.push_back(p); }
  if (!lib.symbols.count(fixtureType))
    if (const NodeSpec* fs = findSpec(fixtureType)) lib.symbols[fixtureType] = atomicSymbolFromSpec(*fs);
  for (SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.srcSlot == std::string(inputGuid) && w.dstChild == gbc &&
        w.dstSlot == "BufferWithViews") {
      w.srcChild = fixtureId; w.srcSlot = "Buffer";
    }
  return true;
}

}  // namespace

// ── ② PARITY (cook-driven vs the mathv oracle) ────────────────────────────────────────────────────
int runT3BlendPointsParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Two equal-length bags. PointsA (A) on a circle; PointsB (B) offset + distinct Color/FX1, so the blend
  // moves each point a non-trivial distance and the FX1 result (=f) is observable.
  const uint32_t N = 24;
  std::vector<SwPoint> inA(N), inB(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    inA[i] = SwPoint{};
    inA[i].Position = SW_PACKED3{ (float)(std::cos(a * 6.2831853) * 1.1 + 0.05),
                                  (float)(std::sin(a * 6.2831853) * 0.8 - 0.03),
                                  (float)((a - 0.5) * 1.4) };
    inA[i].Rotation = SW_FLOAT4{0, 0, 0, 1};
    inA[i].Color = SW_FLOAT4{0.1f, 0.2f, 0.3f, 1.0f};
    inA[i].FX1 = 0.0f; inA[i].FX2 = 0.0f; inA[i].Scale = SW_PACKED3{1, 1, 1};
    inB[i] = inA[i];
    inB[i].Position = SW_PACKED3{ inA[i].Position.x + 2.0f, inA[i].Position.y - 1.5f,
                                  inA[i].Position.z + 0.7f };
    inB[i].Color = SW_FLOAT4{0.9f, 0.8f, 0.7f, 1.0f};
    inB[i].FX1 = 1.0f;
  }
  g_fixtureA = &inA; g_fixtureB = &inB;

  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[blendpoints-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[blendpoints-retire] ②parity FAIL: no root symbol\n"); g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1; }

  if (!repointSrvAtFixture(sym, lib, kInPointsA, "t3xf_blend_a") ||
      !repointSrvAtFixture(sym, lib, kInPointsB, "t3xf_blend_b")) {
    printf("[blendpoints-retire] ②parity FAIL: no PointsA/PointsB→GetBufferComponents wire\n");
    g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1;
  }

  // 骨7 boundary injection: feed the b0 scalars (all five). injectBug PERTURBS BlendFactor (== f in Mix mode,
  // the output depends on it continuously) while the oracle keeps clean kBlendFactor → the readback DIVERGES.
  const float blendInjected = injectBug ? 0.8f : kBlendFactor;
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInBlendFactor] = {blendInjected};  // b0[0]
  boundaryFloatInputs[kInBlendMode]   = {kBlendMode};     // b0[1] (via IntToFloat)
  boundaryFloatInputs[kInPairing]     = {kPairing};       // b0[2] (via IntToFloat)
  boundaryFloatInputs[kInRangeWidth]  = {kWidth};         // b0[3]
  boundaryFloatInputs[kInScatter]     = {kScatter};       // b0[4]

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[blendpoints-retire] ②parity FAIL: no ExecuteBufferUpdate\n"); g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[blendpoints-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // Oracle: the mathv-verified CPU ref, fed the CLEAN params (countA==countB==resultCount==N).
  mathv_ref::BlendPointsParams prm{};
  prm.blendFactor = kBlendFactor; prm.blendMode = kBlendMode; prm.pairingMode = kPairing;
  prm.width = kWidth; prm.scatter = kScatter;
  std::vector<SwPoint> exp = inA;  // pre-seed (Mix mode writes every row)
  mathv_ref::mathvRefBlendPoints(inA.data(), N, inB.data(), N, exp.data(), N, prm);

  // Compare Position + FX1 (=f).
  double maxPosErr = 0.0, maxFx1Err = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      float dx = exp[i].Position.x - got[i].Position.x;
      float dy = exp[i].Position.y - got[i].Position.y;
      float dz = exp[i].Position.z - got[i].Position.z;
      double e = std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
      if (e > maxPosErr) { maxPosErr = e; worstI = (int)i; }
      maxFx1Err = std::max(maxFx1Err, (double)std::fabs(exp[i].FX1 - got[i].FX1));
    }
  printf("[blendpoints-retire] ②parity: haveOut=%d maxPosErr=%.6f maxFx1Err=%.6f (need<1e-3) worstI=%d\n",
         haveOut ? 1 : 0, maxPosErr, maxFx1Err, worstI);

  // did-not-trip guard: the clean run must actually MOVE points (non-identity blend).
  double maxMove = 0.0;
  if (haveOut) for (uint32_t i = 0; i < N; ++i) {
    float dx = inA[i].Position.x - got[i].Position.x, dy = inA[i].Position.y - got[i].Position.y,
          dz = inA[i].Position.z - got[i].Position.z;
    maxMove = std::max(maxMove, std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz));
  }

  mlib->release(); q->release(); dev->release();
  g_fixtureA = g_fixtureB = nullptr;

  const bool parityGreen = haveOut && (maxPosErr < 1e-3) && (maxFx1Err < 1e-3) && (maxMove > 1e-3);
  if (!injectBug) {
    printf("[blendpoints-retire] ②parity VERDICT: %s (maxMove=%.4f)\n", parityGreen ? "GREEN" : "RED", maxMove);
    pool->release();
    return parityGreen ? 0 : 1;
  }
  const bool bites = !parityGreen;
  printf("[blendpoints-retire] ②parity -bug: %s\n", bites ? "BITES (BlendFactor injection load-bearing)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// ── ④ LAYOUT (inline — .t3ui Position constants) ──────────────────────────────────────────────────
int runBlendPointsLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[blendpoints-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };

  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[blendpoints-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[blendpoints-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* pa  = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPointsA) pa = &d;
  const SlotDef* bl  = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinBlend)   bl = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput)  out = &d;
  if (!pa || !bl || !out) {
    printf("[blendpoints-retire] ④layout FAIL: pa=%p bl=%p out=%p\n", (void*)pa, (void*)bl, (void*)out);
    return 1;
  }
  const bool paOk = injectBug ? (nearf(pa->x, 0) && nearf(pa->y, 0)) : (nearf(pa->x, kPaX) && nearf(pa->y, kPaY));
  const bool blOk = injectBug ? (nearf(bl->x, 0) && nearf(bl->y, 0)) : (nearf(bl->x, kBlX) && nearf(bl->y, kBlY));
  const bool ouOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOuX) && nearf(out->y, kOuY));
  printf("[blendpoints-retire] ④layout: pa(%.3f,%.3f) bl(%.3f,%.3f) out(%.3f,%.3f)\n",
         pa->x, pa->y, bl->x, bl->y, out->x, out->y);

  const bool distinct = !(nearf(pa->x, bl->x) && nearf(pa->y, bl->y)) &&
                        !(nearf(pa->x, out->x) && nearf(pa->y, out->y)) &&
                        !(nearf(bl->x, out->x) && nearf(bl->y, out->y));
  const bool nonZero = !(nearf(pa->x, 0) && nearf(pa->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[blendpoints-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (paOk && blOk && ouOk) ? 0 : 1;
  }
  const bool bites = (paOk && blOk && ouOk);  // under -bug all reverted to 0,0
  return bites ? 1 : 0;
}

int runT3BlendPointsRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[blendpoints-retire] FAIL: import\n"); return 1;
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
  printf("[blendpoints-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
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
  printf("[blendpoints-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  // ② PARITY (cook-driven)
  const int g2 = runT3BlendPointsParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[blendpoints-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ④ LAYOUT
  const int g4 = runBlendPointsLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[blendpoints-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[blendpoints-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[blendpoints-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
