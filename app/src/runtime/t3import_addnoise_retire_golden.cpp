// runtime/t3import_addnoise_retire_golden — 廢棄節點退場 harness (--selftest-t3-addnoise-retire).
//
// Retires the flat AddNoise point atom: its human-name references are AUTO-TAKEN-OVER by the nested .t3
// compound (assets/catalog_t3/AddNoise.t3, guid dd586355…). Four gates, each a MEASURED RED→GREEN tooth.
// Expected values are TiXL constants (the .t3 guid + the .t3ui Position numbers) and the mathv-verified
// AddNoise oracle — never sw's own output (P5-safe).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: findSpec("AddNoise") → the compound (type==guid); -bug stand-in atom shadows.
//  ② PARITY (cook-driven, TRANSCENDENTAL fraction gate): import AddNoise.t3 → buildEvalGraph (骨7 inject
//     Amount/Frequency/AmountDistribution/RotationLookupDistance) → cookResident (generic ComputeShaderStage
//     SRV+UAV dispatches computeshaderstage_addnoise) → readback Position vs mathvRefAddNoise. AddNoise is a
//     simplex-noise op (transcendental): CPU ref and fast-math GPU agree to ~1e-3 on the bulk but a few
//     samples ride a simplex-cell boundary and diverge more, so the gate is a DOUBLE FRACTION GATE
//     (MATH_VERIFY_WORKFLOW §2.2): ≥90% within a tight abs tol AND 100% within a loose tol — matching the
//     mathv AddNoise pilot's transcendental class. injectBug PERTURBS the injected Amount (the offset
//     magnitude scales with it) → the bulk shifts well past the tolerance → the fraction gate fails → BITE.
//  ③ REFERENCE REACHABILITY + COOKABILITY: name → spec with boundary I/O + non-empty buildEvalGraph.
//     injectBug drops the compound registration → findSpec nullptr → BITE.
//  ④ LAYOUT: .t3ui Strength/Points/Output pins land on their Position constants; -bug → 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG ────────────────────────────────────────────────────────────────────────────────
// Inject Amount=2, Frequency=1, AmountDistribution=1 (vec .x head → (1,0,0), so the displacement is
// x-only: offset.x = snoiseVec3(pos*0.91).x * Amount/10), RotationLookupDistance=0.25 (keeps the rotation
// frame finite; Position is independent of it). Variation/Phase/NoiseOffset drop to 0 (no hash → the
// displacement is a deterministic function of position; StrengthMode=0 → weight=1). The oracle is fed the
// SAME resolved params. Only Position is compared (the rotation-frame branch amplifies fast-math noise
// divergence and mathv already owns the full kernel corpus).
//
// ZONE: runtime golden (shell tier — binds importer + refreshCompoundSpecs seam + the mathv oracle).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_modify_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/sw_buffer.h"
#include "runtime/t3_import.h"
#include "runtime/tixl_point.h"
#include "mathv_ref_addnoise.h"

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/addnoise_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/addnoise_t3ui_embed.inc"
;

const char* const kGuid = "dd586355-64b3-4e96-af6d-b4927595dee7";
const char* const kName = "AddNoise";
const char* const kInAmount   = "5894156a-cc31-4236-908c-de0e5385fd84";  // Strength
const char* const kInFrequency = "929db7b2-f19c-4a28-b4c2-187365b99760";
const char* const kInPhase     = "aaba1602-e7a1-4b48-81d4-9d7b2b3aa8b1";
const char* const kInVariation = "1dfb45ae-b376-41ea-a1d2-97b170645b50";
const char* const kInAmountDist = "c2df1fa3-88e1-4be2-954e-8c44edd9d421";  // vec3 → Value.x head
const char* const kInRotLookup = "97c25ec6-ef71-42f8-9352-52baf2ce41a4";  // RotationLookupDistance
// .t3ui pins (④): Strength / Points / Output — verbatim from AddNoise.t3ui.
const char* const kPinStrength = "5894156a-cc31-4236-908c-de0e5385fd84";
const char* const kPinPoints   = "3f5abde2-66e1-4b04-9bff-5a19a58aab86";
const char* const kPinOutput   = "bea6aa18-e751-4ce7-b7d7-b7a026c8e019";
constexpr float kStrX = 70.217224f, kStrY = 983.1664f;
constexpr float kPtsX = -129.59567f, kPtsY = 720.00745f;
constexpr float kOutX = 473.44156f,  kOutY = 858.99976f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

constexpr float kAmount = 2.0f, kFrequency = 1.0f, kAmountDistX = 1.0f, kRotLookup = 0.25f;
// transcendental double fraction gate (MATH_VERIFY_WORKFLOW §2.2): ≥90% within tight, 100% within loose.
constexpr float kTightTol = 3e-3f, kLooseTol = 5e-2f;

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
  NodeSpec s; s.type = "t3xf_addnoise_input"; s.title = "t3xf_addnoise_input"; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_addnoise_input(fixtureSpec(), cookFixture);

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

}  // namespace

int runT3AddNoiseParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  const uint32_t N = 32;
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{ (float)(std::cos(a * 6.2831853) * 1.31 + 0.17),
                                 (float)(std::sin(a * 6.2831853) * 0.79 - 0.09),
                                 (float)((a - 0.5) * 1.7 + 0.05) };
    in[i].Rotation = SW_FLOAT4{0, 0, 0, 1};
    in[i].FX1 = 1.0f; in[i].FX2 = 1.0f; in[i].Scale = SW_PACKED3{1, 1, 1};
  }
  g_fixture = &in;

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[addnoise-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixture = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[addnoise-retire] ②parity FAIL: no root\n"); g_fixture = nullptr; pool->release(); return 1; }

  auto childSym = [&](int id) -> std::string {
    for (const SymbolChild& c : sym->children) if (c.id == id) return c.symbolId;
    return std::string();
  };
  const int gbc = [&]{
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstSlot == "BufferWithViews" &&
          childSym(w.dstChild) == "GetBufferComponents") return w.dstChild;
    return 0; }();
  if (!gbc) { printf("[addnoise-retire] ②parity FAIL: no Points→GetBufferComponents wire\n"); g_fixture = nullptr; pool->release(); return 1; }
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_addnoise_input"; sym->children.push_back(p); }
  if (!lib.symbols.count("t3xf_addnoise_input"))
    if (const NodeSpec* fs = findSpec("t3xf_addnoise_input")) lib.symbols["t3xf_addnoise_input"] = atomicSymbolFromSpec(*fs);
  for (SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.dstChild == gbc && w.dstSlot == "BufferWithViews") {
      w.srcChild = fixtureId; w.srcSlot = "Buffer";
    }

  // Inject ALL DIRECT boundary scalars (Amount/Frequency/Phase/Variation/RotationLookupDistance) — even the
  // ones we want at 0 — so the FloatsToBuffer MultiInput assembles the FULL cbuffer in order. A boundary
  // scalar we DON'T inject has its wire drop, and the drop CLOSES THE GAP (the MultiInput collects only
  // wired sources with no padding), which would shift every later float to the wrong cbuffer slot. The
  // vector inputs (AmountDistribution/NoiseOffset) ride Vector3Components whose X/Y/Z→FloatsToBuffer wires
  // are ALWAYS present (internal), so their slots stay filled (0 when the boundary is un-injected); only
  // the .x head needs injecting to make the displacement non-zero.
  const float amtInjected = injectBug ? 0.30f : kAmount;
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInAmount]    = {amtInjected};  // b0[0]
  boundaryFloatInputs[kInFrequency] = {kFrequency};   // b0[1]
  boundaryFloatInputs[kInPhase]     = {0.0f};         // b0[2] — MUST inject to keep alignment
  boundaryFloatInputs[kInVariation] = {0.0f};         // b0[3] — MUST inject to keep alignment (also: no hash)
  boundaryFloatInputs[kInAmountDist] = {kAmountDistX};// b0[4] (Vector3Components.x → AmountDistribution.x)
  boundaryFloatInputs[kInRotLookup] = {kRotLookup};   // b0[7]

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[addnoise-retire] ②parity FAIL: no ExecuteBufferUpdate\n"); g_fixture = nullptr; pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[addnoise-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixture = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  mathv_ref::AddNoiseParams prm{};
  prm.amount = kAmount; prm.frequency = kFrequency; prm.phase = 0.0f; prm.variation = 0.0f;
  prm.amountDistX = kAmountDistX; prm.amountDistY = 0.0f; prm.amountDistZ = 0.0f;
  prm.rotationLookupDistance = kRotLookup;
  prm.noiseOffsetX = prm.noiseOffsetY = prm.noiseOffsetZ = 0.0f;
  prm.strengthMode = 0;
  std::vector<SwPoint> exp(N);
  mathv_ref::mathvRefAddNoise(in.data(), exp.data(), N, prm);

  int within = 0, gross = 0; double maxErr = 0.0, maxMove = 0.0;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      float dx = exp[i].Position.x - got[i].Position.x;
      float dy = exp[i].Position.y - got[i].Position.y;
      float dz = exp[i].Position.z - got[i].Position.z;
      double e = std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
      maxErr = std::max(maxErr, e);
      if (e < kTightTol) ++within;
      if (e > kLooseTol) ++gross;
      float mvx = in[i].Position.x - got[i].Position.x;
      maxMove = std::max(maxMove, (double)std::fabs(mvx));
    }
  const double frac = haveOut ? (double)within / (double)N : 0.0;
  printf("[addnoise-retire] ②parity: haveOut=%d within%.0e=%d/%u (%.0f%%, need>=90%%) gross>%.0e=%d(need 0) "
         "maxErr=%.5f maxMove=%.4f\n", haveOut ? 1 : 0, (double)kTightTol, within, N, frac * 100.0,
         (double)kLooseTol, gross, maxErr, maxMove);

  mlib->release(); q->release(); dev->release();
  g_fixture = nullptr;

  const bool parityGreen = haveOut && (frac >= 0.90) && (gross == 0) && (maxMove > 1e-2);
  if (!injectBug) {
    printf("[addnoise-retire] ②parity VERDICT: %s\n", parityGreen ? "GREEN" : "RED");
    pool->release();
    return parityGreen ? 0 : 1;
  }
  const bool bites = !parityGreen;  // Amount=0.3 → offsets shrink → bulk leaves the tolerance band
  printf("[addnoise-retire] ②parity -bug: %s\n", bites ? "BITES (Amount reaches kernel)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

int runAddNoiseLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[addnoise-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };
  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[addnoise-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[addnoise-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* str = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinStrength) str = &d;
  const SlotDef* pts = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPoints)   pts = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput)   out = &d;
  if (!str || !pts || !out) {
    printf("[addnoise-retire] ④layout FAIL: str=%p pts=%p out=%p\n", (void*)str, (void*)pts, (void*)out);
    return 1;
  }
  const bool strOk = injectBug ? (nearf(str->x, 0) && nearf(str->y, 0)) : (nearf(str->x, kStrX) && nearf(str->y, kStrY));
  const bool ptsOk = injectBug ? (nearf(pts->x, 0) && nearf(pts->y, 0)) : (nearf(pts->x, kPtsX) && nearf(pts->y, kPtsY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[addnoise-retire] ④layout: str(%.3f,%.3f) pts(%.3f,%.3f) out(%.3f,%.3f)\n",
         str->x, str->y, pts->x, pts->y, out->x, out->y);
  const bool distinct = !(nearf(str->x, pts->x) && nearf(str->y, pts->y)) &&
                        !(nearf(str->x, out->x) && nearf(str->y, out->y)) &&
                        !(nearf(pts->x, out->x) && nearf(pts->y, out->y));
  const bool nonZero = !(nearf(str->x, 0) && nearf(str->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[addnoise-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (strOk && ptsOk && outOk) ? 0 : 1;
  }
  const bool bites = (strOk && ptsOk && outOk);
  return bites ? 1 : 0;
}

int runT3AddNoiseRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[addnoise-retire] FAIL: import\n"); return 1;
  }
  refreshCompoundSpecs(lib);

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
  printf("[addnoise-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
         kName, g1spec ? g1spec->type.c_str() : "<null>", csym ? (int)csym->atomic : -1,
         csym ? (int)csym->children.size() : -1,
         injectBug ? (g1bit ? "BITES" : "TOOTHLESS") : (g1green ? "GREEN" : "RED"));

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
  printf("[addnoise-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  const int g2 = runT3AddNoiseParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[addnoise-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  const int g4 = runAddNoiseLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[addnoise-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[addnoise-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[addnoise-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
