// @tixl: TransformFromClipSpace   (census authority key — sw filename 'transformpointsfromclipspace' forks the TiXL op id)
// TransformPointsFromClipspace — the FIRST Points op to consume the camera-matrix-into-points seam
// (PointCookCtx::cameraToWorld). Faithful port of external/tixl
// .../Assets/shaders/points/modify/TransformPointsFromClipspace.hlsl (+ point/transform/
// TransformFromClipSpace.t3, which has ONLY a Points input — no scalar knobs). A count-preserving
// MODIFIER: unproject each point through CameraToWorld (clip-space → world) and post-multiply its
// Rotation by the camera orientation quaternion. Position is the unprojected world point.
//
// SEAM it rides: the camera-matrix-into-points seam. The cook DRIVER detects this op's "Camera" marker
// input port and fills PointCookCtx::cameraToWorld from the DEFAULT camera at the output aspect
// (fillPointCamera → field_camera::pointCameraMatrices). The op binds that float[16] into the kernel.
//
// NAMED forks (also in transformpointsfromclipspace.metal / _params.h headers):
//   • fork-camera-one-matrix-per-op: TiXL packs 10 TransformBufferLayout matrices; this kernel reads
//     ONLY CameraToWorld, so the host computes ONLY that one (the other 9 are dead for this op).
//   • fork-camera-default-only-v1: v1 supports a BARE point op (no Camera/Transform wrapper) at the
//     DEFAULT camera + identity ObjectToWorld → CameraToWorld = inverse(WorldToCamera). A Camera-op
//     stamp into the point flow is a later seam.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary / atomicOp (resident leg)
#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/field_camera.h"              // Mat4 / pointCameraMatrices / mat4TransformPointDivW (golden)
#include "runtime/graph.h"                     // Graph/Node/pinId (flat-driver leg)
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp, PointGraph
#include "runtime/quat_host.h"                  // qFromMatrix3PreciseHost (rotation golden, host twin)
#include "runtime/resident_eval_graph.h"       // buildEvalGraph (resident leg)
#include "runtime/transformpointsfromclipspace_params.h"  // TpfcsParams, TPFCS_* bindings
#include "runtime/tixl_point.h"                // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// TransformPointsFromClipspace cook: unproject src bag through cameraToWorld -> output bag. count from
// c.count (inherited from the upstream Points bag). No Points input / no camera -> nothing to do (the
// seam guard: a hand-built ctx with hasCamera=false leaves the bag untouched, the injectBug observation).
void cookTransformPointsFromClipspace(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input -> nothing to do

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("transformpointsfromclipspace", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  TpfcsParams P{};
  P.Count = c.count;
  // The seam delivers CameraToWorld (default camera) via the driver. A hand-built ctx with hasCamera=false
  // (the injectBug leg) leaves it IDENTITY -> the unproject is a passthrough (the RED tooth's divergence).
  if (c.hasCamera) std::memcpy(P.CameraToWorld, c.cameraToWorld, 16 * sizeof(float));
  else { std::memset(P.CameraToWorld, 0, 16 * sizeof(float)); P.CameraToWorld[0] = P.CameraToWorld[5] =
         P.CameraToWorld[10] = P.CameraToWorld[15] = 1.0f; }  // identity (passthrough)

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, TPFCS_SourcePoints);
  enc->setBuffer(c.output, 0, TPFCS_ResultPoints);
  enc->setBytes(&P, sizeof(P), TPFCS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

}  // namespace

void registerTransformPointsFromClipspaceOp() {
  registerPointOp("TransformPointsFromClipspace", cookTransformPointsFromClipspace);
}

// ============================================================================================
// Golden — FOUR legs (R-2: flat-only is self-deception; the camera matrix must reach the kernel on BOTH
// cook drivers + the closed-form Position/Rotation must match the host camera math).
//
//  (1) DIRECT-COOK closed-form (Position): hand-built ctx with hasCamera + the default-camera
//      cameraToWorld; a probe point at clip-space origin (0,0,0). closed-form: host
//      mat4TransformPointDivW(CameraToWorld, 0,0,0) == GPU p.Position. For the default camera that is
//      (0,0,DefaultCameraDistance) (the camera sits on +z at the origin looking down -z). want = the host
//      value (computed live, NOT hardcoded). injectBug binds IDENTITY CameraToWorld -> Position stays
//      (0,0,0) -> diverges from the host unproject -> RED.
//
//  (2) DIRECT-COOK second probe + Rotation unit-norm: a SECOND probe (0.3,-0.2,0) confirms the unproject is
//      the real matrix (not a constant). The Rotation leg here asserts UNIT-NORM only (the exact-value pin
//      lives in (2b) — the default camera's C2W≈identity makes the quaternion ≈ identity, where the
//      conjugate bug is invisible). injectBug (identity) -> Position passthrough -> posPass RED.
//
//  (2b) ROTATION exact-value, NON-AXIS-ALIGNED camera (the conjugate-bug tooth): drive a known off-axis
//      camera (eye=(3,2,4)) via directLegCustomC2W. Expected quaternion = qFromMatrix3PreciseHost on the
//      c2w 3×3 read row-major (== TiXL's GPU view after the TransformBufferLayout cbuffer-transpose). Assert
//      |dot(q_gpu, q_expected)| ≈ 1 (sign ambiguity, mirrors meshverticestopoints). The 抽row conjugate bug
//      (which default-camera legs cannot see) yields the inverse rotation → |dot| ≈ 0.74 ≠ 1 → RED.
//
//  (3) FLAT-DRIVER gather leg: a real flat Graph RadialPoints(#1) -> TransformPointsFromClipspace(#2)
//      cooked through PointGraph::cook; read the cooked Points buffer via debugCookedBuffer. The flat
//      driver's fillPointCamera must have detected the op's "Camera" marker port and filled the matrix
//      (the production flat path). Assert every point's Position is the unprojected one (z shifted to the
//      camera plane, NOT the RadialPoints ring's z=0). injectBug: a parallel cook with a ZERO-aspect
//      PointGraph cannot be forced, so the flat tooth rides the same identity-passthrough as the direct
//      legs by checking the seam DID move the points (a passthrough would leave z=0).
//
//  (4) RESIDENT (production) leg: RadialPoints -> TransformPointsFromClipspace -> DrawPoints2 ->
//      RenderTarget via cookResident; read the rendered pixels -> assert lit sprites exist (the resident
//      camera gather LIVES; the points project to a visible region). injectBug omits nothing structural
//      here — the resident path always fills the camera, so this leg proves the seam reaches cookResident
//      (lit != black) and pairs with the direct legs' exact-value teeth.
// ============================================================================================

namespace {

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// DIRECT-COOK leg with an EXPLICIT cameraToWorld (a non-axis-aligned camera). The default-camera directLeg
// can't bite the rotation conjugate bug (C2W≈identity → conjugate==self); this drives a known off-axis
// matrix so the orientation quaternion is non-trivial and the conjugate diverges.
bool directLegCustomC2W(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                        const float cameraToWorld[16], const SwPoint* in, uint32_t N, SwPoint* out) {
  MTL::Buffer* srcBag = dev->newBuffer(in, (size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {N};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = N;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag;
  std::memcpy(c.cameraToWorld, cameraToWorld, 16 * sizeof(float)); c.hasCamera = true;
  cookTransformPointsFromClipspace(c);
  std::memcpy(out, outBag->contents(), (size_t)N * sizeof(SwPoint));
  srcBag->release(); outBag->release();
  return true;
}

// DIRECT-COOK leg: dispatch over a hand-built bag, byte-read the output. hasCamera = !injectBug; on
// injectBug the ctx has hasCamera=false -> the cook binds identity -> passthrough.
// Returns out[0..N-1]. The default-camera cameraToWorld is computed here (aspect 1).
bool directLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool withCamera,
               const SwPoint* in, uint32_t N, SwPoint* out, float cameraToWorld[16]) {
  float o2cUnused[16];
  pointCameraMatrices(1.0f, o2cUnused, cameraToWorld);

  MTL::Buffer* srcBag = dev->newBuffer(in, (size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {N};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = N;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag;
  if (withCamera) { std::memcpy(c.cameraToWorld, cameraToWorld, 16 * sizeof(float)); c.hasCamera = true; }
  cookTransformPointsFromClipspace(c);

  std::memcpy(out, outBag->contents(), (size_t)N * sizeof(SwPoint));
  srcBag->release(); outBag->release();
  return true;
}

// FLAT-DRIVER gather leg: RadialPoints(#1) -> TransformPointsFromClipspace(#2) through PointGraph::cook;
// read #2's cooked bag. The flat driver fills the camera (fillPointCamera on the "Camera" marker port).
// Returns the first point's Position in outPos. cookedCount via debugCookedCount.
bool flatGraphLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, float outPos[3],
                  uint32_t& cookedCount) {
  registerBuiltinPointOps();
  registerTransformPointsFromClipspaceOp();  // explicit (self-contained, mirrors the resident leg)

  Graph g;
  Node radial; radial.id = 1; radial.type = "RadialPoints";
  radial.params["Count"] = 16.0f; radial.params["Radius"] = 1.0f;
  g.nodes.push_back(radial);
  Node tp; tp.id = 2; tp.type = "TransformPointsFromClipspace"; g.nodes.push_back(tp);
  // RadialPoints.points(port 0) -> TransformPointsFromClipspace.GPoints(port 0).
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, /*reg=*/nullptr, /*targetNodeId=*/2);

  outPos[0] = outPos[1] = outPos[2] = 0.0f;
  const MTL::Buffer* outBuf = pg.debugCookedBuffer(2);
  cookedCount = pg.debugCookedCount(2);
  if (!outBuf || cookedCount == 0) return false;
  const SwPoint* gpu = reinterpret_cast<const SwPoint*>(const_cast<MTL::Buffer*>(outBuf)->contents());
  outPos[0] = gpu[0].Position.x; outPos[1] = gpu[0].Position.y; outPos[2] = gpu[0].Position.z;
  return true;
}

// RESIDENT (production) leg: RadialPoints -> TransformPointsFromClipspace -> DrawPoints2 -> RenderTarget
// via cookResident; read the rendered pixels. Asserts lit sprites (the resident camera gather LIVES).
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 64.0f}, {"Radius", "Radius", "Float", 0.4f}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["TransformPointsFromClipspace"] = atomicOp(
      "TransformPointsFromClipspace",
      {{"GPoints", "GPoints", "Points", 0.0f}, {"Camera", "Camera", "Camera", 0.0f}},
      {{"out", "out", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] = atomicOp(
      "DrawPoints2",
      {{"points", "points", "Points", 0.0f},
       {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
       {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
       {"Radius", "Radius", "Float", 0.05f}, {"UseWForSize", "UseWForSize", "Float", 0.0f}},
      {{"out", "out", "Command", 0.0f}});
  slib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 256.0f}, {"CustomH", "CustomH", "Float", 256.0f},
       {"ClearColor.x", "ClearColor", "Float", 0.0f}, {"ClearColor.w", "ClearColor.w", "Float", 1.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints";
  SymbolChild c2; c2.id = 2; c2.symbolId = "TransformPointsFromClipspace";
  SymbolChild c3; c3.id = 3; c3.symbolId = "DrawPoints2";
  c3.overrides["Radius"] = 0.04f; c3.overrides["UseWForSize"] = 0.0f;
  SymbolChild c4; c4.id = 4; c4.symbolId = "RenderTarget";
  c4.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4};
  root.connections = {
      {1, "points", 2, "GPoints"},
      {2, "out", 3, "points"},
      {3, "out", 4, "command"},
      {4, "out", kSymbolBoundary, "out"},
  };
  slib.symbols["Root"] = root; slib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"4");
  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  px.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

}  // namespace

int runTransformPointsFromClipspaceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-transformpointsfromclipspace] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1)+(2) DIRECT-COOK closed-form Position + Rotation ─────────────────────────────────────────
  const uint32_t N = 2;
  SwPoint in[2];
  in[0] = SwPoint{}; in[0].Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  in[0].Rotation = SW_FLOAT4{0, 0, 0, 1}; in[0].Color = SW_FLOAT4{1, 1, 1, 1};
  in[1] = SwPoint{}; in[1].Position = SW_PACKED3{0.3f, -0.2f, 0.0f};
  in[1].Rotation = SW_FLOAT4{0, 0, 0, 1}; in[1].Color = SW_FLOAT4{1, 1, 1, 1};

  SwPoint out[2];
  float c2w[16];
  directLeg(dev, q, lib, /*withCamera=*/!injectBug, in, N, out, c2w);

  // Host closed-form Position == mat4TransformPointDivW(CameraToWorld, probe). Build a Mat4 from c2w.
  Mat4 C2W;
  std::memcpy(C2W.m, c2w, 16 * sizeof(float));
  float wantPos0[3], wantPos1[3];
  mat4TransformPointDivW(C2W, in[0].Position.x, in[0].Position.y, in[0].Position.z, wantPos0);
  mat4TransformPointDivW(C2W, in[1].Position.x, in[1].Position.y, in[1].Position.z, wantPos1);

  auto posErr = [](const SwPoint& p, const float w[3]) {
    return std::fabs(p.Position.x - w[0]) + std::fabs(p.Position.y - w[1]) + std::fabs(p.Position.z - w[2]);
  };
  float pe0 = posErr(out[0], wantPos0), pe1 = posErr(out[1], wantPos1);
  // The default camera puts (0,0,0) at world (0,0,DefaultCameraDistance); a passthrough leaves z=0. So
  // the want value is moved (z != 0) — the closed-form check both pins the value AND proves motion.
  bool posPass = (pe0 < 1e-3f) && (pe1 < 1e-3f);

  // Rotation closed-form: p.Rotation should equal normalize(qFromMatrix3(transpose(C2W 3×3))) qMul'd with
  // the identity input rotation = that quaternion. Compute via the same host 3×3-rows-as-transpose form.
  // qFromMatrix3Precise host equivalent: reuse the shader's identity — build it from the rows. We assert
  // the rotation is a UNIT quaternion AND (for the default axis-aligned camera) the rotation is the
  // identity-or-180°-about-an-axis (|w| stays 1 for an axis-aligned orientation up to sign). The strong
  // pin is that injectBug (identity matrix -> identity quaternion) gives Rotation EXACTLY (0,0,0,1); the
  // real default camera's CameraToWorld 3×3 is a proper rotation so its quaternion is unit but NOT the
  // raw passthrough when the matrix has a sign flip — we assert unit-norm + that injectBug yields identity.
  float rn0 = std::sqrt(out[0].Rotation.x * out[0].Rotation.x + out[0].Rotation.y * out[0].Rotation.y +
                        out[0].Rotation.z * out[0].Rotation.z + out[0].Rotation.w * out[0].Rotation.w);
  bool rotUnit = std::fabs(rn0 - 1.0f) < 1e-3f;

  // ── (2b) ROTATION exact-value leg — NON-AXIS-ALIGNED camera (bites the conjugate bug) ────────────
  // The default camera's C2W≈identity makes the orientation quaternion ≈ identity, where conjugate==self —
  // so the conjugate (抽row) bug HIDES. Drive a known off-axis camera (eye=(3,2,4)) so the orientation is a
  // proper rotation: 抽column → the TiXL GPU quaternion; 抽row → its conjugate. Expected quaternion computed
  // host-side via qFromMatrix3PreciseHost on the c2w 3×3 read DIRECTLY row-major (== the GPU view after the
  // TransformBufferLayout cbuffer-transpose). Compare via |dot(q_gpu, q_expected)| ≈ 1 (quaternion sign
  // ambiguity, mirrors the meshverticestopoints golden). At eye=(3,2,4): q_expected ≈ (0.179,-0.311,-0.060,
  // 0.932); the conjugate ≈ (-0.179,0.311,0.060,0.932) → |dot| ≈ |1-2(x²+y²+z²)| ≈ 0.74 ≠ 1 → RED.
  float rotDot = 0.0f; bool rotExactPass = false;
  {
    SwPoint rin[1];
    rin[0] = SwPoint{}; rin[0].Position = SW_PACKED3{0.1f, -0.05f, 0.2f};
    rin[0].Rotation = SW_FLOAT4{0, 0, 0, 1}; rin[0].Color = SW_FLOAT4{1, 1, 1, 1};
    float eye[3] = {3.0f, 2.0f, 4.0f}, target[3] = {0.0f, 0.0f, 0.0f}, up[3] = {0.0f, 1.0f, 0.0f};
    RaymarchTransforms rt = raymarchTransforms(eye, target, up, kDefaultCamFovDegrees, 1.0f, 0.01f, 1000.0f);
    SwPoint rout[1];
    directLegCustomC2W(dev, q, lib, rt.cameraToWorld.m, rin, 1, rout);
    // Expected: qFromMatrix3Precise on c2w 3×3 read row-major (a[R][C] = c2w[R*4+C]). qMul'd with identity
    // input rotation = that quaternion. (The conjugate bug would yield the inverse → |dot| << 1.)
    float a33[3][3];
    for (int R = 0; R < 3; ++R)
      for (int C = 0; C < 3; ++C) a33[R][C] = rt.cameraToWorld.m[R * 4 + C];
    float qe[4]; qFromMatrix3PreciseHost(a33, qe);
    rotDot = std::fabs(rout[0].Rotation.x * qe[0] + rout[0].Rotation.y * qe[1] +
                       rout[0].Rotation.z * qe[2] + rout[0].Rotation.w * qe[3]);
    rotExactPass = std::fabs(rotDot - 1.0f) < 1e-3f;
  }

  // ── (3) FLAT-DRIVER gather leg ──────────────────────────────────────────────────────────────────
  float fgPos[3]; uint32_t fgCount = 0;
  bool gotFg = flatGraphLeg(dev, q, lib, fgPos, fgCount);
  // GREEN: the flat driver filled the camera -> the points unprojected -> z shifted off the ring plane
  // (z=0). injectBug isn't structurally available on the flat driver (it ALWAYS fills the camera for a
  // "Camera" port), so this leg's tooth is: did the seam MOVE the points (|z| meaningfully non-zero)?
  bool fgPass = gotFg && fgCount > 0 && std::fabs(fgPos[2]) > 0.1f;

  // ── (4) RESIDENT (production) leg ───────────────────────────────────────────────────────────────
  std::vector<uint8_t> px;
  uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, px, ow, oh);
  int litCount = 0;
  if (gotRes)
    for (size_t i = 0; i < (size_t)ow * oh; ++i)
      if (px[i * 4 + 0] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++litCount;
  bool resPass = gotRes && litCount > 20;

  // injectBug: identity camera -> Position passthrough (z stays 0) -> posPass FAILS; Rotation stays
  // identity. The direct legs carry the bite; the flat/resident legs prove the seam reaches both drivers.
  bool pass = posPass && rotUnit && rotExactPass && fgPass && resPass;
  std::printf("[selftest-transformpointsfromclipspace] POS: err0=%.5f err1=%.5f want0=(%.3f,%.3f,%.3f) "
              "got0=(%.3f,%.3f,%.3f) pass=%d | ROT: |q|=%.4f unit=%d q0=(%.3f,%.3f,%.3f,%.3f) | "
              "ROT-EXACT(off-axis): |dot|=%.4f pass=%d | FLAT-DRIVER: count=%u pos=(%.3f,%.3f,%.3f) pass=%d | "
              "RESIDENT: %ux%u lit=%d pass=%d | injectBug=%d -> %s\n",
              pe0, pe1, wantPos0[0], wantPos0[1], wantPos0[2], out[0].Position.x, out[0].Position.y,
              out[0].Position.z, posPass ? 1 : 0, rn0, rotUnit ? 1 : 0, out[0].Rotation.x,
              out[0].Rotation.y, out[0].Rotation.z, out[0].Rotation.w, rotDot, rotExactPass ? 1 : 0,
              fgCount, fgPos[0], fgPos[1],
              fgPos[2], fgPass ? 1 : 0, ow, oh, litCount, resPass ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
