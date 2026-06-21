// runtime/image_filter_op_registry — self-registration seam for IMAGE FILTER ops.
//
// ARCHITECTURE rule 7 (data-driven) pushed down to the op layer: adding an image-filter op =
// add ONE leaf .cpp (point_ops_<name>.cpp) that ends with a file-scope `ImageFilterOp` registrar.
// No shared list file is edited — the old撞車點 (node_registry_image_filter.cpp specs table,
// point_ops_register_image_filter.cpp register lines, selftests.cpp kTable rows) are gone.
//
// How it works:
//   - Each leaf defines its registrar at namespace scope. The registrar's constructor runs during
//     pre-main dynamic initialization and:
//       1. calls registerTexOp(cookType, cook)  (point_graph's Meyers-singleton texReg table),
//       2. pushes its NodeSpec into imageFilterSpecSink(),
//       3. (optionally) pushes its {selftestName, selftest} pair into imageFilterSelfTests().
//   - The consumers read those sinks only AFTER main starts:
//       node_registry.cpp's registry() lambda (first findSpec/specTypes) and the selftest
//       dispatcher (runSelftestFromArgs). Because every registrar is a namespace-scope static,
//       all of them finish their dynamic-init constructors before main runs → the sinks are fully
//       populated by first read. (Same lifetime guarantee registerTexOp already relies on: its
//       container is a Meyers singleton, so order between the registrar and that singleton is fine —
//       the singleton is created on first registerTexOp() call, i.e. inside the registrar itself.)
//
// FORK / risk (named): intra-family ORDER in the sink follows cross-TU dynamic-init order, which is
// unspecified. This only affects the Add-menu ordering of image-filter ops (cosmetic) — findSpec is
// keyed by type name and .swproj wires are keyed by port id, neither depends on spec position.
#pragma once
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "runtime/graph.h"        // NodeSpec
#include "runtime/point_graph.h"  // PointTexFn, registerTexOp, RenderResolution

namespace sw {

// Meyers singletons — the two sinks every image-filter leaf registrar feeds.
std::vector<NodeSpec>& imageFilterSpecSink();
std::vector<std::pair<const char*, int (*)(bool)>>& imageFilterSelfTests();

// COMPUTE-leaf sinks (the -cs.hlsl path). A compute leaf differs from a pixel leaf in exactly two
// host-side ways, captured by these two sinks (everything else — cook fn signature, NodeSpec, the
// selftest pair — is identical to ImageFilterOp):
//   (1) its output texture must carry MTL::TextureUsageShaderWrite (it is a RWTexture2D target),
//   (2) its output size may differ from the Resolution pin (Crop shrinks; cookTexNode asks the
//       SizeFn for the real output dims from the cropped INPUT size).
// imageFilterComputeTypes(): the set of cook types that need ShaderWrite on their output.
// imageFilterSizeFns(): optional per-type output-size override. SizeFn(params, inputSize) ->
//   the output RenderResolution. Absent type = output size follows the Resolution pin (pixel rule).
using ImageFilterSizeFn = RenderResolution (*)(const std::map<std::string, float>&,
                                               RenderResolution inputSize);
std::set<std::string>& imageFilterComputeTypes();
std::map<std::string, ImageFilterSizeFn>& imageFilterSizeFns();

// MIPPED-OUTPUT sink (the mip-gen seam, mirror of imageFilterComputeTypes()). A producer op whose
// output should carry a full mip pyramid registers its cookType here; cookTexNode (flat + resident)
// then allocates the output mipped and issues generateMipmaps (a blit) AFTER the leaf fills level 0.
// This is per-op-TYPE (coarser than TiXL's per-INSTANCE GenerateMips bool — named fork, see leaf).
// mip-READ consumers (a downstream op that samples level(lod)) need NO registration: they only set
// setMipFilter(Linear) on their sampler and sample(uv, level(lod)) — zero engine, zero sink entry.
std::set<std::string>& imageFilterMippedOutputTypes();

// ASSET-TEXTURE sink ((E)-seam phase 2, mirror of imageFilterComputeTypes()). An op that needs a
// loaded asset texture bound at the NEXT texture slot (t1 — the perlin-noise asset for RgbTV)
// registers its cookType -> asset key (a TiXL "Lib:..." path). cookTexNode (flat + resident) then
// resolves the key (resolveImageFilterAssetPath, pure-string via SW_ASSETS_DIR — runtime does NOT
// touch ImageIO), decodes+uploads it ONCE via the registered decoder (cachedAssetTexture, NO
// per-frame decode), and hands the cached texture to the leaf via TexCookCtx::assetTexture.
// Absent type (every existing op) = no asset bound, assetTexture stays null — byte-identical path.
std::map<std::string, std::string>& imageFilterAssetTextures();

// Resolve a TiXL "Lib:images/..." asset key to an absolute repo path under SW_ASSETS_DIR. Pure
// string (no platform include) — runtime owns the path math; the platform decoder owns ImageIO.
// Returns "" if SW_ASSETS_DIR is unset. (Mirrors platform::resolveAssetPath, kept runtime-local so
// point_graph.cpp needs no platform include — leaf-seam discipline, ARCHITECTURE.md 葉子接縫.)
std::string resolveImageFilterAssetPath(const std::string& assetKey);

// Asset-decode leaf seam (ARCHITECTURE.md 葉子接縫: runtime exposes a fn-ptr; the APP owns the
// platform decoder and registers it; selftests register a direct shim). Signature mirrors
// platform::decodeImageToTexture: decode the image at `absPath` to an OWNED MTL::Texture* (mipped if
// requested), or nullptr. Until registered, asset binds resolve to null (graceful).
using AssetTextureDecoder = MTL::Texture* (*)(MTL::Device* dev, const char* absPath, bool mipped);
void setAssetTextureDecoder(AssetTextureDecoder fn);
AssetTextureDecoder assetTextureDecoder();

// Cached decoded texture for `assetKey` (decode+upload via the registered decoder on first request,
// then memoize forever — device-global, like the PSO cache; mipped is part of the key). nullptr if
// no decoder is registered or decode fails. Owned by the cache (no caller release). Dropped by
// clearImageFilterAssetCache() so per-run-device selftests get a clean table.
MTL::Texture* cachedAssetTexture(MTL::Device* dev, const std::string& assetKey, bool mipped);
void clearImageFilterAssetCache();

// RAII registrar: declare one file-scope static of this type at the end of each image-filter leaf.
// selftestName/selftest are optional (some ops have no standalone golden); pass both to register a
// `--selftest-<name>` / `--selftest-<name>-bug` pair.
struct ImageFilterOp {
  ImageFilterOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                const char* selftestName = nullptr, int (*selftest)(bool) = nullptr,
                bool mippedOutput = false);
};

// RAII registrar for a FEEDBACK / multi-tex-output op (KeepPreviousFrame / SwapTextures). Reuses the
// SAME spec + selftest sinks as ImageFilterOp (so findSpec/specTypes + run_all discover it), but wires
// the op into the FEEDBACK table (registerFeedbackOp) instead of texReg() — the cook driver routes its
// Texture2D inputs/outputs through the multi-output + optional cross-frame-pair path. `needsPair` = a
// persistent texture pair sized to the first input (KeepPreviousFrame=true / SwapTextures=false);
// `pairFormat` = the MTL::PixelFormat raw value for that pair (ignored when !needsPair).
struct FeedbackOp {
  FeedbackOp(NodeSpec spec, const char* opType, PointFeedbackFn fn, bool needsPair,
             uint32_t pairFormat = 0, const char* selftestName = nullptr,
             int (*selftest)(bool) = nullptr);
};

// RAII registrar for a COMPUTE leaf (-cs.hlsl). Same self-registration as ImageFilterOp PLUS:
//   - inserts cookType into imageFilterComputeTypes() (cookTexNode gives its output ShaderWrite),
//   - if sizeFn != nullptr, stores it in imageFilterSizeFns()[cookType] (output may shrink/grow),
//   - if assetTextureKey != nullptr, registers it in imageFilterAssetTextures() so cookTexNode binds
//     the decoded asset at TexCookCtx::assetTexture (t1). The trailing default = nullptr keeps every
//     existing compute-leaf call site (Crop/FastBlur) byte-identical.
struct ImageFilterComputeOp {
  ImageFilterComputeOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                       ImageFilterSizeFn sizeFn = nullptr, const char* selftestName = nullptr,
                       int (*selftest)(bool) = nullptr, bool mippedOutput = false,
                       const char* assetTextureKey = nullptr);
};

}  // namespace sw
