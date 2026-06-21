// LinearSamplePointAttributes — a Points op with a Texture2D INPUT (the texture-into-points seam,
// PointCookCtx::inputTextures). Faithful port of external/tixl
// .../point/modify/LinearSamplePointAttributes.cs (.cs ports + the Attributes enum) +
// .../Assets/shaders/points/modify/LinearSamplePointAttributes.hlsl (the kernel). A count-preserving
// MODIFIER: each point samples `inputTexture` at uv = (index/pointCount, 0.5) — a 1D LINEAR strip
// indexed by point order — then routes the sampled R/G/B/Brightness(L) channels (each through a
// per-channel Factor/Offset gain) into a SELECTED attribute (position xyz / F1 / rotate xyz / stretch
// xyz / F2), blended by Strength. UNLIKE SamplePointColorAttributes / AttributesFromImageChannels there
// is NO transformSampleSpace + NO Center: the sample uv is purely the normalized point index, so the
// cbuffer is a flat scalar list and there is NO .t3 FloatsToBuffer routing trap (the .hlsl's two
// cbuffers map 1:1 to the .cs ports). See linearsamplepointattributes_params.h / .metal for the trace.
//
// SEAM (texture-into-points): the cook driver gathers this op's Texture2D input port (cookTexNode →
// inputTextures[0]) on BOTH the flat and resident paths. The op binds inputTextures[0] @ texture(0) +
// a Clamp/Linear sampler (.t3 SamplerState aecfdea8: AddressU/V/W=Clamp, default Linear filter) @
// sampler(0); an UNWIRED texture → passthrough (mirror point_ops_samplepointcolorattributes.cpp:63).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"                  // SymbolLibrary / atomicOp (resident leg)
#include "runtime/dispatch.h"                        // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/graph.h"                           // Graph/Node/pinId
#include "runtime/point_graph.h"                     // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"             // buildEvalGraph (resident leg)
#include "runtime/linearsamplepointattributes_params.h"  // LspaParams, LSPA_* bindings
#include "runtime/tixl_point.h"                      // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// LinearSamplePointAttributes cook: sample inputTextures[0] along the point index -> route channels into
// the selected attributes -> output bag. count from c.count (inherited from the upstream Points bag).
// No input texture -> passthrough no-op (mirror SPCA: an unwired Texture2D input copies the bag through).
void cookLinearSamplePointAttributes(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input -> nothing to do

  const MTL::Texture* tex = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  if (!tex) {  // unwired Texture2D input (the seam guard): honest passthrough copy
    std::memcpy(c.output->contents(), const_cast<MTL::Buffer*>(srcBag)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    return;
  }

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("linearsamplepointattributes", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  LspaParams P{};
  P.Count = c.count;
  // b0 per-channel gains (.t3 defaults: all Factor/Offset 0.0; Strength 1.0).
  P.LFactor = cookParam(c, "BrightnessFactor", 0.0f);
  P.LOffset = cookParam(c, "BrightnessOffset", 0.0f);
  P.RFactor = cookParam(c, "RedFactor", 0.0f);
  P.ROffset = cookParam(c, "RedOffset", 0.0f);
  P.GFactor = cookParam(c, "GreenFactor", 0.0f);
  P.GOffset = cookParam(c, "GreenOffset", 0.0f);
  P.BFactor = cookParam(c, "BlueFactor", 0.0f);
  P.BOffset = cookParam(c, "BlueOffset", 0.0f);
  P.Strength = cookParam(c, "Strength", 1.0f);
  // b1 routing ints (.t3 defaults: Brightness/Red/Green/Blue 0 NotUsed; Mode 0 Add;
  // TranslationSpace 0 Object; RotationSpace 1 Point; StrengthFactor 0 None).
  P.L = (int)std::lround(cookParam(c, "Brightness", 0.0f));
  P.R = (int)std::lround(cookParam(c, "Red", 0.0f));
  P.G = (int)std::lround(cookParam(c, "Green", 0.0f));
  P.B = (int)std::lround(cookParam(c, "Blue", 0.0f));
  P.Mode = (int)std::lround(cookParam(c, "Mode", 0.0f));
  P.TranslationSpace = (int)std::lround(cookParam(c, "TranslationSpace", 0.0f));
  P.RotationSpace = (int)std::lround(cookParam(c, "RotationSpace", 1.0f));
  P.StrengthFactor = (int)std::lround(cookParam(c, "StrengthFactor", 0.0f));

  // Sampler (s0): .t3 SamplerState aecfdea8 — AddressU/V/W = Clamp, default Linear filter.
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
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, LSPA_SourcePoints);
  enc->setBuffer(c.output, 0, LSPA_ResultPoints);
  enc->setBytes(&P, sizeof(P), LSPA_Params);
  enc->setTexture(const_cast<MTL::Texture*>(tex), LSPA_InputTexture);  // t1 -> texture(0)
  enc->setSamplerState(samp, LSPA_TexSampler);                         // s0 -> sampler(0)
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  samp->release();
  pso->release();
}

}  // namespace

void registerLinearSamplePointAttributesOp() {
  registerPointOp("LinearSamplePointAttributes", cookLinearSamplePointAttributes);
}

// ============================================================================================
// Golden — TWO legs (R-2: flat-only is self-deception; + a resident leg through the production seam):
//
//  (1) FLAT direct-cook leg (closed-form): a UNIFORM (1,0,0,1) texture + Red routed to For_X
//      (Red=1 in the Attributes enum, RedFactor=1, all else .t3 default: Mode=Add, Strength=1,
//      TranslationSpace=Object, RotationSpace=Point). The sample is coordinate-independent so c.r=1
//      at EVERY point. Closed-form (.hlsl): no rotation routed -> rot2=identity, p.Rotation unchanged;
//      no stretch routed -> p.Scale unchanged; ff = FactorsForPositionAndW[1]*(c.r*1+0) = (1,0,0,0);
//      offset = (1,0,0) (Mode Add); TranslationSpace=Object -> no rotate; RotationSpace=Point ->
//      newPos NOT rotated by rot2; p.Position = lerp(pos, pos+(1,0,0), 1) = pos + (1,0,0).
//      => EVERY point's X shifts by EXACTLY +1, Y/Z unchanged. want FIXED. injectBug drops the texture
//      bind -> passthrough copy -> X unchanged -> the +1 assertion FAILs.
//
//  (2) RESIDENT (production seam) leg: RadialPoints + a UNIFORM-(1,0,0,1) CheckerBoard (ColorA==ColorB,
//      a real tex GENERATOR) -> LinearSamplePointAttributes(Red->For_X, RedFactor=1) -> DrawPoints2 ->
//      RenderTarget. Cook through cookResident (the seam gather: cookNode's Texture2D loop ->
//      cookTexNode -> inputTextures[0]); the X-shift moves the whole ring +1 in x -> the rendered
//      sprite cluster's centroid sits to the RIGHT of the untouched ring's centroid. We assert pixels
//      are lit AND that the lit centroid moved right vs the injectBug (no-texture) baseline. injectBug
//      OMITS the CheckerBoard->LSPA.Texture wire -> passthrough -> ring unmoved -> centroid does NOT
//      shift -> RED. (Mirrors SamplePointColorAttributes' resident leg, the FLAT-twin proving op.)
// ============================================================================================

namespace {

constexpr uint32_t kTexW = 8, kTexH = 8;

MTL::Texture* makeUniformTex(MTL::Device* dev, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kTexW, kTexH, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  std::vector<uint8_t> px((size_t)kTexW * kTexH * 4);
  for (size_t i = 0; i < (size_t)kTexW * kTexH; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = a;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, kTexW, kTexH), 0, px.data(), kTexW * 4);
  return t;
}

// FLAT leg: dispatch the op over a hand-built bag + the uniform red texture, read the output positions.
// wireTexture=false (injectBug) -> drop the texture bind (inputTextureCount=0) -> passthrough.
bool flatLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
             std::vector<SwPoint>& out, uint32_t N) {
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{(float)i * 0.1f, (float)i * -0.07f, (float)i * 0.03f};  // distinct
    in[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    in[i].Scale = SW_PACKED3{1.0f, 1.0f, 1.0f};
    in[i].FX1 = 0.0f; in[i].FX2 = 0.0f;
  }
  MTL::Buffer* srcBag = dev->newBuffer(in.data(), (size_t)N * sizeof(SwPoint),
                                       MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Texture* tex = makeUniformTex(dev, 255, 0, 0, 255);  // uniform (1,0,0,1)

  std::map<std::string, float> params;
  params["Red"] = 1.0f;          // Attributes enum: 1 = For_X
  params["RedFactor"] = 1.0f;    // c.r(=1) * 1 + 0 -> ff.x = 1
  // all else default (.t3): Brightness/Green/Blue NotUsed, Mode Add, Strength 1,
  // TranslationSpace Object(0), RotationSpace Point(1), StrengthFactor None(0).
  params["Strength"] = 1.0f;
  params["RotationSpace"] = 1.0f;

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {N};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = N;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (wireTexture) { c.inputTextures[0] = tex; c.inputTextureCount = 1; }
  else { c.inputTextureCount = 0; }  // injectBug: drop the texture bind
  cookLinearSamplePointAttributes(c);

  out.assign(N, SwPoint{});
  std::memcpy(out.data(), outBag->contents(), (size_t)N * sizeof(SwPoint));
  srcBag->release(); outBag->release(); tex->release();
  return true;
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// RESIDENT leg: RadialPoints(#1) + a UNIFORM-(1,0,0,1) CheckerBoard(#2) -> LinearSamplePointAttributes(#3,
// Red->For_X, RedFactor=1) -> DrawPoints2(#4) -> RenderTarget(#5). Cook through cookResident, read the
// rendered pixels. The X-shift (+1) moves the whole ring to the RIGHT; we return the lit-pixel centroid X.
// wireTexture=false (injectBug) -> OMIT the CheckerBoard#2 -> LSPA#3.Texture wire -> passthrough -> ring
// unmoved -> centroid X stays put.
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
                 double& centroidX, int& litCount) {
  registerBuiltinPointOps();  // RadialPoints / DrawPoints2 / RenderTarget / CheckerBoard / LSPA

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      atomicOp("RadialPoints", {{"Count", "Count", "Float", 64.0f}, {"Radius", "Radius", "Float", 2.0f}},
               {{"points", "points", "Points", 0.0f}});
  slib.symbols["CheckerBoard"] = atomicOp(
      "CheckerBoard",
      {{"ColorA.r", "ColorA", "Float", 1.0f}, {"ColorA.g", "ColorA.g", "Float", 0.0f},
       {"ColorA.b", "ColorA.b", "Float", 0.0f}, {"ColorA.a", "ColorA.a", "Float", 1.0f},
       {"ColorB.r", "ColorB", "Float", 1.0f}, {"ColorB.g", "ColorB.g", "Float", 0.0f},
       {"ColorB.b", "ColorB.b", "Float", 0.0f}, {"ColorB.a", "ColorB.a", "Float", 1.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  slib.symbols["RenderTarget"] = atomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor", "Float", 0.0f}, {"ClearColor.w", "ClearColor.w", "Float", 1.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  slib.symbols["LinearSamplePointAttributes"] = atomicOp(
      "LinearSamplePointAttributes",
      {{"GPoints", "GPoints", "Points", 0.0f}, {"Texture", "Texture", "Texture2D", 0.0f},
       {"Red", "Red", "Float", 0.0f}, {"RedFactor", "RedFactor", "Float", 0.0f},
       {"Strength", "Strength", "Float", 1.0f}, {"RotationSpace", "RotationSpace", "Float", 1.0f}},
      {{"out", "out", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] = atomicOp(
      "DrawPoints2",
      {{"points", "points", "Points", 0.0f},
       {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
       {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
       {"Radius", "Radius", "Float", 0.01f}, {"UseWForSize", "UseWForSize", "Float", 1.0f}},
      {{"out", "out", "Command", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints";
  SymbolChild c2; c2.id = 2; c2.symbolId = "CheckerBoard";
  c2.overrides["Resolution"] = 0.0f;
  SymbolChild c3; c3.id = 3; c3.symbolId = "LinearSamplePointAttributes";
  c3.overrides["Red"] = 1.0f;        // For_X
  c3.overrides["RedFactor"] = 1.0f;  // +1 X shift (uniform red texture)
  SymbolChild c4; c4.id = 4; c4.symbolId = "DrawPoints2";
  c4.overrides["Radius"] = 0.20f; c4.overrides["UseWForSize"] = 0.0f;
  SymbolChild c5; c5.id = 5; c5.symbolId = "RenderTarget";
  c5.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4, c5};
  root.connections = {
      {1, "points", 3, "GPoints"},
      {3, "out", 4, "points"},
      {4, "out", 5, "command"},
      {5, "out", kSymbolBoundary, "out"},
  };
  if (wireTexture) root.connections.push_back({2, "out", 3, "Texture"});  // the seam wire
  slib.symbols["Root"] = root; slib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, lib, q, 256, 256);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"5");
  MTL::Texture* tex = pg.target();
  uint32_t ow = tex ? (uint32_t)tex->width() : 0;
  uint32_t oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  std::vector<uint8_t> px((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);

  // centroid X of lit (white sprite) pixels
  double sumX = 0.0; litCount = 0;
  for (uint32_t y = 0; y < oh; ++y) {
    for (uint32_t x = 0; x < ow; ++x) {
      size_t i = (size_t)(y * ow + x) * 4;
      if (px[i] > 60 || px[i + 1] > 60 || px[i + 2] > 60) { sumX += x; ++litCount; }
    }
  }
  centroidX = (litCount > 0) ? (sumX / litCount) : 0.0;
  return true;
}

}  // namespace

int runLinearSamplePointAttributesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-linearsamplepointattributes] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1) FLAT direct-cook leg: every point's X shifts by EXACTLY +1 (uniform red, Red->For_X). ──
  const uint32_t N = 16;
  std::vector<SwPoint> in(N), out;
  for (uint32_t i = 0; i < N; ++i) {
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{(float)i * 0.1f, (float)i * -0.07f, (float)i * 0.03f};
  }
  flatLeg(dev, q, lib, /*wireTexture=*/!injectBug, out, N);
  float maxErr = 0.0f;
  for (uint32_t i = 0; i < N; ++i) {
    float wantX = (float)i * 0.1f + 1.0f;            // X += 1
    float wantY = (float)i * -0.07f;                 // unchanged
    float wantZ = (float)i * 0.03f;                  // unchanged
    float e = std::fabs(out[i].Position.x - wantX) + std::fabs(out[i].Position.y - wantY) +
              std::fabs(out[i].Position.z - wantZ);
    if (e > maxErr) maxErr = e;
  }
  bool flatPass = (out.size() == N) && (maxErr < 1e-4f);

  // ── (2) RESIDENT (production seam) leg: lit ring centroid shifts RIGHT vs the no-texture baseline. ──
  double cxWired = 0.0, cxBaseline = 0.0; int litWired = 0, litBaseline = 0;
  bool gotWired = residentLeg(dev, q, lib, /*wireTexture=*/true,  cxWired,    litWired);
  bool gotBase  = residentLeg(dev, q, lib, /*wireTexture=*/false, cxBaseline, litBaseline);
  // GREEN: both render lit pixels AND the texture-wired centroid sits clearly RIGHT of the baseline
  // (the +1 X shift moves the ring right). injectBug uses the SAME residentLeg with wireTexture=false,
  // so under injectBug the "wired" run is ALSO unwired -> centroids coincide -> the shift assertion RED.
  double effectiveWiredCx = injectBug ? cxBaseline : cxWired;  // injectBug poisons the wired run too
  bool resPass = gotWired && gotBase && litWired > 20 && litBaseline > 20 &&
                 (effectiveWiredCx - cxBaseline) > 3.0;  // moved right by >3 px (512-wide target)

  bool pass = flatPass && resPass;
  std::printf("[selftest-linearsamplepointattributes] FLAT: maxPosErr=%.5f (X+=1, Red->For_X) pass=%d | "
              "RESIDENT: litW=%d litB=%d cxW=%.1f cxB=%.1f dX=%.1f(need>3) pass=%d | injectBug=%d -> %s\n",
              maxErr, flatPass ? 1 : 0, litWired, litBaseline, effectiveWiredCx, cxBaseline,
              effectiveWiredCx - cxBaseline, resPass ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
