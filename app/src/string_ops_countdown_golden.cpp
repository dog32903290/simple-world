// string_ops_countdown_golden — CountDown golden (--selftest-countdown). Standalone leaf (does NOT
// ride string_rail_golden.cpp — ratchet cap; the blendstrings/wrapstring precedent). Invokes the
// REGISTERED production cook (findStringOp("CountDown")) through a hand-built StringCookCtx.
//
// DETERMINISM: the wall clock is PINNED via setHostNowOverrideForTest (datetime_host seam) — fixed
// epochs in, closed-form strings out; NO wall-clock assertion (GOLDEN_STANDARD 時間 probe 規則).
//
// Expected values hand-derived from CountDown.cs:16-47 + .NET TimeSpan.ToString semantics
// (see the leaf's THE-FORMAT-ENGINE doc). Anchor epochs:
//   target  = "2021-04-17 17:00" (the CountDown.t3 LaunchTime default)
//           = daysFromCivil(2021-04-17)=18734 d → 18734*86400 + 17*3600 = 1618678800 s.
//   nowA    = target + 93784.564 s  (93784.564 = 1*86400 + 2*3600 + 3*60 + 4.564 → 1d 2h 3m 4.564s —
//             every component NONZERO and distinct = the diverging middle; a swapped h/m, a 24h-
//             uncapped hour, or a dropped day all change the string. Fraction .564 keeps ff="56"
//             ≥4e4 ticks away from a digit boundary — the double carrier at 2021 scale wobbles ±3
//             ticks, fork-timespan-ticks-from-double residual; full-7-digit assertions use the
//             1970-scale anchor below, where the double is tick-exact).
//
//   G1 fmt "hh\:mm\:ss\:ff" (.t3 default) → "02:03:04:56"   (escaped-colon custom; ff = 2-digit
//                                                            TRUNCATED tick fraction)
//   G2 fmt "d\.hh\:mm"                    → "1.02:03"        (d + escape '.')
//   G3 fmt "hh:mm"                        → "Invalid Format" (★unescaped ':' in a TimeSpan custom
//                                          format throws FormatException → the .cs:41-45 catch leg)
//   G4 LaunchTime "1970-01-01 00:00" (target=0), now=97445.56 (=1d 3h 4m 5.56s, tick-exact), fmt ""
//                                          → "1.03:04:05.5600000"  (empty → standard "c":
//                                          [-][d.]hh:mm:ss[.fffffff])
//   G5 LaunchTime "not a time", now=97445 (1970-01-02 03:04:05), fmt "hh\:mm\:ss" → "03:04:05"
//      (parse fail → DateTime.Today = UTC midnight 86400 → duration 11045 s = 3h4m5s)
//   G6 LaunchTime "1970-01-03 00:00" (=172800), now=97445 → duration = -75355 s = -(20h55m55s):
//      fmt "hh\:mm\:ss" → "20:55:55"  (custom formats print ABSOLUTE components, NO sign)
//   G7 same, fmt "c"    → "-20:55:55" (standard "c" DOES carry the sign; days==0 → no "d." part)
//
// injectBug: stringInjectBug() → the production cook drops its last output char (real cook-path
// corruption, the string-family tooth) → every non-empty want diverges → RED. Did-not-trip → 0.
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/datetime_host.h"       // setHostNowOverrideForTest (determinism pin)
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS
#include "runtime/string_op_registry.h"  // findStringOp / StringCookCtx / stringInjectBug

namespace sw {
namespace {

// Invoke the REGISTERED CountDown cook through a hand-built ctx. inputStrings = [LaunchTime, Format]
// (spec port order — the same gather the driver performs for two single String ports).
std::string cookDirect(const std::string& launch, const std::string& fmt) {
  const StringCookFn* fn = findStringOp("CountDown");
  if (!fn || !*fn) return "<no-op>";
  std::vector<std::string> inputs{launch, fmt};
  std::string out;
  StringCookCtx c;
  c.inputStrings = &inputs;
  c.output = &out;
  (*fn)(c);
  return out;
}

struct Case {
  double nowEpoch;
  const char* launch;
  const char* fmt;
  const char* want;
  const char* note;
};

}  // namespace

int runCountDownSelftestImpl(bool injectBug) {
  constexpr double kTarget = 1618678800.0;  // 2021-04-17 17:00 (hand-derived above)
  constexpr double kNowA = kTarget + 93784.564;  // 1d 2h 3m 4.564s after target

  const Case cases[] = {
      {kNowA, "2021-04-17 17:00", "hh\\:mm\\:ss\\:ff", "02:03:04:56", ".t3 default fmt"},
      {kNowA, "2021-04-17 17:00", "d\\.hh\\:mm", "1.02:03", "day + escapes"},
      {kNowA, "2021-04-17 17:00", "hh:mm", "Invalid Format", "unescaped colon throws"},
      {97445.56, "1970-01-01 00:00", "", "1.03:04:05.5600000", "empty -> standard c (tick-exact)"},
      {97445.0, "not a time", "hh\\:mm\\:ss", "03:04:05", "parse fail -> Today midnight"},
      {97445.0, "1970-01-03 00:00", "hh\\:mm\\:ss", "20:55:55", "negative: custom sign-less"},
      {97445.0, "1970-01-03 00:00", "c", "-20:55:55", "negative: standard c signed"},
  };

  stringInjectBug() = injectBug;
  bool ok = true;
  int i = 0;
  for (const Case& c : cases) {
    ++i;
    setHostNowOverrideForTest(c.nowEpoch);
    const std::string got = cookDirect(c.launch, c.fmt);
    const bool pass = (got == c.want);
    ok = ok && pass;
    std::printf("[selftest-countdown] G%d (%s) got=\"%s\" want=\"%s\" -> %s\n", i, c.note,
                got.c_str(), c.want, pass ? "PASS" : "FAIL");
  }

  // Hygiene: restore production behavior.
  setHostNowOverrideForTest(-1.0);
  stringInjectBug() = false;

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-countdown] injectBug did NOT trip (drop-last-char tooth is dead)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-countdown] injectBug correctly RED (last output char dropped on the real "
                "cook path)\n");
    return 1;
  }
  std::printf("[selftest-countdown] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

// Register as --selftest-countdown (order 242, after --selftest-blendstrings at 241).
REGISTER_SELFTESTS(/*orderBase=*/242, {"countdown", sw::runCountDownSelftestImpl});
