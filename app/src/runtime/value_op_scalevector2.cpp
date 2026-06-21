// ScaleVector2 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/ScaleVector2.cs (+ ScaleVector2.t3 defaults).
//
//   ScaleVector2.cs Update():  (lines 17-21)
//     var a = A.GetValue(context);
//     var b = B.GetValue(context);
//     var u = UniformScale.GetValue(context);
//     Result.Value = a * b * u;
//
//   C# Vector2 operator* : componentwise multiply, then * scalar u.
//   Result.X = a.X * b.X * u;  Result.Y = a.Y * b.Y * u.
//   ScaleVector2.t3 DefaultValues: A = {X:0, Y:0}, B = {X:1, Y:1}, UniformScale = 1.0.
//   (Port declaration order in .cs lines 24-31: A, B, UniformScale.)
//
// 2 Vector2 inputs + 1 scalar input → 1 Vector2 output.
// Ports: A.x, A.y, B.x, B.y, UniformScale → Result.x, Result.y.
//
// EVAL-SIDE LAYOUT (flat, no multiInput):
//   in[] = [A.x, A.y, B.x, B.y, UniformScale]  (n=5 inputs).
//   Output ports (Result.x, Result.y) at spec indices n=5 and n+1=6.
//   Component k = outIdx - n ∈ {0=x, 1=y}.
//
// FORKS (named):
//   - fork-scalevector2-vec2-as-2-floats (fork-vec4-decompose-arity precedent): each Vector2
//     exposed as 2 consecutive Float ports (Widget::Vec, vecArity=2). NOT an eval fork.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runScaleVector2SelfTest(bool injectBug);

namespace {

// in[] = [A.x, A.y, B.x, B.y, UniformScale]  (n=5).
// Result.Value = a * b * u  (TiXL ScaleVector2.cs line 21 verbatim).
// Component k = outIdx - n: 0=x, 1=y.
float evalScaleVector2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const int k = outIdx - n;  // 0=x, 1=y
  const float u = in[4];
  if (k == 0) return in[0] * in[2] * u;  // A.x * B.x * UniformScale
  if (k == 1) return in[1] * in[3] * u;  // A.y * B.y * UniformScale
  return 0.0f;
}

}  // namespace

static const ValueOp _reg_scalevector2{
    // ScaleVector2 (TiXL Lib.numbers.vec2.ScaleVector2):
    //   Result.Value = A * B * UniformScale  (componentwise vec2 × vec2 × scalar).
    // Port order MUST match evalScaleVector2's in[] read:
    //   A.x, A.y, B.x, B.y, UniformScale (inputs); then Result.x, Result.y (outputs).
    // Port declaration order matches .cs lines 24-31: A, B, UniformScale.
    // Defaults from ScaleVector2.t3: A={X:0,Y:0}, B={X:1,Y:1}, UniformScale=1.0.
    // fork-scalevector2-vec2-as-2-floats: Vector2 inputs exposed as 2 Float ports, Widget::Vec.
    {"ScaleVector2", "ScaleVector2",
     {{"A.x",          "A",            "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"A.y",          "A.y",          "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"B.x",          "B",            "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"B.y",          "B.y",          "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"UniformScale", "UniformScale", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Slider},
      {"Result.x",     "Result.x",     "Float", false},
      {"Result.y",     "Result.y",     "Float", false}},
     evalScaleVector2},
    "scalevector2", runScaleVector2SelfTest};

// --- ScaleVector2 MATH golden ------------------------------------------------------------------
// Builds a 1-node ScaleVector2 graph, sets the 5 params, pulls Result.x / Result.y via evalFloat.
// injectBug asserts a wrong Result.x (drops UniformScale term) → flips RED.
int runScaleVector2SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalSV2 = [&](float ax, float ay, float bx, float by, float u,
                      const char* outName) -> float {
    const NodeSpec* spec = findSpec("ScaleVector2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "ScaleVector2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["A.x"]          = ax;
    g.node(nid)->params["A.y"]          = ay;
    g.node(nid)->params["B.x"]          = bx;
    g.node(nid)->params["B.y"]          = by;
    g.node(nid)->params["UniformScale"] = u;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: A=(2,3), B=(4,5), U=2 → x=2*4*2=16, y=3*5*2=30.
  // injectBug asserts x==8 (drops U) → flips RED.
  {
    float rx = evalSV2(2.0f, 3.0f, 4.0f, 5.0f, 2.0f, "Result.x");
    float want = injectBug ? 8.0f : 16.0f;
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector2] (2*4*2).x=%.5f want=%.5f -> %s\n", rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalSV2(2.0f, 3.0f, 4.0f, 5.0f, 2.0f, "Result.y");
    bool pass = std::fabs(ry - 30.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector2] (3*5*2).y=%.5f want=30.00000 -> %s\n", ry, pass ? "PASS" : "FAIL");
  }

  // IDENTITY: A=(5,7), B=(1,1), U=1 → (5,7). Default B+U.
  {
    float rx = evalSV2(5.0f, 7.0f, 1.0f, 1.0f, 1.0f, "Result.x");
    float ry = evalSV2(5.0f, 7.0f, 1.0f, 1.0f, 1.0f, "Result.y");
    bool pass = std::fabs(rx - 5.0f) < eps && std::fabs(ry - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector2] identity (5,7)=(%.5f,%.5f) want=(5,7) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 A defaults): A=(0,0), B=(1,1), U=1 → (0,0).
  {
    float rx = evalSV2(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, "Result.x");
    float ry = evalSV2(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, "Result.y");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector2] zero-A=(%.5f,%.5f) want=(0,0) t3-defaults -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // UNIFORM SCALE ONLY: A=(1,1), B=(1,1), U=3 → (3,3).
  {
    float rx = evalSV2(1.0f, 1.0f, 1.0f, 1.0f, 3.0f, "Result.x");
    float ry = evalSV2(1.0f, 1.0f, 1.0f, 1.0f, 3.0f, "Result.y");
    bool pass = std::fabs(rx - 3.0f) < eps && std::fabs(ry - 3.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector2] uniform-3=(%.5f,%.5f) want=(3,3) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
