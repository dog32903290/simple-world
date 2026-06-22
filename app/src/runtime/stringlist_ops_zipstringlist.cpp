// ZipStringList stringlist op (stringlist self-registration seam leaf — StringsOne(List<string>) +
// StringsTwo(List<string>) → List<string>). Sub-seam A: the FIRST list-in→list-OUT op on the StringList
// currency (consumes TWO StringLists via the StringListCookCtx::inputLists gather, produces ONE StringList
// via *output — a StringList PRODUCER like SplitString, but driven by two StringList inputs instead of a
// String). TiXL authority: Operators/Lib/string/list/ZipStringList.cs (verbatim below):
//
//   ZipStringList.cs Update():
//     var strOne = StringsOne.GetValue(context);
//     var strTwo = StringsTwo.GetValue(context);
//     if (strOne == null || strTwo == null) { Output.Value = []; return; }   // either null → empty
//     Output.Value = [.. strOne.Zip(strTwo, (a, b) => new[] {a, b}).SelectMany(t => t)];
//
//   Ports: StringsOne = InputSlot<List<string>>;  StringsTwo = InputSlot<List<string>>.
//   Output: Output = Slot<List<string>>.
//
// EVAL-SIDE LAYOUT: a StringList PRODUCER (rides the stringlist cook flow — the SAME path SplitString
// rides), but its inputs are the NEW shape for this channel: TWO StringList inputs → gathered into
// StringListCookCtx::inputLists in SPEC PORT ORDER (StringsOne=inputLists[0], StringsTwo=inputLists[1]).
// Both flat (cookStringListNode's StringList-input loop) and resident (cookResidentStringList's StringList
// branch) ALREADY gather inputLists by spec port order for a stringlist op — ZipStringList composes that
// EXISTING plumbing (SplitString's list-OUTPUT + the dormant inputLists list-INPUT gather, both already
// present for "a future combiner"); NO new wiring. No String inputs, no Float params.
//
// ZIP SEMANTICS (ported 1:1 from the .cs — LINQ Enumerable.Zip + SelectMany):
//   • LINQ Zip pairs element i of StringsOne with element i of StringsTwo, in order, producing the pair
//     {a_i, b_i}; SelectMany flattens the pairs → INTERLEAVE: [a0, b0, a1, b1, a2, b2, ...].
//   • UNEQUAL LENGTHS: LINQ Zip stops at the SHORTER list — pairs run for i ∈ [0, min(|A|,|B|)). The
//     TAIL of the longer list is DROPPED (no padding, no leftover). Output length = 2 * min(|A|,|B|).
//   • Either list null → empty output. sw has no null vs empty distinction for a host list, so an UNWIRED
//     StringList input contributes NO entry to inputLists → that list is treated as empty → min(_, 0) == 0
//     → empty output, faithful to BOTH .cs null-guards (null and empty both yield []).
//
// FORKS (named):
//   - fork-zipstringlist-interleave-order: output is [a0,b0,a1,b1,...] — StringsOne element FIRST in each
//     pair (the .cs new[]{a,b} order, a=StringsOne). The load-bearing ORDER assertion (not [b0,a0,...]
//     and not concatenation [a0,a1,...,b0,b1,...]).
//   - fork-zipstringlist-truncate-to-shorter: LINQ Zip truncates to the shorter list; the longer list's
//     tail is dropped. Output length = 2 * min(|A|,|B|). Ported verbatim (no padding).
//   - fork-zipstringlist-null-is-empty: either input null/unwired → empty output (the .cs null-guard;
//     unwired host list = empty = no inputLists entry → handled as empty).
//   - fork-string-host-not-gpu: string list is host currency; no GPU EvaluationContext touched.
#include <string>
#include <vector>

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/stringlist_op_registry.h"  // StringListOp / StringListCookCtx / stringListInjectBug

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// ZipStringList: interleave inputLists[0] (StringsOne) and inputLists[1] (StringsTwo) up to the shorter
// length. Implements ZipStringList.cs 1:1 (LINQ Zip + SelectMany).
void cookZipStringList(StringListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // inputLists are gathered in spec port order: [0] = StringsOne, [1] = StringsTwo. An UNWIRED StringList
  // input contributes NO entry → that list reads as empty (the .cs null → []). So either missing → empty.
  const std::vector<std::string> a =
      (c.inputLists && c.inputLists->size() > 0) ? (*c.inputLists)[0] : std::vector<std::string>{};
  const std::vector<std::string> b =
      (c.inputLists && c.inputLists->size() > 1) ? (*c.inputLists)[1] : std::vector<std::string>{};

  // LINQ Zip: pair up to the SHORTER list; SelectMany flattens → [a0,b0,a1,b1,...] (StringsOne first).
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  c.output->reserve(2 * n);
  for (std::size_t i = 0; i < n; ++i) {
    c.output->push_back(a[i]);  // StringsOne element first in each pair (new[]{a,b})
    c.output->push_back(b[i]);  // StringsTwo element second
  }

  // Test-only: corrupt the REAL output (drop the last element) so a golden's interleave/truncate RED bites
  // on the actual cook path, not by flipping the expected value. Off in production.
  if (stringListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringListOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order for inputLists):
//     [0] "Output"     = StringList output  (the host string-list currency — StringList PRODUCER)
//     [1] "StringsOne" = StringList input   (inputLists[0]; the .cs StringsOne, FIRST in each zipped pair)
//     [2] "StringsTwo" = StringList input   (inputLists[1]; the .cs StringsTwo, SECOND in each pair)
//   The stringlist driver gathers StringList input ports into inputLists in spec port order: ports [1]/[2]
//   are the two StringList inputs → inputLists[0]==StringsOne, inputLists[1]==StringsTwo. Both are SINGLE
//   inputs (InputSlot<List<string>>, not MultiInput → multiInput=false; the gather takes the first wire).
static const StringListOp _reg_zipstringlist{
    {"ZipStringList", "ZipStringList",
     {{"Output", "Output", "StringList", false},
      {"StringsOne", "StringsOne", "StringList", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, ""},
      {"StringsTwo", "StringsTwo", "StringList", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, ""}},
     /*evaluate=*/nullptr},  // StringList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookZipStringList};

}  // namespace sw
