// PerlinNoise3 value op (value-op self-registration seam leaf — numbers/vec3, unblocked by the
// LocalFxTime seam: eval_context.h's offset-12 slot carries TiXL's LocalFxTime in BARS). 3D twin of
// PerlinNoise2 (value_op_perlinnoise2.cpp): SAME scalar noise math, 3 ScalarNoise calls (seedOffsets
// 0/123/234), Vec3 RangeMin/RangeMax/AmplitudeXYZ/Offset, 3 Float out ports.
// TiXL authority: Operators/Lib/numbers/vec3/PerlinNoise3.cs + Core/Utils/MathUtils.cs (verbatim below).
//
//   PerlinNoise3.cs Update(context):
//     var value = OverrideTime.HasInputConnections ? OverrideTime.GetValue(context)
//                                                   : (float)context.LocalFxTime;
//     value += Phase.GetValue(context);
//     var seed = Seed.GetValue(context);          // InputSlot<int>
//     var period = Frequency.GetValue(context);
//     var octaves = Octaves.GetValue(context);    // InputSlot<int>
//     var rangeMin = RangeMin.GetValue(context);  // Vector3
//     var rangeMax = RangeMax.GetValue(context);  // Vector3
//     var scale = Amplitude.GetValue(context);
//     var scaleXYZ = AmplitudeXYZ.GetValue(context);   // Vector3
//     var biasAndGain = BiasAndGain.GetValue(context); // Vector2 (X,Y)
//     var offset = Offset.GetValue(context);      // Vector3
//     var scaleToUniformFactor = 1.37f;
//     var vec = new Vector3(ScalarNoise(0), ScalarNoise(123), ScalarNoise(234));   // seedOffsets 0/123/234
//     Result.Value = vec.Remap(Vector3.Zero, Vector3.One, rangeMin, rangeMax) * scaleXYZ * scale + offset;
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
//   MathUtils.Remap(Vector3) per-component: factor=(v-inMin)/(inMax-inMin); return factor*(outMax-outMin)+outMin;
//   MathUtils.ApplyGainAndBias(this float value, float gain, float bias):   // <-- gain FIRST, bias SECOND
//     b=bias.Clamp(0,1); g=gain.Clamp(0,1);
//     if (value>0.999f) return 1f;  if (value<0.00001f) return 0f;
//     if (g<0.5f) { value=GetBias(b,value); value=GetSchlickBias(g,value); }
//     else        { value=GetSchlickBias(g,value); value=GetBias(b,value); }
//     return value;
//   GetBias(b,x)        = x / (((1/b - 2)*(1-x)) + 1);
//   GetSchlickBias(g,x) = x<0.5 ? 0.5*GetBias(g, 2x) : 0.5*GetBias(1-g, 2x-1)+0.5;
//
//   PerlinNoise3.t3 DefaultValues: Frequency=1, Seed=0, Octaves=3 (★ NOT 4 like PerlinNoise2),
//     Offset=(0,0,0), RangeMax=(1,1,1), RangeMin=(-1,-1,-1), BiasAndGain=(0.5,0.5),
//     AmplitudeXYZ=(1,1,1), Phase=0, OverrideTime=0, Amplitude=1.
//
// EVAL-SIDE LAYOUT: each Vector3 input decomposes into 3 consecutive Float ports (x,y,z), per the
//   established fork-vec*-decompose-arity convention (PickVector3, BlendVector3). in[] is gathered in
//   port order by the flat/resident Float gather; the 3-output Vector3 dissolves to Result.x/.y/.z
//   (outIdx selects the component). in[] order MUST match the port list in the registrar below:
//     [OverrideTime, Phase, Seed, Frequency, Octaves,
//      RangeMin.x, RangeMin.y, RangeMin.z, RangeMax.x, RangeMax.y, RangeMax.z,
//      Amplitude, AmplitudeXYZ.x, AmplitudeXYZ.y, AmplitudeXYZ.z,
//      BiasAndGain.x, BiasAndGain.y, Offset.x, Offset.y, Offset.z]   (20 inputs; OverrideTime FIRST per TiXL [Input] order).
//   `value` = (OverrideTime != 0 ? OverrideTime : ctx.localFxTime) + Phase (see fork-perlin3-overridetime below).
//
// FORKS (named):
//   - fork-perlin3-overridetime-nonzero-single-clock: TiXL branches on OverrideTime.HasInputConnections;
//     the flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(). The
//     standing single-clock convention (AnimValue/AnimVec2) maps "has input connection" → "the
//     OverrideTime constant is non-zero": value = (overrideTime != 0 ? overrideTime : ctx.localFxTime)
//     + Phase. With the .t3 default OverrideTime=0 this is exactly ctx.localFxTime + Phase (the default
//     authoring case), byte-EXACT to TiXL; a non-zero OverrideTime constant overrides the bars clock.
//     Diverges from TiXL ONLY in the narrow "OverrideTime connected AND driven to exactly 0.0" case —
//     unreachable here without owner-locked cook-core per-port connection plumbing. Same fork as PerlinNoise2.
//   - fork-perlin3-gainbias-precedence-trap: C# member-access binds tighter than `*`, so the source
//     `(... + 1f) * 0.5f.ApplyGainAndBias(biasAndGain.X, biasAndGain.Y)` applies ApplyGainAndBias to the
//     LITERAL 0.5f (gain=BiasAndGain.X, bias=BiasAndGain.Y), NOT to the noise term. Ported VERBATIM as
//     written (live behaviour, NOT "fixed"). With the default BiasAndGain=(0.5,0.5) the multiplier is
//     exactly 0.5 (identity), so the trap is invisible at defaults and bites only when BiasAndGain != 0.5
//     (golden case E pins this). Same fork as PerlinNoise2 (fork-perlin2-gainbias-precedence-trap).
//   - fork-perlin3-seed-octaves-int-on-float-port: TiXL Seed/Octaves are InputSlot<int>; this runtime
//     stores them as Float and truncates (int)(int32) before use (whole-number slider values are
//     byte-identical to TiXL). Octaves additionally clamps to [1,20] exactly as MathUtils.PerlinNoise.
//   - fork-perlin3-noise-int32-wrap: MathUtils.Noise does 32-bit signed int arithmetic that overflows
//     (wraps) by design; the port computes the hash in UNSIGNED 32-bit (well-defined C++; signed
//     overflow is UB) — bit-identical to C# `unchecked int`. Same fork as PerlinNoise2.
//   - fork-perlin3-vec3-as-3-floats: every Vector3 input/output is a triple of Float ports (no Vector3
//     type on this runtime). Not an eval fork — the component mapping is byte-identical to TiXL.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd)

#include <cmath>
#include <cstdint>

#include "runtime/Particle.h"             // EvaluationContext full definition (eval ctx + localFxTime)
#include "runtime/value_op_registry.h"    // ValueOp self-registration
// (the heavy flat+resident self-test body lives in the sibling value_op_perlinnoise3_golden.cpp)

namespace sw {

int runPerlinNoise3SelfTest(bool injectBug);

namespace {

// MathUtils.Noise(int x, int seed) — verbatim BEHAVIOUR. C# `int` arithmetic is `unchecked` (wraps);
// the same wrap is replicated here in UNSIGNED 32-bit so it is well-defined C++ (signed overflow is UB).
// uint32 reproduces C#'s exact bit pattern; `& 0x7fffffff` takes the same non-negative low-31 bits.
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
// ApplyGainAndBias(this float value, float gain, float bias) — gain FIRST, bias SECOND.
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

// MathUtils.Remap(Vector3) per-component (verbatim: factor=(v-inMin)/(inMax-inMin); factor*(outMax-outMin)+outMin).
inline float remap1(float v, float inMin, float inMax, float outMin, float outMax) {
  const float factor = (v - inMin) / (inMax - inMin);
  return factor * (outMax - outMin) + outMin;
}

// PerlinNoise3.cs ScalarNoise(seedOffset) — VERBATIM with the precedence trap
// (fork-perlin3-gainbias-precedence-trap): the ApplyGainAndBias multiplies the LITERAL 0.5f.
inline float scalarNoise(float value, float period, int octaves, int seed, int seedOffset,
                         float bgX, float bgY) {
  const float scaleToUniformFactor = 1.37f;
  const float pn = perlinNoise(value, period, octaves, seed + seedOffset);
  const float halfGainBias = applyGainAndBias(0.5f, bgX, bgY);  // 0.5f.ApplyGainAndBias(bg.X, bg.Y)
  return (pn * scaleToUniformFactor + 1.0f) * halfGainBias;
}

// in[] = [Phase, Seed, Frequency, Octaves,
//         RangeMin.x, RangeMin.y, RangeMin.z, RangeMax.x, RangeMax.y, RangeMax.z,
//         Amplitude, AmplitudeXYZ.x, AmplitudeXYZ.y, AmplitudeXYZ.z,
//         BiasAndGain.x, BiasAndGain.y, Offset.x, Offset.y, Offset.z]   (19 inputs).
// `value` = ctx.localFxTime + Phase (fork-perlin3-overridetime-always-localfxtime).
// Outputs Result.x/.y/.z follow the n inputs → component = outIdx - n ∈ {0,1,2}.
float evalPerlinNoise3(int outIdx, const float* in, int n, const EvaluationContext& ctx) {
  if (n < 20) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y, 2 = Result.z (outputs follow inputs)
  if (comp < 0 || comp > 2) return 0.0f;

  const float overrideTime = in[0];
  const float phase   = in[1];
  const int   seed    = (int)(int32_t)in[2];   // fork-perlin3-seed-octaves-int-on-float-port
  const float period  = in[3];                 // Frequency
  const int   octaves = (int)(int32_t)in[4];
  const float rmnX    = in[5],  rmnY = in[6],  rmnZ = in[7];
  const float rmxX    = in[8],  rmxY = in[9],  rmxZ = in[10];
  const float scale   = in[11];                // Amplitude
  const float sxyzX   = in[12], sxyzY = in[13], sxyzZ = in[14];  // AmplitudeXYZ
  const float bgX     = in[15], bgY   = in[16];                  // BiasAndGain (X,Y)
  const float offX    = in[17], offY  = in[18], offZ  = in[19];  // Offset

  // OverrideTime nonzero overrides the bars clock; Phase always added on top (TiXL: value += Phase).
  const float baseTime = (overrideTime != 0.0f) ? overrideTime : ctx.localFxTime;
  const float value = baseTime + phase;

  // vec = (ScalarNoise(0), ScalarNoise(123), ScalarNoise(234)).
  // vec.Remap(Zero, One, rangeMin, rangeMax) * scaleXYZ * scale + offset  (per component).
  if (comp == 0) {
    const float nx = scalarNoise(value, period, octaves, seed, 0, bgX, bgY);
    return remap1(nx, 0.0f, 1.0f, rmnX, rmxX) * sxyzX * scale + offX;
  }
  if (comp == 1) {
    const float ny = scalarNoise(value, period, octaves, seed, 123, bgX, bgY);
    return remap1(ny, 0.0f, 1.0f, rmnY, rmxY) * sxyzY * scale + offY;
  }
  const float nz = scalarNoise(value, period, octaves, seed, 234, bgX, bgY);
  return remap1(nz, 0.0f, 1.0f, rmnZ, rmxZ) * sxyzZ * scale + offZ;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. CMake globs value_op*.cpp; no shared list file edited — independent leaf.
static const ValueOp _reg_perlinnoise3{
    // PerlinNoise3 (TiXL Lib.numbers.vec3.PerlinNoise3): 3D Perlin noise vector from the bars clock.
    // Port order MUST match evalPerlinNoise3's in[] read. Defaults from PerlinNoise3.t3 (Octaves=3).
    // OverrideTime FIRST (TiXL [Input] order); nonzero overrides the clock (fork-perlin3-overridetime-nonzero-single-clock).
    {"PerlinNoise3", "PerlinNoise3",
     {{"OverrideTime",   "OverrideTime",   "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"Phase",          "Phase",          "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"Seed",           "Seed",           "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"Frequency",      "Frequency",      "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"Octaves",        "Octaves",        "Float", true, 3.0f,  1.0f,       20.0f,     Widget::Slider},
      {"RangeMin.x",     "RangeMin",       "Float", true, -1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"RangeMin.y",     "RangeMin.y",     "Float", true, -1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"RangeMin.z",     "RangeMin.z",     "Float", true, -1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"RangeMax.x",     "RangeMax",       "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"RangeMax.y",     "RangeMax.y",     "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"RangeMax.z",     "RangeMax.z",     "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Amplitude",      "Amplitude",      "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Slider},
      {"AmplitudeXYZ.x", "AmplitudeXYZ",   "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"AmplitudeXYZ.y", "AmplitudeXYZ.y", "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"AmplitudeXYZ.z", "AmplitudeXYZ.z", "Float", true, 1.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"BiasAndGain.x",  "BiasAndGain",    "Float", true, 0.5f,  0.0f,       1.0f,      Widget::Vec, {}, false, 2},
      {"BiasAndGain.y",  "BiasAndGain.y",  "Float", true, 0.5f,  0.0f,       1.0f,      Widget::Vec, {}, false, 1},
      {"Offset.x",       "Offset",         "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Offset.y",       "Offset.y",       "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Offset.z",       "Offset.z",       "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Result.x",       "Result.x",       "Float", false},
      {"Result.y",       "Result.y",       "Float", false},
      {"Result.z",       "Result.z",       "Float", false}},
     evalPerlinNoise3},
    "perlinnoise3", runPerlinNoise3SelfTest};


}  // namespace sw
