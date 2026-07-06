// runtime/t3import_transformmesh_golden — 骨8 首證 (--selftest-t3-transformmesh): the MESH-FAMILY keystone.
//
// This lifts the atomic-replay strategy from the POINT keystone (t3import_transformpoints_golden.cpp) onto
// the MESH currency. It loads the REAL TiXL TransformMesh.t3 (embedded byte-faithful below), walks the
// PRODUCTION path importT3Symbol → buildEvalGraph → cookMatrixOutputNodes → cookResident, reads back the
// transformed mesh VERTEX buffer (SwVertex, 80B), and compares it against the焊死 oracle
// mesh_ops_transformmesh.cpp's closed-form TRS-with-pivot transform. This proves a .t3 GPU-compute MESH
// compound REPLAYS as an sw atom-nested graph and produces byte-faithful transformed vertices — the mesh
// analog of the point keystone, and the validation that 骨7b's MultiInput-order fix holds on real SwVertex.
//
// ── WHAT FLOWS THROUGH THE REAL MACHINE ───────────────────────────────────────────────────────────
// TransformMesh.t3 composes the transform from 14 children. The SAME compute-buffer atoms the point
// keystone proved carry the mesh vertex data UNCHANGED (the buffer machinery is stride-generic —
// StructuredBufferWithViews allocs Stride*Count bytes, ComputeShaderStage binds SRV/UAV by wire order and
// dispatches srvs.front()->elementCount threads): ComputeShaderStage (dispatch), StructuredBufferWithViews
// (UAV write target), GetBufferComponents/GetSRVProperties (SRV/UAV/count rails), FloatsToBuffer (cb0),
// TransformMatrix (the SRT→transposed-4-row matrix), CalcDispatchCount, ExecuteBufferUpdate (terminal).
// The proving kernel is computeshaderstage_transformmesh.metal — a faithful MSL port of
// mesh-TransformVertices.hlsl (SwVertex 80B in/out; math cited from the oracle's verbatim HLSL comment).
// cb0 layout = TransformMatrix.Result (16 floats, transposed) + useVertexSelection (1 scalar), EXACTLY
// what FloatsToBuffer assembles from Vec4Params(matrix)-first then Params(scalar)-second.
//
// ── THE MESH↔BUFFER BRIDGE (骨4 — now NATIVE, scaffold removed) ─────────────────────────────────────
// TransformMesh.t3's mesh-currency seam uses two nested compounds + two value ops the importer once did
// NOT map (their wires dropped on import). 骨4 mapped all four to sw atoms:
//   _MeshBufferComponents (MeshBuffers → {Vertices,Indices,ChunkDefs}) — SymbolId 5b9f1d97… → sw buffer atom
//   _AssembleMeshBuffers  ({Vertices,Indices,ChunkDefs,PrepareCommand} → MeshBuffers) — e0849edd… → sw buffer atom
//   BoolToFloat (numbers/bool/convert) — 9db2fcbf… → existing sw value op
//   IntValue(PBRVertex.Stride) (Types/Values) — cc07b314… → sw Const (int const producer)
// Both compounds are PURE structural passthroughs (verified against their .cs — they only move
// BufferWithViews handles around a MeshBuffers wrapper); on sw's collapsed currency (fork
// bufferwithviews-collapse-to-mtlbuffer) they are single-buffer passthroughs carrying the VERTEX buffer.
// With them mapped, the four mesh-bridge wires now import NATIVELY — no scaffold:
//   (a) Mesh boundary → _MeshBufferComponents.Vertices → GetBufferComponents (the SRV feed)
//   (b) ExecuteBufferUpdate.Output2 → _AssembleMeshBuffers.Vertices → Result output (the terminal)
//   (c) IntValue(PBRVertex.Stride) → StructuredBufferWithViews.Stride (forked 64→80 for SwVertex)
//   (d) UseVertexSelection boundary → BoolToFloat → FloatsToBuffer.Params (the cb0[16] scalar)
// The golden now only (1) supplies the TEST mesh input at _MeshBufferComponents.MeshBuffers (the .t3's
// Mesh boundary has no producer — a test must feed one), (2) authors the transform params on the imported
// TransformMatrix, (3) forks the sw vertex stride 64→80 on the imported Const child. It reads the
// _AssembleMeshBuffers terminal (the true Result output) as SwVertex[] — proving the transformed vertices
// flow END-TO-END through BOTH native bridge compounds. See STEP 1b.
//
// ZONE: runtime golden (shell tier — binds runtime import + resident cook + the oracle reference).
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
#include "runtime/resident_value_cooks.h"   // cookMatrixOutputNodes
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/sw_mesh.h"                // SwVertex (80B)
#include "runtime/t3_import.h"              // importT3Symbol / t3ImportInjectBug

namespace sw {

void registerBuiltinPointOps();

namespace {

static const char* kTransformMeshT3 =
#include "runtime/transformmesh_t3_embed.inc"
;

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
  s.type = "t3xf_input_verts";
  s.title = "t3xf_input_verts";
  s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}};  // a pure producer (no inputs)
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_t3xf_input_verts(fixtureSpec(), cookInputVertsFixture);

// ── Oracle reference (mesh_ops_transformmesh.cpp closed-form, verbatim TRS-with-pivot) ──────────────
// Row-vector convention v·M (matches field_camera Mat4 / mat4Mul, == System.Numerics), pivot=0 here.
struct M4 { double m[16]; };  // row-major, m[r*4+c]
M4 identD() { M4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }
M4 mulD(const M4& a, const M4& b) {
  M4 r{};
  for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) { double s=0; for (int k=0;k<4;k++) s+=a.m[i*4+k]*b.m[k*4+j]; r.m[i*4+j]=s; }
  return r;
}
M4 transD(double x, double y, double z) { M4 r = identD(); r.m[12]=x; r.m[13]=y; r.m[14]=z; return r; }
M4 scaleD(double x, double y, double z) { M4 r = identD(); r.m[0]=x; r.m[5]=y; r.m[10]=z; return r; }
// Rotation = CreateFromYawPitchRoll (System.Numerics), 3×3 as in the oracle's rotateYawPitchRoll (v·M).
M4 rotD(double yaw, double pitch, double roll) {
  double cy=std::cos(yaw), sy=std::sin(yaw), cx=std::cos(pitch), sx=std::sin(pitch), cz=std::cos(roll), sz=std::sin(roll);
  M4 r = identD();
  r.m[0]=cy*cz+sy*sx*sz;  r.m[1]=cx*sz;  r.m[2]=-sy*cz+cy*sx*sz;
  r.m[4]=-cy*sz+sy*sx*cz; r.m[5]=cx*cz;  r.m[6]=sy*sz+cy*sx*cz;
  r.m[8]=sy*cx;           r.m[9]=-sx;    r.m[10]=cy*cx;
  return r;
}
void xformPoint(const M4& m, double x, double y, double z, double out[3]) {
  out[0]=x*m.m[0]+y*m.m[4]+z*m.m[8]+m.m[12];
  out[1]=x*m.m[1]+y*m.m[5]+z*m.m[9]+m.m[13];
  out[2]=x*m.m[2]+y*m.m[6]+z*m.m[10]+m.m[14];
}
void xformDir(const M4& m, double x, double y, double z, double out[3]) {
  out[0]=x*m.m[0]+y*m.m[4]+z*m.m[8];
  out[1]=x*m.m[1]+y*m.m[5]+z*m.m[9];
  out[2]=x*m.m[2]+y*m.m[6]+z*m.m[10];
}
void norm3(double v[3]) { double L=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if (L>1e-12){v[0]/=L;v[1]/=L;v[2]/=L;} }

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

int runT3TransformMeshParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  const double DEG = M_PI / 180.0;
  const float ROT[3] = {37.0f, 53.0f, 71.0f};   // X=pitch, Y=yaw, Z=roll (degrees)
  const float SCL[3] = {1.7f, 0.6f, 2.3f};      // non-uniform
  const float TRN[3] = {0.4f, -0.3f, 0.9f};
  const float useVertexSelection = 0.0f;        // s=1 (full transform on every vertex) — the load-bearing math

  // ---- Input bag: N vertices with non-identity Position AND direction frames ----
  const uint32_t N = 16;
  std::vector<SwVertex> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwVertex{};
    in[i].Position  = SW_MESH_PACKED3{ (float)(std::cos(a*6.2831853)*1.3), (float)(std::sin(a*6.2831853)*0.8), (float)((a-0.5)*1.5) };
    in[i].Normal    = SW_MESH_PACKED3{ 0.0f, 0.0f, 1.0f };
    in[i].Tangent   = SW_MESH_PACKED3{ 1.0f, 0.0f, 0.0f };
    in[i].Bitangent = SW_MESH_PACKED3{ 0.0f, 1.0f, 0.0f };
    in[i].Texcoord  = SW_MESH_FLOAT2{ (float)a, (float)(1.0-a) };
    in[i].Selection = 1.0f;
    in[i].ColorRgb  = SW_MESH_PACKED3{ 0.5f, 0.6f, 0.7f };
  }
  g_fixtureVerts = &in;

  // ---- STEP 1: import the real .t3 via the PRODUCTION importer ----
  // (No t3ImportInjectBug here: its MultiInput-order reversal is toothless for TransformMesh — TransformMesh's
  // mapped wires have no natural (dstChild,dstSlot) collision to swap. The tooth for THIS golden withholds the
  // mesh input so the compute-stage SRV feed never arrives — see STEP 1b (1) -bug.)
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kTransformMeshT3, lib, &rootId, &warnings);
  if (!ok) { printf("[t3-transformmesh] FAIL: importT3Symbol returned false\n"); pool->release(); return 1; }

  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[t3-transformmesh] FAIL: root symbol missing\n"); pool->release(); return 1; }
  { std::map<std::string, int> byType;
    for (const SymbolChild& c : sym->children) byType[c.symbolId]++;
    printf("[t3-transformmesh] import: rootId=%s children=%d conns=%d warnings=%zu\n",
           rootId.c_str(), (int)sym->children.size(), (int)sym->connections.size(), warnings.size());
    printf("[t3-transformmesh]   mapped atom types:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n"); }

  // ---- STEP 1b: NATIVE mesh-bridge import (骨4 — the four mesh-bridge wires now import via sw atoms) ----
  // The two nested passthrough compounds (_MeshBufferComponents / _AssembleMeshBuffers) now map to sw
  // buffer atoms + BoolToFloat / IntValue(→Const) map too, so the Mesh boundary → GBC vertex feed, the
  // ExecuteBufferUpdate → Result output, PBRVertex.Stride → SBV.Stride, and BoolToFloat → FloatsToBuffer
  // wires ALL survive import. No scaffold rewires them. This step only: (1) supplies the TEST mesh input
  // (the .t3's Mesh boundary has no producer — a test must feed one; wired at the compound's MeshBuffers
  // input, the natural seam the boundary feeds), (2) authors the transform params on the imported
  // TransformMatrix (the boundary Vector3 wires need values), (3) forks the sw vertex stride 64→80.
  const int mbcId = childIdOfType(*sym, "_MeshBufferComponents");  // the vertex-unwrap bridge compound (now atom)
  const int asmId = childIdOfType(*sym, "_AssembleMeshBuffers");   // the vertex-rewrap bridge compound (now atom)
  const int tmId  = childIdOfType(*sym, "TransformMatrix");
  const int strideConstId = childIdOfType(*sym, "Const");          // the imported IntValue(PBRVertex.Stride)→Const
  if (!mbcId || !asmId || !tmId || !strideConstId) {
    printf("[t3-transformmesh] FAIL: mesh-bridge atom not imported (mbc=%d asm=%d tm=%d strideConst=%d)\n",
           mbcId, asmId, tmId, strideConstId);
    pool->release(); return 1;
  }

  // (1) Add the fixture SwVertex producer; wire it into _MeshBufferComponents.MeshBuffers — the Mesh
  //     boundary's own downstream. The bridge compounds then carry it through to the compute stage's SRV
  //     NATIVELY (Vertices → GBC → ComputeShaderStage.ShaderResources), no mid-graph rewire.
  //     ── RED tooth (injectBug): DON'T feed the mesh input. With no ShaderResource reaching the compute
  //     stage, ComputeShaderStage early-returns without dispatching (buffer_ops_computeshaderstage.cpp:86
  //     `if (uavs.empty()||srvs.empty()) return`) → the UAV write target stays zeroed → the readback
  //     diverges from the oracle. Same seam the point keystone bites on: proves the golden detects "the
  //     mesh vertex buffer did NOT reach the compute stage / the transform kernel did not run".
  if (!lib.symbols.count("t3xf_input_verts"))
    if (const NodeSpec* fs = findSpec("t3xf_input_verts"))
      lib.symbols["t3xf_input_verts"] = atomicSymbolFromSpec(*fs);
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_input_verts"; sym->children.push_back(p); }
  if (!injectBug) {  // faithful: feed the mesh input. -bug: leave it unfed → compute stage can't dispatch.
    SymbolConnection w; w.srcChild = fixtureId; w.srcSlot = "Buffer"; w.dstChild = mbcId; w.dstSlot = "MeshBuffers";
    sym->connections.push_back(w);
  }

  // (2) TransformMatrix overrides (the .t3 boundary Vector3 wires land only on .x heads — the established
  //     fork-t3-vec3-wire-lands-on-head; author the full transform on the imported TransformMatrix child).
  for (SymbolChild& c : sym->children) if (c.id == tmId) {
    c.overrides["Translation.x"] = TRN[0]; c.overrides["Translation.y"] = TRN[1]; c.overrides["Translation.z"] = TRN[2];
    c.overrides["Rotation_PitchYawRoll.x"] = ROT[0]; c.overrides["Rotation_PitchYawRoll.y"] = ROT[1]; c.overrides["Rotation_PitchYawRoll.z"] = ROT[2];
    c.overrides["Scale.x"] = SCL[0]; c.overrides["Scale.y"] = SCL[1]; c.overrides["Scale.z"] = SCL[2];
    c.overrides["UniformScale"] = 1.0f; c.overrides["RotationMode"] = 0.0f;
    break;
  }

  // (3) FORK `pbrvertex-stride-64-to-80-swvertex`: the imported IntValue(PBRVertex.Stride)→Const carries the
  //     .t3 value 64 (TiXL PbrVertex). sw's SwVertex is 80B, so override the Const's value to 80 ON THE
  //     IMPORTED CHILD (the native IntValue→SBV.Stride wire is faithful; only the constant is forked). SBV
  //     resolves Stride from this wired Const via evalResidentFloat (resolveResidentFloatInputs).
  for (SymbolChild& c : sym->children) if (c.id == strideConstId) { c.overrides["value"] = 80.0f; break; }

  // (void) useVertexSelection: now NATIVE. The .t3's UseVertexSelection boundary (default false) feeds
  //   BoolToFloat.BoolValue → BoolToFloat.Result (=ForFalse=0) → FloatsToBuffer.Params. Unwired boundary
  //   resolves to the Constant default (0 = false) → the 17th cb0 float is 0 (full transform) — exactly the
  //   load-bearing s=1 math, with ZERO scaffold. (Matches useVertexSelection=0.0f documented above.)
  (void)useVertexSelection;

  // ---- STEP 2: build the eval graph (production flattener) + settle the TransformMatrix rows ----
  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);
  ResidentEvalCtx rc; rc.frameIndex = 0; rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.lib = &lib;
  cookMatrixOutputNodes(g, rc);  // TransformMatrix → 4 transposed SRT rows onto extColorOut
  printf("[t3-transformmesh] buildEvalGraph: resident nodes=%zu\n", g.nodes.size());

  // Terminal = _AssembleMeshBuffers (the NATIVE Result output): ExecuteBufferUpdate.Output2 → Assemble.
  // Vertices → NewMeshBuffers → boundary Result. Reading the assemble node proves the FULL native chain
  // (both bridge compounds carried the transformed vertices), not just the compute stage's raw UAV.
  const std::string termPath = std::to_string(asmId);

  // ---- STEP 3: cook the resident graph; read back the _AssembleMeshBuffers output buffer as SwVertex[] ----
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-transformmesh] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f/60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);

  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N && outBuf->elementStride == sizeof(SwVertex);
  std::vector<SwVertex> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwVertex));

  // ---- Reference: the oracle's TRS-with-pivot (pivot=0). M = scale · rot(YawPitchRoll) · translate. ----
  M4 M = mulD(mulD(scaleD(SCL[0]*1.0, SCL[1]*1.0, SCL[2]*1.0),
                   rotD(ROT[1]*DEG, ROT[0]*DEG, ROT[2]*DEG)),
              transD(TRN[0], TRN[1], TRN[2]));
  double maxPos = 0.0, maxDir = 0.0; int worstI = -1; double wExp[3]{}, wGot[3]{};
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      double ep[3]; xformPoint(M, in[i].Position.x, in[i].Position.y, in[i].Position.z, ep);
      double gp[3] = { got[i].Position.x, got[i].Position.y, got[i].Position.z };
      double dp = std::sqrt((ep[0]-gp[0])*(ep[0]-gp[0])+(ep[1]-gp[1])*(ep[1]-gp[1])+(ep[2]-gp[2])*(ep[2]-gp[2]));
      double en[3]; xformDir(M, in[i].Normal.x, in[i].Normal.y, in[i].Normal.z, en); norm3(en);
      double gn[3] = { got[i].Normal.x, got[i].Normal.y, got[i].Normal.z };
      double dn = std::sqrt((en[0]-gn[0])*(en[0]-gn[0])+(en[1]-gn[1])*(en[1]-gn[1])+(en[2]-gn[2])*(en[2]-gn[2]));
      if (dp > maxPos) { maxPos = dp; worstI = (int)i; for (int k=0;k<3;k++){wExp[k]=ep[k];wGot[k]=gp[k];} }
      if (dn > maxDir) maxDir = dn;
    }

  printf("[t3-transformmesh] replay-vs-oracle: haveOut=%d maxPosErr=%.6f(need<1e-3) maxDirErr=%.6f(need<1e-3) "
         "worstI=%d exp=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f)\n",
         haveOut ? 1 : 0, maxPos, maxDir, worstI, wExp[0], wExp[1], wExp[2], wGot[0], wGot[1], wGot[2]);

  const bool parityGreen = haveOut && (maxPos < 1e-3) && (maxDir < 1e-3);
  printf("[t3-transformmesh] PARITY VERDICT: %s\n", parityGreen ? "GREEN (mesh replay reproduces the transform)"
                                                                : "RED (mesh replay seam gap)");

  mlib->release(); q->release(); dev->release();
  g_fixtureVerts = nullptr;

  if (!injectBug) {
    if (!parityGreen) { printf("[t3-transformmesh] FAIL\n"); pool->release(); return 1; }
    printf("[t3-transformmesh] PASS: TransformMesh.t3 replays to parity (mesh GPU-compute replay track LIVE)\n");
    pool->release();
    return 0;
  }

  // injectBug leg: the mesh input was NOT fed into _MeshBufferComponents (STEP 1b (1) -bug). With no
  // ShaderResource reaching the compute stage, ComputeShaderStage early-returns without dispatching, so the
  // UAV write target stays zeroed and the readback (all-zero vertices) diverges from the oracle. Tooth
  // bites iff NOT green — proving the golden depends on the transform kernel running on the real mesh
  // vertex buffer arriving THROUGH the native bridge compounds.
  const bool bites = !parityGreen;
  printf("[t3-transformmesh] -bug: routing tooth %s (parity green under bug == %s)\n",
         bites ? "BITES" : "TOOTHLESS", parityGreen ? "true" : "false");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
