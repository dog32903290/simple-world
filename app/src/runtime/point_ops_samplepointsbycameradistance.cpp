// SamplePointsByCameraDistance — the SECOND camera-matrix-into-points seam consumer
// (PointCookCtx::objectToCamera) AND a rider of the bake-into-point seam (PointCookCtx::inputCurves — the
// WForDistance Curve baked to a 256×1 R32 scratch). Faithful port of external/tixl
// .../Assets/shaders/points/modify/SamplePointsByCameraDistance.hlsl + .../point/modify/
// SamplePointsByCameraDistance.{cs,t3}. A count-preserving MODIFIER: scale each point's W (== SwPoint.FX1
// @12, the renderer's W-size field) by a WForDistance Curve sampled at the camera-space depth.
//
// SEAMS it rides: the camera-matrix-into-points seam (the cook driver detects the "Camera" marker port +
// fills PointCookCtx::objectToCamera from the DEFAULT camera via fillPointCamera) AND the bake-into-point
// seam (the WForDistance Curve is gathered into PointCookCtx::inputCurves; the op bakes it — or the .t3
// embedded linear-0→1 default — into a 256×1 R32 texture in-cook, exactly like MapPointAttributes' curve
// bake, then samples it Clamp+Linear @t1).
//
// NAMED forks (also in samplepointsbycameradistance.metal / _params.h headers):
//   • fork-camera-one-matrix-per-op: the kernel reads ObjectToCamera ONLY; the host computes ONLY that one
//     (the other 9 TransformBufferLayout matrices are dead).
//   • fork-camera-default-only-v1: default camera + identity ObjectToWorld → ObjectToCamera = WorldToCamera.
//   • fork-wfordistance-embedded-default-curve: unwired Curve → the op bakes the .t3 linear-0→1 default.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary / atomicOp (resident leg)
#include "runtime/curve.h"                     // sw::Curve (the baked WForDistance currency)
#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/field_camera.h"              // Mat4 / pointCameraMatrices / mat4TransformPointDivW (golden)
#include "runtime/graph.h"                     // Graph/Node/pinId (flat-driver leg)
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"       // buildEvalGraph (resident leg)
#include "runtime/samplepointsbycameradistance_params.h"  // SpcdParams, SPCD_* bindings
#include "runtime/tixl_point.h"                // SwPoint (64B); .FX1 (@12) == TiXL p.W

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr int kCurveSampleCount = 256;  // CurvesToTexture.cs SampleSize default (the .t3 WForDistance bake)

// The .t3-embedded DEFAULT WForDistance curve (SamplePointsByCameraDistance.t3): a LINEAR line from
// (0,0) to (1,1) — value == normalized depth. Baked when the Curve input is unwired (always, in
// production — no Curve producer op yet).
const Curve& defaultWForDistanceCurve() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = 0.0; k0.value = 0.0;  // .t3 first key Time=0 Value=0 (Linear)
    k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1;
    k1.u = 1.0; k1.value = 1.0;  // .t3 second key Time=1 Value=1 (Linear)
    k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
    c.addOrUpdate(0.0, k0);
    c.addOrUpdate(1.0, k1);
    return c;
  }();
  return c;
}

// Allocate a 1-row R32_Float scratch, upload `host`. ShaderRead/Shared. Caller releases. Mirrors
// point_ops_mappointattributes.cpp makeRowTex (the bake-into-point seam's scratch idiom).
MTL::Texture* makeR32Row(MTL::Device* dev, uint32_t width, const float* host) {
  if (width == 0) width = 1;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR32Float, width, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  t->replaceRegion(MTL::Region::Make2D(0, 0, width, 1), 0, host, (NS::UInteger)(width * sizeof(float)));
  return t;
}

// SamplePointsByCameraDistance cook: bake the WForDistance curve (gathered or .t3 default) into a 256×1
// R32 scratch, then dispatch — d = ObjectToCamera.z, normalized depth → curve → p.FX1 *= t.r. count from
// c.count. No Points input → nothing to do. hasCamera=false (a hand-built ctx without the camera, the
// injectBug leg) leaves ObjectToCamera identity → d=0 for every point → all W(FX1) scale equally (RED tooth).
void cookSamplePointsByCameraDistance(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input → nothing to do

  // ── BAKE the WForDistance curve (R32, 256×1): value = curve.sample(i/sampleCount). Gathered Curve if
  //    wired (a golden injects one), else the .t3 embedded linear-0→1 default. ──
  const Curve* curve = (c.inputCurves && !c.inputCurves->empty()) ? &c.inputCurves->front()
                                                                  : &defaultWForDistanceCurve();
  std::vector<float> curveHost;
  curveHost.reserve(kCurveSampleCount);
  for (int i = 0; i < kCurveSampleCount; ++i)
    curveHost.push_back((float)curve->sample((double)((float)i / kCurveSampleCount)));

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("samplepointsbycameradistance", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  MTL::Texture* curveTex = makeR32Row(c.dev, (uint32_t)kCurveSampleCount, curveHost.data());

  // Clamp + Linear sampler (the .t3 SamplerState default for a curve LUT; Clamp keeps an out-of-[0,1]
  // normalized depth pinned to the curve's endpoint, matching TiXL's PostCurve=Constant intent).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  SpcdParams P{};
  P.Count = c.count;
  P.NearRange = cookParam(c, "NearRange", 0.0f);  // .t3 default 0
  P.FarRange = cookParam(c, "FarRange", 10.0f);   // .t3 default 10
  if (c.hasCamera) std::memcpy(P.ObjectToCamera, c.objectToCamera, 16 * sizeof(float));
  else { std::memset(P.ObjectToCamera, 0, 16 * sizeof(float)); P.ObjectToCamera[0] = P.ObjectToCamera[5] =
         P.ObjectToCamera[10] = P.ObjectToCamera[15] = 1.0f; }  // identity (d=0 everywhere; RED tooth)

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SPCD_SourcePoints);
  enc->setBuffer(c.output, 0, SPCD_ResultPoints);
  enc->setBytes(&P, sizeof(P), SPCD_Params);
  enc->setTexture(curveTex, SPCD_CurveTex);
  enc->setSamplerState(samp, SPCD_TexSampler);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  samp->release();
  curveTex->release();
  pso->release();
}

}  // namespace

void registerSamplePointsByCameraDistanceOp() {
  registerPointOp("SamplePointsByCameraDistance", cookSamplePointsByCameraDistance);
}

// ============================================================================================
// Golden — FOUR legs (R-2: flat-only is self-deception; the camera matrix + the baked curve must both
// reach the kernel, and the depth→W scaling must match the host closed-form).
//
//  (1) DIRECT-COOK closed-form (default-curve W==FX1): hand-built ctx with hasCamera + the default-camera
//      objectToCamera; the .t3 default linear-0→1 curve; input FX1=1 (the W field). For pos=(0,0,0): d = host
//      mat4TransformPointDivW(ObjectToCamera, 0,0,0).z = -DefaultCameraDistance (-2.4142); normalized =
//      (-d-0)/(10-0) = 0.24142; linear curve → 0.24142; want W = 1·0.24142. Computed LIVE host-side, NOT
//      hardcoded. injectBug (hasCamera=false → identity ObjectToCamera) → d=0 → normalized=0 → curve→0 →
//      W=0 for EVERY point → diverges from the per-depth want → RED.
//
//  (2) DIRECT-COOK second probe (depth dependence): a SECOND point pos=(0,0,1) has a DIFFERENT camera
//      depth → a DIFFERENT W. Asserts W(probe0) != W(probe1) (proves the kernel reads the real depth, not
//      a constant) AND each matches its host closed-form value.
//
//  (3) CUSTOM-CURVE bake leg (bites the bake-into-point seam): inject a custom 2-key curve (value 0.5
//      everywhere → a constant-0.5 LUT) via inputCurves; with the default camera depth the W becomes
//      1·0.5 = 0.5 regardless of the normalized depth. Asserts W ≈ 0.5 (the injected curve drove the bake,
//      not the .t3 default-linear). A curve-bake regression (default used instead) yields 0.24142 ≠ 0.5.
//
//  (4) RESIDENT (production) leg — W-tied size LIVE: RadialPoints(W=1, non-zero) →
//      SamplePointsByCameraDistance → DrawPoints2(UseWForSize=1) → RenderTarget via cookResident; read the
//      rendered pixels → assert lit sprites exist. With UseWForSize=1 the renderer reads pt.FX1 as the
//      sprite scale, so this leg drives the WHOLE production chain end-to-end: the op MUST write the depth
//      curve into FX1 (the renderer's W field) for any sprite to render at a visible size. If the op wrote
//      FX2 (the silent-no-op bug), UseWForSize=1 would read the unscaled FX1=1 — so this leg alone does NOT
//      bite the FX2 bug; the direct FX1 byte-read legs carry that tooth. This leg proves the camera+curve
//      gather reaches cookResident AND the W-size path renders.
// ============================================================================================

namespace {

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// A constant-`value` 2-key curve (both keys == value, Linear) — the custom-curve bake probe.
Curve constCurve(double value) {
  Curve c;
  c.preCurveMapping = OutsideBehavior::Constant;
  c.postCurveMapping = OutsideBehavior::Constant;
  VDefinition k0; k0.u = 0.0; k0.value = value;
  k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
  VDefinition k1; k1.u = 1.0; k1.value = value;
  k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
  c.addOrUpdate(0.0, k0); c.addOrUpdate(1.0, k1);
  return c;
}

// A grid of W(FX1)=1 points — the resident-leg source gen. GPU RadialPoints hardcodes FX1=0
// (radial_points.metal:46 → no W to scale), so a W-tied resident leg needs a gen that emits non-zero W
// (mirrors point_ops_drawpoints2.cpp's singlePointGen). Points spread on an 8×8 XY grid at z=0 so several
// project on-screen at the default camera; the depth curve scales each W → a visible W-sized sprite cluster.
void gridPointsW1Gen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    float fx = (float)(i % 8) / 7.0f - 0.5f, fy = (float)((i / 8) % 8) / 7.0f - 0.5f;  // [-0.5,0.5]
    dst[i] = SwPoint{};
    dst[i].Color = {1, 1, 1, 1}; dst[i].Scale = {1, 1, 1};
    dst[i].FX1 = 1.0f;  // W = 1 — the op scales this by the depth curve; UseWForSize reads it
    dst[i].Position = {0.6f * fx, 0.6f * fy, 0.0f};
  }
}

// DIRECT-COOK leg: dispatch over a hand-built bag, byte-read the output FX1 (== p.W, @12). hasCamera =
// withCamera; optional injected curve (null → the .t3 default). Returns out[0..N-1] + the default-camera
// objectToCamera (for the host closed-form).
bool directLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool withCamera,
               const std::vector<Curve>* injectCurve, const SwPoint* in, uint32_t N, SwPoint* out,
               float objectToCamera[16]) {
  float c2wUnused[16];
  pointCameraMatrices(1.0f, objectToCamera, c2wUnused);

  MTL::Buffer* srcBag = dev->newBuffer(in, (size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {N};
  std::map<std::string, float> params;
  params["NearRange"] = 0.0f; params["FarRange"] = 10.0f;
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = N;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (withCamera) { std::memcpy(c.objectToCamera, objectToCamera, 16 * sizeof(float)); c.hasCamera = true; }
  if (injectCurve) c.inputCurves = injectCurve;
  cookSamplePointsByCameraDistance(c);

  std::memcpy(out, outBag->contents(), (size_t)N * sizeof(SwPoint));
  srcBag->release(); outBag->release();
  return true;
}

// RESIDENT (production) leg: RadialPoints → SamplePointsByCameraDistance → DrawPoints2 → RenderTarget via
// cookResident; read the rendered pixels. Asserts lit sprites (the resident camera + curve gather LIVES).
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();
  registerPointOp("RadialPoints", gridPointsW1Gen);  // override with a W(FX1)=1 gen (GPU RadialPoints =W0)

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 64.0f}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["SamplePointsByCameraDistance"] = atomicOp(
      "SamplePointsByCameraDistance",
      {{"Points", "Points", "Points", 0.0f}, {"Camera", "Camera", "Camera", 0.0f},
       {"NearRange", "NearRange", "Float", 0.0f}, {"FarRange", "FarRange", "Float", 10.0f},
       {"WForDistance", "WForDistance", "Curve", 0.0f}},
      {{"out", "out", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] = atomicOp(
      "DrawPoints2",
      {{"points", "points", "Points", 0.0f},
       {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
       {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
       {"Radius", "Radius", "Float", 0.06f}, {"UseWForSize", "UseWForSize", "Float", 1.0f}},
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
  SymbolChild c2; c2.id = 2; c2.symbolId = "SamplePointsByCameraDistance";
  SymbolChild c3; c3.id = 3; c3.symbolId = "DrawPoints2";
  c3.overrides["Radius"] = 0.40f; c3.overrides["UseWForSize"] = 1.0f;  // W-tied size LIVE: RadialPoints emits
  // W(FX1)=1, the op scales it by the depth curve (~0.24 at default depth), DrawPoints2 reads pt.FX1 as the
  // sprite scale. Radius generous so the depth-shrunk sprites still light >20 px. This drives the production
  // W-size path end-to-end; the FX2 silent-no-op would leave FX1=1 unscaled, so the direct FX1 byte-read legs
  // carry the FX2 tooth — this leg proves the camera+curve gather reaches cookResident AND the W path renders.
  SymbolChild c4; c4.id = 4; c4.symbolId = "RenderTarget";
  c4.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4};
  root.connections = {
      {1, "points", 2, "Points"},
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

int runSamplePointsByCameraDistanceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-samplepointsbycameradistance] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1)+(2) DIRECT-COOK default-curve closed-form W + depth dependence ───────────────────────────
  const uint32_t N = 2;
  SwPoint in[2];
  in[0] = SwPoint{}; in[0].Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  in[0].FX1 = 1.0f; in[0].Rotation = SW_FLOAT4{0, 0, 0, 1}; in[0].Color = SW_FLOAT4{1, 1, 1, 1};  // W==FX1
  in[1] = SwPoint{}; in[1].Position = SW_PACKED3{0.0f, 0.0f, 1.0f};
  in[1].FX1 = 1.0f; in[1].Rotation = SW_FLOAT4{0, 0, 0, 1}; in[1].Color = SW_FLOAT4{1, 1, 1, 1};  // W==FX1

  SwPoint out[2];
  float o2c[16];
  directLeg(dev, q, lib, /*withCamera=*/!injectBug, /*injectCurve=*/nullptr, in, N, out, o2c);

  // Host closed-form: d = mat4TransformPointDivW(ObjectToCamera, pos).z (affine, w==1 → DivW.z == raw .z);
  // normalized = (-d-0)/(10-0); linear-0→1 curve → value == normalized; want W = 1·value.
  Mat4 O2C;
  std::memcpy(O2C.m, o2c, 16 * sizeof(float));
  auto wantW = [&](const SwPoint& p) {
    float cs[3];
    mat4TransformPointDivW(O2C, p.Position.x, p.Position.y, p.Position.z, cs);
    float d = cs[2];
    float normalized = (-d - 0.0f) / (10.0f - 0.0f);
    return 1.0f * normalized;  // linear 0→1 curve: value == clamp(normalized) at the sampled LUT texel
  };
  float wantW0 = wantW(in[0]), wantW1 = wantW(in[1]);
  float wErr0 = std::fabs(out[0].FX1 - wantW0), wErr1 = std::fabs(out[1].FX1 - wantW1);  // W==FX1
  // 1e-2 tolerance: the curve is a 256-texel LUT sampled Linear, so the value carries a small quantization
  // vs the analytic curve (the texel grid + interpolation). The depths are far enough apart that this is
  // a tight pin, not a slop.
  bool wPass = (wErr0 < 1e-2f) && (wErr1 < 1e-2f);
  bool depthDep = std::fabs(out[0].FX1 - out[1].FX1) > 0.01f;  // the two depths give different W(FX1)

  // ── (3) CUSTOM-CURVE bake leg: inject a constant-0.5 curve → W = 1·0.5 regardless of depth ───────
  std::vector<Curve> custom = {constCurve(0.5)};
  SwPoint outC[2];
  float o2cC[16];
  directLeg(dev, q, lib, /*withCamera=*/!injectBug, /*injectCurve=*/&custom, in, N, outC, o2cC);
  // With the injected constant-0.5 LUT, BOTH probes scale W to ~0.5 (the bake used the injected curve, not
  // the .t3 linear default which would give the per-depth 0.24142/…). On injectBug (identity camera) d=0 →
  // normalized=0 → curve.sample(0)=0.5 (still constant 0.5) — so this leg does NOT bite injectBug; it bites
  // a CURVE-BAKE regression (default-linear used → 0.24142 ≠ 0.5). Both probes ≈ 0.5 when the bake is right.
  bool curvePass = (std::fabs(outC[0].FX1 - 0.5f) < 1e-2f) && (std::fabs(outC[1].FX1 - 0.5f) < 1e-2f);  // W==FX1

  // ── (4) RESIDENT (production) leg ───────────────────────────────────────────────────────────────
  std::vector<uint8_t> px;
  uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, px, ow, oh);
  int litCount = 0;
  if (gotRes)
    for (size_t i = 0; i < (size_t)ow * oh; ++i)
      if (px[i * 4 + 0] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++litCount;
  bool resPass = gotRes && litCount > 20;

  // injectBug: identity ObjectToCamera → d=0 for every point → normalized=0 → curve→0 → W=0 for ALL points
  // → wantW0/wantW1 (the per-depth host values, 0.24142 / …) diverge from 0 → wPass FAILS; depthDep also
  // collapses (all W==0). The direct legs carry the bite; the resident leg proves the seam reaches cookResident.
  bool pass = wPass && depthDep && curvePass && resPass;
  std::printf("[selftest-samplepointsbycameradistance] W: w0=%.5f(want %.5f) w1=%.5f(want %.5f) err=(%.4f,"
              "%.4f) pass=%d | DEPTH-DEP: |w0-w1|=%.4f dep=%d | CUSTOM-CURVE: w0=%.4f w1=%.4f (want 0.5) "
              "pass=%d | RESIDENT: %ux%u lit=%d pass=%d | injectBug=%d -> %s\n",
              out[0].FX1, wantW0, out[1].FX1, wantW1, wErr0, wErr1, wPass ? 1 : 0,
              std::fabs(out[0].FX1 - out[1].FX1), depthDep ? 1 : 0, outC[0].FX1, outC[1].FX1,
              curvePass ? 1 : 0, ow, oh, litCount, resPass ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
