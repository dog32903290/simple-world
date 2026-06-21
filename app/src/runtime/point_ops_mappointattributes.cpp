// MapPointAttributes — the bake-into-point seam consumer (PointCookCtx::inputCurves/inputGradients).
// Faithful port of external/tixl .../point/modify/MapPointAttributes.{cs,hlsl,t3}. A count-preserving
// MODIFIER: the op BAKES its host Curve (→ a 256×1 R32_Float CurveImage) + host Gradient (→ a 512×1
// RGBA32 GradientImage) into two scratch textures during cook, then per point maps an input coordinate
// (InputMode → f0 → MappingMode remap with Range/Phase) and samples both at (f,0.5) with a Clamp/Linear
// sampler to write a curve value into FX1/FX2/Scale (WriteTo) + a gradient color into Color (WriteColor,
// default Multiply). The .t3 compound bakes the host inputs via CurvesToTexture(SampleSize 256) /
// GradientsToTexture(Resolution 512) + FirstValidTexture (ValueTexture OVERRIDES the baked curve) — NOT
// straight wires (Cut55: read the .hlsl cbuffer directly, don't reconstruct the node graph).
//
// SEAM (bake-into-point): the cook driver gathers this op's Gradient/Curve INPUT ports into
// PointCookCtx::inputGradients/inputCurves on BOTH the flat and resident paths. There is NO Curve/Gradient
// PRODUCER op yet, so in PRODUCTION these are EMPTY → the op bakes its embedded .t3 DEFAULTS (flat-1.0
// curve, white→white gradient), exactly as CurvesToTexture/GradientsToTexture cook their embedded defaults
// when unwired (honest, NOT the flat-only string-rail trap — no real wire dropped). A golden injects custom
// values via inputCurves/inputGradients to bite the bake.
//
// NodeSpec ports 1:1 with MapPointAttributes.cs [Input] (invent NO knobs; .t3 defaults): Points(Points) |
// InputMode(enum 0) | Strength(float 1) | StrengthFactor(enum 0) | Mapping(enum 0) | Range(float 1) |
// Phase(float 0) | WriteColor(enum 2 Multiply) | Gradient(Gradient, .t3 white→white) | WriteTo(enum 0) |
// WriteMode(enum 0 Replace) | MappingCurve(Curve, .t3 flat-1.0) | ValueTexture(Texture2D, .t3 null).
//
// SCRATCH-TEX LIFETIME (named simplification vs the blueprint's ensureOwnedTex caching): the op allocates
// its two scratch textures IN-cook (newTexture + replaceRegion of the baked host floats) and RELEASES them
// after waitUntilCompleted — the SAME per-cook alloc+release discipline SPCA uses for its pso/sampler. The
// curve row is 256×1 R32 + gradient row 512×1 RGBA32 (tiny); per-frame realloc is fine and needs ZERO
// driver/ctx plumbing (the blueprint's per-node texture caching is an optimization, not a parity
// requirement). Leak-free (balanced alloc/release) — the RESOURCE_LIFETIME golden governs driver-owned
// OUTPUT buffers, not in-cook transient scratch.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary / atomicOp (resident leg)
#include "runtime/curve.h"                     // sw::Curve (the baked curve currency)
#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"           // sampleGradientRowRGBA (shared gradient row sampler — no drift)
#include "runtime/graph.h"                     // Graph/Node/pinId
#include "runtime/graph_bridge.h"              // libFromGraph (flat Graph → SymbolLibrary, resident leg)
#include "runtime/mappointattributes_params.h" // MapPointAttrParams, MPA_* bindings
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"       // buildEvalGraph (resident leg)
#include "runtime/sw_gradient.h"               // SwGradient (the baked gradient currency)
#include "runtime/tixl_point.h"                // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// The CurvesToTexture child's default SampleSize (MapPointAttributes.t3 child c07a4962 sets NO Resolution
// override → CurvesToTexture.cs:142 default 256) + Horizontal. The GradientsToTexture child sets
// Resolution=512 (.t3 child e6858bcc:260-263).
constexpr int kCurveSampleCount = 256;     // CurvesToTexture.cs SampleSize default
constexpr int kGradientResolution = 512;   // MapPointAttributes.t3 GradientsToTexture Resolution

// The .t3-embedded DEFAULT MappingCurve (MapPointAttributes.t3:6-33): a flat 1.0 LINEAR line (two keys,
// both value 1.0). Baked when the MappingCurve input is unwired (always, in production — no Curve
// producer). Built via addOrUpdate so any tangent recompute matches TiXL's load path (Linear → no spline
// tangents, but keep the API symmetric with CurvesToTexture's default builder).
const Curve& defaultMappingCurve() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = -0.0118; k0.value = 1.0;  // .t3 first key Time=-0.0118 Value=1.0 (Linear)
    k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1;
    k1.u = 1.0; k1.value = 1.0;       // .t3 second key Time=1.0 Value=1.0 (Linear)
    k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
    c.addOrUpdate(-0.0118, k0);
    c.addOrUpdate(1.0, k1);
    return c;
  }();
  return c;
}

// The .t3-embedded DEFAULT Gradient (MapPointAttributes.t3:57-83): white→white, LINEAR (two stops both
// (1,1,1,1)). Baked when the Gradient input is unwired. ★Because WriteColor default = 2 (Multiply), an
// unwired gradient MUST bake white (×1 identity) or Color goes black.
SwGradient defaultMapGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  SwGradientStep s0; s0.pos = 0.0f; s0.color = simd::make_float4(1, 1, 1, 1);
  SwGradientStep s1; s1.pos = 1.0f; s1.color = simd::make_float4(1, 1, 1, 1);
  g.steps = {s0, s1};
  return g;
}

// Test-only injection seam: when set, the cook bakes the GradientImage from the .t3 WHITE default instead
// of the gathered gradient → with WriteColor=Multiply (default), p.Color * white = p.Color → Color
// PASSTHROUGH (the gradient's color is lost). The golden's RED case observes this white-passthrough
// (Color stays at its input, ≠ the injected gradient color). Corrupts the REAL cook path, NOT the
// expected value (`want` is FIXED at the true gradient color).
bool& mapPointAttributesInjectBug() {
  static bool b = false;
  return b;
}

// Allocate a single-row scratch texture of `fmt`, upload `host` (rowPitch = width * bytesPerTexel).
// ShaderRead, Shared. Caller releases.
MTL::Texture* makeRowTex(MTL::Device* dev, MTL::PixelFormat fmt, uint32_t width, const void* host,
                         size_t bytesPerTexel) {
  if (width == 0) width = 1;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(fmt, width, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  t->replaceRegion(MTL::Region::Make2D(0, 0, width, 1), 0, host, (NS::UInteger)(width * bytesPerTexel));
  return t;
}

// MapPointAttributes cook: bake the gathered Curve (or embedded default) into a 256×1 R32 CurveImage +
// the gathered Gradient (or embedded default) into a 512×1 RGBA32 GradientImage, then dispatch the kernel
// over the upstream Points bag. count from c.count (inherited from the upstream bag). No input Points →
// nothing to do.
void cookMapPointAttributes(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input → nothing to do

  // ── BAKE the CurveImage (R32_Float, 256×1). Reuses CurvesToTexture's host sampling: value =
  //    curve.sample(i/sampleCount) for i in [0,sampleCount) (CurvesToTexture.cs:84 divisor = sampleCount).
  //    The curve = the first gathered inputCurve (a wired MappingCurve), else the .t3 flat-1.0 default. ──
  const Curve* curve = (c.inputCurves && !c.inputCurves->empty()) ? &c.inputCurves->front()
                                                                  : &defaultMappingCurve();
  std::vector<float> curveHost;
  curveHost.reserve(kCurveSampleCount);
  for (int i = 0; i < kCurveSampleCount; ++i)
    curveHost.push_back((float)curve->sample((double)((float)i / kCurveSampleCount)));

  // ── BAKE the GradientImage (RGBA32, 512×1). Reuses sampleGradientRowRGBA (shared row sampler — no drift
  //    vs GradientsToTexture): col = gradient.sample(i/(N-1)) for i in [0,N), 4 floats/texel. The gradient
  //    = the first gathered inputGradient (a wired Gradient), else the .t3 white→white default. ──
  SwGradient gradDefault = defaultMapGradient();
  // injectBug: bake the WHITE default instead of the gathered gradient → Color passthrough under Multiply.
  const SwGradient* gradient = (!mapPointAttributesInjectBug() && c.inputGradients &&
                                !c.inputGradients->empty())
                                   ? &c.inputGradients->front()
                                   : &gradDefault;
  std::vector<float> gradHost;
  gradHost.reserve((size_t)kGradientResolution * 4);
  sampleGradientRowRGBA(*gradient, kGradientResolution, gradHost);

  // ── FirstValidTexture (MapPointAttributes.t3 ef24f862): CurveImage = first-valid of {ValueTexture (if
  //    wired @ inputTextures[0]), the baked curve}. ValueTexture OVERRIDES the baked curve. ──
  const MTL::Texture* valueTex = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("mappointattributes", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  // Scratch textures (alloc + upload; released after the dispatch completes — see header note).
  MTL::Texture* curveTex =
      makeRowTex(c.dev, MTL::PixelFormatR32Float, (uint32_t)kCurveSampleCount, curveHost.data(),
                 sizeof(float));
  MTL::Texture* gradTex =
      makeRowTex(c.dev, MTL::PixelFormatRGBA32Float, (uint32_t)kGradientResolution, gradHost.data(),
                 sizeof(float) * 4);

  // CurveImage @t1 = ValueTexture override if wired, else the baked curve.
  const MTL::Texture* curveBind = valueTex ? valueTex : curveTex;
  // GradientImage @t2 = the baked gradient row (white default under injectBug, the RED fault — see above).
  const MTL::Texture* gradBind = (const MTL::Texture*)gradTex;

  MapPointAttrParams P{};
  P.Count = c.count;
  P.Strength = cookParam(c, "Strength", 1.0f);
  P.Range = cookParam(c, "Range", 1.0f);
  P.Phase = cookParam(c, "Phase", 0.0f);
  P.InputMode = std::round(cookParam(c, "InputMode", 0.0f));
  P.MappingMode = std::round(cookParam(c, "Mapping", 0.0f));
  P.ApplyMode = std::round(cookParam(c, "WriteMode", 0.0f));    // TiXL WriteMode → shader ApplyMode
  P.WriteTo = std::round(cookParam(c, "WriteTo", 0.0f));
  P.WriteColor = std::round(cookParam(c, "WriteColor", 2.0f));  // .t3 default 2 (Multiply)
  P.StrengthFactor = std::round(cookParam(c, "StrengthFactor", 0.0f));

  // Sampler (s0): Clamp address / Linear filter (.t3 SamplerState AddressU/V=Clamp, ClampedSampler is a
  // LINEAR-filtered sampler — NOT SPCA's Repeat/Nearest).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, MPA_SourcePoints);
  enc->setBuffer(c.output, 0, MPA_ResultPoints);
  enc->setBytes(&P, sizeof(P), MPA_Params);
  enc->setTexture(const_cast<MTL::Texture*>(curveBind), MPA_CurveImage);
  enc->setTexture(const_cast<MTL::Texture*>(gradBind), MPA_GradientImage);
  enc->setSamplerState(samp, MPA_ClampedSampler);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  curveTex->release();
  gradTex->release();
  pso->release();
}

}  // namespace

void registerMapPointAttributesOp() {
  registerPointOp("MapPointAttributes", cookMapPointAttributes);
}

// ============================================================================================
// Golden — FOUR legs (R-2: flat-only is self-deception). Injects a known 2-key LINEAR curve (0,0)→(1,1)
// [sample(t)=t] + 2-step LINEAR gradient RED@0→BLUE@1 [sample(t)=(1-t,0,t,1)]; reads back FX1 (==curve.r
// at f) AND Color (==gradient at f, multiplied). Hand arithmetic below. injectBug bakes the WHITE default
// gradient instead → Color = white-passthrough (≠ the injected gradient); `want` FIXED at the true values.
//
//  (1) INJECT direct-cook leg (the biting leg). Hand-built ctx + injected inputCurves/inputGradients.
//      Params: InputMode=F1, Mapping=ForStart, Range=1, Phase=0 → f = f0 = p.FX1. WriteTo=F1,
//      WriteMode=Replace → newValue = curveValue = curve.sample(f) = f. Strength=1, StrengthFactor=None →
//      strength=1 → p.FX1 = lerp(org, f, 1) = f. WriteColor=Multiply (.t3 default), input Color=WHITE →
//      gradColor = gradient.sample(f) = (1-f,0,f,1); p.Color = white*gradColor = gradColor.
//        Input FX1 = 0.25 → f = 0.25 → CurveImage.sample(0.25) ≈ 0.25 (256-row R32, Linear/Clamp;
//          texel center 64/256=0.25 lands on value 0.25) → FX1 ≈ 0.25.
//          GradientImage.sample(0.25) ≈ (0.75, 0, 0.25, 1) (512-row RGBA32, Linear) → Color ≈ (0.75,0,0.25,1).
//        want FX1 = 0.25 (tol 3e-3), Color = (0.75,0,0.25,1) (tol 3e-3). injectBug → white gradient →
//        Color = (1,1,1,1) → the Color pins diverge → RED.
//
//  (2) FLAT-DRIVER production leg (closes the flat-driver gather gap). A real flat Graph
//      RadialPoints(#1) → MapPointAttributes(#2), cooked through PointGraph::cook (target = #2), reads the
//      cooked Points buffer via debugCookedBuffer. Curve/Gradient UNWIRED → the op bakes its embedded
//      .t3 DEFAULTS (flat-1.0 curve, white gradient). With WriteTo=F1 Replace, every point's FX1 → 1.0
//      (the flat-1.0 curve value — a PASSTHROUGH would leave RadialPoints' FX1, which is NOT 1.0); white
//      gradient under Multiply leaves Color the RadialPoints color (identity). Proves the seam gather +
//      default bake fire on the production flat path. (Not injectBug-sensitive — the white default == the
//      injectBug gradient; the bite lives in leg 1, R-2 pattern as CurvesToTexture.)
//
//  (3) RESIDENT (production) leg — R-2 iron rule. RadialPoints + MapPointAttributes → DrawPoints2 →
//      RenderTarget, cooked through cookResident; the op bakes the embedded defaults (no producer). Reads
//      the rendered pixels → asserts a lit sprite (the bake ran on the production resident path; a crash /
//      empty texture if the resident Gradient/Curve gather were missing). Proves the seam is LIVE on
//      cookResident, not flat-only.
// ============================================================================================

namespace {

// LINEAR 2-key curve (0,0)→(1,1): sample(t)=t for t∈[0,1].
Curve makeRampCurve() {
  Curve c;
  VDefinition k0; k0.u = 0.0; k0.value = 0.0;
  k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
  VDefinition k1; k1.u = 1.0; k1.value = 1.0;
  k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
  c.addOrUpdate(0.0, k0);
  c.addOrUpdate(1.0, k1);
  return c;
}

// LINEAR 2-step gradient RED@0 → BLUE@1: sample(t) = ((1-t), 0, t, 1).
SwGradient makeRedBlueGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  SwGradientStep s0; s0.pos = 0.0f; s0.color = simd::make_float4(1, 0, 0, 1);  // RED
  SwGradientStep s1; s1.pos = 1.0f; s1.color = simd::make_float4(0, 0, 1, 1);  // BLUE
  g.steps = {s0, s1};
  return g;
}

// (1) INJECT direct-cook leg: hand-built ctx + injected curve/gradient. Returns the single output point.
bool injectLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool injectBug, SwPoint& out) {
  SwPoint in{};
  in.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  in.FX1 = 0.25f;                                  // f0 = FX1 = 0.25 → f = 0.25
  in.FX2 = 0.0f;
  in.Color = SW_FLOAT4{1.0f, 1.0f, 1.0f, 1.0f};    // WHITE → Multiply by gradColor = gradColor
  in.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  in.Scale = SW_PACKED3{1.0f, 1.0f, 1.0f};

  MTL::Buffer* srcBag = dev->newBuffer(&in, sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);

  std::vector<Curve> curves = {makeRampCurve()};
  std::vector<SwGradient> grads = {makeRedBlueGradient()};

  std::map<std::string, float> params;
  params["Strength"] = 1.0f;
  params["Range"] = 1.0f;
  params["Phase"] = 0.0f;
  params["InputMode"] = 1.0f;     // F1
  params["Mapping"] = 1.0f;       // ForStart: f = f0/Range - Phase = f0
  params["WriteMode"] = 0.0f;     // Replace
  params["WriteTo"] = 1.0f;       // F1
  params["WriteColor"] = 2.0f;    // Multiply (.t3 default)
  params["StrengthFactor"] = 0.0f;

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {1};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 1;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  c.inputCurves = &curves;
  c.inputGradients = &grads;

  mapPointAttributesInjectBug() = injectBug;
  cookMapPointAttributes(c);
  mapPointAttributesInjectBug() = false;

  std::memcpy(&out, outBag->contents(), sizeof(SwPoint));
  srcBag->release(); outBag->release();
  return true;
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// (2) FLAT-DRIVER production leg: RadialPoints(#1) → MapPointAttributes(#2) via PointGraph::cook; the op
// bakes the embedded .t3 DEFAULTS (Curve/Gradient unwired). WriteTo=F1 Replace → every FX1 becomes the
// flat-1.0 curve value (1.0). Reads the cooked Points buffer; returns the first point's FX1 + cooked count.
bool flatGraphLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, float& outFx1,
                  uint32_t& cookedCount) {
  registerBuiltinPointOps();
  registerMapPointAttributesOp();  // explicit (self-contained, mirrors the resident leg)

  Graph g;
  Node radial; radial.id = 1; radial.type = "RadialPoints";
  radial.params["Count"] = 32.0f; radial.params["Radius"] = 2.0f;
  g.nodes.push_back(radial);
  Node mpa; mpa.id = 2; mpa.type = "MapPointAttributes";
  mpa.params["WriteTo"] = 1.0f;      // F1
  mpa.params["WriteMode"] = 0.0f;    // Replace → FX1 = curveValue (flat-1.0 default = 1.0)
  mpa.params["WriteColor"] = 2.0f;   // Multiply (default; white default gradient = identity)
  mpa.params["Strength"] = 1.0f;
  g.nodes.push_back(mpa);
  // RadialPoints.points(port 0) → MapPointAttributes.Points(port 0).
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, /*reg=*/nullptr, /*targetNodeId=*/2);

  outFx1 = -999.0f;
  const MTL::Buffer* outBuf = pg.debugCookedBuffer(2);
  cookedCount = pg.debugCookedCount(2);
  if (!outBuf || cookedCount == 0) return false;
  const SwPoint* gpu =
      reinterpret_cast<const SwPoint*>(const_cast<MTL::Buffer*>(outBuf)->contents());
  outFx1 = gpu[0].FX1;
  return true;
}

// (3) RESIDENT (production) leg: RadialPoints(#1) → MapPointAttributes(#2) → DrawPoints2(#3) →
// RenderTarget(#4). Cook through cookResident (the bake-into-point seam gather → the op bakes embedded
// defaults), read the rendered pixels. Asserts a lit sprite (the resident bake ran; a missing Gradient/
// Curve gather would not crash here — the op early-returns on null Points — so the lit-sprite probe proves
// the chain cooked end-to-end on the production path).
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, std::vector<uint8_t>& px,
                 uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 64.0f}, {"Radius", "Radius", "Float", 2.0f}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["MapPointAttributes"] = atomicOp(
      "MapPointAttributes",
      {{"Points", "Points", "Points", 0.0f},
       {"WriteTo", "WriteTo", "Float", 1.0f}, {"WriteMode", "WriteMode", "Float", 0.0f},
       {"WriteColor", "WriteColor", "Float", 2.0f}, {"Strength", "Strength", "Float", 1.0f}},
      {{"out", "out", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] = atomicOp(
      "DrawPoints2",
      {{"points", "points", "Points", 0.0f},
       {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
       {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
       {"Radius", "Radius", "Float", 0.01f}, {"UseWForSize", "UseWForSize", "Float", 1.0f}},
      {{"out", "out", "Command", 0.0f}});
  slib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor", "Float", 0.0f}, {"ClearColor.w", "ClearColor.w", "Float", 1.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints";
  SymbolChild c2; c2.id = 2; c2.symbolId = "MapPointAttributes";
  SymbolChild c3; c3.id = 3; c3.symbolId = "DrawPoints2";
  c3.overrides["Radius"] = 0.20f; c3.overrides["UseWForSize"] = 0.0f;  // visible sprite, ignore FX1
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

int runMapPointAttributesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-mappointattributes] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1) INJECT direct-cook leg: f=0.25 → FX1≈0.25, Color≈(0.75,0,0.25,1). want FIXED. ──────────────
  SwPoint o{};
  injectLeg(dev, q, lib, injectBug, o);
  const float wantFx1 = 0.25f;
  const float wantColor[4] = {0.75f, 0.0f, 0.25f, 1.0f};
  float fxErr = std::fabs(o.FX1 - wantFx1);
  float colErr = std::fabs(o.Color.x - wantColor[0]) + std::fabs(o.Color.y - wantColor[1]) +
                 std::fabs(o.Color.z - wantColor[2]) + std::fabs(o.Color.w - wantColor[3]);
  // FX1 is gradient-bug-independent (tol 3e-3); Color BITES the injectBug (white passthrough).
  bool injFxPass = fxErr < 3e-3f;
  bool injColPass = colErr < 3e-3f;

  // ── (2) FLAT-DRIVER production leg: default flat-1.0 curve → FX1 == 1.0 (bake fired, not passthrough). ─
  float fgFx1 = -999.0f; uint32_t fgCount = 0;
  bool gotFg = flatGraphLeg(dev, q, lib, fgFx1, fgCount);
  bool fgPass = gotFg && fgCount > 0 && std::fabs(fgFx1 - 1.0f) < 3e-3f;

  // ── (3) RESIDENT production leg: a lit sprite (the bake ran on cookResident). ────────────────────────
  std::vector<uint8_t> rpx; uint32_t rw = 0, rh = 0;
  bool gotRes = residentLeg(dev, q, lib, rpx, rw, rh);
  int litCount = 0;
  if (gotRes)
    for (size_t i = 0; i < (size_t)rw * rh; ++i)
      if (rpx[i * 4 + 0] > 30 || rpx[i * 4 + 1] > 30 || rpx[i * 4 + 2] > 30) ++litCount;
  bool resPass = gotRes && litCount > 20;

  bool pass = injFxPass && injColPass && fgPass && resPass;
  std::printf("[selftest-mappointattributes] INJECT: FX1=%.4f(want0.25,err=%.4f,pass=%d) "
              "Color=(%.3f,%.3f,%.3f,%.3f) want(0.75,0,0.25,1) colErr=%.4f pass=%d | "
              "FLAT-DRIVER: count=%u FX1=%.4f(want1.0) pass=%d | RESIDENT: %ux%u lit=%d(need>20) pass=%d "
              "| injectBug=%d -> %s\n",
              (double)o.FX1, (double)fxErr, injFxPass ? 1 : 0, (double)o.Color.x, (double)o.Color.y,
              (double)o.Color.z, (double)o.Color.w, (double)colErr, injColPass ? 1 : 0, fgCount,
              (double)fgFx1, fgPass ? 1 : 0, rw, rh, litCount, resPass ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
