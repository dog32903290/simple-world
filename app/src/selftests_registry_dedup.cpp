// app/src/selftests_registry_dedup.cpp — selftest gate: no two registered node specs may share the
// same type name. A duplicate causes imgui to emit "visible items with conflicting ID" and produces
// an erroneous extra entry in the Add menu. This gate prevents regressions such as the vec-math
// wave-1/wave-2 double-registration bug (MathOp old-style + ValueOp new-style both registered the
// same 21 op names into the spec pool consumed by specTypes()).
//
// Shell-tier (app/src/ root): may include runtime and node-registry headers.
// Self-registers into selftestRegistry() via REGISTER_SELFTESTS (selftest_registry.h convention).
//
// --selftest-specdedup       : enumerate specTypes(); FAIL (exit nonzero) if any name appears >1×.
// --selftest-specdedup-bug   : run the same detector on a synthetic list with a deliberate dup;
//                              must exit nonzero — proves the detector fires on a real dup.
//                              (If this exits 0 the detector is blind; harness reports NO-BITE.)
#include "runtime/selftest_registry.h"
#include "runtime/graph.h"  // sw::specTypes()

#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace sw {
namespace {

// Returns 0 (PASS) when `types` has no duplicates.
// Returns 1 (FAIL) and prints every duplicated name when there are duplicates.
int checkNoDuplicates(const std::vector<std::string>& types, const char* label) {
  std::unordered_map<std::string, int> counts;
  counts.reserve(types.size());
  for (const auto& t : types) ++counts[t];

  bool anyDup = false;
  for (const auto& kv : counts) {
    if (kv.second > 1) {
      std::printf("[specdedup] %s DUPLICATE (×%d): %s\n", label, kv.second, kv.first.c_str());
      anyDup = true;
    }
  }

  if (!anyDup) {
    std::printf("[specdedup] %s: %zu unique names — PASS\n", label, types.size());
    return 0;
  }
  std::printf("[specdedup] %s: %zu total names, duplicates found — FAIL\n", label, types.size());
  return 1;
}

int runSpecDedupSelfTest(bool injectBug) {
  if (injectBug) {
    // Bug-injection path: feed the detector a synthetic list with a known dup.
    // checkNoDuplicates must return nonzero — that nonzero IS the expected FAIL for -bug.
    // If it returns 0 (no dup detected) the detector is blind and the harness will flag NO-BITE.
    std::vector<std::string> synth = {"AddVec2", "SubVec3", "AddVec2", "Magnitude"};
    return checkNoDuplicates(synth, "synthetic-dup-injection");
  }

  // Normal path: check the real registry pool that specTypes() returns (the exact same list the
  // Add menu iterates — same surface the imgui conflict fires on).
  const std::vector<std::string> types = specTypes();
  return checkNoDuplicates(types, "specTypes()");
}

}  // namespace

// Order 302: one past the highest existing block (301 = buildrandomstring_golden.cpp).
REGISTER_SELFTESTS(/*orderBase=*/302, {"specdedup", runSpecDedupSelfTest});

}  // namespace sw
