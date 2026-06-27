// SetAttributesWithPointFields — the second-point-buffer point-modify (PointCookCtx::inputs[1] =
// FieldPoints) + bake-into-point seam consumer. Faithful port of external/tixl
// .../point/modify/SetAttributesWithPointFields.{cs,t3} +
// .../Assets/shaders/points/modify/SetPointAttributesWithPointFields.hlsl.
//
// ★LEAF, NOT A SEAM (verdict): the cook driver's buffer-input gather (point_graph.cpp:233-246) pushes
// EACH wired Points port into inputs[]/inputCounts[] BY PORT ORDER — it does NOT concatenate multiple
// Points inputs (concatenation happens only inside CombineBuffers' own blit cook). So inputs[0] =
// SourcePoints (port "Points") and inputs[1] = FieldPoints (port "FieldPoints") are DISTINCT and
// addressable. This is the exact pattern SnapToPoints already uses (inputs[0]=Points1 primary,
// inputs[1]=Points2 reference). We opt into OpReg.countFromFirstPointsInput=true so the output sizes to
// SourcePoints (inputs[0]) only — both ports are dataType "Points" so the driver's default sumPointsCount
// would size to N_source+N_field, leaving stale garbage; firstPointsCount = SourcePoints. NO cook-core
// edit needed.
//
// WHAT IT DOES (per SourcePoint): accumulate a stylistic gravity field over every FieldPoint within
// range; offset position along the field, orient toward it, blend color (via a baked Gradient), write W
// (via a baked Curve). VERBATIM math — see setattributeswithpointfields.metal.
//
// BAKE-INTO-POINT (mirror of MapPointAttributes): the .hlsl samples a CurveImage (t2) + GradientImage
// (t3). We bake the WCurve (.t3 default = a ramp 0→1) into a 512×1 R32 CurveImage and the Gradient (.t3
// default = black→white) into a 512×1 RGBA32 GradientImage, in-cook, with a Clamp/Linear sampler. There
// is a Gradient INPUT port (gathered into inputGradients); WCurve has NO producer op yet so it is always
// the embedded .t3 default (honest — same as MapPointAttributes' MappingCurve).
//
// ★.t3 DEFAULT AUDIT (SetAttributesWithPointFields.t3 Inputs[]):
//   Amount 1.0 | Range 1.0 | OffsetRange 0.0 | AffectPosition 0.25 | AffectOrientation 1.0 |
//   AffectColor 1.0 | AffectW 0.0 | Variation 0.0 | BiasAndGain (0.365, 0.59) [→ GainAndBias] |
//   OrientationUpVector (0,0,1) | ColorMode 2 (Blend) | WMode 2 (BlendWithOriginal) |
//   WCurveAffectsWeight false | Gradient black(0,0,0,1)→white(1,1,1,1) | WCurve ramp 0→1 |
//   Points null | FieldPoints null.
//
// SCRATCH-TEX LIFETIME: same per-cook alloc+release discipline as MapPointAttributes (curve 512×1 R32 +
// gradient 512×1 RGBA32, tiny; per-frame realloc fine, zero ctx plumbing).
#include "runtime/point_ops_setattributeswithpointfields.h"

#include "runtime/point_ops.h"

#include <cmath>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/curve.h"                            // sw::Curve (the baked curve currency)
#include "runtime/dispatch.h"                         // calcDispatchCount
#include "runtime/gradient_raster.h"                  // sampleGradientRowRGBA (shared gradient row sampler)
#include "runtime/point_graph.h"                      // PointCookCtx, registerPointOp, cookParam
#include "runtime/setattributeswithpointfields_params.h"  // SetAttrWithFieldsParams, SAWF_* bindings
#include "runtime/sw_gradient.h"                       // SwGradient (the baked gradient currency)
#include "runtime/tixl_point.h"                        // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr int kCurveSampleCount = 512;    // .t3 CurvesToTexture SampleSize
constexpr int kGradientResolution = 512;  // .t3 GradientsToTexture Resolution

// .t3-embedded DEFAULT WCurve (ramp 0→1 LINEAR). Baked when WCurve is unwired (always — no producer).
const Curve& defaultWCurve() {
  static const Curve c = []() {
    Curve cv;
    cv.preCurveMapping = OutsideBehavior::Constant;
    cv.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = 0.0; k0.value = 0.0;
    k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1;
    k1.u = 1.0; k1.value = 1.0;
    k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
    cv.addOrUpdate(0.0, k0);
    cv.addOrUpdate(1.0, k1);
    return cv;
  }();
  return c;
}

// .t3-embedded DEFAULT Gradient (black(0,0,0,1) → white(1,1,1,1), LINEAR). Baked when Gradient unwired.
SwGradient defaultFieldGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  SwGradientStep s0; s0.pos = 0.0f; s0.color = simd::make_float4(0, 0, 0, 1);
  SwGradientStep s1; s1.pos = 1.0f; s1.color = simd::make_float4(1, 1, 1, 1);
  g.steps = {s0, s1};
  return g;
}

// Allocate a single-row scratch texture, upload host floats. Caller releases. (== MapPointAttributes.)
MTL::Texture* makeRowTex(MTL::Device* dev, MTL::PixelFormat fmt, uint32_t width, const void* host,
                         size_t bytesPerTexel) {
  if (width == 0) width = 1;
  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(fmt, width, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  t->replaceRegion(MTL::Region::Make2D(0, 0, width, 1), 0, host, (NS::UInteger)(width * bytesPerTexel));
  return t;
}

}  // namespace

bool& setAttrWithFieldsInjectBug() {
  static bool b = false;
  return b;
}

// SetAttributesWithPointFields cook: bake WCurve→CurveImage + Gradient→GradientImage, then dispatch the
// kernel over SourcePoints (inputs[0]) reading FieldPoints (inputs[1]) as the field bag.
void cookSetAttributesWithPointFields(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input → nothing to do
  const MTL::Buffer* fieldBag = (c.inputCount > 1) ? c.inputs[1] : nullptr;
  uint32_t srcCount = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : c.count;
  uint32_t fieldCount = (c.inputCounts && c.inputCount > 1) ? c.inputCounts[1] : 0u;
  if (srcCount == 0) return;
  if (setAttrWithFieldsInjectBug()) fieldCount = 0u;  // sever the field input (RED path)

  // ── BAKE CurveImage (R32, 512×1): value = curve.sample(i/sampleCount). WCurve = .t3 ramp default. ──
  const Curve* curve = (c.inputCurves && !c.inputCurves->empty()) ? &c.inputCurves->front()
                                                                  : &defaultWCurve();
  std::vector<float> curveHost;
  curveHost.reserve(kCurveSampleCount);
  for (int i = 0; i < kCurveSampleCount; ++i)
    curveHost.push_back((float)curve->sample((double)((float)i / kCurveSampleCount)));

  // ── BAKE GradientImage (RGBA32, 512×1): the gathered Gradient, else the .t3 black→white default. ──
  SwGradient gradDefault = defaultFieldGradient();
  const SwGradient* gradient =
      (c.inputGradients && !c.inputGradients->empty()) ? &c.inputGradients->front() : &gradDefault;
  std::vector<float> gradHost;
  gradHost.reserve((size_t)kGradientResolution * 4);
  sampleGradientRowRGBA(*gradient, kGradientResolution, gradHost);

  MTL::Function* fn = c.lib->newFunction(
      NS::String::string("setattributeswithpointfields", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  MTL::Texture* curveTex = makeRowTex(c.dev, MTL::PixelFormatR32Float, (uint32_t)kCurveSampleCount,
                                      curveHost.data(), sizeof(float));
  MTL::Texture* gradTex = makeRowTex(c.dev, MTL::PixelFormatRGBA32Float, (uint32_t)kGradientResolution,
                                     gradHost.data(), sizeof(float) * 4);

  SetAttrWithFieldsParams P{};
  P.Amount = cookParam(c, "Amount", 1.0f);
  P.Range = cookParam(c, "Range", 1.0f);
  P.OffsetRange = cookParam(c, "OffsetRange", 0.0f);
  P.AffectPosition = cookParam(c, "AffectPosition", 0.25f);
  P.OrientationUpVector[0] = cookParam(c, "OrientationUpVector.x", 0.0f);
  P.OrientationUpVector[1] = cookParam(c, "OrientationUpVector.y", 0.0f);
  P.OrientationUpVector[2] = cookParam(c, "OrientationUpVector.z", 1.0f);
  P.AffectOrientation = cookParam(c, "AffectOrientation", 1.0f);
  P.AffectW = cookParam(c, "AffectW", 0.0f);
  P.AffectColor = cookParam(c, "AffectColor", 1.0f);
  P.GainAndBiasX = cookParam(c, "BiasAndGain.x", 0.365f);  // TiXL BiasAndGain (.cs) → GainAndBias (.hlsl)
  P.GainAndBiasY = cookParam(c, "BiasAndGain.y", 0.59f);
  P.Variation = cookParam(c, "Variation", 0.0f);
  P.ColorMode = std::round(cookParam(c, "ColorMode", 2.0f));            // .t3 default 2 (Blend)
  P.WMode = std::round(cookParam(c, "WMode", 2.0f));                    // .t3 default 2 (Blend)
  P.WCurveAffectsWeight = std::round(cookParam(c, "WCurveAffectsWeight", 0.0f));  // .t3 false
  P.Count = srcCount;
  P.FieldCount = fieldCount;

  // Sampler (s0): Clamp/Clamp + Linear (.t3 SamplerState AddressU/V=Clamp; .hlsl samples (f,0.5)).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // FieldPoints unwired: bind a dummy (srcBag) — the kernel guards FieldCount==0 (loop never runs).
  const MTL::Buffer* fieldBind = fieldBag ? fieldBag : srcBag;

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SAWF_SourcePoints);
  enc->setBuffer(const_cast<MTL::Buffer*>(fieldBind), 0, SAWF_FieldPoints);
  enc->setBuffer(c.output, 0, SAWF_ResultPoints);
  enc->setBytes(&P, sizeof(P), SAWF_Params);
  enc->setTexture(curveTex, SAWF_CurveImage);
  enc->setTexture(gradTex, SAWF_GradientImage);
  enc->setSamplerState(samp, SAWF_TexSampler);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(srcCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  curveTex->release();
  gradTex->release();
  pso->release();
}

void registerSetAttributesWithPointFieldsOp() {
  // countFromFirstPointsInput=true: output count = SourcePoints (inputs[0]) only. Both ports are
  // dataType "Points" so the driver's default sumPointsCount would size to N_src+N_field (stale garbage
  // on the field slice). FieldPoints is a reference bag, not concatenated.
  registerPointOp("SetAttributesWithPointFields", cookSetAttributesWithPointFields,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr, /*countTransform=*/nullptr,
                  /*countFromFirstPointsInput=*/true);
}

}  // namespace sw
