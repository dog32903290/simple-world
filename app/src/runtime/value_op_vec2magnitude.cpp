// Vec2Magnitude value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/Vec2Magnitude.cs (verbatim below).
// Note: file lives in the vec3/ folder in TiXL despite operating on a Vec2.
//
//   Vec2Magnitude.cs Update() [line 16]:
//     Result.Value = Input.GetValue(context).Length();
//
//   C# Vector2.Length() = sqrt(x^2 + y^2).  (2-component Euclidean magnitude → 1 scalar.)
//   Vec2Magnitude.t3 DefaultValues: Input = {X:0, Y:0}.
//   (Default eval: length((0,0)) = 0.)
//
// 1 Vector2 input (= 2 Float ports) → 1 scalar Float output. Pure stateless value op.
//
// NAMED FORK — fork-vec2magnitude-vec2-as-2-floats (same convention as node_registry_math.cpp:950):
//   The Vector2 input is decomposed into 2 consecutive Float ports (head Widget::Vec, vecArity=2).
//   The magnitude math is byte-identical to TiXL; only the host data model differs.
//   in[] = [Input.x, Input.y].
//
// NOTE: Vec2Magnitude also exists in node_registry_math.cpp / value_eval_ops.cpp (old-style).
// This self-registering leaf coexists and exposes the same spec/eval.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runVec2MagnitudeSelfTest(bool injectBug);

namespace {

// in[] = [Input.x, Input.y]  (2 Float ports in spec order).
// Result = sqrt(x^2 + y^2)  (TiXL Vector2.Length() verbatim, .cs:16).
float evalVec2MagnitudeNew(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  return std::sqrt(in[0] * in[0] + in[1] * in[1]);
}

}  // namespace

static const ValueOp _reg_vec2magnitude{
    // Vec2Magnitude (TiXL Lib.numbers.vec3.Vec2Magnitude): length(Input), Vec2 = 2 Float ports.
    // Port order MUST match evalVec2MagnitudeNew's in[]: Input.x/.y, then Result.
    // Defaults from Vec2Magnitude.t3: both components 0.
    {"Vec2Magnitude", "Vec2Magnitude",
     {{"Input.x", "Input",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"Input.y", "Input.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result",  "Result",  "Float", false}},
     evalVec2MagnitudeNew},
    "vec2magnitude", runVec2MagnitudeSelfTest};

// --- Vec2Magnitude MATH golden -----------------------------------------------------------------------
// Builds a 1-node Vec2Magnitude graph, sets Input.x/.y, pulls "Result" via evalFloat (flat path).
// Compares to hand-computed sqrt values from TiXL's Vector2.Length().
// injectBug asserts the Y-term-dropped value so the typical assertion flips RED.
int runVec2MagnitudeSelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  auto evalMag2 = [&](float x, float y) -> float {
    const NodeSpec* spec = findSpec("Vec2Magnitude");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Vec2Magnitude";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input.x"] = x;
    g.node(nid)->params["Input.y"] = y;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Vec2Magnitude((3,4)) = sqrt(9+16) = 5. Classic 3-4-5 right triangle.
  //   injectBug asserts Y-term-dropped value sqrt(9) = 3 → RED (proves Y actually summed).
  {
    float r = evalMag2(3.0f, 4.0f);
    float want = injectBug ? 3.0f : 5.0f;  // bug: drop Y^2 term → sqrt(9) = 3
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-vec2magnitude] Vec2Magnitude((3,4))=%.5f want=%.5f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // UNIT: Vec2Magnitude((1,0)) = 1.
  {
    float r = evalMag2(1.0f, 0.0f);
    bool pass = std::fabs(r - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vec2magnitude] Vec2Magnitude((1,0))=%.5f want=1.00000 (unit) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // DIAGONAL: Vec2Magnitude((1,1)) = sqrt(2) ≈ 1.41421.
  {
    float r = evalMag2(1.0f, 1.0f);
    float want = std::sqrt(2.0f);
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-vec2magnitude] Vec2Magnitude((1,1))=%.5f want=%.5f (sqrt(2)) -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // ZERO: Vec2Magnitude((0,0)) = 0  (t3 defaults).
  {
    float r = evalMag2(0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-vec2magnitude] Vec2Magnitude((0,0))=%.5f want=0.00000 (t3 defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // Y-ONLY: Vec2Magnitude((0,5)) = 5 (confirms Y port is wired correctly).
  {
    float r = evalMag2(0.0f, 5.0f);
    bool pass = std::fabs(r - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vec2magnitude] Vec2Magnitude((0,5))=%.5f want=5.00000 (Y-only) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
