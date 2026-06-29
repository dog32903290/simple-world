// value_op_animvec3_golden — AnimVec3 MATH golden, split out of value_op_animvec3.cpp (ARCHITECTURE
// rule 4, ≤400-line leaf). The leaf carries the eval fn + NodeSpec registration; this sibling carries
// the heavy self-test body (flat + production-resident dual-leg). runAnimVec3SelfTest is forward-
// declared in the leaf and bound to the ValueOp registrar there; the value-op self-registration sink
// invokes it. Same split precedent as floatlist_animfloatlist_golden.cpp (AnimFloatList's golden).
//
// runtime leaf: pure computation, no hardware, no UI.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>
#include <string>

#include "runtime/Particle.h"             // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/anim_math.h"            // AnimMath shape engine (calcValueForNormalizedTime)
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / evalResidentFloat (the PRODUCTION gather)

namespace sw {

// --- AnimVec3 MATH golden -----------------------------------------------------------------------
// Builds a 1-node AnimVec3 graph, injects a known ctx.localFxTime (BARS), sets params, pulls
// Result.x/.y/.z via BOTH the flat path (evalFloat) AND the PRODUCTION resident path (libFromGraph →
// buildEvalGraph → evalResidentFloat — mirror of value_op_perlinnoise3.cpp's resident leg, closing
// the R-2 flat-only gap). Expected values are HAND-COMPUTED from the TiXL formula (arithmetic shown
// in each case), reproduced by an INDEPENDENT float32 reference (/tmp/animvec3_ref2.py in the build
// dossier) — NOT self-referential to this file's helpers.
//
// injectBug DETACHES the bars clock: production evaluate() reads ctx.localFxTime, but under injectBug
// the golden feeds ctx.localFxTime = 0 (and rc.localFxTime = 0 on the resident leg). The FIXED wants
// below are the LIVE (clock-running) values and are INDEPENDENT of injectBug — when the bug fires,
// got (clock=0) ≠ want (clock=live) → RED → exit 1. The two clock points are deliberately chosen so
// fmod(live·rate,1) ≠ fmod(0·...,1) for EVERY component (non-integer rates), so the divergence is
// real (a degenerate Sin-periodicity choice would let time=0 alias the live value and NOT bite).
int runAnimVec3SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // FLAT eval (evalFloat) of AnimVec3 at a given localFxTime with explicit params, named output port.
  auto evalAV = [&](float localFxTime, const char* outName,
                    float shape,
                    float rateX, float rateY, float rateZ, float rateFactor,
                    float phX, float phY, float phZ,
                    float ampX, float ampY, float ampZ, float ampFactor,
                    float offX, float offY, float offZ,
                    float bias, float ratio, float allowSpeed) -> float {
    const NodeSpec* spec = findSpec("AnimVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AnimVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Shape"]            = shape;
    g.node(nid)->params["Rates.x"]          = rateX;
    g.node(nid)->params["Rates.y"]          = rateY;
    g.node(nid)->params["Rates.z"]          = rateZ;
    g.node(nid)->params["RateFactor"]       = rateFactor;
    g.node(nid)->params["Phases.x"]         = phX;
    g.node(nid)->params["Phases.y"]         = phY;
    g.node(nid)->params["Phases.z"]         = phZ;
    g.node(nid)->params["Amplitudes.x"]     = ampX;
    g.node(nid)->params["Amplitudes.y"]     = ampY;
    g.node(nid)->params["Amplitudes.z"]     = ampZ;
    g.node(nid)->params["AmplitudeFactor"]  = ampFactor;
    g.node(nid)->params["Offsets.x"]        = offX;
    g.node(nid)->params["Offsets.y"]        = offY;
    g.node(nid)->params["Offsets.z"]        = offZ;
    g.node(nid)->params["Bias"]             = bias;
    g.node(nid)->params["Ratio"]            = ratio;
    g.node(nid)->params["AllowSpeedFactor"] = allowSpeed;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock → wrong shape value
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // RESIDENT-PATH eval (★ the PRODUCTION gather — value ops flow through the resident engine in the
  // running app. evalResidentFloat gathers AnimVec3's 18 Float inputs into its own in[kMaxFloatIn]
  // array. Mirror of value_op_perlinnoise3.cpp's resident leg / list_routing_golden's
  // cookResidentThenEval: build the SAME single-node graph, libFromGraph → buildEvalGraph (resident
  // path == node id string) → evalResidentFloat the named output slot. rc.localFxTime is detached
  // under injectBug (mirror of the flat leg) so the two paths stay in lock-step. The TOOTH that
  // matters for THIS leg is the cap/gather: if a future cap regression truncated the 18-input gather,
  // evalResidentFloat would NaN (the loud over-cap guard) → the NaN-aware assert flips RED.
  auto evalAVResident = [&](float localFxTime, const char* outSlot,
                            float shape,
                            float rateX, float rateY, float rateZ, float rateFactor,
                            float phX, float phY, float phZ,
                            float ampX, float ampY, float ampZ, float ampFactor,
                            float offX, float offY, float offZ,
                            float bias, float ratio, float allowSpeed) -> float {
    const NodeSpec* spec = findSpec("AnimVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AnimVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Shape"]            = shape;
    g.node(nid)->params["Rates.x"]          = rateX;
    g.node(nid)->params["Rates.y"]          = rateY;
    g.node(nid)->params["Rates.z"]          = rateZ;
    g.node(nid)->params["RateFactor"]       = rateFactor;
    g.node(nid)->params["Phases.x"]         = phX;
    g.node(nid)->params["Phases.y"]         = phY;
    g.node(nid)->params["Phases.z"]         = phZ;
    g.node(nid)->params["Amplitudes.x"]     = ampX;
    g.node(nid)->params["Amplitudes.y"]     = ampY;
    g.node(nid)->params["Amplitudes.z"]     = ampZ;
    g.node(nid)->params["AmplitudeFactor"]  = ampFactor;
    g.node(nid)->params["Offsets.x"]        = offX;
    g.node(nid)->params["Offsets.y"]        = offY;
    g.node(nid)->params["Offsets.z"]        = offZ;
    g.node(nid)->params["Bias"]             = bias;
    g.node(nid)->params["Ratio"]            = ratio;
    g.node(nid)->params["AllowSpeedFactor"] = allowSpeed;
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
    bool pass = std::fabs(got - want) < e;  // NaN-aware: fabs(NaN-want) is NaN, never < e → RED
    ok = ok && pass;
    printf("[selftest-animvec3] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A ★HASH-LIVE PRIMARY (Shape=Sin(7), localFxTime=2.0 bars):
  //   RateFactor(masterRate)=1.3, Rates=(0.7,1.1,1.9), Phases=(0.1,0.2,0.3), Ratio=1, Bias=0.5,
  //   Amplitudes=(2,3,4)·AmplitudeFactor(1.5) = (3,4.5,6), Offsets=(10,20,30), AllowSpeedFactor=FactorA.
  //   rateFactorFromContext = 1 (no FloatVariables on the value rail).
  //   normalizedTime[c] = (2.0 + phase[c]) * 1.3 * 1 * rate[c]:
  //     x: (2.0+0.1)*1.3*0.7 = 1.911     ; Sin: schlickBiasNeg(sin((fmod(1.911,1)+0.25)*2π·),0.5)=0.847677171
  //        Result.x = 0.847677171*3  + 10 = 12.543031692
  //     y: (2.0+0.2)*1.3*1.1 = 3.146     ; Sin shape = 0.607930720 ; *4.5 + 20 = 22.735689163
  //     z: (2.0+0.3)*1.3*1.9 = 5.681     ; Sin shape = -0.420086861; *6   + 30 = 27.479478836
  //   (Sin shape := schlickBiasWithNegative(mapShape(7, clamp(fmod(nt,1)/ratio,0,1)), bias);
  //    mapShape(7,f)=sin((f+0.25)*2*3.141592). Independent float32 reference reproduces these.)
  //   injectBug → ctx.localFxTime=0 → nt=(0+phase)*1.3*rate → DIFFERENT fmod → wrong Sin:
  //     x got≈12.5228, y got≈18.9908, z got≈29.6609  ≠ wants → A flips RED (non-integer rates ensure
  //     the time=0 point does NOT alias the live point — the divergence is real, not tautological).
  {
    const char* tag = "A Shape=Sin t=2.0 (HASH-LIVE)";
    const float wx = 12.543031692f, wy = 22.735689163f, wz = 27.479478836f;
    // FLAT leg.
    const float fx = evalAV(2.0f, "Result.x", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float fy = evalAV(2.0f, "Result.y", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float fz = evalAV(2.0f, "Result.z", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    check("A x FLAT==TiXL", fx, wx, eps);
    check("A y FLAT==TiXL", fy, wy, eps);
    check("A z FLAT==TiXL", fz, wz, eps);
    (void)tag;
    // RESIDENT leg (★ PRODUCTION gather, closes the R-2 gap): resident == flat == TiXL for each comp.
    const float rx = evalAVResident(2.0f, "Result.x", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float ry = evalAVResident(2.0f, "Result.y", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    const float rz = evalAVResident(2.0f, "Result.z", 7, 0.7f,1.1f,1.9f, 1.3f, 0.1f,0.2f,0.3f, 2,3,4, 1.5f, 10,20,30, 0.5f, 1.0f, 1);
    check("A x RESIDENT==TiXL", rx, wx, eps);
    check("A y RESIDENT==TiXL", ry, wy, eps);
    check("A z RESIDENT==TiXL", rz, wz, eps);
    check("A x RESIDENT==flat", rx, fx, eps);
    check("A y RESIDENT==flat", ry, fy, eps);
    check("A z RESIDENT==flat", rz, fz, eps);
  }

  // CASE B (t3 DEFAULTS, Shape=Ramps(1), localFxTime=1.234 bars): proves every default is mirrored
  //   from AnimVec3.t3 (params pre-seeded from p.def, then NOT overridden). Defaults: RateFactor=1,
  //   Rates=(1,1,1), Phases=(0,0,0), Amplitudes=(1,1,1), AmplitudeFactor=1, Offsets=(0,0,0),
  //   Bias=0.5, Ratio=1, AllowSpeedFactor=FactorA(1) → rfc=1.
  //   normalizedTime[c] = (1.234 + 0)*1*1*1 = 1.234 for all c.
  //   Ramps shape = schlickBias(mapShape(1, clamp(fmod(1.234,1)/1, 0,1)), 0.5);  mapShape(1,f)=f.
  //     fmod(1.234f,1)=0.233999968 (float32) ; schlickBias(x,0.5)=x identity → 0.233999968.
  //   Result.x/y/z = 0.233999968*1 + 0 = 0.233999968 (all three identical at defaults).
  //   injectBug → ctx.localFxTime=0 → nt=0 → fmod(0,1)=0 → Ramps=0 → got=0 ≠ 0.234 → B flips RED.
  {
    auto evalDef = [&](const char* outName) -> float {
      const NodeSpec* spec = findSpec("AnimVec3");
      if (!spec) return -999.0f;
      Graph g;
      Node nd; nd.id = g.nextId++; nd.type = "AnimVec3";
      for (const auto& p : spec->ports)
        if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;  // pure t3 defaults
      g.nodes.push_back(nd);
      int nid = g.nodes.back().id;
      int outIdx = -1;
      for (size_t i = 0; i < spec->ports.size(); ++i)
        if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
      EvaluationContext ctx{};
      ctx.localFxTime = injectBug ? 0.0f : 1.234f;
      return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
    };
    // UNCONDITIONAL anchors (t3 defaults, localFxTime=1.234). fmod(1.234f,1)=0.233999968f (float32);
    // schlickBias(x,0.5) is the identity ⇒ Result = 0.233999968 (independent float32 reference).
    check("B x defaults (Ramps,Shape=1,Bias=0.5)", evalDef("Result.x"), 0.233999968f, eps);
    check("B y defaults (Amplitudes.y=1,Offsets.y=0)", evalDef("Result.y"), 0.233999968f, eps);
    check("B z defaults (RateFactor=1,Rates.z=1)", evalDef("Result.z"), 0.233999968f, eps);
  }

  // CASE C (OverrideTime knob = live clock substitute). Self-consistency: node driven by OverrideTime=T
  //   with localFxTime=0 must equal node driven by the clock at localFxTime=T (OverrideTime default 0).
  //   Proves the knob substitutes for the bars clock 1:1. (override leg is injectBug-independent: clock unused.)
  {
    const NodeSpec* spec = findSpec("AnimVec3");
    if (spec) {
      const float T = 1.234f;
      auto at = [&](const char* nm, float ovr, float clk) -> float {  // ovr=OverrideTime, clk=localFxTime
        Graph g; Node nd; nd.id = g.nextId++; nd.type = "AnimVec3";
        for (const auto& p : spec->ports) if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
        g.nodes.push_back(nd); int nid = g.nodes.back().id;
        g.node(nid)->params["OverrideTime"] = ovr;
        int oi = -1; for (size_t i = 0; i < spec->ports.size(); ++i) if (spec->ports[i].id == nm) { oi = (int)i; break; }
        EvaluationContext ctx{}; ctx.localFxTime = clk;
        return oi < 0 ? -997.0f : evalFloat(g, pinId(nid, oi), ctx, 0);
      };
      check("C x OverrideTime=T == clock@T", at("Result.x", T, 0.0f), at("Result.x", 0.0f, T), eps);
      check("C y OverrideTime=T == clock@T", at("Result.y", T, 0.0f), at("Result.y", 0.0f, T), eps);
      check("C z OverrideTime=T == clock@T", at("Result.z", T, 0.0f), at("Result.z", 0.0f, T), eps);
    }
  }

  return ok ? 0 : 1;
}

}  // namespace sw
