// field_paramapply_enumcases — the ENUM-SELECTOR MSL-text-assert case TABLE for
// --selftest-field-paramapply, peeled out of field_paramapply_golden.cpp to keep that TU ≤400 lines
// (ARCHITECTURE.md rule 4, NO grandfather bump). The golden owns the HARNESS (assemble a graph via the
// REAL path -> MSL text); this companion owns the DATA (one row per enum-selector field op).
//
// An enum/selector field op stores the selector as an int/bool MEMBER, NOT a packed float — so a different
// enum value re-emits DIFFERENT MSL TEXT (a swizzle, a helper fn, or a fold expr), never a buffer change.
// A buffer round-trip therefore CANNOT see it. Each row pushes a NON-DEFAULT enum value through the REAL
// graph path (Graph + resolveNodeParams -> buildFieldTree -> configureFieldNodeFromParams (table lookup,
// applyIntSelSlot/applyBoolSelSlot) -> member -> assembleFieldMSL) and asserts the assembled MSL:
//   (a) CONTAINS `wantNonDefault` (the text the non-default selector switches TO), and
//   (b) does NOT contain `wantAbsent` (the DEFAULT selector's text, which the switch must have replaced).
// (a)+(b) together prove the selector apply actually fired and flipped the codegen.
#pragma once

#include <string>
#include <vector>

namespace sw {

struct EnumCase {
  const char* type;             // field op type.
  bool combiner;                // true -> wire 2 SphereSDF children (the fold post line needs >=2 inputs).
  const char* enumPort;         // the selector PortSpec.id to override (e.g. "Axis" / "CombineMethod").
  float nonDefault;             // a NON-DEFAULT enum value to push through the graph.
  const char* wantNonDefault;   // substring the non-default selector must EMIT.
  const char* wantAbsent;       // substring the DEFAULT selector emits, which must be GONE after the switch.
  const char* note;             // human label for the printout (what the enum switches).
};

// The full enum-text-assert table (one row per wave-3 enum-selector op).
std::vector<EnumCase> fieldParamApplyEnumCases();

}  // namespace sw
