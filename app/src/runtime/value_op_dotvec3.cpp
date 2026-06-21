// DotVec3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/DotVec3.cs (verbatim below).
//
//   DotVec3.cs Update() [line 22]:
//     Result.Value = Vector3.Dot(Input1.GetValue(context), Input2.GetValue(context));
//
//   C# Vector3.Dot(a, b) = a.X*b.X + a.Y*b.Y + a.Z*b.Z  (3-component dot → 1 scalar).
//   DotVec3.t3 DefaultValues: Input1 = {X:0, Y:0, Z:0}, Input2 = {X:0, Y:0, Z:0}.
//   (Default eval: dot((0,0,0),(0,0,0)) = 0.)
//
// 2 Vector3 inputs (= 6 Float ports) → 1 scalar Float output. Pure stateless value op.
// Mirrors the already-shipped DotVec4 (value_op_dotvec4.cpp), one component narrower.
//
// NAMED FORK — fork-dotvec3-vec3-as-6-floats (same convention as node_registry_math.cpp:809):
//   Each Vector3 input is decomposed into 3 consecutive Float ports (head Widget::Vec, vecArity=3;
//   ids "<base>.x/.y/.z"). The dot product math is byte-identical to TiXL; only the host data
//   model differs (6 Float wires vs 2 Vec3).
//   in[] = [Input1.x, Input1.y, Input1.z, Input2.x, Input2.y, Input2.z].
//
// NOTE: DotVec3 also exists in node_registry_math.cpp (old-style) and value_eval_ops.cpp.
// This self-registering leaf coexists; findSpec() returns whichever is found first in the
// combined spec list. Both define the same math, same port shape — result is identical.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runDotVec3SelfTest(bool injectBug);

namespace {

// in[] = [Input1.x, .y, .z, Input2.x, .y, .z]  (6 Float ports in spec order).
// Result = dot(Input1, Input2) = x1*x2 + y1*y2 + z1*z2  (TiXL Vector3.Dot verbatim, .cs:22).
float evalDotVec3New(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 6) return 0.0f;
  return in[0] * in[3] + in[1] * in[4] + in[2] * in[5];
}

}  // namespace

static const ValueOp _reg_dotvec3{
    // DotVec3 (TiXL Lib.numbers.vec3.DotVec3): dot(Input1, Input2), each Vec3 = 3 Float ports.
    // Port order MUST match evalDotVec3New's in[]: Input1.x/.y/.z then Input2.x/.y/.z, then Result.
    // Defaults from DotVec3.t3: all 6 components 0.
    {"DotVec3", "DotVec3",
     {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result",   "Result",   "Float", false}},
     evalDotVec3New},
    "dotvec3", runDotVec3SelfTest};

// --- DotVec3 MATH golden -----------------------------------------------------------------------
// Builds a 1-node DotVec3 graph, sets the 6 component params, pulls "Result" via evalFloat (flat
// path — no multiInput). Compares to hand-computed Vector3.Dot values.
// injectBug asserts the Z-term-dropped value so the typical assertion flips RED.
int runDotVec3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalDot = [&](float ax, float ay, float az,
                     float bx, float by, float bz) -> float {
    const NodeSpec* spec = findSpec("DotVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "DotVec3";
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

  // TYPICAL: dot((1,2,3),(4,5,6)) = 1*4 + 2*5 + 3*6 = 4+10+18 = 32.
  //   injectBug asserts the Z-term-dropped value (4+10 = 14) → flips RED, proves Z actually summed.
  {
    float r = evalDot(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);
    float want = injectBug ? 14.0f : 32.0f;  // bug: drop the Z product (3*6=18)
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec3] Dot((1,2,3),(4,5,6))=%.5f want=%.5f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // PERPENDICULAR: dot((1,0,0),(0,1,0)) = 0.
  {
    float r = evalDot(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec3] Dot((1,0,0),(0,1,0))=%.5f want=0.00000 (perp) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // PARALLEL (self-dot gives length squared): dot((3,4,0),(3,4,0)) = 9+16+0 = 25.
  {
    float r = evalDot(3.0f, 4.0f, 0.0f, 3.0f, 4.0f, 0.0f);
    bool pass = std::fabs(r - 25.0f) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec3] Dot((3,4,0),(3,4,0))=%.5f want=25.00000 (self) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE components: dot((-1,2,-3),(5,-6,7)) = -5 -12 -21 = -38.
  {
    float r = evalDot(-1.0f, 2.0f, -3.0f, 5.0f, -6.0f, 7.0f);
    bool pass = std::fabs(r - (-38.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec3] Dot((-1,2,-3),(5,-6,7))=%.5f want=-38.00000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY (DotVec3.t3 defaults): dot((0,0,0),(0,0,0)) = 0.
  {
    float r = evalDot(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec3] Dot(0,0)=%.5f want=0.00000 (t3 defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
