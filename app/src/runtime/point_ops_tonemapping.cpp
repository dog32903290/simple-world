// ToneMapping image-filter texture op (lane image_filter).
// TiXL authority: external/tixl/Operators/Lib/image/color/ToneMapping.cs (ports) +
// external/tixl/Operators/Lib/Assets/shaders/img/fx/ToneMap.hlsl (kernel).
//
// Single-pass port: cookToneMapping reads c.inputTexture (upstream Texture2D via gather
// direct-through), runs one fullscreen pass of tonemapping_vs/tonemapping_fs, writes c.output.
// No upstream texture: clear output to black.
//
// Inputs (from ToneMapping.cs verbatim, 5 inputs + 1 output):
//   Texture2d (Texture2D in)
//   Mode      (int, MappedType enum: 0=Aces 1=Reinhard 2=Filmic 3=Uncharted2 4=AgX 5=AgX_Punchy 6=None)
//   CorrectGamma (bool)
//   Gamma     (float, default 2.2)
//   Exposure  (float, default 1.0)
//
// Forks (named):
// 1. DX11 PS pipeline -> Metal fullscreen triangle VS+FS (same as Tint/ChannelMixer fork class).
// 2. Mode enum: TiXL stores as int on host, passes to shader as float in cbuffer b0.
//    We read it via cookParam as float (same as _ForceKind pattern in particle ops).
// 3. TiXL bug (verbatim, MUST NOT be fixed): ToneMap.hlsl:105 uses 'Mode<4.5' instead of 5.5,
//    making AgX_Punchy (Mode=5) unreachable. We clone the dead branch faithfully. See shader.
// 4. HLSL mul(vec, matrix) row-major -> Metal matrix column layout (transposed in shader).
//    See tonemapping.metal inline comments.
// 5. Fixed linear+clamp sampler — TiXL's host sampler wrapping knobs not exposed.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/tonemapping_params.h"  // ToneMappingParams, TONEMAPPING_Params
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"    // TexCookCtx, cookParam, registerTexOp
#include "runtime/resident_eval_graph.h"
#include "runtime/tex_op_cache.h"   // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ToneMapping texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookToneMapping(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

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

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "tonemapping_vs", "tonemapping_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ToneMapping.cs input defaults (from ToneMapping.cs and ToneMap.hlsl context):
  //   Mode = 0 (Aces), CorrectGamma = false, Gamma = 2.2, Exposure = 1.0
  ToneMappingParams p{};
  // TiXL ToneMapping.cs: InputSlot<int> Mode (MappedType enum), default 0 = Aces
  p.Mode         = cookParam(c, "Mode", 0.0f);
  // TiXL ToneMapping.cs: InputSlot<bool> CorrectGamma, default false
  p.CorrectGamma = cookParam(c, "CorrectGamma", 0.0f);
  // TiXL ToneMapping.cs: InputSlot<float> Gamma, default 2.2
  p.GammaValue   = cookParam(c, "Gamma", 2.2f);
  // TiXL ToneMapping.cs: InputSlot<float> Exposure, default 1.0
  p.Exposure     = cookParam(c, "Exposure", 1.0f);

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(ToneMappingParams), TONEMAPPING_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec literal moved verbatim from node_registry_image_filter.cpp.
static const ImageFilterOp _reg_tonemapping{
    // ToneMapping (TiXL Lib.image.color.ToneMapping): per-pixel tone mapping curve.
    // Single Texture2D in → Texture2D out (point_ops_tonemapping.cpp). Kernel: ToneMap.hlsl
    // per-mode if/else chain (Aces/Reinhard/Filmic/Uncharted2/AgX/AgX_Punchy/None) with
    // optional gamma correction. Params mirror ToneMapping.cs: Texture2d/Mode(enum)/
    // CorrectGamma(bool)/Gamma(float)/Exposure(float). FORKS (named):
    // 1. DX11 PS -> Metal fullscreen-triangle VS+FS (same fork class as Tint/ChannelMixer).
    // 2. Mode passed as float in cbuffer (TiXL int enum -> float threshold dispatch, _ForceKind pattern).
    // 3. TiXL bug verbatim: ToneMap.hlsl:105 'Mode<4.5' makes AgX_Punchy(5) unreachable.
    //    We clone the dead branch faithfully (named fork[verbatim-TiXL-bug] in shader).
    // 4. Fixed linear+clamp sampler (TiXL host wrap knobs omitted).
    // 5. HLSL mul(vec,mat) row-major -> Metal column-layout transpose (named in tonemapping.metal).
    {"ToneMapping", "ToneMapping",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Mode (int enum, TiXL default 0 = Aces)
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 6.0f, Widget::Enum,
       {"Aces", "Reinhard", "Filmic", "Uncharted2", "AgX", "AgX_Punchy", "None"}, true},
      // CorrectGamma (bool, TiXL default false)
      {"CorrectGamma", "CorrectGamma", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // Gamma (float, TiXL default not explicit in .cs; ToneMap.hlsl uses it as-is; common 2.2)
      {"Gamma", "Gamma", "Float", true, 2.2f, 0.1f, 4.0f},
      // Exposure (float, TiXL default 1.0)
      {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 4.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ToneMapping", cookToneMapping, "tonemapping", runToneMappingSelfTest};

// --- ToneMapping MATH golden ----------------------------------------------------------------
// Fill a 64x64 source texture with bright HDR input (rgb = 4.0 normalised to RGBA8 = 255,
// i.e. solid white at 4× over-exposure). Using a RGBA16Float texture so the GPU sees values
// above 1.0 without clamping at ingest.
//
// Green path (injectBug=false):
//   Mode=1 (Reinhard), Exposure=1.0, CorrectGamma=false.
//   Reinhard: out = c/(c+1) = 4/(4+1) = 0.8.
//   Output rgb should all be < 1.0 (compression worked). We assert center pixel float rgb < 0.95.
//   (RGBA16Float readback as float, so precision is good.)
//
// Red path (injectBug=true):
//   Force Mode=6 (None) -> no tone compression applied -> output rgb stays at Exposure*input = 4.0.
//   We readback the texture: float rgb values should be >= 1.0 (or clamped to 1.0 by the format).
//   We assert rgb >= 0.95 (NOT compressed) — the assertion FAILS in the green path (teeth) because
//   Reinhard DID compress, making this a genuine degenerate injectBug path (not a flipped assertion).
//
// Note: we use RGBA16Float as the render target to carry HDR values >1.0. The green path proves
// Reinhard brings them back below 1.0; the red path proves no-tonemap leaves them at 1.0 (clamped
// by float16 to exactly 1.0 since 4.0 is representable, but uncompressed by Reinhard).
int runToneMappingSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-tonemapping] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source texture: RGBA16Float, solid HDR white at 4.0 (over-exposed).
  MTL::TextureDescriptor* srcTd =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA16Float, W, H, false);
  srcTd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  srcTd->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(srcTd);
  MTL::Texture* dst = dev->newTexture(srcTd);

  // Fill source with HDR white (4.0 per channel) using a clear pass.
  {
    MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setTexture(src);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(4.0, 4.0, 4.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = q->commandBuffer();
    cmd->renderCommandEncoder(rpd)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  // injectBug=true: force Mode=6 (None) to skip tone compression -> output stays HDR (>=1.0).
  // injectBug=false: Mode=1 (Reinhard) -> 4/(4+1)=0.8 -> compressed below 1.0.
  std::map<std::string, float> params;
  params["Mode"]         = injectBug ? 6.0f : 1.0f;  // 6=None (no-op) vs 1=Reinhard
  params["CorrectGamma"] = 0.0f;
  params["Gamma"]        = 2.2f;
  params["Exposure"]     = 1.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookToneMapping(c);

  // Readback center pixel as float16 -> interpret as float.
  // RGBA16Float: 2 bytes per channel, 8 bytes per pixel.
  std::vector<uint16_t> outBuf((size_t)W * H * 4, 0);
  dst->getBytes(outBuf.data(), W * 8, MTL::Region::Make2D(0, 0, W, H), 0);
  const uint32_t cx = W / 2, cy = H / 2;
  size_t idx = ((size_t)cy * W + cx) * 4;

  // Convert float16 half to float using bit manipulation.
  auto halfToFloat = [](uint16_t h) -> float {
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exp  = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t bits;
    if (exp == 0) {
      if (mant == 0) { bits = sign << 31; }
      else {
        exp = 1;
        while (!(mant & 0x400)) { mant <<= 1; exp--; }
        mant &= 0x3ff;
        bits = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
      }
    } else if (exp == 31) {
      bits = (sign << 31) | 0x7f800000 | (mant << 13);
    } else {
      bits = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f; __builtin_memcpy(&f, &bits, 4);
    return f;
  };

  float R = halfToFloat(outBuf[idx + 0]);
  float G = halfToFloat(outBuf[idx + 1]);
  float B = halfToFloat(outBuf[idx + 2]);

  // Assertion: rgb must all be < 0.95 (Reinhard compressed 4.0 -> 0.8).
  // Green path (Reinhard): 4/(4+1) = 0.8 < 0.95 -> compressed=true -> PASS.
  // Red path  (None, injectBug): output stays at Exposure*4.0=4.0 (RGBA16Float holds HDR)
  //   -> R=4.0 >= 0.95 -> compressed=false -> FAIL (real degenerate, not a flipped assertion).
  // The SAME assertion drives both paths; injectBug produces a genuine physics failure.
  bool compressed = (R < 0.95f) && (G < 0.95f) && (B < 0.95f);
  printf("[selftest-tonemapping] center R=%.3f G=%.3f B=%.3f Mode=%s compressed=%d -> %s\n",
         R, G, B,
         injectBug ? "None(6)" : "Reinhard(1)",
         compressed ? 1 : 0,
         compressed ? "PASS" : "FAIL");

  // --- AgX matrix-parity sub-check (green path only) -----------------------------------
  // AgX (Mode 4) is the ONLY mode that uses matrix multiplies; the Reinhard assertion above
  // never exercises them. The HLSL `mul(rowVec, M)` -> Metal `M * colVec` translation is the
  // classic metal-cpp transpose trap. We cook AgX on a NON-GRAY input (so off-diagonal terms
  // matter) and compare the GPU center pixel to a C++ replica of ToneMap.hlsl's tonemapAgX
  // computed with the correct row-vector convention. A transposed matrix -> GPU != ref -> FAIL.
  // Permanent bite tooth (locks the AgX transpose forever).
  bool agxOk = true;
  if (!injectBug) {
    const float in0 = 0.30f, in1 = 0.50f, in2 = 0.20f;
    {
      MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
      auto* ca = rpd->colorAttachments()->object(0);
      ca->setTexture(src);
      ca->setLoadAction(MTL::LoadActionClear);
      ca->setClearColor(MTL::ClearColor::Make(in0, in1, in2, 1.0));
      ca->setStoreAction(MTL::StoreActionStore);
      MTL::CommandBuffer* cmd = q->commandBuffer();
      cmd->renderCommandEncoder(rpd)->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();
    }
    std::map<std::string, float> ap;
    ap["Mode"] = 4.0f;  // AgX
    ap["CorrectGamma"] = 0.0f;
    ap["Gamma"] = 2.2f;
    ap["Exposure"] = 1.0f;
    clearTexOpCache();
    TexCookCtx ac;
    ac.dev = dev; ac.lib = lib; ac.queue = q;
    ac.nodeId = 2; ac.inputTexture = src; ac.output = dst; ac.params = &ap;
    cookToneMapping(ac);
    std::vector<uint16_t> ab((size_t)W * H * 4, 0);
    dst->getBytes(ab.data(), W * 8, MTL::Region::Make2D(0, 0, W, H), 0);
    float gR = halfToFloat(ab[idx + 0]);
    float gG = halfToFloat(ab[idx + 1]);
    float gB = halfToFloat(ab[idx + 2]);
    // C++ reference: pow 2.2 -> tonemapAgX(.,false) (ToneMap.hlsl:58-77), row-vector mul.
    auto p22 = [](float v) { return std::pow(v < 0.0f ? 0.0f : v, 2.2f); };
    float r0 = p22(in0), r1 = p22(in1), r2 = p22(in2);
    // m0 rows (ToneMap.hlsl:61), out[j] = sum_i in[i]*M0[i][j]
    float a0 = r0 * 0.842f  + r1 * 0.0784f + r2 * 0.0792f;
    float a1 = r0 * 0.0423f + r1 * 0.878f  + r2 * 0.0792f;
    float a2 = r0 * 0.0424f + r1 * 0.0784f + r2 * 0.879f;
    auto enc = [](float v) { float x = (std::log2(v) + 12.47393f) / 16.5f; return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
    a0 = enc(a0); a1 = enc(a1); a2 = enc(a2);
    auto sig = [](float v) { return 0.5f + 0.5f * std::sin(((-3.11f * v + 6.42f) * v - 0.378f) * v - 1.44f); };
    a0 = sig(a0); a1 = sig(a1); a2 = sig(a2);
    // m1 rows (ToneMap.hlsl:75), out[j] = sum_i a[i]*M1[i][j]
    float e0 = a0 * 1.2f     + a1 * (-0.1f)   + a2 * (-0.1f);
    float e1 = a0 * (-0.053f) + a1 * 1.15f    + a2 * (-0.1f);
    float e2 = a0 * (-0.053f) + a1 * (-0.1f)  + a2 * 1.15f;
    float dr = std::fabs(gR - e0), dg = std::fabs(gG - e1), db = std::fabs(gB - e2);
    float maxErr = dr > dg ? (dr > db ? dr : db) : (dg > db ? dg : db);
    agxOk = maxErr < 0.01f;
    printf("[selftest-tonemapping] AgX parity gpu=(%.4f,%.4f,%.4f) ref=(%.4f,%.4f,%.4f) maxErr=%.5f(need<0.01) -> %s\n",
           gR, gG, gB, e0, e1, e2, maxErr, agxOk ? "PASS" : "FAIL");
  }

  bool allPass = compressed && agxOk;

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return allPass ? 0 : 1;
}

}  // namespace sw
