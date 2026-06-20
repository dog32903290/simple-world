// Random value op (value-op self-registration seam leaf — REPLACES PerlinNoise2, which is BLOCKED).
// TiXL authority: Operators/Lib/numbers/float/random/Random.cs + Core/Utils/MathUtils.cs (verbatim below).
//
//   Random.cs Update():
//     var seed = (uint)Seed.GetValue(context);                    // Seed is InputSlot<int>
//     var makeUniqueForChild = UniqueForChild.GetValue(context);
//     var childId = SymbolChildId;
//     var childSeed = makeUniqueForChild ? (uint)(new BigInteger(childId.ToByteArray()) & 0xFFFFFFFF) : 0;
//     var randomValue = MathUtils.Hash01(childSeed + seed);
//     Result.Value = MathUtils.RemapAndClamp(randomValue, 0f, 1f, Min, Max);
//
//   MathUtils.Hash01(uint x):                                     (deterministic GLIBC-style hash → [0,1))
//     x *= 13331u;  const uint k = 1103515245u;
//     x = ((x>>8)^x)*k;  x = ((x>>8)^x)*k;
//     return (float)((x & 0x7fffffff) / 2147483648.0);
//   MathUtils.RemapAndClamp(value, inMin, inMax, outMin, outMax):
//     var factor = (value - inMin) / (inMax - inMin);
//     var v = factor * (outMax - outMin) + outMin;
//     if (outMin > outMax) Swap(ref outMin, ref outMax);          // clamp bounds always ordered low..high
//     return v.Clamp(outMin, outMax);
//
//   Random.t3 DefaultValues: Min=0.0, Max=1.0, UniqueForChild=true, Seed=3.
//
// WHY this replaces PerlinNoise2 (the work-order's #4): PerlinNoise2.cs sources its noise input from
// `value = OverrideTime.HasInputConnections ? OverrideTime : (float)context.LocalFxTime` — when
// OverrideTime is unwired (the normal case) it reads context.LocalFxTime. The sw EvaluationContext
// (eval_context.h) is a FROZEN 16-byte GPU struct {frameIndex,time,deltaTime,_pad} with NO LocalFxTime
// (only `time`); it is also a large time-DRIVEN op (Remap/RangeMin/Max/AmplitudeXY/Offset/BiasAndGain).
// It is not a clean STATIC pure-value leaf. Per the work order ("已港就回報換 numbers/ 另一顆未港純值 R1"),
// swapped for Random — a genuinely stateless pure-value op with NO context-time dependency (a fixed seed
// gives a fixed value every frame). UNPORTED in both seam tables (grep verified).
//
// EVAL-SIDE LAYOUT: inputs [Min, Max, UniqueForChild, Seed] → ONE Float output. Pure stateless: the
// evaluate fn IS the behaviour (no GPU cook). in[] = [Min, Max, UniqueForChild, Seed] (spec port order).
//
// FORKS (named):
//   - fork-random-uniqueforchild-no-symbolchildid: same as FloatHash — TiXL's UniqueForChild=true path
//     adds a per-INSTANCE childSeed from the node's GUID; the flat value-eval path has no SymbolChildId.
//     This port implements childSeed=0 ALWAYS (byte-EXACT to TiXL for UniqueForChild=false). The
//     UniqueForChild input exists for port-shape parity but is a no-op here (instance-identity path only).
//   - fork-random-seed-int-on-float-port: TiXL's Seed is InputSlot<int>, cast `(uint)Seed`. sw stores it
//     as Float; the eval truncates `(uint)(int)Seed` before hashing (whole-number seeds — the only thing
//     an int slider produces — are byte-identical to TiXL).
//   - fork-random-remap-bounds-swap: RemapAndClamp swaps outMin/outMax for the CLAMP step when
//     outMin>outMax (so the result is clamped into the ordered [low,high] interval) but computes `v`
//     using the ORIGINAL (unswapped) outMin/outMax — i.e. an inverted Min>Max still LERPs in the
//     original direction, only the clamp bounds are reordered. Ported verbatim (tested in golden G5).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runRandomSelfTest(bool injectBug);

namespace {

// MathUtils.Hash01(uint x) → [0,1), ported verbatim.
inline float hash01(uint32_t x) {
  x *= 13331u;
  const uint32_t k = 1103515245u;  // GLIBC LCG constant
  x = ((x >> 8) ^ x) * k;
  x = ((x >> 8) ^ x) * k;
  return (float)((x & 0x7fffffffu) / 2147483648.0);  // double divide then narrow to float, as TiXL
}

// MathUtils.RemapAndClamp, ported verbatim (fork-random-remap-bounds-swap: v uses original bounds,
// clamp uses ordered bounds).
inline float remapAndClamp(float value, float inMin, float inMax, float outMin, float outMax) {
  const float factor = (value - inMin) / (inMax - inMin);
  const float v = factor * (outMax - outMin) + outMin;  // ORIGINAL (unswapped) bounds
  float lo = outMin, hi = outMax;
  if (lo > hi) { const float t = lo; lo = hi; hi = t; }  // swap for the clamp only
  return v < lo ? lo : (v > hi ? hi : v);
}

// in[] = [Min, Max, UniqueForChild, Seed]. childSeed=0 (fork-random-uniqueforchild-no-symbolchildid).
float evalRandom(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  const float mn  = in[0];
  const float mx  = in[1];
  // in[2] = UniqueForChild — no SymbolChildId on this runtime, childSeed stays 0.
  const uint32_t seed = (uint32_t)(int32_t)in[3];  // (uint)Seed (int input on a Float port)
  const float randomValue = hash01(0u + seed);     // childSeed=0
  return remapAndClamp(randomValue, 0.0f, 1.0f, mn, mx);
}

}  // namespace

// Self-registration. File-scope static ValueOp — CMake globs value_op*.cpp; no shared edit point.
static const ValueOp _reg_random{
    // Random (TiXL Lib.numbers.float.random.Random): Hash01(seed) remapped+clamped to [Min,Max].
    // Port order MUST match evalRandom's in[]: Min, Max, UniqueForChild, Seed (inputs), then Result.
    // Defaults from Random.t3: Min=0, Max=1, UniqueForChild=true (no-op here), Seed=3.
    {"Random", "Random",
     {{"Min",            "Min",            "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Max",            "Max",            "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"UniqueForChild", "UniqueForChild", "Float", true, 1.0f, 0.0f,       1.0f,      Widget::Bool},
      {"Seed",           "Seed",           "Float", true, 3.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Result",         "Result",         "Float", false}},
     evalRandom},
    "random", runRandomSelfTest};

// --- Random MATH golden --------------------------------------------------------------------------
// Builds a 1-node Random graph, sets Min/Max/Seed (+UniqueForChild), pulls Result via evalFloat, compares
// to the hand-computed Hash01+RemapAndClamp (Python closed-form against the .cs, NOT self-referential).
// injectBug asserts a WRONG value (the seed=0 result on the seed=3 default) so the typical case flips RED.
int runRandomSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalR = [&](float mn, float mx, float ufc, float seed) -> float {
    const NodeSpec* spec = findSpec("Random");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Random";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Min"]            = mn;
    g.node(nid)->params["Max"]            = mx;
    g.node(nid)->params["UniqueForChild"] = ufc;
    g.node(nid)->params["Seed"]           = seed;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-random] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // GOLDEN 1 (t3 defaults: Min=0, Max=1, Seed=3, UFC=false): Hash01(3) = 0.695385786 → remap [0,1]→[0,1]
  //   identity → 0.695385786. injectBug asserts the seed=0 result (Hash01(0)=0) → RED.
  {
    float got = evalR(0.0f, 1.0f, 0.0f, 3.0f);
    float want = injectBug ? 0.0f : 0.695385786f;  // bug: wrong seed (0) → 0
    check("G1 seed=3 [0,1]", got, want, eps);
  }

  // GOLDEN 2: seed=0 → Hash01(0) = 0.0 (the all-zero seed degenerate, proves the hash bottoms at 0).
  check("G2 seed=0 [0,1]", evalR(0.0f, 1.0f, 0.0f, 0.0f), 0.0f, eps);

  // GOLDEN 3: seed=7 → Hash01(7) = 0.036417314. (Distinct seed → distinct value; the hash actually
  //   depends on the seed bits.)
  check("G3 seed=7 [0,1]", evalR(0.0f, 1.0f, 0.0f, 7.0f), 0.036417314f, eps);

  // GOLDEN 4 (remap to [5,10]): seed=3 → 0.695385786 * (10-5) + 5 = 8.476928931. Proves RemapAndClamp's
  //   factor·range + offset.
  check("G4 seed=3 [5,10]", evalR(5.0f, 10.0f, 0.0f, 3.0f), 8.476928931f, 1e-4f);

  // GOLDEN 5 (fork-random-remap-bounds-swap, inverted Min>Max): seed=3, Min=1, Max=0.
  //   v = 0.695385786 * (0-1) + 1 = 0.304614214 (LERPs in the ORIGINAL direction). Clamp bounds reorder
  //   to [0,1] → 0.304614214 sits inside → unchanged. Proves the v-uses-original-bounds fork.
  check("G5 seed=3 inverted [1,0]", evalR(1.0f, 0.0f, 0.0f, 3.0f), 0.304614214f, eps);

  // GOLDEN 6 (fork-random-uniqueforchild-no-symbolchildid): UFC=true is a no-op here → same as UFC=false.
  check("G6 UFC=true is no-op", evalR(0.0f, 1.0f, 1.0f, 3.0f), 0.695385786f, eps);

  return ok ? 0 : 1;
}

}  // namespace sw
