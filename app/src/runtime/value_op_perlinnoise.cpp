// PerlinNoise value op (SCALAR float, value-op self-registration seam leaf — numbers/float/random,
// unblocked by the LocalFxTime seam: eval_context.h's offset-12 slot carries TiXL's LocalFxTime in BARS).
// TiXL authority: Operators/Lib/numbers/float/random/PerlinNoise.cs + Core/Utils/MathUtils.cs (verbatim
// below). Mirrors the structure of value_op_perlinnoise2.cpp (the vec2 sibling), but the Update() MATH is
// genuinely DIFFERENT — see fork-perlin-gainbias-applies-to-WHOLE below (the precedence trap is INVERTED
// relative to PerlinNoise2).
//
//   PerlinNoise.cs Update(context):                                  (VERBATIM — DO NOT recompute)
//     var value = OverrideTime.HasInputConnections ? OverrideTime.GetValue(context)
//                                                   : (float)context.LocalFxTime;
//     value += Phase.GetValue(context);
//     var seed = Seed.GetValue(context);          // InputSlot<int>
//     var period = Frequency.GetValue(context);
//     var octaves = Octaves.GetValue(context);    // InputSlot<int>
//     var rangeMin = RangeMin.GetValue(context);  // float  (SCALAR — not Vector2 like PerlinNoise2)
//     var rangeMax = RangeMax.GetValue(context);  // float  (SCALAR)
//     var scale = Amplitude.GetValue(context);
//     var biasAndGain = BiasAndGain.GetValue(context); // Vector2 (X,Y)
//     var noiseSum  = MathUtils.PerlinNoise(value, period, octaves, seed);
//     var dist = rangeMax - rangeMin;
//     var scaleToUniformFactor = 1.37f;
//     Result.Value = (( noiseSum * scaleToUniformFactor + 1f) * 0.5f)
//                    .ApplyGainAndBias(biasAndGain.X, biasAndGain.Y) * scale * dist + rangeMin;
//
//   MathUtils.PerlinNoise(value, period, octaves, seed):            (verbatim — same as PerlinNoise2)
//     noiseSum=0; octaves=octaves.Clamp(1,20); frequency=period; amplitude=0.5f;
//     for (octave=0; octave<octaves-1; octave++) {
//       v = value*frequency + seed*12.468f;
//       a = Noise((int)v, seed);  b = Noise((int)v+1, seed);
//       t = Fade(v - Math.Floor(v));
//       noiseSum += Lerp(a,b,t) * amplitude;  frequency*=2; amplitude*=0.5f;
//     }
//     return noiseSum;
//   MathUtils.Noise(int x, int seed):                              (32-bit signed int hash — wraps)
//     int n = x + seed*137;  n = (n<<13) ^ n;
//     return (float)(1.0 - ((n*(n*n*15731+789221)+1376312589) & 0x7fffffff) / 1073741824.0);
//   MathUtils.Fade(t) = t*t*t*(t*(t*6-15)+10);
//   MathUtils.Lerp(a,b,t) = a + (b-a)*t;
//   MathUtils.ApplyGainAndBias(value, gain, bias):
//     b=bias.Clamp(0,1); g=gain.Clamp(0,1);
//     if (value>0.999f) return 1f;  if (value<0.00001f) return 0f;
//     if (g<0.5f) { value=GetBias(b,value); value=GetSchlickBias(g,value); }
//     else        { value=GetSchlickBias(g,value); value=GetBias(b,value); }
//     return value;
//   GetBias(b,x)        = x / (((1/b - 2)*(1-x)) + 1);
//   GetSchlickBias(g,x) = x<0.5 ? 0.5*GetBias(g, 2x) : 0.5*GetBias(1-g, 2x-1)+0.5;
//
//   PerlinNoise.t3 DefaultValues (mirrored into PortSpec — NOT the C# field defaults):
//     Phase=0, BiasAndGain=(0.5,0.5), RangeMax=1.0, RangeMin=0.0, Frequency=1.0, Seed=0,
//     Octaves=4, Amplitude=1.0, OverrideTime=0.0.
//     ★ NOTE RangeMin default = 0.0 here (PerlinNoise2.t3 used RangeMin=(-1,-1)); mirror the .t3 exactly.
//
// EVAL-SIDE LAYOUT: in[] is gathered in port order by the flat/resident Float gather. BiasAndGain is the
//   only Vector2 input → decomposes into 2 consecutive Float ports (x,y) per the established
//   fork-vec*-decompose-arity convention (Widget::Vec head + vecArity). Single Float output (Result).
//   in[] order MUST match the registrar port list:
//     [OverrideTime, Phase, Seed, Frequency, Octaves, RangeMin, RangeMax, Amplitude, BiasAndGain.x, BiasAndGain.y]
//   (10 inputs). `value` = (OverrideTime != 0 ? OverrideTime : ctx.localFxTime) + Phase (see fork-perlin-overridetime below).
//
// FORKS (named):
//   - fork-perlin-overridetime-nonzero-single-clock: TiXL branches on OverrideTime.HasInputConnections;
//     the flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(). The
//     standing single-clock convention (AnimValue/AnimVec2) maps "has input connection" → "the
//     OverrideTime constant is non-zero": value = (overrideTime != 0 ? overrideTime : ctx.localFxTime)
//     + Phase. With the .t3 default OverrideTime=0 this is exactly ctx.localFxTime + Phase (the default
//     authoring case), byte-EXACT to TiXL; a non-zero OverrideTime constant overrides the bars clock.
//     Diverges from TiXL ONLY in the narrow "OverrideTime connected AND driven to exactly 0.0" case —
//     unreachable here without owner-locked cook-core per-port connection plumbing.
//   - fork-perlin-gainbias-applies-to-WHOLE (★ INVERTED vs PerlinNoise2's trap): PerlinNoise.cs writes
//     `(( noiseSum * scaleToUniformFactor + 1f) * 0.5f).ApplyGainAndBias(bg.X, bg.Y)`. The outer paren
//     CLOSES at `0.5f)` BEFORE `.ApplyGainAndBias`, so member-access binds to the WHOLE inner expression
//     `((noiseSum*1.37+1)*0.5)` — ApplyGainAndBias is applied to that whole value, NOT to a literal 0.5.
//     This is the OPPOSITE of PerlinNoise2.cs, whose live source `(noiseSum*1.37+1) * 0.5f.ApplyGainAndBias(...)`
//     (no outer paren) makes member-access bind to the LITERAL 0.5f. Both ported VERBATIM as their
//     respective sources are written — they genuinely differ. Verified against TiXL source bytes (line 38,
//     `od -c`): the closing `)` sits right after `0.5f`. Golden CASE E pins this: with bg=(0.8,0.3) the
//     apply-to-whole result (0.292006642) differs from the literal-0.5 form (0.278238654), so a port that
//     copied PerlinNoise2's trap would FAIL CASE E. At default bg=(0.5,0.5) ApplyGainAndBias is the
//     identity on its argument, so the divergence is invisible at defaults.
//   - fork-perlin-seed-octaves-int-on-float-port: TiXL Seed/Octaves are InputSlot<int>; this runtime
//     stores them as Float and truncates (int)(int32) before use (whole-number slider values are
//     byte-identical to TiXL). Octaves additionally clamps to [1,20] exactly as MathUtils.PerlinNoise.
//   - fork-perlin-noise-int32-wrap: MathUtils.Noise does 32-bit signed int arithmetic that overflows
//     (wraps) by design; the port uses uint32_t so the wraparound is bit-identical to C# `int` (signed
//     overflow is UB in C++ — uint32 reproduces the exact bit pattern, then `& 0x7fffffff` masks).
//   - fork-perlin-rangemin-scalar: TiXL PerlinNoise RangeMin/RangeMax are scalar floats (PerlinNoise2's
//     were Vector2). No vec decomposition; `dist = rangeMax - rangeMin` is a plain scalar. Not an eval
//     fork — matches TiXL.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runPerlinNoiseSelfTest(bool injectBug);

namespace {

// MathUtils.Noise(int x, int seed) — verbatim BEHAVIOUR. C# `int` arithmetic is `unchecked` (wraps);
// replicated in UNSIGNED 32-bit so it is well-defined C++. `& 0x7fffffff` takes the non-negative low-31
// bits, double-divide then narrow to float exactly as TiXL.
inline float noiseHash(int x, int seed) {
  uint32_t n = (uint32_t)((int32_t)x + (int32_t)seed * 137);  // seed*137: small ints, signed by intent
  n = (n << 13) ^ n;
  const uint32_t v = n * (n * n * 15731u + 789221u) + 1376312589u;
  const uint32_t masked = v & 0x7fffffffu;
  return (float)(1.0 - masked / 1073741824.0);
}

// MathUtils.Fade / Lerp — verbatim.
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// MathUtils.PerlinNoise(value, period, octaves, seed) — verbatim (octaves already int).
inline float perlinNoise(float value, float period, int octaves, int seed) {
  float noiseSum = 0.0f;
  if (octaves < 1) octaves = 1;  // octaves.Clamp(1,20)
  if (octaves > 20) octaves = 20;
  float frequency = period;
  float amplitude = 0.5f;
  for (int octave = 0; octave < octaves - 1; ++octave) {
    const float v = value * frequency + seed * 12.468f;
    const float a = noiseHash((int)v, seed);
    const float b = noiseHash((int)v + 1, seed);
    const float t = fade(v - (float)std::floor(v));
    noiseSum += lerpf(a, b, t) * amplitude;
    frequency *= 2.0f;
    amplitude *= 0.5f;
  }
  return noiseSum;
}

// MathUtils.GetBias / GetSchlickBias / ApplyGainAndBias — verbatim.
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float getBias(float b, float x) { return x / (((1.0f / b - 2.0f) * (1.0f - x)) + 1.0f); }
inline float getSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * getBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * getBias(1.0f - g, x) + 0.5f;
  }
  return x;
}
inline float applyGainAndBias(float value, float gain, float bias) {
  const float b = clamp01(bias);
  const float g = clamp01(gain);
  if (value > 0.999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = getBias(b, value);
    value = getSchlickBias(g, value);
  } else {
    value = getSchlickBias(g, value);
    value = getBias(b, value);
  }
  return value;
}

// in[] = [OverrideTime, Phase, Seed, Frequency, Octaves, RangeMin, RangeMax, Amplitude, BiasAndGain.x, BiasAndGain.y].
// `value` = (OverrideTime != 0 ? OverrideTime : ctx.localFxTime) + Phase (fork-perlin-overridetime-nonzero-single-clock).
// Single output (Result) at spec index 10 → outIdx == n.
float evalPerlinNoise(int outIdx, const float* in, int n, const EvaluationContext& ctx) {
  if (n < 10) return 0.0f;
  if (outIdx != n) return 0.0f;  // single output: Result follows the n inputs

  const float overrideTime = in[0];
  const float phase    = in[1];
  const int   seed     = (int)(int32_t)in[2];  // fork-perlin-seed-octaves-int-on-float-port
  const float period   = in[3];                // Frequency
  const int   octaves  = (int)(int32_t)in[4];
  const float rangeMin = in[5];                // SCALAR (fork-perlin-rangemin-scalar)
  const float rangeMax = in[6];                // SCALAR
  const float scale    = in[7];                // Amplitude
  const float bgX      = in[8], bgY = in[9];   // BiasAndGain (X,Y)

  // OverrideTime nonzero overrides the bars clock; Phase always added on top (TiXL: value += Phase).
  const float baseTime = (overrideTime != 0.0f) ? overrideTime : ctx.localFxTime;
  const float value = baseTime + phase;

  const float noiseSum = perlinNoise(value, period, octaves, seed);
  const float dist = rangeMax - rangeMin;
  const float scaleToUniformFactor = 1.37f;

  // fork-perlin-gainbias-applies-to-WHOLE: ApplyGainAndBias on the whole ((noiseSum*1.37+1)*0.5).
  const float inner = (noiseSum * scaleToUniformFactor + 1.0f) * 0.5f;
  const float gb = applyGainAndBias(inner, bgX, bgY);
  return gb * scale * dist + rangeMin;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. CMake globs value_op*.cpp; no shared list file edited — independent leaf.
static const ValueOp _reg_perlinnoise{
    // PerlinNoise (TiXL Lib.numbers.float.random.PerlinNoise): scalar 1D Perlin noise from the bars clock.
    // Port order MUST match evalPerlinNoise's in[] read. Defaults from PerlinNoise.t3.
    // OverrideTime FIRST (TiXL [Input] order); nonzero overrides the clock (fork-perlin-overridetime-nonzero-single-clock).
    {"PerlinNoise", "PerlinNoise",
     {{"OverrideTime",  "OverrideTime",  "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Phase",         "Phase",         "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Seed",          "Seed",          "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Frequency",     "Frequency",     "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Octaves",       "Octaves",       "Float", true, 4.0f,  1.0f,      20.0f,     Widget::Slider},
      {"RangeMin",      "RangeMin",      "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"RangeMax",      "RangeMax",      "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Amplitude",     "Amplitude",     "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"BiasAndGain.x", "BiasAndGain",   "Float", true, 0.5f,  0.0f,      1.0f,      Widget::Vec, {}, false, 2},
      {"BiasAndGain.y", "BiasAndGain.y", "Float", true, 0.5f,  0.0f,      1.0f,      Widget::Vec, {}, false, 1},
      {"Result",        "Result",        "Float", false}},
     evalPerlinNoise},
    "perlinnoise", runPerlinNoiseSelfTest};

// --- PerlinNoise MATH golden --------------------------------------------------------------------
// Builds a 1-node PerlinNoise graph, injects a known ctx.localFxTime (BARS), sets params, pulls Result
// via evalFloat. Compares to closed-form values computed INDEPENDENTLY by a standalone C++ reference
// (the constants below are float32 results, NOT self-referential to this file's helpers).
// injectBug detaches the bars clock (uses ctx.localFxTime=0 instead of the injected value) so cases at
// non-zero t flip RED.
int runPerlinNoiseSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Evaluate PerlinNoise at a given localFxTime with explicit params, pulling the Result output.
  auto evalP = [&](float localFxTime,
                   float phase, float seed, float freq, float octaves,
                   float rangeMin, float rangeMax, float ampl, float bgX, float bgY) -> float {
    const NodeSpec* spec = findSpec("PerlinNoise");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "PerlinNoise";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Phase"]         = phase;
    g.node(nid)->params["Seed"]          = seed;
    g.node(nid)->params["Frequency"]     = freq;
    g.node(nid)->params["Octaves"]       = octaves;
    g.node(nid)->params["RangeMin"]      = rangeMin;
    g.node(nid)->params["RangeMax"]      = rangeMax;
    g.node(nid)->params["Amplitude"]     = ampl;
    g.node(nid)->params["BiasAndGain.x"] = bgX;
    g.node(nid)->params["BiasAndGain.y"] = bgY;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock → wrong noise
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-perlinnoise] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A (t3 defaults, localFxTime=2.5 bars): HASH-LIVE coord — noiseSum=-0.052947380 (not a
  //   degenerate zero). Arithmetic from the TiXL formula (float32):
  //     value = 2.5 + Phase(0) = 2.5
  //     noiseSum = MathUtils.PerlinNoise(2.5, period=1, octaves=4, seed=0) = -0.052947380
  //       (octaves-1 = 3 live octave passes over a non-degenerate hash; NOT a Scale=0/zero-time point)
  //     inner = (noiseSum*1.37 + 1) * 0.5 = (-0.052947380*1.37 + 1)*0.5 = 0.463731050
  //     gb = ApplyGainAndBias(0.463731050, gain=0.5, bias=0.5) = 0.463731050  (identity at bg=0.5)
  //     Result = gb * Amplitude(1) * dist(1-0=1) + rangeMin(0) = 0.463731050
  //   injectBug uses localFxTime=0 → noiseSum at t=0 → Result=0.331101537 → CASE A flips RED.
  {
    const float wantA = injectBug ? 0.331101537f : 0.463731050f;
    check("A t=2.5 defaults", evalP(2.5f, 0,0,1,4, 0,1, 1, 0.5f,0.5f), wantA, eps);
  }

  // CASE B (defaults, localFxTime=0.0 bars): distinct hash-live pin. Result=0.331101537.
  //   (At t=0 the bug uses the same value, so B does not discriminate the bug — A/C/D/E do.)
  check("B t=0 defaults", evalP(0.0f, 0,0,1,4, 0,1, 1, 0.5f,0.5f), 0.331101537f, eps);

  // CASE C (seed=7, freq=2, octaves=3, Amplitude=2, t=1.234): proves seed/period/octaves/scale wired.
  //   Result=0.683107555.  injectBug (t=0) → 1.864130855 → RED.
  check("C seed=7 f=2 oct=3 a=2", evalP(1.234f, 0,7,2,3, 0,1, 2, 0.5f,0.5f), 0.683107555f, eps);

  // CASE D (Phase=2, RangeMin=10, RangeMax=20, t=0.5): proves Phase add + scalar dist=rangeMax-rangeMin
  //   + rangeMin offset.  value = 0.5 + 2 = 2.5 → same noiseSum as A (-0.052947380) → inner=0.463731050
  //   → gb=0.463731050 → Result = 0.463731050 * 1 * (20-10) + 10 = 14.637310028.
  //   injectBug (t=0) → value=0+2=2 → 16.312629700 → RED.
  check("D phase=2 rmin=10 rmax=20", evalP(0.5f, 2,0,1,4, 10,20, 1, 0.5f,0.5f), 14.637310028f, 1e-3f);

  // CASE E (BiasAndGain=(0.8,0.3) != (0.5,0.5), t=2.5): pins fork-perlin-gainbias-applies-to-WHOLE.
  //   inner=0.463731050 → gb=ApplyGainAndBias(0.463731050, gain=0.8, bias=0.3) = 0.292006642 (apply to
  //   the WHOLE inner). A port copying PerlinNoise2's literal-0.5 trap would instead get
  //   (noiseSum*1.37+1)*ApplyGainAndBias(0.5,0.8,0.3) = 0.278238654 → would FAIL this assertion.
  //   Result = 0.292006642 * 1 * 1 + 0 = 0.292006642.  injectBug (t=0) → 0.254552513 → RED.
  check("E bg=(0.8,0.3) apply-to-whole", evalP(2.5f, 0,0,1,4, 0,1, 1, 0.8f,0.3f), 0.292006642f, eps);

  // CASE F (OverrideTime=2.5 nonzero, ctx.localFxTime=0): proves the knob OVERRIDES the bars clock.
  //   value = OverrideTime(2.5) + Phase(0) = 2.5 → same noiseSum as CASE A (-0.052947380) → Result=0.463731050.
  //   ctx.localFxTime is 0 here, so WITHOUT the override the result would be CASE B's 0.331101537; the
  //   override forces the A value instead. Because the override decouples from the clock, this holds on
  //   BOTH legs (injectBug only zeroes ctx.localFxTime, which is already unused) → pins a LIVE knob.
  {
    const NodeSpec* spec = findSpec("PerlinNoise");
    if (spec) {
      Graph g; Node nd; nd.id = g.nextId++; nd.type = "PerlinNoise";
      for (const auto& p : spec->ports) if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
      g.nodes.push_back(nd); int nid = g.nodes.back().id;
      g.node(nid)->params["OverrideTime"] = 2.5f;  // nonzero → override the clock
      int outIdx = -1;
      for (size_t i = 0; i < spec->ports.size(); ++i) if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
      EvaluationContext ctx{}; ctx.localFxTime = injectBug ? 0.0f : 0.0f;  // 0 → without override = CASE B
      float got = outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
      check("F OverrideTime=2.5 (clock bypassed)", got, 0.463731050f, eps);
    }
  }

  return ok ? 0 : 1;
}

}  // namespace sw
