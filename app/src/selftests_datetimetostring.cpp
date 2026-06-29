// selftests_datetimetostring — DateTimeToString golden (--selftest-datetimetostring / -bug). HERMETIC,
// byte-exact.
//
// DateTimeToString (runtime/string_ops_datetimetostring.cpp) renders a route-B epoch-seconds Float into
// a string via a .NET-style DateTime format string. = TiXL Operators/Lib/string/datetime/
// DateTimeToString.cs (v.ToString(format, InvariantCulture)). The String rail is FLAT-cook; this golden
// drives the PRODUCTION cook fn through a hand-built StringCookCtx (the cook-driver shape), exactly like
// the ReadFile golden. Epochs are 1970-anchored so every rendered field is float-EXACT
// (fork-datetime-epoch-as-float).
//
// Reference epoch: 1970-01-15T13:45:09Z. Compute:
//   days = 14 (Jan 15 is the 15th → 14 whole days after Jan 1) → 14*86400 = 1209600
//   time = 13:45:09 = 13*3600 + 45*60 + 9 = 46800 + 2700 + 9 = 49509
//   epoch = 1209600 + 49509 = 1259109 s.  Jan 15 1970 was a THURSDAY (1970-01-01 = Thursday;
//   +14 days = +2 weeks → Thursday again). gmtime(1259109) → 1970-01-15 13:45:09, tm_wday=4 (Thu).
//
// The teeth (-bug): the leaf's stringInjectBug() hook drops the last output char (corrupts the REAL
// cook path); each expected value is the full string → -bug RED. (Mirror of FloatToString/ReadFile.)
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS
#include "runtime/string_op_registry.h"  // StringCookCtx / findStringOp / stringInjectBug

namespace sw {
namespace {

// Cook DateTimeToString through a hand-built StringCookCtx. epoch → params["Value"]; format →
// inputStrings[0] (empty vector = unwired → the op's strDef "" general form is NOT reached here, so an
// EMPTY-format case passes an explicit "" entry). injectBug toggles the leaf's output-corruption hook.
std::string cookDTToStr(float epoch, const std::string& format, bool injectBug) {
  const StringCookFn* fn = findStringOp("DateTimeToString");
  if (!fn || !*fn) return {"ERR:NO_FN"};

  std::vector<std::string> inputs{format};  // inputStrings[0] = Format
  std::map<std::string, float> params{{"Value", epoch}};
  std::string out;
  StringCookCtx ctx{};
  ctx.inputStrings = &inputs;
  ctx.params = &params;
  ctx.output = &out;

  stringInjectBug() = injectBug;
  (*fn)(ctx);
  stringInjectBug() = false;
  return out;
}

int runDateTimeToStringSelftestImpl(bool injectBug) {
  const float kEpoch = 1259109.0f;  // 1970-01-15T13:45:09Z (Thursday) — see header arithmetic.
  bool ok = true;

  auto check = [&](const char* tag, const std::string& got, const std::string& want) {
    bool pass = (got == want);
    ok = ok && pass;
    std::printf("[selftest-datetimetostring] %s got=\"%s\" want=\"%s\" -> %s\n", tag, got.c_str(),
                want.c_str(), pass ? "PASS" : "FAIL");
  };

  // G1: ISO-ish custom "yyyy-MM-dd HH:mm:ss" → "1970-01-15 13:45:09".
  check("G1 iso", cookDTToStr(kEpoch, "yyyy-MM-dd HH:mm:ss", injectBug), "1970-01-15 13:45:09");

  // G2: empty format → general InvariantCulture "G" = "MM/dd/yyyy HH:mm:ss" → "01/15/1970 13:45:09".
  check("G2 general", cookDTToStr(kEpoch, "", injectBug), "01/15/1970 13:45:09");

  // G3: 12-hour + AM/PM "hh:mm tt" → hour 13 → 12h = 1, PM → "01:45 PM".
  check("G3 12h", cookDTToStr(kEpoch, "hh:mm tt", injectBug), "01:45 PM");

  // G4: month/weekday names "ddd MMM d yyyy" → Thu Jan 15 1970 → "Thu Jan 15 1970".
  check("G4 names", cookDTToStr(kEpoch, "ddd MMM d yyyy", injectBug), "Thu Jan 15 1970");

  // G5: full names "dddd, MMMM d" → "Thursday, January 15".
  check("G5 full names", cookDTToStr(kEpoch, "dddd, MMMM d", injectBug), "Thursday, January 15");

  // G6: standard "s" sortable → "1970-01-15T13:45:09".
  check("G6 sortable", cookDTToStr(kEpoch, "s", injectBug), "1970-01-15T13:45:09");

  // G7: standard "u" universal → "1970-01-15 13:45:09Z".
  check("G7 universal", cookDTToStr(kEpoch, "u", injectBug), "1970-01-15 13:45:09Z");

  // G8: 2-digit year + single-digit M/d "yy/M/d" → "70/1/15".
  check("G8 short", cookDTToStr(kEpoch, "yy/M/d", injectBug), "70/1/15");

  std::printf("[selftest-datetimetostring] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-register as --selftest-datetimetostring. orderBase 270 = a free slot after readfile (269). NO
// shared-file edit — this manifest is globbed via src/selftests*.cpp (app/CMakeLists.txt SW_SELFTEST_SRCS).
REGISTER_SELFTESTS(/*orderBase=*/270, {"datetimetostring", runDateTimeToStringSelftestImpl});

}  // namespace sw
