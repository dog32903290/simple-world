// runtime/t3import_displacemeshnoise_golden — 骨9 (--selftest-t3-displacemeshnoise): the MESH-family
// MIXED-SLOT MULTIINPUT proof. This is the honest gap 骨8 left open.
//
// ── WHY THIS GOLDEN EXISTS (the 骨9 承重线) ─────────────────────────────────────────────────────────
// 骨7b fixed resident_eval_flatten's inlineSymbol to resolve a compound's wires in a SINGLE pass over
// sym.connections, so a MultiInput slot fed by BOTH child wires AND boundary wires keeps the true wire
// DECLARATION order in its extraConns (the OLD two-pass code grouped [all child…, all boundary…],
// scrambling any interleave). Downstream the marshal cook packs floatInputs = [primary, extraConns…]
// positionally into a GPU constant buffer, so order == correctness. But 骨7b was only proven on a
// SYNTHETIC graph (resident_mixed_multiinput_golden.cpp, IntsToBuffer) and on TransformMesh — whose
// FloatsToBuffer.Params has NO child/boundary interleave (骨8 explicitly noted "no natural collision to
// swap"). So "骨7b holds on real mesh (SwVertex) currency through a genuinely interleaved mixed slot"
// was NEVER shown end-to-end. THIS golden shows it.
//
// ── WHY DisplaceMeshNoise ──────────────────────────────────────────────────────────────────────────
// Its real TiXL .t3 (embedded byte-faithful below) has a FloatsToBuffer.Params MultiInput fed by 13
// wires whose sources INTERLEAVE boundary and child, in the exact HLSL cbuffer Params order:
//   [0]Amount(BND) [1]Frequency(BND) [2]Phase(BND) [3]Variation(BND) [4..6]AmountDistribution(CH×3)
//   [7]RotationLookupDistance(BND) [8]UseWAsWeight(BND) [9]Space(CH) [10]Direction(CH)
//   [11]OffsetDirection(BND) [12]UseVertexSelection(CH)
// (BND = a boundary Input wire; CH = a sibling child's output — Vector3Components / IntToFloat /
// BoolToFloat.) That is a GENUINE mixed-slot interleave: BND,BND,BND,BND, CH,CH,CH, BND,BND, CH,CH,
// BND, CH. If the flatten did NOT preserve it, these floats land in the WRONG cb1 slots → the noise
// displacement's amplitude / frequency / distribution corrupt → the readback Position diverges. The
// same buffer backbone that TransformMesh proved (GetBufferComponents / ComputeShaderStage /
// StructuredBufferWithViews / ExecuteBufferUpdate / _MeshBufferComponents / _AssembleMeshBuffers) carries
// the SwVertex data UNCHANGED; only the cb1 payload op changes (FloatsToBuffer, now interleaved) and two
// value atoms are added (IntToFloat, Vector3Components — 骨9 map rows).
//
// ── THE PROVING KERNEL ───────────────────────────────────────────────────────────────────────────────
// computeshaderstage_displacemeshnoise.metal — a faithful MSL port of the .t3's ComputeShader.Source
// "Lib:shaders/3d/mesh/mesh-LegacyNoiseDisplace.hlsl", reusing sw's already-ported snoiseVec3/hash31.
//
// ── THE ORACLE (independent, non-circular) ──────────────────────────────────────────────────────────
// Config: Space=0 (PointSpace → posInWorld=(TexCoord.xy,0), NO camera matrix), Direction=0 (WorldSpace →
// NO TBN rotation), Variation=0 (variationOffset=0 → NO hash31), UseVertexSelection=0 (selection=1).
// Then per the .hlsl: Position_out = Position_in + offset, where
//   offset = (snoiseVec3((posInWorld*0.91)*Frequency + Phase) + OffsetDirection) * Amount/100 * AmountDist.
// The oracle recomputes snoiseVec3 in DOUBLE (this file's simplexNoise3, a straight port of the SAME
// Ashima algorithm in noise.metal.h) — NOT derived from the import path, so GREEN is not self-proving.
// We parity-check the Position component (the load-bearing displaced output).
//
// ── THE 骨9 TOOTH (-bug, load-bearing) ──────────────────────────────────────────────────────────────
// The -bug leg REVERSES the FloatsToBuffer.Params wire order in sym.connections before buildEvalGraph.
// Faithful order → these 13 floats fill cb1[0..12] correctly → GREEN. Reversed → Amount(cb1[0]) swaps
// with UseVertexSelection(cb1[12]) etc. → the noise amplitude becomes 0 (UseVertexSelection=0) → offset≈0
// → the readback Positions ≈ the un-displaced input → DIVERGES from the oracle → RED. That RED is the
// proof that the declaration-order preservation (骨7b) is what makes the interleaved mixed slot correct
// on real SwVertex mesh currency. (Reversing exercises the EXACT sym.connections→extraConns→floatInputs
// →cb1 chain the fix guards; it is not a synthetic re-scramble outside that path.)
//
// ZONE: runtime golden (shell tier — binds runtime import + resident cook + the independent oracle).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/buffer_op_registry.h"     // BufferOp / BufferCookCtx (the fixture producer)
#include "runtime/compound_graph.h"         // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/eval_context.h"           // EvaluationContext
#include "runtime/graph.h"                  // findSpec / registerBuiltinPointOps
#include "runtime/graph_bridge.h"           // atomicSymbolFromSpec
#include "runtime/point_graph.h"            // PointGraph / residentSwBufferFor
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph / initResidentCache / ctx
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/sw_mesh.h"                // SwVertex (80B)
#include "runtime/t3_import.h"              // importT3Symbol

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kDisplaceMeshNoiseT3 =
#include "runtime/displacemeshnoise_t3_embed.inc"
;

// Boundary Input def GUIDs (from DisplaceMeshNoise.t3 Inputs[]) — the golden overrides these scalar
// defaults so the config is closed-form (Space=0/Direction=0/Variation=0/UseVertexSelection=0).
struct BParam { const char* guid; float value; };

// ── Test-fixture Buffer producer: a fixed N-vertex SwVertex bag as a SwBuffer (stride 80). ──────────
std::vector<SwVertex>* g_fixtureVerts = nullptr;

void cookInputVertsFixture(BufferCookCtx& c) {
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
  NodeSpec s;
  s.type = "t3dmn_input_verts";
  s.title = "t3dmn_input_verts";
  s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}};  // a pure producer (no inputs)
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_t3dmn_input_verts(fixtureSpec(), cookInputVertsFixture);

// ── Oracle: snoiseVec3 in DOUBLE — a straight port of app/shaders/shared/noise.metal.h (Ashima 3-D
//    simplex), independent of the import/cook path. ────────────────────────────────────────────────
struct V3 { double x, y, z; };
struct V4 { double x, y, z, w; };
V3 add3(V3 a, V3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
V3 sub3(V3 a, V3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
V3 scl3(V3 a, double s) { return {a.x*s, a.y*s, a.z*s}; }
double dot3(V3 a, V3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
double flr(double x) { return std::floor(x); }
double stp(double edge, double x) { return x < edge ? 0.0 : 1.0; }  // step(edge,x)
double mod289d(double x) { return x - std::floor(x * (1.0/289.0)) * 289.0; }
V4 mod289v4(V4 v) { return {mod289d(v.x), mod289d(v.y), mod289d(v.z), mod289d(v.w)}; }
V4 permute(V4 x) {  // ((x*34)+1)*x mod 289
  return mod289v4(V4{(x.x*34.0+1.0)*x.x, (x.y*34.0+1.0)*x.y, (x.z*34.0+1.0)*x.z, (x.w*34.0+1.0)*x.w});
}
V4 taylorInvSqrt(V4 r) {
  return {1.79284291400159 - 0.85373472095314*r.x, 1.79284291400159 - 0.85373472095314*r.y,
          1.79284291400159 - 0.85373472095314*r.z, 1.79284291400159 - 0.85373472095314*r.w};
}
double snoise3(V3 v) {
  const double Cx = 1.0/6.0, Cy = 1.0/3.0;
  const double Dy = 0.5;
  // First corner
  V3 i0 = {flr(v.x + dot3(v, {Cy, Cy, Cy})), flr(v.y + dot3(v, {Cy, Cy, Cy})), flr(v.z + dot3(v, {Cy, Cy, Cy}))};
  V3 x0 = {v.x - i0.x + dot3(i0, {Cx, Cx, Cx}), v.y - i0.y + dot3(i0, {Cx, Cx, Cx}), v.z - i0.z + dot3(i0, {Cx, Cx, Cx})};
  // Other corners: g = step(x0.yzx, x0.xyz), l = 1-g
  V3 g = {stp(x0.y, x0.x), stp(x0.z, x0.y), stp(x0.x, x0.z)};
  V3 l = {1.0-g.x, 1.0-g.y, 1.0-g.z};
  // i1 = min(g.xyz, l.zxy), i2 = max(g.xyz, l.zxy)
  V3 lzxy = {l.z, l.x, l.y};
  V3 i1 = {std::min(g.x, lzxy.x), std::min(g.y, lzxy.y), std::min(g.z, lzxy.z)};
  V3 i2 = {std::max(g.x, lzxy.x), std::max(g.y, lzxy.y), std::max(g.z, lzxy.z)};
  V3 x1 = {x0.x - i1.x + Cx, x0.y - i1.y + Cx, x0.z - i1.z + Cx};
  V3 x2 = {x0.x - i2.x + Cy, x0.y - i2.y + Cy, x0.z - i2.z + Cy};
  V3 x3 = {x0.x - Dy, x0.y - Dy, x0.z - Dy};
  // Permutations (noise.metal.h lines 84-88: permute(permute(permute(iz+…) + iy+…) + ix+…)).
  i0 = {mod289d(i0.x), mod289d(i0.y), mod289d(i0.z)};
  V4 pz = permute(V4{i0.z + 0.0, i0.z + i1.z, i0.z + i2.z, i0.z + 1.0});
  V4 py = permute(V4{pz.x + i0.y + 0.0, pz.y + i0.y + i1.y, pz.z + i0.y + i2.y, pz.w + i0.y + 1.0});
  V4 p  = permute(V4{py.x + i0.x + 0.0, py.y + i0.x + i1.x, py.z + i0.x + i2.x, py.w + i0.x + 1.0});
  // Gradients
  const double n_ = 0.142857142857;  // 1/7
  // ns = n_ * D.wyz - D.xzx, D=(0,.5,1,2): D.wyz=(2,.5,1), D.xzx=(0,1,0).
  V3 ns = {n_*2.0 - 0.0, n_*0.5 - 1.0, n_*1.0 - 0.0};
  double jarr[4] = {p.x, p.y, p.z, p.w};
  double xarr[4], yarr[4], harr[4];
  for (int k = 0; k < 4; ++k) {
    double j = jarr[k] - 49.0 * flr(jarr[k] * ns.z * ns.z);
    double x_ = flr(j * ns.z);
    double y_ = flr(j - 7.0 * x_);
    xarr[k] = x_ * ns.x + ns.y;
    yarr[k] = y_ * ns.x + ns.y;
    harr[k] = 1.0 - std::fabs(xarr[k]) - std::fabs(yarr[k]);
  }
  V4 b0 = {xarr[0], xarr[1], yarr[0], yarr[1]};
  V4 b1 = {xarr[2], xarr[3], yarr[2], yarr[3]};
  double s0[4] = {flr(b0.x)*2.0+1.0, flr(b0.y)*2.0+1.0, flr(b0.z)*2.0+1.0, flr(b0.w)*2.0+1.0};
  double s1[4] = {flr(b1.x)*2.0+1.0, flr(b1.y)*2.0+1.0, flr(b1.z)*2.0+1.0, flr(b1.w)*2.0+1.0};
  double sh[4] = {-stp(harr[0], 0.0), -stp(harr[1], 0.0), -stp(harr[2], 0.0), -stp(harr[3], 0.0)};
  // a0 = b0.xzyw + s0.xzyw * sh.xxyy ; a1 = b1.xzyw + s1.xzyw * sh.zzww
  double a0[4] = {b0.x + s0[0]*sh[0], b0.z + s0[2]*sh[0], b0.y + s0[1]*sh[1], b0.w + s0[3]*sh[1]};
  double a1[4] = {b1.x + s1[0]*sh[2], b1.z + s1[2]*sh[2], b1.y + s1[1]*sh[3], b1.w + s1[3]*sh[3]};
  V3 p0 = {a0[0], a0[1], harr[0]};
  V3 p1 = {a0[2], a0[3], harr[1]};
  V3 p2 = {a1[0], a1[1], harr[2]};
  V3 p3 = {a1[2], a1[3], harr[3]};
  V4 norm = taylorInvSqrt(V4{dot3(p0,p0), dot3(p1,p1), dot3(p2,p2), dot3(p3,p3)});
  p0 = scl3(p0, norm.x); p1 = scl3(p1, norm.y); p2 = scl3(p2, norm.z); p3 = scl3(p3, norm.w);
  double m[4] = {std::max(0.6 - dot3(x0,x0), 0.0), std::max(0.6 - dot3(x1,x1), 0.0),
                 std::max(0.6 - dot3(x2,x2), 0.0), std::max(0.6 - dot3(x3,x3), 0.0)};
  for (int k = 0; k < 4; ++k) { m[k] = m[k]*m[k]; m[k] = m[k]*m[k]; }
  return 42.0 * (m[0]*dot3(p0,x0) + m[1]*dot3(p1,x1) + m[2]*dot3(p2,x2) + m[3]*dot3(p3,x3));
}
V3 snoiseVec3d(V3 p) {
  double s  = snoise3({p.x + 0.0001, p.y,         p.z});
  double s1 = snoise3({p.y - 19.1,   p.z + 33.4,  p.x + 47.2});
  double s2 = snoise3({p.z + 74.2,   p.x - 124.5, p.y + 99.4});
  return {s, s1, s2};
}

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

int runT3DisplaceMeshNoiseParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // Closed-form config (see header): Space=0, Direction=0, Variation=0, UseVertexSelection=0.
  const float AMOUNT = 25.0f, FREQ = 1.3f, PHASE = 0.4f, OFFDIR = 0.2f;
  const V3 AMTDIST = {1.0, 0.7, 1.4};

  // ---- Input bag: N vertices with distinct Position AND TexCoord (posInWorld=(TexCoord.xy,0)). ----
  const uint32_t N = 16;
  std::vector<SwVertex> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwVertex{};
    in[i].Position  = SW_MESH_PACKED3{ (float)(std::cos(a*6.2831853)*1.3), (float)(std::sin(a*6.2831853)*0.8), (float)((a-0.5)*1.5) };
    in[i].Normal    = SW_MESH_PACKED3{ 0.0f, 0.0f, 1.0f };
    in[i].Tangent   = SW_MESH_PACKED3{ 1.0f, 0.0f, 0.0f };
    in[i].Bitangent = SW_MESH_PACKED3{ 0.0f, 1.0f, 0.0f };
    in[i].Texcoord  = SW_MESH_FLOAT2{ (float)(a*2.0), (float)((1.0-a)*1.5) };
    in[i].Selection = 1.0f;
    in[i].ColorRgb  = SW_MESH_PACKED3{ 0.5f, 0.6f, 0.7f };
  }
  g_fixtureVerts = &in;

  // ---- STEP 1: import the real .t3 via the PRODUCTION importer ----
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  if (!importT3Symbol(kDisplaceMeshNoiseT3, lib, &rootId, &warnings)) {
    printf("[t3-displacemeshnoise] FAIL: importT3Symbol returned false\n"); pool->release(); return 1;
  }
  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[t3-displacemeshnoise] FAIL: root symbol missing\n"); pool->release(); return 1; }
  { std::map<std::string, int> byType;
    for (const SymbolChild& c : sym->children) byType[c.symbolId]++;
    printf("[t3-displacemeshnoise] import: rootId=%s children=%d conns=%d warnings=%zu\n",
           rootId.c_str(), (int)sym->children.size(), (int)sym->connections.size(), warnings.size());
    printf("[t3-displacemeshnoise]   mapped atom types:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n"); }

  const int mbcId = childIdOfType(*sym, "_MeshBufferComponents");
  const int asmId = childIdOfType(*sym, "_AssembleMeshBuffers");
  const int f2bId = childIdOfType(*sym, "FloatsToBuffer");
  const int v3cId = childIdOfType(*sym, "Vector3Components");
  const int strideConstId = childIdOfType(*sym, "Const");
  if (!mbcId || !asmId || !f2bId || !v3cId || !strideConstId) {
    printf("[t3-displacemeshnoise] FAIL: expected atom missing (mbc=%d asm=%d f2b=%d v3c=%d strideConst=%d)\n",
           mbcId, asmId, f2bId, v3cId, strideConstId);
    pool->release(); return 1;
  }

  // (a) Override boundary scalar defaults on the root inputDefs → the closed-form config.
  const BParam bp[] = {
      {"357ed675-212f-4b2c-b93b-c8460867a9ae", 0.0f},   // Space = PointSpace (posInWorld=(uv,0))
      {"f108f6f7-5e6f-43c8-9d0b-c2e7bf5adf9c", 0.0f},   // Direction = WorldSpace (no TBN rotation)
      {"e5213b00-6302-45e1-a172-5a11bd91892e", 0.0f},   // Variation = 0 (no hash31)
      {"83cb775f-c600-41c9-9435-604f77a426bd", 0.0f},   // UseVertexSelection = false (selection=1)
      {"b7559321-2dbe-4fe0-ab86-52532d008980", AMOUNT}, // Amount
      {"4b1a66a4-b5e4-4bc3-97f5-bd3cda668893", FREQ},   // Frequency
      {"b89b6730-de46-4d56-b2a8-b7d6f6876620", PHASE},  // Phase
      {"093a468c-c208-4caf-be4f-d5d7d9ceddeb", OFFDIR}, // OffsetDirection
      {"08e2222f-6de1-46a8-bbdc-da83251f424e", 0.5f},   // RotationLookupDistance (only affects normals)
      {"9a82f2a6-390e-4073-bc80-e5fd3b1c0bfe", 0.0f},   // UseWAsWeight (unused in Position math)
  };
  for (const BParam& b : bp)
    for (SlotDef& d : sym->inputDefs) if (d.id == b.guid) { d.def = b.value; break; }

  // (b) AmountDistribution: the single boundary wire lands on Vector3Components.Value.x head
  //     (fork-t3-vec3-wire-lands-on-head). Author the full vec3 on the imported child.
  for (SymbolChild& c : sym->children) if (c.id == v3cId) {
    c.overrides["Value.x"] = (float)AMTDIST.x;
    c.overrides["Value.y"] = (float)AMTDIST.y;
    c.overrides["Value.z"] = (float)AMTDIST.z;
    break;
  }

  // (c) FORK pbrvertex-stride-64-to-80-swvertex: the imported IntValue(PBRVertex.Stride)→Const carries
  //     64 (TiXL PbrVertex); sw's SwVertex is 80B — override on the imported child.
  for (SymbolChild& c : sym->children) if (c.id == strideConstId) { c.overrides["value"] = 80.0f; break; }

  // (d) Supply the TEST mesh input at _MeshBufferComponents.MeshBuffers (the .t3's InputMesh boundary
  //     has no producer). Same seam TransformMesh's golden feeds.
  if (!lib.symbols.count("t3dmn_input_verts"))
    if (const NodeSpec* fs = findSpec("t3dmn_input_verts"))
      lib.symbols["t3dmn_input_verts"] = atomicSymbolFromSpec(*fs);
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3dmn_input_verts"; sym->children.push_back(p); }
  { SymbolConnection w; w.srcChild = fixtureId; w.srcSlot = "Buffer"; w.dstChild = mbcId; w.dstSlot = "MeshBuffers";
    sym->connections.push_back(w); }

  // ---- 骨9 TOOTH (-bug): reverse the FloatsToBuffer.Params wire order in sym.connections. Faithful
  //     order fills cb1[0..12] correctly; reversed swaps Amount(cb1[0]) with UseVertexSelection(cb1[12])
  //     etc. → noise amplitude=0 → offset≈0 → Positions ≈ un-displaced input → RED. Reverses ONLY the
  //     Params wires (the interleaved mixed slot), preserving every other wire. ----
  if (injectBug) {
    std::vector<size_t> paramWireIdx;
    for (size_t i = 0; i < sym->connections.size(); ++i) {
      const SymbolConnection& w = sym->connections[i];
      if (w.dstChild == f2bId && w.dstSlot == "Params") paramWireIdx.push_back(i);
    }
    // In-place reverse the Params wires at their original positions (keeps the interleave slots, flips order).
    for (size_t a = 0, b = paramWireIdx.size(); a < b / 2; ++a)
      std::swap(sym->connections[paramWireIdx[a]], sym->connections[paramWireIdx[b - 1 - a]]);
    printf("[t3-displacemeshnoise] -bug: reversed %zu FloatsToBuffer.Params wires (scrambles cb1 order)\n",
           paramWireIdx.size());
  }

  // ---- STEP 2: build the eval graph (production flattener) ----
  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);
  printf("[t3-displacemeshnoise] buildEvalGraph: resident nodes=%zu\n", g.nodes.size());

  const std::string termPath = std::to_string(asmId);

  // ---- STEP 3: cook the resident graph; read back the _AssembleMeshBuffers output as SwVertex[] ----
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-displacemeshnoise] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f/60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);

  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N && outBuf->elementStride == sizeof(SwVertex);
  std::vector<SwVertex> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwVertex));

  // ---- Oracle: Position_out = Position_in + offset (Space=0/Dir=0/Var=0/sel=1). ----
  double maxPos = 0.0; int worstI = -1; double wExp[3]{}, wGot[3]{};
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      V3 posInWorld = { in[i].Texcoord.x, in[i].Texcoord.y, 0.0 };
      V3 lookup = { posInWorld.x*0.91*FREQ + PHASE, posInWorld.y*0.91*FREQ + PHASE, posInWorld.z*0.91*FREQ + PHASE };
      V3 noise = snoiseVec3d(lookup);
      V3 offset = { (noise.x + OFFDIR) * AMOUNT/100.0 * AMTDIST.x,
                    (noise.y + OFFDIR) * AMOUNT/100.0 * AMTDIST.y,
                    (noise.z + OFFDIR) * AMOUNT/100.0 * AMTDIST.z };
      double ep[3] = { in[i].Position.x + offset.x, in[i].Position.y + offset.y, in[i].Position.z + offset.z };
      double gp[3] = { got[i].Position.x, got[i].Position.y, got[i].Position.z };
      double dp = std::sqrt((ep[0]-gp[0])*(ep[0]-gp[0])+(ep[1]-gp[1])*(ep[1]-gp[1])+(ep[2]-gp[2])*(ep[2]-gp[2]));
      if (dp > maxPos) { maxPos = dp; worstI = (int)i; for (int k=0;k<3;k++){wExp[k]=ep[k];wGot[k]=gp[k];} }
    }

  printf("[t3-displacemeshnoise] replay-vs-oracle: haveOut=%d maxPosErr=%.6f(need<2e-3) worstI=%d "
         "exp=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f)\n",
         haveOut ? 1 : 0, maxPos, worstI, wExp[0], wExp[1], wExp[2], wGot[0], wGot[1], wGot[2]);

  // Threshold 2e-3: float-GPU snoise vs double-oracle snoise carries a few 1e-4 rounding deltas at the
  // simplex floor() branch points; the displaced offset (~Amount/100 scale) stays well under 2e-3.
  const bool parityGreen = haveOut && (maxPos < 2e-3);
  printf("[t3-displacemeshnoise] PARITY VERDICT: %s\n",
         parityGreen ? "GREEN (mesh mixed-slot replay reproduces the noise displacement)"
                     : "RED (mesh mixed-slot replay seam gap / cb1 order scrambled)");

  mlib->release(); q->release(); dev->release();
  g_fixtureVerts = nullptr;

  if (!injectBug) {
    if (!parityGreen) { printf("[t3-displacemeshnoise] FAIL\n"); pool->release(); return 1; }
    printf("[t3-displacemeshnoise] PASS: DisplaceMeshNoise.t3 replays to parity through the INTERLEAVED "
           "FloatsToBuffer.Params mixed slot (骨7b order fix holds on real SwVertex mesh currency)\n");
    pool->release(); return 0;
  }

  // injectBug leg: the Params wire order was reversed → cb1 scrambled → RED. Tooth bites iff NOT green.
  const bool bites = !parityGreen;
  printf("[t3-displacemeshnoise] -bug: cb1-order tooth %s (parity green under bug == %s)\n",
         bites ? "BITES" : "TOOTHLESS", parityGreen ? "true" : "false");
  pool->release();
  return bites ? 1 : 2;
}

}  // namespace sw
