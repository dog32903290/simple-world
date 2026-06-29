// OscillateVec3 value op (value-op self-registration seam leaf — numbers/anim/animators).
// TiXL authority: Operators/Lib/numbers/anim/animators/OscillateVec3.cs (+ OscillateVec3.t3 defaults).
//
//   OscillateVec3.cs Update(context):   (VERBATIM)
//     var t = OverrideTime.HasInputConnections
//                 ? OverrideTime.GetValue(context)
//                 : (float)context.LocalFxTime * SpeedFactor.GetValue(context);
//     var amplitude      = Amplitude.GetValue(context);       // Vector3
//     var period         = Period.GetValue(context);          // Vector3
//     var offset         = Offset.GetValue(context);          // Vector3
//     var phase          = Phase.GetValue(context);           // Vector3
//     var amplitudeScale = AmplitudeScale.GetValue(context);  // float (scalar, applies to all 3)
//     Result.Value = new Vector3(
//         (float)Math.Sin(t / period.X + phase.X) * amplitude.X * amplitudeScale + offset.X,
//         (float)Math.Sin(t / period.Y + phase.Y) * amplitude.Y * amplitudeScale + offset.Y,
//         (float)Math.Sin(t / period.Z + phase.Z) * amplitude.Z * amplitudeScale + offset.Z);
//
//   PURE function, ZERO cross-frame state: no `static`, no per-instance field, no `+=`, no read of a
//   previous-frame value. Output depends ONLY on (ctx.localFxTime) + the input ports. Confirmed against
//   the .cs: every value comes from `*.GetValue(context)` or `context.LocalFxTime`; nothing persists.
//
//   OscillateVec3.t3 DefaultValues (mirrored per-component into the PortSpec below):
//     Offset         = (0, 0, 0)
//     Amplitude      = (1, 1, 0)          <-- note Amplitude.Z default is 0 (not 1)
//     Period         = (1, 1, 1)
//     AmplitudeScale = 1
//     SpeedFactor    = 1
//     OverrideTime   = 0   (DROPPED port — see fork-oscillatevec3-overridetime-always-localfxtime)
//     Phase          = (1.570789, 0, 0)   <-- Phase.X default ≈ π/2 (a quarter-period head start)
//
// EVAL-SIDE LAYOUT (single-wire path — NO multiInput; uses the simple Graph/Node/evalFloat golden
//   like value_op_perlinnoise2.cpp, NOT the resident multiInput gather):
//   Each Vector3 input decomposes into 3 consecutive Float ports (x,y,z) per the established
//   fork-vec*-decompose-arity convention. in[] is gathered in port order. Port/in[] layout:
//     in[0]  = SpeedFactor
//     in[1]  = OverrideTime
//     in[2]  = Amplitude.x   in[3] = Amplitude.y   in[4] = Amplitude.z
//     in[5]  = AmplitudeScale
//     in[6]  = Period.x      in[7] = Period.y      in[8] = Period.z
//     in[9]  = Phase.x       in[10]= Phase.y       in[11]= Phase.z
//     in[12] = Offset.x      in[13]= Offset.y      in[14]= Offset.z   (n = 15 inputs)
//   Output ports Result.x/.y/.z follow the 15 inputs at spec indices 15/16/17 → comp = outIdx - n.
//   t = OverrideTime != 0 ? OverrideTime : ctx.localFxTime * SpeedFactor (fork-oscillatevec3-overridetime-nonzero-single-clock).
//
// FORKS (named):
//   - fork-oscillatevec3-overridetime-nonzero-single-clock: TiXL branches on
//     OverrideTime.HasInputConnections; the flat value-eval path has no per-port wired-vs-unwired probe
//     inside evaluate(). The standing single-clock convention (AnimValue/AnimVec2) maps "has input
//     connection" → "the OverrideTime constant is non-zero": t = overrideTime != 0 ? overrideTime :
//     (float)ctx.localFxTime * SpeedFactor. With the .t3 default OverrideTime=0 this is exactly
//     ctx.localFxTime*SpeedFactor (the default authoring case), byte-EXACT to TiXL; a non-zero
//     OverrideTime constant overrides the bars clock (and bypasses SpeedFactor, matching TiXL's wired
//     branch). Diverges from TiXL ONLY in the narrow "OverrideTime connected AND driven to exactly 0.0"
//     case — unreachable here without owner-locked cook-core per-port connection plumbing.
//   - fork-oscillatevec3-vec3-as-3-floats: every Vector3 input/output is 3 Float ports (no Vector3 type
//     on this runtime). Not an eval fork — the component mapping is byte-identical to TiXL. Same
//     convention as BlendVector3 / PerlinNoise2.
//   - fork-oscillatevec3-amplitudescale-scalar-broadcast: AmplitudeScale is a single float in TiXL that
//     multiplies all three components. Stored as ONE scalar Float port (in[4]); applied to x/y/z alike.
//     Verbatim — not a divergence, documented because it is the only non-Vec3 value input.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runOscillateVec3SelfTest(bool injectBug);

namespace {

// in[] = [SpeedFactor, OverrideTime, Amplitude.x/y/z, AmplitudeScale, Period.x/y/z, Phase.x/y/z, Offset.x/y/z].
// n = 15 inputs; outputs Result.x/.y/.z follow at indices 15/16/17 → comp = outIdx - n.
// t = OverrideTime != 0 ? OverrideTime : ctx.localFxTime * SpeedFactor (fork-oscillatevec3-overridetime-nonzero-single-clock).
// Result[comp] = sin(t / period[comp] + phase[comp]) * amplitude[comp] * amplitudeScale + offset[comp].
float evalOscillateVec3(int outIdx, const float* in, int n, const EvaluationContext& ctx) {
  if (n < 15) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y, 2 = Result.z (outputs follow n inputs)
  if (comp < 0 || comp > 2) return 0.0f;

  const float speedFactor    = in[0];
  const float overrideTime   = in[1];
  const float amplitude[3]   = {in[2], in[3], in[4]};
  const float amplitudeScale = in[5];
  const float period[3]      = {in[6], in[7], in[8]};
  const float phase[3]       = {in[9], in[10], in[11]};
  const float offset[3]      = {in[12], in[13], in[14]};

  // OverrideTime nonzero overrides the bars clock (and bypasses SpeedFactor, matching TiXL's wired branch).
  const float t = (overrideTime != 0.0f) ? overrideTime : ctx.localFxTime * speedFactor;

  // (float)Math.Sin(t / period[comp] + phase[comp]) * amplitude[comp] * amplitudeScale + offset[comp].
  return (float)std::sin(t / period[comp] + phase[comp]) * amplitude[comp] * amplitudeScale +
         offset[comp];
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. CMake globs value_op*.cpp; no shared list file edited — independent leaf.
static const ValueOp _reg_oscillatevec3{
    // OscillateVec3 (TiXL Lib.numbers.anim.animators.OscillateVec3): per-component sine oscillator on
    // the bars clock. Port order MUST match evalOscillateVec3's in[] read. Defaults from OscillateVec3.t3.
    // OverrideTime SECOND (TiXL [Input] order); nonzero overrides the clock (fork-oscillatevec3-overridetime-nonzero-single-clock).
    {"OscillateVec3", "OscillateVec3",
     {{"SpeedFactor",    "SpeedFactor",    "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"OverrideTime",   "OverrideTime",   "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Amplitude.x",    "Amplitude",      "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Amplitude.y",    "Amplitude.y",    "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Amplitude.z",    "Amplitude.z",    "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"AmplitudeScale", "AmplitudeScale", "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Slider},
      {"Period.x",       "Period",         "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Period.y",       "Period.y",       "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Period.z",       "Period.z",       "Float", true, 1.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Phase.x",        "Phase",          "Float", true, 1.570789f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Phase.y",        "Phase.y",        "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Phase.z",        "Phase.z",        "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Offset.x",       "Offset",         "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 3},
      {"Offset.y",       "Offset.y",       "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Offset.z",       "Offset.z",       "Float", true, 0.0f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Result.x",       "Result.x",       "Float", false},
      {"Result.y",       "Result.y",       "Float", false},
      {"Result.z",       "Result.z",       "Float", false}},
     evalOscillateVec3},
    "oscillatevec3", runOscillateVec3SelfTest};

// --- OscillateVec3 MATH golden ------------------------------------------------------------------
// Builds a 1-node OscillateVec3 graph, injects a known ctx.localFxTime (BARS), sets params, pulls
// Result.x/.y/.z via evalFloat. Compares to the closed-form HAND-COMPUTED from the TiXL formula
// (arithmetic shown in each case). injectBug detaches the bars clock (ctx.localFxTime = 0) so the
// HASH-LIVE case flips RED.
int runOscillateVec3SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Evaluate OscillateVec3 at a given localFxTime with explicit params, pulling a named output port.
  auto evalOsc = [&](float localFxTime, const char* outName,
                     float speedFactor,
                     float ampX, float ampY, float ampZ, float ampScale,
                     float perX, float perY, float perZ,
                     float phX, float phY, float phZ,
                     float offX, float offY, float offZ) -> float {
    const NodeSpec* spec = findSpec("OscillateVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "OscillateVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["SpeedFactor"]    = speedFactor;
    g.node(nid)->params["Amplitude.x"]    = ampX;
    g.node(nid)->params["Amplitude.y"]    = ampY;
    g.node(nid)->params["Amplitude.z"]    = ampZ;
    g.node(nid)->params["AmplitudeScale"] = ampScale;
    g.node(nid)->params["Period.x"]       = perX;
    g.node(nid)->params["Period.y"]       = perY;
    g.node(nid)->params["Period.z"]       = perZ;
    g.node(nid)->params["Phase.x"]        = phX;
    g.node(nid)->params["Phase.y"]        = phY;
    g.node(nid)->params["Phase.z"]        = phZ;
    g.node(nid)->params["Offset.x"]       = offX;
    g.node(nid)->params["Offset.y"]       = offY;
    g.node(nid)->params["Offset.z"]       = offZ;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock → wrong oscillation
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-oscillatevec3] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A (HASH-LIVE — sin is fully live, all components distinct, no degenerate t=0/period=0):
  //   localFxTime = 2.0 bars, SpeedFactor = 1.5  →  t = 2.0 * 1.5 = 3.0
  //   Amplitude = (2,3,4), AmplitudeScale = 1.5, Period = (2,3,1.5),
  //   Phase = (0.5, 1.3, 0.25), Offset = (10, 20, 30).
  //   x: arg = 3.0/2.0 + 0.5  = 2.0    ; sin(2.0)   =  0.909297427 ; *2*1.5 + 10 = 12.727891922
  //   y: arg = 3.0/3.0 + 1.3  = 2.3    ; sin(2.3)   =  0.745705212 ; *3*1.5 + 20 = 23.355674744
  //   z: arg = 3.0/1.5 + 0.25 = 2.25   ; sin(2.25)  =  0.778073197 ; *4*1.5 + 30 = 34.668437958
  //   (float32 results from an INDEPENDENT numpy.float32 reference; arithmetic above is the TiXL form.)
  //   injectBug sets ctx.localFxTime=0 → t=0 → arg = phase[comp] → wrong sin → A flips RED.
  {
    const float live = 2.0f, sf = 1.5f;
    // UNCONDITIONAL anchors: want is the LIVE (t=3) value regardless of injectBug.
    // When injectBug fires, ctx.localFxTime=0 → t=0 → got ≠ want → RED.
    // x live: sin(2.0)*2*1.5+10  = 0.909297427*3+10  = 12.727891922
    // y live: sin(2.3)*3*1.5+20  = 0.745705212*4.5+20 = 23.355674744
    // z live: sin(2.25)*4*1.5+30 = 0.778073197*6+30  = 34.668437958
    check("A x live t=3", evalOsc(live, "Result.x", sf, 2,3,4, 1.5f, 2,3,1.5f, 0.5f,1.3f,0.25f, 10,20,30), 12.727891922f, eps);
    check("A y live t=3", evalOsc(live, "Result.y", sf, 2,3,4, 1.5f, 2,3,1.5f, 0.5f,1.3f,0.25f, 10,20,30), 23.355674744f, eps);
    check("A z live t=3", evalOsc(live, "Result.z", sf, 2,3,4, 1.5f, 2,3,1.5f, 0.5f,1.3f,0.25f, 10,20,30), 34.668437958f, eps);
  }

  // CASE B (t3 DEFAULTS, localFxTime = 1.0 bars): proves every default is mirrored from OscillateVec3.t3.
  //   SpeedFactor=1 → t = 1.0 * 1 = 1.0; Amplitude=(1,1,0), AmplitudeScale=1, Period=(1,1,1),
  //   Phase=(1.570789, 0, 0), Offset=(0,0,0).
  //   x: arg = 1.0/1 + 1.570789 = 2.570789 ; sin(2.570789) = 0.540308595 ; *1*1 + 0 = 0.540308595
  //      (Phase.X ≈ π/2 head start — pins the non-obvious 1.570789 default.)
  //   y: arg = 1.0/1 + 0        = 1.0       ; sin(1.0)      = 0.841471016 ; *1*1 + 0 = 0.841471016
  //   z: arg = 1.0/1 + 0        = 1.0       ; sin(1.0)      = 0.841471016 ; *0*1 + 0 = 0.000000000
  //      (Amplitude.Z default = 0 → z is exactly 0 regardless of sin; pins the Amplitude.Z=0 default.)
  //   This case uses the spec defaults verbatim (params pre-seeded from p.def), so it also guards the
  //   PortSpec defaults against drift. injectBug (t=0 → arg=1.570789 for x) flips x RED.
  {
    auto evalDef = [&](const char* outName) -> float {
      const NodeSpec* spec = findSpec("OscillateVec3");
      if (!spec) return -999.0f;
      Graph g;
      Node nd; nd.id = g.nextId++; nd.type = "OscillateVec3";
      for (const auto& p : spec->ports)
        if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;  // pure t3 defaults
      g.nodes.push_back(nd);
      int nid = g.nodes.back().id;
      int outIdx = -1;
      for (size_t i = 0; i < spec->ports.size(); ++i)
        if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
      EvaluationContext ctx{};
      ctx.localFxTime = injectBug ? 0.0f : 1.0f;
      return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
    };
    // UNCONDITIONAL anchors (t3 defaults, localFxTime=1.0).
    // When injectBug fires, ctx.localFxTime=0 → t=0:
    //   x got = sin(1.570789)*1*1+0 ≈ 1.0   ≠ 0.540308595 → RED
    //   y got = sin(0.0)*1*1+0      = 0.0   ≠ 0.841471016 → RED
    //   z = 0 regardless (Amplitude.z=0) → still GREEN (correct; not a clock-live anchor)
    check("B x defaults (Phase.x=pi/2)", evalDef("Result.x"), 0.540308595f, eps);
    check("B y defaults",                evalDef("Result.y"), 0.841471016f, eps);
    check("B z defaults (Amplitude.z=0)", evalDef("Result.z"), 0.000000000f, eps);  // z=0 either way
  }

  // CASE C (OverrideTime=3.0 nonzero): proves the knob OVERRIDES the bars clock AND bypasses SpeedFactor.
  //   t = 3.0 (NOT localFxTime*SpeedFactor). Defaults otherwise (Amplitude=(1,1,0), Period=(1,1,1),
  //   Phase=(1.570789,0,0), AmplitudeScale=1, Offset=0), SpeedFactor driven to 9 to prove it is IGNORED.
  //   x: arg = 3.0/1 + 1.570789 = 4.570789 ; sin(4.570789) = -0.989993... ; *1*1+0
  //   y: arg = 3.0/1 + 0        = 3.0       ; sin(3.0)      =  0.141120002 ; *1*1+0
  //   z: Amplitude.z=0 → 0. Independent of injectBug (ctx.localFxTime unused when overridden) → the
  //   SAME want on both legs; pins that nonzero OverrideTime decouples from the clock (knob is live).
  {
    const NodeSpec* spec = findSpec("OscillateVec3");
    if (spec) {
      auto evalC = [&](const char* outName) -> float {
        Graph g; Node nd; nd.id = g.nextId++; nd.type = "OscillateVec3";
        for (const auto& p : spec->ports) if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
        g.nodes.push_back(nd); int nid = g.nodes.back().id;
        g.node(nid)->params["OverrideTime"] = 3.0f;  // nonzero → override
        g.node(nid)->params["SpeedFactor"]  = 9.0f;  // must be IGNORED on the override branch
        int outIdx = -1;
        for (size_t i = 0; i < spec->ports.size(); ++i) if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
        EvaluationContext ctx{}; ctx.localFxTime = injectBug ? 0.0f : 2.0f;  // unused when overridden
        return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
      };
      check("C x OverrideTime=3 (clock+speed bypassed)", evalC("Result.x"), (float)std::sin(4.570789) , eps);
      check("C y OverrideTime=3 (clock+speed bypassed)", evalC("Result.y"), 0.141120002f, eps);
      check("C z OverrideTime=3 (Amplitude.z=0)",        evalC("Result.z"), 0.0f, eps);
    }
  }

  return ok ? 0 : 1;
}

}  // namespace sw
