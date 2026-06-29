// LoadImage image-SOURCE op (the SOURCE-OP seam, proving op LoadImage). TiXL authority:
// external/tixl Operators/Lib/image/generate/load/LoadImage.cs (a Slot<Texture2D> SOURCE that
// decodes the file at `Path` via ResourceManager.CreateTextureResource, generates mips, and exposes
// the decoded texture as its output). LoadImage has NO pixel kernel — the Texture2D IS the decoded
// asset. We ride the EXISTING resident Texture2D flow: LoadImage cooks into its OWN resolution-
// pinned ensureTex RGBA8Unorm texture (exactly like a generator: CheckerBoard), so a downstream
// Texture2D consumer reads a normal texture with ZERO new engine fields. The decoded asset is
// COPIED 1:1 into that output (loadimage.metal, nearest sampler — same-size copy is byte-exact).
//
// HOW IT RIDES THE EXISTING SEAM (zero point_graph.h / resident.cpp edits):
//   - DECODE: the proven asset-texture seam (image_filter_op_registry.h). cachedAssetTexture(dev,
//     key) resolves a "Lib:..." key under SW_ASSETS_DIR + decodes ONCE via the app-registered
//     decoder (platform::decodeImageToTexture) + memoizes. NO per-frame decode, NO ImageIO in
//     runtime. The leaf registers its DEFAULT path as a static asset key in imageFilterAssetTextures()
//     so the cook driver (flat AND resident) hands the decoded default in via TexCookCtx::assetTexture.
//   - PATH PARAM (flat leg): TexCookCtx carries c.graph + c.nodeId in the FLAT path, so the cook can
//     read the node's per-instance Path override (Node::strParams["Path"]) and re-decode THAT key via
//     cachedAssetTexture. This honors a runtime Path in the flat path (the golden exercises it).
//   - FALLBACK: empty/invalid Path (or no decoder / undecodable file) -> source texture is null ->
//     clear the output to TRANSPARENT BLACK (engine fallback) and return. Never crash. This is the
//     count-fork-safe no-op default (mirrors cookBlur's no-input clear-to-black).
//
// NAMED FORKS (parity discipline):
//   - fork[no-kernel]: TiXL LoadImage has no shader; the copy pass is a host artifact of riding the
//     resolution-pinned output flow. Pixels are the decoded asset, unmodified (loadimage.metal).
//   - fork[premultiply]: the platform decoder (ImageIO/CoreGraphics) premultiplies alpha; TiXL/WIC
//     store straight. For a fully-opaque fixture (A=255) it is a NO-OP — the golden pins opaque
//     texels so decode is byte-exact (image_decode.h:16-20 already names this fork).
//   - fork[level-0]: the decode is level-0 only (mipped=false). TiXL GenerateMips's on load; the
//     golden verifies the DECODE, not mip parity. No imageFilterMippedOutputTypes() registration.
//   - fork[resident-uses-default-path]: the RESIDENT (production) tex-cook driver sets
//     TexCookCtx::graph=nullptr / nodeId=0 (point_graph_resident.cpp:728), so a per-instance Path
//     STRING does NOT reach the cook in resident — the resident leg binds the STATIC default key
//     (imageFilterAssetTextures()["LoadImage"]). A per-instance runtime Path in resident needs a
//     string channel ON TexCookCtx, which lives in point_graph.h (line-count grandfather cap 440,
//     RATCHET-locked) AND a resident driver edit (point_graph_resident.cpp at its cap 820) — both
//     blocked this batch. DEFERRED to the string-into-TexCookCtx seam (a later batch). The default
//     path covers the production "single asset" case; the flat golden proves the Path-override wiring.
//   - fork[path-resolution]: Path is resolved as a "Lib:..." asset key under SW_ASSETS_DIR (TiXL
//     project-relative resource paths). Absolute / arbitrary-cwd paths are NOT resolved here (a
//     follow-up; TiXL's faithful default is the project-relative resource key).
//   - fork[no-hot-reload]: TiXL hot-reloads the texture on file change (ResourceManager watcher); sw
//     is static-load (cachedAssetTexture memoizes forever). Named gap. ImageSequenceClip (frame-
//     indexed source) is a separate, later op — out of scope.
//   - fork[resolution-input]: TiXL LoadImage has NO Resolution/CustomW/CustomH inputs — its output is
//     always the decoded file's NATIVE dimensions. sw adds a Resolution enum (default WindowFollow) and
//     resolution-pins the output, nearest-resampling when output size != native (e.g. 512x512 asset ->
//     8x8 window in the resident leg). A same-size output is byte-exact (what the flat golden pins).
//   - fork[cache-resources-no-flush]: TiXL CacheResources (LoadImage.cs:98-99, [Input] bool, default
//     FALSE — new InputSlot<bool>() + LoadImage.t3 DefaultValue:false) gates whether decoded textures
//     are kept in a per-instance _resourcesCache (cs:52-77) or re-decoded on each cook (cs:19-48
//     DisposeCache path). sw's cachedAssetTexture memoizes FOREVER (a process-global decode-once cache),
//     so sw BEHAVES like CacheResources=true regardless of the knob; the false branch (per-cook flush)
//     is a dead knob — sw has no per-cook flush seam (cachedAssetTexture never invalidates). Output is
//     IDENTICAL either way (cache vs re-decode = perf/memory, not pixels). The param is EXPOSED pinless
//     bool with default 0=FALSE to match TiXL's inspector default; its false branch is UNIMPLEMENTED
//     (follow-up: per-cook flush seam in cachedAssetTexture).
//     Follow-up: implement a per-cook flush path in cachedAssetTexture (cache-invalidation seam) and
//     wire it via CacheResources=false in cookLoadImage. Named: fork[cache-resources-no-flush].
//   NOTE: SourcePathSlot is NOT a TiXL [Input] param — it is a C# interface property alias
//     (LoadImage.cs:104: `public InputSlot<string> SourcePathSlot => Path;`) that re-exposes the
//     Path slot for the IDescriptiveFilename interface. It is not a separate inspector knob.
//
// Self-contained leaf: cookLoadImage + ImageFilterOp self-registration + the static asset-key
// registration + runLoadImageSelfTest. CMake glob auto-picks this file + loadimage.metal.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"                      // Graph/Node/findSpec (flat Path-override read)
#include "runtime/graph_bridge.h"               // libFromGraph (resident golden leg)
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp + cachedAssetTexture + asset sink
#include "runtime/point_graph.h"                // TexCookCtx, registerTexOp
#include "runtime/resident_eval_graph.h"        // buildEvalGraph (resident golden leg)
#include "runtime/tex_op_cache.h"               // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// The DEFAULT asset (the perlin-noise asset already committed under assets/images/). Used as the
// resident static key AND the flat-leg default when the node carries no Path override.
static const char* kLoadImageDefaultKey = "Lib:images/basic/perlin-noise-rgb.png";

// Forward decl so the file-scope registrar (below the anon namespace) can name the selftest.
int runLoadImageSelfTest(bool injectBug);

namespace {

// Resolve the source texture for THIS cook. Flat leg: read the node's per-instance Path override
// (Node::strParams["Path"]) and decode that key; unset -> the spec/default key. Resident leg
// (c.graph == nullptr): the driver already bound the STATIC default key at c.assetTexture.
const MTL::Texture* resolveSourceTexture(TexCookCtx& c) {
  // FLAT: honor the per-instance Path string (c.graph + c.nodeId are set only in the flat path).
  if (c.graph) {
    const Node* n = c.graph->node(c.nodeId);
    // Precedence (faithful to TiXL): an EXPLICIT Path strParam wins, EVEN IF blank — a blanked Path
    // means "no asset" (TiXL: Texture.Value == null -> warning, no texture -> our fallback). Only a
    // node with NO Path strParam at all falls back to the spec default key (the committed asset).
    std::string key = kLoadImageDefaultKey;
    if (n) {
      auto it = n->strParams.find("Path");
      if (it != n->strParams.end()) key = it->second;  // explicit value (may be empty = blank)
    }
    // Empty key (explicit blank Path) -> null -> fallback (clear). cachedAssetTexture returns null
    // for an empty/undecodable key (memoized), which is exactly the fallback trigger.
    return key.empty() ? nullptr : cachedAssetTexture(c.dev, key, /*mipped=*/false);
  }
  // RESIDENT: the static default key was decoded + bound by the cook driver (assetTexture seam).
  return c.assetTexture;
}

// LoadImage source op: decode the asset (via the asset-texture seam) and copy it 1:1 into the op's
// own resolution-pinned output. No input texture (a SOURCE). Null source (empty/invalid Path / no
// decoder) -> clear output to transparent black (fallback, never crash).
void cookLoadImage(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* src = resolveSourceTexture(c);

  // FALLBACK: no decodable source -> clear to transparent black and return (mirror cookBlur's
  // no-input clear). Transparent (alpha 0) so a downstream consumer + the golden can DISTINGUISH the
  // fallback from a real decode (which the opaque fixture loads at alpha 255).
  if (!src) {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(c.output);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));  // transparent black fallback
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    return;
  }

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "loadimage_vs", "loadimage_fs", fmt);
  if (!rps) return;

  // NEAREST + clamp sampler: a same-size copy reads the matching source texel center -> byte-exact
  // (fork[nearest-sampler], loadimage.metal). At a different output resolution it nearest-resamples.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(src, 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

// Register LoadImage's DEFAULT path as a static asset key so the cook driver (flat + resident) binds
// the decoded default at TexCookCtx::assetTexture. A file-scope initializer (zero shared-file edit).
const int _reg_loadimage_asset = [] {
  imageFilterAssetTextures()["LoadImage"] = kLoadImageDefaultKey;
  return 0;
}();

}  // namespace

// Self-registration: feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests() pre-main.
static const ImageFilterOp _reg_loadimage{
    // LoadImage (TiXL Lib.image.generate.load.LoadImage): a Texture2D SOURCE that decodes the file at
    // Path and exposes it as its output. Zero Texture2D inputs (a source, not a filter). Path is a
    // String param (the decoded asset key; default = the committed perlin-noise asset). Resolution
    // picks the output texture size (same enum as a generator; default WindowFollow). CacheResources
    // (TiXL bool, default true) is honored implicitly by cachedAssetTexture's memoization (the decode
    // happens once) — named: we have no non-cached path, which matches TiXL's default true.
    {"LoadImage", "LoadImage",
     {// No Image input — a SOURCE.
      {"out", "out", "Texture2D", false},
      // Path (String): the asset key. strDef = the committed default asset (last positional field).
      {"Path", "Path", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "Lib:images/basic/perlin-noise-rgb.png"},
      // CacheResources (TiXL bool, default true): exposed as a pinless bool; see
      // fork[cache-resources-no-flush] — the false branch (per-cook flush) is not yet implemented.
      {"CacheResources", "CacheResources", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      // Resolution (standard image-source enum; default WindowFollow)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "LoadImage", cookLoadImage, "loadimage", runLoadImageSelfTest};

}  // namespace sw
