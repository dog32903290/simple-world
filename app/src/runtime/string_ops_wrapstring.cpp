// WrapString string op (string self-registration seam leaf — InputText(String) + WrapColumn(int) +
// Mode(enum) → String). TiXL authority: Operators/Lib/string/transform/WrapString.cs (ported 1:1):
//
//   WrapString.cs Update():
//     var str = InputText.GetValue(context);
//     var mode = Mode.GetEnumValue<WrapLinesModes>(context);
//     var wrapColumn = WrapColumn.GetValue(context).Clamp(1,10000);
//     _stringBuilder.Clear(); _stringBuilder.Append(str);
//     InsertLineWraps(mode, _stringBuilder, 0, 0, wrapColumn);   // (insertPos/insertLength unused)
//     Result.Value = _stringBuilder.ToString();
//
//   enum WrapLinesModes { DontWrap=0, WrapAtWords=1, WrapAtCharacters=2, WrapToFillBlock=3, SolidBlock=4 };
//   Ports: InputText = InputSlot<string>; WrapColumn = InputSlot<int>; Mode = InputSlot<int> (Mapped).
//   InsertLineWraps mutates the buffer IN PLACE per mode (verbatim algorithms below).
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). InputText is its ONE String input →
// inputStrings[0] (wired upstream string, or strDef "" when unwired). WrapColumn + Mode are read from
// the RESOLVED Float params (sc.params, the value spine — WrapColumn is an int stored as Float, Mode is
// Widget::Enum stored as Float index), the same int-on-Float contract as ChangeCase.Mode / SubString.
//
// FORKS (named):
//   - fork-wrapcolumn-int-on-float: TiXL WrapColumn is InputSlot<int>; sw carries it as a resolved Float
//     param truncated to int (same int-on-Float contract every numeric param uses). Clamp(1,10000) is
//     faithful to .Clamp(1,10000).
//   - fork-wrapstring-dontwrap-and-wrapatchars-passthrough: DontWrap (0) hits no branch in TiXL's
//     InsertLineWraps → buffer unchanged → passthrough. WrapAtCharacters (2) is DEPRECATED in TiXL: its
//     branch only Log.Warning()s and does NOT touch the buffer → also passthrough. We reproduce both as
//     a verbatim no-op (no log; sw has no Log.Warning sink on the cook rail — the OBSERVABLE string is
//     byte-identical, which is what parity asserts).
//   - fork-wrapstring-stringbuilder-as-stdstring: TiXL uses a reusable StringBuilder field; sw rebuilds
//     a std::string per cook (stateless — no cross-frame buffer). Byte-identical output; the StringBuilder
//     was a TiXL alloc optimization, not semantics.
//   - fork-changecase-null-input (shared): unwired InputText yields strDef "" (sw has no null strings).
//     Wrapping "" → "" for every mode (empty buffer, the while-loops never enter) = no-op, same as TiXL
//     str=null → Append(null) → empty buffer.
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <cstdlib>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

namespace {

// WrapLinesModes (verbatim TiXL enum order).
enum WrapMode { kDontWrap = 0, kWrapAtWords = 1, kWrapAtChars = 2, kWrapToFillBlock = 3, kSolidBlock = 4 };

// Is c one of TiXL's word-break characters (' ' | '.' | ',' | '/')?
inline bool isBreakChar(char c) { return c == ' ' || c == '.' || c == ',' || c == '/'; }

// InsertLineWraps — mutates `sb` in place per mode (WrapString.cs:InsertLineWraps verbatim, 1:1).
void insertLineWraps(int mode, std::string& sb, int wrapColumn) {
  if (mode == kWrapAtChars) {
    // TiXL: "WrapAtCharacters has been deprecated." — Log.Warning only, buffer untouched (passthrough).
    return;
  }
  if (mode == kWrapAtWords) {
    int pos = 0, currentLineLength = 0, lastValidBreakPos = -1;
    while (pos < static_cast<int>(sb.size())) {
      const char c = sb[pos];
      if (c == '\n') {
        currentLineLength = 0;
        lastValidBreakPos = -1;
      } else if (isBreakChar(c)) {
        lastValidBreakPos = pos;
        ++currentLineLength;
      } else {
        ++currentLineLength;
      }
      if (currentLineLength > wrapColumn && lastValidBreakPos != -1) {
        sb[lastValidBreakPos] = '\n';
        pos = lastValidBreakPos;
        lastValidBreakPos = -1;
        currentLineLength = 0;
      }
      ++pos;
    }
  } else if (mode == kWrapToFillBlock) {
    int pos = 0, currentLineLength = 0, lastValidBreakPos = -1;
    while (pos < static_cast<int>(sb.size())) {
      const char c = sb[pos];
      if (c == '\n') {
        sb[pos] = ' ';  // TiXL turns the existing newline into a space, then treats it as a break.
        lastValidBreakPos = pos;
        ++currentLineLength;
      } else if (isBreakChar(c)) {
        lastValidBreakPos = pos;
        ++currentLineLength;
      } else {
        ++currentLineLength;
      }
      if (currentLineLength > wrapColumn && lastValidBreakPos != -1) {
        sb[lastValidBreakPos] = '\n';
        pos = lastValidBreakPos;
        lastValidBreakPos = -1;
        currentLineLength = 0;
      }
      ++pos;
    }
  } else if (mode == kSolidBlock) {
    int pos = 0, currentLineLength = 0;
    while (pos < static_cast<int>(sb.size())) {
      const char c = sb[pos];
      if (c == '\n') {
        sb.erase(static_cast<size_t>(pos), 1);  // StringBuilder.Remove(pos,1)
        continue;                                 // (no pos advance — TiXL `continue`)
      }
      ++currentLineLength;
      ++pos;
      if (currentLineLength == wrapColumn) {
        sb.insert(static_cast<size_t>(pos), 1, '\n');  // StringBuilder.Insert(pos,'\n')
        ++pos;
        currentLineLength = 0;
      }
    }
  }
  // kDontWrap (0) and any out-of-range: no branch → buffer unchanged (passthrough), faithful to TiXL.
}

// WrapString: InputText (String) + WrapColumn (int param) + Mode (enum) → host string.
void cookWrapString(StringCookCtx& c) {
  if (!c.output) return;

  // InputText: the first (and only) String input — wired upstream string, or strDef "" when unwired.
  std::string sb;
  if (c.inputStrings && !c.inputStrings->empty()) sb = (*c.inputStrings)[0];

  // WrapColumn: resolved Float param, truncated to int, clamped to [1,10000] (fork-wrapcolumn-int-on-float).
  int wrapColumn = static_cast<int>(stringFloatParam(c.params, "WrapColumn", 1.0f));
  if (wrapColumn < 1) wrapColumn = 1;
  if (wrapColumn > 10000) wrapColumn = 10000;

  // Mode: resolved Float param truncated to int (enum index). No clamp beyond the branch dispatch — an
  // out-of-range Mode falls through to passthrough (same as DontWrap), faithful to the C# switch default.
  const int mode = static_cast<int>(stringFloatParam(c.params, "Mode", 0.0f));

  insertLineWraps(mode, sb, wrapColumn);
  *c.output = sb;

  // Test-only: corrupt the REAL output (drop the last char) so golden RED case fires on the actual cook
  // path, not by flipping the expected value. Off in production.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports (ORDER MATTERS for the gather): "Result" output first, then "InputText" (single String input,
//   strDef=""), then "WrapColumn" (Float, default 1), then "Mode" (Float, Widget::Enum, 5 labels).
//   WrapString.t3 default: Mode = DontWrap (0), WrapColumn unset → clamp floor 1.
static const StringOp _reg_wrapstring{
    {"WrapString", "WrapString",
     {{"Result", "Result", "String", false},
      {"InputText", "InputText", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, /*strDef=*/""},
      {"WrapColumn", "WrapColumn", "Float", true, 1.0f, 1.0f, 10000.0f, Widget::Slider},
      {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"DontWrap", "WrapAtWords", "WrapAtCharacters", "WrapToFillBlock", "SolidBlock"}}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookWrapString};

}  // namespace sw
