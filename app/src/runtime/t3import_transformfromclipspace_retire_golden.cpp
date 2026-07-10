// runtime/t3import_transformfromclipspace_retire_golden — 廢棄節點退場 harness
// (--selftest-t3-transformfromclipspace-retire).
//
// Retires the flat TransformPointsFromClipspace point atom: its human-name references are
// AUTO-TAKEN-OVER by the nested .t3 compound (assets/catalog_t3/TransformFromClipSpace.t3, guid
// 81377edc…). Four gates, each a MEASURED RED→GREEN tooth. Expected values are TiXL constants (the
// .t3 guid + the .t3ui Position numbers) and the mathv-verified TransformFromClipSpace oracle — never
// sw's own output (P5-safe).
//
// ── ★ NAME FORK — the reason this retirement needed a NEW machine extension ────────────────────────
// sw registered this flat atom as "TransformPointsFromClipspace" (after the .hlsl FILENAME —
// TransformPointsFromClipspace.hlsl / point_ops_transformpointsfromclipspace.cpp's own header:
// "@tixl: TransformFromClipSpace — sw filename forks the TiXL op id"), while the .t3's OWN embedded
// name (parsed from its `"Id": "81377edc…"/*TransformFromClipSpace*/` comment, which becomes
// Symbol.name at import) is "TransformFromClipSpace" — TiXL's canonical name. graph_bridge.cpp's Pass
// 2 name-alias (keyed by Symbol.name) therefore does NOT cover sw's actual registered name. This
// golden is what PROVES the fix: graph_bridge.cpp's new Pass 3 kLegacyNameAlias table maps
// "TransformFromClipSpace" -> "TransformPointsFromClipspace", so findSpec("TransformPointsFromClipspace")
// (kName below — the string every EXISTING sw reference/scene actually uses) resolves to the compound
// once the flat atom's sink row is gone. Without Pass 3, gate ① and ③ below would stay RED forever.
//
// ── CAMERA — the first retirement to READ a live TransformsConstBuffer matrix ──────────────────────
// TransformFromClipSpace.hlsl's `cbuffer Params : register(b0) {}` is declared EMPTY (zero authored
// scalars); the .t3 still wires a FloatsToBuffer into ComputeShaderStage.ConstantBuffers (wire order:
// FloatsToBuffer first, TransformsConstBuffer second) but FloatsToBuffer's own Params/Vec4Params get
// ZERO wires -> buffer_ops_floatstobuffer.cpp early-returns (.cs:28-29 totalFloatCount==0) -> its
// output SwBuffer stays at the struct default (bytes=nullptr). buffer_ops_computeshaderstage.cpp's
// cook SKIPS any wired ConstantBuffers entry whose `->bytes` is null BEFORE assigning cb#-slot order
// (named fork `computestage-empty-floatstobuffer-reindex`, documented in full in
// computeshaderstage_transformfromclipspace.metal) — so TransformsConstBuffer becomes cbs[0] and lands
// at **b0**, not the b1 its wire position would naively suggest. This ②parity gate is the end-to-end
// numeric proof that (a) the reindex lands the Transforms buffer where the kernel expects it and (b)
// the kernel's un-transpose read of TransformsConstBuffer's raw bytes (transposed row-major storage,
// see the kernel's header comment) recovers the SAME CameraToWorld matrix the flat op's own
// PointCookCtx::cameraToWorld seam already uses (both ultimately call defaultLayerCameraForward, per
// field_camera.cpp / view_camera_active.cpp — CameraToWorld = inverse(WorldToCamera) = inverse(
// lookAtRH(eye,target,up)), which is ASPECT-INDEPENDENT, so any test aspect reproduces the identical
// matrix).
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: findSpec("TransformPointsFromClipspace") falls through the atom sinks to the
//     compound's LEGACY name alias (Pass 3) → COMPOUND spec (type==guid). injectBug pushes a stand-in
//     atom into a live sink → BITE.
//  ② PARITY (cook-driven): import TransformFromClipSpace.t3 → buildEvalGraph → cookResident (generic
//     ComputeShaderStage; the resident buffer cook's camera bridge fills the wired TransformsConstBuffer
//     with the DEFAULT camera) → readback Position+Rotation vs mathvRefTransformFromClipSpace fed the
//     SAME default-camera CameraToWorld (host-computed via pointCameraMatrices, independently of the
//     cook path). injectBug WITHHOLDS the Points fixture wire → the SRV/UAV element counts collapse to
//     0 → no dispatch → readback diverges (count 0 vs N).
//  ③ REFERENCE REACHABILITY + COOKABILITY: name → spec with boundary I/O + non-empty buildEvalGraph.
//     injectBug drops the compound registration → findSpec nullptr → BITE.
//  ④ LAYOUT: .t3ui Points/Output pins land on their Position constants; -bug → 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ZONE: runtime golden (shell tier — binds importer + refreshCompoundSpecs seam + the mathv oracle).
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
#include "runtime/field_camera.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"
#include "runtime/point_modify_op_registry.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/sw_buffer.h"
#include "runtime/t3_import.h"
#include "runtime/tixl_point.h"
#include "mathv_ref_transformfromclipspace.h"

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/transformfromclipspace_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/transformfromclipspace_t3ui_embed.inc"
;

const char* const kGuid = "81377edc-0a42-4bb1-9440-2f2433d5757f";
// The LEGACY sw-registered name (NOT the .t3's own embedded "TransformFromClipSpace" — see the ★NAME
// FORK note above). This is the string every findSpec/makeNode/.swproj reference actually uses today.
const char* const kName = "TransformPointsFromClipspace";
// .t3ui pins (④): Points / Output — verbatim from TransformFromClipSpace.t3ui.
const char* const kPinPoints = "e02d3e37-4da6-4528-b06f-6f26c818d1d8";
const char* const kPinOutput = "fa70200b-cfcb-4efe-afbd-48cefea1ca39";
constexpr float kPtsX = -510.71268f, kPtsY = 632.316f;
constexpr float kOutX = 812.7836f, kOutY = 534.6297f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

std::vector<SwPoint>* g_fixturePts = nullptr;
void cookFixturePts(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes || !g_fixturePts) return;
  const uint32_t n = (uint32_t)g_fixturePts->size();
  if (n == 0) return;
  void* dst = c.requestBytes(n * (uint32_t)sizeof(SwPoint));
  if (!dst) return;
  std::memcpy(dst, g_fixturePts->data(), n * sizeof(SwPoint));
  c.output->elementStride = (uint32_t)sizeof(SwPoint);
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}
NodeSpec fixtureSpec() {
  NodeSpec s; s.type = "t3xf_tfcs_input"; s.title = "t3xf_tfcs_input"; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_tfcs_input(fixtureSpec(), cookFixturePts);

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
NodeSpec standInFlatAtomSpec() {
  NodeSpec s; s.type = kName; s.title = kName; s.category = "point.transform";
  s.ports = {{"GPoints", "GPoints", "Points", true}, {"out", "out", "Points", false}};
  s.evaluate = nullptr; return s;
}

}  // namespace

int runT3TransformFromClipSpaceParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Fixture: N points with non-trivial Position AND non-identity Rotation (so the qMul path is also
  // exercised, not just an identity-rotation passthrough).
  const uint32_t N = 10;
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)(N - 1);
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{ (float)((a - 0.5) * 2.0), (float)(std::sin(a * 6.2831853) * 0.4),
                                 (float)(std::cos(a * 6.2831853) * 0.4) };
    // A non-identity rotation per point (rotate about Z by a*90deg): (0,0,sin(ang/2),cos(ang/2)).
    double ang = a * (M_PI * 0.5);
    in[i].Rotation = SW_FLOAT4{ 0.0f, 0.0f, (float)std::sin(ang * 0.5), (float)std::cos(ang * 0.5) };
    in[i].Color = SW_FLOAT4{1, 1, 1, 1}; in[i].FX1 = 1.0f; in[i].FX2 = 1.0f;
    in[i].Scale = SW_PACKED3{1, 1, 1};
  }
  g_fixturePts = &in;

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[tfcs-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixturePts = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[tfcs-retire] ②parity FAIL: no root\n"); g_fixturePts = nullptr; pool->release(); return 1; }

  auto childSym = [&](int id) -> std::string {
    for (const SymbolChild& c : sym->children) if (c.id == id) return c.symbolId;
    return std::string();
  };
  // Find the child receiving the boundary "Points" wire (must be a GetBufferComponents — the SRV feed;
  // the OTHER GetBufferComponents child in this .t3 receives its input from the output SBV, not the
  // boundary, so there is no ambiguity here, but the type check is kept for the same safety precedent
  // WrapPointPosition's golden documents).
  const int gbc = [&]{
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstSlot == "BufferWithViews" &&
          childSym(w.dstChild) == "GetBufferComponents") return w.dstChild;
    return 0; }();
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!gbc || !ebuId) {
    printf("[tfcs-retire] ②parity FAIL: no Points->GetBufferComponents wire or no ExecuteBufferUpdate (gbc=%d ebu=%d)\n", gbc, ebuId);
    g_fixturePts = nullptr; pool->release(); return 1;
  }

  if (!lib.symbols.count("t3xf_tfcs_input"))
    if (const NodeSpec* fs = findSpec("t3xf_tfcs_input")) lib.symbols["t3xf_tfcs_input"] = atomicSymbolFromSpec(*fs);
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_tfcs_input"; sym->children.push_back(p); }
  // RED tooth (injectBug): DON'T repoint — the boundary "Points" wire stays unfed (no producer in this
  // isolated cook) -> the SRV/UAV element counts collapse to 0 -> no dispatch -> readback diverges.
  if (!injectBug) {
    for (SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstChild == gbc && w.dstSlot == "BufferWithViews") {
        w.srcChild = fixtureId; w.srcSlot = "Buffer";
      }
  }

  ResidentEvalGraph g = buildEvalGraph(lib, rootId);  // no scalar boundary inputs to inject (b0 Params empty)
  initResidentCache(g);
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[tfcs-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixturePts = nullptr; pool->release(); return 1; }

  const uint32_t W = 64, H = 64;  // aspect=1.0; CameraToWorld is aspect-independent (lookAtRH has no aspect term)
  PointGraph pg(dev, mlib, q, W, H);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // Oracle: the SAME default-camera CameraToWorld the resident buffer cook's camera bridge computes
  // (fillBufferCamera -> activeViewCameraForward -> defaultLayerCameraForward, no live override engaged
  // in a selftest process) — computed HOST-SIDE via the point-rail's equivalent, pointCameraMatrices
  // (both funnel through the identical defaultLayerCameraForward(aspect); aspect-independent result).
  float o2cUnused[16], cameraToWorld[16];
  pointCameraMatrices((float)W / (float)H, o2cUnused, cameraToWorld);
  mathv_ref::TransformFromClipSpaceParams prm{};
  {
    // cameraToWorld[16] is row-major (m[row*4+col], field_camera.h's Mat4 convention) — assign field by
    // field into the SAME convention's named struct (mathv_ref_transformfromclipspace.h's Mat4.m_rc).
    mathv_ref::Mat4& m = prm.cameraToWorld;
    const float* c = cameraToWorld;
    m.m00 = c[0];  m.m01 = c[1];  m.m02 = c[2];  m.m03 = c[3];
    m.m10 = c[4];  m.m11 = c[5];  m.m12 = c[6];  m.m13 = c[7];
    m.m20 = c[8];  m.m21 = c[9];  m.m22 = c[10]; m.m23 = c[11];
    m.m30 = c[12]; m.m31 = c[13]; m.m32 = c[14]; m.m33 = c[15];
  }
  std::vector<SwPoint> exp(N);
  mathv_ref::mathvRefTransformFromClipSpace(in.data(), exp.data(), N, prm);

  double maxPosErr = 0.0, maxRotErr = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      float dx = exp[i].Position.x - got[i].Position.x;
      float dy = exp[i].Position.y - got[i].Position.y;
      float dz = exp[i].Position.z - got[i].Position.z;
      double pe = std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
      double dot = (double)exp[i].Rotation.x * got[i].Rotation.x + (double)exp[i].Rotation.y * got[i].Rotation.y +
                   (double)exp[i].Rotation.z * got[i].Rotation.z + (double)exp[i].Rotation.w * got[i].Rotation.w;
      double re = std::fabs(std::fabs(dot) - 1.0);
      if (pe > maxPosErr) { maxPosErr = pe; worstI = (int)i; }
      if (re > maxRotErr) maxRotErr = re;
    }
  double maxMove = 0.0;  // did-not-trip guard: the unproject must actually move points off z=identity plane
  if (haveOut) for (uint32_t i = 0; i < N; ++i) {
    float dz = got[i].Position.z - in[i].Position.z;
    maxMove = std::max(maxMove, (double)std::fabs(dz));
  }
  printf("[tfcs-retire] ②parity: haveOut=%d count=%u(need %u) maxPosErr=%.6f(need<1e-3) maxRotErr=%.6f(need<1e-3) worstI=%d maxMove=%.4f\n",
         haveOut ? 1 : 0, outBuf ? outBuf->elementCount : 0u, N, maxPosErr, maxRotErr, worstI, maxMove);

  mlib->release(); q->release(); dev->release();
  g_fixturePts = nullptr;

  const bool parityGreen = haveOut && (maxPosErr < 1e-3) && (maxRotErr < 1e-3) && (maxMove > 1e-3);
  if (!injectBug) {
    printf("[tfcs-retire] ②parity VERDICT: %s\n", parityGreen ? "GREEN" : "RED");
    pool->release();
    return parityGreen ? 0 : 1;
  }
  const bool bites = !parityGreen;  // no Points fixture wired -> UAV/SRV stay 0-length -> diverges
  printf("[tfcs-retire] ②parity -bug: %s\n", bites ? "BITES (Points SRV reaches kernel)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

int runTransformFromClipSpaceLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[tfcs-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };
  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[tfcs-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[tfcs-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* pts = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinPoints) pts = &d;
  const SlotDef* out = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput) out = &d;
  if (!pts || !out) {
    printf("[tfcs-retire] ④layout FAIL: pts=%p out=%p\n", (void*)pts, (void*)out);
    return 1;
  }
  const bool ptsOk = injectBug ? (nearf(pts->x, 0) && nearf(pts->y, 0)) : (nearf(pts->x, kPtsX) && nearf(pts->y, kPtsY));
  const bool outOk = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0)) : (nearf(out->x, kOutX) && nearf(out->y, kOutY));
  printf("[tfcs-retire] ④layout: pts(%.3f,%.3f) out(%.3f,%.3f)\n", pts->x, pts->y, out->x, out->y);
  const bool distinct = !(nearf(pts->x, out->x) && nearf(pts->y, out->y));
  const bool nonZero = !(nearf(pts->x, 0) && nearf(pts->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[tfcs-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (ptsOk && outOk) ? 0 : 1;
  }
  const bool bites = (ptsOk && outOk);
  return bites ? 1 : 0;
}

int runT3TransformFromClipSpaceRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[tfcs-retire] FAIL: import\n"); return 1;
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
  printf("[tfcs-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
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
  printf("[tfcs-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  const int g2 = runT3TransformFromClipSpaceParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[tfcs-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  const int g4 = runTransformFromClipSpaceLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[tfcs-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[tfcs-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[tfcs-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
