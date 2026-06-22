// BlendStrings string op (string self-registration seam leaf — MultiInput<string> + Blend/BlendSpread/
// Scramble/ScrambleSeed/MaxLength/Characters → string). TiXL authority:
// Operators/Lib/string/combine/BlendStrings.cs:17-118 (ported verbatim below).
//
//   BlendStrings.cs Update():
//     var stringInputs = InputStrings.GetCollectedTypedInputs();
//     var stringCount = stringInputs.Count;
//     floatIndex = Blend.GetChangedValue(...).Clamp(0, stringCount - 1.0001f);
//     var index = (int)floatIndex;
//     if (stringCount == 0) return;
//     if (stringCount == 1) { strA = strB = stringInputs[0]; }
//     else { strA = stringInputs[index]; strB = stringInputs[index+1]; }
//     var blendIndex = floatIndex - index;
//     strA ??= string.Empty; strB ??= string.Empty;
//     if (strA == "" && strB == "") { Result.Value = ""; return; }
//     var totalMaxLength = MaxLength.GetValue(context).Clamp(1, 10000);
//     var maxLength = Math.Max(strA.Length, strB.Length).Clamp(1, totalMaxLength);
//     var scrambleFactor = Scramble.GetValue(context);
//     var scrambleSeed = ScrambleSeed.GetValue(context);
//     var chars = Characters.GetValue(context);
//     if (string.IsNullOrEmpty(chars)) chars = FallbackChars;
//     // for each charIndex in [0, maxLength):
//     //   charA = GetCharOrSpace(strA, charIndex); charB = GetCharOrSpace(strB, charIndex)
//     //   if charA=='\n' || charB=='\n': append charA; continue
//     //   charAInt = chars.IndexOf(charA).Clamp(0, charCount-1)
//     //   charBInt = chars.IndexOf(charB).Clamp(0, charCount-1)
//     //   hashA = Hash01((uint)(charIndex*123 + scrambleSeed/100))
//     //   scrambleOffset = hashA < scrambleFactor
//     //       ? (Hash01((uint)(charIndex*123 + scrambleSeed)) - 0.5f) * charCount
//     //       : 0
//     //   x = (maxLength<=1) ? 0 : charIndex/(float)(maxLength-1)
//     //   blendProgressForChar = ((x-blendIndex)/blendSpread - blendIndex + 1).Clamp(0,1)
//     //   blendedValue = (int)(charBInt + (charAInt-charBInt)*blendProgressForChar + scrambleOffset).Clamp(0,charCount-1)
//     //   append chars[blendedValue]
//     Result.Value = _stringBuilder.ToString();
//
//   Inputs:
//     InputStrings  = MultiInputSlot<string> (the blend source list)
//     Blend         = InputSlot<float>  (which two adjacent inputs to blend and how far)
//     BlendSpread   = InputSlot<float>  (spatial spread of the transition zone)
//     Scramble      = InputSlot<float>  (scramble probability threshold [0,1])
//     ScrambleSeed  = InputSlot<int>    (RNG seed for scramble)
//     MaxLength     = InputSlot<int>    (hard output length cap)
//     Characters    = InputSlot<string> (character palette; empty → FallbackChars)
//   Output: Result (string)
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). Gather contract: InputStrings is the
// MultiInput; all other ports are either Float (Blend/BlendSpread/Scramble) or Int dissolved to Float
// (ScrambleSeed/MaxLength). Characters is a String port — it is the LAST String port, so the driver
// gathers: inputStrings = [multiWire0, ..., multiWireN-1, Characters].
// LAST entry = Characters const / wired string; everything before = InputStrings MultiInput wires.
// (Identical layout to CombineStrings where Separator is the last single-cardinality String port.)
//
// FORKS (named):
//   - fork-rng-hash01-verbatim-uint32: MathUtils.Hash01 (MathUtils.cs:131-138) uses C# uint (32-bit
//     unsigned). Ported 1:1 using uint32_t; all arithmetic wraps mod 2^32 identically. The unsigned
//     right-shift >> on uint in C# == >> on uint32_t in C++ (both logical/unsigned). EXACT.
//   - fork-blendstrings-characters-ascii-only-indexof: TiXL chars.IndexOf(charA) works on C# char
//     (UTF-16 code unit). FallbackChars contains non-ASCII Unicode (Á Ä Ó Ö Ü Ú ä å ó ö ß ú ü) whose
//     C++ std::string representation in UTF-8 spans multiple bytes. Our port uses
//     std::string::find(char) which finds a single byte. For ASCII chars (A-Z, a-z, 0-9, space,
//     punctuation) the byte value is identical in UTF-8 and UTF-16 ⇒ no parity difference. For
//     non-ASCII chars, the find result is the byte offset into the UTF-8 encoded FallbackChars (not
//     the C# char index). In practice, when the input strings contain only ASCII chars from the
//     FallbackChars, all lookup results match exactly. Inputs with non-ASCII produce different
//     indices; named fork-blendstrings-characters-ascii-only-indexof. goldens use ASCII-only.
//   - fork-blendstrings-characters-last-in-gather: Characters (single String port) is gathered LAST
//     by the driver (same as CombineStrings Separator). inputStrings.back() = Characters value or
//     its strDef const ("")→FallbackChars. Multi-wires before back() = InputStrings wires.
//   - fork-int-dissolve-to-float: ScrambleSeed (int) and MaxLength (int) dissolve to Float (Int port
//     type not present in sw). ScrambleSeed read as (int)floatParam. MaxLength read as (int)floatParam.
//   - fork-float-clamp-vs-math-clamp: TiXL MathUtils.Clamp = Min(Max(v,min),max). When min>max
//     (e.g. stringCount=1 → Clamp(blend, 0, -0.0001f) → always -0.0001f), this is faithfully handled
//     by our std::clamp (which has the same Min(Max) reduction behavior per C++17 std::clamp contract
//     when min≤max; here max<min corner exists only transiently and the clamped floatIndex → index=0).
//   - fork-no-dirtyflag-trigger: TiXL's dirty-flag bookkeeping (GetChangedValue / DirtyFlag.Clear)
//     is a dirty-tracking optimization; the flat cook is stateless and cooks every frame → no-op fork.
//   - fork-string-host-not-gpu: string currency is host-side (no GPU EvaluationContext).

#include <algorithm>  // std::clamp, std::max
#include <cmath>      // std::floor (for truncation toward zero)
#include <cstdint>    // uint32_t
#include <cstring>    // std::strlen
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug / stringFloatParam

namespace sw {

int runStringRailSelfTest(bool injectBug);  // string_rail_golden.cpp (declared for the registrar)

namespace {

// FallbackChars — verbatim from BlendStrings.cs:155. Contains non-ASCII Unicode (see fork above).
static const char kFallbackChars[] =
    " .-/\\?#<^*()&AÁÄBCDEFGHIJKLMNOÄÓÖPQRSTUÜÜÚVWXYZaäåbcdefghijklmnoóöpqrsßtuúüvwxyz0123456789";

// MathUtils.Hash01 verbatim 1:1 port. fork-rng-hash01-verbatim-uint32.
// C# source (MathUtils.cs:131-138):
//   x *= 13331U;
//   const uint k = 1103515245U;  // GLIB C
//   x = ((x>>8)^x)*k;
//   x = ((x>>8)^x)*k;
//   return (float)( (x & 0x7fffffff) / 2147483648.0);
static float hash01(uint32_t x) {
  x *= 13331U;
  const uint32_t k = 1103515245U;  // GLIB C (verbatim from MathUtils.cs:134)
  x = ((x >> 8) ^ x) * k;
  x = ((x >> 8) ^ x) * k;
  return static_cast<float>((x & 0x7fffffffU) / 2147483648.0);
}

// ProgressTransition verbatim port. BlendStrings.cs:148-151:
//   return ((x-progress)/spread - progress + 1).Clamp(0,1)
static float progressTransition(float x, float progress, float spread) {
  float v = (x - progress) / spread - progress + 1.0f;
  return std::clamp(v, 0.0f, 1.0f);
}

// GetCharOrSpace verbatim port. BlendStrings.cs:134-140:
//   if index<0 || index>=str.Length: return ' '
//   return str[index]
static char getCharOrSpace(const std::string& str, int index) {
  if (index < 0 || index >= (int)str.size()) return ' ';
  return str[static_cast<std::size_t>(index)];
}

// cookBlendStrings: verbatim port of BlendStrings.cs:17-118.
void cookBlendStrings(StringCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // inputStrings layout: [multiWire0...multiWireN-1, Characters]
  // The LAST entry is the Characters port (single-cardinality String, gathered last — see fork above).
  const std::vector<std::string>& gathered = c.inputStrings ? *c.inputStrings : std::vector<std::string>{};

  // Separate Characters (last) from InputStrings wires (all before last).
  // If gathered is empty, there are no InputStrings wires AND no Characters value.
  std::string chars;
  int stringCount = 0;
  if (gathered.empty()) {
    // No wires at all — Characters strDef is "" → use FallbackChars.
    chars = kFallbackChars;
    stringCount = 0;
  } else {
    chars = gathered.back();  // fork-blendstrings-characters-last-in-gather
    if (chars.empty()) chars = kFallbackChars;  // IsNullOrEmpty → FallbackChars
    stringCount = static_cast<int>(gathered.size()) - 1;  // InputStrings wire count
  }

  // Blend (float param), BlendSpread (float param), Scramble (float param).
  const float blend       = stringFloatParam(c.params, "Blend",      0.0f);
  const float blendSpread = stringFloatParam(c.params, "BlendSpread",1.0f);
  const float scramble    = stringFloatParam(c.params, "Scramble",   0.0f);
  // ScrambleSeed (int dissolved to float).
  const int scrambleSeed  = static_cast<int>(stringFloatParam(c.params, "ScrambleSeed", 0.0f));
  // MaxLength (int dissolved to float).
  const int totalMaxLength = std::clamp(
      static_cast<int>(stringFloatParam(c.params, "MaxLength", 10000.0f)), 1, 10000);

  // floatIndex = Blend.Clamp(0, stringCount - 1.0001f). fork-float-clamp-vs-math-clamp.
  const float floatIndex = std::clamp(blend, 0.0f,
                                       static_cast<float>(stringCount) - 1.0001f);
  const int index = static_cast<int>(floatIndex);  // C# (int)floatIndex = truncation toward zero

  // Early return when no InputStrings wires.
  if (stringCount == 0) {
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  // Select strA / strB from the gathered InputStrings wires.
  const std::string& strA = gathered[static_cast<std::size_t>(index)];
  const std::string& strB = (stringCount == 1)
                                ? gathered[static_cast<std::size_t>(index)]   // count==1: strA==strB
                                : gathered[static_cast<std::size_t>(index + 1)];

  // strA ?? "" and strB ?? "" are already handled (std::string is always non-null, defaults to "").
  // Both empty → result is "".
  if (strA.empty() && strB.empty()) {
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  const float blendIndex = floatIndex - static_cast<float>(index);

  const int rawMax  = std::max(static_cast<int>(strA.size()), static_cast<int>(strB.size()));
  const int maxLength = std::clamp(rawMax, 1, totalMaxLength);

  const int charCount = static_cast<int>(chars.size());
  if (charCount == 0) {
    // Empty palette after FallbackChars guard — degenerate, output "".
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }

  c.output->reserve(static_cast<std::size_t>(maxLength));

  for (int charIndex = 0; charIndex < maxLength; ++charIndex) {
    const char charA = getCharOrSpace(strA, charIndex);
    const char charB = getCharOrSpace(strB, charIndex);

    // BlendStrings.cs:94-98: if '\n' in either → pass through charA unchanged, skip blending.
    if (charA == '\n' || charB == '\n') {
      c.output->push_back(charA);
      continue;
    }

    // chars.IndexOf(charA).Clamp(0, charCount-1) — fork-blendstrings-characters-ascii-only-indexof.
    // std::string::find returns npos (-1 as int) when not found; clamp keeps it at 0.
    const int charAInt = std::clamp(
        static_cast<int>(chars.find(charA)), 0, charCount - 1);
    const int charBInt = std::clamp(
        static_cast<int>(chars.find(charB)), 0, charCount - 1);

    // Scramble. hashA = Hash01((uint)(charIndex * 123 + scrambleSeed/100)).
    // Note: scrambleSeed/100 in C# integer division (truncation). Then cast to uint.
    const uint32_t seedA = static_cast<uint32_t>(charIndex * 123 + scrambleSeed / 100);
    const float hashA = hash01(seedA);

    float scrambleOffset = 0.0f;
    if (hashA < scramble) {
      const uint32_t seedB = static_cast<uint32_t>(charIndex * 123 + scrambleSeed);
      scrambleOffset = (hash01(seedB) - 0.5f) * static_cast<float>(charCount);
    }

    // x = (maxLength <= 1) ? 0 : charIndex / (float)(maxLength-1)
    const float x = (maxLength <= 1) ? 0.0f
                                     : static_cast<float>(charIndex) / static_cast<float>(maxLength - 1);

    const float bp = progressTransition(x, blendIndex, blendSpread);

    // blendedValue = (int)(charBInt + (charAInt - charBInt) * bp + scrambleOffset).Clamp(0, charCount-1)
    const float rawBlended = static_cast<float>(charBInt)
                           + static_cast<float>(charAInt - charBInt) * bp
                           + scrambleOffset;
    const int blendedValue = std::clamp(static_cast<int>(rawBlended), 0, charCount - 1);

    c.output->push_back(chars[static_cast<std::size_t>(blendedValue)]);
  }

  // Test-only: corrupt the output so a golden's RED case bites on the actual cook path.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports (ORDER MATTERS for the gather):
//     "Result" output (String)
//     "InputStrings" MultiInput String — wires expand into inputStrings[0..N-1]
//     "Blend"        Float param (rides params, NOT inputStrings)
//     "BlendSpread"  Float param (rides params)
//     "Scramble"     Float param (rides params)
//     "ScrambleSeed" Float param (Int dissolved — rides params)
//     "MaxLength"    Float param (Int dissolved — rides params)
//     "Characters"   String input (single, LAST String port → gathered as inputStrings.back())
//   PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless,
//   vecArity, multiInput, strDef}.
static const StringOp _reg_blendstrings{
    {"BlendStrings", "BlendStrings",
     {{"Result",      "Result",      "String", false},
      {"InputStrings","InputStrings","String", true,  0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/true, ""},
      {"Blend",       "Blend",       "Float",  true,  0.0f, 0.0f, 1.0f, Widget::Slider},
      {"BlendSpread", "BlendSpread", "Float",  true,  1.0f, 0.001f, 5.0f, Widget::Slider},
      {"Scramble",    "Scramble",    "Float",  true,  0.0f, 0.0f, 1.0f, Widget::Slider},
      {"ScrambleSeed","ScrambleSeed","Float",  true,  0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"MaxLength",   "MaxLength",   "Float",  true,  10000.0f, 1.0f, 10000.0f, Widget::Slider},
      {"Characters",  "Characters",  "String", true,  0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       /*multiInput=*/false, ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookBlendStrings};

}  // namespace sw
