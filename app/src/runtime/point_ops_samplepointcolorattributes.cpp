// SamplePointColorAttributes — the FIRST Points op with a Texture2D INPUT, and the proving op for the
// texture-into-points seam (PointCookCtx::inputTextures). Faithful port of external/tixl
// .../point/modify/SamplePointColorAttributes.cs (.cs ports) +
// .../Assets/shaders/points/modify/SamplePointColorAttributes.hlsl (the kernel). A count-preserving
// MODIFIER: each point samples `inputTexture` at uv = (pos-Center).xy*(1,-1)+0.5, multiplies by
// BaseColor, and BLENDS the result into its own Color via BlendColors(Mode). Position UNTOUCHED.
//
// SEAM (texture-into-points): the cook driver gathers this op's Texture2D input port (cookTexNode →
// inputTextures[0]) on BOTH the flat and resident paths. The op binds inputTextures[0] @ texture(0)
// + a sampler @ sampler(0); an UNWIRED texture → passthrough (mirror point_ops_crop.cpp:55).
//
// NodeSpec ports 1:1 with SamplePointColorAttributes.cs [Input] (invent NO knobs; .t3 defaults):
//   GPoints(Points) | Texture(Texture2D) | BaseColor(Vec4 default (1,1,1,1)) | BlendMode(int enum,
//   default 0 Normal) | Center(Vec3 default 0) | Stretch(Vec2 default (1,1)) | Scale(float default 2)
//   | TextureRotate(Vec3 default 0) | TextureMode(TextureAddressMode default Wrap) | Visibility(dead).
//
// transformSampleSpace is composed FAITHFULLY now (no more identity fork): the host folds the .t3
// Scale chain into Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, Scale) with Aspect = texW/texH
// (the .t3 Div/GetTextureSize correction, a no-op for square textures), and passes Scale3 + the
// TextureRotate Euler; the shader applies mul(float4(pos,0), M) == qRotateVec3(pos·Scale3, R). The
// sampler is Repeat-wrap + Nearest (.t3 TextureMode default Wrap / SamplerState Filter=MinMagMipPoint),
// with TextureMode driving the U/V wrap. See samplepointcolorattributes_params.h / .metal for the full
// .t3 trace (TransformMatrix child 23b4b95e / ScaleVector3 468d48a7 / SamplerState e96c13da).
#include "runtime/point_ops.h"
#include "runtime/tex_op_cache.h"

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
#include "runtime/graph.h"                     // Graph/Node/pinId
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"       // buildEvalGraph (resident leg)
#include "runtime/samplepointcolorattributes_params.h"  // SpcaParams, SPCA_* bindings
#include "runtime/tixl_point.h"                // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// SamplePointColorAttributes cook: sample inputTextures[0] per point -> blend into Color -> output bag.
// count from c.count (inherited from the upstream Points bag). No input texture -> passthrough no-op
// (mirror point_ops_crop.cpp:55: an unwired Texture2D input copies the bag through unchanged).
void cookSamplePointColorAttributes(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired Points input -> nothing to do

  // Unwired Texture2D input (the seam guard): copy the source bag through unchanged. The sample would
  // be (0,0,0,0); BlendColors Normal with a transparent texel leaves Color untouched — but a straight
  // copy is the honest passthrough (and is what the golden's injectBug observes).
  const MTL::Texture* tex = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  if (!tex) {
    std::memcpy(c.output->contents(), const_cast<MTL::Buffer*>(srcBag)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    return;
  }

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "samplepointcolorattributes");
  if (!pso) return;

  SpcaParams P{};
  P.Count = c.count;
  float center[3] = {0, 0, 0};
  cookVecN(c, "Center", center, 3, center);  // Vector3 -> Center.x/.y/.z
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];
  P.Mode = std::round(cookParam(c, "BlendMode", 0.0f));  // RgbBlendMode index (0 = Normal)
  // BaseColor is a Vec4 COLOR (port ids .r/.g/.b/.a, like Crop's PaddingColor) — read each component
  // (mapVecN uses .x/.y/.z/.w suffixes, so a Vec4 color must be read per-channel by its .r/.g/.b/.a id).
  P.BaseColorR = cookParam(c, "BaseColor.r", 1.0f);
  P.BaseColorG = cookParam(c, "BaseColor.g", 1.0f);
  P.BaseColorB = cookParam(c, "BaseColor.b", 1.0f);
  P.BaseColorA = cookParam(c, "BaseColor.a", 1.0f);

  // transformSampleSpace (host half): fold the .t3 Scale chain into Scale3 and pass the TextureRotate
  // Euler. Scale3 = (Stretch.x·Aspect·Scale, Stretch.y·Scale, Scale); Aspect = texW/texH (.t3 Div over
  // GetTextureSize) is a one-axis (X) correction, a NO-OP for a square texture. The shader applies the
  // rotation (Y·X·Z) + scale via qRotateVec3(pos·Scale3, R) (w=0 mul drops the translation).
  float stretch[2] = {1.0f, 1.0f};
  cookVecN(c, "Stretch", stretch, 2, stretch);            // Vector2 -> Stretch.x/.y
  const float scaleU = cookParam(c, "Scale", 2.0f);        // .t3 ScaleUniform, default 2.0
  const float texW = (float)tex->width(), texH = (float)tex->height();
  const float aspect = (texH != 0.0f) ? (texW / texH) : 1.0f;  // .t3 Div: NaN-guard (B==0 -> 1, no warp)
  P.ScaleX = stretch[0] * aspect * scaleU;
  P.ScaleY = stretch[1] * scaleU;
  P.ScaleZ = scaleU;
  float rot[3] = {0, 0, 0};
  cookVecN(c, "TextureRotate", rot, 3, rot);              // Vector3 Euler degrees -> RotX/.y/.z
  P.RotX = rot[0]; P.RotY = rot[1]; P.RotZ = rot[2];

  // Sampler (s0): Nearest filter (.t3 SamplerState Filter=MinMagMipPoint) + wrap from TextureMode (the
  // NodeSpec enum index: 0=Wrap, 1=Clamp, 2=Mirror, 3=Border; .t3 default Wrap → Repeat).
  const int texMode = (int)std::lround(cookParam(c, "TextureMode", 0.0f));
  MTL::SamplerAddressMode addr;
  switch (texMode) {
    case 1:  addr = MTL::SamplerAddressModeClampToEdge;        break;  // Clamp
    case 2:  addr = MTL::SamplerAddressModeMirrorRepeat;       break;  // Mirror
    case 3:  addr = MTL::SamplerAddressModeClampToBorderColor; break;  // Border
    default: addr = MTL::SamplerAddressModeRepeat;             break;  // 0 Wrap (.t3 default)
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
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SPCA_SourcePoints);
  enc->setBuffer(c.output, 0, SPCA_ResultPoints);
  enc->setBytes(&P, sizeof(P), SPCA_Params);
  enc->setTexture(const_cast<MTL::Texture*>(tex), SPCA_InputTexture);  // t1 -> texture(0)
  enc->setSamplerState(samp, SPCA_TexSampler);                         // s0 -> sampler(0)
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

void registerSamplePointColorAttributesOp() {
  registerPointOp("SamplePointColorAttributes", cookSamplePointColorAttributes);
}

// ============================================================================================
// Golden — FOUR legs (R-2: flat-only is self-deception; + the matrix/sampler fix needs a non-uniform
// texture to BITE, + the flat-DRIVER Texture2D gather must be exercised, not just a hand-built ctx).
//
//  (1) UNIFORM direct-cook leg: hand-built ctx + a uniform (1,0,0,1) texture, byte-read the output
//      Color. closed-form: sample c=(1,0,0,1) at EVERY uv (coordinate-independent), BlendColors Normal
//      with tA=(0,0,0,0) -> a=1, rgb=(1,0,0) -> Color=(1,0,0,1). want FIXED. injectBug drops the
//      texture bind -> passthrough -> (0,0,0,0) -> RED.
//
//  (2) NON-UNIFORM direct-cook leg (the test that BITES the transformSampleSpace + Repeat + Nearest
//      fix). A 2×2 four-distinct-texel texture (top-left RED, top-right GREEN, bottom-left BLUE,
//      bottom-right YELLOW) + DEFAULT params (Scale=2, Stretch=1, TextureRotate=0, Center=0). Square
//      texture -> Aspect=1 -> Scale3=(2,2,2). For pos=(px,py,0):
//        posInObject = pos·(2,2,2) (rotation identity) ; uv = (px·2 + 0.5, -py·2 + 0.5)
//        Repeat wrap maps uv into [0,1); Nearest picks the texel: u<0.5 col0/u>=0.5 col1,
//        v<0.5 row0(top)/v>=0.5 row1(bottom).
//      Point A = (0.375, -0.125, 0): u = 0.375·2+0.5 = 1.25 -wrap-> 0.25 (col0); v = 0.125·2+0.5 = 0.75
//        (row1) => texel(0,1) = BLUE (0,0,1,1).  [identity fork: u=0.875(col1), v=0.625(row1)=YELLOW≠BLUE]
//      Point B = (0.375,  0.125, 0): u = 1.25 -wrap-> 0.25 (col0); v = -0.125·2+0.5 = 0.25 (row0)
//        => texel(0,0) = RED (1,0,0,1).        [identity fork: u=0.875(col1), v=0.375(row0)=GREEN≠RED]
//      Both correct uvs land DEAD-CENTER of their texel (0.25/0.75) so Nearest==Linear there (the
//      probe doesn't ride a filter boundary); the col0 result requires the Scale=2 Repeat WRAP (1.25
//      ->0.25) — the old identity+ClampToEdge impl stays in col1 -> wrong texel -> this golden FAILS
//      on identity, PASSES after the fix. want = {BLUE, RED}, FIXED at true TiXL values. injectBug
//      drops the texture bind -> Color=(0,0,0,0) -> RED (bites here too).
//
//  (3) FLAT-DRIVER gather leg (closes the flat-driver test gap): the legs above hand-build PointCookCtx
//      and call the cook directly, BYPASSING PointGraph::cook — so the flat-driver's Texture2D gather
//      (point_graph.cpp:447-458) was untested. This leg drives a real flat Graph
//      RadialPoints(#1) -> SamplePointColorAttributes(#3), RenderTarget(#2, ClearColor red) -> SPCA.Texture,
//      through PointGraph::cook (target = SPCA), then reads SPCA's cooked Points buffer back via
//      debugCookedBuffer -> asserts Color went RED (the flat gather threaded the RenderTarget texture
//      into inputTextures[0]). injectBug OMITS the RenderTarget->SPCA.Texture wire -> the flat gather
//      loses its texture -> passthrough -> the WHITE RadialPoints color survives (not red) -> RED.
//      (Mirrors how the resident leg drives cookResident; this is the FLAT twin of that production path.)
//
//  (4) RESIDENT (production) leg: RadialPoints + a uniform-(1,0,0,1) CheckerBoard -> SPCA -> DrawPoints2
//      -> RenderTarget, cook through cookResident (the seam gather: cookNode's Texture2D loop ->
//      cookTexNode -> inputTextures[0]), read the rendered pixels -> assert a RED sprite. injectBug OMITS
//      the texture wire -> passthrough -> white RadialPoints color renders (not red) -> RED.
// ============================================================================================

namespace {

constexpr uint32_t kTexW = 8, kTexH = 8;  // small uniform solid (coordinate-independent)

// A uniform RGBA8 texture of color (r,g,b,a) in 0..255. ShaderRead for sampling.
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

// FLAT leg: dispatch the op over a hand-built bag (all Color=0) + the uniform red texture, byte-read
// the output Color. wireTexture=false (injectBug) -> drop the texture bind (inputTextureCount=0).
bool flatLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
             SwPoint* outFirst, uint32_t N) {
  std::vector<SwPoint> in(N);
  for (uint32_t i = 0; i < N; ++i) {
    in[i] = SwPoint{};
    in[i].Position = SW_PACKED3{(float)i * 0.01f, (float)i * -0.013f, 0.0f};  // varied (uv varies)
    in[i].Color = SW_FLOAT4{0.0f, 0.0f, 0.0f, 0.0f};                          // start transparent black
    in[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  }
  MTL::Buffer* srcBag = dev->newBuffer(in.data(), (size_t)N * sizeof(SwPoint),
                                       MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Texture* tex = makeUniformTex(dev, 255, 0, 0, 255);  // uniform (1,0,0,1)

  std::map<std::string, float> params;
  params["BlendMode"] = 0.0f;  // Normal
  params["BaseColor.r"] = 1.0f; params["BaseColor.g"] = 1.0f;
  params["BaseColor.b"] = 1.0f; params["BaseColor.a"] = 1.0f;
  params["Center.x"] = 0.0f; params["Center.y"] = 0.0f; params["Center.z"] = 0.0f;

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {N};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = N;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (wireTexture) { c.inputTextures[0] = tex; c.inputTextureCount = 1; }
  else { c.inputTextureCount = 0; }  // injectBug: drop the texture bind
  cookSamplePointColorAttributes(c);

  std::vector<SwPoint> out(N);
  std::memcpy(out.data(), outBag->contents(), (size_t)N * sizeof(SwPoint));
  std::memcpy(outFirst, out.data(), (size_t)N * sizeof(SwPoint));

  srcBag->release(); outBag->release(); tex->release();
  return true;
}

// A 2×2 RGBA8 texture with FOUR distinct texels:
//   (col0,row0)=top-left RED  (col1,row0)=top-right GREEN
//   (col0,row1)=bot-left BLUE (col1,row1)=bot-right YELLOW
// Row 0 is the TOP (smaller v). replaceRegion row order = top-to-bottom (row 0 first).
MTL::Texture* makeFourTexelTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 2, 2, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  // 2 rows × 2 cols × 4 bytes. row0 (top): RED, GREEN ; row1 (bottom): BLUE, YELLOW.
  uint8_t px[16] = {
      255, 0,   0,   255,   0,   255, 0,   255,  // row0: (0,0)=red   (1,0)=green
      0,   0,   255, 255,   255, 255, 0,   255,  // row1: (0,1)=blue  (1,1)=yellow
  };
  t->replaceRegion(MTL::Region::Make2D(0, 0, 2, 2), 0, px, 2 * 4);
  return t;
}

// NON-UNIFORM direct-cook leg: the 2×2 texture + DEFAULT params (Scale=2/Stretch=1/Rotate=0/Center=0),
// two hand-computed points (A -> BLUE, B -> RED; see the golden header for the uv arithmetic). want is
// FIXED at the true TiXL texels. wireTexture=false (injectBug) drops the texture bind -> Color=(0,0,0,0).
// Returns the two output points in out[0]=A, out[1]=B.
bool nonUniformLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
                   SwPoint out[2]) {
  SwPoint in[2];
  in[0] = SwPoint{}; in[0].Position = SW_PACKED3{0.375f, -0.125f, 0.0f};  // A -> uv(0.25,0.75) BLUE
  in[0].Color = SW_FLOAT4{0, 0, 0, 0}; in[0].Rotation = SW_FLOAT4{0, 0, 0, 1};
  in[1] = SwPoint{}; in[1].Position = SW_PACKED3{0.375f, 0.125f, 0.0f};   // B -> uv(0.25,0.25) RED
  in[1].Color = SW_FLOAT4{0, 0, 0, 0}; in[1].Rotation = SW_FLOAT4{0, 0, 0, 1};

  MTL::Buffer* srcBag = dev->newBuffer(in, 2 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(2 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Texture* tex = makeFourTexelTex(dev);

  std::map<std::string, float> params;  // all DEFAULT (Scale=2, Stretch=1, TextureRotate=0, Wrap)
  params["BlendMode"] = 0.0f;
  params["BaseColor.r"] = 1.0f; params["BaseColor.g"] = 1.0f;
  params["BaseColor.b"] = 1.0f; params["BaseColor.a"] = 1.0f;
  params["Center.x"] = 0.0f; params["Center.y"] = 0.0f; params["Center.z"] = 0.0f;
  params["Stretch.x"] = 1.0f; params["Stretch.y"] = 1.0f;
  params["Scale"] = 2.0f;  // .t3 default
  params["TextureRotate.x"] = 0.0f; params["TextureRotate.y"] = 0.0f; params["TextureRotate.z"] = 0.0f;
  params["TextureMode"] = 0.0f;  // Wrap -> Repeat

  const MTL::Buffer* ins[1] = {srcBag};
  uint32_t insCounts[1] = {2};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 2;
  c.inputs = ins; c.inputCounts = insCounts; c.inputCount = 1;
  c.output = outBag; c.params = &params;
  if (wireTexture) { c.inputTextures[0] = tex; c.inputTextureCount = 1; }
  else { c.inputTextureCount = 0; }  // injectBug: drop the texture bind
  cookSamplePointColorAttributes(c);

  std::memcpy(out, outBag->contents(), 2 * sizeof(SwPoint));
  srcBag->release(); outBag->release(); tex->release();
  return true;
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// RESIDENT leg: RadialPoints(#1) + a UNIFORM-(1,0,0,1) CheckerBoard(#2) -> SamplePointColorAttributes(#3)
// -> DrawPoints2(#4) -> RenderTarget(#5, the REAL executor — no override). Cook through cookResident,
// read the rendered pixels. CheckerBoard with ColorA==ColorB==(1,0,0,1) is a registered tex GENERATOR
// producing a uniform red texture (same uniform-input trick the RemapColor golden uses), so the texture
// source is a real op and the SINK RenderTarget runs its real point-render executor unmodified.
// wireTexture=false (injectBug) -> OMIT the CheckerBoard#2 -> SPCA#3.Texture wire (the seam loses its
// texture -> passthrough -> the white RadialPoints color renders, not red).
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();  // RadialPoints / DrawPoints2 / RenderTarget / CheckerBoard / SPCA

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
  slib.symbols["SamplePointColorAttributes"] = atomicOp(
      "SamplePointColorAttributes",
      {{"GPoints", "GPoints", "Points", 0.0f}, {"Texture", "Texture", "Texture2D", 0.0f},
       {"BaseColor.r", "BaseColor", "Float", 1.0f}, {"BaseColor.g", "BaseColor.g", "Float", 1.0f},
       {"BaseColor.b", "BaseColor.b", "Float", 1.0f}, {"BaseColor.a", "BaseColor.a", "Float", 1.0f},
       {"BlendMode", "BlendMode", "Float", 0.0f},
       {"Center.x", "Center", "Float", 0.0f}, {"Center.y", "Center.y", "Float", 0.0f},
       {"Center.z", "Center.z", "Float", 0.0f}},
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
  SymbolChild c2; c2.id = 2; c2.symbolId = "CheckerBoard";  // uniform red (ColorA==ColorB)
  c2.overrides["Resolution"] = 0.0f;
  SymbolChild c3; c3.id = 3; c3.symbolId = "SamplePointColorAttributes";
  SymbolChild c4; c4.id = 4; c4.symbolId = "DrawPoints2";
  c4.overrides["Radius"] = 0.20f; c4.overrides["UseWForSize"] = 0.0f;  // visible sprite, ignore FX1
  SymbolChild c5; c5.id = 5; c5.symbolId = "RenderTarget";
  c5.overrides["Resolution"] = 0.0f;  // SINK: the real executor renders the point command
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

// FLAT-DRIVER gather leg (closes the flat-driver test gap): a real flat Graph cooked through
// PointGraph::cook (the production flat path), exercising the flat-driver's Texture2D gather
// (point_graph.cpp:447-458) + the Points-buffer readback. RadialPoints(#1) -> SPCA(#3); RenderTarget(#2,
// ClearColor RED, no command -> a uniform red texture via LoadActionClear) -> SPCA.Texture(#3). Cook to
// SPCA (#3), read its cooked output bag via debugCookedBuffer -> assert every Color went RED (the flat
// gather threaded the RenderTarget's Texture2D into inputTextures[0] and the op sampled it). wireTexture
// =false (injectBug) OMITS the RenderTarget->SPCA.Texture wire -> the flat gather loses its texture ->
// passthrough -> the WHITE RadialPoints color (radial_points.metal:48) survives -> not red -> RED.
// Returns the cooked Color of the first point in `outColor` (NaN-safe: all-zero if readback failed).
bool flatGraphLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireTexture,
                  float outColor[4], uint32_t& cookedCount) {
  registerBuiltinPointOps();                 // RadialPoints / RenderTarget / SPCA NodeSpecs + cook fns
  registerSamplePointColorAttributesOp();    // explicit (self-contained, mirrors the resident leg)

  Graph g;
  Node radial; radial.id = 1; radial.type = "RadialPoints";
  radial.params["Count"] = 32.0f; radial.params["Radius"] = 2.0f;
  g.nodes.push_back(radial);
  Node rt; rt.id = 2; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;            // Custom -> CustomW/H (a fixed, window-independent size)
  rt.params["CustomW"] = 64.0f; rt.params["CustomH"] = 64.0f;
  rt.params["ClearColor.x"] = 1.0f; rt.params["ClearColor.y"] = 0.0f;
  rt.params["ClearColor.z"] = 0.0f; rt.params["ClearColor.w"] = 1.0f;  // uniform RED
  g.nodes.push_back(rt);
  Node spca; spca.id = 3; spca.type = "SamplePointColorAttributes"; g.nodes.push_back(spca);

  // RadialPoints.points(port 0) -> SPCA.GPoints(port 0).
  g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});
  // RenderTarget.out(Texture2D, port 1) -> SPCA.Texture(port 1). injectBug DROPS this -> no texture.
  if (wireTexture)
    g.connections.push_back({102, pinId(2, 1), pinId(3, 1)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, /*reg=*/nullptr, /*targetNodeId=*/3);  // cook the SPCA node itself (Points producer)

  outColor[0] = outColor[1] = outColor[2] = outColor[3] = 0.0f;
  const MTL::Buffer* outBuf = pg.debugCookedBuffer(3);
  cookedCount = pg.debugCookedCount(3);
  if (!outBuf || cookedCount == 0) return false;
  const SwPoint* gpu =
      reinterpret_cast<const SwPoint*>(const_cast<MTL::Buffer*>(outBuf)->contents());
  outColor[0] = gpu[0].Color.x; outColor[1] = gpu[0].Color.y;
  outColor[2] = gpu[0].Color.z; outColor[3] = gpu[0].Color.w;
  return true;
}

}  // namespace

int runSamplePointColorAttributesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-samplepointcolorattributes] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1) UNIFORM direct-cook leg: byte-read the output Color. want FIXED at (1,0,0,1). ───────────
  const uint32_t N = 16;
  std::vector<SwPoint> outPts(N);
  flatLeg(dev, q, lib, /*wireTexture=*/!injectBug, outPts.data(), N);
  const float wantR = 1.0f, wantG = 0.0f, wantB = 0.0f, wantA = 1.0f;  // closed-form (Normal blend)
  bool flatPass = true;
  float maxErr = 0.0f;
  for (uint32_t i = 0; i < N; ++i) {
    const SwPoint& p = outPts[i];
    float e = std::fabs(p.Color.x - wantR) + std::fabs(p.Color.y - wantG) +
              std::fabs(p.Color.z - wantB) + std::fabs(p.Color.w - wantA);
    if (e > maxErr) maxErr = e;
    if (e > 1e-4f) flatPass = false;
  }

  // ── (2) NON-UNIFORM direct-cook leg: bites transformSampleSpace + Repeat + Nearest. ─────────────
  //   want A = BLUE (0,0,1,1), want B = RED (1,0,0,1) — FIXED true TiXL texels (see golden header).
  SwPoint nu[2];
  nonUniformLeg(dev, q, lib, /*wireTexture=*/!injectBug, nu);
  const float wantA4[4] = {0.0f, 0.0f, 1.0f, 1.0f};  // A -> texel(0,1) BLUE
  const float wantB4[4] = {1.0f, 0.0f, 0.0f, 1.0f};  // B -> texel(0,0) RED
  auto colErr = [](const SwPoint& p, const float w[4]) {
    return std::fabs(p.Color.x - w[0]) + std::fabs(p.Color.y - w[1]) +
           std::fabs(p.Color.z - w[2]) + std::fabs(p.Color.w - w[3]);
  };
  float nuErrA = colErr(nu[0], wantA4), nuErrB = colErr(nu[1], wantB4);
  bool nuPass = (nuErrA < 1e-4f) && (nuErrB < 1e-4f);

  // ── (3) FLAT-DRIVER gather leg: drive PointGraph::cook + read SPCA's Points buffer (gap closure). ─
  float fgColor[4]; uint32_t fgCount = 0;
  bool gotFg = flatGraphLeg(dev, q, lib, /*wireTexture=*/!injectBug, fgColor, fgCount);
  // GREEN: the flat gather threaded the RED RenderTarget texture -> Color RED. injectBug omits the
  // wire -> white RadialPoints color survives (R≈G≈B≈1) -> not red-dominant -> RED.
  bool fgPass = gotFg && fgCount > 0 && fgColor[0] > 0.6f && fgColor[1] < 0.4f && fgColor[2] < 0.4f;

  // ── (4) RESIDENT (production seam) leg: red sprite on the rendered texture. ──────────────────────
  std::vector<uint8_t> px;
  uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, /*wireTexture=*/!injectBug, px, ow, oh);
  int redLit = 0, anyLit = 0;
  if (gotRes) {
    for (size_t i = 0; i < (size_t)ow * oh; ++i) {
      int R = px[i * 4 + 0], G = px[i * 4 + 1], B = px[i * 4 + 2];
      if (R > 30 || G > 30 || B > 30) ++anyLit;
      if (R > 120 && G < 60 && B < 60) ++redLit;  // red-dominant teeth-guard (texture color flowed)
    }
  }
  // GREEN: a real RED sprite area. injectBug -> passthrough -> white RadialPoints color renders ->
  // redLit collapses (the sprite is white/not-red) -> RED.
  bool resPass = gotRes && redLit > 20;

  bool pass = flatPass && nuPass && fgPass && resPass;
  std::printf("[selftest-samplepointcolorattributes] UNIFORM: maxColorErr=%.5f want(1,0,0,1) pass=%d | "
              "NONUNIF: errA=%.4f(BLUE) errB=%.4f(RED) pass=%d | FLAT-DRIVER: count=%u "
              "color=(%.2f,%.2f,%.2f,%.2f) pass=%d | RESIDENT: %ux%u redLit=%d(need>20) pass=%d | "
              "injectBug=%d -> %s\n",
              maxErr, flatPass ? 1 : 0, nuErrA, nuErrB, nuPass ? 1 : 0, fgCount, fgColor[0], fgColor[1],
              fgColor[2], fgColor[3], fgPass ? 1 : 0, ow, oh, redLit, resPass ? 1 : 0, injectBug ? 1 : 0,
              pass ? "PASS" : "FAIL");
  std::printf("  nonuniform A.Color=(%.2f,%.2f,%.2f,%.2f) B.Color=(%.2f,%.2f,%.2f,%.2f)\n",
              nu[0].Color.x, nu[0].Color.y, nu[0].Color.z, nu[0].Color.w,
              nu[1].Color.x, nu[1].Color.y, nu[1].Color.z, nu[1].Color.w);

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
