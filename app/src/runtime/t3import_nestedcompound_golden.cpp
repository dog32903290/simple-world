// runtime/t3import_nestedcompound_golden — THE COMPOUND-RECURSION keystone (--selftest-t3-nestedcompound).
//
// The 復合戰役 total upstream gate: prove the .t3 importer RECURSES a child that is itself a TiXL
// compound into a NESTED sub-compound (not skip, not flatten). Load the REAL DrawPointsDOF.t3 (root),
// whose lone TransformPoints child (SymbolId 7f6c64fe…) is a pure sub-graph compound, through the
// PRODUCTION path importT3Symbol(…, resolver) → the recursion builds TransformPoints as a nested Symbol.
//
// ── TWO ASSERTIONS ────────────────────────────────────────────────────────────────────────────────
// 1. STRUCTURAL (遞迴靈魂): after import — lib[TransformPoints].atomic==false && children非空 (a nested
//    compound was BUILT, not skipped/flattened); the parent DrawPointsDOF holds a SymbolChild whose
//    symbolId == the TransformPoints guid; no "unmapped skipped" warning for that child; AND the
//    DrawPointsDOF→TransformPoints.Points CROSS-LAYER connection resolved to TransformPoints' Points
//    inputDef (= §1.3: swSlotNameForGuid returns "" for a compound guid, so this wire ONLY survives via
//    the nested-Symbol inputDef fallback — absent that fix it drops silently and the parity below is a
//    false green).
// 2. PARITY: cook the NESTED TransformPoints terminal buffer through buildEvalGraph → cookResident and
//    compare to the焊死 host-matrix oracle (the SAME closed-form the母本 t3import_transformpoints_golden
//    verifies TransformPoints with). The input Points cross the DrawPointsDOF→TransformPoints BOUNDARY
//    (a test-fixture Buffer producer lives in the ROOT and feeds the nested compound through the §1.3
//    connection) — so the cross-layer seam is LOAD-BEARING in the cook, not just structurally present.
//
// ── TEST SCAFFOLD (honest, mirrors the母本's documented scaffolding) ──────────────────────────────
// The transform is authored as constant overrides on the NESTED TransformMatrix child (its .t3 boundary
// Vector3 wires only land on .x heads — fork-t3-vec3-wire-lands-on-head), so those boundary→TM wires are
// severed first. The marshal Params (Space=ObjectSpace, StrengthFactor) ride a Const-child scaffold into
// IntsToBuffer.Params (two CONSTANT boundary wires into one MultiInput would collide destructively in the
// flatten; Connection drivers append in order — this is the母本's pre-骨7 Const-child pattern, applied
// inside the nested compound). Only the input source + the transform authoring are test-supplied; the
// RECURSION + the cross-layer Points connection are the production seam under test.
//
// injectBug flips t3RecurseDisable → the TransformPoints child reverts to "unmapped, skipped" → the
// nested compound never exists → the nested terminal path vanishes → no output buffer → RED (bites).
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
#include "runtime/graph_bridge.h"           // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"    // ResidentEvalGraph / buildEvalGraph / initResidentCache / ctx
#include "runtime/resident_value_cooks.h"   // cookMatrixOutputNodes (settle TransformMatrix rows)
#include "runtime/point_graph.h"            // PointGraph / residentSwBufferFor
#include "runtime/sw_buffer.h"              // SwBuffer
#include "runtime/t3_import.h"              // importT3Symbol / T3Resolver / t3RecurseDisable / symbolIdOfT3
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {

int runTransformPointsParityProbe(bool injectBug);  // oracle self-check (fused-kernel Euler-order gate)
void registerBuiltinPointOps();

namespace {

static const char* kDrawPointsDOFT3 =
#include "runtime/drawpointsdof_t3_embed.inc"
;
static const char* kTransformPointsT3 =
#include "runtime/transformpoints_t3_embed.inc"
;

// Guid constants (from the two .t3 top-level Ids + TransformPoints' Points inputDef).
const char* const kDrawPointsDOFGuid = "dcd04bb7-4531-40ab-bb18-26a3bc269dc4";
const char* const kTransformPointsGuid = "7f6c64fe-ca2e-445e-a9b4-c70291ce354e";
const char* const kTPPointsInputDef = "565ff364-c3d9-4c60-a9a0-79fdd36d3477";
const char* const kTPSpaceBoundary = "1ab4671f-7977-4e7e-bb06-f828ae32e3af";        // ObjectSpace(1) rail

// ── Test-fixture Buffer producer (identical to the母本; emits a fixed N-point SwPoint bag) ─────────
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
  s.ports = {{"Buffer", "Buffer", "Buffer", false}};
  s.evaluate = nullptr;
  return s;
}
const BufferOp _reg_t3xf_nested_input_points(fixtureSpec(), cookInputPointsFixture);

// ── TiXL host-matrix reference (closed-form, the xfprobe's proven math — verbatim from the母本) ────
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

int childIdOfType(const Symbol& s, const std::string& type) {
  for (const SymbolChild& c : s.children) if (c.symbolId == type) return c.id;
  return 0;
}

}  // namespace

int runT3NestedCompoundParity(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  registerBuiltinPointOps();

  // ---- Reference transform (ObjectSpace; matches the母本 / xfprobe shape) ----
  const double DEG = M_PI / 180.0;
  const float ROT[3] = {37.0f, 53.0f, 71.0f};
  const float SCL[3] = {1.7f, 0.6f, 2.3f};
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

  // ---- STEP 1: import the ROOT DrawPointsDOF.t3 with a resolver that recurses TransformPoints.t3 ----
  std::map<std::string, std::string> resolverMap;
  { std::string tpId; if (symbolIdOfT3(kTransformPointsT3, &tpId)) resolverMap[tpId] = kTransformPointsT3; }
  T3Resolver resolve = [&resolverMap](const std::string& g, std::string& out) -> bool {
    auto it = resolverMap.find(g);
    if (it == resolverMap.end()) return false;
    out = it->second; return true;
  };
  t3RecurseDisable() = injectBug;  // -bug: refuse recursion → TransformPoints reverts to skip
  SymbolLibrary lib;
  std::string rootId;
  std::vector<std::string> warnings;
  bool ok = importT3Symbol(kDrawPointsDOFT3, lib, &rootId, &warnings, resolve);
  t3RecurseDisable() = false;
  if (!ok) { printf("[t3-nested] FAIL: importT3Symbol(DrawPointsDOF) returned false\n"); pool->release(); return injectBug ? 1 : 1; }

  Symbol* dp = lib.find(rootId);
  if (!dp) { printf("[t3-nested] FAIL: root DrawPointsDOF symbol missing\n"); pool->release(); return 1; }

  // ---- STEP 2: STRUCTURAL assertions (the recursion soul) ----
  Symbol* tp = lib.find(kTransformPointsGuid);          // nested compound built by the recursion?
  const int tpChildId = childIdOfType(*dp, kTransformPointsGuid);  // parent references it by guid?
  bool unmappedTP = false;
  for (const std::string& w : warnings)
    if (w.find("f8aadb8e") != std::string::npos && w.find("unmapped") != std::string::npos) unmappedTP = true;
  // §1.3 cross-layer connection: DrawPointsDOF→TransformPoints.Points resolved to the Points inputDef?
  bool crossLayerPointsWire = false;
  for (const SymbolConnection& c : dp->connections)
    if (c.dstChild == tpChildId && c.dstSlot == kTPPointsInputDef) crossLayerPointsWire = true;

  const bool nestedBuilt = tp && !tp->atomic && !tp->children.empty();
  const bool structGreen = (rootId == kDrawPointsDOFGuid) && nestedBuilt && (tpChildId != 0) &&
                           !unmappedTP && crossLayerPointsWire;
  printf("[t3-nested] structural: root=%s tpChild=%d nested{present=%d atomic=%d children=%d} "
         "unmappedTP=%d crossLayerPointsWire=%d -> %s\n",
         rootId.c_str(), tpChildId, tp ? 1 : 0, tp ? (int)tp->atomic : -1,
         tp ? (int)tp->children.size() : -1, unmappedTP ? 1 : 0, crossLayerPointsWire ? 1 : 0,
         structGreen ? "GREEN" : (injectBug ? "RED(expected under -bug)" : "RED"));

  if (!injectBug && !structGreen) {
    printf("[t3-nested] FAIL: structural gate red (recursion / §1.3 seam not live)\n");
    pool->release(); return 1;
  }

  // Under -bug the recursion is OFF: TransformPoints is skipped, so there is no nested compound to cook.
  // That absence IS the bite — no terminal, no output buffer, parity RED. Short-circuit to the verdict.
  if (injectBug && !nestedBuilt) {
    printf("[t3-nested] -bug: recursion tooth BITES (nested TransformPoints absent → no terminal → RED)\n");
    g_fixturePoints = nullptr; pool->release();
    return 1;  // bites (RED under bug) — healthy tooth (GOLDEN_STANDARD 特徵3)
  }

  // ---- STEP 3: TEST-SCAFFOLD authoring INSIDE the nested TransformPoints compound + the ROOT fixture ----
  // (a) sever the boundary→TransformMatrix wires (author the transform directly) + set TM overrides.
  const int tmId = childIdOfType(*tp, "TransformMatrix");
  const int intsId = childIdOfType(*tp, "IntsToBuffer");
  const int ebuId = childIdOfType(*tp, "ExecuteBufferUpdate");
  if (!tmId || !intsId || !ebuId) {
    printf("[t3-nested] FAIL: nested TM/IntsToBuffer/ExecuteBufferUpdate not all mapped (tm=%d ints=%d ebu=%d)\n",
           tmId, intsId, ebuId);
    pool->release(); return 1;
  }
  {
    std::vector<SymbolConnection> kept;
    for (const SymbolConnection& c : tp->connections)
      if (c.dstChild != tmId) kept.push_back(c);   // drop boundary→TransformMatrix (transform is authored)
    tp->connections = std::move(kept);
  }
  for (SymbolChild& c : tp->children) if (c.id == tmId) {
    c.overrides["Translation.x"] = TRN[0]; c.overrides["Translation.y"] = TRN[1]; c.overrides["Translation.z"] = TRN[2];
    c.overrides["Rotation_PitchYawRoll.x"] = ROT[0]; c.overrides["Rotation_PitchYawRoll.y"] = ROT[1]; c.overrides["Rotation_PitchYawRoll.z"] = ROT[2];
    c.overrides["Scale.x"] = SCL[0]; c.overrides["Scale.y"] = SCL[1]; c.overrides["Scale.z"] = SCL[2];
    c.overrides["UniformScale"] = 1.0f; c.overrides["RotationMode"] = 0.0f;
    break;
  }
  // (b) Const-child scaffold for the marshal Params (FloatsToBuffer.Strength + IntsToBuffer.Space/
  //     StrengthFactor). The resident marshal cook (point_graph_resident_buffer.cpp:181-183) collects
  //     ONLY Connection-driven scalars — a Constant primary is not a payload float. In the nested flatten
  //     a boundary→Params wire resolves to a Constant, so every marshal param would silently drop
  //     (strength→0 → identity transform). Repoint each to a Const CHILD (Connection driver, appended in
  //     wire order — the母本's pre-骨7 pattern), fed the TransformPoints boundary default value.
  const int fbId = childIdOfType(*tp, "FloatsToBuffer");
  if (!fbId) { printf("[t3-nested] FAIL: nested FloatsToBuffer not mapped\n"); pool->release(); return 1; }
  if (!lib.symbols.count("Const"))
    if (const NodeSpec* cs = findSpec("Const")) lib.symbols["Const"] = atomicSymbolFromSpec(*cs);
  auto marshalParamValue = [](const std::string& srcSlot) -> float {
    if (srcSlot.rfind("1ab4671f", 0) == 0) return 1.0f;  // Space = ObjectSpace(1)
    if (srcSlot.rfind("a2b65311", 0) == 0) return 0.0f;  // StrengthFactor = 0
    if (srcSlot.rfind("fb2cfc5e", 0) == 0) return 1.0f;  // Strength = 1
    return 0.0f;
  };
  for (SymbolConnection& c : tp->connections)
    if (c.srcChild == kSymbolBoundary && c.dstSlot == "Params" && (c.dstChild == intsId || c.dstChild == fbId)) {
      const int cid = tp->nextChildId++;
      SymbolChild s; s.id = cid; s.symbolId = "Const"; s.overrides["value"] = marshalParamValue(c.srcSlot);
      tp->children.push_back(s);
      c.srcChild = cid; c.srcSlot = "out";
    }
  // (c) ROOT fixture: add the Buffer producer to DrawPointsDOF and repoint the DrawPointsDOF→
  //     TransformPoints.Points cross-layer wire's SOURCE at it (the §1.3-resolved dstSlot is left intact
  //     → the buffer flows ACROSS the boundary into the nested compound's Points input).
  const int fixtureId = dp->nextChildId++;
  { SymbolChild p; p.id = fixtureId; p.symbolId = "t3xf_input_points"; dp->children.push_back(p); }
  if (!lib.symbols.count("t3xf_input_points"))
    if (const NodeSpec* fs = findSpec("t3xf_input_points")) lib.symbols["t3xf_input_points"] = atomicSymbolFromSpec(*fs);
  int repointed = 0;
  for (SymbolConnection& c : dp->connections)
    if (c.dstChild == tpChildId && c.dstSlot == kTPPointsInputDef) {
      c.srcChild = fixtureId; c.srcSlot = "Buffer"; ++repointed;
    }
  if (repointed == 0) { printf("[t3-nested] FAIL: no DrawPointsDOF→TransformPoints.Points wire to repoint\n"); pool->release(); return 1; }

  // ---- STEP 4: build the eval graph (recursively inlines the nested compound) + settle matrix rows ----
  ResidentEvalGraph g = buildEvalGraph(lib, rootId);
  initResidentCache(g);
  ResidentEvalCtx rc; rc.frameIndex = 0; rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.lib = &lib;
  cookMatrixOutputNodes(g, rc);
  // The nested terminal lives UNDER the TransformPoints child's inline prefix: "<tpChildId>/<ebuId>".
  const std::string termPath = std::to_string(tpChildId) + "/" + std::to_string(ebuId);
  printf("[t3-nested] buildEvalGraph: resident nodes=%zu termPath=%s\n", g.nodes.size(), termPath.c_str());

  // ---- STEP 5: cook the resident graph; read back the nested ExecuteBufferUpdate output buffer ----
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) { printf("[t3-nested] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }

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
      V4 expRot = qMulD(R, oq);
      V3 gp = { got[i].Position.x, got[i].Position.y, got[i].Position.z };
      V4 gr = { got[i].Rotation.x, got[i].Rotation.y, got[i].Rotation.z, got[i].Rotation.w };
      double ep = std::sqrt((expPos.x-gp.x)*(expPos.x-gp.x)+(expPos.y-gp.y)*(expPos.y-gp.y)+(expPos.z-gp.z)*(expPos.z-gp.z));
      double er = 1.0 - std::fabs(dot4(expRot, gr));
      if (ep > maxPos) { maxPos = ep; worstI = (int)i; wExp = expPos; wGot = gp; }
      if (er > maxRot) maxRot = er;
    }

  int oracle = runTransformPointsParityProbe(/*injectBug=*/false);  // the fused-kernel oracle must be GREEN

  printf("[t3-nested] replay-vs-host-matrix: haveOut=%d maxPosErr=%.6f(need<1e-3) maxRotErr=%.6f(need<1e-4) "
         "worstI=%d exp=(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f)\n",
         haveOut ? 1 : 0, maxPos, maxRot, worstI, wExp.x, wExp.y, wExp.z, wGot.x, wGot.y, wGot.z);
  printf("[t3-nested] oracle xfprobe (faithful) -> %s\n", oracle == 0 ? "GREEN" : "RED");

  const bool parityGreen = haveOut && (oracle == 0) && (maxPos < 1e-3) && (maxRot < 1e-4);
  printf("[t3-nested] PARITY VERDICT: %s\n", parityGreen ? "GREEN (nested compound replays the transform)"
                                                         : "RED (nested replay seam gap)");
  mlib->release(); q->release(); dev->release();
  g_fixturePoints = nullptr;

  if (!injectBug) {
    if (!(structGreen && parityGreen)) { printf("[t3-nested] FAIL\n"); pool->release(); return 1; }
    printf("[t3-nested] PASS: DrawPointsDOF.t3 recurses TransformPoints into a nested compound that replays to parity\n");
    pool->release(); return 0;
  }

  // injectBug leg (recursion disabled but nested somehow still built — shouldn't happen; kept for symmetry):
  // the tooth bites iff parity is NOT green.
  const bool bites = !parityGreen;
  printf("[t3-nested] -bug: recursion tooth %s (parity green under bug == %s)\n",
         bites ? "BITES" : "TOOTHLESS", parityGreen ? "true" : "false");
  pool->release();
  return bites ? 1 : 0;  // dead tooth exits 0 → --bite NO-BITE list catches it (GOLDEN_STANDARD 特徵3)
}

}  // namespace sw
