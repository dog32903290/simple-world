// Int2ToVector2 value op (value-op self-registration seam leaf — Phase C numbers/vec2 mining).
// TiXL authority: Operators/Lib/numbers/vec2/Int2ToVector2.cs (+ Int2ToVector2.t3 defaults).
//
//   Int2ToVector2.cs Update():
//     var s = Int2.GetValue(context);
//     Result.Value = s;   // implicit Int2 → Vector2 cast: Result.X = s.Width, Result.Y = s.Height
//
//   Int2ToVector2.t3 DefaultValues: Int2 = {X:0, Y:0}.
//   C# implicit Int2→Vector2: Vector2.X = (float)Int2.Width, Vector2.Y = (float)Int2.Height.
//
// 1 Int2 input → 1 Vector2 output. Int2 decomposed into 2 Float ports (Int2.Width, Int2.Height);
// Vector2 output exposed as 2 Float ports (Result.X, Result.Y). 2 inputs → 2 outputs.
//
// EVAL-SIDE LAYOUT (multi-output; mirrors AddInt2 convention, value_op_addint2.cpp):
//   in[] = [Int2.Width, Int2.Height]  (n=2 inputs).
//   Output ports (Result.X, Result.Y) at spec indices n and n+1.
//   Component k = outIdx - n ∈ {0=X, 1=Y}.
//
// FORKS (named):
//   - fork-int2tovec2-int-on-float-port (fork-addint2-int-on-float-port precedent, value_op_addint2.cpp):
//     TiXL Int2 stores Width/Height as int. This runtime stores all values as Float.
//     The conversion is (int)-truncation before casting to float: Result.X = (float)(int)in[0].
//     For whole-number inputs (the only values int sliders produce) this is byte-identical to
//     TiXL's implicit Int2→Vector2 cast. Named here to document the behaviour.
//   - fork-int2tovec2-vec2-as-2-floats (fork-vec4-decompose-arity precedent — Vector4Components,
//     DotVec4): TiXL Int2 and Vector2 are native types. This runtime has only Float ports, so
//     both are exposed as 2 consecutive Float ports (head Widget::Vec, vecArity=2). NOT an eval
//     fork: the component mapping is byte-identical to TiXL; only the data model differs.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest.
int runInt2ToVec2SelfTest(bool injectBug);

namespace {

// in[] = [Int2.Width, Int2.Height]  (n=2).
// Result.Value = s  (TiXL implicit Int2→Vector2).
// fork-int2tovec2-int-on-float-port: (int)-truncate each component before producing the float result.
// Component k = outIdx - n: 0=X (Width), 1=Y (Height).
float evalInt2ToVec2(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 2) return 0.0f;
  const int k = outIdx - n;
  if (k == 0) return (float)(int)in[0];  // Result.X = (float)Int2.Width
  if (k == 1) return (float)(int)in[1];  // Result.Y = (float)Int2.Height
  return 0.0f;
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests()
// during pre-main dynamic init. No shared file edited — independent leaf.
static const ValueOp _reg_int2tovec2{
    // Int2ToVector2 (TiXL Lib.numbers.vec2.Int2ToVector2):
    //   Result.Value = Int2.GetValue(context)  (implicit Int2→Vector2 cast).
    // Port order MUST match evalInt2ToVec2's in[] read:
    //   Int2.Width, Int2.Height (inputs); then Result.X, Result.Y (outputs).
    // Defaults from Int2ToVector2.t3: Int2 = {X:0, Y:0} → Width=0, Height=0.
    // fork-int2tovec2-vec2-as-2-floats: both Int2 and Vector2 exposed as 2 Float ports, Widget::Vec.
    {"Int2ToVector2", "Int2ToVector2",
     {{"Int2.Width",  "Int2",        "Float", true, 0.0f, -16384.0f, 16384.0f, Widget::Vec, {}, false, 2},
      {"Int2.Height", "Int2.Height", "Float", true, 0.0f, -16384.0f, 16384.0f, Widget::Vec, {}, false, 1},
      {"Result.x",   "Result.x",    "Float", false},
      {"Result.y",   "Result.y",    "Float", false}},
     evalInt2ToVec2},
    "int2tovec2", runInt2ToVec2SelfTest};

// --- Int2ToVector2 MATH golden ------------------------------------------------------------------
// Builds a 1-node Int2ToVector2 graph, sets the 2 Int2 component params, pulls each output pin
// (Result.x / Result.y) via evalFloat. Compares to hand-computed Int2→Vec2 cast.
// injectBug asserts a wrong expected X so the typical assertion flips RED.
int runInt2ToVec2SelfTest(bool injectBug) {
  const float eps = 1e-4f;
  bool ok = true;

  // Helper: evaluate Int2ToVector2 with explicit width/height, pulling a named output port.
  auto evalI2V2 = [&](float w, float h, const char* outName) -> float {
    const NodeSpec* spec = findSpec("Int2ToVector2");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Int2ToVector2";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Int2.Width"]  = w;
    g.node(nid)->params["Int2.Height"] = h;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Int2(1920, 1080) → Result.x=1920.0, Result.y=1080.0.
  // injectBug asserts Result.x==0 (wrong) so the X assertion flips RED.
  {
    float rX = evalI2V2(1920.0f, 1080.0f, "Result.x");
    float wantX = injectBug ? 0.0f : 1920.0f;
    bool pass = std::fabs(rX - wantX) < eps;
    ok = ok && pass;
    printf("[selftest-int2tovec2] (1920,1080).x=%.0f want=%.0f -> %s\n",
           rX, wantX, pass ? "PASS" : "FAIL");
  }
  {
    float rY = evalI2V2(1920.0f, 1080.0f, "Result.y");
    bool pass = std::fabs(rY - 1080.0f) < eps;
    ok = ok && pass;
    printf("[selftest-int2tovec2] (1920,1080).y=%.0f want=1080 -> %s\n",
           rY, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE: Int2(-100, -200) → Result.x=-100.0, Result.y=-200.0.
  {
    float rX = evalI2V2(-100.0f, -200.0f, "Result.x");
    float rY = evalI2V2(-100.0f, -200.0f, "Result.y");
    bool pass = std::fabs(rX - (-100.0f)) < eps && std::fabs(rY - (-200.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-int2tovec2] (-100,-200)=(%.0f,%.0f) want=(-100,-200) -> %s\n",
           rX, rY, pass ? "PASS" : "FAIL");
  }

  // ZERO (t3 defaults): Int2(0,0) → Result.x=0, Result.y=0.
  {
    float rX = evalI2V2(0.0f, 0.0f, "Result.x");
    float rY = evalI2V2(0.0f, 0.0f, "Result.y");
    bool pass = std::fabs(rX) < eps && std::fabs(rY) < eps;
    ok = ok && pass;
    printf("[selftest-int2tovec2] (0,0)=(%.0f,%.0f) want=(0,0) t3-default -> %s\n",
           rX, rY, pass ? "PASS" : "FAIL");
  }

  // TRUNCATION (fork-int2tovec2-int-on-float-port): Int2(3.9, 1.7) → (3.0, 1.0).
  // TiXL stores Width/Height as int → truncates toward zero.
  {
    float rX = evalI2V2(3.9f, 1.7f, "Result.x");
    float rY = evalI2V2(3.9f, 1.7f, "Result.y");
    bool pass = std::fabs(rX - 3.0f) < eps && std::fabs(rY - 1.0f) < eps;
    ok = ok && pass;
    printf("[selftest-int2tovec2] trunc(3.9,1.7)=(%.0f,%.0f) want=(3,1) int-truncate -> %s\n",
           rX, rY, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
