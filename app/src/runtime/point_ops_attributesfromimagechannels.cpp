// AttributesFromImageChannels — a Points op that samples a Texture2D INPUT (the texture-into-points
// seam: PointCookCtx::inputTextures[0]) and ROUTES the sampled R/G/B/Brightness channels — each through
// a per-channel Factor/Offset gain — into a SELECTED point attribute (position xyz / F1 / F2 / rotate
// xyz / scale), scaled by Strength·alpha. Faithful port of external/tixl
// .../point/modify/AttributesFromImageChannels.{cs,hlsl} + its .t3.
//
// SEAM (texture-into-points): identical to SamplePointColorAttributes — the cook driver gathers this op's
// Texture2D input port (cookTexNode -> inputTextures[0]) on BOTH the flat and resident paths. We bind
// inputTextures[0] @ texture(0) + a sampler @ sampler(0); an UNWIRED texture -> passthrough (copy the
// source bag through, mirror point_ops_samplepointcolorattributes.cpp:62-67).
//
// NodeSpec ports 1:1 with AttributesFromImageChannels.cs [Input] (.t3 defaults):
//   GPoints(Points) | Brightness/Red/Green/Blue (int Attributes enum, default 0 NotUsed) | each
//   *Factor(float default 0) + *Offset(float default 0) | Texture(Texture2D) | Center(Vec3 0) |
//   Stretch(Vec2 (1,1)) | Scale(float 1.0) | TextureRotate(Vec3 0) | TextureMode(default Clamp) |
//   RotationSpace(int 1) | TranslationSpace(int 0) | Mode(int 0 Add) | Strength(float 1) |
//   StrengthFactor(int 0 None) | GainAndBias(Vec2 (0.5,0.5)).
//
// transformSampleSpace fold (host half): Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, Scale) ·
// UniformScale(0.5) — the .t3 TransformMatrix UniformScale=0.5 AND Scale default=1.0 (BOTH differ from
// SPCA's 1.0 / 2.0). Aspect = texW/texH (.t3 Div, NaN-guarded). The shader applies qRotateVec3(pos·Scale3,
// R) + the AFIC-own uv scale float2(0.5,-0.5). See attributesfromimagechannels_params.h / .metal.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/attributesfromimagechannels_params.h"  // AficParams, AFIC_* bindings
#include "runtime/compound_graph.h"            // SymbolLibrary / atomicOp (resident leg)
#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/graph.h"                     // Graph/Node/pinId
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"       // buildEvalGraph (resident leg)
#include "runtime/tex_op_cache.h"               // cachedComputePSO
#include "runtime/tixl_point.h"                // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr float kAficUniformScale = 0.5f;  // .t3 TransformMatrix UniformScale (child 1a1e129f)

void cookAttributesFromImageChannels(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input -> nothing to do

  // Unwired Texture2D input (the seam guard): copy the source bag through unchanged. With no texture the
  // sample is (0,0,0,0) -> strength = Strength·0 = 0 -> every factor 0 -> the only mutation is the
  // rotation overwrite to identity; a straight copy is the honest passthrough (what injectBug observes).
  const MTL::Texture* tex = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  if (!tex) {
    std::memcpy(c.output->contents(), const_cast<MTL::Buffer*>(srcBag)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    return;
  }

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "attributesfromimagechannels");
  if (!pso) return;

  AficParams P{};
  P.Count = c.count;
  float center[3] = {0, 0, 0};
  cookVecN(c, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  // transformSampleSpace fold: Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, Scale)·UniformScale.
  float stretch[2] = {1.0f, 1.0f};
  cookVecN(c, "Stretch", stretch, 2, stretch);
  const float scaleU = cookParam(c, "Scale", 1.0f);        // .t3 Scale default 1.0 (SPCA was 2.0)
  const float texW = (float)tex->width(), texH = (float)tex->height();
  const float aspect = (texH != 0.0f) ? (texW / texH) : 1.0f;  // .t3 Div NaN-guard
  P.ScaleX = stretch[0] * aspect * scaleU * kAficUniformScale;
  P.ScaleY = stretch[1] * scaleU * kAficUniformScale;
  P.ScaleZ = scaleU * kAficUniformScale;
  float rot[3] = {0, 0, 0};
  cookVecN(c, "TextureRotate", rot, 3, rot);
  P.RotX = rot[0]; P.RotY = rot[1]; P.RotZ = rot[2];

  // b0 per-channel gains (.cs Brightness/Red/Green/Blue Factor+Offset; .hlsl L/R/G/B Factor/Offset).
  P.LFactor = cookParam(c, "BrightnessFactor", 0.0f);
  P.LOffset = cookParam(c, "BrightnessOffset", 0.0f);
  P.RFactor = cookParam(c, "RedFactor", 0.0f);
  P.ROffset = cookParam(c, "RedOffset", 0.0f);
  P.GFactor = cookParam(c, "GreenFactor", 0.0f);
  P.GOffset = cookParam(c, "GreenOffset", 0.0f);
  P.BFactor = cookParam(c, "BlueFactor", 0.0f);
  P.BOffset = cookParam(c, "BlueOffset", 0.0f);

  P.Strength = cookParam(c, "Strength", 1.0f);
  float gainBias[2] = {0.5f, 0.5f};
  cookVecN(c, "GainAndBias", gainBias, 2, gainBias);  // .t3 default (0.5,0.5) = identity
  P.GainAndBiasX = gainBias[0]; P.GainAndBiasY = gainBias[1];

  // b1 routing ints (each selects an Attribute target 0..12) + Spaces/Mode/StrengthFactor.
  P.L = (int)std::lround(cookParam(c, "Brightness", 0.0f));
  P.R = (int)std::lround(cookParam(c, "Red", 0.0f));
  P.G = (int)std::lround(cookParam(c, "Green", 0.0f));
  P.B = (int)std::lround(cookParam(c, "Blue", 0.0f));
  P.Mode = (int)std::lround(cookParam(c, "Mode", 0.0f));                          // .t3 default 0 (Add)
  P.TranslationSpace = (int)std::lround(cookParam(c, "TranslationSpace", 0.0f));  // .t3 default 0
  P.RotationSpace = (int)std::lround(cookParam(c, "RotationSpace", 1.0f));        // .t3 default 1
  P.StrengthFactor = (int)std::lround(cookParam(c, "StrengthFactor", 0.0f));      // .t3 default 0 (None)

  // Sampler (s0): Nearest filter + wrap from TextureMode (.t3 default Clamp -> ClampToEdge; enum index
  // 0=Wrap,1=Clamp,2=Mirror,3=Border). The registry default is Clamp (index 1) per the .t3.
  const int texMode = (int)std::lround(cookParam(c, "TextureMode", 1.0f));
  MTL::SamplerAddressMode addr;
  switch (texMode) {
    case 0:  addr = MTL::SamplerAddressModeRepeat;             break;  // Wrap
    case 2:  addr = MTL::SamplerAddressModeMirrorRepeat;       break;  // Mirror
    case 3:  addr = MTL::SamplerAddressModeClampToBorderColor; break;  // Border
    default: addr = MTL::SamplerAddressModeClampToEdge;        break;  // 1 Clamp (.t3 default)
  }
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(addr);
  sd->setTAddressMode(addr);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, AFIC_SourcePoints);
  enc->setBuffer(c.output, 0, AFIC_ResultPoints);
  enc->setBytes(&P, sizeof(P), AFIC_Params);
  enc->setTexture(const_cast<MTL::Texture*>(tex), AFIC_InputTexture);  // t1 -> texture(0)
  enc->setSamplerState(samp, AFIC_TexSampler);                         // s0 -> sampler(0)
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  samp->release();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

}  // namespace

void registerAttributesFromImageChannelsOp() {
  registerPointOp("AttributesFromImageChannels", cookAttributesFromImageChannels);
}

// ============================================================================================
// Golden — exercises the attribute ROUTING + gain/bias concretely, on a uniform texture (so the
// uv/transform is coordinate-independent and the routing math is isolated), through THREE legs:
//
//  (1) ROUTING direct-cook leg: a UNIFORM texture c=(0.8,0.4,0.0,1.0). Route Red(=1)->Position_X with
//      RedFactor=0.5/RedOffset=0.1 ; Green(=2)->Position_Y with GreenFactor=2.0/GreenOffset=0.0 ; all
//      others NotUsed. GainAndBias=(0.5,0.5) is IDENTITY (.hlsl bias-functions: GetBias(0.5,x)=x,
//      GetSchlickBias(x,0.5)=x). Strength=1, StrengthFactor=0 -> strength = 1·alpha·1 = 1·1·1 = 1.
//        factors[Position_X] = (rgbl.r·0.5 + 0.1)·1 = 0.8·0.5 + 0.1 = 0.50
//        factors[Position_Y] = (rgbl.g·2.0 + 0.0)·1 = 0.4·2.0       = 0.80
//        p.Position += float3(factors[X],factors[Y],factors[Z])·strength (strength=1) -> +(0.50,0.80,0)
//      Starting Position (0,0,0) -> want (0.50, 0.80, 0.00). FIXED at the true TiXL values. injectBug
//      drops the texture bind -> passthrough -> Position UNCHANGED (0,0,0) -> RED (the routed offset
//      never lands).
//
//  (2) FLAT-DRIVER gather leg (closes the flat-driver test gap): a real flat Graph cooked through
//      PointGraph::cook (production flat path), exercising the flat-driver's Texture2D gather +
//      Points-buffer readback. RadialPoints(#1) -> AFIC(#3) ; RenderTarget(#2, ClearColor a uniform
//      texture) -> AFIC.Texture(#3). With Red->Scale_Uniform and a uniform-1 channel the cooked points'
//      Scale grows; we read AFIC's cooked Points buffer via debugCookedBuffer and assert Scale moved.
//      injectBug OMITS the RenderTarget->AFIC.Texture wire -> the flat gather loses its texture ->
//      passthrough -> Scale unchanged (RadialPoints default) -> RED.
//
//  (3) RESIDENT (production seam) leg: RadialPoints + a UNIFORM CheckerBoard -> AFIC -> DrawPoints2 ->
//      RenderTarget, cooked through cookResident (the seam gather). Route Red->Scale_Uniform with a big
//      RedFactor so the sprites VISIBLY grow; assert the rendered texture lights up a large area.
//      injectBug OMITS the texture wire -> passthrough -> tiny default sprites -> litArea collapses -> RED.
// ============================================================================================

namespace {

constexpr uint32_t kTexW = 8, kTexH = 8;

MTL::Texture* makeUniformTexF(MTL::Device* dev, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
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

// (1) ROUTING direct-cook leg: route R->Position_X / G->Position_Y with non-identity gains, uniform
// texture c=(0.8,0.4,0,1). out[0] receives the single cooked point. wireTexture=false drops the texture.
bool routingLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
                SwPoint& out) {
  SwPoint in{};
  in.Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
  in.Color = SW_FLOAT4{0, 0, 0, 0};
  in.Rotation = SW_FLOAT4{0, 0, 0, 1};
  in.Scale = SW_PACKED3{1.0f, 1.0f, 1.0f};

  MTL::Buffer* srcBag = dev->newBuffer(&in, sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);
  // c = (0.8,0.4,0.0,1.0) -> 204,102,0,255 (0.8·255=204, 0.4·255=102).
  MTL::Texture* tex = makeUniformTexF(dev, 204, 102, 0, 255);

  std::map<std::string, float> params;
  params["Red"] = 1.0f;   params["RedFactor"] = 0.5f;  params["RedOffset"] = 0.1f;  // -> Position_X
  params["Green"] = 2.0f; params["GreenFactor"] = 2.0f; params["GreenOffset"] = 0.0f; // -> Position_Y
  params["Blue"] = 0.0f;  params["Brightness"] = 0.0f;
  params["Strength"] = 1.0f; params["StrengthFactor"] = 0.0f;
  params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;  // identity
  params["Center.x"] = 0.0f; params["Center.y"] = 0.0f; params["Center.z"] = 0.0f;
  params["TextureMode"] = 1.0f;  // Clamp (uniform texture -> uv irrelevant)
  params["TranslationSpace"] = 0.0f; params["RotationSpace"] = 1.0f; params["Mode"] = 0.0f;

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {1};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 1;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (wireTexture) { c.inputTextures[0] = tex; c.inputTextureCount = 1; }
  else { c.inputTextureCount = 0; }
  cookAttributesFromImageChannels(c);

  std::memcpy(&out, outBag->contents(), sizeof(SwPoint));
  srcBag->release(); outBag->release(); tex->release();
  return true;
}

Symbol aficAtomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// (3) RESIDENT leg: RadialPoints + UNIFORM-white CheckerBoard -> AFIC(Red->Scale_Uniform, big factor) ->
// DrawPoints2(UseWForSize -> Scale drives sprite size) -> RenderTarget. cookResident; assert a big lit
// area. injectBug omits the texture wire -> passthrough -> tiny default sprites -> litArea collapses.
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] =
      aficAtomicOp("RadialPoints", {{"Count", "Count", "Float", 64.0f}, {"Radius", "Radius", "Float", 0.3f}},
                   {{"points", "points", "Points", 0.0f}});
  slib.symbols["CheckerBoard"] = aficAtomicOp(
      "CheckerBoard",
      {{"ColorA.r", "ColorA", "Float", 1.0f}, {"ColorA.g", "ColorA.g", "Float", 1.0f},
       {"ColorA.b", "ColorA.b", "Float", 1.0f}, {"ColorA.a", "ColorA.a", "Float", 1.0f},
       {"ColorB.r", "ColorB", "Float", 1.0f}, {"ColorB.g", "ColorB.g", "Float", 1.0f},
       {"ColorB.b", "ColorB.b", "Float", 1.0f}, {"ColorB.a", "ColorB.a", "Float", 1.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  slib.symbols["RenderTarget"] = aficAtomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor", "Float", 0.0f}, {"ClearColor.w", "ClearColor.w", "Float", 1.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  slib.symbols["AttributesFromImageChannels"] = aficAtomicOp(
      "AttributesFromImageChannels",
      {{"GPoints", "GPoints", "Points", 0.0f}, {"Texture", "Texture", "Texture2D", 0.0f},
       {"Red", "Red", "Float", 4.0f},  // 4 = F1 (FX1) — DrawPoints2 UseWForSize uses FX1 for sprite size
       {"RedFactor", "RedFactor", "Float", 8.0f}, {"RedOffset", "RedOffset", "Float", 0.0f},
       {"Strength", "Strength", "Float", 1.0f}},
      {{"out", "out", "Points", 0.0f}});
  slib.symbols["DrawPoints2"] = aficAtomicOp(
      "DrawPoints2",
      {{"points", "points", "Points", 0.0f},
       {"Color.x", "Color", "Float", 1.0f}, {"Color.y", "Color.y", "Float", 1.0f},
       {"Color.z", "Color.z", "Float", 1.0f}, {"Color.w", "Color.w", "Float", 1.0f},
       {"Radius", "Radius", "Float", 0.02f}, {"UseWForSize", "UseWForSize", "Float", 1.0f}},
      {{"out", "out", "Command", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RadialPoints";
  SymbolChild c2; c2.id = 2; c2.symbolId = "CheckerBoard";  // uniform white (ColorA==ColorB)
  c2.overrides["Resolution"] = 0.0f;
  SymbolChild c3; c3.id = 3; c3.symbolId = "AttributesFromImageChannels";
  SymbolChild c4; c4.id = 4; c4.symbolId = "DrawPoints2";
  c4.overrides["Radius"] = 0.08f; c4.overrides["UseWForSize"] = 1.0f;  // FX1 drives sprite size
  // RadialPoints sets FX1=0 -> with UseWForSize the bugged passthrough renders ZERO-size sprites
  // (litArea collapses). The wired path routes Red(=F1) +8 -> FX1=8 -> big sprites -> large litArea.
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
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  px.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

// (2) FLAT-DRIVER gather leg: RadialPoints(#1) -> AFIC(#3); RenderTarget(#2, ClearColor uniform red) ->
// AFIC.Texture(#3). Route Red->Scale_Uniform(9) with RedFactor so Scale grows; cook to AFIC, read its
// cooked output bag via debugCookedBuffer, assert Scale.x moved. injectBug omits the wire -> passthrough.
bool flatGraphLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
                  float outScale[3], float& baseScale, uint32_t& cookedCount) {
  registerBuiltinPointOps();
  registerAttributesFromImageChannelsOp();

  Graph g;
  Node radial; radial.id = 1; radial.type = "RadialPoints";
  radial.params["Count"] = 32.0f; radial.params["Radius"] = 0.3f;
  g.nodes.push_back(radial);
  Node rt; rt.id = 2; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;
  rt.params["CustomW"] = 64.0f; rt.params["CustomH"] = 64.0f;
  rt.params["ClearColor.x"] = 1.0f; rt.params["ClearColor.y"] = 0.0f;
  rt.params["ClearColor.z"] = 0.0f; rt.params["ClearColor.w"] = 1.0f;  // uniform RED (r=1)
  g.nodes.push_back(rt);
  Node afic; afic.id = 3; afic.type = "AttributesFromImageChannels";
  afic.params["Red"] = 9.0f;            // Scale_Uniform
  afic.params["RedFactor"] = 3.0f;      // factor so Scale grows (r=1 -> +3·strength)
  afic.params["Strength"] = 1.0f;
  g.nodes.push_back(afic);

  g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});   // RadialPoints.points -> AFIC.GPoints
  if (wireTexture)
    g.connections.push_back({102, pinId(2, 1), pinId(3, 1)}); // RenderTarget.out -> AFIC.Texture

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, /*reg=*/nullptr, /*targetNodeId=*/3);

  outScale[0] = outScale[1] = outScale[2] = 0.0f; baseScale = 0.0f;
  const MTL::Buffer* outBuf = pg.debugCookedBuffer(3);
  cookedCount = pg.debugCookedCount(3);
  if (!outBuf || cookedCount == 0) return false;
  const SwPoint* gpu =
      reinterpret_cast<const SwPoint*>(const_cast<MTL::Buffer*>(outBuf)->contents());
  outScale[0] = gpu[0].Scale.x; outScale[1] = gpu[0].Scale.y; outScale[2] = gpu[0].Scale.z;
  baseScale = gpu[0].Scale.x;
  return true;
}

}  // namespace

int runAttributesFromImageChannelsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-attributesfromimagechannels] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1) ROUTING direct-cook leg: want Position = (0.50, 0.80, 0.00), FIXED at true TiXL values. ──
  SwPoint r{};
  routingLeg(dev, q, lib, /*wireTexture=*/!injectBug, r);
  const float wantPX = 0.50f, wantPY = 0.80f, wantPZ = 0.00f;
  float routeErr = std::fabs(r.Position.x - wantPX) + std::fabs(r.Position.y - wantPY) +
                   std::fabs(r.Position.z - wantPZ);
  bool routePass = routeErr < 1e-3f;

  // ── (2) FLAT-DRIVER gather leg: Red->Scale_Uniform; cooked Scale should rise above the base. ──
  float fgScale[3]; float fgBase = 0.0f; uint32_t fgCount = 0;
  bool gotFg = flatGraphLeg(dev, q, lib, /*wireTexture=*/!injectBug, fgScale, fgBase, fgCount);
  // RadialPoints Scale default ~1; AFIC Red(=1)->Scale_Uniform += 3·strength -> Scale.x ~ 1+3 = 4.
  // injectBug omits the wire -> passthrough -> Scale.x stays at the RadialPoints default (< 2).
  bool fgPass = gotFg && fgCount > 0 && fgScale[0] > 2.5f;

  // ── (3) RESIDENT (production seam) leg: Red->Scale_Uniform grows the sprites -> a large lit area. ──
  std::vector<uint8_t> px;
  uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, /*wireTexture=*/!injectBug, px, ow, oh);
  int litArea = 0;
  if (gotRes) {
    for (size_t i = 0; i < (size_t)ow * oh; ++i) {
      int R = px[i * 4 + 0], G = px[i * 4 + 1], B = px[i * 4 + 2];
      if (R > 40 || G > 40 || B > 40) ++litArea;  // grown white sprites cover a big area
    }
  }
  bool resPass = gotRes && litArea > 200;  // injectBug -> tiny default sprites -> litArea collapses

  bool pass = routePass && fgPass && resPass;
  std::printf("[selftest-attributesfromimagechannels] ROUTING: pos=(%.3f,%.3f,%.3f) want(0.50,0.80,0.00) "
              "err=%.4f pass=%d | FLAT-DRIVER: count=%u Scale.x=%.2f(need>2.5) pass=%d | RESIDENT: %ux%u "
              "litArea=%d(need>200) pass=%d | injectBug=%d -> %s\n",
              r.Position.x, r.Position.y, r.Position.z, routeErr, routePass ? 1 : 0, fgCount, fgScale[0],
              fgPass ? 1 : 0, ow, oh, litArea, resPass ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
