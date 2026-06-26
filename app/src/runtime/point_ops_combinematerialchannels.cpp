// CombineMaterialChannels image-filter texture op (lane multi-image, image/use) — the FIXED-port PBR
// channel packer. The SIBLING of CombineMaterialChannels2 (point_ops_combinematerialchannels2.cpp):
// CMC2 is the generic 15-way channel selector (img-combine-3.hlsl); CMC is the OPINIONATED
// roughness/metallic/occlusion packer with its OWN shader + a roughness remap CURVE.
//
// TiXL authority:
//   external/tixl Operators/Lib/image/use/CombineMaterialChannels.cs  — op ports: Roughness / Metallic /
//       Occlusion (FIXED numbered Texture2D, NOT MultiInput), Resolution(Int2), GenerateMips(bool),
//       RemapRoughness(Curve).
//   .../CombineMaterialChannels.t3  — STEP-0 verified: standard _ImageFxShaderSetup render-pipeline
//       boilerplate (VertexShaderStage / PixelShaderStage / Draw / RenderTarget / 2 SamplerStates)
//       wrapping the OWN pixel shader, PLUS one CurvesToTexture child that rasterizes RemapRoughness into
//       the t3 RemapCurves LUT. The op's BEHAVIOR is the single .hlsl psMain — ATOMIC.
//       Connected-flags: each material input feeds GetTextureSize→BoolToFloat→FloatsToBuffer; the flag is
//       1.0 iff that input is WIRED. In sw: c.inputTextures[i] != null.
//       Samplers (verbatim): texSampler(s0)=Linear, AddressU/V=Mirror; clampedSampler(s1)=Linear, Clamp.
//       RemapRoughness default Curve = identity ramp (0,0)->(1,1) Linear → remap is a passthrough.
//   .../CombineMaterialChannels.hlsl — the pixel shader, ported VERBATIM into combinematerialchannels.metal:
//       roughness = isRoughnessConnected ? pow(Roughness.r,1) : 0.5
//       metallic  = isMetallicConnected  ? Metallic.g         : 0.0
//       occlusion = isOcclusionConnected ? Occlusion.r        : 1.0
//       roughness = RemapCurves.Sample(clampedSampler, float2(roughness,0.25)).r
//       return float4(roughness, metallic, occlusion, 1)
//
// SEAM NOTE — ZERO shared-graph edit. The multi-image seam already gathers up to kMaxTexInputs=4
// Texture2D ports into TexCookCtx::inputTextures[] in spec port order, DENSE (each port occupies the next
// slot, wired or null — point_graph_tex_cook.cpp:211-216 / point_graph_resident_tex_cook.cpp:236-242). So
// slot 0=Roughness, 1=Metallic, 2=Occlusion always. The RemapRoughness Curve is consumed INSIDE the cook
// (rasterized to an inline 1xN LUT, bound at t3) — NOT routed through the ownTex/inputCurves driver gather
// (CMC is a normal RGBA8 image filter, NOT an own-tex op).
//
// FORK (named):
//   [fork-cmc-embedded-default-curve] — there is no Curve PRODUCER op yet, so a wired RemapRoughness input
//     has no source. TiXL's RemapRoughness slot-default falls back to its embedded identity Curve when
//     unwired (CombineMaterialChannels.t3). We mirror that: when c.inputCurves is empty/null (ALWAYS in
//     production today), the op rasterizes its embedded default identity curve → remap is a passthrough,
//     byte-faithful to the default .t3. The golden injects a custom curve via c.inputCurves to exercise a
//     real remap (and the proof tooth). Identical, documented fork class as point_ops_curvestotexture.cpp.
//   [fork-cmc-mirror-sampler-not-load-bearing] — texSampler AddressU/V=Mirror; not load-bearing (all maps
//     sampled at the SAME in-[0,1] texCoord, no warp/OOB) — kept faithful.
//   GenerateMips + Resolution(Int2)/WindowFollow host plumbing handled by the engine identically to CMC2.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/combinematerialchannels_params.h"  // CombineMaterialChannelsParams, COMBINEMATERIALCHANNELS_Params
#include "runtime/curve.h"                            // sw::Curve / VDefinition (the RemapRoughness currency)
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// RemapRoughness LUT width — the CurvesToTexture child in CombineMaterialChannels.t3 samples the curve
// at this many texels. (TiXL's CurvesToTexture default SampleSize=256; we mirror that width.)
constexpr int kRemapLutN = 256;

// The .t3-embedded default RemapRoughness Curve: identity ramp (0,0)->(1,1), LINEAR interpolation
// (CombineMaterialChannels.t3). Used when the RemapRoughness input is unwired (always, in production —
// no Curve producer yet; [fork-cmc-embedded-default-curve]). Built via addOrUpdate so any tangent recompute
// matches TiXL on load (Linear keys → no spline math, but the entry point is the same).
const Curve& defaultRemapRoughnessCurve() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = 0.0; k0.value = 0.0;
    k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1;
    k1.u = 1.0; k1.value = 1.0;
    k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
    c.addOrUpdate(0.0, k0);
    c.addOrUpdate(1.0, k1);
    return c;
  }();
  return c;
}

// Rasterize a remap Curve to a 1xN RGBA32Float LUT row (R = curve.sample(i/N) per texel, GBA copies of R).
// Caller owns the returned texture. Mirrors rasterizeGradientRow's inline-texture pattern; the curve is
// sampled at the SAME t = i/N convention CurvesToTexture uses (curve.sample((float)i / N)).
MTL::Texture* rasterizeRemapCurveRow(MTL::Device* dev, const Curve& cv, int n) {
  if (!dev || n < 1) return nullptr;
  std::vector<float> row((size_t)n * 4, 0.0f);
  for (int i = 0; i < n; ++i) {
    const float v = (float)cv.sample((double)((float)i / n));  // CurvesToTexture.cs:84 (divisor = N)
    row[(size_t)i * 4 + 0] = v;
    row[(size_t)i * 4 + 1] = v;
    row[(size_t)i * 4 + 2] = v;
    row[(size_t)i * 4 + 3] = 1.0f;
  }
  MTL::TextureDescriptor* td = MTL::TextureDescriptor::texture2DDescriptor(
      MTL::PixelFormatRGBA32Float, (NS::UInteger)n, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);
  if (!tex) return nullptr;
  tex->replaceRegion(MTL::Region::Make2D(0, 0, n, 1), 0, row.data(), (NS::UInteger)n * 4 * sizeof(float));
  return tex;
}

void clearTexture(MTL::CommandQueue* q, MTL::Texture* out) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// injectBug hook (golden only): drop the Metallic gather (-> IsMetallicConnected=0 -> G outputs 0.0
// instead of Metallic.g). Directly exercises the 2nd Texture2D port + its connected-flag.
bool g_cmcIgnoreMetallic = false;

// CombineMaterialChannels cook: read Roughness([0])+Metallic([1])+Occlusion([2]), rasterize the
// RemapRoughness curve (embedded default, or c.inputCurves[0] if a golden injects one) into the t3 LUT,
// one fullscreen pass into c.output.
void cookCombineMaterialChannels(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* roughness = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* metallic  = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  const MTL::Texture* occlusion = c.inputTextureCount > 2 ? c.inputTextures[2] : nullptr;
  if (g_cmcIgnoreMetallic) metallic = nullptr;  // golden injectBug: drop the Metallic wire

  // Connected-flags = wired iff the slot is non-null (CombineMaterialChannels.t3 BoolToFloat(IsConnected)).
  CombineMaterialChannelsParams p{};
  p.IsRoughnessConnected = roughness ? 1.0f : 0.0f;
  p.IsMetallicConnected  = metallic  ? 1.0f : 0.0f;
  p.IsOcclusionConnected = occlusion ? 1.0f : 0.0f;
  p._pad = 0.0f;

  // The shader still SAMPLES all four texture slots regardless of the flags (the HLSL does too — the flag
  // only gates which result is USED). A null Metal texture binding samples (0,0,0,0); since the flag is 0
  // for any null input, that sampled value is discarded. To keep bindings valid we substitute a non-null
  // texture (the output is never read) for any unwired material slot — purely defensive (sampled value
  // gated off by the 0 flag). RemapCurves (t3) is always a real LUT.
  const MTL::Texture* anyTex = roughness ? roughness : (metallic ? metallic : occlusion);
  if (!anyTex) { clearTexture(c.queue, c.output); return; }  // nothing wired → faithful: roughness=0.5 etc.
  const MTL::Texture* bindR = roughness ? roughness : anyTex;
  const MTL::Texture* bindM = metallic  ? metallic  : anyTex;
  const MTL::Texture* bindO = occlusion ? occlusion : anyTex;

  // RemapRoughness LUT: rasterize the embedded default identity curve (production) or the golden-injected
  // curve (c.inputCurves[0]). [fork-cmc-embedded-default-curve]
  const Curve* remap = &defaultRemapRoughnessCurve();
  if (c.inputCurves && !c.inputCurves->empty()) remap = &(*c.inputCurves)[0];
  MTL::Texture* lut = rasterizeRemapCurveRow(c.dev, *remap, kRemapLutN);  // owned; release after draw
  if (!lut) return;

  MTL::RenderPipelineState* rps = cachedTexPSO(
      c.dev, c.lib, "combinematerialchannels_vs", "combinematerialchannels_fs", fmt);
  if (!rps) { lut->release(); return; }

  // texSampler (s0): linear, AddressU/V=Mirror (CombineMaterialChannels.t3 SamplerState).
  MTL::SamplerDescriptor* sd0 = MTL::SamplerDescriptor::alloc()->init();
  sd0->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setSAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  sd0->setTAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  MTL::SamplerState* texSampler = c.dev->newSamplerState(sd0);
  sd0->release();

  // clampedSampler (s1): linear, Clamp (CombineMaterialChannels.t3 clampedSampler) — LOAD-BEARING: the LUT
  // is sampled at x=roughness which can exceed [0,1]; Clamp pins the lookup to the row ends.
  MTL::SamplerDescriptor* sd1 = MTL::SamplerDescriptor::alloc()->init();
  sd1->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd1->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* clampedSampler = c.dev->newSamplerState(sd1);
  sd1->release();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(bindR), 0);  // t0 = Roughness
  enc->setFragmentTexture(const_cast<MTL::Texture*>(bindM), 1);  // t1 = Metallic
  enc->setFragmentTexture(const_cast<MTL::Texture*>(bindO), 2);  // t2 = Occlusion
  enc->setFragmentTexture(lut, 3);                               // t3 = RemapCurves LUT
  enc->setFragmentSamplerState(texSampler, 0);
  enc->setFragmentSamplerState(clampedSampler, 1);
  enc->setFragmentBytes(&p, sizeof(CombineMaterialChannelsParams), COMBINEMATERIALCHANNELS_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  texSampler->release();
  clampedSampler->release();
  lut->release();  // rps is cache-owned (tex_op_cache)
}

}  // namespace

int runCombineMaterialChannelsSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from CombineMaterialChannels.cs — three FIXED Texture2D inputs
// (Roughness/Metallic/Occlusion), the out, Resolution/Custom (engine resolution plumbing, mirrors CMC2).
// RemapRoughness Curve port is exposed (host-value input, consumed in-cook via inputCurves /
// embedded-default fork). Type name "CombineMaterialChannels".
static const ImageFilterOp _reg_combinematerialchannels{
    {"CombineMaterialChannels", "CombineMaterialChannels",
     {{"Roughness", "Roughness", "Texture2D", true},
      {"Metallic", "Metallic", "Texture2D", true},
      {"Occlusion", "Occlusion", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"RemapRoughness", "RemapRoughness", "Curve", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {}, false, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "CombineMaterialChannels", cookCombineMaterialChannels, "combinematerialchannels",
    runCombineMaterialChannelsSelfTest};

// --- CombineMaterialChannels FLAT CHANNEL-PACK GOLDEN (closed-form, d=0 saturated solid) ---------------
// Three FLAT solids: Roughness(r=120), Metallic(g=200), Occlusion(r=160). All three wired → all flags 1.
// RemapRoughness = a NON-identity injected curve (0,0)->(1,0.5): halves roughness (LUT remap of a known
// value, so a dropped/mis-routed LUT bind misses). Expected packed material:
//   out.r = remap(120/255) = remap(0.4706) = 0.4706 * 0.5 = 0.2353 -> 60 (8-bit)
//   out.g = Metallic.g = 200
//   out.b = Occlusion.r = 160
//   out.a = 255
// d=0 plateau, non-degenerate (R!=G!=B). injectBug drops Metallic -> IsMetallicConnected=0 -> G outputs 0
// instead of 200 -> RED (200-LSB miss on G, exercises the 2nd port + its connected-flag).
constexpr uint32_t kGW = 32, kGH = 32;
constexpr uint8_t kR_r = 120;  // Roughness map .r
constexpr uint8_t kM_g = 200;  // Metallic  map .g
constexpr uint8_t kO_r = 160;  // Occlusion map .r
constexpr uint8_t kExpR = 60;  // remap(120/255)=0.2353 -> 0.2353*255 ~= 60
constexpr uint8_t kExpG = kM_g, kExpB = kO_r, kExpA = 255;

// The golden's injected RemapRoughness curve: (0,0)->(1,0.5) LINEAR. remap(x) = 0.5*x.
const Curve& goldenHalfCurve() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0; k0.u = 0.0; k0.value = 0.0;
    k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1; k1.u = 1.0; k1.value = 0.5;
    k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
    c.addOrUpdate(0.0, k0);
    c.addOrUpdate(1.0, k1);
    return c;
  }();
  return c;
}

static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

static bool cmcCookCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool ignoreMetallic,
                          uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* rough = dev->newTexture(td);
  MTL::Texture* metal = dev->newTexture(td);
  MTL::Texture* occl = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  fillSolid(rough, kGW, kGH, kR_r, 10, 10);
  fillSolid(metal, kGW, kGH, 10, kM_g, 10);
  fillSolid(occl, kGW, kGH, kO_r, 10, 10);

  std::map<std::string, float> params;  // no scalar params load-bearing for the pack
  std::vector<Curve> curves;
  curves.push_back(goldenHalfCurve());  // inject the 0.5*x remap

  g_cmcIgnoreMetallic = ignoreMetallic;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = rough; c.inputTextures[1] = metal; c.inputTextures[2] = occl;
  c.inputTextureCount = 3;
  c.inputTexture = rough;
  c.inputCurves = &curves;
  cookCombineMaterialChannels(c);
  g_cmcIgnoreMetallic = false;

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  const uint32_t cx = kGW / 2, cy = kGH / 2;
  size_t i = ((size_t)cy * kGW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  rough->release(); metal->release(); occl->release(); dst->release();
  return true;
}

int runCombineMaterialChannelsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-combinematerialchannels] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  cmcCookCenter(dev, q, lib, /*ignoreMetallic=*/injectBug, got);

  bool nonDegenerate = (kExpR != kExpG) && (kExpG != kExpB) && (kExpR != kExpB);
  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = nonDegenerate && match;
  printf("[selftest-combinematerialchannels] want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) "
         "d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d injectBug=%d -> %s\n",
         kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2], got[3], dR, dG, dB, dA,
         kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
