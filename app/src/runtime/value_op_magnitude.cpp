// Magnitude value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/Magnitude.cs (verbatim below).
//
//   Magnitude.cs Update() [line 16]:
//     Result.Value = Input.GetValue(context).Length();
//
//   C# Vector3.Length() = sqrt(x^2 + y^2 + z^2).  (3-component Euclidean magnitude → 1 scalar.)
//   Magnitude.t3 DefaultValues: Input = {X:0, Y:0, Z:0}.
//   (Default eval: length((0,0,0)) = 0.)
//
// 1 Vector3 input (= 3 Float ports) → 1 scalar Float output. Pure stateless value op.
//
// NAMED FORK — fork-magnitude-vec3-as-3-floats (same convention as node_registry_math.cpp:800):
//   The Vector3 input is decomposed into 3 consecutive Float ports (head Widget::Vec, vecArity=3).
//   The magnitude math is byte-identical to TiXL; only the host data model differs.
//   in[] = [Input.x, Input.y, Input.z].
//
// NOTE: Magnitude also exists in node_registry_math.cpp / value_eval_ops.cpp (old-style).
// This self-registering leaf coexists and exposes the same spec/eval.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runMagnitudeSelfTest(bool injectBug);

namespace {

// in[] = [Input.x, Input.y, Input.z]  (3 Float ports in spec order).
// Result = sqrt(x^2 + y^2 + z^2)  (TiXL Vector3.Length() verbatim, .cs:16).
float evalMagnitudeNew(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  return std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
}

}  // namespace

static const ValueOp _reg_magnitude{
    // Magnitude (TiXL Lib.numbers.vec3.Magnitude): length(Input), Vec3 = 3 Float ports.
    // Port order MUST match evalMagnitudeNew's in[]: Input.x/.y/.z, then Result.
    // Defaults from Magnitude.t3: all 3 components 0.
    {"Magnitude", "Magnitude",
     {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input.z", "Input.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result",  "Result",  "Float", false}},
     evalMagnitudeNew},
    "magnitude", runMagnitudeSelfTest};

// --- Magnitude MATH golden -----------------------------------------------------------------------
// Builds a 1-node Magnitude graph, sets the 3 Input components, pulls "Result" via evalFloat (flat
// path). Compares to hand-computed sqrt values from TiXL's Vector3.Length().
// injectBug asserts the Y-term-dropped value so the typical assertion flips RED.
int runMagnitudeSelfTest(bool injectBug) {
  const float eps = 1e-4f;  // sqrt results have small rounding; 1e-4 is sufficient for short vecs
  bool ok = true;

  auto evalMag = [&](float x, float y, float z) -> float {
    const NodeSpec* spec = findSpec("Magnitude");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Magnitude";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input.x"] = x;
    g.node(nid)->params["Input.y"] = y;
    g.node(nid)->params["Input.z"] = z;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Magnitude((3,0,4)) = sqrt(9+0+16) = sqrt(25) = 5. Classic 3-4-5 Pythagorean triple.
  //   injectBug asserts Y-term-missing value sqrt(9+16)=sqrt(25)=5… same here.
  //   Use a triangle where dropping Y changes the result:
  //   Magnitude((3,4,0)) = sqrt(9+16) = 5. Drop Y → sqrt(9+0) = 3 → different. Bug: wrong result.
  //   Actually use Magnitude((1,2,3)) = sqrt(1+4+9) = sqrt(14) ≈ 3.7417.
  //   injectBug: drop Z → sqrt(1+4) = sqrt(5) ≈ 2.2361 → clearly different.
  {
    float r = evalMag(1.0f, 2.0f, 3.0f);
    float want = injectBug ? std::sqrt(5.0f) : std::sqrt(14.0f);  // bug: drop Z^2 term
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-magnitude] Magnitude((1,2,3))=%.5f want=%.5f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // PYTHAGOREAN: Magnitude((3,4,0)) = sqrt(9+16) = 5.
  {
    float r = evalMag(3.0f, 4.0f, 0.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-magnitude] Magnitude((3,4,0))=%.5f want=5.00000 (3-4-5) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // UNIT VECTOR: Magnitude((0,0,1)) = 1.
  {
    float r = evalMag(0.0f, 0.0f, 1.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-magnitude] Magnitude((0,0,1))=%.5f want=1.00000 (unit) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // ZERO: Magnitude((0,0,0)) = 0  (t3 default — sqrt(0)=0, no guard needed per fork-magnitude-zero-guard).
  {
    float r = evalMag(0.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-magnitude] Magnitude((0,0,0))=%.5f want=0.00000 (t3 defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // 3D: Magnitude((0,3,4)) = sqrt(0+9+16) = 5 (confirms Y and Z both counted).
  {
    float r = evalMag(0.0f, 3.0f, 4.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-magnitude] Magnitude((0,3,4))=%.5f want=5.00000 (Y+Z) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
