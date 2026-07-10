// runtime/t3import_snaptopoints_retire_golden — 廢棄節點退場 harness
// (--selftest-t3-snaptopoints-retire).
//
// Retires the flat SnapToPoints point atom: its human-name references are AUTO-TAKEN-OVER by the nested
// .t3 compound (assets/catalog_t3/SnapToPoints.t3, guid 5822b0d8…) via the replace-in-place seam
// (graph_bridge.cpp refreshCompoundSpecs name alias + findSpec's dynamicSpecs tail). Four gates, each a
// MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3's own Id guid + the .t3ui Position
// numbers) and the mathv-verified SnapToPoints oracle — never sw's own output (GOLDEN_STANDARD 特徵1 /
// P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: with the flat atom retired, findSpec("SnapToPoints") falls through every atom sink
//     to the compound's NAME alias → the COMPOUND spec (type==guid, evaluate==nullptr) and lib[guid] is a
//     non-atomic compound with children. injectBug pushes a stand-in flat atom into a LIVE sink → findSpec
//     hits it FIRST → returns the ATOM → BITE.
//  ② PARITY (cook-driven, mathv oracle): import SnapToPoints.t3 → buildEvalGraph (骨7 boundary injection)
//     → cookResident → read back the ExecuteBufferUpdate output vs mathvRefSnapToPoints. SnapToPoints is a
//     DUAL-SRV index-paired op: each Points1[i] is lerped toward Points2[i]; the observable is the written
//     Position AND FX1 (LegacyPoint.W). This is the retire §8 佈線 focus: proves FloatsToBuffer(b0
//     [BlendFactor,Distance,MaxAmount]) assembly + the TWO SRV wires (Points1 t0 / Points2 t1) reach the
//     generic ComputeShaderStage that dispatches the ported kernel computeshaderstage_snaptopoints.
//     injectBug PERTURBS the boundary-injected MaxAmount (the Position snap scale; the output depends on it
//     continuously) while the oracle keeps the clean value → the Position readback diverges → BITE. This
//     proves the injected boundary param reaches the kernel cbuffer (dead injection → output unchanged →
//     oracle still matches → NO-BITE).
//  ③ REFERENCE REACHABILITY + COOKABILITY: the NAME reference resolves to a spec carrying the compound
//     boundary (Points1/Points2 in + Output out) AND buildEvalGraph flattens to a NON-EMPTY resident graph.
//     injectBug drops the compound registration (setDynamicSpecs({})) → findSpec nullptr → BITE.
//  ④ LAYOUT: import .t3 + sibling .t3ui through the production layout seam → boundary pins land on their
//     .t3ui Position constants (non-zero, distinct). injectBug flips t3LayoutDisable() → every pos 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG (deterministic, exact — polynomial smoothstep, NOT transcendental) ──────────────
// Two boundary Points inputs (PointsA_/PointsB_) each drive a GetBufferComponents SRV; we repoint BOTH at
// fixture producers (distinguished by the boundary wire's srcSlot == the input def guid). The scalar params
// (BlendFactor/Distance/MaxAmount) are BOUNDARY inputs injected via the 骨7 seam (all three, in b0 order).
// The dead BlendMode input rides an IntToFloat that is DISCARDED (no wire into FloatsToBuffer) so it is not
// injected. Points2 is placed so most points snap a NON-TRIVIAL distance (liveness). smoothstep/length/lerp
// are all finite polynomial/algebraic float ops (no eps class), so GPU vs CPU-oracle must match to <1e-3.
// Only Position and FX1(W) are compared — the kernel writes ONLY those two fields (Rotation/Color/Scale/FX2
// carry from Points1; the oracle leaves them untouched, an AMBIGUITY it documents).
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
#include "mathv_ref_snaptopoints.h"         // mathvRefSnapToPoints (mathv-verified CPU oracle)

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/snaptopoints_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/snaptopoints_t3ui_embed.inc"
;

// Expected constants — the .t3's OWN Id + the .t3ui Position numbers (never sw output).
const char* const kGuid = "5822b0d8-32ed-4db3-975b-0e8fb8d7dd17";
const char* const kName = "SnapToPoints";
// boundary input def guids (compound Inputs) — the 骨7 injection / SRV-repoint keys (LOWERCASE: the importer
// lowercases boundary-wire srcSlot to the input def guid).
const char* const kInPointsA    = "aeb6072f-4275-4822-a3e0-fb1f59615dd9";  // Points1 (t0)
const char* const kInPointsB    = "1abba695-f044-459b-9c89-20441a32fa6b";  // Points2 (t1)
const char* const kInBlendFactor= "1acfa764-f427-4cf5-b08c-81667d13feca";  // b0[0]
const char* const kInDistance   = "6f953ff7-0790-4ed6-9c25-c57b9d41a6da";  // b0[1]
const char* const kInMaxAmount  = "8ba57792-f184-4f5f-a3c3-772e1f5fbe1d";  // b0[2]
// .t3ui pins (④): Points1 input / BlendFactor input / Output — verbatim from SnapToPoints.t3ui.
const char* const kPinPoints1 = "aeb6072f-4275-4822-a3e0-fb1f59615dd9";
const char* const kPinBlend   = "1acfa764-f427-4cf5-b08c-81667d13feca";
const char* const kPinOutput  = "d92815b8-4a13-4970-80ef-ef59858a43f6";
constexpr float kP1X = -452.71265f, kP1Y = 906.453f;
constexpr float kBlX = -448.6706f,  kBlY = 765.16473f;
constexpr float kOuX = 792.6956f,   kOuY = 659.3562f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

// PARITY config values (see header): the clean kernel params.
constexpr float kBlendFactor = 0.5f, kDistance = 1.0f, kMaxAmount = 1.0f;

// ── Two test-fixture Buffer producers (Points1 = A bag, Points2 = snap-target bag) ─────────────────
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
const BufferOp _reg_t3xf_snap_a(fixtureSpec("t3xf_snap_a"), cookFixtureA);
const BufferOp _reg_t3xf_snap_b(fixtureSpec("t3xf_snap_b"), cookFixtureB);

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
NodeSpec standInFlatAtomSpec() {
  NodeSpec s; s.type = kName; s.title = kName; s.category = "point.transform";
  s.ports = {{"Points1", "Points1", "Points", true}, {"Points2", "Points2", "Points", true},
             {"out", "out", "Points", false}};
  s.evaluate = nullptr; return s;
}

// Repoint the boundary→GetBufferComponents SRV wire keyed by input def guid (srcSlot) at a fixture producer.
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
int runT3SnapToPointsParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Two equal-length bags. Points1 (A) on a circle; Points2 (B) pulled toward the origin (0.55x) so each
  // point snaps a non-trivial, per-point-varying distance (dist < Distance=1 → strong snap).
  const uint32_t N = 24;
  std::vector<SwPoint> inA(N), inB(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    inA[i] = SwPoint{};
    inA[i].Position = SW_PACKED3{ (float)(std::cos(a * 6.2831853) * 1.1 + 0.05),
                                  (float)(std::sin(a * 6.2831853) * 0.8 - 0.03),
                                  (float)((a - 0.5) * 1.4) };
    inA[i].Rotation = SW_FLOAT4{0, 0, 0, 1};
    inA[i].FX1 = 0.1f + 0.2f * (float)i; inA[i].FX2 = 1.0f; inA[i].Scale = SW_PACKED3{1, 1, 1};
    inB[i] = inA[i];
    inB[i].Position = SW_PACKED3{ inA[i].Position.x * 0.55f, inA[i].Position.y * 0.55f,
                                  inA[i].Position.z * 0.55f };
    inB[i].FX1 = 5.0f;  // distinct W target so the W-lerp is observable
  }
  g_fixtureA = &inA; g_fixtureB = &inB;

  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[snaptopoints-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[snaptopoints-retire] ②parity FAIL: no root symbol\n"); g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1; }

  if (!repointSrvAtFixture(sym, lib, kInPointsA, "t3xf_snap_a") ||
      !repointSrvAtFixture(sym, lib, kInPointsB, "t3xf_snap_b")) {
    printf("[snaptopoints-retire] ②parity FAIL: no Points1/Points2→GetBufferComponents wire\n");
    g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1;
  }

  // 骨7 boundary injection: feed the b0 scalars (all three, in order). injectBug PERTURBS MaxAmount (scales
  // the Position snap continuously) while the oracle keeps clean kMaxAmount → the Position readback DIVERGES.
  const float maxAmtInjected = injectBug ? 0.1f : kMaxAmount;
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInBlendFactor] = {kBlendFactor};    // b0[0]
  boundaryFloatInputs[kInDistance]    = {kDistance};       // b0[1]
  boundaryFloatInputs[kInMaxAmount]   = {maxAmtInjected};  // b0[2]

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[snaptopoints-retire] ②parity FAIL: no ExecuteBufferUpdate\n"); g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[snaptopoints-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixtureA = g_fixtureB = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // Oracle: the mathv-verified CPU ref, fed the CLEAN params.
  mathv_ref::SnapToPointsParams prm{};
  prm.blendFactor = kBlendFactor; prm.distance = kDistance; prm.maxAmount = kMaxAmount;
  std::vector<SwPoint> exp(N);
  mathv_ref::mathvRefSnapToPoints(inA.data(), inB.data(), exp.data(), N, prm);

  // Compare Position + FX1 (the only fields the kernel writes).
  double maxPosErr = 0.0, maxWErr = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      float dx = exp[i].Position.x - got[i].Position.x;
      float dy = exp[i].Position.y - got[i].Position.y;
      float dz = exp[i].Position.z - got[i].Position.z;
      double e = std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
      if (e > maxPosErr) { maxPosErr = e; worstI = (int)i; }
      maxWErr = std::max(maxWErr, (double)std::fabs(exp[i].FX1 - got[i].FX1));
    }
  printf("[snaptopoints-retire] ②parity: haveOut=%d maxPosErr=%.6f maxWErr=%.6f (need<1e-3) worstI=%d\n",
         haveOut ? 1 : 0, maxPosErr, maxWErr, worstI);

  // did-not-trip guard: the clean run must actually MOVE points (non-identity snap).
  double maxMove = 0.0;
  if (haveOut) for (uint32_t i = 0; i < N; ++i) {
    float dx = inA[i].Position.x - got[i].Position.x, dy = inA[i].Position.y - got[i].Position.y,
          dz = inA[i].Position.z - got[i].Position.z;
    maxMove = std::max(maxMove, std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz));
  }

  mlib->release(); q->release(); dev->release();
  g_fixtureA = g_fixtureB = nullptr;

  const bool parityGreen = haveOut && (maxPosErr < 1e-3) && (maxWErr < 1e-3) && (maxMove > 1e-3);
  if (!injectBug) {
    printf("[snaptopoints-retire] ②parity VERDICT: %s (maxMove=%.4f)\n", parityGreen ? "GREEN" : "RED", maxMove);
    pool->release();
    return parityGreen ? 0 : 1;
  }
  const bool bites = !parityGreen;
  printf("[snaptopoints-retire] ②parity -bug: %s\n", bites ? "BITES (MaxAmount injection load-bearing)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

// ── ④ LAYOUT (inline — .t3ui Position constants) ──────────────────────────────────────────────────
int runSnapToPointsLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[snaptopoints-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };

  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[snaptopoints-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[snaptopoints-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* p1  = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPoints1) p1 = &d;
  const SlotDef* bl  = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinBlend)   bl = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput)  out = &d;
  if (!p1 || !bl || !out) {
    printf("[snaptopoints-retire] ④layout FAIL: p1=%p bl=%p out=%p\n", (void*)p1, (void*)bl, (void*)out);
    return 1;
  }
  const bool p1Ok = injectBug ? (nearf(p1->x, 0) && nearf(p1->y, 0)) : (nearf(p1->x, kP1X) && nearf(p1->y, kP1Y));
  const bool blOk = injectBug ? (nearf(bl->x, 0) && nearf(bl->y, 0)) : (nearf(bl->x, kBlX) && nearf(bl->y, kBlY));
  const bool ouOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOuX) && nearf(out->y, kOuY));
  printf("[snaptopoints-retire] ④layout: p1(%.3f,%.3f) bl(%.3f,%.3f) out(%.3f,%.3f)\n",
         p1->x, p1->y, bl->x, bl->y, out->x, out->y);

  const bool distinct = !(nearf(p1->x, bl->x) && nearf(p1->y, bl->y)) &&
                        !(nearf(p1->x, out->x) && nearf(p1->y, out->y)) &&
                        !(nearf(bl->x, out->x) && nearf(bl->y, out->y));
  const bool nonZero = !(nearf(p1->x, 0) && nearf(p1->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[snaptopoints-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (p1Ok && blOk && ouOk) ? 0 : 1;
  }
  const bool bites = (p1Ok && blOk && ouOk);  // under -bug all reverted to 0,0
  return bites ? 1 : 0;
}

int runT3SnapToPointsRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[snaptopoints-retire] FAIL: import\n"); return 1;
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
  printf("[snaptopoints-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
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
  printf("[snaptopoints-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  // ② PARITY (cook-driven)
  const int g2 = runT3SnapToPointsParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[snaptopoints-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  // ④ LAYOUT
  const int g4 = runSnapToPointsLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[snaptopoints-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[snaptopoints-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[snaptopoints-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
