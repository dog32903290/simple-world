// runtime/selftest_registry — the sink + registerSelftests() + sorted read.
// See selftest_registry.h for the full self-registration contract (mirror of value_op_registry.cpp,
// plus an explicit global `order` sort so --selftest-list stays byte-identical to the old kTable
// order even where area rows were interleaved).
#include "runtime/selftest_registry.h"

#include <algorithm>

namespace sw {

// Raw insertion sink: a function-local Meyers singleton, constructed inside the first
// registerSelftests() call during pre-main dynamic init. Entries land in TU/link order (unspecified)
// — order is recovered on read by sorting on `order`.
static std::vector<SelftestEntry>& rawSink() {
  static std::vector<SelftestEntry> sink;
  return sink;
}

int registerSelftests(int orderBase,
                      std::initializer_list<std::pair<const char*, int (*)(bool)>> rows) {
  int i = 0;
  for (const auto& r : rows) {
    rawSink().push_back(SelftestEntry{r.first, r.second, orderBase + i});
    ++i;
  }
  return static_cast<int>(rows.size());
}

const std::vector<SelftestEntry>& selftestRegistry() {
  // Sort once on first read into a stable cache, keyed on the global order index. Deterministic
  // regardless of cross-TU init order → identical to the old single-kTable source order.
  static const std::vector<SelftestEntry> sorted = [] {
    std::vector<SelftestEntry> v = rawSink();
    std::stable_sort(v.begin(), v.end(),
                     [](const SelftestEntry& a, const SelftestEntry& b) { return a.order < b.order; });
    return v;
  }();
  return sorted;
}

}  // namespace sw
