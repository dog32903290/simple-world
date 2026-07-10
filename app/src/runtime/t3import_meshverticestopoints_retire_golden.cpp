// runtime/t3import_meshverticestopoints_retire_golden — 廢棄節點退場 harness
// (--selftest-t3-meshverticestopoints-retire).
//
// Retires the flat MeshVerticesToPoints point atom: its human-name references are AUTO-TAKEN-OVER by
// the nested .t3 compound (assets/catalog_t3/MeshVerticesToPoints.t3, guid 2467e1ed…). Four gates,
// each a MEASURED RED→GREEN tooth. Expected values are TiXL constants (the .t3 guid + the .t3ui
// Position numbers) and the mathv-verified MeshVerticesToPoints oracle — never sw's own output (P5-safe).
//
// ── MESH SRV, POINT UAV — the distinguishing feature ─────────────────────────────────────────────
// MeshVerticesToPoints.hlsl is a GENERATOR (one Point per Mesh vertex): the .t3's Mesh boundary input
// feeds `_MeshBufferComponents.MeshBuffers` (guid 5b9f1d97…, the SAME native mesh-bridge atom the
// TransformMesh/DisplaceMeshNoise goldens already proved) → GetBufferComponents → ComputeShaderStage's
// ShaderResources (t0, the SwVertex SRV). The output StructuredBufferWithViews already bakes Stride=64
// (TiXL's native Point stride == sw's 64B SwPoint) — unlike the mesh-OUTPUT retirement family
// (TransformMesh), there is NO PbrVertex 64→80 allocation fork here: the OUTPUT currency is Points,
// not Mesh, so no test-only stride override is needed. This ②parity gate is the end-to-end proof that
// a Mesh-consuming GENERATOR compound (SRV in, fresh Point UAV out) cooks through the generic
// ComputeShaderStage exactly as a Points-consuming MODIFIER does.
//
// ── the four gates (RETIREMENT_BATTLE_SPEC §5, MATH_VERIFY_WORKFLOW §8) ──────────────────────────
//  ① TAKEOVER POLARITY: findSpec("MeshVerticesToPoints") falls through the atom sinks to the
//     compound's NAME alias → COMPOUND spec (type==guid). injectBug pushes a stand-in atom into a live
//     sink → BITE.
//  ② PARITY (cook-driven): import MeshVerticesToPoints.t3 → buildEvalGraph (骨7 inject OffsetByTBN/W) →
//     cookResident (generic ComputeShaderStage: mesh SRV → fresh Point UAV) → readback vs
//     mathvRefMeshVerticesToPoints. injectBug WITHHOLDS the mesh fixture wire (mirrors
//     t3import_transformmesh_golden.cpp's mesh-bridge tooth) → the SRV never reaches the compute stage
//     → the UAV stays empty → readback diverges (count 0 vs N).
//  ③ REFERENCE REACHABILITY + COOKABILITY: name → spec with boundary I/O + non-empty buildEvalGraph.
//     injectBug drops the compound registration → findSpec nullptr → BITE.
//  ④ LAYOUT: .t3ui OffsetByTBN/Mesh/W/Output pins land on their Position constants; -bug → 0,0.
//
// A single injectBug bool drives all four teeth. did-not-trip → return 0 (GOLDEN_STANDARD 特徵3 / P1).
//
// ── PARITY CONFIG (deterministic, exact) ─────────────────────────────────────────────────────────
// Inject OffsetByTBN (vec3 → Value.x head, fork-t3-vec3-wire-lands-on-head) = 1.5 and W (plain float,
// full value) = 2.0 → OffsetByTBN=(1.5,0,0), OffsetScale=2.0 → Position = vertex + 3.0*Tangent (only the
// Tangent term survives; Bitangent/Normal terms vanish since OffsetByTBN.y/.z land at 0). Rotation is
// UNCHANGED by these params (pure TBN-basis math) so the SAME fixture also pins the rotation path. The
// oracle is fed the SAME resolved params. Pure exact arithmetic (compare/abs/add + qFromMatrix3Precise's
// closed-form branches), matchable to <1e-3.
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
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"
#include "runtime/point_modify_op_registry.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/sw_buffer.h"
#include "runtime/sw_mesh.h"
#include "runtime/t3_import.h"
#include "runtime/tixl_point.h"
#include "mathv_ref_meshverticestopoints.h"

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kT3 =
#include "runtime/meshverticestopoints_t3_embed.inc"
;
static const char* kT3ui =
#include "runtime/meshverticestopoints_t3ui_embed.inc"
;

const char* const kGuid = "2467e1ed-f7fc-4c90-8230-b80ba6b42a2d";
const char* const kName = "MeshVerticesToPoints";
const char* const kInOffsetByTBN = "664b9a97-0709-40d5-b0a0-651092e658af";  // vec3 → Value.x head
const char* const kInW = "e5ab7ae6-d8de-4c92-9130-1082e5a56ba1";            // plain float
// .t3ui pins (④): OffsetByTBN / Mesh / W / Output — verbatim from MeshVerticesToPoints.t3ui.
const char* const kPinOffsetByTBN = "664b9a97-0709-40d5-b0a0-651092e658af";
const char* const kPinMesh        = "b990cf29-00a5-4e39-8687-4502c7c7eebc";
const char* const kPinW           = "e5ab7ae6-d8de-4c92-9130-1082e5a56ba1";
const char* const kPinOutput      = "53089fc7-3f0b-46c4-81e1-04ecbb92efce";
constexpr float kTbnX = -32.39586f, kTbnY = 639.0343f;
constexpr float kMeshX = -559.72864f, kMeshY = 447.2469f;
constexpr float kWX = 107.60414f, kWY = 744.0343f;
constexpr float kOutX = 473.48828f, kOutY = 667.5932f;
constexpr float kLayoutEps = 0.01f;
bool nearf(float a, float b, float e = kLayoutEps) { return std::fabs(a - b) < e; }

constexpr float kOffsetByTbnX = 1.5f;  // parity OffsetByTBN (vec .x head)
constexpr float kOffsetScale = 2.0f;   // parity W (plain scalar)

std::vector<SwVertex>* g_fixtureVerts = nullptr;
void cookFixtureVerts(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes || !g_fixtureVerts) return;
  const uint32_t n = (uint32_t)g_fixtureVerts->size();
  if (n == 0) return;
  const uint32_t bytes = n * (uint32_t)sizeof(SwVertex);
  void* dst = c.requestBytes(bytes);
  if (!dst) return;
  std::memcpy(dst, g_fixtureVerts->data(), bytes);
  c.output->elementStride = (uint32_t)sizeof(SwVertex);  // 80
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}
NodeSpec fixtureSpec() {
  NodeSpec s; s.type = "t3xf_mvtp_input_verts"; s.title = "t3xf_mvtp_input_verts"; s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}}; s.evaluate = nullptr; return s;
}
const BufferOp _reg_t3xf_mvtp_input_verts(fixtureSpec(), cookFixtureVerts);

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}
void countPorts(const NodeSpec& s, int& nIn, int& nOut) {
  nIn = nOut = 0; for (const PortSpec& p : s.ports) (p.isInput ? nIn : nOut)++;
}
NodeSpec standInFlatAtomSpec() {
  NodeSpec s; s.type = kName; s.title = kName; s.category = "point.generate";
  s.ports = {{"Mesh", "Mesh", "Mesh", true}, {"out", "out", "Points", false}};
  s.evaluate = nullptr; return s;
}

}  // namespace

int runT3MeshVerticesToPointsParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Fixture: N vertices with non-identity Position AND direction frames (each vertex gets its own
  // rotated TBN basis so the rotation path is exercised across the buffer, not just at one point).
  const uint32_t N = 12;
  std::vector<SwVertex> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    double ang = a * 6.2831853;
    in[i] = SwVertex{};
    in[i].Position  = SW_MESH_PACKED3{ (float)(std::cos(ang) * 1.3), (float)(std::sin(ang) * 0.8), (float)((a - 0.5) * 1.5) };
    // Rotated TBN basis about Z by `ang` (still orthonormal): T=(cos,sin,0) B=(-sin,cos,0) N=(0,0,1).
    in[i].Tangent   = SW_MESH_PACKED3{ (float)std::cos(ang), (float)std::sin(ang), 0.0f };
    in[i].Bitangent = SW_MESH_PACKED3{ (float)-std::sin(ang), (float)std::cos(ang), 0.0f };
    in[i].Normal    = SW_MESH_PACKED3{ 0.0f, 0.0f, 1.0f };
    in[i].Texcoord  = SW_MESH_FLOAT2{ (float)a, (float)(1.0 - a) };
    in[i].Selection = 0.3f + 0.6f * (float)a;
    in[i].ColorRgb  = SW_MESH_PACKED3{ 0.5f, 0.6f, 0.7f };
  }
  g_fixtureVerts = &in;

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  bool ok = importT3Symbol(kT3, lib, &rootId, &warnings);
  if (!ok || rootId != std::string(kGuid)) {
    printf("[mvtp-retire] ②parity FAIL: import bad (ok=%d id=%s)\n", ok, rootId.c_str());
    g_fixtureVerts = nullptr; pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[mvtp-retire] ②parity FAIL: no root\n"); g_fixtureVerts = nullptr; pool->release(); return 1; }

  const int mbcId = childIdOfType(*sym, "_MeshBufferComponents");
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!mbcId || !ebuId) {
    printf("[mvtp-retire] ②parity FAIL: mesh-bridge atom not imported (mbc=%d ebu=%d)\n", mbcId, ebuId);
    g_fixtureVerts = nullptr; pool->release(); return 1;
  }

  // Add the fixture SwVertex producer; wire it into _MeshBufferComponents.MeshBuffers — the Mesh
  // boundary's own downstream (mirrors t3import_transformmesh_golden.cpp's mesh-bridge injection).
  // RED tooth (injectBug): DON'T feed the mesh input. With no ShaderResource reaching the compute
  // stage, the fresh UAV (sized from the SAME empty mesh gather) stays 0-length → readback diverges.
  if (!lib.symbols.count("t3xf_mvtp_input_verts"))
    if (const NodeSpec* fs = findSpec("t3xf_mvtp_input_verts"))
      lib.symbols["t3xf_mvtp_input_verts"] = atomicSymbolFromSpec(*fs);
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_mvtp_input_verts"; sym->children.push_back(p); }
  if (!injectBug) {
    SymbolConnection w; w.srcChild = fixtureId; w.srcSlot = "Buffer"; w.dstChild = mbcId; w.dstSlot = "MeshBuffers";
    sym->connections.push_back(w);
  }

  // 骨7 inject OffsetByTBN (vec .x head) + W (plain scalar). injectBug leaves the mesh unfed (above);
  // the param injection stays faithful on both legs — the -bug divergence is carried entirely by the
  // missing mesh SRV, not by the params.
  std::map<std::string, std::vector<float>> boundaryFloatInputs;
  boundaryFloatInputs[kInOffsetByTBN] = {kOffsetByTbnX};
  boundaryFloatInputs[kInW] = {kOffsetScale};

  ResidentEvalGraph g = buildEvalGraph(lib, rootId, boundaryFloatInputs);
  initResidentCache(g);
  const std::string termPath = std::to_string(ebuId);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[mvtp-retire] ②parity FAIL: no metallib\n"); q->release(); dev->release(); g_fixtureVerts = nullptr; pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);
  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // Oracle fed the SAME resolved params: OffsetByTBN=(kOffsetByTbnX,0,0), OffsetScale=kOffsetScale.
  mathv_ref::MeshVerticesToPointsParams prm{};
  prm.offsetByTBNx = kOffsetByTbnX; prm.offsetByTBNy = 0.0f; prm.offsetByTBNz = 0.0f;
  prm.offsetScale = kOffsetScale;
  std::vector<SwPoint> exp(N);
  mathv_ref::mathvRefMeshVerticesToPoints(in.data(), exp.data(), N, prm);

  double maxPosErr = 0.0, maxRotErr = 0.0; int worstI = -1;
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      float dx = exp[i].Position.x - got[i].Position.x;
      float dy = exp[i].Position.y - got[i].Position.y;
      float dz = exp[i].Position.z - got[i].Position.z;
      double pe = std::sqrt((double)dx*dx + (double)dy*dy + (double)dz*dz);
      // Rotation sign ambiguity (q and -q are the same rotation): compare |dot|.
      double dot = (double)exp[i].Rotation.x * got[i].Rotation.x + (double)exp[i].Rotation.y * got[i].Rotation.y +
                   (double)exp[i].Rotation.z * got[i].Rotation.z + (double)exp[i].Rotation.w * got[i].Rotation.w;
      double re = std::fabs(std::fabs(dot) - 1.0);
      if (pe > maxPosErr) { maxPosErr = pe; worstI = (int)i; }
      if (re > maxRotErr) maxRotErr = re;
    }
  double maxMove = 0.0;  // did-not-trip guard: the offset must actually move points
  if (haveOut) for (uint32_t i = 0; i < N; ++i) {
    float dx = in[i].Position.x - got[i].Position.x;
    float dy = in[i].Position.y - got[i].Position.y;
    maxMove = std::max(maxMove, (double)std::sqrt(dx*dx + dy*dy));
  }
  printf("[mvtp-retire] ②parity: haveOut=%d count=%u(need %u) maxPosErr=%.6f(need<1e-3) maxRotErr=%.6f(need<1e-3) worstI=%d maxMove=%.4f\n",
         haveOut ? 1 : 0, outBuf ? outBuf->elementCount : 0u, N, maxPosErr, maxRotErr, worstI, maxMove);

  mlib->release(); q->release(); dev->release();
  g_fixtureVerts = nullptr;

  const bool parityGreen = haveOut && (maxPosErr < 1e-3) && (maxRotErr < 1e-3) && (maxMove > 1e-3);
  if (!injectBug) {
    printf("[mvtp-retire] ②parity VERDICT: %s\n", parityGreen ? "GREEN" : "RED");
    pool->release();
    return parityGreen ? 0 : 1;
  }
  const bool bites = !parityGreen;  // no mesh fixture wired → UAV stays 0-length → diverges from N-count oracle
  printf("[mvtp-retire] ②parity -bug: %s\n", bites ? "BITES (mesh SRV reaches kernel)" : "TOOTHLESS");
  pool->release();
  return bites ? 1 : 0;
}

int runMeshVerticesToPointsLayoutGate(bool injectBug) {
  std::string id;
  if (!symbolIdOfT3(kT3, &id) || id != std::string(kGuid)) { printf("[mvtp-retire] ④layout FAIL: id\n"); return 1; }
  const T3LayoutResolver layoutResolve = [&id](const std::string& guid, std::string& out) -> bool {
    if (guid != id) return false; out = kT3ui; return true; };
  t3LayoutDisable() = injectBug;
  SymbolLibrary lib; std::string rootId; std::vector<std::string> warnings;
  const bool ok = importT3Symbol(kT3, lib, &rootId, &warnings, T3Resolver{}, layoutResolve);
  t3LayoutDisable() = false;
  if (!ok || rootId != std::string(kGuid)) { printf("[mvtp-retire] ④layout FAIL: import\n"); return 1; }
  Symbol* s = lib.find(rootId);
  if (!s) { printf("[mvtp-retire] ④layout FAIL: no root\n"); return 1; }

  const SlotDef* tbn  = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinOffsetByTBN) tbn  = &d;
  const SlotDef* mesh = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinMesh)        mesh = &d;
  const SlotDef* w    = nullptr; for (const SlotDef& d : s->inputDefs)  if (d.id == kPinW)           w    = &d;
  const SlotDef* out  = nullptr; for (const SlotDef& d : s->outputDefs) if (d.id == kPinOutput)      out  = &d;
  if (!tbn || !mesh || !w || !out) {
    printf("[mvtp-retire] ④layout FAIL: tbn=%p mesh=%p w=%p out=%p\n", (void*)tbn, (void*)mesh, (void*)w, (void*)out);
    return 1;
  }
  const bool tbnOk  = injectBug ? (nearf(tbn->x, 0) && nearf(tbn->y, 0))   : (nearf(tbn->x, kTbnX)  && nearf(tbn->y, kTbnY));
  const bool meshOk = injectBug ? (nearf(mesh->x, 0) && nearf(mesh->y, 0)) : (nearf(mesh->x, kMeshX) && nearf(mesh->y, kMeshY));
  const bool wOk    = injectBug ? (nearf(w->x, 0) && nearf(w->y, 0))      : (nearf(w->x, kWX)     && nearf(w->y, kWY));
  const bool outOk  = injectBug ? (nearf(out->x, 0) && nearf(out->y, 0))  : (nearf(out->x, kOutX)  && nearf(out->y, kOutY));
  printf("[mvtp-retire] ④layout: tbn(%.3f,%.3f) mesh(%.3f,%.3f) w(%.3f,%.3f) out(%.3f,%.3f)\n",
         tbn->x, tbn->y, mesh->x, mesh->y, w->x, w->y, out->x, out->y);
  auto distinctPair = [](const SlotDef* a, const SlotDef* b) {
    return !(nearf(a->x, b->x) && nearf(a->y, b->y));
  };
  const bool distinct = distinctPair(tbn, mesh) && distinctPair(tbn, w) && distinctPair(tbn, out) &&
                        distinctPair(mesh, w) && distinctPair(mesh, out) && distinctPair(w, out);
  const bool nonZero = !(nearf(tbn->x, 0) && nearf(tbn->y, 0));
  if (!injectBug) {
    if (!(nonZero && distinct)) { printf("[mvtp-retire] ④layout NO-BITE: seam not exercised\n"); return 0; }
    return (tbnOk && meshOk && wOk && outOk) ? 0 : 1;
  }
  const bool bites = (tbnOk && meshOk && wOk && outOk);
  return bites ? 1 : 0;
}

int runT3MeshVerticesToPointsRetireGates(bool injectBug) {
  registerBuiltinPointOps();

  SymbolLibrary lib;
  std::string rootId; std::vector<std::string> warnings;
  if (!importT3Symbol(kT3, lib, &rootId, &warnings) || rootId != std::string(kGuid)) {
    printf("[mvtp-retire] FAIL: import\n"); return 1;
  }
  refreshCompoundSpecs(lib);

  const NodeSpec* g1spec = findSpec(kName);
  const Symbol* csym = lib.find(kGuid);
  const bool g1green = g1spec && g1spec->type == std::string(kGuid) && g1spec->evaluate == nullptr &&
                       csym && !csym->atomic && !csym->children.empty();
  bool g1bit = false;
  if (injectBug) {
    // MeshVerticesToPoints' OWN family (generatorSpecsExtra()) is a compile-time-fixed vector — not a
    // mutable self-registration sink. Any LIVE atom sink findSpec checks before the dynamicSpecs
    // name-fallback proves the same shadow-polarity claim (mirrors t3import_snaptopoints_retire_golden.cpp,
    // whose own family — pointCombineSpecs() — is equally immutable and which uses this SAME live sink).
    pointModifySpecSink().push_back(standInFlatAtomSpec());
    const NodeSpec* shadowed = findSpec(kName);
    g1bit = shadowed && shadowed->type == std::string(kName);
    pointModifySpecSink().pop_back();
  }
  printf("[mvtp-retire] ①takeover: findSpec(\"%s\")->type=%s atomic=%d children=%d -> %s\n",
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
  printf("[mvtp-retire] ③reference: in=%d out=%d nodes=%zu -> %s\n", g3in, g3out, g3nodes,
         injectBug ? (g3bit ? "BITES" : "TOOTHLESS") : (g3green ? "GREEN" : "RED"));

  const int g2 = runT3MeshVerticesToPointsParity(injectBug);
  const bool g2green = (g2 == 0), g2bit = (g2 != 0);
  printf("[mvtp-retire] ②parity -> %s\n", injectBug ? (g2bit ? "BITES" : "TOOTHLESS") : (g2green ? "GREEN" : "RED"));

  const int g4 = runMeshVerticesToPointsLayoutGate(injectBug);
  const bool g4green = (g4 == 0), g4bit = (g4 != 0);
  printf("[mvtp-retire] ④layout -> %s\n", injectBug ? (g4bit ? "BITES" : "TOOTHLESS") : (g4green ? "GREEN" : "RED"));

  setDynamicSpecs({});

  if (!injectBug) {
    const bool green = g1green && g3green && g2green && g4green;
    printf("[mvtp-retire] VERDICT: %s (①%d ③%d ②%d ④%d)\n", green ? "PASS (retirement takeover LIVE)" : "FAIL",
           g1green, g3green, g2green, g4green);
    return green ? 0 : 1;
  }
  const bool allBit = g1bit && g3bit && g2bit && g4bit;
  printf("[mvtp-retire] -bug VERDICT: %s (①%d ③%d ②%d ④%d)\n",
         allBit ? "ALL TEETH BITE" : "DEAD TOOTH (NO-BITE)", g1bit, g3bit, g2bit, g4bit);
  return allBit ? 1 : 0;
}

}  // namespace sw
