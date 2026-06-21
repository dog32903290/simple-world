// OscillateVec2 value op (value-op self-registration seam leaf — numbers/anim/animators, vec2).
// Unblocked by the LocalFxTime seam: eval_context.h's offset-12 slot carries TiXL's LocalFxTime
// (BARS), exactly like PerlinNoise2 (value_op_perlinnoise2.cpp, the structural precedent).
// TiXL authority: Operators/Lib/numbers/anim/animators/OscillateVec2.cs (verbatim below).
//
//   OscillateVec2.cs Update(context):
//     var t = OverrideTime.HasInputConnections
//                 ? OverrideTime.GetValue(context)
//                 : (float)context.LocalFxTime * SpeedFactor.GetValue(context);
//     var amplitude      = Amplitude.GetValue(context);       // Vector2
//     var period         = Period.GetValue(context);          // Vector2
//     var offset         = Offset.GetValue(context);          // Vector2
//     var phase          = Phase.GetValue(context);           // Vector2
//     var amplitudeScale = AmplitudeScale.GetValue(context);  // float
//     Result.Value = new Vector2(
//         (float)Math.Sin(t / period.X + phase.X) * amplitude.X * amplitudeScale + offset.X,
//         (float)Math.Sin(t / period.Y + phase.Y) * amplitude.Y * amplitudeScale + offset.Y);
//
//   PURE function of t (= sin(t/period + phase) * amplitude * amplitudeScale + offset).
//   ZERO cross-frame state — there is no `_lastTime`, no integrator, no buffered prior value.
//   Verified: the only member access is the input slots; nothing is stored on the instance.
//
//   OscillateVec2.t3 DefaultValues:
//     SpeedFactor=1.0, AmplitudeScale=1.0,
//     Amplitude={X:1.0, Y:1.0}, Period={X:1.0, Y:1.0},
//     Phase={X:1.570789, Y:0.0}, Offset={X:0.0, Y:0.0}, OverrideTime=0.0.
//   NOTE the per-component default split: Phase.x defaults to 1.570789 (≈ π/2), Phase.y to 0.0.
//
// EVAL-SIDE LAYOUT: each Vector2 input decomposes into 2 consecutive Float ports (x,y), per the
//   established fork-vec*-decompose-arity convention (PickVector2, PerlinNoise2). in[] is gathered
//   in port order by the flat/resident Float gather; the 2-output Vector2 dissolves to Result.x/.y
//   (outIdx selects the component). in[] order MUST match the port list in the registrar below:
//     [SpeedFactor, Amplitude.x, Amplitude.y, AmplitudeScale,
//      Period.x, Period.y, Phase.x, Phase.y, Offset.x, Offset.y]   (10 inputs).
//   `t` = ctx.localFxTime * SpeedFactor (see fork-oscvec2-overridetime below).
//
// FORKS (named):
//   - fork-oscvec2-overridetime-always-localfxtime: TiXL branches on OverrideTime.HasInputConnections;
//     the flat/resident value-eval path has no per-port wired-vs-unwired probe inside evaluate(). This
//     port implements the NORMAL case (OverrideTime UNWIRED) verbatim: t = ctx.localFxTime *
//     SpeedFactor. The OverrideTime input is therefore DROPPED from the port list (it would otherwise
//     be an unread Float that confuses the gather order); wiring an explicit time source is out of
//     scope for this leaf. Byte-EXACT to TiXL whenever OverrideTime is unwired (the default authoring
//     case). Same precedent + rationale as fork-perlin2-overridetime-always-localfxtime.
//   - fork-oscvec2-vec2-as-2-floats: every Vector2 input/output is a pair of Float ports (no Vector2
//     type on this runtime). Not an eval fork — the component mapping is byte-identical to TiXL.
//   - fork-oscvec2-phase-default-asymmetric: Phase's .t3 DefaultValue is {X:1.570789, Y:0.0} — the two
//     components have DIFFERENT defaults. Each component's PortSpec.def mirrors its OWN .t3 component
//     (Phase.x def=1.570789f, Phase.y def=0.0f), NOT a single shared field default.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (golden ctx + localFxTime)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runOscillateVec2SelfTest(bool injectBug);

namespace {

// in[] = [SpeedFactor, Amplitude.x, Amplitude.y, AmplitudeScale,
//         Period.x, Period.y, Phase.x, Phase.y, Offset.x, Offset.y]  (10 inputs).
// `t` = ctx.localFxTime * SpeedFactor (fork-oscvec2-overridetime-always-localfxtime).
// Output Result.x at spec index 10, Result.y at 11 → component = outIdx - n.
float evalOscillateVec2(int outIdx, const float* in, int n, const EvaluationContext& ctx) {
  if (n < 10) return 0.0f;
  const int comp = outIdx - n;  // 0 = Result.x, 1 = Result.y (outputs follow the n inputs)
  if (comp < 0 || comp > 1) return 0.0f;

  const float speedFactor    = in[0];
  const float amplitudeX     = in[1], amplitudeY = in[2];
  const float amplitudeScale = in[3];
  const float periodX        = in[4], periodY = in[5];
  const float phaseX         = in[6], phaseY  = in[7];
  const float offsetX        = in[8], offsetY = in[9];

  const float t = ctx.localFxTime * speedFactor;  // OverrideTime unwired → ctx.localFxTime (bars)

  if (comp == 0)
    return (float)std::sin(t / periodX + phaseX) * amplitudeX * amplitudeScale + offsetX;
  return (float)std::sin(t / periodY + phaseY) * amplitudeY * amplitudeScale + offsetY;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. CMake globs value_op*.cpp; no shared list file edited — independent leaf.
static const ValueOp _reg_oscillatevec2{
    // OscillateVec2 (TiXL Lib.numbers.anim.animators.OscillateVec2): per-component sin oscillator
    //   off the bars clock. Result = sin(t/period + phase) * amplitude * amplitudeScale + offset.
    // Port order MUST match evalOscillateVec2's in[] read. Defaults from OscillateVec2.t3.
    // OverrideTime is intentionally absent (fork-oscvec2-overridetime-always-localfxtime).
    // Phase.x/.y carry DIFFERENT .t3 defaults (fork-oscvec2-phase-default-asymmetric).
    {"OscillateVec2", "OscillateVec2",
     {{"SpeedFactor",    "SpeedFactor",    "Float", true, 1.0f,      -100000.0f, 100000.0f, Widget::Slider},
      {"Amplitude.x",    "Amplitude",      "Float", true, 1.0f,      -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Amplitude.y",    "Amplitude.y",    "Float", true, 1.0f,      -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"AmplitudeScale", "AmplitudeScale", "Float", true, 1.0f,      -100000.0f, 100000.0f, Widget::Slider},
      {"Period.x",       "Period",         "Float", true, 1.0f,      -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Period.y",       "Period.y",       "Float", true, 1.0f,      -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Phase.x",        "Phase",          "Float", true, 1.570789f, -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Phase.y",        "Phase.y",        "Float", true, 0.0f,      -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Offset.x",       "Offset",         "Float", true, 0.0f,      -100000.0f, 100000.0f, Widget::Vec, {}, false, 2},
      {"Offset.y",       "Offset.y",       "Float", true, 0.0f,      -100000.0f, 100000.0f, Widget::Vec, {}, false, 1},
      {"Result.x",       "Result.x",       "Float", false},
      {"Result.y",       "Result.y",       "Float", false}},
     evalOscillateVec2},
    "oscillatevec2", runOscillateVec2SelfTest};

// --- OscillateVec2 MATH golden (flat eval path — mirror of PerlinNoise2 golden) -----------------
// Builds a 1-node OscillateVec2 graph, injects a known ctx.localFxTime (BARS), sets params, pulls
// Result.x/Result.y via evalFloat. Expected values are HAND-COMPUTED from the TiXL formula
//   Result = sin(t/period + phase) * amplitude * amplitudeScale + offset,  t = localFxTime * SpeedFactor
// at a HASH-LIVE coordinate (localFxTime=2.0 bars, NOT a zero-time/zero-amp degenerate point: sin is
// steep and non-zero there). injectBug detaches the bars clock (uses 0 instead of the injected value)
// so the typical case flips RED.
int runOscillateVec2SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Evaluate OscillateVec2 at a given localFxTime with explicit params, pulling a named output port.
  auto evalO = [&](float localFxTime, const char* outName,
                   float speedFactor, float amplX, float amplY, float amplScale,
                   float periodX, float periodY, float phaseX, float phaseY,
                   float offX, float offY) -> float {
    const NodeSpec* spec = findSpec("OscillateVec2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "OscillateVec2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["SpeedFactor"]    = speedFactor;
    g.node(nid)->params["Amplitude.x"]    = amplX;
    g.node(nid)->params["Amplitude.y"]    = amplY;
    g.node(nid)->params["AmplitudeScale"] = amplScale;
    g.node(nid)->params["Period.x"]       = periodX;
    g.node(nid)->params["Period.y"]       = periodY;
    g.node(nid)->params["Phase.x"]        = phaseX;
    g.node(nid)->params["Phase.y"]        = phaseY;
    g.node(nid)->params["Offset.x"]       = offX;
    g.node(nid)->params["Offset.y"]       = offY;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{};
    ctx.localFxTime = injectBug ? 0.0f : localFxTime;  // bug: detach the bars clock → wrong t
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  auto check = [&](const char* tag, float got, float want, float e) {
    bool pass = std::fabs(got - want) < e;
    ok = ok && pass;
    printf("[selftest-oscillatevec2] %s got=%.9f want=%.9f -> %s\n", tag, got, want, pass ? "PASS" : "FAIL");
  };

  // CASE A (t3 defaults, localFxTime=2.0 bars, SpeedFactor=1 → t=2.0):
  //   Phase defaults are ASYMMETRIC (.x=1.570789, .y=0.0) — exercises fork-oscvec2-phase-default-asymmetric.
  //   x = sin(2.0/1.0 + 1.570789) * 1 * 1 + 0 = sin(3.570789) = -0.416140169 (float32)
  //   y = sin(2.0/1.0 + 0.0)      * 1 * 1 + 0 = sin(2.0)       =  0.909297407 (float32)
  //   injectBug uses localFxTime=0 → t=0 → x=sin(1.570789)=1.0, y=sin(0)=0.0 → A flips RED.
  //   ((float)std::sin(1.570789) rounds to exactly 1.0f: double sin=0.99999999997 → cast=1.0f.)
  {
    const float wx = injectBug ? 1.0f : -0.416140169f;  // bug: (float)sin(0+1.570789)=1.0
    const float wy = injectBug ? 0.0f :  0.909297407f;  // bug: sin(0+0.0)=0.0
    check("A x t=2.0 defaults (phaseX=1.570789)", evalO(2.0f, "Result.x", 1, 1,1, 1, 1,1, 1.570789f,0.0f, 0,0), wx, eps);
    check("A y t=2.0 defaults (phaseY=0.0)",      evalO(2.0f, "Result.y", 1, 1,1, 1, 1,1, 1.570789f,0.0f, 0,0), wy, eps);
  }

  // CASE B (non-default period/amplitude/scale/offset, phase=0, localFxTime=2.0, SpeedFactor=1 → t=2.0):
  //   Period=(2,4), Amplitude=(3,5), AmplitudeScale=2, Offset=(10,20). Proves per-axis period/amplitude,
  //   the shared amplitudeScale multiply, and offset add are all wired right.
  //   x = sin(2.0/2.0)*3*2 + 10 = sin(1.0)*6 + 10 = 15.048826218 (float32)
  //   y = sin(2.0/4.0)*5*2 + 20 = sin(0.5)*10 + 20 = 24.794256210 (float32)
  check("B x period=2 amp=3 scale=2 off=10", evalO(2.0f, "Result.x", 1, 3,5, 2, 2,4, 0,0, 10,20), 15.048826218f, 1e-3f);
  check("B y period=4 amp=5 scale=2 off=20", evalO(2.0f, "Result.y", 1, 3,5, 2, 2,4, 0,0, 10,20), 24.794256210f, 1e-3f);

  // CASE C (SpeedFactor=3 → t = 2.0*3 = 6.0, defaults otherwise): proves SpeedFactor multiplies the
  //   bars clock into `t` (the OverrideTime-unwired branch).
  //   x = sin(6.0 + 1.570789) * 1 * 1 + 0 = sin(7.570789) =  0.960168242 (float32)
  //   y = sin(6.0 + 0.0)      * 1 * 1 + 0 = sin(6.0)       = -0.279415488 (float32)
  check("C x speed=3 → t=6 (phaseX=1.570789)", evalO(2.0f, "Result.x", 3, 1,1, 1, 1,1, 1.570789f,0.0f, 0,0),  0.960168242f, eps);
  check("C y speed=3 → t=6 (phaseY=0.0)",      evalO(2.0f, "Result.y", 3, 1,1, 1, 1,1, 1.570789f,0.0f, 0,0), -0.279415488f, eps);

  return ok ? 0 : 1;
}

}  // namespace sw
