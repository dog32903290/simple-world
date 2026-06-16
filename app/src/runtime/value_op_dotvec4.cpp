// DotVec4 value op (value-op self-registration seam leaf).
// TiXL authority: Operators/Lib/numbers/vec4/DotVec4.cs (+ DotVec4.t3 defaults).
//
//   DotVec4.cs Update():
//     Result.Value = Vector4.Dot(Input1.GetValue(context), Input2.GetValue(context));
//
//   C# Vector4.Dot(a, b) = a.X*b.X + a.Y*b.Y + a.Z*b.Z + a.W*b.W  (4-component dot → 1 scalar).
//   DotVec4.t3 DefaultValues: Input1 = {X:0,Y:0,Z:0,W:0}, Input2 = {X:0,Y:0,Z:0,W:0}.
//   (Default eval: dot((0,0,0,0),(0,0,0,0)) = 0.)
//
// 2 Vector4 inputs (= 8 Float ports) → 1 scalar Float output. Pure stateless value op: behaviour
// is entirely the evaluate fn, registered via the ValueOp seam (no GPU cook). Mirrors the already
// shipped DotVec3 (value_eval_ops.cpp:279 / node_registry_math.cpp:546) one component wider.
//
// NAMED FORK — fork-dotvec4-vec4-as-8-floats (mirror of DotVec3's vec3-as-6-floats convention):
//   This runtime has only Float ports — no native Vector4 type. So each Vector4 input is decomposed
//   into 4 consecutive Float ports (head Widget::Vec, vecArity=4; ids "<base>.x/.y/.z/.w"), exactly
//   as DotVec3 splits each Vector3 into 3. The vector is "N scalars wearing one widget" (graph.h:22)
//   — the Inspector draws the 4 components as ONE DragFloat4 row. NOT an eval/golden fork: the dot
//   product is byte-identical to TiXL; only the host data model differs (8 Float wires vs 2 Vec4).
//   in[] = [Input1.x, Input1.y, Input1.z, Input1.w, Input2.x, Input2.y, Input2.z, Input2.w].
//
//   Widget arity note (COSMETIC ONLY, does NOT affect eval/golden): the Inspector + node-registry
//   Vec drawing clamp N = vecArity > 4 ? 4 : vecArity (inspector.cpp:124 / node_registry.cpp:117),
//   so vecArity=4 draws all four components with no fallback gap. Precedent: StarGlowStreaks Color
//   ports already use a Widget::Vec head with vecArity=4 (point_ops_starglowstreaks.cpp).
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"            // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"   // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runDotVec4SelfTest(bool injectBug);

namespace {

// in[] = [Input1.x, .y, .z, .w, Input2.x, .y, .z, .w]  (the 8 Float ports in spec order).
// Result = dot(Input1, Input2) = sum of componentwise products (TiXL Vector4.Dot verbatim).
float evalDotVec4(int /*outIdx*/, const float* in, int n, const EvaluationContext&) {
  if (n < 8) return 0.0f;
  return in[0] * in[4] + in[1] * in[5] + in[2] * in[6] + in[3] * in[7];
}

}  // namespace

// Self-registration. File-scope static ValueOp feeds valueOpSpecSink() + valueOpSelfTests() during
// pre-main dynamic init. No shared file edited (mathSpecs/kTable untouched) — independent leaf.
static const ValueOp _reg_dotvec4{
    // DotVec4 (TiXL Lib.numbers.vec4.DotVec4): dot(Input1, Input2), each Vec4 = 4 Float ports.
    // Port order MUST match evalDotVec4's in[] read: Input1.x/.y/.z/.w then Input2.x/.y/.z/.w, out.
    // Defaults from DotVec4.t3: all 8 components 0. PortSpec field order (graph.h:27):
    //   id, name, dataType, isInput, def, minV, maxV, widget, labels, pinless, vecArity, multiInput.
    {"DotVec4", "DotVec4",
     {{"Input1.x", "Input1",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Input1.y", "Input1.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input1.z", "Input1.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input1.w", "Input1.w", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.x", "Input2",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Input2.y", "Input2.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.z", "Input2.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Input2.w", "Input2.w", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Result",   "Result",   "Float", false}},
     evalDotVec4},
    "dotvec4", runDotVec4SelfTest};

// --- DotVec4 MATH golden ------------------------------------------------------------------------
// Builds a 1-node DotVec4 graph, sets the 8 component params, pulls "Result" via evalFloat (flat
// path — DotVec4 has no multiInput, so the single-slot-per-port evalFloat is correct here, exactly
// like value_op_sin.cpp). Compares to the hand-derived TiXL Vector4.Dot. injectBug asserts a WRONG
// value (W term dropped) so the typical assertion flips RED, proving the tooth bites.
int runDotVec4SelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate DotVec4 with explicit 8 components.
  auto evalDot = [&](float ax, float ay, float az, float aw,
                     float bx, float by, float bz, float bw) -> float {
    const NodeSpec* spec = findSpec("DotVec4");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "DotVec4";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Input1.x"] = ax; g.node(nid)->params["Input1.y"] = ay;
    g.node(nid)->params["Input1.z"] = az; g.node(nid)->params["Input1.w"] = aw;
    g.node(nid)->params["Input2.x"] = bx; g.node(nid)->params["Input2.y"] = by;
    g.node(nid)->params["Input2.z"] = bz; g.node(nid)->params["Input2.w"] = bw;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == "Result") { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: dot((2,3,4,5),(1,2,3,4)) = 2 + 6 + 12 + 20 = 40.
  //   injectBug asserts the W-term-dropped value (2+6+12 = 20) → flips RED, proving all 4 terms
  //   (especially the .w term that DotVec3 lacks) are actually summed.
  {
    float r = evalDot(2.0f, 3.0f, 4.0f, 5.0f, 1.0f, 2.0f, 3.0f, 4.0f);
    float want = injectBug ? 20.0f : 40.0f;  // bug: drop the W product (5*4=20)
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec4] Dot((2,3,4,5),(1,2,3,4))=%.5f want=%.5f -> %s\n",
           r, want, pass ? "PASS" : "FAIL");
  }

  // PERPENDICULAR: dot((1,0,0,0),(0,1,0,0)) = 0.
  {
    float r = evalDot(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec4] Dot((1,0,0,0),(0,1,0,0))=%.5f want=0.00000 (perp) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // W-AXIS ONLY: dot((0,0,0,3),(0,0,0,2)) = 6. Isolates the W term (the new component vs DotVec3).
  {
    float r = evalDot(0.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 2.0f);
    bool pass = std::fabs(r - 6.0f) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec4] Dot((0,0,0,3),(0,0,0,2))=%.5f want=6.00000 (W-axis) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // NEGATIVE components: dot((-1,2,-3,4),(5,-6,7,-8)) = -5 -12 -21 -32 = -70.
  {
    float r = evalDot(-1.0f, 2.0f, -3.0f, 4.0f, 5.0f, -6.0f, 7.0f, -8.0f);
    bool pass = std::fabs(r - (-70.0f)) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec4] Dot((-1,2,-3,4),(5,-6,7,-8))=%.5f want=-70.00000 -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY (DotVec4.t3 defaults): dot((0,0,0,0),(0,0,0,0)) = 0.
  {
    float r = evalDot(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    bool pass = std::fabs(r) < eps;
    ok = ok && pass;
    printf("[selftest-dotvec4] Dot(0,0)=%.5f want=0.00000 (t3 defaults) -> %s\n",
           r, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
