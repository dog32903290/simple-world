// RemapVec2 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/RemapVec2.cs (+ RemapVec2.t3 defaults).
//
//   RemapVec2.cs Update():  (lines 18–43)
//     var value  = Value.GetValue(context);
//     var inMin  = RangeInMin.GetValue(context);
//     var inMax  = RangeInMax.GetValue(context);
//     var outMin = RangeOutMin.GetValue(context);
//     var outMax = RangeOutMax.GetValue(context);
//     var factor = (value - inMin) / (inMax - inMin);         // line 24
//     var v      = factor * (outMax - outMin) + outMin;       // line 25
//     switch (Mode) {
//       case Clamped: v = MathUtils.Clamp(v, outMin, outMax); break;
//       case Modulo:  v = new Vector2(Fmod(v.X, delta.X), Fmod(v.Y, delta.Y)); break;
//     }
//     Result.Value = v;
//
//   All operands are Vector2 (componentwise). Mode enum: Normal=0, Clamped=1, Modulo=2.
//   RemapVec2.t3 DefaultValues:
//     Value={X:0,Y:0}, RangeInMin={X:0,Y:0}, RangeInMax={X:1,Y:1},
//     RangeOutMin={X:0,Y:0}, RangeOutMax={X:1,Y:1}, Mode=0.
//
// EVAL-SIDE LAYOUT (flat, no multiInput):
//   in[] = [Value.x, Value.y,
//           RangeInMin.x, RangeInMin.y,
//           RangeInMax.x, RangeInMax.y,
//           RangeOutMin.x, RangeOutMin.y,
//           RangeOutMax.x, RangeOutMax.y,
//           Mode]                              (n=11 inputs).
//   Output ports (Result.x, Result.y) at spec indices n=11 and n+1=12.
//   Component k = outIdx - n ∈ {0=x, 1=y}.
//
// FORKS (named):
//   - fork-remapvec2-vec2-as-2-floats (precedent: fork-addvec2-vec2-as-2-floats): TiXL has
//     native Vector2 ports. This runtime exposes each Vector2 as 2 consecutive Float ports
//     (head Widget::Vec, vecArity=2). Eval math is byte-identical; only data model differs.
//   - fork-remapvec2-zerodenom-guard: TiXL C# (inMax-inMin)==0 yields NaN/Inf for factor.
//     This runtime clamps factor to 0.0f when denom==0 (v → outMin). Named fork; in practice
//     inMax!=inMin under normal use so this never fires.
//   - fork-remapvec2-modulo-floor: TiXL uses MathUtils.Fmod which is a positive-modulo
//     (Euclidean fmod: v - delta*floor(v/delta)). std::fmod behaves differently for negative
//     values. This runtime uses floor-based fmod (matching TiXL Fmod semantics).
//   - fork-remapvec2-modulo-zero-delta-guard: TiXL Modulo with delta==0 would div-by-zero.
//     This runtime passes v through unchanged when delta==0. Named fork.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runRemapVec2SelfTest(bool injectBug);

namespace {

// in[] = [Value.x, Value.y, RangeInMin.x, RangeInMin.y, RangeInMax.x, RangeInMax.y,
//         RangeOutMin.x, RangeOutMin.y, RangeOutMax.x, RangeOutMax.y, Mode]  (n=11).
// Result.Value = remap(value, inMin, inMax, outMin, outMax) per component, then apply Mode.
// TiXL RemapVec2.cs lines 24-41 verbatim.
// fork-remapvec2-zerodenom-guard: (inMax-inMin)==0 → factor=0.
// fork-remapvec2-modulo-floor: Fmod uses floor-based division (matches TiXL MathUtils.Fmod).
// fork-remapvec2-modulo-zero-delta-guard: delta==0 → passthrough.
// Component k = outIdx - n: 0=x, 1=y.
float evalRemapVec2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 11) return 0.0f;
  const int k = outIdx - n;  // 0=x, 1=y
  if (k < 0 || k > 1) return 0.0f;
  const float value  = in[k];        // Value.x or Value.y
  const float inMin  = in[2 + k];   // RangeInMin.x or .y
  const float inMax  = in[4 + k];   // RangeInMax.x or .y
  const float outMin = in[6 + k];   // RangeOutMin.x or .y
  const float outMax = in[8 + k];   // RangeOutMax.x or .y
  const int   mode   = (int)std::lround(in[10]);  // Mode enum (Normal=0, Clamped=1, Modulo=2)

  // Lines 24-25: factor = (value - inMin) / (inMax - inMin);  v = factor * (outMax - outMin) + outMin
  const float denom  = inMax - inMin;
  const float factor = (denom != 0.0f) ? (value - inMin) / denom : 0.0f;  // fork-zerodenom-guard
  float v = factor * (outMax - outMin) + outMin;

  if (mode == 1) {
    // Clamped: MathUtils.Clamp(v, outMin, outMax) — TiXL RemapVec2.cs line 31.
    // Assumes outMin < outMax (faithful to TiXL; user-enforced constraint).
    v = v < outMin ? outMin : (v > outMax ? outMax : v);
  } else if (mode == 2) {
    // Modulo: MathUtils.Fmod(v, delta) — TiXL RemapVec2.cs lines 36-38.
    // fork-remapvec2-modulo-floor: Fmod = v - delta * floor(v / delta)  (Euclidean).
    // fork-remapvec2-modulo-zero-delta-guard: delta==0 → passthrough.
    const float delta = outMax - outMin;
    if (delta != 0.0f) v = v - delta * std::floor(v / delta);
    // else: passthrough (delta==0 guard)
  }
  // Normal (mode==0): unclamped passthrough.
  return v;
}

}  // namespace

static const ValueOp _reg_remapvec2{
    // RemapVec2 (TiXL Lib.numbers.vec2.RemapVec2):
    //   factor = (Value - RangeInMin) / (RangeInMax - RangeInMin);
    //   Result = factor * (RangeOutMax - RangeOutMin) + RangeOutMin; then Mode.
    // Port order MUST match evalRemapVec2's in[] read (n=11):
    //   Value.x, Value.y, RangeInMin.x, RangeInMin.y, RangeInMax.x, RangeInMax.y,
    //   RangeOutMin.x, RangeOutMin.y, RangeOutMax.x, RangeOutMax.y, Mode;
    //   then Result.x, Result.y (outputs).
    // Defaults from RemapVec2.t3: Value={0,0}, InMin={0,0}, InMax={1,1}, OutMin={0,0}, OutMax={1,1}, Mode=0.
    // fork-remapvec2-vec2-as-2-floats: each Vector2 port = 2 consecutive Float ports, Widget::Vec.
    {"RemapVec2", "RemapVec2",
     {{"Value.x",       "Value",          "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Value.y",       "Value.y",        "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"RangeInMin.x",  "RangeInMin",     "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"RangeInMin.y",  "RangeInMin.y",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"RangeInMax.x",  "RangeInMax",     "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"RangeInMax.y",  "RangeInMax.y",   "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"RangeOutMin.x", "RangeOutMin",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"RangeOutMin.y", "RangeOutMin.y",  "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"RangeOutMax.x", "RangeOutMax",    "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"RangeOutMax.y", "RangeOutMax.y",  "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Mode",          "Mode",           "Float", true, 0.0f,   0.0f,   2.0f, Widget::Enum,
       {"Normal", "Clamped", "Modulo"}},
      {"Result.x",      "Result.x",       "Float", false},
      {"Result.y",      "Result.y",       "Float", false}},
     evalRemapVec2},
    "remapvec2", runRemapVec2SelfTest};

// --- RemapVec2 MATH golden ---------------------------------------------------------------------
// Builds a 1-node RemapVec2 graph, sets the 11 component params, pulls Result.x / Result.y
// via evalFloat (flat path). Compares against hand-computed TiXL formula values.
// injectBug asserts wrong expected x (0.0f instead of 1.0f) so the typical assertion flips RED.
int runRemapVec2SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalRV2 = [&](float vx, float vy,
                     float inMinX, float inMinY, float inMaxX, float inMaxY,
                     float outMinX, float outMinY, float outMaxX, float outMaxY,
                     float mode, const char* outName) -> float {
    const NodeSpec* spec = findSpec("RemapVec2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "RemapVec2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value.x"]       = vx;
    g.node(nid)->params["Value.y"]       = vy;
    g.node(nid)->params["RangeInMin.x"]  = inMinX;
    g.node(nid)->params["RangeInMin.y"]  = inMinY;
    g.node(nid)->params["RangeInMax.x"]  = inMaxX;
    g.node(nid)->params["RangeInMax.y"]  = inMaxY;
    g.node(nid)->params["RangeOutMin.x"] = outMinX;
    g.node(nid)->params["RangeOutMin.y"] = outMinY;
    g.node(nid)->params["RangeOutMax.x"] = outMaxX;
    g.node(nid)->params["RangeOutMax.y"] = outMaxY;
    g.node(nid)->params["Mode"]          = mode;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL Normal (mode=0): Value=(0.5,0.75), inMin=(0,0), inMax=(1,1), outMin=(0,0), outMax=(2,4).
  // Hand arithmetic:
  //   x: factor=(0.5-0)/(1-0)=0.5; v=0.5*(2-0)+0=1.0
  //   y: factor=(0.75-0)/(1-0)=0.75; v=0.75*(4-0)+0=3.0
  // → Result=(1.0, 3.0).
  // injectBug asserts x==0.0 (wrong) → flips RED.
  {
    float rx = evalRV2(0.5f, 0.75f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 2.f, 4.f, 0.f, "Result.x");
    float want = injectBug ? 0.0f : 1.0f;
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-remapvec2] Normal x=%.5f want=%.5f -> %s\n", rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalRV2(0.5f, 0.75f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 2.f, 4.f, 0.f, "Result.y");
    bool pass = std::fabs(ry - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-remapvec2] Normal y=%.5f want=3.00000 -> %s\n", ry, pass ? "PASS" : "FAIL");
  }

  // CLAMPED (mode=1): Value=(1.5,1.5), inMin=(0,0), inMax=(1,1), outMin=(0,0), outMax=(1,1).
  // Hand arithmetic:
  //   x: factor=(1.5-0)/1=1.5; v=1.5*(1-0)+0=1.5 → clamped to 1.0
  //   y: same → 1.0
  // → Result=(1.0, 1.0).
  {
    float rx = evalRV2(1.5f, 1.5f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f, "Result.x");
    float ry = evalRV2(1.5f, 1.5f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f, "Result.y");
    bool pass = std::fabs(rx - 1.0f) < eps && std::fabs(ry - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-remapvec2] Clamped (1.5→clamp)=(%.5f,%.5f) want=(1,1) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // CLAMPED UNDER: Value=(-0.5,-0.5), same range → factor=-0.5 → v=-0.5 → clamped to 0.0.
  {
    float rx = evalRV2(-0.5f, -0.5f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f, "Result.x");
    bool pass = std::fabs(rx - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-remapvec2] Clamped under (-0.5→0)=%.5f want=0 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  // MODULO (mode=2): Value=(0.5,0.75), inMin=(0,0), inMax=(1,1), outMin=(0,0), outMax=(0.3,0.3).
  // Hand arithmetic:
  //   x: factor=0.5/1=0.5; v_before=0.5*0.3+0=0.15; delta=0.3; Fmod(0.15,0.3)=0.15-0.3*floor(0.5)=0.15
  //   y: factor=0.75; v_before=0.75*0.3=0.225; Fmod(0.225,0.3)=0.225-0.3*floor(0.75)=0.225
  // → Result=(0.15, 0.225).
  {
    float rx = evalRV2(0.5f, 0.75f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.3f, 0.3f, 2.f, "Result.x");
    float ry = evalRV2(0.5f, 0.75f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.3f, 0.3f, 2.f, "Result.y");
    bool passX = std::fabs(rx - 0.15f) < 1e-4f;   // float accumulation may shift slightly
    bool passY = std::fabs(ry - 0.225f) < 1e-4f;
    ok = ok && passX && passY;
    printf("[selftest-remapvec2] Modulo=(%.5f,%.5f) want=(0.15,0.225) -> %s\n",
           rx, ry, (passX && passY) ? "PASS" : "FAIL");
  }

  // T3 DEFAULTS: Value=(0,0), inMin=(0,0), inMax=(1,1), outMin=(0,0), outMax=(1,1), mode=Normal.
  // factor=0/1=0; v=0*(1-0)+0=0 → Result=(0,0).
  {
    float rx = evalRV2(0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, "Result.x");
    float ry = evalRV2(0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 1.f, 1.f, 0.f, "Result.y");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps;
    ok = ok && pass;
    printf("[selftest-remapvec2] defaults (0,0)=(%.5f,%.5f) want=(0,0) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
