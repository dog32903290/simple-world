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
//
// ── STAGE 2 (TEXTURE_COMPUTE_SEAM_SPEC §5 table row 2): SRV-tex read + b0 CB (first sealed: depth-to-
//   linear). A stage whose kernel reads a Texture2D SRV takes the SRV-TEX PATH: the first wired
//   ShaderResourceTextures = the SRV (bound at the kernel's srv texture index), its GetDimensions drives
//   the output allocation + 2D dispatch (fork computestagetex-srv-in-self-allocates-output — sw's tex-track
//   producer idiom allocates its own output at the input's W×H, folding away TiXL's OutputTexture boundary
//   input + GetTextureSize + CalcInt2DispatchCount; for depth-to-linear input.dims==output.dims so exact),
//   and the b0 CB rides the SCALAR CB0..CB{n-1} Float ports (fork computestagetex-cb-scalar-rail: the tex
//   currency carries no Buffer, so the importer bakes FloatsToBuffer's Params — in wire order == the
//   cbuffer layout — onto CB# ports; the cook packs them tightly at buffer(0)). Live re-wiring of the CB
//   params through the compound boundary is a later refinement (fork computestagetex-cb-defaults-baked:
//   the authored .t3 defaults are baked, same spirit as stage-1's baked Size). The generator path (BRDF,
//   no SRV) is unchanged.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
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
  if (src.find("img/post-fx/depth-to-linear.hlsl") != std::string::npos)
    return "computeshaderstage_depthtolinear";  // stage 2: 1SRV-tex+1UAV-tex+b0 CB (depth linearization)
  return src;  // unmapped path → the PSO lookup fails loudly (no silent wrong kernel)
}

// Per-kernel dispatch/binding metadata (data-driven, ARCHITECTURE rule 7 — one row per texture-out
// kernel ported). numthreads (fork computestage-per-kernel-threadgroup) + the Metal texture indices the
// transpiled MSL assigns to the SRV read / UAV write (transpiler binding, verified against the .metal
// signature) + the b0 CB float count (0 = generator, no CB) + the output PixelFormat when the .t3 has NO
// Texture2d allocator (SRV-tex path; Invalid → the folded Format strParam supplies it, the BRDF path).
struct TexKernelMeta {
  uint32_t tgx = 8, tgy = 8;
  int srvTexIndex = -1;   // Metal texture index of the SRV read (-1 = generator, no SRV input)
  int uavTexIndex = 0;    // Metal texture index of the UAV output write
  int cbFloatCount = 0;   // # tightly-packed floats in the b0 CB (0 = no CB)
  MTL::PixelFormat outFmt = MTL::PixelFormatInvalid;  // Invalid → use the folded Format strParam (BRDF)
};

TexKernelMeta texKernelMeta(const std::string& kernel) {
  TexKernelMeta m;
  if (kernel == "computeshaderstage_brdflookup") {  // numthreads(32,32,1); u0→texture(0); no SRV/CB
    m.tgx = 32; m.tgy = 32; m.uavTexIndex = 0;
  } else if (kernel == "computeshaderstage_depthtolinear") {  // numthreads(16,16,1)
    // transpiler: CB(b0)→buffer(0), InputTexture(t0)→texture(0), OutputTexture(u0)→texture(1).
    m.tgx = 16; m.tgy = 16; m.srvTexIndex = 0; m.uavTexIndex = 1; m.cbFloatCount = 6;
    m.outFmt = MTL::PixelFormatR32Float;  // RWTexture2D<float> (no Texture2d allocator in the .t3)
  }
  return m;
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

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, kernel.c_str());
  if (!pso) return;  // unported/unknown kernel → loud fail (no silent black), no redirect
  const TexKernelMeta meta = texKernelMeta(kernel);

  // SRV-TEX PATH (stage 2): the first wired ShaderResourceTextures = the SRV read. Its GetDimensions
  // drives the output allocation + dispatch (fork computestagetex-srv-in-self-allocates-output). The
  // GENERATOR path (BRDF, meta.srvTexIndex<0) sizes from OutW/OutH + Format instead. `srv` is null when
  // an SRV-tex kernel's ShaderResourceTextures is unwired → nothing to read (early return, no redirect).
  const MTL::Texture* srv = (c.inputTextureCount > 0) ? c.inputTextures[0] : nullptr;
  uint32_t W = 0, H = 0;
  MTL::PixelFormat fmt = MTL::PixelFormatInvalid;
  if (meta.srvTexIndex >= 0) {
    if (!srv) return;  // SRV-tex kernel, no input texture → nothing to dispatch
    W = (uint32_t)srv->width();
    H = (uint32_t)srv->height();
    fmt = meta.outFmt;  // per-kernel output format (no Texture2d allocator in the SRV-tex .t3)
  } else {
    W = (uint32_t)std::lround(cookParam(c, "OutW", 512.0f));
    H = (uint32_t)std::lround(cookParam(c, "OutH", 512.0f));
    fmt = texFormatFor(texStrParam(c.strParams, "Format", "R16G16B16A16_UNorm"));
  }
  if (W == 0 || H == 0) return;
  if (fmt == MTL::PixelFormatInvalid) return;  // unmapped Format → loud fail (§6 risk 5)

  // Allocate the shaderWrite output texture (fork computestage-allocates-uav-texture). Cache-keyed by
  // this node's cookKey so it persists/reuses across frames (reallocs only on size/format change).
  MTL::Texture* out =
      cachedScratchTex(c.dev, (uint64_t)fmt, W, H, c.cookKey + ".csstex", /*shaderWrite=*/true);
  if (!out) return;

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

  // Pack the b0 CB (scalar rail CB0..CB{n-1}, fork computestagetex-cb-scalar-rail; wire order == the
  // cbuffer layout). Tightly packed floats → setBytes at buffer(0), matching the transpiled struct.
  float cb[16] = {0};
  const int cbN = meta.cbFloatCount < 16 ? meta.cbFloatCount : 16;
  for (int i = 0; i < cbN; ++i) {
    char key[8];
    std::snprintf(key, sizeof(key), "CB%d", i);
    cb[i] = cookParam(c, key, 0.0f);
  }

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  if (cbN > 0) enc->setBytes(cb, sizeof(float) * (size_t)cbN, 0);  // b0 → buffer(0)
  if (meta.srvTexIndex >= 0 && srv)
    enc->setTexture(const_cast<MTL::Texture*>(srv), meta.srvTexIndex);  // SRV read (t0)
  enc->setTexture(out, meta.uavTexIndex);  // UAV write (u0)
  enc->dispatchThreadgroups(MTL::Size::Make(ceilDiv(W, meta.tgx), ceilDiv(H, meta.tgy), 1),
                            MTL::Size::Make(meta.tgx, meta.tgy, 1));
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
      // ShaderResourceTextures: MultiInput<Texture2D> SRV read (stage 2; folded from SrvFromTexture2d).
      // The first wire drives dispatch/output size (input GetDimensions). Unwired for a generator (BRDF).
      {"ShaderResourceTextures", "ShaderResourceTextures", "Texture2D", true, 0.0f, 0.0f, 0.0f,
       Widget::Slider, {}, false, 1, /*multiInput=*/true},
      {"KernelName", "KernelName", "String", true},  // folded from ComputeShader.Source
      {"Format", "Format", "String", true},          // folded from Texture2d.Format (generator path)
      {"OutW", "OutW", "Float", true, 512.0f, 1.0f, 8192.0f},  // baked from Size.X (generator path)
      {"OutH", "OutH", "Float", true, 512.0f, 1.0f, 8192.0f},  // baked from Size.Y (generator path)
  };
  // b0 CB scalar rail (fork computestagetex-cb-scalar-rail): CB0..CB7 pinless Float ports the importer
  // bakes from FloatsToBuffer.Params (wire order == the cbuffer layout). The cook packs meta.cbFloatCount
  // of them at buffer(0). Pinless (Inspector-only) — these are folded, not canvas-wired. 8 covers the
  // sealed depth kernel's 6-float CB with headroom; a bigger CB extends this row count.
  for (int i = 0; i < 8; ++i) {
    char id[8];
    std::snprintf(id, sizeof(id), "CB%d", i);
    spec.ports.push_back({id, id, "Float", true, 0.0f, 0.0f, 0.0f, Widget::Slider, {},
                          /*pinless=*/true});
  }
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
