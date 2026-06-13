// PolarTransformPoints — lane-P MODIFIER op (batch 16): TRS pre-transform + cartesian->polar warp.
// Faithful port of external/tixl .../point/transform/PolarTransformPoints (.cs slots, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output): each point is
// TRS-transformed, then mapped cartesian->cylindrical (Mode 0) or ->spherical (Mode 1), and its
// rotation is composed with the polar-angle rotations. Count is INHERITED from upstream.
//
// TiXL parity (PolarTransformPoints.cs/.hlsl):
//   - ports: Points, Translation(Vec3,0), Rotation(Vec3,0), Scale(Vec3,1), UniformScale(f,1),
//     Mode(enum{CartesianToCylindrical=0,CartesianToSpherical=1},0).
//   - math: pos = mul(float4(pos,1),TransformMatrix); rotYAxis=qFromAngleAxis(pos.x,(0,1,0));
//     rotXAxis=qFromAngleAxis(-pos.y,(1,0,0)); [spherical pre-step]; cylindrical wrap;
//     [spherical rot]; rot=qMul(rotYAxis,rot).
//   - FORK (see polartransformpoints.metal): the TRS matrix is composed IN the shader from raw
//     scalars (pivot=0/shear=0/invert=false — no such ports on PolarTransform), not a host float4x4.
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                     // calcDispatchCount
#include "runtime/graph.h"                        // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"                  // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/polartransformpoints_params.h"  // PolarTransformParams, PolarTransformBinding
#include "runtime/tixl_point.h"                   // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// PolarTransformPoints modifier: dispatch the polartransformpoints kernel input bag -> output bag.
// count comes from c.count (inherited from upstream Points bag). No input bag = safe no-op.
void cookPolarTransformPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("polartransformpoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  PolarTransformParams P{};
  P.Count = c.count;
  P.Mode = (int)(cookParam(c, "Mode", 0.0f) + 0.5f);
  P.UniformScale = cookParam(c, "UniformScale", 1.0f);
  float t[3] = {0, 0, 0}, r[3] = {0, 0, 0}, s[3] = {1, 1, 1};
  cookVecN(c, "Translation", t, 3, t);
  cookVecN(c, "Rotation", r, 3, r);
  cookVecN(c, "Scale", s, 3, s);
  P.TranslationX = t[0]; P.TranslationY = t[1]; P.TranslationZ = t[2];
  P.RotationX = r[0]; P.RotationY = r[1]; P.RotationZ = r[2];
  P.ScaleX = s[0]; P.ScaleY = s[1]; P.ScaleZ = s[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, POLARXF_SourcePoints);
  enc->setBuffer(c.output, 0, POLARXF_ResultPoints);
  enc->setBytes(&P, sizeof(P), POLARXF_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capPolar = nullptr;
void captureDrawPolar(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capPolar || !pts || c.count == 0) return;
  g_capPolar->assign(c.count, SwPoint{});
  std::memcpy(g_capPolar->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerPolarTransformPointsOp() {
  registerPointOp("PolarTransformPoints", cookPolarTransformPoints);
}

// Golden: LinePoints(N along X, x in [-pi,pi]) -> PolarTransformPoints(Mode=Cylindrical,
// Translation.z=R) -> capture.
// The TRS step lifts each point to (x, 0, R); the cylindrical wrap maps it to
// (R*sin(x), 0, R*cos(x)) — a circle of radius R in the XZ plane (a "cylinder of radius R wrapped
// by angle x"). This exercises BOTH the TRS pre-transform (Translation.z) AND the polar warp.
// TEETH:
//   (1) count is PRESERVED.
//   (2) EVERY output point lies on the cylinder: dist from the Y axis sqrt(x^2+z^2) ~= R
//       (max radial error < 1e-3).
//   (3) the wrap is non-degenerate: the angle actually sweeps -> output spans BOTH signs of x and
//       of z (sin/cos over [-pi,pi]) -> not all bunched at one angle.
// injectBug: Translation.z=0 -> points stay at (x,0,0) -> wrap gives (0*sin,0,0*cos)=origin ->
//   radius 0 for all -> "on cylinder radius R" assertion FAILS.
int runPolarTransformPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128;
  const float R = 2.0f;
  const float PI = 3.14159265358979323846f;
  const float TZ = injectBug ? 0.0f : R;   // lift to z=R (bug: leave at z=0 -> warp collapses)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-polartransform] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerPolarTransformPointsOp();
  std::vector<SwPoint> captured;
  g_capPolar = &captured;
  registerDrawOp("DrawPoints", captureDrawPolar);

  Graph g;
  Node gen; gen.id = 1; gen.type = "LinePoints";
  gen.params["Count"] = (float)N;
  gen.params["Length"] = 2.0f * PI;      // Pivot=0.5 default -> x spread -pi..+pi about origin
  gen.params["Direction.x"] = 1.0f; gen.params["Direction.y"] = 0.0f; gen.params["Direction.z"] = 0.0f;
  g.nodes.push_back(gen);

  Node pt; pt.id = 2; pt.type = "PolarTransformPoints";
  pt.params["Mode"] = 0.0f;  // CartesianToCylindrical
  pt.params["Translation.x"] = 0.0f; pt.params["Translation.y"] = 0.0f; pt.params["Translation.z"] = TZ;
  pt.params["Scale.x"] = 1.0f; pt.params["Scale.y"] = 1.0f; pt.params["Scale.z"] = 1.0f;
  pt.params["UniformScale"] = 1.0f;
  g.nodes.push_back(pt);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk = (captured.size() == N);
  float maxRadErr = 0.0f;
  float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
  for (const SwPoint& p : captured) {
    float radial = std::sqrt(p.Position.x * p.Position.x + p.Position.z * p.Position.z);
    float e = std::fabs(radial - R);
    if (e > maxRadErr) maxRadErr = e;
    if (p.Position.x < minX) minX = p.Position.x;
    if (p.Position.x > maxX) maxX = p.Position.x;
    if (p.Position.z < minZ) minZ = p.Position.z;
    if (p.Position.z > maxZ) maxZ = p.Position.z;
  }
  bool onCylinder = countOk && !captured.empty() && (maxRadErr < 1e-3f);
  // angle sweep: x spans -pi..pi -> sin crosses 0 both ways (x neg & pos), cos hits +R and dips
  bool swept = (maxX > 0.5f) && (minX < -0.5f) && (maxZ > 0.5f) && (minZ < R - 0.5f);

  bool pass = countOk && onCylinder && swept;
  printf("[selftest-polartransform] n=%zu maxRadErr=%.5f(need<1e-3 of R=%.1f) "
         "x∈[%.2f,%.2f] z∈[%.2f,%.2f] swept=%s -> %s\n",
         captured.size(), maxRadErr, R, minX, maxX, minZ, maxZ, swept ? "yes" : "NO",
         pass ? "PASS" : "FAIL");

  g_capPolar = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// ============================================================================
// refuter-P GPU adversarial probe (batch 16, permanent bite tooth): falsifies
// the承重斷言 that the in-shader TRS Euler product matches TiXL host
// TransformMatrix (CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)). Runs the REAL
// kernel with Rotation!=0 + non-uniform Scale + Translation!=0, captures GPU
// positions, and compares each against the TiXL C++ reference path. Prints
// maxPosErr. injectBug=true swaps yaw/roll in the C++ reference (Z·Y·X) to mismatch
// the correct Y·X·Z shader -> FAIL (bite). RED face: Z·Y·X (old) -> maxPosErr >> 1e-3.
// GREEN face: Y·X·Z (fixed) -> maxPosErr < 1e-3 for both Mode=0 and Mode=1.
// ============================================================================
namespace {

struct PV3 { double x, y, z; };
struct PV4 { double x, y, z, w; };
struct PM4 { double m[4][4]; };

// .NET Quaternion.CreateFromYawPitchRoll (verbatim from dotnet/runtime Quaternion.cs)
static PV4 pCfypr(double yaw, double pitch, double roll) {
  double sr = std::sin(roll * 0.5),  cr = std::cos(roll * 0.5);
  double sp = std::sin(pitch * 0.5), cp = std::cos(pitch * 0.5);
  double sy = std::sin(yaw * 0.5),   cy = std::cos(yaw * 0.5);
  return { cy*sp*cr + sy*cp*sr,
           sy*cp*cr - cy*sp*sr,
           cy*cp*sr - sy*sp*cr,
           cy*cp*cr + sy*sp*sr };
}
// .NET Matrix4x4.CreateFromQuaternion (row-major, row-vector convention)
static PM4 pCfq(PV4 q) {
  double xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z, xy=q.x*q.y, wz=q.z*q.w,
         xz=q.z*q.x, wy=q.y*q.w, yz=q.y*q.z, wx=q.x*q.w;
  PM4 r{};
  r.m[0][0]=1-2*(yy+zz); r.m[0][1]=2*(xy+wz);   r.m[0][2]=2*(xz-wy);
  r.m[1][0]=2*(xy-wz);   r.m[1][1]=1-2*(zz+xx); r.m[1][2]=2*(yz+wx);
  r.m[2][0]=2*(xz+wy);   r.m[2][1]=2*(yz-wx);   r.m[2][2]=1-2*(yy+xx);
  r.m[3][3]=1;
  return r;
}
static PM4 pScale(double sx, double sy, double sz) {
  PM4 r{}; r.m[0][0]=sx; r.m[1][1]=sy; r.m[2][2]=sz; r.m[3][3]=1; return r;
}
static PM4 pTrans(double tx, double ty, double tz) {
  PM4 r{}; for(int i=0;i<4;i++) r.m[i][i]=1;
  r.m[3][0]=tx; r.m[3][1]=ty; r.m[3][2]=tz; return r;
}
static PM4 pMatMul(const PM4& a, const PM4& b) {
  PM4 r{};
  for (int i=0;i<4;i++) for (int j=0;j<4;j++) {
    double s=0; for(int k=0;k<4;k++) s+=a.m[i][k]*b.m[k][j]; r.m[i][j]=s;
  }
  return r;
}
// row-vector (v,1)*M, drop w  (== HLSL mul(float4(pos,1), M))
static PV3 pXform(PV3 v, const PM4& m) {
  return { v.x*m.m[0][0]+v.y*m.m[1][0]+v.z*m.m[2][0]+m.m[3][0],
           v.x*m.m[0][1]+v.y*m.m[1][1]+v.z*m.m[2][1]+m.m[3][1],
           v.x*m.m[0][2]+v.y*m.m[1][2]+v.z*m.m[2][2]+m.m[3][2] };
}
static PV3 pPolarWarp(PV3 pos, int mode) {
  if (mode > 0) pos = { pos.x, pos.z*std::sin(pos.y), pos.z*std::cos(pos.y) };
  pos = { pos.z*std::sin(pos.x), pos.y, pos.z*std::cos(pos.x) };
  return pos;
}

}  // namespace (probe helpers)

int runPolarTransformPointsParityProbe(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float PI = 3.14159265358979323846f;
  // Adversarial: yaw30/pitch45/roll60, non-uniform Scale(2,0.5,1), Translation(0.3,-0.2,0.7).
  const float ROT[3] = {45.0f, 30.0f, 60.0f};   // RotationX=pitch, RotationY=yaw, RotationZ=roll
  const float SCL[3] = {2.0f, 0.5f, 1.0f};
  const float TRN[3] = {0.3f, -0.2f, 0.7f};
  // 3D line direction so generated points vary in x,y,z (all 3 Euler axes get exercised).
  const float DIR[3] = {1.0f, 0.7f, -0.4f};
  const float LEN = 2.0f * PI;
  const float PIVOT = 0.5f;  // LinePoints default

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[polarprobe] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerPolarTransformPointsOp();
  std::vector<SwPoint> captured;
  g_capPolar = &captured;
  registerDrawOp("DrawPoints", captureDrawPolar);

  int worstMode = -1; float worstErr = 0.0f;
  for (int mode = 0; mode <= 1; ++mode) {
    captured.clear();
    Graph g;
    Node gen; gen.id = 1; gen.type = "LinePoints";
    gen.params["Count"] = (float)N;
    gen.params["Length"] = LEN;
    gen.params["Pivot"] = PIVOT;
    gen.params["Direction.x"] = DIR[0]; gen.params["Direction.y"] = DIR[1];
    gen.params["Direction.z"] = DIR[2];
    g.nodes.push_back(gen);

    Node pt; pt.id = 2; pt.type = "PolarTransformPoints";
    pt.params["Mode"] = (float)mode;
    pt.params["Translation.x"] = TRN[0]; pt.params["Translation.y"] = TRN[1];
    pt.params["Translation.z"] = TRN[2];
    pt.params["Rotation.x"] = ROT[0]; pt.params["Rotation.y"] = ROT[1];
    pt.params["Rotation.z"] = ROT[2];
    pt.params["Scale.x"] = SCL[0]; pt.params["Scale.y"] = SCL[1];
    pt.params["Scale.z"] = SCL[2];
    pt.params["UniformScale"] = 1.0f;
    g.nodes.push_back(pt);

    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

    PointGraph pg(dev, lib, q, 64, 64);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f;
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

    // TiXL host-matrix reference:
    //   M = Scale * CreateFromQuaternion(CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)) * Translation
    // injectBug: simulate the old Z·Y·X shader bug by swapping yaw/roll in the reference
    //   (reference uses Z as yaw, X as roll -> mismatches the correct Y·X·Z shader -> FAIL).
    const double DEG = M_PI / 180.0;
    PV4 Rt = injectBug
      ? pCfypr(ROT[2]*DEG, ROT[0]*DEG, ROT[1]*DEG)   // BUGGY Z·Y·X reference (yaw=Z, roll=Y)
      : pCfypr(ROT[1]*DEG, ROT[0]*DEG, ROT[2]*DEG);  // correct: yaw=Y, pitch=X, roll=Z
    PM4 M = pMatMul(pMatMul(pScale(SCL[0], SCL[1], SCL[2]), pCfq(Rt)),
                   pTrans(TRN[0], TRN[1], TRN[2]));

    float maxErr = 0.0f; int worstI = -1;
    PV3 worstExp{}, worstGot{};
    for (uint32_t i = 0; i < captured.size(); ++i) {
      // reconstruct LinePoints input position (Pivot=0.5 default)
      int steps = (int)N - 1;
      double tparam = steps > 0 ? (double)i / (double)steps : 0.5;
      double f = tparam - PIVOT;
      PV3 in = { DIR[0]*LEN*f, DIR[1]*LEN*f, DIR[2]*LEN*f };
      PV3 exp = pPolarWarp(pXform(in, M), mode);
      const SwPoint& gp = captured[i];
      PV3 got = { gp.Position.x, gp.Position.y, gp.Position.z };
      double e = std::sqrt((exp.x-got.x)*(exp.x-got.x) +
                           (exp.y-got.y)*(exp.y-got.y) +
                           (exp.z-got.z)*(exp.z-got.z));
      if (e > maxErr) {
        maxErr = (float)e; worstI = (int)i;
        worstExp = exp; worstGot = got;
      }
    }
    printf("[polarprobe] Mode=%d n=%zu maxPosErr=%.5f (need<1e-3) worstI=%d "
           "TiXL_expect=(%.4f,%.4f,%.4f) GPU_got=(%.4f,%.4f,%.4f)\n",
           mode, captured.size(), maxErr, worstI,
           worstExp.x, worstExp.y, worstExp.z, worstGot.x, worstGot.y, worstGot.z);
    if (maxErr > worstErr) { worstErr = maxErr; worstMode = mode; }
  }

  bool pass = (worstErr < 1e-3f);
  printf("[polarprobe] params: Rot(deg)=(%.0f,%.0f,%.0f) Scale=(%.2f,%.2f,%.2f)"
         " Trans=(%.2f,%.2f,%.2f)\n",
         ROT[0], ROT[1], ROT[2], SCL[0], SCL[1], SCL[2],
         TRN[0], TRN[1], TRN[2]);
  printf("[polarprobe] WORST maxPosErr=%.5f @Mode=%d -> %s "
         "(PASS=shader matches TiXL host matrix; FAIL=承重斷言 BROKEN)\n",
         worstErr, worstMode, pass ? "PASS" : "FAIL");

  g_capPolar = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
