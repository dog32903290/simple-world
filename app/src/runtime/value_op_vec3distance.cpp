// Vec3Distance value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/Vec3Distance.cs (verbatim below).
//
//   Vec3Distance.cs Update() [line 16]:
//     Result.Value = Vector3.Distance(Input1.GetValue(context), Input2.GetValue(context));
//
//   C# Vector3.Distance(a, b) = sqrt((a.X-b.X)^2 + (a.Y-b.Y)^2 + (a.Z-b.Z)^2).
//   (Euclidean distance between two 3D points → 1 scalar.)
//   Vec3Distance.t3 DefaultValues: Input1 = {X:0, Y:0, Z:0}, Input2 = {X:0, Y:0, Z:0}.
//   (Default eval: distance((0,0,0),(0,0,0)) = 0.)
//
// 2 Vector3 inputs (= 6 Float ports) → 1 scalar Float output. Pure stateless value op.
//
// NAMED FORK — fork-vec3distance-vec3-as-6-floats (same convention as node_registry_math.cpp:821):
//   Each Vector3 input is decomposed into 3 consecutive Float ports (head Widget::Vec, vecArity=3).
//   The distance math is byte-identical to TiXL; only the host data model differs.
//   in[] = [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z].
//
// NOTE: Vec3Distance also exists in node_registry_math.cpp / value_eval_ops.cpp (old-style).
// This self-registering leaf coexists and exposes the same spec/eval.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runVec3DistanceSelfTest(bool injectBug);

namespace {

// in[] = [Input1.x, .y, .z, Input2.x, .y, .z]  (6 Float ports in spec order).
// Result = sqrt((x1-x2)^2 + (y1-y2)^2 + (z1-z2)^2)  (TiXL Vector3.Distance verbatim, .cs:16).
float evalVec3DistanceNew(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  float dx = in[0] - in[3], dy = in[1] - in[4], dz = in[2] - in[5];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

}  // namespace

static const ValueOp _reg_vec3distance{
    // Vec3Distance (TiXL Lib.numbers.vec3.Vec3Distance): distance(Input1, Input2), each Vec3 = 3 Float ports.
    // Port order MUST match evalVec3DistanceNew's in[]: Input1.x/.y/.z then Input2.x/.y/.z, then Result.
    // Defaults from Vec3Distance.t3: all 6 components 0.
    {"Vec3Distance", "Vec3Distance",
     {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result",   "Result",   "Float", false}},
     evalVec3DistanceNew},
    "vec3distance", runVec3DistanceSelfTest};

// --- Vec3Distance MATH golden -----------------------------------------------------------------------
// Builds a 1-node Vec3Distance graph, sets the 6 component params, pulls "Result" via evalFloat.
// Compares to hand-computed Vector3.Distance values.
// injectBug asserts a wrong value (Z-delta dropped) so the typical assertion flips RED.
int runVec3DistanceSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  auto evalDist = [&](float ax, float ay, float az,
                      float bx, float by, float bz) -> float {
    const NodeSpec* spec = findSpec("Vec3Distance");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Vec3Distance";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input1.x"] = ax; g.node(nid)->params["Input1.y"] = ay;
    g.node(nid)->params["Input1.z"] = az;
    g.node(nid)->params["Input2.x"] = bx; g.node(nid)->params["Input2.y"] = by;
    g.node(nid)->params["Input2.z"] = bz;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Vec3Distance((0,0,0),(0,3,4)) = sqrt(0+9+16) = sqrt(25) = 5.
  //   injectBug asserts Z-delta-dropped value sqrt(0+9) = 3 → RED (proves Z delta counted).
  {
    float r = evalDist(0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 4.0f);
    float want = injectBug ? 3.0f : 5.0f;  // bug: drop Z delta → sqrt(9) = 3
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-vec3distance] Vec3Distance((0,0,0),(0,3,4))=%.5f want=%.5f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // SELF-DISTANCE: Vec3Distance(p, p) = 0 for any p.
  {
    float r = evalDist(1.0f, 2.0f, 3.0f, 1.0f, 2.0f, 3.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-vec3distance] Vec3Distance(p,p)=%.5f want=0.00000 (self) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // AXIS X ONLY: Vec3Distance((0,0,0),(5,0,0)) = 5.
  {
    float r = evalDist(0.0f, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vec3distance] Vec3Distance((0,0,0),(5,0,0))=%.5f want=5.00000 (X-axis) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE COORDS: Vec3Distance((1,2,3),(-2,-2,-1)) = sqrt((3)^2+(4)^2+(4)^2) = sqrt(41) ≈ 6.4031.
  {
    float r = evalDist(1.0f, 2.0f, 3.0f, -2.0f, -2.0f, -1.0f);
    float want = std::sqrt(9.0f + 16.0f + 16.0f);  // sqrt(41)
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-vec3distance] Vec3Distance((1,2,3),(-2,-2,-1))=%.5f want=%.5f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY (t3 defaults): Vec3Distance((0,0,0),(0,0,0)) = 0.
  {
    float r = evalDist(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-vec3distance] Vec3Distance(0,0)=%.5f want=0.00000 (t3 defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
