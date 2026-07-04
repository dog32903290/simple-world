// runtime/stateful_value_ops_randomchoice — RandomChoiceIndex (TiXL numbers/int/basic/
// RandomChoiceIndex.cs): a DETERMINISTIC no-consecutive-repeat random index. Value (an index walk)
// → a pseudo-random choice in [0, Mod) where consecutive Value steps never repeat the same choice.
//
// TiXL authority: Operators/Lib/numbers/int/basic/RandomChoiceIndex.cs
//   + Core/Utils/MathUtils.XxHash (MathUtils.cs:113-129) + MathUtils.Mod (MathUtils.cs:273-284).
//
//   Update() (RandomChoiceIndex.cs:26-58):
//     n = Value; modulo = Mod;
//     modulo <  2 → Result = 0;                                        // cs:30-34
//     modulo == 2 → Result = n.Mod(2);                                 // cs:36-40
//     refill when !_initialized || n < _lastBufferIndex || n >= _lastBufferIndex+100
//                 || modulo != _modulo:                                 // cs:42-45
//        bufferStartIndex = n - n.Mod(100) - 25;                        // cs:48 (100/4)
//        FillBuffer(bufferStartIndex, modulo); _lastBufferIndex = bufferStartIndex;  // cs:49-51
//     Result = _buffer[n.Mod(100)];                                     // cs:54-57
//
//   FillBuffer(start, modulo) (cs:65-94):
//     _buffer[0] = XxHash(start).Mod(modulo);                           // cs:68-69
//     for i in 1..99:  offset = XxHash(start+i).Mod(modulo-1) + 1;      // cs:75 (the +1 = the
//                      _buffer[i] = (_buffer[i-1]+offset).Mod(modulo);  //  no-repeat guarantee)
//     expectedNext = XxHash(start+100).Mod(modulo);                     // cs:81
//     for i = 99 down to 1: if _buffer[i] != expectedNext break;        // cs:83-86 (walk back,
//        offset = XxHash(start+i+13331).Mod(modulo-1) + 1;              //  cs:88 — flip collisions
//        _buffer[i] = expectedNext = (expectedNext+offset).Mod(modulo); //  with the NEXT block head)
//
// STATE (the cross-frame channel, cs:60-62): s[0] = _lastBufferIndex, s[1] = _modulo; st.init =
// _initialized. The 100-int _buffer is NOT stored — its content is a PURE function of
// (_lastBufferIndex, _modulo) (the XxHash chain above), so each cook recomputes it locally from the
// two persisted scalars: byte-identical results, no 100-slot state (StatefulValueState has s[12]).
// The hysteresis is REAL and faithful: e.g. cook n=10 (start=-25) then n=-10 — NO refill triggers
// (-10 >= -25 && -10 < 75), so the op reads slot (-10).Mod(100)=90 of the OLD start=-25 buffer
// (= chain index 65), whereas a FRESH instance at n=-10 refills at start=-125 and reads a different
// value. The production golden pins both (history vs fresh diverge).
//
// FORKS (named):
//   - fork-int-bool-dissolve-to-float: TiXL Value/Mod/Result are int; Float ports with the C# (int)
//     cast convention (truncate toward zero, value-spine precedent). Result re-widened to Float.
//   - fork-rci-buffer-recompute: the _buffer cache is recomputed per cook instead of persisted (pure
//     function of the persisted (start, modulo) — output byte-identical; see STATE above).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <cstdint>
#include <map>
#include <string>

#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

// = MathUtils.XxHash(uint) (MathUtils.cs:113-124). Exact uint32 arithmetic.
inline uint32_t xxHashU32(uint32_t p) {
  const uint32_t prime32A = 3266489917U;
  const uint32_t prime32B = 668265263U, prime32C = 374761393U;
  uint32_t h32 = p + prime32C;
  h32 = prime32B * ((h32 << 17) | (h32 >> (32 - 17)));
  h32 = 2246822519U * (h32 ^ (h32 >> 15));
  h32 = prime32A * (h32 ^ (h32 >> 13));
  return h32 ^ (h32 >> 16);
}
// = MathUtils.XxHash(int) (MathUtils.cs:126-129): (int)XxHash((uint)p) — may be negative.
inline int32_t xxHashI32(int32_t p) { return (int32_t)xxHashU32((uint32_t)p); }

// = MathUtils.Mod (MathUtils.cs:273-284): C# truncated % then wrap negatives positive.
inline int csMod(int val, int repeat) {
  if (repeat == 0) return 0;
  int x = val % repeat;
  if (x < 0) x = repeat + x;
  return x;
}

constexpr int kBufLen = 100;  // = ModBufferLength (RandomChoiceIndex.cs:96)

// RandomChoiceIndex TEETH hook (file-local; 0 = production, set by --selftest-randomchoiceindex only
// via setRandomChoiceIndexBug). Corrupts a REAL production term so the golden's FIXED expected values
// bite: 1 = DROP the "+1" in the chain offset (cs:75 `Mod(modulo-1) + 1` → `Mod(modulo-1)`) — the
// anti-repetition rule breaks, every chained buffer value from index 1 on shifts → the mod≥3 probes
// go RED (the mod<2 / mod==2 early paths are untouched, proving the bite is in the chain itself).
int g_randomChoiceIndexBug = 0;

// = FillBuffer (RandomChoiceIndex.cs:65-94), recomputed per cook from (start, modulo) — see header.
void fillBuffer(int start, int modulo, int buf[kBufLen]) {
  buf[0] = csMod(xxHashI32(start), modulo);  // cs:68-69
  const int plusOne = (g_randomChoiceIndexBug == 1) ? 0 : 1;  // bug 1: DROP the no-repeat "+1" (cs:75)
  for (int i = 1; i < kBufLen; ++i) {
    const int offset = csMod(xxHashI32(start + i), modulo - 1) + plusOne;  // cs:75
    buf[i] = csMod(buf[i - 1] + offset, modulo);                           // cs:76-77
  }
  int expectedNext = csMod(xxHashI32(start + kBufLen), modulo);  // cs:81
  for (int i = kBufLen - 1; i > 0; --i) {                        // cs:83-93
    if (buf[i] != expectedNext) break;
    const int offset = csMod(xxHashI32(start + i + 13331), modulo - 1) + plusOne;  // cs:88
    const int newValue = csMod(expectedNext + offset, modulo);                     // cs:89
    buf[i] = newValue;
    expectedNext = newValue;
  }
}

// Step fn (= Update, cs:26-58). State: s[0]=_lastBufferIndex, s[1]=_modulo, st.init=_initialized.
void stepRandomChoiceIndex(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                           StatefulValueState& st, float out[3], const TransportSnapshot&,
                           ContextVarMap*, const std::string&) {
  const int n = (int)getIn(in, "Value", 0.0f);     // C# (int) cast = truncate toward zero
  const int modulo = (int)getIn(in, "Mod", 1.0f);  // .t3 default Mod=1 → Result 0
  if (modulo < 2) { out[0] = 0.0f; return; }       // cs:30-34
  if (modulo == 2) { out[0] = (float)csMod(n, 2); return; }  // cs:36-40

  int lastBufferIndex = (int)st.s[0];
  const int lastModulo = (int)st.s[1];
  if (!st.init || n < lastBufferIndex || n >= lastBufferIndex + kBufLen ||
      modulo != lastModulo) {  // cs:42-45
    lastBufferIndex = n - csMod(n, kBufLen) - kBufLen / 4;  // cs:48 — "counting backwards more unlikely"
    st.s[0] = (float)lastBufferIndex;
    st.s[1] = (float)modulo;
    st.init = true;
  }
  int buf[kBufLen];
  fillBuffer(lastBufferIndex, modulo, buf);  // fork-rci-buffer-recompute (pure fn of persisted state)
  out[0] = (float)buf[csMod(n, kBufLen)];    // cs:54-57
}

}  // namespace

void setRandomChoiceIndexBug(int mode) { g_randomChoiceIndexBug = mode; }

static const StatefulOpReg _reg_RandomChoiceIndex{"RandomChoiceIndex", stepRandomChoiceIndex};

}  // namespace sw
