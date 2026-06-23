// field_paramapply_cases — the parameterized buffer-round-trip case TABLE for --selftest-field-paramapply,
// peeled out of field_paramapply_golden.cpp to keep that TU ≤400 lines (ARCHITECTURE.md rule 4, NO
// grandfather bump). The golden owns the HARNESS (graph build, GPU readback, enum + BoolSel text checks,
// slot-id guard); this companion owns the DATA (one row per applied slot of every migrated field op).
//
// One row = set `paramId`=`value` on op `type` via the REAL graph path, assert the assembled floatParams
// lands `expect` (within tol) at index `slot`. Adding a slot's coverage = add a row here (data-driven).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace sw {

struct RoundTripCase {
  const char* type;
  std::map<std::string, float> overrides;  // the non-default param(s) to push through the graph.
  const char* paramId;                     // the param under test (printed label).
  int slot;                                // floatParams index its packed value lands at.
  float expect;                            // expected packed value at `slot`.
  const char* note;                        // non-empty -> printed as a [DERIVED] annotation.
};

// The full round-trip table (wave-0/1 already-migrated + proving ops, plus the wave-2 every-slot set).
std::vector<RoundTripCase> fieldParamApplyRoundTripCases();

}  // namespace sw
