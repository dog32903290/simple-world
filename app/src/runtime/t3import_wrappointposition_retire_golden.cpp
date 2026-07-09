// runtime/t3import_wrappointposition_retire_golden — 廢棄節點退場 harness
// (--selftest-t3-wrappointposition-retire).
//
// Retires the flat WrapPointPosition point atom: its human-name references are AUTO-TAKEN-OVER by the
// nested .t3 compound (assets/catalog_t3/WrapPointPosition.t3, guid 0814a593…). Four gates, each a
// MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3 guid + the .t3ui Position
// numbers) and the mathv-verified WrapPointPosition oracle — never sw's own output (P5-safe).
//
// ── IN-PLACE (no SRV) — the distinguishing feature ───────────────────────────────────────────────
// WrapPointPosition.hlsl declares `RWStructuredBuffer<LegacyPoint> ResultPoints : u0;` and reads AND
// writes it — NO SourcePoints SRV. The .t3 wires the input GPoints buffer straight onto
// ComputeShaderStage.Uavs (u0) via GetBufferComponents; no ShaderResources. This ②parity gate is the
// end-to-end proof of the generic-seam IN-PLACE dispatch extension (buffer_ops_computeshaderstage.cpp
// computestage-inplace-uav-no-srv): the cook sizes the dispatch from the UAV and the ABI-repacked
// computeshaderstage_wrappointposition.metal wraps each point in place.
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: findSpec("WrapPointPosition") falls through the atom sinks to the compound's
//     NAME alias → COMPOUND spec (type==guid). injectBug pushes a stand-in atom into a live sink → BITE.
//  ② PARITY (cook-driven): import WrapPointPosition.t3 → buildEvalGraph (骨7 inject Size) → cookResident
//     (generic ComputeShaderStage IN-PLACE dispatch) → readback vs mathvRefWrapPointPosition. injectBug
//     PERTURBS the injected Size.x (the cube-fold band depends on it continuously) → readback diverges.
//  ③ REFERENCE REACHABILITY + COOKABILITY: name → spec with boundary I/O + non-empty buildEvalGraph.
//     injectBug drops the compound registration → findSpec nullptr → BITE.
//  ④ LAYOUT: .t3ui Size/GPoints/Output pins land on their Position constants; -bug → 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG (deterministic, exact) ─────────────────────────────────────────────────────────
// Inject ONLY Size (vec3 → Value.x head → (Sx,0,0), fork-t3-vec3-wire-lands-on-head); Center/UseCamera/
// AddLineBreaks drop to 0 (→ center=0, WriteLineBreaks=0 edge-fade path). With Size=(2.5,0,0), padded.x=
// 2.5*0.5 + 2.5*0.1 = 1.5; fixture x spans [-3.5,3.5] so points beyond ±1.5 CUBE-FOLD by ∓2.5 on x
// (y/z unfolded since Size.y=Size.z=0 → wrappedP.n = p.n). The oracle is fed the SAME (2.5,0,0). Pure
// exact arithmetic (compare/abs/add), matchable to <1e-3.
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
#include "mathv_ref_wrappointposition.h"

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/wrappointposition_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/wrappointposition_t3ui_embed.inc"
;

const char* const kGuid = "0814a593-80ab-416f-a3ca-eef78b0a9c0c";
const char* const kName = "WrapPointPosition";
const char* const kInSize = "1d054b2e-1e1b-4899-a003-0d6e000d2d8d";  // vec3 → Value.x head
// .t3ui pins (④): Size / GPoints / Output — verbatim from WrapPointPosition.t3ui.
const char* const kPinSize   = "1d054b2e-1e1b-4899-a003-0d6e000d2d8d";
const char* const kPinPoints = "dbe72c8b-6cc2-454b-83db-712f0cd4211c";
const char* const kPinOutput = "2889b8bf-5bb2-48f8-8fe0-02f95282c5f1";
constexpr float kSzX = -233.76004f, kSzY = 686.2818f;
constexpr float kPtsX = -238.81335f, kPtsY = 566.684f;
constexpr float kOutX = 812.4039f,   kOutY = 360.61105f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

constexpr float kSizeX = 2.5f;  // parity Size.x (vec .x head)

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
  NodeSpec s; s.type = "t3xf_wrap_input"; s.title = "t3xf_wrap_input"; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_wrap_input(fixtureSpec(), cookFixture);

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

int runT3WrapPointPositionParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Fixture: points spanning x∈[-3.5,3.5] (beyond padded.x=1.5) so the cube fold on x is exercised.
  const uint32_t N = 24;
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double t = ((double)i / (double)(N - 1)) * 2.0 - 1.0;  // -1..1
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{ (float)(t * 3.5), (float)(std::sin(t * 3.0) * 0.13),
                                 (float)(t * 0.11) };
    in[i].Rotation = SW_FLOAT4{0, 0, 0, 1};
    in[i].FX1 = 1.0f; in[i].FX2 = 1.0f; in[i].Scale = SW_PACKED3{1, 1, 1};
  }
  g_fixture = &in;

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[wrap-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixture = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[wrap-retire] ②parity FAIL: no root\n"); g_fixture = nullptr; pool->release(); return 1; }

  // NB: BOTH GetBufferComponents.BufferWithViews AND ExecuteBufferUpdate.BufferToUpdate map to the sw slot
  // name "BufferWithViews" (t3_import_maps). The input buffer must feed the GetBufferComponents that drives
  // ComputeShaderStage.Uavs — so require the dst child to be a GetBufferComponents (else the fixture would
  // repoint onto ExecuteBufferUpdate.BufferToUpdate and the real GBC stays boundary-empty → no dispatch).
  auto childSym = [&](int id) -> std::string {
    for (const SymbolChild& c : sym->children) if (c.id == id) return c.symbolId;
    return std::string();
  };
  const int gbc = [&]{
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstSlot == "BufferWithViews" &&
          childSym(w.dstChild) == "GetBufferComponents") return w.dstChild;
    return 0; }();
  if (!gbc) { printf("[wrap-retire] ②parity FAIL: no Points→GetBufferComponents wire\n"); g_fixture = nullptr; pool->release(); return 1; }
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_wrap_input"; sym->children.push_back(p); }
  if (!lib.symbols.count("t3xf_wrap_input"))
    if (const NodeSpec* fs = findSpec("t3xf_wrap_input")) lib.symbols["t3xf_wrap_input"] = atomicSymbolFromSpec(*fs);
  for (SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.dstChild == gbc && w.dstSlot == "BufferWithViews") {
      w.srcChild = fixtureId; w.srcSlot = "Buffer";
    }

  // 骨7 inject Size (vec .x head). injectBug perturbs Size.x → the cube-fold band shifts → divergence
  // (proves the injected boundary value reaches the kernel cbuffer through FloatsToBuffer).
  const float sizeInjected = injectBug ? 7.0f : kSizeX;  // bug: 7.0 → padded.x=4.2 > all |x| → NO fold
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInSize] = {sizeInjected};

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[wrap-retire] ②parity FAIL: no ExecuteBufferUpdate\n"); g_fixture = nullptr; pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[wrap-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixture = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // Oracle fed the SAME resolved params: center=0, useCamera=0, size=(kSizeX,0,0), writeLineBreaks=0.
  mathv_ref::WrapPointPositionParams prm{};
  prm.centerX = prm.centerY = prm.centerZ = 0.0f;
  prm.useCamera = 0.0f;
  prm.sizeX = kSizeX; prm.sizeY = 0.0f; prm.sizeZ = 0.0f;
  prm.writeLineBreaks = 0.0f;
  prm.camToWorldTx = prm.camToWorldTy = prm.camToWorldTz = 0.0f;
  std::vector<SwPoint> exp(N);
  mathv_ref::mathvRefWrapPointPosition(in.data(), exp.data(), N, prm);

  double maxErr = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      float dx = exp[i].Position.x - got[i].Position.x;
      float dy = exp[i].Position.y - got[i].Position.y;
      float dz = exp[i].Position.z - got[i].Position.z;
      double e = std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
      if (e > maxErr) { maxErr = e; worstI = (int)i; }
    }
  double maxMove = 0.0;  // did-not-trip guard: the fold must actually move points
  if (haveOut) for (uint32_t i = 0; i < N; ++i) {
    float dx = in[i].Position.x - got[i].Position.x;
    maxMove = std::max(maxMove, (double)std::fabs(dx));
  }
  printf("[wrap-retire] ②parity: haveOut=%d maxPosErr=%.6f(need<1e-3) worstI=%d maxMove=%.4f\n",
         haveOut ? 1 : 0, maxErr, worstI, maxMove);

  mlib->release(); q->release(); dev->release();
  g_fixture = nullptr;

  const bool parityGreen = haveOut && (maxErr < 1e-3) && (maxMove > 1e-3);
  if (!injectBug) {
    printf("[wrap-retire] ②parity VERDICT: %s\n", parityGreen ? "GREEN" : "RED");
    pool->release();
    return parityGreen ? 0 : 1;
  }
  const bool bites = !parityGreen;  // Size.x=7 → no fold → diverges from the kSizeX oracle
  printf("[wrap-retire] ②parity -bug: %s\n", bites ? "BITES (Size reaches kernel)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

int runWrapLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[wrap-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };
  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[wrap-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[wrap-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* sz  = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinSize)   sz  = &d;
  const SlotDef* pts = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPoints) pts = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput) out = &d;
  if (!sz || !pts || !out) {
    printf("[wrap-retire] ④layout FAIL: sz=%p pts=%p out=%p\n", (void*)sz, (void*)pts, (void*)out);
    return 1;
  }
  const bool szOk  = injectBug ? (nearf(sz->x, 0) && nearf(sz->y, 0))   : (nearf(sz->x, kSzX)  && nearf(sz->y, kSzY));
  const bool ptsOk = injectBug ? (nearf(pts->x, 0) && nearf(pts->y, 0)) : (nearf(pts->x, kPtsX) && nearf(pts->y, kPtsY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[wrap-retire] ④layout: sz(%.3f,%.3f) pts(%.3f,%.3f) out(%.3f,%.3f)\n",
         sz->x, sz->y, pts->x, pts->y, out->x, out->y);
  const bool distinct = !(nearf(sz->x, pts->x) && nearf(sz->y, pts->y)) &&
                        !(nearf(sz->x, out->x) && nearf(sz->y, out->y)) &&
                        !(nearf(pts->x, out->x) && nearf(pts->y, out->y));
  const bool nonZero = !(nearf(sz->x, 0) && nearf(sz->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[wrap-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (szOk && ptsOk && outOk) ? 0 : 1;
  }
  const bool bites = (szOk && ptsOk && outOk);
  return bites ? 1 : 0;
}

int runT3WrapPointPositionRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[wrap-retire] FAIL: import\n"); return 1;
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
  printf("[wrap-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
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
  printf("[wrap-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  const int g2 = runT3WrapPointPositionParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[wrap-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  const int g4 = runWrapLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[wrap-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[wrap-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[wrap-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
