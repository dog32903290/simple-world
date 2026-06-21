// LerpVec3 value op (value-op self-registration seam leaf — Phase C numbers/vec3 mining).
// TiXL authority: Operators/Lib/numbers/vec3/LerpVec3.cs (+ LerpVec3.t3 defaults).
//
//   LerpVec3.cs Update() (lines 17-23):
//     var f = F.GetValue(context);
//     if (Clamp.GetValue(context)) { f = f.Clamp(0, 1); }
//     Result.Value = Vector3.Lerp(A.GetValue(context), B.GetValue(context), f);
//
//   C# Vector3.Lerp(a, b, t) = a + (b - a) * t  (standard linear interpolation).
//   .Clamp(0,1) is MathUtils.Clamp, equivalent to std::clamp (does NOT affect true-value, only f).
//
//   Ports (from LerpVec3.cs field declaration order):
//     A      = InputSlot<Vector3>  (line 28)  — start vec, default {0,0,0}
//     B      = InputSlot<Vector3>  (line 31)  — end vec,   default {0,0,0}
//     F      = InputSlot<float>    (line 34)  — blend factor, default 0.0
//     Clamp  = InputSlot<bool>     (line 37)  — clamp f to [0,1], default false
//   Output:
//     Result = Slot<Vector3>       (line 8)   — interpolated vec3
//
//   LerpVec3.t3 DefaultValues (from .t3): A={0,0,0}, B={0,0,0}, F=0.0, Clamp=false.
//
// EVAL-SIDE LAYOUT (flat path — no multiInput):
//   Each Vector3 input decomposes into 3 consecutive Float ports (fork-vec3-as-3-floats convention).
//   Clamp is a Bool-as-Float port (0.0f = false, non-zero = true).
//   in[] = [A.x, A.y, A.z, B.x, B.y, B.z, F, Clamp]  (n = 8)
//   Output ports Result.x/.y/.z follow at spec indices 8/9/10.
//   Component k = outIdx - 8 (0=x, 1=y, 2=z).
//   eval (LerpVec3.cs lines 17-23):
//     f = F; if (Clamp != 0) f = clamp(f, 0, 1);
//     Result[k] = A[k] + (B[k] - A[k]) * f.
//
// FORKS (named):
//   - fork-lerpvec3-vec3-as-3-floats: each Vector3 (A, B, Result) is 3 Float ports.
//     eval is byte-identical to TiXL Vector3.Lerp(A, B, f) per component. Not an eval fork.
//   - fork-lerpvec3-bool-as-float: Clamp is InputSlot<bool> in TiXL but Float in this runtime
//     (Cut-32 runtime-wide bool-as-Float contract). Truthy = non-zero. Not an eval fork.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <algorithm>  // std::clamp
#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runLerpVec3SelfTest(bool injectBug);

namespace {

// in[] = [A.x, A.y, A.z, B.x, B.y, B.z, F, Clamp]  (n = 8).
// Component k = outIdx - 8: 0=x, 1=y, 2=z.
// LerpVec3.cs line 23: Result = Vector3.Lerp(A, B, f).
// With optional clamp of f to [0,1] when Clamp is truthy (line 19-21).
float evalLerpVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 8) return 0.0f;
  const int k = outIdx - 8;  // 0=x, 1=y, 2=z
  if (k < 0 || k > 2) return 0.0f;
  float f = in[6];                    // F (line 18)
  const bool doClamp = (in[7] != 0.0f);  // Clamp (line 19, bool-as-Float)
  if (doClamp) f = std::clamp(f, 0.0f, 1.0f);  // .Clamp(0,1) (line 20)
  const float a = in[k];      // A[k]
  const float b = in[3 + k];  // B[k]
  return a + (b - a) * f;  // Vector3.Lerp (line 23)
}

}  // namespace

static const ValueOp _reg_lerpvec3{
    // LerpVec3 (TiXL Lib.numbers.vec3.LerpVec3):
    //   Result = Vector3.Lerp(A, B, f)  [optionally clamping f to [0,1]].
    // Port order matches in[] read: A.x/y/z, B.x/y/z, F, Clamp, then Result.x/y/z.
    // Defaults from LerpVec3.t3: A={0,0,0}, B={0,0,0}, F=0.0, Clamp=false (0.0f).
    // fork-lerpvec3-vec3-as-3-floats: A and B each become 3 Float ports (Widget::Vec, vecArity=3).
    // fork-lerpvec3-bool-as-float: Clamp encoded as Float 0/1, Widget::Bool.
    {"LerpVec3", "LerpVec3",
     {{"A.x",      "A",        "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,  {}, false, 3},
      {"A.y",      "A.y",      "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,  {}, false, 1},
      {"A.z",      "A.z",      "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,  {}, false, 1},
      {"B.x",      "B",        "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,  {}, false, 3},
      {"B.y",      "B.y",      "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,  {}, false, 1},
      {"B.z",      "B.z",      "Float", true,  0.0f,  -100.0f, 100.0f, Widget::Vec,  {}, false, 1},
      {"F",        "F",        "Float", true,  0.0f,  -10.0f,  10.0f,  Widget::Slider},
      {"Clamp",    "Clamp",    "Float", true,  0.0f,  0.0f,    1.0f,   Widget::Bool},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalLerpVec3},
    "lerpvec3", runLerpVec3SelfTest};

// --- LerpVec3 MATH golden -----------------------------------------------------------------------
// Builds 1-node LerpVec3 graph, sets components, pulls Result.x/.y/.z via evalFloat (flat path).
// Hand-computed from LerpVec3.cs lines 17-23: Result = A + (B-A)*f (with optional clamp).
// injectBug asserts wrong Result.x (uses A instead of Lerp) → flips RED.
int runLerpVec3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalLerp = [&](float ax, float ay, float az,
                      float bx, float by, float bz,
                      float f, float clampF,
                      const char* outPort) -> float {
    const NodeSpec* spec = findSpec("LerpVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "LerpVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["A.x"] = ax; g.node(nid)->params["A.y"] = ay;
    g.node(nid)->params["A.z"] = az;
    g.node(nid)->params["B.x"] = bx; g.node(nid)->params["B.y"] = by;
    g.node(nid)->params["B.z"] = bz;
    g.node(nid)->params["F"]     = f;
    g.node(nid)->params["Clamp"] = clampF;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outPort) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // CASE 1: Lerp((0,0,0),(10,20,30), F=0.5, Clamp=false).
  //   x = 0 + (10-0)*0.5 = 5
  //   y = 0 + (20-0)*0.5 = 10
  //   z = 0 + (30-0)*0.5 = 15
  // injectBug: claim Result.x = A.x = 0 instead of 5 → RED.
  {
    float rx = evalLerp(0,0,0, 10,20,30, 0.5f, 0.0f, "Result.x");
    float want = injectBug ? 0.0f : 5.0f;  // bug: use A.x instead of Lerp → RED
    bool pass = std::fabs(rx - want) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((0,0,0),(10,20,30),0.5).x=%.5f want=%.5f -> %s\n",
           rx, want, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalLerp(0,0,0, 10,20,30, 0.5f, 0.0f, "Result.y");
    bool pass = std::fabs(ry - 10.0f) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((0,0,0),(10,20,30),0.5).y=%.5f want=10.00000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalLerp(0,0,0, 10,20,30, 0.5f, 0.0f, "Result.z");
    bool pass = std::fabs(rz - 15.0f) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((0,0,0),(10,20,30),0.5).z=%.5f want=15.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // CASE 2: Lerp((2,4,6),(8,10,12), F=0.25, Clamp=false).
  //   x = 2 + (8-2)*0.25 = 2 + 1.5 = 3.5
  //   y = 4 + (10-4)*0.25 = 4 + 1.5 = 5.5
  //   z = 6 + (12-6)*0.25 = 6 + 1.5 = 7.5
  {
    float rx = evalLerp(2,4,6, 8,10,12, 0.25f, 0.0f, "Result.x");
    bool pass = std::fabs(rx - 3.5f) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((2,4,6),(8,10,12),0.25).x=%.5f want=3.50000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalLerp(2,4,6, 8,10,12, 0.25f, 0.0f, "Result.y");
    bool pass = std::fabs(ry - 5.5f) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((2,4,6),(8,10,12),0.25).y=%.5f want=5.50000 -> %s\n",
           ry, pass ? "PASS" : "FAIL");
  }

  // CASE 3: Clamp=true with F=1.5 → f clamped to 1.0 → Result = B = (8,10,12).
  //   x = 2 + (8-2)*1.0 = 8; y = 4 + (10-4)*1.0 = 10; z = 6 + (12-6)*1.0 = 12.
  {
    float rx = evalLerp(2,4,6, 8,10,12, 1.5f, 1.0f, "Result.x");
    bool pass = std::fabs(rx - 8.0f) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((2,4,6),(8,10,12),f=1.5,clamp=true).x=%.5f want=8.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalLerp(2,4,6, 8,10,12, 1.5f, 1.0f, "Result.z");
    bool pass = std::fabs(rz - 12.0f) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((2,4,6),(8,10,12),f=1.5,clamp=true).z=%.5f want=12.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // CASE 4: Clamp=false with F=1.5 → extrapolation.
  //   x = 2 + (8-2)*1.5 = 2 + 9 = 11; y = 4 + 6*1.5 = 13; z = 6 + 6*1.5 = 15.
  {
    float rx = evalLerp(2,4,6, 8,10,12, 1.5f, 0.0f, "Result.x");
    bool pass = std::fabs(rx - 11.0f) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp((2,4,6),(8,10,12),f=1.5,clamp=false).x=%.5f want=11.00000 (extrapolate) -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  // CASE 5: T3 defaults — Lerp((0,0,0),(0,0,0), F=0, Clamp=false) → (0,0,0).
  {
    float rx = evalLerp(0,0,0, 0,0,0, 0.0f, 0.0f, "Result.x");
    bool pass = std::fabs(rx) < eps;
    ok = ok && pass;
    printf("[selftest-lerpvec3] Lerp(t3 defaults).x=%.5f want=0.00000 -> %s\n",
           rx, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
