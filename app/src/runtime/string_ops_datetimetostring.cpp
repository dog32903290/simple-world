// DateTimeToString string op (string self-registration seam leaf — DateTime route B: float-epoch).
// TiXL authority: Operators/Lib/string/datetime/DateTimeToString.cs (verbatim below).
//
//   DateTimeToString.cs Update():
//     var v = Value.GetValue(context);                    // a System.DateTime
//     var format = Format.GetValue(context);              // a custom/standard .NET format string
//     try {
//       Output.Value = string.IsNullOrEmpty(format)
//           ? v.ToString(CultureInfo.InvariantCulture)
//           : v.ToString(format, CultureInfo.InvariantCulture);
//     } catch (FormatException) { Output.Value = "Invalid Format"; }
//   Ports: Value=InputSlot<DateTime>; Format=InputSlot<string>. Output=Slot<string>.
//   DateTimeToString.t3: Format has no scalar default (empty → general "G" rendering).
//
// ROUTE B (DateTime-as-epoch-Float, the no-new-currency port — task directive, same as DateTimeToFloat):
//   `Value` here = Unix-epoch SECONDS on a Float port; the calendar fields are derived via host gmtime
//   (UTC). EVAL-SIDE LAYOUT: a String PRODUCER (rides cookStringNode). Value is read from the resolved
//   Float params (sc.params); Format is its ONE String input → inputStrings[0] (wired upstream string,
//   or strDef const when unwired — here strDef default "" = the general "G" form).
//
// FORKS (named):
//   - fork-datetime-epoch-as-float: DateTime → Unix-epoch SECONDS on a Float port (route B; same carrier
//     precision caveat as DateTimeToFloat — present-day epochs degrade in the 24-bit float mantissa, so
//     the golden drives 1970-anchored epochs whose rendered fields are exact). The MATH is exact for the
//     epoch given; gmtime (UTC) yields the calendar fields.
//   - fork-datetime-net-format-narrow-vocabulary: .NET DateTime custom format strings are a LARGE
//     vocabulary (d/dd/ddd/dddd, M/MM/MMM/MMMM, y/yy/yyy/yyyy, H/HH/h/hh, m/mm, s/ss, tt, f..fffffff,
//     K/z/zzz, standard single-letter forms d/D/t/T/f/F/g/G/s/u/o/r...). C++ has no .NET format engine.
//     We implement the COMMON custom specifiers faithfully (yyyy/yy/MM/M/dd/d/HH/H/hh/h/mm/m/ss/s/tt/
//     MMM/MMMM/ddd/dddd) + the general empty-format "G"-style form + the standard "s" (sortable
//     ISO-8601) and "u" (universal-sortable) single-letter forms, and pass any UNRECOGNISED specifier
//     character THROUGH AS A LITERAL — which is exactly what .NET does for an unknown char inside a
//     MULTI-char custom format (it does NOT throw). ★fork-datetime-lonechar-no-formatexception:
//     a LONE single char is treated by .NET as a STANDARD specifier; if it is not a valid standard form
//     (e.g. "q", or "M"/"H"/"y" alone) .NET THROWS FormatException → the .cs catch yields "Invalid Format".
//     We do NOT implement that throw-leg — only "s"/"u" lone-chars are handled as standard; any other lone
//     char falls through renderCustom (rendered as a custom specifier/literal, never "Invalid Format").
//     This lone-char edge DIVERGES (named here, untested); all MULTI-char real timestamp patterns are
//     faithful. The supported vocabulary covers every common timestamp
//     pattern; less-common specifiers (f-fractions, z/zzz offsets, K, M/MMM month names beyond English,
//     R/RFC1123, o/round-trip) render their literal-passthrough or English-name approximation, NAMED
//     here rather than throwing.
//   - fork-datetime-utc-invariant: InvariantCulture + UTC. Month/day names are the English invariant
//     abbreviations (Jan..Dec / Sun..Sat) — InvariantCulture IS English, so MMM/MMMM/ddd/dddd are
//     byte-exact to .NET InvariantCulture. AM/PM (tt) = "AM"/"PM" (invariant designators).
//   - fork-string-host-not-gpu: string is host currency; no GPU EvaluationContext touched.
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringFloatParam / stringInjectBug

namespace sw {
namespace {

const char* const kMonthAbbr[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kMonthFull[12] = {"January", "February", "March",     "April",   "May",      "June",
                                    "July",    "August",   "September", "October", "November", "December"};
const char* const kDayAbbr[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};  // tm_wday 0=Sun
const char* const kDayFull[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// Append `v` zero-padded to `width` digits.
void pad(std::string& out, int v, int width) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%0*d", width, v);
  out += buf;
}

// Render a .NET custom DateTime format string against a broken-down UTC `tm`. Unknown chars pass
// through as literals (faithful to .NET custom-format behaviour). Recognises the common specifier runs.
std::string renderCustom(const std::string& fmt, const std::tm& t) {
  std::string out;
  const int hour24 = t.tm_hour;
  const int hour12 = (hour24 % 12 == 0) ? 12 : hour24 % 12;
  const int year = t.tm_year + 1900;
  const int month = t.tm_mon + 1;   // tm_mon 0-based
  const int wday = t.tm_wday;       // 0=Sun

  for (std::string::size_type i = 0; i < fmt.size();) {
    const char c = fmt[i];
    // Count the run length of the same specifier char.
    std::string::size_type j = i;
    while (j < fmt.size() && fmt[j] == c) ++j;
    const int run = (int)(j - i);

    switch (c) {
      case 'y':  // yy → 2-digit, yyyy → 4-digit (yyy → 3 min, etc.)
        if (run <= 2) pad(out, year % 100, 2);
        else pad(out, year, run);
        i = j; continue;
      case 'M':  // M/MM numeric ; MMM abbr ; MMMM full
        if (run == 1) out += std::to_string(month);
        else if (run == 2) pad(out, month, 2);
        else if (run == 3) out += kMonthAbbr[month - 1];
        else out += kMonthFull[month - 1];
        i = j; continue;
      case 'd':  // d/dd numeric day ; ddd abbr weekday ; dddd full weekday
        if (run == 1) out += std::to_string(t.tm_mday);
        else if (run == 2) pad(out, t.tm_mday, 2);
        else if (run == 3) out += kDayAbbr[wday];
        else out += kDayFull[wday];
        i = j; continue;
      case 'H':  // 24-hour
        if (run == 1) out += std::to_string(hour24);
        else pad(out, hour24, 2);
        i = j; continue;
      case 'h':  // 12-hour
        if (run == 1) out += std::to_string(hour12);
        else pad(out, hour12, 2);
        i = j; continue;
      case 'm':  // minute
        if (run == 1) out += std::to_string(t.tm_min);
        else pad(out, t.tm_min, 2);
        i = j; continue;
      case 's':  // second
        if (run == 1) out += std::to_string(t.tm_sec);
        else pad(out, t.tm_sec, 2);
        i = j; continue;
      case 't':  // t → "A"/"P" ; tt → "AM"/"PM" (invariant)
        if (run == 1) out += (hour24 < 12) ? "A" : "P";
        else out += (hour24 < 12) ? "AM" : "PM";
        i = j; continue;
      default:
        // Unknown char → literal passthrough (faithful to .NET custom-format: unknown chars are
        // copied verbatim). '\\' escapes the next char; ':' and '/' are date/time separators that in
        // InvariantCulture render as themselves.
        if (c == '\\' && i + 1 < fmt.size()) { out += fmt[i + 1]; i += 2; continue; }
        out.append(run, c);
        i = j; continue;
    }
  }
  return out;
}

// The general / "G" form (= v.ToString(InvariantCulture)): InvariantCulture's "G" pattern is
// "MM/dd/yyyy HH:mm:ss" (24-hour). Empty Format hits this (string.IsNullOrEmpty(format) leg).
std::string renderGeneral(const std::tm& t) { return renderCustom("MM/dd/yyyy HH:mm:ss", t); }

// DateTimeToString: Value (resolved Float epoch param) + Format (String input) → host string.
void cookDateTimeToString(StringCookCtx& c) {
  if (!c.output) return;
  const float epoch = stringFloatParam(c.params, "Value", 0.0f);  // route-B epoch seconds
  std::string fmt;
  if (c.inputStrings && !c.inputStrings->empty()) fmt = (*c.inputStrings)[0];  // Format (inputStrings[0])

  const std::time_t tt = (std::time_t)std::floor((double)epoch);
  std::tm t{};
#if defined(_WIN32)
  gmtime_s(&t, &tt);
#else
  gmtime_r(&tt, &t);
#endif

  std::string result;
  if (fmt.empty()) {
    result = renderGeneral(t);  // string.IsNullOrEmpty(format) ? v.ToString(InvariantCulture)
  } else if (fmt == "s") {
    // Standard "s" = sortable ISO-8601 "yyyy-MM-ddTHH:mm:ss" (culture-invariant by definition).
    result = renderCustom("yyyy-MM-dd\\THH:mm:ss", t);
  } else if (fmt == "u") {
    // Standard "u" = universal-sortable "yyyy-MM-dd HH:mm:ssZ".
    result = renderCustom("yyyy-MM-dd HH:mm:ss\\Z", t);
  } else {
    result = renderCustom(fmt, t);  // custom format string
  }
  *c.output = result;

  // Test-only: corrupt the REAL output (drop the last char) so a golden's transport RED bites on the
  // actual cook path, not by flipping the expected value. Off in production. (Mirror of FloatToString.)
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

}  // namespace

// Self-registration. File-scope static StringOp — explicit CMake list (string_ops_*).
//   Ports: "Value"  = scalar Float input (the route-B epoch seconds; read via resolved params);
//          "Format" = String input (wire-OR-const; strDef "" = the general "G" rendering);
//          "Output" = the String output (host string currency).
static const StringOp _reg_datetimetostring{
    {"DateTimeToString", "DateTimeToString",
     {{"Output", "Output", "String", false},
      {"Value", "Value", "Float", true, 0.0f, -2.0e9f, 2.0e9f, Widget::Slider},
      {"Format", "Format", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookDateTimeToString};

}  // namespace sw
