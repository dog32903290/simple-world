// Vector4Components value op (value-op self-registration seam leaf — MULTI-OUTPUT).
// TiXL authority: Operators/Lib/numbers/vec4/Vector4Components.cs.
//
//   Vector4Components.cs Update():
//     Vector4 value = Value.GetValue(context);
//     X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z; W.Value = value.W;
//
//   Ports: Value = InputSlot<Vector4> (the single vec4 input). Outputs: X, Y, Z, W (Slot<float>).
//   Pure stateless decompose: each output is just the corresponding component of Value.
//
// EVAL-SIDE LAYOUT (multi-output convention, mirrors Vector3Components — value_eval_ops.cpp:298 +
// node_registry_math.cpp:570): the ONE Vector4 input is decomposed into FOUR Float input ports
// (Value.x/.y/.z/.w) that occupy in[0..3]; the FOUR named output ports (X/Y/Z/W) come AFTER the
// inputs. With n = 4 input ports, an output port lives at spec index outIdx in [n, n+3], so the
// component is k = outIdx - n ∈ [0,3]. evalFloat pulls each output pin independently (flat path —
// Vector4Components is NOT a multiInput op, so no resident gather is needed; one wire per port).
//
// FORKS (named):
//   - fork-vec4-decompose-arity: TiXL's Value is a single Vector4 slot. This runtime has only scalar
//     Float ports, so the vec4 is exposed as four Float ports (Value.x/.y/.z/.w) grouped under one
//     Widget::Vec head (vecArity=4) — purely a UI/authoring affordance. The decompose is byte-
//     identical: each output reads its own component, no arithmetic. Same convention already shipped
//     for Vector3Components (vec3 → X/Y/Z via vecArity=3); this just extends the head to 4 (the W
//     component) — vecArity=4 is the only cosmetic difference vs the vec3 op.
#include "runtime/graph.h"  // NodeSpec, EvaluationContext (fwd), findSpec/evalFloat/pinId for golden

#include <cmath>
#include <cstdio>

#include "runtime/Particle.h"           // EvaluationContext full definition (for the golden ctx)
#include "runtime/value_op_registry.h"  // ValueOp self-registration

namespace sw {

// Forward decl so the file-scope ValueOp registrar can name the selftest (defined below it).
int runVector4ComponentsSelfTest(bool injectBug);

namespace {

// in[] = [Value.x, Value.y, Value.z, Value.w] (the 4 decomposed Float input ports, n=4).
// Output ports X/Y/Z/W are at spec index outIdx in [n, n+3] → component k = outIdx - n.
//   X.Value = value.X; Y.Value = value.Y; Z.Value = value.Z; W.Value = value.W  (TiXL verbatim).
float evalVector4Components(int outIdx, const float* in, int n, const EvaluationContext&) {
  if (n < 4) return 0.0f;
  int k = outIdx - n;  // output port index → component (0=X, 1=Y, 2=Z, 3=W)
  if (k < 0 || k > 3) return 0.0f;
  return in[k];
}

}  // namespace

// Self-registration. File-scope static ValueOp — independent leaf .cpp (no shared edit point;
// CMake globs value_op*.cpp). Feeds valueOpSpecSink() + valueOpSelfTests() during pre-main init.
static const ValueOp _reg_vector4components{
    // Vector4Components (TiXL Lib.numbers.vec4.Vector4Components): decompose Vec4 → X/Y/Z/W Float.
    // Port order MUST match evalVector4Components's in[] read: the 4 Value components first (grouped
    // under one Widget::Vec head, vecArity=4 — fork-vec4-decompose-arity), then the 4 named outputs.
    // Vector4Components default Value = {0,0,0,0} (Vector4 default).
    {"Vector4Components", "Vector4Components",
     {{"Value.x", "Value",   "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 4},
      {"Value.y", "Value.y", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Value.z", "Value.z", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"Value.w", "Value.w", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, false, 1},
      {"X", "X", "Float", false},
      {"Y", "Y", "Float", false},
      {"Z", "Z", "Float", false},
      {"W", "W", "Float", false}},
     evalVector4Components},
    "vector4components", runVector4ComponentsSelfTest};

// --- Vector4Components MATH golden (flat path — pulls each of the 4 output pins independently) ----
// Builds a 1-node Vector4Components graph, sets the 4 component params, pulls each output pin
// (X/Y/Z/W) via evalFloat, and compares to the input components (decompose is identity per channel).
// The 4 components are deliberately ALL DISTINCT so a mis-wired output (e.g. X reading W, or k off
// by one) cannot coincidentally pass. injectBug swaps the X expectation to the W value so the
// typical-case assertion flips RED, proving the tooth bites and the per-component routing is real.
int runVector4ComponentsSelfTest(bool injectBug) {
  const float eps = 1e-5f;
  bool ok = true;

  // Helper: evaluate Vector4Components with explicit components, pulling a named output port.
  // Returns the value at output port `outName` (one of X/Y/Z/W).
  auto evalComp = [&](float vx, float vy, float vz, float vw, const char* outName) -> float {
    const NodeSpec* spec = findSpec("Vector4Components");
    if (!spec) return -999.0f;
    Graph g;
    Node nd; nd.id = g.nextId++; nd.type = "Vector4Components";
    for (const auto& p : spec->ports)
      if (p.isInput && p.dataType == "Float") nd.params[p.id] = p.def;
    g.nodes.push_back(nd);
    int nid = g.nodes.back().id;
    g.node(nid)->params["Value.x"] = vx;
    g.node(nid)->params["Value.y"] = vy;
    g.node(nid)->params["Value.z"] = vz;
    g.node(nid)->params["Value.w"] = vw;
    int outIdx = -1;
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].id == outName) { outIdx = (int)i; break; }
    EvaluationContext ctx{}; ctx.time = 0.0f;
    return outIdx < 0 ? -997.0f : evalFloat(g, pinId(nid, outIdx), ctx, 0);
  };

  // TYPICAL: Value = (1.5, -2.5, 3.5, -4.5) → X=1.5, Y=-2.5, Z=3.5, W=-4.5 (per-channel identity).
  // injectBug asserts X == W (-4.5) — the "all outputs read the last component" / k-collapse failure
  // mode — so the typical X assertion flips RED.
  const float vx = 1.5f, vy = -2.5f, vz = 3.5f, vw = -4.5f;
  {
    float r = evalComp(vx, vy, vz, vw, "X");
    float want = injectBug ? vw : vx;  // bug: X reads W
    bool pass = std::fabs(r - want) < eps;
    ok = ok && pass;
    printf("[selftest-vector4components] X(%.1f,%.1f,%.1f,%.1f)=%.4f want=%.4f -> %s\n",
           vx, vy, vz, vw, r, want, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalComp(vx, vy, vz, vw, "Y");
    bool pass = std::fabs(r - vy) < eps;
    ok = ok && pass;
    printf("[selftest-vector4components] Y=%.4f want=%.4f -> %s\n", r, vy, pass ? "PASS" : "FAIL");
  }
  {
    float r = evalComp(vx, vy, vz, vw, "Z");
    bool pass = std::fabs(r - vz) < eps;
    ok = ok && pass;
    printf("[selftest-vector4components] Z=%.4f want=%.4f -> %s\n", r, vz, pass ? "PASS" : "FAIL");
  }
  // W is the component Vector3Components lacks — the load-bearing reason this is a distinct op.
  {
    float r = evalComp(vx, vy, vz, vw, "W");
    bool pass = std::fabs(r - vw) < eps;
    ok = ok && pass;
    printf("[selftest-vector4components] W=%.4f want=%.4f -> %s\n", r, vw, pass ? "PASS" : "FAIL");
  }

  // BOUNDARY: default Value = (0,0,0,0) → every output 0 (Vector4Components.t3 default decompose).
  {
    float rx = evalComp(0.0f, 0.0f, 0.0f, 0.0f, "X");
    float rw = evalComp(0.0f, 0.0f, 0.0f, 0.0f, "W");
    bool pass = std::fabs(rx) < eps && std::fabs(rw) < eps;
    ok = ok && pass;
    printf("[selftest-vector4components] default(0,0,0,0) X=%.4f W=%.4f want=0 -> %s\n",
           rx, rw, pass ? "PASS" : "FAIL");
  }

  return ok ? 0 : 1;
}

}  // namespace sw
