// DivideVector2 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/DivideVector2.cs (+ DivideVector2.t3 defaults).
//
//   DivideVector2.cs Update():  (line 16–21)
//     var a = A.GetValue(context);
//     var b = B.GetValue(context);
//     var u = UniformScale.GetValue(context);
//     Result.Value = (a / b) / u;
//
//   Vector2 division is component-wise: Result.x = (A.x / B.x) / U, Result.y = (A.y / B.y) / U.
//   DivideVector2.t3 DefaultValues: A = {X:0, Y:0}, B = {X:1, Y:1}, UniformScale = 1.0.
//
// EVAL-SIDE LAYOUT (flat, no multiInput):
//   in[] = [A.x, A.y, B.x, B.y, UniformScale]  (n=5 inputs).
//   Output ports (Result.x, Result.y) at spec indices n=5 and n+1=6.
//   Component k = outIdx - n ∈ {0=x, 1=y}.
//
// FORKS (named):
//   - fork-dividevector2-vec2-as-2-floats (precedent: fork-addvec2-vec2-as-2-floats): TiXL has
//     native Vector2 ports. This runtime exposes each Vector2 as 2 consecutive Float ports
//     (head Widget::Vec, vecArity=2). Eval math is byte-identical; only data model differs.
//   - fork-dividevector2-zero-guard: TiXL C# performs (a/b)/u with no explicit zero-check; a
//     divide-by-zero in C# yields ±Infinity/NaN (IEEE 754). This runtime returns 0.0f when
//     B.x==0, B.y==0, or UniformScale==0 to avoid propagating NaN/Inf through the graph.
//     Named fork because TiXL would output ±Inf/NaN; we output 0. In practice, TiXL users
//     never set B=0 or U=0 (sliders default B={1,1}, U=1), so this guard is never triggered
//     under normal use and produces no observable parity difference.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runDivideVector2SelfTest(bool injectBug);

namespace {

// in[] = [A.x, A.y, B.x, B.y, UniformScale]  (n=5).
// Result.Value = (A / B) / UniformScale  (TiXL DivideVector2.cs line 20 verbatim).
// fork-dividevector2-zero-guard: B.k==0 or UniformScale==0 → return 0.
// Component k = outIdx - n: 0=x, 1=y.
float evalDivideVector2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 5) return 0.0f;
  const int k = outIdx - n;  // 0=x, 1=y
  if (k < 0 || k > 1) return 0.0f;
  const float a = in[k];        // A.x or A.y
  const float b = in[2 + k];   // B.x or B.y
  const float u = in[4];       // UniformScale
  if (b == 0.0f || u == 0.0f) return 0.0f;  // fork-dividevector2-zero-guard
  return (a / b) / u;
}

}  // namespace

static const ValueOp _reg_dividevector2{
    // DivideVector2 (TiXL Lib.numbers.vec2.DivideVector2):
    //   Result.Value = (A / B) / UniformScale  (componentwise).
    // Port order MUST match evalDivideVector2's in[] read:
    //   A.x, A.y, B.x, B.y, UniformScale (inputs); then Result.x, Result.y (outputs).
    // Defaults from DivideVector2.t3: A = {X:0, Y:0}, B = {X:1, Y:1}, UniformScale = 1.0.
    // fork-dividevector2-vec2-as-2-floats: A and B each exposed as 2 Float ports, Widget::Vec.
    {"DivideVector2", "DivideVector2",
     {{"A.x",          "A",             "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"A.y",          "A.y",           "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"B.x",          "B",             "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"B.y",          "B.y",           "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"UniformScale", "UniformScale",  "Float", true, 1.0f, -100.0f, 100.0f},
      {"Result.x",     "Result.x",      "Float", false},
      {"Result.y",     "Result.y",      "Float", false}},
     evalDivideVector2},
    "dividevector2", runDivideVector2SelfTest};

// --- DivideVector2 MATH golden -----------------------------------------------------------------
// Builds a 1-node DivideVector2 graph, sets params, pulls Result.x / Result.y via evalFloat.
// injectBug asserts wrong expected x (10.0f) so the typical assertion flips RED.
int runDivideVector2SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalDV2 = [&](float ax, float ay, float bx, float by, float u, const char* outName) -> float {
    const NodeSpec* spec = findSpec("DivideVector2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "DivideVector2";
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

  // TYPICAL: A=(6,8), B=(2,4), U=1.
  // Result.x = (6/2)/1 = 3.0.  Result.y = (8/4)/1 = 2.0.
  // injectBug asserts x==10.0 (wrong) → flips RED.
  {
    float rx = evalDV2(6.0f, 8.0f, 2.0f, 4.0f, 1.0f, "Result.x");
    float want = injectBug ? 10.0f : 3.0f;
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-dividevector2] (6/2)/1=%.5f want=%.5f -> %s\n", rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalDV2(6.0f, 8.0f, 2.0f, 4.0f, 1.0f, "Result.y");
    bool pass = std::fabs(ry - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-dividevector2] (8/4)/1=%.5f want=2.00000 -> %s\n", ry, pass ? "PASS" : "FAIL");
  }

  // UNIFORM SCALE: A=(6,8), B=(2,4), U=2.
  // Result.x = (6/2)/2 = 1.5.  Result.y = (8/4)/2 = 1.0.
  {
    float rx = evalDV2(6.0f, 8.0f, 2.0f, 4.0f, 2.0f, "Result.x");
    float ry = evalDV2(6.0f, 8.0f, 2.0f, 4.0f, 2.0f, "Result.y");
    bool pass = std::fabs(rx - 1.5f) < eps && std::fabs(ry - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-dividevector2] (6/2,8/4)/U=2=(%.5f,%.5f) want=(1.5,1) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // T3 DEFAULTS: A=(0,0), B=(1,1), U=1 → Result=(0,0).
  {
    float rx = evalDV2(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, "Result.x");
    float ry = evalDV2(0.0f, 0.0f, 1.0f, 1.0f, 1.0f, "Result.y");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps;
    ok = ok && pass;
    printf("[selftest-dividevector2] defaults A=(0,0)/B=(1,1)/U=1=(%.5f,%.5f) want=(0,0) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // ZERO GUARD (fork-dividevector2-zero-guard): B=(0,1), U=1 → Result.x=0 (guard fires).
  // B.x==0 → (A.x/0)/1 would be Inf in TiXL; we return 0.0f. Named fork.
  {
    float rx = evalDV2(5.0f, 8.0f, 0.0f, 4.0f, 1.0f, "Result.x");
    bool pass = std::fabs(rx - 0.0f) < eps;
    ok = ok && pass;
    printf("[selftest-dividevector2] B.x=0 guard: Result.x=%.5f want=0 (fork-zero-guard) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
