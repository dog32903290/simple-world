// Vector3Components value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/Vector3Components.cs (+ Vector3Components.t3 defaults).
//
//   Vector3Components.cs Update() (lines 20-25):
//     Vector3 value = Value.GetValue(context);
//     X.Value = value.X;
//     Y.Value = value.Y;
//     Z.Value = value.Z;
//
//   Ports (from Vector3Components.cs):
//     Outputs: X (line 7), Y (line 9), Z (line 11) — float scalars.
//     Input:   Value (line 29) = InputSlot<Vector3>, default {0,0,0}.
//
//   Vector3Components.t3 DefaultValues: Value={X:0, Y:0, Z:0}.
//
// 1 Vector3 input → 3 scalar Float outputs (X, Y, Z). Pure decompose op — no math.
// Mirrors Vector2Components (value_op_vector2components.cpp) extended to 3 components.
//
// EVAL-SIDE LAYOUT (flat path — decompose):
//   in[] = [Value.x, Value.y, Value.z]  (n = 3)
//   Output ports (X, Y, Z) at spec indices 3, 4, 5.
//   Component k = outIdx - n = outIdx - 3: 0=X, 1=Y, 2=Z.
//
// FORKS (named):
//   - fork-vector3components-vec3-as-3-floats (runtime-wide vec3 convention): TiXL has
//     native Vector3 for the Value input. This runtime exposes it as 3 consecutive Float ports
//     (Widget::Vec, vecArity=3). Component mapping is byte-identical.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runVector3ComponentsSelfTest(bool injectBug);

namespace {

// in[] = [Value.x, Value.y, Value.z]  (n = 3).
// X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z.  (Vector3Components.cs lines 22-24)
// Component k = outIdx - n: 0=X, 1=Y, 2=Z.
float evalVector3Components(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const int k = outIdx - n;  // outIdx - 3
  if (k == 0) return in[0];  // X = value.X
  if (k == 1) return in[1];  // Y = value.Y
  if (k == 2) return in[2];  // Z = value.Z
  return 0.0f;
}

}  // namespace

static const ValueOp _reg_vector3components{
    // Vector3Components (TiXL Lib.numbers.vec3.Vector3Components):
    //   X = value.X;  Y = value.Y;  Z = value.Z.
    // Port order MUST match evalVector3Components's in[] read:
    //   Value.x, Value.y, Value.z (inputs); then X, Y, Z (outputs).
    // Defaults from Vector3Components.t3: Value={X:0, Y:0, Z:0}.
    // fork-vector3components-vec3-as-3-floats: Vector3 input as 3 Float ports, Widget::Vec.
    {"Vector3Components", "Vector3Components",
     {{"Value.x", "Value",   "Float", true,  0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Value.y", "Value.y", "Float", true,  0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Value.z", "Value.z", "Float", true,  0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"X",       "X",       "Float", false},
      {"Y",       "Y",       "Float", false},
      {"Z",       "Z",       "Float", false}},
     evalVector3Components},
    "vector3components", runVector3ComponentsSelfTest};

// --- Vector3Components MATH golden ------------------------------------------------------------
// Builds 1-node Vector3Components graph, sets Value, pulls X/Y/Z via evalFloat (flat path).
// Hand-computed from Vector3Components.cs lines 22-24: X=value.X, Y=value.Y, Z=value.Z.
// injectBug asserts wrong Z (uses Y value) → flips RED, proving Z maps to value.Z not value.Y.
int runVector3ComponentsSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalV3C = [&](float x, float y, float z, const char* outName) -> float {
    const NodeSpec* spec = findSpec("Vector3Components");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Vector3Components";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value.x"] = x;
    g.node(nid)->params["Value.y"] = y;
    g.node(nid)->params["Value.z"] = z;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Value=(7,8,9) → X=7, Y=8, Z=9.
  // injectBug: assert Z==8 (confuses Z with Y) → RED (Z is 9, not 8).
  {
    float rx = evalV3C(7.0f, 8.0f, 9.0f, "X");
    bool pass = std::fabs(rx - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vector3components] (7,8,9).X=%.5f want=7.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalV3C(7.0f, 8.0f, 9.0f, "Y");
    bool pass = std::fabs(ry - 8.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vector3components] (7,8,9).Y=%.5f want=8.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalV3C(7.0f, 8.0f, 9.0f, "Z");
    float want = injectBug ? 8.0f : 9.0f;  // bug: swap Z with Y → RED
    bool pass = std::fabs(rz - want) < eps;
    ok = ok && pass;
    printf("[selftest-vector3components] (7,8,9).Z=%.5f want=%.5f -> %s\n",
           rz, want, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): Value=(0,0,0) → X=0, Y=0, Z=0.
  {
    float rx = evalV3C(0.0f, 0.0f, 0.0f, "X");
    float ry = evalV3C(0.0f, 0.0f, 0.0f, "Y");
    float rz = evalV3C(0.0f, 0.0f, 0.0f, "Z");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps && std::fabs(rz) < eps;
    ok = ok && pass;
    printf("[selftest-vector3components] (0,0,0)=(%.5f,%.5f,%.5f) want=(0,0,0) t3-defaults -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE: Value=(-3.5, -7.2, 1.1) → X=-3.5, Y=-7.2, Z=1.1.
  {
    float rx = evalV3C(-3.5f, -7.2f, 1.1f, "X");
    float ry = evalV3C(-3.5f, -7.2f, 1.1f, "Y");
    float rz = evalV3C(-3.5f, -7.2f, 1.1f, "Z");
    bool pass = std::fabs(rx - (-3.5f)) < eps
             && std::fabs(ry - (-7.2f)) < eps
             && std::fabs(rz - 1.1f) < eps;
    ok = ok && pass;
    printf("[selftest-vector3components] (-3.5,-7.2,1.1)=(%.5f,%.5f,%.5f) want=(-3.5,-7.2,1.1) -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  // Z ISOLATE: Value=(0,0,5) → X=0, Y=0, Z=5. Proves Z maps to value.Z.
  {
    float rz = evalV3C(0.0f, 0.0f, 5.0f, "Z");
    bool pass = std::fabs(rz - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vector3components] z-isolate.Z=%.5f want=5.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
