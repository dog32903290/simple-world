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
#include <utility>
#include <vector>

#include "runtime/graph.h"        // NodeSpec
#include "runtime/point_graph.h"  // PointTexFn, registerTexOp

namespace sw {

// Meyers singletons — the two sinks every image-filter leaf registrar feeds.
std::vector<NodeSpec>& imageFilterSpecSink();
std::vector<std::pair<const char*, int (*)(bool)>>& imageFilterSelfTests();

// RAII registrar: declare one file-scope static of this type at the end of each image-filter leaf.
// selftestName/selftest are optional (some ops have no standalone golden); pass both to register a
// `--selftest-<name>` / `--selftest-<name>-bug` pair.
struct ImageFilterOp {
  ImageFilterOp(NodeSpec spec, const char* cookType, PointTexFn cook,
                const char* selftestName = nullptr, int (*selftest)(bool) = nullptr);
};

}  // namespace sw
