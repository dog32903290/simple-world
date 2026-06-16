// runtime/value_op_registry — self-registration seam for STATELESS VALUE ops.
//
// Mirror of image_filter_op_registry.h (commit edaff22), pushed down to the value-op layer:
// adding a stateless value op = add ONE leaf .cpp (value_op_<name>.cpp) that ends with a
// file-scope `ValueOp` registrar. No shared list file is edited — the old撞車點 (the central
// mathSpecs() table in node_registry_math.cpp, plus the kTable selftest row) is bypassed for
// new value ops.
//
// Why this is SIMPLER than the image-filter seam: a value op has NO GPU cook. Its entire
// behaviour is the pure `evaluate` fn, which already lives in the NodeSpec.evaluate field
// (graph.h:47). So a value op self-registers by pushing exactly ONE thing — its NodeSpec
// (carrying evaluate) — plus an optional selftest pair. There is no registerTexOp / asset /
// mip / size / compute machinery: those segments of the image-filter registrar are dropped.
//
// How it works (identical lifetime guarantee to ImageFilterOp):
//   - Each leaf defines its registrar at namespace scope. Its constructor runs during pre-main
//     dynamic initialization and:
//       1. pushes its NodeSpec into valueOpSpecSink(),
//       2. (optionally) pushes its {selftestName, selftest} pair into valueOpSelfTests().
//   - The consumers read those sinks only AFTER main starts: node_registry.cpp's findSpec /
//     specTypes (live, uncached) and the selftest dispatcher (runSelftestFromArgs). Because every
//     registrar is a namespace-scope static, all of them finish their dynamic-init constructors
//     before main runs → the sinks are fully populated by first read.
//
// INIT-ORDER (the one subtle line): doc::g_lib is a pre-main static that calls findSpec — but
// only for compound/non-value-op types, and findSpec/specTypes read the sink LIVE (uncached),
// so the file-scope ValueOp registrar's pre-main dynamic-init always completes before any
// post-main read. Same guarantee the image-filter sink already relies on.
//
// FORK / risk (named): intra-family ORDER in the sink follows cross-TU dynamic-init order, which
// is unspecified. This only affects the Add-menu ordering of value ops (cosmetic) — findSpec is
// keyed by type name and .swproj wires are keyed by port id, neither depends on spec position.
//
// SCOPE: this seam serves STATELESS value ops only (pure evaluate fn). Stateful value ops
// (kStatefulValueOps[] — per-instance memory across frames) keep their existing registration;
// a stateful self-registration seam is a separate, later piece of work.
#pragma once
#include <utility>
#include <vector>

#include "runtime/graph.h"  // NodeSpec (carries the pure evaluate fn)

namespace sw {

// Meyers singletons — the two sinks every value-op leaf registrar feeds. Signatures byte-for-byte
// match imageFilterSpecSink() / imageFilterSelfTests() so the consumer loops are copy-paste.
std::vector<NodeSpec>& valueOpSpecSink();
std::vector<std::pair<const char*, int (*)(bool)>>& valueOpSelfTests();

// RAII registrar: declare one file-scope static of this type at the end of each value-op leaf.
// selftestName/selftest are optional (pass both to register a `--selftest-<name>` /
// `--selftest-<name>-bug` pair). No cookType / PointTexFn — a value op has no GPU cook; its
// behaviour is entirely the NodeSpec.evaluate fn.
struct ValueOp {
  ValueOp(NodeSpec spec, const char* selftestName = nullptr, int (*selftest)(bool) = nullptr);
};

}  // namespace sw
