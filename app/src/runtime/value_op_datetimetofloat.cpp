// DateTimeToFloat value op (value-op self-registration seam leaf — DateTime route B: float-epoch).
// TiXL authority: Operators/Lib/string/datetime/DateTimeToFloat.cs (verbatim below).
//
//   DateTimeToFloat.cs Update():
//     var dateTime = Value.GetValue(context);                       // a System.DateTime
//     var offset   = HourOffset.GetValue(context);                  // hours (float)
//     var offsetSpan = new TimeSpan(0, (int)offset, (int)(offset*60 % 60), (int)(offset*60*60 % 60));
//     dateTime -= offsetSpan;
//     value = OutputMapping switch {
//       TimeOfDay_Hours      => (float)(dateTime.TimeOfDay.TotalMilliseconds / (60*60*1000)),
//       TimeOfDay_Normalized => (float)(dateTime.TimeOfDay.TotalMilliseconds / (24*60*60*1000)),
//       DayOfTheYear         => dateTime.DayOfYear,
//       DayOfTheMonths       => dateTime.Day,
//       _                    => 0f };
//     Output.Value = value;
//   DateTimeToFloat.t3 defaults: OutputMapping=0 (TimeOfDay_Hours), HourOffset=0.0, Value=null.
//
// ROUTE B (DateTime-as-epoch-Float, the no-new-currency port — task directive, mirrors Matrix-as-16-
//   floats / Vec3ToString): TiXL's DateTime port is a System.DateTime currency. sw has no DateTime
//   currency; route B carries a DateTime as a DOUBLE-EPOCH (Unix seconds since 1970-01-01T00:00:00) on
//   a plain Float port. The DateTime PRODUCERS (NowAsDateTime / StringToDateTime — deferred) emit epoch
//   seconds; this op consumes one. So `Value` here = epoch SECONDS (Float). The calendar fields
//   (TimeOfDay/DayOfYear/Day) are derived from that epoch via host gmtime (UTC) — see forks.
//
// FORKS (named):
//   - fork-datetime-epoch-as-float: DateTime currency → Unix-epoch SECONDS on a Float port (route B).
//     Float has 24-bit mantissa; a present-day epoch (~1.75e9 s) exceeds it, so the SECONDS resolution
//     of the input degrades to ~128 s near 2025 (a known route-B limit — sub-field precision for
//     TimeOfDay near the present is coarse). The op MATH is exact for the epoch it is GIVEN; the
//     precision loss is in the float CARRIER, identical to every other large-magnitude Float in sw.
//     The golden therefore drives epochs whose field outputs are float-representable EXACTLY.
//   - fork-datetime-utc-not-local: .NET DateTime fields are read in the DateTime's own (naive/local)
//     calendar; route-B epoch is UTC. The HourOffset input already exists to shift into any local zone
//     (it is a hours TimeSpan SUBTRACTED before the field read, cs verbatim). So this op extracts UTC
//     calendar fields of (epoch − offset), which is byte-exact to .NET when the DateTime was UTC-naive
//     and the offset carries the zone — the common producer path (NowAsDateTime → DateTime.Now is local
//     in TiXL; route-B producers are deferred and will emit UTC epoch, so HourOffset is the zone knob).
//   - fork-datetime-offsetspan-truncation: the offset TimeSpan uses C#'s (int) truncation-toward-zero
//     decomposition (hours=(int)offset, min=(int)(offset*60%60), sec=(int)(offset*3600%60)) — ported
//     verbatim so a fractional HourOffset decomposes IDENTICALLY (e.g. 1.5h → 1h30m0s).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>
#include <ctime>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runDateTimeToFloatSelfTest(bool injectBug);

namespace {

// C# `new TimeSpan(0, (int)offset, (int)(offset*60 % 60), (int)(offset*60*60 % 60))` → total SECONDS.
// (int) is truncation toward zero (C# cast semantics); std::fmod matches C#'s % on doubles.
inline double offsetSpanSeconds(double offset) {
  const long hours = (long)offset;                       // (int)offset
  const long mins = (long)std::fmod(offset * 60.0, 60.0);   // (int)(offset*60 % 60)
  const long secs = (long)std::fmod(offset * 3600.0, 60.0); // (int)(offset*60*60 % 60)
  return (double)hours * 3600.0 + (double)mins * 60.0 + (double)secs;
}

// in[] = [Value(epoch seconds), OutputMapping(enum), HourOffset(hours)].
//   OutputMapping: 0=TimeOfDay_Hours, 1=TimeOfDay_Normalized, 2=DayOfTheYear, 3=DayOfTheMonths.
float evalDateTimeToFloat(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const double epoch = (double)in[0] - offsetSpanSeconds((double)in[2]);  // dateTime -= offsetSpan
  const int mode = (int)std::lround((double)in[1]);

  // Decompose the adjusted epoch into UTC calendar fields. TimeOfDay (since-midnight) in ms uses the
  // positive modulo of epoch by one day; gmtime gives DayOfYear (tm_yday+1) and Day (tm_mday).
  switch (mode) {
    case 0:    // TimeOfDay_Hours = ms / 3,600,000
    case 1: {  // TimeOfDay_Normalized = ms / 86,400,000
      double secOfDay = std::fmod(epoch, 86400.0);
      if (secOfDay < 0.0) secOfDay += 86400.0;  // since-midnight is always [0,86400)
      const double ms = secOfDay * 1000.0;
      return (mode == 0) ? (float)(ms / (60.0 * 60.0 * 1000.0))
                         : (float)(ms / (24.0 * 60.0 * 60.0 * 1000.0));
    }
    case 2:    // DayOfTheYear = dateTime.DayOfYear (1-based)
    case 3: {  // DayOfTheMonths = dateTime.Day
      const std::time_t t = (std::time_t)std::floor(epoch);
      std::tm tmv{};
#if defined(_WIN32)
      gmtime_s(&tmv, &t);
#else
      gmtime_r(&t, &tmv);
#endif
      return (mode == 2) ? (float)(tmv.tm_yday + 1) : (float)tmv.tm_mday;
    }
    default:
      return 0.0f;  // cs `_ => 0f`
  }
}

}  // namespace

// Self-registration. File-scope static ValueOp — CMake globs value_op*.cpp; no shared edit point.
//   Port order MUST match evalDateTimeToFloat's in[]: Value, OutputMapping, HourOffset, then Output.
//   Defaults from DateTimeToFloat.t3: OutputMapping=0 (TimeOfDay_Hours), HourOffset=0.0, Value=0 (the
//   route-B epoch default = 1970-01-01T00:00:00Z; .t3's null DateTime has no scalar default).
static const ValueOp _reg_datetimetofloat{
    {"DateTimeToFloat", "DateTimeToFloat",
     {{"Value",         "Value",         "Float", true, 0.0f, -2.0e9f, 2.0e9f, Widget::Slider},
      {"OutputMapping", "OutputMapping", "Float", true, 0.0f, 0.0f,    3.0f,   Widget::Enum,
       {"TimeOfDay_Hours", "TimeOfDay_Normalized", "DayOfTheYear", "DayOfTheMonths"}},
      {"HourOffset",    "HourOffset",    "Float", true, 0.0f, -24.0f,  24.0f,  Widget::Slider},
      {"Output",        "Output",        "Float", false}},
     evalDateTimeToFloat},
    "datetimetofloat", runDateTimeToFloatSelfTest};

// --- DateTimeToFloat MATH golden -----------------------------------------------------------------
// Builds a 1-node DateTimeToFloat graph, sets the inputs, pulls Output via evalFloat, compares to the
// hand-computed closed form (epoch arithmetic against DateTimeToFloat.cs). Epochs chosen so every field
// output is float-EXACT (fork-datetime-epoch-as-float: present-day epochs degrade, so the golden uses
// 1970-anchored epochs whose fields are small integers / exact fractions).
int runDateTimeToFloatSelfTest(bool injectBug) {
  bool ok = true;

  auto evalDT = [&](float epoch, float mode, float offset) -> float {
    const NodeSpec* spec = findSpec("DateTimeToFloat");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "DateTimeToFloat";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value"]         = epoch;
    g.node(nid)->params["OutputMapping"] = mode;
    g.node(nid)->params["HourOffset"]    = offset;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Output") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check = [&](const char* tag, float got, float want) {
    bool pass = std::fabs(got - want) < 1e-4f;
    ok = ok && pass;
    printf("[selftest-datetimetofloat] %s got=%.6f want=%.6f -> %s\n", tag, got, want,
           pass ? "PASS" : "FAIL");
  };

  // GOLDEN 1: epoch = 1970-01-01T06:00:00Z = 6*3600 = 21600 s. TimeOfDay_Hours (mode 0) = 6.0.
  //   injectBug asserts 0.0 (the "field-not-derived" / dead-mode bug) → RED.
  {
    float got = evalDT(21600.0f, 0.0f, 0.0f);
    float want = injectBug ? 0.0f : 6.0f;
    check("G1 06:00 -> hours=6", got, want);
  }

  // GOLDEN 2: same epoch, TimeOfDay_Normalized (mode 1) = 6h/24h = 0.25.
  check("G2 06:00 -> norm=0.25", evalDT(21600.0f, 1.0f, 0.0f), 0.25f);

  // GOLDEN 3: epoch = 1970-01-02T00:00:00Z = 86400 s. DayOfTheYear (mode 2) = day-of-year of Jan 2 = 2.
  check("G3 Jan2 -> dayOfYear=2", evalDT(86400.0f, 2.0f, 0.0f), 2.0f);

  // GOLDEN 4: same epoch, DayOfTheMonths (mode 3) = day-of-month of Jan 2 = 2.
  check("G4 Jan2 -> dayOfMonth=2", evalDT(86400.0f, 3.0f, 0.0f), 2.0f);

  // GOLDEN 5 (HourOffset subtract, fork-datetime-offsetspan-truncation): epoch=21600 (06:00Z), offset=2h
  //   → (epoch - 7200) = 14400 s = 04:00 → TimeOfDay_Hours = 4.0. (Proves the offset is SUBTRACTED before
  //   the field read, and the offset TimeSpan decomposes to exactly 2h.)
  check("G5 06:00 -2h -> hours=4", evalDT(21600.0f, 0.0f, 2.0f), 4.0f);

  // GOLDEN 6 (DayOfYear across a leap-irrelevant boundary): epoch = 1970-02-01T00:00:00Z = 31*86400 =
  //   2678400 s. Jan has 31 days → Feb 1 is day-of-year 32.
  check("G6 Feb1 -> dayOfYear=32", evalDT(2678400.0f, 2.0f, 0.0f), 32.0f);

  return ok ? 0 : 1;
}

}  // namespace sw
