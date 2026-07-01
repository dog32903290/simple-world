// runtime/t3import_transformpoints_golden — THE KEYSTONE首證 (--selftest-t3-transformpoints).
//
// The REAL bet of the atomic-replay strategy, now GREEN: load the REAL TiXL TransformPoints.t3
// (embedded byte-faithful below), walk the PRODUCTION path importT3Symbol → buildEvalGraph →
// cookMatrixOutputNodes → cookResident, read back the transformed Point buffer, and compare it against
// the TiXL host-matrix reference (the SAME closed-form the焊死 xfprobe verifies the fused kernel with).
// This proves a .t3 GPU-compute compound REPLAYS as an sw atom-nested graph and produces byte-faithful
// transformed points — the閘 that unlocks the 187 unfinished compute compounds' GPU-replay track.
//
// ── WHAT MADE IT GREEN (compute-stage keystone) ──────────────────────────────────────────────────
// The .t3 composes the transform from 11 children. The 5 formerly-unmapped ones now have sw atoms:
//   ComputeShaderStage (generic bind CB/SRV/UAV + dispatch a named MSL kernel), StructuredBufferWithViews
//   (allocate the UAV write target), TransformMatrix (the SRT→4-row matrix, existing math atom),
//   CalcDispatchCount (existing value op; dispatch folded into the stage), and ComputeShader (its Source
//   folds onto the stage's KernelName). The proving kernel is computeshaderstage_transformpoints.metal —
//   a faithful HLSL-ABI port of TransformPoints.hlsl driven by the REAL assembled const buffers
//   (FloatsToBuffer matrix+strength at b0, IntsToBuffer space+strengthfactor at b1). The compute-stage
//   ATOM is GENERIC (binding driven by wired buffers), not TransformPoints-special-cased.
//
// ── FEEDING THE INPUT (honest test scaffold) ─────────────────────────────────────────────────────
// The .t3's `Points` is a BOUNDARY input with no in-graph producer, and cookResident has no GPU-buffer
// boundary-injection seam. So the golden REWIRES the imported symbol: a test-fixture Buffer producer
// (t3xf_input_points, registered in this TU) emits a known N-point bag, and the boundary→GetBufferComponents
// wire is repointed at it. The TRANSFORM is set as constant overrides on the imported TransformMatrix child
// (the .t3's boundary Vector3 inputs don't vec-decompose through one wire — fork-t3-vec3-wire-lands-on-head),
// matching the reference. This scaffolding drives the SAME production atoms/cook the live app runs; only the
// input source + the transform's authoring are test-supplied.
//
// ZONE: runtime golden (shell tier — binds runtime import + resident cook + the host-matrix reference).
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
#include "runtime/graph.h"                  // findSpec / registerBuiltinPointOps
#include "runtime/graph_bridge.h"           // atomicSymbolFromSpec (add the fixture atom symbol to the lib)
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph / initResidentCache / ctx
#include "runtime/resident_value_cooks.h"   // cookMatrixOutputNodes (settle TransformMatrix rows)
#include "runtime/point_graph.h"            // PointGraph / residentSwBufferFor
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/t3_import.h"              // importT3Symbol / t3ImportInjectBug
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {

int runTransformPointsParityProbe(bool injectBug);  // oracle self-check (fused-kernel Euler-order gate)
void registerBuiltinPointOps();

namespace {

static const char* kTransformPointsT3 =
#include "runtime/transformpoints_t3_embed.inc"
;

// ── Test-fixture Buffer producer ────────────────────────────────────────────────────────────────
// Emits a fixed N-point SwPoint bag as a SwBuffer (elementStride 64, elementCount N), the input the
// .t3's `Points` boundary would receive from an upstream point generator. Registered as a Buffer op so
// findSpec resolves it and the resident buffer cook drives it. Lives ONLY in this golden TU.
std::vector<SwPoint>* g_fixturePoints = nullptr;

void cookInputPointsFixture(BufferCookCtx& c) {
  if (!c.output || !c.requestBytes || !g_fixturePoints) return;
  const uint32_t n = (uint32_t)g_fixturePoints->size();
  if (n == 0) return;
  const uint32_t bytes = n * (uint32_t)sizeof(SwPoint);
  void* dst = c.requestBytes(bytes);
  if (!dst) return;
  std::memcpy(dst, g_fixturePoints->data(), bytes);
  c.output->elementStride = (uint32_t)sizeof(SwPoint);
  c.output->elementCount = n;
  c.output->elementFormat = 0;
}

NodeSpec fixtureSpec() {
  NodeSpec s;
  s.type = "t3xf_input_points";
  s.title = "t3xf_input_points";
  s.category = "test";
  s.ports = {{"Buffer", "Buffer", "Buffer", false}};  // a pure producer (no inputs)
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_t3xf_input_points(fixtureSpec(), cookInputPointsFixture);

// ── TiXL host-matrix reference (closed-form, the xfprobe's proven math) ───────────────────────────
struct V3 { double x, y, z; };
struct V4 { double x, y, z, w; };
struct M4 { double m[4][4]; };
V4 cfypr(double yaw, double pitch, double roll) {
  double sr = std::sin(roll*0.5), cr = std::cos(roll*0.5);
  double sp = std::sin(pitch*0.5), cp = std::cos(pitch*0.5);
  double sy = std::sin(yaw*0.5), cy = std::cos(yaw*0.5);
  return { cy*sp*cr + sy*cp*sr, sy*cp*cr - cy*sp*sr, cy*cp*sr - sy*sp*cr, cy*cp*cr + sy*sp*sr };
}
V4 qMulD(V4 a, V4 b) {
  return { b.x*a.w + a.x*b.w + (a.y*b.z - a.z*b.y),
           b.y*a.w + a.y*b.w + (a.z*b.x - a.x*b.z),
           b.z*a.w + a.z*b.w + (a.x*b.y - a.y*b.x),
           a.w*b.w - (a.x*b.x + a.y*b.y + a.z*b.z) };
}
double dot4(V4 a, V4 b) { return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w; }
M4 cfq(V4 q) {
  double xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z, xy=q.x*q.y, wz=q.z*q.w,
         xz=q.z*q.x, wy=q.y*q.w, yz=q.y*q.z, wx=q.x*q.w;
  M4 r{};
  r.m[0][0]=1-2*(yy+zz); r.m[0][1]=2*(xy+wz);   r.m[0][2]=2*(xz-wy);
  r.m[1][0]=2*(xy-wz);   r.m[1][1]=1-2*(zz+xx); r.m[1][2]=2*(yz+wx);
  r.m[2][0]=2*(xz+wy);   r.m[2][1]=2*(yz-wx);   r.m[2][2]=1-2*(yy+xx);
  r.m[3][3]=1; return r;
}
M4 scaleM(double x, double y, double z){ M4 r{}; r.m[0][0]=x; r.m[1][1]=y; r.m[2][2]=z; r.m[3][3]=1; return r; }
M4 transM(double x, double y, double z){ M4 r{}; for(int i=0;i<4;i++) r.m[i][i]=1; r.m[3][0]=x; r.m[3][1]=y; r.m[3][2]=z; return r; }
M4 mul(const M4&a,const M4&b){ M4 r{}; for(int i=0;i<4;i++)for(int j=0;j<4;j++){double s=0;for(int k=0;k<4;k++)s+=a.m[i][k]*b.m[k][j]; r.m[i][j]=s;} return r; }
V3 xform(V3 v,const M4&m){ return { v.x*m.m[0][0]+v.y*m.m[1][0]+v.z*m.m[2][0]+m.m[3][0],
                                    v.x*m.m[0][1]+v.y*m.m[1][1]+v.z*m.m[2][1]+m.m[3][1],
                                    v.x*m.m[0][2]+v.y*m.m[1][2]+v.z*m.m[2][2]+m.m[3][2] }; }

// Find the imported symbol child of a given sw type; return its id (0 if absent).
int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

int runT3TransformPointsParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();  // registers atoms + point ops → findSpec resolves them

  // ---- Reference transform (ObjectSpace; matches the shape the xfprobe exercises) ----
  const double DEG = M_PI / 180.0;
  const float ROT[3] = {37.0f, 53.0f, 71.0f};   // RotationX=pitch, Y=yaw, Z=roll
  const float SCL[3] = {1.7f, 0.6f, 2.3f};      // non-uniform Stretch
  const float TRN[3] = {0.4f, -0.3f, 0.9f};

  // ---- Input bag: N points with non-identity Position AND per-point Rotation (as the xfprobe) ----
  const uint32_t N = 24;
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{ (float)(std::cos(a*6.2831853)*1.3),
                                 (float)(std::sin(a*6.2831853)*0.8), (float)((a-0.5)*1.5) };
    V4 oq = cfypr((20.0+90.0*a)*DEG, (-15.0+40.0*a)*DEG, (10.0+60.0*a)*DEG);
    in[i].Rotation = SW_FLOAT4{ (float)oq.x, (float)oq.y, (float)oq.z, (float)oq.w };
    in[i].FX1 = 1.0f; in[i].FX2 = 1.0f;
    in[i].Scale = SW_PACKED3{1.0f, 1.0f, 1.0f};
  }
  g_fixturePoints = &in;

  // ---- STEP 1: import the real .t3 via the PRODUCTION importer ----
  t3ImportInjectBug() = injectBug;
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kTransformPointsT3, lib, &rootId, &warnings);
  t3ImportInjectBug() = false;
  if (!ok) { printf("[t3-transformpoints] FAIL: importT3Symbol returned false\n"); pool->release(); return 1; }

  Symbol* sym = const_cast<Symbol*>(lib.find(rootId));
  if (!sym) { printf("[t3-transformpoints] FAIL: root symbol missing\n"); pool->release(); return 1; }
  const int nChildren = (int)sym->children.size();
  int unmappedChildren = 0, droppedWires = 0;
  for (const std::string& w : warnings) {
    if (w.find("unmapped SymbolId") != std::string::npos) ++unmappedChildren;
    else if (w.find("dropped") != std::string::npos) ++droppedWires;
  }
  printf("[t3-transformpoints] import: rootId=%s children=%d conns=%d (unmapped=%d dropped=%d) warnings=%zu\n",
         rootId.c_str(), nChildren, (int)sym->connections.size(), unmappedChildren, droppedWires,
         warnings.size());
  { std::map<std::string, int> byType;
    for (const SymbolChild& c : sym->children) byType[c.symbolId]++;
    printf("[t3-transformpoints]   mapped atom types:");
    for (const auto& kv : byType) printf(" %s×%d", kv.first.c_str(), kv.second);
    printf("\n"); }

  // ---- STEP 1b: TEST-SCAFFOLD rewire (input source + transform authoring; see file header) ----
  // (a) Add the fixture producer child; repoint the `Points` boundary wire (into GetBufferComponents) at it.
  const int gbc2 = [&]{  // the GetBufferComponents fed by the Points boundary (the input-side one)
    for (const SymbolConnection& w : sym->connections)
      if (w.srcChild == kSymbolBoundary && w.dstSlot == "BufferWithViews") return w.dstChild;
    return 0; }();
  if (!gbc2) { printf("[t3-transformpoints] FAIL: no Points boundary → GetBufferComponents wire\n"); pool->release(); return 1; }
  const int fixtureId = sym->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_input_points"; sym->children.push_back(p); }
  // Add the fixture ATOM symbol to the lib so buildEvalGraph can inline it (mirror of the importer's
  // atomicSymbolFromSpec(findSpec(...)) for every mapped atom).
  if (!lib.symbols.count("t3xf_input_points"))
    if (const NodeSpec* fs = findSpec("t3xf_input_points"))
      lib.symbols["t3xf_input_points"] = atomicSymbolFromSpec(*fs);
  // repoint the boundary wire: srcChild boundary→fixture, srcSlot Points→"Buffer"
  for (SymbolConnection& w : sym->connections)
    if (w.srcChild == kSymbolBoundary && w.dstChild == gbc2 && w.dstSlot == "BufferWithViews") {
      w.srcChild = fixtureId; w.srcSlot = "Buffer";
    }

  // (b) Set the transform as overrides on the imported TransformMatrix child (Rotation/Scale/Translation
  //     heads; the .t3 boundary Vector3 wires land only on .x heads — fork-t3-vec3-wire-lands-on-head).
  const int tmId = childIdOfType(*sym, "TransformMatrix");
  if (!tmId) { printf("[t3-transformpoints] FAIL: TransformMatrix child not mapped\n"); pool->release(); return 1; }
  for (SymbolChild& c : sym->children) if (c.id == tmId) {
    c.overrides["Translation.x"] = TRN[0]; c.overrides["Translation.y"] = TRN[1]; c.overrides["Translation.z"] = TRN[2];
    c.overrides["Rotation_PitchYawRoll.x"] = ROT[0]; c.overrides["Rotation_PitchYawRoll.y"] = ROT[1]; c.overrides["Rotation_PitchYawRoll.z"] = ROT[2];
    c.overrides["Scale.x"] = SCL[0]; c.overrides["Scale.y"] = SCL[1]; c.overrides["Scale.z"] = SCL[2];
    c.overrides["UniformScale"] = 1.0f; c.overrides["RotationMode"] = 0.0f;
    break;
  }
  // (c) Marshal-payload scalars via Const producers (the .t3 wires these from boundary inputs; sw's
  //     resident marshal-Float gather only collects Connection-driven sources, so the golden supplies the
  //     values a parent/upstream would as real wired Const nodes). Repoint each boundary→marshal.Params
  //     wire at a Const: Strength=1.0 → FloatsToBuffer.Params; Space=1(ObjectSpace) + StrengthFactor=0 →
  //     IntsToBuffer.Params (wire order = Space then StrengthFactor, matching cb1 = {Space, StrengthFactor}).
  auto addConst = [&](float v) -> int {
    int id = sym->nextChildId++;
    SymbolChild c; c.id = id; c.symbolId = "Const"; c.overrides["value"] = v;
    sym->children.push_back(c);
    if (!lib.symbols.count("Const"))
      if (const NodeSpec* fs = findSpec("Const")) lib.symbols["Const"] = atomicSymbolFromSpec(*fs);
    return id;
  };
  // Rebind each boundary→(marshal).Params wire (in ARRAY ORDER, preserving MultiInput order) to a Const.
  // Strength boundary guid → FloatsToBuffer.Params; Space & StrengthFactor boundary → IntsToBuffer.Params.
  for (SymbolConnection& w : sym->connections) {
    if (w.srcChild != kSymbolBoundary || w.dstSlot != "Params") continue;
    const std::string swType = [&]{
      for (const SymbolChild& c : sym->children) if (c.id == w.dstChild) return c.symbolId; return std::string(); }();
    float v = 0.0f;
    if (swType == "FloatsToBuffer") v = 1.0f;          // Strength
    else if (swType == "IntsToBuffer") {
      // two wires land here: Space(first) then StrengthFactor(second). Distinguish by the boundary guid.
      // Space guid 1ab4671f…, StrengthFactor guid a2b65311…
      v = (w.srcSlot.rfind("1ab4671f", 0) == 0) ? 1.0f /*ObjectSpace*/ : 0.0f /*StrengthFactor*/;
    }
    w.srcChild = addConst(v); w.srcSlot = "out";
  }

  // ---- STEP 2: build the eval graph via the PRODUCTION flattener + settle the matrix rows ----
  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);
  ResidentEvalCtx rc; rc.frameIndex = 0; rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.lib = &lib;
  cookMatrixOutputNodes(g, rc);  // TransformMatrix → 4 transposed SRT rows onto extColorOut
  printf("[t3-transformpoints] buildEvalGraph: resident nodes=%zu\n", g.nodes.size());

  // Locate the ExecuteBufferUpdate resident node (the graph's terminal — its output is the transformed bag).
  const int ebuId = childIdOfType(*sym, "ExecuteBufferUpdate");
  if (!ebuId) { printf("[t3-transformpoints] FAIL: ExecuteBufferUpdate not mapped\n"); pool->release(); return 1; }
  const std::string termPath = std::to_string(ebuId);

  // ---- STEP 3: cook the resident graph; read back the ExecuteBufferUpdate output buffer ----
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(
      NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-transformpoints] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

  PointGraph pg(dev, mlib, q, 64, 64);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f/60.0f;
  pg.cookResident(g, ctx, nullptr, termPath);
  const SwBuffer* outBuf = pg.residentSwBufferFor(termPath);

  bool haveOut = outBuf && outBuf->bytes && outBuf->elementCount == N;
  std::vector<SwPoint> got(N);
  if (haveOut) std::memcpy(got.data(), const_cast<MTL::Buffer*>(outBuf->bytes)->contents(), N * sizeof(SwPoint));

  // ---- Reference (ObjectSpace, pivot=0): M = Scale * CreateFromQuaternion(R) * Translation ----
  V4 R = cfypr(ROT[1]*DEG, ROT[0]*DEG, ROT[2]*DEG);  // yaw=Y, pitch=X, roll=Z (proven Euler order)
  M4 M = mul(mul(scaleM(SCL[0],SCL[1],SCL[2]), cfq(R)), transM(TRN[0],TRN[1],TRN[2]));
  double maxPos = 0.0, maxRot = 0.0; int worstI = -1; V3 wExp{}, wGot{};
  if (haveOut)
    for (uint32_t i = 0; i < N; ++i) {
      V3 op = { in[i].Position.x, in[i].Position.y, in[i].Position.z };
      V4 oq = { in[i].Rotation.x, in[i].Rotation.y, in[i].Rotation.z, in[i].Rotation.w };
      V3 expPos = xform(op, M);
      V4 expRot = qMulD(R, oq);  // ObjectSpace: newRot = qMul(R, orgRot)
      V3 gp = { got[i].Position.x, got[i].Position.y, got[i].Position.z };
      V4 gr = { got[i].Rotation.x, got[i].Rotation.y, got[i].Rotation.z, got[i].Rotation.w };
      double ep = std::sqrt((expPos.x-gp.x)*(expPos.x-gp.x)+(expPos.y-gp.y)*(expPos.y-gp.y)+(expPos.z-gp.z)*(expPos.z-gp.z));
      double er = 1.0 - std::fabs(dot4(expRot, gr));
      if (ep > maxPos) { maxPos = ep; worstI = (int)i; wExp = expPos; wGot = gp; }
      if (er > maxRot) maxRot = er;
    }

  // ---- Oracle self-check: the fused-kernel xfprobe must be GREEN (its known-good transform) ----
  int oracle = runTransformPointsParityProbe(/*injectBug=*/false);

  printf("[t3-transformpoints] replay-vs-host-matrix: haveOut=%d maxPosErr=%.6f(need<1e-3) maxRotErr=%.6f(need<1e-4) "
         "worstI=%d exp=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f)\n",
         haveOut ? 1 : 0, maxPos, maxRot, worstI, wExp.x, wExp.y, wExp.z, wGot.x, wGot.y, wGot.z);
  printf("[t3-transformpoints] oracle xfprobe (faithful) -> %s\n", oracle == 0 ? "GREEN" : "RED");

  const bool parityGreen = haveOut && (oracle == 0) && (maxPos < 1e-3) && (maxRot < 1e-4);
  printf("[t3-transformpoints] PARITY VERDICT: %s\n", parityGreen ? "GREEN (replay reproduces the transform)"
                                                                   : "RED (replay seam gap)");

  mlib->release(); q->release(); dev->release();
  g_fixturePoints = nullptr;

  if (!injectBug) {
    if (!parityGreen) { printf("[t3-transformpoints] FAIL\n"); pool->release(); return 1; }
    printf("[t3-transformpoints] PASS: TransformPoints.t3 replays to parity (GPU-compute replay track LIVE)\n");
    pool->release();
    return 0;
  }

  // injectBug leg: the importer's routing tooth reversed the FIRST MultiInput collision (the two boundary
  // wires into IntsToBuffer.Params = Space/StrengthFactor). That perturbs a KEPT wire order → the replay
  // must DIVERGE from the reference (or fail to produce). Tooth bites iff parity is NOT green under -bug.
  const bool bites = !parityGreen;
  printf("[t3-transformpoints] -bug: routing tooth %s (parity green under bug == %s)\n",
         bites ? "BITES" : "TOOTHLESS", parityGreen ? "true" : "false");
  pool->release();
  return bites ? 1 : 2;  // non-zero = bite seen by --bite; 2 flags a toothless tooth distinctly
}

}  // namespace sw
