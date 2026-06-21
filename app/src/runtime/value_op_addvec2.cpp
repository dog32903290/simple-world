// AddVec2 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/AddVec2.cs (+ AddVec2.t3 defaults).
//
//   AddVec2.cs Update():  (line 17)
//     Result.Value = Input1.GetValue(context) + Input2.GetValue(context);
//
//   C# Vector2 operator+ : componentwise addition (Input1.X+Input2.X, Input1.Y+Input2.Y).
//   AddVec2.t3 DefaultValues: Input1 = {X:0, Y:0}, Input2 = {X:0, Y:0}.
//
// 2 Vector2 inputs (= 4 Float ports) → 1 Vector2 output (= 2 Float ports). Pure stateless
// value op registered via the ValueOp seam. Mirrors AddVec3/AddVec4 family.
//
// EVAL-SIDE LAYOUT (flat, no multiInput):
//   in[] = [Input1.x, Input1.y, Input2.x, Input2.y]  (n=4 inputs).
//   Output ports (Result.x, Result.y) at spec indices n=4 and n+1=5.
//   Component k = outIdx - n ∈ {0=x, 1=y}.
//
// FORKS (named):
//   - fork-addvec2-vec2-as-2-floats (fork-vec4-decompose-arity precedent): TiXL has native
//     Vector2. This runtime has only Float ports, so each Vector2 is exposed as 2 consecutive
//     Float ports (head Widget::Vec, vecArity=2). NOT an eval fork: math is byte-identical.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runAddVec2SelfTest(bool injectBug);

namespace {

// in[] = [Input1.x, Input1.y, Input2.x, Input2.y]  (n=4).
// Result.Value = Input1 + Input2  (TiXL AddVec2.cs line 17 verbatim).
// Component k = outIdx - n: 0=x, 1=y.
float evalAddVec2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  const int k = outIdx - n;  // 0=x, 1=y
  if (k == 0) return in[0] + in[2];  // Input1.x + Input2.x
  if (k == 1) return in[1] + in[3];  // Input1.y + Input2.y
  return 0.0f;
}

}  // namespace

static const ValueOp _reg_addvec2{
    // AddVec2 (TiXL Lib.numbers.vec2.AddVec2):
    //   Result.Value = Input1 + Input2  (componentwise Vector2 addition).
    // Port order MUST match evalAddVec2's in[] read:
    //   Input1.x, Input1.y, Input2.x, Input2.y (inputs); then Result.x, Result.y (outputs).
    // Defaults from AddVec2.t3: Input1 = {X:0, Y:0}, Input2 = {X:0, Y:0}.
    // fork-addvec2-vec2-as-2-floats: both Vector2 inputs exposed as 2 Float ports, Widget::Vec.
    {"AddVec2", "AddVec2",
     {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false}},
     evalAddVec2},
    "addvec2", runAddVec2SelfTest};

// --- AddVec2 MATH golden -----------------------------------------------------------------------
// Builds a 1-node AddVec2 graph, sets the 4 component params, pulls each output pin
// (Result.x / Result.y) via evalFloat (flat path — no multiInput).
// injectBug asserts a wrong expected x so the typical assertion flips RED.
int runAddVec2SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalAV2 = [&](float ax, float ay, float bx, float by, const char* outName) -> float {
    const NodeSpec* spec = findSpec("AddVec2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AddVec2";
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
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: (1,2) + (3,4) = (4,6).
  // injectBug asserts x==10 (wrong) → flips RED.
  {
    float rx = evalAV2(1.0f, 2.0f, 3.0f, 4.0f, "Result.x");
    float want = injectBug ? 10.0f : 4.0f;
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-addvec2] (1+3).x=%.5f want=%.5f -> %s\n", rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalAV2(1.0f, 2.0f, 3.0f, 4.0f, "Result.y");
    bool pass = std::fabs(ry - 6.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addvec2] (2+4).y=%.5f want=6.00000 -> %s\n", ry, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE: (-1,-2) + (-3,-4) = (-4,-6).
  {
    float rx = evalAV2(-1.0f, -2.0f, -3.0f, -4.0f, "Result.x");
    float ry = evalAV2(-1.0f, -2.0f, -3.0f, -4.0f, "Result.y");
    bool pass = std::fabs(rx - (-4.0f)) < eps && std::fabs(ry - (-6.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-addvec2] (-1-3,-2-4)=(%.5f,%.5f) want=(-4,-6) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): (0,0) + (0,0) = (0,0).
  {
    float rx = evalAV2(0.0f, 0.0f, 0.0f, 0.0f, "Result.x");
    float ry = evalAV2(0.0f, 0.0f, 0.0f, 0.0f, "Result.y");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps;
    ok = ok && pass;
    printf("[selftest-addvec2] (0+0)=(%.5f,%.5f) want=(0,0) t3-defaults -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // MIXED SIGN: (-1.5, 3.0) + (2.5, -1.0) = (1.0, 2.0).
  {
    float rx = evalAV2(-1.5f, 3.0f, 2.5f, -1.0f, "Result.x");
    float ry = evalAV2(-1.5f, 3.0f, 2.5f, -1.0f, "Result.y");
    bool pass = std::fabs(rx - 1.0f) < eps && std::fabs(ry - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addvec2] (-1.5+2.5, 3-1)=(%.5f,%.5f) want=(1,2) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
