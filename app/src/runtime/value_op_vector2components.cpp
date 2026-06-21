// Vector2Components value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/Vector2Components.cs (+ Vector2Components.t3 defaults).
//
//   Vector2Components.cs Update():  (lines 18-21)
//     Vector2 value = Value.GetValue(context);
//     X.Value = value.X;
//     Y.Value = value.Y;
//
//   Vector2Components.t3 DefaultValues: Value={X:0, Y:0}.
//   Port declaration order in .cs: outputs X (line 7), Y (line 9); input Value (line 24).
//
// 1 Vector2 input → 2 scalar Float outputs (X, Y). Decompose op.
// Mirrors Int2Components (value_op_int2components.cpp) but without derived outputs (Length/AR).
//
// EVAL-SIDE LAYOUT (multi-output decompose, mirrors int2components convention):
//   in[] = [Value.x, Value.y]  (n=2 inputs).
//   Output ports (X, Y) at spec indices n=2 and n+1=3.
//   Component k = outIdx - n ∈ {0=X, 1=Y}.
//
// FORKS (named):
//   - fork-vector2components-vec2-as-2-floats (fork-vec4-decompose-arity precedent): TiXL has
//     native Vector2. This runtime exposes the Vector2 input as 2 consecutive Float ports
//     (Widget::Vec, vecArity=2). NOT an eval fork: component mapping is byte-identical.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runVector2ComponentsSelfTest(bool injectBug);

namespace {

// in[] = [Value.x, Value.y]  (n=2).
// X.Value = value.X;  Y.Value = value.Y;  (TiXL Vector2Components.cs lines 19-20 verbatim).
// Component k = outIdx - n: 0=X, 1=Y.
float evalVector2Components(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int k = outIdx - n;
  if (k == 0) return in[0];  // X = value.X
  if (k == 1) return in[1];  // Y = value.Y
  return 0.0f;
}

}  // namespace

static const ValueOp _reg_vector2components{
    // Vector2Components (TiXL Lib.numbers.vec2.Vector2Components):
    //   X = value.X;  Y = value.Y.
    // Port order MUST match evalVector2Components's in[] read:
    //   Value.x, Value.y (inputs); then X, Y (outputs).
    // Input port declaration order matches .cs line 24: Value.
    // Output port order matches .cs lines 7, 9: X then Y.
    // Defaults from Vector2Components.t3: Value={X:0, Y:0}.
    // fork-vector2components-vec2-as-2-floats: Vector2 input exposed as 2 Float ports, Widget::Vec.
    {"Vector2Components", "Vector2Components",
     {{"Value.x", "Value",   "Float", true,  0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Value.y", "Value.y", "Float", true,  0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"X",       "X",       "Float", false},
      {"Y",       "Y",       "Float", false}},
     evalVector2Components},
    "vector2components", runVector2ComponentsSelfTest};

// --- Vector2Components MATH golden ------------------------------------------------------------
// Builds a 1-node Vector2Components graph, sets Value, pulls X and Y via evalFloat (flat path).
// injectBug asserts a wrong Y (uses X value) → flips RED, proving X and Y ports are distinct.
int runVector2ComponentsSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalV2C = [&](float x, float y, const char* outName) -> float {
    const NodeSpec* spec = findSpec("Vector2Components");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Vector2Components";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value.x"] = x;
    g.node(nid)->params["Value.y"] = y;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Value=(7,9) → X=7, Y=9.
  // injectBug asserts Y==7 (swaps with X) → flips RED, proving Y maps to value.Y not value.X.
  {
    float rx = evalV2C(7.0f, 9.0f, "X");
    bool pass = std::fabs(rx - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vector2components] (7,9).X=%.5f want=7.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalV2C(7.0f, 9.0f, "Y");
    float want = injectBug ? 7.0f : 9.0f;  // bug: swap Y with X
    bool pass = std::fabs(ry - want) < eps;
    ok = ok && pass;
    printf("[selftest-vector2components] (7,9).Y=%.5f want=%.5f -> %s\n",
           ry, want, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): Value=(0,0) → X=0, Y=0.
  {
    float rx = evalV2C(0.0f, 0.0f, "X");
    float ry = evalV2C(0.0f, 0.0f, "Y");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps;
    ok = ok && pass;
    printf("[selftest-vector2components] (0,0)=(%.5f,%.5f) want=(0,0) t3-defaults -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE: Value=(-3.5, -7.2) → X=-3.5, Y=-7.2.
  {
    float rx = evalV2C(-3.5f, -7.2f, "X");
    float ry = evalV2C(-3.5f, -7.2f, "Y");
    bool pass = std::fabs(rx - (-3.5f)) < eps && std::fabs(ry - (-7.2f)) < eps;
    ok = ok && pass;
    printf("[selftest-vector2components] (-3.5,-7.2)=(%.5f,%.5f) want=(-3.5,-7.2) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  // X ISOLATE: Value=(5,0) → X=5, Y=0. Proves X maps to value.X.
  {
    float rx = evalV2C(5.0f, 0.0f, "X");
    float ry = evalV2C(5.0f, 0.0f, "Y");
    bool pass = std::fabs(rx - 5.0f) < eps && std::fabs(ry) < eps;
    ok = ok && pass;
    printf("[selftest-vector2components] x-isolate=(%.5f,%.5f) want=(5,0) -> %s\n",
           rx, ry, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
