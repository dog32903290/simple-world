// NormalizeVector3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/NormalizeVector3.cs (+ NormalizeVector3.t3 defaults).
//
//   NormalizeVector3.cs Update():
//     var a = A.GetValue(context);                   (NormalizeVector3.cs line 19)
//     var length = a.Length();                        (NormalizeVector3.cs line 20)
//     if (length > 0.001f)                            (NormalizeVector3.cs line 21)
//     {
//         a /= length;                                (NormalizeVector3.cs line 23)
//     }
//     var f = Factor.GetValue(context);               (NormalizeVector3.cs line 25)
//     Result.Value = a * f;                           (NormalizeVector3.cs line 26)
//
//   Ports (from NormalizeVector3.cs field order):
//     A      = InputSlot<Vector3>  (NormalizeVector3.cs line 29)
//     Factor = InputSlot<float>    (NormalizeVector3.cs line 32)
//     Output: Result = Slot<Vector3>  (NormalizeVector3.cs line 9)
//
//   NormalizeVector3.t3 DefaultValues: A = {X:0,Y:0,Z:0}, Factor = 1.0.
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   in[] = [A.x, A.y, A.z, Factor]  (n = 4)
//   Output ports Result.x/.y/.z follow at spec indices 4/5/6.
//   Component k = outIdx - n (0=x, 1=y, 2=z).
//   eval: if (length > 0.001f) normalize, then multiply by Factor.
//
// FORKS (named):
//   - fork-normalizevector3-vec3-as-3-floats: Vector3 input/output as 3 Float ports.
//   - fork-normalizevector3-zero-guard: TiXL explicit threshold length > 0.001f
//     (NormalizeVector3.cs line 21). Vectors with length <= 0.001f pass through unchanged (A*Factor).
//     Named: this is faithful to TiXL behavior, not a divergence.
#include "runtime/graph.h"           // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId
#include "runtime/value_eval_ops.h"  // evalNormalizeVector3 (defined in value_eval_ops.cpp)

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runNormalizeVector3SelfTest(bool injectBug);

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited — independent leaf.
static const ValueOp _reg_normalizevector3{
    // NormalizeVector3 (TiXL Lib.numbers.vec3.NormalizeVector3):
    //   if (length(A) > 0.001) normalize(A) then multiply by Factor.
    // Port order must match evalNormalizeVector3's in[] read: A.x/y/z, Factor, Result.x/y/z.
    // Defaults from NormalizeVector3.t3: A = {0,0,0}, Factor = 1.0.
    {"NormalizeVector3", "NormalizeVector3",
     {{"A.x",    "A",      "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 3},
      {"A.y",    "A.y",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"A.z",    "A.z",    "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Factor", "Factor", "Float", true, 1.0f, -10.0f,  10.0f},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalNormalizeVector3},
    "normalizevector3", runNormalizeVector3SelfTest};

// --- NormalizeVector3 MATH golden --------------------------------------------------------------
// Builds 1-node NormalizeVector3 graph, sets components, pulls Result.x/.y/.z via evalFloat.
// Hand-computed from NormalizeVector3.cs lines 20-26:
//   length = sqrt(ax^2+ay^2+az^2); if > 0.001: normalize; then * Factor.
// injectBug asserts wrong Result.x (Factor dropped → normalized only) → flips RED.
int runNormalizeVector3SelfTest(bool injectBug) {
  const float eps = 1e-4f;  // slightly wider for sqrt precision
  bool ok = true;

  auto evalNorm = [&](float ax, float ay, float az, float factor,
                      const char* outPort) -> float {
    const NodeSpec* spec = findSpec("NormalizeVector3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "NormalizeVector3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["A.x"] = ax; g.node(nid)->params["A.y"] = ay;
    g.node(nid)->params["A.z"] = az;
    g.node(nid)->params["Factor"] = factor;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Normalize((3,0,4), Factor=1.0).
  //   length = sqrt(9+0+16) = sqrt(25) = 5; normalized = (0.6,0,0.8); *1 = (0.6,0,0.8).
  // injectBug: Factor dropped → claim x = 0.6 still (degenerate for Factor=1). Use Factor=2.
  {
    float rx = evalNorm(3.0f, 0.0f, 4.0f, 1.0f, "Result.x");
    bool pass = std::fabs(rx - 0.6f) < eps;
    ok = ok && pass;
    printf("[selftest-normalizevector3] Norm((3,0,4),f=1).x=%.5f want=0.60000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalNorm(3.0f, 0.0f, 4.0f, 1.0f, "Result.y");
    bool pass = std::fabs(ry) < eps;
    ok = ok && pass;
    printf("[selftest-normalizevector3] Norm((3,0,4),f=1).y=%.5f want=0.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalNorm(3.0f, 0.0f, 4.0f, 1.0f, "Result.z");
    bool pass = std::fabs(rz - 0.8f) < eps;
    ok = ok && pass;
    printf("[selftest-normalizevector3] Norm((3,0,4),f=1).z=%.5f want=0.80000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // FACTOR: Normalize((3,0,4), Factor=2.0) → (1.2, 0, 1.6).
  //   normalized = (0.6, 0, 0.8); *2 = (1.2, 0, 1.6).
  // injectBug: claim x = 0.6 (Factor dropped = 1.0 assumed) → RED when Factor=2.
  {
    float rx = evalNorm(3.0f, 0.0f, 4.0f, 2.0f, "Result.x");
    float want = injectBug ? 0.6f : 1.2f;  // bug: Factor not applied → RED
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-normalizevector3] Norm((3,0,4),f=2).x=%.5f want=%.5f -> %s\n",
           rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalNorm(3.0f, 0.0f, 4.0f, 2.0f, "Result.z");
    bool pass = std::fabs(rz - 1.6f) < eps;  // 0.8 * 2
    ok = ok && pass;
    printf("[selftest-normalizevector3] Norm((3,0,4),f=2).z=%.5f want=1.60000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // ZERO-GUARD: Normalize((0,0,0), Factor=1) → (0,0,0) (length=0 ≤ 0.001, passthrough * Factor).
  {
    float rx = evalNorm(0.0f, 0.0f, 0.0f, 1.0f, "Result.x");
    bool pass = std::fabs(rx) < eps;
    ok = ok && pass;
    printf("[selftest-normalizevector3] Norm(zero,f=1).x=%.5f want=0.00000 (zero-guard) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  // NEAR-ZERO-GUARD: Normalize((0.0005,0,0), Factor=5) → (0.0025,0,0) (length=0.0005 ≤ 0.001).
  //   TiXL: length <= 0.001 → passthrough → Result = A * Factor = (0.0005*5,0,0) = (0.0025,0,0).
  {
    float rx = evalNorm(0.0005f, 0.0f, 0.0f, 5.0f, "Result.x");
    bool pass = std::fabs(rx - 0.0025f) < eps;
    ok = ok && pass;
    printf("[selftest-normalizevector3] Norm(near-zero,f=5).x=%.6f want=0.00250 (near-zero-guard) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
