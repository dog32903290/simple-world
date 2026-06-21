// AddVec3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/AddVec3.cs (+ AddVec3.t3 defaults).
//
//   AddVec3.cs Update():
//     Result.Value = Input1.GetValue(context) + Input2.GetValue(context);
//     (AddVec3.cs line 23 — C# Vector3 operator+ is component-wise add.)
//
//   Ports (from AddVec3.cs field order):
//     Input1 = InputSlot<Vector3>   (AddVec3.cs line 31)
//     Input2 = InputSlot<Vector3>   (AddVec3.cs line 34)
//     Output: Result = Slot<Vector3> (AddVec3.cs line 8)
//
//   AddVec3.t3 DefaultValues: Input1 = {X:0,Y:0,Z:0}, Input2 = {X:0,Y:0,Z:0}.
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   Each Vector3 input decomposes into 3 consecutive Float ports (fork-vec3-as-3-floats convention).
//   in[] = [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z]  (n = 6)
//   Output ports Result.x/.y/.z follow at spec indices 6/7/8.
//   Component k = outIdx - n (0=x, 1=y, 2=z).
//   eval: Result[k] = Input1[k] + Input2[k]  (AddVec3.cs line 23).
//
// FORKS (named):
//   - fork-addvec3-vec3-as-3-floats: each Vector3 input/output is 3 consecutive Float ports.
//     eval is byte-identical to TiXL component-wise add. Not an eval fork.
#include "runtime/graph.h"        // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId
#include "runtime/value_eval_ops.h"  // evalAddVec3 (defined in value_eval_ops.cpp)

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runAddVec3SelfTest(bool injectBug);

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited — independent leaf.
static const ValueOp _reg_addvec3{
    // AddVec3 (TiXL Lib.numbers.vec3.AddVec3):
    //   Result = Input1 + Input2 (component-wise Vector3 add).
    // Port order must match evalAddVec3's in[] read: Input1.x/y/z, Input2.x/y/z, Result.x/y/z.
    // Defaults from AddVec3.t3: Input1 = {0,0,0}, Input2 = {0,0,0}.
    {"AddVec3", "AddVec3",
     {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalAddVec3},
    "addvec3", runAddVec3SelfTest};

// --- AddVec3 MATH golden -----------------------------------------------------------------------
// Builds 1-node AddVec3 graph, sets components, pulls Result.x/.y/.z via evalFloat (flat path).
// Hand-computed from AddVec3.cs line 23: Result = Input1 + Input2.
// injectBug asserts a wrong Result.x (uses subtraction instead) → flips RED.
int runAddVec3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalAdd = [&](float i1x, float i1y, float i1z,
                     float i2x, float i2y, float i2z,
                     const char* outPort) -> float {
    const NodeSpec* spec = findSpec("AddVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "AddVec3";
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

  // TYPICAL: AddVec3((1,2,3),(4,5,6)) → (5,7,9).
  // injectBug: claim Result.x = 1-4 = -3 instead of 5. Bug is in the expected value,
  // not the compute — when injectBug, we assert the wrong value so the test FAILS.
  {
    float rx = evalAdd(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, "Result.x");
    float want = injectBug ? -3.0f : 5.0f;  // bug: claim sub instead of add → RED
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-addvec3] Add((1,2,3),(4,5,6)).x=%.5f want=%.5f -> %s\n",
           rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalAdd(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, "Result.y");
    bool pass = std::fabs(ry - 7.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addvec3] Add((1,2,3),(4,5,6)).y=%.5f want=7.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalAdd(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, "Result.z");
    bool pass = std::fabs(rz - 9.0f) < eps;
    ok = ok && pass;
    printf("[selftest-addvec3] Add((1,2,3),(4,5,6)).z=%.5f want=9.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE components: AddVec3((-1,0,3),(1,0,-3)) → (0,0,0).
  {
    float rx = evalAdd(-1.0f, 0.0f, 3.0f, 1.0f, 0.0f, -3.0f, "Result.x");
    float ry = evalAdd(-1.0f, 0.0f, 3.0f, 1.0f, 0.0f, -3.0f, "Result.y");
    float rz = evalAdd(-1.0f, 0.0f, 3.0f, 1.0f, 0.0f, -3.0f, "Result.z");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps && std::fabs(rz) < eps;
    ok = ok && pass;
    printf("[selftest-addvec3] Add((-1,0,3),(1,0,-3))=(%.5f,%.5f,%.5f) want=(0,0,0) -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  // DEFAULTS (t3): AddVec3((0,0,0),(0,0,0)) → (0,0,0).
  {
    float rx = evalAdd(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, "Result.x");
    bool pass = std::fabs(rx) < eps;
    ok = ok && pass;
    printf("[selftest-addvec3] Add(t3 defaults).x=%.5f want=0.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
