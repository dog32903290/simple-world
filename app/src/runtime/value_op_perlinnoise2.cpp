// PerlinNoise2 value op (value-op self-registration seam leaf — numbers/vec2, unblocked by the
// LocalFxTime seam: eval_context.h's offset-12 slot now carries TiXL's LocalFxTime in BARS).
// TiXL authority: Operators/Lib/numbers/vec2/PerlinNoise2.cs + Core/Utils/MathUtils.cs (verbatim below).
//
//   PerlinNoise2.cs Update(context):
//     var value = OverrideTime.HasInputConnections ? OverrideTime.GetValue(context)
//                                                   : (float)context.LocalFxTime;
//     value += Phase.GetValue(context);
//     var seed = Seed.GetValue(context);          // InputSlot<int>
//     var period = Frequency.GetValue(context);
//     var octaves = Octaves.GetValue(context);    // InputSlot<int>
//     var rangeMin = RangeMin.GetValue(context);  // Vector2
//     var rangeMax = RangeMax.GetValue(context);  // Vector2
//     var scale = Amplitude.GetValue(context);
//     var scaleXY = AmplitudeXY.GetValue(context);// Vector2
//     var biasAndGain = BiasAndGain.GetValue(context); // Vector2 (X,Y)
//     var offset = Offset.GetValue(context);      // Vector2
//     var scaleToUniformFactor = 1.37f;
//     var vec = new Vector2(ScalarNoise(0), ScalarNoise(123));
//     Result.Value = vec.Remap(Vector2.Zero, Vector2.One, rangeMin, rangeMax) * scaleXY * scale + offset;
//
//     float ScalarNoise(int seedOffset) {
//       return (MathUtils.PerlinNoise(value, period, octaves, seed + seedOffset) * scaleToUniformFactor + 1f)
//              * 0.5f.ApplyGainAndBias(biasAndGain.X, biasAndGain.Y);   // <-- precedence! see fork below
//     }
//
//   MathUtils.PerlinNoise(value, period, octaves, seed):           (verbatim — DO NOT recompute)
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
//   MathUtils.Remap(Vector2): factor=(v-inMin)/(inMax-inMin); return factor*(outMax-outMin)+outMin;
//   MathUtils.ApplyGainAndBias(value, gain, bias):
//     b=bias.Clamp(0,1); g=gain.Clamp(0,1);
//     if (value>0.999f) return 1f;  if (value<0.00001f) return 0f;
//     if (g<0.5f) { value=GetBias(b,value); value=GetSchlickBias(g,value); }
//     else        { value=GetSchlickBias(g,value); value=GetBias(b,value); }
//     return value;
//   GetBias(b,x)        = x / (((1/b - 2)*(1-x)) + 1);
//   GetSchlickBias(g,x) = x<0.5 ? 0.5*GetBias(g, 2x) : 0.5*GetBias(1-g, 2x-1)+0.5;
//
//   PerlinNoise2.t3 DefaultValues: Amplitude=1, Frequency=1, AmplitudeXY=(1,1), Phase=0, Seed=0,
//     Offset=(0,0), BiasAndGain=(0.5,0.5), Octaves=4, RangeMin=(-1,-1), RangeMax=(1,1), OverrideTime=0.
//
// EVAL-SIDE LAYOUT: each Vector2 input decomposes into 2 consecutive Float ports (x,y), per the
//   established fork-vec*-decompose-arity convention (Int2ToVector2, PickVector2). in[] is gathered
//   in port order by the flat/resident Float gather; the 2-output Vector2 dissolves to Result.x/.y
//   (outIdx selects the component). in[] order MUST match the port list in the registrar below:
//     [Phase, Seed, Frequency, Octaves, RangeMin.x, RangeMin.y, RangeMax.x, RangeMax.y,
//      Amplitude, AmplitudeXY.x, AmplitudeXY.y, BiasAndGain.x, BiasAndGain.y, Offset.x, Offset.y]
//   (15 inputs). `value` = ctx.localFxTime + Phase (see fork-perlin2-overridetime below).
//
// FORKS (named):
//   - fork-perlin2-overridetime-always-localfxtime: TiXL branches on OverrideTime.HasInputConnections;
//     the flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(). This
//     port implements the NORMAL case (OverrideTime UNWIRED) verbatim: value = ctx.localFxTime. The
//     OverrideTime input is therefore DROPPED from the port list (it would otherwise be an unread Float
//     that confuses the gather order); wiring an explicit time source is out of scope for this leaf.
//     Byte-EXACT to TiXL whenever OverrideTime is unwired (the default authoring case).
//   - fork-perlin2-gainbias-precedence-trap: C# member-access binds tighter than `*`, so the source
//     `(... + 1f) * 0.5f.ApplyGainAndBias(biasAndGain.X, biasAndGain.Y)` applies ApplyGainAndBias to the
//     LITERAL 0.5f (gain=BiasAndGain.X, bias=BiasAndGain.Y), NOT to the noise term. The commented-out
//     reference block in PerlinNoise2.cs (lines 36-40) instead applies it to the whole `(...)*0.5`, but
//     the LIVE code is the precedence-bound form. Ported VERBATIM as written (live behaviour). With the
//     default BiasAndGain=(0.5,0.5) the multiplier is exactly 0.5 (identity), so the trap is invisible
//     at defaults and bites only when BiasAndGain != 0.5 (golden case E pins this).
//   - fork-perlin2-seed-octaves-int-on-float-port: TiXL Seed/Octaves are InputSlot<int>; this runtime
//     stores them as Float and truncates (int)(int32) before use (whole-number slider values are
//     byte-identical to TiXL). Octaves additionally clamps to [1,20] exactly as MathUtils.PerlinNoise.
//   - fork-perlin2-noise-int32-wrap: MathUtils.Noise does 32-bit signed int arithmetic that overflows
//     (wraps) by design; the port uses int32_t so the wraparound is bit-identical to C# `int`.
//   - fork-perlin2-vec2-as-2-floats: every Vector2 input/output is a pair of Float ports (no Vector2
//     type on this runtime). Not an eval fork — the component mapping is byte-identical to TiXL.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runPerlinNoise2SelfTest(bool injectBug);

namespace {

// MathUtils.Noise(int x, int seed) — verbatim BEHAVIOUR. C# `int` arithmetic is `unchecked` (wraps);
// the same wrap is replicated here in UNSIGNED 32-bit so it is well-defined C++ (signed overflow is UB,
// which UBSan trips on — see fork-perlin2-noise-int32-wrap). uint32 reproduces C#'s exact bit pattern
// (verified bit-identical to the signed form across a ±200 seed × ±2000 x sweep) and `& 0x7fffffff`
// takes the same non-negative low-31 bits.
inline float noiseHash(int x, int seed) {
  uint32_t n = (uint32_t)((int32_t)x + (int32_t)seed * 137);  // seed*137: small ints, signed by intent
  n = (n << 13) ^ n;
  const uint32_t v = n * (n * n * 15731u + 789221u) + 1376312589u;
  const uint32_t masked = v & 0x7fffffffu;
  return (float)(1.0 - masked / 1073741824.0);  // double divide then narrow to float, as TiXL
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

// MathUtils.Remap(Vector2) per-component.
inline float remap1(float v, float inMin, float inMax, float outMin, float outMax) {
  const float factor = (v - inMin) / (inMax - inMin);
  return factor * (outMax - outMin) + outMin;
}

// PerlinNoise2.cs ScalarNoise(seedOffset) — VERBATIM with the precedence trap
// (fork-perlin2-gainbias-precedence-trap): the ApplyGainAndBias multiplies the LITERAL 0.5f.
inline float scalarNoise(float value, float period, int octaves, int seed, int seedOffset,
                         float bgX, float bgY) {
  const float scaleToUniformFactor = 1.37f;
  const float pn = perlinNoise(value, period, octaves, seed + seedOffset);
  const float halfGainBias = applyGainAndBias(0.5f, bgX, bgY);  // 0.5f.ApplyGainAndBias(bg.X, bg.Y)
  return (pn * scaleToUniformFactor + 1.0f) * halfGainBias;
}

// in[] = [Phase, Seed, Frequency, Octaves, RangeMin.x, RangeMin.y, RangeMax.x, RangeMax.y,
//         Amplitude, AmplitudeXY.x, AmplitudeXY.y, BiasAndGain.x, BiasAndGain.y, Offset.x, Offset.y].
// `value` = ctx.localFxTime + Phase (fork-perlin2-overridetime-always-localfxtime).
// Output Result.x at spec index 15, Result.y at 16 → component = outIdx - 15.
float evalPerlinNoise2(int outIdx, const float* in, int n, const EvaluationContext& ctx) {
  if (n < 15) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y (outputs follow the n inputs)
  if (comp < 0 || comp > 1) return 0.0f;

  const float phase   = in[0];
  const int   seed    = (int)(int32_t)in[1];   // fork-perlin2-seed-octaves-int-on-float-port
  const float period  = in[2];                 // Frequency
  const int   octaves = (int)(int32_t)in[3];
  const float rmnX    = in[4],  rmnY = in[5];
  const float rmxX    = in[6],  rmxY = in[7];
  const float scale   = in[8];                 // Amplitude
  const float sxyX    = in[9],  sxyY = in[10]; // AmplitudeXY
  const float bgX     = in[11], bgY  = in[12]; // BiasAndGain (X,Y)
  const float offX    = in[13], offY = in[14]; // Offset

  const float value = ctx.localFxTime + phase;  // OverrideTime unwired → ctx.localFxTime (bars)

  const float nx = scalarNoise(value, period, octaves, seed, 0,   bgX, bgY);
  const float ny = scalarNoise(value, period, octaves, seed, 123, bgX, bgY);

  // vec.Remap(Zero, One, rangeMin, rangeMax) * scaleXY * scale + offset  (per component).
  if (comp == 0) return remap1(nx, 0.0f, 1.0f, rmnX, rmxX) * sxyX * scale + offX;
  return remap1(ny, 0.0f, 1.0f, rmnY, rmxY) * sxyY * scale + offY;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. CMake globs value_op*.cpp; no shared list file edited — independent leaf.
static const ValueOp _reg_perlinnoise2{
    // PerlinNoise2 (TiXL Lib.numbers.vec2.PerlinNoise2): 2D Perlin noise vector from the bars clock.
    // Port order MUST match evalPerlinNoise2's in[] read. Defaults from PerlinNoise2.t3.
    // OverrideTime is intentionally absent (fork-perlin2-overridetime-always-localfxtime).
    {"PerlinNoise2", "PerlinNoise2",
     {{"Phase",         "Phase",         "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"Seed",          "Seed",          "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"Frequency",     "Frequency",     "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"Octaves",       "Octaves",       "Float", true, 4.0f,  1.0f,       20.0f,     Widget::Slider},
      {"RangeMin.x",    "RangeMin",      "Float", true, -1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"RangeMin.y",    "RangeMin.y",    "Float", true, -1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"RangeMax.x",    "RangeMax",      "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"RangeMax.y",    "RangeMax.y",    "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Amplitude",     "Amplitude",     "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"AmplitudeXY.x", "AmplitudeXY",   "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"AmplitudeXY.y", "AmplitudeXY.y", "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"BiasAndGain.x", "BiasAndGain",   "Float", true, 0.5f,  0.0f,       1.0f,      Widget::Vec, {}, false, 2},
      {"BiasAndGain.y", "BiasAndGain.y", "Float", true, 0.5f,  0.0f,       1.0f,      Widget::Vec, {}, false, 1},
      {"Offset.x",      "Offset",        "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Offset.y",      "Offset.y",      "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Result.x",      "Result.x",      "Float", false},
      {"Result.y",      "Result.y",      "Float", false}},
     evalPerlinNoise2},
    "perlinnoise2", runPerlinNoise2SelfTest};

// --- PerlinNoise2 MATH golden -------------------------------------------------------------------
// Builds a 1-node PerlinNoise2 graph, injects a known ctx.localFxTime (BARS), sets params, pulls
// Result.x/Result.y via evalFloat. Compares to the closed-form computed INDEPENDENTLY (Python numpy
// int32 reference + a standalone C++ reference — both agree within 1e-5, see the build agent dossier;
// the constants below are the float32 results, NOT self-referential to this file's helpers).
// injectBug flips ctx.localFxTime detachment (uses 0 instead of the injected value) so the typical
// case flips RED.
int runPerlinNoise2SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Evaluate PerlinNoise2 at a given localFxTime with explicit params, pulling a named output port.
  auto evalP2 = [&](float localFxTime, const char* outName,
                    float phase, float seed, float freq, float octaves,
                    float rmnX, float rmnY, float rmxX, float rmxY,
                    float ampl, float sxyX, float sxyY, float bgX, float bgY,
                    float offX, float offY) -> float {
    const NodeSpec* spec = findSpec("PerlinNoise2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "PerlinNoise2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Phase"]         = phase;
    g.node(nid)->params["Seed"]          = seed;
    g.node(nid)->params["Frequency"]     = freq;
    g.node(nid)->params["Octaves"]       = octaves;
    g.node(nid)->params["RangeMin.x"]    = rmnX;
    g.node(nid)->params["RangeMin.y"]    = rmnY;
    g.node(nid)->params["RangeMax.x"]    = rmxX;
    g.node(nid)->params["RangeMax.y"]    = rmxY;
    g.node(nid)->params["Amplitude"]     = ampl;
    g.node(nid)->params["AmplitudeXY.x"] = sxyX;
    g.node(nid)->params["AmplitudeXY.y"] = sxyY;
    g.node(nid)->params["BiasAndGain.x"] = bgX;
    g.node(nid)->params["BiasAndGain.y"] = bgY;
    g.node(nid)->params["Offset.x"]      = offX;
    g.node(nid)->params["Offset.y"]      = offY;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock → wrong noise
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-perlinnoise2] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A (t3 defaults, localFxTime=2.5 bars): proves the bars clock feeds `value` and the full chain.
  //   want from independent reference: x=-0.072537899, y=-0.142278671.
  //   injectBug uses localFxTime=0 → CASE B values → A assertion flips RED.
  {
    const float wx = injectBug ? -0.337796926f : -0.072537899f;
    const float wy = injectBug ?  0.985253453f : -0.142278671f;
    check("A x t=2.5 defaults", evalP2(2.5f, "Result.x", 0,0,1,4, -1,-1,1,1, 1, 1,1, 0.5f,0.5f, 0,0), wx, eps);
    check("A y t=2.5 defaults", evalP2(2.5f, "Result.y", 0,0,1,4, -1,-1,1,1, 1, 1,1, 0.5f,0.5f, 0,0), wy, eps);
  }

  // CASE B (defaults, localFxTime=0.0 bars): distinct seed-time pin (x=-0.337796926, y=0.985253453).
  check("B x t=0", evalP2(0.0f, "Result.x", 0,0,1,4, -1,-1,1,1, 1, 1,1, 0.5f,0.5f, 0,0), -0.337796926f, eps);
  check("B y t=0", evalP2(0.0f, "Result.y", 0,0,1,4, -1,-1,1,1, 1, 1,1, 0.5f,0.5f, 0,0),  0.985253453f, eps);

  // CASE C (seed=7, freq=2, octaves=3, Amplitude=2): proves seed/period/octaves/scale all wired right.
  //   x=-0.633784890, y=0.419452906.
  check("C x seed=7 f=2 oct=3 a=2", evalP2(1.234f, "Result.x", 0,7,2,3, -1,-1,1,1, 2, 1,1, 0.5f,0.5f, 0,0), -0.633784890f, eps);
  check("C y seed=7 f=2 oct=3 a=2", evalP2(1.234f, "Result.y", 0,7,2,3, -1,-1,1,1, 2, 1,1, 0.5f,0.5f, 0,0),  0.419452906f, eps);

  // CASE D (Phase=2, AmplitudeXY=(0.5,2), Offset=(10,20)): proves Phase add, per-axis AmplitudeXY, Offset.
  //   x=9.963730812, y=19.715442657.
  check("D x phase=2 sxy=(0.5,2) off=(10,20)", evalP2(0.5f, "Result.x", 2,0,1,4, -1,-1,1,1, 1, 0.5f,2,0.5f,0.5f, 10,20), 9.963730812f, 1e-3f);
  check("D y phase=2 sxy=(0.5,2) off=(10,20)", evalP2(0.5f, "Result.y", 2,0,1,4, -1,-1,1,1, 1, 0.5f,2,0.5f,0.5f, 10,20), 19.715442657f, 1e-3f);

  // CASE E (BiasAndGain=(0.8,0.3) != (0.5,0.5)): pins fork-perlin2-gainbias-precedence-trap. The
  //   ApplyGainAndBias multiplies the literal 0.5 → multiplier becomes 0.3 (not 0.5). x=-0.443522692,
  //   y=-0.485367179. A port that mis-applies gain/bias to the noise term (the commented-out reference)
  //   would NOT reproduce these.
  check("E x bg=(0.8,0.3) precedence", evalP2(2.5f, "Result.x", 0,0,1,4, -1,-1,1,1, 1, 1,1, 0.8f,0.3f, 0,0), -0.443522692f, eps);
  check("E y bg=(0.8,0.3) precedence", evalP2(2.5f, "Result.y", 0,0,1,4, -1,-1,1,1, 1, 1,1, 0.8f,0.3f, 0,0), -0.485367179f, eps);

  return ok ? 0 : 1;
}

}  // namespace sw
