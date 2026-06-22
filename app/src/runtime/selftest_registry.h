// runtime/selftest_registry — self-registration seam for the CLI --selftest router.
//
// ARCHITECTURE rule 7 (data-driven) applied to the verify router: the old central kTable in
// selftests.cpp grew ~1 row per op (plus a fwd-decl) — a shared撞車點 that pushed the file over
// 400 lines and onto the line-count grandfather. This seam splits that table the SAME way the
// node registries split theirs (point_modify_op_registry / math_op_registry / value_op_registry):
// a Meyers-singleton sink fed by file-scope registrars in per-AREA manifest leaves
// (selftests_<area>.cpp). selftests.cpp becomes a THIN reader: it dispatches / lists / bites by
// iterating this sink (plus the pre-existing imageFilterSelfTests() / valueOpSelfTests() sinks).
//
// Adding a selftest now = add ONE row to the relevant area leaf's REGISTER_SELFTESTS block (or, for
// an image-filter / value-op leaf, the leaf's own ImageFilterOp/ValueOp registrar — already a true
// zero-shared-file add). selftests.cpp is never touched.
//
// ORDER (why each row carries an explicit `order` key): --selftest-list must stay BYTE-IDENTICAL to
// the pre-split baseline (the run_all harness diffs it and every tooth must still enumerate).
// Cross-TU dynamic-init order is unspecified, so we do NOT rely on it: each registration carries the
// global order index of its old kTable row, and selftestRegistry() returns the entries sorted by
// that index. The list is then deterministic regardless of link/init order — identical to the old
// single-table source order, even where an area's rows were interleaved with others in the table.
//
// LIFETIME: every registrar is a file-scope static, so all of them finish their pre-main
// dynamic-init constructors before main runs → the sink is fully populated by first read (same
// guarantee value_op_registry.h documents; the sink itself is a function-local Meyers singleton,
// constructed inside the first registerSelftests() call).
#pragma once
#include <initializer_list>
#include <utility>
#include <vector>

namespace sw {

// One --selftest[-name] / -bug pair. name "" → "--selftest" (the base color eye test).
// fn(bool injectBug) -> process exit code (0 = PASS / GREEN; nonzero = FAIL / RED).
struct SelftestEntry {
  const char* name;
  int (*fn)(bool);
  int order;  // stable global sort key = old kTable row position (see ORDER note above)
};

// The sink. Returned SORTED by `order` so iteration order is deterministic and identical to the old
// single-kTable source order — see ORDER note. Sorting happens lazily on read (cheap: ~230 entries,
// only read by the CLI dispatcher, never on a hot path).
const std::vector<SelftestEntry>& selftestRegistry();

// Append a batch of {name, fn} rows starting at global index `orderBase`. Each area manifest leaf
// calls this once from a file-scope initializer (REGISTER_SELFTESTS). Row i lands at order
// (orderBase + i), so a leaf's rows keep their relative order and slot into the global sequence at
// the positions their kTable rows held. Returns a dummy int (initializes a namespace-scope static).
int registerSelftests(int orderBase,
                      std::initializer_list<std::pair<const char*, int (*)(bool)>> rows);

}  // namespace sw

// One-call registration for an area leaf. ORDER_BASE is the global index of the leaf's FIRST row in
// the old kTable; the rest follow in source order. Usage at file scope inside namespace sw:
//   REGISTER_SELFTESTS(/*orderBase=*/1,
//       {"graph", runGraphRoundtripSelfTest},
//       {"save",  runSaveLoadSelfTest},
//       ...);
// Expands to a uniquely-named namespace-scope static initialized by registerSelftests(), so the rows
// land in the sink during pre-main dynamic init. __LINE__ keeps the static name unique per leaf.
#define SW_SELFTEST_CAT2(a, b) a##b
#define SW_SELFTEST_CAT(a, b) SW_SELFTEST_CAT2(a, b)
#define REGISTER_SELFTESTS(ORDER_BASE, ...)                                       \
  namespace {                                                                     \
  const int SW_SELFTEST_CAT(_selftest_reg_, __LINE__) =                           \
      ::sw::registerSelftests((ORDER_BASE), {__VA_ARGS__});                       \
  }
