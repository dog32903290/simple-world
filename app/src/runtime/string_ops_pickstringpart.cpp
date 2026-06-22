// PickStringPart string op (MULTI-OUTPUT seam consumer — Sub-seam B). Emits TWO outputs in one cook:
//   Fragments (String, port 0 → ctx.output) + TotalCount (Int dissolved to Float, port 1 → scalarOutputs).
// TiXL authority: Operators/Lib/string/logic/PickStringPart.cs (ported below).
//
//   PickStringPart.cs Update():
//     _splitInto = (EntityTypes)SplitInto.GetValue(context);   // 0=Characters 1=Words 2=Lines 3=Sentences
//     var inputText = InputText.GetValue(context);
//     if (inputText == null) { Fragments.Value = null; return; }
//     if (!string.IsNullOrEmpty(inputText)) inputText = inputText.Replace("\\n", "\n");
//     switch (_splitInto):
//       Characters: _chunks = Regex.Split(inputText, "");        _delimiter = "";
//       Words:      _chunks = new Regex("[\\s\\.\\;\\,()`:]+").Split(inputText); _delimiter = " ";
//       Lines:      _chunks = new Regex("\\n+").Split(inputText); _delimiter = "\n";
//       Sentences:  _chunks = new Regex("\\.[\\s\\.]*").Split(inputText); _delimiter = ". ";
//       default:    _chunks = new string[0];
//     _numberOfChunks = (_chunks.Length>0 && IsNullOrEmpty(_chunks[last])) ? _chunks.Length-1 : _chunks.Length;
//     // then:
//     var fragmentStart = FragmentStart.GetValue(context);
//     var fragmentCount = FragmentCount.GetValue(context);
//     Fragments.Value = GetFragment(fragmentStart, fragmentCount);
//     TotalCount.Value = _chunks.Length;   // ★ RAW chunk count (NOT _numberOfChunks)
//
//   GetFragment(startFragment, fragmentCount):
//     if (fragmentCount<=0 || _numberOfChunks==0) return "";
//     sb; for(index=0; index<fragmentCount; index++) {
//        if(index>0) sb.Append(_delimiter);
//        moduloIndex = (startFragment+index) % _numberOfChunks; if(moduloIndex<0) moduloIndex+=_numberOfChunks;
//        sb.Append(_chunks[moduloIndex]); }
//     return sb.ToString();
//
//   Ports: InputText=InputSlot<string>; SplitInto=InputSlot<int>(enum); FragmentStart=InputSlot<int>;
//          FragmentCount=InputSlot<int>. Outputs: Fragments=Slot<string>; TotalCount=Slot<int>.
//
// EVAL-SIDE LAYOUT: a String PRODUCER + Int scalar producer (multi-output). InputText is the ONE String
// input → inputStrings[0] (wired upstream string, or strDef const when unwired). SplitInto/FragmentStart/
// FragmentCount are Int params dissolved to Float (the value spine; fork-int-bool-dissolve-to-float,
// Cut32 convention). Fragments → *output (port 0); TotalCount → scalarOutputs[1] (the multi-output sink).
//
// FORKS (named):
//   - fork-enum-order-characters-words-lines-sentences: SplitInto enum order is Characters=0, Words=1,
//     Lines=2, Sentences=3 (verbatim from PickStringPart.cs EntityTypes). Read as (int)(float) param.
//   - fork-regex-split-csharp-semantics: C# Regex.Split keeps LEADING and TRAILING empty strings around
//     matches at the boundaries. We port each split BY HAND (not std::regex_token_iterator, whose
//     empty-match behaviour is implementation-defined) to match C# exactly:
//       • Characters: Regex.Split(s,"") in C# = ["", c0, c1, …, cn-1, ""] (empty pattern matches at the
//         position BEFORE each char AND at the very end → a leading "" and a trailing ""). For "" input
//         → [""] (one empty chunk). _delimiter = "".
//       • Words/Lines/Sentences: split on the .cs regex (one-or-more of the class). C# keeps a leading ""
//         when the string STARTS with a separator run and a trailing "" when it ENDS with one. We mirror:
//         walk the string, emit the text between consecutive separator-runs, including boundary empties.
//   - fork-totalcount-raw-chunks: TotalCount = _chunks.Length (RAW, INCLUDING the boundary empties) —
//     NOT _numberOfChunks (which strips ONE trailing empty for the GetFragment modulo). Faithful to .cs:87.
//   - fork-int-bool-dissolve-to-float / fork-string-host-not-gpu: as every ported string op.
//   - fork-null-input: C# inputText==null → Fragments=null (TotalCount left untouched). sw has no null
//     std::string; an UNWIRED InputText yields the strDef const "" (empty), which flows the normal path
//     (Characters→[""], else→[""]) — not the C# null short-circuit. Named: a null source is unreachable
//     on the sw rail (a String input is always wire-OR-const, never null).
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

namespace {

// Replace every literal backslash-n ("\\n") with a real newline (PickStringPart.cs:45,
// inputText.Replace("\\n","\n")). Byte-wise, same as the sibling ops' translation.
std::string replaceLiteralNewline(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out.push_back('\n'); ++i; }
    else out.push_back(s[i]);
  }
  return out;
}

// Is `c` a Words-mode separator? C# class "[\s\.\;\,()`:]" = whitespace OR one of . ; , ( ) ` :
bool isWordSep(char c) {
  if (std::isspace(static_cast<unsigned char>(c))) return true;  // \s
  switch (c) {
    case '.': case ';': case ',': case '(': case ')': case '`': case ':': return true;
    default: return false;
  }
}

// Split `s` on MAXIMAL runs of chars for which sep(ch) is true (mirrors C# `new Regex("CLASS+").Split`).
// C# keeps the boundary empties: a leading "" when s starts with a sep run, a trailing "" when it ends
// with one (and consecutive runs collapse — "+" is one-or-more, so a single run is ONE split point).
// Returns the chunk vector in order, including boundary empties — exactly C# Regex.Split semantics.
std::vector<std::string> splitOnSepRuns(const std::string& s, bool (*sep)(char)) {
  std::vector<std::string> chunks;
  std::string cur;
  std::size_t i = 0;
  while (i < s.size()) {
    if (sep(s[i])) {
      // Close the current chunk at this split point, then skip the WHOLE run (the "+" maximal match).
      chunks.push_back(cur);
      cur.clear();
      while (i < s.size() && sep(s[i])) ++i;
    } else {
      cur.push_back(s[i]);
      ++i;
    }
  }
  chunks.push_back(cur);  // trailing chunk (the "" trailing-empty when s ended with a sep run)
  return chunks;
}

// Sentences mode: split on the regex "\.[\s\.]*" = a '.' followed by zero-or-more of (whitespace or '.').
// So a split point is a '.' and it consumes any following run of spaces/dots. Mirror C# Regex.Split:
// emit the text between split points, keep boundary empties (leading "" if s starts with '.', trailing
// "" if s ends with a '.'-run). Reuse the run-skip shape but the run START requires a '.' specifically.
std::vector<std::string> splitOnSentences(const std::string& s) {
  std::vector<std::string> chunks;
  std::string cur;
  std::size_t i = 0;
  while (i < s.size()) {
    if (s[i] == '.') {
      // Split point: the '.' itself, then a maximal run of [\s\.] (whitespace or more dots).
      chunks.push_back(cur);
      cur.clear();
      ++i;  // consume the leading '.'
      while (i < s.size() &&
             (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '.'))
        ++i;
    } else {
      cur.push_back(s[i]);
      ++i;
    }
  }
  chunks.push_back(cur);
  return chunks;
}

// Characters mode: C# Regex.Split(s, "") = ["", c0, c1, …, cn-1, ""].  (empty pattern matches before
// every char and at end → leading "" + trailing ""). For empty s → [""].
std::vector<std::string> splitOnCharacters(const std::string& s) {
  std::vector<std::string> chunks;
  chunks.emplace_back();  // leading "" (the empty match at index 0)
  for (char ch : s) chunks.emplace_back(1, ch);
  chunks.emplace_back();  // trailing "" (the empty match at the end)
  return chunks;
}

// GetFragment (PickStringPart.cs:93-118): join `fragmentCount` chunks starting at `startFragment`,
// wrapping modulo `numberOfChunks`, separated by `delimiter`. Early-empty when count<=0 or no chunks.
std::string getFragment(const std::vector<std::string>& chunks, int numberOfChunks,
                        const std::string& delimiter, int startFragment, int fragmentCount) {
  if (fragmentCount <= 0 || numberOfChunks == 0) return "";
  std::string sb;
  for (int index = 0; index < fragmentCount; ++index) {
    if (index > 0) sb += delimiter;
    int moduloIndex = (startFragment + index) % numberOfChunks;
    if (moduloIndex < 0) moduloIndex += numberOfChunks;
    sb += chunks[static_cast<std::size_t>(moduloIndex)];
  }
  return sb;
}

void cookPickStringPart(StringCookCtx& c) {
  if (!c.output) return;

  // InputText: the ONE String input port → inputStrings[0] (wired upstream string or strDef const "").
  std::string inputText =
      (c.inputStrings && !c.inputStrings->empty()) ? (*c.inputStrings)[0] : std::string{};

  // \\n → \n literal translation (PickStringPart.cs:45; only when non-empty, matching the .cs guard).
  if (!inputText.empty()) inputText = replaceLiteralNewline(inputText);

  const int splitInto     = (int)stringFloatParam(c.params, "SplitInto", 0.0f);  // enum
  const int fragmentStart = (int)stringFloatParam(c.params, "FragmentStart", 0.0f);
  const int fragmentCount = (int)stringFloatParam(c.params, "FragmentCount", 0.0f);

  std::vector<std::string> chunks;
  std::string delimiter;
  switch (splitInto) {
    case 0:  // Characters
      chunks = splitOnCharacters(inputText); delimiter = ""; break;
    case 1:  // Words: "[\s\.\;\,()`:]+"
      chunks = splitOnSepRuns(inputText, &isWordSep); delimiter = " "; break;
    case 2:  // Lines: "\n+"
      chunks = splitOnSepRuns(inputText, +[](char ch) { return ch == '\n'; }); delimiter = "\n"; break;
    case 3:  // Sentences: "\.[\s\.]*"
      chunks = splitOnSentences(inputText); delimiter = ". "; break;
    default:
      chunks.clear(); delimiter = ""; break;  // PickStringPart.cs default → empty string[0]
  }

  // _numberOfChunks: strip ONE trailing empty for the GetFragment modulo (PickStringPart.cs:75-77).
  const int rawCount = (int)chunks.size();
  const int numberOfChunks =
      (rawCount > 0 && chunks[static_cast<std::size_t>(rawCount - 1)].empty()) ? rawCount - 1 : rawCount;

  // Port 0 (Fragments) → *output. (No injectBug yet — corrupt AFTER both outputs so the tooth bites.)
  *c.output = getFragment(chunks, numberOfChunks, delimiter, fragmentStart, fragmentCount);

  // Port 1 (TotalCount) → scalarOutputs[1]. ★ RAW _chunks.Length (fork-totalcount-raw-chunks), Int→Float.
  if (c.scalarOutputs) (*c.scalarOutputs)[1] = (float)rawCount;

  // Test-only: corrupt the REAL output(s) so the golden's RED bites on the actual cook path. Drop the
  // last char of Fragments AND knock TotalCount off its true value — both multi-output channels go RED.
  if (stringInjectBug()) {
    if (!c.output->empty()) c.output->pop_back();
    if (c.scalarOutputs) (*c.scalarOutputs)[1] = -999.0f;  // sentinel ≠ any valid count
  }
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Output ports (the MULTI-OUTPUT layout — port index is the scalarOutputs/extraStrOutputs key):
//     [0] "Fragments"  = String output  (MAIN → ctx.output → extStrOut[0] / stringBuf[id])
//     [1] "TotalCount" = Float output   (Int dissolved → scalarOutputs[1] → extOut[1] / outCache[1])
//   Input ports (gathered after the outputs; only "InputText" is a String → inputStrings[0]):
//     [2] "InputText"     = String input (wire-OR-const; strDef "")
//     [3] "SplitInto"     = Float/Int (enum: 0=Characters 1=Words 2=Lines 3=Sentences) — rides params
//     [4] "FragmentStart" = Float/Int — rides params
//     [5] "FragmentCount" = Float/Int — rides params
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//                         vecArity, multiInput, strDef}.
static const StringOp _reg_pickstringpart{
    {"PickStringPart", "PickStringPart",
     {{"Fragments",  "Fragments",  "String", false},
      {"TotalCount", "TotalCount", "Float",  false},
      {"InputText",  "InputText",  "String", true,  0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, ""},
      {"SplitInto",     "SplitInto",     "Float", true, 0.0f, 0.0f, 3.0f, Widget::Enum,
       {"Characters", "Words", "Lines", "Sentences"}},
      {"FragmentStart", "FragmentStart", "Float", true, 0.0f, -1000000.0f, 1000000.0f, Widget::Slider},
      {"FragmentCount", "FragmentCount", "Float", true, 0.0f, -1000000.0f, 1000000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookPickStringPart};

}  // namespace sw
