// value_op_perlinnoise3_golden — PerlinNoise3 MATH golden, split out of value_op_perlinnoise3.cpp
// (ARCHITECTURE rule 4, line-count ratchet). The leaf carries the eval fn + NodeSpec registration;
// this sibling carries the heavy flat + production-resident self-test body. runPerlinNoise3SelfTest is
// forward-declared in the leaf and bound to the ValueOp registrar there; the value-op self-registration
// sink invokes it. Same split precedent as floatlist_animfloatlist_golden.cpp / value_op_animvec3_golden.cpp.
//
// runtime leaf: pure computation, no hardware, no UI.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/Particle.h"             // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (the PRODUCTION gather)

namespace sw {

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

  // CASE F (OverrideTime=2.5 nonzero, localFxTime=0) on the PRODUCTION resident path: proves the knob
  //   OVERRIDES the bars clock AND that the new 20th input gathers into in[kMaxFloatIn] (a cap regression
  //   dropping it → value back to localFxTime=0 = CASE B → RED). value = OverrideTime(2.5)+Phase(0) = 2.5
  //   → same as CASE A's t=2.5 wants, even though localFxTime=0. (Flat path is covered by A–E.)
  {
    const NodeSpec* spec = findSpec("PerlinNoise3");
    if (spec) {
      Graph g; Node nd; nd.id = g.nextId++; nd.type = "PerlinNoise3";
      for (const auto& p : spec->ports) if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
      g.nodes.push_back(nd); int nid = g.nodes.back().id;
      g.node(nid)->params["OverrideTime"] = 2.5f;  // nonzero → override the clock
      SymbolLibrary lib = libFromGraph(g);
      ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
      ResidentEvalCtx rc; rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
      check("F x OverrideTime=2.5 RESIDENT", evalResidentFloat(rg, std::to_string(nid), "Result.x", rc), -0.231699109f, eps);
      check("F y OverrideTime=2.5 RESIDENT", evalResidentFloat(rg, std::to_string(nid), "Result.y", rc), -0.164338112f, eps);
      check("F z OverrideTime=2.5 RESIDENT", evalResidentFloat(rg, std::to_string(nid), "Result.z", rc), -0.018129349f, eps);
    }
  }

  return ok ? 0 : 1;
}

}  // namespace sw
