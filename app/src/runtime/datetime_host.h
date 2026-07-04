// runtime/datetime_host — the shared host-side DateTime kernel for the string/datetime family
// (NowAsDateTime / CountDown / StringToDateTime). Two jobs, both pure host (no GPU, no UI):
//
//   1. WALL-CLOCK NOW (TiXL DateTime.Now, CountDown.cs:18 / NowAsDateTime.cs:17): the ONE place the
//      family reads the wall clock, with a test override so every golden cooks DETERMINISTICALLY
//      (fixed epoch in, closed-form out — no wall-clock assertions, GOLDEN_STANDARD rule).
//   2. C# DateTime.TryParse → route-B epoch (CountDown.cs:24 / StringToDateTime.cs:18): the shared
//      parse used by BOTH consumers so they accept byte-identical vocabularies.
//
// ROUTE B recap (pinned in value_op_datetimetofloat.cpp): sw has no DateTime currency; a DateTime is
// a Unix-epoch SECONDS double (UTC). Producers emit UTC epoch; DateTimeToFloat.HourOffset is the
// zone knob.
//
// FORKS (named, family-wide):
//   - fork-datetime-utc-not-local: TiXL DateTime.Now / DateTime.TryParse are LOCAL-naive; route B is
//     UTC. Both `now` and a parsed target sit in the SAME basis, so DIFFERENCES (CountDown's
//     duration) are exact; absolute calendar fields shift by the zone (the pinned family fork —
//     DateTimeToFloat.HourOffset is the knob).
//   - fork-datetime-tryparse-narrow-vocabulary: C# DateTime.TryParse (CurrentCulture) accepts a large
//     culture-dependent vocabulary. We accept the ISO-calendar core: "yyyy-M-d", "yyyy-M-d H:m",
//     "yyyy-M-d H:m:s[.fff]" (space or 'T' separator, 1-2 digit month/day/hour/min/sec, optional
//     fractional seconds, optional trailing 'Z'). This covers the .t3-authored form
//     ("2021-04-17 17:00", CountDown.t3) and every machine-written timestamp; culture forms
//     (M/d/yyyy, month names) are NOT accepted — a named divergence, not a silent one.
//
// runtime leaf: pure computation. Consumers: value_op_nowasdatetime.cpp, string_ops_countdown.cpp,
// host_scalar_ops_stringtodatetime.cpp.
#pragma once

#include <string>

namespace sw {

// Wall-clock now as UTC Unix-epoch seconds (TiXL DateTime.Now, route B). When the test override is
// set (>= 0), returns the override instead — the goldens' determinism seam.
double hostNowEpochSeconds();

// Test-only: pin hostNowEpochSeconds() to a fixed epoch (goldens). Pass a negative value to clear
// (production behavior restored). NOT an injectBug seam — this is the CLOCK input, not a corruption.
void setHostNowOverrideForTest(double epochSecs);

// C# DateTime.TryParse subset (fork-datetime-tryparse-narrow-vocabulary above): parse `s` into a UTC
// Unix-epoch seconds value. Returns true + writes epochOut on success; false (epochOut untouched) on
// failure. Accepts "yyyy-M-d[ H:m[:s[.fff]]]" with ' ' or 'T' date/time separator, optional 'Z'.
bool tryParseDateTimeToEpoch(const std::string& s, double& epochOut);

// days-from-civil (Howard Hinnant): days since 1970-01-01 of the proleptic-Gregorian date y-m-d.
// Exposed so CountDown's golden can hand-derive its expected epochs from calendar dates.
long long daysFromCivil(int y, int m, int d);

}  // namespace sw
