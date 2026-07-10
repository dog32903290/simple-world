// point_ops_computeshaderstagetex — the TEX-track compute-stage atom (texture-compute keystone,
// TEXTURE_COMPUTE_SEAM_SPEC.md stage 1). The tex-currency twin of buffer_ops_computeshaderstage.cpp:
// it ALLOCATES a shaderWrite output texture, binds it as the UAV (u0 == Metal texture(0)), DISPATCHES
// a named MSL kernel 2D over the output W×H, and RETURNS the written texture. This is the sw port of
// the texture-out slice of TiXL's ComputeShaderStage — the op the 66 texture-bound compute compounds
// route through (66/190 = 34.7% of TiXL compute shaders, ENGINE_GAP_BUFFER_SHAPES.md).
//
// TiXL authority: the _ComputeBRDFLookup.t3 subgraph (external/tixl/Operators/Lib/render/_):
//   Texture2d(f52db9a4)  — allocates the R16G16B16A16_UNorm SRV+UAV texture (Size + Format).
//   UavFromTexture2d(84e02044) — wraps it as a UAV (Metal: the texture IS the UAV → no view object).
//   ComputeShaderStage(8bef116d) — SetUnorderedAccessViews(u0) + Dispatch (Gfx/ComputeShaderStage.cs).
//   CalcInt2DispatchCount(cc11774e) — dispatch = ceil(Size / numthreads) (elided: sw derives it here).
//   ExecuteTextureUpdate(6c2f8241) — runs the pass, forwards the texture (degenerates to passthrough).
// The importer FOLDS that whole subgraph onto this ONE atom (t3_import_texcompute.cpp) — same collapse
// philosophy as fastblur's Layer2d wrapper / the buffer stage's ExecuteBufferUpdate forwarder.
//
// ── sw COLLAPSE (named forks, TEXTURE_COMPUTE_SEAM_SPEC §3) ─────────────────────────────────────────
// • computeshaderstage-splits-by-uav-currency: a stage whose Uavs is fed by UavFromTexture2d is a
//   TEXTURE-OUT stage → this tex-track atom (a buffer-UAV stage stays on buffer_ops_computeshaderstage).
//   The rail split happens at import; this atom is the tex leg.
// • computestage-allocates-uav-texture: the Texture2d child's allocation folds INTO the stage — the
//   stage allocates its own shaderWrite output texture (Size + Format), exactly as fastblur allocates
//   its scratch via cachedScratchTex(...,shaderWrite=true) (point_ops_fastblur.cpp:167).
// • computestage-per-kernel-threadgroup: TiXL's numthreads varies per kernel (BRDF 32×32). The
//   kernel→threadgroup metadata table below carries each; the 2D dispatch = ceil(W/tgx)×ceil(H/tgy)
//   over the output extent (fastblur.cpp:108 precedent), = GetDimensions/CalcInt2DispatchCount.
// • computeshaderstage-dispatch-in-cook: the stage dispatches DURING its cook and outputs the written
//   texture directly (ExecuteTextureUpdate → passthrough). TiXL's deferred Command ordering is
//   preserved (the tex cook already runs dependency-first). Named, not silent.
// • computestage-default-sampler: this stage-1 kernel (BRDF) declares NO sampler/SRV; per-op samplers
//   are stage 3. (No sampler bound here.)
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration (spec sink + registerTexOp)
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, RenderResolution
#include "runtime/tex_op_cache.h"               // cachedComputePSO, cachedScratchTex

namespace sw {
namespace {

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return b ? (a + b - 1) / b : 0; }

// injectBug hook (golden ②parity only): when set, the cook ALLOCATES the output texture but SKIPS the
// UAV bind + dispatch (= "UavFromTexture2d fold off / UAV not bound") and clears the texture to black,
// so the readback diverges from the BRDF oracle. Real cook-path perturbation (a dropped dispatch, not
// an assert flip). Default false in production; the golden toggles it. File-scope so the golden reads it.
bool g_texStageSkipDispatch = false;

// A wired String param off TexCookCtx (mirror of bufferStrParam for the buffer stage's KernelName).
std::string texStrParam(const std::map<std::string, std::string>* sp, const char* id,
                        const std::string& def) {
  if (!sp) return def;
  auto it = sp->find(id);
  return it != sp->end() ? it->second : def;
}

// Map a TiXL HLSL asset path (ComputeShader.Source, folded onto KernelName at import) to the ported MSL
// kernel name. Already-an-MSL-name (no path sep / .hlsl) passes through. Extend one row per texture-out
// kernel ported (data-driven, ARCHITECTURE rule 7). Twin of buffer_ops_computeshaderstage.cpp:kernelNameFor.
std::string texKernelNameFor(const std::string& src) {
  if (src.empty()) return src;
  if (src.find('/') == std::string::npos && src.find(".hlsl") == std::string::npos) return src;
  if (src.find("3d/rendering/ComputeBrdfLookupTexture-cs.hlsl") != std::string::npos)
    return "computeshaderstage_brdflookup";  // stage 1: 1UAV-tex pure generator (split-sum BRDF LUT)
  return src;  // unmapped path → the PSO lookup fails loudly (no silent wrong kernel)
}

// Per-kernel threadgroup dims (fork computestage-per-kernel-threadgroup): the .hlsl numthreads(...).
// Default 8×8 for an unlisted kernel (a safe 2D tile). BRDF = numthreads(32,32,1) (…-cs.hlsl:64).
void texKernelThreadgroup(const std::string& kernel, uint32_t& tgx, uint32_t& tgy) {
  tgx = 8; tgy = 8;
  if (kernel == "computeshaderstage_brdflookup") { tgx = 32; tgy = 32; }
}

// Texture2d Format enum (SharpDX.DXGI.Format string, folded from the Texture2d child) → MTL::PixelFormat.
// Small table, one row per format seen; an unmapped format loud-fails (returns Invalid → no texture),
// never silently picks a wrong format (TEXTURE_COMPUTE_SEAM_SPEC §6 risk 5).
MTL::PixelFormat texFormatFor(const std::string& fmt) {
  if (fmt == "R16G16B16A16_UNorm") return MTL::PixelFormatRGBA16Unorm;
  if (fmt == "R16G16B16A16_Float") return MTL::PixelFormatRGBA16Float;
  if (fmt == "R8G8B8A8_UNorm") return MTL::PixelFormatRGBA8Unorm;
  if (fmt == "R32G32B32A32_Float") return MTL::PixelFormatRGBA32Float;
  return MTL::PixelFormatInvalid;  // unmapped → loud fail (no dispatch)
}

// The tex-track compute stage cook. Allocates the shaderWrite output texture, dispatches the kernel
// into it, and redirects it out (the driver returns redirectTexture as the node's output — same seam
// UseTextureReference uses, so residentTexFor/target() see the written texture).
void cookComputeShaderStageTex(TexCookCtx& c) {
  if (!c.dev || !c.lib || !c.queue) return;

  const std::string kernel =
      texKernelNameFor(texStrParam(c.strParams, "KernelName", std::string()));
  if (kernel.empty()) return;  // no shader → nothing to dispatch (TiXL _cs==null early-return)

  const uint32_t W = (uint32_t)std::lround(cookParam(c, "OutW", 512.0f));
  const uint32_t H = (uint32_t)std::lround(cookParam(c, "OutH", 512.0f));
  if (W == 0 || H == 0) return;

  const MTL::PixelFormat fmt =
      texFormatFor(texStrParam(c.strParams, "Format", "R16G16B16A16_UNorm"));
  if (fmt == MTL::PixelFormatInvalid) return;  // unmapped Format → loud fail (§6 risk 5)

  // Allocate the shaderWrite output texture (fork computestage-allocates-uav-texture). Cache-keyed by
  // this node's cookKey so it persists/reuses across frames (reallocs only on size/format change).
  MTL::Texture* out =
      cachedScratchTex(c.dev, (uint64_t)fmt, W, H, c.cookKey + ".csstex", /*shaderWrite=*/true);
  if (!out) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, kernel.c_str());
  if (!pso) return;  // unported/unknown kernel → loud fail (no silent black), no redirect

  if (g_texStageSkipDispatch) {
    // injectBug: UAV not bound / dispatch dropped → clear to black so the readback is a clean divergence.
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(out);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    c.redirectTexture = out;
    return;
  }

  // Bind the allocated texture as the UAV (u0 == Metal texture(0)) and dispatch 2D over W×H.
  uint32_t tgx = 8, tgy = 8;
  texKernelThreadgroup(kernel, tgx, tgy);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setTexture(out, 0);  // RWTexture2D<float4> LUT : register(u0) → texture(0)
  enc->dispatchThreadgroups(MTL::Size::Make(ceilDiv(W, tgx), ceilDiv(H, tgy), 1),
                            MTL::Size::Make(tgx, tgy, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  // The written texture IS the node's output (ExecuteTextureUpdate passthrough). PSO is cache-owned.
  c.redirectTexture = out;
}

// NodeSpec: a texture-out generator (no Texture2D input). KernelName/Format ride the String rail
// (folded at import); OutW/OutH ride the Float rail (baked from the .t3 Size default). Output Texture2D.
NodeSpec makeSpec() {
  NodeSpec spec;
  spec.type = "ComputeShaderStageTex";
  spec.title = "ComputeShaderStageTex";
  spec.category = "render/texture";
  spec.ports = {
      {"Output", "Output", "Texture2D", false},
      {"KernelName", "KernelName", "String", true},  // folded from ComputeShader.Source
      {"Format", "Format", "String", true},          // folded from Texture2d.Format
      {"OutW", "OutW", "Float", true, 512.0f, 1.0f, 8192.0f},  // baked from Size.X
      {"OutH", "OutH", "Float", true, 512.0f, 1.0f, 8192.0f},  // baked from Size.Y
  };
  spec.evaluate = nullptr;
  return spec;
}

// Self-registration: a plain tex op (registerTexOp + spec into imageFilterSpecSink so findSpec sees it).
// NOT ImageFilterComputeOp — this atom REDIRECTS to its own op-allocated texture rather than writing the
// resolution-pinned RGBA8 ensureTex output, so it needs neither the Image input nor the ShaderWrite-on-
// ensureTex marking that ImageFilterComputeOp implies. No standalone selftest here — the kernel is proven
// by --selftest-mathv-brdflookup (direct dispatch) and the seam by --selftest-t3-brdflookup.
static const ImageFilterOp _reg_computeshaderstagetex{makeSpec(), "ComputeShaderStageTex",
                                                      cookComputeShaderStageTex};

}  // namespace

// Golden hook (t3import_brdflookup_golden.cpp ②parity injectBug): drop the UAV dispatch → black texture.
bool& computeShaderStageTexSkipDispatch() { return g_texStageSkipDispatch; }

}  // namespace sw
