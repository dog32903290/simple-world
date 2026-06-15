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

// RAII registrar: declare one file-scope static of this type at the end of each image-filter leaf.
// selftestName/selftest are optional (some ops have no standalone golden); pass both to register a
// `--selftest-<name>` / `--selftest-<name>-bug` pair.
struct ImageFilterOp {
  ImageFilterOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                const char* selftestName = nullptr, int (*selftest)(bool) = nullptr,
                bool mippedOutput = false);
};

// RAII registrar for a COMPUTE leaf (-cs.hlsl). Same self-registration as ImageFilterOp PLUS:
//   - inserts cookType into imageFilterComputeTypes() (cookTexNode gives its output ShaderWrite),
//   - if sizeFn != nullptr, stores it in imageFilterSizeFns()[cookType] (output may shrink/grow).
struct ImageFilterComputeOp {
  ImageFilterComputeOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                       ImageFilterSizeFn sizeFn = nullptr, const char* selftestName = nullptr,
                       int (*selftest)(bool) = nullptr, bool mippedOutput = false);
};

}  // namespace sw
