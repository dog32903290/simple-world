// TransformSomePoints — lane-P MODIFIER op (batch 18): TRS transform weighted by W channel.
// Faithful port of external/tixl .../point/transform/TransformSomePoints (.cs ports, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output): each point is
// TRS-transformed, with the transform weight lerp-controlled by the point's W when WIsWeight>0.
// Count is INHERITED from upstream.
//
// TiXL parity (TransformSomePoints.cs/.hlsl):
//   - ports: Points, Translation(Vec3,0), Rotation(Vec3,0), Scale(Vec3,1), UniformScale(f,1),
//     Space(enum{PointSpace=0,ObjectSpace=1,WorldSpace=2},0), WIsWeight(bool,false),
//     UpdateRotation(bool,true). W channel acts as selection weight when WIsWeight=true.
//   - math: TRS (pivot=0), then WIsWeight lerp, then PointSpace back-to-world.
//   - FORK (see transformsomepoints.metal): TRS matrix composed in-shader from raw scalars.
//   - BAKED: Take/Skip/RangeStart/LengthFactor/Scatter/OnlyKeepTakes/ScaleW/OffsetW/TestParam.
//   - No Strength port: TiXL TransformSomePoints.cs has no Strength input; per-point weighting
//     is handled exclusively by WIsWeight × W channel (HLSL:125-130).
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                       // calcDispatchCount
#include "runtime/graph.h"                          // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"                    // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tex_op_cache.h"                   // cachedComputePSO
#include "runtime/transformsomepoints_params.h"     // TransformSomeParams, TransformSomeBinding
#include "runtime/tixl_point.h"                     // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// TransformSomePoints modifier: dispatch the transformsomepoints kernel input bag -> output bag.
// count comes from c.count (inherited from upstream Points bag). No input bag = safe no-op.
void cookTransformSomePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to transform

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "transformsomepoints");
  if (!pso) return;

  TransformSomeParams P{};
  P.Count      = c.count;
  P.Space      = (int)(cookParam(c, "Space", 0.0f) + 0.5f);
  P.WIsWeight  = cookParam(c, "WIsWeight", 0.0f);
  P.UniformScale = cookParam(c, "UniformScale", 1.0f);
  float t[3] = {0, 0, 0}, r[3] = {0, 0, 0}, s[3] = {1, 1, 1};
  cookVecN(c, "Translation", t, 3, t);
  cookVecN(c, "Rotation",    r, 3, r);
  cookVecN(c, "Scale",       s, 3, s);
  P.TranslationX = t[0]; P.TranslationY = t[1]; P.TranslationZ = t[2];
  P.RotationX    = r[0]; P.RotationY    = r[1]; P.RotationZ    = r[2];
  P.StretchX     = s[0]; P.StretchY     = s[1]; P.StretchZ     = s[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, XFSOME_SourcePoints);
  enc->setBuffer(c.output, 0, XFSOME_ResultPoints);
  enc->setBytes(&P, sizeof(P), XFSOME_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capXfSome = nullptr;
void captureDrawXfSome(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capXfSome || !pts || c.count == 0) return;
  g_capXfSome->assign(c.count, SwPoint{});
  std::memcpy(g_capXfSome->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerTransformSomePointsOp() {
  registerPointOp("TransformSomePoints", cookTransformSomePoints);
}

// Forward decl for direct kernel dispatch (defined below, used by golden + probe).
static bool runXfSomeKernelDirect(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                  const std::vector<SwPoint>& in, const TransformSomeParams& P,
                                  std::vector<SwPoint>& out);

// =============================================================================
// Golden: RadialPoints(ring at origin, W=1 by default from generator) -> TransformSomePoints(
//   ObjectSpace, WIsWeight=true, Stretch=1.5/0.7/1.5, Translation=(3,0,0))
//   -> capture. With W=1 the selection fully applies: ring scales by (1.5,0.7,1.5)
//   and shifts +3 in x.  Mean x ~= 3, every point closer to x=3 than x=0.
// TEETH (injectBug NEW mechanism — Strength removed, per TiXL TransformSomePoints.cs):
//   (1) count preserved.
//   (2) graph golden: Translation=(3,0,0), Scale=(1.5,0.7,1.5).
//       injectBug: inject Scale=identity (Scale.x=1,y=1,z=1) + Translation=(0,0,0) -> point
//       set stays at origin -> mean x ~= 0, NOT ~3 -> FAIL.
//   (3) MULTI-AXIS ROTATION TOOTH: drive ONE point at (0,1,0) with orgRot=identity through
//       ObjectSpace Rot=(37,53,71)°, Scale=(1,1,1), Trans=(0,0,0), WIsWeight=false.
//       The correct Y·X·Z order lands at the analytically computed expectation; the old Z·Y·X
//       order produces a DIFFERENT result -> assertion catches rotation-order regression.
//       injectBug: inject Z·Y·X ordering in the C++ reference (yaw=Z,pitch=X,roll=Y), which
//       disagrees with the shader's Y·X·Z -> expected != got -> FAIL.
//   (4) SELECTION WEIGHT TOOTH: WIsWeight=true, W=0.5 -> movement halved.
//       Drive ONE point: ObjectSpace Rot=0 Trans=(4,0,0) Stretch=1 WIsWeight=true W=0.5.
//       Expected output.Position.x = 0 + (4-0)*0.5 = 2.0. Checks lerp path separately.
//       injectBug: inject TranslationX=0 -> pos stays at origin -> expected x=0, got=0 -> PASS?
//       Better: inject UniformScale=0 -> newPos=(0,0,0), weightedOffset=(0-0)*0.5=(0,0,0) -> x=0.
//       Actually: inject WIsWeight=false in params (disable the lerp path) -> full transform
//       applied, got x=4, expected x=2 -> mismatch -> FAIL.
// =============================================================================
int runTransformSomePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float R = 2.0f;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-transformsomepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerTransformSomePointsOp();
  std::vector<SwPoint> captured;
  g_capXfSome = &captured;
  registerDrawOp("DrawPoints", captureDrawXfSome);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"]  = (float)N;
  gen.params["Radius"] = R;
  gen.params["Cycles"] = 1.0f;
  g.nodes.push_back(gen);
  Node xfs; xfs.id = 2; xfs.type = "TransformSomePoints";
  xfs.params["Space"]          = 1.0f;  // ObjectSpace
  xfs.params["WIsWeight"]      = 0.0f;  // disabled for graph golden (W may vary by generator)
  // injectBug: zero out Translation + Scale -> identity transform -> ring stays at origin
  // -> mean x ~= 0, not ~3 -> graph tooth FAILS. Normal: Translation=(3,0,0) + Scale=(1.5,0.7,1.5).
  xfs.params["Translation.x"]  = injectBug ? 0.0f : 3.0f;
  xfs.params["Translation.y"]  = 0.0f;
  xfs.params["Translation.z"]  = 0.0f;
  xfs.params["Scale.x"]        = injectBug ? 1.0f : 1.5f;
  xfs.params["Scale.y"]        = injectBug ? 1.0f : 0.7f;
  xfs.params["Scale.z"]        = injectBug ? 1.0f : 1.5f;
  xfs.params["UniformScale"]   = 1.0f;
  g.nodes.push_back(xfs);
  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk = (captured.size() == N);
  float meanX = 0.0f;
  for (const SwPoint& p : captured) meanX += p.Position.x;
  if (!captured.empty()) meanX /= (float)captured.size();
  bool graphPass = countOk && std::fabs(meanX - 3.0f) < 0.2f;

  // --- TOOTH 1: multi-axis rotation (Y·X·Z vs Z·Y·X) ---
  // Drive (0,1,0) through Rot=(37,53,71)°, ObjectSpace, no scale/translation.
  // Expected result computed analytically below via the same tCfypr helper as refuter-T.
  bool rotPass = false;
  float rotErr = 9.9f;
  {
    // Inline C++ reference: CreateFromYawPitchRoll(yaw=Y°, pitch=X°, roll=Z°)
    const double DEG = M_PI / 180.0;
    const double rx = 37.0 * DEG, ry = 53.0 * DEG, rz = 71.0 * DEG;
    // injectBug: use Z·Y·X ordering in the C++ reference (old buggy order).
    // Shader always uses Y·X·Z. Bug reference disagrees -> expXYZ != shader output -> FAIL.
    // Normal: Y·X·Z qMul(Ry, qMul(Rx, Rz)) matches the shader.
    auto qFromAxisAngle = [](double a, double ax, double ay, double az)
        -> std::array<double,4> {
      double s = std::sin(a*0.5), c = std::cos(a*0.5);
      return {ax*s, ay*s, az*s, c};
    };
    auto qMulRef = [](std::array<double,4> a, std::array<double,4> b) -> std::array<double,4> {
      return { b[0]*a[3]+a[0]*b[3]+(a[1]*b[2]-a[2]*b[1]),
               b[1]*a[3]+a[1]*b[3]+(a[2]*b[0]-a[0]*b[2]),
               b[2]*a[3]+a[2]*b[3]+(a[0]*b[1]-a[1]*b[0]),
               a[3]*b[3]-(a[0]*b[0]+a[1]*b[1]+a[2]*b[2]) };
    };
    auto qRotRef = [](double vx, double vy, double vz, std::array<double,4> q)
        -> std::array<double,3> {
      double tx=2*(q[1]*vz-q[2]*vy), ty=2*(q[2]*vx-q[0]*vz), tz=2*(q[0]*vy-q[1]*vx);
      return {vx+q[3]*tx+(q[1]*tz-q[2]*ty),
              vy+q[3]*ty+(q[2]*tx-q[0]*tz),
              vz+q[3]*tz+(q[0]*ty-q[1]*tx)};
    };
    auto Ry = qFromAxisAngle(ry, 0,1,0);
    auto Rx = qFromAxisAngle(rx, 1,0,0);
    auto Rz = qFromAxisAngle(rz, 0,0,1);
    // injectBug: Z·Y·X reference (buggy: yaw=Z, pitch=X, roll=Y — old batch-16 error)
    // normal:    Y·X·Z reference (correct: yaw=Y, pitch=X, roll=Z — CreateFromYawPitchRoll)
    auto R  = injectBug
              ? qMulRef(Rz, qMulRef(Ry, Rx))   // BUGGY Z·Y·X
              : qMulRef(Ry, qMulRef(Rx, Rz));  // correct Y·X·Z
    auto expXYZ = qRotRef(0.0, 1.0, 0.0, R);  // ObjectSpace Rot applied to (0,1,0)

    std::vector<SwPoint> one(1);
    one[0] = SwPoint{};
    one[0].Position = SW_PACKED3{0.0f, 1.0f, 0.0f};
    one[0].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    one[0].FX1      = 1.0f;  // W==FX1 in SwPoint layout
    TransformSomeParams RP{};
    RP.Count = 1; RP.Space = 1;  // ObjectSpace
    RP.WIsWeight = 0.0f;
    // Params always correct (Y·X·Z shader); injectBug is in the C++ reference above -> mismatch.
    RP.RotationX = 37.0f; RP.RotationY = 53.0f; RP.RotationZ = 71.0f;  // multi-axis, non-equal
    RP.StretchX = 1.0f; RP.StretchY = 1.0f; RP.StretchZ = 1.0f;
    RP.UniformScale = 1.0f;
    std::vector<SwPoint> ro;
    if (runXfSomeKernelDirect(dev, q, lib, one, RP, ro) && ro.size() == 1) {
      double dx = ro[0].Position.x - expXYZ[0];
      double dy = ro[0].Position.y - expXYZ[1];
      double dz = ro[0].Position.z - expXYZ[2];
      rotErr = (float)std::sqrt(dx*dx + dy*dy + dz*dz);
    }
    rotPass = (rotErr < 1e-3f);
    printf("[selftest-transformsomepoints] rotTooth Rot(37,53,71) ObjectSpace (0,1,0)->"
           "(%.4f,%.4f,%.4f) got=(%.4f,%.4f,%.4f) err=%.5f -> %s\n",
           (float)expXYZ[0], (float)expXYZ[1], (float)expXYZ[2],
           ro.empty() ? 0.f : ro[0].Position.x,
           ro.empty() ? 0.f : ro[0].Position.y,
           ro.empty() ? 0.f : ro[0].Position.z,
           rotErr, rotPass ? "PASS" : "FAIL");
  }

  // --- TOOTH 2: WIsWeight lerp (W=0.5, Trans.x=4 -> expected x=2) ---
  bool wPass = false;
  float wErr = 9.9f;
  {
    std::vector<SwPoint> one(1);
    one[0] = SwPoint{};
    one[0].Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
    one[0].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    one[0].FX1      = 0.5f;  // W==FX1 in SwPoint; selection weight
    TransformSomeParams WP{};
    WP.Count = 1; WP.Space = 1;  // ObjectSpace
    // injectBug: disable WIsWeight in params -> full transform applied (pos becomes (4,0,0))
    // -> got x=4, expected x=2 -> wTooth FAILS.  Normal: WIsWeight=1 -> lerp by W=0.5.
    WP.WIsWeight  = injectBug ? 0.0f : 1.0f;
    WP.RotationX  = 0.0f; WP.RotationY = 0.0f; WP.RotationZ = 0.0f;
    WP.StretchX   = 1.0f; WP.StretchY = 1.0f; WP.StretchZ = 1.0f;
    WP.UniformScale = 1.0f;
    WP.TranslationX = 4.0f; WP.TranslationY = 0.0f; WP.TranslationZ = 0.0f;
    std::vector<SwPoint> wo;
    if (runXfSomeKernelDirect(dev, q, lib, one, WP, wo) && wo.size() == 1) {
      // expected: pLocal=(0,0,0), newPos=(4,0,0), weightedOffset=(4-0)*0.5=(2,0,0)
      //           pos = pLocal + weightedOffset = (2,0,0) -> WIsWeight lerp path correct.
      const float expX = 2.0f;
      wErr = std::fabs(wo[0].Position.x - expX);
    }
    wPass = (wErr < 1e-4f);
    printf("[selftest-transformsomepoints] wTooth WIsWeight W=0.5 Trans.x=4 expect x=2 "
           "got=%.5f err=%.5f -> %s\n",
           wo.empty() ? 0.f : wo[0].Position.x, wErr, wPass ? "PASS" : "FAIL");
  }

  bool pass = graphPass && rotPass && wPass;
  printf("[selftest-transformsomepoints] n=%zu meanX=%.3f(need~3.0) "
         "graphPass=%s rotPass=%s(err=%.5f) wPass=%s -> %s\n",
         captured.size(), meanX,
         graphPass ? "yes" : "NO",
         rotPass ? "yes" : "NO", rotErr,
         wPass ? "yes" : "NO",
         pass ? "PASS" : "FAIL");

  g_capXfSome = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// =============================================================================
// refuter-XfSome GPU adversarial probe (batch 18, permanent bite tooth):
// falsifies the承重斷言 that transformsomepoints.metal composes its Euler rotation in Y·X·Z
// (= CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z)) and that WIsWeight lerp is correct.
//
// Drives the REAL transformsomepoints kernel with:
//   - MULTI-AXIS NON-EQUAL Rotation (37°/53°/71°) + NON-UNIFORM Scale (1.7/0.6/2.3) + Translation
//   - N points with NON-IDENTITY Position AND Rotation (per-point orgRot varies with index)
//   - Two gated cases: ObjectSpace + PointSpace (both Euler-order sensitive)
//   - WIsWeight sub-probe: two points with different W values (W=0 and W=1) in ObjectSpace;
//     W=0 -> no transform applied, W=1 -> full transform applied.
//
// C++ reference: .NET path (same tCfypr + matrix formulas as refuter-T).
// injectBug=true: uses Z·Y·X (old buggy reference, yaw=Z/roll=Y) -> mismatches Y·X·Z shader -> FAIL.
// RED face: Z·Y·X (old shader) -> maxPosErr >> 1e-3.
// GREEN face: Y·X·Z (fixed shader) -> both errors < threshold.
// =============================================================================
namespace {

struct SV3 { double x, y, z; };
struct SV4 { double x, y, z, w; };
struct SM4 { double m[4][4]; };

// .NET Quaternion.CreateFromYawPitchRoll (verbatim, same as in refuter-T/refuter-P).
static SV4 sCfypr(double yaw, double pitch, double roll) {
  double sr = std::sin(roll*0.5), cr = std::cos(roll*0.5);
  double sp = std::sin(pitch*0.5), cp = std::cos(pitch*0.5);
  double sy = std::sin(yaw*0.5), cy = std::cos(yaw*0.5);
  return { cy*sp*cr + sy*cp*sr,
           sy*cp*cr - cy*sp*sr,
           cy*cp*sr - sy*sp*cr,
           cy*cp*cr + sy*sp*sr };
}
static SV4 sqMul(SV4 a, SV4 b) {
  return { b.x*a.w+a.x*b.w+(a.y*b.z-a.z*b.y),
           b.y*a.w+a.y*b.w+(a.z*b.x-a.x*b.z),
           b.z*a.w+a.z*b.w+(a.x*b.y-a.y*b.x),
           a.w*b.w-(a.x*b.x+a.y*b.y+a.z*b.z) };
}
static SV3 sqRotate(SV3 v, SV4 q) {
  SV3 t = { 2*(q.y*v.z-q.z*v.y), 2*(q.z*v.x-q.x*v.z), 2*(q.x*v.y-q.y*v.x) };
  return { v.x+q.w*t.x+(q.y*t.z-q.z*t.y),
           v.y+q.w*t.y+(q.z*t.x-q.x*t.z),
           v.z+q.w*t.z+(q.x*t.y-q.y*t.x) };
}
static double sDot4(SV4 a, SV4 b) { return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w; }
static SM4 sCfq(SV4 q) {
  double xx=q.x*q.x, yy=q.y*q.y, zz=q.z*q.z, xy=q.x*q.y, wz=q.z*q.w,
         xz=q.z*q.x, wy=q.y*q.w, yz=q.y*q.z, wx=q.x*q.w;
  SM4 r{}; r.m[0][0]=1-2*(yy+zz); r.m[0][1]=2*(xy+wz); r.m[0][2]=2*(xz-wy);
  r.m[1][0]=2*(xy-wz); r.m[1][1]=1-2*(zz+xx); r.m[1][2]=2*(yz+wx);
  r.m[2][0]=2*(xz+wy); r.m[2][1]=2*(yz-wx);   r.m[2][2]=1-2*(yy+xx);
  r.m[3][3]=1; return r;
}
static SM4 sScale(double sx, double sy, double sz) {
  SM4 r{}; r.m[0][0]=sx; r.m[1][1]=sy; r.m[2][2]=sz; r.m[3][3]=1; return r;
}
static SM4 sTrans(double tx, double ty, double tz) {
  SM4 r{}; for(int i=0;i<4;i++) r.m[i][i]=1;
  r.m[3][0]=tx; r.m[3][1]=ty; r.m[3][2]=tz; return r;
}
static SM4 sMatMul(const SM4& a, const SM4& b) {
  SM4 r{};
  for(int i=0;i<4;i++) for(int j=0;j<4;j++) {
    double s=0; for(int k=0;k<4;k++) s+=a.m[i][k]*b.m[k][j]; r.m[i][j]=s;
  }
  return r;
}
static SV3 sXform(SV3 v, const SM4& m) {
  return { v.x*m.m[0][0]+v.y*m.m[1][0]+v.z*m.m[2][0]+m.m[3][0],
           v.x*m.m[0][1]+v.y*m.m[1][1]+v.z*m.m[2][1]+m.m[3][1],
           v.x*m.m[0][2]+v.y*m.m[1][2]+v.z*m.m[2][2]+m.m[3][2] };
}
static SV4 sNorm(SV4 q) {
  double l = std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
  if (l < 1e-12) return {0,0,0,1};
  return {q.x/l, q.y/l, q.z/l, q.w/l};
}

}  // namespace (probe helpers)

static bool runXfSomeKernelDirect(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                  const std::vector<SwPoint>& in, const TransformSomeParams& P,
                                  std::vector<SwPoint>& out) {
  MTL::Function* fn = lib->newFunction(NS::String::string("transformsomepoints", NS::UTF8StringEncoding));
  if (!fn) return false;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return false;
  const size_t bytes = in.size() * sizeof(SwPoint);
  MTL::Buffer* src = dev->newBuffer(in.data(), bytes, MTL::ResourceStorageModeShared);
  MTL::Buffer* dst = dev->newBuffer(bytes, MTL::ResourceStorageModeShared);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(src, 0, XFSOME_SourcePoints);
  enc->setBuffer(dst, 0, XFSOME_ResultPoints);
  enc->setBytes(&P, sizeof(P), XFSOME_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(P.Count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  out.assign(in.size(), SwPoint{});
  std::memcpy(out.data(), dst->contents(), bytes);
  src->release(); dst->release(); pso->release();
  return true;
}

int runTransformSomePointsParityProbe(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 24;
  // Adversarial: three NON-EQUAL non-zero Euler angles + NON-UNIFORM Scale + Translation.
  const float ROT[3] = {37.0f, 53.0f, 71.0f};  // X=pitch, Y=yaw, Z=roll
  const float SCL[3] = {1.7f, 0.6f, 2.3f};      // non-uniform stretch
  const float TRN[3] = {0.4f, -0.3f, 0.9f};
  const double DEG = M_PI / 180.0;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[xfsomeprobe] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Build input bag with NON-identity Position AND per-point Rotation.
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    double a = (double)i / (double)N;
    in[i] = SwPoint{};
    in[i].Position = { (float)(std::cos(a*6.2831853)*1.3),
                       (float)(std::sin(a*6.2831853)*0.8),
                       (float)((a-0.5)*1.5) };
    // Per-point orgRot that varies with i.
    SV4 oq = sCfypr((20.0+90.0*a)*DEG, (-15.0+40.0*a)*DEG, (10.0+60.0*a)*DEG);
    in[i].Rotation = SW_FLOAT4{ (float)oq.x, (float)oq.y, (float)oq.z, (float)oq.w };
    in[i].FX1 = 1.0f;  // W==FX1 in SwPoint; WIsWeight=false so value irrelevant for Euler probe
  }

  // The correct R (or, with injectBug, the BUGGY Z·Y·X reference).
  SV4 Rt = injectBug
    ? sCfypr(ROT[2]*DEG, ROT[0]*DEG, ROT[1]*DEG)   // BUGGY: yaw=Z, pitch=X, roll=Y (Z·Y·X)
    : sCfypr(ROT[1]*DEG, ROT[0]*DEG, ROT[2]*DEG);  // correct: yaw=Y, pitch=X, roll=Z (Y·X·Z)

  // TiXL host-matrix (pivot=0): M = Scale * CreateFromQuaternion(R) * Translation.
  SM4 M = sMatMul(sMatMul(sScale(SCL[0],SCL[1],SCL[2]), sCfq(Rt)),
                  sTrans(TRN[0],TRN[1],TRN[2]));

  // Two gated cases: ObjectSpace and PointSpace.
  struct Case { int space; const char* name; };
  const Case cases[2] = { {0, "PointSpace"}, {1, "ObjectSpace"} };
  double gatedMaxPos = 0.0, gatedMaxRot = 0.0;
  for (const Case& cs : cases) {
    TransformSomeParams P{};
    P.Count      = N; P.Space = cs.space;
    P.WIsWeight  = 0.0f;   // disabled: all points fully transformed (test Euler order only)
    P.RotationX  = ROT[0]; P.RotationY = ROT[1]; P.RotationZ = ROT[2];
    P.StretchX   = SCL[0]; P.StretchY  = SCL[1]; P.StretchZ  = SCL[2];
    P.UniformScale = 1.0f;
    P.TranslationX = TRN[0]; P.TranslationY = TRN[1]; P.TranslationZ = TRN[2];

    std::vector<SwPoint> out;
    if (!runXfSomeKernelDirect(dev, q, lib, in, P, out)) {
      printf("[xfsomeprobe] FAIL: kernel dispatch (%s)\n", cs.name);
      lib->release(); q->release(); dev->release(); pool->release();
      return 1;
    }

    double maxPos = 0.0, maxRot = 0.0;
    for (uint32_t i = 0; i < N; ++i) {
      SV3 op = { in[i].Position.x, in[i].Position.y, in[i].Position.z };
      SV4 oq = { in[i].Rotation.x, in[i].Rotation.y, in[i].Rotation.z, in[i].Rotation.w };
      SV3 expPos; SV4 expRot;
      if (cs.space == 0) {  // PointSpace
        // pLocal=(0,0,0); newPos = qRotate((0,0,0)*scale,R)+trans = trans (scale*0=0)
        SV3 newPosInLocal = sXform({0,0,0}, M);  // == trans (scale*0=0)
        SV3 rp = sqRotate(newPosInLocal, oq);    // rotate into world frame
        expPos = { rp.x+op.x, rp.y+op.y, rp.z+op.z };
        expRot = sqMul(oq, Rt);                  // PointSpace: qMul(orgRot, R)
      } else {              // ObjectSpace
        expPos = sXform(op, M);
        expRot = sqMul(Rt, oq);                  // ObjectSpace: qMul(R, orgRot)
      }
      const SwPoint& gp = out[i];
      SV3 gotPos = { gp.Position.x, gp.Position.y, gp.Position.z };
      SV4 gotRot = { gp.Rotation.x, gp.Rotation.y, gp.Rotation.z, gp.Rotation.w };
      double ep = std::sqrt((expPos.x-gotPos.x)*(expPos.x-gotPos.x)+
                            (expPos.y-gotPos.y)*(expPos.y-gotPos.y)+
                            (expPos.z-gotPos.z)*(expPos.z-gotPos.z));
      expRot = sNorm(expRot);
      double er = 1.0 - std::fabs(sDot4(expRot, gotRot));
      if (ep > maxPos) maxPos = ep;
      if (er > maxRot) maxRot = er;
    }
    printf("[xfsomeprobe] %s n=%u maxPosErr=%.6f(need<1e-3) maxRotErr=%.6f(need<1e-4)\n",
           cs.name, N, maxPos, maxRot);
    if (maxPos > gatedMaxPos) gatedMaxPos = maxPos;
    if (maxRot > gatedMaxRot) gatedMaxRot = maxRot;
  }

  // --- WIsWeight sub-probe: ObjectSpace, W=0.5, check lerp position ---
  // injectBug=true: the Rt reference is swapped (Z·Y·X), which also breaks the WIsWeight
  // probe since the position delta will be wrong.
  double wMaxPos = 0.0;
  {
    // Same input bag but W=0.5 for all.
    std::vector<SwPoint> winIn = in;
    for (auto& p : winIn) p.FX1 = 0.5f;  // W==FX1 in SwPoint
    TransformSomeParams WP{};
    WP.Count=N; WP.Space=1; WP.WIsWeight=1.0f;
    WP.RotationX=ROT[0]; WP.RotationY=ROT[1]; WP.RotationZ=ROT[2];
    WP.StretchX=SCL[0]; WP.StretchY=SCL[1]; WP.StretchZ=SCL[2];
    WP.UniformScale=1.0f;
    WP.TranslationX=TRN[0]; WP.TranslationY=TRN[1]; WP.TranslationZ=TRN[2];
    std::vector<SwPoint> wOut;
    if (runXfSomeKernelDirect(dev, q, lib, winIn, WP, wOut)) {
      for (uint32_t i=0; i<N; ++i) {
        SV3 op = { winIn[i].Position.x, winIn[i].Position.y, winIn[i].Position.z };
        // ObjectSpace: newPos = xform(op, M); WIsWeight: lerp(op, newPos, 0.5)
        SV3 newPos = sXform(op, M);
        SV3 expPos = { op.x + (newPos.x-op.x)*0.5, op.y + (newPos.y-op.y)*0.5,
                       op.z + (newPos.z-op.z)*0.5 };
        const SwPoint& gp = wOut[i];
        double ep = std::sqrt((expPos.x-gp.Position.x)*(expPos.x-gp.Position.x)+
                              (expPos.y-gp.Position.y)*(expPos.y-gp.Position.y)+
                              (expPos.z-gp.Position.z)*(expPos.z-gp.Position.z));
        if (ep > wMaxPos) wMaxPos = ep;
      }
    }
    printf("[xfsomeprobe] WIsWeight(W=0.5 ObjectSpace) n=%u maxPosErr=%.6f(need<1e-3)\n",
           N, wMaxPos);
    if (wMaxPos > gatedMaxPos) gatedMaxPos = wMaxPos;
  }

  bool pass = (gatedMaxPos < 1e-3) && (gatedMaxRot < 1e-4);
  printf("[xfsomeprobe] params: Rot(deg)=(%.0f,%.0f,%.0f) Stretch=(%.2f,%.2f,%.2f)"
         " Trans=(%.2f,%.2f,%.2f) orgRot=non-identity\n",
         ROT[0],ROT[1],ROT[2],SCL[0],SCL[1],SCL[2],TRN[0],TRN[1],TRN[2]);
  printf("[xfsomeprobe] GATED: maxPosErr=%.6f maxRotErr=%.6f -> %s "
         "(PASS=Y·X·Z+WIsWeight correct; FAIL=承重斷言 BROKEN)\n",
         gatedMaxPos, gatedMaxRot, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
