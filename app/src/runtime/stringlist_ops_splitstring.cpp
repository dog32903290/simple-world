// SplitString stringlist op (stringlist self-registration seam leaf — String input + Split(String) ->
// List<string>). Sub-seam A: the FIRST StringList PRODUCER (the proving producer for the StringList
// currency channel + the genuinely-WIRED list source the JoinStringList resident golden gathers from).
// TiXL authority: Operators/Lib/string/list/SplitString.cs (verbatim below):
//
//   SplitString.cs Update():
//     var split = Split.GetValue(context);
//     var c = (split.Length == 0 || split == "\\n") ? '\n' : split[0];
//     var str = String.GetValue(context);
//     if (string.IsNullOrEmpty(str)) { Fragments.Value = _emptyList; return; }
//     Fragments.Value = str.Split(c).ToList();
//     Count.Value = Fragments.Value.Count;
//
//   Ports: String = InputSlot<string> (default "Line\nLine"); Split = InputSlot<string> (default "\n").
//   Outputs: Fragments = Slot<List<string>>;  Count = Slot<int> (the fragment count).
//
// EVAL-SIDE LAYOUT: a StringList PRODUCER (rides the stringlist cook flow). String is the ONE String
// input → inputStrings[0] (wired upstream string, or strDef const when unwired). Split is the SECOND
// String input → inputStrings[1] (wire-OR-const, default "\n"). Both ride the StringCookCtx-style String
// gather the stringlist driver performs (mirror of the String rail's gather).
//
// SPLIT-CHAR SEMANTICS (ported 1:1 from the .cs):
//   c = (split empty OR split == literal "\\n") ? '\n' : split[0]   — only the FIRST char of Split is
//       used as the delimiter (C# char Split), EXCEPT the literal two-char sequence backslash-n maps to a
//       real newline (the .t3-editor escape, same convention as CombineStrings's separator \\n→\n fork).
//   empty str → empty list (the .cs _emptyList early-return).
//   str.Split(c) → C# String.Split with NO options: ADJACENT delimiters yield EMPTY fragments, and a
//       trailing delimiter yields a trailing empty fragment (NOT RemoveEmptyEntries). We mirror this
//       exactly (std::string scan, push every segment incl. empties).
//
// COUNT OUTPUT (port 1, Int→Float dissolve): TiXL's Count = Fragments.Count. The StringList cook flow is
// single-(list)-output today (no multi-output sink on StringListCookCtx — unlike StringCookCtx Sub-seam B),
// so Count is NOT transported here. fork-splitstring-count-deferred: Count is a derived scalar (= list
// .size()) a consumer can recompute; wiring it would need a scalarOutputs sink on StringListCookCtx. The
// PRODUCING-list output (Fragments) is the load-bearing channel for Sub-seam A (JoinStringList consumes it).
//
// FORKS (named):
//   - fork-splitchar-first-char-only: TiXL splits on a single CHAR (split[0]); multi-char Split delimiters
//     are truncated to their first char (faithful to char Split), EXCEPT literal "\\n" → newline.
//   - fork-csharp-split-keeps-empties: C# String.Split default keeps empty fragments (adjacent/leading/
//     trailing delimiters); ported verbatim (no RemoveEmptyEntries).
//   - fork-splitstring-count-deferred: the Int Count output is not transported (single-list-output flow).
//   - fork-string-host-not-gpu: string list is host currency; no GPU EvaluationContext touched.
#include <string>
#include <vector>

#include "runtime/graph.h"                    // NodeSpec, PortSpec, Widget
#include "runtime/stringlist_op_registry.h"  // StringListOp / StringListCookCtx / stringListInjectBug

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// Split `str` on the single char `c`, C# String.Split semantics (KEEP empty fragments: adjacent /
// leading / trailing delimiters produce empty entries). A string with no delimiter → one fragment (str).
void splitKeepEmpties(const std::string& str, char c, std::vector<std::string>& out) {
  out.clear();
  std::string::size_type start = 0;
  while (true) {
    std::string::size_type pos = str.find(c, start);
    if (pos == std::string::npos) {
      out.push_back(str.substr(start));  // final segment (may be empty if str ended on a delimiter)
      break;
    }
    out.push_back(str.substr(start, pos - start));  // segment before the delimiter (may be empty)
    start = pos + 1;
  }
}

// SplitString: String (input 0) + Split (input 1) → host string list. Implements SplitString.cs 1:1.
void cookSplitString(StringListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // inputStrings[0] = String (the text to split); inputStrings[1] = Split (the delimiter spec).
  const std::string str =
      (c.inputStrings && c.inputStrings->size() > 0) ? (*c.inputStrings)[0] : std::string{};
  const std::string split =
      (c.inputStrings && c.inputStrings->size() > 1) ? (*c.inputStrings)[1] : std::string{"\n"};

  // c = (split empty OR literal "\\n") ? '\n' : split[0]  (SplitString.cs:21-23).
  char delim;
  if (split.empty() || split == "\\n")
    delim = '\n';
  else
    delim = split[0];

  // empty str → empty list (SplitString.cs:26-30, the _emptyList early-return).
  if (str.empty()) {
    // (output already cleared = empty list)
  } else {
    splitKeepEmpties(str, delim, *c.output);  // str.Split(c).ToList() — keeps empties
  }

  // Test-only: corrupt the REAL output (drop the last fragment) so the golden's RED case fires on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringListInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringListOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order for inputStrings):
//     [0] "Fragments" = StringList output (the host string-list currency — StringList PRODUCER)
//     [1] "String"    = String input  (wire-OR-const; strDef "Line\nLine" = the TiXL default text)
//     [2] "Split"     = String input  (wire-OR-const; strDef "\n" = split on newline by default)
//   The stringlist driver gathers String input ports into inputStrings in spec order: ports [1]/[2] are
//   the two String inputs → inputStrings[0]==String, inputStrings[1]==Split. (Count, the TiXL Int output,
//   is not exposed — fork-splitstring-count-deferred above.)
static const StringListOp _reg_splitstring{
    {"SplitString", "SplitString",
     {{"Fragments", "Fragments", "StringList", false},
      {"String", "String", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "Line\nLine"},
      {"Split", "Split", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "\n"}},
     /*evaluate=*/nullptr},  // StringList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookSplitString};

}  // namespace sw
