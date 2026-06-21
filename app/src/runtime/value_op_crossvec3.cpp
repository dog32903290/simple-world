// CrossVec3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/CrossVec3.cs (+ CrossVec3.t3 defaults).
//
//   CrossVec3.cs Update():
//     Result.Value = Vector3.Cross(Input1.GetValue(context), Input2.GetValue(context));
//     (CrossVec3.cs line 14 — C# System.Numerics.Vector3.Cross, right-hand rule.)
//
//   C# Vector3.Cross(a, b):
//     cross.X = a.Y*b.Z - a.Z*b.Y   (CrossVec3.cs line 14, .NET System.Numerics.Vector3 contract)
//     cross.Y = a.Z*b.X - a.X*b.Z
//     cross.Z = a.X*b.Y - a.Y*b.X
//
//   Ports (from CrossVec3.cs field order):
//     Input1 = InputSlot<System.Numerics.Vector3>  (CrossVec3.cs line 16)
//     Input2 = InputSlot<System.Numerics.Vector3>  (CrossVec3.cs line 19)
//     Output: Result = Slot<System.Numerics.Vector3> (CrossVec3.cs line 22)
//
//   CrossVec3.t3 DefaultValues: Input1 = {X:0,Y:0,Z:0}, Input2 = {X:0,Y:0,Z:0}.
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   Each Vector3 decomposes into 3 consecutive Float ports (fork-vec3-as-3-floats convention).
//   in[] = [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]  (n = 6)
//   Output ports Result.x/.y/.z follow at spec indices 6/7/8.
//   Component k = outIdx - n (0=x, 1=y, 2=z).
//   eval: cross product per right-hand rule  (CrossVec3.cs line 14).
//
// FORKS (named):
//   - fork-crossvec3-vec3-as-3-floats: each Vector3 input/output is 3 consecutive Float ports.
//     cross product formula is byte-identical to TiXL C# Vector3.Cross. Not an eval fork.
#include "runtime/graph.h"           // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId
#include "runtime/value_eval_ops.h"  // evalCrossVec3 (defined in value_eval_ops.cpp)

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runCrossVec3SelfTest(bool injectBug);

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited — independent leaf.
static const ValueOp _reg_crossvec3{
    // CrossVec3 (TiXL Lib.numbers.vec3.CrossVec3):
    //   Result = Vector3.Cross(Input1, Input2) — right-hand rule cross product.
    // Port order must match evalCrossVec3's in[] read: Input1.x/y/z, Input2.x/y/z, Result.x/y/z.
    // Defaults from CrossVec3.t3: Input1 = {0,0,0}, Input2 = {0,0,0}.
    {"CrossVec3", "CrossVec3",
     {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalCrossVec3},
    "crossvec3", runCrossVec3SelfTest};

// --- CrossVec3 MATH golden ---------------------------------------------------------------------
// Builds 1-node CrossVec3 graph, sets components, pulls Result.x/.y/.z via evalFloat.
// Hand-computed from CrossVec3.cs line 14 (C# Vector3.Cross right-hand rule):
//   cross.X = a.Y*b.Z - a.Z*b.Y
//   cross.Y = a.Z*b.X - a.X*b.Z
//   cross.Z = a.X*b.Y - a.Y*b.X
// injectBug asserts wrong Result.x (sign flipped: a.Z*b.Y - a.Y*b.Z) → flips RED.
int runCrossVec3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalCross = [&](float i1x, float i1y, float i1z,
                       float i2x, float i2y, float i2z,
                       const char* outPort) -> float {
    const NodeSpec* spec = findSpec("CrossVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "CrossVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input1.x"] = i1x; g.node(nid)->params["Input1.y"] = i1y;
    g.node(nid)->params["Input1.z"] = i1z;
    g.node(nid)->params["Input2.x"] = i2x; g.node(nid)->params["Input2.y"] = i2y;
    g.node(nid)->params["Input2.z"] = i2z;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // BASIS VECTOR: Cross((1,0,0),(0,1,0)) → (0,0,1).
  //   x = 0*0 - 0*1 = 0; y = 0*0 - 1*0 = 0; z = 1*1 - 0*0 = 1.
  // injectBug: flip x sign → claim Result.x = -(0*0 - 0*1) = 0 → same here (degenerate).
  // Use a better case below for the actual bite.
  {
    float rx = evalCross(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, "Result.x");
    float ry = evalCross(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, "Result.y");
    float rz = evalCross(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, "Result.z");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps && std::fabs(rz - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-crossvec3] Cross((1,0,0),(0,1,0))=(%.5f,%.5f,%.5f) want=(0,0,1) -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  // GENERAL: Cross((1,2,3),(4,5,6)) → (2*6-3*5, 3*4-1*6, 1*5-2*4) = (-3,6,-3).
  // injectBug: flip Result.x sign → claim 3 instead of -3 → RED.
  {
    float rx = evalCross(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, "Result.x");
    float want = injectBug ? 3.0f : -3.0f;  // bug: flip x sign → RED
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-crossvec3] Cross((1,2,3),(4,5,6)).x=%.5f want=%.5f -> %s\n",
           rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalCross(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, "Result.y");
    bool pass = std::fabs(ry - 6.0f) < eps;  // 3*4 - 1*6 = 6
    ok = ok && pass;
    printf("[selftest-crossvec3] Cross((1,2,3),(4,5,6)).y=%.5f want=6.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalCross(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, "Result.z");
    bool pass = std::fabs(rz + 3.0f) < eps;  // 1*5 - 2*4 = -3
    ok = ok && pass;
    printf("[selftest-crossvec3] Cross((1,2,3),(4,5,6)).z=%.5f want=-3.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // ANTI-COMMUTATIVE: Cross(B,A) = -Cross(A,B). Cross((4,5,6),(1,2,3)) → (3,-6,3).
  {
    float rx = evalCross(4.0f, 5.0f, 6.0f, 1.0f, 2.0f, 3.0f, "Result.x");
    bool pass = std::fabs(rx - 3.0f) < eps;  // -(2*6-3*5) negated = 3
    ok = ok && pass;
    printf("[selftest-crossvec3] Cross((4,5,6),(1,2,3)).x=%.5f want=3.00000 (anti-comm) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
