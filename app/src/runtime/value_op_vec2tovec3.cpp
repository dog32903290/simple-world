// Vec2ToVec3 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/Vec2ToVec3.cs (+ Vec2ToVec3.t3 defaults).
//
//   Vec2ToVec3.cs Update():  (lines 16-18)
//     var a = XY.GetValue(context);
//     var z = Z.GetValue(context);
//     Result.Value = new Vector3(a.X, a.Y, z);
//
//   Vec2ToVec3.t3 DefaultValues: XY={X:0,Y:0}, Z=0.0.
//   Port declaration order in .cs lines 22-26: XY (a71e7512), Z (F1E036D8).
//
// 1 Vector2 input + 1 scalar Z → 1 Vector3 output.
// Ports: XY.x, XY.y, Z → Result.x, Result.y, Result.z.
//
// EVAL-SIDE LAYOUT (flat, no multiInput):
//   in[] = [XY.x, XY.y, Z]  (n=3 inputs).
//   Output ports (Result.x, Result.y, Result.z) at spec indices n=3, n+1=4, n+2=5.
//   Component k = outIdx - n ∈ {0=x, 1=y, 2=z}.
//
// FORKS (named):
//   - fork-vec2tovec3-vec2-as-2-floats (fork-vec4-decompose-arity precedent): Vector2 input
//     exposed as 2 consecutive Float ports (Widget::Vec, vecArity=2). Vector3 output exposed
//     as 3 Float ports. NOT an eval fork.
//   - fork-vec2tovec3-vec3-as-3-floats: Vector3 output exposed as 3 Float ports (Widget::Vec,
//     vecArity=3), matching the vec3 output convention already used by BlendVector3/PickVector3.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

int runVec2ToVec3SelfTest(bool injectBug);

namespace {

// in[] = [XY.x, XY.y, Z]  (n=3).
// Result = new Vector3(XY.X, XY.Y, Z)  (TiXL Vec2ToVec3.cs line 18 verbatim).
// Component k = outIdx - n: 0=x (XY.X), 1=y (XY.Y), 2=z (Z).
float evalVec2ToVec3(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 3) return 0.0f;
  const int k = outIdx - n;
  if (k == 0) return in[0];  // a.X
  if (k == 1) return in[1];  // a.Y
  if (k == 2) return in[2];  // z
  return 0.0f;
}

}  // namespace

static const ValueOp _reg_vec2tovec3{
    // Vec2ToVec3 (TiXL Lib.numbers.vec2.Vec2ToVec3):
    //   Result.Value = new Vector3(XY.X, XY.Y, Z).
    // Port order MUST match evalVec2ToVec3's in[] read:
    //   XY.x, XY.y, Z (inputs); then Result.x, Result.y, Result.z (outputs).
    // Port declaration order matches .cs: XY (line 22), Z (line 25).
    // Defaults from Vec2ToVec3.t3: XY={X:0,Y:0}, Z=0.0.
    // fork-vec2tovec3-vec2-as-2-floats: Vector2 input exposed as 2 Float ports, Widget::Vec.
    // fork-vec2tovec3-vec3-as-3-floats: Vector3 output exposed as 3 Float ports.
    {"Vec2ToVec3", "Vec2ToVec3",
     {{"XY.x",     "XY",       "Float", true,  0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 2},
      {"XY.y",     "XY.y",     "Float", true,  0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Z",        "Z",        "Float", true,  0.0f, -100.0f, 100.0f, Widget::Slider},
      {"Result.x", "Result.x", "Float", false},
      {"Result.y", "Result.y", "Float", false},
      {"Result.z", "Result.z", "Float", false}},
     evalVec2ToVec3},
    "vec2tovec3", runVec2ToVec3SelfTest};

// --- Vec2ToVec3 MATH golden --------------------------------------------------------------------
// Builds a 1-node Vec2ToVec3 graph, sets XY + Z, pulls each of the 3 output pins via evalFloat.
// injectBug asserts a wrong Result.z (asserts Y instead of Z value) → flips RED.
int runVec2ToVec3SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  auto evalV23 = [&](float x, float y, float z, const char* outName) -> float {
    const NodeSpec* spec = findSpec("Vec2ToVec3");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Vec2ToVec3";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["XY.x"] = x;
    g.node(nid)->params["XY.y"] = y;
    g.node(nid)->params["Z"]    = z;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: XY=(1,2), Z=3 → Result=(1,2,3).
  // injectBug asserts Result.z==2 (uses XY.y) → flips RED, proving Z is distinct from XY.y.
  {
    float rx = evalV23(1.0f, 2.0f, 3.0f, "Result.x");
    bool pass = std::fabs(rx - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vec2tovec3] (1,2,3).x=%.5f want=1.00000 -> %s\n", rx, pass ? "PASS" : "FAIL");
  }
  {
    float ry = evalV23(1.0f, 2.0f, 3.0f, "Result.y");
    bool pass = std::fabs(ry - 2.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vec2tovec3] (1,2,3).y=%.5f want=2.00000 -> %s\n", ry, pass ? "PASS" : "FAIL");
  }
  {
    float rz = evalV23(1.0f, 2.0f, 3.0f, "Result.z");
    float want = injectBug ? 2.0f : 3.0f;  // bug: substitute XY.y for Z
    bool pass = std::fabs(rz - want) < eps;
    ok = ok && pass;
    printf("[selftest-vec2tovec3] (1,2,3).z=%.5f want=%.5f -> %s\n", rz, want, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): XY=(0,0), Z=0 → (0,0,0).
  {
    float rx = evalV23(0.0f, 0.0f, 0.0f, "Result.x");
    float ry = evalV23(0.0f, 0.0f, 0.0f, "Result.y");
    float rz = evalV23(0.0f, 0.0f, 0.0f, "Result.z");
    bool pass = std::fabs(rx) < eps && std::fabs(ry) < eps && std::fabs(rz) < eps;
    ok = ok && pass;
    printf("[selftest-vec2tovec3] zeros=(%.5f,%.5f,%.5f) want=(0,0,0) t3-defaults -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  // Z ISOLATE: XY=(0,0), Z=5 → (0,0,5). Proves Z maps to result.z.
  {
    float rz = evalV23(0.0f, 0.0f, 5.0f, "Result.z");
    bool pass = std::fabs(rz - 5.0f) < eps;
    ok = ok && pass;
    printf("[selftest-vec2tovec3] Z-isolate z=%.5f want=5.00000 -> %s\n",
           rz, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE: XY=(-1,-2), Z=-3 → (-1,-2,-3).
  {
    float rx = evalV23(-1.0f, -2.0f, -3.0f, "Result.x");
    float ry = evalV23(-1.0f, -2.0f, -3.0f, "Result.y");
    float rz = evalV23(-1.0f, -2.0f, -3.0f, "Result.z");
    bool pass = std::fabs(rx - (-1.0f)) < eps &&
                std::fabs(ry - (-2.0f)) < eps &&
                std::fabs(rz - (-3.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-vec2tovec3] negatives=(%.5f,%.5f,%.5f) want=(-1,-2,-3) -> %s\n",
           rx, ry, rz, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
