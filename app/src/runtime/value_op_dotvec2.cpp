// DotVec2 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/DotVec2.cs (+ DotVec2.t3 defaults).
//
//   DotVec2.cs Update():  (line 22)
//     Result.Value = Vector2.Dot(Input1.GetValue(context), Input2.GetValue(context));
//
//   C# Vector2.Dot(a, b) = a.X*b.X + a.Y*b.Y  (2-component dot → 1 scalar float).
//   DotVec2.t3 DefaultValues: Input1 = {X:0, Y:0}, Input2 = {X:0, Y:0}.
//   Port declaration order in .cs lines 7-13: Input1 (bac6bef8), Input2 (3f28ec3a).
//
// 2 Vector2 inputs (= 4 Float ports) → 1 scalar Float output. Pure stateless value op;
// mirrors DotVec4 (value_op_dotvec4.cpp) reduced to 2 components.
//
// EVAL-SIDE LAYOUT (flat, no multiInput, single scalar output):
//   in[] = [Input1.x, Input1.y, Input2.x, Input2.y]  (n=4 inputs).
//   Single output "Result" at spec index 4.
//
// FORKS (named):
//   - fork-dotvec2-vec2-as-2-floats (fork-dotvec4-vec4-as-8-floats precedent): each Vector2
//     exposed as 2 consecutive Float ports (Widget::Vec, vecArity=2). NOT an eval fork.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runDotVec2SelfTest(bool injectBug);

namespace {

// in[] = [Input1.x, Input1.y, Input2.x, Input2.y]  (n=4).
// Result = Vector2.Dot(Input1, Input2) = Input1.x*Input2.x + Input1.y*Input2.y.
// (TiXL DotVec2.cs line 22 verbatim; outIdx unused — single scalar output.)
float evalDotVec2(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  return in[0] * in[2] + in[1] * in[3];
}

}  // namespace

static const ValueOp _reg_dotvec2{
    // DotVec2 (TiXL Lib.numbers.vec2.DotVec2): dot(Input1, Input2), each Vec2 = 2 Float ports.
    // Port order MUST match evalDotVec2's in[] read:
    //   Input1.x, Input1.y, Input2.x, Input2.y (inputs); then Result (output).
    // Port declaration order matches .cs: Input1 (line 8), Input2 (line 11), Result output (line 12-13).
    // Defaults from DotVec2.t3: Input1={X:0,Y:0}, Input2={X:0,Y:0}.
    // fork-dotvec2-vec2-as-2-floats: both Vector2 inputs exposed as 2 Float ports, Widget::Vec.
    {"DotVec2", "DotVec2",
     {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result",   "Result",   "Float", false}},
     evalDotVec2},
    "dotvec2", runDotVec2SelfTest};

// --- DotVec2 MATH golden -----------------------------------------------------------------------
// Builds a 1-node DotVec2 graph, sets the 4 component params, pulls "Result" via evalFloat.
// injectBug asserts a wrong value (only X-term, drops Y) → flips RED.
int runDotVec2SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalDot = [&](float ax, float ay, float bx, float by) -> float {
    const NodeSpec* spec = findSpec("DotVec2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "DotVec2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input1.x"] = ax;
    g.node(nid)->params["Input1.y"] = ay;
    g.node(nid)->params["Input2.x"] = bx;
    g.node(nid)->params["Input2.y"] = by;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: dot((1,2),(3,4)) = 1*3 + 2*4 = 3+8 = 11.
  // injectBug asserts 3 (only X-term, drops 2*4=8) → flips RED.
  {
    float r = evalDot(1.0f, 2.0f, 3.0f, 4.0f);
    float want = injectBug ? 3.0f : 11.0f;
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec2] dot((1,2),(3,4))=%.5f want=%.5f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // PERPENDICULAR: dot((1,0),(0,1)) = 0.
  {
    float r = evalDot(1.0f, 0.0f, 0.0f, 1.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec2] dot((1,0),(0,1))=%.5f want=0.00000 (perp) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // Y-AXIS ONLY: dot((0,3),(0,2)) = 6. Isolates the Y term.
  {
    float r = evalDot(0.0f, 3.0f, 0.0f, 2.0f);
    bool pass = std::fabs(r - 6.0f) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec2] dot((0,3),(0,2))=%.5f want=6.00000 (Y-axis) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE: dot((-1,2),(-3,-4)) = (-1*-3)+(2*-4) = 3-8 = -5.
  {
    float r = evalDot(-1.0f, 2.0f, -3.0f, -4.0f);
    bool pass = std::fabs(r - (-5.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec2] dot((-1,2),(-3,-4))=%.5f want=-5.00000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): dot((0,0),(0,0)) = 0.
  {
    float r = evalDot(0.0f, 0.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec2] dot(0,0)=%.5f want=0.00000 (t3 defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
