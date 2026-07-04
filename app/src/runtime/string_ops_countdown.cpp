// CountDown string op (string self-registration seam leaf — wall-clock duration → formatted string).
// TiXL authority: Operators/Lib/string/datetime/CountDown.cs:16-47 (verbatim below):
//
//   CountDown.cs Update():
//     var now = DateTime.Now;                                        // :18 (wall clock)
//     try {
//       var targetTime = DateTime.Today;                             // :22 (parse-fail fallback)
//       if (DateTime.TryParse(LaunchTime.GetValue(ctx), out var d))  // :24
//         targetTime = d;
//       else Log.Warning($"invalid format for lauchTime ...");       // :31 (warning only)
//       var duration = DateTime.Now - targetTime;                    // :34 (a TimeSpan)
//       var format = Format.GetValue(ctx);                           // :37
//       Output.Value = duration.ToString(format, InvariantCulture);  // :38
//     } catch (FormatException) { Output.Value = "Invalid Format"; } // :41-45
//   Inputs: LaunchTime / Format (both InputSlot<string>). Output = Slot<string>
//   (DirtyFlagTrigger.Animated — re-cooks every frame; sw's string rail cooks every frame anyway).
//   CountDown.t3 defaults (re-read & confirmed): LaunchTime "2021-04-17 17:00",
//   Format "hh\:mm\:ss\:ff" (JSON-unescaped: hh\:mm\:ss\:ff — escaped-colon TimeSpan custom format).
//
// NOTE the name lies: duration = Now - target — for a PAST target this counts UP since the target
// (only a FUTURE LaunchTime counts down, and then .NET custom formats print the ABSOLUTE components,
// sign-less). Ported verbatim, quirk and all.
//
// EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). inputStrings = [LaunchTime, Format]
// (two single-cardinality String ports in spec port order, each wire-OR-const). No Float params.
//
// THE FORMAT ENGINE — .NET TimeSpan.ToString(format, InvariantCulture), ported per spec:
//   STANDARD (whole format is 1 char, or empty): "c"/"t"/"T" = constant [-][d.]hh:mm:ss[.fffffff];
//     "g" = short [-][d:]h:mm:ss[.fffffff-trimmed]; "G" = long [-]d:hh:mm:ss.fffffff. Any other
//     single char → FormatException. Empty/null → "c".
//   CUSTOM (2+ chars): specifier runs d(1-8) / h(1-2) / m(1-2) / s(1-2) / f(1-7) / F(1-7, trailing
//     zeros trimmed), "\x" escape, 'quoted' / "quoted" literals, "%x" single-specifier escape.
//     ★Any OTHER unescaped char — INCLUDING ':' '.' ' ' — throws FormatException (unlike DateTime
//     formats, TimeSpan custom formats have NO separator chars; that is exactly why the .t3 default
//     escapes its colons, and why the .cs carries the catch → "Invalid Format" leg).
//     Custom formats print ABSOLUTE component values with NO sign (System.Globalization
//     TimeSpanFormat.FormatCustomized: day/time negated before digit emission).
//   Components from ABSOLUTE ticks (100 ns): d = total whole days (no 24h-cap), h = hours%24,
//     m = min%60, s = sec%60, f/F = the 7-digit tick fraction's leading digits (TRUNCATED, not
//     rounded — digits are carved off the tick count).
//
// FORKS (named):
//   - fork-datetime-utc-not-local / fork-datetime-tryparse-narrow-vocabulary: family forks pinned in
//     runtime/datetime_host.h. now AND the parsed target sit in the SAME (UTC) basis, so the
//     DIFFERENCE (the only thing this op renders) is exact whenever both parse — the zone shift
//     cancels. Residual divergence: the DateTime.Today fallback (midnight boundary is the UTC day's,
//     not the local day's — fork-countdown-today-utc-midnight).
//   - fork-countdown-warning-dropped: Log.Warning(:31) is an editor-console affordance; dropped
//     (LogMessage's sink precedent — no behaviour-bearing effect on the output string).
//   - fork-timespan-ticks-from-double: TiXL subtracts two DateTimes (integer 100ns ticks); sw's
//     route-B seconds are double. Ticks are recovered via llround(|sec|*1e7). RESIDUAL: a double at
//     present-day epoch scale (~1.6e9 s) carries ~2.4e-7 s (≈±3 ticks) of representation error, so
//     the LAST 'f' digits of a 2021-scale duration can wobble by a few 100ns — exact at small
//     (1970-scale) magnitudes and for every digit ≥ 1µs. C# is tick-exact (integer DateTime math);
//     this is the double CARRIER, not the format math (the float twin of fork-datetime-epoch-as-float).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <cmath>    // std::llround, std::floor, std::fabs
#include <cstdio>   // std::snprintf
#include <string>
#include <vector>

#include "runtime/datetime_host.h"       // hostNowEpochSeconds / tryParseDateTimeToEpoch (shared kernel)
#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug

namespace sw {

namespace {

constexpr long long kTicksPerSecond = 10000000LL;         // 100 ns ticks (.NET TimeSpan.TicksPerSecond)
constexpr long long kTicksPerDay = 86400LL * kTicksPerSecond;

struct TsParts {  // absolute-value components (custom formats are sign-less — see header)
  bool negative = false;
  long long days = 0;
  int hours = 0, minutes = 0, seconds = 0;
  long long fracTicks = 0;  // [0, 1e7) — the 7-digit fraction
};

TsParts splitTicks(double totalSeconds) {
  TsParts p;
  p.negative = totalSeconds < 0.0;
  // fork-timespan-ticks-from-double (header): recover .NET ticks from the double-seconds carrier.
  const long long ticks = std::llround(std::fabs(totalSeconds) * static_cast<double>(kTicksPerSecond));
  p.days = ticks / kTicksPerDay;
  long long rem = ticks % kTicksPerDay;
  p.hours = static_cast<int>(rem / (3600LL * kTicksPerSecond));
  rem %= 3600LL * kTicksPerSecond;
  p.minutes = static_cast<int>(rem / (60LL * kTicksPerSecond));
  rem %= 60LL * kTicksPerSecond;
  p.seconds = static_cast<int>(rem / kTicksPerSecond);
  p.fracTicks = rem % kTicksPerSecond;
  return p;
}

void appendPadded(std::string& out, long long v, int width) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%0*lld", width, v);
  out += buf;
}

// f/F: the leading `count` digits of the 7-digit tick fraction (TRUNCATED — carved off the ticks).
// F trims trailing zeros (emits nothing when they are all zero).
void appendFraction(std::string& out, long long fracTicks, int count, bool trim) {
  long long scaled = fracTicks;
  for (int i = 0; i < 7 - count; ++i) scaled /= 10;
  char buf[16];
  std::snprintf(buf, sizeof buf, "%0*lld", count, scaled);
  int len = count;
  if (trim)
    while (len > 0 && buf[len - 1] == '0') --len;
  out.append(buf, static_cast<size_t>(len));
}

// One custom specifier run. Returns false (FormatException) on an over-long run.
bool appendRun(std::string& out, const TsParts& p, char spec, int count) {
  switch (spec) {
    case 'd':
      if (count > 8) return false;
      appendPadded(out, p.days, count);
      return true;
    case 'h':
      if (count > 2) return false;
      appendPadded(out, p.hours, count);
      return true;
    case 'm':
      if (count > 2) return false;
      appendPadded(out, p.minutes, count);
      return true;
    case 's':
      if (count > 2) return false;
      appendPadded(out, p.seconds, count);
      return true;
    case 'f':
      if (count > 7) return false;
      appendFraction(out, p.fracTicks, count, /*trim=*/false);
      return true;
    case 'F':
      if (count > 7) return false;
      appendFraction(out, p.fracTicks, count, /*trim=*/true);
      return true;
    default:
      return false;  // not a specifier — caller throws
  }
}

bool isSpecChar(char c) {
  return c == 'd' || c == 'h' || c == 'm' || c == 's' || c == 'f' || c == 'F';
}

// The "c" constant form: [-][d.]hh:mm:ss[.fffffff]. Shared by "c"/"t"/"T"/empty.
void formatConstant(std::string& out, const TsParts& p) {
  if (p.negative) out += '-';
  if (p.days != 0) {
    appendPadded(out, p.days, 1);
    out += '.';
  }
  appendPadded(out, p.hours, 2);
  out += ':';
  appendPadded(out, p.minutes, 2);
  out += ':';
  appendPadded(out, p.seconds, 2);
  if (p.fracTicks != 0) {
    out += '.';
    appendFraction(out, p.fracTicks, 7, /*trim=*/false);
  }
}

// TimeSpan.ToString(format, InvariantCulture) — see THE FORMAT ENGINE (header). Returns false on
// FormatException (the caller renders "Invalid Format", CountDown.cs:41-45).
bool formatTimeSpan(double totalSeconds, const std::string& fmt, std::string& out) {
  out.clear();
  const TsParts p = splitTicks(totalSeconds);

  if (fmt.empty()) {  // null/empty → "c"
    formatConstant(out, p);
    return true;
  }
  if (fmt.size() == 1) {  // STANDARD single-letter forms
    switch (fmt[0]) {
      case 'c':
      case 't':
      case 'T':
        formatConstant(out, p);
        return true;
      case 'g':  // [-][d:]h:mm:ss[.fffffff-trimmed]
        if (p.negative) out += '-';
        if (p.days != 0) {
          appendPadded(out, p.days, 1);
          out += ':';
        }
        appendPadded(out, p.hours, 1);
        out += ':';
        appendPadded(out, p.minutes, 2);
        out += ':';
        appendPadded(out, p.seconds, 2);
        if (p.fracTicks != 0) {
          out += '.';
          appendFraction(out, p.fracTicks, 7, /*trim=*/true);
        }
        return true;
      case 'G':  // [-]d:hh:mm:ss.fffffff (always full)
        if (p.negative) out += '-';
        appendPadded(out, p.days, 1);
        out += ':';
        appendPadded(out, p.hours, 2);
        out += ':';
        appendPadded(out, p.minutes, 2);
        out += ':';
        appendPadded(out, p.seconds, 2);
        out += '.';
        appendFraction(out, p.fracTicks, 7, /*trim=*/false);
        return true;
      default:
        return false;  // invalid standard specifier → FormatException
    }
  }

  // CUSTOM format (2+ chars): specifier runs / \x escapes / quoted literals / %x; all else throws.
  size_t i = 0;
  const size_t n = fmt.size();
  while (i < n) {
    const char c = fmt[i];
    if (c == '\\') {  // escape: next char is a literal
      if (i + 1 >= n) return false;
      out += fmt[i + 1];
      i += 2;
    } else if (c == '\'' || c == '"') {  // quoted literal (must be terminated)
      const char quote = c;
      size_t j = i + 1;
      while (j < n && fmt[j] != quote) {
        out += fmt[j];
        ++j;
      }
      if (j >= n) return false;  // unterminated quote → FormatException
      i = j + 1;
    } else if (c == '%') {  // %x = single-count specifier
      if (i + 1 >= n || !isSpecChar(fmt[i + 1])) return false;
      if (!appendRun(out, p, fmt[i + 1], 1)) return false;
      i += 2;
    } else if (isSpecChar(c)) {  // specifier run
      size_t j = i;
      while (j < n && fmt[j] == c) ++j;
      if (!appendRun(out, p, c, static_cast<int>(j - i))) return false;
      i = j;
    } else {
      return false;  // ★unescaped ':' '.' ' ' or any other char → FormatException (header)
    }
  }
  return true;
}

// cookCountDown: verbatim port of CountDown.cs:16-47 (see header).
void cookCountDown(StringCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // inputStrings = [LaunchTime, Format] (spec port order; wire-OR-const).
  const std::string launch =
      (c.inputStrings && c.inputStrings->size() > 0) ? (*c.inputStrings)[0] : std::string{};
  const std::string fmt =
      (c.inputStrings && c.inputStrings->size() > 1) ? (*c.inputStrings)[1] : std::string{};

  const double now = hostNowEpochSeconds();  // DateTime.Now (:18) via the family wall-clock seam
  double target;
  if (!tryParseDateTimeToEpoch(launch, target)) {
    // DateTime.Today (:22) = midnight of the current day (fork-countdown-today-utc-midnight).
    // Log.Warning(:31) dropped — fork-countdown-warning-dropped.
    target = std::floor(now / 86400.0) * 86400.0;
  }
  const double duration = now - target;  // DateTime.Now - targetTime (:34)

  std::string s;
  if (!formatTimeSpan(duration, fmt, s)) s = "Invalid Format";  // the catch leg (:41-45)
  *c.output = s;

  // Test-only: corrupt the output so the golden's RED case bites on the actual cook path.
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — independent leaf .cpp (no shared edit point).
//   Ports: "Output" = the String output; "LaunchTime"/"Format" = single String inputs
//   (wire-OR-const; strDefs = the CountDown.t3 defaults, colons escaped per the TimeSpan engine).
static const StringOp _reg_countdown{
    {"CountDown", "CountDown",
     {{"Output", "Output", "String", false},
      {"LaunchTime", "LaunchTime", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1,
       false, "2021-04-17 17:00"},
      {"Format", "Format", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false,
       "hh\\:mm\\:ss\\:ff"}},
     /*evaluate=*/nullptr,  // String output cannot ride NodeSpec::evaluate (returns ONE float)
     "string.datetime"},
    cookCountDown};

}  // namespace sw
