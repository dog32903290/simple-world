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
//     [Phase, Seed, Frequency, Octaves,
//      RangeMin.x, RangeMin.y, RangeMin.z, RangeMax.x, RangeMax.y, RangeMax.z,
//      Amplitude, AmplitudeXYZ.x, AmplitudeXYZ.y, AmplitudeXYZ.z,
//      BiasAndGain.x, BiasAndGain.y, Offset.x, Offset.y, Offset.z]   (19 inputs).
//   `value` = ctx.localFxTime + Phase (see fork-perlin3-overridetime below).
//
// FORKS (named):
//   - fork-perlin3-overridetime-always-localfxtime: TiXL branches on OverrideTime.HasInputConnections;
//     the flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(). This
//     port implements the NORMAL case (OverrideTime UNWIRED) verbatim: value = ctx.localFxTime. The
//     OverrideTime input is therefore DROPPED from the port list (it would otherwise be an unread Float
//     that confuses the gather order); wiring an explicit time source is out of scope for this leaf.
//     Byte-EXACT to TiXL whenever OverrideTime is unwired (the default authoring case). Same fork as
//     PerlinNoise2 (fork-perlin2-overridetime-always-localfxtime).
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
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

#include "runtime/Particle.h"             // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (the PRODUCTION gather)
#include "runtime/value_op_registry.h"    // ValueOp self-registration

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
  if (n < 19) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y, 2 = Result.z (outputs follow inputs)
  if (comp < 0 || comp > 2) return 0.0f;

  const float phase   = in[0];
  const int   seed    = (int)(int32_t)in[1];   // fork-perlin3-seed-octaves-int-on-float-port
  const float period  = in[2];                 // Frequency
  const int   octaves = (int)(int32_t)in[3];
  const float rmnX    = in[4],  rmnY = in[5],  rmnZ = in[6];
  const float rmxX    = in[7],  rmxY = in[8],  rmxZ = in[9];
  const float scale   = in[10];                // Amplitude
  const float sxyzX   = in[11], sxyzY = in[12], sxyzZ = in[13];  // AmplitudeXYZ
  const float bgX     = in[14], bgY   = in[15];                  // BiasAndGain (X,Y)
  const float offX    = in[16], offY  = in[17], offZ  = in[18];  // Offset

  const float value = ctx.localFxTime + phase;  // OverrideTime unwired → ctx.localFxTime (bars)

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
    // OverrideTime is intentionally absent (fork-perlin3-overridetime-always-localfxtime).
    {"PerlinNoise3", "PerlinNoise3",
     {{"Phase",          "Phase",          "Float", true, 0.0f,  -100000.0f, 100000.0f, Widget::Slider},
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

// --- PerlinNoise3 MATH golden -------------------------------------------------------------------
// Builds a 1-node PerlinNoise3 graph, injects a known ctx.localFxTime (BARS), sets params, pulls
// Result.x/.y/.z via evalFloat. Compares to closed-form values computed INDEPENDENTLY (standalone
// Python int32 reference, /tmp/perlin3_ref.py in the build dossier — the SAME engine reproduces the
// PerlinNoise2 published goldens bit-faithfully (PN2-A/-C/-E within 1e-5), so the constants below are
// NOT self-referential to this file's helpers). injectBug detaches ctx.localFxTime (uses 0 instead of
// the injected value) so the t3-defaults case flips RED.
//
// HASH-LIVE coordinate (Cut63 rule): primary assertion is CASE C — seed=7, Frequency=2, Octaves=3,
//   localFxTime=1.234 — every octave's `v = value*frequency + seed*12.468f` lands on a live hash cell
//   (NOT a degenerate Scale=0 / Frequency=0 / zero-time point); all three components are far from zero.
//
// ARITHMETIC (CASE A, t3 defaults, localFxTime=2.5, Octaves=3 → loop runs octaves-1 = 2 iterations):
//   value = 2.5 + Phase(0) = 2.5.  seed offsets {0,123,234} for x/y/z.
//   PerlinNoise(2.5, period=1, oct=3, seed): noiseSum = sum over octave∈{0,1} of Lerp(a,b,Fade(frac))*amp:
//     oct0: freq=1 amp=0.5 → v=2.5*1 + seed*12.468; a=Noise(int(v),seed); b=Noise(int(v)+1,seed); ...
//     oct1: freq=2 amp=0.25 → v=2.5*2 + seed*12.468; ...
//   ScalarNoise = (noiseSum*1.37 + 1) * ApplyGainAndBias(0.5, 0.5, 0.5).  bg=(0.5,0.5): g=0.5 → else
//     branch: Schlick(0.5,0.5)=0.5 then GetBias(0.5,0.5)=0.5 → multiplier = 0.5 (identity).
//   Result.c = Remap(ScalarNoise, 0,1, -1,1)*1*1 + 0 = ScalarNoise*2 - 1.
//   Independent reference → x=-0.231699109, y=-0.164338112, z=-0.018129349.
// (z is near zero but the noise term is fully LIVE — only the final remap+offset lands near 0; this is
//  the t3-defaults pin, not the hash-live primary. The hash-live primary is CASE C below.)
int runPerlinNoise3SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Evaluate PerlinNoise3 at a given localFxTime with explicit params, pulling a named output port.
  auto evalP3 = [&](float localFxTime, const char* outName,
                    float phase, float seed, float freq, float octaves,
                    float rmnX, float rmnY, float rmnZ, float rmxX, float rmxY, float rmxZ,
                    float ampl, float sxyzX, float sxyzY, float sxyzZ, float bgX, float bgY,
                    float offX, float offY, float offZ) -> float {
    const NodeSpec* spec = findSpec("PerlinNoise3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "PerlinNoise3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Phase"]          = phase;
    g.node(nid)->params["Seed"]           = seed;
    g.node(nid)->params["Frequency"]      = freq;
    g.node(nid)->params["Octaves"]        = octaves;
    g.node(nid)->params["RangeMin.x"]     = rmnX;
    g.node(nid)->params["RangeMin.y"]     = rmnY;
    g.node(nid)->params["RangeMin.z"]     = rmnZ;
    g.node(nid)->params["RangeMax.x"]     = rmxX;
    g.node(nid)->params["RangeMax.y"]     = rmxY;
    g.node(nid)->params["RangeMax.z"]     = rmxZ;
    g.node(nid)->params["Amplitude"]      = ampl;
    g.node(nid)->params["AmplitudeXYZ.x"] = sxyzX;
    g.node(nid)->params["AmplitudeXYZ.y"] = sxyzY;
    g.node(nid)->params["AmplitudeXYZ.z"] = sxyzZ;
    g.node(nid)->params["BiasAndGain.x"]  = bgX;
    g.node(nid)->params["BiasAndGain.y"]  = bgY;
    g.node(nid)->params["Offset.x"]       = offX;
    g.node(nid)->params["Offset.y"]       = offY;
    g.node(nid)->params["Offset.z"]       = offZ;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock → wrong noise
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // RESIDENT-PATH eval (★ the PRODUCTION gather — the wide value goldens above are FLAT-ONLY, an
  // R-2 gap: value ops flow through the resident engine in the running app, and evalResidentFloat
  // gathers PerlinNoise3's 19 Float inputs into its own in[kMaxFloatIn] array. Mirror of
  // list_routing_golden's cookResidentThenEval: build the SAME single-node graph, libFromGraph →
  // buildEvalGraph (resident path == node id string) → evalResidentFloat the named output slot —
  // the EXACT production evaluation. rc.localFxTime is detached under injectBug (mirror of the flat
  // leg's ctx.localFxTime detach) so the two paths stay in lock-step. The TOOTH that matters for
  // THIS leg is the cap/gather: if a future cap regression truncated the 19-input gather,
  // evalResidentFloat would NaN (the loud over-cap guard) → the NaN-aware assert flips RED
  // (std::fabs(NaN - want) is NaN, never < eps). This proves all 19 inputs gather on the production
  // in[kMaxFloatIn] path, not just the flat in[].
  auto evalP3Resident = [&](float localFxTime, const char* outSlot,
                            float phase, float seed, float freq, float octaves,
                            float rmnX, float rmnY, float rmnZ, float rmxX, float rmxY, float rmxZ,
                            float ampl, float sxyzX, float sxyzY, float sxyzZ, float bgX, float bgY,
                            float offX, float offY, float offZ) -> float {
    const NodeSpec* spec = findSpec("PerlinNoise3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "PerlinNoise3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Phase"]          = phase;
    g.node(nid)->params["Seed"]           = seed;
    g.node(nid)->params["Frequency"]      = freq;
    g.node(nid)->params["Octaves"]        = octaves;
    g.node(nid)->params["RangeMin.x"]     = rmnX;
    g.node(nid)->params["RangeMin.y"]     = rmnY;
    g.node(nid)->params["RangeMin.z"]     = rmnZ;
    g.node(nid)->params["RangeMax.x"]     = rmxX;
    g.node(nid)->params["RangeMax.y"]     = rmxY;
    g.node(nid)->params["RangeMax.z"]     = rmxZ;
    g.node(nid)->params["Amplitude"]      = ampl;
    g.node(nid)->params["AmplitudeXYZ.x"] = sxyzX;
    g.node(nid)->params["AmplitudeXYZ.y"] = sxyzY;
    g.node(nid)->params["AmplitudeXYZ.z"] = sxyzZ;
    g.node(nid)->params["BiasAndGain.x"]  = bgX;
    g.node(nid)->params["BiasAndGain.y"]  = bgY;
    g.node(nid)->params["Offset.x"]       = offX;
    g.node(nid)->params["Offset.y"]       = offY;
    g.node(nid)->params["Offset.z"]       = offZ;
    // PRODUCTION chain (list_routing_golden's cookResidentThenEval shape): flat Graph → SymbolLibrary
    // (child id == node id ⇒ resident path == id-as-string) → resident eval graph → evalResidentFloat.
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime   = 0.0f;
    rc.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock (mirror of flat leg)
    rc.frameIndex  = 0;
    rc.lib         = &lib;
    return evalResidentFloat(rg, std::to_string(nid), outSlot, rc);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-perlinnoise3] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A (t3 defaults, localFxTime=2.5 bars, Octaves=3): proves the bars clock feeds `value` and the
  //   full x/y/z chain (seedOffsets 0/123/234). want from independent reference:
  //   x=-0.231699109, y=-0.164338112, z=-0.018129349.
  //   injectBug uses localFxTime=0 → CASE B values → A assertion flips RED (REAL term flipped: the
  //   bars-clock `value` term that feeds every octave's hash).
  {
    const float wx = injectBug ? -0.289540231f : -0.231699109f;
    const float wy = injectBug ?  0.844502926f : -0.164338112f;
    const float wz = injectBug ? -0.910425782f : -0.018129349f;
    const float fx = evalP3(2.5f, "Result.x", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0);
    const float fy = evalP3(2.5f, "Result.y", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0);
    const float fz = evalP3(2.5f, "Result.z", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0);
    check("A x t=2.5 defaults", fx, wx, eps);
    check("A y t=2.5 defaults", fy, wy, eps);
    check("A z t=2.5 defaults", fz, wz, eps);

    // RESIDENT-PATH leg for CASE A (★ proves the PRODUCTION gather, not just flat — closes the R-2
    // gap). All 19 Float inputs gather on evalResidentFloat's in[kMaxFloatIn] array. Each component
    // must equal BOTH the flat value (fx/fy/fz, same engine) AND the hand-computed TiXL want
    // (wx/wy/wz). The PRIMARY tooth here is the CAP REGRESSION: if a future >cap op (or a lowered
    // cap) truncated the 19-input gather, evalResidentFloat returns NaN (the loud over-cap guard) →
    // std::fabs(NaN - want) is NaN, never < eps → RED on BOTH the ==TiXL and ==flat asserts. (Note:
    // CASE A is deliberately injectBug-NEUTRAL — under injectBug rc.localFxTime detaches to 0 and wx
    // also flips to the CASE B value, so resident == flat == want still holds; the localFxTime teeth
    // live on CASE C/D/E. CASE A's job is the cap/gather correctness proof on the PRODUCTION path.)
    const float rx = evalP3Resident(2.5f, "Result.x", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0);
    const float ry = evalP3Resident(2.5f, "Result.y", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0);
    const float rz = evalP3Resident(2.5f, "Result.z", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0);
    check("A x t=2.5 RESIDENT==TiXL", rx, wx, eps);
    check("A y t=2.5 RESIDENT==TiXL", ry, wy, eps);
    check("A z t=2.5 RESIDENT==TiXL", rz, wz, eps);
    check("A x t=2.5 RESIDENT==flat", rx, fx, eps);
    check("A y t=2.5 RESIDENT==flat", ry, fy, eps);
    check("A z t=2.5 RESIDENT==flat", rz, fz, eps);
  }

  // CASE B (defaults, localFxTime=0.0 bars): distinct seed-time pin (x=-0.289540231, y=0.844502926,
  //   z=-0.910425782). Proves t=0 is a different, well-separated point from t=2.5 (the injectBug target).
  check("B x t=0", evalP3(0.0f, "Result.x", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0), -0.289540231f, eps);
  check("B y t=0", evalP3(0.0f, "Result.y", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0),  0.844502926f, eps);
  check("B z t=0", evalP3(0.0f, "Result.z", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.5f,0.5f, 0,0,0), -0.910425782f, eps);

  // CASE C ★HASH-LIVE PRIMARY (seed=7, freq=2, octaves=3, Amplitude=2, localFxTime=1.234): proves
  //   seed/period/octaves/scale all wired right at a fully-live hash coordinate (all 3 comps far from 0).
  //   x=-0.633785009, y=0.419452906, z=0.754573107. x/y additionally CROSS-CHECK against PerlinNoise2's
  //   published CASE C (same seedOffsets 0/123, same octaves=3): -0.633784890 / 0.419452906 — match.
  check("C x seed=7 f=2 oct=3 a=2 (HASH-LIVE)", evalP3(1.234f, "Result.x", 0,7,2,3, -1,-1,-1,1,1,1, 2, 1,1,1, 0.5f,0.5f, 0,0,0), -0.633785009f, eps);
  check("C y seed=7 f=2 oct=3 a=2 (HASH-LIVE)", evalP3(1.234f, "Result.y", 0,7,2,3, -1,-1,-1,1,1,1, 2, 1,1,1, 0.5f,0.5f, 0,0,0),  0.419452906f, eps);
  check("C z seed=7 f=2 oct=3 a=2 (HASH-LIVE)", evalP3(1.234f, "Result.z", 0,7,2,3, -1,-1,-1,1,1,1, 2, 1,1,1, 0.5f,0.5f, 0,0,0),  0.754573107f, eps);

  // CASE D (Phase=2, AmplitudeXYZ=(0.5,2,3), Offset=(10,20,30), localFxTime=0.5): proves Phase add,
  //   per-axis AmplitudeXYZ, per-axis Offset. value = 0.5 + 2 = 2.5.
  //   x=9.884150505, y=19.671323776, z=29.945611954.
  check("D x phase=2 sxyz=(.5,2,3) off=(10,20,30)", evalP3(0.5f, "Result.x", 2,0,1,3, -1,-1,-1,1,1,1, 1, 0.5f,2,3, 0.5f,0.5f, 10,20,30), 9.884150505f, 1e-3f);
  check("D y phase=2 sxyz=(.5,2,3) off=(10,20,30)", evalP3(0.5f, "Result.y", 2,0,1,3, -1,-1,-1,1,1,1, 1, 0.5f,2,3, 0.5f,0.5f, 10,20,30), 19.671323776f, 1e-3f);
  check("D z phase=2 sxyz=(.5,2,3) off=(10,20,30)", evalP3(0.5f, "Result.z", 2,0,1,3, -1,-1,-1,1,1,1, 1, 0.5f,2,3, 0.5f,0.5f, 10,20,30), 29.945611954f, 1e-3f);

  // CASE E (BiasAndGain=(0.8,0.3) != (0.5,0.5), localFxTime=2.5, Octaves=3): pins
  //   fork-perlin3-gainbias-precedence-trap. ApplyGainAndBias multiplies the LITERAL 0.5 (gain=0.8,
  //   bias=0.3) → multiplier != 0.5. x=-0.539019465, y=-0.498602867, z=-0.410877585. A port that
  //   mis-applies gain/bias to the noise term would NOT reproduce these.
  check("E x bg=(0.8,0.3) precedence", evalP3(2.5f, "Result.x", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.8f,0.3f, 0,0,0), -0.539019465f, eps);
  check("E y bg=(0.8,0.3) precedence", evalP3(2.5f, "Result.y", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.8f,0.3f, 0,0,0), -0.498602867f, eps);
  check("E z bg=(0.8,0.3) precedence", evalP3(2.5f, "Result.z", 0,0,1,3, -1,-1,-1,1,1,1, 1, 1,1,1, 0.8f,0.3f, 0,0,0), -0.410877585f, eps);

  return ok ? 0 : 1;
}

}  // namespace sw
