// runtime/combine — "combine selected children into a new Symbol" (批次 4, 照 TiXL
// Editor/UiModel/Modification/Combine.cs). Pure lib surgery, runtime leaf (compound_graph
// only; no Metal, no upward deps).
//
// Contract (faithful to TiXL, forks named):
//   • selected children MOVE into a new compound Symbol; their ids are REGENERATED
//     (1..N, fresh numbering — TiXL regenerates Guids), overrides/positions carried
//     (positions re-based to the new canvas).
//   • INTERNAL wires (both endpoints selected) move with remapped endpoints, order kept.
//   • INBOUND crossing wires (source outside, target inside) each grow an inputDef named
//     after the target slot (deduped with a counter) + a boundary wire inside + a parent
//     rewire from the old source to the new instance. dataType from the target's def.
//     FORK: the new inputDef's default = the target slot's declared default (TiXL leaves
//     C# type default = 0; ours is more useful when later unwired).
//   • OUTBOUND crossing wires: one outputDef per DISTINCT (sourceChild, sourceSlot) —
//     one inner output feeding N outside targets shares one outputDef, N parent rewires.
//     FORK: TiXL makes one outputDef PER outbound connection; sharing is eval-identical
//     and avoids duplicate defs.
//   • refused when inputDefs+outputDefs would exceed 99 — a kept practical cap on a symbol's
//     external ports (OURS, not TiXL's — TiXL has no port ceiling; same limit the loader
//     enforces). NOT an encoding constraint anymore: the editor's boundary pins sit in their
//     own high id band. Safe to lift later if a real graph ever needs it.
//   • COST (TiXL has the same, via Guid regen): moved children's resident paths change,
//     so per-path runtime state (GPU sim buffers, AudioReaction counters) RESETS on
//     combine — particles visibly restart. Expected, not a bug.
//   • an unresolvable crossing (target/source slot not found in the lib) is DROPPED
//     silently (cook-tolerance spirit; TiXL logs an error instead).
//   • parent: selected children + every wire touching them removed; ONE new instance at
//     the selection's bounding-box center; crossing wires rewired in original order
//     (multi-input order contract).
//   • the new Symbol's id is GENERATED ASCII ("Compound-N", unique in the lib, outside
//     the "sw-type:"/uuid namespaces); `name` is the user's display title (any UTF-8 —
//     CJK survives since the crude_json sw-patch).
//   • NOT undoable (照 TiXL: UndoRedoStack.Clear() — undoing the delete would orphan the
//     new symbol). The CALLER must clear the command stack.
//   • TiXL also moves Animator curves bound to moved children into the new Symbol's
//     Animator (Combine.cs:190 + CopySymbolChildrenCommand.cs:196) — our animator lands
//     in the time lane (S3); this contract note is the hook for it.
#pragma once
#include <string>
#include <vector>

#include "runtime/compound_graph.h"

namespace sw {

struct CombineResult {
  bool ok = false;
  std::string newSymbolId;  // the generated definition id ("" on failure)
  int newChildId = 0;       // the instance's child id in the parent (0 on failure)
  std::string error;        // human-readable reason on failure
};

CombineResult combineChildren(SymbolLibrary& lib, const std::string& parentSymbolId,
                              const std::vector<int>& childIds, const std::string& name);

// Headless RED->GREEN proof: combining {RadialPoints, ParticleSystem} out of the default
// graph yields a new symbol (2 children + the internal wire + 1 inputDef from the inbound
// Turbulence wire + 1 outputDef from the outbound Draw wire), the parent holds one instance
// with both crossings rewired, value-graph evaluation is IDENTICAL pre/post combine
// (resident eval on a Const/Multiply lib), and overrides ride along. injectBug drops a
// parent rewire so the evaluation-identical assertion FAILS (teeth).
int runCombineSelfTest(bool injectBug);

}  // namespace sw
