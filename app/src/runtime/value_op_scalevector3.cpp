// ScaleVector3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/ScaleVector3.cs (+ ScaleVector3.t3 defaults).
//
//   ScaleVector3.cs Update():
//     var a = A.GetValue(context);              (ScaleVector3.cs line 21)
//     var b = B.GetValue(context);              (ScaleVector3.cs line 22)
//     var u = ScaleUniform.GetValue(context);   (ScaleVector3.cs line 23)
//     Result.Value = a * b * u;                 (ScaleVector3.cs line 24)
//     (C# Vector3 * Vector3 = component-wise multiply; * float = scale all components.)
//
//   Ports (from ScaleVector3.cs field order):
//     A            = InputSlot<Vector3>   (ScaleVector3.cs line 27)
//     B            = InputSlot<Vector3>   (ScaleVector3.cs line 30)
//     ScaleUniform = InputSlot<float>     (ScaleVector3.cs line 33)
//     Output: Result = Slot<Vector3>      (ScaleVector3.cs line 9)
//
//   ScaleVector3.t3 DefaultValues: A = {X:1,Y:1,Z:1}, B = {X:1,Y:1,Z:1}, ScaleUniform = 1.0.
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   Each Vector3 decomposes into 3 consecutive Float ports (fork-vec3-as-3-floats convention).
//   in[] = [A.x, A.y, A.z, B.x, B.y, B.z, ScaleUniform]  (n = 7)
//   Output ports Result.x/.y/.z follow at spec indices 7/8/9.
//   Component k = outIdx - n (0=x, 1=y, 2=z).
//   eval: Result[k] = A[k] * B[k] * ScaleUniform   (ScaleVector3.cs line 24).
//
// FORKS (named):
//   - fork-scalevector3-vec3-as-3-floats: each Vector3 input/output is 3 consecutive Float ports.
//     eval is byte-identical to TiXL component-wise multiply + scalar scale. Not an eval fork.
#include "runtime/graph.h"           // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId
#include "runtime/value_eval_ops.h"  // evalScaleVector3 (defined in value_eval_ops.cpp)

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runScaleVector3SelfTest(bool injectBug);

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited — independent leaf.
static const ValueOp _reg_scalevector3{
    // ScaleVector3 (TiXL Lib.numbers.vec3.ScaleVector3):
    //   Result = A * B * ScaleUniform (component-wise Vector3 multiply then scalar scale).
    // Port order must match evalScaleVector3's in[] read: A.x/y/z, B.x/y/z, ScaleUniform, Result.x/y/z.
    // Defaults from ScaleVector3.t3: A = {1,1,1}, B = {1,1,1}, ScaleUniform = 1.0.
    {"ScaleVector3", "ScaleVector3",
     {{"A.x",          "A",            "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"A.y",          "A.y",          "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"A.z",          "A.z",          "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"B.x",          "B",            "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"B.y",          "B.y",          "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"B.z",          "B.z",          "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"ScaleUniform", "ScaleUniform", "Float", true, 1.0f, -100.0f, 100.0f},
      {"Result.x",     "Result.x",     "Float", false},
      {"Result.y",     "Result.y",     "Float", false},
      {"Result.z",     "Result.z",     "Float", false}},
     evalScaleVector3},
    "scalevector3", runScaleVector3SelfTest};

// --- ScaleVector3 MATH golden ------------------------------------------------------------------
// Builds 1-node ScaleVector3 graph, sets components, pulls Result.x/.y/.z via evalFloat.
// Hand-computed from ScaleVector3.cs line 24: Result = A * B * ScaleUniform.
// injectBug asserts wrong Result.x (ScaleUniform dropped → A.x * B.x only) → flips RED.
int runScaleVector3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalScale = [&](float ax, float ay, float az,
                       float bx, float by, float bz,
                       float u, const char* outPort) -> float {
    const NodeSpec* spec = findSpec("ScaleVector3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "ScaleVector3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["A.x"] = ax; g.node(nid)->params["A.y"] = ay;
    g.node(nid)->params["A.z"] = az;
    g.node(nid)->params["B.x"] = bx; g.node(nid)->params["B.y"] = by;
    g.node(nid)->params["B.z"] = bz;
    g.node(nid)->params["ScaleUniform"] = u;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: ScaleVector3(A=(2,3,4), B=(1,2,0.5), u=3.0)
  //   x = 2*1*3 = 6; y = 3*2*3 = 18; z = 4*0.5*3 = 6.
  // injectBug: drop ScaleUniform → claim x = 2*1 = 2 (wrong, missing *3 factor) → RED.
  {
    float rx = evalScale(2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 0.5f, 3.0f, "Result.x");
    float want = injectBug ? 2.0f : 6.0f;  // bug: ScaleUniform dropped → A.x*B.x only → RED
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector3] Scale(A=(2,3,4),B=(1,2,0.5),u=3).x=%.5f want=%.5f -> %s\n",
           rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalScale(2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 0.5f, 3.0f, "Result.y");
    bool pass = std::fabs(ry - 18.0f) < eps;  // 3*2*3 = 18
    ok = ok && pass;
    printf("[selftest-scalevector3] Scale(A=(2,3,4),B=(1,2,0.5),u=3).y=%.5f want=18.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalScale(2.0f, 3.0f, 4.0f, 1.0f, 2.0f, 0.5f, 3.0f, "Result.z");
    bool pass = std::fabs(rz - 6.0f) < eps;  // 4*0.5*3 = 6
    ok = ok && pass;
    printf("[selftest-scalevector3] Scale(A=(2,3,4),B=(1,2,0.5),u=3).z=%.5f want=6.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // DEFAULTS (t3): A=(1,1,1), B=(1,1,1), ScaleUniform=1 → Result=(1,1,1).
  {
    float rx = evalScale(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, "Result.x");
    bool pass = std::fabs(rx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector3] Scale(t3 defaults).x=%.5f want=1.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  // ZERO ScaleUniform: A=(5,5,5), B=(5,5,5), u=0 → Result=(0,0,0).
  {
    float rx = evalScale(5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 0.0f, "Result.x");
    bool pass = std::fabs(rx) < eps;
    ok = ok && pass;
    printf("[selftest-scalevector3] Scale(u=0).x=%.5f want=0.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
