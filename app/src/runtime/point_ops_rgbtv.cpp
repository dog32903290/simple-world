// RgbTV image-filter texture op (lane image_filter) — the (E)-seam phase-2 leaf: the FIRST op that
// binds a SECOND asset texture (t1 = the perlin-noise asset) AND the FIRST real consumer of the
// Cut-53 mip seam (it samples its INPUT at LOD 0..7).
//
// TiXL authority (traced 1:1 in STEP-0, see rgbtv_params.h for the routing table):
//   Operators/Lib/image/fx/glitch/RgbTV.cs   — op ports (24 floats + Image + Resolution).
//   Operators/Lib/image/fx/glitch/RgbTV.t3   — _multiImageFxSetup compound: the FloatsToBuffer
//       MultiInput is fed by 24 connections whose ORDER is EXACTLY the .hlsl cbuffer field order
//       (verified: the only two intermediate-math entries are Clamp(Add(GlitchAmount,AnimValue))
//       => the GlitchAmount field, and BoolToFloat(...) => the GlitchTime field; both land at their
//       natural cbuffer slots). At DEFAULT op settings: GlitchTimeOverride=0 -> Compare(0<0)=false
//       -> BoolToFloat picks ForFalse=_Time -> GlitchTime field = running time; GlitchFlicker=0 ->
//       AnimValue amplitude 0 -> AnimValue=0 -> GlitchAmount field = clamp(1.0+0,0,1e4)=1.0.
//   Operators/Lib/Assets/shaders/img/fx/RgbTV.hlsl — the pixel shader (ported 1:1 -> rgbtv.metal).
//
// FORK #1 (named): "我方 compute leaf vs TiXL .t3 _multiImageFxSetup PIXEL pass — SAME RgbTV.hlsl
// math; the leaf runs it as a compute kernel (RWTexture2D) instead of a fullscreen draw." The .t3
// VS/PS/RenderTarget/sampler/FloatsToBuffer machinery is INCIDENTAL plumbing: it exposes only the 24
// scalar params + Image + Resolution to the user, and feeds the shader two fixed samplers (a clamp
// sampler and a wrap sampler) + the noise asset. We bake those verbatim. NO Layer2d/Execute seam.
//
// FORK #2 (named): the input MIPS. TiXL's .t3 renders the op into a RenderTarget with GenerateMips=1,
// so inputTexture (the op's OWN output fed back... actually the input image) carries mips; the .hlsl
// hardcodes mipLevelCount=7 and samples LOD 0..7. We replicate by generating mips on the INPUT inside
// the cook: copy input level0 into a MIPPED cached scratch, generateMipmaps (blit), then the kernel
// samples that scratch's LODs. This is the first real consumer of the Cut-53 mip seam (which until now
// shipped with only a selftest). Per-op, not per-instance (TiXL's GenerateMips is a per-instance bool;
// RgbTV always wants it -> baked on).
//
// FORK #3 (named, LOUD — improvement-over-TiXL-WIP, NOT byte-parity at the noise path):
//   *** TiXL ships RgbTV with the perlin noise node DISCONNECTED. We connect it on purpose. ***
//
//   STEP-0 backward trace of the .hlsl t1 (noiseTexture) SRV — authoritative, by GUID through RgbTV.t3
//   (PixelShaderStage 6d65586b ShaderResources slot 50052906, second SRV = t1):
//       t1 <- SrvFromTexture2d(da98052c) <- Blur(7e6a92a3, Size=1.6, Wrap) <- LoadImage(a46ebca8,
//             Path = EMPTY/null, NO incoming connection)
//   i.e. TiXL feeds t1 a Blur of an EMPTY LoadImage ≈ BLACK. With a black noiseTexture the .hlsl's
//   noiseColor = abs(0 - 0.5)*GlitchAmount*Visibility = a UNIFORM 0.5*GA*Vis — the CRT distortion
//   degenerates to a flat constant (no per-pixel glitch). The actual perlin asset is loaded by a
//   DIFFERENT node, LoadImage(e60808e8, "Lib:images/basic/perlin-noise-rgb.png") -> Grain(912d2bec)
//   -> DANGLES (its output connects to nothing); the standalone PerlinNoise(be822ec7) node also
//   DANGLES. This is a TiXL WIP/bug: the author wired up a noise asset + a blur but never connected
//   them to the shader.
//
//   DECISION (orchestrator, per 柏為's 你決定 + visual-artist intent): ship the perlin-CONNECTED
//   version as a NAMED IMPROVEMENT FORK. We bind the RAW decoded perlin-noise-rgb.png at t1 (Cut-57
//   decode) so the CRT glitch ACTUALLY WORKS (noiseColor varies per pixel). This is an
//   improvement-over-TiXL-WIP — NOT byte-parity with TiXL at the noise path (TiXL's noise path is
//   degenerate-black; matching it would mean shipping a broken CRT effect).
//
//   PRE-BLUR (Blur Size=1.6): NOT ported. TiXL's Blur(1.6) sits on the EMPTY/disconnected LoadImage,
//   not on the perlin asset (the perlin goes through Grain, then dangles). There is no coherent
//   "intended" graph to faithfully reproduce — the Blur and the perlin are two separate dangling
//   experiments. Porting a separable-gaussian pre-pass would invent fidelity to a graph that does not
//   cohere; we bind the raw asset (the actual noise the author loaded). If the visual ever wants a
//   softer distortion noise, a one-time blit/separable-blur on the cached asset at bind time is the
//   place to add it (one-time, NOT per-frame) — left out deliberately, not by omission.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterComputeOp + asset-texture seam
#include "runtime/point_graph.h"                // TexCookCtx, cookParam
#include "runtime/rgbtv_params.h"               // RgbTvParams/RgbTvResolution, RGBTV_* bindings
#include "runtime/tex_op_cache.h"               // cachedComputePSO, cachedScratchTex (mipped scratch)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

// Fill RgbTvParams from the cook params. Values are 1:1 with RgbTV.cs ports + the two .t3-routed
// fields (GlitchAmount/GlitchTime). DEFAULTS here mirror RgbTV.t3's DefaultValue per port — so an
// unwired node cooks at TiXL defaults. GlitchTime defaults to 0 (the golden + an unwired graph have
// no running clock; the production app would feed a time param later — named: time is not yet a port).
void fillParams(const TexCookCtx& c, RgbTvParams& p) {
  p.Visibility         = cookParam(c, "Visibility", 1.0f);
  p.PatternAmount      = cookParam(c, "PatternAmount", 0.2f);
  p.ImageBrightness    = cookParam(c, "ImageBrightness", 0.5f);
  p.BlackLevel         = cookParam(c, "BlackLevel", -0.100000024f);
  p.Contrast           = cookParam(c, "Contrast", 1.0f);
  p.BlurImage          = cookParam(c, "BlurImage", 0.0f);
  p.GlowIntensity      = cookParam(c, "GlowIntensity", 0.1f);
  p.GlowBlur           = cookParam(c, "GlowBlur", 0.8f);
  p.PatternSize        = cookParam(c, "PatternSize", 0.025f);
  p.ShiftColums        = cookParam(c, "ShiftColumns", 0.5f);
  p.Gaps               = cookParam(c, "Gaps", 0.03f);
  p.PatternBlurX       = cookParam(c, "PatternBlur.x", 0.25f);
  p.PatternBlurY       = cookParam(c, "PatternBlur.y", 0.25f);
  p.GlitchAmount       = cookParam(c, "GlitchAmount", 1.0f);  // .t3 clamp(Add(...)) default 1.0
  p.GlitchTime         = cookParam(c, "GlitchTime", 0.0f);    // .t3 BoolToFloat(_Time); default 0
  p.GlitchDistort      = cookParam(c, "GlitchDistort", 1.0f);
  p.ShadeDistortion    = cookParam(c, "ShadeDistortion", 2.0f);
  p.NoiseForDistortion = cookParam(c, "NoiseForDistortion", 20.0f);
  p.Noise              = cookParam(c, "Noise", 0.1f);
  p.NoiseSpeed         = cookParam(c, "NoiseSpeed", 10.0f);
  p.NoiseExponent      = cookParam(c, "NoiseExponent", 1.0f);
  p.NoiseColorize      = cookParam(c, "NoiseColorize", 0.5f);
  p.Buldge             = cookParam(c, "Buldge", 0.15f);
  p.Vignette           = cookParam(c, "Vignette", 1.0f);
}

// injectBug hook (golden only): when set, fillParams' RGB stripe term is neutralized by forcing
// PatternAmount=0 in the cook — a real wiring perturbation (the RGB-stripe pattern never blends in),
// so the pattern-dominated golden pixels collapse toward the plain image and the pinned values miss.
bool g_rgbtvDropPattern = false;

// NOISE-PATH injectBug hook (golden only): when set, forces ShadeDistortion=0 in the cook. That
// neutralizes the entire noise->shade term (imgCol.rgb *= clamp(1 - pow(noiseColor.r,1.4)*
// ShadeDistortion*...)): with ShadeDistortion=0 the factor collapses to 1, so the noise-driven
// darkening vanishes. A real wiring perturbation that ONLY affects the noise/distortion path — so the
// GlitchAmount>0 golden (which has a nonzero noiseColor) reddens, while the GlitchAmount=0 golden is
// untouched (its noiseColor.r path is multiplied by GlitchAmount=0 anyway). Proves the >0 pins ride
// the noise path, not just the stripe math.
bool g_rgbtvKillNoiseShade = false;

// RgbTV cook: generate input mips, bind input(mipped)@0 + noise asset@1 + output@2, dispatch.
void cookRgbTv(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();
  const uint32_t W = (uint32_t)c.output->width(), H = (uint32_t)c.output->height();

  // No upstream texture: nothing to process — clear output (parity with the other leaves' no-input).
  if (!c.inputTexture) {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(c.output);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    return;
  }

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "rgbtv_cs");
  if (!pso) return;

  // --- Input mips (Cut-53 mip seam consumer) ---------------------------------------------------
  // Allocate a MIPPED scratch the same size as the input, copy input level 0 in (blit), then
  // generateMipmaps to fill LODs 1..N. The kernel then samples this scratch's LODs (the .hlsl
  // mipLevelCount=7 loop). Sized off the INPUT dims (which equal the output dims here — no SizeFn).
  const uint32_t IW = (uint32_t)c.inputTexture->width(), IH = (uint32_t)c.inputTexture->height();
  MTL::Texture* mippedIn = cachedScratchTex(c.dev, (uint64_t)fmt, IW, IH, "rgbtv.mipin",
                                            /*shaderWrite=*/false, /*mipped=*/true);
  if (!mippedIn) return;
  {
    MTL::CommandBuffer* mc = c.queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = mc->blitCommandEncoder();
    // Copy input level 0 -> mippedIn level 0 (formats match: both = output format).
    blit->copyFromTexture(const_cast<MTL::Texture*>(c.inputTexture), 0, 0,
                          MTL::Origin::Make(0, 0, 0), MTL::Size::Make(IW, IH, 1), mippedIn, 0, 0,
                          MTL::Origin::Make(0, 0, 0));
    blit->generateMipmaps(mippedIn);
    blit->endEncoding();
    mc->commit();
    mc->waitUntilCompleted();
  }

  // --- Samplers: s0 "clamp" (linear + mip-linear), s1 wrap (linear) — the two TiXL SamplerState nodes.
  // s0 = SamplerState 5803fb7b: AddressU/V = MirrorOnce, AddressW = Clamp (verified in RgbTV.t3 lines
  // 290-308). MirrorOnce mirrors the texture once about the edge then clamps — the s0 tap reads the
  // input at the bulge/glitch-shifted uv2, which goes OOB at the default Buldge=0.15, so the address
  // mode is load-bearing (ClampToEdge would smear the edge column; MirrorOnce reflects it). The Metal
  // equivalent of D3D11 MIRROR_ONCE is MirrorClampToEdge. (s1 = SamplerState 7be639ed: AddressW=Clamp,
  // U/V left at the node default = Wrap -> repeat; correct as-is.)
  MTL::SamplerDescriptor* sdC = MTL::SamplerDescriptor::alloc()->init();
  sdC->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sdC->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sdC->setMipFilter(MTL::SamplerMipFilterLinear);  // mip-READ consumer: enable trilinear LOD sampling
  sdC->setSAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);  // s0 AddressU = MirrorOnce
  sdC->setTAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);  // s0 AddressV = MirrorOnce
  MTL::SamplerState* clampS = c.dev->newSamplerState(sdC);
  sdC->release();

  MTL::SamplerDescriptor* sdW = MTL::SamplerDescriptor::alloc()->init();
  sdW->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sdW->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sdW->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sdW->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* wrapS = c.dev->newSamplerState(sdW);
  sdW->release();

  RgbTvParams p{};
  fillParams(c, p);
  if (g_rgbtvDropPattern) p.PatternAmount = 0.0f;
  if (g_rgbtvKillNoiseShade) p.ShadeDistortion = 0.0f;  // noise-path perturbation (GlitchAmount>0 golden)

  RgbTvResolution res{};
  res.TargetWidth = (float)W;
  res.TargetHeight = (float)H;

  // Noise asset @ t1. cookTexNode set c.assetTexture from the registered key. If it's null (no
  // decoder registered, or decode failed), bind the input as a harmless stand-in so the kernel still
  // runs — but the golden drives GlitchAmount=0 so the noise tap is multiplied out regardless.
  const MTL::Texture* noiseTex = c.assetTexture ? c.assetTexture : c.inputTexture;

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setTexture(mippedIn, RGBTV_Input);                              // input (mipped) @ texture(0)
  enc->setTexture(const_cast<MTL::Texture*>(noiseTex), RGBTV_Noise);   // noise asset   @ texture(1)
  enc->setTexture(c.output, RGBTV_Result);                            // output (RW)   @ texture(2)
  enc->setSamplerState(clampS, RGBTV_ClampSampler);                   // s0 clamp
  enc->setSamplerState(wrapS, RGBTV_WrapSampler);                     // s1 wrap
  enc->setBytes(&p, sizeof(RgbTvParams), RGBTV_Params);              // b0
  enc->setBytes(&res, sizeof(RgbTvResolution), RGBTV_Res);          // b1
  MTL::Size tg = MTL::Size::Make(RGBTV_TGX, RGBTV_TGY, 1);
  MTL::Size grid = MTL::Size::Make(ceilDiv(W, RGBTV_TGX), ceilDiv(H, RGBTV_TGY), 1);
  enc->dispatchThreadgroups(grid, tg);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  clampS->release();
  wrapS->release();
  // pso + scratch are cache-owned (tex_op_cache), not released here.
}

}  // namespace

int runRgbTvSelfTest(bool injectBug);

// Self-registration (COMPUTE leaf + ASSET texture): ImageFilterComputeOp marks "RgbTV" ShaderWrite,
// no SizeFn (output = Resolution pin), registers --selftest-rgbtv[-bug], and declares the noise asset
// key (bound at t1 via cookTexNode). Ports 1:1 with RgbTV.cs: Image + 24 scalar params (PatternBlur
// is a Vec2 -> two .x/.y Float Vec ports) + Resolution. Defaults verbatim from RgbTV.t3.
//
// NAMED IMPROVEMENT FORK (see FORK #3 in the file header): the asset key below CONNECTS the perlin
// noise asset that TiXL ships DISCONNECTED (TiXL's t1 = Blur of an empty LoadImage ≈ black -> a
// degenerate uniform-noise CRT). Binding the real perlin makes the glitch actually work. This is an
// improvement-over-TiXL-WIP, NOT byte-parity at the noise path.
static const ImageFilterComputeOp _reg_rgbtv{
    {"RgbTV", "RgbTV",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Visibility", "Visibility", "Float", true, 1.0f, 0.0f, 1.0f},
      {"PatternAmount", "PatternAmount", "Float", true, 0.2f, 0.0f, 1.0f},
      {"ImageBrightness", "ImageBrightness", "Float", true, 0.5f, 0.0f, 4.0f},
      {"BlackLevel", "BlackLevel", "Float", true, -0.100000024f, -1.0f, 1.0f},
      {"Contrast", "Contrast", "Float", true, 1.0f, 0.0f, 8.0f},
      {"BlurImage", "BlurImage", "Float", true, 0.0f, -1.0f, 1.0f},
      {"GlowIntensity", "GlowIntensity", "Float", true, 0.1f, 0.0f, 10.0f},
      {"GlowBlur", "GlowBlur", "Float", true, 0.8f, 0.0f, 1.0f},
      {"PatternSize", "PatternSize", "Float", true, 0.025f, 0.001f, 1.0f},
      {"ShiftColumns", "ShiftColumns", "Float", true, 0.5f, -2.0f, 2.0f},
      {"Gaps", "Gaps", "Float", true, 0.03f, 0.0f, 0.49f},
      // PatternBlur (Vec2, TiXL default (0.25,0.25)) -> PatternBlurX=.x, PatternBlurY=.y.
      {"PatternBlur.x", "PatternBlur", "Float", true, 0.25f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"PatternBlur.y", "PatternBlur.y", "Float", true, 0.25f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"GlitchAmount", "GlitchAmount", "Float", true, 1.0f, 0.0f, 10.0f},
      {"GlitchTime", "GlitchTime", "Float", true, 0.0f, 0.0f, 1000.0f},
      {"GlitchDistort", "GlitchDistort", "Float", true, 1.0f, 0.0f, 10.0f},
      {"ShadeDistortion", "ShadeDistortion", "Float", true, 2.0f, 0.0f, 10.0f},
      {"NoiseForDistortion", "NoiseForDistortion", "Float", true, 20.0f, 0.0f, 100.0f},
      {"Noise", "Noise", "Float", true, 0.1f, 0.0f, 1.0f},
      {"NoiseSpeed", "NoiseSpeed", "Float", true, 10.0f, 0.0f, 100.0f},
      {"NoiseExponent", "NoiseExponent", "Float", true, 1.0f, 0.0f, 10.0f},
      {"NoiseColorize", "NoiseColorize", "Float", true, 0.5f, 0.0f, 1.0f},
      {"Buldge", "Buldge", "Float", true, 0.15f, -1.0f, 1.0f},
      {"Vignette", "Vignette", "Float", true, 1.0f, 0.0f, 2.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "RgbTV", cookRgbTv, /*sizeFn=*/nullptr, "rgbtv", runRgbTvSelfTest, /*mippedOutput=*/false,
    "Lib:images/basic/perlin-noise-rgb.png"};

// --- RgbTV GOLDEN (EXACT-PIXEL in a NOISE-INDEPENDENT region) ---------------------------------
// The output is noise-dependent in general (asset noiseColor + procedural GetNoiseFromRandom). To get
// a deterministic exact-pixel tooth we drive GlitchAmount=0 (a param we set in the golden, like
// FastBlur forces MaxLevels=1). With GlitchAmount=0:
//   noiseColor = abs(noise-0.5)*0*Visibility + 0.03 = 0.03  (CONSTANT — asset-independent)
//   glichOffset = pow(0.03,4)*GlitchDistort*(1,0.1) ~= 6.5e-8  (negligible uv2 shift)
//   noiseDelta = abs(GetNoiseFromRandom)*(...)*Noise*GlitchAmount = ...*0 = 0  (procedural noise gone)
// so the output is a pure deterministic function of the (mipped) input + the RGB-stripe/vignette math.
//
// Input: a UNIFORM mid-gray field (all pixels = G), so every mip LOD = G and Input.sample(...,uv2,i)=G
// for all i and all uv2 (sampling a constant texture at any LOD/coord = the constant). That collapses
// the blur/glow accumulation to G and makes imgCol a closed-form constant per channel BEFORE the RGB
// stripe. The remaining position-dependence is the RGB-stripe pattern (GetColor) + vignette, both
// deterministic from uv. We pin the CENTER pixel where the math is most tractable, plus 4 neighbors.
//
// Rather than hand-derive the full stripe (smoothstep/pow chains that are exact but tedious), we PIN
// to HARDCODED CONSTANTS — the unmodified correct-wiring GPU output of THIS kernel at GlitchAmount=0 +
// uniform gray, captured once and BAKED into the source (the SAME methodology FastBlur's exact tooth
// uses for its derived row: hardcoded {x,want} pins, NOT a re-captured reference). Hardcoding is the
// load-bearing choice: it makes the tooth bite a GLOBAL routing error (e.g. swapping the
// ImageBrightness/Contrast cbuffer slots) — a self-captured reference would shift BOTH the reference
// AND the output together and miss it. Verified deterministic across runs (RGBA8Unorm, fixed input).
// injectBug (g_rgbtvDropPattern) sets PatternAmount=0 -> the RGB stripe never blends in -> the pinned
// pattern pixels collapse toward plain gray -> miss the pins -> RED. A real wiring perturbation.
constexpr uint32_t kGW = 64, kGH = 64;
constexpr uint8_t kGray = 128;  // uniform input level

// HARDCODED correct-wiring pins: row Y=32, x=30..34, the R-G-B sub-pixel stripe signature. Captured
// from the verified GREEN build; baked here so a global param-routing error reddens the tooth.
struct RgbTvPin { uint32_t x; uint8_t r, g, b; };
constexpr RgbTvPin kRgbTvPins[] = {
    {30, 56, 67, 56},  // green-dominant stripe column
    {31, 67, 56, 56},  // red-dominant
    {32, 56, 56, 65},  // blue-dominant
    {33, 56, 65, 56},  // green-dominant
    {34, 65, 57, 57},  // red-dominant
};

// Run the cook once on a uniform-gray input at GlitchAmount=0 and read back row kGH/2.
static bool rgbtvCookGrayRow(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool dropPattern,
                             std::vector<uint8_t>& out) {
  MTL::TextureDescriptor* tdSrc =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  tdSrc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  tdSrc->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(tdSrc);
  std::vector<uint8_t> in((size_t)kGW * kGH * 4, 0);
  for (size_t i = 0; i < (size_t)kGW * kGH; ++i) {
    in[i * 4 + 0] = kGray; in[i * 4 + 1] = kGray; in[i * 4 + 2] = kGray; in[i * 4 + 3] = 255;
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, kGW, kGH), 0, in.data(), kGW * 4);

  MTL::TextureDescriptor* tdDst =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  tdDst->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                  MTL::TextureUsageShaderWrite);
  tdDst->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(tdDst);

  std::map<std::string, float> params;
  params["GlitchAmount"] = 0.0f;  // kill BOTH noise sources -> deterministic
  g_rgbtvDropPattern = dropPattern;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  c.assetTexture = nullptr;  // GlitchAmount=0 makes the noise tap irrelevant; stand-in = input
  cookRgbTv(c);
  g_rgbtvDropPattern = false;

  out.assign((size_t)kGW * kGH * 4, 0);
  dst->getBytes(out.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  src->release(); dst->release();
  return true;
}

// --- RgbTV GlitchAmount>0 GOLDEN (the NOISE PATH — refuter Point 1/4) -------------------------------
// The GlitchAmount=0 golden above is noise-INDEPENDENT (it multiplies the whole noise tap out). The
// DEFAULT-setting render has GlitchAmount=1.0, so the noise path (noiseColor -> glichOffset ->
// noiseDelta -> ShadeDistortion shade) MUST be covered. This second golden drives GlitchAmount=1.0.
//
// Determinism: the noiseColor tap is Noise.sample(wrapS, noiseUv, 0). We bind a UNIFORM noise texture
// (every texel = kNoise) via c.assetTexture, so the sample is the constant kNoise/255 regardless of
// noiseUv -> noiseColor is a deterministic function of y only ((0.4+pow(1-uv2.y,6)*3) edge-amp + 0.03).
// kNoise = 204 (=0.8) so abs(0.8-0.5)=0.3 != 0 -> noiseColor, glichOffset, and the ShadeDistortion
// shade term are all LIVE (a 0.5 noise would zero noiseColor and defeat the point). The input stays
// uniform gray, so the blur/glow mips collapse to the constant and the only spatial variation is the
// (now noise-shaded) image + RGB stripe + vignette — all deterministic. We pin the SAME center row.
//
// Pins BAKED from the verified GREEN build (same methodology as the >0... err, =0 golden). They differ
// from kRgbTvPins because the noise shade darkens imgCol and glichOffset shifts uv2. injectBug
// (g_rgbtvKillNoiseShade) forces ShadeDistortion=0 -> the noise->shade darkening vanishes -> the pins
// brighten/shift -> miss -> RED. That perturbation touches ONLY the noise path, proving these pins
// ride it (the GlitchAmount=0 golden is untouched by it).
constexpr uint8_t kNoise = 204;  // uniform noise texel = 0.8 -> abs(0.8-0.5)=0.3 (nonzero noiseColor)

constexpr RgbTvPin kRgbTvGlitchPins[] = {
    {30, 48, 57, 48},  // green-dominant stripe, darkened by noise shade
    {31, 57, 47, 47},  // red-dominant
    {32, 47, 47, 56},  // blue-dominant
    {33, 48, 56, 48},  // green-dominant
    {34, 57, 48, 48},  // red-dominant
};

// Cook on uniform-gray input with GlitchAmount=1.0 and a UNIFORM kNoise asset texture; read row kGH/2.
// killShade -> ShadeDistortion=0 (noise-path perturbation). Returns the row.
static bool rgbtvCookGlitchRow(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool killShade,
                               std::vector<uint8_t>& out) {
  MTL::TextureDescriptor* tdSrc =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  tdSrc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  tdSrc->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(tdSrc);
  std::vector<uint8_t> in((size_t)kGW * kGH * 4, 0);
  for (size_t i = 0; i < (size_t)kGW * kGH; ++i) {
    in[i * 4 + 0] = kGray; in[i * 4 + 1] = kGray; in[i * 4 + 2] = kGray; in[i * 4 + 3] = 255;
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, kGW, kGH), 0, in.data(), kGW * 4);

  // Uniform noise asset texture (every texel = kNoise). Bound at t1 via c.assetTexture.
  const uint32_t kNW = 16;
  MTL::TextureDescriptor* tdN =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kNW, kNW, false);
  tdN->setUsage(MTL::TextureUsageShaderRead);
  tdN->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* noiseTex = dev->newTexture(tdN);
  std::vector<uint8_t> npx((size_t)kNW * kNW * 4, kNoise);
  for (size_t i = 0; i < (size_t)kNW * kNW; ++i) npx[i * 4 + 3] = 255;
  noiseTex->replaceRegion(MTL::Region::Make2D(0, 0, kNW, kNW), 0, npx.data(), kNW * 4);

  MTL::TextureDescriptor* tdDst =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  tdDst->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                  MTL::TextureUsageShaderWrite);
  tdDst->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(tdDst);

  std::map<std::string, float> params;
  params["GlitchAmount"] = 1.0f;  // DEFAULT setting -> noise path LIVE
  g_rgbtvKillNoiseShade = killShade;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  c.assetTexture = noiseTex;  // KNOWN uniform noise -> deterministic noiseColor
  cookRgbTv(c);
  g_rgbtvKillNoiseShade = false;

  out.assign((size_t)kGW * kGH * 4, 0);
  dst->getBytes(out.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  src->release(); noiseTex->release(); dst->release();
  return true;
}

int runRgbTvSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  clearImageFilterAssetCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-rgbtv] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const uint32_t Y = kGH / 2;
  auto px = [&](const std::vector<uint8_t>& v, uint32_t x, int ch) {
    return (int)v[((size_t)Y * kGW + x) * 4 + ch];
  };
  const int kTol = 2;

  // ===== CASE A: GlitchAmount=0 golden (noise-INDEPENDENT, stripe/vignette math) =====
  // injectBug -> drop the RGB-stripe pattern (PatternAmount=0).
  std::vector<uint8_t> got;
  rgbtvCookGrayRow(dev, q, lib, /*dropPattern=*/injectBug, got);

  bool reshapesA = false;
  for (const RgbTvPin& p : kRgbTvPins)
    if (std::abs((int)p.r - kGray) > 3 || std::abs((int)p.g - kGray) > 3 ||
        std::abs((int)p.b - kGray) > 3)
      reshapesA = true;

  bool matchA = true;
  int maxDeltaA = 0;
  for (const RgbTvPin& p : kRgbTvPins) {
    int want[4] = {p.r, p.g, p.b, 255};
    for (int ch = 0; ch < 4; ++ch) {
      int d = std::abs(px(got, p.x, ch) - want[ch]);
      maxDeltaA = std::max(maxDeltaA, d);
      if (d > kTol) matchA = false;
    }
  }

  // ===== CASE B: GlitchAmount=1.0 golden (the NOISE PATH — refuter Point 1/4) =====
  // A KNOWN uniform noise texture (=0.8) drives a nonzero, deterministic noiseColor; the noise->shade
  // term darkens imgCol and glichOffset shifts uv2. injectBug -> kill ShadeDistortion (noise-path
  // perturbation) -> the noise darkening vanishes -> the >0 pins miss -> RED.
  std::vector<uint8_t> gotB;
  rgbtvCookGlitchRow(dev, q, lib, /*killShade=*/injectBug, gotB);

  bool reshapesB = false;
  for (const RgbTvPin& p : kRgbTvGlitchPins)
    if (std::abs((int)p.r - kGray) > 3 || std::abs((int)p.g - kGray) > 3 ||
        std::abs((int)p.b - kGray) > 3)
      reshapesB = true;

  bool matchB = true;
  int maxDeltaB = 0;
  for (const RgbTvPin& p : kRgbTvGlitchPins) {
    int want[4] = {p.r, p.g, p.b, 255};
    for (int ch = 0; ch < 4; ++ch) {
      int d = std::abs(px(gotB, p.x, ch) - want[ch]);
      maxDeltaB = std::max(maxDeltaB, d);
      if (d > kTol) matchB = false;
    }
  }

  // INVARIANT (cross-check): the two golden rows must DIFFER — the GlitchAmount>0 noise shade really
  // changes the output vs GlitchAmount=0. If they were identical, the noise path would be unexercised.
  bool casesDiffer = false;
  for (const RgbTvPin& p : kRgbTvGlitchPins)
    for (int ch = 0; ch < 3; ++ch)
      if (std::abs(px(got, p.x, ch) - px(gotB, p.x, ch)) > kTol) casesDiffer = true;

  // Tooth: BOTH goldens must match the baked correct-wiring pins (and the noise case must genuinely
  // differ from the noise-free case). no-bug -> both match -> PASS. injectBug (or a param-routing
  // error) -> at least one misses -> FAIL.
  bool pass = reshapesA && matchA && reshapesB && matchB && casesDiffer;

  printf("[selftest-rgbtv] caseA(GA=0): reshapes=%d maxDelta=%d match(<=%d)=%d | "
         "caseB(GA=1,noise): reshapes=%d maxDelta=%d match=%d | casesDiffer=%d injectBug=%d -> %s\n",
         reshapesA ? 1 : 0, maxDeltaA, kTol, matchA ? 1 : 0, reshapesB ? 1 : 0, maxDeltaB, matchB ? 1 : 0,
         casesDiffer ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");
  printf("  -- caseA (GlitchAmount=0) --\n");
  for (const RgbTvPin& p : kRgbTvPins)
    printf("  pin px(%u,%u) want=(%d,%d,%d) got=(%d,%d,%d)\n", p.x, Y, p.r, p.g, p.b, px(got, p.x, 0),
           px(got, p.x, 1), px(got, p.x, 2));
  printf("  -- caseB (GlitchAmount=1, uniform noise=0.8) --\n");
  for (const RgbTvPin& p : kRgbTvGlitchPins)
    printf("  pin px(%u,%u) want=(%d,%d,%d) got=(%d,%d,%d)\n", p.x, Y, p.r, p.g, p.b, px(gotB, p.x, 0),
           px(gotB, p.x, 1), px(gotB, p.x, 2));

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  clearImageFilterAssetCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
