// SearchAndReplace string op (string self-registration seam leaf — OriginalString + SearchPattern +
// Replace(String) + UseRegex(bool) → string). TiXL authority:
// Operators/Lib/string/search/SearchAndReplace.cs:16-45 (verbatim below):
//
//   SearchAndReplace.cs Update():
//     var useRegex = UseRegex.GetValue(context);
//     var content = OriginalString.GetValue(context);
//     var replacement = Replace.GetValue(context)?.Replace("\\n","\n");
//     var pattern = SearchPattern.GetValue(context);
//     if (string.IsNullOrEmpty(content) || string.IsNullOrEmpty(replacement)
//         || string.IsNullOrEmpty(pattern)) {
//         Result.Value = content ?? string.Empty;
//         return;
//     }
//     if (useRegex) {
//         try { Result.Value = Regex.Replace(content, pattern, replacement,
//                                            RegexOptions.Multiline | RegexOptions.Singleline); }
//         catch (Exception) { Log.Error($"'{pattern}' is an incorrect search pattern", this); }
//     } else {
//         Result.Value = content.Replace(pattern, replacement, StringComparison.Ordinal);
//     }
//
//   Ports: OriginalString = InputSlot<string>; SearchPattern = InputSlot<string>;
//          Replace = InputSlot<string>; UseRegex = InputSlot<bool>. Output: Result (string).
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode) with THREE String input ports
// (OriginalString, SearchPattern, Replace) + ONE Float param (UseRegex, bool dissolved). The driver's
// gather hands inputStrings in spec port order, one entry per String input port (wired upstream OR
// strDef const). We declare the three String ports in order:
//     inputStrings = [ OriginalString, SearchPattern, Replace ]
// and UseRegex rides params (Float). This is the load-bearing gather contract: the THREE String ports
// land at fixed indices 0/1/2 (mirror of IndexOf's two-String-input gather + a third).
//
// SEMANTICS (ported 1:1 from the .cs):
//   replacement = Replace with literal "\n" → real newline (the SAME replaceLiteralNewline as
//     CombineStrings — C# Replace("\\n","\n"); the source literal "\\n" is two chars: backslash + 'n').
//   Guard (SearchAndReplace.cs:22-28): if content OR replacement OR pattern IsNullOrEmpty →
//     Result = content (or "" if content null). We mirror: write content unchanged and return.
//   UseRegex == false → content.Replace(pattern, replacement, Ordinal) = literal replace-ALL of every
//     non-overlapping occurrence (we implement the std::string find/replace loop).
//   UseRegex == true → std::regex_replace(content, regex(pattern, multiline), replacement). On a BAD
//     pattern the .cs catches the exception, logs, and LEAVES Result.Value UNCHANGED (does NOT write) —
//     we mirror by NOT writing *c.output on std::regex_error (it keeps its prior/empty value). See fork.
//
// FORKS (named):
//   - fork-searchreplace-bool-dissolve-to-float: TiXL UseRegex is InputSlot<bool>; sw has no Bool port
//     type, so it dissolves bool→Float (stored in params; >0.5 == true). Cut32 convention.
//   - fork-searchreplace-bad-pattern-result-unchanged: on a regex compile error, the .cs catch block
//     does NOT assign Result.Value → it keeps whatever it held (default/previous). We reproduce by NOT
//     writing *c.output (it stays at its current value — empty for a fresh node). EXACT behavior match.
//   - fork-searchreplace-regex-flavor-csharp-vs-stdregex: TiXL uses .NET System.Text.RegularExpressions
//     (Regex.Replace, RegexOptions.Multiline|Singleline). We use C++ std::regex (ECMAScript grammar) +
//     std::regex_constants::multiline. They AGREE on common syntax (literals, `.`, `*` `+` `?`, char
//     classes `[...]`, `\d \w \s`, groups, alternation `|`, and `$N` replacement backreferences which
//     are IDENTICAL between C# and ECMAScript) and on Multiline (`^`/`$` match at line boundaries).
//     They DIVERGE on:
//       (a) DOTALL / Singleline: C# RegexOptions.Singleline makes `.` match '\n'. ECMAScript std::regex
//           has NO equivalent flag — `.` NEVER matches '\n'. So a pattern relying on `.` spanning
//           newlines diverges. NOT-PORTABLE; goldens stay clear of this (no `.`-across-newline).
//       (b) .NET-only advanced syntax (named groups (?<name>), balancing groups, \A \Z \G, conditional
//           (?(...)...) ) — unsupported / different in ECMAScript. NOT-PORTABLE.
//     This is an INHERENT-complexity fork (two distinct regex engines), named here, NOT hidden behind a
//     fake-intuition translation. Common-case literal/anchored/char-class regex is byte-parity.
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
//   - fork-no-dirtyflag-trigger: the .cs has no DirtyFlag dance; the flat cook is stateless — no-op fork.
#include <regex>
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// C# Replace("\\n","\n"): every literal two-char backslash-n becomes a real newline (mirror of
// CombineStrings' replaceLiteralNewline — kept local so this leaf stays an independent TU).
std::string replaceLiteralNewline(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'n') {
      out.push_back('\n');
      ++i;  // consume the 'n'
    } else {
      out.push_back(s[i]);
    }
  }
  return out;
}

// Literal replace-ALL (C# string.Replace(pattern, replacement, Ordinal)): every non-overlapping
// occurrence of `pattern` in `content` is replaced. pattern is guaranteed non-empty here (the guard
// already ran), so the loop always advances.
std::string literalReplaceAll(const std::string& content, const std::string& pattern,
                              const std::string& replacement) {
  std::string out;
  std::size_t pos = 0;
  for (;;) {
    std::size_t hit = content.find(pattern, pos);
    if (hit == std::string::npos) {
      out.append(content, pos, std::string::npos);
      break;
    }
    out.append(content, pos, hit - pos);  // copy the gap before the match
    out.append(replacement);              // emit the replacement
    pos = hit + pattern.size();           // skip past the matched pattern (non-overlapping)
  }
  return out;
}

// SearchAndReplace: 3 String inputs (OriginalString/SearchPattern/Replace) + UseRegex Float param.
void cookSearchAndReplace(StringCookCtx& c) {
  if (!c.output) return;

  // inputStrings[0]=OriginalString, [1]=SearchPattern, [2]=Replace (spec port order; wired-OR-const).
  auto gathered = [&](std::size_t i) -> std::string {
    return (c.inputStrings && i < c.inputStrings->size()) ? (*c.inputStrings)[i] : std::string{};
  };
  const std::string content = gathered(0);
  const std::string pattern = gathered(1);
  // Replace gets the literal-"\n"→newline translation (SearchAndReplace.cs:20).
  const std::string replacement = replaceLiteralNewline(gathered(2));

  const bool useRegex = stringFloatParam(c.params, "UseRegex", 0.0f) > 0.5f;  // bool dissolved to Float

  // Guard (SearchAndReplace.cs:22-28): any of content/replacement/pattern empty → Result = content.
  if (content.empty() || replacement.empty() || pattern.empty()) {
    *c.output = content;  // content?? "" — content is already a (possibly empty) std::string
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();  // bug bites the write
    return;
  }

  if (useRegex) {
    // fork-searchreplace-bad-pattern-result-unchanged: on a compile error the .cs catch leaves
    // Result.Value UNCHANGED → we do NOT write *c.output (it keeps its prior/empty value).
    try {
      // ECMAScript grammar + multiline (^/$ at line boundaries) — see flavor fork. (No DOTALL: `.`
      // does NOT match '\n', diverging from C# Singleline — named, NOT-PORTABLE.)
      std::regex re(pattern, std::regex_constants::ECMAScript | std::regex_constants::multiline);
      *c.output = std::regex_replace(content, re, replacement);  // replace-all; $N backrefs match C#
    } catch (const std::regex_error&) {
      // Bad pattern: leave *c.output unchanged (faithful to the .cs catch block — no assignment).
      return;
    }
  } else {
    // Literal replace-all, Ordinal (byte-wise) — SearchAndReplace.cs:43.
    *c.output = literalReplaceAll(content, pattern, replacement);
  }

  // Test-only: corrupt the REAL output (drop the last char) so the golden's RED case fires on the
  // actual cook path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports (ORDER MATTERS for the gather): "Result" output, then the THREE String input ports in order
//   (OriginalString [0], SearchPattern [1], Replace [2]), then UseRegex Float (bool dissolved — rides
//   params, NOT inputStrings).
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//   vecArity, multiInput, strDef}.
static const StringOp _reg_searchandreplace{
    {"SearchAndReplace", "SearchAndReplace",
     {{"Result", "Result", "String", false},
      {"OriginalString", "OriginalString", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false,
       1, false, ""},
      {"SearchPattern", "SearchPattern", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"Replace", "Replace", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""},
      {"UseRegex", "UseRegex", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookSearchAndReplace};

}  // namespace sw
