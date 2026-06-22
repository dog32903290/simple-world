// StringInsert string op (string self-registration seam leaf — Original(String) + Insertion(String) +
// Position(Int) + UseModuloPosition(bool) → String). TiXL authority:
// Operators/Lib/string/buffers/transform/StringInsert.cs (verbatim below):
//
//   StringInsert.cs Update():
//     var original = Original.GetValue(context);
//     var insert = Insertion.GetValue(context);
//     if (string.IsNullOrEmpty(original) || string.IsNullOrEmpty(insert))
//         return;
//     var maxPosition = original.Length - insert.Length;
//     if (maxPosition <= 0)
//         return;
//     var position = Position.GetValue(context);
//     if (UseModuloPosition.GetValue(context))
//     {
//         position = Math.Abs(position) % maxPosition;
//     }
//     else
//     {
//         position.Clamp(0, maxPosition);   // ★ NO-OP: return value discarded — position NOT clamped
//     }
//     try
//     {
//         Result.Value = original.Remove(position, insert.Length).Insert(position, insert);
//     }
//     catch(Exception e)
//     {
//         Log.Warning("Failed to insert string:" + e);
//     }
//
//   Ports: Original/Insertion = InputSlot<string>; Position = InputSlot<int>;
//          UseModuloPosition = InputSlot<bool>. Output: Result = Slot<string>.
//   Semantics: remove `insert.Length` chars at `position`, then insert `insert` there — i.e. OVERWRITE
//   `insert.Length` chars of `original` starting at `position` with `insert`. (NOT a pure insertion;
//   the .cs name is misleading — it Remove()s first, so it's a splice/overwrite of equal length.)
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). Original + Insertion are its TWO String
// input ports → inputStrings[0]=Original, inputStrings[1]=Insertion (wired upstream, or strDef const
// "" when unwired). Position (Int) and UseModuloPosition (bool) are params dissolved to Float (the
// value spine, resolved via stringFloatParam / params).
//
// ★★ FAITHFULLY-PORTED TIXL BUG (named fork-stringinsert-clamp-noop): in the NON-modulo branch, TiXL
// calls `position.Clamp(0, maxPosition)` as a STATEMENT and DISCARDS the return value (C#'s Clamp is a
// pure extension method — `int Clamp(this int v, …)` — it does NOT mutate `position` in place). So the
// non-modulo path uses the RAW (unclamped) position. If that raw position is out of range,
// `original.Remove(position, insert.Length)` throws ArgumentOutOfRangeException, caught → Result
// UNCHANGED. We reproduce this 1:1: non-modulo path does NOT clamp; out-of-range → no write (→ empty,
// see fork-stringinsert-unset-becomes-empty). Porting the clamp "correctly" would be a parity DIVERGENCE.
//
// FORKS (named):
//   - fork-stringinsert-clamp-noop: the discarded-return-value Clamp bug, ported 1:1 (see above).
//   - fork-int-bool-dissolve-to-float: Position (int) and UseModuloPosition (bool) dissolve to Float
//     (Cut32 convention). UseModuloPosition is read as (modF != 0.0f) ? true : false; Position is
//     (int)(float). Bools ride the value spine as 0.0/1.0.
//   - fork-stringinsert-unset-becomes-empty: TiXL's early-returns (null/empty input, maxPosition<=0)
//     and the catch (out-of-range Remove) LEAVE Result at its prior value (null/empty on first eval).
//     sw's driver-owned stringBuf persists per node id across cooks, so to stay deterministic we WRITE
//     the empty string on every non-writing path (same convention as SubString's early-exit). For a
//     freshly-evaluated node the observable downstream contract (empty) is identical to TiXL's.
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <cmath>
#include <cstdlib>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// StringInsert: Original + Insertion (String inputs) + Position (Int param) + UseModuloPosition (bool
// param) → host string. Implements StringInsert.cs Update() 1:1 INCLUDING the discarded-clamp bug.
void cookStringInsert(StringCookCtx& c) {
  if (!c.output) return;

  // Original = inputStrings[0]; Insertion = inputStrings[1] (spec port order). Unwired → strDef "".
  std::string original;
  std::string insert;
  if (c.inputStrings) {
    if (c.inputStrings->size() > 0) original = (*c.inputStrings)[0];
    if (c.inputStrings->size() > 1) insert   = (*c.inputStrings)[1];
  }

  // StringInsert.cs:20-21 — IsNullOrEmpty(original) || IsNullOrEmpty(insert) → return (unset → empty).
  if (original.empty() || insert.empty()) {
    *c.output = std::string{};
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  // StringInsert.cs:23-26 — maxPosition = original.Length - insert.Length; if <= 0 → return.
  const int maxPosition = (int)original.size() - (int)insert.size();
  if (maxPosition <= 0) {
    *c.output = std::string{};
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  // Position (Int param dissolved to Float) + UseModuloPosition (bool param dissolved to Float).
  int position = (int)stringFloatParam(c.params, "Position", 0.0f);
  const bool useModulo = stringFloatParam(c.params, "UseModuloPosition", 0.0f) != 0.0f;

  if (useModulo) {
    // StringInsert.cs:32 — position = Math.Abs(position) % maxPosition. (maxPosition > 0 here, so the
    // C# % is well-defined and non-negative since Abs() is non-negative.)
    position = std::abs(position) % maxPosition;
  } else {
    // StringInsert.cs:36 — position.Clamp(0, maxPosition) — RETURN VALUE DISCARDED (the faithful bug).
    // position is left RAW (unclamped) on this path. fork-stringinsert-clamp-noop.
  }

  // StringInsert.cs:41 — original.Remove(position, insert.Length).Insert(position, insert), in a
  // try/catch. C# string.Remove(start, count) throws if start<0 || count<0 || start+count>Length.
  // Here count = insert.Length > 0. The valid range for position is [0, original.Length - insert.Length]
  // = [0, maxPosition]. Out of range → C# throws → caught → Result unchanged (→ empty here).
  const int insLen = (int)insert.size();
  if (position < 0 || position + insLen > (int)original.size()) {
    // Out-of-range Remove would throw in TiXL → caught → Result unchanged. fork-…-unset-becomes-empty.
    *c.output = std::string{};
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  // In range: Remove(position, insLen) then Insert(position, insert) = overwrite insLen chars at
  // `position` with `insert`. Net effect: original[0,position) + insert + original[position+insLen, end).
  std::string out = original;
  out.erase(static_cast<std::string::size_type>(position),
            static_cast<std::string::size_type>(insLen));
  out.insert(static_cast<std::string::size_type>(position), insert);
  *c.output = out;

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order for inputStrings):
//     [0] "Result"            = String output (the host string currency — String PRODUCER)
//     [1] "Original"          = String input  (wire-OR-const; strDef "")  → inputStrings[0]
//     [2] "Insertion"         = String input  (wire-OR-const; strDef "")  → inputStrings[1]
//     [3] "Position"          = Float/Int input (value spine; Int dissolved to Float; default 0)
//     [4] "UseModuloPosition" = Float/bool input (value spine; bool dissolved to Float; default 0)
//   The driver gathers String input ports into inputStrings in spec order: ports [1] and [2] are
//   String inputs → inputStrings[0]=Original, inputStrings[1]=Insertion. Position + UseModuloPosition
//   ride params.
static const StringOp _reg_stringinsert{
    {"StringInsert", "StringInsert",
     {{"Result",            "Result",            "String", false},
      {"Original",          "Original",          "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, ""},
      {"Insertion",         "Insertion",         "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, ""},
      {"Position",          "Position",          "Float",  true, 0.0f, -1000000.0f, 1000000.0f,
       Widget::Slider},
      {"UseModuloPosition", "UseModuloPosition", "Float",  true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookStringInsert};

}  // namespace sw
