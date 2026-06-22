// BuildRandomString string op — the FIRST cross-frame BUFFER-ACCUMULATING String producer (rides the
// stateful-string seam: StringState.buffer/index/lastUpdateTime persist between frames). TiXL authority:
//   external/tixl/Operators/Lib/string/random/BuildRandomString.cs (Update() ported 1:1 below, cited inline).
//
// WHAT IT DOES: maintains a persistent StringBuilder (= StringState.buffer, the `_fallbackBuffer` twin) and,
// once per distinct LocalFxTime, INSERTS `InsertString + Separator` at a moving cursor `_index`
// (StringState.index), optionally scrambling existing chars and wrapping lines, capping the buffer at
// MaxLength. Result = buffer.ToString(). Across frames the buffer GROWS / cycles → a teletype-style evolving
// random string. The state (buffer + index + lastUpdateTime) IS the op (a single frame shows only one insert).
//
// SEAM IT RIDES: the stateful-string seam (StringState, threaded by the driver flat: Impl::stringState /
// resident: s_stringState). BuildRandomString is the buffer/index/time twin of HasStringChanged's lastString.
//
// ★ FORKS (forced — honestly named; given parameters TiXL itself is non-deterministic, so a golden pins a
//   reproducible subset):
//   - fork-buildrandomstring-system-random-reseeded: TiXL's `_random = new Random()` is TIME-SEEDED
//     (System.Random with no seed) → JumpToRandomPos picks a non-reproducible index EVERY run, so byte-match
//     is impossible on that path. sw substitutes a PINNABLE XxHash-stream RNG seeded from StringState.rngState
//     (advanced per draw). The OBSERVABLE CONTRACT (given JumpToRandomPos, jump the cursor to a pseudo-random
//     position in [0,len)) is preserved; the exact position differs from TiXL's (both are "some random pos").
//     The golden pins determinism by leaving JumpToRandomPos=false (the .t3 default) → this path is not taken.
//   - fork-buildrandomstring-no-override-builder: TiXL's OverrideBuilder (InputSlot<StringBuilder>) lets an
//     upstream op hand in a mutable StringBuilder currency. sw has NO mutable-StringBuilder channel (the
//     StringBuilder=String decision: a StringBuilder rides as a host std::string), so there is no
//     OverrideBuilder port — the op ALWAYS uses _fallbackBuffer (StringState.buffer). TiXL's
//     `!OverrideBuilder.HasInputConnections || stringBuilder == null` branch is therefore ALWAYS taken
//     (the fallback path), which is exactly what we implement.
//   - fork-buildrandomstring-xxhash-verbatim: MathUtils.XxHash(uint) ported verbatim (uint32 wraparound +
//     logical >> ), used by the scramble path. (MathUtils.cs:113-124.)
//   - fork-localfxtime-bars-vs-secs: the debounce reads LocalFxTime. sw LocalFxTime is BARS (EvaluationContext
//     .localFxTime), TiXL's is SECS — the literal 0.001 threshold's MEANING differs (0.001 bars ≠ 0.001 secs).
//     The behaviour (skip Update when the clock has not advanced) is identical; only the unit differs.
//   - fork-buildrandomstring-builder-output-dropped: TiXL has a second Output `Builder` (Slot<StringBuilder>).
//     No StringBuilder currency in sw → that output is dropped; only Result (String) is produced.
#include <cmath>
#include <cstdint>
#include <string>

#include "runtime/eval_context.h"        // EvaluationContext (debounce reads ctx->localFxTime)
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / StringState / stringInjectBug / stringFloatParam
#include "runtime/string_ops_buildrandomstring_wrap.h"  // WrapLinesModes / insertLineWraps

namespace sw {

int runBuildRandomStringSelfTest(bool injectBug);  // buildrandomstring_golden.cpp (declared for the registrar)

namespace {

// MathUtils.XxHash(uint) — verbatim (MathUtils.cs:113-124). uint32 arithmetic wraps; >> is logical.
uint32_t xxHash(uint32_t p) {
  const uint32_t prime32A = 3266489917u;
  const uint32_t prime32B = 668265263u, prime32C = 374761393u;
  uint32_t h32 = p + prime32C;
  h32 = prime32B * ((h32 << 17) | (h32 >> (32 - 17)));
  h32 = 2246822519u * (h32 ^ (h32 >> 15));
  h32 = prime32A * (h32 ^ (h32 >> 13));
  return h32 ^ (h32 >> 16);
}

// WriteMode enum (matches BuildRandomString.cs:Modes — the WriteMode Int param is the enum index).
enum class Modes { Insert = 0, Overwrite = 1, OverwriteAtFixedOffset = 2 };

// fork-buildrandomstring-system-random-reseeded: a pinnable RNG for the JumpToRandomPos path (replaces
// the time-seeded System.Random). Returns a value in [0, bound) and advances the per-node rngState. Only
// used when JumpToRandomPos is on; the golden never enables it (so this never affects the pinned output).
int randIndex(uint32_t& rngState, int bound) {
  if (bound <= 0) return 0;
  rngState = xxHash(rngState + 0x9E3779B9u);  // advance the stream (golden-ratio increment, deterministic)
  return static_cast<int>(rngState % static_cast<uint32_t>(bound));
}

// Read a String param from THIS node's stored override (the wire-OR-const for an UNWIRED String port is
// resolved into inputStrings by the driver, but for a param-only String — InsertString / Separator — the
// cleanest read is the inputStrings gather by spec port order). BuildRandomString's String inputs in spec
// order are [InsertString, Separator] → inputStrings[0], inputStrings[1].
const std::string& gatheredString(const StringCookCtx& c, size_t idx, const std::string& fallback) {
  if (c.inputStrings && idx < c.inputStrings->size()) return (*c.inputStrings)[idx];
  return fallback;
}

// BuildRandomString: accumulate StringState.buffer across frames. Body is BuildRandomString.cs:Update()
// ported 1:1 (cited inline). Result (port 0) = buffer; debounce + index + buffer all ride StringState.
void cookBuildRandomString(StringCookCtx& c) {
  if (!c.output) return;

  const int maxLength = static_cast<int>(stringFloatParam(c.params, "MaxLength", 1000.0f));

  // cs:27-30 — DEBOUNCE. `if (Result.Value != null && Abs(LocalFxTime - _lastUpdateTime) < 0.001) return;`
  // Equivalent: if state is primed AND the clock has not advanced, re-emit the CURRENT buffer unchanged and
  // skip the Update. With NO state slot (hand-built single-frame ctx) there is no persistence → never
  // debounced. fork-localfxtime-bars-vs-secs (LocalFxTime is BARS here).
  const double localFxTime = c.ctx ? static_cast<double>(c.ctx->localFxTime) : 0.0;
  if (c.state && c.state->primed && std::abs(localFxTime - c.state->lastUpdateTime) < 0.001) {
    *c.output = c.state->buffer;  // unchanged (the cached Result.Value)
    if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
    return;
  }
  if (c.state) {
    c.state->primed = true;
    c.state->lastUpdateTime = localFxTime;  // cs:30 — _lastUpdateTime = context.LocalFxTime
  }

  const int scrambleSeed = static_cast<int>(stringFloatParam(c.params, "ScrambleSeed", 0.0f));

  // cs:34-39 — maxLength <= 0 → Result = empty, return.
  if (maxLength <= 0) {
    if (c.state) c.state->buffer.clear();
    *c.output = std::string{};
    return;  // nothing to corrupt under injectBug (empty)
  }

  // cs:41-44 — fork-buildrandomstring-no-override-builder: ALWAYS the fallback buffer (StringState.buffer).
  // With no state slot (single-frame hand-built ctx) a transient local buffer/index keeps the op a pure
  // function of this frame (no persistence — the golden drives the stateful legs through the state slot).
  std::string localBuffer;
  int localIndex = 0;
  uint32_t localRng = 0;
  std::string& buffer = c.state ? c.state->buffer : localBuffer;
  int& index = c.state ? c.state->index : localIndex;
  uint32_t& rngState = c.state ? c.state->rngState : localRng;

  // cs:46-50 — Clear → buffer.Clear(); _index = 0.
  const bool clear = stringFloatParam(c.params, "Clear", 0.0f) != 0.0f;
  if (clear) {
    buffer.clear();
    index = 0;
  }

  // cs:54-82 — SCRAMBLE: walk the buffer; for chunks whose XxHash falls below ScrambleRatio, decrement chars
  // (space→'Z' first), skipping '\n'. (TiXL wraps this whole block in try/catch; on any throw Result is left
  // at the prior buffer — sw's index math below is range-guarded so it cannot throw, faithful to "no throw".)
  const float scrambleRatio = stringFloatParam(c.params, "ScrambleRatio", 0.0f);
  const bool scrambleEnabled = stringFloatParam(c.params, "Scramble", 0.0f) != 0.0f;
  if (scrambleRatio > 0.0f && scrambleEnabled) {
    for (int i = 0; i < static_cast<int>(buffer.size()); i++) {
      // cs:63 — hash = (float)((double)XxHash((uint)index + (uint)scrambleSeed*123127) / uint.MaxValue).
      const uint32_t hv = xxHash(static_cast<uint32_t>(i) + static_cast<uint32_t>(scrambleSeed) * 123127u);
      const float hash = static_cast<float>(static_cast<double>(hv) / 4294967295.0);  // uint.MaxValue
      if (hash < scrambleRatio) {
        // cs:66 — scrambleChunkEnd = index + hash*buffer.Length + 1 (float chunk end).
        const float scrambleChunkEnd = static_cast<float>(i) + hash * static_cast<float>(buffer.size()) + 1.0f;
        while (i < static_cast<int>(buffer.size()) && static_cast<float>(i) < scrambleChunkEnd) {
          char ch = buffer[static_cast<std::string::size_type>(i)];
          if (ch != '\n') {
            if (ch == 32) ch = static_cast<char>(90);   // cs:73 — space → 'Z'
            ch = static_cast<char>(ch - 1);             // cs:75 — decrement
            buffer[static_cast<std::string::size_type>(i)] = ch;
          }
          i++;
        }
      }
    }
  }

  // cs:84-161 — INSERT block.
  const bool doInsert = stringFloatParam(c.params, "Insert", 1.0f) != 0.0f;  // .t3 default true
  if (doInsert) {
    // cs:86-87 — JumpToRandomPos → _index = (int)_random.NextLong(0, buffer.Length).
    // fork-buildrandomstring-system-random-reseeded: pinnable RNG instead of time-seeded System.Random.
    if (stringFloatParam(c.params, "JumpToRandomPos", 0.0f) != 0.0f) {
      index = randIndex(rngState, static_cast<int>(buffer.size()));
    }

    // cs:89-91 — separator, with "\\n" → newline.
    std::string separator = gatheredString(c, 1, " ");  // Separator = inputStrings[1] (.t3 default " ")
    if (!separator.empty()) {
      std::string::size_type p;
      while ((p = separator.find("\\n")) != std::string::npos) separator.replace(p, 2, "\n");
    }

    // cs:93-98 — insertString = str + separator; insertLength / currentLength / lineWrap / mode.
    const std::string str = gatheredString(c, 0, "");  // InsertString = inputStrings[0]
    const std::string insertString = str + separator;
    int insertLength = static_cast<int>(insertString.size());
    int currentLength = static_cast<int>(buffer.size());
    const WrapLinesModes lineWrap = static_cast<WrapLinesModes>(
        static_cast<int>(stringFloatParam(c.params, "WrapLines", 1.0f)));  // .t3 default 1 = WrapAtWords
    const Modes mode = static_cast<Modes>(static_cast<int>(stringFloatParam(c.params, "WriteMode", 0.0f)));

    // cs:100-101 — if (_index > maxLength) _index = 0.
    if (index > maxLength) index = 0;

    // cs:103-107 — pos = _index; clamp insertLength so pos+insertLength does not exceed maxLength.
    int pos = index;
    if (pos + insertLength > maxLength) insertLength = maxLength - pos;

    // cs:109-112 — non-Insert mode AND pos < currentLength - insertLength → Remove(pos, insertLength).
    if (mode != Modes::Insert && pos < currentLength - insertLength) {
      if (insertLength > 0 && pos >= 0 && pos + insertLength <= static_cast<int>(buffer.size()))
        buffer.erase(static_cast<std::string::size_type>(pos), static_cast<std::string::size_type>(insertLength));
    }

    // cs:114-117 — pos > currentLength → pad with spaces up to pos.
    if (pos > currentLength) {
      buffer.append(static_cast<std::string::size_type>(pos - currentLength), ' ');
    }

    // cs:119 — Insert(pos, insertString). insertString.size() may exceed the clamped insertLength (TiXL
    // inserts the WHOLE insertString and only truncates the buffer to maxLength at the end). Guard pos.
    if (pos < 0) pos = 0;
    if (pos > static_cast<int>(buffer.size())) pos = static_cast<int>(buffer.size());
    buffer.insert(static_cast<std::string::size_type>(pos), insertString);

    // cs:121 — InsertLineWraps(lineWrap, buffer, pos, insertLength, WrapLineColumn.Clamp(1,1000)).
    int wrapColumn = static_cast<int>(stringFloatParam(c.params, "WrapLineColumn", 60.0f));
    if (wrapColumn < 1) wrapColumn = 1;
    if (wrapColumn > 1000) wrapColumn = 1000;
    insertLineWraps(lineWrap, buffer, pos, insertLength, wrapColumn);

    // cs:125-153 — advance _index by mode.
    switch (mode) {
      case Modes::Insert:
      case Modes::Overwrite:
        // cs:127-138 — both Insert and Overwrite: _index += insertLength; _index %= maxLength.
        index += insertLength;
        index %= maxLength;
        break;
      case Modes::OverwriteAtFixedOffset: {
        // cs:139-150 — _index += OverwriteOffset; wrap above maxLength, wrap-back below 0.
        index += static_cast<int>(stringFloatParam(c.params, "OverwriteOffset", 10.0f));  // .t3 default 10
        if (index > maxLength) {
          index = index % maxLength;
        } else if (index < 0) {
          index += maxLength - insertLength;
        }
        break;
      }
    }

    // cs:158-159 — buffer.Length > maxLength → truncate to maxLength.
    if (static_cast<int>(buffer.size()) > maxLength)
      buffer.resize(static_cast<std::string::size_type>(maxLength));
  }

  // cs:172-173 — Builder.Value = stringBuilder (dropped, fork); Result.Value = stringBuilder.ToString().
  *c.output = buffer;

  // Test-only RED tooth: corrupt the REAL accumulated buffer (drop its last char) so the golden's RED case
  // fires on the actual cross-frame cook — AND the corruption persists into StringState.buffer so the NEXT
  // frame accumulates on top of a damaged buffer (the bite rides the real state path, not a flipped expected).
  if (stringInjectBug()) {
    if (!buffer.empty()) buffer.pop_back();
    *c.output = buffer;
  }
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Port ORDER (position in spec = gather order for inputStrings; String inputs gather in spec order):
//     [0]  "Result"          = String output (the host string currency — String PRODUCER)
//     [1]  "InsertString"    = String input (wire-OR-const; strDef "Tooll likes Cats.") → inputStrings[0]
//     [2]  "Separator"       = String input (wire-OR-const; strDef " ")                 → inputStrings[1]
//     [3]  "MaxLength"       = Float/Int param (default 1000)
//     [4]  "Insert"          = Float/bool param (default 1 = true)
//     [5]  "Clear"           = Float/bool param (default 0)
//     [6]  "WriteMode"       = Float/enum param (default 0 = Insert)            [Insert/Overwrite/OverwriteAtFixedOffset]
//     [7]  "WrapLines"       = Float/enum param (default 1 = WrapAtWords)       [DontWrap/WrapAtWords/WrapAtCharacters/WrapToFillBlock/SolidBlock]
//     [8]  "WrapLineColumn"  = Float/Int param (default 60)
//     [9]  "OverwriteOffset" = Float/Int param (default 10)
//     [10] "JumpToRandomPos" = Float/bool param (default 0)
//     [11] "Scramble"        = Float/bool param (default 0)
//     [12] "ScrambleRatio"   = Float param (default 0)
//     [13] "ScrambleSeed"    = Float/Int param (default 0)
//   (OverrideBuilder dropped — fork-buildrandomstring-no-override-builder; Builder output dropped — fork.)
//   The driver gathers the two String input ports into inputStrings in spec order: inputStrings[0]=
//   InsertString, inputStrings[1]=Separator. Every other param rides the Float value spine.
static const StringOp _reg_buildrandomstring{
    {"BuildRandomString", "BuildRandomString",
     {{"Result",          "Result",          "String", false},
      {"InsertString",    "InsertString",    "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, "Tooll likes Cats."},
      {"Separator",       "Separator",       "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {},
       false, 1, false, " "},
      {"MaxLength",       "MaxLength",       "Float",  true, 1000.0f, 0.0f, 100000.0f, Widget::Slider},
      {"Insert",          "Insert",          "Float",  true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"Clear",           "Clear",           "Float",  true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"WriteMode",       "WriteMode",       "Float",  true, 0.0f, 0.0f, 2.0f, Widget::Enum,
       {"Insert", "Overwrite", "OverwriteAtFixedOffset"}},
      {"WrapLines",       "WrapLines",       "Float",  true, 1.0f, 0.0f, 4.0f, Widget::Enum,
       {"DontWrap", "WrapAtWords", "WrapAtCharacters", "WrapToFillBlock", "SolidBlock"}},
      {"WrapLineColumn",  "WrapLineColumn",  "Float",  true, 60.0f, 1.0f, 1000.0f, Widget::Slider},
      {"OverwriteOffset", "OverwriteOffset", "Float",  true, 10.0f, -1000.0f, 1000.0f, Widget::Slider},
      {"JumpToRandomPos", "JumpToRandomPos", "Float",  true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Scramble",        "Scramble",        "Float",  true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"ScrambleRatio",   "ScrambleRatio",   "Float",  true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      {"ScrambleSeed",    "ScrambleSeed",    "Float",  true, 0.0f, 0.0f, 100000.0f, Widget::Slider}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookBuildRandomString};

}  // namespace sw
